@echo off
setlocal enabledelayedexpansion

echo ========================================
echo CMake Build (use %%CMAKE%%, cmd.exe)
echo ========================================
echo.

chcp 65001 >nul

echo [0] Tool check...
echo   CMAKE = "%CMAKE%"
echo   CMAKE_GENERATOR = "%CMAKE_GENERATOR%"
echo   CMAKE_TOOLCHAIN_FILE = "%CMAKE_TOOLCHAIN_FILE%"
echo.

if "%CMAKE%"=="" (
    echo ✗ ERROR: %%CMAKE%% is not set.
    echo   Please set %%CMAKE%% to your cmake folder or cmake.exe path, e.g.
    echo   set CMAKE=E:\path\to\cmake\bin
    echo   or
    echo   set CMAKE=E:\path\to\cmake\bin\cmake.exe
    echo.
    pause
    exit /b 1
)

rem Allow CMAKE to be either a folder (containing cmake.exe) or the full path to cmake.exe.
for /f "tokens=* delims= " %%A in ("%CMAKE%") do set "CMAKE=%%A"
set "CMAKE_EXE=%CMAKE%"
if exist "%CMAKE%\cmake.exe" set "CMAKE_EXE=%CMAKE%\cmake.exe"

if not exist "%CMAKE_EXE%" (
    echo ✗ ERROR: "%CMAKE_EXE%" not found.
    echo.
    pause
    exit /b 1
)

set "BUILD_DIR=out\cmake-build"

echo [1/3] Configure...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

set "GEN_ARGS="
if not "%CMAKE_GENERATOR%"=="" (
    set "GEN_ARGS=-G %CMAKE_GENERATOR%"
)

set "TOOLCHAIN_ARGS="
if not "%CMAKE_TOOLCHAIN_FILE%"=="" (
    set "TOOLCHAIN_ARGS=-DCMAKE_TOOLCHAIN_FILE=%CMAKE_TOOLCHAIN_FILE%"
)

"%CMAKE_EXE%" %GEN_ARGS% -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Release -DNEOALZETTE_MSVC_STATIC_RUNTIME=ON -DNEOALZETTE_MINGW_STATIC=ON %TOOLCHAIN_ARGS%
if %ERRORLEVEL% NEQ 0 goto :cmake_failed

echo.
echo [2/3] Build...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% NEQ 0 goto :cmake_failed

echo.
echo [3/3] Done.
echo   Build folder: %BUILD_DIR%
echo.
pause
exit /b 0

:cmake_failed
echo.
echo ✗ CMake FAILED!
echo.
pause
exit /b 1

