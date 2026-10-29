#include "preview/PreviewWindow.hpp"
#include "triggerlab/InputAuthorization.hpp"
#include <algorithm>
#include <charconv>
#include <commdlg.h>
#include <cwctype>

namespace triggerlab::win32 {

bool PreviewWindow::inputAuthorized() const {
    return triggerlab::InputAuthorization{colorReady, verified,
                                          !paramsOnDisk || !colorOnDisk || !settingsLoadWarning.empty() ||
                                              settingsDirty() || colorDirty()}
        .allows(settings.dryRun);
}

bool PreviewWindow::persistSettings(std::string &error) {
    const bool ok = triggerlab::saveAnalyzerSettings(settingsPath, settings, error);
    if (ok) {
        savedSettings = settings;
        paramsOnDisk = true;
    }
    return ok;
}

bool PreviewWindow::fieldIs(HWND field, int value) {
    char text[32]{};
    GetWindowTextA(field, text, 32);
    int actual = 0;
    const auto end = text + std::char_traits<char>::length(text);
    const auto parsed = std::from_chars(text, end, actual);
    return parsed.ec == std::errc{} && parsed.ptr == end && actual == value;
}

bool PreviewWindow::settingsDirty() const {
    const int values[] = {savedSettings.side,       savedSettings.ignoreRadius, savedSettings.minimumPercent,
                          savedSettings.offsetX,    savedSettings.offsetY,      savedSettings.dwellMs,
                          savedSettings.cooldownMs, savedSettings.clickMs};
    for (int i = 0; i < 8; ++i)
        if (!fieldIs(settingEdits[i], values[i]))
            return true;
    return settings.center != savedSettings.center || settings.darkTheme != savedSettings.darkTheme ||
           settings.advancedMode != savedSettings.advancedMode;
}

bool PreviewWindow::sameColor(const triggerlab::ColorMatchConfig &a,
                              const triggerlab::ColorMatchConfig &b) noexcept {
    return a.space == b.space && a.target.red == b.target.red && a.target.green == b.target.green &&
           a.target.blue == b.target.blue && a.channelTolerance == b.channelTolerance &&
           a.hueTolerance == b.hueTolerance && a.saturationTolerance == b.saturationTolerance &&
           a.valueTolerance == b.valueTolerance && a.minimumCoverage == b.minimumCoverage;
}

bool PreviewWindow::colorDirty() const {
    const int values[] = {savedColor.target.red, savedColor.target.green, savedColor.target.blue,
                          savedColor.channelTolerance};
    for (int i = 0; i < 4; ++i)
        if (!fieldIs(colorEdits[i], values[i]))
            return true;
    return !sameColor(color, savedColor);
}

bool PreviewWindow::profileChanged() const {
    if (!profileBaseline)
        return false;
    const auto &s = profileBaseline->settings;
    const auto &c = profileBaseline->color;
    const int parameters[] = {s.side,    s.ignoreRadius, s.minimumPercent, s.offsetX,
                              s.offsetY, s.dwellMs,      s.cooldownMs,     s.clickMs};
    const int rgb[] = {c.target.red, c.target.green, c.target.blue, c.channelTolerance};
    for (int i = 0; i < 8; ++i)
        if (!fieldIs(settingEdits[i], parameters[i]))
            return true;
    for (int i = 0; i < 4; ++i)
        if (!fieldIs(colorEdits[i], rgb[i]))
            return true;
    return settings.center != s.center || !sameColor(color, c);
}

bool PreviewWindow::readDraft(triggerlab::AnalyzerSettings &next, triggerlab::ColorMatchConfig &nextColor) {
    next = settings;
    nextColor = color;
    int values[8]{}, rgb[4]{};
    auto invalid = [&](const std::wstring &message, HWND field, bool advanced = true) {
        status = notice = validationMessage = message;
        if (advanced) {
            settings.advancedMode = true;
            profilePane = false;
            layoutMode();
        }
        SetFocus(field);
        SendMessageW(field, EM_SETSEL, 0, -1);
        InvalidateRect(window, nullptr, FALSE);
        return false;
    };
    auto read = [&](HWND field, int &value) {
        char text[32]{};
        GetWindowTextA(field, text, 32);
        const auto end = text + std::char_traits<char>::length(text);
        const auto p = std::from_chars(text, end, value);
        return p.ec == std::errc{} && p.ptr == end;
    };
    const wchar_t *labels[] = {L"Размер области", L"Исключение центра", L"Порог",    L"Сдвиг X",
                               L"Сдвиг Y",        L"Задержка",          L"Интервал", L"Нажатие"};
    for (int i = 0; i < 8; ++i)
        if (!read(settingEdits[i], values[i]))
            return invalid(std::wstring(labels[i]) + L": введите целое число", settingEdits[i]);
    next.side = values[0];
    next.ignoreRadius = values[1];
    next.minimumPercent = values[2];
    next.offsetX = values[3];
    next.offsetY = values[4];
    next.dwellMs = values[5];
    next.cooldownMs = values[6];
    next.clickMs = values[7];
    if (next.side < 5 || next.side > 101 || next.side % 2 == 0)
        return invalid(L"Размер области: нечётное число от 5 до 101", settingEdits[0]);
    if (next.ignoreRadius < 0 || next.ignoreRadius >= next.side / 2)
        return invalid(L"Исключение центра: от 0 до " + std::to_wstring(next.side / 2 - 1), settingEdits[1]);
    if (next.minimumPercent < 1 || next.minimumPercent > 100)
        return invalid(L"Порог: от 1 до 100 процентов", settingEdits[2]);
    for (int i = 3; i <= 4; ++i)
        if (values[i] < -2000 || values[i] > 2000)
            return invalid(std::wstring(labels[i]) + L": от −2000 до 2000 пикселей", settingEdits[i]);
    if (next.dwellMs < 1 || next.dwellMs > 2000)
        return invalid(L"Задержка: от 1 до 2000 мс", settingEdits[5]);
    if (next.cooldownMs < 50 || next.cooldownMs > 2000)
        return invalid(L"Интервал: от 50 до 2000 мс", settingEdits[6]);
    if (next.clickMs < 5 || next.clickMs > 150)
        return invalid(L"Нажатие: от 5 до 150 мс", settingEdits[7]);
    const wchar_t *channels[] = {L"Красный (R)", L"Зелёный (G)", L"Синий (B)", L"Допуск"};
    for (int i = 0; i < 4; ++i)
        if (!read(colorEdits[i], rgb[i]) || rgb[i] < 0 || rgb[i] > 255)
            return invalid(std::wstring(channels[i]) + L": целое число от 0 до 255", colorEdits[i], i != 3);
    if (rgb[0] != color.target.red || rgb[1] != color.target.green || rgb[2] != color.target.blue)
        nextColor.space = triggerlab::ColorSpace::RGB;
    nextColor.target = {static_cast<std::uint8_t>(rgb[2]), static_cast<std::uint8_t>(rgb[1]),
                        static_cast<std::uint8_t>(rgb[0]), 255};
    nextColor.channelTolerance = rgb[3];
    validationMessage.clear();
    return true;
}

bool PreviewWindow::chosenColor(const triggerlab::ColorMatchConfig &draft) const {
    return colorReady || draft.target.red != color.target.red || draft.target.green != color.target.green ||
           draft.target.blue != color.target.blue;
}

bool PreviewWindow::saveAll() {
    actionError.clear();
    triggerlab::AnalyzerSettings next;
    triggerlab::ColorMatchConfig nextColor;
    if (!readDraft(next, nextColor)) {
        notice = status;
        return false;
    }
    cancelSelection();
    disableClicks();
    if (settingsDirty() || colorDirty())
        verified = false;
    colorReady = chosenColor(nextColor);
    settings = next;
    color = nextColor;
    gate.setTiming(std::chrono::milliseconds{settings.dwellMs},
                   std::chrono::milliseconds{settings.cooldownMs});
    sample = {};
    mask = {};
    displayFrame.reset();
    displayMask = {};
    analysisPoint.reset();
    std::string error;
    const bool savedParams = persistSettings(error),
               savedRgb = !colorReady || triggerlab::saveColorProfile(colorPath, color, error);
    if (savedParams)
        settingsLoadWarning.clear();
    if (savedRgb && colorReady) {
        savedColor = color;
        colorOnDisk = true;
    }
    status = notice =
        savedParams && savedRgb
            ? (colorReady ? L"Все настройки сохранены" : L"Параметры сохранены. Теперь выберите цвет мишени")
        : !savedParams && !savedRgb ? L"Не удалось сохранить настройки"
        : savedParams ? L"Параметры сохранены; цвет сохранить не удалось"
                      : L"Не удалось сохранить параметры";
    if (!savedParams || !savedRgb)
        actionError = status + L". Проверьте доступ к папке настроек и повторите сохранение.";
    syncActions();
    InvalidateRect(window, nullptr, FALSE);
    return savedParams && savedRgb;
}

void PreviewWindow::onEditChange(HWND field) {
    if (!uiReady || uiSync)
        return;
    const bool parameter = std::find(settingEdits.begin(), settingEdits.end(), field) != settingEdits.end();
    const bool rgb = std::find(colorEdits.begin(), colorEdits.end(), field) != colorEdits.end();
    if (!parameter && !rgb)
        return;
    validationMessage.clear();
    disableClicks();
    verified = false;
    notice = L"Есть несохранённые изменения. Нажмите «Сохранить всё»";
    syncActions();
    InvalidateRect(window, nullptr, FALSE);
}

void PreviewWindow::refreshProfiles() {
    profiles = triggerlab::listTrainingProfiles(settingsPath.parent_path() / L"profiles");
    SendMessageW(profileChoice, CB_RESETCONTENT, 0, 0);
    for (const auto &item : profiles) {
        const auto name = wide(item.name);
        SendMessageW(profileChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    if (!profiles.empty())
        SendMessageW(profileChoice, CB_SETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(window, control::loadProfile), !profiles.empty());
}

void PreviewWindow::saveProfile() {
    triggerlab::AnalyzerSettings next;
    triggerlab::ColorMatchConfig nextColor;
    if (!readDraft(next, nextColor)) {
        notice = status;
        return;
    }
    if (!chosenColor(nextColor)) {
        notice = L"Сначала выберите цвет мишени, затем сохраните профиль";
        return;
    }
    wchar_t name[128]{};
    GetWindowTextW(profileName, name, 128);
    std::wstring label(name);
    while (!label.empty() && std::iswspace(label.front()))
        label.erase(label.begin());
    while (!label.empty() && std::iswspace(label.back()))
        label.pop_back();
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, label.data(),
                                           static_cast<int>(label.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(length, '\0');
    if (length)
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, label.data(), static_cast<int>(label.size()),
                            utf8.data(), length, nullptr, nullptr);
    if (utf8.empty() || utf8.size() > 120) {
        notice = L"Введите название профиля: до 60 русских букв";
        return;
    }
    cancelSelection();
    disableClicks();
    std::string error;
    if (!triggerlab::saveTrainingProfile(settingsPath.parent_path() / L"profiles", {utf8, next, nextColor},
                                         error)) {
        notice = L"Профиль не сохранён. Проверьте название и доступ к папке";
        return;
    }
    selectedProfile = label;
    profileBaseline = triggerlab::TrainingProfile{utf8, next, nextColor};
    refreshProfiles();
    for (std::size_t i = 0; i < profiles.size(); ++i)
        if (profiles[i].name == utf8)
            SendMessageW(profileChoice, CB_SETCURSEL, i, 0);
    const bool saved = saveAll();
    if (saved)
        notice = L"Профиль «" + label + L"» и настройки сохранены";
}

void PreviewWindow::loadProfile() {
    const auto selected = SendMessageW(profileChoice, CB_GETCURSEL, 0, 0);
    if (selected < 0 || static_cast<std::size_t>(selected) >= profiles.size()) {
        notice = L"Выберите сохранённый профиль";
        return;
    }
    const auto profile = triggerlab::loadTrainingProfile(profiles[selected].path);
    if (!profile) {
        notice = L"Профиль повреждён; текущие настройки сохранены";
        return;
    }
    if (settingsDirty() || colorDirty()) {
        notice = L"Сначала сохраните текущие изменения кнопкой «Сохранить всё»";
        return;
    }
    stop();
    const bool dark = settings.darkTheme, advanced = settings.advancedMode;
    settings = profile->settings;
    settings.darkTheme = dark;
    settings.advancedMode = advanced;
    settings.dryRun = true;
    color = profile->color;
    selectedProfile = wide(profile->name);
    profileBaseline = *profile;
    SetWindowTextW(profileName, selectedProfile.c_str());
    syncSettings();
    syncColor();
    verified = false;
    colorReady = true;
    gate.setTiming(std::chrono::milliseconds{settings.dwellMs},
                   std::chrono::milliseconds{settings.cooldownMs});
    const bool saved = saveAll();
    if (saved)
        notice = L"Профиль «" + selectedProfile + L"» загружен. Начните проверку без кликов";
}

void PreviewWindow::syncSettings() {
    const bool wasSync = uiSync;
    uiSync = true;
    const int values[] = {settings.side,    settings.ignoreRadius, settings.minimumPercent, settings.offsetX,
                          settings.offsetY, settings.dwellMs,      settings.cooldownMs,     settings.clickMs};
    for (int i = 0; i < 8; ++i)
        SetWindowTextW(settingEdits[i], std::to_wstring(values[i]).c_str());
    SendMessageW(modeControl, CB_SETCURSEL, settings.center ? 0 : 1, 0);
    SendMessageW(dryControl, BM_SETCHECK, settings.dryRun ? BST_CHECKED : BST_UNCHECKED, 0);
    uiSync = wasSync;
}

bool PreviewWindow::applySettings() {
    cancelSelection();
    disableClicks();
    int values[8]{};
    for (int i = 0; i < 8; ++i) {
        char text[32]{};
        GetWindowTextA(settingEdits[i], text, 32);
        const std::string value(text);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), values[i]);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
            status = L"Параметры: введите целые числа";
            return false;
        }
    }
    auto next = settings;
    next.side = values[0];
    next.ignoreRadius = values[1];
    next.minimumPercent = values[2];
    next.offsetX = values[3];
    next.offsetY = values[4];
    next.dwellMs = values[5];
    next.cooldownMs = values[6];
    next.clickMs = values[7];
    if (!triggerlab::validAnalyzerSettings(next)) {
        status = L"ROI 5..101 нечётный; игнор < ROI/2; порог 1..100%; задержка 1..2000; интервал 50..2000";
        return false;
    }
    settings = next;
    verified = false;
    displayFrame.reset();
    displayMask = {};
    gate.setTiming(std::chrono::milliseconds{settings.dwellMs},
                   std::chrono::milliseconds{settings.cooldownMs});
    std::string error;
    const bool saved = persistSettings(error);
    if (saved)
        settingsLoadWarning.clear();
    status =
        saved ? L"Параметры сохранены; триггер выключен" : L"Параметры применены, но сохранение не удалось";
    return true;
}

