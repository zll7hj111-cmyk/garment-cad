@echo off
rem Release build: offline dependency sources + Ninja Release (build/out-rel)
rem Usage: tools\build_release.bat [target]
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

set "SRCDEPS=E:\garment-cad\build\out\_deps"

cmake --preset release ^
  -DFETCHCONTENT_SOURCE_DIR_MINIZ=%SRCDEPS%\miniz-src ^
  -DFETCHCONTENT_SOURCE_DIR_ELAWIDGETTOOLS=%SRCDEPS%\elawidgettools-src ^
  -DFETCHCONTENT_SOURCE_DIR_SPDLOG=%SRCDEPS%\spdlog-src ^
  -DFETCHCONTENT_SOURCE_DIR_TRACY=%SRCDEPS%\tracy-src
if errorlevel 1 exit /b 1

if "%~1"=="" (
  cmake --build --preset release
) else (
  cmake --build build\out-rel --target %~1
)
exit /b %errorlevel%
