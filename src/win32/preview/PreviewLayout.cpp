#include "preview/PreviewWindow.hpp"
#include <algorithm>
#include <commctrl.h>

namespace triggerlab::win32 {

void PreviewWindow::placeLogical(HWND control, int x, int y, int width, int height) {
    for (auto &entry : controlRects)
        if (entry.control == control) {
            entry.logical = {x, y, x + width, y + height};
            return;
        }
}

bool PreviewWindow::pinned(HWND child) const {
    const int id = GetDlgCtrlID(child);
    return id == control::startCapture || id == control::stop || id == control::toggleTheme ||
           id == control::guidedAction || id == control::toggleAdvanced || id == control::headerStatus ||
           (id == control::toggleProfiles && !narrow);
}

void PreviewWindow::placeHeader() {
    auto put = [&](int id, int x, int y, int w, int h) {
        for (auto &entry : controlRects)
            if (entry.control == GetDlgItem(window, id)) {
                entry.placed = {x, y, x + w, y + h};
                return;
            }
    };
    const int width = canvasWidth - 48;
    if (canvasWidth >= 620) {
        headerHeight = 144;
        put(21, canvasWidth - 192, 12, 168, 36);
        put(23, canvasWidth - 392, 12, 188, 36);
        if (!narrow)
            put(25, canvasWidth - 524, 12, 120, 36);
        const int cell = std::min(250, (width - 12) / 2);
        put(1, 24, 60, cell, 40);
        put(22, 24, 60, cell, 40);
        put(2, 36 + cell, 60, cell, 40);
        put(30, 24, 110, narrow ? width : 470, 26);
    } else {
        headerHeight = 144;
        const int cell = (width - 12) / 2;
        put(23, 24, 36, cell, 32);
        put(21, 36 + cell, 36, cell, 32);
        put(1, 24, 76, cell, 40);
        put(22, 24, 76, cell, 40);
        put(2, 36 + cell, 76, cell, 40);
        put(30, 24, 120, width, 22);
    }
    SetWindowTextW(GetDlgItem(window, control::toggleAdvanced),
                   canvasWidth < 360 ? (settings.advancedMode ? L"Простой" : L"Настройки")
                                     : (settings.advancedMode ? L"Простой режим" : L"Расширенные"));
}

void PreviewWindow::narrowControls() {
    const int left = 24, width = canvasWidth - 48;
    auto put = [&](HWND child, int x, int y, int w, int h) {
        for (auto &entry : controlRects)
            if (entry.control == child) {
                entry.placed = {x, y, x + w, y + h};
                return;
            }
    };
    auto button = [&](int id, int y, int height = 40) {
        put(GetDlgItem(window, id), left, y, width, height);
    };
    int y = 64;
    if (settings.advancedMode) {
        button(control::toggleProfiles, 0, 36);
        y += 44;
    }
    auto begin = [&](RECT &card) {
        card = {12, y, canvasWidth - 12, 0};
        y += 58;
    };
    auto end = [&](RECT &card) {
        y += 16;
        card.bottom = y;
        y += 16;
    };
    begin(windowCard);
    button(control::chooseWindow, y, 58);
    y += 66;
    button(control::findKovaaks, y);
    y += 48;
    button(control::findAimlabs, y);
    y += 48;
    put(modeControl, left, y, width, 140);
    y += 40;
    end(windowCard);
    begin(runCard);
    if (settings.advancedMode) {
        put(dryControl, left, y, width, 40);
        y += 48;
        put(autoControl, left, y, width, 40);
        y += 48;
    }
    y += 76; // Wrapped next-action hint.
    end(runCard);
    begin(optionsCard);
    if (settings.advancedMode && !profilePane) {
        const int columns = canvasWidth >= 480 ? 2 : 1, cell = (width - 12 * (columns - 1)) / columns;
        for (int i = 0; i < 8; ++i) {
            const int x = left + (i % columns) * (cell + 12), top = y + (i / columns) * 68;
            put(settingLabels[i], x, top, cell, 22);
            put(settingEdits[i], x, top + 24, cell - 28, 32);
            put(settingHelp[i], x + cell - 22, top + 24, 22, 32);
        }
        y += ((8 + columns - 1) / columns) * 68;
        put(settingsHint, left, y, width, 62);
        y += 70;
    } else {
        // The painter supplies labels immediately above these fields.
        y += 24;
        put(profileChoice, left, y, width, 180);
        y += 40;
        button(control::loadProfile, y);
        y += 72;
        put(profileName, left, y, width, 34);
        y += 42;
        button(control::saveProfile, y);
        y += 48;
        y += 62;
    }
    button(control::saveAll, y);
    y += 48;
    end(optionsCard);
    begin(colorCard);
    if (settings.advancedMode) {
        const int columns = canvasWidth >= 360 ? 2 : 1, cell = (width - 12 * (columns - 1)) / columns;
        for (int i = 0; i < 4; ++i) {
            const int x = left + (i % columns) * (cell + 12), top = y + (i / columns) * 68;
            put(colorLabels[i], x, top, cell, 22);
            put(colorEdits[i], x, top + 24, cell - (i == 3 ? 28 : 0), 32);
            if (i == 3)
                put(colorHelp, x + cell - 22, top + 24, 22, 32);
        }
        y += ((4 + columns - 1) / columns) * 68;
        button(control::applyColor, y);
        y += 48;
        button(control::saveColor, y);
        y += 48;
    } else {
        put(colorLabels[3], left, y, width, 22);
        put(colorEdits[3], left, y + 24, width, 32);
        y += 68;
        put(colorHint, left, y, width, 70);
        y += 78;
    }
    y += 58; // Current-color caption and swatch.
    for (int id : {control::pickAimColor, control::pickCursorColor, control::chooseColor,
                   control::importImage, control::editImage}) {
        button(id, y);
        y += 48;
    }
    end(colorCard);
    begin(qualityCard);
    button(control::targetReference, y);
    y += 48;
    button(control::backgroundReference, y);
    y += 48;
    const int feedbackHeight = canvasWidth < 480 ? 288 : 160;
    put(qualityInfo, left, y, width, feedbackHeight);
    y += feedbackHeight + 8;
    end(qualityCard);
    begin(stateCard);
    y += 280 + (canvasWidth < 480 ? 112 : 56);
    end(stateCard);
    for (auto &card : previewCards) {
        begin(card);
        y += 242;
        end(card);
    }
    footerY = y;
    canvasHeight = y + 156;
}

void PreviewWindow::homeControls() {
    auto put = [&](HWND child, int x, int y, int w, int h) {
        for (auto &entry : controlRects)
            if (entry.control == child) {
                entry.placed = {x, y, x + w, y + h};
                return;
            }
    };
    auto button = [&](int id, int x, int y, int w, int h = 38) { put(GetDlgItem(window, id), x, y, w, h); };
    const int edge = 16, gap = 16, full = canvasWidth - 2 * edge;
    const bool columns = canvasWidth >= 800;
    const int cell = columns ? (full - gap) / 2 : full;
    runCard = {edge, 16, canvasWidth - edge, 156};
    put(homeHint, edge + 20, 80, full - 40, 60);
    windowCard = {edge, 172, edge + cell, 486};
    const int wx = windowCard.left + 16, ww = cell - 32;
    button(control::chooseWindow, wx, 230, ww, 58);
    if (cell >= 340) {
        button(control::findKovaaks, wx, 300, (ww - 12) / 2);
        button(control::findAimlabs, wx + (ww + 12) / 2, 300, (ww - 12) / 2);
    } else {
        button(control::findKovaaks, wx, 300, ww);
        button(control::findAimlabs, wx, 346, ww);
    }
    put(modeControl, wx, cell >= 340 ? 364 : 408, ww, 140);
    const int colorTop = columns ? 172 : 502, cx = columns ? edge + cell + gap : edge;
    colorCard = {cx, colorTop, cx + cell, colorTop + 450};
    const int x = cx + 16, width = cell - 32;
    button(control::pickAimColor, x, colorTop + 58, width, 42);
    button(control::importImage, x, colorTop + 110, width);
    put(colorLabels[3], x, colorTop + 166, width, 22);
    put(colorEdits[3], x, colorTop + 192, width - 28, 32);
    put(colorHelp, x + width - 22, colorTop + 192, 22, 32);
    button(control::saveAll, x, colorTop + 382, width, 42);
    int y = colorCard.bottom + 16;
    const int summaryHeight = canvasWidth < 480 ? 272 : 160;
    stateCard = {edge, y, canvasWidth - edge, y + 66 + summaryHeight};
    put(homeSummary, edge + 20, y + 54, full - 40, summaryHeight);
    y = stateCard.bottom + 16;
    button(control::toggleDetails, edge, y, full, 42);
    y += 58;
    if (homeDetails) {
        optionsCard = {edge, y, canvasWidth - edge, y + 340};
        put(profileChoice, edge + 16, y + 74, full - 32, 180);
        button(control::loadProfile, edge + 16, y + 116, full - 32);
        put(profileName, edge + 16, y + 196, full - 32, 34);
        button(control::saveProfile, edge + 16, y + 240, full - 32);
        y = optionsCard.bottom + 16;
        // Optional color sources stay available without crowding the first run.
        for (int id : {control::pickCursorColor, control::chooseColor, control::editImage}) {
            button(id, edge, y, full);
            y += 46;
        }
        const int feedbackHeight = canvasWidth < 480 ? 288 : 186;
        qualityCard = {edge, y, canvasWidth - edge, y + 168 + feedbackHeight};
        button(control::targetReference, edge + 16, y + 56, full - 32);
        button(control::backgroundReference, edge + 16, y + 102, full - 32);
        put(qualityInfo, edge + 16, y + 152, full - 32, feedbackHeight);
        y = qualityCard.bottom + 16;
        for (auto &card : previewCards) {
            card = {edge, y, canvasWidth - edge, y + 300};
            y += 316;
        }
    } else {
        optionsCard = {};
        qualityCard = {};
        for (auto &card : previewCards)
            card = {};
    }
    footerY = y;
    canvasHeight = y + 146;
}

void PreviewWindow::arrange() {
    if (!uiReady || arranging)
        return;
    arranging = true;
    struct LayoutDrawing {
        HWND window;
        bool &arranging;
        bool suspended;
        void finish() {
            if (!arranging)
                return;
            arranging = false;
            if (suspended)
                SendMessageW(window, WM_SETREDRAW, TRUE, 0);
        }
        ~LayoutDrawing() {
            finish();
        }
    } drawing{window, arranging, IsWindowVisible(window) != FALSE};
    if (drawing.suspended)
        SendMessageW(window, WM_SETREDRAW, FALSE, 0);
    // Both bars are included when computing the viewport, avoiding a
    // horizontal bar hiding the final row of a nearly fitting canvas.
    RECT client{};
    GetClientRect(window, &client);
    const LONG style = GetWindowLongW(window, GWL_STYLE);
    const int barWidth = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi),
              barHeight = GetSystemMetricsForDpi(SM_CYHSCROLL, dpi);
    const int availableWidth = client.right + ((style & WS_VSCROLL) ? barWidth : 0);
    const int availableHeight = client.bottom + ((style & WS_HSCROLL) ? barHeight : 0);
    const bool wasNarrow = narrow;
    narrow = availableWidth - barWidth < scaled(1102);
    canvasWidth =
        narrow ? std::max(220, std::max(1, availableWidth - barWidth) * 96 / static_cast<int>(dpi)) : 1102;
    for (auto &entry : controlRects)
        entry.placed = entry.logical;
    if (!settings.advancedMode)
        homeControls();
    else if (narrow)
        narrowControls();
    else {
        canvasHeight = 950 - 84 + 210 + 56;
    }
    placeHeader();
    if (narrow != wasNarrow)
        scrollX = scrollY = 0;
    bool horizontal = availableWidth < scaled(canvasWidth),
         vertical = availableHeight - scaled(headerHeight) < scaled(canvasHeight);
    if (vertical && availableWidth - barWidth < scaled(canvasWidth))
        horizontal = true;
    if (horizontal && availableHeight - barHeight - scaled(headerHeight) < scaled(canvasHeight))
        vertical = true;
    if (horizontal != ((style & WS_HSCROLL) != 0))
        ShowScrollBar(window, SB_HORZ, horizontal);
    if (vertical != ((style & WS_VSCROLL) != 0))
        ShowScrollBar(window, SB_VERT, vertical);
    GetClientRect(window, &client);
    SCROLLINFO sx{sizeof(sx),
                  SIF_RANGE | SIF_PAGE | SIF_POS,
                  0,
                  scaled(canvasWidth) - 1,
                  static_cast<UINT>(client.right),
                  scrollX};
    SCROLLINFO sy{sizeof(sy),
                  SIF_RANGE | SIF_PAGE | SIF_POS,
                  0,
                  scaled(canvasHeight) - 1,
                  static_cast<UINT>(std::max(1, static_cast<int>(client.bottom) - scaled(headerHeight))),
                  scrollY};
    SetScrollInfo(window, SB_HORZ, &sx, FALSE);
    SetScrollInfo(window, SB_VERT, &sy, FALSE);
    sx.fMask = sy.fMask = SIF_POS;
    GetScrollInfo(window, SB_HORZ, &sx);
    GetScrollInfo(window, SB_VERT, &sy);
    scrollX = sx.nPos;
    scrollY = sy.nPos;
    struct Placement {
        HWND child, after;
        int x, y, width, height;
        UINT flags;
    };
    std::vector<Placement> placements;
    placements.reserve(controlRects.size() + 1);
    if (headerBackdrop)
        placements.push_back({headerBackdrop, HWND_TOP, 0, 0, static_cast<int>(client.right),
                              scaled(headerHeight), SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW});
    for (const auto &entry : controlRects) {
        const auto &r = entry.placed;
        const bool fixed = pinned(entry.control);
        const int origin =
            fixed ? 0 : scaled(headerHeight - ((narrow || !settings.advancedMode) ? 0 : 84)) - scrollY;
        placements.push_back({entry.control, fixed ? HWND_TOP : nullptr,
                              scaled(r.left) - (fixed ? 0 : scrollX), scaled(r.top) + origin,
                              scaled(r.right - r.left), scaled(r.bottom - r.top),
                              SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW | (fixed ? 0u : SWP_NOZORDER)});
    }
    // Move the complete layout before painting any control or exposed background.
    HDWP batch = BeginDeferWindowPos(static_cast<int>(placements.size()));
    for (const auto &p : placements) {
        if (!batch)
            break;
        batch = DeferWindowPos(batch, p.child, p.after, p.x, p.y, p.width, p.height, p.flags);
    }
    const bool moved = batch && EndDeferWindowPos(batch);
    if (!moved)
        for (const auto &p : placements)
            SetWindowPos(p.child, p.after, p.x, p.y, p.width, p.height, p.flags);
    for (const auto &entry : controlRects) {
        const bool fixed = pinned(entry.control);
        // WM_SETREDRAW temporarily hides the owner from IsWindowVisible.
        if (!(GetWindowLongW(entry.control, GWL_STYLE) & WS_VISIBLE))
            continue;
        if (fixed)
            SetWindowRgn(entry.control, nullptr, FALSE);
        else {
            // Child windows stay attached to the main command handlers, but
            // their paint and hit-test region cannot cover the fixed header.
            RECT child{}, visible{}, viewport{0, scaled(headerHeight), client.right, client.bottom};
            GetWindowRect(entry.control, &child);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&child), 2);
            const bool intersects = IntersectRect(&visible, &child, &viewport) != FALSE;
            HRGN clip = intersects ? CreateRectRgn(visible.left - child.left, visible.top - child.top,
                                                   visible.right - child.left, visible.bottom - child.top)
                                   : CreateRectRgn(0, 0, 0, 0);
            if (clip && !SetWindowRgn(entry.control, clip, FALSE))
                DeleteObject(clip);
        }
    }
    drawing.finish();
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
}

