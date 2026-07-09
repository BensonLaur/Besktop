# 技能: Besktop 提交流程

## 概述

用于在 Besktop Core 仓库中按约定式提交规范组织提交。

## 提交格式

```text
<type>(<scope>): <中文摘要>
```

示例：

```text
docs(plan): 初始化项目规划文档
feat(overlay): 增加透明覆盖层窗口
fix(icon): 修复高 DPI 坐标偏移
```

## 执行步骤

### 步骤 1: 查看状态

```powershell
git -C D:\Projects\Benson\Besktop status --short
```

### 步骤 2: 查看差异

```powershell
git -C D:\Projects\Benson\Besktop diff
```

### 步骤 3: 检查空白和格式

```powershell
git -C D:\Projects\Benson\Besktop diff --check
```

### 步骤 4: 组织提交

按主题拆分提交。文档初始化可以使用：

```text
docs(plan): 初始化 Besktop 文档规划
```

## 注意事项

- 未经用户要求，不要主动提交。
- 不要把 Besktop-Plus 的私有内容提交到 Core。
- 不要把构建产物、临时文件或密钥提交进仓库。
