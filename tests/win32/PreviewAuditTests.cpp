#include "PreviewTestHarness.hpp"
#include <fstream>
#include <iostream>

namespace triggerlab::win32 {
int PreviewTestHarness::auditTransitionsTest(PreviewTestHarness &app) {
    using namespace std::chrono_literals;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    std::ofstream report(std::filesystem::path(executable).parent_path() / L"audit-test-report.txt");
    report << std::unitbuf;
    POINT cursor{};
    if (!GetCursorPos(&cursor) || inputHeld()) {
        report << "SKIP: interactive desktop or idle physical input unavailable\n";
        return 77;
    }
    const auto previous = GetForegroundWindow();
    HWND target = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"TriggerLab audit target", WS_POPUP | WS_VISIBLE,
                                  cursor.x - 20, cursor.y - 20, 180, 180, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    if (!target)
        return 2;
    struct Cleanup {
        PreviewTestHarness &app;
        HWND target, previous;
        ~Cleanup() {
            app.stop();
            app.targetWindow = nullptr;
            DestroyWindow(target);
            if (previous && IsWindow(previous))
                SetForegroundWindow(previous);
        }
    } cleanup{app, target, previous};
    const DWORD foregroundThread = GetWindowThreadProcessId(previous, nullptr);
    const DWORD thread = GetCurrentThreadId();
    const bool attached =
        foregroundThread && foregroundThread != thread && AttachThreadInput(thread, foregroundThread, TRUE);
    SetForegroundWindow(target);
    SetActiveWindow(target);
    SetFocus(target);
    if (attached)
        AttachThreadInput(thread, foregroundThread, FALSE);
    if (GetForegroundWindow() != target) {
        report << "SKIP: owned test target cannot receive foreground\n";
        return 77;
    }
    auto check = [&](bool value, const char *reason) {
        report << (value ? "PASS: " : "FAIL: ") << reason << '\n';
        return value;
    };
    auto notify = [&](int id, int code, HWND control) {
        SendMessageW(app.window, WM_COMMAND, MAKEWPARAM(id, code), reinterpret_cast<LPARAM>(control));
    };
    auto keyF9 = [&] {
        INPUT keys[2]{};
        keys[0].type = keys[1].type = INPUT_KEYBOARD;
        keys[0].ki.wVk = keys[1].ki.wVk = VK_F9;
        keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
        if (SendInput(2, keys, sizeof(INPUT)) != 2)
            return false;
        const auto deadline = triggerlab::Clock::now() + 1s;
        while (app.running && triggerlab::Clock::now() < deadline) {
            pumpTestMessages();
            Sleep(1);
        }
        return !app.running && !app.stopHotkeyRegistered;
    };
    DWORD process = 0;
    GetWindowThreadProcessId(target, &process);
    if (!app.applyWindowChoice({target, process, L"TriggerLab audit target", L"TriggerLab"}))
        return 2;
    if (!app.settings.advancedMode)
        app.command(control::toggleAdvanced);
    const auto targetColor = triggerlab::Pixel{19, 67, 231, 255};
    auto observe = [&](bool visible, bool unknown = false) {
        const auto bounds = app.clientBounds();
        const auto roi =
            bounds ? triggerlab::aimRegion(*bounds, app.settings) : std::optional<triggerlab::Region>{};
        triggerlab::CaptureSample sample;
        sample.status = triggerlab::CaptureStatus::Ok;
        sample.region = roi.value_or(triggerlab::Region{cursor.x - 8, cursor.y - 8, 17, 17});
        sample.frame.emplace(sample.region.width, sample.region.height);
        const auto pixel = unknown   ? triggerlab::Pixel{0, 0, 0, 255}
                           : visible ? targetColor
                                     : triggerlab::Pixel{40, 40, 40, 255};
        sample.frame->clear(pixel);
        sample.cursorPosition = triggerlab::CapturePoint{cursor.x, cursor.y};
        sample.cursorPixel = pixel;
        sample.allBlack = unknown;
        app.analyzedClient = bounds;
        app.analysisPoint = triggerlab::CapturePoint{sample.region.x + sample.region.width / 2,
                                                     sample.region.y + sample.region.height / 2};
        app.acceptSample(std::move(sample), false);
    };
    for (bool center : {true, false}) {
        app.stop();
        SendMessageW(app.modeControl, CB_SETCURSEL, center ? 0 : 1, 0);
        notify(control::detectionMode, CBN_SELCHANGE, app.modeControl);
        triggerlab::CaptureSample picked;
        picked.status = triggerlab::CaptureStatus::Ok;
        picked.frame.emplace(1, 1);
        picked.frame->clear(targetColor);
        picked.cursorPosition = triggerlab::CapturePoint{cursor.x, cursor.y};
        picked.cursorPixel = targetColor;
        app.acceptSample(std::move(picked), true);
        SetWindowTextW(app.settingEdits[5], L"1");
        SetWindowTextW(app.settingEdits[7], L"150");
        notify(control::saveAll, BN_CLICKED, GetDlgItem(app.window, control::saveAll));
        if (!check(!app.settingsDirty() && !app.colorDirty() && !app.verified, "saved unverified draft"))
            return 2;
        app.command(control::startCapture);
        KillTimer(app.window, timer::capture); // Deterministic injected frames; same production adapter.
        if (!check(app.running && app.stopHotkeyRegistered && !app.autoClick,
                   "capture owns F9 before arming"))
            return 2;
        SetForegroundWindow(target);
        if (!check(keyF9(), "real F9 stops capture while another window has focus"))
            return 2;
        app.command(control::startCapture);
        KillTimer(app.window, timer::capture);
        if (!app.settings.dryRun)
            SendMessageW(app.dryControl, BM_CLICK, 0, 0);
        SendMessageW(app.autoControl, BM_CLICK, 0, 0);
        SetForegroundWindow(target);
        observe(true);
        Sleep(2);
        observe(true);
        if (!check(app.verified && app.diagnosticCount > 0 && app.settings.dryRun,
                   "native UI diagnostic verifies"))
            return 2;
        recoveryDowns = recoveryUps = 0;
        rejectRecoveryUp = false;
        SendMessageW(app.dryControl, BM_CLICK, 0, 0);
        if (!check(app.verified && !app.autoClick && !app.settings.dryRun,
                   "native dryRun toggle preserves verification and disarms"))
            return 2;
        SendMessageW(app.autoControl, BM_CLICK, 0, 0);
        SetForegroundWindow(target);
        observe(true);
        Sleep(2);
        observe(true);
        if (!check(recoveryDowns == 1 && app.syntheticDown, "first authorized Down"))
            return 2;
        observe(false, true);
        if (!check(app.gate.clickedHover(), "unknown black frame during Down preserves latch"))
            return 2;
        Sleep(180);
        app.finishClick();
        observe(true);
        Sleep(60);
        observe(true);
        observe(false, true);
        observe(true);
        Sleep(2);
        observe(true);
        if (!check(recoveryDowns == 1 && recoveryUps == 1 && app.gate.clickedHover(),
                   "unknown black frame after Up does not authorize repeated Down"))
            return 2;
        observe(false);
        observe(true);
        Sleep(2);
        observe(true);
        if (!check(recoveryDowns == 2, "confirmed departure permits next hover"))
            return 2;
        app.disableClicks();
        if (!check(app.running && app.stopHotkeyRegistered && !app.syntheticDown,
                   "disarming retains capture F9 and releases Down"))
            return 2;
        SetForegroundWindow(target);
        if (!check(keyF9(), "F9 still stops capture after disarming"))
            return 2;

        // A changed detection mode, color, parameter or chosen window invalidates the result.
        auto verifyAgain = [&] {
            if (!app.settings.dryRun)
                SendMessageW(app.dryControl, BM_CLICK, 0, 0);
            app.command(control::saveAll);
            app.command(control::startCapture);
            KillTimer(app.window, timer::capture);
            SendMessageW(app.autoControl, BM_CLICK, 0, 0);
            SetForegroundWindow(target);
            observe(true);
            Sleep(2);
            observe(true);
            app.disableClicks();
            return app.verified;
        };
        if (!verifyAgain())
            return 2;
        SetWindowTextW(app.settingEdits[7], L"30");
        if (!check(!app.verified && !app.autoClick, "changed parameter invalidates verification"))
            return 2;
        if (!verifyAgain())
            return 2;
        SetWindowTextW(app.colorEdits[3], app.color.channelTolerance == 0 ? L"1" : L"0");
        if (!check(!app.verified && !app.autoClick, "changed color invalidates verification"))
            return 2;
        if (!verifyAgain())
            return 2;
        app.applyWindowChoice({target, process, L"TriggerLab audit target", L"TriggerLab"});
        if (!check(!app.verified, "window choice invalidates verification"))
            return 2;
        if (!verifyAgain())
            return 2;
        SendMessageW(app.modeControl, CB_SETCURSEL, center ? 1 : 0, 0);
        notify(control::detectionMode, CBN_SELCHANGE, app.modeControl);
        if (!check(!app.verified && !app.autoClick, "changed detection mode invalidates verification"))
            return 2;
    }
    app.stop();
    if (!RegisterHotKey(nullptr, 55, MOD_NOREPEAT, VK_F9))
        return 2;
    app.command(control::startCapture);
    const bool conflictRejected = !app.running && !app.actionError.empty();
    UnregisterHotKey(nullptr, 55);
    if (!check(conflictRejected, "F9 conflict prevents capture with a visible error"))
        return 2;
    const auto corruptRoot = app.settingsPath.parent_path() / L"corrupt";
    std::filesystem::create_directories(corruptRoot);
    {
        std::ofstream out(corruptRoot / L"analyzer-settings.json");
        out << "broken";
    }
    PreviewTestHarness corrupted(corruptRoot, recoverySender);
    if (!check(!corrupted.paramsOnDisk && !corrupted.settingsLoadWarning.empty() &&
                   corrupted.settings.dryRun && !corrupted.verified,
               "corrupt settings require explicit save and show recovery"))
        return 2;
    return 0;
}
} // namespace triggerlab::win32
