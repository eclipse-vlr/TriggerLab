#pragma once
#include <windows.h>

namespace triggerlab::win32 {
struct ThemeColors {
    COLORREF background, card, input, border, text, muted, accent, button, pressed, stopFill, stopText;
};
inline ThemeColors themeColors(bool dark) {
    if (dark)
        return {RGB(48, 51, 57),    RGB(62, 66, 73),    RGB(73, 78, 86),    RGB(91, 98, 108),
                RGB(241, 243, 247), RGB(190, 198, 211), RGB(147, 190, 255), RGB(77, 83, 94),
                RGB(91, 102, 120),  RGB(96, 65, 69),    RGB(255, 184, 184)};
    return {RGB(242, 246, 252), RGB(255, 255, 255), RGB(248, 250, 252), RGB(226, 232, 240),
            RGB(30, 41, 59),    RGB(100, 116, 139), RGB(37, 99, 235),   RGB(241, 245, 249),
            RGB(219, 234, 254), RGB(254, 242, 242), RGB(185, 28, 28)};
}

} // namespace triggerlab::win32
