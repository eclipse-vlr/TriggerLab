#pragma once
#include "triggerlab/TriggerEngine.hpp"
namespace triggerlab {
// A sustained match produces one event; invalid captures never rearm a fired match.
class HoverClickGate {
public:
    bool step(bool valid,bool matching,Clock::time_point now) noexcept {
        if(!valid) {frames_=0;return false;}
        if(!matching) {frames_=0;latched_=false;return false;}
        if(latched_) return false;
        if(frames_<2) ++frames_;
        if(frames_<2 || now<next_) return false;
        latched_=true;frames_=0;next_=now+std::chrono::milliseconds{250};return true;
    }
    void reset() noexcept {frames_=0;latched_=false;next_={};}
private:
    int frames_=0;bool latched_=false;Clock::time_point next_{};
};
}
