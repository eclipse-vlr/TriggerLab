#include <windows.h>
#include <windowsx.h>
#include "triggerlab/AimInput.hpp"

#include "triggerlab/Frame.hpp"
#include "triggerlab/Config.hpp"
#include <array>
#include <filesystem>
#include <cwchar>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <charconv>
#include "triggerlab/Diagnostics.hpp"
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

std::wstring formatNumber(double value) {
    char buffer[64]{}; const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value);
    const std::string text(buffer,result.ptr); return std::wstring(text.begin(),text.end());
}
const wchar_t* sceneLabel(triggerlab::SceneProfile profile) noexcept {
    switch(profile) {
        case triggerlab::SceneProfile::Crossing: return L"Crossing";
        case triggerlab::SceneProfile::Multiple: return L"Multiple targets";
        case triggerlab::SceneProfile::Precision: return L"Precision";
    }
    return L"Unknown";
}
// Own one compatible surface; never expose the intermediate erase/draw stages.
class BackBuffer {
public:
    ~BackBuffer() { clear(); }
    BackBuffer()=default;
    BackBuffer(const BackBuffer&)=delete;
    BackBuffer& operator=(const BackBuffer&)=delete;
    bool ensure(HDC screen,int width,int height) noexcept {
        if(dc_ && width==width_ && height==height_) return true;
        if(width<=0 || height<=0) return false;
        HDC dc=CreateCompatibleDC(screen); if(!dc) return false;
        HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);
        if(!bitmap) { DeleteDC(dc); return false; }
        HGDIOBJ previous=SelectObject(dc,bitmap);
        if(!previous || previous==HGDI_ERROR) { DeleteObject(bitmap); DeleteDC(dc); return false; }
        clear(); dc_=dc; bitmap_=bitmap; previous_=previous; width_=width; height_=height; return true;
    }
    HDC dc() const noexcept {return dc_;}
