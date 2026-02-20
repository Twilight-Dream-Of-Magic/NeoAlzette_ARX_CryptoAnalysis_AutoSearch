@echo off
setlocal EnableExtensions EnableDelayedExpansion

chcp 65001

rem ============================================================================
rem build_and_test.bat
rem
rem Goals:
rem   - Robust on Windows cmd.exe even when the repo path contains spaces and []
rem   - Clear errors; minimal cmd escaping tricks
rem
rem Requirements:
rem   - Set env var CLANG64 to your mingw64\bin directory containing clang++.exe
rem     OR pass: --mingw64 "X:\path\to\mingw64\bin"
rem
rem Options:
rem   --no-pause     : do not pause at the end (useful for CI / scripting)
rem   --no-clean     : do not delete old .exe files before building
rem   --build-only   : build but do not run tests (aliases: build-only, /build-only, "build only")
rem   --run-only     : run tests without rebuilding
rem   --smoke        : compile/interface-only smoke; implies --build-only --no-clean --no-pause
rem ============================================================================

set "NO_PAUSE=0"
set "DO_CLEAN=1"
set "DO_BUILD=1"
set "DO_RUN=1"
set "SMOKE=0"

:parse_args
if "%~1"=="" goto :args_done

rem Two-token synonym (e.g. user types: build_and_test.bat build only)
if /I "%~1"=="build" if /I "%~2"=="only" (
  set "DO_RUN=0"
  shift
  shift
  goto :parse_args
)

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
rem Do not use (set ... & shift & goto) on one line: some cmd builds mishandle goto inside parens.
if /I "%~1"=="--build-only" goto :arg_build_only
if /I "%~1"=="-build-only" goto :arg_build_only
if /I "%~1"=="/build-only" goto :arg_build_only
if /I "%~1"=="build-only" goto :arg_build_only
if /I "%~1"=="--run-only" (
  set "DO_BUILD=0"
  set "DO_CLEAN=0"
  shift
  goto :parse_args
)
if /I "%~1"=="--smoke" (
  set "SMOKE=1"
  set "DO_RUN=0"
  set "DO_CLEAN=0"
  set "NO_PAUSE=1"
  shift
  goto :parse_args
)
if /I "%~1"=="--mingw64" (
  if "%~2"=="" (
    echo ERROR: --mingw64 requires a path argument.
    exit /b 2
  )
  set "CLANG64=%~2"
  shift
  shift
  goto :parse_args
)
echo ERROR: unknown argument: %~1
exit /b 2

:arg_build_only
set "DO_RUN=0"
shift
goto :parse_args

:args_done

rem ---- Detect project root (prefer script dir; fallback to current dir) ----
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
  echo   Tried:
  echo     script dir = "%ROOT_SCRIPT%"
  echo     current dir = "%ROOT_CWD%"
  echo   Expected file:
  echo     test_neoalzette_arx_trace.cpp
  goto :fail_nopopd
)

pushd "%ROOT%"
if errorlevel 1 (
  echo ERROR: cannot cd to project root: "%ROOT%"
  goto :fail_nopopd
)

echo ============================================================
echo Build and Test - NeoAlzette AutoSearch (clang++ / C++20)
echo ============================================================
echo.

rem Trim leading spaces (common when env vars are edited manually)
for /f "tokens=* delims= " %%A in ("%CLANG64%") do set "CLANG64=%%A"

if "%CLANG64%"=="" (
  echo ERROR: CLANG64 is not set.
  echo   Example:
  echo     set CLANG64=E:\[About Programming]\WindowsLibsGCC\mingw64\bin
  echo   Or:
  echo     build_and_test.bat --mingw64 "E:\...\mingw64\bin"
  goto :fail
)

set "CXX=%CLANG64%\clang++.exe"
if not exist "%CXX%" (
  echo ERROR: clang++.exe not found at:
  echo   "%CXX%"
  goto :fail
)