void PreviewWindow::reveal(HWND control) {
    if (!uiReady || !IsWindowVisible(control) || pinned(control))
        return;
    RECT r{}, client{};
    GetWindowRect(control, &r);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&r), 2);
    GetClientRect(window, &client);
    const int margin = scaled(6);
    if (r.left < margin)
        scrollX += r.left - margin;
    else if (r.right > client.right - margin)
        scrollX += r.right - client.right + margin;
    if (r.top < scaled(headerHeight) + margin)
        scrollY += r.top - scaled(headerHeight) - margin;
    else if (r.bottom > client.bottom - margin)
        scrollY += r.bottom - client.bottom + margin;
    arrange();
}

void PreviewWindow::scroll(int bar, int request) {
    SCROLLINFO info{sizeof(info), SIF_ALL};
    GetScrollInfo(window, bar, &info);
    int &position = bar == SB_HORZ ? scrollX : scrollY;
    switch (request) {
    case SB_LINEUP:
        position -= scaled(36);
        break;
    case SB_LINEDOWN:
        position += scaled(36);
        break;
    case SB_PAGEUP:
        position -= static_cast<int>(info.nPage);
        break;
    case SB_PAGEDOWN:
        position += static_cast<int>(info.nPage);
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        position = info.nTrackPos;
        break;
    case SB_TOP:
        position = 0;
        break;
    case SB_BOTTOM:
        position = info.nMax;
        break;
    }
    arrange();
}

