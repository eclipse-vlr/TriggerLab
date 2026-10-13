#include <windows.h>

#include "triggerlab/Frame.hpp"
#include "triggerlab/Config.hpp"
#include <array>
#include <filesystem>
#include <cwchar>
#include <sstream>
#include <iomanip>
#include "triggerlab/Simulation.hpp"
#include "triggerlab/TargetDetector.hpp"
#include "triggerlab/TriggerEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <string>

namespace {

constexpr int kFrameWidth = 960;
constexpr int kFrameHeight = 540;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerIntervalMs = 16;

const wchar_t* stateLabel(triggerlab::TriggerState state) noexcept {
    switch (state) {
        case triggerlab::TriggerState::Idle: return L"IDLE";
        case triggerlab::TriggerState::Armed: return L"ARMED";
        case triggerlab::TriggerState::Detected: return L"DETECTED";
        case triggerlab::TriggerState::Cooldown: return L"COOLDOWN";
    }
    return L"UNKNOWN";
}

void drawText(HDC dc, int x, int y, const std::wstring& text) {
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
}

class App {
public:
    App()
        : frame_(kFrameWidth, kFrameHeight), simulation_(kFrameWidth, kFrameHeight),
          previousTick_(triggerlab::Clock::now()) {
        wchar_t localAppData[32768]{};
        const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, 32768);
        if (length > 0 && length < 32768) configPath_ = std::filesystem::path(localAppData) / L"TriggerLab" / L"settings.json";
        else { configPath_ = std::filesystem::path(L"settings.json"); }
        const auto loaded = triggerlab::loadSettings(configPath_);
        engine_.setConfig(loaded.settings.engine); detector_.setConfig(loaded.settings.detector);
        configStatus_ = std::wstring(loaded.message.begin(), loaded.message.end());
        simulation_.render(frame_);
    }

