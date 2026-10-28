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

void PreviewWindow::paintHeader(HDC dc, int clientWidth) {
    const auto p = themeColors(settings.darkTheme);
    RECT area{0, 0, clientWidth, scaled(headerHeight)};
    SetDCBrushColor(dc, p.card);
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const int saved = SaveDC(dc);
    mapCanvas(dc);
    SelectObject(dc, canvasWidth < 620 ? headingFont : titleFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, p.text);
    RECT title{24, 8, 210, 46};
    DrawTextW(dc, L"TriggerLab", -1, &title, DT_LEFT | DT_SINGLELINE);
    if (!narrow) {
        const wchar_t *stages[] = {L"Окно", L"Цвет", L"Проверка", L"Клики"};
        const int current = wizardStep();
        SelectObject(dc, uiFont);
        for (int i = 0; i < 4; ++i) {
            SetTextColor(dc, i < current    ? (settings.darkTheme ? RGB(134, 239, 172) : RGB(22, 163, 74))
                             : i == current ? p.accent
                                            : p.muted);
            const auto value =
                (i < current ? L"✓ " : std::to_wstring(i + 1) + L". ") + std::wstring(stages[i]);
            TextOutW(dc, 548 + i * 132, 112, value.c_str(), static_cast<int>(value.size()));
        }
    }
    SetDCPenColor(dc, p.border);
    auto pen = SelectObject(dc, GetStockObject(DC_PEN));
    MoveToEx(dc, 0, headerHeight - 1, nullptr);
    LineTo(dc, canvasWidth, headerHeight - 1);
    SelectObject(dc, pen);
    RestoreDC(dc, saved);
}

