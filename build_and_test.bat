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
rem   - Optional compiler env vars / dirs:
rem       CXX      = compiler executable path or bare command name
rem       CLANG64  = bin dir containing clang++.exe / g++.exe
rem       MINGW64  = bin dir containing g++.exe / clang++.exe
rem   - If none of the above is set, the script falls back to g++.exe from PATH
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
  echo     CMakeLists.txt
  goto :fail_nopopd
)

pushd "%ROOT%"
if errorlevel 1 (
  echo ERROR: cannot cd to project root: "%ROOT%"
  goto :fail_nopopd
)

echo ============================================================
echo Build and Test - NeoAlzette AutoSearch (C++20)
echo ============================================================
echo.

rem Trim leading spaces (common when env vars are edited manually)
for /f "tokens=* delims= " %%A in ("%CLANG64%") do set "CLANG64=%%A"
for /f "tokens=* delims= " %%A in ("%MINGW64%") do set "MINGW64=%%A"
for /f "tokens=* delims= " %%A in ("%CXX%") do set "CXX=%%A"

if not defined CXX (
  if not "%CLANG64%"=="" (
    if exist "%CLANG64%\g++.exe" set "CXX=%CLANG64%\g++.exe"
    if not defined CXX if exist "%CLANG64%\clang++.exe" set "CXX=%CLANG64%\clang++.exe"
  )
)
if not defined CXX (
  if not "%MINGW64%"=="" (
    if exist "%MINGW64%\g++.exe" set "CXX=%MINGW64%\g++.exe"
    if not defined CXX if exist "%MINGW64%\clang++.exe" set "CXX=%MINGW64%\clang++.exe"
  )
)
if not defined CXX set "CXX=g++.exe"

if exist "%CXX%" (
  rem full path resolved directly
) else (
  where "%CXX%" >nul 2>nul
  if errorlevel 1 (
    echo ERROR: compiler not found:
    echo   "%CXX%"
    echo   Set one of:
    echo     set CXX=E:\...\g++.exe
    echo     set CLANG64=E:\...\bin
    echo     set MINGW64=E:\...\bin
    echo   Or ensure g++.exe is on PATH.
    goto :fail
  )
)