    void attach(HWND window) noexcept { window_ = window; }
    void createControls() {
        const wchar_t* labels[] = {L"Cooldown ms [50..2000]", L"ROI radius [0..30]", L"Stable frames [1..20]",
            L"Coverage [0.01..1]", L"Minimum red [0..255]", L"Minimum blue [0..255]", L"Maximum green [0..255]"};
        for (int i=0; i<7; ++i) {
            CreateWindowW(L"STATIC", labels[i], WS_CHILD|WS_VISIBLE, 976, 55+i*48, 250, 20, window_, nullptr, nullptr, nullptr);
            edits_[i]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
                976,75+i*48,240,24,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),nullptr,nullptr);
        }
        CreateWindowW(L"BUTTON",L"Apply and save",WS_CHILD|WS_VISIBLE|WS_TABSTOP,976,410,240,30,window_,reinterpret_cast<HMENU>(201),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Reset statistics / scene",WS_CHILD|WS_VISIBLE|WS_TABSTOP,976,450,240,30,window_,reinterpret_cast<HMENU>(202),nullptr,nullptr);
        syncControls();
    }
    void syncControls() {
        const auto e=engine_.config(); const auto d=detector_.config();
        std::wostringstream coverage; coverage.imbue(std::locale::classic()); coverage << std::setprecision(17) << d.minimumCoverage;
        const std::array<std::wstring,7> values{std::to_wstring(e.cooldown.count()),std::to_wstring(d.roiRadius),
            std::to_wstring(e.stableFrames),coverage.str(),std::to_wstring(d.minimumRed),
            std::to_wstring(d.minimumBlue),std::to_wstring(d.maximumGreen)};
        for (int i=0;i<7;++i) if (edits_[i]) SetWindowTextW(edits_[i],values[i].c_str());
    }
    void save() {
        std::string error;
        if (triggerlab::saveSettings(configPath_,{engine_.config(),detector_.config()},error)) configStatus_=L"Settings saved";
        else configStatus_=L"Save failed: " + std::wstring(error.begin(),error.end());
    }
    void command(int id) {
        if (id!=201 && id!=202) return;
        if (id==202) onKeyDown('R',0);
        if (id==201) {
            const char* keys[]={"cooldown_ms","roi_radius","stable_frames","minimum_coverage","minimum_red","minimum_blue","maximum_green"};
            std::string json="{";
            for(int i=0;i<7;++i) {
                wchar_t value[128]{}; GetWindowTextW(edits_[i],value,128);
                if(i) json+=",";
                json += "\"" + std::string(keys[i]) + "\":";
                for (wchar_t c : std::wstring(value)) json += c<128 ? static_cast<char>(c) : '?';
            }
            json+="}";
            const auto parsed=triggerlab::deserializeSettings(json);
            if (parsed.ok) { engine_.setConfig(parsed.settings.engine); detector_.setConfig(parsed.settings.detector); save(); syncControls(); }
            else configStatus_=L"Invalid values: settings were not changed";
        }
        SetFocus(window_); InvalidateRect(window_,nullptr,FALSE);
    }
    bool smokeTest(const std::filesystem::path& path) {
        configPath_=path;
        engine_.setConfig({}); detector_.setConfig({}); syncControls();
        for(auto edit:edits_) if(!IsWindow(edit)) return false;
        onKeyDown('R',0); tick(); if(engine_.shotCount()!=0) return false;
        onKeyDown(VK_SPACE,0); tick(); tick(); onKeyUp(VK_SPACE);
        if(engine_.shotCount()!=1 || !lastDetection_.found) return false;
        tick(); if(lastResult_.state!=triggerlab::TriggerState::Idle) return false;
        SetWindowTextW(edits_[0],L"725"); command(201);
        const auto loaded=triggerlab::loadSettings(path);
        if(!loaded.ok || loaded.settings.engine.cooldown.count()!=725) return false;
        SetWindowTextW(edits_[0],L"invalid"); command(201);
        if(engine_.config().cooldown.count()!=725) return false;
        command(202); return engine_.shotCount()==0;
    }
    void onFocusLost() noexcept { holdingSpace_ = false; }

    void onKeyDown(WPARAM key, LPARAM lparam) {
        if (key == VK_SPACE) {
            holdingSpace_ = true;
            return;
        }
        if (key == VK_ESCAPE) {
            DestroyWindow(window_);
            return;
        }
        if ((lparam & (1LL << 30)) != 0) return; // avoid key-repeat toggles
        if (key == 'R') {
            engine_.reset();
            simulation_ = triggerlab::Simulation{kFrameWidth, kFrameHeight};
            simulation_.render(frame_);
            lastDetection_ = {};
            lastResult_ = {};
        } else if (key == VK_UP || key == VK_DOWN) {
            auto config = engine_.config();
            const int change = (key == VK_UP) ? 25 : -25;
            config.cooldown += std::chrono::milliseconds{change};
            engine_.setConfig(config);
        } else if (key == VK_RIGHT || key == VK_LEFT) {
            auto config = detector_.config();
            config.roiRadius += (key == VK_RIGHT) ? 1 : -1;
            detector_.setConfig(config);
        }
        if (key==VK_UP || key==VK_DOWN || key==VK_LEFT || key==VK_RIGHT) { syncControls(); save(); }
        InvalidateRect(window_, nullptr, FALSE);
    }

    void onKeyUp(WPARAM key) noexcept {
        if (key == VK_SPACE) holdingSpace_ = false;
    }

    void tick() {
        const auto now = triggerlab::Clock::now();
        const double dt = std::chrono::duration<double>(now - previousTick_).count();
        previousTick_ = now;
        if (dt > 0.0) {
            const double instantaneousFps = 1.0 / dt;
            fps_ = fps_ == 0.0 ? instantaneousFps : fps_ * 0.9 + instantaneousFps * 0.1;
        }

        simulation_.advance(dt);
        simulation_.render(frame_);
        lastDetection_ = detector_.detect(frame_, frame_.width() / 2, frame_.height() / 2);
        lastResult_ = engine_.step({holdingSpace_, lastDetection_.found}, now);
        if (lastResult_.fired) simulation_.onSimulatedShot();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void paint(HDC dc, const RECT& client) {
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = frame_.width();
        bitmap.bmiHeader.biHeight = -frame_.height(); // top-down pixel buffer
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;

        FillRect(dc,&client,GetSysColorBrush(COLOR_BTNFACE));
        RECT panel{960,0,client.right,client.bottom};
        FillRect(dc,&panel,GetSysColorBrush(COLOR_BTNFACE));
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(20,20,20));
        drawText(dc,976,18,L"SETTINGS - v0.2");
        const int displayWidth = std::min(960L, client.right - client.left);
        const int displayHeight = std::min(540L, client.bottom - client.top);
        if (displayWidth <= 0 || displayHeight <= 0) return;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, 0, 0, displayWidth, displayHeight,
                      0, 0, frame_.width(), frame_.height(), frame_.data(), &bitmap,
                      DIB_RGB_COLORS, SRCCOPY);

        const int crossX = displayWidth / 2;
        const int crossY = displayHeight / 2;
        HGDIOBJ previousPen = SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, lastDetection_.found ? RGB(255, 225, 70) : RGB(226, 241, 247));
        const int innerGap = std::max(3, displayWidth / 280);
        const int outerGap = std::max(15, displayWidth / 62);
        MoveToEx(dc, crossX - outerGap, crossY, nullptr);
        LineTo(dc, crossX - innerGap, crossY);
        MoveToEx(dc, crossX + innerGap, crossY, nullptr);
        LineTo(dc, crossX + outerGap, crossY);
        MoveToEx(dc, crossX, crossY - outerGap, nullptr);
        LineTo(dc, crossX, crossY - innerGap);
        MoveToEx(dc, crossX, crossY + innerGap, nullptr);
        LineTo(dc, crossX, crossY + outerGap);
        SelectObject(dc, previousPen);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(246, 246, 252));
        const int fontHeight = 18;
        const int textX = 18;
        drawText(dc, textX, 18, L"TRIGGERLAB v0.2  |  LOCAL PRACTICE SANDBOX");
        drawText(dc, textX, 18 + fontHeight, L"HOLD SPACE: arm  |  R: reset  |  ESC: exit");
        drawText(dc, textX, 18 + fontHeight * 2,
                 L"UP/DOWN: cooldown  |  LEFT/RIGHT: detection ROI");

        const int textY = 18 + fontHeight * 4;
        SetTextColor(dc, holdingSpace_ ? RGB(130, 255, 176) : RGB(240, 190, 156));
        drawText(dc, textX, textY,
                 L"Engine: " + std::wstring(stateLabel(lastResult_.state)) +
                 (holdingSpace_ ? L" (holding)" : L" (inactive)"));
        SetTextColor(dc, RGB(235, 240, 245));
        const int coveragePercent = static_cast<int>(std::lround(lastDetection_.coverage * 100.0));
        drawText(dc, textX, textY + fontHeight,
                 L"Target: " + std::wstring(lastDetection_.found ? L"YES" : L"NO") +
                 L"  |  Coverage: " + std::to_wstring(coveragePercent) + L"%");
        drawText(dc, textX, textY + fontHeight * 2,
                 L"Simulated shots: " + std::to_wstring(lastResult_.shotCount) +
                 L"  |  Cooldown: " + std::to_wstring(engine_.config().cooldown.count()) + L"ms");
        drawText(dc, textX, textY + fontHeight * 3,
                 L"ROI radius: " + std::to_wstring(detector_.config().roiRadius) +
                 L"px  |  FPS: " + std::to_wstring(static_cast<int>(std::lround(fps_))));

        drawText(dc,textX,textY + fontHeight*4,L"Stable frames: " + std::to_wstring(engine_.config().stableFrames));
        drawText(dc,textX,displayHeight-52,configStatus_);
        if (simulation_.isHitFlashActive()) {
            SetTextColor(dc, RGB(123, 255, 170));
            drawText(dc, crossX + 22, crossY - 55, L"SIMULATED HIT");
        }
        SetTextColor(dc, RGB(180, 192, 202));
        drawText(dc, textX, displayHeight - 28, L"No real mouse input | No game access | No anti-cheat interaction");
    }

