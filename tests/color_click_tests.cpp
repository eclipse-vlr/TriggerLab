#include "triggerlab/ColorProfile.hpp"
#include "triggerlab/HoverClickGate.hpp"
#include "triggerlab/ClickPermission.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
void check(bool c,const char* why) {if(!c) {std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    using namespace triggerlab;using namespace std::chrono_literals;
    ColorMatchConfig color;color.target={19,67,231,255};color.channelTolerance=0;
    auto json=serializeColorProfile(color);auto parsed=deserializeColorProfile(json);
    check(parsed.ok && parsed.color.target.red==231 && parsed.color.target.blue==19,"selected color round trip BGRA");
    check(deserializeColorProfile("{\"blue\":19,\"red\":231,\"schema_version\":1,\"green\":67,\"tolerance\":0}").ok,"unordered profile fields");
    for(auto text:{"{}","[]","{\"schema_version\":1,\"red\":true}","{\"schema_version\":1,\"red\":256,\"green\":0,\"blue\":0,\"tolerance\":0}","{\"schema_version\":1,\"red\":01,\"green\":0,\"blue\":0,\"tolerance\":0}","{\"schema_version\":1,\"red\":1,\"red\":1,\"blue\":0,\"tolerance\":0}"}) check(!deserializeColorProfile(text).ok,"bad profile rejected");
    HoverClickGate gate;auto time=Clock::time_point{}+1s;
    for(const auto text:{
        R"({"schema_version":1,"red ":1,"green":2,"blue":3,"tolerance":4})",
        R"({"schema_version":1,"red":-00,"green":2,"blue":3,"tolerance":4})",
        R"({"schema_version":1,"red":1,"green":2,"blue":3,"tolerance":)"})
        check(!deserializeColorProfile(text).ok,"malformed color key or number rejected");
    check(!gate.step(true,true,time),"first matching frame waits");
    check(!gate.step(true,true,time+35ms),"35ms is too early");
    check(!gate.step(true,true,time+69ms),"69ms is too early");
    check(gate.step(true,true,time+70ms),"ready exactly at 70ms");
    check(gate.step(true,true,time+71ms),"readiness alone does not consume click");
    gate.acknowledgeClick(time+71ms);
    check(!gate.step(true,true,time+1s),"continuous hover never repeats");
    check(!gate.step(false,false,time+2s) && !gate.step(true,true,time+3s),"capture failure does not rearm");
    check(!gate.step(true,false,time+4s),"actual loss rearms");
    check(!gate.step(true,true,time+5s) && !gate.step(true,true,time+5s+35ms) && gate.step(true,true,time+5s+70ms),"new hover waits full dwell");
    gate.reset();
    check(!gate.step(true,true,time) && !gate.step(false,false,time+35ms) && !gate.step(true,true,time+70ms),"invalid sample restarts dwell");
    check(!gate.step(true,true,time+105ms) && gate.step(true,true,time+140ms),"recovered capture waits 70ms");
    gate.reset();
    check(!gate.step(true,true,time) && !gate.step(true,true,time+1s),"UI stall cannot satisfy dwell");
    check(!gate.step(true,true,time+1035ms) && gate.step(true,true,time+1070ms),"fresh samples after stall required");
    gate.reset();
    check(!gate.step(true,true,time) && !gate.step(true,true,time+35ms) && !gate.step(true,false,time+69ms),"departure at 69ms cancels");
    gate.acknowledgeClick(time+69ms); // A cancelled operation cannot latch the gate.
    check(!gate.step(true,true,time+70ms) && !gate.step(true,true,time+105ms) && gate.step(true,true,time+140ms),"reentry starts new dwell");
    gate.acknowledgeClick(time+140ms);
    check(!gate.step(true,false,time+141ms) && !gate.step(true,true,time+142ms),"rearm preserves click cooldown");
    check(!gate.step(true,true,time+189ms),"no repeat within 49ms of click");
    check(!gate.step(true,true,time+190ms),"50ms cooldown does not bypass 70ms dwell");
    check(gate.step(true,true,time+212ms),"next eligible hover fires without former 250ms delay");
    gate.reset();
    check(!gate.step(true,true,time+1s) && !gate.step(true,true,time),"backwards timestamp restarts dwell");
    ClickPermission permit{1,1,1,10,10,true};check(permit.allowed(),"explicit target matches");
    permit.hoverWindow=2;check(!permit.allowed(),"other window blocked");permit.hoverWindow=1;permit.foregroundWindow=2;check(!permit.allowed(),"background window blocked");
    permit.foregroundWindow=1;permit.currentProcess=11;check(!permit.allowed(),"reused window process blocked");permit.currentProcess=10;permit.exists=false;check(!permit.allowed(),"closed target blocked");
    auto path=std::filesystem::temp_directory_path()/("triggerlab-color-"+std::to_string(Clock::now().time_since_epoch().count()))/"color.json";
    std::string error;check(saveColorProfile(path,color,error),"color disk save");check(loadColorProfile(path).ok,"color disk load");
    color.channelTolerance=300;check(!saveColorProfile(path,color,error) && loadColorProfile(path).color.channelTolerance==0,"invalid save preserves selected color");
    std::filesystem::remove_all(path.parent_path());
    std::cout<<"PASS: color profile, hover gate and target permission\n";
}
