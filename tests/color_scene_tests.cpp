#include "triggerlab/AnalyzerSettings.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <string>

namespace {
using namespace triggerlab;
int failures = 0, scenes = 0;
Pixel rgb(int red, int green, int blue) {
    return {static_cast<std::uint8_t>(blue), static_cast<std::uint8_t>(green), static_cast<std::uint8_t>(red),
            255};
}
void check(bool value, const std::string &name) {
    if (!value) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}
Frame background(int kind) {
    Frame image(31, 31);
    for (int y = 0; y < 31; ++y)
        for (int x = 0; x < 31; ++x) {
            if (kind == 0)
                image.at(x, y) = rgb(20, 20, 30);
            else if (kind == 1) {
                const int gray = x * 255 / 30;
                image.at(x, y) = rgb(gray, gray, gray);
            } else {
                // Fixed texture, no random seed or dependency on the machine.
                const int shade = (x * 17 + y * 29) % 70;
                image.at(x, y) = rgb(shade, 40, 100 + shade);
            }
        }
    return image;
}
void rectangle(Frame &image, int left, int top, int width, int height, Pixel color) {
    for (int y = top; y < top + height; ++y)
        for (int x = left; x < left + width; ++x)
            image.at(x, y) = color;
}
ColorMask scene(const std::string &name, const Frame &image, const ColorMatchConfig &color,
                const AnalyzerSettings &settings, int matching, int eligible, bool found, int excluded = 0) {
    ++scenes;
    const auto mask = aimColorMask(image, color, settings);
    check(mask.matchingPixels == matching, name + ": matching pixel count");
    check(std::abs(mask.coverage - double(matching) / eligible) < 1e-12, name + ": coverage denominator");
    check(mask.found == found, name + ": detection decision");
    check(mask.pixels.size() == 961, name + ": full ROI mask");
    int white = 0;
    bool binary = true;
    for (auto value : mask.pixels) {
        white += value == 255;
        binary = binary && (value == 0 || value == 255);
    }
    check(binary, name + ": binary mask");
    check(white == matching, name + ": mask agrees with count");
    check(excludedColorPixels(image, color, settings) == excluded, name + ": excluded crosshair count");
    std::cout << name << " matching=" << mask.matchingPixels << " coverage=" << mask.coverage
              << " found=" << mask.found << '\n';
    return mask;
}
} // namespace
int main() {
    using namespace triggerlab;
    AnalyzerSettings settings;
    ColorMatchConfig color;
    color.target = rgb(215, 198, 35);
    color.channelTolerance = 24;
    // Color at the ROI edge remains visible in the mask, but must not authorize
    // a click before the aim point reaches the target.
    const std::array<std::array<int, 2>, 5> positions = {{{0, 0}, {25, 0}, {0, 25}, {25, 25}, {0, 12}}};
    for (auto space : {ColorSpace::RGB, ColorSpace::HSV}) {
        color.space = space;
        for (int kind = 0; kind < 3; ++kind) {
            auto plain = background(kind);
            const auto prefix =
                std::string(space == ColorSpace::RGB ? "rgb" : "hsv") + "-background-" + std::to_string(kind);
            scene(prefix, plain, color, settings, 0, 912, false);
            for (std::size_t i = 0; i < positions.size(); ++i) {
                auto image = plain;
                rectangle(image, positions[i][0], positions[i][1], 6, 6, color.target);
                const auto mask =
                    scene(prefix + "-target-" + std::to_string(i), image, color, settings, 36, 912, false);
                if (mask.pixels.size() == 961)
                    for (int dy : {0, 5})
                        for (int dx : {0, 5})
                            check(mask.pixels[(positions[i][1] + dy) * 31 + positions[i][0] + dx] == 255,
                                  prefix + ": target position in top-down mask");
                const auto quality = compareColorSamples(&image, &plain, color, settings);
                check(quality.valid && !quality.targetFound && !quality.backgroundFound,
                      prefix + ": off-center sample needs alignment");
                check(quality.targetCoverage && std::abs(*quality.targetCoverage - 36. / 912) < 1e-12 &&
                          quality.backgroundCoverage && *quality.backgroundCoverage == 0,
                      prefix + ": labelled sample coverage");
            }
            auto centered = plain;
            rectangle(centered, 11, 11, 9, 9, color.target);
            rectangle(centered, 12, 12, 7, 7, rgb(255, 255, 255));
            scene(prefix + "-centered", centered, color, settings, 32, 912, true);
        }
    }
    color.space = ColorSpace::RGB;
    auto image = background(0);
    rectangle(image, 0, 0, 3, 3, color.target);
    scene("tiny-3x3", image, color, settings, 9, 912, false);
    // Both counts straddle the default 3% threshold (27/912 and 28/912).
    image = background(0);
    rectangle(image, 11, 11, 9, 9, color.target);
    rectangle(image, 12, 12, 7, 7, rgb(255, 255, 255));
    for (int x : {11, 12, 13, 14, 16})
        image.at(x, 11) = rgb(20, 20, 30);
    scene("below-threshold-27", image, color, settings, 27, 912, false);
    image.at(16, 11) = color.target;
    scene("above-threshold-28", image, color, settings, 28, 912, true);
    settings.ignoreRadius = 0;
    scene("exclusion-off-same-28", image, color, settings, 28, 961, false);
    image.at(15, 15) = color.target;
    scene("exclusion-off-29", image, color, settings, 29, 961, true);
    settings.ignoreRadius = 3;
    image = background(0);
    rectangle(image, 12, 12, 7, 7, color.target);
    scene("crosshair-only", image, color, settings, 0, 912, false, 49);
    settings.ignoreRadius = 0;
    scene("crosshair-with-exclusion-off", image, color, settings, 49, 961, true);
    settings.ignoreRadius = 3;
    image = background(0);
    rectangle(image, 11, 11, 9, 9, color.target);
    rectangle(image, 12, 12, 7, 7, rgb(255, 255, 255));
    scene("target-ring-around-white-crosshair", image, color, settings, 32, 912, true);
    settings.ignoreRadius = 0;
    image = background(0);
    rectangle(image, 12, 12, 6, 6, rgb(239, 174, 59));
    scene("rgb-inclusive-tolerance-24", image, color, settings, 36, 961, true);
    rectangle(image, 12, 12, 6, 6, rgb(240, 174, 59));
    scene("rgb-outside-tolerance-25", image, color, settings, 0, 961, false);
    // Screen antialiasing is represented by already composited opaque pixels.
    image = background(0);
    rectangle(image, 12, 12, 6, 6, rgb(118, 110, 32));
    rectangle(image, 13, 13, 4, 4, color.target);
    scene("antialiased-small-target-rgb", image, color, settings, 16, 961, false);
    color.space = ColorSpace::HSV;
    color.valueTolerance = .4;
    scene("antialiased-small-target-relaxed-hsv", image, color, settings, 36, 961, true);
    color = {};
    color.target = rgb(255, 255, 0);
    color.channelTolerance = 24;
    image = background(0);
    rectangle(image, 12, 12, 6, 6, rgb(200, 200, 0));
    scene("dim-yellow-rgb", image, color, settings, 0, 961, false);
    color.space = ColorSpace::HSV;
    color.valueTolerance = .25;
    scene("dim-yellow-hsv", image, color, settings, 36, 961, true);
    color.target = rgb(255, 0, 0);
    color.hueTolerance = 3;
    color.saturationTolerance = 0;
    color.valueTolerance = 0;
    image = background(0);
    rectangle(image, 12, 12, 6, 6, rgb(255, 0, 10));
    scene("hue-wrap-near-red", image, color, settings, 36, 961, true);
    rectangle(image, 12, 12, 6, 6, rgb(255, 14, 0));
    scene("hue-outside-three-degrees", image, color, settings, 0, 961, false);
    color = {};
    color.target = rgb(215, 198, 35);
    auto target = background(0);
    rectangle(target, 12, 12, 6, 6, color.target);
    auto similarBackground = background(1);
    rectangle(similarBackground, 12, 12, 6, 6, color.target);
    scene("same-color-background", similarBackground, color, settings, 36, 961, true);
    const auto quality = compareColorSamples(&target, &similarBackground, color, settings);
    check(quality.valid && quality.targetFound && quality.backgroundFound,
          "same-color background is reported as a false-positive risk");
    std::cout << "scenes=" << scenes << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
