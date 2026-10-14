#include "triggerlab/AnalyzerSettings.hpp"
#include "triggerlab/Config.hpp"
#include <fstream>
#include <charconv>
#include <map>
#include <cstdlib>
namespace triggerlab {
bool validAnalyzerSettings(const AnalyzerSettings& s) noexcept {
    return s.side>=5 && s.side<=101 && s.side%2==1 && s.ignoreRadius>=0 && s.ignoreRadius<s.side/2 &&
        s.minimumPercent>=1 && s.minimumPercent<=100 && s.offsetX>=-2000 && s.offsetX<=2000 && s.offsetY>=-2000 && s.offsetY<=2000 &&
        s.dwellMs>=1 && s.dwellMs<=2000 && s.cooldownMs>=50 && s.cooldownMs<=2000 && s.clickMs>=5 && s.clickMs<=150;
}
std::optional<Region> aimRegion(Region client,const AnalyzerSettings& s) noexcept {
    if(!validAnalyzerSettings(s) || !intersectRegion(client,client)) return {};
    const auto region=cursorRegion(client.x+client.width/2+s.offsetX,client.y+client.height/2+s.offsetY,s.side,client);
    if(!region || region->width!=s.side || region->height!=s.side) return {};
    return region;
}
ColorMask aimColorMask(const Frame& frame,const ColorMatchConfig& color,const AnalyzerSettings& settings) {
    if(!validAnalyzerSettings(settings)) throw std::invalid_argument("Invalid analyzer settings");
    auto mask=makeColorMask(frame,color);mask.matchingPixels=0;int eligible=0;
    const int cx=frame.width()/2,cy=frame.height()/2;
    for(int y=0;y<frame.height();++y) for(int x=0;x<frame.width();++x) {
        auto& value=mask.pixels[static_cast<std::size_t>(y)*frame.width()+x];
        if(settings.ignoreRadius>0 && std::abs(x-cx)<=settings.ignoreRadius && std::abs(y-cy)<=settings.ignoreRadius) value=0;
        else {++eligible;if(value) ++mask.matchingPixels;}
    }
    mask.coverage=eligible?double(mask.matchingPixels)/eligible:0;
    mask.found=eligible>0 && mask.matchingPixels>0 && mask.coverage*100>=settings.minimumPercent;
    return mask;
}
AnalyzerSettings loadAnalyzerSettings(const std::filesystem::path& path) {
    try {
        std::ifstream in(path,std::ios::binary);if(!in) return {};
        std::string text(8193,'\0');in.read(text.data(),static_cast<std::streamsize>(text.size()));
        if(in.bad() || in.gcount()>8192) return {};text.resize(static_cast<std::size_t>(in.gcount()));
        std::size_t p=0;std::map<std::string,int> values;
        auto ws=[&] {while(p<text.size() && (text[p]==' ' || text[p]=='\n' || text[p]=='\r' || text[p]=='\t')) ++p;};
        auto take=[&](char c) {ws();if(p<text.size() && text[p]==c) {++p;return true;}return false;};
        if(!take('{')) return {};
        for(;;) {
            if(!take('"')) return {};const auto start=p;
            while(p<text.size() && ((text[p]>='a' && text[p]<='z') || text[p]=='_')) ++p;
            const std::string key=text.substr(start,p-start);if(p>=text.size() || text[p++]!='"' || !take(':')) return {};
            ws();const auto n=p;if(p<text.size() && text[p]=='-') ++p;
            if(p>=text.size() || text[p]<'0' || text[p]>'9') return {};
            if(text[p]=='0') ++p;else while(p<text.size() && text[p]>='0' && text[p]<='9') ++p;
            int number=0;auto parsed=std::from_chars(text.data()+n,text.data()+p,number);
            if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+p || !values.emplace(key,number).second) return {};
            if(take('}')) break;if(!take(',')) return {};
        }
        ws();if(p!=text.size() || values.size()!=10+static_cast<std::size_t>(values.contains("click_ms"))+static_cast<std::size_t>(values.contains("dark_theme"))+static_cast<std::size_t>(values.contains("advanced_mode"))) return {};
        for(auto key:{"schema_version","center","dry_run","side","ignore_radius","minimum_percent","offset_x","offset_y","dwell_ms","cooldown_ms"}) if(!values.contains(key)) return {};
        if(values["schema_version"]!=1 || values["center"]<0 || values["center"]>1 || values["dry_run"]<0 || values["dry_run"]>1) return {};
        AnalyzerSettings s{values["center"]==1,values["dry_run"]==1,values["side"],values["ignore_radius"],values["minimum_percent"],values["offset_x"],values["offset_y"],values["dwell_ms"],values["cooldown_ms"]};
        if(values.contains("click_ms")) s.clickMs=values["click_ms"];
        if(values.contains("dark_theme")) {if(values["dark_theme"]<0 || values["dark_theme"]>1) return {};s.darkTheme=values["dark_theme"]==1;}
        if(values.contains("advanced_mode")) {if(values["advanced_mode"]<0 || values["advanced_mode"]>1) return {};s.advancedMode=values["advanced_mode"]==1;}
        if(!validAnalyzerSettings(s)) return {};return s;
    } catch(const std::exception&) {return {};}
}
bool saveAnalyzerSettings(const std::filesystem::path& path,const AnalyzerSettings& s,std::string& error) {
    if(!validAnalyzerSettings(s)) {error="Invalid analyzer settings";return false;}
    const std::string data="{\n\"schema_version\":1,\n\"center\":"+std::to_string(s.center)+",\n\"dry_run\":"+std::to_string(s.dryRun)+",\n\"side\":"+std::to_string(s.side)+",\n\"ignore_radius\":"+std::to_string(s.ignoreRadius)+",\n\"minimum_percent\":"+std::to_string(s.minimumPercent)+",\n\"offset_x\":"+std::to_string(s.offsetX)+",\n\"offset_y\":"+std::to_string(s.offsetY)+",\n\"dwell_ms\":"+std::to_string(s.dwellMs)+",\n\"cooldown_ms\":"+std::to_string(s.cooldownMs)+"\n}\n";
    auto extended=data;extended.insert(extended.rfind('}'),",\"click_ms\":"+std::to_string(s.clickMs)+",\n\"dark_theme\":"+std::to_string(s.darkTheme)+",\n\"advanced_mode\":"+std::to_string(s.advancedMode)+"\n");
    return writeAtomicTextFile(path,extended,error);
}
}
