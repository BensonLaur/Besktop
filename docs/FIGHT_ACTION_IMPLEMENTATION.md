# Icon Fight 第一轮动作系统实施计划

本文档把 [FIGHT_ACTION_CATALOG.md](FIGHT_ACTION_CATALOG.md) 冻结的第一轮动作范围映射到当前 Besktop Core。它是工程实施计划，不代表所有内容必须在一个提交或一个 session 中完成。

## 当前基础

当前 `IconFightScene` 已具备：

- 全量桌面图标演员、稳定随机种子、错峰觉醒和自由漫游。
- 固定尺寸双面图标薄片和局部 3D 两段式四肢。
- 步态、转向、身体投影、两段腿部 IK 和前后层四肢绘制。
- `ActorPose` 中预留的 `punch`、`kick`、`dodge`、`hit` 表现参数。
- 每演员每帧一次姿态、身体投影和骨架准备缓存。
- 分阶段帧耗时统计和 `BESKTOP_MAX_ACTORS` 对照测试。

目前缺少的是可复用动作片段、每演员动作状态、攻击者与防守者配对、接触事件、命中判定和结果反馈。不能继续只向 `BuildPose` 添加互相覆盖的时间公式。

## 实施目标

第一轮完成后，系统应支持：

- 演员从漫游自然进入准备、攻击、防守、受击、打空回收，再回到漫游。
- 同一套动作数据可镜像到左右朝向。
- 攻击者和目标共享同一条交互时间线，接触时刻一致。
- 直拳、勾拳、踢击使用固定骨长和 IK 目标，不拉伸手脚。
- 转身后踢驱动图标薄片、肩胯和踢腿共同旋转。
- 防守窗口可以产生格挡、闪避或打空，而不是每次攻击必定命中。
- 受击反馈根据轻击/重击区分偏转和后退。
- 默认演出逐步出现少量配对互动，不让全部演员同时攻击。

## 非目标

- 本轮不做真实格斗游戏级物理、逐像素碰撞、生命值和胜负系统。
- 不做倒地、起身、抱摔、地面动作或多人围攻。
- 不在本轮把动作数据开放为用户可编辑外部文件。
- 不切换 D2D/D3D 渲染后端；动作系统先复用当前 GDI+ 渲染路径。
- 不加入 Plus 私有玩法、商业动作包或授权判断。

## 建议数据模型

### 动作标识与阶段

```cpp
enum class ActionId {
    None,
    SwingPunch,
    LeadStraight,
    RearStraight,
    Hook,
    Uppercut,
    FrontKick,
    SideKick,
    RoundhouseKick,
    SpinningBackKick,
    Layback,
    Slip,
    Parry,
    LightHitReact,
    HeavyStagger,
    WhiffRecovery,
};

enum class ActionPhase {
    Prepare,
    Active,
    Contact,
    Recover,
    Complete,
};
```

代码层可以把左右侧闪实现为同一 `Slip` clip 加方向参数，公开动作 ID 和调试名称仍保留 `slip_left`、`slip_right`。

### 动作片段

```text
ActionClip
  id
  duration
  prepareEnd
  activeStart / activeEnd
  recoverStart
  body key poses
  hand / foot IK targets
  root motion
  contact events
  block / evade windows
```

第一轮先使用 C++ 内置只读表，验证动作语言和数据结构。等动作稳定后再迁移为免费 Pack 内嵌数据，避免在姿态仍频繁调整时同时设计序列化格式。

### 每演员运行状态

```text
ActorActionState
  actionId
  phase
  localTime
  playbackRate
  direction
  targetActor
  actionSeed
  cooldown
  rootMotionApplied
  contactConsumed
```

演员的漫游数据继续保留。动作开始时暂停选择新漫游目标；动作完成后恢复原有目标或重新选择安全目标。

### 交互配对

```text
CombatPair
  attacker
  defender
  desiredDistance
  attackAction
  defenseAction
  expectedResult
  startTime
  contactResolved
```

`CombatDirector` 只负责选择少量双方、预约空间和动作结果，不直接绘制姿态。第一版同时活跃的交互对数量应受控，避免全量演员一起聚集和产生性能峰值。

## 姿态合成顺序

每帧姿态按固定顺序生成：

```text
觉醒/身体基础姿态
  + 漫游步态与朝向
  + 当前 ActionClip 局部姿态
  + RootMotion
  + 受击覆盖层
  → 身体投影
  → 四肢 IK
  → 绘制
```

