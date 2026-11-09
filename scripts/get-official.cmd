@echo off
REM 下载官方 7-Zip 安装包到 downloads\（版本更新时修改下面两处）
set VERSION=26.06
set URL=https://github.com/ip7z/7zip/releases/download/%VERSION%/7z%VERSION:.=%-x64.exe

if exist "%~dp0..\downloads\7z%VERSION:.=%-x64.exe" (
  echo 已存在，跳过下载
  exit /b 0
)
echo 下载 %URL% ...
curl -L -o "%~dp0..\downloads\7z%VERSION:.=%-x64.exe" "%URL%"
