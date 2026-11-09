; ============================================================
; 7-Zip Modern Setup —— 基于 7-Zip 官方安装包的增强安装器
;
; 功能：
;   1. 静默安装官方 7-Zip（保留全部功能与右键菜单）
;   2. 安装时可视化勾选文件格式，装完自动写入文件关联
;   3. 全部注册表写入位于 HKCU，卸载时完整还原
;
; 版权说明：
;   内嵌的 7-Zip 本体版权归 Igor Pavlov 所有，遵循 LGPL/BSD
;   许可发布。本脚本只做安装流程增强，未修改 7-Zip 任何二进制。
; ============================================================

#define OfficialExe "7z2606-x64.exe"
#define AppVersion "26.06"
#define ModVersion "3"

[Setup]
AppName=7-Zip Modern
AppVersion={#AppVersion}.{#ModVersion}
AppVerName=7-Zip Modern {#AppVersion} (mod {#ModVersion})
AppPublisher=7-Zip Modern Project
AppPublisherURL=https://github.com/ip7z/7zip
AppSupportURL=https://github.com/ip7z/7zip
DefaultDirName={autopf}\7-Zip
DisableDirPage=yes
DisableProgramGroupPage=yes
WizardStyle=modern
OutputDir=output
OutputBaseFilename=7zip-modern-{#AppVersion}-x64-setup
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=7-Zip Modern {#AppVersion}
LicenseFile=..\LICENSES.txt
InfoBeforeFile=..\WELCOME.txt
Compression=lzma2
SolidCompression=yes
CloseApplications=yes

[Languages]
; 使用仓库内自带的中文语言包，保证任意构建环境（含 CI）都能编译
Name: "chinese"; MessagesFile: "Languages\ChineseSimplified.isl"

[Files]
; 官方安装包仅在安装过程中解压到临时目录，不直接落到目标机器
Source: "..\downloads\{#OfficialExe}"; Flags: dontcopy
; 7-Zip Modern 定制二进制（官方安装完成后覆盖，实现界面与右键菜单改造）
; 注意：必须在官方安装器运行（ssInstall）之后落盘，Inno 的文件复制顺序满足此要求
Source: "..\dist\7zFM.exe";   DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\7-zip.dll";  DestDir: "{app}"; Flags: ignoreversion restartreplace

[Run]
Filename: "{code:LaunchManager}"; Description: "打开 7-Zip 文件管理器"; Flags: nowait postinstall skipifsilent unchecked

[UninstallRun]
; 调用官方卸载程序，静默卸载 7-Zip 本体（含右键菜单壳扩展）
Filename: "{code:OfficialUninstaller}"; Parameters: "/S"; RunOnceId: "OfficialUninstall"

[Code]
// ------------------------------------------------------------
// 常量：支持的文件格式（扩展名 / 显示名 / 说明）
// ------------------------------------------------------------
var
  FormatBox: TNewCheckListBox;
  InstallPath: String;
  FormatExts: TArrayOfString;
  FormatNames: TArrayOfString;
  FormatCount: Integer;

// ------------------------------------------------------------
// 初始化格式表
// ------------------------------------------------------------
procedure InitFormats();
begin
  FormatCount := 16;
  SetArrayLength(FormatExts, FormatCount);
  SetArrayLength(FormatNames, FormatCount);

  FormatExts[0]  := '7z';   FormatNames[0]  := '7-Zip 压缩包';
  FormatExts[1]  := 'zip';  FormatNames[1]  := 'ZIP 压缩包';
  FormatExts[2]  := 'rar';  FormatNames[2]  := 'RAR 压缩包';
  FormatExts[3]  := 'tar';  FormatNames[3]  := 'TAR 打包文件';
  FormatExts[4]  := 'gz';   FormatNames[4]  := 'GZip 压缩包';
  FormatExts[5]  := 'bz2';  FormatNames[5]  := 'BZip2 压缩包';
  FormatExts[6]  := 'xz';   FormatNames[6]  := 'XZ 压缩包';
  FormatExts[7]  := 'zst';  FormatNames[7]  := 'Zstandard 压缩包';
  FormatExts[8]  := 'iso';  FormatNames[8]  := '光盘镜像';
  FormatExts[9]  := 'cab';  FormatNames[9]  := 'Windows 系统包';
  FormatExts[10] := 'wim';  FormatNames[10] := 'Windows 映像';
  FormatExts[11] := 'deb';  FormatNames[11] := 'Debian 软件包';
  FormatExts[12] := 'rpm';  FormatNames[12] := 'RedHat 软件包';
  FormatExts[13] := 'arj';  FormatNames[13] := 'ARJ 压缩包';
  FormatExts[14] := 'lzh';  FormatNames[14] := 'LZH 压缩包';
  FormatExts[15] := 'z';    FormatNames[15] := '传统压缩包';
end;

// ------------------------------------------------------------
// 读取官方 7-Zip 实际安装路径（安装器写入 HKLM\SOFTWARE\7-Zip\Path）
// ------------------------------------------------------------
function DetectInstallDir(): String;
begin
  Result := '';
  if not RegQueryStringValue(HKLM, 'SOFTWARE\7-Zip', 'Path', Result) then
    if not RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\7-Zip', 'Path', Result) then
      Result := '';
  if (Result = '') or (Pos('7-Zip', Result) = 0) then
    Result := ExpandConstant('{autopf}') + '\7-Zip';
  if Copy(Result, Length(Result), 1) <> '\' then
    Result := Result + '\';
end;

// ------------------------------------------------------------
// 自定义页面：文件格式勾选（默认全选，一看就懂）
// ------------------------------------------------------------
procedure CreateFormatPage();
var
  Page: TWizardPage;
  Hint: TNewStaticText;
  i: Integer;
begin
  Page := CreateCustomPage(wpLicense,
    '文件关联设置',
    '勾选希望双击直接用 7-Zip 打开的文件格式');

  Hint := TNewStaticText.Create(Page);
  Hint.Parent := Page.Surface;
  Hint.Caption :=
    '安装完成后将立即生效，无需手动去设置面板里点一遍。' + #13#10 +
    '之后随时可以在 7-Zip「工具 → 选项 → 系统集成」里调整。';
  Hint.WordWrap := True;
  Hint.Left := 0;
  Hint.Top := 0;
  Hint.Width := Page.Surface.Width;
  Hint.Height := ScaleY(34);

  FormatBox := TNewCheckListBox.Create(Page);
  FormatBox.Parent := Page.Surface;
  FormatBox.Left := 0;
  FormatBox.Top := Hint.Top + Hint.Height + ScaleY(8);
  FormatBox.Width := Page.Surface.Width;
  FormatBox.Height := Page.Surface.Height - FormatBox.Top - ScaleY(4);

  for i := 0 to FormatCount - 1 do
    FormatBox.AddCheckBox('.' + FormatExts[i] + '  ——  ' + FormatNames[i],
      '', 0, True, True, False, True, nil);
end;

procedure InitializeWizard();
begin
  InstallPath := '';
  InitFormats();
  CreateFormatPage();
end;

// ------------------------------------------------------------
// 安装第一步：静默运行官方安装器（保留右键菜单等全部功能）
// ------------------------------------------------------------
procedure CurStepChanged(CurStep: TSetupStep);
var
  TempExe, Cmd, Icon, ProgId: String;
  ResultCode: Integer;
  i: Integer;
begin
  if CurStep = ssInstall then
  begin
    ExtractTemporaryFile('{#OfficialExe}');
    TempExe := ExpandConstant('{tmp}\{#OfficialExe}');
    WizardForm.StatusLabel.Caption := '正在安装 7-Zip 本体（官方原版，静默模式）...';
    WizardForm.ProgressGauge.Style := npbstMarquee;
    try
      Exec(TempExe, '/S', '', SW_SHOW, ewWaitUntilTerminated, ResultCode);
    finally
      WizardForm.ProgressGauge.Style := npbstNormal;
    end;
    InstallPath := DetectInstallDir();
  end;

  if CurStep = ssPostInstall then
  begin
    WizardForm.StatusLabel.Caption := '正在写入文件关联...';
    for i := 0 to FormatCount - 1 do
    begin
      if not FormatBox.Checked[i] then
        Continue;

      ProgId := '7-Zip.' + FormatExts[i];
      Icon := InstallPath + '7zFM.exe,0';
      Cmd := '"' + InstallPath + '7zFM.exe" "%1"';

      // ProgID：显示名 / 图标 / 打开命令
      RegWriteStringValue(HKEY_CURRENT_USER,
        'Software\Classes\' + ProgId, '', '7-Zip ' + FormatNames[i]);
      RegWriteStringValue(HKEY_CURRENT_USER,
        'Software\Classes\' + ProgId + '\DefaultIcon', '', Icon);
      RegWriteStringValue(HKEY_CURRENT_USER,
        'Software\Classes\' + ProgId + '\shell\open\command', '', Cmd);

      // 扩展名指向 ProgID
      RegWriteStringValue(HKEY_CURRENT_USER,
        'Software\Classes\.' + FormatExts[i], '', ProgId);

      // 清掉旧的"用户选择"记录，让新关联立即生效
      RegDeleteKeyIncludingSubKeys(HKEY_CURRENT_USER,
        'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.' +
        FormatExts[i] + '\UserChoice');

      // 把 7-Zip 加进"打开方式"候选列表
      RegWriteStringValue(HKEY_CURRENT_USER,
        'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.' +
        FormatExts[i] + '\OpenWithProgids', ProgId, '');
    end;

    // 通知资源管理器刷新图标缓存
    try
      Exec(ExpandConstant('{cmd}'), '/C assoc 2>nul', '', SW_HIDE,
        ewWaitUntilTerminated, ResultCode);
    except
    end;
  end;
end;

// ------------------------------------------------------------
// [Run] / [UninstallRun] 辅助函数
// ------------------------------------------------------------
function LaunchManager(Param: String): String;
begin
  Result := InstallPath + '7zFM.exe';
end;

function OfficialUninstaller(Param: String): String;
var
  P: String;
begin
  P := DetectInstallDir() + 'Uninstall.exe';
  Result := P;
end;

// ------------------------------------------------------------
// 卸载：清理本安装器写过的关联（只清理确属 7-Zip 的项）
// ------------------------------------------------------------
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ProgId, CurVal: String;
  i: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    // 卸载进程不会执行 InitializeWizard，这里必须重新初始化格式表
    InitFormats();
    for i := 0 to FormatCount - 1 do
    begin
      ProgId := '7-Zip.' + FormatExts[i];

      // 只有当扩展名确实指向我们的 ProgID 时才清空，避免误伤用户后来的选择
      if RegQueryStringValue(HKEY_CURRENT_USER,
        'Software\Classes\.' + FormatExts[i], '', CurVal) then
        if CurVal = ProgId then
          RegDeleteValue(HKEY_CURRENT_USER,
            'Software\Classes\.' + FormatExts[i], '');

      RegDeleteKeyIncludingSubKeys(HKEY_CURRENT_USER,
        'Software\Classes\' + ProgId);
    end;
  end;
end;
