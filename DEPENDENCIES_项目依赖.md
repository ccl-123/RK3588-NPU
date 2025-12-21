# Project Dependencies & Build Requirements / 项目依赖与构建要求

This document outlines the minimal and locked dependencies required to build the **Face Recognition System** on the Rockchip RK3588 platform.
本文档列出了在瑞芯微 RK3588 平台上构建**人脸识别考勤系统**所需的最小及锁定依赖项。

---

## 1. Core Build Tools / 核心构建工具

| Tool / 工具 | Version / 版本 | Requirement / 要求 |
| :--- | :--- | :--- |
| **CMake** | `3.14.0`+ | Minimum required / 最低版本要求 |
| **GCC/G++** | `9.4.0`+ | Supports C++17 (Ubuntu 20.04+) / 支持 C++17 标准 |
| **Make** | `4.2`+ | Standard build tool / 标准构建工具 |

---

## 2. System Libraries (Ubuntu/Debian) / 系统库 (Ubuntu/Debian)

Install these using `apt-get` for a minimal setup.
使用 `apt-get` 进行最小化安装。

| Library / 库 | Package Name / 软件包名 | Locked Version / 参考锁定版本 |
| :--- | :--- | :--- |
| **OpenCV** | `libopencv-dev` | `4.5.4` (or 4.2+ from repo) |
| **SQLite3** | `libsqlite3-dev` | `3.31.1`+ |
| **Qt5 Core** | `qtbase5-dev` | `5.12.8` (Ubuntu 20.04) / `5.15.2` |
| **Qt5 SVG** | `libqt5svg5-dev` | `5.12.8`+ |
| **Qt5 Multimedia** | `qtmultimedia5-dev` | `5.12.8`+ |
| **SPDLog** | `libspdlog-dev` | `1.5.0`+ (Header-only preferred) |

### Minimal Installation Command / 最小化安装命令
```bash
sudo apt-get update && sudo apt-get install -y --no-install-recommends \
    cmake \
    build-essential \
    libopencv-dev \
    libsqlite3-dev \
    qtbase5-dev \
    libqt5svg5-dev \
    qtmultimedia5-dev \
    libqt5network5 \
    libspdlog-dev
```

---

## 3. Vendor-Specific Dependencies (Rockchip) / 厂商特定依赖 (Rockchip)

These libraries are platform-specific and must match your board's BSP/Kernel version.
这些库是特定于平台的，必须与您的开发板 BSP/内核版本匹配。

| Component / 组件 | Library File / 库文件 | Header Path / 头文件路径 |
| :--- | :--- | :--- |
| **RKNN Runtime** | `librknnrt.so` | `C++/runtime/librknn_api/include` |
| **RGA** | `librga.so` | `C++/3rdparty/rga/include` |

**Note on Version Locking / 版本锁定说明:**
- **RKNN**: Ensure `librknnrt.so` version matches the model conversion tool version (e.g., v1.6.0). Mismatches will cause inference failures.
  请确保 `librknnrt.so` 版本与模型转换工具版本匹配（如 v1.6.0），不匹配会导致推理失败。
- **RGA**: Use the version provided by your board vendor (Firefly/Rockchip) to ensure compatibility with the DRM driver.
  请使用开发板厂商（Firefly/Rockchip）提供的版本，以确保与 DRM 驱动的兼容性。