set "INCLUDE_DIR=%ROOT%\include"
set "CORE_SRC=%ROOT%\src\neoalzette\neoalzette_core.cpp"
set "BEST_SEARCH_SHARED_CORE_SRC=%ROOT%\src\auto_search_frame\best_search_shared_core.cpp"
set "LINEAR_CONST_FIXED_ALPHA_SRC=%ROOT%\src\arx_analysis_operators\linear_correlation\constant_fixed_alpha_core.cpp"
set "LINEAR_CONST_FIXED_BETA_SRC=%ROOT%\src\arx_analysis_operators\linear_correlation\constant_fixed_beta_core.cpp"
set "LINEAR_PROFILE_U_BETA_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\linear_bnb_profile_fixed_u_fixed_beta.cpp"
set "LINEAR_PROFILE_U_ALPHA_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\linear_bnb_profile_fixed_u_fixed_alpha.cpp"
set "LINEAR_PROFILE_VW_BETA_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\linear_bnb_profile_fixed_vw_fixed_beta.cpp"
set "LINEAR_PROFILE_VW_ALPHA_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\linear_bnb_profile_fixed_vw_fixed_alpha.cpp"
set "LINEAR_BNB_VARVAR_FIXED_U_Q2_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\varvar\fixed_u_q2.cpp"
set "LINEAR_BNB_VARVAR_FIXED_VW_Q2_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\varvar\fixed_vw_q2.cpp"
set "LINEAR_BNB_VARVAR_WEIGHT_SLICED_CLAT_Q2_SRC=%ROOT%\src\auto_search_frame_bnb_detail\linear\varvar_z_shell_weight_sliced_clat_q2.cpp"
set "LINEAR_BNB_VARVAR_Q1_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\varvar\varvar_q1.cpp"
set "LINEAR_BNB_VARCONST_FIXED_BETA_Q2_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\varconst\fixed_beta_q2.cpp"
set "LINEAR_BNB_VARCONST_FIXED_ALPHA_Q2_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\varconst\fixed_alpha_q2.cpp"
set "LINEAR_BNB_VARCONST_Q1_SRC=%ROOT%\src\auto_search_frame_bnb_detail\polarity\linear\varconst\varconst_q1.cpp"
set "LINEAR_BEST_SEARCH_MATH_SRC=%ROOT%\src\auto_search_frame\linear_best_search_math.cpp"
set "LINEAR_BEST_SEARCH_CHECKPOINT_SRC=%ROOT%\src\auto_search_frame\linear_best_search_checkpoint.cpp"
set "LINEAR_BEST_SEARCH_ENGINE_SRC=%ROOT%\src\auto_search_frame\linear_best_search_engine.cpp"
set "LINEAR_BEST_SEARCH_COLLECTOR_SRC=%ROOT%\src\auto_subspace_hull\linear_best_search_collector.cpp"
set "DIFFERENTIAL_BNB_VARVAR_WEIGHT_SLICED_PDDT_Q2_SRC=%ROOT%\src\auto_search_frame_bnb_detail\differential\varvar_weight_sliced_pddt_q2.cpp"
set "DIFFERENTIAL_BEST_SEARCH_MATH_SRC=%ROOT%\src\auto_search_frame\differential_best_search_math.cpp"
set "DIFFERENTIAL_BEST_SEARCH_CHECKPOINT_SRC=%ROOT%\src\auto_search_frame\differential_best_search_checkpoint.cpp"
set "DIFFERENTIAL_BEST_SEARCH_ENGINE_SRC=%ROOT%\src\auto_search_frame\differential_best_search_engine.cpp"
set "DIFFERENTIAL_BEST_SEARCH_COLLECTOR_SRC=%ROOT%\src\auto_subspace_hull\differential_best_search_collector.cpp"

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
  del /Q test_neoalzette_differential_best_search.exe 2>nul
  del /Q test_neoalzette_linear_best_search.exe 2>nul
  del /Q test_neoalzette_differential_hull_wrapper.exe 2>nul
  del /Q test_neoalzette_linear_hull_wrapper.exe 2>nul
  del /Q test_runtime_thread_pool.exe 2>nul
  echo.
) else (
  echo [1/3] Cleaning old builds... ^(skipped: --no-clean^)
  echo.
)

