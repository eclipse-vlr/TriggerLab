#include "triggerlab/Diagnostics.hpp"
#include "triggerlab/AimInput.hpp"
#include "triggerlab/Simulation.hpp"
#include "triggerlab/TargetDetector.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
void check(bool value,const char* why) { if(!value) { std::cerr<<why<<'\n'; std::exit(1); } }
int colored(const triggerlab::Frame& frame) {
    int count=0; for(int y=0;y<frame.height();++y) for(int x=0;x<frame.width();++x) {auto p=frame.at(x,y); if(p.red>200 && p.blue>200 && p.green<100) ++count;} return count;
}
int main() {
    using namespace triggerlab; using namespace std::chrono_literals;
    AimInput aim;
    check(aim.x()==480 && aim.y()==270 && !aim.active(),"aim defaults");
    aim.move(0,0); aim.press(); check(aim.active() && aim.x()==0,"mouse arms inside scene");
    aim.move(960,100); check(!aim.active() && aim.x()==0,"panel cannot arm or move aim");
    aim.move(-1,100); check(!aim.inside(),"signed capture coordinates");
    aim.move(959,539); check(aim.active(),"captured drag returns to scene");
    aim.release(); check(!aim.active(),"mouse release disarms");
    aim.press(); aim.leave(); check(!aim.active(),"mouse leave disarms");
    aim.center(); check(aim.x()==480 && aim.y()==270 && !aim.active(),"right click centers and disarms");
    Diagnostics metrics; const auto t=Clock::time_point{}+1s;
    check(!metrics.percentile(.5),"empty percentile");
    for(int i=1;i<=20;++i) {
        auto at=t+i*1s;
        metrics.observe({true,true},{TriggerState::Detected,false,0},at);
        metrics.observe({true,true},{TriggerState::Cooldown,true,static_cast<std::uint64_t>(i)},at+std::chrono::milliseconds{i});
        metrics.observe({false,true},{TriggerState::Idle,false,static_cast<std::uint64_t>(i)},at+100ms);
    }
    check(metrics.sampleCount()==20 && *metrics.percentile(.5)==10 && *metrics.percentile(.95)==19,"nearest rank percentiles");
    check(!metrics.percentile(-1) && !metrics.percentile(2),"invalid quantile");
    metrics.reset(); metrics.observe({true,true},{TriggerState::Detected,false,0},t);
    metrics.observe({true,false},{TriggerState::Armed,false,0},t+10ms);
    metrics.observe({true,true},{TriggerState::Detected,false,0},t+20ms);
    metrics.observe({true,true},{TriggerState::Cooldown,true,1},t+25ms);
    check(*metrics.percentile(.5)==5,"lost target restarts timing");
    check(metrics.csv().starts_with("elapsed_ms,active,target_visible,state,fired,shots\n"),"CSV header");
    check(metrics.csv().find("25.000,1,1,3,1,1")!=std::string::npos,"CSV event values");
    metrics.reset();
    for(int i=0;i<5000;++i) metrics.observe({true,true},{TriggerState::Cooldown,true,static_cast<std::uint64_t>(i)},t+std::chrono::milliseconds{i});
    check(metrics.sampleCount()==2048 && metrics.events().size()==4096,"bounded diagnostics memory");
    metrics.reset(); check(metrics.events().empty() && !metrics.percentile(.95),"reset clears diagnostics");
    Frame frame(960,540); Simulation scene(960,540); TargetDetector detector;
    scene.render(frame); const int classic=colored(frame); check(detector.detect(frame,480,270).found,"classic centered");
    scene.setConfig({SceneProfile::Multiple,1}); scene.render(frame); check(colored(frame)>classic*2,"multiple targets rendered");
    scene.setConfig({SceneProfile::Precision,1}); scene.render(frame); check(colored(frame)<classic/2 && detector.detect(frame,480,270).found,"precision smaller and detectable");
    scene.setConfig({SceneProfile::Crossing,2}); scene.advance(.05); check(std::abs(scene.elapsedSeconds()-.1)<1e-9,"speed scales scene time");
    scene.advance(std::numeric_limits<double>::quiet_NaN());
    scene.advance(std::numeric_limits<double>::infinity());
    check(std::abs(scene.elapsedSeconds()-.1)<1e-9,"invalid timestep does not poison scene geometry");
    scene.onSimulatedShot(); scene.reset(); check(scene.elapsedSeconds()==0 && !scene.isHitFlashActive() && scene.config().speed==2,"reset preserves profile");
    scene.setConfig({static_cast<SceneProfile>(99),100}); check(scene.config().profile==SceneProfile::Crossing && scene.config().speed==3,"scene clamping");
    Frame tiny(16,16); for(auto profile:{SceneProfile::Crossing,SceneProfile::Multiple,SceneProfile::Precision}) {scene.setConfig({profile,1});scene.render(tiny);scene.onSimulatedShot();scene.render(tiny);}
    std::cout<<"PASS: scenes and diagnostics\n";
}
