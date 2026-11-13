@echo off
rem Build minisearch with MSVC + Ninja (Release). Pure cmd so it works regardless
rem of PowerShell execution policy. Override tool paths via environment vars.
setlocal EnableDelayedExpansion
set "ROOT=%~dp0"
cd /d "%ROOT%"

if "%VCVARS%"=="" set "VCVARS=C:\develop\vs2022\community\VC\Auxiliary\Build\vcvars64.bat"
if "%CMAKE_BIN%"=="" set "CMAKE_BIN=C:\develop\vs2022\community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if "%NINJA_BIN%"=="" set "NINJA_BIN=C:\develop\vs2022\community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"

if not exist "%VCVARS%" echo ERROR: vcvars64.bat not found: %VCVARS% & exit /b 1
if not exist "%CMAKE_BIN%" echo ERROR: cmake.exe not found: %CMAKE_BIN% & exit /b 1
if not exist "%NINJA_BIN%" echo ERROR: ninja.exe not found: %NINJA_BIN% & exit /b 1

for %%I in ("%NINJA_BIN%") do set "NINJA_DIR=%%~dpI"

echo == vcvars ==
rem UTF-8 console codepage: MSVC emits UTF-8 /showIncludes lines and CMake must
rem decode them with the same codepage, otherwise ninja cannot parse deps.
chcp 65001 >nul
call "%VCVARS%" >nul
if errorlevel 1 echo ERROR: vcvars64.bat failed & exit /b 1

set "PATH=%NINJA_DIR%;%PATH%"

echo == configure (%BUILD_TYPE%) ==
"%CMAKE_BIN%" -G Ninja -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_MAKE_PROGRAM="%NINJA_BIN%"
if errorlevel 1 exit /b 1

echo == compile ==
"%CMAKE_BIN%" --build build
if errorlevel 1 exit /b 1

echo == build OK ==
exit /b 0
