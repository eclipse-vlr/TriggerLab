bool windowPickerDialogSmoke() {
    HWND target = CreateWindowW(L"STATIC", L"Тест окна — Unicode 窗口", WS_OVERLAPPEDWINDOW, 0, 0, 240, 180,
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
    const auto choices = enumerateWindows(nullptr);
    const auto found =
        std::find_if(choices.begin(), choices.end(), [&](const auto &c) { return c.window == target; });
    if (found == choices.end() || found->title != L"Тест окна — Unicode 窗口" || found->application.empty() ||
        !windowChoiceAvailable(*found, nullptr))
        return false;
    auto wrong = *found;
    wrong.process = 0;
    if (windowChoiceAvailable(wrong, nullptr) || windowChoiceAvailable(*found, target))
        return false;
    for (bool dark : {false, true}) {
        Picker picker(nullptr, dark, target);
        if (!createPicker(picker))
            return false;
        picker.populate({*found});
        if (SendMessageW(picker.list, LB_GETCURSEL, 0, 0) != 0 || !IsWindowEnabled(picker.buttons[0]))
            return false;
        for (UINT dpi : {96u, 120u, 144u, 192u}) {
            RECT suggested{40, 40, 680, 520};
            SendMessageW(picker.window, WM_DPICHANGED, MAKELONG(dpi, dpi),
                         reinterpret_cast<LPARAM>(&suggested));
            LOGFONTW font{};
            GetObjectW(picker.font, sizeof(font), &font);
            RECT client{};
            GetClientRect(picker.window, &client);
            if (picker.dpi != dpi || font.lfHeight != -picker.scaled(16))
                return false;
            HDC dc = GetDC(picker.list);
            if (!dc)
                return false;
            const auto old = SelectObject(dc, picker.font);
            const auto label = found->title + L" — " + found->application;
            SIZE extent{};
            GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &extent);
            SelectObject(dc, old);
            ReleaseDC(picker.list, dc);
            if (SendMessageW(picker.list, LB_GETHORIZONTALEXTENT, 0, 0) < extent.cx)
                return false;
            std::vector<RECT> rectangles;
            for (HWND control : {picker.hint, picker.list, picker.buttons[0], picker.buttons[1],
                                 picker.buttons[2], picker.buttons[3]}) {
                RECT r{};
                GetWindowRect(control, &r);
                MapWindowPoints(nullptr, picker.window, reinterpret_cast<POINT *>(&r), 2);
                if (r.left < 0 || r.top < 0 || r.right > client.right || r.bottom > client.bottom)
                    return false;
                for (auto previous : rectangles) {
                    RECT overlap{};
                    if (IntersectRect(&overlap, &r, &previous))
                        return false;
                }
                rectangles.push_back(r);
            }
        }
        SendMessageW(picker.window, WM_COMMAND, 1, 0);
        if (!picker.done || !picker.result.choice || picker.result.choice->window != target)
            return false;
    }
    Picker empty(nullptr, false, nullptr);
    if (!createPicker(empty))
        return false;
    empty.populate({});
    SendMessageW(empty.window, WM_COMMAND, 1, 0);
    if (empty.done || IsWindowEnabled(empty.buttons[0]))
        return false;
    SendMessageW(empty.window, WM_COMMAND, 3, 0);
    if (!empty.done || empty.result.choice || empty.result.manual)
        return false;
    Picker changing(nullptr, true, target);
    if (!createPicker(changing))
        return false;
    changing.populate({*found});
    HWND added = CreateWindowW(L"STATIC", L"New refresh target", WS_OVERLAPPEDWINDOW, 0, 0, 240, 180, nullptr,
                               nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!added)
        return false;
    Cleanup addedCleanup{added};
    ShowWindow(added, SW_SHOWNOACTIVATE);
    SetWindowTextW(target, L"Renamed refresh target");
    SendMessageW(changing.window, WM_COMMAND, 6, 0);
    auto indexOf = [&](HWND candidate) {
        const auto it = std::find_if(changing.rows.begin(), changing.rows.end(),
                                     [&](const auto &c) { return c.window == candidate; });
        return it == changing.rows.end() ? -1 : static_cast<int>(it - changing.rows.begin());
    };
    const int kept = indexOf(target), newIndex = indexOf(added);
    if (kept < 0 || newIndex < 0 || changing.rows[kept].title != L"Renamed refresh target" ||
        SendMessageW(changing.list, LB_GETCURSEL, 0, 0) != kept || changing.result.choice)
        return false;
    SendMessageW(changing.list, LB_SETCURSEL, newIndex, 0);
    SendMessageW(changing.window, WM_COMMAND, MAKEWPARAM(4, LBN_SELCHANGE), 0);
    SendMessageW(changing.window, WM_COMMAND, 6, 0);
    if (SendMessageW(changing.list, LB_GETCURSEL, 0, 0) != indexOf(added) ||
        !IsWindowEnabled(changing.buttons[0]))
        return false;
    DestroyWindow(added);
    addedCleanup.target = nullptr;
    SendMessageW(changing.window, WM_COMMAND, 6, 0);
    if (indexOf(added) >= 0 || SendMessageW(changing.list, LB_GETCURSEL, 0, 0) != LB_ERR ||
        IsWindowEnabled(changing.buttons[0]) || changing.result.choice)
        return false;
    SendMessageW(changing.window, WM_COMMAND, 3, 0);
    if (!changing.done || changing.result.choice || changing.result.manual)
        return false;
    Picker closed(nullptr, true, target);
    if (!createPicker(closed))
        return false;
    closed.populate({*found});
    DestroyWindow(target);
    cleanup.target = nullptr;
    SendMessageW(closed.window, WM_COMMAND, 1, 0);
    if (closed.done || closed.result.choice)
        return false;
    SendMessageW(closed.window, WM_COMMAND, 2, 0);
    return closed.done && closed.result.manual && !closed.result.choice;
}
