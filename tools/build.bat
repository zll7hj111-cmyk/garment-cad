@echo off
rem One-click build wrapper: vcvars64 + cmake configure + build (Ninja).
rem Usage: tools\build.bat [debug|release]   (default: debug)
setlocal

set "PRESET=default"
if /i "%~1"=="release" set "PRESET=release"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

cmake --preset %PRESET%
if errorlevel 1 exit /b 1

if not "%~2"=="" (
    cmake --build --preset %PRESET% --target %~2
) else (
    cmake --build --preset %PRESET%
)
if errorlevel 1 exit /b 1

endlocal
