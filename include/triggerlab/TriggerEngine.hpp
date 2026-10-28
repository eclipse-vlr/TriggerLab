#pragma once

#include <chrono>
#include <cstdint>

namespace triggerlab {

using Clock = std::chrono::steady_clock;

struct EngineConfig {
    std::chrono::milliseconds cooldown{180};
    int stableFrames = 2;
};

enum class TriggerState {
    Idle,
    Armed,
    Detected,
    Cooldown,
};

struct TriggerInput {
    bool active = false;
    bool targetVisible = false;
};

struct TriggerOutput {
    TriggerState state = TriggerState::Idle;
    bool fired = false; // Only a simulated event; never generates OS input.
    std::uint64_t shotCount = 0;
};

class TriggerEngine {
  public:
    explicit TriggerEngine(EngineConfig config = {});
    void setConfig(EngineConfig config) noexcept;
    [[nodiscard]] const EngineConfig &config() const noexcept {
        return config_;
    }
    [[nodiscard]] TriggerOutput step(TriggerInput input, Clock::time_point now) noexcept;
    void reset() noexcept;
    [[nodiscard]] TriggerState state() const noexcept {
        return state_;
    }
    [[nodiscard]] std::uint64_t shotCount() const noexcept {
        return shotCount_;
    }

  private:
    EngineConfig config_;
    TriggerState state_ = TriggerState::Idle;
    Clock::time_point nextAllowedShot_{};
    int consecutiveVisibleFrames_ = 0;
    std::uint64_t shotCount_ = 0;
};

} // namespace triggerlab
