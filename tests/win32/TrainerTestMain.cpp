#include "trainer/TrainerWindow.hpp"
#include "triggerlab/Version.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace triggerlab::win32 {
namespace {
DWORD startupError = 0;
std::wstring startupPhase;
HWND WINAPI rejectTrainerWindow(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                LPVOID) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
}
void recordStartupError(const wchar_t *phase, DWORD code) {
    startupPhase = phase;
    startupError = code;
}
} // namespace

struct TrainerTestHarness : TrainerWindow {
    using TrainerWindow::TrainerWindow;
    bool startupFailureTest(HINSTANCE instance) {
        TrainerTestHarness failed(configPath_.parent_path());
        return !failed.TrainerWindow::createWindow(instance, rejectTrainerWindow, recordStartupError) &&
               !failed.window_ && startupError == ERROR_NOT_ENOUGH_MEMORY &&
               startupPhase == L"Create trainer window";
    }
    bool layoutTest() {
        RECT original{};
        GetWindowRect(window_, &original);
        const UINT actualDpi = dpi_;
        for (UINT testDpi : {96u, 120u, 144u, 192u}) {
            setDpi(testDpi);
            SetWindowPos(window_, nullptr, 0, 0, 1000, 680, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            arrange();
            for (const auto &entry : controlRects_) {
                reveal(entry.control);
                RECT rect{}, client{};
                GetWindowRect(entry.control, &rect);
                MapWindowPoints(nullptr, window_, reinterpret_cast<POINT *>(&rect), 2);
                GetClientRect(window_, &client);
                if (rect.left < 0 || rect.top < 0 || rect.right > client.right || rect.bottom > client.bottom)
                    return false;
            }
            scroll(SB_HORZ, SB_TOP);
            scroll(SB_VERT, SB_TOP);
            mouseMove(scaled(480), scaled(270));
            if (aim_.x() != 480 || aim_.y() != 270)
                return false;
            scroll(SB_HORZ, SB_BOTTOM);
            scroll(SB_VERT, SB_BOTTOM);
            const int x = scaled(480) - scrollX_, y = scaled(270) - scrollY_;
            mouseMove(x, y);
            if (aim_.x() != 480 || aim_.y() != 270)
                return false;
            LOGFONTW font{};
            if (!GetObjectW(controlFont_, sizeof(font), &font) || font.lfHeight != -scaled(16))
                return false;
        }
        setDpi(actualDpi);
        SetWindowPos(window_, nullptr, 0, 0, original.right - original.left, original.bottom - original.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        scroll(SB_HORZ, SB_TOP);
        scroll(SB_VERT, SB_TOP);
        return true;
    }
    bool smokeTest(const std::filesystem::path &path) {
        configPath_ = path;
        engine_.setConfig({});
        detector_.setConfig({});
        simulation_.setConfig({});
        syncControls();
        for (auto edit : edits_)
            if (!IsWindow(edit))
                return false;
        onKeyDown('R', 0);
        tick();
        if (engine_.shotCount() != 0)
            return false;
        onKeyDown(VK_SPACE, 0);
        tick();
        tick();
        onKeyUp(VK_SPACE);
        if (engine_.shotCount() != 1 || !lastDetection_.found)
            return false;
        tick();
        if (lastResult_.state != triggerlab::TriggerState::Idle)
            return false;
        SetWindowTextW(edits_[0], L"725");
        command(201);
        const auto loaded = triggerlab::loadSettings(path);
        if (!loaded.ok || loaded.settings.engine.cooldown.count() != 725)
            return false;
        SetWindowTextW(edits_[0], L"invalid");
        command(201);
        if (engine_.config().cooldown.count() != 725)
            return false;
        if (diagnostics_.sampleCount() != 1)
            return false;
        command(205);
        if (!paused_ || holdingSpace_)
            return false;
        command(205);
        SendMessageW(sceneControl_, CB_SETCURSEL, 1, 0);
        SetWindowTextW(speedControl_, L"1.5");
        command(204);
        if (simulation_.config().profile != triggerlab::SceneProfile::Multiple ||
            simulation_.config().speed != 1.5)
            return false;
        const auto restored = triggerlab::loadSettings(path);
        if (!restored.ok || restored.settings.scene.speed != 1.5)
            return false;
        command(206);
        if (!std::filesystem::exists(path.parent_path() / L"events.csv"))
            return false;
        command(202);
        simulation_.setConfig({});
        mouseDown(480, 270);
        tick();
        tick();
        if (engine_.shotCount() != 1 || !active())
            return false;
        mouseMove(975, 270);
        tick();
        if (active() || lastResult_.state != triggerlab::TriggerState::Idle)
            return false;
        mouseUp();
        mouseMove(10, 10);
        tick();
        if (lastDetection_.found)
            return false;
        onFocusLost();
        if (active())
            return false;
        command(202);
        const auto shots = engine_.shotCount();
        SetWindowTextW(edits_[0], L"900");
        for (int language : {0, 1}) {
            SendMessageW(languageControl_, CB_SETCURSEL, language, 0);
            command(208, CBN_SELCHANGE);
            const auto restoredLanguage = triggerlab::loadSettings(path);
            if (!restoredLanguage.ok || static_cast<int>(restoredLanguage.settings.language) != language ||
                engine_.shotCount() != shots)
                return false;
            wchar_t text[128]{};
            GetWindowTextW(GetDlgItem(window_, 201), text, 128);
            if (std::wstring(text) != tr(L"Apply and save"))
                return false;
            GetWindowTextW(edits_[0], text, 128);
            if (std::wstring(text) != L"900")
                return false;
        }
        return engine_.shotCount() == 0 && diagnostics_.sampleCount() == 0;
    }
    bool paintResourcesStable() {
        HDC dc = GetDC(window_);
        if (!dc)
            return false;
        RECT client{};
        GetClientRect(window_, &client);
        paint(dc, client);
        const DWORD initial = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 100; ++i) {
            RECT size{0, 0, client.right + (i % 2) * 16, client.bottom};
            paint(dc, size);
        }
        paint(dc, client);
        const bool stable = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == initial;
        ReleaseDC(window_, dc);
        return stable;
    }
};
} // namespace triggerlab::win32

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct TestDirectory {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     (L"triggerlab-trainer-test-" + std::to_wstring(GetCurrentProcessId()));
        ~TestDirectory() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } data;
    try {
        triggerlab::win32::TrainerTestHarness app(data.path);
        if (!app.createWindow(instance))
            return 1;
        return app.startupFailureTest(instance) && app.layoutTest() &&
                       app.smokeTest(data.path / L"settings.json") && app.paintResourcesStable()
                   ? 0
                   : 2;
    } catch (const std::exception &) {
        return 1;
    }
}
