# 插件框架 MVP 验证说明

本文档记录当前 Besktop Core 侧已经跑通的插件框架 MVP。

## MVP 目标

本阶段不验证桌面舞台和动画效果，只验证插件框架链路：

- Core 能加载内置免费内容。
- Core 能从开发目录加载 Plus loose folder。
- Core 能根据 entitlement 判断 Plus 内容锁定或启用。
- Core 能读取由 Besktop-Plus 生成并嵌入 exe 的 Plus 内容资源。
- GUI 主程序和 CLI 使用同一套运行时。

## 已实现组件

```text
besktop_runtime
  JsonValue / ParseJson
  IPackProvider
  EmbeddedPackProvider
  DevFolderPackProvider
  PackLoader
  FeatureSetEntitlementService
  MVP report formatter

besktop_mvp_cli
  终端验证工具。

besktop
  GUI 壳，当前用 MessageBox 展示同一份 MVP report。
```

## 构建免费 Core MVP

当前在 Codex PowerShell 中，`cl.exe` 不在默认 PATH。使用 VS 环境脚本后再调用 CMake：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S D:\Projects\Benson\Besktop -B D:\Projects\Benson\Besktop\build-mvp-nmake -G "NMake Makefiles"'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build D:\Projects\Benson\Besktop\build-mvp-nmake --target besktop_mvp_cli'
```

验证内置免费包：

```powershell
D:\Projects\Benson\Besktop\build-mvp-nmake\besktop_mvp_cli.exe
```

期望结果：

```text
Loaded packs: 1
Icon Fight Basic
status: enabled
```

## 加载 Plus loose folder

不传授权：

```powershell
D:\Projects\Benson\Besktop\build-mvp-nmake\besktop_mvp_cli.exe --pack-dir D:\Projects\Benson\Besktop-Plus\packs
```

期望结果：

```text
Loaded packs: 2
Icon Fight Couple Pack
entitlement: locked
status: not-enabled
missing entitlement(s): plus.couple
```

传入 mock 授权：

```powershell
D:\Projects\Benson\Besktop\build-mvp-nmake\besktop_mvp_cli.exe --pack-dir D:\Projects\Benson\Besktop-Plus\packs --features plus.couple
```

期望结果：

```text
Icon Fight Couple Pack
entitlement: satisfied
status: enabled
```

## 加载内嵌 Plus 内容

先在 Besktop-Plus 生成 MVP pack 和 extra rc：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File D:\Projects\Benson\Besktop-Plus\tools\build-mvp-pack.ps1
```

再构建带 Plus 资源的 Core：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S D:\Projects\Benson\Besktop -B D:\Projects\Benson\Besktop\build-plus-mvp-nmake -G "NMake Makefiles" -DBESKTOP_EXTRA_RC="D:\Projects\Benson\Besktop-Plus\dist\generated\besktop_plus_packs.rc"'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build D:\Projects\Benson\Besktop\build-plus-mvp-nmake --target besktop_mvp_cli'
```

验证内嵌但未授权：

```powershell
D:\Projects\Benson\Besktop\build-plus-mvp-nmake\besktop_mvp_cli.exe
```

期望看到：

```text
Icon Fight Couple Pack
source: embedded:201
entitlement: locked
status: not-enabled
```

验证内嵌且授权：

```powershell
D:\Projects\Benson\Besktop\build-plus-mvp-nmake\besktop_mvp_cli.exe --features plus.couple
```

期望看到：

```text
Icon Fight Couple Pack
source: embedded:201
entitlement: satisfied
status: enabled
```

## 当前限制

- `.bpack` MVP 当前只打包 `manifest.json`，动作文件仍保留在 Plus 源目录，后续再扩展完整 PackBuilder。
- 签名验证当前是 MVP 占位逻辑：内嵌资源视为 trusted，dev folder 允许 unsigned。
- entitlement 当前通过 `--features` 模拟，尚未接入本地 signed license。
- 尚未实现桌面舞台、图标扫描、动画系统和真实 Plus 动作执行。