攻击 clip 只覆盖需要控制的身体角度、手脚目标和根运动。未覆盖的肢体继续使用站姿或平衡姿态，避免进入动作时所有关节瞬间重置。

受击优先级高于普通攻击；安全退出、边界修正和任务栏避让优先级高于 root motion。

## 动作统一时间结构

所有第一轮动作使用相同语义阶段：

1. `Prepare`：蓄力、重心调整或抬膝，给观众可读预兆。
2. `Active`：拳脚快速接近目标。
3. `Contact`：触发一次接触判定和命中事件。
4. `Recover`：收拳、收腿、恢复平衡；打空时可以替换为更长的 `WhiffRecovery`。
5. `Complete`：短混合回站姿，再回漫游或进入反击动作。

动作速度可以不同，但阶段含义不能变化。接触事件必须只消费一次，低帧率下即使跨过精确时刻也不能漏掉。

## 简化命中与防守模型

第一轮使用可解释的几何判定：

- 攻击开始时锁定目标和朝向，不在接触帧瞬移追踪目标。
- 每个攻击事件记录攻击肢体、目标高度、基础距离和命中半径。
- 接触时根据攻击者手/脚世界位置与目标身体范围判断是否在距离内。
- 目标处于 `Parry` 有效窗口且攻击是拳法时，结果为 `Blocked`。
- 目标处于 `Layback` 或 `Slip` 有效窗口且身体离开攻击线时，结果为 `Evaded`。
- 不在范围、方向错误或被闪避时，攻击者进入 `WhiffRecovery`。
- 普通命中触发 `LightHitReact`；转身后踢、侧踢等重动作可触发 `HeavyStagger`。

第一轮结果枚举建议为：

```text
HitLight
HitHeavy
Blocked
Evaded
Whiffed
```

先由导演预选一部分可读结果，再由接触时的几何条件最终确认，避免为了固定剧本让明显打空的动作仍触发命中。

## 第一轮分阶段实施

### 阶段 A：动作播放骨架与单人预览

目标：建立数据结构，不立即开启自动打架。

- 新增 `ActionId`、`ActionClip`、`ActionPlayer` 和 `ActorActionState`。
- 将动作时间推进与 `IconFightScene::Update` 的 delta time 对齐。
- 增加受诊断总开关保护的动作预览选项，例如：

```powershell
$env:BESKTOP_ACTION_PREVIEW='lead_straight'
$env:BESKTOP_MAX_ACTORS='1'
```

- 预览模式循环“站姿 → 动作 → 恢复”，便于慢放、截图和性能测量。
- 首先实现 `lead_straight`、`layback`、`light_hit_react` 三个代表动作，验证攻击、防守和受击三条路径。

验收：三个动作能独立循环、左右镜像、固定骨长、无姿态跳变；普通 Release 不读取预览变量。

### 阶段 B：拳法、防守与打空

- 实现 `rear_straight`、`uppercut`、`hook`、`swing_punch`。
- 实现 `slip_left/right`、`parry` 和 `whiff_recovery`。
- 建立单次 `ContactEvent`、防守窗口和攻击结果枚举。
- 用固定的两个演员完成直拳—拨挡、直拳—侧闪、挥拳—打空、上勾拳—轻受击。

验收：攻击和防守接触时刻一致；低帧率或 8 倍动画速度测试不重复/遗漏事件。

### 阶段 C：腿法、转身与重击反馈

- 实现 `front_kick`、`side_kick`、`roundhouse_kick`、`spinning_back_kick`。
- 实现 `heavy_stagger`。
- 侧踢和前踢复用支撑脚锁定；回旋踢驱动髋部和支撑脚旋转。
- 转身后踢必须包含头肩/薄片预转、侧面对屏幕、踢击、回正四段，不把它简化成原地抬腿。

验收：脚长不拉伸、支撑脚不明显滑动、薄片正反面不镜像、重击后退不进入任务栏和屏幕外。

### 阶段 D：受控互动导演

- 新增轻量 `CombatDirector` 和 `CombatPair`。
- 从距离合适且没有冷却的演员中选择双方。
- 先靠近到动作需要的距离，再停止漫游并对齐朝向。
- 同时只允许少量交互对，其他图标继续自由漫游。
- 动作结束后设置随机冷却，避免同一批图标持续打架。
- 默认演出按固定随机种子保持可复现，同时保留个体差异。

