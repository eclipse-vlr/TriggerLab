#include "triggerlab/AnalyzerSettings.hpp"
#include "triggerlab/HoverClickGate.hpp"
#include <iostream>
#include <fstream>
#include <cstdlib>
void check(bool value,const char* why) {if(!value) {std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    using namespace triggerlab;using namespace std::chrono_literals;
    AnalyzerSettings s;check(validAnalyzerSettings(s) && s.center && s.dryRun,"safe center defaults");
    {
        AnalyzerSettings qualitySettings;qualitySettings.side=5;qualitySettings.ignoreRadius=1;qualitySettings.minimumPercent=25;
        ColorMatchConfig match;match.target={0,0,255,255};match.channelTolerance=0;
        Frame target(5,5),background(5,5);
        for(int y=0;y<5;++y) for(int x=0;x<5;++x) {target.at(x,y)=match.target;background.at(x,y)={0,255,0,255};}
        auto quality=compareColorSamples(nullptr,nullptr,match,qualitySettings);
        check(quality.valid && !quality.targetCoverage && !quality.backgroundCoverage,"missing samples are not a successful calibration");
        quality=compareColorSamples(&target,nullptr,match,qualitySettings);check(quality.targetFound && !quality.backgroundCoverage,"partial target sample");
        quality=compareColorSamples(&target,&background,match,qualitySettings);
        check(quality.targetFound && !quality.backgroundFound && *quality.targetCoverage==1 && *quality.backgroundCoverage==0,"samples distinguish target from background");
        match.channelTolerance=255;quality=compareColorSamples(&target,&background,match,qualitySettings);check(quality.backgroundFound,"broad tolerance warns about background");
        match.channelTolerance=0;target=background;target.at(0,0)=match.target;
        quality=compareColorSamples(&target,&background,match,qualitySettings);check(!quality.targetFound && *quality.targetCoverage==1./16,"excluded center not in coverage denominator");
        qualitySettings.minimumPercent=100;check(!compareColorSamples(&target,&background,match,qualitySettings).targetFound,"high threshold rejects small target");
        target=background;target.at(2,2)=match.target;check(excludedColorPixels(target,match,qualitySettings)==1 && !compareColorSamples(&target,&background,match,qualitySettings).targetFound,"color only in crosshair is ignored");
        qualitySettings.ignoreRadius=0;check(excludedColorPixels(target,match,qualitySettings)==0,"zero exclusion");
        qualitySettings.side=7;check(!compareColorSamples(&target,&background,match,qualitySettings).valid,"ROI changes require new samples");
        qualitySettings.side=5;qualitySettings.center=false;check(!compareColorSamples(&target,&background,match,qualitySettings).valid,"cursor mode is not ROI calibration");
        qualitySettings.center=true;match.channelTolerance=-1;check(!compareColorSamples(&target,&background,match,qualitySettings).valid,"invalid color rejected");
        match.channelTolerance=0;match.space=ColorSpace::HSV;for(int y=0;y<5;++y) for(int x=0;x<5;++x) target.at(x,y)=match.target;
        quality=compareColorSamples(&target,&background,match,qualitySettings);check(quality.valid && quality.targetFound && !quality.backgroundFound,"HSV samples supported");
    }
    auto roi=aimRegion({-1920,20,1280,720},s);
    check(roi && roi->x==-1295 && roi->y==365 && roi->width==31,"client center, negative monitor origin");
    s.offsetX=30;s.offsetY=-20;roi=aimRegion({0,0,1280,720},s);
    check(roi && roi->x==655 && roi->y==325,"crosshair offsets");
    s.offsetX=2000;check(!aimRegion({0,0,1280,720},s),"offscreen crosshair blocked");
    s={};s.side=30;check(!validAnalyzerSettings(s),"even ROI blocked");s={};s.ignoreRadius=15;check(!validAnalyzerSettings(s),"ignore cannot erase full ROI");
    s={};Frame frame(31,31);frame.clear({0,0,0,255});ColorMatchConfig c;c.target={0,0,255,255};c.channelTolerance=0;
    for(int y=12;y<=18;++y) for(int x=12;x<=18;++x) frame.at(x,y)=c.target;
    check(!aimColorMask(frame,c,s).found,"colored crosshair alone cannot trigger");
    frame.clear(c.target);for(int y=12;y<=18;++y) for(int x=12;x<=18;++x) frame.at(x,y)={255,255,255,255};
    auto mask=aimColorMask(frame,c,s);check(mask.found && mask.coverage==1 && mask.matchingPixels==912,"target around white crosshair detected");
    check(mask.pixels[15*31+15]==0,"crosshair is black in mask");
    frame.clear({0,0,0,255});frame.at(0,0)=c.target;check(!aimColorMask(frame,c,s).found,"one unrelated pixel below coverage threshold");
    HoverClickGate gate;gate.setTiming(120ms,100ms);auto t=Clock::time_point{}+1s;
    check(!gate.step(true,true,t) && !gate.step(true,true,t+40ms) && !gate.step(true,true,t+80ms) && !gate.step(true,true,t+119ms) && gate.step(true,true,t+120ms),"custom dwell used");
    auto dir=std::filesystem::temp_directory_path()/("triggerlab-analyzer-"+std::to_string(Clock::now().time_since_epoch().count()));
    auto path=dir/"settings.json";s.dwellMs=123;s.cooldownMs=456;s.clickMs=60;s.offsetX=-80;s.dryRun=false;s.darkTheme=true;std::string error;
    check(saveAnalyzerSettings(path,s,error),"settings save");auto saved=loadAnalyzerSettings(path);
    check(saved.dwellMs==123 && saved.cooldownMs==456 && saved.clickMs==60 && saved.offsetX==-80 && !saved.dryRun && saved.darkTheme,"settings and theme roundtrip");
    std::string current;
    {std::ifstream in(path);current.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());}
    const auto themeEntry=current.find(",\n\"dark_theme\":1");check(themeEntry!=std::string::npos,"theme serialized");
    auto legacy=current;legacy.erase(themeEntry,std::string(",\n\"dark_theme\":1").size());
    {std::ofstream out(path);out<<legacy;}
    check(loadAnalyzerSettings(path).dwellMs==123 && !loadAnalyzerSettings(path).darkTheme,"legacy settings keep values and light theme");
    auto malformed=current;malformed.replace(malformed.find("\"dark_theme\":1"),14,"\"dark_theme\":2");
    {std::ofstream out(path);out<<malformed;}
    check(loadAnalyzerSettings(path).dwellMs==70 && !loadAnalyzerSettings(path).darkTheme,"invalid theme rejected");
    check(saveAnalyzerSettings(path,s,error),"restore valid settings");
    s.side=200;check(!saveAnalyzerSettings(path,s,error) && loadAnalyzerSettings(path).dwellMs==123,"invalid save preserves file");
    {std::ofstream out(path);out<<"{\"schema_version\":1,\"center\":0}";}
    check(loadAnalyzerSettings(path).dryRun && loadAnalyzerSettings(path).dwellMs==70,"corruption returns safe defaults");
    std::filesystem::remove_all(dir);std::cout<<"PASS: crosshair ROI, mask, timings, settings\n";
}