private:
    std::filesystem::path configPath_;
    std::wstring configStatus_;
    std::array<HWND,7> edits_{};
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

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* app = static_cast<App*>(create->lpCreateParams);
        if (app == nullptr) return FALSE;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->attach(window);
        return TRUE;
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            RECT minimum{0,0,kFrameWidth+280,kFrameHeight};
            AdjustWindowRect(&minimum,WS_OVERLAPPEDWINDOW,FALSE);
            limits->ptMinTrackSize={minimum.right-minimum.left,minimum.bottom-minimum.top};
            return 0;
        }
        case WM_COMMAND:
            if (app) app->command(LOWORD(wparam));
            return 0;
        case WM_CREATE:
            if (app) app->createControls();
            if (SetTimer(window, kTimerId, kTimerIntervalMs, nullptr) == 0) {
                MessageBoxW(window, L"Could not start UI timer.", L"TriggerLab", MB_OK | MB_ICONERROR);
                return -1;
            }
            return 0;
        case WM_TIMER:
            if (wparam == kTimerId && app) app->tick();
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(window);
            return 0;
        case WM_KEYDOWN:
            if (app) app->onKeyDown(wparam, lparam);
            return 0;
        case WM_KEYUP:
            if (app) app->onKeyUp(wparam);
            return 0;
        case WM_KILLFOCUS:
            if (app) app->onFocusLost();
            return 0;
        case WM_ERASEBKGND:
            return 1; // full-frame repaint eliminates flicker
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(window, &ps);
            RECT rect{};
            GetClientRect(window, &rect);
            if (app) app->paint(dc, rect);
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

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    try {
        App app;
        constexpr const wchar_t* className = L"TriggerLabSandboxClass";
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = windowProcedure;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.lpszClassName = className;
        if (RegisterClassExW(&wc) == 0) {
            MessageBoxW(nullptr, L"Could not register window class.", L"TriggerLab", MB_OK | MB_ICONERROR);
            return 1;
        }

        RECT rect{0, 0, kFrameWidth+280, kFrameHeight};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        HWND window = CreateWindowExW(0, className, L"TriggerLab - Local Practice Sandbox",
                                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                      rect.right - rect.left, rect.bottom - rect.top,
                                      nullptr, nullptr, instance, &app);
        if (!window) {
            MessageBoxW(nullptr, L"Could not create practice window.", L"TriggerLab", MB_OK | MB_ICONERROR);
            return 1;
        }
        if (std::wstring(commandLine)==L"--smoke-test") {
            const auto directory=std::filesystem::temp_directory_path()/ (L"triggerlab-gui-test-"+std::to_wstring(GetCurrentProcessId()));
            const bool passed=app.smokeTest(directory/L"settings.json");
            HDC dc=GetDC(window); RECT client{}; GetClientRect(window,&client); app.paint(dc,client); ReleaseDC(window,dc);
            DestroyWindow(window); std::filesystem::remove_all(directory);
            return passed?0:2;
        }
        ShowWindow(window, showCommand);
        UpdateWindow(window);

        MSG message{};
        BOOL status = 0;
        while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (GetFocus()!=window && IsDialogMessageW(window,&message)) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return status == -1 ? 1 : static_cast<int>(message.wParam);
    } catch (const std::exception&) {
        MessageBoxW(nullptr, L"Unexpected initialization error.", L"TriggerLab", MB_OK | MB_ICONERROR);
        return 1;
    }
}
