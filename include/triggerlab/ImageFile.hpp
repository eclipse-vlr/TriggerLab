#pragma once
#include "triggerlab/ImageColor.hpp"
#include <filesystem>
#include <string>

namespace triggerlab {
struct ImageFileResult {
    std::optional<ImageColor> selected;
    std::wstring error;
};
struct ImagePixelsResult {std::optional<Frame> image;std::wstring error;};
ImagePixelsResult loadImagePixels(const std::filesystem::path& path);
// Windows WIC decoder. First frame; max 64 MiB encoded, 4096x4096 decoded.
ImageFileResult loadImageColor(const std::filesystem::path& path);
}
