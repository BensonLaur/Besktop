<p align="center">
  <img src="resources/app/besktop-icon-master.png" alt="Besktop Logo" width="180">
</p>

# Besktop

> 我的电脑“中毒”了：桌面图标开始打架。
> 不是病毒，不删文件，只是一个让桌面图标开打的 Windows 桌面整活工具。

Besktop 的首个玩法叫 **Icon Fight**：它会把你的桌面临时变成一个安全的全屏舞台，让全部桌面图标像小角色一样错峰醒来、长出白色手脚并自由闲逛。打架动作将在后续版本加入。

## 运行效果

<table>
  <tr>
    <th width="50%">启动前：原始桌面</th>
    <th width="50%">启动后：图标全部醒来</th>
  </tr>
  <tr>
    <td><img src="docs/images/desktop-before.webp" alt="运行 Besktop 前的原始 Windows 桌面"></td>
    <td><img src="docs/images/desktop-awakened.webp" alt="运行 Besktop 后桌面图标长出手脚并自由漫游"></td>
  </tr>
</table>

所有动画都发生在 Besktop 重建的全屏舞台中；退出后，真实桌面仍保持原样。

舞台会在全屏窗口出现前一次性捕获主显示器任务栏，并把静态画面绘回原位置；它不是可交互的任务栏，也不会持续刷新时钟或托盘状态。图标漫游使用系统报告的工作区域避开任务栏。当前重点验证主显示器与 Windows 默认 Shell，尚不承诺完整多显示器和第三方 Shell 兼容性。

目标很简单：录屏 5 秒，朋友看完问一句：

```text
这个在哪下载？
```

当前阶段：**v0.1.0 RC**。桌面舞台、真实图标采集、图标演员、基础动作和安全退出已经落地，正在完成首个公开下载候选版本的兼容性与发布验收，尚未正式发布。

## 它会发生什么？

计划中的第一版体验：

1. 双击运行 `Besktop.exe`。
2. 当前桌面看起来一切正常。
3. 全部图标保持真实原位，并开始错峰觉醒。
4. 每个图标在原位长出简洁的白色手脚。
5. 觉醒后的图标离开原位，在桌面安全区域自由闲逛。
6. 按 `Esc` 退出，真实桌面恢复原样。

## 为什么有趣？

因为它把很严肃、很日常的 Windows 桌面变成了一个突然失控的小剧场。

不是两个陌生角色在打架，而是你的浏览器、聊天软件、游戏、文件夹、快捷方式突然活了。第一眼像电脑出事了，第二眼发现它只是太会整活。

Besktop 想抓住的就是这种反差：

```text
哪有这种病毒啊哈哈哈哈。
```

## 安全边界

Besktop 不是真病毒。

- 不删除文件。
- 不移动真实桌面图标。
- 不修改 Explorer。
- 不做自动开机启动。
- 不后台驻留。
- 不提权。
- 按 `Esc` 退出。

动画发生在 Besktop 自己重建的安全桌面舞台中。真实桌面始终留在下面，不被修改。

## 首个玩法：Icon Fight

Icon Fight 的目标不是做复杂游戏，而是做一个“看一眼就想转发”的桌面小惊喜。

计划中的基础效果：

- 图标文字先出现异常。
- 图标长出简洁的白色手脚。
- 图标本体像一张双面小薄片一样翻转、侧身和摆动，左转右转都能认出原来的图标。
- 白色手脚像长在图标薄片外侧的小骨架，而不是贴在图标表面的线条。
- 动作系统先打磨走路、转身、脚落地和手脚反相摆动，再扩展拳击、侧踢、闪避和受击。
- 角色之间可以靠近、出拳、转身侧踢、闪避、受击和恢复。
- 被攻击的图标也会加入打架，形成逐步扩散的桌面群架。
- 动作轻量、夸张、短循环，适合录屏传播。

## 当前状态

本仓库目前已经完成：

