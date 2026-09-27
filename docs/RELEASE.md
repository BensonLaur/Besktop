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

### 本地自愿支持卡片（2026-09-27）

小螃蟹菜单新增“支持作者”：默认显示微信原图，可切换支付宝，通过关闭按钮或“继续观看”返回舞台。扫码等待不因鼠标离开超时，不启动浏览器、不创建订单、不检测付款或解锁功能；`Esc` / `Ctrl+Shift+B` 仍直接退出。两张图片经作者明确授权公开，随源码存放在 `resources/support/` 并嵌入 EXE，无需旁置文件。

“支持作者”“这是啥？”及离站确认卡片的静态内容按当前布局缓存；仅首次显示、切换卡片/支付方式、DPI 或位置尺寸变化、资源重新加载时重绘缓存。小螃蟹、菜单淡入和后台角色仍实时绘制，不通过暂停动画缓解卡顿。缓存包含气泡尾巴、阴影和边框，场景重置时释放；构建失败退回直接绘制，不显示上一张收款图。

离线渲染测试 `besktop_stage_guide_support_render_tests` 覆盖资源解码、失败清理、缓存失效/复用、关闭无残留、动画未被缓存、二维码黑白码块一致性，以及四种屏幕/DPI 下的 20 组支付/介绍/确认卡片样片。传入输出目录可导出样片；传入 `--benchmark` 可在内存 HDC 上对比直接绘制与缓存绘制（均包含小螃蟹）的稳定帧耗时。该耗时不包含首次缓存构建或整个桌面舞台，不能当作整机帧率。正式发布前仍须验证双架构真实窗口交互和手机扫码，不沿用旧候选哈希或支付验收结论。

- 尚未执行代码签名。
- 尚未制作安装器。
- 尚未接入自动更新。
- 尚未创建 GitHub Release 或 tag。
- 当前产物已嵌入 Besktop 品牌图标。
- Windows Defender、SmartScreen 信誉、多台实体机器、不同 DPI/显卡和特殊 Shell 的兼容性仍需人工验证。

正式分发前应在干净机器上重新验证启动、`Esc`/`Ctrl+Shift+B` 退出，以及退出后真实桌面和任务栏保持原样。
