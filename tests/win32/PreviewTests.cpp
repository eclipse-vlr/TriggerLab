#include "ImagePaletteDialog.hpp"
#include "PreviewTestHarness.hpp"
#include "triggerlab/ImageFile.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <commctrl.h>
#include <dwmapi.h>
#include <fstream>
#include <iostream>

namespace triggerlab::win32 {
bool PreviewTestHarness::inputRecoveryTest() {
    // The advanced command must enforce the same policy as the wizard.
    targetWindow = window;
    targetProcess = GetCurrentProcessId();
    running = true;
    settings.dryRun = false;
    colorReady = false;
    verified = false;
    SendMessageW(autoControl, BM_SETCHECK, BST_CHECKED, 0);
    command(control::enableTrigger);
    if (autoClick || syntheticDown)
        return false;
    colorReady = true;
    verified = true;
    SetWindowTextW(settingEdits[0], L"invalid");
    running = true;
    SendMessageW(autoControl, BM_SETCHECK, BST_CHECKED, 0);
    command(control::enableTrigger);
    if (autoClick || syntheticDown)
        return false;
    syncSettings();
    color.space = triggerlab::ColorSpace::HSV;
    syncActions();
    if (IsWindowEnabled(colorEdits[3]))
        return false;
    color.space = triggerlab::ColorSpace::RGB;
    syncActions();
    if (!IsWindowEnabled(colorEdits[3]))
        return false;
    stop();
    targetWindow = nullptr;
    targetProcess = 0;

    recoveryDowns = 0;
    recoveryUps = 0;
    rejectRecoveryUp = true;
    running = true;
    autoClick = true;
    if (!inputPulse.start(5))
        return false;
    syntheticDown = true;
    const auto deadline = triggerlab::Clock::now() + std::chrono::seconds{2};
    while (autoClick && triggerlab::Clock::now() < deadline) {
        finishClick();
        Sleep(1);
    }
    if (autoClick || !syntheticDown || !inputPulse.pendingRelease() || recoveryDowns != 1 || recoveryUps != 0)
        return false;
    SendMessageW(window, WM_HOTKEY, 9, 0);
    if (running || autoClick || !syntheticDown || !IsWindowEnabled(GetDlgItem(window, control::stop)))
        return false;
    if (inputPulse.start(5) || recoveryDowns != 1)
        return false;
    rejectRecoveryUp = false;
    SendMessageW(window, WM_HOTKEY, 9, 0);
    if (syntheticDown || inputPulse.pendingRelease() || recoveryUps != 1 || clickCount != 0)
        return false;
    if (!lastPress || !lastPress->releaseAccepted || !lastPress->interrupted)
        return false;
    // Recovery must also work after Stop killed the capture timer.
    rejectRecoveryUp = true;
    autoClick = true;
    running = true;
    if (!inputPulse.start(150))
        return false;
    syntheticDown = true;
    stop();
    if (running || autoClick || !syntheticDown)
        return false;
    rejectRecoveryUp = false;
    SendMessageW(window, WM_TIMER, 3, 0);
    if (syntheticDown || inputPulse.pendingRelease() || recoveryDowns != 2 || recoveryUps != 2 ||
        clickCount != 0)
        return false;
    std::cout << "PASS: failed Up remains pending, blocks Down and recovers through Stop/F9 and the service "
                 "timer\n";
    return true;
}

bool PreviewTestHarness::windowPickerTest() {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    std::ofstream report(std::filesystem::path(executable).parent_path() / L"window-picker-test-report.txt");
    if (!triggerlab::windowPickerDialogSmoke()) {
        report << "FAIL: window picker enumeration, selection or layout\n";
        return false;
    }
    HWND target = CreateWindowW(L"STATIC", L"Window picker integration", WS_OVERLAPPEDWINDOW, 0, 0, 240, 180,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!target)
        return false;
    struct Cleanup {
        HWND target;
        ~Cleanup() {
            if (IsWindow(target))
                DestroyWindow(target);
        }
    } cleanup{target};
    ShowWindow(target, SW_SHOWNOACTIVATE);
    DWORD process = 0;
    GetWindowThreadProcessId(target, &process);
    triggerlab::WindowChoice choice{target, process, L"Window picker integration", L"TriggerLab"};
    running = true;
    autoClick = true;
    verified = true;
    settings.dryRun = false;
    sample.frame.emplace(1, 1);
    displayFrame.emplace(1, 1);
    targetReference.emplace(1, 1);
    backgroundReference.emplace(1, 1);
    const auto originalColor = color.target;
    const auto originalClicks = clickCount;
    const bool selected = applyWindowChoice(choice) && targetWindow == target && targetProcess == process &&
                          !running && !autoClick && !verified && settings.dryRun && !syntheticDown &&
                          !sample.frame && !displayFrame && !targetReference && !backgroundReference &&
                          color.target.red == originalColor.red &&
                          color.target.green == originalColor.green &&
                          color.target.blue == originalColor.blue && clickCount == originalClicks;
    auto invalid = choice;
    invalid.process = 0;
    const bool rejected = !applyWindowChoice(invalid) && targetWindow == target && !running && !autoClick;
    DestroyWindow(target);
    cleanup.target = nullptr;
    const bool closed = !applyWindowChoice(choice);
    refreshTargetButton();
    const bool passed = selected && rejected && closed && !targetWindow && !running && !autoClick;
    report << (passed ? "PASS" : "FAIL")
           << ": enumeration with title/application, both themes, 100-200% DPI, empty list, cancel, stale "
              "window/process, manual fallback and input reset\n";
    return passed;
}

bool PreviewTestHarness::scrollPaintTest(std::ostream &report) {
    struct Probe {
        PreviewTestHarness &app;
        int erases = 0, paints = 0;
        std::vector<std::string> events;
    } probe{*this, 0, 0, {}};
    const auto callback =
        +[](HWND child, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR context) -> LRESULT {
        auto &probe = *reinterpret_cast<Probe *>(context);
        if (probe.app.arranging) {
            if (message == WM_ERASEBKGND)
                ++probe.erases;
            if (message == WM_PAINT)
                ++probe.paints;
            if ((message == WM_ERASEBKGND || message == WM_PAINT) && probe.events.size() < 16) {
                char name[128]{};
                GetClassNameA(child, name, 128);
                probe.events.push_back(std::string(name) + " id=" + std::to_string(GetDlgCtrlID(child)) +
                                       " message=" + std::to_string(message));
            }
        }
        return DefSubclassProc(child, message, wp, lp);
    };
    std::vector<HWND> controls{window};
    EnumChildWindows(
        window,
        [](HWND child, LPARAM value) -> BOOL {
            reinterpret_cast<std::vector<HWND> *>(value)->push_back(child);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&controls));
    struct Cleanup {
        const std::vector<HWND> &controls;
        SUBCLASSPROC callback;
        ~Cleanup() {
            for (HWND child : controls)
                RemoveWindowSubclass(child, callback, 90);
        }
    } cleanup{controls, callback};
    for (HWND child : controls)
        if (!SetWindowSubclass(child, callback, 90, reinterpret_cast<DWORD_PTR>(&probe)))
            return false;
    scroll(SB_VERT, SB_TOP);
    UpdateWindow(window);
    for (int i = 0; i < 4; ++i) {
        scroll(SB_VERT, SB_LINEDOWN);
        UpdateWindow(window);
    }
    scroll(SB_VERT, SB_TOP);
    UpdateWindow(window);
    report << "scroll intermediate erases=" << probe.erases << " paints=" << probe.paints << '\n';
    for (const auto &event : probe.events)
        report << event << '\n';
    return probe.erases == 0 && probe.paints == 0;
}

bool PreviewTestHarness::helpTooltipsTest(std::ostream &report) {
    report << "tooltip tools=" << SendMessageW(tooltip, TTM_GETTOOLCOUNT, 0, 0) << '\n';
    if (!tooltip || !colorHelp || !IsWindowVisible(colorHelp))
        return false;
    for (int index = 0; index < 9; ++index) {
        const HWND help = index < 8 ? settingHelp[index] : colorHelp;
        if (!help ||
            (IsWindowVisible(help) != FALSE) != (index == 8 || (settings.advancedMode && !profilePane))) {
            report << "help visibility index=" << index << '\n';
            return false;
        }
        wchar_t text[2048]{};
        TOOLINFOW info{TTTOOLINFOW_V2_SIZE};
        info.uFlags = TTF_IDISHWND;
        info.hwnd = window;
        info.uId = reinterpret_cast<UINT_PTR>(help);
        info.lpszText = text;
        if (!SendMessageW(tooltip, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&info)) || !info.lpszText ||
            info.lpszText == LPSTR_TEXTCALLBACKW || std::wstring(info.lpszText) != parameterHelp(index)) {
            report << "help text index=" << index << '\n';
            return false;
        }
    }
    return SendMessageW(tooltip, TTM_GETDELAYTIME, TTDT_INITIAL, 0) == 400 &&
           SendMessageW(tooltip, TTM_GETDELAYTIME, TTDT_AUTOPOP, 0) == 20000;
}

