#pragma once
#include "triggerlab/Frame.hpp"
#include <optional>

namespace triggerlab {
struct ImageColor {
    Pixel color;
    double coverage = 0; // Fraction of opaque pixels in the dominant RGB bucket.
};
// Group nearby RGB values (8 levels/channel), ignoring alpha below 128.
// Return an actual image pixel nearest the winning group's mean, never a blend
// between unrelated colors. Ties are deterministic.
std::optional<ImageColor> dominantImageColor(const Frame& image);
}
