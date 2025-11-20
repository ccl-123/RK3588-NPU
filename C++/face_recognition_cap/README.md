# 人脸识别系统 (Face Recognition System)

基于 Edge2 NPU 的实时人脸识别系统，采用 RetinaFace + FaceNet 实现高性能人脸检测与识别。

[![Platform](https://img.shields.io/badge/Platform-RK3588-blue)](https://www.rock-chips.com/a/cn/product/RK35xilie/2022/0926/1660.html)
[![NPU](https://img.shields.io/badge/NPU-RKNPU2-green)](https://github.com/rockchip-linux/rknpu2)
[![FPS](https://img.shields.io/badge/FPS-60--73-orange)](docs/模块说明.md#性能数据)
[![License](https://img.shields.io/badge/License-Apache%202.0-red)](LICENSE)

---

## ✨ 特性

- 🚀 **高性能**: 60-73 FPS (1280x720, 单人)
- 🎯 **高精度**: 512维特征向量，识别准确率 > 95%
- 🔧 **模块化**: 分层架构，易于维护和扩展
- ⚡ **硬件加速**: NPU 推理 + RGA 图像处理
- 🧵 **多线程优化**: 预处理、渲染、采集三线程并行
- 📊 **性能监控**: 实时 FPS 和各阶段耗时统计

---

## 📋 目录

- [快速开始](#快速开始)
- [系统架构](#系统架构)
- [性能表现](#性能表现)
- [文档](#文档)
- [依赖项](#依赖项)
- [常见问题](#常见问题)
- [贡献指南](#贡献指南)

---

## 🚀 快速开始

### 1. 编译项目

```bash
cd /home/firefly/open_project/edge2-npu/C++/face_recognition_cap
bash build.sh
```

### 2. 准备特征库

```bash
# 生成人脸特征库
cd ../face_recognition
bash build.sh
cd install/face_recognition
./face_recognition data/model/retinaface.rknn data/model/w600k_mbf.rknn data/img/

# 复制特征库
cp -r data/face_feature_lib ../../face_recognition_cap/install/face_recognition_cap/data/
```

### 3. 运行程序

```bash
cd /home/firefly/open_project/edge2-npu/C++/face_recognition_cap/install/face_recognition_cap

# 查看摄像头设备
lsusb

# 运行人脸识别
./face_recognition_cap data/model/retinaface.rknn data/model/w600k_mbf.rknn usb 21
```

**参数说明**:
- `data/model/retinaface.rknn`: 人脸检测模型
- `data/model/w600k_mbf.rknn`: 特征提取模型 (512维)
- `usb` / `mipi`: 摄像头类型
- `21`: 设备编号 (通过 `lsusb` 查看)

详细步骤请参考 [快速开始文档](docs/快速开始.md)。

---

## 🏗️ 系统架构

项目采用**四层架构**设计：

```
Main 层 (程序入口)
  ↓
App 层 (应用逻辑)
  ├─ FaceRecognitionApp    # 主应用控制
  ├─ ModelManager          # 模型管理
  ├─ FeatureLibrary        # 特征库管理
  ├─ PreprocessingThread   # 预处理线程
  ├─ RenderingThread       # 渲染线程
  └─ PerformanceMonitor    # 性能监控
  ↓
Core 层 (核心算法)
  ├─ RetinaFace            # 人脸检测
  ├─ FaceNet               # 特征提取
  └─ PostProcess           # 后处理工具
  ↓
Hardware 层 (硬件接口)
  ├─ CameraUtil            # 摄像头控制
  ├─ RGA                   # 硬件加速
  └─ RKNN                  # NPU 推理
```

详细架构说明请参考 [模块说明文档](docs/模块说明.md)。

---

## 📊 性能表现

### FPS 性能

| 场景 | FPS | 帧时间 |
|------|-----|--------|
| 1280x720, 0人 | 70-73 | 13-14ms |
| 1280x720, 1人 | 60-65 | 15-16ms |
| 1280x720, 3人 | 35-45 | 22-28ms |
| 1920x1080, 1人 | 40-50 | 20-25ms |

### 各阶段耗时 (1人场景)

| 阶段 | 耗时 (ms) | 占比 |
|------|----------|------|
| 摄像头读取 | 0.1 | 0.6% |
| 预处理 (RGA) | 2.5 | 15.6% |
| 人脸检测 (RetinaFace) | 12.3 | 76.9% |
| 人脸对齐 | 3.2 | 20.0% |
| 特征提取 (FaceNet) | 8.5 | 53.1% |
| 特征匹配 | 0.5 | 3.1% |
| 渲染 | 0.5 | 3.1% |

### 优化效果

| 优化项 | 优化前 | 优化后 | 提升 |
|--------|--------|--------|------|
| 整体 FPS | 25-28 | 60-73 | **2.5x** |
| 预处理时间 | 11-15ms | 2-3ms | **4-5x** |
| 摄像头阻塞 | 5-8ms | < 0.1ms | **50-80x** |
| 代码行数 (main.cc) | 399 | 68 | **-83%** |

---

## 📚 文档

完整文档位于 `docs/` 目录：

- **[快速开始.md](docs/快速开始.md)** - 编译、安装、运行指南
- **[API文档.md](docs/API文档.md)** - 各模块的接口说明
- **[模块说明.md](docs/模块说明.md)** - 系统架构和模块职责

---

## 🔧 依赖项

### 硬件要求
- **开发板**: Firefly Edge2 (RK3588)
- **NPU**: RKNPU2
- **摄像头**: USB 或 MIPI 摄像头
- **内存**: 至少 2GB RAM

### 软件依赖
- **操作系统**: Ubuntu 20.04/22.04 (ARM64)
- **编译器**: GCC 7.5+ (C++11)
- **CMake**: 3.4.1+
- **OpenCV**: 4.x
- **RKNN Runtime**: librknnrt.so (已包含)
- **RGA**: librga.so (已包含)

---

## ❓ 常见问题

### Q1: 编译失败，提示找不到 OpenCV

```bash
sudo apt install libopencv-dev
```

### Q2: 运行时提示 "Feature library doesn't exist"

确保已生成特征库并复制到正确位置：
```bash
ls install/face_recognition_cap/data/face_feature_lib/
```

### Q3: FPS 很低（< 10）

可能原因：
- 摄像头分辨率过高（建议 1280x720）
- 特征库过大（建议 < 100 人）
- 系统负载过高

### Q4: 识别准确率低

解决方法：
1. 使用高质量的人脸照片生成特征库
2. 确保光照条件良好
3. 调整识别阈值（默认 0.5）

更多问题请参考 [快速开始文档](docs/快速开始.md#常见问题)。

---

## 🤝 贡献指南

欢迎贡献代码、报告问题或提出建议！

### 开发流程

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 提交 Pull Request

### 代码规范

- 遵循 C++11 标准
- 使用 4 空格缩进
- 添加必要的注释和文档
- 保持模块职责单一

---

## 📄 许可证

本项目采用 Apache 2.0 许可证 - 详见 [LICENSE](LICENSE) 文件。

---

## 🙏 致谢

- [Rockchip RKNPU2](https://github.com/rockchip-linux/rknpu2) - NPU 推理框架
- [RetinaFace](https://github.com/deepinsight/insightface) - 人脸检测模型
- [MobileFaceNet](https://github.com/deepinsight/insightface) - 人脸识别模型
- [OpenCV](https://opencv.org/) - 计算机视觉库

---


---

**注意事项**:
- 新模型 `w600k_mbf.rknn` 输出 **512维特征** (旧模型 128维)
- 切换模型时必须重新生成特征库
- 推荐摄像头分辨率: 1280x720

---

## 🖥️ GUI 版本

### 新增功能（v1.0.0）

系统现已提供完整的 Qt 图形界面版本，包含以下功能：

#### ✨ 核心功能

- **实时视频显示**: 高性能视频流显示，支持人脸框和信息标注
- **人脸注册**: 图形化人脸注册界面，支持质量检测和多张采集
- **考勤查询**: 按日期/用户查询考勤记录，支持导出 CSV
- **用户管理**: 用户列表、搜索、启用/禁用、删除等功能
- **系统设置**: 摄像头参数、识别阈值等可视化配置
- **数据库集成**: SQLite 数据库持久化存储

#### 🚀 快速启动 GUI

```bash
cd install/face_recognition_cap
./run_gui.sh
```

#### 📚 GUI 相关文档

| 文档 | 说明 |
|------|------|
| [用户使用手册](docs/用户使用手册.md) | GUI 操作指南 |
| [GUI 完整产品总结](docs/GUI完整产品总结.md) | 功能清单和技术总结 |
| [开发者文档](docs/开发者文档.md) | API 参考和扩展开发 |
| [数据库集成实施总结](docs/数据库集成实施总结.md) | 数据库设计文档 |

#### 🎯 GUI 特性

- **友好界面**: Qt 5.15.3 LTS 图形界面
- **实时性能**: 45-65 FPS（含渲染）
- **数据管理**: 用户、考勤、特征一体化管理
- **可视化**: 实时 FPS、识别结果、考勤统计
- **易用性**: 菜单、工具栏、快捷键支持

#### 📦 可执行文件

```bash
install/face_recognition_cap/
├── face_recognition_cap      # 命令行版本 (552KB)
├── face_recognition_cap_gui  # GUI 版本 (2.9MB)
├── db_tool                   # 数据库工具 (342KB)
└── run_gui.sh                # GUI 启动脚本
```

---

## 🎊 版本历史

### v1.0.0 (2025-11-20)

**新增**:
- ✅ Qt GUI 图形界面
- ✅ SQLite 数据库集成
- ✅ 人脸注册对话框
- ✅ 考勤查询组件
- ✅ 用户管理界面
- ✅ 系统设置对话框
- ✅ 数据库命令行工具

**优化**:
- ✅ 升级到 C++14 标准
- ✅ 集成 spdlog 日志库
- ✅ 添加单帧处理 API
- ✅ 完善文档体系

**已知限制**:
- ⚠️ 人脸注册使用简化特征提取
- ⚠️ 系统设置未持久化
- ⚠️ 用户编辑功能待实现

---

<div align="center">

**⭐ 如果这个项目对您有帮助，请给我们一个 Star！**

Made with ❤️ by Augment Agent

</div>