int PreviewTestHarness::helpPopupTest() {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    std::ofstream report(std::filesystem::path(executable).parent_path() / L"help-popup-test-report.txt");
    report << std::unitbuf;
    POINT cursor{};
    if (!GetCursorPos(&cursor) || inputHeld()) {
        report << "SKIP: physical input held\n";
        return 77;
    }
    // Put an inert question mark under the existing pointer. No SendInput
    // or cursor movement: exercise the native hover timer and real popup.
    HWND owner = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"TriggerLab tooltip test", WS_POPUP | WS_VISIBLE,
                                 cursor.x - 20, cursor.y - 20, 80, 80, window, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    if (!owner)
        return 2;
    HWND help = CreateWindowW(L"STATIC", L"?",
                              WS_CHILD | WS_VISIBLE | WS_BORDER | SS_CENTER | SS_CENTERIMAGE | SS_NOTIFY, 10,
                              10, 30, 30, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    TOOLINFOW info{TTTOOLINFOW_V2_SIZE};
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = owner;
    info.uId = reinterpret_cast<UINT_PTR>(help);
    info.lpszText = const_cast<wchar_t *>(parameterHelp(1));
    struct Cleanup {
        HWND tooltip, owner;
        TOOLINFOW &info;
        ~Cleanup() {
            SendMessageW(tooltip, TTM_POP, 0, 0);
            SendMessageW(tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
            DestroyWindow(owner);
        }
    } cleanup{tooltip, owner, info};
    if (!help || !SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info)))
        return 2;
    SetWindowSubclass(help, viewportProcedure, 2, reinterpret_cast<DWORD_PTR>(this));
    UpdateWindow(owner);
    if (WindowFromPoint(cursor) != help) {
        report << "SKIP: tooltip target covered\n";
        return 77;
    }
    for (bool dark : {false, true}) {
        settings.darkTheme = dark;
        applyTheme();
        SendMessageW(tooltip, TTM_POP, 0, 0);
        SendMessageW(help, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
        const auto deadline = triggerlab::Clock::now() + std::chrono::seconds{2};
        while (!IsWindowVisible(tooltip) && triggerlab::Clock::now() < deadline) {
            POINT current{};
            if (!GetCursorPos(&current) || current.x != cursor.x || current.y != cursor.y || inputHeld()) {
                report << "SKIP: physical input changed\n";
                return 77;
            }
            pumpTestMessages();
            Sleep(5);
        }
        RECT rect{};
        GetWindowRect(tooltip, &rect);
        if (!IsWindowVisible(tooltip) || rect.right <= rect.left || rect.bottom <= rect.top) {
            report << "FAIL: native hover popup dark=" << dark << '\n';
            return 2;
        }
        report << "PASS: native hover popup dark=" << dark << " width=" << rect.right - rect.left
               << " height=" << rect.bottom - rect.top << '\n';
    }
    return 0;
}

bool PreviewTestHarness::firstRunTest() {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    std::ofstream report(std::filesystem::path(executable).parent_path() / L"first-run-test-report.txt");
    report << std::unitbuf;
    const auto started = triggerlab::Clock::now();
    auto stage = [&](const char *name) {
        report << "STAGE " << name << " elapsed_ms="
               << std::chrono::duration_cast<std::chrono::milliseconds>(triggerlab::Clock::now() - started)
                      .count()
               << '\n';
    };
    stage("initial layout");
    auto check = [&](bool value, const char *message) {
        if (!value)
            report << message;
        return value;
    };
    MINMAXINFO minimum{};
    SendMessageW(window, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&minimum));
    if (!check(minimum.ptMinTrackSize.y < 800, "FAIL: small screens cannot resize the window\n"))
        return false;
    RECT stopBefore{}, stopAfter{};
    GetWindowRect(GetDlgItem(window, control::stop), &stopBefore);
    scroll(SB_VERT, SB_BOTTOM);
    GetWindowRect(GetDlgItem(window, control::stop), &stopAfter);
    if (!check(EqualRect(&stopBefore, &stopAfter), "FAIL: Stop scrolls out of view\n"))
        return false;
    scroll(SB_VERT, SB_TOP);
    const UINT actualDpi = dpi;
    RECT original{};
    GetWindowRect(window, &original);
    for (UINT testDpi : {96u, 120u, 144u, 192u}) {
        setDpi(testDpi);
        SetWindowPos(window, nullptr, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        for (int mode : {0, 1, 2, 3})
            for (bool dark : {false, true}) {
                report << "LAYOUT dpi=" << testDpi << " mode=" << mode << " dark=" << dark << '\n';
                const bool advanced = mode == 1 || mode == 2;
                profilePane = mode == 2;
                homeDetails = mode == 3;
                settings.advancedMode = advanced;
                settings.darkTheme = dark;
                applyTheme();
                layoutMode();
                if (!check(helpTooltipsTest(report),
                           "FAIL: question-mark help visibility, content or timing\n"))
                    return false;
                if (testDpi == 96 && !check(scrollPaintTest(report),
                                            "FAIL: controls repaint during incomplete scroll layout\n"))
                    return false;
                if (!advanced && !check(IsWindowVisible(homeHint) && IsWindowVisible(homeSummary) &&
                                            (IsWindowVisible(profileChoice) != FALSE) == homeDetails &&
                                            (IsWindowVisible(qualityInfo) != FALSE) == homeDetails,
                                        "FAIL: home details visibility\n"))
                    return false;
                scrollX = scrollY = 0;
                arrange();
                RECT field{};
                GetWindowRect(colorEdits[3], &field);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&field), 2);
                if (!check(narrow && scrollX == 0 && !(GetWindowLongW(window, GWL_STYLE) & WS_HSCROLL),
                           "FAIL: narrow window needs horizontal scrolling\n"))
                    return false;
                for (const auto &entry : controlRects)
                    if (IsWindowVisible(entry.control)) {
                        RECT r{};
                        GetWindowRect(entry.control, &r);
                        MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&r), 2);
                        RECT client{};
                        GetClientRect(window, &client);
                        if (!check(r.left >= 0 && r.right <= client.right,
                                   "FAIL: responsive control outside width\n"))
                            return false;
                        for (const auto &other : controlRects)
                            if (entry.control != other.control && IsWindowVisible(other.control)) {
                                RECT second{}, overlap{};
                                GetWindowRect(other.control, &second);
                                MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&second), 2);
                                if (!check(!IntersectRect(&overlap, &r, &second),
                                           "FAIL: responsive controls overlap\n"))
                                    return false;
                            }
                    }
                LOGFONTW font{};
                GetObjectW(controlFont, sizeof(font), &font);
                if (!check(font.lfHeight == -scaled(16), "FAIL: scaled field font\n"))
                    return false;
                reveal(colorEdits[3]);
                GetWindowRect(colorEdits[3], &field);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&field), 2);
                RECT client{};
                GetClientRect(window, &client);
                if (!(scrollY > 0 && field.left >= 0 && field.top >= scaled(headerHeight) &&
                      field.right <= client.right && field.bottom <= client.bottom))
                    report << "focus dpi=" << dpi << " header=" << scaled(headerHeight)
                           << " scroll=" << scrollY << " rect=" << field.left << "," << field.top << ","
                           << field.right << "," << field.bottom << " client=" << client.right << ","
                           << client.bottom << "\n";
                if (!check(scrollY > 0 && field.left >= 0 && field.top >= scaled(headerHeight) &&
                               field.right <= client.right && field.bottom <= client.bottom,
                           "FAIL: focused field not visible after scroll\n"))
                    return false;
                std::vector<std::pair<HWND, RECT>> fixedRects;
                for (const auto &entry : controlRects)
                    if (pinned(entry.control) && IsWindowVisible(entry.control)) {
                        RECT r{};
                        GetWindowRect(entry.control, &r);
                        fixedRects.push_back({entry.control, r});
                    }
                scroll(SB_HORZ, SB_BOTTOM);
                scroll(SB_VERT, SB_BOTTOM);
                RECT backdrop{};
                GetClientRect(headerBackdrop, &backdrop);
                if (!check(headerBackdrop && backdrop.bottom == scaled(headerHeight),
                           "FAIL: opaque header has wrong height\n"))
                    return false;
                for (const auto &entry : controlRects)
                    if (IsWindowVisible(entry.control) && !pinned(entry.control)) {
                        bool behindHeader = false;
                        for (HWND above = GetWindow(entry.control, GW_HWNDPREV); above;
                             above = GetWindow(above, GW_HWNDPREV))
                            if (above == headerBackdrop) {
                                behindHeader = true;
                                break;
                            }
                        if (!check(behindHeader &&
                                       (GetWindowLongW(entry.control, GWL_STYLE) & WS_CLIPSIBLINGS),
                                   "FAIL: body control can repaint over header\n"))
                            return false;
                    }
                for (const auto &[control, before] : fixedRects) {
                    RECT after{};
                    GetWindowRect(control, &after);
                    if (!check(EqualRect(&before, &after), "FAIL: fixed header moves on scroll\n"))
                        return false;
                    RECT area{};
                    GetWindowRect(window, &area);
                    if (!check(after.top >= area.top && after.bottom <= area.bottom,
                               "FAIL: fixed header outside window\n"))
                        return false;
                }
                SCROLLINFO info{sizeof(info), SIF_ALL};
                GetScrollInfo(window, SB_VERT, &info);
                if (!check(scrollY + static_cast<int>(info.nPage) == scaled(canvasHeight),
                           "FAIL: bottom of canvas inaccessible\n"))
                    return false;
                InvalidateRect(window, nullptr, FALSE);
                UpdateWindow(window);
                stage("layout complete");
            }
    }
    stage("expanded window");
    // Windows limits thick-frame windows to the current monitor. Allow an
    // off-screen size only here so this layout check works on small screens.
    homeDetails = false;
    settings.advancedMode = false;
    layoutMode();
    allowOversizedTestWindow = true;
    setDpi(96);
    SetWindowPos(window, nullptr, 0, 0, 1300, 1550, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    allowOversizedTestWindow = false;
    if (!check(!narrow && scrollX == 0 && scrollY == 0 &&
                   !(GetWindowLongW(window, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)),
               "FAIL: scrollbars remain on large window\n"))
        return false;
    setDpi(actualDpi);
    SetWindowPos(window, nullptr, 0, 0, original.right - original.left, original.bottom - original.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    settings.advancedMode = false;
    settings.darkTheme = false;
    profilePane = false;
    applyTheme();
    layoutMode();
    report << "PASS: 100/125/150/200% DPI, both modes/themes, small window, focus scrolling and expanded "
              "window\n";
    stage("workflow and persistence");
    if (!check(wizardStep() == 0 && !colorReady && !verified && !running && !autoClick && profiles.empty(),
               "FAIL: cold launch\n"))
        return false;
    HWND target = CreateWindowW(L"STATIC", L"First-run test target", WS_OVERLAPPED, 0, 0, 200, 200, nullptr,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!target)
        return false;
    targetWindow = target;
    GetWindowThreadProcessId(target, &targetProcess);
    const bool selected = wizardStep() == 1;
    command(control::toggleDetails);
    const bool expanded = homeDetails && IsWindowVisible(profileChoice);
    command(control::toggleDetails);
    if (!check(expanded && !homeDetails && !IsWindowVisible(profileChoice) && wizardStep() == 1 && !running &&
                   !autoClick,
               "FAIL: home tools toggle changes workflow\n")) {
        DestroyWindow(target);
        return false;
    }
    const bool saved = saveAll();
    const bool stillNeedsColor = wizardStep() == 1 && !colorReady && !std::filesystem::exists(colorPath);
    if (!selected || !saved || !stillNeedsColor) {
        DestroyWindow(target);
        report << "FAIL: saving defaults completes color selection\n";
        return false;
    }
    {
        PreviewTestHarness beforeColor(settingsPath.parent_path());
        if (beforeColor.colorReady) {
            DestroyWindow(target);
            report << "FAIL: restart falsely restores an unchosen color\n";
            return false;
        }
    }
    SetWindowTextW(profileName, L"Ещё без цвета");
    saveProfile();
    if (!profiles.empty()) {
        DestroyWindow(target);
        report << "FAIL: incomplete profile saved\n";
        return false;
    }
    triggerlab::CaptureSample picked;
    picked.status = triggerlab::CaptureStatus::Ok;
    picked.frame.emplace(1, 1);
    picked.cursorPosition = triggerlab::CapturePoint{-100000, -100000};
    picked.cursorPixel = triggerlab::Pixel{19, 67, 231, 255};
    picked.frame->at(0, 0) = *picked.cursorPixel;
    acceptSample(std::move(picked), true);
    if (!colorReady || wizardStep() != 2 || !saveAll() || colorDirty() || settingsDirty()) {
        DestroyWindow(target);
        return false;
    }
    if (!feedbackTest()) {
        DestroyWindow(target);
        report << "FAIL: feedback states or presentation changed input\n";
        return false;
    }
    if (RegisterHotKey(window, hotkey::stop, MOD_NOREPEAT, VK_F9)) {
        autoClick = true;
        settings.dryRun = true;
        guidedAction();
        const bool first = autoClick && settings.dryRun;
        guidedAction();
        const bool repeated = autoClick && settings.dryRun;
        stop();
        if (!first || !repeated) {
            DestroyWindow(target);
            report << "FAIL: repeating diagnostic loses its own F9 registration\n";
            return false;
        }
    } else
        report << "SKIP: F9 already owned; repeat-button subcheck omitted\n";
    SetWindowTextW(profileName, L"Моя первая тренировка");
    saveProfile();
    if (profiles.size() != 1 || profileChanged()) {
        DestroyWindow(target);
        return false;
    }
    const auto savedPath = profiles.front().path;
    SetWindowTextW(settingEdits[5], L"oops");
    settings.advancedMode = false;
    layoutMode();
    const bool invalidRejected =
        !saveAll() && settings.advancedMode && GetFocus() == settingEdits[5] && !running && !autoClick;
    SetWindowTextW(settingEdits[5], L"70");
    saveAll();
    if (!invalidRejected) {
        DestroyWindow(target);
        report << "FAIL: invalid hidden field remains hidden/unfocused\n";
        return false;
    }
    for (bool dark : {false, true}) {
        settings.darkTheme = dark;
        applyTheme();
        for (int i = 0; i < 2; ++i) {
            command(control::toggleAdvanced);
            if (colorDirty() || settingsDirty()) {
                DestroyWindow(target);
                return false;
            }
        }
    }
    settings.advancedMode = false;
    layoutMode();
    std::string error;
    if (!persistSettings(error)) {
        DestroyWindow(target);
        return false;
    }
    stage("restart and profile drafts");
    const auto root = settingsPath.parent_path();
    PreviewTestHarness restarted(root);
    HWND freshWindow = CreateWindowW(L"TriggerLabCapturePreviewClass", L"First-run restart",
                                     WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 1120, 980, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), &restarted);
    bool restored = freshWindow && restarted.profiles.size() == 1 && !restarted.running &&
                    !restarted.autoClick && restarted.settings.dryRun && !restarted.verified;
    if (freshWindow) {
        SetWindowTextW(restarted.colorEdits[0], L"111");
        restarted.saveAll();
        restarted.loadProfile();
        restored = restored && restarted.color.target.red == 231 && restarted.settings.dwellMs == 70 &&
                   !restarted.profileChanged() && !restarted.colorDirty() && !restarted.settingsDirty() &&
                   restarted.settings.dryRun;
        SetWindowTextW(restarted.colorEdits[0], L"123");
        restarted.loadProfile();
        restored = restored && restarted.color.target.red == 231 && restarted.colorDirty();
        DestroyWindow(freshWindow);
    }
    DestroyWindow(target);
    targetWindow = nullptr;
    const bool passed = restored && std::filesystem::exists(savedPath);
    report
        << (passed
                ? "PASS: cold launch, explicit color, validation, save/restart/load, drafts and both themes\n"
                : "FAIL: restart/profile load\n");
    return passed;
}

