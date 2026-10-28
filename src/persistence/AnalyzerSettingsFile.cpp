#include "triggerlab/AnalyzerSettings.hpp"
#include "triggerlab/AtomicFile.hpp"
#include "persistence/NumericObject.hpp"
#include <fstream>

namespace triggerlab {
AnalyzerSettings loadAnalyzerSettings(const std::filesystem::path& path) {
    try {
        std::ifstream in(path,std::ios::binary);if(!in) return {};
        std::string text(8193,'\0');in.read(text.data(),static_cast<std::streamsize>(text.size()));
        if(in.bad() || in.gcount()>8192) return {};
        text.resize(static_cast<std::size_t>(in.gcount()));
        auto object=persistence::parseNumericObject<int>(text);
        if(!object) return {};
        auto& values=*object;
        if(values.size()!=10+static_cast<std::size_t>(values.contains("click_ms"))+static_cast<std::size_t>(values.contains("dark_theme"))+static_cast<std::size_t>(values.contains("advanced_mode"))) return {};
        for(auto key:{"schema_version","center","dry_run","side","ignore_radius","minimum_percent","offset_x","offset_y","dwell_ms","cooldown_ms"}) if(!values.contains(key)) return {};
        if(values["schema_version"]!=1 || values["center"]<0 || values["center"]>1 || values["dry_run"]<0 || values["dry_run"]>1) return {};
        AnalyzerSettings s{values["center"]==1,values["dry_run"]==1,values["side"],values["ignore_radius"],values["minimum_percent"],values["offset_x"],values["offset_y"],values["dwell_ms"],values["cooldown_ms"]};
        if(values.contains("click_ms")) s.clickMs=values["click_ms"];
        if(values.contains("dark_theme")) {if(values["dark_theme"]<0 || values["dark_theme"]>1) return {};s.darkTheme=values["dark_theme"]==1;}
        if(values.contains("advanced_mode")) {if(values["advanced_mode"]<0 || values["advanced_mode"]>1) return {};s.advancedMode=values["advanced_mode"]==1;}
        if(!validAnalyzerSettings(s)) return {};
        return s;
    } catch(const std::exception&) {return {};}
}
bool saveAnalyzerSettings(const std::filesystem::path& path,const AnalyzerSettings& s,std::string& error) {
    if(!validAnalyzerSettings(s)) {error="Invalid analyzer settings";return false;}
    const std::string data="{\n\"schema_version\":1,\n\"center\":"+std::to_string(s.center)+",\n\"dry_run\":"+std::to_string(s.dryRun)+",\n\"side\":"+std::to_string(s.side)+",\n\"ignore_radius\":"+std::to_string(s.ignoreRadius)+",\n\"minimum_percent\":"+std::to_string(s.minimumPercent)+",\n\"offset_x\":"+std::to_string(s.offsetX)+",\n\"offset_y\":"+std::to_string(s.offsetY)+",\n\"dwell_ms\":"+std::to_string(s.dwellMs)+",\n\"cooldown_ms\":"+std::to_string(s.cooldownMs)+"\n}\n";
    auto extended=data;extended.insert(extended.rfind('}'),",\"click_ms\":"+std::to_string(s.clickMs)+",\n\"dark_theme\":"+std::to_string(s.darkTheme)+",\n\"advanced_mode\":"+std::to_string(s.advancedMode)+"\n");
    return writeAtomicTextFile(path,extended,error);
}
}
