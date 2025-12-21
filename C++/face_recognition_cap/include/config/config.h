/**
 * @file config.h
 * @brief 全局配置常量 - 集中管理所有可调参数
 * @author CL
 * @date 2025-12-04
 * 
 * 配置分类：
 * - [固定] 系统常量，不可通过 UI 修改
 * - [默认] UI 可配置项的默认值，运行时从 ConfigManager 读取
 * 
 * 使用方法：
 *   #include "config/config.h"
 *   int dim = Config::Model::FEATURE_DIM;  // 固定常量
 *   float threshold = Config::Default::RECOGNITION_THRESHOLD;  // 默认值
 */

#pragma once

#include <cstdlib>  // for std::getenv

namespace Config {

// ==================== 路径配置 [固定] ====================
namespace Path {
    // 模型路径 (相对于可执行文件目录)
    constexpr const char* YOLO_MODEL = "data/model/yolov8n-face.rknn";
    constexpr const char* FACENET_MODEL = "data/model/w600k_mbf.rknn";
    
    // 数据路径（单库）
    constexpr const char* FEATURE_LIB = "data/face_feature_lib/";
    constexpr const char* DATABASE = "data/database/face_recognition.db";
}

// ==================== 模型参数 [固定] ====================
namespace Model {
    constexpr int FEATURE_DIM = 512;               // 特征向量维度 (MobileFaceNet)
    constexpr int YOLO_INPUT_SIZE = 640;           // YOLO 输入尺寸
}

// ==================== 摄像头参数 [固定] ====================
namespace Camera {
    constexpr int WIDTH = 1280;                    // 摄像头宽度
    constexpr int HEIGHT = 720;                    // 摄像头高度
    constexpr bool USE_ASYNC_USB = true;           // 异步USB读取 (固定开启)
}

// ==================== 性能参数 [固定] ====================
namespace Performance {
    constexpr int REPORT_INTERVAL = 50;            // 性能报告间隔 (帧数)
    constexpr int QUEUE_MAX_SIZE = 2;              // 线程队列最大大小
    constexpr bool USE_RGA = true;                // 是否启用RGA硬件加速 (禁用可避免Valgrind警告)
}

// ==================== UI/定时任务参数 [固定] ====================
namespace UI {
    constexpr int WEATHER_REFRESH_INTERVAL_SEC = 600;  // 天气刷新间隔 (秒) - 10分钟
    constexpr int SENTENCE_REFRESH_INTERVAL_SEC = 60;  // 每日一句刷新间隔 (秒) - 1分钟
    constexpr int NEWS_REFRESH_INTERVAL_SEC = 300;     // 热点滚动刷新间隔 (秒) - 5分钟
    constexpr int FACE_CARD_WIDTH = 160;               // 人脸检测状态卡片固定宽度 (px)

    // 热点跑马灯参数
    constexpr int NEWS_TICKER_SPEED_PX_PER_SEC = 130;   // 滚动速度（像素/秒）
    constexpr int NEWS_TICKER_SPACING = 40;             // 离屏后与下一条的间隔像素
    constexpr int NEWS_TICKER_FONT_SIZE = 15;           // 字号
    constexpr const char* NEWS_API_BASE = "https://api.freejk.com/shuju/hotlist/";  // 热榜服务基地址
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

// ==================== 外部 API 端点 [固定] ====================
namespace API {
    constexpr const char* LOCATION = "http://ip-api.com/json/";                     // IP 定位
    constexpr const char* WEATHER_FORECAST = "https://api.open-meteo.com/v1/forecast";  // 天气/UV
    constexpr const char* AIR_QUALITY = "https://air-quality-api.open-meteo.com/v1/air-quality"; // AQI
    constexpr const char* DAILY_SENTENCE = "https://v1.hitokoto.cn/";               // 每日一言
    constexpr const char* HOLIDAY_BASE = "https://api.jiejiariapi.com/v1";          // 节假日API
}

// ==================== 检测参数 [固定] ====================
namespace Detection {
    constexpr float BOX_CONF_THRESHOLD = 0.5f;     // 人脸检测置信度阈值
    constexpr float NMS_THRESHOLD = 0.45f;         // NMS阈值
}

// ==================== 默认值 [UI 可配置] ====================
// 这些值仅作为 ConfigManager 的初始默认值
// 运行时应从 ConfigManager 读取用户设置
namespace Default {
    // 识别设置
    constexpr float RECOGNITION_THRESHOLD = 0.60f; // 人脸识别相似度阈值
    constexpr int DUPLICATE_CHECK_INTERVAL = 300;  // 防重复打卡间隔 (秒)
    constexpr int RECOGNITION_CONFIRM_COUNT = 5;   // 识别确认次数
    constexpr int USER_CONFIRM_DURATION_MS = 1000; // 用户确认时间 (毫秒)

    // 音频设置
    constexpr int AUDIO_VOLUME = 100;               // 默认音量 (0-100)
    constexpr bool AUDIO_ENABLED = true;           // 默认启用音频

    // 考勤设置
    constexpr int WORK_START_HOUR = 9;             // 上班时间 (时)
    constexpr int WORK_START_MINUTE = 0;           // 上班时间 (分)
    constexpr int WORK_END_HOUR = 18;              // 下班时间 (时)
    constexpr int WORK_END_MINUTE = 0;             // 下班时间 (分)
    constexpr int LATE_THRESHOLD = 30;             // 迟到阈值 (分钟)
    constexpr int EARLY_LEAVE_THRESHOLD = 30;      // 早退阈值 (分钟)

    // 摄像头设置
    constexpr int CAMERA_ID = 21;                  // 默认摄像头ID

    // 设备信息
    constexpr const char* DEVICE_ID = "device_001";    // 设备ID
    constexpr const char* LOCATION = "Main Entrance";  // 设备位置
}

// 腾讯云 AI 配置
namespace TencentAI {
    // SSE 接口地址（修复：使用正确的腾讯云 SSE 接口地址）
    constexpr const char* API_URL = "https://wss.lke.cloud.tencent.com/v1/qbot/chat/sse";

    // 访客 ID（用于标识设备/用户）
    constexpr const char* VISITOR_BIZ_ID = "device_001";

    // 从环境变量读取敏感配置
    // 环境变量名：TENCENT_APP_KEY, TENCENT_SECRET_ID, TENCENT_SECRET_KEY
    // 使用方法：在 ~/.bashrc 中设置这些环境变量
    inline const char* getAppKey() {
        static const char* key = std::getenv("TENCENT_APP_KEY");
        return key ? key : "";
    }

    inline const char* getSecretId() {
        static const char* id = std::getenv("TENCENT_SECRET_ID");
        return id ? id : "";
    }

    inline const char* getSecretKey() {
        static const char* key = std::getenv("TENCENT_SECRET_KEY");
        return key ? key : "";
    }
}

} // namespace Config