bool PreviewTestHarness::feedbackTest() {
    // Isolated first-run settings and explicit focus/input observations keep
    // these presentation checks independent of desktop input and capture.
    const auto originalClicks = clickCount, originalDiagnostics = diagnosticCount;
    auto check = [&](FeedbackTone tone, const wchar_t *title, bool foreground = true, bool held = false) {
        const auto value = feedback(foreground, held);
        return value.tone == tone && value.title == title && !value.detail.empty() &&
               clickCount == originalClicks && diagnosticCount == originalDiagnostics && !syntheticDown;
    };
    if (!check(FeedbackTone::Neutral, L"Нужна проверка цвета"))
        return false;
    verified = true;
    if (!check(FeedbackTone::Good, L"Готово к запуску"))
        return false;
    actionError = L"F9 занят: повторите запуск";
    if (!check(FeedbackTone::Error, L"Действие не выполнено"))
        return false;
    validationMessage = L"Введите задержку от 1 до 2000 мс";
    if (!check(FeedbackTone::Error, L"Ошибка параметров") ||
        feedback(true, false).detail != validationMessage)
        return false;
    validationMessage.clear();
    actionError.clear();
    running = true;
    autoClick = true;
    settings.dryRun = true;
    if (!check(FeedbackTone::Waiting, L"Пауза: окно не активно", false))
        return false;
    sample = {};
    sample.status = triggerlab::CaptureStatus::CopyFailed;
    if (!check(FeedbackTone::Error, L"Нет свежего кадра"))
        return false;
    sample.status = triggerlab::CaptureStatus::CursorMoved;
    if (!check(FeedbackTone::Waiting, L"Нет свежего кадра"))
        return false;
    sample.status = triggerlab::CaptureStatus::Ok;
    sample.frame.emplace(settings.side, settings.side);
    sample.allBlack = true;
    if (!check(FeedbackTone::Waiting, L"Снимок чёрный"))
        return false;
    sample.allBlack = false;
    mask.found = false;
    if (!check(FeedbackTone::Waiting, L"Ожидание мишени"))
        return false;
    mask.matchingPixels = 100;
    mask.coverage = .5;
    if (detectionHint().find(L"центр ещё не на мишени") == std::wstring::npos)
        return false;
    mask.matchingPixels = 0;
    mask.coverage = 0;
    if (!check(FeedbackTone::Waiting, L"Пауза: удерживается ввод", true, true))
        return false;
    mask.found = true;
    gate.reset();
    if (!check(FeedbackTone::Waiting, L"Цвет найден: выдержка задержки"))
        return false;
    const auto start = triggerlab::Clock::now();
    for (int elapsed = 0; elapsed <= settings.dwellMs; ++elapsed)
        gate.step(true, true, start + std::chrono::milliseconds{elapsed});
    gate.acknowledgeClick(start + std::chrono::milliseconds{settings.dwellMs});
    if (!check(FeedbackTone::Good, L"Проверка пройдена"))
        return false;
    settings.dryRun = false;
    if (!check(FeedbackTone::Good, L"Клик завершён"))
        return false;
    autoClick = false;
    if (!check(FeedbackTone::Neutral, L"Захват без кликов"))
        return false;
    stop();
    verified = false;
    settings.dryRun = true;
    syncSettings();
    return check(FeedbackTone::Neutral, L"Нужна проверка цвета");
}

