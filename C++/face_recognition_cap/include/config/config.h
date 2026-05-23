/**
 * @file config.h
 * @brief 全局配置中心
 *
 * 配置分类:
 *   [固定] 编译时常量，修改后需 ./build.sh
 *   [默认] UI 可配置项初始值，运行时从 ConfigManager 读取
 *   [环境] 通过环境变量配置 (敏感信息)
 */

#pragma once

#include <cstdlib>

namespace Config {

// ===== 模型参数 [固定] =====
namespace Model {
    constexpr int FEATURE_DIM = 512;        // FaceNet 特征向量维度
    constexpr int YOLO_INPUT_SIZE = 640;    // YOLO 输入图像尺寸 (正方形)
}

// ===== 路径配置 [固定] =====
// 相对于可执行文件目录
namespace Path {
    constexpr const char* YOLO_MODEL    = "data/model/yolov8n-face.rknn";   // 人脸检测模型
    constexpr const char* FACENET_MODEL = "data/model/w600k_resnet50.rknn"; // 特征提取模型，w600k_mbf.rknn或resnet50
    constexpr const char* FEATURE_LIB   = "data/face_feature_lib/";         // 特征库目录
    constexpr const char* DATABASE      = "data/database/face_recognition.db";
}

// ===== 摄像头 [固定] =====
namespace Camera {
    constexpr int WIDTH = 1280;             // 采集宽度
    constexpr int HEIGHT = 720;             // 采集高度
    constexpr bool USE_ASYNC_USB = true;    // 异步采集模式
}

// ===== 检测阈值 [固定] =====
namespace Detection {
    constexpr float BOX_CONF_THRESHOLD = 0.5f;  // 人脸检测置信度
    constexpr float NMS_THRESHOLD = 0.45f;      // 非极大值抑制阈值
}

// ===== 性能调优 [固定] =====
namespace Performance {
    constexpr int REPORT_INTERVAL = 50;         // 性能报告间隔 (帧)
    constexpr int QUEUE_MAX_SIZE = 2;           // 线程队列容量 (越小延迟越低)
    constexpr bool USE_RGA = true;              // RGA 硬件加速 (Valgrind 调试时设 false)
    constexpr bool ENABLE_PERF_REPORT = false;   // 输出 FPS/延迟日志
}

// ===== 默认值 [UI 可配置] =====
// 运行时应从 ConfigManager 读取
namespace Default {
    // 识别参数
    constexpr float RECOGNITION_THRESHOLD = 0.60f;  // 人脸匹配阈值 [0.0-1.0]
    constexpr int DUPLICATE_CHECK_INTERVAL = 300;   // 防重复打卡间隔 (秒)
    constexpr int RECOGNITION_CONFIRM_COUNT = 5;    // 连续识别确认次数
    constexpr int USER_CONFIRM_DURATION_MS = 1000;  // 身份确认时间 (毫秒)

    // 音频
    constexpr int AUDIO_VOLUME = 100;               // 音量 [0-100]
    constexpr bool AUDIO_ENABLED = true;            // 启用语音播报

    // 考勤规则
    constexpr int WORK_START_HOUR = 9;              // 上班时间
    constexpr int WORK_START_MINUTE = 0;
    constexpr int WORK_END_HOUR = 18;               // 下班时间
    constexpr int WORK_END_MINUTE = 0;
    constexpr int LATE_THRESHOLD = 30;              // 迟到容忍 (分钟)
    constexpr int EARLY_LEAVE_THRESHOLD = 30;       // 早退容忍 (分钟)

    // 硬件
    constexpr int CAMERA_ID = 21;                   // 摄像头设备号 (/dev/video21)

    // 设备标识
    constexpr const char* DEVICE_ID = "device_001";
    constexpr const char* LOCATION = "Main Entrance";

    // 天气/位置 (默认佛山)
    constexpr const char* CITY = "佛山";
    constexpr double LATITUDE = 23.0215;
    constexpr double LONGITUDE = 113.1214;
}

// ===== UI 定时任务 [固定] =====
namespace UI {
    // 数据刷新间隔
    constexpr int WEATHER_REFRESH_INTERVAL_SEC = 600;   // 天气: 10 分钟
    constexpr int SENTENCE_REFRESH_INTERVAL_SEC = 60;   // 每日一句: 1 分钟
    constexpr int NEWS_REFRESH_INTERVAL_SEC = 300;      // 热点新闻: 5 分钟
    constexpr int FACE_CARD_WIDTH = 160;                // 人脸卡片宽度 (px)

