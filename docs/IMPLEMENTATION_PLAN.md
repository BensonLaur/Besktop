# Besktop 感染演出模式整体实施计划

本文档是 Besktop Core 从当前 Pack MVP 进入“感染演出视觉 MVP”的实施总计划。它属于公开主仓库文档，只记录安全、克制、可公开的技术实现路线；更完整的传播语境和产品体验母版保留在 Besktop-Plus 私有仓库。

## 实施目标

第一阶段目标不是支付、授权或 Plus 内容，而是证明 Besktop 可以在 Windows 上自然呈现核心效果：

- 启动 `Besktop.exe` 后进入全屏桌面舞台。
- 第一帧尽量接近当前桌面。
- 图标文字先出现轻微异常。
- 桌面图标变成可动画的图标演员。
- 捕获到的全部有效桌面图标错峰觉醒并长出白色手脚。
- 觉醒后的图标在任务栏安全区之外自由漫游；拳击、侧踢、闪避、受击延后。
- 图标本体是固定尺寸的双面贴图薄片，能随动作产生 3D 翻转和摆动。
- 图标手脚是局部 3D 两段式骨架，而不是贴在图标平面上的简单线段。
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
- 图标图像通过 Shell 图像接口获取；失败时使用系统图标或内置占位图。
- 图标文字由 Besktop 自己绘制，不截图文字。
- 任务栏第一版在舞台窗口创建前截取一次静态图像，并按检测到的上、下、左、右边缘位置绘回；它不提供交互或实时刷新。
- 漫游安全区域使用主显示器 `rcWork` 映射到舞台客户区，不把固定底部任务栏高度作为正常路径；工作区域无效时保留集中式安全回退。
- 如果真实桌面采集失败，进入 demo actor 模式，仍显示可验证动画。

验收标准：

- 能在舞台中看到当前壁纸或明确的降级背景。
- 能显示若干真实桌面图标；采集失败时能显示 demo 图标。
- 图标文字由 Besktop 绘制，后续可以抖动、错位、淡出。

## 阶段 3：渲染基础

目标：让桌面舞台具备后续动画所需的渲染能力。

实现内容：

- 使用 D3D11 或当前 MVP 的 GDI+ 投影绘制图标贴图薄片。
- 图标贴图是固定尺寸的双面平面：正面和背面都显示同一个图标，背面不镜像。
- 图标贴图支持平移、`rotateX`、`rotateY`、`rotateZ` 和简单透视；动作不通过拉伸图标本体实现。
- 使用 Direct2D / DirectWrite 绘制文字、白色手脚、提示文案和调试辅助。
- 使用 WIC 加载壁纸、图标和任务栏截图。
- 第一帧尽量接近真实桌面，随后进入演出。

验收标准：

- 图标本体可以离开原位移动，而不会露出原始桌面图标重影。
- 图标文字可以独立抖动、错位或淡出。
- 至少一个图标能像纸片一样翻转，侧面对屏幕时接近一条窄线。
- 左转和右转时仍能看到正常方向的原图标，而不是空白背面或镜像图标。

## 阶段 4：图标演员和动作系统

目标：实现第一版 Icon Fight 的可读觉醒和漫游。动作系统先把“全部图标错峰醒来并像人一样自由走路”打稳，战斗动作留到后续阶段。

建议核心对象：