void PreviewWindow::syncColor() {
    const bool wasSync = uiSync;
    uiSync = true;
    const int values[] = {color.target.red, color.target.green, color.target.blue, color.channelTolerance};
    for (int i = 0; i < 4; ++i)
        SetWindowTextW(colorEdits[i], std::to_wstring(values[i]).c_str());
    uiSync = wasSync;
    if (uiReady) {
        colorReady = true;
        verified = false;
        validationMessage.clear();
        displayFrame.reset();
        displayMask = {};
    }
}

bool PreviewWindow::applyColor() {
    cancelSelection();
    disableClicks();
    int values[4]{};
    for (int i = 0; i < 4; ++i) {
        char text[32]{};
        GetWindowTextA(colorEdits[i], text, 32);
        const std::string value(text);
        auto result = std::from_chars(value.data(), value.data() + value.size(), values[i]);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || values[i] < 0 ||
            values[i] > 255) {
            status = L"RGB и допуск: целые числа 0..255";
            return false;
        }
    }
    color.target = {static_cast<std::uint8_t>(values[2]), static_cast<std::uint8_t>(values[1]),
                    static_cast<std::uint8_t>(values[0]), 255};
    color.channelTolerance = values[3];
    color.space = triggerlab::ColorSpace::RGB;
    colorReady = true;
    verified = false;
    displayFrame.reset();
    displayMask = {};
    return true;
}

void PreviewWindow::chooseColor() {
    cancelSelection();
    disableClicks();
    COLORREF custom[16]{};
    CHOOSECOLORW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = window;
    picker.rgbResult = RGB(color.target.red, color.target.green, color.target.blue);
    picker.lpCustColors = custom;
    picker.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&picker)) {
        color.target = {GetBValue(picker.rgbResult), GetGValue(picker.rgbResult), GetRValue(picker.rgbResult),
                        255};
        color.space = triggerlab::ColorSpace::RGB;
        syncColor();
        status = L"Цвет выбран. Нажмите «Сохранить всё»";
    }
}

void PreviewWindow::saveColor() {
    if (!applyColor())
        return;
    std::string error;
    const bool saved = triggerlab::saveColorProfile(colorPath, color, error);
    if (saved) {
        savedColor = color;
        colorOnDisk = true;
    }
    status = saved ? L"Цвет сохранён" : L"Не удалось сохранить цвет";
}

} // namespace triggerlab::win32
