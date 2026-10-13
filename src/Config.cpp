#include "triggerlab/Config.hpp"
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif
namespace triggerlab {
bool validSettings(const Settings& s) noexcept {
    return (s.language==Language::English || s.language==Language::Russian) && static_cast<int>(s.scene.profile)>=0 && static_cast<int>(s.scene.profile)<=2 &&
        std::isfinite(s.scene.speed) && s.scene.speed>=.25 && s.scene.speed<=3 && s.engine.cooldown.count() >= 50 && s.engine.cooldown.count() <= 2000 &&
        s.engine.stableFrames >= 1 && s.engine.stableFrames <= 20 &&
        s.detector.roiRadius >= 0 && s.detector.roiRadius <= 30 &&
        std::isfinite(s.detector.minimumCoverage) && s.detector.minimumCoverage >= .01 && s.detector.minimumCoverage <= 1;
}
std::string serializeSettings(const Settings& s) {
    if (!validSettings(s)) throw std::invalid_argument("Invalid settings");
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(17) << "{\n  \"cooldown_ms\": " << s.engine.cooldown.count()
        << ",\n  \"roi_radius\": " << s.detector.roiRadius
        << ",\n  \"stable_frames\": " << s.engine.stableFrames
        << ",\n  \"minimum_coverage\": " << s.detector.minimumCoverage
        << ",\n  \"minimum_red\": " << int(s.detector.minimumRed)
        << ",\n  \"minimum_blue\": " << int(s.detector.minimumBlue)
        << ",\n  \"maximum_green\": " << int(s.detector.maximumGreen)
        << ",\n  \"scene_profile\": " << static_cast<int>(s.scene.profile)
        << ",\n  \"scene_speed\": " << s.scene.speed << ",\n  \"language\": " << static_cast<int>(s.language) << "\n}\n";
    return out.str();
}
ConfigResult deserializeSettings(std::string_view json) {
    // Strict flat numeric schema: reject unknown/duplicate/missing keys and trailing data.
    std::size_t p = 0;
    auto whitespace = [&] { while (p < json.size() && (json[p]==' ' || json[p]=='\r' || json[p]=='\n' || json[p]=='\t')) ++p; };
    auto take = [&](char c) { whitespace(); if (p < json.size() && json[p]==c) { ++p; return true; } return false; };
    auto fail = [] { return ConfigResult{{}, false, "Invalid configuration; defaults applied"}; };
    std::map<std::string, double> values;
    if (!take('{')) return fail();
    for (;;) {
        if (!take('"')) return fail();
        const auto start = p;
        while (p < json.size() && json[p]!='"') {
            if (json[p]=='\\' || static_cast<unsigned char>(json[p]) < 32) return fail();
            ++p;
        }
        std::string key(json.substr(start, p-start));
        if (!take('"') || !take(':')) return fail();
        whitespace(); const auto numberStart = p;
        if (p < json.size() && json[p]=='-') ++p;
        if (p >= json.size() || json[p]<'0' || json[p]>'9') return fail();
        if (json[p]=='0') ++p; else while (p < json.size() && json[p]>='0' && json[p]<='9') ++p;
        if (p < json.size() && json[p]=='.') {
            ++p; auto digits=p; while (p < json.size() && json[p]>='0' && json[p]<='9') ++p;
            if (p==digits) return fail();
        }
        if (p < json.size() && (json[p]=='e' || json[p]=='E')) {
            ++p; if (p < json.size() && (json[p]=='+' || json[p]=='-')) ++p;
            auto digits=p; while (p < json.size() && json[p]>='0' && json[p]<='9') ++p;
            if (p==digits) return fail();
        }
        double value=0; auto parsed=std::from_chars(json.data()+numberStart,json.data()+p,value);
        if (parsed.ec != std::errc{} || !std::isfinite(value) || !values.emplace(key,value).second) return fail();
        if (take('}')) break;
        if (!take(',')) return fail();
    }
    whitespace(); if (p!=json.size() || (values.size()<7 || values.size()>10)) return fail();
    const char* keys[] = {"cooldown_ms","roi_radius","stable_frames","minimum_coverage","minimum_red","minimum_blue","maximum_green"};
    for (auto key : keys) if (!values.contains(key)) return fail();
    for (auto key : keys) if (std::string_view(key)!="minimum_coverage" && (std::floor(values[key])!=values[key] || values[key]<0 || values[key]>2000)) return fail();
    for (auto key : {"minimum_red","minimum_blue","maximum_green"}) if (values[key]>255) return fail();
    const bool hasScene=values.contains("scene_profile");
    const bool hasLanguage=values.contains("language");
    if(hasScene!=values.contains("scene_speed") || values.size()!=7+(hasScene?2:0)+(hasLanguage?1:0)) return fail();
    Settings s;
    if(hasLanguage) {
        const auto language=values["language"];
        if(language!=0 && language!=1) return fail();
        s.language=static_cast<Language>(static_cast<int>(language));
    }
    if(hasScene) {
        auto profile=values["scene_profile"]; auto speed=values["scene_speed"];
        if(profile<0 || profile>2 || std::floor(profile)!=profile || speed<.25 || speed>3) return fail();
        s.scene={static_cast<SceneProfile>(static_cast<int>(profile)),speed};
    }
    s.engine.cooldown=std::chrono::milliseconds{static_cast<int>(values["cooldown_ms"])};
    s.engine.stableFrames=static_cast<int>(values["stable_frames"]);
    s.detector.roiRadius=static_cast<int>(values["roi_radius"]);
    s.detector.minimumCoverage=values["minimum_coverage"];
    s.detector.minimumRed=static_cast<std::uint8_t>(values["minimum_red"]);
    s.detector.minimumBlue=static_cast<std::uint8_t>(values["minimum_blue"]);
    s.detector.maximumGreen=static_cast<std::uint8_t>(values["maximum_green"]);
    if (!validSettings(s)) return fail();
    return {s,true,"Settings loaded"};
}
ConfigResult loadSettings(const std::filesystem::path& path) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {{},false,"Cannot read configuration; defaults applied"};
        std::string data(65537,'\0'); in.read(data.data(), static_cast<std::streamsize>(data.size()));
        if (in.bad() || in.gcount()>65536) return {{},false,"Configuration read error or too large; defaults applied"};
        data.resize(static_cast<std::size_t>(in.gcount())); return deserializeSettings(data);
    } catch (const std::exception&) { return {{},false,"Configuration read error; defaults applied"}; }
}
bool saveSettings(const std::filesystem::path& path, const Settings& s, std::string& error) {
    auto temporary=path; temporary += ".tmp";
    try {
        const auto data=serializeSettings(s);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        { std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
          out.write(data.data(),static_cast<std::streamsize>(data.size())); out.flush();
          if (!out) throw std::runtime_error("Cannot write temporary settings file");
          out.close(); if (!out) throw std::runtime_error("Cannot close settings file"); }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace settings file");
#else
        std::filesystem::rename(temporary,path);
#endif
        error.clear(); return true;
    } catch (const std::exception& e) {
        error=e.what(); std::error_code ignored; std::filesystem::remove(temporary,ignored); return false;
    }
}
}
