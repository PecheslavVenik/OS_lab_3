@echo off
setlocal

echo ========================================
echo СКРИПТ_ДЛЯ_ШИНДОУС.cmd
echo ========================================

set MODE=%1
if "%MODE%"=="" set MODE=build
set COUNT=%2
if "%COUNT%"=="" set COUNT=1

where git >nul 2>nul
if %errorlevel% neq 0 (
    echo Гит не установлен
    exit /b 1
)

where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo Симейк не установлен
    exit /b 1
)

echo.
echo Пулл из гит репозитория...
git pull origin main
if %errorlevel% neq 0 (
    echo Нет интернета или ошибка гита.
    echo Продолжаем так ладно
)

if /I "%MODE%"=="clean" (
    call :clean
    pause
    exit /b 0
)

call :build
if /I "%MODE%"=="run" (
    call :run %COUNT%
)

pause
exit /b 0

:build
if not exist build (
    mkdir build
)

cd build

echo.
echo Симейк конфигуринг проджект...
cmake .. -G "MinGW Makefiles"
if %errorlevel% neq 0 (
    echo Симейк конфигуринг провален
    cd ..
    exit /b 1
)

echo.
echo Сборка...
cmake --build . --config Debug
if %errorlevel% neq 0 (
    echo Сборка провалена
    cd ..
    exit /b 1
)

cd ..

echo.
echo ========================================
echo Биллд завершен успешно!
echo Exе: build\lab3.exe
echo ========================================
exit /b 0

:clean
del /q program.log 2>nul
del /q leader.lock 2>nul
echo Очищены логи
exit /b 0

:run
set /a N=%1
if "%N%"=="" set /a N=1
if %N% lss 1 set /a N=1

call :clean

if %N% gtr 1 (
    for /l %%i in (2,1,%N%) do (
        start "Follower %%i" /min build\lab3.exe follower
        timeout /t 1 >nul
    )
)

build\lab3.exe

if %N% gtr 1 (
    for /l %%i in (2,1,%N%) do (
        taskkill /FI "WINDOWTITLE eq Follower %%i" /F 2>nul
    )
)
exit /b 0
