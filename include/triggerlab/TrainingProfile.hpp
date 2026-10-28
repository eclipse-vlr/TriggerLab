#pragma once
#include "triggerlab/AnalyzerSettings.hpp"
#include <optional>
#include <vector>
namespace triggerlab {
struct TrainingProfile {
    std::string name;
    AnalyzerSettings settings;
    ColorMatchConfig color;
};
struct NamedProfile {
    std::string name;
    std::filesystem::path path;
};
std::optional<TrainingProfile> loadTrainingProfile(const std::filesystem::path &);
bool saveTrainingProfile(const std::filesystem::path &directory, const TrainingProfile &, std::string &error);
std::vector<NamedProfile> listTrainingProfiles(const std::filesystem::path &directory);
} // namespace triggerlab
