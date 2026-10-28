#include "triggerlab/ColorMask.hpp"
#include "triggerlab/TargetDetector.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
void check(bool c, const char *why) {
    if (!c) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
int main() {
    using namespace triggerlab;
    ColorMatchConfig c;
    c.target = {10, 20, 30, 255};
    c.channelTolerance = 5;
    check(matchesColor({15, 15, 35, 0}, c), "inclusive RGB tolerance and alpha ignored");
    check(!matchesColor({16, 20, 30, 255}, c), "RGB outside tolerance");
    c.target = {0, 0, 255, 255};
    c.channelTolerance = 0;
    check(matchesColor({0, 0, 255, 255}, c) && !matchesColor({255, 0, 0, 255}, c), "red is not blue");
    Frame frame(3, 2);
    frame.clear({255, 255, 255, 255});
    frame.at(0, 0) = c.target;
    c.minimumCoverage = 1. / 6;
    auto mask = makeColorMask(frame, c);
    check(mask.pixels.size() == 6 && mask.pixels[0] == 255 && mask.pixels[1] == 0 && mask.found &&
              mask.matchingPixels == 1,
          "mask layout and coverage boundary");
    c.minimumCoverage = .17;
    check(!makeColorMask(frame, c).found, "coverage below threshold");
    c.minimumCoverage = .25;
    auto clipped = makeColorMask(frame, {-1, -1, 3, 3}, c);
    check(clipped.pixels.size() == 4 && clipped.coverage == .25 && clipped.found, "clipped mask coverage");
    check(makeColorMask(frame, {99, 99, 1, 1}, c).pixels.empty(), "outside ROI");
    check(makeColorMask(frame, {0, 0, 0, 1}, c).pixels.empty(), "empty ROI");
    TargetDetector legacy({.roiRadius = 1});
    check(legacy.detectColor(frame, 0, 0, c).pixels.size() == 4, "legacy extension clips ROI");
    check(!legacy.detectColor(frame, -1, 0, c).found, "outside detector center");
    c.space = ColorSpace::HSV;
    c.target = {0, 0, 255, 255};
    c.hueTolerance = 3;
    c.saturationTolerance = 0;
    c.valueTolerance = 0;
    check(matchesColor({8, 0, 255, 255}, c) && matchesColor({0, 8, 255, 255}, c), "circular hue near red");
    check(!matchesColor({20, 0, 255, 255}, c), "hue outside tolerance");
    c.hueTolerance = 60;
    check(matchesColor({0, 255, 255, 255}, c), "hue boundary yellow");
    c.target = {0, 0, 0, 255};
    c.valueTolerance = 0;
    check(matchesColor({0, 0, 0, 255}, c) && !matchesColor({255, 255, 255, 255}, c), "black vs white HSV");
    c.target = {255, 255, 255, 255};
    check(matchesColor(c.target, c), "white HSV undefined hue");
    check(std::abs(toHsv({255, 0, 0, 255}).hue - 240) < 1e-9, "blue hue");
    check(std::abs(toHsv({0, 255, 0, 255}).hue - 120) < 1e-9, "green hue");
    c.minimumCoverage = std::numeric_limits<double>::quiet_NaN();
    bool threw = false;
    try {
        makeColorMask(frame, c);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    check(threw, "NaN rejected");
    c = {};
    c.channelTolerance = 256;
    check(!matchesColor(c.target, c), "invalid tolerance rejects match");
    for (auto target : {Pixel{0, 0, 0, 255}, Pixel{255, 255, 255, 255}}) {
        c = {};
        c.target = target;
        c.channelTolerance = 0;
        frame.clear(target);
        check(makeColorMask(frame, c).coverage == 1, "RGB black and white");
    }
    std::cout << "PASS: color matching and masks\n";
}
