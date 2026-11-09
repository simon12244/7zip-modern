@echo off
REM 7-Zip Modern 一键构建脚本
REM 前置：安装 Inno Setup 7，ISCC.exe 在 PATH 中（或修改下面 SET 行）

set ISCC=ISCC.exe
if not exist "%ISCC%" set ISCC="%LOCALAPPDATA%\Programs\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" set ISCC="C:\Program Files (x86)\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" set ISCC="C:\Program Files\Inno Setup 7\ISCC.exe"

echo [1/2] 编译安装包...
%ISCC% "%~dp0..\installer\7zip-modern.iss"
if errorlevel 1 (
  echo 编译失败！
  exit /b 1
)

echo [2/2] 完成，产物：
dir /b "%~dp0..\installer\output\*.exe"
