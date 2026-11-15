@echo off
rem Builds dBTray.exe with the Visual Studio 2022 C++ tools (the free Build Tools are enough).
rem
rem   build.cmd          x64 release build      -> build\x64\dBTray.exe
rem   build.cmd arm64    ARM64 release build    -> build\arm64\dBTray.exe
rem   build.cmd test     builds and runs the loudness tests
setlocal enableextensions
cd /d "%~dp0"

set "TARGET=x64"
set "TESTS="
if /i "%~1"=="arm64" set "TARGET=arm64"
if /i "%~1"=="test" set "TESTS=1"

set "HOST=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "HOST=arm64"
set "VCARCH=%HOST%_%TARGET%"
if "%HOST%"=="%TARGET%" set "VCARCH=%TARGET%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -find VC\Auxiliary\Build\vcvarsall.bat`) do set "VCVARS=%%i"
)
if not defined VCVARS (
    echo Visual Studio 2022 or its Build Tools with the "Desktop development with C++" workload were not found.
    exit /b 1
)
call "%VCVARS%" %VCARCH% >nul 2>&1 || exit /b 1

set "OUT=build\%TARGET%"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
set "COMMON=/nologo /std:c++17 /permissive- /W4 /utf-8 /MT /EHsc /DUNICODE /D_UNICODE /DNOMINMAX"

if defined TESTS (
    if not exist "%OUT%\tests" mkdir "%OUT%\tests"
    cl %COMMON% /O2 tests\LoudnessTests.cpp src\Loudness.cpp /Fo%OUT%\tests\ /Fe%OUT%\tests\LoudnessTests.exe || exit /b 1
    %OUT%\tests\LoudnessTests.exe
    exit /b
)

rem /O1, /GL and /OPT keep the executable small; /MT links the C++ runtime in, so the .exe runs
rem on its own.
set "LIBS=user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib uxtheme.lib dwmapi.lib shcore.lib d2d1.lib dwrite.lib"
rc /nologo /fo %OUT%\obj\dBTray.res res\dBTray.rc || exit /b 1
cl %COMMON% /O1 /GL /Gy /Ires src\*.cpp %OUT%\obj\dBTray.res /Fo%OUT%\obj\ /Fe%OUT%\dBTray.exe ^
    /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /MANIFEST:NO %LIBS% || exit /b 1

for %%f in (%OUT%\dBTray.exe) do echo Built %%f (%%~zf bytes)
