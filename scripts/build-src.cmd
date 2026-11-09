@echo off
REM 7-Zip Modern source build (MSVC x64)
REM Requires Visual Studio 2022 (Build Tools or full IDE) with C++ workload.

set VCVARS=
REM 1) Ask vswhere (present on every VS install, also on CI runners)
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat
  )
)

REM 2) Fallback: well-known paths
if not defined VCVARS for %%P in (
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
) do if not defined VCVARS if exist %%P set VCVARS=%%~P

if not defined VCVARS (
  echo ERROR: vcvars64.bat not found. Install VS 2022 with the C++ workload.
  exit /b 1
)
echo Using %VCVARS%
call "%VCVARS%" >nul

REM vcvars locates the Windows SDK through the registry. On locked-down
REM machines that query is blocked, leaving INCLUDE/LIB without the SDK and
REM every compile fails on <stddef.h>. Add the newest installed SDK back.
REM Single line if-statements only: "%ProgramFiles(x86)%" contains a
REM parenthesis, which would end a multi-line block early.
set "NEEDSDK="
echo %INCLUDE% | find /i "Windows Kits" >nul || set "NEEDSDK=1"
if defined NEEDSDK set "WINSDKROOT=%ProgramFiles(x86)%\Windows Kits\10"
if defined NEEDSDK if not exist "%WINSDKROOT%\Include" set "WINSDKROOT="
if defined WINSDKROOT for /f "delims=" %%v in ('dir /b /ad /o-n "%WINSDKROOT%\Include" 2^>nul') do if not defined WINSDKVER set "WINSDKVER=%%v"
if defined WINSDKVER set "INCLUDE=%INCLUDE%;%WINSDKROOT%\Include\%WINSDKVER%\ucrt;%WINSDKROOT%\Include\%WINSDKVER%\um;%WINSDKROOT%\Include\%WINSDKVER%\shared;%WINSDKROOT%\Include\%WINSDKVER%\winrt"
if defined WINSDKVER set "LIB=%LIB%;%WINSDKROOT%\Lib\%WINSDKVER%\ucrt\x64;%WINSDKROOT%\Lib\%WINSDKVER%\um\x64"
REM rc.exe (resource compiler) lives in the SDK bin directory and is only on
REM PATH through vcvars, so add it back too.
if defined WINSDKVER set "PATH=%PATH%;%WINSDKROOT%\bin\%WINSDKVER%\x64"
if defined WINSDKVER echo Windows SDK %WINSDKVER% added to INCLUDE/LIB/PATH.

set SRC=%~dp0..\src\src2606
if not exist "%SRC%" (
  echo ERROR: source tree not found at %SRC%
  echo Run scripts\get-source.cmd first.
  exit /b 1
)

echo [1/3] Building 7z.dll (Format7zF)
pushd "%SRC%\CPP\7zip\Bundles\Format7zF"
nmake NEW_COMPILER=1
if errorlevel 1 (popd & exit /b 1)
popd

echo [2/3] Building 7-zip.dll shell extension (Explorer)
pushd "%SRC%\CPP\7zip\UI\Explorer"
nmake NEW_COMPILER=1
if errorlevel 1 (popd & exit /b 1)
popd

echo [3/3] Building 7zFM.exe (Fm)
pushd "%SRC%\CPP\7zip\Bundles\Fm"
nmake NEW_COMPILER=1
if errorlevel 1 (popd & exit /b 1)
popd

echo.
echo Source build finished.
