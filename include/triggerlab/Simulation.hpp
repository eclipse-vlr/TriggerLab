#pragma once

#include "triggerlab/Frame.hpp"

namespace triggerlab {

enum class SceneProfile { Crossing, Multiple, Precision };
struct SceneConfig { SceneProfile profile=SceneProfile::Crossing; double speed=1.0; };
class Simulation {
public:
    Simulation(int width, int height) noexcept : width_(width), height_(height) {}
    void setConfig(SceneConfig config) noexcept;
    [[nodiscard]] const SceneConfig& config() const noexcept { return config_; }
    void reset() noexcept { timeSeconds_=0; hitFlashSeconds_=0; }
    void advance(double deltaSeconds) noexcept;
    void render(Frame& frame) const noexcept;
    void onSimulatedShot() noexcept;
    [[nodiscard]] bool isHitFlashActive() const noexcept { return hitFlashSeconds_ > 0.0; }
    [[nodiscard]] double elapsedSeconds() const noexcept { return timeSeconds_; }

private:
    static void fillEllipse(Frame& frame, int cx, int cy, int rx, int ry, Pixel color) noexcept;
    static void horizontalLine(Frame& frame, int y, Pixel color) noexcept;

    int hitFlashTarget_=0;
    SceneConfig config_;
    int width_;
    int height_;
    double timeSeconds_ = 0.0;
    double hitFlashSeconds_ = 0.0;
};

} // namespace triggerlab
