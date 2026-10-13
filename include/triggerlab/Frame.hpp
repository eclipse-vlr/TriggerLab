#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace triggerlab {

// BGRA layout is compatible with an uncompressed 32-bit Win32 DIB.
struct Pixel {
    std::uint8_t blue = 0;
    std::uint8_t green = 0;
    std::uint8_t red = 0;
    std::uint8_t alpha = 255;
};
static_assert(sizeof(Pixel) == 4, "Pixel must use exactly four bytes");

class Frame {
public:
    Frame(int width, int height)
        : width_(width), height_(height) {
        if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
            throw std::invalid_argument("Frame dimensions must be in [1,4096]");
        }
        pixels_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
    }

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] const Pixel* data() const noexcept { return pixels_.data(); }
    [[nodiscard]] Pixel* data() noexcept { return pixels_.data(); }

    [[nodiscard]] bool contains(int x, int y) const noexcept {
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    [[nodiscard]] const Pixel& at(int x, int y) const {
        if (!contains(x, y)) throw std::out_of_range("Frame pixel coordinate");
        return pixels_[index(x, y)];
    }

    [[nodiscard]] Pixel& at(int x, int y) {
        if (!contains(x, y)) throw std::out_of_range("Frame pixel coordinate");
        return pixels_[index(x, y)];
    }

    void clear(Pixel pixel) noexcept {
        std::fill(pixels_.begin(), pixels_.end(), pixel);
    }

    void setIfInside(int x, int y, Pixel pixel) noexcept {
        if (contains(x, y)) pixels_[index(x, y)] = pixel;
    }

private:
    [[nodiscard]] std::size_t index(int x, int y) const noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
               static_cast<std::size_t>(x);
    }

    int width_;
    int height_;
    std::vector<Pixel> pixels_;
};

} // namespace triggerlab
