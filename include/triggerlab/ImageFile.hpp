#pragma once
#include "triggerlab/ImageColor.hpp"
#include <filesystem>
#include <string>

namespace triggerlab {
struct ImageFileResult {
    std::optional<ImageColor> selected;
    std::wstring error;
};
// Windows WIC decoder. First frame; max 64 MiB encoded, 4096x4096 decoded.
ImageFileResult loadImageColor(const std::filesystem::path& path);
}