if "%DO_BUILD%"=="1" (
  echo [2/3] Compiling ^(5 targets^) ...
  echo.

  echo   - test_runtime_thread_pool.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_runtime_thread_pool.cpp" "%ROOT%\common\runtime_component.cpp" -o "%ROOT%\test_runtime_thread_pool.exe"
  if errorlevel 1 goto :build_failed
  echo.

  echo   - test_neoalzette_differential_best_search.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_neoalzette_differential_best_search.cpp" "%ROOT%\test_arx_operator_self_test_differential.cpp" "%ROOT%\common\runtime_component.cpp" "%BEST_SEARCH_SHARED_CORE_SRC%" "%LINEAR_CONST_FIXED_ALPHA_SRC%" "%LINEAR_CONST_FIXED_BETA_SRC%" "%DIFFERENTIAL_BNB_VARVAR_WEIGHT_SLICED_PDDT_Q2_SRC%" "%DIFFERENTIAL_BEST_SEARCH_MATH_SRC%" "%DIFFERENTIAL_BEST_SEARCH_CHECKPOINT_SRC%" "%DIFFERENTIAL_BEST_SEARCH_ENGINE_SRC%" "%DIFFERENTIAL_BEST_SEARCH_COLLECTOR_SRC%" "%CORE_SRC%" -o "%ROOT%\test_neoalzette_differential_best_search.exe"
  if errorlevel 1 goto :build_failed
  echo.

  echo   - test_neoalzette_linear_best_search.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_neoalzette_linear_best_search.cpp" "%ROOT%\test_arx_operator_self_test_linear.cpp" "%ROOT%\common\runtime_component.cpp" "%BEST_SEARCH_SHARED_CORE_SRC%" "%LINEAR_CONST_FIXED_ALPHA_SRC%" "%LINEAR_CONST_FIXED_BETA_SRC%" "%LINEAR_BNB_VARVAR_WEIGHT_SLICED_CLAT_Q2_SRC%" "%LINEAR_BNB_VARVAR_FIXED_U_Q2_SRC%" "%LINEAR_BNB_VARVAR_FIXED_VW_Q2_SRC%" "%LINEAR_BNB_VARVAR_Q1_SRC%" "%LINEAR_BNB_VARCONST_FIXED_BETA_Q2_SRC%" "%LINEAR_BNB_VARCONST_FIXED_ALPHA_Q2_SRC%" "%LINEAR_BNB_VARCONST_Q1_SRC%" "%LINEAR_PROFILE_U_BETA_SRC%" "%LINEAR_PROFILE_U_ALPHA_SRC%" "%LINEAR_PROFILE_VW_BETA_SRC%" "%LINEAR_PROFILE_VW_ALPHA_SRC%" "%LINEAR_BEST_SEARCH_MATH_SRC%" "%LINEAR_BEST_SEARCH_CHECKPOINT_SRC%" "%LINEAR_BEST_SEARCH_ENGINE_SRC%" "%LINEAR_BEST_SEARCH_COLLECTOR_SRC%" "%CORE_SRC%" -o "%ROOT%\test_neoalzette_linear_best_search.exe"
  if errorlevel 1 goto :build_failed
  echo.

  echo   - test_neoalzette_differential_hull_wrapper.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_neoalzette_differential_hull_wrapper.cpp" "%ROOT%\common\runtime_component.cpp" "%BEST_SEARCH_SHARED_CORE_SRC%" "%LINEAR_CONST_FIXED_ALPHA_SRC%" "%LINEAR_CONST_FIXED_BETA_SRC%" "%DIFFERENTIAL_BNB_VARVAR_WEIGHT_SLICED_PDDT_Q2_SRC%" "%DIFFERENTIAL_BEST_SEARCH_MATH_SRC%" "%DIFFERENTIAL_BEST_SEARCH_CHECKPOINT_SRC%" "%DIFFERENTIAL_BEST_SEARCH_ENGINE_SRC%" "%DIFFERENTIAL_BEST_SEARCH_COLLECTOR_SRC%" "%CORE_SRC%" -o "%ROOT%\test_neoalzette_differential_hull_wrapper.exe"
  if errorlevel 1 goto :build_failed
  echo.

  echo   - test_neoalzette_linear_hull_wrapper.exe
  "%CXX%" %COMMON_FLAGS% -I"%INCLUDE_DIR%" -I"%ROOT%" "%ROOT%\test_neoalzette_linear_hull_wrapper.cpp" "%ROOT%\common\runtime_component.cpp" "%BEST_SEARCH_SHARED_CORE_SRC%" "%LINEAR_CONST_FIXED_ALPHA_SRC%" "%LINEAR_CONST_FIXED_BETA_SRC%" "%LINEAR_BNB_VARVAR_WEIGHT_SLICED_CLAT_Q2_SRC%" "%LINEAR_BNB_VARVAR_FIXED_U_Q2_SRC%" "%LINEAR_BNB_VARVAR_FIXED_VW_Q2_SRC%" "%LINEAR_BNB_VARVAR_Q1_SRC%" "%LINEAR_BNB_VARCONST_FIXED_BETA_Q2_SRC%" "%LINEAR_BNB_VARCONST_FIXED_ALPHA_Q2_SRC%" "%LINEAR_BNB_VARCONST_Q1_SRC%" "%LINEAR_PROFILE_U_BETA_SRC%" "%LINEAR_PROFILE_U_ALPHA_SRC%" "%LINEAR_PROFILE_VW_BETA_SRC%" "%LINEAR_PROFILE_VW_ALPHA_SRC%" "%LINEAR_BEST_SEARCH_MATH_SRC%" "%LINEAR_BEST_SEARCH_CHECKPOINT_SRC%" "%LINEAR_BEST_SEARCH_ENGINE_SRC%" "%LINEAR_BEST_SEARCH_COLLECTOR_SRC%" "%CORE_SRC%" -o "%ROOT%\test_neoalzette_linear_hull_wrapper.exe"
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

  if not exist "%ROOT%\test_runtime_thread_pool.exe" (
    echo ERROR: missing executable: test_runtime_thread_pool.exe
    goto :fail
  )
  "%ROOT%\test_runtime_thread_pool.exe"
  if errorlevel 1 goto :fail

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
if exist "%CAND%\CMakeLists.txt" (
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
