#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
namespace triggerlab {
// Half-open physical pixel rectangle. 64-bit arithmetic handles negative origins safely.
struct Region {
    std::int64_t x = 0, y = 0, width = 0, height = 0;
};
inline bool validRegion(Region r) noexcept {
    constexpr auto limit = static_cast<std::int64_t>(2147483647);
    return r.width > 0 && r.height > 0 && r.width <= 4096 && r.height <= 4096 && r.x >= -limit &&
           r.y >= -limit && r.x <= limit - r.width && r.y <= limit - r.height;
}
inline std::optional<Region> intersectRegion(Region a, Region b) noexcept {
    // Bounds can exceed the capture-size limit; reject overflow before adding.
    auto safe = [](Region r) {
        constexpr auto limit = static_cast<std::int64_t>(2147483647);
        return r.width > 0 && r.height > 0 && r.x >= -limit && r.y >= -limit && r.x <= limit - r.width &&
               r.y <= limit - r.height;
    };
    if (!safe(a) || !safe(b))
        return {};
    const auto x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    const auto right = std::min(a.x + a.width, b.x + b.width),
               bottom = std::min(a.y + a.height, b.y + b.height);
    if (right <= x || bottom <= y)
        return {};
    return Region{x, y, right - x, bottom - y};
}
inline std::optional<Region> cursorRegion(std::int64_t x, std::int64_t y, int side, Region bounds) noexcept {
    if (!intersectRegion(bounds, bounds) || side < 1 || side > 4095 || side % 2 == 0 || x < bounds.x ||
        y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height)
        return {};
    Region requested{x - side / 2, y - side / 2, side, side};
    if (!validRegion(requested))
        return {};
    return intersectRegion(requested, bounds);
}
} // namespace triggerlab
