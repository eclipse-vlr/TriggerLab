#include "trainer/TrainerWindow.hpp"
#include "StartupDiagnostics.hpp"
#include <algorithm>
#include <charconv>

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
        limits->ptMinTrackSize = {std::max(480, app ? app->scaled(300) : 480), 320};
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
    case WM_SIZE:
        if (app)
            app->arrange();
        return 0;
    case WM_DPICHANGED:
        if (app) {
            app->setDpi(HIWORD(wparam));
            const auto *rect = reinterpret_cast<const RECT *>(lparam);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                         rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            app->arrange();
        }
        return 0;
    case WM_HSCROLL:
    case WM_VSCROLL:
        if (app)
            app->scroll(message == WM_HSCROLL ? SB_HORZ : SB_VERT, LOWORD(wparam));
        return 0;
    case WM_MOUSEWHEEL:
        if (app) {
            app->wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wparam);
            const int steps = app->wheelRemainder_ / WHEEL_DELTA;
            app->wheelRemainder_ %= WHEEL_DELTA;
            if (wparam & MK_SHIFT)
                app->scrollX_ -= steps * app->scaled(90);
            else
                app->scrollY_ -= steps * app->scaled(90);
            app->onFocusLost();
            app->arrange();
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
    case WM_NCDESTROY:
        if (app)
            app->attach(nullptr);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

TrainerWindow::~TrainerWindow() {
    if (window_ && IsWindow(window_))
        DestroyWindow(window_);
    if (controlFont_)
        DeleteObject(controlFont_);
    if (paintFont_)
        DeleteObject(paintFont_);
}
bool TrainerWindow::createWindow(HINSTANCE instance) {
    return createWindow(instance, CreateWindowExW, triggerlab::reportStartupError);
}
bool TrainerWindow::createWindow(HINSTANCE instance, WindowCreator creator, ErrorReporter report) {
    constexpr const wchar_t *className = L"TriggerLabSandboxClass";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProcedure;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = className;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        report(L"Register trainer window class", GetLastError());
        return false;
    }

    RECT rect{0, 0, kTrainerWidth, kTrainerHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE);
    HWND window = creator(0, className, L"TriggerLab - Local Practice Sandbox",
                          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                          rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
    if (!window) {
        report(L"Create trainer window", GetLastError());
        return false;
    }

    return true;
}
void TrainerWindow::show(int command) {
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const auto work = monitor.rcWork;
        const int width = std::min(scaled(kTrainerWidth + 20), static_cast<int>(work.right - work.left));
        const int height = std::min(scaled(kTrainerHeight + 40), static_cast<int>(work.bottom - work.top));
        SetWindowPos(window_, nullptr, work.left + (work.right - work.left - width) / 2,
                     work.top + (work.bottom - work.top - height) / 2, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(window_, command);
    UpdateWindow(window_);
}
int TrainerWindow::messageLoop() {
    MSG message{};
    BOOL status = 0;
    while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (GetFocus() != window_ && IsDialogMessageW(window_, &message)) {
            reveal(GetFocus());
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return status == -1 ? 1 : static_cast<int>(message.wParam);
}
} // namespace triggerlab::win32
