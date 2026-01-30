@echo off
setlocal

echo ========================================
echo Lab3 Build Script (Windows)
echo ========================================

set MODE=%1
if "%MODE%"=="" set MODE=build
set COUNT=%2
if "%COUNT%"=="" set COUNT=1

where git >nul 2>nul
if %errorlevel% neq 0 (
    echo Git is not installed
    exit /b 1
)

where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo CMake is not installed
    exit /b 1
)

echo.
echo Pulling from git repository...
git pull origin main
if %errorlevel% neq 0 (
    echo No internet or git error.
    echo Continuing anyway
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
echo Configuring project with CMake...
cmake ..
if %errorlevel% neq 0 (
    echo CMake configuration failed
    cd ..
    exit /b 1
)

echo.
echo Building...
cmake --build . --config Debug
if %errorlevel% neq 0 (
    echo Build failed
    cd ..
    exit /b 1
)

cd ..

echo.
echo ========================================
echo Build completed successfully.
echo Executable: build\lab3.exe
echo ========================================
exit /b 0

:clean
del /q program.log 2>nul
del /q leader.lock 2>nul
echo Logs cleaned
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
