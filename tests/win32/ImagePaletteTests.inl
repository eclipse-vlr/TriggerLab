bool imagePaletteDialogSmoke() {
    Frame image(2, 1);
    image.at(0, 0) = {0, 0, 255, 255};
    image.at(1, 0) = {255, 0, 0, 255};
    Editor editor(image);
    auto window = createEditor(nullptr, editor);
    if (!window)
        return false;
    bool ok = editor.palette.size() == 2 && IsWindowEnabled(GetDlgItem(window, 1));
    ok = ok && SendMessageW(editor.buttons[0], WM_GETFONT, 0, 0) != 0;
    SendMessageW(window, WM_COMMAND, 11, 0);
    ok = ok && editor.selected == 1;
    const int x = editor.scaled(static_cast<int>(editor.display.x)),
              y = editor.scaled(static_cast<int>(editor.display.y)) - editor.scrollY;
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    ok = ok && editor.region.width == 1 && editor.palette.size() == 1 && editor.palette[0].color.red == 255;
    SendMessageW(window, WM_COMMAND, 3, 0);
    ok = ok && editor.palette.size() == 2;
    for (UINT dpi : {96u, 120u, 144u, 192u})
        for (bool dark : {false, true}) {
            editor.dark = dark;
            // Exercise the window-message path used during a monitor transition,
            // including its suggested outer bounds. No system DPI setting changes.
            SendMessageW(window, WM_COMMAND, 11, 0);
            const auto selectedBefore = editor.palette[editor.selected].color;
            const Region regionBefore = editor.region;
            editor.dragging = true;
            SetCapture(window);
            RECT suggested{40, 40, 680, 520};
            SendMessageW(window, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
            RECT actual{};
            GetWindowRect(window, &actual);
            ok = ok && editor.dpi == dpi && EqualRect(&actual, &suggested) && !editor.dragging &&
                 GetCapture() != window;
            ok = ok && editor.region.x == regionBefore.x && editor.region.y == regionBefore.y &&
                 editor.region.width == regionBefore.width && editor.region.height == regionBefore.height;
            ok = ok && editor.selected == 1 &&
                 editor.palette[editor.selected].color.red == selectedBefore.red &&
                 editor.palette[editor.selected].color.green == selectedBefore.green &&
                 editor.palette[editor.selected].color.blue == selectedBefore.blue;
            for (int width : {640, 1280}) {
                SetWindowPos(window, nullptr, 0, 0, width, 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                editor.scrollY = 0;
                editor.arrange();
                LOGFONTW font{};
                GetObjectW(editor.font, sizeof(font), &font);
                ok = ok && font.lfHeight == -editor.scaled(16);
                RECT client{};
                GetClientRect(window, &client);
                for (int id : {10, 11, 12, 13, 14, 1, 2, 3}) {
                    RECT r{};
                    GetWindowRect(GetDlgItem(window, id), &r);
                    MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&r), 2);
                    ok = ok && r.left >= 0 && r.right <= client.right;
                    for (int other : {10, 11, 12, 13, 14, 1, 2, 3})
                        if (other != id) {
                            RECT second{}, overlap{};
                            GetWindowRect(GetDlgItem(window, other), &second);
                            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&second), 2);
                            ok = ok && !IntersectRect(&overlap, &r, &second);
                        }
                }
                // Physical mouse coordinates must map to the same source pixels at every DPI.
                for (int pixel : {0, 1}) {
                    editor.scrollY = editor.scaled(40);
                    editor.arrange();
                    const int px = editor.scaled(static_cast<int>(editor.display.x) +
                                                 (pixel ? static_cast<int>(editor.display.width) - 1 : 0));
                    const int py = editor.scaled(static_cast<int>(editor.display.y)) - editor.scrollY;
                    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(px, py));
                    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(px, py));
                    ok = ok && editor.region.x == pixel && editor.region.width == 1 &&
                         editor.palette.size() == 1;
                    ok = ok &&
                         (pixel ? editor.palette[0].color.blue == 255 : editor.palette[0].color.red == 255);
                }
                SendMessageW(window, WM_COMMAND, 3, 0);
                ok = ok && editor.palette.size() == 2;
                editor.reveal(GetDlgItem(window, 1));
                RECT apply{};
                GetWindowRect(GetDlgItem(window, 1), &apply);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&apply), 2);
                ok = ok && apply.top >= 0 && apply.bottom <= client.bottom;
                HDC resized = GetDC(window);
                editor.paint(resized);
                ReleaseDC(window, resized);
            }
        }
    HDC dc = GetDC(window);
    editor.paint(dc);
    const DWORD resources = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 20; ++i)
        editor.paint(dc);
    ok = ok && GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == resources;
    ReleaseDC(window, dc);
    SendMessageW(window, WM_COMMAND, 2, 0);
    ok = ok && editor.done && !editor.accepted;
    Editor accepted(image);
    auto second = createEditor(nullptr, accepted);
    if (!second)
        return false;
    SendMessageW(second, WM_COMMAND, 11, 0);
    SendMessageW(second, WM_COMMAND, 1, 0);
    return ok && accepted.done && accepted.accepted && accepted.selected == 1;
}
