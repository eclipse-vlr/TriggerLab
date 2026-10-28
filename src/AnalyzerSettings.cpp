#include "triggerlab/AnalyzerSettings.hpp"
#include <algorithm>
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
    // Coverage alone detects a target entering the outer ROI before the aim
    // point reaches it. Require fresh color at the center, or on all four
    // sides immediately outside an obscuring crosshair. A second, diagonal
    // quartet avoids the arms of a plus-shaped crosshair. Each quartet must
    // surround the aim point; color on just one side is insufficient.
    auto matching=[&](int x,int y) {
        return frame.contains(x,y) && mask.pixels[static_cast<std::size_t>(y)*frame.width()+x]!=0;
    };
    const int radius=settings.ignoreRadius+1;
    const bool cardinal=matching(cx-radius,cy) && matching(cx+radius,cy) && matching(cx,cy-radius) && matching(cx,cy+radius);
    const bool diagonal=matching(cx-radius,cy-radius) && matching(cx+radius,cy-radius) &&
        matching(cx-radius,cy+radius) && matching(cx+radius,cy+radius);
    const bool centered=settings.ignoreRadius==0?matching(cx,cy):(cardinal || diagonal);
    mask.found=centered && eligible>0 && mask.matchingPixels>0 && mask.coverage*100>=settings.minimumPercent;
    return mask;
}
ColorSampleCheck compareColorSamples(const Frame* target,const Frame* background,const ColorMatchConfig& color,const AnalyzerSettings& settings) {
    ColorSampleCheck result;
    auto compatible=[&](const Frame* frame) {return !frame || (frame->width()==settings.side && frame->height()==settings.side);};
    if(!settings.center || !validAnalyzerSettings(settings) || !validColorConfig(color) || !compatible(target) || !compatible(background)) {result.valid=false;return result;}
    if(target) {const auto mask=aimColorMask(*target,color,settings);result.targetCoverage=mask.coverage;result.targetFound=mask.found;}
    if(background) {const auto mask=aimColorMask(*background,color,settings);result.backgroundCoverage=mask.coverage;result.backgroundFound=mask.found;}
    return result;
}
int excludedColorPixels(const Frame& frame,const ColorMatchConfig& color,const AnalyzerSettings& settings) {
    if(!settings.center || !validAnalyzerSettings(settings) || !validColorConfig(color) || settings.ignoreRadius==0) return 0;
    int count=0;const int cx=frame.width()/2,cy=frame.height()/2,r=settings.ignoreRadius;
    for(int y=std::max(0,cy-r);y<=std::min(frame.height()-1,cy+r);++y)
        for(int x=std::max(0,cx-r);x<=std::min(frame.width()-1,cx+r);++x) if(matchesColor(frame.at(x,y),color)) ++count;
    return count;
}
}
