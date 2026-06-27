# RK3588 本地 ASR 语音识别部署教程

本文档指导你在 RK3588 开发板上部署和配置本地离线 ASR（语音识别）服务，并与系统的智能看板功能集成。

---

## 1. 简介

系统支持**云端 ASR**与**本地离线 ASR**双端无缝切换：
* **云端 ASR**：调用远端云 API，识别速度快，但需要连接外网及配置 `MIMO_ASR_API_KEY` 环境变量。
* **本地 ASR**：基于 `sherpa-onnx` 离线框架，使用量化 Zipformer 中文模型在 RK3588 CPU 上进行本地流式语音识别。

本地 ASR 的模型加载耗时约 **9 秒**，系统已实现**后台异步加载**与**代数防重置保护**，加载期间界面流畅、按钮处于加载保护状态，不会卡顿或弹出系统无响应弹窗。

---

## 2. 模型下载与放置

本地 ASR 使用 `csukuangfj/sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30` 流式识别模型。

### 2.1 下载链接
推荐通过 ModelScope（阿里魔搭社区，国内访问快）或 Hugging Face 下载：
* **ModelScope (推荐国内用户)**:
  [csukuangfj/sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30](https://modelscope.cn/models/csukuangfj/sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/summary)
* **Hugging Face**:
  [csukuangfj/sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30](https://huggingface.co/csukuangfj/sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30)

### 2.2 放置位置
下载完成后，请将整个模型文件夹重命名为 `sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30`，并放置到项目的 `data/model/` 目录下。

具体相对布局关系如下：
```text
install/face_recognition_cap/
├── face_recognition_cap_gui   # GUI 运行主程序
└── data/
    └── model/
        └── sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/
            ├── encoder.int8.onnx  # 编码器模型 (必需)
            ├── decoder.onnx       # 解码器模型 (必需)
            ├── joiner.int8.onnx   # 连接器模型 (必需)
            └── tokens.txt         # 字典文本 (必需)
```

> [!NOTE]
> 程序已实现**动态相对路径解析**。无论从项目根目录启动、还是从 `install/face_recognition_cap/` 部署目录启动，系统都会自动在工作目录、二进制文件同级以及 build 输出相对路径中寻找该文件夹。

---

## 3. 依赖及编译配置

### 3.1 三方依赖说明
* **头文件**：已内置于 `C++/3rdparty/sherpa-onnx/include`。
* **静态链接库**：已内置于 `C++/3rdparty/sherpa-onnx/lib` (包含 `libonnxruntime.a`、`libsherpa-onnx-core.a` 等)。
* **音频采集**：使用系统标准的 ALSA 录音框架，若系统缺失请通过 `sudo apt install libasound2-dev` 进行安装。

### 3.2 编译选项
编译时，系统使用 `ENABLE_LOCAL_SHERPA_ASR` 宏控制本地 ASR：
* **AUTO (默认)**：CMake 会自动检测 `C++/3rdparty/sherpa-onnx` 是否存在必需的头文件和 `.a` 静态库文件。若完整则自动开启，若缺失则降级为只编译云端 ASR，**不会使 Clean Build 挂掉**。
* **ON**：强行编译本地 ASR，如检测到三方依赖缺失会直接报 `FATAL_ERROR` 停止 CMake 配置。
  ```bash
  cmake -DENABLE_LOCAL_SHERPA_ASR=ON ..
  ```
* **OFF**：强行关闭本地 ASR 编译。
  ```bash
  cmake -DENABLE_LOCAL_SHERPA_ASR=OFF ..
  ```

---

## 4. 运行使用

1. **进入看板**：打开 `face_recognition_cap_gui` 应用，切换到“智能看板”页面。
2. **切换后端**：
   * 输入框右侧默认显示“云端ASR”。
   * 点击切换为“本地ASR”，状态栏将显示 `本地ASR加载中...` 并暂时禁用麦克风输入和切换操作。
   * 后台加载线程（耗时约9秒）完成后，状态栏更新为 `本地ASR就绪`，按钮恢复可用。
3. **点击录音**：
   * 录音就绪后，点击麦克风按钮即可开始说话。本地 ASR 将在解码子线程中流式（实时）把你的语音识别转换为文本填入输入框，并在说话完毕后完成输入。

---

## 5. 内存常驻与生命周期优化

* **常驻内存机制**：在系统初始化或首次开启后，ASR 本地模型将持续常驻内存。当用户在“智能看板”、“考勤数据管理”、“用户管理”等页面之间来回切换时，系统仅停止活动录音，不会销毁模型，界面切换 0ms 零卡顿。
* **AI 交互联动**：在智能看板发送 AI 消息或进行对话分析时，系统会自动取消录音并关闭麦克风状态，防止语音识别与大模型输出产生干扰。
