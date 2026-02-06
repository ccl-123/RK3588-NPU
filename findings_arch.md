# 架构复审报告

**复审日期**: 2026-02-06
**复审范围**: 基于 `findings.md` 的“总项目架构审查”条目进行现状复核

---

## 修复验证（与当前代码一致）

| 问题 | 优先级 | 状态 | 验证说明 |
|------|--------|------|----------|
| `Config::LocalLLM::MODEL_PATH` 硬编码绝对路径 | P0 | ✅已修复 | 已改为 `MODEL_ENV + MODEL_PATH + getModelPath()`，支持 `LOCAL_LLM_MODEL_PATH` 覆盖（commit: `f56cac4`） |
| 头文件保护未统一为 `#pragma once` | P1 | 🔄部分修复 | 关键文件已修复，但仓内仍存在其他 `#ifndef` 保护文件 |
| MainWindow 与页面耦合过深 | P1 | ✅已修复 | 状态刷新改为 `RecognitionPage` 接口调用，不再跨页面持有内部 `QLabel*`（commit: `a6bf3d1`） |
| 无单元测试目标 | P2 | ❌未修复 | `CMakeLists.txt` 仍未引入 `enable_testing()` / `add_test()` |
| 编译警告级别未设置 | P2 | ❌未修复 | 仍未统一配置 `-Wall/-Wextra/-Wpedantic` |
| Agent 核心逻辑与 Qt 耦合 | P2 | ❌未修复 | `react_agent.h` 仍依赖 `QObject/QString` |

**结论**: 架构项“已修复 2 项、部分修复 1 项、未修复 3 项”。

---

## 重构计划进度（对照 refactor_plan）

| Phase | 描述 | 状态 | 完成度 |
|-------|------|------|--------|
| Phase 1 | 代码清理与规范化 | ✅ 完成 | 4/4 |
| Phase 2 | 主题系统修复 (P0) | ✅ 完成 | 4/4 |
| Phase 3 | 按钮样式修复 (P0) | ✅ 完成 | 3/3 |
| Phase 4 | Settings 加载无副作用 (P0) | ✅ 完成 | 2/2 |
| Phase 5 | 去硬编码 (P1) | ✅ 完成 | 5/5 |
| Phase 6 | 样式统一 (P1) | ✅ 完成 | 4/4 |
| Phase 7 | 音频冷却逻辑重构 (P1) | ✅ 完成 | 3/3 |
| Phase 8 | MainWindow 与页面解耦 (P1) | ✅ 完成 | 3/3 |
| Phase 9 | 大文件拆分 (P2) | ⏳ 待开始 | 0/5 |

---

## 近期关键提交（架构相关）

| Commit | 描述 |
|--------|------|
| `f56cac4` | 修复本地模型路径硬编码并增加缺失保护 |
| `6a35a67` | 清理 Dashboard 剩余内联样式并收敛到 QSS |
| `34c1acf` | 下沉音频冷却逻辑到 AudioManager |
| `a6bf3d1` | 解耦 MainWindow 与 RecognitionPage 状态更新 |
| `5623565` | 为 ConfigManager 读写增加互斥保护 |

---

## 后续建议（架构层）

1. 优先推进 Phase 9：拆分 `main_window.cc` 与 `dashboard_page.cc`，降低单文件复杂度。
2. 增加最小 smoke test 和 `ctest` 骨架，建立基础回归保障。
3. 将编译告警提升到统一标准并分阶段清警告。
4. 规划 Agent 与 Qt 解耦专项，避免后续跨端复用受限。
