/**
 * @file npu_resource_manager.cc
 * @brief RK3588 NPU 资源集中仲裁器实现
 */

#include "app/npu_resource_manager.h"

#include <spdlog/spdlog.h>

NpuResourceManager& NpuResourceManager::instance() {
    static NpuResourceManager manager;
    return manager;
}

bool NpuResourceManager::request_vision(const char* reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    return request_locked(NpuResourceState::VISION_ACTIVE, reason);
}

bool NpuResourceManager::request_llm(const char* reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    return request_locked(NpuResourceState::LLM_ACTIVE, reason);
}

void NpuResourceManager::release_vision(const char* reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    release_locked(NpuResourceState::VISION_ACTIVE, reason);
}

void NpuResourceManager::release_llm(const char* reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    release_locked(NpuResourceState::LLM_ACTIVE, reason);
}

NpuResourceState NpuResourceManager::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string NpuResourceManager::active_reason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_reason_;
}

const char* NpuResourceManager::state_name(NpuResourceState state) {
    switch (state) {
    case NpuResourceState::IDLE:
        return "IDLE";
    case NpuResourceState::VISION_ACTIVE:
        return "VISION_ACTIVE";
    case NpuResourceState::LLM_ACTIVE:
        return "LLM_ACTIVE";
    default:
        return "UNKNOWN";
    }
}

bool NpuResourceManager::request_locked(NpuResourceState requested_state, const char* reason) {
    const char* request_reason = reason ? reason : "unknown";
    if (state_ == requested_state) {
        active_reason_ = request_reason;
        spdlog::debug("NPU resource already owned by {} ({})", state_name(state_), active_reason_);
        return true;
    }

    if (state_ != NpuResourceState::IDLE) {
        spdlog::warn("NPU resource request denied: requested={}, current={}, owner_reason={}, request_reason={}",
                     state_name(requested_state), state_name(state_), active_reason_, request_reason);
        return false;
    }

    state_ = requested_state;
    active_reason_ = request_reason;
    spdlog::info("NPU resource acquired: state={}, reason={}", state_name(state_), active_reason_);
    return true;
}

void NpuResourceManager::release_locked(NpuResourceState expected_state, const char* reason) {
    const char* release_reason = reason ? reason : "unknown";
    if (state_ == NpuResourceState::IDLE) {
        active_reason_.clear();
        spdlog::debug("NPU resource already idle ({})", release_reason);
        return;
    }

    if (state_ != expected_state) {
        spdlog::warn("NPU resource release ignored: expected={}, current={}, owner_reason={}, release_reason={}",
                     state_name(expected_state), state_name(state_), active_reason_, release_reason);
        return;
    }

    spdlog::info("NPU resource released: state={}, owner_reason={}, release_reason={}",
                 state_name(state_), active_reason_, release_reason);
    state_ = NpuResourceState::IDLE;
    active_reason_.clear();
}
