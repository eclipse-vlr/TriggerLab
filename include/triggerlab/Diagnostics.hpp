#pragma once
#include "triggerlab/TriggerEngine.hpp"
#include <deque>
#include <optional>
#include <string>
#include <vector>
namespace triggerlab {
struct SessionEvent { double elapsedMs; bool active; bool targetVisible; TriggerState state; bool fired; std::uint64_t shots; };
class Diagnostics {
public:
    void observe(TriggerInput input, TriggerOutput output, Clock::time_point now);
    void reset() noexcept;
    [[nodiscard]] std::optional<double> percentile(double quantile) const;
    [[nodiscard]] std::size_t sampleCount() const noexcept { return latencies_.size(); }
    [[nodiscard]] const std::deque<SessionEvent>& events() const noexcept { return events_; }
    [[nodiscard]] std::string csv() const;
private:
    std::optional<Clock::time_point> start_, visibleSince_;
    std::optional<TriggerInput> previousInput_;
    TriggerState previousState_=TriggerState::Idle;
    std::deque<double> latencies_;
    std::deque<SessionEvent> events_;
};
}
