#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <processthreadsapi.h>
#include <conio.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#define LOG_FILE "program.log"
#define LEADER_LOCK_FILE "leader.lock"

#ifdef _WIN32
#define SHM_NAME "Local\\CrossPlatformCounter"
#define MUTEX_COUNTER_NAME "Local\\CounterMutex"
#define MUTEX_LOG_NAME "Local\\LogMutex"
#define MUTEX_LEADER_NAME "Local\\LeaderMutex"
#else
#define SHM_NAME "/cross_platform_counter_shm"
#endif

typedef struct {
    long counter;
    int leader_pid;
    bool child1_running;
    bool child2_running;
    int child1_pid;
    int child2_pid;
} SharedArea;

#ifdef _WIN32
HANDLE hMap = NULL;
HANDLE hCounterMutex = NULL, hLogMutex = NULL, hLeaderMutex = NULL;
SharedArea* g_area = NULL;
#else
int shm_fd = -1;
pthread_mutex_t *counter_mutex, *log_mutex, *leader_mutex;
SharedArea* g_area = NULL;
int leader_lock_fd = -1;
#endif

volatile bool g_running = true;
bool g_is_leader = false;
int g_my_pid = 0;

#ifndef _WIN32
void sleep_ms(long ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
    }
}
#endif

// --- LOCALE ---
void setup_locale() {
#ifdef _WIN32
    setlocale(LC_ALL, "Russian");
#else
    setlocale(LC_ALL, "ru_RU.UTF-8");
#endif
}

// --- TIME ---
void get_time_str(char* buf, size_t sz) {
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buf, sz, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
    struct timespec ts;
    struct tm tm_info;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_info);
    snprintf(buf, sz, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
        tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
        tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, ts.tv_nsec/1000000);
#endif
}

// --- LOG ---
void log_msg(const char *fmt, ...) {
    char tbuf[64];
    get_time_str(tbuf, sizeof(tbuf));
    char msgbuf[512];
    char linebuf[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);

    int len = snprintf(linebuf, sizeof(linebuf), "[%s] [PID:%d] %s\n", tbuf, g_my_pid, msgbuf);
    if (len <= 0) {
        return;
    }
    if (len >= (int)sizeof(linebuf)) {
        len = (int)sizeof(linebuf) - 1;
    }
#ifdef _WIN32
    DWORD res = WaitForSingleObject(hLogMutex, INFINITE);
    if (res != WAIT_OBJECT_0 && res != WAIT_ABANDONED) {
        return;
    }
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        fputs(linebuf, f);
        fclose(f);
    }
    ReleaseMutex(hLogMutex);
#else
    int fd = open(LOG_FILE, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (fd < 0) {
        return;
    }
    flock(fd, LOCK_EX);
    write(fd, linebuf, (size_t)len);
    flock(fd, LOCK_UN);
    close(fd);
#endif
}

bool is_pid_alive(int pid) {
#ifdef _WIN32
    if (pid <= 0) {
        return false;
    }
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) {
        return false;
    }
    DWORD res = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return (res == WAIT_TIMEOUT);
#else
    if (pid <= 0) {
        return false;
    }
    if (kill(pid, 0) == 0) {
        return true;
    }
    return (errno == EPERM);
#endif
}

