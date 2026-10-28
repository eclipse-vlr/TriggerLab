#pragma once
#include "triggerlab/TriggerEngine.hpp"
#include <algorithm>
#include <optional>

namespace triggerlab {
// Observe fresh samples, then acknowledge only input actually sent to the OS.
// Unknown samples interrupt dwell but never rearm an already clicked hover.
class HoverClickGate {
  public:
    static constexpr std::chrono::milliseconds dwell{70};
    static constexpr std::chrono::milliseconds cooldown{50};
    // GDI capture can exceed 50ms on a composed desktop. Every step still
    // requires a fresh valid frame; longer stalls must restart observation.
    static constexpr std::chrono::milliseconds maximumSampleGap{200};
    void setTiming(std::chrono::milliseconds delay, std::chrono::milliseconds interval) noexcept {
        delay_ = std::clamp(delay, std::chrono::milliseconds{1}, std::chrono::milliseconds{2000});
        interval_ = std::clamp(interval, std::chrono::milliseconds{50}, std::chrono::milliseconds{2000});
        reset();
    }

    bool step(bool valid, bool matching, Clock::time_point now) noexcept {
        ready_ = false;
        if (!valid) {
            since_.reset();
            lastSample_.reset();
            return false;
        }
        if (!matching) {
            since_.reset();
            lastSample_.reset();
            latched_ = false;
            return false;
        }
        if (latched_)
            return false;
        if (!lastSample_ || now < *lastSample_ || now - *lastSample_ > maximumSampleGap)
            since_ = now;
        lastSample_ = now;
        ready_ = now - *since_ >= delay_ && (!lastClick_ || now - *lastClick_ >= interval_);
        return ready_;
    }
    void acknowledgeClick(Clock::time_point now) noexcept {
        if (!ready_)
            return;
        latched_ = true;
        ready_ = false;
        since_.reset();
        lastSample_.reset();
        lastClick_ = now;
    }
    void reset() noexcept {
        since_.reset();
        lastSample_.reset();
        lastClick_.reset();
        latched_ = ready_ = false;
    }
    bool clickedHover() const noexcept {
        return latched_;
    }
    std::chrono::milliseconds elapsed(Clock::time_point now) const noexcept {
        return since_ ? std::chrono::duration_cast<std::chrono::milliseconds>(now - *since_)
                      : std::chrono::milliseconds{0};
    }

  private:
    std::optional<Clock::time_point> since_, lastSample_, lastClick_;
    bool latched_ = false, ready_ = false;
    std::chrono::milliseconds delay_{dwell}, interval_{cooldown};
};
} // namespace triggerlab