private:
    void clear() noexcept { if(dc_) {SelectObject(dc_,previous_); DeleteObject(bitmap_); DeleteDC(dc_);} dc_=nullptr; }
    HDC dc_=nullptr; HBITMAP bitmap_=nullptr; HGDIOBJ previous_=nullptr;
    int width_=0,height_=0;
};
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
        engine_.setConfig(loaded.settings.engine); detector_.setConfig(loaded.settings.detector); simulation_.setConfig(loaded.settings.scene);
        configStatus_ = std::wstring(loaded.message.begin(), loaded.message.end());
        simulation_.render(frame_);
    }

    void attach(HWND window) noexcept { window_ = window; }
    void createControls() {
        const wchar_t* labels[] = {L"Cooldown ms [50..2000]", L"ROI radius [0..30]", L"Stable frames [1..20]",
            L"Coverage [0.01..1]", L"Minimum red [0..255]", L"Minimum blue [0..255]", L"Maximum green [0..255]"};
        for (int i=0; i<7; ++i) {
            CreateWindowW(L"STATIC", labels[i], WS_CHILD|WS_VISIBLE, 976, 55+i*36, 250, 20, window_, nullptr, nullptr, nullptr);
            edits_[i]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
                976,73+i*36,240,24,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),nullptr,nullptr);
        }
        CreateWindowW(L"BUTTON",L"Apply and save",WS_CHILD|WS_VISIBLE|WS_TABSTOP,976,340,240,30,window_,reinterpret_cast<HMENU>(201),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Reset statistics / scene",WS_CHILD|WS_VISIBLE|WS_TABSTOP,976,375,240,30,window_,reinterpret_cast<HMENU>(202),nullptr,nullptr);
        CreateWindowW(L"STATIC",L"Scene / speed [0.25..3]",WS_CHILD|WS_VISIBLE,976,418,240,20,window_,nullptr,nullptr,nullptr);
        sceneControl_=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,976,442,240,180,window_,reinterpret_cast<HMENU>(203),nullptr,nullptr);
        for(auto label:{L"Crossing (classic)",L"Multiple targets",L"Precision (small target)"}) SendMessageW(sceneControl_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        speedControl_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"1",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,976,478,240,24,window_,nullptr,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Apply scene and save",WS_CHILD|WS_VISIBLE|WS_TABSTOP,976,510,240,30,window_,reinterpret_cast<HMENU>(204),nullptr,nullptr);
        pauseControl_=CreateWindowW(L"BUTTON",L"Pause scene (P)",WS_CHILD|WS_VISIBLE|WS_TABSTOP,976,550,240,30,window_,reinterpret_cast<HMENU>(205),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Export event log (CSV)",WS_CHILD|WS_VISIBLE|WS_TABSTOP,976,590,240,30,window_,reinterpret_cast<HMENU>(206),nullptr,nullptr);
        syncControls();
    }
    void syncControls() {
        const auto e=engine_.config(); const auto d=detector_.config();
        const std::array<std::wstring,7> values{std::to_wstring(e.cooldown.count()),std::to_wstring(d.roiRadius),
            std::to_wstring(e.stableFrames),formatNumber(d.minimumCoverage),std::to_wstring(d.minimumRed),
            std::to_wstring(d.minimumBlue),std::to_wstring(d.maximumGreen)};
        for (int i=0;i<7;++i) if (edits_[i]) SetWindowTextW(edits_[i],values[i].c_str());
        SendMessageW(sceneControl_,CB_SETCURSEL,static_cast<WPARAM>(simulation_.config().profile),0);
        SetWindowTextW(speedControl_,formatNumber(simulation_.config().speed).c_str());
    }
    void save() {
        std::string error;
        if (triggerlab::saveSettings(configPath_,{engine_.config(),detector_.config(),simulation_.config()},error)) configStatus_=L"Settings saved";
        else configStatus_=L"Save failed: " + std::wstring(error.begin(),error.end());
    }
    void command(int id) {
        if(id==204) {
            wchar_t value[128]{}; GetWindowTextW(speedControl_,value,128);
            std::wistringstream input(value); input.imbue(std::locale::classic()); double speed=0;
            const auto selected=SendMessageW(sceneControl_,CB_GETCURSEL,0,0);
            if(input>>speed && (input>>std::ws).eof() && std::isfinite(speed) && speed>=.25 && speed<=3 && selected>=0 && selected<=2) {
                simulation_.setConfig({static_cast<triggerlab::SceneProfile>(selected),speed});
                resetSession(); save(); syncControls();
            } else configStatus_=L"Invalid scene settings: not changed";
        } else if(id==205) togglePause();
        else if(id==206) exportLog();
        else if(id!=201 && id!=202) return;
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
    void resetSession() {
        holdingSpace_=false; centerAim(); engine_.reset(); diagnostics_.reset(); simulation_.reset();
        simulation_.render(frame_); lastDetection_={}; lastResult_={}; previousTick_=triggerlab::Clock::now();
    }
    void togglePause() {
        paused_=!paused_; holdingSpace_=false; mouseUp();
        lastResult_=engine_.step({false,false},triggerlab::Clock::now());
        diagnostics_.observe({false,false},lastResult_,triggerlab::Clock::now());
        SetWindowTextW(pauseControl_,paused_?L"Resume scene (P)":L"Pause scene (P)");
    }
    void exportLog() {
        try {
            const auto path=configPath_.parent_path()/L"events.csv";
            if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path,std::ios::binary|std::ios::trunc); out<<diagnostics_.csv(); out.close();
            configStatus_=out?L"Event log saved beside settings: events.csv":L"Event log export failed";
        } catch(const std::exception&) { configStatus_=L"Event log export failed"; }
    }
    bool smokeTest(const std::filesystem::path& path) {
        configPath_=path;
        engine_.setConfig({}); detector_.setConfig({}); simulation_.setConfig({}); syncControls();
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
        if(diagnostics_.sampleCount()!=1) return false;
        command(205); if(!paused_ || holdingSpace_) return false; command(205);
        SendMessageW(sceneControl_,CB_SETCURSEL,1,0); SetWindowTextW(speedControl_,L"1.5"); command(204);
        if(simulation_.config().profile!=triggerlab::SceneProfile::Multiple || simulation_.config().speed!=1.5) return false;
        const auto restored=triggerlab::loadSettings(path);
        if(!restored.ok || restored.settings.scene.speed!=1.5) return false;
        command(206); if(!std::filesystem::exists(path.parent_path()/L"events.csv")) return false;
        command(202);
        simulation_.setConfig({}); mouseDown(480,270); tick(); tick();
        if(engine_.shotCount()!=1 || !active()) return false;
        mouseMove(975,270); tick(); if(active() || lastResult_.state!=triggerlab::TriggerState::Idle) return false;
        mouseUp(); mouseMove(10,10); tick(); if(lastDetection_.found) return false;
        onFocusLost(); if(active()) return false;
        command(202); return engine_.shotCount()==0 && diagnostics_.sampleCount()==0;
    }
    void onFocusLost() noexcept {
        holdingSpace_=false; aim_.release();
        if(GetCapture()==window_) ReleaseCapture();
    }
    void mouseMove(int x,int y) noexcept {
        aim_.move(x,y);
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT),TME_LEAVE,window_,0}; TrackMouseEvent(&tracking);
    }
    void mouseDown(int x,int y) noexcept {
        mouseMove(x,y); if(!aim_.inside()) return;
        SetFocus(window_); aim_.press(); if(!paused_) SetCapture(window_);
    }
    void mouseUp() noexcept { aim_.release(); if(GetCapture()==window_) ReleaseCapture(); }
    void mouseLeave() noexcept { aim_.leave(); }
    void mouseCaptureLost() noexcept {aim_.release();}
    void centerAim() noexcept {mouseUp(); aim_.center();}
    bool active() const noexcept {return !paused_ && (holdingSpace_ || aim_.active());}

    void onKeyDown(WPARAM key, LPARAM lparam) {
        if (key == VK_SPACE) {
            holdingSpace_ = !paused_;
            return;
        }
        if (key == VK_ESCAPE) {
            DestroyWindow(window_);
            return;
        }
        if ((lparam & (1LL << 30)) != 0) return; // avoid key-repeat toggles
        if (key == 'P') togglePause();
        else if (key == 'R') {
            resetSession();
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

        if(!paused_) simulation_.advance(dt);
        simulation_.render(frame_);
        lastDetection_ = detector_.detect(frame_, aim_.x(), aim_.y());
        lastResult_ = engine_.step({active(), lastDetection_.found}, now);
        diagnostics_.observe({active(),lastDetection_.found},lastResult_,now);
        if (lastResult_.fired) simulation_.onSimulatedShot(aim_.x(),aim_.y());
        RECT scene{0,0,kFrameWidth,kFrameHeight}; InvalidateRect(window_, &scene, FALSE);
    }

    void paint(HDC screen, const RECT& client) {
        if(!backBuffer_.ensure(screen,client.right-client.left,client.bottom-client.top)) return;
        const HDC dc=backBuffer_.dc();
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = frame_.width();
        bitmap.bmiHeader.biHeight = -frame_.height(); // top-down pixel buffer
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;

        SetDCBrushColor(dc,RGB(31,38,48)); FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        RECT panel{960,0,client.right,client.bottom};
        FillRect(dc,&panel,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(225,230,238));
        drawText(dc,976,18,L"SETTINGS / SCENE - v0.3.1");
        const int displayWidth = std::min(960L, client.right - client.left);
        const int displayHeight = std::min(540L, client.bottom - client.top);
        if (displayWidth <= 0 || displayHeight <= 0) return;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, 0, 0, displayWidth, displayHeight,
                      0, 0, frame_.width(), frame_.height(), frame_.data(), &bitmap,
                      DIB_RGB_COLORS, SRCCOPY);

        const int crossX = aim_.x();
        const int crossY = aim_.y();
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
        drawText(dc, textX, 18, L"TRIGGERLAB v0.3.1  |  LOCAL PRACTICE SANDBOX");
        drawText(dc, textX, 18 + fontHeight, L"MOUSE: aim | HOLD LMB / SPACE: arm | RMB: center | P: pause | R: reset");
        drawText(dc, textX, 18 + fontHeight * 2,
                 L"UP/DOWN: cooldown  |  LEFT/RIGHT: detection ROI");

        const int textY = 18 + fontHeight * 4;
        SetTextColor(dc, active() ? RGB(130, 255, 176) : RGB(240, 190, 156));
        drawText(dc, textX, textY,
                 L"Engine: " + std::wstring(stateLabel(lastResult_.state)) +
                 (active() ? L" (holding)" : L" (inactive)"));
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
        auto metric=[](std::optional<double> value) { if(!value) return std::wstring(L"--"); return std::to_wstring(static_cast<int>(std::lround(*value))); };
        drawText(dc,textX,textY+fontHeight*5,L"Detection to shot: P50 " + metric(diagnostics_.percentile(.50))+L" ms / P95 "+metric(diagnostics_.percentile(.95))+L" ms | Samples: "+std::to_wstring(diagnostics_.sampleCount()));
        drawText(dc,textX,textY+fontHeight*6,std::wstring(L"Scene: ")+sceneLabel(simulation_.config().profile)+L" | Speed: "+formatNumber(simulation_.config().speed)+(paused_?L" | PAUSED":L" | RUNNING"));
        drawText(dc,textX,textY+fontHeight*7,L"Recent events: "+std::to_wstring(diagnostics_.events().size()));
        int eventY=displayHeight-145;
        const auto& events=diagnostics_.events();
        for(std::size_t i=events.size()>3?events.size()-3:0;i<events.size();++i) {
            const auto& event=events[i];
            drawText(dc,textX,eventY,L"+"+std::to_wstring(static_cast<int>(event.elapsedMs))+L"ms "+stateLabel(event.state)+(event.fired?L" SIMULATED SHOT":L"")+L" | target "+(event.targetVisible?L"YES":L"NO")); eventY+=fontHeight;
        }
        drawText(dc,textX,displayHeight-52,configStatus_);
        if (simulation_.isHitFlashActive()) {
            SetTextColor(dc, RGB(123, 255, 170));
            drawText(dc, crossX + 22, crossY - 55, L"SIMULATED HIT");
        }
        SetTextColor(dc, RGB(180, 192, 202));
        drawText(dc, textX, displayHeight - 28, L"Simulated shots only | Local scene | No external input");
        BitBlt(screen,0,0,client.right-client.left,client.bottom-client.top,dc,0,0,SRCCOPY);
    }

