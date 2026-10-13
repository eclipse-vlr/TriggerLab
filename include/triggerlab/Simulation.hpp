#pragma once

#include "triggerlab/Frame.hpp"

namespace triggerlab {

class Simulation {
public:
    Simulation(int width, int height) noexcept : width_(width), height_(height) {}
    void advance(double deltaSeconds) noexcept;
    void render(Frame& frame) const noexcept;
    void onSimulatedShot() noexcept { hitFlashSeconds_ = 0.22; }
    [[nodiscard]] bool isHitFlashActive() const noexcept { return hitFlashSeconds_ > 0.0; }
    [[nodiscard]] double elapsedSeconds() const noexcept { return timeSeconds_; }

private:
    static void fillEllipse(Frame& frame, int cx, int cy, int rx, int ry, Pixel color) noexcept;
    static void horizontalLine(Frame& frame, int y, Pixel color) noexcept;

    int width_;
    int height_;
    double timeSeconds_ = 0.0;
    double hitFlashSeconds_ = 0.0;
};

} // namespace triggerlab
