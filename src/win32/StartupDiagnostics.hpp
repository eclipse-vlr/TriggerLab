#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <windows.h>
namespace triggerlab {
inline void reportStartupError(const wchar_t *phase, DWORD code = GetLastError()) noexcept {
    try {
        const std::wstring message =
            std::wstring(L"TriggerLab: ") + phase + L" (Windows error " + std::to_wstring(code) + L").";
        wchar_t local[32768]{};
        const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        if (size && size < 32768) {
            const auto directory = std::filesystem::path(local) / L"TriggerLab";
            std::error_code ignored;
            std::filesystem::create_directories(directory, ignored);
            std::wofstream log(directory / L"startup.log", std::ios::app);
            log << message << L'\n';
        }
        OutputDebugStringW(message.c_str());
        MessageBoxW(nullptr, message.c_str(), L"TriggerLab — ошибка запуска", MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(nullptr, L"Не удалось запустить TriggerLab.", L"TriggerLab", MB_OK | MB_ICONERROR);
    }
}
inline void reportStartupException(const char *text) noexcept {
    try {
        std::wstring detail;
        for (const unsigned char c : std::string(text))
            detail.push_back(c);
        reportStartupError(detail.c_str(), 0);
    } catch (...) {
        reportStartupError(L"Initialization", 0);
    }
}
} // namespace triggerlab
