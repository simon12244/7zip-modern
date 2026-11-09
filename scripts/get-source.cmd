@echo off
REM Fetch and extract the official 7-Zip source tree into src\srcXXXX,
REM then overlay the 7-Zip Modern patched files.

set VERSION=26.06
set NUM=%VERSION:.=%
set FILE=7z%NUM%-src.tar.xz
set DIR=%~dp0..\src\src%NUM%
set DL=%~dp0..\downloads\%FILE%
set SZ=0

if not exist "%~dp0..\downloads" mkdir "%~dp0..\downloads"

if exist "%DL%" for %%A in ("%DL%") do set SZ=%%~zA
if %SZ% LSS 500000 (
  echo Downloading source %VERSION% ...
  for %%R in (1 2 3 4 5) do (
    curl -L -f -s --retry 3 --retry-delay 5 --max-time 900 -o "%DL%" "https://github.com/ip7z/7zip/releases/download/%VERSION%/%FILE%"
    set SZ=0
    if exist "%DL%" for %%A in ("%DL%") do set SZ=%%~zA
    if not errorlevel 1 goto :downloaded
    echo Retry %%R ...
    del /q "%DL%" 2>nul
  )
)

:downloaded
for %%A in ("%DL%") do set SZ=%%~zA
if %SZ% LSS 500000 (
  echo ERROR: source download failed or file looks invalid.
  exit /b 1
)

if not exist "%DIR%\CPP" (
  echo Extracting source ...
  if not exist "%~dp0..\src" mkdir "%~dp0..\src"
  "C:\Program Files\7-Zip\7z.exe" x "%DL%" -so | "C:\Program Files\7-Zip\7z.exe" x -si -ttar -o"%DIR%" -y
  if errorlevel 1 (
    echo ERROR: extraction failed. Install 7-Zip or fix the 7z.exe path above.
    exit /b 1
  )
  attrib -R "%DIR%\*.*" /S /D >nul 2>&1
)

echo Overlaying 7-Zip Modern sources ...
xcopy "%~dp0..\overlay\*" "%DIR%\" /E /Y /Q >nul
if errorlevel 1 (
  echo ERROR: overlay copy failed.
  exit /b 1
)

echo Source ready at %DIR%
exit /b 0
