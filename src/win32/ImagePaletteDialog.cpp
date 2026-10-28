#include "ImagePaletteDialog.hpp"
#include <array>
#include <cmath>
#include <commctrl.h>
#include <stdexcept>
#include <string>
#include <windows.h>
#include <windowsx.h>

namespace triggerlab {
namespace {
struct Editor {
    const Frame &image;
    HWND window = nullptr;
    Region display{}, region;
    std::vector<ImageColor> palette;
    std::array<HWND, 5> buttons{};
    UINT dpi = 96;
    HFONT font = nullptr, canvasFont = nullptr;
    bool dark = false, arranging = false;
    int scrollY = 0, canvasHeight = 665, wheel = 0, bufferWidth = 0, bufferHeight = 0;
    RECT instruction{}, regionText{}, hintText{}, swatches[5]{};
    std::array<RECT, 8> controls{};
    int scaled(int v) const {
        return MulDiv(v, dpi, 96);
    }
    POINT logical(POINT p) const {
        return {MulDiv(p.x, 96, dpi), MulDiv(p.y + scrollY, 96, dpi)};
    }
    int selected = 0;
    POINT start{}, end{};
    bool dragging = false, done = false, accepted = false;
    HDC imageDc = nullptr, bufferDc = nullptr;
    HBITMAP imageBitmap = nullptr, bufferBitmap = nullptr;
    HGDIOBJ imagePrevious = nullptr, bufferPrevious = nullptr;
    explicit Editor(const Frame &value, bool darkTheme = false)
        : image(value), region{0, 0, value.width(), value.height()}, dark(darkTheme) {}
    void setDpi(UINT value) {
        dpi = value ? value : 96;
        HFONT next = CreateFontW(-scaled(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                 L"Segoe UI");
        if (!next)
            throw std::runtime_error("Palette font unavailable");
        if (!canvasFont) {
            canvasFont =
                CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            if (!canvasFont) {
                DeleteObject(next);
                throw std::runtime_error("Palette canvas font unavailable");
            }
        }
        for (HWND button : buttons)
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(next), FALSE);
        for (int id : {1, 2, 3})
            SendMessageW(GetDlgItem(window, id), WM_SETFONT, reinterpret_cast<WPARAM>(next), FALSE);
        if (font)
            DeleteObject(font);
        font = next;
        arrange();
    }
    void arrange() {
        if (arranging || !buttons[0])
            return;
        arranging = true;
        dragging = false;
        if (GetCapture() == window)
            ReleaseCapture();
        RECT client{};
        GetClientRect(window, &client);
        const int totalWidth = client.right + ((GetWindowLongW(window, GWL_STYLE) & WS_VSCROLL)
                                                   ? GetSystemMetricsForDpi(SM_CXVSCROLL, dpi)
                                                   : 0);
        // Reserve the scrollbar width consistently, even when it is hidden.
        const int width = std::max(120, (totalWidth - GetSystemMetricsForDpi(SM_CXVSCROLL, dpi)) * 96 /
                                            static_cast<int>(dpi));
        const bool wide = width >= 760;
        instruction = {20, 16, width - 20, 70};
        const int imageWidth = wide ? width - 260 : width - 40, imageHeight = wide ? 430 : 240;
        const double ratio =
            std::min(double(imageWidth) / image.width(), double(imageHeight) / image.height());
        const int w = std::max(1, static_cast<int>(std::lround(image.width() * ratio))),
                  h = std::max(1, static_cast<int>(std::lround(image.height() * ratio)));
        display = {20 + (imageWidth - w) / 2, 80 + (imageHeight - h) / 2, w, h};
        const int columns = wide ? 1 : width >= 460 ? 2 : 1;
        int y = wide ? 80 : 80 + imageHeight + 18;
        const int cell = wide ? 210 : (width - 40 - 12 * (columns - 1)) / columns;
        for (int i = 0; i < 5; ++i) {
            const int x = wide ? width - 230 : 20 + (i % columns) * (cell + 12), top = y + (i / columns) * 72;
            swatches[i] = {x, top + 6, x + 24, top + 56};
            controls[i] = {x + 34, top, x + cell, top + 64};
        }
        y = wide ? 80 + imageHeight + 18 : y + ((5 + columns - 1) / columns) * 72 + 14;
        regionText = {20, y, width - 20, y + 48};
        y += 52;
        hintText = {20, y, width - 20, y + 64};
        y += 72;
        if (width >= 600) {
            const int buttonWidth = (width - 64) / 3;
            for (int i = 0; i < 3; ++i)
                controls[5 + i] = {20 + i * (buttonWidth + 12), y, 20 + i * (buttonWidth + 12) + buttonWidth,
                                   y + 40};
            y += 48;
        } else {
            for (int i = 0; i < 3; ++i) {
                controls[5 + i] = {20, y, width - 20, y + 40};
                y += 48;
            }
        }
        canvasHeight = y + 12;
        ShowScrollBar(window, SB_VERT, client.bottom < scaled(canvasHeight));
        GetClientRect(window, &client);
        SCROLLINFO info{sizeof(info),
                        SIF_RANGE | SIF_PAGE | SIF_POS,
                        0,
                        scaled(canvasHeight) - 1,
                        static_cast<UINT>(client.bottom),
                        scrollY};
        SetScrollInfo(window, SB_VERT, &info, TRUE);
        info.fMask = SIF_POS;
        GetScrollInfo(window, SB_VERT, &info);
        scrollY = info.nPos;
        for (int i = 0; i < 8; ++i) {
            HWND child = i < 5 ? buttons[i] : GetDlgItem(window, i == 5 ? 3 : i == 6 ? 1 : 2);
            const auto r = controls[i];
            SetWindowPos(child, nullptr, scaled(r.left), scaled(r.top) - scrollY, scaled(r.right - r.left),
                         scaled(r.bottom - r.top), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        arranging = false;
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    void reveal(HWND child) {
        RECT r{}, client{};
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&r), 2);
        GetClientRect(window, &client);
        if (r.top < scaled(12))
            scrollY += r.top - scaled(12);
        else if (r.bottom > client.bottom - scaled(12))
            scrollY += r.bottom - client.bottom + scaled(12);
        arrange();
    }
    static LRESULT CALLBACK childProcedure(HWND child, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                           DWORD_PTR context) {
        auto &app = *reinterpret_cast<Editor *>(context);
        if (message == WM_SETFOCUS)
            app.reveal(child);
        if (message == WM_MOUSEWHEEL)
            return SendMessageW(app.window, message, wp, lp);
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(child, childProcedure, id);
        return DefSubclassProc(child, message, wp, lp);
    }
    void drawButton(DRAWITEMSTRUCT item) {
        const bool enabled = !(item.itemState & ODS_DISABLED);
        const int saved = SaveDC(item.hDC);
        SetDCBrushColor(item.hDC, dark ? RGB(77, 83, 94) : RGB(241, 245, 249));
        FillRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SelectObject(item.hDC, font);
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, enabled ? (dark ? RGB(241, 243, 247) : RGB(30, 41, 59)) : RGB(128, 128, 128));
        wchar_t caption[256]{};
        GetWindowTextW(item.hwndItem, caption, 256);
        RECT r = item.rcItem;
        InflateRect(&r, -scaled(6), -scaled(4));
        DrawTextW(item.hDC, caption, -1, &r, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        if (item.itemState & (ODS_FOCUS | ODS_SELECTED))
            DrawFocusRect(item.hDC, &r);
        RestoreDC(item.hDC, saved);
    }
    ~Editor() {
        if (font)
            DeleteObject(font);
        if (canvasFont)
            DeleteObject(canvasFont);
        if (imageDc && imagePrevious)
            SelectObject(imageDc, imagePrevious);
        if (bufferDc && bufferPrevious)
            SelectObject(bufferDc, bufferPrevious);
        if (imageBitmap)
            DeleteObject(imageBitmap);
        if (bufferBitmap)
            DeleteObject(bufferBitmap);
        if (imageDc)
            DeleteDC(imageDc);
        if (bufferDc)
            DeleteDC(bufferDc);
    }
    void refresh() {
        palette = imagePalette(image, region);
        selected = 0;
        for (int i = 0; i < 5; ++i) {
            std::wstring label = L"—";
            if (i < static_cast<int>(palette.size())) {
                const auto p = palette[i].color;
                label = L"RGB " + std::to_wstring(p.red) + L", " + std::to_wstring(p.green) + L", " +
                        std::to_wstring(p.blue) + L"\n" +
                        std::to_wstring(static_cast<int>(palette[i].coverage * 100)) + L"% области";
            }
            SetWindowTextW(buttons[i], label.c_str());
            EnableWindow(buttons[i], i < static_cast<int>(palette.size()));
        }
        EnableWindow(GetDlgItem(window, 1), !palette.empty());
        InvalidateRect(window, nullptr, FALSE);
    }
    void create() {
        for (int i = 0; i < 5; ++i)
            buttons[i] = CreateWindowW(
                L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 790, 65 + i * 86, 175, 64,
                window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(10 + i)), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Всё изображение", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 20,
                      610, 190, 32, window, reinterpret_cast<HMENU>(3), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Применить цвет", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 590,
                      610, 180, 32, window, reinterpret_cast<HMENU>(1), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Отмена", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 790, 610, 175,
                      32, window, reinterpret_cast<HMENU>(2), nullptr, nullptr);
        EnumChildWindows(
            window,
            [](HWND child, LPARAM context) -> BOOL {
                SetWindowSubclass(child, childProcedure, 1, context);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
        setDpi(GetDpiForWindow(window));
        refresh();
    }
    void render(HDC dc) {
        RECT client{};
        GetClientRect(window, &client);
        SetDCBrushColor(dc, dark ? RGB(48, 51, 57) : RGB(242, 246, 252));
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const int saved = SaveDC(dc);
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowExtEx(dc, 96, 96, nullptr);
        SetViewportExtEx(dc, dpi, dpi, nullptr);
        SetViewportOrgEx(dc, 0, -scrollY, nullptr);
        // Owner-drawn canvas coordinates are logical; child controls use device-pixel fonts.
        SelectObject(dc, canvasFont);
        SetTextColor(dc, dark ? RGB(241, 243, 247) : RGB(30, 41, 59));
        SetBkMode(dc, TRANSPARENT);
        auto text = [&](RECT area, const std::wstring &s) {
            DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &area,
                      DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
        };
        text(instruction, L"Выберите пиксель или выделите область мышью. Затем выберите оттенок и нажмите "
                          L"«Применить цвет».");
        // Checkerboard makes transparency visible instead of implying a black background.
        for (int y = static_cast<int>(display.y); y < display.y + display.height; y += 16)
            for (int x = static_cast<int>(display.x); x < display.x + display.width; x += 16) {
                RECT tile{x, y, static_cast<LONG>(std::min<std::int64_t>(x + 16, display.x + display.width)),
                          static_cast<LONG>(std::min<std::int64_t>(y + 16, display.y + display.height))};
                SetDCBrushColor(
                    dc, ((x - static_cast<int>(display.x)) / 16 + (y - static_cast<int>(display.y)) / 16) % 2
                            ? RGB(65, 65, 65)
                            : RGB(110, 110, 110));
                FillRect(dc, &tile, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            }
        // Alpha-blend a premultiplied DIB over the checkerboard.
        if (!imageDc) {
            imageDc = CreateCompatibleDC(dc);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = image.width();
            info.bmiHeader.biHeight = -image.height();
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            void *bits = nullptr;
            imageBitmap = imageDc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
            if (!imageBitmap || !bits)
                throw std::runtime_error("Image surface unavailable");
            imagePrevious = SelectObject(imageDc, imageBitmap);
            if (!imagePrevious || imagePrevious == HGDI_ERROR) {
                imagePrevious = nullptr;
                throw std::runtime_error("Image bitmap selection failed");
            }
            auto *pixels = static_cast<Pixel *>(bits);
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    auto p = image.at(x, y);
                    p.red = static_cast<std::uint8_t>(unsigned(p.red) * p.alpha / 255);
                    p.green = static_cast<std::uint8_t>(unsigned(p.green) * p.alpha / 255);
                    p.blue = static_cast<std::uint8_t>(unsigned(p.blue) * p.alpha / 255);
                    pixels[y * image.width() + x] = p;
                }
        }
        AlphaBlend(dc, static_cast<int>(display.x), static_cast<int>(display.y),
                   static_cast<int>(display.width), static_cast<int>(display.height), imageDc, 0, 0,
                   image.width(), image.height(), {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA});
        Region outline = region;
        if (dragging)
            outline = *imageSelection(image.width(), image.height(), display, start.x, start.y, end.x, end.y);
        RECT border{
            static_cast<LONG>(display.x + outline.x * display.width / image.width()),
            static_cast<LONG>(display.y + outline.y * display.height / image.height()),
            static_cast<LONG>(display.x + (outline.x + outline.width) * display.width / image.width()),
            static_cast<LONG>(display.y + (outline.y + outline.height) * display.height / image.height())};
        FrameRect(dc, &border, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
            const auto p = palette[i].color;
            SetDCBrushColor(dc, RGB(p.red, p.green, p.blue));
            RECT swatch = swatches[i];
            FillRect(dc, &swatch, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if (i == selected) {
                InflateRect(&swatch, 2, 2);
                FrameRect(dc, &swatch, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            }
        }
        text(regionText, L"Область: " + std::to_wstring(region.x) + L", " + std::to_wstring(region.y) +
                             L" | " + std::to_wstring(region.width) + L" × " +
                             std::to_wstring(region.height) + L" пикселей");
        text(hintText, palette.empty()
                           ? L"Нет непрозрачных пикселей в области. Выделите другой фрагмент."
                           : L"Применить — выбрать цвет; сохранение выполняется в основном окне.");
        RestoreDC(dc, saved);
    }
    void paint(HDC dc) {
        RECT client{};
        GetClientRect(window, &client);
        if (client.right <= 0 || client.bottom <= 0)
            return;
        if (bufferDc && (bufferWidth != client.right || bufferHeight != client.bottom)) {
            SelectObject(bufferDc, bufferPrevious);
            DeleteObject(bufferBitmap);
            DeleteDC(bufferDc);
            bufferDc = nullptr;
            bufferBitmap = nullptr;
            bufferPrevious = nullptr;
        }
        if (!bufferDc) {
            bufferDc = CreateCompatibleDC(dc);
            bufferBitmap = bufferDc ? CreateCompatibleBitmap(dc, client.right, client.bottom) : nullptr;
            if (!bufferBitmap)
                throw std::runtime_error("Paint buffer unavailable");
            bufferPrevious = SelectObject(bufferDc, bufferBitmap);
            if (!bufferPrevious || bufferPrevious == HGDI_ERROR) {
                bufferPrevious = nullptr;
                throw std::runtime_error("Paint bitmap selection failed");
            }
        }
        bufferWidth = client.right;
        bufferHeight = client.bottom;
        render(bufferDc);
        BitBlt(dc, 0, 0, client.right, client.bottom, bufferDc, 0, 0, SRCCOPY);
    }
};
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto *app = reinterpret_cast<Editor *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<Editor *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        app->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(hwnd, message, wp, lp);
    try {
        switch (message) {
        case WM_CREATE:
            app->create();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == 1 && !app->palette.empty()) {
                app->accepted = true;
                DestroyWindow(hwnd);
            } else if (LOWORD(wp) == 2)
                DestroyWindow(hwnd);
            else if (LOWORD(wp) == 3) {
                app->region = {0, 0, app->image.width(), app->image.height()};
                app->refresh();
            } else if (LOWORD(wp) >= 10 && LOWORD(wp) < 10 + app->palette.size()) {
                app->selected = LOWORD(wp) - 10;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            POINT p = app->logical({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            const auto r = app->display;
            if (p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height) {
                app->start = app->end = p;
                app->dragging = true;
                SetCapture(hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (app->dragging) {
                app->end = app->logical({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (app->dragging) {
                app->end = app->logical({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
                app->region = *imageSelection(app->image.width(), app->image.height(), app->display,
                                              app->start.x, app->start.y, app->end.x, app->end.y);
                app->dragging = false;
                ReleaseCapture();
                app->refresh();
            }
            return 0;
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            app->dragging = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_SIZE:
            app->arrange();
            return 0;
        case WM_DPICHANGED: {
            app->setDpi(HIWORD(wp));
            auto r = *reinterpret_cast<RECT *>(lp);
            SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            app->arrange();
            return 0;
        }
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO *>(lp)->ptMinTrackSize = {320, 320};
            return 0;
        case WM_DRAWITEM:
            app->drawButton(*reinterpret_cast<DRAWITEMSTRUCT *>(lp));
            return TRUE;
        case WM_VSCROLL: {
            SCROLLINFO info{sizeof(info), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &info);
            switch (LOWORD(wp)) {
            case SB_LINEUP:
                app->scrollY -= app->scaled(36);
                break;
            case SB_LINEDOWN:
                app->scrollY += app->scaled(36);
                break;
            case SB_PAGEUP:
                app->scrollY -= info.nPage;
                break;
            case SB_PAGEDOWN:
                app->scrollY += info.nPage;
                break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                app->scrollY = info.nTrackPos;
                break;
            case SB_TOP:
                app->scrollY = 0;
                break;
            case SB_BOTTOM:
                app->scrollY = info.nMax;
                break;
            }
            app->arrange();
            return 0;
        }
        case WM_MOUSEWHEEL:
            app->wheel += GET_WHEEL_DELTA_WPARAM(wp);
            app->scrollY -= app->wheel / WHEEL_DELTA * app->scaled(108);
            app->wheel %= WHEEL_DELTA;
            app->arrange();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            try {
                app->paint(dc);
            } catch (...) {
                EndPaint(hwnd, &ps);
                throw;
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            app->done = true;
            return 0;
        }
    } catch (const std::exception &) {
        app->accepted = false;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
HWND createEditor(HWND owner, Editor &editor) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = procedure;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TriggerLabImagePalette";
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor))
        return nullptr;
    RECT bounds{0, 0, MulDiv(990, dpi, 96), MulDiv(710, dpi, 96)};
    AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const auto work = monitor.rcWork;
    const int w = std::min(bounds.right - bounds.left, work.right - work.left),
              h = std::min(bounds.bottom - bounds.top, work.bottom - work.top);
    return CreateWindowExW(
        WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Область изображения и палитра",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left + (work.right - work.left - w) / 2,
        work.top + (work.bottom - work.top - h) / 2, w, h, owner, nullptr, wc.hInstance, &editor);
}
} // namespace
std::optional<ImageColor> showImagePalette(HWND owner, const Frame &image, bool darkTheme) {
    Editor editor(image, darkTheme);
    HWND window = createEditor(owner, editor);
    if (!window)
        return {};
    const bool enabled = IsWindowEnabled(owner) != FALSE;
    if (enabled)
        EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (!editor.done) {
        const auto status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            DestroyWindow(window);
            if (status == 0)
                PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            DestroyWindow(window);
            continue;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (enabled)
        EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (!editor.accepted || editor.palette.empty())
        return {};
    return editor.palette[editor.selected];
}
#ifdef TRIGGERLAB_UI_TESTS
#include "../../tests/win32/ImagePaletteTests.inl"
#endif

} // namespace triggerlab
