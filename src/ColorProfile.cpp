#include "triggerlab/ColorProfile.hpp"
#include "triggerlab/Config.hpp"
#include <charconv>
#include <map>
#include <fstream>
namespace triggerlab {
std::string serializeColorProfile(const ColorMatchConfig& c) {
    if(!validColorConfig(c)) throw std::invalid_argument("Invalid color");
    return "{\n  \"schema_version\": 1,\n  \"red\": "+std::to_string(c.target.red)+",\n  \"green\": "+std::to_string(c.target.green)+",\n  \"blue\": "+std::to_string(c.target.blue)+",\n  \"tolerance\": "+std::to_string(c.channelTolerance)+"\n}\n";
}
ColorProfileResult deserializeColorProfile(std::string_view json) {
    auto fail=[] {return ColorProfileResult{{},false,"Invalid color profile"};};
    std::size_t p=0;auto ws=[&] {while(p<json.size() && (json[p]==' ' || json[p]=='\n' || json[p]=='\r' || json[p]=='\t')) ++p;};
    auto take=[&](char c) {ws();if(p<json.size() && json[p]==c) {++p;return true;}return false;};
    std::map<std::string,int> values;
    if(!take('{')) return fail();
    for(;;) {
        if(!take('"')) return fail();const auto start=p;
        while(p<json.size() && ((json[p]>='a' && json[p]<='z') || json[p]=='_')) ++p;
        std::string key(json.substr(start,p-start));
        if(p>=json.size() || json[p]!='"') return fail();
        ++p;if(!take(':')) return fail();
        ws();if(p>=json.size() || json[p]<'0' || json[p]>'9') return fail();
        int value=0;const auto result=std::from_chars(json.data()+p,json.data()+json.size(),value);
        if(result.ec!=std::errc{} || result.ptr==json.data()+p || value<0 || value>255) return fail();
        if(json[p]=='0' && result.ptr>json.data()+p+1) return fail();
        p=static_cast<std::size_t>(result.ptr-json.data());if(!values.emplace(key,value).second) return fail();
        if(take('}')) break;if(!take(',')) return fail();
    }
    ws();if(p!=json.size() || values.size()!=5) return fail();
    for(auto key:{"schema_version","red","green","blue","tolerance"}) if(!values.contains(key)) return fail();
    if(values["schema_version"]!=1) return fail();
    ColorMatchConfig color;color.target={static_cast<std::uint8_t>(values["blue"]),static_cast<std::uint8_t>(values["green"]),static_cast<std::uint8_t>(values["red"]),255};color.channelTolerance=values["tolerance"];
    return {color,true,{}};
}
ColorProfileResult loadColorProfile(const std::filesystem::path& path) {
    try {std::ifstream in(path,std::ios::binary);if(!in) return {{},false,"Cannot read color"};
        std::string data(8193,'\0');in.read(data.data(),static_cast<std::streamsize>(data.size()));
        if(in.bad() || in.gcount()>8192) return {{},false,"Color file read failed"};
        data.resize(static_cast<std::size_t>(in.gcount()));return deserializeColorProfile(data);
    } catch(const std::exception&) {return {{},false,"Color file read failed"};}
}
bool saveColorProfile(const std::filesystem::path& path,const ColorMatchConfig& color,std::string& error) {
    try {return writeAtomicTextFile(path,serializeColorProfile(color),error);}catch(const std::exception& e) {error=e.what();return false;}
}
}
