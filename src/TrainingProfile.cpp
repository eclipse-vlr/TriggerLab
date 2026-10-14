#include "triggerlab/TrainingProfile.hpp"
#include "triggerlab/Config.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <locale>
#include <algorithm>
namespace triggerlab {
namespace {
bool validName(const std::string& name) {
    return !name.empty() && name.size()<=120 && name.front()!=' ' && name.back()!=' ' &&
        std::none_of(name.begin(),name.end(),[](unsigned char c) {return c<32 || c==127;});
}
std::filesystem::path profilePath(const std::filesystem::path& directory,const std::string& name) {
    // User names are labels, never filesystem paths. Check collisions before replacement.
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:name) {hash^=c;hash*=1099511628211ULL;}
    std::ostringstream filename;filename<<"profile-"<<std::hex<<hash<<".tlprofile";return directory/filename.str();
}
}
std::optional<TrainingProfile> loadTrainingProfile(const std::filesystem::path& path) {
    try {
        std::ifstream file(path,std::ios::binary);if(!file) return {};
        std::string data(4097,'\0');file.read(data.data(),static_cast<std::streamsize>(data.size()));
        if(file.bad() || file.gcount()>4096) return {};data.resize(static_cast<std::size_t>(file.gcount()));
        std::istringstream input(data);input.imbue(std::locale::classic());std::string magic;
        TrainingProfile profile;int center=0,space=0,r=0,g=0,b=0;
        if(!std::getline(input,magic) || magic!="TriggerLab training profile 1" || !std::getline(input,profile.name) || !validName(profile.name)) return {};
        auto& s=profile.settings;auto& c=profile.color;
        if(!(input>>center>>s.side>>s.ignoreRadius>>s.minimumPercent>>s.offsetX>>s.offsetY>>s.dwellMs>>s.cooldownMs>>s.clickMs
            >>space>>r>>g>>b>>c.channelTolerance>>c.hueTolerance>>c.saturationTolerance>>c.valueTolerance>>c.minimumCoverage)) return {};
        input>>std::ws;if(!input.eof() || center<0 || center>1 || space<0 || space>1 || r<0 || r>255 || g<0 || g>255 || b<0 || b>255) return {};
        s.center=center==1;s.dryRun=true;c.space=space==0?ColorSpace::RGB:ColorSpace::HSV;
        c.target={static_cast<std::uint8_t>(b),static_cast<std::uint8_t>(g),static_cast<std::uint8_t>(r),255};
        if(!validAnalyzerSettings(s) || !validColorConfig(c)) return {};return profile;
    } catch(const std::exception&) {return {};}
}
bool saveTrainingProfile(const std::filesystem::path& directory,const TrainingProfile& profile,std::string& error) {
    try {
        if(!validName(profile.name) || !validAnalyzerSettings(profile.settings) || !validColorConfig(profile.color)) {error="Invalid training profile";return false;}
        const auto path=profilePath(directory,profile.name);
        if(std::filesystem::exists(path)) {const auto existing=loadTrainingProfile(path);if(!existing || existing->name!=profile.name) {error="Profile file conflicts; use another name";return false;}}
        std::ostringstream output;output.imbue(std::locale::classic());output<<std::setprecision(17);
        const auto& s=profile.settings;const auto& c=profile.color;
        output<<"TriggerLab training profile 1\n"<<profile.name<<'\n'<<s.center<<' '<<s.side<<' '<<s.ignoreRadius<<' '<<s.minimumPercent<<' '<<s.offsetX<<' '<<s.offsetY<<' '<<s.dwellMs<<' '<<s.cooldownMs<<' '<<s.clickMs<<'\n'
            <<(c.space==ColorSpace::RGB?0:1)<<' '<<int(c.target.red)<<' '<<int(c.target.green)<<' '<<int(c.target.blue)<<' '<<c.channelTolerance<<' '<<c.hueTolerance<<' '<<c.saturationTolerance<<' '<<c.valueTolerance<<' '<<c.minimumCoverage<<'\n';
        return writeAtomicTextFile(path,output.str(),error);
    } catch(const std::exception& e) {error=e.what();return false;}
}
std::vector<NamedProfile> listTrainingProfiles(const std::filesystem::path& directory) {
    std::vector<NamedProfile> result;std::error_code error;
    for(std::filesystem::directory_iterator it(directory,error),end;!error && it!=end;it.increment(error)) {
        if(it->is_regular_file(error) && it->path().extension()==".tlprofile") if(auto profile=loadTrainingProfile(it->path())) result.push_back({profile->name,it->path()});
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) {return a.name<b.name;});return result;
}
}
