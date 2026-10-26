#include "triggerlab/ColorProfile.hpp"
#include "triggerlab/Config.hpp"
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

namespace triggerlab {
std::string serializeColorProfile(const ColorMatchConfig& c) {
    if(!validColorConfig(c)) throw std::invalid_argument("Invalid color");
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<std::setprecision(17)
        <<"{\n  \"schema_version\": 2,\n  \"red\": "<<int(c.target.red)
        <<",\n  \"green\": "<<int(c.target.green)
        <<",\n  \"blue\": "<<int(c.target.blue)
        <<",\n  \"tolerance\": "<<c.channelTolerance
        <<",\n  \"color_space\": "<<(c.space==ColorSpace::RGB?0:1)
        <<",\n  \"hue_tolerance\": "<<c.hueTolerance
        <<",\n  \"saturation_tolerance\": "<<c.saturationTolerance
        <<",\n  \"value_tolerance\": "<<c.valueTolerance
        <<",\n  \"minimum_coverage\": "<<c.minimumCoverage<<"\n}\n";
    return out.str();
}

ColorProfileResult deserializeColorProfile(std::string_view json) {
    auto fail=[] {return ColorProfileResult{{},false,"Invalid color profile"};};
    std::size_t p=0;
    auto ws=[&] {while(p<json.size() && (json[p]==' ' || json[p]=='\n' || json[p]=='\r' || json[p]=='\t')) ++p;};
    auto take=[&](char c) {ws();if(p<json.size() && json[p]==c) {++p;return true;}return false;};
    std::map<std::string,double> values;
    if(!take('{')) return fail();
    for(;;) {
        if(!take('"')) return fail();
        const auto keyStart=p;
        while(p<json.size() && ((json[p]>='a' && json[p]<='z') || json[p]=='_')) ++p;
        std::string key(json.substr(keyStart,p-keyStart));
        if(key.empty() || p>=json.size() || json[p++]!='"' || !take(':')) return fail();
        ws();const auto numberStart=p;
        if(p<json.size() && json[p]=='-') ++p;
        if(p>=json.size() || json[p]<'0' || json[p]>'9') return fail();
        if(json[p]=='0') ++p;
        else while(p<json.size() && json[p]>='0' && json[p]<='9') ++p;
        if(p<json.size() && json[p]=='.') {
            const auto digits=++p;
            while(p<json.size() && json[p]>='0' && json[p]<='9') ++p;
            if(p==digits) return fail();
        }
        if(p<json.size() && (json[p]=='e' || json[p]=='E')) {
            ++p;if(p<json.size() && (json[p]=='+' || json[p]=='-')) ++p;
            const auto digits=p;
            while(p<json.size() && json[p]>='0' && json[p]<='9') ++p;
            if(p==digits) return fail();
        }
        double value=0;
        const auto parsed=std::from_chars(json.data()+numberStart,json.data()+p,value);
        if(parsed.ec!=std::errc{} || parsed.ptr!=json.data()+p || !std::isfinite(value) || !values.emplace(key,value).second) return fail();
        if(take('}')) break;
        if(!take(',')) return fail();
    }
    ws();if(p!=json.size()) return fail();
    for(auto key:{"schema_version","red","green","blue","tolerance"}) {
        const auto it=values.find(key);
        if(it==values.end() || it->second<0 || it->second>255 || std::floor(it->second)!=it->second) return fail();
    }
    const auto version=values.at("schema_version");
    if((version!=1 && version!=2) || values.size()!=(version==1?5u:10u)) return fail();
    ColorMatchConfig color;
    color.target={static_cast<std::uint8_t>(values.at("blue")),static_cast<std::uint8_t>(values.at("green")),static_cast<std::uint8_t>(values.at("red")),255};
    color.channelTolerance=static_cast<int>(values.at("tolerance"));
    if(version==2) {
        for(auto key:{"color_space","hue_tolerance","saturation_tolerance","value_tolerance","minimum_coverage"})
            if(!values.contains(key)) return fail();
        const auto space=values.at("color_space");
        if(space!=0 && space!=1) return fail();
        color.space=space==0?ColorSpace::RGB:ColorSpace::HSV;
        color.hueTolerance=values.at("hue_tolerance");
        color.saturationTolerance=values.at("saturation_tolerance");
        color.valueTolerance=values.at("value_tolerance");
        color.minimumCoverage=values.at("minimum_coverage");
    }
    if(!validColorConfig(color)) return fail();
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
