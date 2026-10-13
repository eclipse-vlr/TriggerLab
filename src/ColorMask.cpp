#include "triggerlab/ColorMask.hpp"
#include <cmath>
namespace triggerlab {
bool validColorConfig(const ColorMatchConfig& c) noexcept {
    return (c.space==ColorSpace::RGB || c.space==ColorSpace::HSV) && c.channelTolerance>=0 && c.channelTolerance<=255 &&
        std::isfinite(c.hueTolerance) && c.hueTolerance>=0 && c.hueTolerance<=180 &&
        std::isfinite(c.saturationTolerance) && c.saturationTolerance>=0 && c.saturationTolerance<=1 &&
        std::isfinite(c.valueTolerance) && c.valueTolerance>=0 && c.valueTolerance<=1 &&
        std::isfinite(c.minimumCoverage) && c.minimumCoverage>0 && c.minimumCoverage<=1;
}
Hsv toHsv(Pixel p) noexcept {
    const double r=p.red/255.,g=p.green/255.,b=p.blue/255.;
    const double maximum=std::max({r,g,b}),minimum=std::min({r,g,b}),delta=maximum-minimum;
    Hsv hsv{0,maximum==0?0:delta/maximum,maximum};
    if(delta==0) return hsv;
    if(maximum==r) hsv.hue=60*std::fmod((g-b)/delta,6.);
    else if(maximum==g) hsv.hue=60*((b-r)/delta+2);
    else hsv.hue=60*((r-g)/delta+4);
    if(hsv.hue<0) hsv.hue+=360;
    return hsv;
}
bool matchesColor(Pixel p,const ColorMatchConfig& c) noexcept {
    if(!validColorConfig(c)) return false;
    if(c.space==ColorSpace::RGB) return std::abs(int(p.red)-int(c.target.red))<=c.channelTolerance &&
        std::abs(int(p.green)-int(c.target.green))<=c.channelTolerance && std::abs(int(p.blue)-int(c.target.blue))<=c.channelTolerance;
    const auto a=toHsv(p),b=toHsv(c.target);
    const double difference=std::abs(a.hue-b.hue);
    // Hue is undefined for grayscale; use S/V alone when either color has no chroma.
    const bool hueMatches=a.saturation==0 || b.saturation==0 || std::min(difference,360-difference)<=c.hueTolerance;
    return hueMatches && std::abs(a.saturation-b.saturation)<=c.saturationTolerance && std::abs(a.value-b.value)<=c.valueTolerance;
}
ColorMask makeColorMask(const Frame& frame,Region region,const ColorMatchConfig& config) {
    if(!validColorConfig(config)) throw std::invalid_argument("Invalid color matching configuration");
    ColorMask result;
    if(!validRegion(region)) return result;
    const auto clipped=intersectRegion(region,{0,0,frame.width(),frame.height()});
    if(!clipped) return result;
    result.region=*clipped;
    result.pixels.resize(static_cast<std::size_t>(clipped->width*clipped->height));
    std::size_t index=0;
    for(int y=static_cast<int>(clipped->y);y<clipped->y+clipped->height;++y) {
        for(int x=static_cast<int>(clipped->x);x<clipped->x+clipped->width;++x) {
            const bool match=matchesColor(frame.at(x,y),config);
            result.pixels[index++]=match?255:0; result.matchingPixels+=match?1:0;
        }
    }
    result.coverage=static_cast<double>(result.matchingPixels)/result.pixels.size();
    result.found=result.coverage>=config.minimumCoverage;
    return result;
}
}
