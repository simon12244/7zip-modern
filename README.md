# 7-Zip Modern

> 安装即关联 —— 让 7-Zip 装完就能双击打开压缩包

## 这是什么

基于 **官方 7-Zip 原版安装包** 的增强安装器（不修改 7-Zip 任何二进制文件）。

官方 7-Zip 安装完后，想双击打开 `.zip` / `.rar` / `.tar.gz` 等文件，还得去
「工具 → 选项 → 系统集成」里一个一个勾。本项目把它变成一步：

1. **可视化格式勾选页**：安装时直接勾选要关联的格式，默认全选
2. **自动写入文件关联**：装完立即生效，双击压缩包直接用 7-Zip 打开
3. **功能零损失**：内嵌官方原版，压缩核心、右键菜单（解压到 / 压缩到
   / 测试等）完整保留
4. **干净卸载**：所有关联写在用户注册表（HKCU），卸载时自动还原

## 支持关联的格式

`.7z` `.zip` `.rar` `.tar` `.gz` `.bz2` `.xz` `.zst` `.iso` `.cab`
`.wim` `.deb` `.rpm` `.arj` `.lzh` `.z`

## 构建

```
scripts\build.cmd
```

前置条件：安装 [Inno Setup 7](https://jrsoftware.org/isinfo.php)，
并把 `ISCC.exe` 所在目录加入 PATH，或修改脚本中的路径。

产出：`installer\output\7zip-modern-<版本>-x64-setup.exe`

## 内嵌官方版本

当前内嵌 **7-Zip 26.06**（2026-11-06 发布，官方原版未修改）。
更新版本：下载新的 `7zXXX-x64.exe` 放入 `downloads\`，并修改
`installer\7zip-modern.iss` 顶部的 `OfficialExe` 与版本号定义。

## 目录结构

```
7zip-modern/
├── installer/
│   ├── 7zip-modern.iss      # Inno Setup 脚本（核心逻辑都在这里）
│   └── output/              # 构建产物
├── downloads/               # 官方安装包（构建时自动内嵌）
├── scripts/build.cmd        # 一键构建脚本
├── LICENSES.txt             # 许可说明（安装时展示）
└── WELCOME.txt              # 安装器欢迎页文案
```

## 技术细节（给关心注册表的同学）

关联写入位置（全部用户级，无需动系统目录）：

| 项 | 路径 |
|---|---|
| ProgID | `HKCU\Software\Classes\7-Zip.<ext>` |
| 图标 | `...\7-Zip.<ext>\DefaultIcon` → `7zFM.exe,0` |
| 打开命令 | `...\7-Zip.<ext>\shell\open\command` → `"...\7zFM.exe" "%1"` |
| 扩展名指向 | `HKCU\Software\Classes\.<ext>` → `7-Zip.<ext>` |
| 清除旧选择 | 删除 `FileExts\.<ext>\UserChoice` |
| 打开方式候选 | `FileExts\.<ext>\OpenWithProgids` |

> 说明：Windows 10/11 的 UserChoice 是哈希保护的，程序无法直接伪造。
> 本安装器的做法是删除旧的 UserChoice 记录并写入 Classes 默认值，
> 绝大多数场景立即生效；若格式曾被用户明确指定给其他软件，
> Windows 可能在下次双击时弹一次确认框。

## 许可

- 7-Zip 本体：Copyright (C) Igor Pavlov，GNU LGPL（部分组件 BSD /
  unRAR 限制条款）
- 本安装脚本：GNU LGPL，与上游保持一致

详见 [LICENSES.txt](LICENSES.txt)。
