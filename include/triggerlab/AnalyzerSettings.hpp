#pragma once
#include "triggerlab/ColorMask.hpp"
#include <filesystem>
#include <string>
namespace triggerlab {
struct AnalyzerSettings {
    bool center=true,dryRun=true;
    int side=31,ignoreRadius=3,minimumPercent=3,offsetX=0,offsetY=0,dwellMs=70,cooldownMs=50,clickMs=30;
    bool darkTheme=false,advancedMode=false;
};
bool validAnalyzerSettings(const AnalyzerSettings&) noexcept;
AnalyzerSettings loadAnalyzerSettings(const std::filesystem::path&);
bool saveAnalyzerSettings(const std::filesystem::path&,const AnalyzerSettings&,std::string& error);
std::optional<Region> aimRegion(Region client,const AnalyzerSettings&) noexcept;
// Coverage remains diagnostic; found also requires alignment at the aim point
// or on all four cardinal or diagonal probes outside an excluded crosshair.
ColorMask aimColorMask(const Frame&,const ColorMatchConfig&,const AnalyzerSettings&);
struct ColorSampleCheck {
    bool valid=true;
    std::optional<double> targetCoverage,backgroundCoverage;
    bool targetFound=false,backgroundFound=false;
};
// Read-only assessment of user-labelled samples; never authorizes input.
ColorSampleCheck compareColorSamples(const Frame* target,const Frame* background,const ColorMatchConfig&,const AnalyzerSettings&);
int excludedColorPixels(const Frame&,const ColorMatchConfig&,const AnalyzerSettings&);
}