- 项目定位、开源边界和商业边界文档。
- Windows 原生技术路线。
- 感染演出模式实施计划。
- 最小玩法包加载 MVP。

下一步继续打磨漫游视觉、任务栏复刻和兼容性；拳击、侧踢、闪避、受击等打架动作延后。

## 技术方向

初步推荐技术栈：

- C++20
- Win32
- Direct3D 11
- Direct2D / DirectWrite
- Windows Imaging Component
- CMake + Visual Studio 2022

## 调试开关

Debug 构建默认允许开发者通过环境变量开启诊断能力：

```powershell
$env:BESKTOP_FRAME_STATS='1'      # 每秒记录 paint fps / timer fps / paint avg/max
$env:BESKTOP_FRAME_TRACE='1'      # 记录首帧和壁纸缓存的分段 trace
$env:BESKTOP_ANIMATION_SPEED='0.5' # 0.5 倍速慢放观察动作，默认 1.0
$env:BESKTOP_ANIMATION_OFFSET='4.5' # 从动画第 4.5 秒开始，便于直接观察某个动作阶段
$env:BESKTOP_DEBUG_ICON_PLANE='1'   # 显示图标薄片调试边框
$env:BESKTOP_RENDER_SHADOWS='1'     # 显示开发期阴影效果
$env:BESKTOP_MAX_ACTORS='10'        # 仅创建前 10 个演员；未设置或设为 0 时全部觉醒
```

普通 Release 构建会忽略上述单项变量，并固定使用 `1.0x` 动画速度和 `0` 秒偏移。需要现场诊断时，必须先显式设置总开关，再设置所需单项：

```powershell
$env:BESKTOP_ENABLE_DIAGNOSTICS='1'
$env:BESKTOP_FRAME_TRACE='1'
```

Debug 或已启用诊断的 Release 会记录详细 Info 日志；普通 Release 默认只记录 Warning/Error，且不会仅因正常启动创建日志。这些开关只用于调试，不改变真实桌面，不属于用户可见玩法设置。

关键文档：

- 技术方案：[docs/TECHNICAL_PLAN.md](docs/TECHNICAL_PLAN.md)
- 实施计划：[docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md)
- 仓库定位和发布模型：[docs/REPOSITORY_AND_RELEASE_MODEL.md](docs/REPOSITORY_AND_RELEASE_MODEL.md)
- Core 单 EXE 发布构建：[docs/RELEASE.md](docs/RELEASE.md)
- v0.1.0 发布说明候选稿：[docs/RELEASE_NOTES_v0.1.0.md](docs/RELEASE_NOTES_v0.1.0.md)
- v0.1.0 发布候选验收清单：[docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md)
- 插件框架 MVP：[docs/MVP_PLUGIN_FRAMEWORK.md](docs/MVP_PLUGIN_FRAMEWORK.md)

## 仓库关系

```text
Besktop
  开源核心仓库，GPL-3.0。
  负责主程序、基础引擎、免费玩法、公开文档和通用扩展接口。

Besktop-Plus
  私有商业仓库。
  负责付费玩法内容、支付授权、商业发布编排和私有资源。
```

用户层面的发布主体始终是 **Besktop**。第一版优先追求“一个 `Besktop.exe` 下载后即可运行”的传播形态。

## 当前不做什么

- 不接入真实支付。
- 不实现授权系统。
- 不发布第二个 Plus 软件。
- 不做自动开机启动。
- 不修改或移动真实桌面文件。
- 不引入大型游戏引擎。
- 暂不接受代码 PR。

## 开源与商业边界

Besktop Core 采用 GPL-3.0。作者保留将自己编写的核心代码用于 Besktop Plus 商业分发的权利。

早期不接受代码贡献，主要原因是项目未来可能需要双授权和清晰版权边界。欢迎 issue、使用反馈、动作创意、命名建议和兼容性报告。

详细边界见 [docs/OPEN_CORE_BOUNDARY.md](docs/OPEN_CORE_BOUNDARY.md)。
