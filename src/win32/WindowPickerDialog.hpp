#pragma once
#include <windows.h>
#include <optional>
#include <string>

namespace triggerlab {
struct WindowChoice {
    HWND window=nullptr;
    DWORD process=0;
    std::wstring title,application;
};
struct WindowPickerResult {
    std::optional<WindowChoice> choice;
    bool manual=false;
    std::wstring error;
};
bool windowChoiceAvailable(const WindowChoice& choice,HWND owner);
WindowPickerResult showWindowPicker(HWND owner,bool dark,HWND current);
#ifdef TRIGGERLAB_UI_TESTS
bool windowPickerDialogSmoke();
#endif
}
