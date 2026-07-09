# Besktop Agent 指南

本仓库是 Besktop 的开源核心仓库。进入本仓库工作时，先遵守本文档，再按需读取 `.agent-skills/` 中的技能文档。

## 当前阶段

项目处于“文档和计划打磨期”。除非用户明确要求开始实现，否则优先完善中文文档、产品边界、技术方案、路线图和仓库约定，不主动推进功能代码。

## 文档语言

- 项目文档默认使用中文。
- 产品名、玩法名、英文宣传语可以保留英文，例如 `Besktop`、`Icon Fight`、`Let your desktop icons fight.`。
- 面向用户的中文表达优先自然、易传播，不使用生硬直译。
- 代码标识符使用英文；必要注释可以用中文，但保持简洁。

## 提交规范

使用约定式提交，提交摘要使用中文。

格式：

```text
<type>(<scope>): <中文摘要>
```

`scope` 可省略。

常用类型：

- `feat`: 新功能。
- `fix`: 修复问题。
- `docs`: 文档。
- `refactor`: 重构。
- `test`: 测试。
- `build`: 构建系统。
- `ci`: 持续集成。
- `chore`: 杂项维护。
- `style`: 格式调整。

示例：

```text
docs(plan): 初始化 Besktop 产品规划
feat(overlay): 增加透明覆盖层窗口
fix(icon): 修复高 DPI 下图标坐标偏移
```

提交前必须检查：

- `git status --short`
- `git diff --check`
- 文档是否仍为中文主导。
- 是否误提交了 Plus 私有资源、密钥、客户数据或临时文件。

## 项目技能 (.agent-skills/)

当用户提到以下关键词时，先读取对应技能文档，按文档中的步骤执行，不要凭记忆操作。

| 触发词 | 技能文件 | 说明 |
| --- | --- | --- |
| “规划”、“路线图”、“打磨文档” | `.agent-skills/project-planning.md` | 更新产品规划、技术方案和路线图 |
| “技术方案”、“选型”、“架构” | `.agent-skills/technical-planning.md` | 评估和更新 Windows 原生技术路线 |
| “开源边界”、“商业边界”、“Plus 边界” | `.agent-skills/open-core-boundary.md` | 维护 Core 与 Plus 的边界 |
| “仓库定位”、“发布形态”、“单 EXE”、“插件” | `.agent-skills/repository-release-model.md` | 维护仓库协作和单 exe 发布模型 |
| “提交”、“commit” | `.agent-skills/commit-workflow.md` | 按约定式提交检查和组织提交 |

技能文档是延迟加载的，仅在触发时读取，平时不消耗上下文。