set "INCLUDE_DIR=%ROOT%\include"
set "CORE_SRC=%ROOT%\src\neoalzette\neoalzette_core.cpp"
set "BEST_SEARCH_SHARED_CORE_SRC=%ROOT%\src\auto_search_frame\best_search_shared_core.cpp"
set "LINEAR_BEST_SEARCH_MATH_SRC=%ROOT%\src\auto_search_frame\linear_best_search_math.cpp"
set "LINEAR_BEST_SEARCH_CHECKPOINT_SRC=%ROOT%\src\auto_search_frame\linear_best_search_checkpoint.cpp"
set "LINEAR_BEST_SEARCH_ENGINE_SRC=%ROOT%\src\auto_search_frame\linear_best_search_engine.cpp"
set "LINEAR_BEST_SEARCH_COLLECTOR_SRC=%ROOT%\src\auto_search_frame\linear_best_search_collector.cpp"
set "DIFFERENTIAL_BEST_SEARCH_MATH_SRC=%ROOT%\src\auto_search_frame\differential_best_search_math.cpp"
set "DIFFERENTIAL_BEST_SEARCH_CHECKPOINT_SRC=%ROOT%\src\auto_search_frame\differential_best_search_checkpoint.cpp"
set "DIFFERENTIAL_BEST_SEARCH_ENGINE_SRC=%ROOT%\src\auto_search_frame\differential_best_search_engine.cpp"
set "DIFFERENTIAL_BEST_SEARCH_COLLECTOR_SRC=%ROOT%\src\auto_search_frame\differential_best_search_collector.cpp"

if not exist "%CORE_SRC%" (
  echo ERROR: core source not found:
  echo   "%CORE_SRC%"
  goto :fail
)

set "COMMON_FLAGS=-std=c++20 -O3 -static -Wall -Wextra -lpsapi"

echo Toolchain:
echo   ROOT = "%ROOT%"
echo   CXX  = "%CXX%"
if "%SMOKE%"=="1" (
  echo   MODE = smoke ^(build-only, no-clean, no-pause^)
)
echo.

if "%DO_CLEAN%"=="1" (
  echo [1/3] Cleaning old builds...
  del /Q test_neoalzette_arx_trace.exe 2>nul
  del /Q test_neoalzette_differential_best_search.exe 2>nul
  del /Q test_neoalzette_linear_best_search.exe 2>nul
  del /Q test_neoalzette_differential_hull_wrapper.exe 2>nul
  del /Q test_neoalzette_linear_hull_wrapper.exe 2>nul
  echo.
) else (
  echo [1/3] Cleaning old builds... ^(skipped: --no-clean^)
  echo.
)

