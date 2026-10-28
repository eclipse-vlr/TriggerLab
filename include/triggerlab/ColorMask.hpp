#pragma once
#include "triggerlab/Frame.hpp"
#include "triggerlab/Region.hpp"
#include <vector>
namespace triggerlab {
enum class ColorSpace { RGB, HSV };
struct Hsv {
    double hue = 0, saturation = 0, value = 0;
};
struct ColorMatchConfig {
    ColorSpace space = ColorSpace::RGB;
    Pixel target{255, 0, 255, 255};
    int channelTolerance = 24;
    double hueTolerance = 12, saturationTolerance = .15, valueTolerance = .15;
    double minimumCoverage = .18;
};
bool validColorConfig(const ColorMatchConfig &config) noexcept;
Hsv toHsv(Pixel pixel) noexcept;
bool matchesColor(Pixel pixel, const ColorMatchConfig &config) noexcept;
struct ColorMask {
    Region region{};
    int matchingPixels = 0;
    double coverage = 0;
    bool found = false;
    std::vector<std::uint8_t> pixels; // top-down 0/255, one byte per pixel
};
ColorMask makeColorMask(const Frame &frame, Region region, const ColorMatchConfig &config);
inline ColorMask makeColorMask(const Frame &frame, const ColorMatchConfig &config) {
    return makeColorMask(frame, {0, 0, frame.width(), frame.height()}, config);
}
} // namespace triggerlab
