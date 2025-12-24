# RK3588 本地 LLM (RKLLM) 部署全流程指南

> **适用设备**: Firefly RK3588 系列 (iCore-3588Q, ROC-RK3588S-PC 等)  
> **核心工具**: rkllm-toolkit (PC端), rkllm-runtime (板端)  
> **更新日期**: 2025-12-25

本文档详细说明如何将一个在大模型平台（如 HuggingFace）上下载的开源 LLM，部署到 RK3588 的 NPU 上完全离线运行。

---

## 1. PC 端环境准备 (模型转换)

在 PC (x86_64 Linux, 推荐 Ubuntu 20.04/22.04) 上搭建模型转换环境。

### 1.1 安装 Conda
如果尚未安装 Miniconda，请先安装：
```bash
wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh
bash Miniconda3-latest-Linux-x86_64.sh
```

### 1.2 创建 RKLLM 虚拟环境
```bash
# 创建名为 rkllm 的环境，指定 python 3.8 (官方推荐)
conda create -n rkllm python=3.8
conda activate rkllm
```

### 1.3 安装 RKLLM Toolkit
从 Rockchip 提供的网盘或 SDK 中获取 `rkllm_toolkit-x.x.x-cp38-cp38-linux_x86_64.whl`。
```bash
pip install rkllm_toolkit-1.1.4-cp38-cp38-linux_x86_64.whl
```

---

## 2. 模型获取与转换

### 2.1 下载原始模型
以 **Qwen2.5-1.5B-Instruct** 为例，从 HuggingFace 或 ModelScope 下载原始 PyTorch 模型。

```bash
# 需先安装 git-lfs
git clone https://www.modelscope.cn/qwen/Qwen2.5-1.5B-Instruct.git
```

### 2.2 编写转换脚本
在模型目录下创建 `export_rkllm.py`：

```python
from rkllm.api import RKLLM

# 1. 初始化
llm = RKLLM()

# 2. 加载模型
# model_path: 原始模型文件夹路径
# device: 'cpu' 或 'cuda' (如果有NVIDIA显卡)
ret = llm.load_huggingface(model = './Qwen2.5-1.5B-Instruct', device='cpu')
if ret != 0:
    print('Load model failed!')
    exit(ret)

# 3. 构建 RKLLM 模型 (量化)
# quantization_bit: 'w8a8' (8位权重8位激活，推荐) 或 'w4a16' (4位权重16位激活)
# target_platform: 'rk3588'
# num_npu_core: 3 (RK3588有3个NPU核心)
ret = llm.build(
    do_quantization=True,
    optimization_level=1,
    quantized_dtype='w8a8',
    target_platform='rk3588',
    num_npu_core=3
)
if ret != 0:
    print('Build model failed!')
    exit(ret)

# 4. 导出模型
ret = llm.export_rkllm("./qwen2.5-1.5b-instruct_w8a8_rk3588.rkllm")
if ret != 0:
    print('Export model failed!')
    exit(ret)

print("Export success!")
```

### 2.3 执行转换
```bash
python export_rkllm.py
```
转换成功后，你将得到一个 `qwen2.5-1.5b-instruct_w8a8_rk3588.rkllm` 文件。

---

## 3. C++ 交叉编译 (接入项目)

要将 LLM 集成到我们的人脸识别项目中，需要进行交叉编译。