验收：全量桌面不会瞬间聚成一团；至少能自然出现命中、格挡、闪避、打空和反击；关闭战斗导演时仍保持当前纯漫游行为。

### 阶段 E：第一轮完整回归与节奏打磨

- 调整准备、接触、回收和受击硬直时长。
- 限制重动作和旋转动作的同时触发数量。
- 检查录屏中 5–10 秒内能否看懂一次完整交换。
- 决定第一版默认何时从漫游进入互动，以及互动持续多久。
- 动作稳定后再设计内置免费 Pack 的序列化格式，本轮不提前外部化。

## 建议代码边界

优先拆出独立模块，避免继续扩大单个 `icon_fight_scene.cpp`：

```text
src/besktop/animation/action_clip.h/.cpp
  ActionId、ActionPhase、ActionClip、ContactEvent、内置动作表

src/besktop/animation/action_player.h/.cpp
  ActionInstance、时间推进、阶段和事件消费

src/besktop/animation/combat_director.h/.cpp
  演员配对、动作选择、结果计划和冷却

src/besktop/animation/icon_fight_scene.*
  演员生命周期、漫游、动作系统集成和渲染入口
```

关键姿态采样和 IK 数学可以先放在 `action_clip.cpp`，但不得依赖 Win32 窗口或桌面采集对象，便于后续增加纯逻辑测试。

## 调试与可复现性

建议逐步增加以下只读诊断选项，全部由 `RuntimeOptions` 集中解析：

```text
BESKTOP_ACTION_PREVIEW=<action_id>
BESKTOP_COMBAT_PAIRS=<数量>
BESKTOP_ACTION_SEED=<整数>
```

Debug 可以直接使用；Release 必须先设置 `BESKTOP_ENABLE_DIAGNOSTICS=1`。普通 Release 默认不记录演员名称、路径、动作明细或逐帧事件。

动作预览应兼容现有：

- `BESKTOP_ANIMATION_SPEED`
- `BESKTOP_ANIMATION_OFFSET`
- `BESKTOP_FRAME_STATS`
- `BESKTOP_MAX_ACTORS`

## 性能约束

第一轮动作必须遵守 [RENDER_PERFORMANCE.md](RENDER_PERFORMANCE.md)：

- 参考环境全量 47 个演员稳定保持至少 30 FPS。
- 同环境稳定 FPS 和平均绘制耗时原则上不得回退超过 10%；若 30 FPS 底线更严格，以底线为准。
- 同时测量全量演员与 10 个演员，并覆盖动作接触高峰，不只测稳定漫游。
- 动作姿态、事件和命中数学每演员每帧只计算一次。
- 不通过减少默认演员、删除文字、降低动画速度或降低刷新目标掩盖回退。
- 至少运行 5 分钟，确认 GDI 句柄和私有内存稳定。

图标主体仍是当前最大渲染热点。第一轮动作应尽量保持每演员每帧一次图标主体绘制；额外残影、拖尾、多层闪白和粒子放到性能预算更充足的后续阶段。

## 验证矩阵

每个阶段至少验证：

- Debug x64、Release x64、Release Win32 的 GUI 与 Pack CLI。
- `Esc` 和 `Ctrl+Shift+B` 安全退出。
- 1 个演员动作预览、2 个演员固定互动、10 个演员对照、全量演员默认演出。
- 0.5 倍慢放检查接触与支撑脚，8 倍速检查事件不重复、不丢失。
- 左右朝向、屏幕边缘、任务栏上/下/左/右的 root motion 安全。
- Windows 10/11 x64；关键里程碑再回归 Windows 7 32 位。
- `git diff --check`、工作区状态和构建产物排除。

## 推荐的下一个实现任务

不要在第一个实现 session 中完成全部动作。下一任务只做“阶段 A：动作播放骨架与单人预览”：

1. 建立可测试的 `ActionClip` / `ActionPlayer` 数据结构。
2. 接入每演员 `ActorActionState`，但默认 Release 仍保持当前纯漫游。
3. 增加诊断预览选项。
4. 用 `lead_straight`、`layback`、`light_hit_react` 验证攻击、防守、受击三种姿态。
5. 保持全量漫游性能和所有安全退出、跨位宽图标采集行为不变。

阶段 A 稳定后，再按 B、C、D、E 逐步扩大范围。
