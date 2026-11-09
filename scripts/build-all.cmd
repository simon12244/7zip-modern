@echo off
REM Full pipeline: official package -> source -> patched build -> installer
REM Used both locally and by GitHub Actions.

echo ============================================
echo  7-Zip Modern - full build
echo ============================================

call "%~dp0get-official.cmd"
if errorlevel 1 exit /b 1

call "%~dp0get-source.cmd"
if errorlevel 1 exit /b 1

call "%~dp0build-src.cmd"
if errorlevel 1 exit /b 1

call "%~dp0collect-dist.cmd"
if errorlevel 1 exit /b 1

call "%~dp0build.cmd"
if errorlevel 1 exit /b 1

echo.
echo Done. Installer is in installer\output\
