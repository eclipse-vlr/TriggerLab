#include "ImagePaletteDialog.hpp"
#include "StartupDiagnostics.hpp"
#include "preview/PreviewWindow.hpp"
#include "triggerlab/ClickPermission.hpp"
#include "triggerlab/ImageFile.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <cwctype>
#include <dwmapi.h>
#include <uxtheme.h>

namespace triggerlab::win32 {

void PreviewWindow::guidedAction() {
    if (autoClick && !settings.dryRun) {
        stop();
        return;
    }
    const int step = wizardStep();
    if (step == 0) {
        chooseTargetWindow();
        return;
    }
    if (step == 1) {
        if (settings.center)
            beginAimSelection();
        else
            beginSelection(true);
        return;
    }
    if (settingsDirty() || colorDirty()) {
        if (!saveAll())
            return;
    }
    const bool diagnostic = wizardStep() != 3;
    disableClicks(); // Release our existing F9 registration before re-arming.
    settings.dryRun = diagnostic;
    syncSettings();
    command(control::startCapture);
    SendMessageW(autoControl, BM_SETCHECK, BST_CHECKED, 0);
    command(control::enableTrigger);
}

void PreviewWindow::createControls() {
    auto font = [](int size, int weight) {
        return CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    uiFont = font(16, FW_NORMAL);
    titleFont = font(30, FW_SEMIBOLD);
    headingFont = font(18, FW_SEMIBOLD);
    auto button = [&](int id, const wchar_t *label, int x, int y, int w, int h = 36) {
        return CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, w, h,
                             window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    };
    auto label = [&](const wchar_t *text, int x, int y, int width) {
        return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, 22, window, nullptr,
                             nullptr, nullptr);
    };
    auto question = [&](int id, int x, int y, int height) {
        return CreateWindowW(
            L"STATIC", L"?", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_CENTER | SS_CENTERIMAGE | SS_NOTIFY, x, y,
            22, height, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    };
    button(control::toggleTheme, L"Серая тема: выключена", 914, 20, 168, 36);
    button(control::toggleAdvanced, L"Расширенные", 710, 20, 188, 36);
    button(control::toggleProfiles, L"Профили", 574, 20, 120, 36);
    button(control::toggleDetails, L"Профили и диагностика", 24, 700, 500, 42);
    homeHint =
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 24, 200, 500, 76, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control::homeHint)), nullptr, nullptr);
    homeSummary =
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 24, 600, 500, 160, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control::homeSummary)), nullptr, nullptr);
    headerStatus =
        CreateWindowW(L"STATIC", L"Остановлено · без кликов", WS_CHILD | WS_VISIBLE, 24, 110, 500, 26, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control::headerStatus)), nullptr, nullptr);
    button(control::chooseWindow, L"Выбрать окно (3 сек.)", 40, 130, 260, 58);
    button(control::findKovaaks, L"Kovaak’s", 316, 130, 135);
    button(control::findAimlabs, L"Aimlabs", 316, 174, 135, 30);
    modeControl = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 40, 210, 410, 140, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control::detectionMode)), nullptr, nullptr);
    SendMessageW(modeControl, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Обнаружение вокруг прицела"));
    SendMessageW(modeControl, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Обнаружение под курсором"));
    button(control::startCapture, L"Начать захват", 548, 130, 250, 42);
    button(control::stop, L"Остановить · F9", 812, 130, 250, 42);
    button(control::guidedAction, L"Выбрать окно", 548, 130, 250, 42);
    dryControl = CreateWindowW(
        L"BUTTON", L"Проверять без кликов", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 548, 190,
        235, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control::dryRun)), nullptr, nullptr);
    autoControl = CreateWindowW(
        L"BUTTON", L"Включить триггер", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 812, 190, 250,
        28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control::enableTrigger)), nullptr, nullptr);
    const wchar_t *settingsLabels[] = {L"Область, px", L"Исключить центр", L"Порог, %",     L"Сдвиг X, px",
                                       L"Сдвиг Y, px", L"Задержка, мс",    L"Интервал, мс", L"Нажатие, мс"};
    for (int i = 0; i < 8; ++i) {
        settingLabels[i] = label(settingsLabels[i], 40 + i * 128, 300, 124);
        settingEdits[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 40 + i * 128,
                                          326, 84, 30, window, nullptr, nullptr, nullptr);
        settingHelp[i] = question(control::firstParameterHelp + i, 130 + i * 128, 326, 30);
    }
    settingsHint =
        label(L"Клик только у центра. Наведите на ? рядом с полем для подробной подсказки.", 40, 368, 780);
    button(control::saveParameters, L"Сохранить", 938, 365, 124, 30);
    button(control::saveAll, L"Сохранить всё", 844, 365, 218, 30);
    profileChoice = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 40, 326, 366, 180, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control::profileChoice)), nullptr, nullptr);
    button(control::loadProfile, L"Загрузить", 420, 326, 124, 30);
    profileName = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 560, 326, 328,
        30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control::profileName)), nullptr, nullptr);
    SendMessageW(profileName, EM_LIMITTEXT, 60, 0);
    button(control::saveProfile, L"Сохранить профиль", 902, 326, 160, 30);
    SendMessageW(profileName, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Например: статичные мишени"));
    const wchar_t *colors[] = {L"Красный · R", L"Зелёный · G", L"Синий · B", L"Допуск"};
    for (int i = 0; i < 4; ++i) {
        colorLabels[i] = label(colors[i], 40 + i * 110, 457, 104);
        colorEdits[i] =
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
                            40 + i * 110, 482, 96, 30, window, nullptr, nullptr, nullptr);
    }
    colorHelp = question(control::colorToleranceHelp, 444, 482, 30);
    SetWindowPos(colorEdits[3], nullptr, 0, 0, 68, 30, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    button(control::applyColor, L"Применить", 482, 482, 120, 30);
    button(control::saveColor, L"Сохранить цвет", 618, 482, 160, 30);
    button(control::pickAimColor, L"Цвет из прицела (3 сек.)", 40, 530, 240);
    button(control::pickCursorColor, L"Пипетка (3 сек.)", 294, 530, 174);
    button(control::chooseColor, L"Выбрать цвет…", 482, 530, 166);
    button(control::importImage, L"Из файла…", 662, 530, 152);
    button(control::editImage, L"Палитра / область", 828, 530, 234);
    button(control::targetReference, L"Снять мишень (3 сек.)", 40, 652, 220, 40);
    button(control::backgroundReference, L"Снять фон (3 сек.)", 276, 652, 220, 40);
    qualityInfo =
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 520, 640, 540, 136, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control::qualityInfo)), nullptr, nullptr);
    colorHint = label(L"Выберите цвет мишени с экрана или из файла. Сохраните изменения кнопкой выше.", 160,
                      482, 625);
    EnumChildWindows(
        window,
        [](HWND child, LPARAM f) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(uiFont));
    EnumChildWindows(
        window,
        [](HWND child, LPARAM) -> BOOL {
            SetWindowTheme(child, L"", L"");
            return TRUE;
        },
        0);
    for (HWND control : {dryControl, autoControl})
        SetWindowSubclass(control, checkProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&common);
    tooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (tooltip) {
        SetWindowTheme(tooltip, L"", L"");
        SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 420);
        SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
        SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);
        SendMessageW(tooltip, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), FALSE);
        // The application also supports common-controls v5 without a v6
        // manifest. It rejects the newer lpReserved-sized TOOLINFO.
        auto tip = [&](HWND control, const wchar_t *text) {
            TOOLINFOW info{TTTOOLINFOW_V2_SIZE};
            info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            info.hwnd = window;
            info.uId = reinterpret_cast<UINT_PTR>(control);
            info.lpszText = const_cast<wchar_t *>(text);
            SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        };
        for (int i = 0; i < 8; ++i) {
            tip(settingEdits[i], parameterHelp(i));
            tip(settingHelp[i], parameterHelp(i));
        }
        tip(colorEdits[3], parameterHelp(8));
        tip(colorHelp, parameterHelp(8));
        tip(dryControl, L"Проверка распознавания и задержки без отправки мышиных кликов.");
        tip(autoControl, L"Сначала выберите окно и начните захват. Один клик на непрерывное совпадение; F9 "
                         L"останавливает работу.");
        tip(GetDlgItem(window, control::pickAimColor),
            L"Окно тренажёра активируется автоматически. За 3 секунды наведите прицел на мишень, затем "
            L"выберите цвет в палитре. F9 — отмена.");
        tip(GetDlgItem(window, control::targetReference),
            L"Окно тренажёра активируется автоматически. Наведите прицел на мишень за 3 секунды. Клики "
            L"выключены; F9 — отмена.");
        tip(GetDlgItem(window, control::backgroundReference),
            L"Окно тренажёра активируется автоматически. Наведите прицел на фон без мишени за 3 секунды. "
            L"Эталон хранится только в памяти; F9 — отмена.");
        tip(GetDlgItem(window, control::toggleAdvanced),
            L"Простой режим показывает основные действия; расширенный открывает ручные параметры.");
        tip(GetDlgItem(window, control::toggleTheme),
            L"Переключение между светлой и тёмно-серой темой. Выбор сохраняется автоматически.");
    }
    EnumChildWindows(
        window,
        [](HWND child, LPARAM context) -> BOOL {
            auto &app = *reinterpret_cast<PreviewWindow *>(context);
            if (GetParent(child) != app.window)
                return TRUE;
            SetWindowLongW(child, GWL_STYLE, GetWindowLongW(child, GWL_STYLE) | WS_CLIPSIBLINGS);
            RECT r{};
            GetWindowRect(child, &r);
            MapWindowPoints(nullptr, app.window, reinterpret_cast<POINT *>(&r), 2);
            // A combo's window rect contains only its collapsed height.
            if (child == app.modeControl)
                r.bottom = r.top + 140;
            if (child == app.profileChoice)
                r.bottom = r.top + 180;
            app.controlRects.push_back({child, r});
            SetWindowSubclass(child, viewportProcedure, 2, context);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    // An opaque sibling below the pinned controls prevents standard Win32
    // fields from repainting over the header during movement or focus.
    headerBackdrop = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                                   window, nullptr, nullptr, nullptr);
    SetWindowSubclass(headerBackdrop, headerProcedure, 3, reinterpret_cast<DWORD_PTR>(this));
    syncColor();
    syncSettings();
    refreshTargetButton();
    applyTheme();
    refreshProfiles();
    uiReady = true;
    setDpi(GetDpiForWindow(window));
    layoutMode();
    syncActions();
    SetTimer(window, timer::recovery, 500, nullptr);
}

