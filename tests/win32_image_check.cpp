#include <windows.h>
#include "triggerlab/ImageFile.hpp"
#include "triggerlab/AnalyzerSettings.hpp"
#include "triggerlab/TriggerEngine.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using namespace triggerlab;
struct Options {ColorMatchConfig color;AnalyzerSettings settings;int x=0,y=0;};
int number(std::wstring_view value) {
    std::string ascii;
    for(auto c:value) {if(c>127) throw std::invalid_argument("Expected a decimal integer");ascii+=static_cast<char>(c);}
    int result=0;const auto parsed=std::from_chars(ascii.data(),ascii.data()+ascii.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=ascii.data()+ascii.size()) throw std::invalid_argument("Invalid decimal integer");
    return result;
}
Options options(int argc,wchar_t** argv) {
    if(argc<7 || argc>11) throw std::invalid_argument("Usage: TriggerLabImageCheck IMAGE R G B X Y [SIDE [TOLERANCE [IGNORE [PERCENT]]]]");
    std::array<int,9> values{255,0,255,0,0,31,24,3,3};
    for(int i=2;i<argc;++i) values[i-2]=number(argv[i]);
    for(int i=0;i<3;++i) if(values[i]<0 || values[i]>255) throw std::invalid_argument("RGB must be in [0,255]");
    Options result;
    result.color.target={static_cast<std::uint8_t>(values[2]),static_cast<std::uint8_t>(values[1]),static_cast<std::uint8_t>(values[0]),255};
    result.x=values[3];result.y=values[4];result.settings.side=values[5];result.color.channelTolerance=values[6];
    result.settings.ignoreRadius=values[7];result.settings.minimumPercent=values[8];
    if(result.x<0 || result.y<0 || !validAnalyzerSettings(result.settings) || !validColorConfig(result.color))
        throw std::invalid_argument("Invalid ROI, tolerance, exclusion or threshold");
    return result;
}
Frame crop(const Frame& image,const Options& opts) {
    const auto side=opts.settings.side;
    if(opts.x<0 || opts.y<0 || opts.x>image.width()-side || opts.y>image.height()-side)
        throw std::invalid_argument("ROI must fit entirely inside the image");
    Frame result(side,side);
    for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
        const auto pixel=image.at(opts.x+x,opts.y+y);
        if(pixel.alpha!=255) throw std::invalid_argument("ROI contains transparency; use an opaque screenshot");
        result.at(x,y)=pixel;
    }
    return result;
}
void writeFixture(const std::filesystem::path& path) {
    BITMAPFILEHEADER header{};header.bfType=0x4D42;header.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);
    header.bfSize=header.bfOffBits+32*32*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=32;info.biHeight=-32;info.biPlanes=1;info.biBitCount=32;
    Frame image(32,32);image.clear({30,20,20,255});
    for(int y=1;y<7;++y) for(int x=1;x<7;++x) image.at(x,y)={35,198,215,255};
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header),sizeof(header));out.write(reinterpret_cast<const char*>(&info),sizeof(info));
    out.write(reinterpret_cast<const char*>(image.data()),32*32*4);
    if(!out) throw std::runtime_error("Cannot write test image");
}
int selfTest() {
    int failures=0;
    auto check=[&](bool ok,const char* message) {if(!ok) {std::cerr<<"FAIL: "<<message<<'\n';++failures;}};
    auto rejects=[&](auto action,const char* message) {bool rejected=false;try {action();} catch(const std::invalid_argument&) {rejected=true;}check(rejected,message);};
    for(auto invalid:{L"",L"2.5",L"0x10",L"12x",L"99999999999999",L"１２"}) rejects([&] {number(invalid);},"bad integer rejected");
    check(number(L"0")==0 && number(L"255")==255,"decimal boundaries");
    auto parse=[&](int index,const wchar_t* replacement) {
        std::array<std::wstring,11> args={L"check",L"image.png",L"215",L"198",L"35",L"0",L"0",L"31",L"24",L"3",L"3"};
        args[index]=replacement;std::array<wchar_t*,11> pointers{};
        for(std::size_t i=0;i<args.size();++i) pointers[i]=args[i].data();
        return options(static_cast<int>(pointers.size()),pointers.data());
    };
    const auto parsed=parse(2,L"255");check(parsed.color.target.red==255 && parsed.color.target.blue==35,"CLI uses RGB order");
    for(auto invalid:{L"-1",L"256"}) rejects([&] {parse(2,invalid);},"invalid RGB rejected");
    rejects([&] {parse(5,L"-1");},"negative CLI offset rejected");
    for(auto invalid:{L"4",L"30",L"102"}) rejects([&] {parse(7,invalid);},"invalid side rejected");
    rejects([&] {parse(8,L"256");},"invalid tolerance rejected");
    rejects([&] {parse(9,L"15");},"full exclusion rejected");
    for(auto invalid:{L"0",L"101"}) rejects([&] {parse(10,invalid);},"invalid percent rejected");
    rejects([&] {options(6,nullptr);},"missing CLI values rejected");
    rejects([&] {options(12,nullptr);},"excess CLI values rejected");
    const auto dir=std::filesystem::temp_directory_path()/(L"triggerlab-image-check-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(Clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    struct Cleanup {std::filesystem::path path;~Cleanup() {std::error_code error;std::filesystem::remove_all(path,error);}} cleanup{dir};
    const auto path=dir/L"образец.bmp";writeFixture(path);
    auto loaded=loadImagePixels(path);check(loaded.image.has_value(),"decode saved image through WIC");
    if(!loaded.image) return 1;
    Options opts;opts.x=opts.y=1;opts.color.target={35,198,215,255};
    const auto image=crop(*loaded.image,opts);const auto mask=aimColorMask(image,opts.color,opts.settings);
    check(!mask.found && mask.matchingPixels==36 && std::abs(mask.coverage-36./912)<1e-12,"decoded ROI preserves color coverage but rejects off-center target");
    opts.x=std::numeric_limits<int>::max();rejects([&] {crop(*loaded.image,opts);},"large offset rejected without overflow");
    opts.x=2;rejects([&] {crop(*loaded.image,opts);},"partial ROI rejected");
    opts.x=-1;rejects([&] {crop(*loaded.image,opts);},"negative ROI rejected");
    opts.x=1;loaded.image->at(1,1).alpha=128;rejects([&] {crop(*loaded.image,opts);},"transparent screenshot ROI rejected");
    check(!loadImagePixels(dir/L"missing.png").image,"missing image rejected");
    const auto corrupt=dir/L"corrupt.png";{std::ofstream out(corrupt);out<<"invalid";}
    check(!loadImagePixels(corrupt).image,"corrupt image rejected");
    std::cout<<"image_check_failures="<<failures<<'\n';return failures?1:0;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==2 && std::wstring_view(argv[1])==L"--self-test") return selfTest();
        const auto opts=options(argc,argv);
        auto loaded=triggerlab::loadImagePixels(argv[1]);
        if(!loaded.image) {std::wcerr<<L"Image error: "<<loaded.error<<L'\n';return 1;}
        const auto image=crop(*loaded.image,opts);const auto mask=triggerlab::aimColorMask(image,opts.color,opts.settings);
        std::cout<<std::setprecision(10)<<"{\"width\":"<<loaded.image->width()<<",\"height\":"<<loaded.image->height()
            <<",\"roi_x\":"<<opts.x<<",\"roi_y\":"<<opts.y<<",\"side\":"<<opts.settings.side
            <<",\"red\":"<<int(opts.color.target.red)<<",\"green\":"<<int(opts.color.target.green)<<",\"blue\":"<<int(opts.color.target.blue)
            <<",\"tolerance\":"<<opts.color.channelTolerance<<",\"ignore_radius\":"<<opts.settings.ignoreRadius<<",\"minimum_percent\":"<<opts.settings.minimumPercent
            <<",\"matching_pixels\":"<<mask.matchingPixels<<",\"coverage_percent\":"<<mask.coverage*100
            <<",\"found\":"<<(mask.found?"true":"false")<<",\"excluded_pixels\":"<<triggerlab::excludedColorPixels(image,opts.color,opts.settings)<<"}\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
