#include "trainer/TrainerWindow.hpp"
#include <cmath>
#include <fstream>
#include <sstream>

namespace triggerlab::win32 {
void TrainerWindow::createControls() {
    const wchar_t *labels[] = {L"Cooldown ms [50..2000]", L"ROI radius [0..30]",   L"Stable frames [1..20]",
                               L"Coverage [0.01..1]",     L"Minimum red [0..255]", L"Minimum blue [0..255]",
                               L"Maximum green [0..255]"};
    for (int i = 0; i < 7; ++i) {
        labelControls_[i] = CreateWindowW(L"STATIC", labels[i], WS_CHILD | WS_VISIBLE, 976, 55 + i * 36, 250,
                                          20, window_, nullptr, nullptr, nullptr);
        edits_[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 976, 73 + i * 36,
                                    240, 24, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(100 + i)),
                                    nullptr, nullptr);
    }
    CreateWindowW(L"BUTTON", L"Apply and save", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 976, 340, 240, 30,
                  window_, reinterpret_cast<HMENU>(201), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Reset statistics / scene", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 976, 375, 240,
                  30, window_, reinterpret_cast<HMENU>(202), nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Scene / speed [0.25..3]", WS_CHILD | WS_VISIBLE, 976, 418, 240, 20, window_,
                  reinterpret_cast<HMENU>(207), nullptr, nullptr);
    sceneControl_ =
        CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 976, 442, 240,
                      180, window_, reinterpret_cast<HMENU>(203), nullptr, nullptr);
    for (auto label : {L"Crossing (classic)", L"Multiple targets", L"Precision (small target)"})
        SendMessageW(sceneControl_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    speedControl_ =
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        976, 478, 240, 24, window_, nullptr, nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Apply scene and save", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 976, 510, 240, 30,
                  window_, reinterpret_cast<HMENU>(204), nullptr, nullptr);
    pauseControl_ = CreateWindowW(L"BUTTON", L"Pause scene (P)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 976, 550,
                                  240, 30, window_, reinterpret_cast<HMENU>(205), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Export event log (CSV)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 976, 590, 240, 30,
                  window_, reinterpret_cast<HMENU>(206), nullptr, nullptr);
    languageLabel_ = CreateWindowW(L"STATIC", L"Language / Язык", WS_CHILD | WS_VISIBLE, 976, 634, 240, 20,
                                   window_, nullptr, nullptr, nullptr);
    languageControl_ =
        CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 976, 658, 240,
                      100, window_, reinterpret_cast<HMENU>(208), nullptr, nullptr);
    SendMessageW(languageControl_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
    SendMessageW(languageControl_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Русский"));
    EnumChildWindows(
        window_,
        [](HWND child, LPARAM context) -> BOOL {
            auto &app = *reinterpret_cast<TrainerWindow *>(context);
            if (GetParent(child) != app.window_)
                return TRUE;
            RECT rect{};
            GetWindowRect(child, &rect);
            MapWindowPoints(nullptr, app.window_, reinterpret_cast<POINT *>(&rect), 2);
            if (child == app.sceneControl_)
                rect.bottom = rect.top + 180;
            if (child == app.languageControl_)
                rect.bottom = rect.top + 100;
            app.controlRects_.push_back({child, rect});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    paintFont_ =
        CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    setDpi(GetDpiForWindow(window_));
    syncControls();
    updateLanguage();
}

std::wstring TrainerWindow::tr(std::wstring_view key) const {
    return std::wstring(triggerlab::translate(language_, key));
}

void TrainerWindow::updateLanguage() {
    const wchar_t *labels[] = {L"Cooldown ms [50..2000]", L"ROI radius [0..30]",   L"Stable frames [1..20]",
                               L"Coverage [0.01..1]",     L"Minimum red [0..255]", L"Minimum blue [0..255]",
                               L"Maximum green [0..255]"};
    for (int i = 0; i < 7; ++i)
        SetWindowTextW(labelControls_[i], tr(labels[i]).c_str());
    const wchar_t *buttons[] = {L"Apply and save",
                                L"Reset statistics / scene",
                                L"",
                                L"Apply scene and save",
                                paused_ ? L"Resume scene (P)" : L"Pause scene (P)",
                                L"Export event log (CSV)",
                                L"Scene / speed [0.25..3]"};
    for (int id = 201; id <= 207; ++id)
        if (id != 203)
            SetWindowTextW(GetDlgItem(window_, id), tr(buttons[id - 201]).c_str());
    const auto selected = SendMessageW(sceneControl_, CB_GETCURSEL, 0, 0);
    SendMessageW(sceneControl_, CB_RESETCONTENT, 0, 0);
    for (auto label : {L"Crossing (classic)", L"Multiple targets", L"Precision (small target)"})
        SendMessageW(sceneControl_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tr(label).c_str()));
    SendMessageW(sceneControl_, CB_SETCURSEL, selected, 0);
    SetWindowTextW(languageLabel_, tr(L"Language / Язык").c_str());
    SendMessageW(languageControl_, CB_SETCURSEL, static_cast<WPARAM>(language_), 0);
    SetWindowTextW(window_, tr(L"TriggerLab - Local Practice Sandbox").c_str());
    InvalidateRect(window_, nullptr, FALSE);
}

void TrainerWindow::syncControls() {
    const auto e = engine_.config();
    const auto d = detector_.config();
    const std::array<std::wstring, 7> values{
        std::to_wstring(e.cooldown.count()), std::to_wstring(d.roiRadius),  std::to_wstring(e.stableFrames),
        formatNumber(d.minimumCoverage),     std::to_wstring(d.minimumRed), std::to_wstring(d.minimumBlue),
        std::to_wstring(d.maximumGreen)};
    for (int i = 0; i < 7; ++i)
        if (edits_[i])
            SetWindowTextW(edits_[i], values[i].c_str());
    SendMessageW(sceneControl_, CB_SETCURSEL, static_cast<WPARAM>(simulation_.config().profile), 0);
    SetWindowTextW(speedControl_, formatNumber(simulation_.config().speed).c_str());
}

void TrainerWindow::save() {
    std::string error;
    if (triggerlab::saveSettings(
            configPath_, {engine_.config(), detector_.config(), simulation_.config(), language_}, error))
        configStatus_ = L"Settings saved";
    else
        configStatus_ = L"Save failed";
}

void TrainerWindow::command(int id, int notification) {
    if (id == 208) {
        if (notification != CBN_SELCHANGE)
            return;
        const auto selected = SendMessageW(languageControl_, CB_GETCURSEL, 0, 0);
        if (selected < 0 || selected > 1)
            return;
        language_ = static_cast<triggerlab::Language>(selected);
        updateLanguage();
        save();
        SetFocus(window_);
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (id == 204) {
        wchar_t value[128]{};
        GetWindowTextW(speedControl_, value, 128);
        std::wistringstream input(value);
        input.imbue(std::locale::classic());
        double speed = 0;
        const auto selected = SendMessageW(sceneControl_, CB_GETCURSEL, 0, 0);
        if (input >> speed && (input >> std::ws).eof() && std::isfinite(speed) && speed >= .25 &&
            speed <= 3 && selected >= 0 && selected <= 2) {
            simulation_.setConfig({static_cast<triggerlab::SceneProfile>(selected), speed});
            resetSession();
            save();
            syncControls();
        } else
            configStatus_ = L"Invalid scene settings: not changed";
    } else if (id == 205)
        togglePause();
    else if (id == 206)
        exportLog();
    else if (id != 201 && id != 202)
        return;
    if (id == 202)
        onKeyDown('R', 0);
    if (id == 201) {
        const char *keys[] = {"cooldown_ms", "roi_radius",   "stable_frames", "minimum_coverage",
                              "minimum_red", "minimum_blue", "maximum_green"};
        std::string json = "{";
        for (int i = 0; i < 7; ++i) {
            wchar_t value[128]{};
            GetWindowTextW(edits_[i], value, 128);
            if (i)
                json += ",";
            json += "\"" + std::string(keys[i]) + "\":";
            for (wchar_t c : std::wstring(value))
                json += c < 128 ? static_cast<char>(c) : '?';
        }
        json += "}";
        const auto parsed = triggerlab::deserializeSettings(json);
        if (parsed.ok) {
            engine_.setConfig(parsed.settings.engine);
            detector_.setConfig(parsed.settings.detector);
            save();
            syncControls();
        } else
            configStatus_ = L"Invalid values: settings were not changed";
    }
    SetFocus(window_);
    InvalidateRect(window_, nullptr, FALSE);
}

void TrainerWindow::exportLog() {
    try {
        const auto path = configPath_.parent_path() / L"events.csv";
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << diagnostics_.csv();
        out.close();
        configStatus_ = out ? L"Event log saved beside settings: events.csv" : L"Event log export failed";
    } catch (const std::exception &) {
        configStatus_ = L"Event log export failed";
    }
}
} // namespace triggerlab::win32
