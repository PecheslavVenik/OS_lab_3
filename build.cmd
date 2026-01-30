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
echo Checking for MinGW/GCC...

REM Check if GCC is already in PATH
where gcc >nul 2>nul
if %errorlevel% equ 0 (
    echo GCC found in PATH
    goto git_pull
)

echo GCC not found in PATH. Searching in common locations...

REM Check common locations
if exist "C:\msys64\mingw64\bin\gcc.exe" (
    echo Found MinGW at: C:\msys64\mingw64\bin
    set "PATH=C:\msys64\mingw64\bin;%PATH%"
    goto git_pull
)

if exist "C:\mingw64\bin\gcc.exe" (
    echo Found MinGW at: C:\mingw64\bin
    set "PATH=C:\mingw64\bin;%PATH%"
    goto git_pull
)

if exist "C:\MinGW\bin\gcc.exe" (
    echo Found MinGW at: C:\MinGW\bin
    set "PATH=C:\MinGW\bin;%PATH%"
    goto git_pull
)

REM Check CLion bundled MinGW
if exist "%LOCALAPPDATA%\Programs\CLion\bin\mingw\bin\gcc.exe" (
    echo Found MinGW at: %LOCALAPPDATA%\Programs\CLion\bin\mingw\bin
    set "PATH=%LOCALAPPDATA%\Programs\CLion\bin\mingw\bin;%PATH%"
    goto git_pull
)

echo ERROR: MinGW not found. Please:
echo 1. Install MinGW or MSYS2
echo 2. Add gcc.exe to PATH
echo 3. Or build from CLion IDE directly
exit /b 1

:git_pull
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
echo Using generator: MinGW Makefiles
cmake .. -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
if %errorlevel% neq 0 (
    echo CMake configuration failed
    echo.
    echo Troubleshooting:
    echo 1. Make sure MinGW is installed
    echo 2. Check that gcc.exe is in PATH: where gcc
    echo 3. Or open project in CLion and build from IDE
    cd ..
    exit /b 1
)

echo.
echo Building...
mingw32-make
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