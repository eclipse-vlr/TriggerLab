#include "triggerlab/Config.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
void check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
int main() {
    using namespace triggerlab;
    Settings s; s.engine.cooldown=std::chrono::milliseconds{725}; s.engine.stableFrames=7;
    s.detector={30,.789,255,0,255}; s.scene={SceneProfile::Precision,2.75}; s.language=Language::English;
    const auto json=serializeSettings(s); const auto parsed=deserializeSettings(json);
    check(parsed.ok && serializeSettings(parsed.settings)==json,"non-default round trip");
    check(deserializeSettings(serializeSettings(Settings{})).ok,"defaults round trip");
    const auto base=serializeSettings(Settings{});
    auto legacy=base.substr(0,base.find(",\n  \"scene_profile\""))+"\n}\n";
    check(deserializeSettings(legacy).ok && deserializeSettings(legacy).settings.scene.speed==1,"v0.2 schema compatibility");
    auto v03=base.substr(0,base.find(",\n  \"language\""))+"\n}\n";
    check(deserializeSettings(v03).ok && deserializeSettings(v03).settings.language==Language::Russian,"v0.3 language default");
    check(deserializeSettings(json).settings.language==Language::English,"English language round trip");
    for(const auto& entry:translations) {
        check(!entry.english.empty() && !entry.russian.empty(),"complete translation entry");
        check(translate(Language::English,entry.english)==entry.english && translate(Language::Russian,entry.english)==entry.russian,"both languages resolve");
        int count=0; for(const auto& other:translations) if(other.english==entry.english) ++count;
        check(count==1,"unique translation key");
    }
    check(translate(Language::Russian,L"TriggerLab")==L"TriggerLab","brand fallback");
    auto changed=[&](std::string from,std::string to) {auto data=base; data.replace(data.find(from),from.size(),to); return data;};
    for (const auto& data : {std::string(""),std::string("{}"),std::string("[]"),base+"false",
        changed("\"language\": 1","\"language\": 2"),changed("\"language\": 1","\"language\": 0.5"),changed("\"language\": 1","\"language\": \"ru\""),changed("180","49"),changed("180","2001"),changed("180","true"),changed("180","\"180\""),
        changed("180","1e999"),changed("180","0180"),changed("180","180.5"),
        changed("\"roi_radius\": 5","\"roi_radius\": -1"),changed("\"roi_radius\": 5","\"roi_radius\": 31"),
        changed("\"stable_frames\": 2","\"stable_frames\": 0"),changed("\"stable_frames\": 2","\"stable_frames\": 21"),
        changed("\"scene_profile\": 0","\"scene_profile\": 3"),changed("\"scene_speed\": 1","\"scene_speed\": 0"),changed("\"scene_speed\": 1","\"scene_speed\": 4"),changed("160","256"),changed("150","-1"),changed("130","null"),
        changed("\"minimum_coverage\": 0.45000000000000001","\"minimum_coverage\": 0"),
        changed("\"roi_radius\"","\"unknown\""),changed("\"roi_radius\"","\"cooldown_ms\"")}) {
        auto result=deserializeSettings(data);
        check(!result.ok && serializeSettings(result.settings)==base,"invalid JSON must return defaults");
    }
    for (auto pair : {std::pair{50,1},std::pair{2000,20}}) {
        Settings edge; edge.engine.cooldown=std::chrono::milliseconds{pair.first}; edge.engine.stableFrames=pair.second;
        edge.detector.minimumCoverage=pair.first==50?.01:1.; edge.detector.roiRadius=pair.first==50?0:30;
        check(deserializeSettings(serializeSettings(edge)).ok,"boundary values");
    }
    auto dir=std::filesystem::temp_directory_path()/ ("triggerlab-config-test-"+std::to_string(Clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir); auto path=dir/"settings.json";
    check(!loadSettings(path).ok,"missing file"); std::string error;
    check(saveSettings(path,s,error),"save initial"); check(serializeSettings(loadSettings(path).settings)==json,"disk round trip");
    Settings invalid=s; invalid.detector.minimumCoverage=std::numeric_limits<double>::quiet_NaN();
    check(!saveSettings(path,invalid,error) && !error.empty(),"invalid settings save rejected");
    check(serializeSettings(loadSettings(path).settings)==json,"failed save preserves previous file");
    {std::ofstream blocked(dir/"blocked");blocked<<"file";}
    check(!saveSettings(dir/"blocked"/"settings.json",Settings{},error),"unwritable temporary path rejected");
    check(serializeSettings(loadSettings(path).settings)==json,"write failure preserves previous file");
    {std::ofstream out(path); out<<"broken";} check(!loadSettings(path).ok,"corrupted file");
    {std::ofstream out(path); out<<std::string(65537,' ');} check(!loadSettings(path).ok,"oversize file");
    std::filesystem::remove_all(dir); std::cout<<"PASS: configuration validation and persistence\n";
}
