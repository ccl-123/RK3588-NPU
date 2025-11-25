/**
 * @file app_constants.h
 * @brief 应用程序配置常量定义
 * @author CL
 * @date 2025-11-25
 * 
 * 集中管理所有配置常量，避免魔法数字散落各处
 */

#pragma once

namespace AppConstants {

// ==================== 识别确认参数 ====================
// 连续识别次数阈值（防止误识别）[🔒 系统级参数，不可在设置页面修改]
constexpr int CONFIRM_THRESHOLD = 5;

// 确认超时时间（毫秒）[🔒 系统级参数，不可在设置页面修改]
constexpr int CONFIRM_TIMEOUT_MS = 3000;

// 最低相似度要求 [⚙️ 可在设置页面修改]
constexpr float DEFAULT_SIMILARITY_THRESHOLD = 0.60f;

// ==================== 音频播放参数 ====================
// 音频播放冷却时间（毫秒，防止重复播放）[🔒 系统级参数，不可修改]
constexpr int AUDIO_COOLDOWN_MS = 10000;

// 陌生人提示音冷却时间（毫秒）[🔒 系统级参数，不可修改]
constexpr int STRANGER_AUDIO_COOLDOWN_MS = 10000;

// 音频队列最大大小 [🔒 系统级参数，不可修改]
constexpr int AUDIO_QUEUE_MAX_SIZE = 5;

// 默认音量（0-100）[⚙️ 可在设置页面修改]
constexpr int DEFAULT_AUDIO_VOLUME = 70;

// ==================== 考勤参数 ====================
// 防重复打卡间隔（秒）[⚙️ 可在设置页面修改]
constexpr int DUPLICATE_CHECK_INTERVAL = 300;

// 默认上班时间 [⚙️ 可在设置页面修改]
constexpr int DEFAULT_WORK_START_HOUR = 9;
constexpr int DEFAULT_WORK_START_MINUTE = 0;

// 默认下班时间 [⚙️ 可在设置页面修改]
constexpr int DEFAULT_WORK_END_HOUR = 18;
constexpr int DEFAULT_WORK_END_MINUTE = 0;

// 默认迟到阈值（分钟）[⚙️ 可在设置页面修改]
constexpr int DEFAULT_LATE_THRESHOLD = 30;

// 默认早退阈值（分钟）[⚙️ 可在设置页面修改]
constexpr int DEFAULT_EARLY_LEAVE_THRESHOLD = 30;

// ==================== UI 尺寸参数 ====================
// 用户信息面板高度（像素）[🔒 UI 固定参数，不可修改]
constexpr int INFO_PANEL_HEIGHT = 140;

// 卡片间距（像素）[🔒 UI 固定参数，不可修改]
constexpr int CARD_SPACING = 24;

// 卡片内边距（像素）[🔒 UI 固定参数，不可修改]
constexpr int CARD_PADDING = 32;

// ==================== 人脸注册参数 ====================
// 最少采集人脸数 [🔒 系统级参数，不可修改]
constexpr int MIN_FACES_TO_REGISTER = 3;

// 最多采集人脸数 [🔒 系统级参数，不可修改]
constexpr int MAX_FACES_TO_REGISTER = 10;

// 人脸质量阈值 [🔒 系统级参数，不可修改]
constexpr float FACE_QUALITY_THRESHOLD = 0.7f;

// ==================== 摄像头参数 ====================
// 默认摄像头设备ID [⚙️ 可在设置页面修改]
constexpr int DEFAULT_CAMERA_ID = 21;

// 默认摄像头类型 [⚙️ 可在设置页面修改]
constexpr const char* DEFAULT_CAMERA_TYPE = "usb";

// 默认启用异步读取（固定，不可修改）
constexpr bool DEFAULT_ASYNC_USB = true;

// ==================== 性能参数 ====================
// FPS 更新间隔（毫秒）[🔒 系统级参数，不可修改]
constexpr int FPS_UPDATE_INTERVAL = 1000;

// 性能报告间隔（帧数）[🔒 系统级参数，不可修改]
constexpr int PERF_REPORT_INTERVAL = 10;

// ==================== 数据库参数 ====================
// 特征向量维度 [🔒 模型固定参数，不可修改]
constexpr int FEATURE_VECTOR_DIM = 512;

} // namespace AppConstants

