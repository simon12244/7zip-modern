@echo off
REM 7-Zip Modern installer build script
REM Requires Inno Setup 7. ISCC.exe should be on PATH or found automatically.

set ISCC=ISCC.exe
if not exist "%ISCC%" set ISCC="%LOCALAPPDATA%\Programs\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" set ISCC="C:\Program Files (x86)\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" set ISCC="C:\Program Files\Inno Setup 7\ISCC.exe"

echo [1/2] Compiling installer...
%ISCC% "%~dp0..\installer\7zip-modern.iss"
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo [2/2] Output:
dir /b "%~dp0..\installer\output\*.exe"
