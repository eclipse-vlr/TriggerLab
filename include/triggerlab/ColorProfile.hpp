#pragma once
#include "triggerlab/ColorMask.hpp"
#include <filesystem>
#include <string>
#include <string_view>
namespace triggerlab {
struct ColorProfileResult {
    ColorMatchConfig color{};
    bool ok = false;
    std::string error;
};
// Write schema 2 with complete RGB/HSV settings; read legacy RGB schema 1 too.
std::string serializeColorProfile(const ColorMatchConfig &color);
ColorProfileResult deserializeColorProfile(std::string_view json);
ColorProfileResult loadColorProfile(const std::filesystem::path &path);
bool saveColorProfile(const std::filesystem::path &path, const ColorMatchConfig &color, std::string &error);
} // namespace triggerlab
