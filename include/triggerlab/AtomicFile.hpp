#pragma once

#include <filesystem>
#include <string>

namespace triggerlab {
// Replace a complete file using a unique temporary file on the same filesystem.
// Concurrent writers produce complete files; the last successful replacement wins.
bool writeAtomicTextFile(const std::filesystem::path& path, const std::string& data, std::string& error);
}
