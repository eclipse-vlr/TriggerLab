#pragma once
#include "preview/PreviewWindow.hpp"
#include <atomic>
#include <ostream>

namespace triggerlab::win32 {
void pumpTestMessages();
bool testMessagePumpYields();
extern std::atomic<int> recoveryDowns, recoveryUps;
extern std::atomic<bool> rejectRecoveryUp;
UINT WINAPI recoverySender(UINT count, LPINPUT input, int size);

// Only this test executable receives access to the view's internal state.
struct PreviewTestHarness : PreviewWindow {
    using PreviewWindow::PreviewWindow;
    HWND handle() const noexcept { return window; }
    bool inputRecoveryTest();
    bool windowPickerTest();
    bool scrollPaintTest(std::ostream& report);
    bool helpTooltipsTest(std::ostream& report);
    int helpPopupTest();
    bool firstRunTest();
    bool feedbackTest();
    bool smokeTest(const std::filesystem::path& path);
    static int rapidTargetTest(PreviewTestHarness& app);
    static int referenceCaptureTest(PreviewTestHarness& app);
    static int liveClickTest(PreviewTestHarness& app, bool centerMode=false, bool guided=false, bool stress=false);
};
} // namespace triggerlab::win32