bool PreviewTestHarness::smokeTest(const std::filesystem::path &path) {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    std::ofstream report(std::filesystem::path(executable).parent_path() / L"smoke-test-report.txt");
    auto fail = [&](int line) {
        report << "FAIL: preview smoke at line " << line << '\n';
        return false;
    };
    if (lastPress || pressTimingText().find(L"ещё не измерено") == std::wstring::npos)
        return fail(__LINE__);
    lastPress = PressTiming{std::chrono::microseconds{12800}, 5, false, true};
    if (pressTimingText().find(L"12,8 мс (задано 5)") == std::wstring::npos)
        return fail(__LINE__);
    lastPress->interrupted = true;
    if (pressTimingText().find(L"прервано") == std::wstring::npos)
        return fail(__LINE__);
    lastPress->releaseAccepted = false;
    if (pressTimingText().find(L"ошибка отпускания") == std::wstring::npos)
        return fail(__LINE__);
    lastPress.reset();
    if (!testMessagePumpYields()) {
        report << "FAIL: message pump did not yield with a continuously replenished queue\n";
        return false;
    }
    report << "PASS: message pump yields with a continuously replenished queue\n";
    if (!triggerlab::imagePaletteDialogSmoke())
        return fail(__LINE__);
    colorPath = path;
    settingsPath = path.parent_path() / L"analyzer-settings.json";
    const bool originalTheme = settings.darkTheme;
    command(control::toggleTheme);
    if (settings.darkTheme == originalTheme ||
        triggerlab::loadAnalyzerSettings(settingsPath).darkTheme != settings.darkTheme || running ||
        autoClick)
        return fail(__LINE__);
    command(control::toggleTheme);
    if (settings.darkTheme != originalTheme ||
        triggerlab::loadAnalyzerSettings(settingsPath).darkTheme != originalTheme)
        return fail(__LINE__);
    const wchar_t *analyzerValues[] = {L"41", L"4", L"5", L"-20", L"10", L"100", L"80", L"40"};
    for (int i = 0; i < 8; ++i)
        SetWindowTextW(settingEdits[i], analyzerValues[i]);
    if (!applySettings())
        return fail(__LINE__);
    const auto loadedSettings = triggerlab::loadAnalyzerSettings(settingsPath);
    if (loadedSettings.side != 41 || loadedSettings.offsetX != -20 || loadedSettings.dwellMs != 100 ||
        loadedSettings.cooldownMs != 80)
        return fail(__LINE__);
    SetWindowTextW(settingEdits[0], L"40");
    if (applySettings() || settings.side != 41)
        return fail(__LINE__);
    settings = {};
    syncSettings();
    gate.setTiming(std::chrono::milliseconds{70}, std::chrono::milliseconds{50});
    for (auto edit : colorEdits)
        if (!IsWindow(edit))
            return fail(__LINE__);
    const wchar_t *values[] = {L"231", L"67", L"19", L"12"};
    for (int i = 0; i < 4; ++i)
        SetWindowTextW(colorEdits[i], values[i]);
    command(control::applyColor);
    command(control::saveColor);
    const auto loaded = triggerlab::loadColorProfile(path);
    if (!loaded.ok || loaded.color.target.red != 231 || loaded.color.target.blue != 19 ||
        loaded.color.channelTolerance != 12)
        return fail(__LINE__);
    SetWindowTextW(colorEdits[0], L"999");
    command(control::saveColor);
    if (triggerlab::loadColorProfile(path).color.target.red != 231)
        return fail(__LINE__);
    SendMessageW(autoControl, BM_SETCHECK, BST_CHECKED, 0);
    command(control::enableTrigger);
    if (autoClick || SendMessageW(autoControl, BM_GETCHECK, 0, 0) != BST_UNCHECKED)
        return fail(__LINE__);
    command(control::startCapture);
    if (!running)
        return fail(__LINE__);
    command(control::stop);
    // F9 keeps evidence of a failed run; a second Stop preserves it too.
    command(control::startCapture);
    runFrames = 12;
    runAlignedFrames = 2;
    runUnavailableFrames = 3;
    runBestCoverage = .75;
    runLastIssue = L"Область прицела перекрыта другим окном";
    command(control::stop);
    const auto stoppedResult = clickStatus;
    if (stoppedResult.find(L"Кадры: 12; в центре: 2; недоступны: 3; макс. цвет: 75%") != 0 ||
        stoppedResult.find(runLastIssue) == std::wstring::npos || running || autoClick || sample.frame)
        return fail(__LINE__);
    command(control::stop);
    if (clickStatus != stoppedResult)
        return fail(__LINE__);
    command(control::startCapture);
    if (runFrames || runAlignedFrames || runUnavailableFrames || runBestCoverage || !runLastIssue.empty())
        return fail(__LINE__);
    command(control::stop);
    // Exercise one-shot sampling with a known pixel, independently of Start
    // and of the desktop content / user's cursor. No OS input is sent.
    triggerlab::CaptureSample pixel;
    pixel.status = triggerlab::CaptureStatus::Ok;
    pixel.frame.emplace(1, 1);
    pixel.region = {0, 0, 1, 1};
    pixel.cursorPosition = triggerlab::CapturePoint{0, 0};
    pixel.cursorPixel = triggerlab::Pixel{10, 20, 30, 255};
    pixel.frame->at(0, 0) = *pixel.cursorPixel;
    color.space = triggerlab::ColorSpace::HSV;
    acceptSample(std::move(pixel), true);
    if (running || autoClick || color.target.red != 30 || color.target.blue != 10 ||
        color.space != triggerlab::ColorSpace::RGB)
        return fail(__LINE__);
    wchar_t field[32]{};
    GetWindowTextW(colorEdits[0], field, 32);
    if (std::wstring(field) != L"30")
        return fail(__LINE__);
    // Failed sampling clears the preview and preserves the previous color.
    acceptSample({}, true);
    if (color.target.red != 30 || sample.frame || sample.cursorPixel || !mask.pixels.empty())
        return fail(__LINE__);
    if (!displayFrame || displayFrame->at(0, 0).red != 30)
        return fail(__LINE__); // Stale display never becomes an input sample.
    // Deadlines are injected to test cancellation even if another process owns F9.
    pickDeadline = GetTickCount64() + 3000;
    selectionTick(pickDeadline - 1001);
    GetWindowTextW(GetDlgItem(window, control::pickCursorColor), field, 32);
    if (std::wstring(field) != L"Пипетка: 2 сек.")
        return fail(__LINE__);
    command(control::saveColor); // Saving cancels delayed overwrite.
    if (pickDeadline || targetDeadline || !triggerlab::loadColorProfile(path).ok)
        return fail(__LINE__);
    pickDeadline = GetTickCount64() + 3000;
    targetDeadline = pickDeadline;
    command(control::stop);
    if (pickDeadline || targetDeadline)
        return fail(__LINE__);
    // The real button can start while stopped, or fail cleanly if F9 is busy.
    command(control::pickCursorColor);
    if (running || autoClick || targetDeadline)
        return fail(__LINE__);
    if (!pickDeadline && status != L"F9 занят: выбор не начат")
        return fail(__LINE__);
    command(control::stop);
    // Decode a real file through the same import action used by the dialog.
    const auto imagePath = path.parent_path() / L"цвет.bmp";
    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + 12;
    BITMAPINFOHEADER imageHeader{};
    imageHeader.biSize = sizeof(imageHeader);
    imageHeader.biWidth = 3;
    imageHeader.biHeight = 1;
    imageHeader.biPlanes = 1;
    imageHeader.biBitCount = 24;
    imageHeader.biSizeImage = 12;
    const unsigned char imagePixels[] = {19, 67, 231, 19, 67, 231, 255, 0, 0, 0, 0, 0};
    {
        std::ofstream out(imagePath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));
        out.write(reinterpret_cast<const char *>(&imageHeader), sizeof(imageHeader));
        out.write(reinterpret_cast<const char *>(imagePixels), sizeof(imagePixels));
        if (!out)
            return fail(__LINE__);
    }
    for (bool advanced : {false, true}) {
        settings.advancedMode = advanced;
        layoutMode();
        pickDeadline = GetTickCount64() + 3000;
        if (!importImage(imagePath) || pickDeadline || running || autoClick || color.target.red != 231 ||
            color.target.blue != 19)
            return fail(__LINE__);
        wchar_t saveCaption[64]{};
        const auto saveButton = GetDlgItem(window, control::saveAll);
        GetWindowTextW(saveButton, saveCaption, 64);
        // The smoke-test owner is hidden; check the control's own visibility flag.
        if (!(GetWindowLongW(saveButton, GWL_STYLE) & WS_VISIBLE) || !saveCaption[0] ||
            status.find(L"«" + std::wstring(saveCaption) + L"»") == std::wstring::npos) {
            report << "FAIL: imported color suggests an unavailable save action\n";
            return fail(__LINE__);
        }
    }
    settings.advancedMode = false;
    layoutMode();
    command(control::saveColor);
    if (triggerlab::loadColorProfile(path).color.target.red != 231)
        return fail(__LINE__);
    if (importImage(path) || color.target.red != 231 || autoClick)
        return fail(__LINE__); // JSON is not an image.
    HWND test = CreateWindowW(L"STATIC", L"Test window title", WS_OVERLAPPED, 0, 0, 240, 240, nullptr,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!test)
        return fail(__LINE__);
    targetWindow = test;
    GetWindowThreadProcessId(test, &targetProcess);
    refreshTargetButton();
    const auto referenceColor = color.target;
    auto reference = [&](triggerlab::Pixel p) {
        triggerlab::CaptureSample value;
        value.status = triggerlab::CaptureStatus::Ok;
        value.frame.emplace(settings.side, settings.side);
        for (int y = 0; y < settings.side; ++y)
            for (int x = 0; x < settings.side; ++x)
                value.frame->at(x, y) = p;
        return value;
    };
    if (!recordReference(reference(color.target), SnapshotPurpose::Target) || autoClick ||
        color.target.red != referenceColor.red) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    if (!recordReference(reference({0, 255, 0, 255}), SnapshotPurpose::Background)) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    if (!referencesCurrent() || !targetReference || !backgroundReference ||
        recordReference({}, SnapshotPurpose::Target)) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    auto rejected = reference({0, 0, 0, 255});
    rejected.allBlack = true;
    if (recordReference(std::move(rejected), SnapshotPurpose::Target) ||
        targetReference->at(0, 0).red != referenceColor.red) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    rejected = reference({0, 255, 0, 255});
    rejected.frame.emplace(1, 1);
    if (recordReference(std::move(rejected), SnapshotPurpose::Target) ||
        targetReference->width() != settings.side) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    const auto checked =
        triggerlab::compareColorSamples(&*targetReference, &*backgroundReference, color, settings);
    if (!checked.targetFound || checked.backgroundFound || !targetReference) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    sample = reference({0, 255, 0, 255});
    mask = triggerlab::aimColorMask(*sample.frame, color, settings);
    if (detectionHint().find(L"Цвет не найден") == std::wstring::npos) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    sample.frame->at(settings.side / 2, settings.side / 2) = color.target;
    mask = triggerlab::aimColorMask(*sample.frame, color, settings);
    if (detectionHint().find(L"перекрестием") == std::wstring::npos) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    sample.frame->at(0, 0) = color.target;
    mask = triggerlab::aimColorMask(*sample.frame, color, settings);
    if (detectionHint().find(L"нужно") == std::wstring::npos) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    sample = reference(color.target);
    mask = triggerlab::aimColorMask(*sample.frame, color, settings);
    if (detectionHint().find(L"триггер выключен") == std::wstring::npos) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    autoClick = true;
    syntheticDown = true;
    const bool pendingHint = detectionHint().find(L"ожидаем отпускание") != std::wstring::npos;
    syntheticDown = false;
    autoClick = false;
    if (!pendingHint) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    sample = {};
    sample.status = triggerlab::CaptureStatus::CopyFailed;
    if (detectionHint().find(L"Windows") == std::wstring::npos) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    mask = {};
    snapshotPurpose = SnapshotPurpose::Target;
    aimPickDeadline = GetTickCount64() + 3000;
    SendMessageW(window, WM_HOTKEY, 9, 0);
    if (aimPickDeadline || snapshotPurpose != SnapshotPurpose::Color || autoClick || !targetReference) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    const auto previousColor = color.target;
    command(control::pickAimColor);
    if (aimPickDeadline || autoClick || status != L"Выбранное окно недоступно для активации") {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    cancelSelection();
    if (aimPickDeadline || color.target.red != previousColor.red ||
        color.target.green != previousColor.green || color.target.blue != previousColor.blue) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    settings.center = false;
    command(control::pickAimColor);
    if (aimPickDeadline) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    settings.center = true;
    colorReady = false;
    verified = false;
    if (wizardStep() != 1) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    colorReady = true;
    if (wizardStep() != 2) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    verified = true;
    if (wizardStep() != 3) {
        DestroyWindow(test);
        return fail(__LINE__);
    }
    wchar_t caption[256]{};
    GetWindowTextW(GetDlgItem(window, control::chooseWindow), caption, 256);
    bool labels = std::wstring(caption).find(L"Test window title") != std::wstring::npos;
    SetWindowTextW(test, L"Renamed target");
    refreshTargetButton();
    GetWindowTextW(GetDlgItem(window, control::chooseWindow), caption, 256);
    labels = labels && std::wstring(caption).find(L"Renamed target") != std::wstring::npos;
    DestroyWindow(test);
    refreshTargetButton();
    GetWindowTextW(GetDlgItem(window, control::chooseWindow), caption, 256);
    labels = labels && !targetWindow && std::wstring(caption).find(L"не выбрано") != std::wstring::npos;
    command(control::pickAimColor); // A closed target cannot start a delayed snapshot.
    if (!labels || running || autoClick || sample.frame || pickDeadline || targetDeadline ||
        aimPickDeadline || wizardStep() != 0)
        return fail(__LINE__);
    SetWindowTextW(colorEdits[0], L"222");
    if (!colorDirty() || verified || !saveAll() || colorDirty() || settingsDirty())
        return fail(__LINE__);
    const auto rgbBefore = color;
    SendMessageW(window, WM_KEYDOWN, 'H', 0);
    if (color.space != triggerlab::ColorSpace::HSV || !colorDirty() || verified)
        return fail(__LINE__);
    color.hueTolerance = 17.5;
    color.saturationTolerance = .123456789;
    color.valueTolerance = .23456789;
    color.minimumCoverage = .27;
    if (!saveAll() || colorDirty())
        return fail(__LINE__);
    {
        PreviewTestHarness restarted(settingsPath.parent_path());
        if (!sameColor(restarted.color, color) || !restarted.settings.dryRun)
            return fail(__LINE__);
    }
    color = rgbBefore;
    syncColor();
    if (!saveAll())
        return fail(__LINE__);
    SetWindowTextW(profileName, L"Проверка профиля");
    saveProfile();
    if (profiles.size() != 1 || profiles[0].name.empty() || profileChanged())
        return fail(__LINE__);
    SetWindowTextW(colorEdits[0], L"111");
    if (!saveAll())
        return fail(__LINE__);
    if (!profileChanged())
        return fail(__LINE__);
    SetWindowTextW(colorEdits[0], L"123");
    loadProfile(); // Unsaved drafts block replacement.
    if (color.target.red != 111 || !colorDirty())
        return fail(__LINE__);
    SetWindowTextW(colorEdits[0], L"111");
    verified = true;
    loadProfile();
    if (color.target.red != 222 || verified || !settings.dryRun || running || autoClick || colorDirty() ||
        settingsDirty() || profileChanged())
        return fail(__LINE__);
    const bool originalAdvanced = settings.advancedMode;
    command(control::toggleAdvanced);
    if (settings.advancedMode == originalAdvanced ||
        triggerlab::loadAnalyzerSettings(settingsPath).advancedMode != settings.advancedMode)
        return fail(__LINE__);
    command(control::toggleAdvanced);
    return settings.advancedMode == originalAdvanced && !running && !autoClick;
}
} // namespace triggerlab::win32
