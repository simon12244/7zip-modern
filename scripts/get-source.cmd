@echo off
setlocal
REM Fetch and extract the official 7-Zip source tree into src\srcXXXX,
REM then overlay the 7-Zip Modern patched files.

set VERSION=26.06
set NUM=%VERSION:.=%
set FILE=7z%NUM%-src.tar.xz
set ROOT=%~dp0..
set DIR=%ROOT%\src\src%NUM%
set DL=%ROOT%\downloads\%FILE%
set URL=https://github.com/ip7z/7zip/releases/download/%VERSION%/%FILE%

if not exist "%ROOT%\downloads" mkdir "%ROOT%\downloads"

call :fetch "%DL%" "%URL%" 500000
if errorlevel 1 (
  echo ERROR: source download failed or file looks invalid.
  exit /b 1
)

if exist "%DIR%\CPP" goto :overlay

echo Extracting source ...
if not exist "%ROOT%\src" mkdir "%ROOT%\src"
"C:\Program Files\7-Zip\7z.exe" x "%DL%" -so | "C:\Program Files\7-Zip\7z.exe" x -si -ttar -o"%DIR%" -y
if errorlevel 1 (
  echo ERROR: extraction failed. Install 7-Zip or fix the 7z.exe path above.
  exit /b 1
)
attrib -R "%DIR%\*.*" /S /D >nul 2>&1

:overlay
echo Overlaying 7-Zip Modern sources ...
xcopy "%ROOT%\overlay\*" "%DIR%\" /E /Y /Q >nul
if errorlevel 1 (
  echo ERROR: overlay copy failed.
  exit /b 1
)

echo Source ready at %DIR%
exit /b 0

:fetch
set "F=%~1"
set "U=%~2"
set "MIN=%~3"
if exist "%F%" for %%A in ("%F%") do if %%~zA GEQ %MIN% (
  echo Source archive already present.
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
