# Besktop 感染演出模式整体实施计划

本文档是 Besktop Core 从当前 Pack MVP 进入“感染演出视觉 MVP”的实施总计划。它属于公开主仓库文档，只记录安全、克制、可公开的技术实现路线；更完整的传播语境和产品体验母版保留在 Besktop-Plus 私有仓库。

## 实施目标

第一阶段目标不是支付、授权或 Plus 内容，而是证明 Besktop 可以在 Windows 上自然呈现核心效果：

- 启动 `Besktop.exe` 后进入全屏桌面舞台。
- 第一帧尽量接近当前桌面。
- 图标文字先出现轻微异常。
- 桌面图标变成可动画的图标演员。
- 至少两个图标长出白色手脚并开始互动。
- 能看到拳击、转身侧踢、闪避、受击。
- 图标本体有明显 2.5D 摆动。
- `Esc` 能稳定退出，退出后真实桌面文件无变化。

## 当前基础

当前仓库已经跑通 Pack MVP：

- `besktop_runtime` 能加载内置免费包和开发期 Plus 包。
- `besktop_mvp_cli` 用于验证包加载链路。
- GUI `besktop` 当前仍是 MessageBox 壳。

后续实现必须保留 `besktop_mvp_cli`，不要拆掉现有 Pack MVP。视觉 MVP 可以先硬编码基础动作，等效果成立后再逐步迁移到内置免费玩法包。

## 阶段 1：应用壳和安全退出

目标：把 GUI `besktop` 从 MessageBox 变成真正的 Win32 应用壳。

实现内容：

- 创建全屏无边框窗口。
- 第一版只覆盖主显示器。
- 窗口覆盖任务栏，用于完整桌面舞台演出。
- 支持 `Esc` 立即退出。
- 预留 `Ctrl + Shift + B` 强制退出热键。
- 保留系统级退出方式，不禁用 Alt+Tab，不阻止任务管理器。
- 不自启动、不后台驻留、不提权。

验收标准：

- 双击 `Besktop.exe` 能显示全屏窗口。
- `Esc` 能稳定关闭窗口并退出进程。
- 关闭后桌面文件、图标位置和 Explorer 状态没有变化。

## 阶段 2：桌面舞台快照

目标：建立运行时使用的桌面舞台数据，不直接操作真实桌面元素。

建议数据模型：

```text
DesktopSnapshot
  monitorBounds
  wallpaper
  taskbarImage
  icons[]

DesktopIconSnapshot
  id
  displayName
  screenRect
  iconImage
  fallbackKind
```

实现内容：

- 壁纸优先通过 `IDesktopWallpaper` 获取路径和显示方式。
- 在桌面舞台中按同样规则重绘壁纸。
- 图标目标方案是通过 Shell View / `IFolderView` 获取位置、名称和图像。
- 当前视觉 MVP 先通过桌面 `SysListView32` 只读采样获取位置和名称。
- 图标图像后续通过 Shell 图像接口获取；当前先使用抽象演员贴图。
- 图标文字由 Besktop 自己绘制，不截图文字。
- 任务栏第一版可以截取静态图像并绘制在舞台底部。
- 如果真实桌面采集失败，进入 demo actor 模式，仍显示可验证动画。

验收标准：

- 能在舞台中看到当前壁纸或明确的降级背景。
- 能显示若干真实桌面图标；采集失败时能显示 demo 图标。
- 图标文字由 Besktop 绘制，后续可以抖动、错位、淡出。

## 阶段 3：渲染基础

目标：让桌面舞台具备后续动画所需的渲染能力。

实现内容：

- 使用 D3D11 绘制图标贴图 quad。
- 图标贴图支持平移、缩放、`rotateX`、`rotateY`、`rotateZ` 和简单透视。
- 使用 Direct2D / DirectWrite 绘制文字、白色手脚、提示文案和调试辅助。
- 使用 WIC 加载壁纸、图标和任务栏截图。
- 第一帧尽量接近真实桌面，随后进入演出。

验收标准：

- 图标本体可以离开原位移动，而不会露出原始桌面图标重影。
- 图标文字可以独立抖动、错位或淡出。
- 至少一个图标能出现明显 2.5D 摆动。

## 阶段 4：图标演员和动作系统

目标：实现第一版 Icon Fight 的可读动作。

建议核心对象：

```text
IconActor
  position
  velocity
  iconTexture
  label
  bodyTransform
  limbPose
  state
  target
```

建议状态机：

```text
Sleeping
TextShaking
Awakening
GrowingLimbs
Wandering
SeekingTarget
Attacking
Dodging
HitReact
Knockdown
Recovering
```

实现内容：

- 第一版动作使用硬编码关键帧。
- 实现拳击、转身侧踢、闪避、受击四类核心动作。
- 白色手脚先用简单线段或胶囊体绘制。
- 被打到或撞到的闲逛图标进入打架状态。
- 群架逐步扩散，不要一开始让全部图标同时混乱。

验收标准：

- 至少两个图标长出白色手脚。
- 能看到拳击、侧踢、闪避、受击。
- 被攻击图标会加入互动，形成扩散感。
- 动作节奏适合录屏，5 秒内能看懂效果。

## 阶段 5：与 Pack MVP 的衔接

当前视觉 MVP 允许硬编码动作和演出节奏。等核心效果成立后，再做以下迁移：

- 把基础动作数据迁移到内置免费 pack。
- 保持 PackLoader 作为统一入口。
- 为 Plus 内容预留签名包和授权开关，但不在本轮实现。
- 不把支付、授权私钥、客户数据或 Plus 私有资源写入 Core。

## 构建与验证

构建路径沿用当前 VS2022 + CMake + NMake 流程：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S D:\Projects\Benson\Besktop -B D:\Projects\Benson\Besktop\build-mvp-nmake -G "NMake Makefiles"'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build D:\Projects\Benson\Besktop\build-mvp-nmake --target besktop'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build D:\Projects\Benson\Besktop\build-mvp-nmake --target besktop_mvp_cli'
```

每次阶段性提交前检查：

```powershell
git status --short
git diff --check
```

同时确认：

- `besktop_mvp_cli` 的现有包加载行为不被破坏。
- 没有误提交 Plus 私有文案、授权逻辑、密钥或商业资源。
- 没有实现自启动、后台驻留、提权或真实桌面文件修改。

## 第一版默认取舍

- 只支持主显示器。
- 覆盖任务栏。
- 任务栏使用静态截图复刻。
- 真实图标采集失败时允许 demo 模式。
- 动作先硬编码，后续再数据化。
- 公开文档使用“安全桌面演出”“桌面舞台”“图标演员”等表达。
