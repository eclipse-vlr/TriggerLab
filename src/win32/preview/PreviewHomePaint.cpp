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

void PreviewWindow::paintHome(HDC dc) {
    const auto p = themeColors(settings.darkTheme);
    auto text = [&](RECT r, const std::wstring &value, COLORREF color, HFONT font = nullptr) {
        SelectObject(dc, font ? font : uiFont);
        SetTextColor(dc, color);
        DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &r,
                  DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    };
    auto card = [&](RECT r, const std::wstring &title) {
        auto brush = SelectObject(dc, GetStockObject(DC_BRUSH)),
             pen = SelectObject(dc, GetStockObject(DC_PEN));
        SetDCBrushColor(dc, p.card);
        SetDCPenColor(dc, p.border);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, 16, 16);
        SelectObject(dc, brush);
        SelectObject(dc, pen);
        text({r.left + 16, r.top + 16, r.right - 16, r.top + 60}, title, p.text, headingFont);
    };
    const auto feedback = currentFeedback();
    card(runCard, std::to_wstring(wizardStep() + 1) + L" / 4 · " + feedback.title);
    RECT marker{runCard.left + 1, runCard.top + 16, runCard.left + 5, runCard.bottom - 16};
    SetDCBrushColor(dc, feedbackColor(feedback.tone));
    FillRect(dc, &marker, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    card(windowCard, L"Окно тренировки");
    card(colorCard, L"Цвет мишени");
    if (windowCard.right - windowCard.left >= 340)
        text({windowCard.left + 16, windowCard.top + 238, windowCard.right - 16, windowCard.bottom - 12},
             L"Прицел перемещаете вы. Анализатор проверяет цвет в выбранном окне.", p.muted);
    text({colorCard.left + 16, colorCard.top + 240, colorCard.right - 16, colorCard.top + 264},
         colorReady ? L"Выбранный цвет" : L"Цвет ещё не выбран", p.muted);
    RECT swatch{colorCard.left + 16, colorCard.top + 272, colorCard.right - 16, colorCard.top + 302};
    SetDCBrushColor(dc, colorReady ? RGB(color.target.red, color.target.green, color.target.blue) : p.input);
    FillRect(dc, &swatch, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    text({colorCard.left + 16, colorCard.top + 314, colorCard.right - 16, colorCard.top + 374},
         L"Выберите оттенок мишени, затем сохраните его. Допуск помогает учитывать близкие оттенки.",
         p.muted);
    card(stateCard, L"Результат и состояние");
    if (homeDetails) {
        card(optionsCard, L"Профили тренировки");
        text({optionsCard.left + 16, optionsCard.top + 50, optionsCard.right - 16, optionsCard.top + 74},
             profiles.empty() ? L"Пока нет сохранённых профилей" : L"Сохранённые профили", p.muted);
        text({optionsCard.left + 16, optionsCard.top + 170, optionsCard.right - 16, optionsCard.top + 194},
             L"Название профиля", p.muted);
        text({optionsCard.left + 16, optionsCard.top + 286, optionsCard.right - 16, optionsCard.bottom - 12},
             selectedProfile.empty()
                 ? L"Сохраните цвет и параметры для следующего запуска"
                 : L"Профиль: " + selectedProfile + (profileChanged() ? L" · изменён" : L" · сохранён"),
             p.muted);
        card(qualityCard, L"Мишень и фон");
        for (int i = 0; i < 2; ++i) {
            const auto r = previewCards[i];
            card(r, i == 0 ? L"Последний снимок" : L"Маска найденного цвета");
            const int side = std::min(200, static_cast<int>(r.right - r.left) - 32),
                      x = r.left + (r.right - r.left - side) / 2, y = r.top + 56;
            RECT image{x, y, x + side, y + side};
            SetDCBrushColor(dc, p.input);
            FillRect(dc, &image, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if (displayFrame) {
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = displayFrame->width();
                info.bmiHeader.biHeight = -displayFrame->height();
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                info.bmiHeader.biCompression = BI_RGB;
                triggerlab::Frame maskFrame(displayFrame->width(), displayFrame->height());
                if (i == 1)
                    for (std::size_t n = 0; n < displayMask.pixels.size(); ++n) {
                        const auto value = displayMask.pixels[n];
                        maskFrame.data()[n] = {value, value, value, 255};
                    }
                StretchDIBits(dc, x, y, side, side, 0, 0, displayFrame->width(), displayFrame->height(),
                              i == 0 ? displayFrame->data() : maskFrame.data(), &info, DIB_RGB_COLORS,
                              SRCCOPY);
                HBRUSH border = CreateSolidBrush(displayFound ? p.accent : p.border);
                FrameRect(dc, &image, border);
                DeleteObject(border);
                if (i == 0 && displaySettings.center && displaySettings.ignoreRadius > 0) {
                    const int width = displayFrame->width(), center = width / 2,
                              radius = displaySettings.ignoreRadius;
                    RECT excluded{x + (center - radius) * side / width, y + (center - radius) * side / width,
                                  x + (center + radius + 1) * side / width,
                                  y + (center + radius + 1) * side / width};
                    HBRUSH hatch = CreateHatchBrush(HS_DIAGCROSS, p.accent);
                    FillRect(dc, &excluded, hatch);
                    DeleteObject(hatch);
                }
            } else
                text({r.left + 16, y + 44, r.right - 16, y + 130}, L"Снимок появится после запуска проверки",
                     p.muted);
            text({r.left + 16, r.top + 264, r.right - 16, r.bottom - 10},
                 displayFrame ? L"Сохранённый кадр · " +
                                    std::to_wstring((GetTickCount64() - displayTime) / 1000) + L" сек. назад"
                              : L"Ожидаем первый снимок",
                 p.muted);
        }
    }
    text({24, footerY, canvasWidth - 24, footerY + 50}, saveState(), p.muted);
    text({24, footerY + 52, canvasWidth - 24, footerY + 132}, notice, p.muted);
}

} // namespace triggerlab::win32
