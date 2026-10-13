#pragma once

#include "triggerlab/Frame.hpp"

#include <cstdint>

namespace triggerlab {

struct DetectorConfig {
    int roiRadius = 5;
    double minimumCoverage = 0.45;
    std::uint8_t minimumRed = 160;
    std::uint8_t minimumBlue = 150;
    std::uint8_t maximumGreen = 130;
};

struct Detection {
    bool found = false;
    double coverage = 0.0;
    int matchingPixels = 0;
    int samplePixels = 0;
};

class TargetDetector {
public:
    explicit TargetDetector(DetectorConfig config = {});
    void setConfig(DetectorConfig config) noexcept;
    [[nodiscard]] const DetectorConfig& config() const noexcept { return config_; }
    [[nodiscard]] Detection detect(const Frame& frame, int centerX, int centerY) const noexcept;

private:
    DetectorConfig config_;
};

} // namespace triggerlab
