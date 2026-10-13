#include "triggerlab/TargetDetector.hpp"

#include <algorithm>

namespace triggerlab {

TargetDetector::TargetDetector(DetectorConfig config) { setConfig(config); }

void TargetDetector::setConfig(DetectorConfig config) noexcept {
    config.roiRadius = std::clamp(config.roiRadius, 0, 30);
    config.minimumCoverage = std::clamp(config.minimumCoverage, 0.01, 1.0);
    config_ = config;
}

Detection TargetDetector::detect(const Frame& frame, int centerX, int centerY) const noexcept {
    Detection result;
    if (!frame.contains(centerX, centerY)) return result;

    const int left = std::max(0, centerX - config_.roiRadius);
    const int right = std::min(frame.width() - 1, centerX + config_.roiRadius);
    const int top = std::max(0, centerY - config_.roiRadius);
    const int bottom = std::min(frame.height() - 1, centerY + config_.roiRadius);

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const Pixel& p = frame.at(x, y);
            ++result.samplePixels;
            if (p.red >= config_.minimumRed && p.blue >= config_.minimumBlue &&
                p.green <= config_.maximumGreen) {
                ++result.matchingPixels;
            }
        }
    }
    if (result.samplePixels > 0) {
        result.coverage = static_cast<double>(result.matchingPixels) / result.samplePixels;
    }
    result.found = result.coverage >= config_.minimumCoverage;
    return result;
}

} // namespace triggerlab
