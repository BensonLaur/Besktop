# Besktop Core 发布构建

本文档记录公开 Core `0.1.0` 的 Windows 单 EXE 构建与验证方法。该流程只生成本地测试产物，不创建 GitHub Release、Git tag 或签名文件。

## 构建环境

- Windows 10/11 x64。
- Visual Studio 2022，安装“使用 C++ 的桌面开发”工作负载。
- CMake 3.24 或更高版本。
- PowerShell 5.1 或更高版本。脚本通过 Visual Studio 安装信息分别初始化 x64/Win32 MSVC 环境，不依赖当前终端的编译器位宽；`dumpbin` 不在 `PATH` 时也会从 Visual Studio 中定位。

## 一键构建

在仓库根目录的 PowerShell 中运行：

```powershell
.\tools\build-release.ps1 -Architecture All
```

脚本默认构建全部架构，也可用 `-Architecture x64` 或 `-Architecture Win32` 单独构建。两个架构分别使用 `build-release-package-x64/` 和 `build-release-package-win32/`，不会复用 CMake 缓存。脚本使用各自的 `vcvarsall.bat` 环境和 NMake 执行干净重建，编译 GUI 和 Pack CLI，运行 Pack CLI，再生成：

```text
dist/Besktop.exe
dist/Besktop-win32.exe
dist/SHA256SUMS.txt
```

可按需指定目录或不生成哈希清单：

```powershell
.\tools\build-release.ps1 -Architecture x64
.\tools\build-release.ps1 -Architecture Win32

.\tools\build-release.ps1 -Architecture All `
  -BuildDirectory .\build-release-local `
  -OutputDirectory .\dist-local

.\tools\build-release.ps1 -SkipHashFile
```

脚本不会执行 `git add`、提交、打 tag 或推送，也不会读取 Besktop-Plus。

## 产物验证

脚本会检查：

- 两个 EXE 存在、非空且文件名准确；PE 机器类型分别为 x64 与 x86。
- Pack CLI 成功返回。
- FileVersion 数值为 `0.1.0.0`，ProductVersion 表达 `0.1.0`。
- 应用图标资源已嵌入。
- `dumpbin /dependents` 未发现 `VCRUNTIME`、`MSVCP` 或 `CONCRT` 外部运行库依赖。
- 输出目录没有项目 DLL。
- SHA-256 成功生成。

手工查看哈希：

```powershell
Get-FileHash .\dist\Besktop*.exe -Algorithm SHA256
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

当前 Release 使用 `/MT` 静态链接 MSVC C/C++ 运行库，并把免费基础 Pack 作为 RCDATA 嵌入 GUI。普通 64 位 Windows 用户应下载 `Besktop.exe`；只有 32 位 Windows 用户下载 `Besktop-win32.exe`。它们是同一个 Besktop 的架构兼容产物，用户只运行与系统匹配的一个独立 EXE，均不需要另装 VC Runtime。

普通 Release 默认关闭 diagnostics，只有显式设置 `BESKTOP_ENABLE_DIAGNOSTICS=1` 后才会读取其他诊断环境变量。安全退出 `Esc` 和 `Ctrl+Shift+B` 始终保留。

## 当前发布边界

- 尚未执行代码签名。
- 尚未制作安装器。
- 尚未接入自动更新。
- 尚未创建 GitHub Release 或 tag。
- 当前产物已嵌入 Besktop 品牌图标。
- Windows Defender、SmartScreen 信誉、多台实体机器、不同 DPI/显卡和特殊 Shell 的兼容性仍需人工验证。

正式分发前应在干净机器上重新验证启动、`Esc`/`Ctrl+Shift+B` 退出，以及退出后真实桌面和任务栏保持原样。
