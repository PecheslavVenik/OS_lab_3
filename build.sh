#!/bin/bash
set -e
mkdir -p build
cd build

clang++ ../main.cpp -o program -pthread

echo "Удаляю общую память и логи..."
rm -f program.log
if [ -e /dev/shm/shared_counter_shm_v2 ]; then rm -f /dev/shm/shared_counter_shm_v2; fi

read -p "Сколько процессов всего запустить (1 — только лидер)? (по умолчанию: 3): " N
N=${N:-3}
if [ "$N" -lt 1 ]; then N=1; fi

FOLLOWERS_PIDS=()

if [ "$N" -eq 1 ]; then
    echo "Запуск интерактивного ЛИДЕРА."
    ./program
else
    for i in $(seq 2 $N); do
        ./program follower > /dev/null 2>&1 &
        FOLLOWERS_PIDS+=($!)
        sleep 0.4
    done
    echo "Запуск интерактивного ЛИДЕРА (используйте команды set <num> и quit)..."
    ./program
    echo "Лидер завершён! Завершаю всех подчинённых..."
    for pid in "${FOLLOWERS_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    echo "Готово!"
fi
