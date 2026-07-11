# Debug Session: image-list-clear-failure

**Status:** `[OPEN]`
**Session ID:** `image-list-clear-failure`
**Date:** 2026-05-29

## Problem Definition

### Symptoms
- 图片列表中删除选中项后，图片项视觉上未被移除
- 清空列表按钮点击后，列表仍显示图片项
- 数据层（ImageManager）操作正确（测试 PASS），但视图层（QListWidget）未同步更新

### Expected Behavior
- 删除选中项后，QListWidget 应立即移除对应行并刷新视图
- 清空列表后，QListWidget 应显示为空列表

### Reproduction Steps
1. 启动 QDV 应用程序
2. 切换到"训练推理"视图
3. 导入若干图片
4. 选中一张图片，点击"删除选中"按钮 → 图片未从列表移除
5. 点击"清空列表"按钮 → 列表不为空

### Previous Fix Attempt
- 将 `rebuildImageList()` 和 `onClearAll()` 中的 `blockSignals(true/false)` 替换为 `disconnect/connect`
- 198 个单元测试全部 PASS（含 9 个新增 UI 测试）
- 用户反馈实际运行中问题仍然存在

## Hypotheses

| ID | Hypothesis | Likelihood | Effort |
|----|------------|------------|--------|
| A | 实际运行的二进制文件未包含修复代码（stale binary） | High | Low |
| B | `disconnect` 在某些条件下未生效，导致 currentRowChanged 信号被重复触发 | Medium | Low |
| C | QListWidget 在 Windows 平台的 Qt 特定版本中存在重绘 bug | Low | Medium |
| D | ImageManager 单例在多次操作后状态异常 | Low | Low |
| E | 其他代码路径在操作后重新触发了列表重建 | Medium | Medium |

## Investigation Log

### Step 1 - 代码审查
已完成对 TrainingInferenceView.cpp、ImageManager.cpp、TrainingInferenceView.h 的全面审查。
发现 `blockSignals` 已被替换为 `disconnect/connect`。

### Step 2 - 单元测试
198/198 测试 PASS，包括 9 个 UI 层测试。