    // 新闻跑马灯
    constexpr int NEWS_TICKER_SPEED_PX_PER_SEC = 130;   // 滚动速度 (px/s)
    constexpr int NEWS_TICKER_SPACING = 40;             // 条目间距 (px)
    constexpr int NEWS_TICKER_FONT_SIZE = 15;
    constexpr const char* NEWS_API_BASE = "https://api.freejk.com/shuju/hotlist/";
    constexpr const char* NEWS_SOURCES[] = {
        "baidu", "netease-news", "ithome", "douyin",
        "smzdm", "qq-news", "sina-news", "dgtle"
    };
    constexpr const char* NEWS_SOURCE_LABELS[] = {
        "百度新闻", "网易新闻", "IT之家", "抖音热榜",
        "什么值得买", "腾讯新闻", "新浪新闻", "数字尾巴"
    };
    constexpr const char* NEWS_SOURCE_COLORS[] = {
        "#1890ff", "#fa8c16", "#722ed1", "#eb2f96",
        "#13c2c2", "#52c41a", "#d46b08", "#722ed1"
    };
    constexpr int NEWS_SOURCES_COUNT = 8;
}

// ===== 外部 API [固定] =====
namespace API {
    constexpr const char* LOCATION         = "http://ip-api.com/json/";                          // IP 定位
    constexpr const char* WEATHER_FORECAST = "https://api.open-meteo.com/v1/forecast";           // 天气预报
    constexpr const char* AIR_QUALITY      = "https://air-quality-api.open-meteo.com/v1/air-quality"; // 空气质量
    constexpr const char* DAILY_SENTENCE   = "https://v1.hitokoto.cn/";                          // 每日一言
    constexpr const char* HOLIDAY_BASE     = "https://api.jiejiariapi.com/v1";                   // 节假日
}

// ===== 本地 LLM [固定] =====
// RKLLM 本地推理配置
namespace LocalLLM {
    constexpr const char* MODEL_ENV = "LOCAL_LLM_MODEL_PATH";
    constexpr const char* MODEL_PATH = "/home/firefly/open_project/Qwen3-1.7B_W8A8_RK3588.rkllm";
    inline const char* getModelPath() {
        const char* env_path = std::getenv(MODEL_ENV);
        return (env_path && env_path[0] != '\0') ? env_path : MODEL_PATH;
    }
    constexpr int MAX_NEW_TOKENS = 1028;        // 单次最大生成长度
    constexpr int MAX_CONTEXT_LEN = 1024 * 8;   // 上下文窗口 (tokens)
}

// ===== Agent 配置 [固定] =====
// ReAct Agent 智能助手配置
namespace Agent {
    constexpr int MAX_ITERATIONS = 10;           // ReAct 最大循环次数
    constexpr int CONVERSATION_HISTORY = 10;    // 对话历史保留轮数
    constexpr int LLM_TIMEOUT_MS = 200000;       // LLM 推理超时 (毫秒，200秒)
    constexpr bool STREAM_OUTPUT = true;        // 流式输出模式

    // 工具调用配置
    namespace Tools {
        constexpr bool ENABLE_ATTENDANCE = true;    // 启用考勤查询工具
        constexpr bool ENABLE_USER = true;          // 启用用户查询工具
        constexpr bool ENABLE_SYSTEM = true;        // 启用系统信息工具
    }
}

// ===== 自定义接入（OpenAI 兼容接口）[环境变量] =====
// 用途：
//   远端请求调用 OpenAI 兼容的 /v1/chat/completions 接口。
//
// 当前命名沿用历史变量名 LLAMA_CPP_*，但并不强依赖 llama.cpp。
// 只要服务兼容 OpenAI Chat Completions 基本格式，都可以接入。
//
// 需要配置的内容：
// 1. LLAMA_CPP_SERVER_URL
//    - 服务地址。支持以下格式：
//        a) 基地址 (如 http://127.0.0.1:8080) -> 自动拼接 /v1/chat/completions
//        b) 带版本的地址 (如 https://.../v1) -> 自动拼接 /chat/completions
//        c) 完整 Endpoint (如 https://.../v1/chat/completions) -> 保持原样
//    - 例如：
//        export LLAMA_CPP_SERVER_URL=https://token-plan-cn.xiaomimimo.com/v1
//
// 2. LLAMA_CPP_SERVER_MODEL
//    - 填写请求体中的 model 字段
//    - 该值必须与目标 OpenAI 兼容服务支持的模型名一致
//    - 例如：
//        export LLAMA_CPP_SERVER_MODEL=gpt-5.4
//        export LLAMA_CPP_SERVER_MODEL=gpt-5.2
//        export LLAMA_CPP_SERVER_MODEL=gpt-5.1
//
// 3. LLAMA_CPP_SERVER_API_KEY
//    - 如果你的 OpenAI 兼容服务需要鉴权，就填写这里
//    - 代码会自动注入请求头:
//        Authorization: Bearer <API_KEY>
//    - 例如：
//        export LLAMA_CPP_SERVER_API_KEY=sk-123321
//
// 注意：
// - 如果 LLAMA_CPP_SERVER_URL 为空，则远端 AI 功能不可用，需要先配置 OpenAI 兼容服务地址。
// - 如果你的服务路径不是标准的 /v1/chat/completions，需要继续扩展这里的配置项。
namespace LlamaCpp {
    constexpr const char* BASE_URL_ENV = "LLAMA_CPP_SERVER_URL";
    constexpr const char* MODEL_ENV = "LLAMA_CPP_SERVER_MODEL";
    constexpr const char* API_KEY_ENV = "LLAMA_CPP_SERVER_API_KEY";
    constexpr const char* DEFAULT_BASE_URL = "";
    constexpr const char* DEFAULT_MODEL = "local-llama";

    inline const char* getBaseUrl() {
        static const char* v = std::getenv(BASE_URL_ENV);
        return (v && v[0] != '\0') ? v : DEFAULT_BASE_URL;
    }

    inline const char* getModel() {
        static const char* v = std::getenv(MODEL_ENV);
        return (v && v[0] != '\0') ? v : DEFAULT_MODEL;
    }

    inline const char* getApiKey() {
        static const char* v = std::getenv(API_KEY_ENV);
        return v ? v : "";
    }
}

} // namespace Config
