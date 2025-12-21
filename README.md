<div align="center">

[English](./README.md) | [简体中文](./README_zh.md)

# RK3588 NPU Face Recognition System

**High-Performance Embedded Face Recognition & Attendance Solution based on Rockchip NPU**

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20RK3588-green.svg?style=flat&logo=linux)](http://www.rock-chips.com/)
[![Framework](https://img.shields.io/badge/Framework-Qt5-success.svg?style=flat&logo=qt)](https://www.qt.io/)
[![License](https://img.shields.io/badge/Usage-Non--Commercial-orange.svg?style=flat)](LICENSE)
[![NPU](https://img.shields.io/badge/NPU-RKNN-orange.svg?style=flat)](https://github.com/airockchip/rknn-toolkit2)

[Core Features](#core-features) • [Tech Stack](#tech-stack) • [Quick Start](#quick-start) • [Documentation](#documentation)

</div>

---

## Project Introduction

This project is a high-performance face recognition attendance system designed specifically for the Rockchip RK3588 platform. It not only achieves extreme performance through deep integration of RKNN hardware acceleration inference, RGA graphics acceleration engine, and V4L2 zero-copy capture technology, but also **fully integrates the Tencent Cloud Hunyuan Large Language Model (LLM)** to create an intelligent "Attendance AI Assistant".

The system no longer just records clock-ins; it can analyze attendance patterns, diagnose abnormal behaviors, and automatically generate analysis reports through natural language dialogue, upgrading traditional hardware terminals into intelligent management Agents with logical thinking capabilities.

---

## Core Features

### 🤖 AI-Driven Intelligent Management
- **Attendance AI Assistant**: Integrated with Tencent Cloud Hunyuan LLM, supporting complex attendance data interaction via natural language.
- **Full Data Deep Analysis**: Supports multi-dimensional diagnosis (attendance rate, departmental comparison, abnormal trends) on "Today / Last 7 Days / Last 30 Days" full detailed attendance data.
- **Intelligent Diagnosis & Prediction**: AI automatically identifies potential attendance anomaly patterns (such as long-term late arrivals, early departure trends) and provides targeted management suggestions.
- **Instant Weekly Report Generation**: Generates structured and professional attendance weekly reports and trend reports with one click based on real-time attendance streams.

### 🚀 Extreme Performance Optimization
- **NPU Hardware Acceleration**: Integrated with RKNN Runtime, implementing full-process NPU offloading for YOLOv8-face detection and FaceNet recognition, greatly reducing CPU load.
- **RGA Graphics Acceleration**: Utilizes the Rockchip RGA 2D hardware engine to handle image scaling, flipping, and format conversion (YUV -> RGB), eliminating image preprocessing bottlenecks.
- **Zero-Copy Capture**: Implements zero-copy data flow from kernel to application layer based on V4L2 + mmap + shared_ptr mechanism, optimizing memory bandwidth utilization.
- **Multi-Threaded Pipeline**: Adopts a 5-stage pipeline design of capture, preprocessing, detection, recognition, and rendering to maximize parallel processing capabilities.

### 💼 Comprehensive Business Functions
- **Flexible Interaction Modes**: Provides a modern touch-enabled GUI interface based on Qt5, while also supporting Headless CLI operation mode.
- **Attendance Management System**: Built-in SQLite3 database, supporting minute-level flexible rule engine and multi-frame anti-shake recognition algorithms.
- **High Robustness Design**: Includes automatic recovery mechanism for camera hot-plugging, real-time synchronization, and dynamic loading of face feature libraries.

---

## Tech Stack

| Module | Technology | Description |
| :--- | :--- | :--- |
| **Language** | **C++17** | Core logic development, fully utilizing new standard library features |
| **AI Agent / LLM** | **Tencent Cloud Hunyuan LLM** | **Core Highlight: Intelligent Analysis Dialogue System based on SSE Protocol** |
| **UI Framework** | Qt 5.15+ | Modern graphical user interface, supporting dynamic theme switching |
| **Deep Learning** | RKNN Toolkit2 | NPU Model Inference (YOLOv8, FaceNet) |
| **Computer Vision** | OpenCV 4.5+ | Image processing and algorithm assistance |
| **Hardware Accel** | Rockchip RGA | 2D Hardware Acceleration Engine |
| **Data Capture** | V4L2 | Linux Video Driver Interface (mmap zero-copy mode) |
| **Database** | SQLite3 | Embedded local storage, DAO architecture design |
| **Logging** | spdlog | Asynchronous high-performance logging |

---

## Quick Start

### 1. Requirements
Ensure the hardware is an RK3588 series development board (e.g., Orange Pi 5, Rock 5B), and the system has basic build tools and a C++17 compatible compiler installed:

```bash
sudo apt update
sudo apt install cmake build-essential libopencv-dev qt5-default libsqlite3-dev
```

### 2. Build & Run
```bash
cd C++/face_recognition_cap
./build.sh

# Start GUI Mode
./build/build_linux_aarch64/face_recognition_cap
```

---

## Documentation

*   [Quick Start Guide](C++/face_recognition_cap/docs/用户文档/快速开始.md) - Environment setup and detailed compilation steps.
*   [Architecture Design](C++/face_recognition_cap/docs/开发文档/README.md) - System architecture, pipeline design, and core module explanation.
*   [API Documentation](C++/face_recognition_cap/docs/开发文档/API文档.md) - Secondary development and integration interfaces.
*   [User Manual](C++/face_recognition_cap/docs/用户文档/用户使用手册.md) - GUI function operation guide.

---

## License & Disclaimer

### 1. Terms of Use
This project and its source code are **for learning, research, and personal exchange only**. You are free to obtain and modify the code subject to the following conditions:
- **No Commercial Use**: You may not use this project or any modified version for any form of commercial product, paid service, or profit-making activity.
- **Preserve Notices**: You must retain the original copyright notice and this license when distributing or propagating the code.

### 2. Disclaimer
This program is provided "AS IS", without warranty of any kind, express or implied. The author does not guarantee the stability or safety of the program and is not responsible for any losses caused by the use of this program.

---

## Copyright

Copyright © 2025 **Edge2-NPU Project**. All Rights Reserved.
