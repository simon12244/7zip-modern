@echo off
REM Collect the freshly built binaries that we ship (the modified ones only).
REM 7z.dll and 7z.exe stay official - only UI and shell extension are patched.

set SRC=%~dp0..\src\src2606
set DIST=%~dp0..\dist

if not exist "%DIST%" mkdir "%DIST%"

copy /y "%SRC%\CPP\7zip\Bundles\Fm\x64\7zFM.exe" "%DIST%\7zFM.exe" >nul
if errorlevel 1 (echo ERROR: 7zFM.exe not built & exit /b 1)
copy /y "%SRC%\CPP\7zip\UI\Explorer\x64\7-zip.dll" "%DIST%\7-zip.dll" >nul
if errorlevel 1 (echo ERROR: 7-zip.dll not built & exit /b 1)

echo Collected:
dir /b "%DIST%"