### 3.1 准备交叉编译工具链
确保已安装 `aarch64-linux-gnu-gcc` 和 `g++` (推荐版本 11+)。
```bash
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

### 3.2 确认依赖库
项目目录中必须包含 RKLLM 的运行时库和头文件：
- 头文件: `C++/rkllm_runtime/include/rkllm.h`
- 库文件: `C++/rkllm_runtime/lib/librkllmrt.so`

### 3.3 编译项目
使用项目根目录下的 `cross_build.sh`：

```bash
cd /home/cl/EC-A3588Q/face_attendance/RK3588-NPU/C++/face_recognition_cap
./cross_build.sh
```
脚本会自动链接 `librkllmrt.so` 并生成可执行文件到 `install/` 目录。

---

## 4. RK3588 板端部署

### 4.1 检查 NPU 驱动版本
在 RK3588 上执行：
```bash
dmesg | grep -i rknpu
# 或
cat /proc/device-tree/rknpu/status
```
确保 NPU 驱动版本 >= 0.9.6。如果版本过低，可能需要更新固件。

### 4.2 传输文件清单
将以下文件通过 `scp` 传输到开发板 `/home/firefly/open_project/` 目录下：

| 文件类型 | 文件名 | 来源 | 目标路径 |
| :--- | :--- | :--- | :--- |
| **可执行程序** | `face_recognition_cap_gui` | 编译产物 (`install/`) | `~/open_project/.../install/` |
| **运行时库** | `librkllmrt.so` | RKLLM SDK | `~/open_project/.../install/lib/` |
| **RKNN库** | `librknnrt.so` | RKNN SDK | `~/open_project/.../install/lib/` |
| **模型文件** | `qwen...rk3588.rkllm` | PC 转换产物 | `~/open_project/` (绝对路径需在代码中配置) |

```bash
# 示例传输命令
scp qwen2.5-1.5b-instruct_w8a8_rk3588.rkllm firefly@192.168.1.103:/home/firefly/open_project/
scp -r install/face_recognition_cap firefly@192.168.1.103:/home/firefly/open_project/edge2-npu/C++/
```

### 4.3 运行配置
在板端运行前，**必须设置 LD_LIBRARY_PATH**，否则会报错找不到 `librkllmrt.so`。

```bash
# SSH 登录板端
ssh firefly@192.168.1.103
cd /home/firefly/open_project/edge2-npu/C++/face_recognition_cap/install

# 设置环境变量 (包含当前目录下的 lib 文件夹)
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

# 启动程序
./face_recognition_cap_gui -platform xcb
```

---

## 5. 关键注意事项

1.  **NPU 资源互斥**:
    *   RKNN (视觉) 和 RKLLM (语言) **不能同时运行**。
    *   我们的程序已实现自动切换：进入智能看板时会自动调用 `rknn_destroy` 释放视觉模型，然后延迟 500ms 再 `rkllm_init`。
    *   **切勿**在后台手动运行其他占用 NPU 的程序。

2.  **内存占用**:
    *   2B 参数量的模型 (W8A8) 运行时约占用 2-3GB 内存。请确保系统有足够的剩余内存。

3.  **系统提示词 (Prompt)**:
    *   RKLLM 对系统提示词的支持依赖于模型本身的训练方式。建议将系统提示词 (`<|im_start|>system...`) 直接拼接在用户输入前，或者使用 `rkllm_set_chat_template` 接口 (本项目已封装)。

4.  **上下文长度**:
    *   转换模型时设置的长度是硬限制 (如 4096)。如果输入的考勤数据历史记录过长，会导致 `RKLLM_ERR_CONTEXT_FULL` 错误。

---

## 6. 故障排查速查表

| 现象 | 可能原因 | 解决方案 |
| :--- | :--- | :--- |
| **程序启动报错 `libxxx.so not found`** | `LD_LIBRARY_PATH` 未设置 | `export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH` |
| **点击本地模型后程序闪退** | 1. 找不到模型文件 <br> 2. NPU 资源未释放 | 1. 检查 `config.h` 路径配置 <br> 2. 确保之前已执行 `rknn_destroy` |
| **推理输出乱码** | 模型量化参数不对或 Chat Template 错误 | 检查 `rkllm_set_chat_template` 参数 |
| **推理速度极慢 (>5分钟)** | RKNN 线程未完全停止，与 RKLLM 抢占 NPU | 确保 `release_models()` 中停止了所有识别线程 |
