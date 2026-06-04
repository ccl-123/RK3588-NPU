/**
 * @file npu_resource_manager.h
 * @brief RK3588 NPU 资源集中仲裁器
 */

#ifndef NPU_RESOURCE_MANAGER_H
#define NPU_RESOURCE_MANAGER_H

#include <mutex>
#include <string>

enum class NpuResourceState {
    IDLE,
    VISION_ACTIVE,
    LLM_ACTIVE
};

class NpuResourceManager {
public:
    static NpuResourceManager& instance();

    bool request_vision(const char* reason);
    bool request_llm(const char* reason);

    void release_vision(const char* reason);
    void release_llm(const char* reason);

    NpuResourceState state() const;
    std::string active_reason() const;

    static const char* state_name(NpuResourceState state);

private:
    NpuResourceManager() = default;

    bool request_locked(NpuResourceState requested_state, const char* reason);
    void release_locked(NpuResourceState expected_state, const char* reason);

    mutable std::mutex mutex_;
    NpuResourceState state_{NpuResourceState::IDLE};
    std::string active_reason_;
};

#endif // NPU_RESOURCE_MANAGER_H
