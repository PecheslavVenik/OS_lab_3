@echo off
setlocal enabledelayedexpansion

REM --- Сборка ---
echo Компилирую...
if exist program.exe del program.exe
cl.exe /EHsc /Fe:program.exe main.cpp

REM --- Очистка общих ресурсов ---
echo Очищаю логи и общую память...
if exist program.log del program.log

REM Очистить shared memory вручную не требуется — Windows удаляет Global\* когда нет процессов.

REM --- Запуск ---
set /p INSTANCES=Сколько процессов всего запустить (1 — только лидер)? (по умолчанию 3):
if "%INSTANCES%"=="" set INSTANCES=3

set /a N=%INSTANCES%
if "%N%"=="1" (
    echo Запуск интерактивного ЛИДЕРА...
    program.exe
) else (
    set FOLLOWERS=
    REM Запускаем followers
    for /l %%i in (2,1,%N%) do (
        start "Follower %%i" /min program.exe follower
        REM Нет способа получить PID сразу — просто запускаем процессы по названию.
        REM Можно убить все потом по названию позже.
        timeout /t 1 >nul
    )
    echo Запуск интерактивного ЛИДЕРА...
    program.exe
    echo Лидер завершён! Завершаю всех followers...
    REM Закрываем процессы по названию окна
    for /l %%i in (2,1,%N%) do (
        taskkill /FI "WINDOWTITLE eq Follower %%i" /F 2>nul
    )
    echo Готово!
)

endlocal
