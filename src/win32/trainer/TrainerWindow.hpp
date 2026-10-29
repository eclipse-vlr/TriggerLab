#pragma once
#include "BackBuffer.hpp"
#include "triggerlab/AimInput.hpp"
#include "triggerlab/Config.hpp"
#include "triggerlab/Diagnostics.hpp"
#include "triggerlab/Simulation.hpp"
#include "triggerlab/TargetDetector.hpp"
#include "triggerlab/TriggerEngine.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include <windowsx.h>

namespace triggerlab::win32 {
inline constexpr int kFrameWidth = 960;
inline constexpr int kFrameHeight = 540;
inline constexpr int kTrainerWidth = kFrameWidth + 280;
inline constexpr int kTrainerHeight = 710;
inline constexpr UINT_PTR kTimerId = 1;
inline constexpr UINT kTimerIntervalMs = 16;
const wchar_t *stateLabel(triggerlab::TriggerState state) noexcept;
const wchar_t *sceneLabel(triggerlab::SceneProfile profile) noexcept;
void drawText(HDC dc, int x, int y, const std::wstring &text);
std::wstring formatNumber(double value);

class TrainerWindow {
  public:
    ~TrainerWindow();
    TrainerWindow(const TrainerWindow &) = delete;
    TrainerWindow &operator=(const TrainerWindow &) = delete;
    bool createWindow(HINSTANCE instance);
    void show(int command);
    int messageLoop();
    explicit TrainerWindow(const std::filesystem::path &dataDirectory = {});

    void attach(HWND window) noexcept;
    void createControls();
    std::wstring tr(std::wstring_view key) const;
    void updateLanguage();
    void syncControls();
    void save();
    void command(int id, int notification = BN_CLICKED);
    void resetSession();
    void togglePause();
    void exportLog();

    void onFocusLost() noexcept;
    void mouseMove(int x, int y) noexcept;
    void mouseDown(int x, int y) noexcept;
    void mouseUp() noexcept;
    void mouseLeave() noexcept;
    void mouseCaptureLost() noexcept;
    void centerAim() noexcept;
    bool active() const noexcept;

    void onKeyDown(WPARAM key, LPARAM lparam);

    void onKeyUp(WPARAM key) noexcept;

    void tick();

    void paint(HDC screen, const RECT &client);

  private:
    friend struct TrainerTestHarness;
    friend LRESULT CALLBACK windowProcedure(HWND, UINT, WPARAM, LPARAM);
    using WindowCreator = decltype(&CreateWindowExW);
    using ErrorReporter = void (*)(const wchar_t *, DWORD);
    bool createWindow(HINSTANCE instance, WindowCreator creator, ErrorReporter report);
    void setDpi(UINT value);
    int scaled(int value) const;
    void arrange();
    void scroll(int bar, int request);
    void reveal(HWND control);
    struct ControlRect {
        HWND control;
        RECT logical;
    };
    std::vector<ControlRect> controlRects_;
    UINT dpi_ = 96;
    int scrollX_ = 0, scrollY_ = 0, wheelRemainder_ = 0;
    bool arranging_ = false;
    HFONT controlFont_ = nullptr, paintFont_ = nullptr;
    BackBuffer backBuffer_;
    triggerlab::AimInput aim_;
    std::filesystem::path configPath_;
    std::wstring configStatus_;
    triggerlab::Language language_ = triggerlab::Language::Russian;
    std::array<HWND, 7> labelControls_{};
    HWND languageLabel_ = nullptr, languageControl_ = nullptr;
    std::array<HWND, 7> edits_{};
    HWND sceneControl_ = nullptr, speedControl_ = nullptr, pauseControl_ = nullptr;
    triggerlab::Diagnostics diagnostics_;
    bool paused_ = false;
    HWND window_ = nullptr;
    triggerlab::Frame frame_;
    triggerlab::Simulation simulation_;
    triggerlab::TargetDetector detector_;
    triggerlab::TriggerEngine engine_;
    triggerlab::Clock::time_point previousTick_;
    triggerlab::Detection lastDetection_{};
    triggerlab::TriggerOutput lastResult_{};
    bool holdingSpace_ = false;
    double fps_ = 0.0;
};
} // namespace triggerlab::win32
