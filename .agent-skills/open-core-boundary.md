# 技能: Besktop 开源边界维护

## 概述

用于维护 Besktop Core 与 Besktop Plus 的功能、代码和商业边界。

## 核心原则

Core 要完整、好玩、可免费传播。Plus 负责额外的社交、个性化和商业扩展。

用户只看到一个产品：`Besktop`。Plus 是内部商业扩展层，不是第二个独立软件。

## 执行步骤

### 步骤 1: 阅读边界文档

```powershell
Get-Content -Path D:\Projects\Benson\Besktop\docs\OPEN_CORE_BOUNDARY.md
```

### 步骤 2: 判断内容归属

放入 Core 的内容应满足：

- 可公开。
- 不含商业密钥。
- 不含客户数据。
- 对免费体验有基础价值。

放入 Plus 的内容包括：

- 付费玩法包。
- 支付回调。
- 授权生成。
- 私有资源。
- 售后工具。
- 商业打包脚本。

### 步骤 3: 更新文档或提出拆分建议

如果发现 Plus 内容进入 Core，应建议移到 `D:\Projects\Benson\Besktop-Plus`。

## 注意事项

- 早期不接受代码 PR，避免双授权版权边界混乱。
- 可以接受 issue、反馈和非代码建议。
