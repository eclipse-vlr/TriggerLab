#pragma once
#include "triggerlab/AtomicFile.hpp"
#include "triggerlab/TargetDetector.hpp"
#include "triggerlab/Simulation.hpp"
#include "triggerlab/TriggerEngine.hpp"
#include <filesystem>
#include "triggerlab/Localization.hpp"
#include <string>
#include <string_view>
namespace triggerlab {
struct Settings { EngineConfig engine; DetectorConfig detector; SceneConfig scene; Language language=Language::Russian; };
struct ConfigResult { Settings settings{}; bool ok = false; std::string message; };
bool validSettings(const Settings& settings) noexcept;
std::string serializeSettings(const Settings& settings);
ConfigResult deserializeSettings(std::string_view json);
ConfigResult loadSettings(const std::filesystem::path& path);
bool saveSettings(const std::filesystem::path& path, const Settings& settings, std::string& error);
}