void PreviewWindow::paint(HDC target, RECT rect) {
    const auto p = themeColors(settings.darkTheme);
    const int w = rect.right, h = rect.bottom;
    if (w <= 0 || h <= 0)
        return;
    if (!backBuffer || bufferWidth != w || bufferHeight != h) {
        backBuffer = std::make_unique<PaintSurface>(target, w, h);
        bufferWidth = w;
        bufferHeight = h;
    }
    if (!backBuffer->valid())
        return;
    HDC dc = backBuffer->dc;
    SetDCBrushColor(dc, p.background);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, 0, scaled(headerHeight), w, h);
    mapCanvas(dc, scrollX, scrollY - scaled(headerHeight - ((narrow || !settings.advancedMode) ? 0 : 84)));
    auto oldFont = SelectObject(dc, uiFont);
    if (!settings.advancedMode) {
        paintHome(dc);
        SelectObject(dc, oldFont);
        RestoreDC(dc, savedDc);
        paintHeader(dc, w);
        BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
        return;
    }
    if (narrow) {
        paintNarrow(dc);
        SelectObject(dc, oldFont);
        RestoreDC(dc, savedDc);
        paintHeader(dc, w);
        BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
        return;
    }
    auto line = [&](int x, int y, const std::wstring &text, COLORREF color = CLR_INVALID,
                    HFONT font = nullptr) {
        SelectObject(dc, font ? font : uiFont);
        SetTextColor(dc, color == CLR_INVALID ? p.muted : color);
        TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
    };
    auto wrapped = [&](RECT area, const std::wstring &text, COLORREF color) {
        SelectObject(dc, uiFont);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &area,
                  DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    };
    auto card = [&](int left, int top, int right, int bottom) {
        auto b = SelectObject(dc, GetStockObject(DC_BRUSH));
        auto oldPen = SelectObject(dc, GetStockObject(DC_PEN));
        SetDCBrushColor(dc, p.card);
        SetDCPenColor(dc, p.border);
        RoundRect(dc, left, top, right, bottom, 16, 16);
        SelectObject(dc, b);
        SelectObject(dc, oldPen);
    };
    line(24, 14, L"TriggerLab", p.text, titleFont);
    line(210, 26, settings.advancedMode ? L"Расширенные настройки" : L"Пошаговая настройка");
    const wchar_t *steps[] = {L"Окно", L"Цвет мишени", L"Проверка", L"Клики"};
    const int step = wizardStep();
    for (int i = 0; i < 4; ++i)
        line(24 + i * 270, 62, (i < step ? L"✓ " : std::to_wstring(i + 1) + L". ") + std::wstring(steps[i]),
             i < step    ? (settings.darkTheme ? RGB(134, 239, 172) : RGB(22, 163, 74))
             : i == step ? p.accent
                         : p.muted);
    card(20, 84, 488, 242);
    card(508, 84, 1082, 242);
    card(20, 260, 1082, 406);
    card(20, 424, 1082, 584);
    line(40, 100, L"1. Окно и режим обнаружения", p.text, headingFont);
    line(548, 100, settings.advancedMode ? L"Проверка и запуск" : L"Следующий шаг", p.text, headingFont);
    const bool parameters = settings.advancedMode && !profilePane;
    line(40, 276, parameters ? L"Область и время срабатывания" : L"Профиль тренировки", p.text, headingFont);
    if (!parameters) {
        line(40, 300, profiles.empty() ? L"Пока нет сохранённых профилей" : L"Сохранённые профили");
        line(560, 300, L"Название сценария / тренировки");
        wrapped({40, 370, 825, 392},
                selectedProfile.empty()
                    ? L"Профиль хранит цвет, область и время срабатывания"
                    : L"Профиль: " + selectedProfile + (profileChanged() ? L" · изменён" : L" · сохранён"),
                p.muted);
    }
    wrapped({548, 130, 1062, 180}, currentFeedback().detail, p.muted);
    line(40, 436, L"2. Цвет мишени", p.text, headingFont);
    line(800, 442, L"Текущий цвет");
    HBRUSH swatch = CreateSolidBrush(RGB(color.target.red, color.target.green, color.target.blue));
    RECT patch{800, 482, 1062, 512};
    FillRect(dc, &patch, swatch);
    DeleteObject(swatch);
    card(20, 602, 1082, 788);
    line(40, 620, L"Проверка качества цвета", p.text, headingFont);
    const int previewDc = SaveDC(dc);
    OffsetViewportOrgEx(dc, 0, scaled(210), nullptr);
    card(20, 602, 360, 882);
    card(380, 602, 720, 882);
    card(740, 602, 1082, 938);
    line(40, 620, L"Область прицела", p.text, headingFont);
    line(400, 620, L"Совпадения цвета", p.text, headingFont);
    line(760, 620, L"Состояние", p.text, headingFont);
    RECT image{40, 652, 340, 852}, maskRect{400, 652, 700, 852};
    SetDCBrushColor(dc, p.input);
    FillRect(dc, &image, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    FillRect(dc, &maskRect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (displayFrame) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = displayFrame->width();
        info.bmiHeader.biHeight = -displayFrame->height();
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const int side = 200;
        const int x = 90, mx = 450;
        StretchDIBits(dc, x, 652, side, side, 0, 0, displayFrame->width(), displayFrame->height(),
                      displayFrame->data(), &info, DIB_RGB_COLORS, SRCCOPY);
        triggerlab::Frame maskFrame(displayFrame->width(), displayFrame->height());
        for (std::size_t i = 0; i < displayMask.pixels.size(); ++i) {
            auto value = displayMask.pixels[i];
            maskFrame.data()[i] = {value, value, value, 255};
        }
        StretchDIBits(dc, mx, 652, side, side, 0, 0, maskFrame.width(), maskFrame.height(), maskFrame.data(),
                      &info, DIB_RGB_COLORS, SRCCOPY);
        HBRUSH outline = CreateSolidBrush(
            displayFound ? (settings.darkTheme ? RGB(134, 239, 172) : RGB(22, 163, 74)) : p.accent);
        RECT roiBorder{x, 652, x + side, 852};
        FrameRect(dc, &roiBorder, outline);
        DeleteObject(outline);
        if (displaySettings.center && displaySettings.ignoreRadius > 0) {
            const int width = displayFrame->width(), center = width / 2,
                      radius = displaySettings.ignoreRadius;
            RECT excluded{x + (center - radius) * side / width, 652 + (center - radius) * side / width,
                          x + (center + radius + 1) * side / width,
                          652 + (center + radius + 1) * side / width};
            HBRUSH hatch =
                CreateHatchBrush(HS_DIAGCROSS, settings.darkTheme ? RGB(255, 204, 143) : RGB(180, 83, 9));
            FillRect(dc, &excluded, hatch);
            FrameRect(dc, &excluded, hatch);
            DeleteObject(hatch);
        }
    } else {
        wrapped({65, 720, 320, 780}, L"Начните захват и активируйте выбранное окно", p.muted);
        wrapped({420, 720, 685, 780}, L"Белым показан найденный цвет", p.muted);
    }
    RECT badge{760, 651, 1062, 680};
    SetDCBrushColor(dc, p.input);
    FillRect(dc, &badge, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    line(770, 656, settings.dryRun ? L"ПРОВЕРКА · без кликов" : L"РЕАЛЬНЫЕ КЛИКИ",
         settings.dryRun      ? p.accent
         : settings.darkTheme ? RGB(255, 204, 143)
                              : RGB(180, 83, 9));
    wrapped({760, 693, 1062, 734}, nextHint(), running ? p.accent : p.muted);
    const auto elapsed = gate.elapsed(triggerlab::Clock::now()).count();
    line(760, 742,
         !sample.frame ? (displayFrame ? L"На паузе · прошлый кадр" : L"Ожидаем первый снимок")
         : autoClick && colorFound() && !gate.clickedHover()
             ? L"Ожидание: " + std::to_wstring(std::min<std::int64_t>(elapsed, settings.dwellMs)) + L" / " +
                   std::to_wstring(settings.dwellMs) + L" мс"
         : colorFound() ? L"✓ Цвет найден"
                        : L"Цвет пока не найден",
         colorFound() ? (settings.darkTheme ? RGB(134, 239, 172) : RGB(22, 163, 74)) : p.muted);
    RECT waiting{760, 683, 1062, 687};
    SetDCBrushColor(dc, p.border);
    FillRect(dc, &waiting, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    waiting.right =
        waiting.left + static_cast<LONG>(302 * std::clamp(double(elapsed) / settings.dwellMs, 0.0, 1.0));
    SetDCBrushColor(dc, p.accent);
    FillRect(dc, &waiting, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    wrapped({760, 771, 1062, 811}, clickStatus, p.text);
    line(760, 817,
         L"Клики: " + std::to_wstring(clickCount) + L"   Проверки: " + std::to_wstring(diagnosticCount));
    line(760, 845,
         sample.frame ? L"Найдено: " + std::to_wstring(static_cast<int>(mask.coverage * 100)) +
                            L"%  /  порог " + std::to_wstring(settings.minimumPercent) + L"%"
                      : L"Свежего кадра пока нет");
    RECT track{760, 871, 1062, 875};
    SetDCBrushColor(dc, p.border);
    FillRect(dc, &track, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    track.right = track.left + static_cast<LONG>(302 * std::clamp(mask.coverage, 0.0, 1.0));
    SetDCBrushColor(dc, p.accent);
    FillRect(dc, &track, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    line(40, 858,
         displayFrame
             ? L"Снимок: " + std::to_wstring((GetTickCount64() - displayTime) / 1000) + L" сек. назад"
             : L"Снимок появится после запуска");
    line(400, 858, settings.center ? L"Перекрестие исключено" : L"Проверка пикселя курсора");
    wrapped({760, 886, 1062, 934}, pressTimingText(), p.muted);
    line(24, 950, saveState(),
         settingsDirty() || colorDirty() ? (settings.darkTheme ? RGB(255, 204, 143) : RGB(180, 83, 9))
                                         : p.muted);
    wrapped({24, 974, 1062, 994}, notice, p.muted);
    line(860, 950, L"F9 / Esc — остановить");
    RestoreDC(dc, previewDc);
    SelectObject(dc, oldFont);
    RestoreDC(dc, savedDc);
    paintHeader(dc, w);
    BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
}

} // namespace triggerlab::win32
