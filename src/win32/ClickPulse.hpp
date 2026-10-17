#pragma once
#include <windows.h>
#include "triggerlab/TriggerEngine.hpp"
#include <mutex>
#include <optional>

namespace triggerlab {
// Public methods belong to one UI/owner thread. Only the callback and these
// methods may send an Up. The UI consumes completion on its own thread;
// the callback never touches window controls.
class ClickPulse {
public:
    struct Result {
        std::chrono::microseconds elapsed;
        int requestedMs;
        bool interrupted;
        bool releaseAccepted;
        Clock::time_point finishedAt;
    };
    using Sender=UINT (WINAPI*)(UINT,LPINPUT,int);
    explicit ClickPulse(Sender sender=SendInput):sender_(sender) {
        timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer_ && GetLastError()==ERROR_INVALID_PARAMETER)
            timer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        if(timer_) wait_=CreateThreadpoolWait(callback,this,nullptr);
    }
    ClickPulse(const ClickPulse&)=delete;
    ClickPulse& operator=(const ClickPulse&)=delete;
    ~ClickPulse() {
        cancel();
        if(wait_) {SetThreadpoolWait(wait_,nullptr,nullptr);WaitForThreadpoolWaitCallbacks(wait_,TRUE);CloseThreadpoolWait(wait_);}
        if(timer_) CloseHandle(timer_);
    }
    bool start(int milliseconds) {
        {std::lock_guard lock(mutex_);if(down_ || result_) {SetLastError(ERROR_BUSY);return false;}}
        // Drain a previous callback before reusing its one-shot wait. A queued
        // callback from a cancelled pulse must never release the next pulse.
        if(wait_) WaitForThreadpoolWaitCallbacks(wait_,TRUE);
        std::lock_guard lock(mutex_);
        if(!timer_ || !wait_) {SetLastError(ERROR_NOT_ENOUGH_MEMORY);return false;}
        if(milliseconds<5 || milliseconds>150) {SetLastError(ERROR_INVALID_PARAMETER);return false;}
        if(down_ || result_) {SetLastError(ERROR_BUSY);return false;}
        INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
        started_=Clock::now();
        if(sender_(1,&input,sizeof(INPUT))!=1) return false;
        down_=true;requested_=milliseconds;
        LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>(milliseconds)*10000;
        if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)) {
            const auto error=GetLastError();release(true);SetLastError(error);return false;
        }
        SetThreadpoolWait(wait_,timer_,nullptr);return true;
    }
    void cancel() {
        std::lock_guard lock(mutex_);
        if(wait_) SetThreadpoolWait(wait_,nullptr,nullptr);
        if(timer_) CancelWaitableTimer(timer_);
        release(true);
    }
    std::optional<Result> takeResult() {
        std::lock_guard lock(mutex_);
        auto completed=result_;result_.reset();return completed;
    }
private:
    static void CALLBACK callback(PTP_CALLBACK_INSTANCE,void* context,PTP_WAIT,TP_WAIT_RESULT) {
        auto& self=*static_cast<ClickPulse*>(context);
        std::lock_guard lock(self.mutex_);self.release(false);
    }
    void release(bool interrupted) {
        if(!down_) return;
        INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTUP;
        const auto upAt=Clock::now();
        const bool accepted=sender_(1,&input,sizeof(INPUT))==1;
        if(!accepted) sender_(1,&input,sizeof(INPUT));
        down_=false;
        result_=Result{std::chrono::duration_cast<std::chrono::microseconds>(upAt-started_),requested_,interrupted,accepted,Clock::now()};
    }
    Sender sender_;
    HANDLE timer_=nullptr;
    PTP_WAIT wait_=nullptr;
    std::mutex mutex_;
    bool down_=false;
    int requested_=0;
    Clock::time_point started_{};
    std::optional<Result> result_;
};
}
