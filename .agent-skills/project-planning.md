# 技能: Besktop 项目规划

## 概述

用于维护 Besktop Core 的中文产品说明、路线图、阶段计划和文档一致性。

## 前置条件

- 当前仓库为 `D:\Projects\Benson\Besktop`。
- 用户希望先打磨文档和计划，再进入实现。
- 任何新增文档默认使用中文。

## 执行步骤

### 步骤 1: 查看现有文档

```powershell
rg --files D:\Projects\Benson\Besktop
```

重点阅读：

- `D:\Projects\Benson\Besktop\README.md`
- `D:\Projects\Benson\Besktop\docs\TECHNICAL_PLAN.md`
- `D:\Projects\Benson\Besktop\docs\OPEN_CORE_BOUNDARY.md`
- `D:\Projects\Benson\Besktop\docs\ROADMAP.md`
- `D:\Projects\Benson\Besktop\docs\REPOSITORY_AND_RELEASE_MODEL.md`

### 步骤 2: 确认本次修改范围

优先修改文档，不主动实现功能代码。除非用户明确要求，否则不要扩展 `src/`。

### 步骤 3: 保持文档结构一致

文档应覆盖：

- 产品定位。
- 免费核心体验。
- Plus 商业边界。
- 仓库协作和单 exe 发布模型。
- 技术路线。
- 风险与待验证问题。
- 阶段路线图。

## 验证步骤

```powershell
git -C D:\Projects\Benson\Besktop status --short
git -C D:\Projects\Benson\Besktop diff --check
```

确认没有误改商业仓库内容，没有把 Plus 私有信息写入 Core。

## 注意事项

- 不要把支付密钥、授权私钥或客户数据写入 Core。
- 不要把 Core 写成残缺试用版，免费版必须完整好玩。
- 不要在文档中承诺还未验证的 Windows 兼容性。
