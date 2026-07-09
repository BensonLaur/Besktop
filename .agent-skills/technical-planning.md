# 技能: Besktop 技术方案

## 概述

用于评估和维护 Besktop Core 的 Windows 原生技术路线。

## 技术基线

当前推荐方向：

- C++20
- Win32
- Direct3D 11
- Direct2D / DirectWrite
- WIC
- CMake + Visual Studio 2022

## 执行步骤

### 步骤 1: 先读计划文档

```powershell
Get-Content -Path D:\Projects\Benson\Besktop\docs\TECHNICAL_PLAN.md
```

### 步骤 2: 评估变更是否符合项目目标

技术选择必须服务于：

- 小体积。
- 易传播。
- Windows 原生体验。
- 低运行时依赖。
- 对杀毒软件和用户信任友好。
- 未来支持玩法包和授权验证。

### 步骤 3: 更新文档

如果技术判断发生变化，先更新 `docs/TECHNICAL_PLAN.md`，再考虑代码实现。

## 验证步骤

文档阶段只需要检查：

```powershell
git -C D:\Projects\Benson\Besktop diff --check
```

进入实现阶段后，再补充构建和原型验证命令。

## 注意事项

- 不要优先引入 Electron、Unity 或 Unreal，除非用户明确改变“小体积、原生、易传播”的目标。
- 不要直接操作真实桌面文件；动画应发生在覆盖层。
- 不要把支付或授权私钥放入客户端。
