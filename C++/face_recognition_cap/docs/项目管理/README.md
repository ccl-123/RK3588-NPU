# 项目管理文档

> 版本: v2.4
> 更新日期: 2026-04-01

## 当前状态

当前项目已经从“基础人脸识别考勤系统”演进为“带端云 Agent 的智能考勤终端”。

## 最近版本重点

### v2.4

- 修复 Agent 收尾状态与上下文保留问题
- 支持 OpenAI 兼容远端大模型接口
- 工具集扩展为：
  - `query_attendance`
  - `lookup_user_attendance`
  - `lookup_department_attendance`
  - `lookup_attendance_ranking`
  - `lookup_missing_attendance`
  - `query_user`
  - `system_info`
  - `help`
  - `calculator`
- 文档统一收拢到 `C++/face_recognition_cap/docs`

### v2.2

- ReAct Agent 框架落地
- 本地 RKLLM 与腾讯云模式并存

## 当前任务完成情况

| 任务 | 状态 | 说明 |
|------|------|------|
| NPU 视觉推理 | ✅ | YOLO + FaceNet |
| 本地 RKLLM | ✅ | 本地 Agent / Chat |
| 腾讯云远端 | ✅ | LKE SSE |
| OpenAI 兼容远端 | ✅ | `/v1/chat/completions` |
| 单员工考勤分析 | ✅ | 已支持 |
| 部门考勤分析 | ✅ | 已支持 |
| 排名分析 | ✅ | 已支持 |
| 缺卡缺勤分析 | ✅ | 已支持 |
| 文档统一整理 | ✅ | 已迁回 `face_recognition_cap/docs` |

## 参考文档

- [GUI完整产品总结.md](./GUI完整产品总结.md)
- [本地LLM集成.md](./本地LLM集成.md)
- [../开发文档/Agent模块说明.md](../开发文档/Agent模块说明.md)
- [../开发文档/OpenAI兼容接口接入说明.md](../开发文档/OpenAI兼容接口接入说明.md)
