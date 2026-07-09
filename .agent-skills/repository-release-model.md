# 技能: Besktop 仓库与发布模型

## 概述

用于维护 Besktop 与 Besktop-Plus 的协作方式，以及单 exe 发布模型。

## 当前结论

- 用户只看到一个产品：Besktop。
- 第一版优先发布一个文件：`Besktop.exe`。
- Plus 是内部商业扩展层，不是第二个独立软件。
- 插件和玩法包是工程概念，第一版不要求用户看到外部 `packs/` 目录。

## 执行步骤

### 步骤 1: 阅读发布模型文档

```powershell
Get-Content -Path D:\Projects\Benson\Besktop\docs\REPOSITORY_AND_RELEASE_MODEL.md
```

### 步骤 2: 判断内容应该放在哪里

放入公开仓库的内容：

- Core 引擎。
- 免费玩法。
- 公开接口。
- 授权验证客户端。
- mock 数据和示例包。

放入私有仓库的内容：

- Plus 付费内容。
- 支付回调。
- 授权签发。
- 商业发布脚本。
- 私有素材和运营数据。

### 步骤 3: 更新相关文档

如果发布模型变化，优先同步更新：

- `D:\Projects\Benson\Besktop\README.md`
- `D:\Projects\Benson\Besktop\docs\REPOSITORY_AND_RELEASE_MODEL.md`
- `D:\Projects\Benson\Besktop\docs\OPEN_CORE_BOUNDARY.md`
- `D:\Projects\Benson\Besktop\docs\TECHNICAL_PLAN.md`

## 注意事项

- 不要把 Plus 描述成第二个用户软件。
- 不要把工程内部包误写成用户必须管理的外部文件。
- 第一版以传播便利为优先，单 exe 优先级高于强防破解。