if "%DO_BUILD%"=="1" (
  echo [2/3] Compiling ^(5 targets^) ...
  echo.

  echo   - test_neoalzette_arx_trace.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_neoalzette_arx_trace.cpp" "%CORE_SRC%" -o "%ROOT%\test_neoalzette_arx_trace.exe"
  if errorlevel 1 goto :build_failed
  echo.

  echo   - test_neoalzette_differential_best_search.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_neoalzette_differential_best_search.cpp" "%ROOT%\test_arx_operator_self_test.cpp" "%ROOT%\common\runtime_component.cpp" "%BEST_SEARCH_SHARED_CORE_SRC%" "%LINEAR_BEST_SEARCH_MATH_SRC%" "%DIFFERENTIAL_BEST_SEARCH_MATH_SRC%" "%DIFFERENTIAL_BEST_SEARCH_CHECKPOINT_SRC%" "%DIFFERENTIAL_BEST_SEARCH_ENGINE_SRC%" "%DIFFERENTIAL_BEST_SEARCH_COLLECTOR_SRC%" "%CORE_SRC%" -o "%ROOT%\test_neoalzette_differential_best_search.exe"
  if errorlevel 1 goto :build_failed
  echo.

  echo   - test_neoalzette_linear_best_search.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_neoalzette_linear_best_search.cpp" "%ROOT%\test_arx_operator_self_test.cpp" "%ROOT%\common\runtime_component.cpp" "%BEST_SEARCH_SHARED_CORE_SRC%" "%LINEAR_BEST_SEARCH_MATH_SRC%" "%LINEAR_BEST_SEARCH_CHECKPOINT_SRC%" "%DIFFERENTIAL_BEST_SEARCH_MATH_SRC%" "%LINEAR_BEST_SEARCH_ENGINE_SRC%" "%LINEAR_BEST_SEARCH_COLLECTOR_SRC%" "%CORE_SRC%" -o "%ROOT%\test_neoalzette_linear_best_search.exe"
  if errorlevel 1 goto :build_failed
  echo.

  echo   - test_neoalzette_differential_hull_wrapper.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_neoalzette_differential_hull_wrapper.cpp" "%ROOT%\common\runtime_component.cpp" "%BEST_SEARCH_SHARED_CORE_SRC%" "%DIFFERENTIAL_BEST_SEARCH_MATH_SRC%" "%DIFFERENTIAL_BEST_SEARCH_CHECKPOINT_SRC%" "%DIFFERENTIAL_BEST_SEARCH_ENGINE_SRC%" "%DIFFERENTIAL_BEST_SEARCH_COLLECTOR_SRC%" "%CORE_SRC%" -o "%ROOT%\test_neoalzette_differential_hull_wrapper.exe"
  if errorlevel 1 goto :build_failed
  echo.

  echo   - test_neoalzette_linear_hull_wrapper.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_neoalzette_linear_hull_wrapper.cpp" "%ROOT%\common\runtime_component.cpp" "%BEST_SEARCH_SHARED_CORE_SRC%" "%LINEAR_BEST_SEARCH_MATH_SRC%" "%LINEAR_BEST_SEARCH_CHECKPOINT_SRC%" "%LINEAR_BEST_SEARCH_ENGINE_SRC%" "%LINEAR_BEST_SEARCH_COLLECTOR_SRC%" "%CORE_SRC%" -o "%ROOT%\test_neoalzette_linear_hull_wrapper.exe"
  if errorlevel 1 goto :build_failed
  echo.

  echo OK: compilation successful.
  echo.
) else (
  echo [2/3] Compiling... ^(skipped: --run-only^)
  echo.
)

if "%DO_RUN%"=="1" (
  echo [3/3] Running tests...
  echo ============================================================

  if not exist "%ROOT%\test_neoalzette_differential_best_search.exe" (
    echo ERROR: missing executable: test_neoalzette_differential_best_search.exe
    goto :fail
  )
  "%ROOT%\test_neoalzette_differential_best_search.exe" --selftest
  if errorlevel 1 goto :fail

  echo.
  echo ============================================================
  if not exist "%ROOT%\test_neoalzette_linear_best_search.exe" (
    echo ERROR: missing executable: test_neoalzette_linear_best_search.exe
    goto :fail
  )
  "%ROOT%\test_neoalzette_linear_best_search.exe" --selftest
  if errorlevel 1 goto :fail

  echo.
  echo ============================================================
  if not exist "%ROOT%\test_neoalzette_linear_hull_wrapper.exe" (
    echo ERROR: missing executable: test_neoalzette_linear_hull_wrapper.exe
    goto :fail
  )
  "%ROOT%\test_neoalzette_linear_hull_wrapper.exe" --selftest
  if errorlevel 1 goto :fail

  echo.
  echo ============================================================
  if not exist "%ROOT%\test_neoalzette_differential_hull_wrapper.exe" (
    echo ERROR: missing executable: test_neoalzette_differential_hull_wrapper.exe
    goto :fail
  )
  "%ROOT%\test_neoalzette_differential_hull_wrapper.exe" --selftest
  if errorlevel 1 goto :fail

  echo.
  echo OK: all done.
  echo ============================================================
) else (
  echo [3/3] Running tests... ^(skipped: --build-only^)
)

goto :ok

:is_project_root
setlocal
set "CAND=%~1"
if exist "%CAND%\test_neoalzette_arx_trace.cpp" (
  endlocal & exit /b 0
)
endlocal & exit /b 1

:build_failed
echo.
echo ERROR: compilation failed.
goto :fail

:fail
echo.
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
rem (end)
