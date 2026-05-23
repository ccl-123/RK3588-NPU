# OpenAI 兼容接口接入说明

> 版本: v1.0
> 更新日期: 2026-04-01

## 目的

当前项目的远端 AI Agent 使用 OpenAI 兼容 Chat Completions 接口。

只要你的服务兼容 OpenAI Chat Completions 基本协议，就可以作为远端后端，而不需要改业务代码。

适用对象包括：

- llama.cpp server
- vLLM / SGLang 提供的 OpenAI 兼容接口
- One API / New API / LiteLLM 之类的网关
- 公司内部封装但兼容 `/v1/chat/completions` 的代理服务

## 协议对齐方式

核心代码位于：

- [config.h](/home/cl/EC-A3588Q/face_attendance/RK3588-NPU/C++/face_recognition_cap/include/config/config.h)
- [ai_analysis_service.cc](/home/cl/EC-A3588Q/face_attendance/RK3588-NPU/C++/face_recognition_cap/src/gui_services/ai_analysis_service.cc)

当前行为如下：

- 当 `LLAMA_CPP_SERVER_URL` 为空时，远端模式不可用，界面会提示先配置服务地址
- 当 `LLAMA_CPP_SERVER_URL` 非空时，远端模式请求：

```text
<LLAMA_CPP_SERVER_URL>/v1/chat/completions
```

请求体按 OpenAI Chat Completions 风格组织，主要字段包括：

- `model`
- `stream`
- `messages`

如果配置了 `LLAMA_CPP_SERVER_API_KEY`，代码会自动加：

```http
Authorization: Bearer <API_KEY>
```

## 必要环境变量

### 1. 服务地址

```bash
export LLAMA_CPP_SERVER_URL=http://127.0.0.1:8080
```

说明：

- 只写基地址
- 不要自己补 `/v1/chat/completions`
- 不要带结尾斜杠更稳

常见示例：

```bash
export LLAMA_CPP_SERVER_URL=http://127.0.0.1:8080
export LLAMA_CPP_SERVER_URL=http://10.35.105.23:8317
export LLAMA_CPP_SERVER_URL=https://your-proxy.example.com
```

### 2. 模型名

```bash
export LLAMA_CPP_SERVER_MODEL=gpt-4o-mini
```

说明：

- 该值会直接写入 OpenAI 请求体的 `model`
- 必须与目标服务真实支持的模型名一致

例如：

```bash
export LLAMA_CPP_SERVER_MODEL=gpt-5.4
export LLAMA_CPP_SERVER_MODEL=gpt-4o-mini
export LLAMA_CPP_SERVER_MODEL=qwen2.5-7b-instruct
```

### 3. API Key

```bash
export LLAMA_CPP_SERVER_API_KEY=sk-your-key
```

说明：

- 如果目标服务不需要鉴权，可以不配置
- 如果需要鉴权，代码会自动注入 `Authorization: Bearer ...`

## 本地与远端的区别

要区分这两组变量：

- `LOCAL_LLM_MODEL_PATH`
  作用于 RK3588 本地 RKLLM 模型
- `LLAMA_CPP_SERVER_*`
  作用于远端 OpenAI 兼容大模型接口

也就是说，本地 Agent 与远端 Agent 不是一回事，配置不要混用。

## 推荐导出方式

临时测试：

```bash
export LLAMA_CPP_SERVER_URL=http://127.0.0.1:8080
export LLAMA_CPP_SERVER_MODEL=gpt-4o-mini
export LLAMA_CPP_SERVER_API_KEY=sk-your-key
```

持久化到 shell：

```bash
cat >> ~/.bashrc <<'EOF'
export LLAMA_CPP_SERVER_URL=http://127.0.0.1:8080
export LLAMA_CPP_SERVER_MODEL=gpt-4o-mini
export LLAMA_CPP_SERVER_API_KEY=sk-your-key
EOF

source ~/.bashrc
```

## 常见问题

### 1. 为什么变量名还是 `LLAMA_CPP_*`？

这是历史命名。当前代码按“OpenAI 兼容接口”处理，不要求后端必须是 llama.cpp。

### 2. 为什么远端模式提示未配置服务地址？

先检查：

```bash
echo "$LLAMA_CPP_SERVER_URL"
```

如果为空，说明变量没有导出成功。

### 3. URL 需要写完整接口路径吗？

不需要。只写基地址，代码会自动拼 `/v1/chat/completions`。

### 4. 系统提示词在哪里生效？

Agent 会直接在 OpenAI 兼容请求内容中携带自己的系统提示与工具定义。

## 当前远端 Agent 工具能力

在 OpenAI 兼容远端模式下，运行时默认可见的工具主要包括：

- `query_attendance`
- `lookup_user_attendance`
- `lookup_department_attendance`
- `lookup_attendance_ranking`
- `lookup_missing_attendance`
- `query_user`
- `system_info`
- `help`
- `calculator`

对应用户问题示例：

- “今天多少人打卡”
- “张三这周出勤怎么样”
- “研发部今天出勤率多少”
- “本月迟到最多的是谁”
- “今天谁还没签到”

## 建议

如果项目要长期支持“自定义大模型接口”，建议把下面 3 个变量写进部署脚本或 systemd Environment 文件：

1. `LLAMA_CPP_SERVER_URL`
2. `LLAMA_CPP_SERVER_MODEL`
3. `LLAMA_CPP_SERVER_API_KEY`

否则后期排查“为什么没切到自定义模型”“为什么返回 401”“为什么命中了错误模型”会很慢。
