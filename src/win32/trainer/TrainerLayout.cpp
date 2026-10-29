#include "trainer/TrainerWindow.hpp"
#include <algorithm>

namespace triggerlab::win32 {
int TrainerWindow::scaled(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

void TrainerWindow::setDpi(UINT value) {
    dpi_ = value ? value : 96;
    HFONT next =
        CreateFontW(-scaled(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (next) {
        for (const auto &entry : controlRects_)
            SendMessageW(entry.control, WM_SETFONT, reinterpret_cast<WPARAM>(next), FALSE);
        if (controlFont_)
            DeleteObject(controlFont_);
        controlFont_ = next;
    }
    arrange();
}

void TrainerWindow::arrange() {
    if (!window_ || arranging_)
        return;
    arranging_ = true;
    RECT client{};
    for (int pass = 0; pass < 2; ++pass) {
        GetClientRect(window_, &client);
        SCROLLINFO horizontal{sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS};
        horizontal.nMax = scaled(kTrainerWidth) - 1;
        horizontal.nPage = static_cast<UINT>(std::max(0L, client.right));
        horizontal.nPos = scrollX_;
        SetScrollInfo(window_, SB_HORZ, &horizontal, TRUE);
        SCROLLINFO vertical{sizeof(vertical), SIF_RANGE | SIF_PAGE | SIF_POS};
        vertical.nMax = scaled(kTrainerHeight) - 1;
        vertical.nPage = static_cast<UINT>(std::max(0L, client.bottom));
        vertical.nPos = scrollY_;
        SetScrollInfo(window_, SB_VERT, &vertical, TRUE);
    }
    GetClientRect(window_, &client);
    scrollX_ = std::clamp(scrollX_, 0, std::max(0, scaled(kTrainerWidth) - static_cast<int>(client.right)));
    scrollY_ = std::clamp(scrollY_, 0, std::max(0, scaled(kTrainerHeight) - static_cast<int>(client.bottom)));
    SetScrollPos(window_, SB_HORZ, scrollX_, TRUE);
    SetScrollPos(window_, SB_VERT, scrollY_, TRUE);
    for (const auto &entry : controlRects_) {
        const auto rect = entry.logical;
        MoveWindow(entry.control, scaled(rect.left) - scrollX_, scaled(rect.top) - scrollY_,
                   scaled(rect.right - rect.left), scaled(rect.bottom - rect.top), TRUE);
    }
    arranging_ = false;
    InvalidateRect(window_, nullptr, FALSE);
}

void TrainerWindow::scroll(int bar, int request) {
    SCROLLINFO info{sizeof(info), SIF_ALL};
    GetScrollInfo(window_, bar, &info);
    int &position = bar == SB_HORZ ? scrollX_ : scrollY_;
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
        position = info.nTrackPos;
        break;
    case SB_TOP:
        position = 0;
        break;
    case SB_BOTTOM:
        position = info.nMax;
        break;
    default:
        return;
    }
    onFocusLost();
    arrange();
}

void TrainerWindow::reveal(HWND control) {
    if (!control || GetParent(control) != window_)
        return;
    RECT rect{}, client{};
    GetWindowRect(control, &rect);
    MapWindowPoints(nullptr, window_, reinterpret_cast<POINT *>(&rect), 2);
    GetClientRect(window_, &client);
    if (rect.left < 0)
        scrollX_ += rect.left;
    else if (rect.right > client.right)
        scrollX_ += rect.right - client.right;
    if (rect.top < 0)
        scrollY_ += rect.top;
    else if (rect.bottom > client.bottom)
        scrollY_ += rect.bottom - client.bottom;
    arrange();
}
} // namespace triggerlab::win32
