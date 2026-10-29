#include "StartupDiagnostics.hpp"
#include "trainer/TrainerWindow.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        triggerlab::win32::TrainerWindow app;
        if (!app.createWindow(instance))
            return 1;
        app.show(show);
        return app.messageLoop();
    } catch (const std::exception &error) {
        triggerlab::reportStartupException(error.what());
        return 1;
    }
}