void PreviewWindow::command(int id, int notification) {
    if (id == control::startCapture || id == control::stop || id == control::pickCursorColor ||
        id == control::chooseWindow || id == control::findKovaaks || id == control::findAimlabs ||
        id == control::pickAimColor || id == control::guidedAction || id == control::saveAll ||
        id == control::loadProfile || id == control::saveProfile || id == control::targetReference ||
        id == control::backgroundReference)
        actionError.clear();
    if (id == control::startCapture) {
        if (SetTimer(window, timer::capture, 10, nullptr)) {
            runFrames = runAlignedFrames = runUnavailableFrames = 0;
            runBestCoverage = 0;
            runLastIssue.clear();
            running = true;
            status = L"Захват активен";
        } else {
            status = L"Не удалось начать захват";
            actionError = L"Не удалось создать таймер захвата. Остановите анализатор и повторите запуск.";
        }
    } else if (id == control::stop)
        stop();
    else if (id == control::applyColor) {
        if (applyColor())
            status = L"RGB применён; автоклик выключен";
    } else if (id == control::chooseColor)
        chooseColor();
    else if (id == control::pickCursorColor)
        beginSelection(true);
    else if (id == control::saveColor)
        saveColor();
    else if (id == control::chooseWindow)
        chooseTargetWindow();
    else if (id == control::editImage)
        editImage();
    else if (id == control::saveParameters)
        applySettings();
    else if (id == control::findKovaaks)
        findTrainer(L"kovaak");
    else if (id == control::findAimlabs)
        findTrainer(L"aimlab");
    else if (id == control::pickAimColor)
        beginAimSelection();
    else if (id == control::targetReference || id == control::backgroundReference)
        beginAimSelection(id == control::targetReference ? SnapshotPurpose::Target
                                                         : SnapshotPurpose::Background);
    else if (id == control::guidedAction) {
        guidedAction();
        syncActions();
        notice = status;
        InvalidateRect(window, nullptr, FALSE);
        return;
    } else if (id == control::toggleAdvanced) {
        settings.advancedMode = !settings.advancedMode;
        profilePane = false;
        layoutMode();
        std::string error;
        notice =
            persistSettings(error) ? L"Режим интерфейса сохранён" : L"Режим изменён; сохранение не удалось";
        return;
    } else if (id == control::saveAll) {
        saveAll();
        return;
    } else if (id == control::toggleProfiles) {
        profilePane = !profilePane;
        layoutMode();
        return;
    } else if (id == control::toggleDetails) {
        homeDetails = !homeDetails;
        layoutMode();
        reveal(GetDlgItem(window, control::toggleDetails));
        return;
    } else if (id == control::loadProfile) {
        loadProfile();
        syncActions();
        InvalidateRect(window, nullptr, FALSE);
        return;
    } else if (id == control::saveProfile) {
        saveProfile();
        syncActions();
        InvalidateRect(window, nullptr, FALSE);
        return;
    } else if (id == control::profileChoice && notification == CBN_SELCHANGE) {
        const auto selected = SendMessageW(profileChoice, CB_GETCURSEL, 0, 0);
        if (selected >= 0 && static_cast<std::size_t>(selected) < profiles.size())
            SetWindowTextW(profileName, wide(profiles[selected].name).c_str());
        return;
    } else if (id == control::toggleTheme) {
        settings.darkTheme = !settings.darkTheme;
        applyTheme();
        std::string error;
        notice = persistSettings(error) ? L"Тема сохранена" : L"Тема изменена, но не удалось сохранить выбор";
        return;
    } else if (id == control::detectionMode || id == control::dryRun) {
        if (id == control::detectionMode && notification != CBN_SELCHANGE)
            return;
        cancelSelection();
        disableClicks();
        settings.center = SendMessageW(modeControl, CB_GETCURSEL, 0, 0) == 0;
        settings.dryRun = SendMessageW(dryControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
        verified = false;
        layoutMode();
        sample = {};
        mask = {};
        analysisPoint.reset();
        std::string error;
        status = persistSettings(error) ? L"Режим сохранён; включите триггер отдельно"
                                        : L"Режим изменён; сохранение не удалось";
    } else if (id == control::importImage) {
        chooseImage();
        SetFocus(window);
        InvalidateRect(window, nullptr, FALSE);
        return;
    } else if (id == control::enableTrigger) {
        cancelSelection();
        if (syntheticDown || !running || !targetWindow || !IsWindow(targetWindow) || !inputAuthorized()) {
            disableClicks();
            status = L"Выберите окно и цвет, сохраните настройки; перед кликами выполните проверку";
        } else {
            autoClick = SendMessageW(autoControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            gate.reset();
            if (autoClick && !RegisterHotKey(window, hotkey::stop, MOD_NOREPEAT, VK_F9)) {
                disableClicks();
                status = L"F9 занят: автоклик не включён";
                notice = status;
                actionError = status + L". Освободите F9 в другом приложении и повторите запуск.";
                syncActions();
                InvalidateRect(window, nullptr, FALSE);
                return;
            }
            if (!autoClick)
                UnregisterHotKey(window, hotkey::stop);
            status = autoClick ? (settings.dryRun ? L"Проверка активна: клики не отправляются"
                                                  : L"Клики включены только в выбранном окне")
                               : L"Триггер выключен";
        }
    } else
        return;
    if (id == control::enableTrigger && autoClick) {
        // Arming is an explicit user action: activate the chosen window,
        // and do not immediately steal focus back to the analyzer.
        if (!SetForegroundWindow(targetWindow)) {
            clickStatus = L"Активируйте выбранное окно вручную";
            status = settings.dryRun ? L"Проверка активна; активируйте тренировку"
                                     : L"Клики включены; активируйте тренировку";
        } else
            clickStatus = L"Ожидаю цвет в выбранном окне";
    } else if (!((id == control::pickAimColor || id == control::targetReference ||
                  id == control::backgroundReference) &&
                 aimPickDeadline))
        SetFocus(window);
    syncActions();
    notice = status;
    InvalidateRect(window, nullptr, FALSE);
}

LRESULT CALLBACK PreviewWindow::checkProcedure(HWND control, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                               DWORD_PTR context) {
    if (message == WM_PAINT) {
        auto &app = *reinterpret_cast<PreviewWindow *>(context);
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(control, &paint);
        app.drawCheck(control, dc);
        EndPaint(control, &paint);
        return 0;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(control, checkProcedure, id);
    return DefSubclassProc(control, message, wp, lp);
}

void PreviewWindow::drawCheck(HWND control, HDC dc) {
    const int savedDc = SaveDC(dc);
    mapCanvas(dc);
    const auto p = themeColors(settings.darkTheme);
    RECT bounds{};
    GetClientRect(control, &bounds);
    bounds.right = MulDiv(bounds.right, 96, dpi);
    bounds.bottom = MulDiv(bounds.bottom, 96, dpi);
    SetDCBrushColor(dc, p.card);
    FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const bool checked = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const int top = (bounds.bottom - 18) / 2;
    auto oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH)),
         oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, checked ? RGB(37, 99, 235) : p.input);
    SetDCPenColor(dc, checked ? RGB(37, 99, 235) : p.border);
    RoundRect(dc, 0, top, 18, top + 18, 5, 5);
    if (checked) {
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        auto previous = SelectObject(dc, pen);
        MoveToEx(dc, 4, top + 9, nullptr);
        LineTo(dc, 8, top + 13);
        LineTo(dc, 14, top + 5);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    wchar_t text[256]{};
    GetWindowTextW(control, text, 256);
    auto oldFont = SelectObject(dc, uiFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? p.text : p.muted);
    RECT label{26, 0, bounds.right, bounds.bottom};
    DrawTextW(dc, text, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (GetFocus() == control) {
        InflateRect(&label, -2, -4);
        DrawFocusRect(dc, &label);
    }
    SelectObject(dc, oldFont);
    RestoreDC(dc, savedDc);
}

void PreviewWindow::drawButton(DRAWITEMSTRUCT item) {
    const int savedDc = SaveDC(item.hDC);
    mapCanvas(item.hDC);
    item.rcItem.right = MulDiv(item.rcItem.right, 96, dpi);
    item.rcItem.bottom = MulDiv(item.rcItem.bottom, 96, dpi);
    const auto p = themeColors(settings.darkTheme);
    const bool primary = item.CtlID == 1 || item.CtlID == 20 || item.CtlID == 22;
    const bool stop = item.CtlID == 2, disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF fill = disabled  ? p.input
                          : primary ? (pressed ? RGB(29, 78, 216) : RGB(37, 99, 235))
                          : stop    ? p.stopFill
                          : pressed ? p.pressed
                                    : p.button;
    SetDCBrushColor(item.hDC, fill);
    SetDCPenColor(item.hDC, primary && !disabled ? fill : p.border);
    auto oldBrush = SelectObject(item.hDC, GetStockObject(DC_BRUSH));
    auto oldPen = SelectObject(item.hDC, GetStockObject(DC_PEN));
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, 10, 10);
    wchar_t caption[512]{};
    GetWindowTextW(item.hwndItem, caption, 512);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? p.muted : primary ? RGB(255, 255, 255) : stop ? p.stopText : p.text);
    auto oldFont = SelectObject(item.hDC, uiFont);
    RECT text = item.rcItem;
    InflateRect(&text, -8, -4);
    if (item.CtlID == 21) {
        const int left = item.rcItem.right - 54, top = (item.rcItem.top + item.rcItem.bottom) / 2 - 11;
        SetDCBrushColor(item.hDC, settings.darkTheme ? RGB(37, 99, 235) : p.border);
        SetDCPenColor(item.hDC, settings.darkTheme ? RGB(37, 99, 235) : p.border);
        RoundRect(item.hDC, left, top, left + 44, top + 22, 22, 22);
        SetDCBrushColor(item.hDC, RGB(255, 255, 255));
        SetDCPenColor(item.hDC, RGB(255, 255, 255));
        const int knob = left + (settings.darkTheme ? 24 : 3);
        Ellipse(item.hDC, knob, top + 3, knob + 16, top + 19);
        text.right = left - 4;
        DrawTextW(item.hDC, canvasWidth < 360 ? L"Тема" : L"Серая тема", -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else
        DrawTextW(item.hDC, caption, -1, &text, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    if (item.itemState & ODS_FOCUS) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(item.hDC, &focus);
    }
    SelectObject(item.hDC, oldFont);
    SelectObject(item.hDC, oldBrush);
    SelectObject(item.hDC, oldPen);
    RestoreDC(item.hDC, savedDc);
}

} // namespace triggerlab::win32
