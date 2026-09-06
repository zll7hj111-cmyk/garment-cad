@echo off
rem One-click build wrapper: vcvars64 + on-demand cmake configure + build (Ninja).
rem Usage:
rem   tools\build.bat                          (build all in relwithdebinfo)
rem   tools\build.bat WildWindPattern          (fast incremental build of main app, ~0.3s)
rem   tools\build.bat <test_name>              (fast incremental build of single test)
rem   tools\build.bat [reldeb|debug|release]   (build all in specified preset)
rem   tools\build.bat reldeb WildWindPattern   (build specific target in specified preset)
rem   tools\build.bat reconfig                 (force re-run cmake configure)
setlocal enabledelayedexpansion

set "PRESET=relwithdebinfo"
set "BUILD_DIR=build\out-reldeb"
set "TARGET="
set "DO_CONFIG=0"

rem Parse arguments
set "ARG1=%~1"
set "ARG2=%~2"

if /i "%ARG1%"=="config" (
    set "DO_CONFIG=1"
    set "ARG1="
) else if /i "%ARG1%"=="reconfig" (
    set "DO_CONFIG=1"
    set "ARG1="
) else if /i "%ARG1%"=="--config" (
    set "DO_CONFIG=1"
    set "ARG1="
)

if /i "%ARG1%"=="debug" (
    set "PRESET=default"
    set "BUILD_DIR=build\out"
    set "TARGET=%ARG2%"
) else if /i "%ARG1%"=="release" (
    set "PRESET=release"
    set "BUILD_DIR=build\out-rel"
    set "TARGET=%ARG2%"
) else if /i "%ARG1%"=="reldeb" (
    set "PRESET=relwithdebinfo"
    set "BUILD_DIR=build\out-reldeb"
    set "TARGET=%ARG2%"
) else if /i "%ARG1%"=="relwithdebinfo" (
    set "PRESET=relwithdebinfo"
    set "BUILD_DIR=build\out-reldeb"
    set "TARGET=%ARG2%"
) else if not "%ARG1%"=="" (
    rem If ARG1 is not a preset name, treat it directly as TARGET in relwithdebinfo mode
    set "TARGET=%ARG1%"
)

if /i "%ARG2%"=="--config" set "DO_CONFIG=1"
if /i "%ARG2%"=="-c" set "DO_CONFIG=1"

rem Ensure MSVC environment (avoid re-running if already configured)
if "%VSCMD_ARG_TGT_ARCH%"=="" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 exit /b 1
)

rem Check if cmake configure is needed
if not exist "%BUILD_DIR%\build.ninja" set "DO_CONFIG=1"

if "%DO_CONFIG%"=="1" (
    echo [build.bat] Configuring CMake with preset [%PRESET%]...
    cmake --preset %PRESET%
    if errorlevel 1 exit /b 1
)

rem Run build
if not "%TARGET%"=="" (
    echo [build.bat] Building target [%TARGET%] [%PRESET%]...
    cmake --build --preset %PRESET% --target %TARGET%
) else (
    echo [build.bat] Building ALL targets [%PRESET%]...
    cmake --build --preset %PRESET%
)
if errorlevel 1 exit /b 1

endlocal

