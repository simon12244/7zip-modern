@echo off
REM Download the official 7-Zip installer into downloads\
REM Bump VERSION when a new 7-Zip release ships.

set VERSION=26.06
set FILE=7z%VERSION:.=%-x64.exe
set URL=https://github.com/ip7z/7zip/releases/download/%VERSION%/%FILE%

if not exist "%~dp0..\downloads" mkdir "%~dp0..\downloads"

if exist "%~dp0..\downloads\%FILE%" (
  echo Already exists, skipping download.
  exit /b 0
)
echo Downloading %URL%
curl -L -o "%~dp0..\downloads\%FILE%" "%URL%"
if errorlevel 1 exit /b 1
echo Saved to downloads\%FILE%