```text
IconActor
  position
  velocity
  iconTexture
  label
  bodyPlane
  bodyTransform
  local3DLimbs
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

- 当前视觉 MVP 可以先使用硬编码关键帧和程序化参数，但不要长期依赖零散 `sin` 公式拼动作。
- 优先实现 Locomotion MVP：全量演员化、错峰长手脚、随机目标漫游、停留、转身、脚落地和手脚反相摆动。
- 走路阶段使用两段 IK 解腿部，至少做到脚落地短暂锁定、摆动脚抬起、身体轻微 bob。
- 动作系统后续应迁移为 `AnimationClip + KeyPose + IKTarget + ContactEvent + RootMotion`。
- 第一版默认演出不包含攻击和命中特效；走路和转身稳定后，再规划拳击、转身侧踢、闪避、受击。
- 白色手脚使用两段式骨架：手臂为肩膀、肘、手；腿为胯、膝、脚。
- 肩膀和胯部位于图标薄片之外的局部 3D 空间，不直接贴在图标平面内。
- 所有关节先在局部 3D 中计算，再和图标薄片使用同一套投影绘制到屏幕。
- 拳击和侧踢也应使用手脚目标点和命中事件，不通过拉伸手脚或临时改线段长度实现。
- 被打到或撞到的闲逛图标进入打架状态。
- 群架逐步扩散，不要一开始让全部图标同时混乱。

验收标准：

- 至少两个图标长出白色手脚。
- 手脚能看出肘、膝两段结构，且在图标正对屏幕外时分布在窄线两侧。
- 走路时能看出左右脚交替、手脚反相、落地脚相对稳定。
- 转身时图标薄片能接近竖线，四肢仍从局部 3D 肩/胯锚点自然伸出。
- 能看到拳击、侧踢、闪避、受击。
- 被攻击图标会加入互动，形成扩散感。
- 动作节奏适合录屏，5 秒内能看懂效果。

## 阶段 5：与 Pack MVP 的衔接

当前视觉 MVP 允许硬编码动作和演出节奏。等核心效果成立后，再做以下迁移：

- 把基础动作数据迁移到内置免费 pack。
- 把走路、转身、拳击、侧踢、闪避、受击整理为轻量 `AnimationClip`。
- 在动作数据中保存关键姿态、IK 目标点、落地事件、命中事件和 root motion。
- 保持 PackLoader 作为统一入口。
- 为 Plus 内容预留签名包和授权开关，但不在本轮实现。
- 不把支付、授权私钥、客户数据或 Plus 私有资源写入 Core。

## 构建与验证

公开 Core `0.1.0` 的可分发测试产物使用 `tools/build-release.ps1` 生成。CMake 项目版本是 C++ 版本常量与 GUI Windows 版本资源的唯一来源；MSVC Release 使用 `/MT`，发布脚本会运行 Pack CLI、检查外部 MSVC 运行库依赖并输出 `dist/Besktop.exe` 和 SHA-256。详细步骤见 `docs/RELEASE.md`。

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

性能与动作调试可使用以下开关：

```powershell
$env:BESKTOP_FRAME_STATS='1'
$env:BESKTOP_FRAME_TRACE='1'
$env:BESKTOP_ANIMATION_SPEED='0.5'
$env:BESKTOP_ANIMATION_OFFSET='4.5'
$env:BESKTOP_MAX_ACTORS='10'
```

Debug 构建可直接使用这些开关。Release 构建必须先设置 `BESKTOP_ENABLE_DIAGNOSTICS=1`；未设置总开关时，Release 会忽略单项调试变量，并保持 `1.0x` 动画速度、`0` 秒偏移和低噪声 Warning/Error 日志。

- `BESKTOP_FRAME_STATS=1` 用于确认实际帧率、总绘制耗时，以及缓冲、静态背景、演员姿态、四肢、图标主体、文字和最终 BitBlt 的分阶段耗时。
- `BESKTOP_FRAME_TRACE=1` 用于定位首帧、壁纸缓存和渲染阶段问题。
- `BESKTOP_ANIMATION_SPEED` 用于慢放或加速动画，默认 `1.0`。
- `BESKTOP_ANIMATION_OFFSET` 用于从指定动画秒数开始，减少等待左走、右走、转身、出拳等阶段的时间。
- `BESKTOP_MAX_ACTORS` 用于限制本次演出的演员数量，便于对比不同图标规模的帧率；未设置或设为 `0` 时保持全部觉醒。

新增动作或视觉效果前后必须按 [RENDER_PERFORMANCE.md](RENDER_PERFORMANCE.md) 复测全量演员和小规模对照组，记录稳定帧、动作高峰帧、分阶段耗时和资源稳定性。

## 第一版默认取舍

- 只支持主显示器。
- 覆盖任务栏。
- 任务栏使用静态截图复刻。
- 真实图标采集失败时允许 demo 模式。
- 动作先硬编码验证，随后迁移为 `AnimationClip + IK + ContactEvent` 数据化模型。
- 公开文档使用“安全桌面演出”“桌面舞台”“图标演员”等表达。
