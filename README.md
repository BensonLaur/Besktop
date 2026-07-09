# Besktop

> Let your desktop icons fight.

Besktop 是 Benson 的 Windows 桌面娱乐工具。首个玩法名为 **Icon Fight**，中文可称为“桌斗 / 图标开打 / 桌面图标打架”：让桌面图标像小角色一样活动、闪躲、出拳、侧踢，在安全的桌面舞台中演出一段轻量、可分享的互动动画。

当前阶段：**感染演出视觉 MVP 实施准备期**。产品边界、技术路线和实施总计划已经落地，下一步优先实现桌面舞台、图标演员、基础动作和安全退出。

## 项目定位

Besktop Core 是开源核心仓库，目标是提供一个完整、好玩、容易传播的免费体验，而不是把核心乐趣拆走后再收费。

核心体验包括：

- 重建一份安全的全屏桌面舞台，不破坏真实桌面文件。
- 读取或模拟桌面图标位置，将图标表现为可动角色。
- 提供基础的待机、移动、出拳、踢腿、闪避、受击、恢复动作。
- 提供免费基础玩法和自愿打赏入口。
- 为后续 Plus 付费内容保留清晰扩展边界。

## 仓库关系

```text
Besktop
  开源核心仓库，GPL-3.0。
  负责主程序、基础引擎、免费玩法、公开文档和通用扩展接口。

Besktop-Plus
  私有商业仓库。
  负责付费玩法内容、支付授权、商业发布编排和私有资源。
```

用户层面的发布主体始终是 **Besktop**。第一版优先追求“一个 `Besktop.exe` 下载后即可运行”的传播形态。Plus 可以在工程上被设计成插件或玩法包，但第一版不要求用户看到外部 `packs/` 目录；Plus 内容可以在商业打包阶段嵌入最终 exe，并通过授权解锁。

## 首个玩法：Icon Fight

Icon Fight 的目标不是做复杂游戏，而是做一个“看一眼就想转发”的桌面小惊喜。

计划中的基础效果：

- 图标长出简洁的白色手脚。
- 图标本体随动作产生倾斜、摆动、压缩、回弹等 2.5D 变化。
- 角色之间可以靠近、出拳、转身侧踢、闪避、受击和恢复。
- 动作轻量、夸张、短循环，适合录屏传播。

## 技术方向

初步推荐技术栈：

- C++20
- Win32
- Direct3D 11
- Direct2D / DirectWrite
- Windows Imaging Component
- CMake + Visual Studio 2022

技术方案详见 [docs/TECHNICAL_PLAN.md](docs/TECHNICAL_PLAN.md)。

感染演出模式实施计划详见 [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md)。

仓库定位和发布模型详见 [docs/REPOSITORY_AND_RELEASE_MODEL.md](docs/REPOSITORY_AND_RELEASE_MODEL.md)。

插件框架 MVP 验证说明见 [docs/MVP_PLUGIN_FRAMEWORK.md](docs/MVP_PLUGIN_FRAMEWORK.md)。

## 当前不做什么

在方案稳定前，先不急着实现复杂功能：

- 不接入真实支付。
- 不实现授权系统。
- 不发布第二个 Plus 软件。
- 不做自动开机启动。
- 不修改或移动真实桌面文件。
- 不引入大型游戏引擎。
- 不接受代码 PR。

## 开源与商业边界

Besktop Core 采用 GPL-3.0。作者保留将自己编写的核心代码用于 Besktop Plus 商业分发的权利。

早期不接受代码贡献，主要原因是项目未来可能需要双授权和清晰版权边界。欢迎 issue、使用反馈、动作创意、命名建议和兼容性报告。

详细边界见 [docs/OPEN_CORE_BOUNDARY.md](docs/OPEN_CORE_BOUNDARY.md)。
