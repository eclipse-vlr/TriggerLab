#include "trainer/TrainerWindow.hpp"
#include "triggerlab/Version.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace triggerlab::win32 {
const wchar_t *stateLabel(triggerlab::TriggerState state) noexcept {
    switch (state) {
    case triggerlab::TriggerState::Idle:
        return L"IDLE";
    case triggerlab::TriggerState::Armed:
        return L"ARMED";
    case triggerlab::TriggerState::Detected:
        return L"DETECTED";
    case triggerlab::TriggerState::Cooldown:
        return L"COOLDOWN";
    }
    return L"UNKNOWN";
}

void drawText(HDC dc, int x, int y, const std::wstring &text) {
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
}

std::wstring formatNumber(double value) {
    char buffer[64]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    const std::string text(buffer, result.ptr);
    return std::wstring(text.begin(), text.end());
}
const wchar_t *sceneLabel(triggerlab::SceneProfile profile) noexcept {
    switch (profile) {
    case triggerlab::SceneProfile::Crossing:
        return L"Crossing";
    case triggerlab::SceneProfile::Multiple:
        return L"Multiple targets";
    case triggerlab::SceneProfile::Precision:
        return L"Precision";
    }
    return L"Unknown";
}
TrainerWindow::TrainerWindow(const std::filesystem::path &dataDirectory)
    : frame_(kFrameWidth, kFrameHeight), simulation_(kFrameWidth, kFrameHeight),
      previousTick_(triggerlab::Clock::now()) {
    wchar_t localAppData[32768]{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, 32768);
    if (length > 0 && length < 32768)
        configPath_ = std::filesystem::path(localAppData) / L"TriggerLab" / L"settings.json";
    else {
        configPath_ = std::filesystem::path(L"settings.json");
    }
    if (!dataDirectory.empty())
        configPath_ = dataDirectory / L"settings.json";
    const auto loaded = triggerlab::loadSettings(configPath_);
    engine_.setConfig(loaded.settings.engine);
    detector_.setConfig(loaded.settings.detector);
    simulation_.setConfig(loaded.settings.scene);
    language_ = loaded.settings.language;
    configStatus_ = loaded.ok ? L"Settings loaded" : L"Cannot load settings; defaults applied";
    simulation_.render(frame_);
}

void TrainerWindow::attach(HWND window) noexcept {
    window_ = window;
}
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
        auto *app = static_cast<TrainerWindow *>(create->lpCreateParams);
        if (app == nullptr)
            return FALSE;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->attach(window);
        return TRUE;
    }
    auto *app = reinterpret_cast<TrainerWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, RGB(225, 230, 238));
        SetBkColor(dc, RGB(31, 38, 48));
        SetDCBrushColor(dc, RGB(31, 38, 48));
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_GETMINMAXINFO: {
        auto *limits = reinterpret_cast<MINMAXINFO *>(lparam);
        RECT minimum{0, 0, kFrameWidth + 280, 710};
        AdjustWindowRect(&minimum, WS_OVERLAPPEDWINDOW, FALSE);
        limits->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        return 0;
    }
    case WM_COMMAND:
        if (app)
            app->command(LOWORD(wparam), HIWORD(wparam));
        return 0;
    case WM_CREATE:
        if (app)
            app->createControls();
        if (SetTimer(window, kTimerId, kTimerIntervalMs, nullptr) == 0) {
            MessageBoxW(window, app->tr(L"Could not start UI timer.").c_str(), L"TriggerLab",
                        MB_OK | MB_ICONERROR);
            return -1;
        }
        return 0;
    case WM_TIMER:
        if (wparam == kTimerId && app)
            app->tick();
        return 0;
    case WM_MOUSEMOVE:
        if (app)
            app->mouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_LBUTTONDOWN:
        if (app)
            app->mouseDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_LBUTTONUP:
        if (app)
            app->mouseUp();
        return 0;
    case WM_RBUTTONDOWN:
        if (app)
            app->centerAim();
        return 0;
    case WM_MOUSELEAVE:
        if (app)
            app->mouseLeave();
        return 0;
    case WM_CAPTURECHANGED:
        if (app)
            app->mouseCaptureLost();
        return 0;
    case WM_CANCELMODE:
        if (app)
            app->onFocusLost();
        return 0;
    case WM_KEYDOWN:
        if (app)
            app->onKeyDown(wparam, lparam);
        return 0;
    case WM_KEYUP:
        if (app)
            app->onKeyUp(wparam);
        return 0;
    case WM_KILLFOCUS:
        if (app)
            app->onFocusLost();
        return 0;
    case WM_ERASEBKGND:
        return false; // full-frame repaint eliminates flicker
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        RECT rect{};
        GetClientRect(window, &rect);
        if (app)
            app->paint(dc, rect);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, kTimerId);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

TrainerWindow::~TrainerWindow() {
    if (window_ && IsWindow(window_))
        DestroyWindow(window_);
}
bool TrainerWindow::createWindow(HINSTANCE instance) {
    constexpr const wchar_t *className = L"TriggerLabSandboxClass";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProcedure;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = className;
    if (RegisterClassExW(&wc) == 0) {
        MessageBoxW(nullptr, tr(L"Could not register window class.").c_str(), L"TriggerLab",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    RECT rect{0, 0, kFrameWidth + 280, 710};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE);
    HWND window =
        CreateWindowExW(0, className, L"TriggerLab - Local Practice Sandbox",
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
    if (!window) {
        MessageBoxW(nullptr, tr(L"Could not create practice window.").c_str(), L"TriggerLab",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    return true;
}
void TrainerWindow::show(int command) {
    ShowWindow(window_, command);
    UpdateWindow(window_);
}
int TrainerWindow::messageLoop() {
    MSG message{};
    BOOL status = 0;
    while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (GetFocus() != window_ && IsDialogMessageW(window_, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return status == -1 ? 1 : static_cast<int>(message.wParam);
}
} // namespace triggerlab::win32
