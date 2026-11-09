@echo off
setlocal
REM Download the official 7-Zip installer into downloads\
REM Bump VERSION when a new 7-Zip release ships.

set VERSION=26.06
set NUM=%VERSION:.=%
set FILE=7z%NUM%-x64.exe
set URL=https://github.com/ip7z/7zip/releases/download/%VERSION%/%FILE%
set ROOT=%~dp0..
set OUT=%ROOT%\downloads\%FILE%

if not exist "%ROOT%\downloads" mkdir "%ROOT%\downloads"

call :fetch "%OUT%" "%URL%" 500000
if errorlevel 1 (
  echo ERROR: download failed or file looks invalid.
  exit /b 1
)
echo Saved to downloads\%FILE%
exit /b 0

:fetch
set "F=%~1"
set "U=%~2"
set "MIN=%~3"
if exist "%F%" for %%A in ("%F%") do if %%~zA GEQ %MIN% (
  echo Already downloaded.
  exit /b 0
)
echo Downloading %U%
for %%R in (1 2 3 4 5) do (
  curl -L -f -s --retry 3 --retry-all-errors --retry-delay 5 --max-time 900 -o "%F%" "%U%"
  if exist "%F%" for %%A in ("%F%") do if %%~zA GEQ %MIN% exit /b 0
  echo Retry %%R ...
  del /q "%F%" >nul 2>&1
)
exit /b 1
