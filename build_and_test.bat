@echo off
setlocal EnableExtensions EnableDelayedExpansion

chcp 65001 >nul

rem Build-only compile check for non-root AutoSearch modules.
rem Root-level .cpp research programs are intentionally not built or run here.

set "NO_PAUSE=0"
set "DO_CLEAN=1"
set "BUILD_DIR="
set "GENERATOR="

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--no-pause" (
  set "NO_PAUSE=1"
  shift
  goto :parse_args
)
if /I "%~1"=="--no-clean" (
  set "DO_CLEAN=0"
  shift
  goto :parse_args
)
if /I "%~1"=="--build-only" (
  shift
  goto :parse_args
)
if /I "%~1"=="build-only" (
  shift
  goto :parse_args
)
if /I "%~1"=="--smoke" (
  set "DO_CLEAN=0"
  set "NO_PAUSE=1"
  shift
  goto :parse_args
)
if /I "%~1"=="--build-dir" (
  if "%~2"=="" (
    echo ERROR: --build-dir requires a path argument.
    exit /b 2
  )
  set "BUILD_DIR=%~2"
  shift
  shift
  goto :parse_args
)
if /I "%~1"=="--generator" (
  if "%~2"=="" (
    echo ERROR: --generator requires a generator name.
    exit /b 2
  )
  set "GENERATOR=%~2"
  shift
  shift
  goto :parse_args
)
if /I "%~1"=="--run-only" (
  echo ERROR: this script no longer runs programs.
  exit /b 2
)
echo ERROR: unknown argument: %~1
exit /b 2

:args_done
set "ROOT="
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%.") do set "ROOT_SCRIPT=%%~fI"
set "ROOT_CWD=%CD%"

call :is_project_root "%ROOT_SCRIPT%"
if "%ERRORLEVEL%"=="0" (
  set "ROOT=%ROOT_SCRIPT%"
) else (
  call :is_project_root "%ROOT_CWD%"
  if "%ERRORLEVEL%"=="0" set "ROOT=%ROOT_CWD%"
)

if not defined ROOT (
  echo ERROR: cannot locate project root directory.
  goto :fail_nopopd
)

if not defined BUILD_DIR set "BUILD_DIR=%ROOT%\build\compile-check"

where cmake.exe >nul 2>nul
if errorlevel 1 (
  echo ERROR: cmake.exe not found on PATH.
  goto :fail_nopopd
)

pushd "%ROOT%"
if errorlevel 1 goto :fail_nopopd

echo ============================================================
echo Build - NeoAlzette AutoSearch compile checks
echo ============================================================
echo ROOT      = "%ROOT%"
echo BUILD_DIR = "%BUILD_DIR%"
echo.

if "%DO_CLEAN%"=="1" (
  echo [1/3] Cleaning build directory...
  if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
  echo.
) else (
  echo [1/3] Cleaning build directory... ^(skipped^)
  echo.
)

echo [2/3] Configuring CMake...
if defined GENERATOR (
  cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" -DNEOALZETTE_BUILD_PROGRAMS=OFF -DCMAKE_BUILD_TYPE=Release
) else (
  cmake -S "%ROOT%" -B "%BUILD_DIR%" -DNEOALZETTE_BUILD_PROGRAMS=OFF -DCMAKE_BUILD_TYPE=Release
)
if errorlevel 1 goto :fail
echo.

echo [3/3] Building non-root compile targets...
cmake --build "%BUILD_DIR%" --config Release --target neoalzette_linear_best_search_compile neoalzette_differential_best_search_compile --parallel
if errorlevel 1 goto :fail
echo.
echo OK: compile checks completed. No program was run.
goto :ok

:is_project_root
setlocal
set "CAND=%~1"
if exist "%CAND%\CMakeLists.txt" (
  endlocal & exit /b 0
)
endlocal & exit /b 1

:fail
echo.
echo ERROR: build failed.
if "%NO_PAUSE%"=="0" pause
popd
exit /b 1

:fail_nopopd
echo.
if "%NO_PAUSE%"=="0" pause
exit /b 1

:ok
echo.
if "%NO_PAUSE%"=="0" pause
popd
exit /b 0