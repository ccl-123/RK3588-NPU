# RK3588 离线语音 TTS 播报部署与架构说明

本文档详细说明在 RK3588 开发板上基于 Sherpa-ONNX 部署的离线流式 TTS（语音合成）系统架构、模型配置及使用方法。

---

## 1. 简介

系统全新升级了语音播报引擎，**彻底抛弃并替换了旧版 `data/voice` 目录下的固化静态 `.wav` 音频文件**。
采用基于 `sherpa-onnx` 的离线 TTS 引擎，能够根据考勤打卡事件，实时动态合成包含员工真实姓名与考勤状态的个性化语音提示音（如：“*张三，签到成功，祝您生活愉快！*”）。

---

## 2. 核心架构与设计

### 2.1 异步工作线程初始化
TTS 引擎加载及模型初始化由后台单独的工作线程（`TtsWorkerThread`）异步完成，主 GUI 线程保持 60fps 顺畅运行。系统启动时，开机 splash 画面同步展示 TTS 异步初始化进度，视频预览无需阻塞等待 TTS 即可快速呈现。

### 2.2 音频播放队列与设备管理 (`AudioManager`)
- **线程安全队列**：`AudioManager` 内置互斥锁保护的音频播放任务队列，避免并发播放导致的音频设备竞争或破音。
- **独立播放线程**：采用 `aplay` 系统工具异步播放合成后的 WAV 音频，播放完成后自动清理共享内存或临时文件。
- **设备自动检测**：启动时自动检测系统可用的 ALSA 音频输出设备。

---

## 3. 模型配置与路径

### 3.1 TTS 模型与下载地址
系统采用 `vits-piper-zh_CN-huayan-medium` 中文高质量离线 TTS 语音合成模型。

- **模型名称**：`vits-piper-zh_CN-huayan-medium`
- **官方下载地址**：[https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-zh_CN-huayan-medium.tar.bz2](https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-zh_CN-huayan-medium.tar.bz2)

### 3.2 放置位置
下载后解压，请将模型文件放置在项目及部署目录下的 `data/model/tts/` 目录中：
```text
install/face_recognition_cap/
└── data/
    └── model/
        └── tts/ (即 vits-piper-zh_CN-huayan-medium 目录下的内容)
            ├── zh_CN-huayan-medium.onnx  # VITS 语音合成主模型
            ├── tokens.txt                # 字符/音素 Token 映射
            ├── lexicon.txt               # 词典映射文件 (若有)
            └── espeak-ng-data/           # 语言音素支持数据目录
```

> [!NOTE]
> 系统支持自动相对路径与绝对路径解析。无论在开发编译目录还是板端安装目录，系统启动时均会在 `data/model/tts/` 路径下自动扫描并装载模型。

---

## 4. 业务集成与触发机制

当人脸识别流水线成功识别到打卡人员时，`AttendanceService` 会向 GUI 抛出考勤通知。`RecognitionPage` 和 `AudioManager` 接收到通知后：
1. 提取员工在 SQLite 数据库中的真实姓名（如：“陈亮”）。
2. 匹配打卡类型（签到/签退）与考勤状态（正常/迟到/早退）。
3. 动态拼接播报文本并调用 `TtsServiceLocal::instance()->speakAsync(text)`。
4. TTS 引擎在毫秒级内将文本合成波形数据写入临时内存区并交付 `AudioManager` 异步播放。
