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
    // Schema 1 remains readable; schema 2 must retain the classifier, not only RGB.
    const auto legacy=deserializeColorProfile(R"({"schema_version":1,"red":255,"green":255,"blue":0,"tolerance":0})");
    check(legacy.ok && legacy.color.space==ColorSpace::RGB && legacy.color.hueTolerance==12,"legacy RGB defaults preserved");
    ColorMatchConfig hsv;hsv.space=ColorSpace::HSV;hsv.target={0,255,255,255};hsv.channelTolerance=0;
    hsv.hueTolerance=17.5;hsv.saturationTolerance=.123456789;hsv.valueTolerance=.23456789;hsv.minimumCoverage=.27;
    const auto hsvJson=serializeColorProfile(hsv);
    const auto restored=deserializeColorProfile(hsvJson);
    check(restored.ok && restored.color.space==hsv.space && restored.color.hueTolerance==hsv.hueTolerance &&
        restored.color.saturationTolerance==hsv.saturationTolerance && restored.color.valueTolerance==hsv.valueTolerance &&
        restored.color.minimumCoverage==hsv.minimumCoverage && restored.color.channelTolerance==0,"all HSV values round trip exactly");
    const Pixel dimYellow{0,235,235,255};
    check(matchesColor(dimYellow,hsv) && matchesColor(dimYellow,restored.color) && !matchesColor(dimYellow,legacy.color),"classification unchanged after HSV restore");
    auto changed=[&](std::string_view key,std::string_view value) {
        auto result=hsvJson;const auto begin=result.find(std::string("\"")+std::string(key)+"\": ");
        check(begin!=std::string::npos,"test field exists");
        const auto number=begin+key.size()+4,end=result.find_first_of(",\n",number);
        result.replace(number,end-number,value);return result;
    };
    for(const auto& invalid: {
        changed("schema_version","3"),changed("color_space","2"),changed("color_space","0.5"),
        changed("hue_tolerance","181"),changed("hue_tolerance","-1"),
        changed("saturation_tolerance","1.1"),changed("value_tolerance","-0.1"),
        changed("minimum_coverage","0"),changed("minimum_coverage","1.1"),
        changed("value_tolerance","1e999"),changed("value_tolerance","NaN"),
        changed("red","1.5"),changed("red","01"),changed("red","1e"),
        hsvJson+"trailing"}) check(!deserializeColorProfile(invalid).ok,"malformed or invalid schema 2 rejected");
    auto missing=hsvJson;const auto field=missing.find("  \"color_space\"");
    missing.erase(field,missing.find('\n',field)-field+1);
    check(!deserializeColorProfile(missing).ok,"missing schema 2 key rejected");
    auto unknown=hsvJson;unknown.replace(unknown.find("color_space"),11,"unknown_key");
    check(!deserializeColorProfile(unknown).ok,"unknown schema 2 key rejected");
    auto duplicate=hsvJson;duplicate.insert(duplicate.rfind('}'),",\"red\":255");
    check(!deserializeColorProfile(duplicate).ok,"duplicate schema 2 key rejected");
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
    check(saveColorProfile(path,hsv,error),"save HSV profile to disk");
    const auto disk=loadColorProfile(path);
    check(disk.ok && serializeColorProfile(disk.color)==hsvJson && matchesColor(dimYellow,disk.color),"disk reload preserves complete HSV behavior");
    std::filesystem::remove_all(path.parent_path());
    std::cout<<"PASS: color profile, hover gate and target permission\n";
}
