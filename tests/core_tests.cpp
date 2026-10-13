#include "triggerlab/Frame.hpp"
#include "triggerlab/Simulation.hpp"
#include "triggerlab/TargetDetector.hpp"
#include "triggerlab/TriggerEngine.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

int checks = 0;

void check(bool condition, const char* why) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << why << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testFrame() {
    using namespace triggerlab;
    Frame frame(30, 20);
    check(frame.width() == 30 && frame.height() == 20, "frame dimensions");
    check(!frame.contains(-1, 0) && !frame.contains(30, 0), "bounds");
    frame.clear({9, 10, 11, 255});
    check(frame.at(20, 10).red == 11, "frame clear");
    frame.setIfInside(-1, 2, {200, 200, 200, 255});
    check(frame.at(0, 2).blue == 9, "clipped drawing");
    bool threw = false;
    try { Frame invalid(0, 50); (void)invalid; } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "invalid frame dimensions rejected");
}

void testDetector() {
    using namespace triggerlab;
    Frame frame(41, 41);
    frame.clear({22, 20, 19, 255});
    TargetDetector detector;
    check(!detector.detect(frame, 20, 20).found, "empty scene no detection");
    check(!detector.detect(frame, 100, 100).found, "outside frame no detection");

    constexpr Pixel target{230, 65, 237, 255};
    for (int y = 17; y <= 24; ++y) {
        for (int x = 17; x <= 24; ++x) frame.setIfInside(x, y, target);
    }
    const Detection detection = detector.detect(frame, 20, 20);
    check(detection.found, "target detected at crosshair");
    check(detection.matchingPixels == 64 && detection.samplePixels == 121,
          "matching pixel counts");
    // 64/121 > 45%: the 8x8 target is sufficiently large for the default ROI.
}

void testDetectorThresholdsAndEdges() {
    using namespace triggerlab;
    Frame frame(10, 10);
    frame.clear({0, 0, 0, 255});
    TargetDetector detector;
    detector.setConfig({.roiRadius = 1, .minimumCoverage = 0.4});
    frame.setIfInside(0, 0, {230, 65, 237, 255});
    check(!detector.detect(frame, 0, 0).found, "1/4 coverage below threshold");
    frame.setIfInside(0, 1, {230, 65, 237, 255});
    const Detection edge = detector.detect(frame, 0, 0);
    check(edge.found && edge.samplePixels == 4, "ROI safely clipped at frame edge");
    detector.setConfig({.roiRadius = 999, .minimumCoverage = 2.0});
    check(detector.config().roiRadius == 30 && detector.config().minimumCoverage == 1.0,
          "detector configuration clamped");
}

void testTriggerEngine() {
    using namespace triggerlab;
    using namespace std::chrono_literals;
    TriggerEngine engine({.cooldown = 180ms, .stableFrames = 2});
    const auto t = Clock::time_point{} + 1s;
    check(!engine.step({false, true}, t).fired && engine.state() == TriggerState::Idle,
          "disabled engine never fires");
    check(!engine.step({true, false}, t + 10ms).fired && engine.state() == TriggerState::Armed,
          "arming with no target");
    check(!engine.step({true, true}, t + 20ms).fired && engine.state() == TriggerState::Detected,
          "first confirmed frame does not fire");
    check(engine.step({true, true}, t + 30ms).fired && engine.shotCount() == 1,
          "two frames produce simulated event");
    check(!engine.step({true, true}, t + 40ms).fired && engine.state() == TriggerState::Cooldown,
          "cooldown blocks repeated shot");
    check(!engine.step({true, true}, t + 230ms).fired, "stable-frame counter restarted");
    check(engine.step({true, true}, t + 240ms).fired && engine.shotCount() == 2,
          "second event after cooldown");
    check(!engine.step({false, true}, t + 250ms).fired, "release disarms instantly");
    engine.reset();
    check(engine.shotCount() == 0 && engine.state() == TriggerState::Idle, "reset counters");
    engine.setConfig({.cooldown = 1ms, .stableFrames = 100});
    check(engine.config().cooldown == 50ms && engine.config().stableFrames == 20,
          "engine config clamping");
}

void testSimulation() {
    using namespace triggerlab;
    Frame frame(320, 240);
    Simulation simulation(320, 240);
    simulation.render(frame);
    TargetDetector detector({.roiRadius = 7, .minimumCoverage = 0.3});
    bool targetColoredPixelsPresent = false;
    for (int y = 0; y < frame.height() && !targetColoredPixelsPresent; ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const auto& p = frame.at(x, y);
            if (p.red > 200 && p.blue > 200 && p.green < 100) {
                targetColoredPixelsPresent = true;
                break;
            }
        }
    }
    check(targetColoredPixelsPresent, "simulation renders detectable dummy");
    check(detector.detect(frame, frame.width() / 2, frame.height() / 2).found,
          "initial dummy centered under crosshair");
    simulation.advance(1.0);
    check(simulation.elapsedSeconds() <= 0.1 + 0.00001, "simulation delta clamped");
    simulation.onSimulatedShot();
    check(simulation.isHitFlashActive(), "simulated shot feedback begins");
    for (int i = 0; i < 10; ++i) simulation.advance(0.05);
    check(!simulation.isHitFlashActive(), "simulated shot feedback expires");
    Frame tiny(16, 16);
    simulation.render(tiny);
    check(true, "small scene renders without exceeding boundaries");
}

} // namespace

int main() {
    testFrame();
    testDetector();
    testDetectorThresholdsAndEdges();
    testTriggerEngine();
    testSimulation();
    std::cout << "PASS: " << checks << " checks\n";
    return EXIT_SUCCESS;
}
