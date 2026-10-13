#pragma once
#include "triggerlab/TriggerEngine.hpp"
#include <optional>

namespace triggerlab {
// Observe fresh samples, then acknowledge only input actually sent to the OS.
// Unknown samples interrupt dwell but never rearm an already clicked hover.
class HoverClickGate {
public:
    static constexpr std::chrono::milliseconds dwell{70};
    static constexpr std::chrono::milliseconds cooldown{50};
    static constexpr std::chrono::milliseconds maximumSampleGap{50};

    bool step(bool valid, bool matching, Clock::time_point now) noexcept {
        ready_ = false;
        if (!valid) { since_.reset(); lastSample_.reset(); return false; }
        if (!matching) {
            since_.reset(); lastSample_.reset(); latched_ = false;
            return false;
        }
        if (latched_) return false;
        if (!lastSample_ || now < *lastSample_ || now - *lastSample_ > maximumSampleGap)
            since_ = now;
        lastSample_ = now;
        ready_ = now - *since_ >= dwell && (!lastClick_ || now - *lastClick_ >= cooldown);
        return ready_;
    }
    void acknowledgeClick(Clock::time_point now) noexcept {
        if (!ready_) return;
        latched_ = true; ready_ = false;
        since_.reset(); lastSample_.reset(); lastClick_ = now;
    }
    void reset() noexcept {
        since_.reset(); lastSample_.reset(); lastClick_.reset();
        latched_ = ready_ = false;
    }
private:
    std::optional<Clock::time_point> since_, lastSample_, lastClick_;
    bool latched_ = false, ready_ = false;
};
}
