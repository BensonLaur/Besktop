# Besktop 技术方案草案

本文档用于在实现前打磨技术路线。当前阶段以确认边界、降低风险、保留扩展性为主，不急于写完整功能。

## 技术目标

Besktop Core 需要满足几个关键目标：

- 体积小，方便用户下载、转发和尝试。
- 启动快，退出明确，行为透明。
- 原生 Windows 体验，不依赖浏览器壳或大型运行时。
- 对杀毒软件友好，不做隐藏驻留或静默自启动。
- 只在 Besktop 自己的全屏桌面舞台中演出，不破坏真实桌面文件。
- 免费核心体验完整，Plus 只扩展社交和个性化乐趣。
- 第一版发布物优先为单个 `Besktop.exe`，方便传播和试用。

## 总体架构

```text
app/
  BesktopApp
  Settings
  TrayController
  HotkeyController

desktop/
  DesktopSnapshot
  WallpaperSnapshot
  DesktopIconScanner
  IconSnapshot
  IconTextureCache
  TaskbarSnapshot

render/
  StageWindow
  D3DDevice
  D2DContext
  SpriteRenderer
  LimbRenderer

animation/
  IconActor
  ActionClip
  ActionStateMachine
  CombatDirector

packs/
  ActionPackManifest
  ActionPackLoader
  PackSignatureVerifier
```

这里的 `packs/` 是工程概念，不代表第一版必须向用户暴露外部包目录。免费内容和 Plus 内容都可以在构建阶段嵌入 exe，运行时仍通过同一套 `ActionPackLoader` 以“包”的形式加载。

## 桌面舞台与渲染

第一版采用全屏桌面重建舞台，而不是直接在真实桌面上叠加透明动画。

- 使用 Win32 创建全屏无边框窗口。
- 第一版只覆盖主显示器，并覆盖任务栏。
- 启动时读取桌面背景、图标位置、图标名称、图标图像和任务栏静态视觉。
- 在 Besktop 窗口中重新绘制一份桌面舞台。
- 真实桌面始终留在下面，不修改、不移动、不隐藏真实桌面文件。
- `Esc` 立即退出，预留 `Ctrl + Shift + B` 强制退出热键。
- 使用 Direct3D 11 绘制图标贴图、粒子和 2.5D 变换。
- 使用 Direct2D 绘制白色手脚、线条、简易 UI 和调试辅助。
- 使用 DirectWrite 绘制图标文字和提示文案。
- 使用 WIC 加载壁纸、图标和任务栏截图。

这样做的原因是：如果只使用透明覆盖层，真实图标仍留在原处，动画图标移动后会露出重影；如果直接使用整屏截图当背景，截图里也会包含原始图标。桌面舞台需要分离重建壁纸、图标、文字和任务栏，才能让图标演员真正离开原位。

图标本体不是直接操作 Explorer 里的真实图标，而是采集图标外观后在舞台中绘制一个“演员”。这样才能做旋转、倾斜、缩放、受击和 3D 方向摆动。

## 图标采集策略

首选路径：

1. 使用 `IDesktopWallpaper` 获取当前显示器壁纸路径和显示方式。
2. 目标路径是通过 Shell View / `IFolderView` 读取 Explorer 桌面图标视图。
3. 获取可见图标的位置、名称和图标图像。
4. 由 Besktop 自己绘制图标文字，而不是截图文字。
5. 截取任务栏区域作为第一版静态视觉。
6. 为每个图标创建 `IconActor`。

当前视觉 MVP 的落地路径：

1. 壁纸仍通过 `IDesktopWallpaper` 获取。
2. 图标位置和名称先通过桌面 `SysListView32` 只读采样获取。
3. 图标图像暂时使用抽象演员贴图，后续再接入 Shell 图像接口。
4. 采样失败时继续进入 demo actor 模式，保证动画可见。

容错路径：

1. 如果壁纸读取失败，使用纯色或内置演示背景。
2. 如果真实桌面图标位置读取失败，使用桌面快捷方式生成近似网格。
3. 如果图标图像读取失败，使用系统图标或内置占位图。
4. 如果桌面扫描完全失败，进入 demo actor 模式，保证用户仍能看到效果。

## 动作系统

第一版优先使用手写关键帧，而不是引入复杂动画引擎。

每个 `IconActor` 维护：

- `screenPosition`：舞台坐标。
- `iconTexture`：图标贴图。
- `bodyTransform`：平移、缩放、旋转、倾斜、透视感参数。
- `limbPose`：手脚关键点。
- `state`：静止、文字抖动、醒来、长手脚、闲逛、寻敌、出拳、侧踢、闪避、受击、倒下、恢复。

动作数据未来可以从 JSON 或紧凑二进制资源加载，方便免费包和 Plus 包共用同一套运行时。

第一版推荐把基础动作数据作为内置资源嵌入 `Besktop.exe`。Plus 内容如果随商业版一起发布，也优先在 Besktop-Plus 的打包阶段压缩、签名后嵌入最终 exe，而不是要求用户手动管理外部包文件。

## Plus 扩展边界

Core 可以知道“玩法包”和“授权状态”的概念，但不能包含支付密钥、私钥、客户数据或私有资源。

建议边界：

- Core 负责加载免费包和验证签名包。
- Plus 负责生成、签名、嵌入或分发付费内容。
- Core 内置公钥，Plus 服务端持有私钥。
- 未授权时，Core 仍然提供完整基础玩法。

## 发布形态

优先发布形态：

```text
Besktop.exe
  Core 引擎
  免费 Icon Fight 内容
  Plus 内容容器
  授权验证客户端
```

用户下载和传播的是一个 exe。内部可以有包、插件、资源段或追加数据，但这些属于实现细节。

可选的后续形态：

- 单 exe 内置所有首批 Plus 内容，适合第一版传播。
- exe 加在线下载签名 Plus 包，适合后续内容频繁更新。
- exe 加外部 `packs/` 目录，仅在用户确实需要管理大量内容时考虑。

## 第一阶段里程碑

实现阶段先做感染演出视觉 MVP。

1. 建立 Win32 全屏无边框 `StageWindow`。
2. 支持 `Esc` 退出和强制退出热键。
3. 建立 `DesktopSnapshot` 数据模型。
4. 重绘壁纸、图标、图标文字和任务栏静态视觉。
5. 绘制测试图标演员。
6. 增加白色手脚和基础动作关键帧。
7. 实现拳击、转身侧踢、闪避、受击。
8. 接入真实桌面图标采集，并保留 demo 降级模式。

## 待验证问题

- Explorer 桌面图标位置读取在多显示器、高 DPI、不同 Windows 版本下的稳定性。
- `IDesktopWallpaper` 对多显示器、跨区壁纸和不同显示模式的还原准确度。
- 全屏桌面舞台与全屏程序、游戏、远程桌面、屏幕录制软件的兼容性。
- Direct3D 11 与 Direct2D 混合渲染的复杂度和体积影响。
- 杀毒软件对桌面扫描、全屏窗口、任务栏截图和热键行为的敏感度。
- 付费包签名机制是否需要从第一版 Core 就预留。
- 单 exe 内嵌 Plus 内容时的体积、更新便利性和逆向提取风险。
