<div align="center">

[English](./README.md) | [简体中文](./README_zh.md)

# RK3588 NPU Face Recognition System

**基于 Rockchip NPU 的高性能嵌入式人脸识别与考勤解决方案**

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20RK3588-green.svg?style=flat&logo=linux)](http://www.rock-chips.com/)
[![Framework](https://img.shields.io/badge/Framework-Qt5-success.svg?style=flat&logo=qt)](https://www.qt.io/)
[![License](https://img.shields.io/badge/Usage-Non--Commercial-orange.svg?style=flat)](LICENSE)
[![NPU](https://img.shields.io/badge/NPU-RKNN-orange.svg?style=flat)](https://github.com/airockchip/rknn-toolkit2)

[核心特性](#核心特性) • [技术栈](#技术栈) • [快速开始](#快速开始) • [文档中心](#文档中心)

</div>

---

## 项目简介

本项目是专为 Rockchip RK3588 平台打造的高性能人脸识别考勤系统。不仅通过深度整合 RKNN 硬件加速推理、RGA 图形加速引擎以及 V4L2 零拷贝采集技术实现了极致的性能，更**全面接入了腾讯云混元大模型 (LLM)**，打造了智能化的“考勤 AI agent助手”。

系统不再仅仅记录打卡，而是能够通过自然语言对话，深度分析考勤规律、诊断异常行为、自动生成分析报表，将传统的硬件终端升级为具备逻辑思维能力的智能管理 Agent。

---

## 核心特性

### 🤖 AI 驱动的智能化管理
- **考勤 AI 助手**: 集成腾讯云混元大模型，支持通过自然语言进行复杂的考勤数据交互。
- **全量数据深度分析**: 支持对“今日 / 近 7 日 / 近 30 日”全量详细考勤数据进行多维度诊断（出勤率、部门对比、异常趋势）。
- **智能诊断与预测**: AI 可自动识别潜在的考勤异常规律（如长期迟到、早退趋势），并给出针对性的管理建议。
- **即时周报生成**: 基于实时考勤流水，一键生成结构化、专业化的考勤周报与趋势报告。

###  性能优化
- **NPU 硬件加速**: 集成 RKNN Runtime，实现 YOLOv8-face 检测与 FaceNet 识别的全流程 NPU 卸载，极大降低 CPU 负载。
- **RGA 图形加速**: 利用 Rockchip RGA 2D 硬件引擎处理图像缩放、翻转与格式转换 (YUV -> RGB)，消除图像预处理瓶颈。
- **零拷贝采集**: 基于 V4L2 + mmap + shared_ptr 机制实现从内核到应用层的零拷贝数据流，优化内存带宽利用率。
- **多线程流水线**: 采用采集、预处理、检测、识别、渲染 5 级流水线设计，最大化并行处理能力。

###  业务功能完备
- **灵活交互模式**: 提供基于 Qt5 的现代化触控 GUI 界面，同时支持 Headless CLI 运行模式。
- **考勤管理系统**: 内置 SQLite3 数据库，支持分钟级弹性规则引擎、多帧防抖识别算法。
- **高健壮性设计**: 包含摄像头热插拔自动恢复机制、人脸特征库实时同步与动态加载。

---

## 技术栈

| 模块 | 技术选型 | 说明 |
| :--- | :--- | :--- |
| **编程语言** | **C++17** | 核心逻辑开发，充分利用标准库新特性 |
| **AI Agent / LLM** | **腾讯云混元大模型** | **核心亮点：基于 SSE 协议的智能分析对话系统** |
| **UI 框架** | Qt 5.15+ | 现代化图形用户界面，支持动态主题切换 |
| **深度学习** | RKNN Toolkit2 | NPU 模型推理 (YOLOv8, FaceNet) |
| **计算机视觉** | OpenCV 4.5+ | 图像处理与算法辅助 |
| **硬件加速** | Rockchip RGA | 2D 硬件加速引擎 |
| **数据采集** | V4L2 | Linux 视频驱动接口 (mmap 零拷贝模式) |
| **数据库** | SQLite3 | 嵌入式本地存储，DAO 架构设计 |
| **日志系统** | spdlog | 异步高性能日志记录 |

---

## 快速开始

### 1. 环境要求
确保硬件为 RK3588 系列开发板（如 Orange Pi 5, Rock 5B），系统已安装基础构建工具及支持 C++17 的编译器：

```bash
sudo apt update
sudo apt install cmake build-essential libopencv-dev qt5-default libsqlite3-dev
```

### 2. 构建与运行
```bash
cd C++/face_recognition_cap
./build.sh

# 启动 GUI 模式
./build/build_linux_aarch64/face_recognition_cap
```

---

## 文档中心

*   [快速入门指南](C++/face_recognition_cap/docs/用户文档/快速开始.md) - 环境搭建与详细编译步骤。
*   [架构设计文档](C++/face_recognition_cap/docs/开发文档/README.md) - 系统架构、流水线设计与核心模块说明。
*   [API 接口文档](C++/face_recognition_cap/docs/开发文档/API文档.md) - 二次开发与集成接口。
*   [用户使用手册](C++/face_recognition_cap/docs/用户文档/用户使用手册.md) - GUI 功能操作指南。

---

## 使用许可与免责声明

### 1. 使用条款
本项目及其源代码**仅供学习、研究与个人交流使用**。在满足以下条件的前提下，您可以自由获取和修改代码：
- **严禁商用**：不得将本项目或其修改版本用于任何形式的商业产品、收费服务或盈利活动。
- **保留声明**：在分发或传播代码时，必须保留原始的版权声明和本使用许可。

### 2. 免责声明
本程序按“原样”提供，不附带任何形式的明示或暗示保证。作者不保证程序的稳定性或安全性，因使用本程序产生的任何损失，作者概不负责。

---

## 版权声明

Copyright © 2025 **Edge2-NPU Project**. All Rights Reserved.