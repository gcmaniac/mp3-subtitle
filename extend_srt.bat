@echo off
setlocal
if "%~1"=="" (
    echo Usage: extend_srt.bat ^<input.srt^> [gap_ms] [output.srt]
    echo Example: extend_srt.bat tests\17102.ar.srt 150
    exit /b 1
)

set "INPUT=%~1"
set "GAP=%~2"
if "%GAP%"=="" set "GAP=150"

if "%~3"=="" (
    python "%~dp0scripts\extend_srt.py" "%INPUT%" --gap %GAP%
) else (
    python "%~dp0scripts\extend_srt.py" "%INPUT%" --gap %GAP% -o "%~3"
)
exit /b %ERRORLEVEL%
