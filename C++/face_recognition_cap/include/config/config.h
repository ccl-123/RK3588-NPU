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
    inline const char* MODEL_PATH = "/home/firefly/open_project/Qwen3-1.7B_W8A8_RK3588.rkllm";
    constexpr int MAX_NEW_TOKENS = 1028;        // 单次最大生成长度
    constexpr int MAX_CONTEXT_LEN = 1024 * 6;   // 上下文窗口 (tokens)
}

// ===== Agent 配置 [固定] =====
// ReAct Agent 智能助手配置
namespace Agent {
    constexpr int MAX_ITERATIONS = 5;           // ReAct 最大循环次数
    constexpr int CONVERSATION_HISTORY = 10;    // 对话历史保留轮数
    constexpr int LLM_TIMEOUT_MS = 60000;       // LLM 推理超时 (毫秒)
    constexpr bool STREAM_OUTPUT = true;        // 流式输出模式

    // 工具调用配置
    namespace Tools {
        constexpr bool ENABLE_ATTENDANCE = true;    // 启用考勤查询工具
        constexpr bool ENABLE_USER = true;          // 启用用户查询工具
        constexpr bool ENABLE_SYSTEM = true;        // 启用系统信息工具
    }
}

// ===== 腾讯云 LLM [环境变量] =====
// 配置方法: export TENCENT_APP_KEY / TENCENT_SECRET_ID / TENCENT_SECRET_KEY
namespace TencentAI {
    constexpr const char* API_URL = "https://wss.lke.cloud.tencent.com/v1/qbot/chat/sse";
    constexpr const char* VISITOR_BIZ_ID = "device_001";

    inline const char* getAppKey() {
        static const char* v = std::getenv("TENCENT_APP_KEY");
        return v ? v : "";
    }
    inline const char* getSecretId() {
        static const char* v = std::getenv("TENCENT_SECRET_ID");
        return v ? v : "";
    }
    inline const char* getSecretKey() {
        static const char* v = std::getenv("TENCENT_SECRET_KEY");
        return v ? v : "";
    }
}

} // namespace Config
