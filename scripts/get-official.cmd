@echo off
REM Download the official 7-Zip installer into downloads\
REM Bump VERSION when a new 7-Zip release ships.

set VERSION=26.06
set FILE=7z%VERSION:.=%-x64.exe
set URL=https://github.com/ip7z/7zip/releases/download/%VERSION%/%FILE%
set OUT=%~dp0..\downloads\%FILE%

if not exist "%~dp0..\downloads" mkdir "%~dp0..\downloads"

call :check
if not errorlevel 1 (
  echo Already downloaded: %FILE%
  exit /b 0
)

echo Downloading %URL%
for %%R in (1 2 3 4 5) do (
  curl -L -f -s --retry 3 --retry-delay 5 --max-time 900 -o "%OUT%" "%URL%"
  call :check
  if not errorlevel 1 goto :ok
  echo Retry %%R ...
)
echo ERROR: download failed or file looks invalid.
exit /b 1

:ok
echo Saved to downloads\%FILE%
exit /b 0

:check
if not exist "%OUT%" exit /b 1
for %%A in ("%OUT%") do if %%~zA LSS 500000 exit /b 1
exit /b 0
