#include "triggerlab/TriggerEngine.hpp"

#include <algorithm>

namespace triggerlab {

TriggerEngine::TriggerEngine(EngineConfig config) { setConfig(config); }

void TriggerEngine::setConfig(EngineConfig config) noexcept {
    config.cooldown = std::clamp(config.cooldown, std::chrono::milliseconds{50},
                                 std::chrono::milliseconds{2000});
    config.stableFrames = std::clamp(config.stableFrames, 1, 20);
    config_ = config;
}

TriggerOutput TriggerEngine::step(TriggerInput input, Clock::time_point now) noexcept {
    if (!input.active) {
        state_ = TriggerState::Idle;
        consecutiveVisibleFrames_ = 0;
        nextAllowedShot_ = now;
        return {state_, false, shotCount_};
    }

    if (!input.targetVisible) {
        consecutiveVisibleFrames_ = 0;
        state_ = (now < nextAllowedShot_) ? TriggerState::Cooldown : TriggerState::Armed;
        return {state_, false, shotCount_};
    }

    if (now < nextAllowedShot_) {
        state_ = TriggerState::Cooldown;
        return {state_, false, shotCount_};
    }

    if (consecutiveVisibleFrames_ < config_.stableFrames) ++consecutiveVisibleFrames_;
    if (consecutiveVisibleFrames_ < config_.stableFrames) {
        state_ = TriggerState::Detected;
        return {state_, false, shotCount_};
    }

    // Deliberately does not inject mouse events or interact with other processes.
    ++shotCount_;
    consecutiveVisibleFrames_ = 0;
    nextAllowedShot_ = now + config_.cooldown;
    state_ = TriggerState::Cooldown;
    return {state_, true, shotCount_};
}

void TriggerEngine::reset() noexcept {
    state_ = TriggerState::Idle;
    nextAllowedShot_ = {};
    consecutiveVisibleFrames_ = 0;
    shotCount_ = 0;
}

} // namespace triggerlab