private:
    BackBuffer backBuffer_;
    triggerlab::AimInput aim_;
    std::filesystem::path configPath_;
    std::wstring configStatus_;
    std::array<HWND,7> edits_{};
    HWND sceneControl_=nullptr, speedControl_=nullptr, pauseControl_=nullptr;
    triggerlab::Diagnostics diagnostics_;
    bool paused_=false;
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
        case WM_CTLCOLORSTATIC: {
            HDC dc=reinterpret_cast<HDC>(wparam); SetTextColor(dc,RGB(225,230,238)); SetBkColor(dc,RGB(31,38,48));
            SetDCBrushColor(dc,RGB(31,38,48)); return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            RECT minimum{0,0,kFrameWidth+280,640};
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
        case WM_MOUSEMOVE:
            if(app) app->mouseMove(GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)); return 0;
        case WM_LBUTTONDOWN:
            if(app) app->mouseDown(GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)); return 0;
        case WM_LBUTTONUP:
            if(app) app->mouseUp(); return 0;
        case WM_RBUTTONDOWN:
            if(app) app->centerAim(); return 0;
        case WM_MOUSELEAVE:
            if(app) app->mouseLeave(); return 0;
        case WM_CAPTURECHANGED:
            if(app) app->mouseCaptureLost(); return 0;
        case WM_CANCELMODE:
            if(app) app->onFocusLost(); return 0;
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

        RECT rect{0, 0, kFrameWidth+280, 640};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE);
        HWND window = CreateWindowExW(0, className, L"TriggerLab - Local Practice Sandbox",
                                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                      rect.right - rect.left, rect.bottom - rect.top,
                                      nullptr, nullptr, instance, &app);
        if (!window) {
            MessageBoxW(nullptr, L"Could not create practice window.", L"TriggerLab", MB_OK | MB_ICONERROR);
            return 1;
        }
        if (std::wstring(commandLine)==L"--smoke-test") {
            const auto directory=std::filesystem::temp_directory_path()/ (L"triggerlab-gui-test-"+std::to_wstring(GetCurrentProcessId()));
            const bool passed=app.smokeTest(directory/L"settings.json");
            HDC dc=GetDC(window); RECT client{}; GetClientRect(window,&client); app.paint(dc,client);
            const DWORD initialResources=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            for(int i=0;i<100;++i) { RECT size{0,0,client.right+(i%2)*16,client.bottom}; app.paint(dc,size); }
            app.paint(dc,client);
            const bool resourcesStable=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==initialResources;
            ReleaseDC(window,dc);
            DestroyWindow(window); std::filesystem::remove_all(directory);
            return passed && resourcesStable?0:2;
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
