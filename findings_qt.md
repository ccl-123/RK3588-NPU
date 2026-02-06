# Qt 前端复审报告

**复审日期**: 2026-02-06
**复审范围**: `main_window.cc`、`dashboard_page.cc`、`recognition_page.h`、QSS 主题文件

---

## 修复验证（与当前代码一致）

| 问题 | 状态 | 验证说明 |
|------|------|----------|
| P1: Dashboard 内联样式残留 | ❌未修复 | `dashboard_page.cc` 仍有 11 处 `setStyleSheet`（行 899, 911, 986, 1112, 1117, 1145, 1152, 1159, 1167, 1222, 1257） |
| P2: 文件规模过大 | ❌未修复 | `dashboard_page.cc` 1964 行，`main_window.cc` 1724 行，仍未进入拆分阶段 |
| P3: MainWindow 与页面强耦合 | ❌未修复 | `MainWindow` 仍直接获取并保存 `RecognitionPage` 内部 10 个 `QLabel*`（行 829-838） |
| P4: 音频冷却逻辑位置不当 | ❌未修复 | `checkAudioCooldown` / `updateAudioPlayTime` 仍在 `MainWindow`（`main_window.cc` 682/703） |

---

## 已确认修复项（Qt 相关）

| Commit | 修复内容 |
|--------|----------|
| `0a3c782` | 主题切换统一走 ThemeManager，避免覆盖用户保存主题 |
| `26642a7` | 危险按钮属性统一为 `buttonType="danger"` |
| `81d9e28` | 移除 AiChatText 内联样式，转 QSS |
| `358f0db` | 移除 AI Agent 标题内联样式，转 QSS |
| `1e225f7` | 状态标签（范围/后端/Agent）改为属性 + QSS 驱动 |

---

## 更正说明

- 旧结论“MainWindow 与页面耦合已明显降低，仅信号槽连接”不准确，已更正。当前仍存在直接暴露控件指针的耦合方式。

---

## 建议优先级

1. 先清理 `dashboard_page.cc` 剩余 11 处内联样式，并补齐 QSS 选择器。
2. 随后执行 Phase 8：移除 `RecognitionPage::*Label()` 暴露接口，改 `updateState(...)`。
3. 再推进 Phase 9：拆分 Dashboard/MainWindow，控制单文件复杂度。
