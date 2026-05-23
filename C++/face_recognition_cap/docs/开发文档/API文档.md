# API 文档

> 版本: v2.4
> 更新日期: 2026-04-01

本文档按当前代码实现，概览项目中最常用的核心 API。

## App 层

### FaceRecognitionApp

文件：

- `include/app/face_recognition_app.h`

主要职责：

- 初始化算法模型与线程流水线
- 控制识别主循环
- 提供帧回调与识别结果回调

关键接口：

```cpp
int initialize(const AppConfig& config);
int run();
void set_recognition_callback(RecognitionCallback callback);
void set_frame_callback(FrameCallback callback);
bool reinitialize_camera(const std::string& device_number);
bool extract_feature_from_frame(const cv::Mat& frame, std::vector<float>& feature, cv::Rect* face_box = nullptr);
```

### LocalLLMThread

文件：

- `include/app/local_llm_thread.h`

主要职责：

- 封装 RKLLM C API
- 异步初始化本地模型
- 流式回调本地大模型输出

关键接口：

```cpp
bool initModel(const QString& model_path, int max_new_tokens, int max_context_len);
void requestInference(const QString& prompt);
void abortInference();
void resetContext();
void releaseModelAsync();
```

## Service 层

### AttendanceService

文件：

- `include/service/attendance_service.h`

主要职责：

- 记录考勤
- 自动判断签到 / 签退
- 判定正常 / 迟到 / 早退
- 查询单用户、单日、区间考勤记录与统计

关键接口：

```cpp
int record_attendance(int user_id, const std::string& user_name, float similarity, const std::string& face_image_path = "", int check_type = 1);
bool has_today_check_record(int user_id, int check_type);
int auto_determine_check_type(int user_id, std::time_t current_time);
int determine_status(std::time_t check_time, int check_type);
std::vector<db::AttendanceRecord> query_user_records(int user_id, const std::string& start_date, const std::string& end_date);
std::vector<db::AttendanceRecord> query_records_by_date(const std::string& date);
std::vector<db::AttendanceRecord> query_records_range(const std::string& start_date, const std::string& end_date);
AttendanceStatistics get_statistics(const std::string& date);
std::vector<AttendanceStatistics> get_statistics_range(const std::string& start_date, const std::string& end_date);
```

### UserService

文件：

- `include/service/user_service.h`

主要职责：

- 注册与维护用户
- 管理人脸特征
- 查询用户列表与统计

关键接口：

```cpp
RegistrationResult register_user(const std::string& user_name, ...);
int add_face_feature(int user_id, const std::vector<float>& feature_vector, float quality = 0.0f, const std::string& source_image = "");
bool get_user(int user_id, db::UserInfo& user);
std::vector<db::UserInfo> get_all_users(int status = -1);
int get_feature_count(int user_id);
bool set_user_status(int user_id, bool enabled);
```

## Agent / AI 服务层

### AgentService

文件：

- `include/agent/agent_service.h`

主要职责：

- 注册内置工具
- 持有 `ReactAgent`
- 对外暴露统一 `chat()` 入口

关键接口：

```cpp
void registerBuiltinTools(service::AttendanceService* attendance, service::UserService* user);
QString chat(const QString& user_input, std::function<QString(const QString&)> llm_callback);
void clearHistory();
ToolRegistry* getToolRegistry();
ConversationMemory* getMemory();
QString getToolsJson() const;
```

### AiAnalysisService

文件：

- `include/gui_services/ai_analysis_service.h`

主要职责：

- 管理远端 AI 请求
- 支持 OpenAI 兼容接口
- 通过 `LLAMA_CPP_SERVER_URL` 配置远端 `/v1/chat/completions` 服务
- 负责远端 Agent 调用与流式事件输出

关键接口：

```cpp
void requestAnalysis(const service::AttendanceStatistics& stats, const QString& trend_summary, const QString& detail_records = "", const QString& user_prompt = "", int range_days = 1);
void requestAgentChat(const QString& user_input);
void initializeAgent(service::AttendanceService* attendance_svc, service::UserService* user_svc);
void cancelAnalysis();
bool isAnalyzing() const;
```

关键 signals：

```cpp
void streamEventReady(const agent::AgentStreamEvent& event);
void analysisFinished();
void errorOccurred(const QString& errorMsg);
void agentThinking();
void agentToolCalling(const QString& tool_name);
void agentToolCompleted(const QString& tool_name, const QString& result);
```

### LocalAiAnalysisService

文件：

- `include/gui_services/local_ai_analysis_service.h`

主要职责：

- 管理本地 RKLLM 模型生命周期
- 管理本地 Agent / Chat 模式切换
- 输出统一流式事件

关键接口：

```cpp
bool initializeLocalLLM(const QString& model_path);
void requestAnalysis(const service::AttendanceStatistics& stats, const QString& trend_summary, const QString& detail_records = "", const QString& user_prompt = "", int range_days = 1);
void requestAgentChat(const QString& user_input);
void initializeAgent(service::AttendanceService* attendance_svc, service::UserService* user_svc);
void setAgentMode(bool enabled);
void cancelAnalysis();
```

## 当前内置工具 API

### 全局考勤

- `query_attendance`

### 单员工考勤

- `lookup_user_attendance`

### 部门考勤

- `lookup_department_attendance`

### 排名分析

- `lookup_attendance_ranking`

### 缺卡 / 缺勤

- `lookup_missing_attendance`

### 用户信息

- `query_user`

### 系统信息

- `system_info`

### 辅助工具

- `help`
- `calculator`

## 配置入口

主要配置集中在：

- [config.h](/home/cl/EC-A3588Q/face_attendance/RK3588-NPU/C++/face_recognition_cap/include/config/config.h)

常用环境变量：

```bash
export LOCAL_LLM_MODEL_PATH=/path/to/your_model.rkllm
export LLAMA_CPP_SERVER_URL=http://127.0.0.1:8080
export LLAMA_CPP_SERVER_MODEL=gpt-4o-mini
export LLAMA_CPP_SERVER_API_KEY=sk-your-key
```
