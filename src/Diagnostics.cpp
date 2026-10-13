#include "triggerlab/Diagnostics.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace triggerlab {
void Diagnostics::observe(TriggerInput input, TriggerOutput output, Clock::time_point now) {
    if (!start_) start_=now;
    if (!input.active || !input.targetVisible) visibleSince_.reset();
    else if (!visibleSince_) visibleSince_=now;
    if (output.fired && visibleSince_) {
        latencies_.push_back(std::max(0.0,std::chrono::duration<double,std::milli>(now-*visibleSince_).count()));
        if(latencies_.size()>2048) latencies_.pop_front();
        visibleSince_.reset();
    }
    if (!previousInput_ || previousInput_->active!=input.active || previousInput_->targetVisible!=input.targetVisible || previousState_!=output.state || output.fired) {
        events_.push_back({std::chrono::duration<double,std::milli>(now-*start_).count(),input.active,input.targetVisible,output.state,output.fired,output.shotCount});
        if(events_.size()>4096) events_.pop_front();
    }
    previousInput_=input; previousState_=output.state;
}
void Diagnostics::reset() noexcept { start_.reset(); visibleSince_.reset(); previousInput_.reset(); latencies_.clear(); events_.clear(); previousState_=TriggerState::Idle; }
std::optional<double> Diagnostics::percentile(double quantile) const {
    if(latencies_.empty() || !std::isfinite(quantile) || quantile<0 || quantile>1) return {};
    std::vector<double> sorted(latencies_.begin(),latencies_.end()); std::sort(sorted.begin(),sorted.end());
    const auto index=static_cast<std::size_t>(std::max(1.0,std::ceil(quantile*sorted.size())))-1;
    return sorted[index];
}
std::string Diagnostics::csv() const {
    std::ostringstream out; out.imbue(std::locale::classic()); out<<std::fixed<<std::setprecision(3)<<"elapsed_ms,active,target_visible,state,fired,shots\n";
    for(const auto& event:events_) out<<event.elapsedMs<<','<<event.active<<','<<event.targetVisible<<','<<static_cast<int>(event.state)<<','<<event.fired<<','<<event.shots<<'\n';
    return out.str();
}
}
