@echo off
rem Test runner gate: unfiltered full ctest is closing-acceptance only.
rem Usage:
rem   tools\test.bat -R <pattern> [more ctest args]   (daily: only affected tests, seconds)
rem   tools\test.bat all                              (full suite, parallel -j; closing acceptance)
rem   tools\test.bat -N                               (list registered test names)
setlocal
set "SCRIPT_DIR=%~dp0"
set "CTEST_DIR=%SCRIPT_DIR%..\build\out-reldeb"

if /i "%~1"=="-N" goto list
if /i "%~1"=="all" goto all
if /i "%~1"=="-R" goto filtered
goto usage

:list
ctest --test-dir "%CTEST_DIR%" -N
exit /b %errorlevel%

:all
set "JOBS=%NUMBER_OF_PROCESSORS%"
if defined CTEST_JOBS set "JOBS=%CTEST_JOBS%"
if "%JOBS%"=="" set "JOBS=4"
set /a "JOBS_INT=%JOBS%" 1>nul 2>nul
if errorlevel 1 set "JOBS_INT=4"
if %JOBS_INT% GTR 12 set "JOBS_INT=12"
echo [test.bat] Full ctest with %JOBS_INT% parallel jobs ^(perf tests run serial-protected^)...
ctest --test-dir "%CTEST_DIR%" -C RelWithDebInfo --output-on-failure -j %JOBS_INT%
exit /b %errorlevel%

:filtered
if "%~2"=="" goto usage
ctest --test-dir "%CTEST_DIR%" -C RelWithDebInfo --output-on-failure %*
exit /b %errorlevel%

:usage
echo [test.bat] GATE: unfiltered ctest is refused ^(full GUI event-loop suite + forces full relink^).
echo [test.bat] Daily:    tools\test.bat -R test_xxx        ^(seconds^)
echo [test.bat] Closing:  tools\test.bat all                ^(parallel full suite^)
echo [test.bat] List:     tools\test.bat -N
exit /b 1
