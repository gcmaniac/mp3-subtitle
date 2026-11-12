@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 (
    if exist "D:\c_projects\compilers\mingw64\bin\gcc.exe" (
        set "PATH=D:\c_projects\compilers\mingw64\bin;%PATH%"
    ) else (
        echo [ERROR] gcc not found in PATH.
        exit /b 1
    )
)

set "CC=gcc"
set "WINDRES=windres"
set "CFLAGS=-std=c11 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Isrc"
set "LDFLAGS=-municode -mwindows -specs=src/no-default-manifest.specs"
set "LIBS=-lwinmm -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32"

if /i "%~1"=="clean" goto :do_clean
if /i "%~1"=="test" goto :do_test
if /i "%~1"=="all" goto :do_all
if "%~1"=="" goto :do_build

echo Usage: make.bat [all^|test^|clean]
exit /b 1

:do_clean
echo Cleaning build artifacts...
del /Q src\*.o src\*.res mp3-subtitle.exe test_srt.exe 2>nul
echo Clean done.
exit /b 0

:do_build
echo [1/7] Compiling src/main.c...
%CC% %CFLAGS% -c src/main.c -o src/main.o || exit /b 1

echo [2/7] Compiling src/player.c...
%CC% %CFLAGS% -c src/player.c -o src/player.o || exit /b 1

echo [3/7] Compiling src/prefs.c...
%CC% %CFLAGS% -c src/prefs.c -o src/prefs.o || exit /b 1

echo [4/7] Compiling src/srt.c...
%CC% %CFLAGS% -c src/srt.c -o src/srt.o || exit /b 1

echo [5/7] Compiling src/trackbar.c...
%CC% %CFLAGS% -c src/trackbar.c -o src/trackbar.o || exit /b 1

echo [6/7] Compiling resources (src/app.rc)...
%WINDRES% -i src/app.rc -o src/app.res -O coff || exit /b 1

echo [7/7] Linking mp3-subtitle.exe...
%CC% %LDFLAGS% -o mp3-subtitle.exe src/main.o src/player.o src/prefs.o src/srt.o src/trackbar.o src/app.res %LIBS% || exit /b 1

echo Build succeeded: mp3-subtitle.exe
exit /b 0

:do_test
echo Compiling tests/test_srt.c...
%CC% %CFLAGS% -municode tests/test_srt.c src/srt.c src/prefs.c src/trackbar.c -o test_srt.exe -lcomctl32 -luser32 || exit /b 1
echo Running test_srt.exe...
test_srt.exe || exit /b 1
exit /b 0

:do_all
call :do_build || exit /b 1
call :do_test || exit /b 1
exit /b 0

