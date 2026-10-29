#include "PreviewTestHarness.hpp"
#include <iostream>

using namespace triggerlab::win32;

namespace {
struct TestDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 (L"triggerlab-ui-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                  std::to_wstring(triggerlab::Clock::now().time_since_epoch().count()));
    ~TestDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        const std::wstring option(commandLine);
        const bool inert = option == L"--input-recovery-test" || option == L"--target-sequence-test" ||
                           option == L"--audit-transitions-test";
        TestDirectory data;
        PreviewTestHarness app(data.path, inert ? recoverySender : SendInput);
        if (!app.createWindow(instance))
            return 1;
        if (option == L"--input-recovery-test")
            return app.inputRecoveryTest() ? 0 : 2;
        if (option == L"--audit-transitions-test")
            return PreviewTestHarness::auditTransitionsTest(app);
        if (option == L"--target-sequence-test")
            return PreviewTestHarness::rapidTargetTest(app);
        if (option == L"--help-popup-test")
            return app.helpPopupTest();
        if (option == L"--first-run-test") {
            ShowWindow(app.handle(), SW_SHOW);
            return app.firstRunTest() ? 0 : 2;
        }
        if (option == L"--window-picker-test")
            return app.windowPickerTest() ? 0 : 2;
        if (option == L"--reference-test")
            return PreviewTestHarness::referenceCaptureTest(app);
        if (option == L"--pulse-test")
            return PreviewTestHarness::liveClickTest(app, true, false, true);
        if (option == L"--guided-click-test")
            return PreviewTestHarness::liveClickTest(app, true, true);
        if (option == L"--click-test")
            return PreviewTestHarness::liveClickTest(app);
        if (option == L"--center-click-test")
            return PreviewTestHarness::liveClickTest(app, true);
        if (option == L"--smoke-test")
            return app.smokeTest(data.path / L"selected-color.json") ? 0 : 2;
        return 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
