// Audit-only harness. Never merge this probe into a product release.
#define wWinMain triggerlabAuditedEntryPoint
#include "../src/win32/capture_preview.cpp"
#undef wWinMain
#include <iostream>

namespace {
int auditInputCalls=0;
UINT WINAPI auditSender(UINT count,LPINPUT,int) {
    ++auditInputCalls;
    return count; // Never forwards input to Windows.
}
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/
        (L"triggerlab-audit-"+std::to_wstring(GetCurrentProcessId()));
    try {
        Preview app(root,auditSender);
        const auto instance=GetModuleHandleW(nullptr);
        WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;
        wc.lpszClassName=L"TriggerLabAuditMain";
        if(!RegisterClassW(&wc)) return 2;
        HWND own=CreateWindowW(wc.lpszClassName,L"Audit analyzer",WS_OVERLAPPEDWINDOW,
            0,0,1120,980,nullptr,nullptr,instance,&app);
        HWND target=CreateWindowW(L"STATIC",L"Audit target",WS_OVERLAPPEDWINDOW,
            20,20,300,240,nullptr,nullptr,instance,nullptr);
        if(!own || !target) return 2;
        ShowWindow(target,SW_SHOWNOACTIVATE);
        DWORD process=0;GetWindowThreadProcessId(target,&process);
        triggerlab::WindowChoice choice{target,process,L"Audit target",L"AuditProbe"};
        if(!app.applyWindowChoice(choice)) return 2;
        app.command(23); // Advanced UI.
        SendMessageW(app.dryControl,BM_SETCHECK,BST_UNCHECKED,0);
        app.command(16); // Turn off dry-run before any color verification.
        app.command(1);
        SendMessageW(app.autoControl,BM_SETCHECK,BST_CHECKED,0);
        app.command(8);
        std::cout<<"AUDIT armed_without_color_or_verification="
                 <<(app.autoClick && !app.settings.dryRun && !app.colorReady && !app.verified)<<'\n';
        app.stop();

        // A new invalid edit disables input, but re-arming ignores that draft.
        app.colorReady=true;app.verified=false;
        SetWindowTextW(app.settingEdits[0],L"invalid");
        app.command(1);
        SendMessageW(app.autoControl,BM_SETCHECK,BST_CHECKED,0);
        app.command(8);
        std::cout<<"AUDIT armed_with_invalid_unsaved_draft="
                 <<(app.autoClick && !app.settings.dryRun && app.settingsDirty() && !app.verified)<<'\n';
        app.stop();

        app.syncSettings();app.colorReady=true;
        app.color.space=triggerlab::ColorSpace::HSV;app.syncColor();
        SetWindowTextW(app.colorEdits[3],L"0");
        if(!app.saveAll()) return 2;
        const auto low=app.color;
        SetWindowTextW(app.colorEdits[3],L"255");
        if(!app.saveAll()) return 2;
        const auto high=app.color;
        std::cout<<"AUDIT hsv_ui_tolerance_changes_only_rgb_field="
                 <<(low.space==triggerlab::ColorSpace::HSV && high.space==low.space &&
                    low.channelTolerance==0 && high.channelTolerance==255 &&
                    low.hueTolerance==high.hueTolerance &&
                    low.saturationTolerance==high.saturationTolerance &&
                    low.valueTolerance==high.valueTolerance)<<'\n';
        std::cout<<"AUDIT actual_SendInput_calls=0; fake_sender_calls="<<auditInputCalls<<'\n';
        DestroyWindow(target);DestroyWindow(own);
        std::filesystem::remove_all(root);
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"AUDIT setup failed: "<<e.what()<<'\n';
        return 2;
    }
}
