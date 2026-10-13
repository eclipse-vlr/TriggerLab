#include "triggerlab/Simulation.hpp"

#include <algorithm>
#include <cmath>

namespace triggerlab {

void Simulation::onSimulatedShot(int aimX, int aimY) noexcept {
    hitFlashSeconds_=.22; hitFlashTarget_=0;
    if(config_.profile==SceneProfile::Multiple) {
        if(aimX<0) aimX=width_/2;
        if(aimY<0) aimY=height_/2;
        double closest=1e20;
        for(int i=0;i<3;++i) {
            const double cx=width_/2+std::lround(.20*width_*std::sin(timeSeconds_*1.05+i*2.094395102393195));
            const double cy=height_/2+std::lround(.035*height_*std::sin(timeSeconds_*.68+i*2.094395102393195));
            const double distance=(cx-aimX)*(cx-aimX)+(cy-aimY)*(cy-aimY);
            if(distance<closest) { closest=distance; hitFlashTarget_=i; }
        }
    }
}
void Simulation::setConfig(SceneConfig config) noexcept {
    if(static_cast<int>(config.profile)<0 || static_cast<int>(config.profile)>2) config.profile=SceneProfile::Crossing;
    config.speed=std::isfinite(config.speed)?std::clamp(config.speed,.25,3.0):1.0;
    config_=config; reset();
}
void Simulation::advance(double deltaSeconds) noexcept {
    const double delta = std::clamp(deltaSeconds, 0.0, 0.1);
    timeSeconds_ += delta * config_.speed;
    hitFlashSeconds_ = std::max(0.0, hitFlashSeconds_ - delta);
}

void Simulation::horizontalLine(Frame& frame, int y, Pixel color) noexcept {
    if (y < 0 || y >= frame.height()) return;
    for (int x = 0; x < frame.width(); ++x) frame.setIfInside(x, y, color);
}

void Simulation::fillEllipse(Frame& frame, int cx, int cy, int rx, int ry, Pixel color) noexcept {
    if (rx < 1 || ry < 1) return;
    const int left = std::max(0, cx - rx);
    const int right = std::min(frame.width() - 1, cx + rx);
    const int top = std::max(0, cy - ry);
    const int bottom = std::min(frame.height() - 1, cy + ry);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const double dx = static_cast<double>(x - cx) / rx;
            const double dy = static_cast<double>(y - cy) / ry;
            if (dx * dx + dy * dy <= 1.0) frame.setIfInside(x, y, color);
        }
    }
}

void Simulation::render(Frame& frame) const noexcept {
    constexpr Pixel background{31, 24, 18, 255};
    constexpr Pixel gridLine{53, 40, 32, 255};
    constexpr Pixel targetBorder{248, 138, 255, 255};
    constexpr Pixel targetMain{230, 65, 237, 255};
    constexpr Pixel accent{111, 226, 170, 255};
    frame.clear(background);

    const int w = frame.width();
    const int h = frame.height();
    const int floor = h * 2 / 3;
    horizontalLine(frame, floor, accent);
    for (int y = floor + 20; y < h; y += 28) horizontalLine(frame, y, gridLine);
    for (int x = 0; x < w; x += 64) {
        for (int y = floor + 1; y < h; ++y) frame.setIfInside(x, y, gridLine);
    }

    // Moving practice dummy crossing the fixed center crosshair periodically.
    const int count=config_.profile==SceneProfile::Multiple?3:1;
    for(int target=0;target<count;++target) {
    const double phase=target*2.094395102393195;
    const int cx = w / 2 + static_cast<int>(std::lround(0.20 * w * std::sin(timeSeconds_ * 1.05+phase)));
    const int cy = h / 2 + static_cast<int>(std::lround(0.035 * h * std::sin(timeSeconds_ * 0.68+phase)));
    const int scale = std::max(3, std::min(w, h) / (config_.profile==SceneProfile::Precision?64:32));
    fillEllipse(frame, cx, cy - 2 * scale, scale + 2, scale + 2, targetBorder);
    fillEllipse(frame, cx, cy - 2 * scale, scale, scale, targetMain);
    fillEllipse(frame, cx, cy + scale, 2 * scale, 3 * scale, targetBorder);
    fillEllipse(frame, cx, cy + scale, 2 * scale - 2, 3 * scale - 2, targetMain);

    if (hitFlashSeconds_ > 0.0 && target==hitFlashTarget_) {
        // Simple short-lived flash visible around the shooting dummy.
        for (int r = 3 * scale + 4; r <= 3 * scale + 7; ++r) {
            constexpr double tau = 6.283185307179586;
            for (int step = 0; step < 180; ++step) {
                const double a = tau * step / 180.0;
                frame.setIfInside(cx + static_cast<int>(std::lround(r * std::cos(a))),
                                  cy + static_cast<int>(std::lround(r * std::sin(a))), accent);
            }
        }
    }
    }
}

} // namespace triggerlab
