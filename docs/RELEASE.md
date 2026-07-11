# Besktop Core 发布构建

本文档记录公开 Core `0.1.0` 的 Windows 单 EXE 构建与验证方法。该流程只生成本地测试产物，不创建 GitHub Release、Git tag 或签名文件。

## 构建环境

- Windows 10/11 x64。
- Visual Studio 2022，安装“使用 C++ 的桌面开发”工作负载。
- CMake 3.24 或更高版本。
- Visual Studio Developer PowerShell，确保 `cl`、`nmake` 和 `dumpbin` 可从 `PATH` 访问。

## 一键构建

在仓库根目录的 Visual Studio Developer PowerShell 中运行：

```powershell
.\tools\build-release.ps1
```

脚本使用专用的 `build-release-package/` 目录配置 Release，执行干净重建，编译 GUI 和 Pack CLI，运行 Pack CLI，再生成：

```text
dist/Besktop.exe
dist/SHA256SUMS.txt
```

可按需指定目录或不生成哈希清单：

```powershell
.\tools\build-release.ps1 `
  -BuildDirectory .\build-release-local `
  -OutputDirectory .\dist-local

.\tools\build-release.ps1 -SkipHashFile
```

脚本不会执行 `git add`、提交、打 tag 或推送，也不会读取 Besktop-Plus。

## 产物验证

脚本会检查：

- `Besktop.exe` 存在、非空且文件名大小写正确。
- Pack CLI 成功返回。
- FileVersion 数值为 `0.1.0.0`，ProductVersion 表达 `0.1.0`。
- `dumpbin /dependents` 未发现 `VCRUNTIME`、`MSVCP` 或 `CONCRT` 外部运行库依赖。
- 输出目录没有项目 DLL。
- SHA-256 成功生成。

手工查看哈希：

```powershell
Get-FileHash .\dist\Besktop.exe -Algorithm SHA256
Get-Content .\dist\SHA256SUMS.txt
```

手工查看 Windows 版本信息：

```powershell
[System.Diagnostics.FileVersionInfo]::GetVersionInfo(
  (Resolve-Path .\dist\Besktop.exe)
) | Format-List FileDescription,ProductName,FileVersion,ProductVersion,OriginalFilename,InternalName,CompanyName,Comments
```

也可以在资源管理器中打开 `Besktop.exe` 的“属性 → 详细信息”。

## “单 EXE”的边界

当前 Release 使用 `/MT` 静态链接 MSVC C/C++ 运行库，并把免费基础 Pack 作为 RCDATA 嵌入 GUI。用户运行 Core 只需要 `Besktop.exe`；Windows 自带的系统 DLL 仍由操作系统提供，不会被静态打包进产物。

普通 Release 默认关闭 diagnostics，只有显式设置 `BESKTOP_ENABLE_DIAGNOSTICS=1` 后才会读取其他诊断环境变量。安全退出 `Esc` 和 `Ctrl+Shift+B` 始终保留。

## 当前发布边界

- 尚未执行代码签名。
- 尚未制作安装器。
- 尚未接入自动更新。
- 尚未创建 GitHub Release 或 tag。
- 最终品牌图标尚未设计，当前使用 Windows 默认应用图标。
- Windows Defender、SmartScreen 信誉、多台实体机器、不同 DPI/显卡和特殊 Shell 的兼容性仍需人工验证。

正式分发前应在干净机器上重新验证启动、`Esc`/`Ctrl+Shift+B` 退出，以及退出后真实桌面和任务栏保持原样。
