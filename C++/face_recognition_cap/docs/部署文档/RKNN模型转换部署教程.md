# RKNN 模型转换与部署教程

> 本教程介绍如何将深度学习模型转换为 RKNN 格式，并部署到 RK3588 开发板上运行。

---

>  **声明**
> 
> 本教程仅供参考，内容不保证 100% 准确。由于工具版本更新、API 变化等原因，实际操作可能与文档描述有所不同。
> 
> **建议**：
> - 遇到问题时，优先查阅官方 GitHub 仓库的最新文档和 Issues
> - 用 AI 大模型辅助学习：**ChatGPT**、**DeepSeek**、**通义千问**、**豆包** 等都是很好的学习助手


---

## 📋 目录

- [1. 概述](#1-概述)
- [2. 环境准备](#2-环境准备)
- [3. 获取官方工具和例程](#3-获取官方工具和例程)
- [4. RKNN-Toolkit2 安装](#4-rknn-toolkit2-安装)
- [5. 模型转换流程](#5-模型转换流程)
- [6. 部署到开发板](#6-部署到开发板)
- [7. 常见问题](#7-常见问题)

---

## 1. 概述

### 1.1 什么是 RKNN

**RKNN** (Rockchip Neural Network) 是瑞芯微为其 NPU 设计的神经网络推理格式。要在 RK3588 的 NPU 上运行深度学习模型，需要先将模型转换为 `.rknn` 格式。

### 1.2 转换工具链

| 工具 | 运行环境 | 功能 |
|------|----------|------|
| **RKNN-Toolkit2** | PC (x86 Linux/Windows) | 模型转换、量化、仿真 |
| **RKNN Runtime** | 开发板 (ARM64) | 模型推理运行 |

### 1.3 支持的模型格式

| 框架 | 格式 | 说明 |
|------|------|------|
| **ONNX** | `.onnx` | 推荐，兼容性最好 |
| **PyTorch** | `.pt` / `.torchscript` | 需先导出为 ONNX |
| **TensorFlow** | `.pb` / SavedModel | TF 1.x 和 2.x |
| **TFLite** | `.tflite` | TensorFlow Lite |
| **Caffe** | `.caffemodel` | 旧版支持 |

### 1.4 转换流程图

```
原始模型 (PyTorch/TF)
        ↓
   导出为 ONNX（推荐）
        ↓
   RKNN-Toolkit2 转换
        ↓
      .rknn 模型
        ↓
   部署到开发板
        ↓
   RKNN Runtime 推理
```

---

## 2. 环境准备

### 2.1 PC 端环境要求

模型转换。

| 项目 | 要求 |
|------|------|
| **操作系统** | Ubuntu 18.04/20.04/22.04 (x86_64) 或 Windows 10/11 |
| **Python** | 3.8 / 3.9 / 3.10 |
| **内存** | ≥ 8GB |
| **磁盘** | ≥ 10GB 可用空间 |

### 2.2 建议使用虚拟环境

建议使用 Python 虚拟环境或 Conda 环境，避免依赖冲突（可选）。

---

## 3. 获取官方工具和例程

### 3.1 Firefly 资料下载

访问 Firefly 资料下载页面获取 RKNN 相关工具：

> 📦 **下载地址**：https://www.t-firefly.com/doc/download/202.html

下载以下资源：
- **RKNN-Toolkit2**：模型转换工具（PC 端使用）
- **RKNN 例程**：转换和部署示例代码

### 3.2 GitHub 官方仓库

也可以从 GitHub 获取最新版本：

| 仓库 | 地址 | 说明 |
|------|------|------|
| **RKNN-Toolkit2** | https://github.com/rockchip-linux/rknn-toolkit2 | 模型转换工具 |
| **RKNPU2** | https://github.com/rockchip-linux/rknpu2 | 运行时库和示例 |
| **RKNN Model Zoo** | https://github.com/airockchip/rknn_model_zoo | 预转换模型和示例 |

### 3.3 官方文档

每个仓库都包含详细的文档和使用说明：

- `rknn-toolkit2/doc/` - 转换工具文档
- `rknn_model_zoo/examples/` - 各类模型转换示例
- `rknpu2/examples/` - 推理运行示例

---

## 4. RKNN-Toolkit2 安装

### 4.1 安装步骤

请参考官方仓库的 README 文档进行安装：

> 📖 **安装文档**：https://github.com/rockchip-linux/rknn-toolkit2

主要步骤：
1. 下载对应 Python 版本的 `.whl` 安装包
2. 安装依赖项
3. 使用 `pip install` 安装 RKNN-Toolkit2
4. 验证安装是否成功

### 4.2 验证安装

安装完成后，在 Python 中导入 `rknn.api` 模块，如无报错则安装成功。

---

## 5. 模型转换流程

### 5.1 转换基本步骤

1. **创建 RKNN 对象**
2. **配置参数**（均值、标准差、目标平台等）
3. **加载原始模型**（ONNX/TF/Caffe）
4. **构建 RKNN 模型**（可选量化）
5. **导出 .rknn 文件**
6. **释放资源**

### 5.2 关键参数说明

| 参数 | 说明 |
|------|------|
| `target_platform` | 目标芯片，设为 `rk3588` |
| `mean_values` | 输入图像均值（与训练时保持一致） |
| `std_values` | 输入图像标准差（与训练时保持一致） |
| `do_quantization` | 是否量化：`True` (INT8) / `False` (FP16) |
| `dataset` | 量化校准数据集文件路径 |

### 5.3 量化数据集（可选）

如需 INT8 量化，需要准备校准数据集：
- 创建一个文本文件，每行写一个图片路径
- 建议准备 100-500 张有代表性的图片
- 图片应覆盖实际使用场景

### 5.4 使用官方示例

**强烈建议**参考 RKNN Model Zoo 中的转换示例：

> 📖 **示例地址**：https://github.com/airockchip/rknn_model_zoo/tree/main/examples

每个模型示例目录下都有：
- `model/` - 模型文件和转换脚本
- `python/` - Python 转换代码
- `cpp/` - C++ 推理代码
- `README.md` - 详细说明

---

## 6. 部署到开发板

### 6.1 复制模型文件

将转换好的 `.rknn` 模型文件复制到项目里；pc端转换的需要复制到开发板。

### 6.2 开发板环境准备

1. 从 GitHub 克隆 RKNPU2 仓库到开发板
2. 设置运行时库的环境变量
3. 参考 `rknpu2/examples/` 中的示例运行推理

### 6.3 运行官方示例

RKNN Model Zoo 提供了编译好的示例程序，可直接在开发板上运行测试：

> 📖 **运行说明**：https://github.com/airockchip/rknn_model_zoo

---

## 7. 常见问题

### Q1: 转换时报错 "Unsupported op"

**原因**：模型中包含 RKNN 不支持的算子

**解决方案**：
- 查看 RKNN 支持的算子列表（官方文档）
- 更新 RKNN-Toolkit2 版本
- 修改模型结构，替换不支持的算子

### Q2: 量化后精度下降严重

**解决方案**：
- 增加量化校准数据集的图片数量
- 使用更有代表性的校准图片
- 尝试 FP16 替代 INT8

### Q3: 模型加载失败

**可能原因**：
- 转换时的目标平台不匹配
- RKNN 版本不兼容

**解决方案**：
- 确保转换时指定正确的 `target_platform`
- 使用匹配版本的工具和运行时

### Q4: 推理速度慢

**可能原因**：
- 模型未正确使用 NPU
- 数据传输开销大

**解决方案**：
- 检查 NPU 负载是否正常
- 参考官方示例优化代码

---

## 📚 参考资源

### 官方资源

| 资源 | 链接 |
|------|------|
| **Firefly 资料下载** | https://www.t-firefly.com/doc/download/202.html |
| **RKNN-Toolkit2** | https://github.com/rockchip-linux/rknn-toolkit2 |
| **RKNPU2** | https://github.com/rockchip-linux/rknpu2 |
| **RKNN Model Zoo** | https://github.com/airockchip/rknn_model_zoo |

### 支持的模型类型

RKNN Model Zoo 提供了以下类型的预转换模型和示例：

| 类别 | 模型 |
|------|------|
| **目标检测** | YOLOv5, YOLOv7, YOLOv8, YOLOX |
| **图像分类** | ResNet, MobileNet, EfficientNet |
| **人脸检测** | RetinaFace, SCRFD |
| **人脸识别** | ArcFace, MobileFaceNet |
| **姿态估计** | HRNet, Lite-HRNet |
| **语义分割** | DeepLabV3, PP-LiteSeg |
| **文字识别** | PPOCR, CRNN |

---

## 💡 学习建议

1. **先跑通官方示例**：不要急于转换自己的模型，先把 RKNN Model Zoo 的示例跑通
2. **理解预处理参数**：`mean_values` 和 `std_values` 必须与模型训练时一致
3. **善用 AI 助手**：遇到报错或不理解的地方，可以询问：
   - [ChatGPT](https://chat.openai.com/)
   - [DeepSeek](https://chat.deepseek.com/)
   - [通义千问](https://tongyi.aliyun.com/)
   - [豆包](https://www.doubao.com/)
4. **查看 GitHub Issues**：很多问题别人已经遇到过，搜索 Issues 往往能找到答案

---

<div align="center">

**祝模型转换顺利！**

</div>