LRESULT CALLBACK PreviewWindow::viewportProcedure(HWND child, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                                  DWORD_PTR context) {
    auto &app = *reinterpret_cast<PreviewWindow *>(context);
    if (message == WM_SETFOCUS)
        app.reveal(child);
    if (message == WM_MOUSEWHEEL && child != app.modeControl && child != app.profileChoice)
        return SendMessageW(app.window, message, wp, lp);
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(child, viewportProcedure, id);
    return DefSubclassProc(child, message, wp, lp);
}

LRESULT CALLBACK PreviewWindow::headerProcedure(HWND child, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                                DWORD_PTR context) {
    auto &app = *reinterpret_cast<PreviewWindow *>(context);
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(child, &ps);
        RECT r{};
        GetClientRect(child, &r);
        app.paintHeader(dc, r.right);
        EndPaint(child, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(child, headerProcedure, id);
    return DefSubclassProc(child, message, wp, lp);
}

void PreviewWindow::layoutMode() {
    const bool advanced = settings.advancedMode, parameters = advanced && !profilePane;
    for (HWND field : settingEdits)
        ShowWindow(field, parameters ? SW_SHOW : SW_HIDE);
    for (HWND label : settingLabels)
        ShowWindow(label, parameters ? SW_SHOW : SW_HIDE);
    for (HWND help : settingHelp)
        ShowWindow(help, parameters ? SW_SHOW : SW_HIDE);
    ShowWindow(settingsHint, parameters ? SW_SHOW : SW_HIDE);
    for (int id : {control::loadProfile, control::saveProfile, control::profileChoice, control::profileName})
        ShowWindow(GetDlgItem(window, id), parameters ? SW_HIDE : SW_SHOW);
    for (int i = 0; i < 3; ++i) {
        ShowWindow(colorEdits[i], advanced ? SW_SHOW : SW_HIDE);
        ShowWindow(colorLabels[i], advanced ? SW_SHOW : SW_HIDE);
    }
    for (int id : {control::startCapture, control::applyColor, control::saveColor, control::enableTrigger,
                   control::dryRun})
        ShowWindow(GetDlgItem(window, id), advanced ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(window, control::guidedAction), advanced ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(window, control::saveParameters), SW_HIDE);
    ShowWindow(colorHint, advanced ? SW_HIDE : SW_SHOW);
    placeLogical(colorLabels[3], advanced ? 370 : 40, 457, 104, 22);
    placeLogical(colorEdits[3], advanced ? 370 : 40, 482, advanced ? 68 : 96, 30);
    SetWindowTextW(GetDlgItem(window, control::toggleAdvanced), advanced ? L"Простой режим" : L"Расширенные");
    ShowWindow(GetDlgItem(window, control::toggleProfiles), advanced ? SW_SHOW : SW_HIDE);
    SetWindowTextW(GetDlgItem(window, control::toggleProfiles), profilePane ? L"Параметры" : L"Профили");
    ShowWindow(GetDlgItem(window, control::toggleDetails), advanced ? SW_HIDE : SW_SHOW);
    ShowWindow(homeHint, advanced ? SW_HIDE : SW_SHOW);
    ShowWindow(homeSummary, advanced ? SW_HIDE : SW_SHOW);
    SetWindowTextW(GetDlgItem(window, control::toggleDetails),
                   homeDetails ? L"Скрыть инструменты" : L"Профили и диагностика");
    if (!advanced) {
        for (int id :
             {control::loadProfile, control::saveProfile, control::profileChoice, control::profileName,
              control::pickCursorColor, control::chooseColor, control::editImage, control::targetReference,
              control::backgroundReference, control::qualityInfo})
            ShowWindow(GetDlgItem(window, id), homeDetails ? SW_SHOW : SW_HIDE);
        ShowWindow(colorHint, SW_HIDE);
    }
    arrange();
    syncActions();
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

} // namespace triggerlab::win32
