@echo off
rem =======================================================================================
rem RetroSaveConverter - local Windows build script
rem
rem Produces RetroSaveConverter.exe (compatible with Windows 7 x64 and later).
rem
rem Option 1: MinGW-w64 (e.g. from https://winlibs.com/ or MSYS2) - g++ in PATH:
rem            builds a fully static executable (no runtime DLLs needed)
rem Option 2: Microsoft Visual C++ - run from a "Developer Command Prompt" (cl in PATH)
rem            links against the VC runtime statically (/MT)
rem =======================================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
if not exist build mkdir build

set "OUTPUT=RetroSaveConverter.exe"

where g++ >nul 2>nul
if %errorlevel%==0 goto mingw

where cl >nul 2>nul
if %errorlevel%==0 goto msvc

echo.
echo ERROR: neither MinGW-w64 ^(g++^) nor MSVC ^(cl^) was found in PATH.
echo   - MinGW-w64: download from https://winlibs.com/ and add its bin folder to PATH
echo   - MSVC:      run this script from "x64 Native Tools Command Prompt"
echo.
exit /b 1

rem ---------------------------------------------------------------------------------------
:mingw
echo [1/3] compiling resources (MinGW-w64)...
set "WINDRES=windres"
where windres >nul 2>nul || set "WINDRES=x86_64-w64-mingw32-windres"
%WINDRES% src\app.rc -O coff -o build\app.res -I src
if errorlevel 1 goto fail

echo [2/3] compiling sources...
g++ -std=c++17 -O2 -Wall -Wextra ^
    -DUNICODE -D_UNICODE ^
    -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0600 ^
    -municode -mwindows ^
    -static -static-libgcc -static-libstdc++ ^
    src\main.cpp src\converter.cpp build\app.res ^
    -o %OUTPUT% ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lgdi32
if errorlevel 1 goto fail

echo [3/3] done.
echo.
echo Built: %OUTPUT% ^(static, Windows 7 x64+^)
goto :eof

rem ---------------------------------------------------------------------------------------
:msvc
echo [1/3] compiling resources (MSVC)...
rc /i src /fo build\app.res src\app.rc
if errorlevel 1 goto fail

echo [2/3] compiling sources...
cl /nologo /utf-8 /EHsc /O2 /W4 /MT ^
   /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /D_WIN32_IE=0x0600 ^
   src\main.cpp src\converter.cpp build\app.res ^
   /Fe:%OUTPUT% ^
   /link /subsystem:windows user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib
if errorlevel 1 goto fail

del /q build\*.obj >nul 2>nul

echo [3/3] done.
echo.
echo Built: %OUTPUT% ^(MSVC, static CRT, Windows 7 x64+^)
goto :eof

:fail
echo.
echo BUILD FAILED.
exit /b 1