// --- TIMER/THREAD ---
#ifdef _WIN32
DWORD WINAPI timer_thread(LPVOID) {
#else
void* timer_thread(void*) {
#endif
    while (g_running) {
#ifdef _WIN32
        Sleep(300);
        WaitForSingleObject(hCounterMutex, INFINITE);
        g_area->counter++;
        ReleaseMutex(hCounterMutex);
#else
        sleep_ms(300);
        pthread_mutex_lock(counter_mutex);
        g_area->counter++;
        pthread_mutex_unlock(counter_mutex);
#endif
    }
    return 0;
}

#ifdef _WIN32
DWORD WINAPI log_thread(LPVOID) {
#else
void* log_thread(void*) {
#endif
    while (g_running) {
#ifdef _WIN32
        Sleep(1000);
        WaitForSingleObject(hCounterMutex, INFINITE);
        long cnt = g_area->counter;
        ReleaseMutex(hCounterMutex);
#else
        sleep_ms(1000);
        pthread_mutex_lock(counter_mutex);
        long cnt = g_area->counter;
        pthread_mutex_unlock(counter_mutex);
#endif
        log_msg("Текущее значение счётчика: %ld", cnt);
    }
    return 0;
}

#ifdef _WIN32
DWORD WINAPI spawner_thread(LPVOID arg) {
    char** argv = (char**)arg;
#else
void* spawner_thread(void* arg) {
    char **argv = (char**)arg;
#endif
    while (g_running) {
#ifdef _WIN32
        Sleep(3000);
        WaitForSingleObject(hCounterMutex, INFINITE);
        bool busy1 = g_area->child1_running;
        int pid1 = g_area->child1_pid;
        if (busy1 && !is_pid_alive(pid1)) {
            g_area->child1_running = false;
            g_area->child1_pid = 0;
            busy1 = false;
        }
        if (!busy1) {
            g_area->child1_running = true;
            g_area->child1_pid = 0;
            ReleaseMutex(hCounterMutex);
            // Spawn
            STARTUPINFO si = {sizeof(si)};
            PROCESS_INFORMATION pi;
            char cmdline[256];
            snprintf(cmdline, sizeof(cmdline), "%s child 1", argv[0]);
            BOOL ok = CreateProcess(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
            if (ok) {
                WaitForSingleObject(hCounterMutex, INFINITE);
                g_area->child1_pid = (int)pi.dwProcessId;
                ReleaseMutex(hCounterMutex);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                WaitForSingleObject(hCounterMutex, INFINITE);
                g_area->child1_running = false;
                g_area->child1_pid = 0;
                ReleaseMutex(hCounterMutex);
                log_msg("Child1 не запустился");
            }
        } else {
            ReleaseMutex(hCounterMutex);
            log_msg("Child1 всё ещё выполняется, запуск пропущен");
        }

        WaitForSingleObject(hCounterMutex, INFINITE);
        bool busy2 = g_area->child2_running;
        int pid2 = g_area->child2_pid;
        if (busy2 && !is_pid_alive(pid2)) {
            g_area->child2_running = false;
            g_area->child2_pid = 0;
            busy2 = false;
        }
        if (!busy2) {
            g_area->child2_running = true;
            g_area->child2_pid = 0;
            ReleaseMutex(hCounterMutex);
            // Spawn
            STARTUPINFO si = {sizeof(si)};
            PROCESS_INFORMATION pi;
            char cmdline[256];
            snprintf(cmdline, sizeof(cmdline), "%s child 2", argv[0]);
            BOOL ok = CreateProcess(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
            if (ok) {
                WaitForSingleObject(hCounterMutex, INFINITE);
                g_area->child2_pid = (int)pi.dwProcessId;
                ReleaseMutex(hCounterMutex);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                WaitForSingleObject(hCounterMutex, INFINITE);
                g_area->child2_running = false;
                g_area->child2_pid = 0;
                ReleaseMutex(hCounterMutex);
                log_msg("Child2 не запустился");
            }
        } else {
            ReleaseMutex(hCounterMutex);
            log_msg("Child2 всё ещё выполняется, запуск пропущен");
        }
#else
        sleep_ms(3000);
        pthread_mutex_lock(counter_mutex);
        bool busy1 = g_area->child1_running;
        int pid1 = g_area->child1_pid;
        if (busy1 && !is_pid_alive(pid1)) {
            g_area->child1_running = false;
            g_area->child1_pid = 0;
            busy1 = false;
        }
        if (!busy1) {
            g_area->child1_running = true;
            g_area->child1_pid = 0;
            pthread_mutex_unlock(counter_mutex);
            // Spawn
            pid_t pid = fork();
            if (pid == 0) {
                execl(argv[0], argv[0], "child", "1", NULL);
                exit(1);
            } else if (pid > 0) {
                pthread_mutex_lock(counter_mutex);
                g_area->child1_pid = (int)pid;
                pthread_mutex_unlock(counter_mutex);
            } else if (pid < 0) {
                pthread_mutex_lock(counter_mutex);
                g_area->child1_running = false;
                g_area->child1_pid = 0;
                pthread_mutex_unlock(counter_mutex);
                log_msg("Child1 не запустился");
            }
        } else {
            pthread_mutex_unlock(counter_mutex);
            log_msg("Child1 всё ещё выполняется, запуск пропущен");
        }
        pthread_mutex_lock(counter_mutex);
        bool busy2 = g_area->child2_running;
        int pid2 = g_area->child2_pid;
        if (busy2 && !is_pid_alive(pid2)) {
            g_area->child2_running = false;
            g_area->child2_pid = 0;
            busy2 = false;
        }
        if (!busy2) {
            g_area->child2_running = true;
            g_area->child2_pid = 0;
            pthread_mutex_unlock(counter_mutex);
            // Spawn
            pid_t pid = fork();
            if (pid == 0) {
                execl(argv[0], argv[0], "child", "2", NULL);
                exit(1);
            } else if (pid > 0) {
                pthread_mutex_lock(counter_mutex);
                g_area->child2_pid = (int)pid;
                pthread_mutex_unlock(counter_mutex);
            } else if (pid < 0) {
                pthread_mutex_lock(counter_mutex);
                g_area->child2_running = false;
                g_area->child2_pid = 0;
                pthread_mutex_unlock(counter_mutex);
                log_msg("Child2 не запустился");
            }
        } else {
            pthread_mutex_unlock(counter_mutex);
            log_msg("Child2 всё ещё выполняется, запуск пропущен");
        }
#endif
    }
    return 0;
}

void run_child(int child_no) {
    log_msg("Дочерний процесс %d запущен", child_no);
    if (child_no == 1) {
#ifdef _WIN32
        WaitForSingleObject(hCounterMutex, INFINITE);
#else
        pthread_mutex_lock(counter_mutex);
#endif
        g_area->counter += 10;
        long cnt = g_area->counter;
        g_area->child1_running = false;
        g_area->child1_pid = 0;
#ifdef _WIN32
        ReleaseMutex(hCounterMutex);
#else
        pthread_mutex_unlock(counter_mutex);
#endif
        log_msg("Операция Child1: +10, счётчик теперь %ld", cnt);
    } else if (child_no == 2) {
#ifdef _WIN32
        WaitForSingleObject(hCounterMutex, INFINITE);
#else
        pthread_mutex_lock(counter_mutex);
#endif
        g_area->counter *= 2;
        long cnt1 = g_area->counter;
#ifdef _WIN32
        ReleaseMutex(hCounterMutex);
#else
        pthread_mutex_unlock(counter_mutex);
#endif
        log_msg("Операция Child2: ×2, счётчик теперь %ld", cnt1);

#ifdef _WIN32
        Sleep(2000);
        WaitForSingleObject(hCounterMutex, INFINITE);
#else
        sleep_ms(2000);
        pthread_mutex_lock(counter_mutex);
#endif
        g_area->counter /= 2;
        long cnt2 = g_area->counter;
        g_area->child2_running = false;
        g_area->child2_pid = 0;
#ifdef _WIN32
        ReleaseMutex(hCounterMutex);
#else
        pthread_mutex_unlock(counter_mutex);
#endif
        log_msg("Операция Child2: ÷2, счётчик теперь %ld", cnt2);
    }
    log_msg("Дочерний процесс %d завершён", child_no);
}

#ifdef _WIN32
BOOL WINAPI win_exit_handler(DWORD sig) { g_running = false; return TRUE; }
#else
void handle_exit(int sig) { g_running = false; }
#endif

// --- SHARED MEMORY ---
void init_shm() {
#ifdef _WIN32
    hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedArea), SHM_NAME);
    if (!hMap) { 
        DWORD err = GetLastError();
        fprintf(stderr, "CreateFileMapping error: %lu\n", err); 
        exit(1); 
    }
    bool is_new = (GetLastError() != ERROR_ALREADY_EXISTS);
    g_area = (SharedArea*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedArea));
    if (!g_area) { fprintf(stderr, "MapViewOfFile error\n"); exit(1); }

    hCounterMutex = CreateMutexA(NULL, FALSE, MUTEX_COUNTER_NAME);
    hLogMutex     = CreateMutexA(NULL, FALSE, MUTEX_LOG_NAME);
    hLeaderMutex  = CreateMutexA(NULL, FALSE, MUTEX_LEADER_NAME);

    if (is_new) { memset(g_area, 0, sizeof(SharedArea)); }
#else
    shm_fd = shm_open(SHM_NAME, O_RDWR|O_CREAT, 0666);
    if (shm_fd < 0) { perror("shm_open"); exit(1); }
    struct stat st; fstat(shm_fd, &st);
    size_t required_size = sizeof(SharedArea) + 3 * sizeof(pthread_mutex_t);
    bool newly_created = (st.st_size == 0) || ((size_t)st.st_size < required_size);
    if ((size_t)st.st_size < required_size) {
        ftruncate(shm_fd, (off_t)required_size);
    }
    void* area = mmap(NULL, required_size, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (area == MAP_FAILED) { perror("mmap"); exit(1); }
    g_area = (SharedArea*)area;
    counter_mutex = (pthread_mutex_t*)((char*)area+sizeof(SharedArea));
    log_mutex     = (pthread_mutex_t*)((char*)area+sizeof(SharedArea)+sizeof(pthread_mutex_t));
    leader_mutex  = (pthread_mutex_t*)((char*)area+sizeof(SharedArea)+2*sizeof(pthread_mutex_t));
    if (newly_created) {
        pthread_mutexattr_t ma;
        pthread_mutexattr_init(&ma);
        pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(counter_mutex, &ma);
        pthread_mutex_init(log_mutex, &ma);
        pthread_mutex_init(leader_mutex, &ma);
        pthread_mutexattr_destroy(&ma);
        memset(g_area, 0, sizeof(SharedArea));
    }
#endif
}

// --- LEADER CAPTURE ---
void become_leader() {
#ifdef _WIN32
    DWORD res = WaitForSingleObject(hLeaderMutex, 0);
    if (res == WAIT_OBJECT_0 || res == WAIT_ABANDONED) {
        g_is_leader = true;
        g_area->leader_pid = g_my_pid;
    } else {
        g_is_leader = false;
    }
#else
    leader_lock_fd = open(LEADER_LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (leader_lock_fd < 0) {
        g_is_leader = false;
        return;
    }
    if (flock(leader_lock_fd, LOCK_EX | LOCK_NB) == 0) {
        g_is_leader = true;
        g_area->leader_pid = g_my_pid;
    } else {
        close(leader_lock_fd);
        leader_lock_fd = -1;
        g_is_leader = false;
    }
#endif
}

void input_loop() {
    printf("Счётчик запущен. Доступны команды:\n  set <число>\n  quit\n");
    char buf[64];
    while (g_running && fgets(buf, sizeof(buf), stdin)) {
        if (strncmp(buf, "set ", 4) == 0) {
            long v = atol(buf + 4);
#ifdef _WIN32
            WaitForSingleObject(hCounterMutex, INFINITE);
            g_area->counter = v;
            ReleaseMutex(hCounterMutex);
#else
            pthread_mutex_lock(counter_mutex);
            g_area->counter = v;
            pthread_mutex_unlock(counter_mutex);
#endif
            log_msg("Пользователь задал значение счётчика: %ld", v);
            printf("Счётчик установлен в %ld\n", v);
        }
        if (strncmp(buf, "quit", 4) == 0) { g_running = false; }
    }
    g_running = false;
}

void release_leader() {
#ifdef _WIN32
    ReleaseMutex(hLeaderMutex);
#else
    if (leader_lock_fd >= 0) {
        flock(leader_lock_fd, LOCK_UN);
        close(leader_lock_fd);
        leader_lock_fd = -1;
    }
#endif
}

int main(int argc, char **argv) {
    setup_locale();
#ifdef _WIN32
    g_my_pid = GetCurrentProcessId();
    SetConsoleCtrlHandler(win_exit_handler, TRUE);
#else
    g_my_pid = getpid();
    signal(SIGINT, handle_exit);
    signal(SIGTERM, handle_exit);
#endif
    init_shm();

    bool is_child = (argc > 1 && strcmp(argv[1], "child") == 0);
    if (is_child) {
        int child_no = (argc > 2) ? atoi(argv[2]) : 0;
        run_child(child_no);
        return 0;
    }
    bool is_follower = (argc > 1 && strcmp(argv[1], "follower") == 0);
    bool allow_input = !is_follower;

    if (!is_follower) {
        become_leader();
    }

    log_msg("Программа запущена (%s)", g_is_leader ? "Лидер" : "Подчинённый");

#ifdef _WIN32
    HANDLE t1 = CreateThread(NULL, 0, timer_thread, NULL, 0, NULL);
    HANDLE t2 = NULL;
    HANDLE t3 = NULL;
#else
    pthread_t t1; pthread_create(&t1, NULL, timer_thread, NULL);
    pthread_t t2, t3;
#endif

    if (g_is_leader) {
#ifdef _WIN32
        t2 = CreateThread(NULL, 0, log_thread, NULL, 0, NULL);
        t3 = CreateThread(NULL, 0, spawner_thread, argv, 0, NULL);
#else
        pthread_create(&t2, NULL, log_thread, NULL);
        pthread_create(&t3, NULL, spawner_thread, argv);
#endif
    }

    if (allow_input) {
        input_loop();
    } else {
        while (g_running) {
#ifdef _WIN32
            Sleep(1000);
#else
            sleep_ms(1000);
#endif
        }
    }

    if (g_is_leader) {
#ifdef _WIN32
        WaitForSingleObject(t1, INFINITE);
        WaitForSingleObject(t2, INFINITE);
        WaitForSingleObject(t3, INFINITE);
#else
        pthread_join(t1, NULL); pthread_join(t2, NULL); pthread_join(t3, NULL);
#endif
        log_msg("Программа завершена");
        release_leader();
    } else {
#ifdef _WIN32
        WaitForSingleObject(t1, INFINITE);
#else
        pthread_join(t1, NULL);
#endif
        log_msg("Подчинённый завершён");
    }
    return 0;
}
