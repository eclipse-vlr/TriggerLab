#include "preview/PreviewWindow.hpp"
#include "StartupDiagnostics.hpp"
#include "triggerlab/ClickPermission.hpp"
#include "triggerlab/ImageFile.hpp"
#include "ImagePaletteDialog.hpp"
#include <commdlg.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwctype>

namespace triggerlab::win32 {

PreviewWindow::~PreviewWindow() {if(window && IsWindow(window)) DestroyWindow(window);if(controlFont) DeleteObject(controlFont);if(uiFont) DeleteObject(uiFont);if(titleFont) DeleteObject(titleFont);if(headingFont) DeleteObject(headingFont);if(cardBrush) DeleteObject(cardBrush);if(inputBrush) DeleteObject(inputBrush);}

PreviewWindow::PreviewWindow(const std::filesystem::path& dataDirectory,triggerlab::ClickPulse::Sender sender):inputPulse(sender) {
        wchar_t local[32768]{};auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
        colorPath=length>0 && length<32768?std::filesystem::path(local)/L"TriggerLab"/L"selected-color.json":std::filesystem::path(L"selected-color.json");
        if(!dataDirectory.empty()) colorPath=dataDirectory/L"selected-color.json";
        const auto loaded=triggerlab::loadColorProfile(colorPath);if(loaded.ok) {color=loaded.color;colorReady=true;}
        settingsPath=colorPath.parent_path()/L"analyzer-settings.json";
        settings=triggerlab::loadAnalyzerSettings(settingsPath);
        settings.dryRun=true; // Every launch starts in diagnostic mode.
        savedSettings=settings;savedColor=color;
        std::error_code settingsError;
        paramsOnDisk=std::filesystem::exists(settingsPath,settingsError);colorOnDisk=loaded.ok;
        if(settingsError) throw std::runtime_error("Cannot access settings directory");
        gate.setTiming(std::chrono::milliseconds{settings.dwellMs},std::chrono::milliseconds{settings.cooldownMs});
    }

void PreviewWindow::applyTheme() {
        const auto p=themeColors(settings.darkTheme);
        auto newCard=CreateSolidBrush(p.card),newInput=CreateSolidBrush(p.input);
        if(newCard && newInput) {DeleteObject(cardBrush);DeleteObject(inputBrush);cardBrush=newCard;inputBrush=newInput;}
        else {if(newCard) DeleteObject(newCard);if(newInput) DeleteObject(newInput);}
        SetWindowTextW(GetDlgItem(window,control::toggleTheme),settings.darkTheme?L"Серая тема: включена":L"Серая тема: выключена");
        BOOL dark=settings.darkTheme;
        DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
        DwmSetWindowAttribute(window,34,&p.background,sizeof(p.background));
        const COLORREF systemCaptionText=0xFFFFFFFF;
        DwmSetWindowAttribute(window,36,&systemCaptionText,sizeof(systemCaptionText));
        if(tooltip) {SendMessageW(tooltip,TTM_SETTIPBKCOLOR,p.card,0);SendMessageW(tooltip,TTM_SETTIPTEXTCOLOR,p.text,0);}
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }

std::wstring PreviewWindow::wide(const std::string& text) {
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
        std::wstring result(length,L'\0');if(length) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),length);return result;
    }

int PreviewWindow::scaled(int value) const {return MulDiv(value,static_cast<int>(dpi),96);}

void PreviewWindow::mapCanvas(HDC dc,int x,int y) const {
        SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,96,96,nullptr);
        SetViewportExtEx(dc,dpi,dpi,nullptr);SetViewportOrgEx(dc,-x,-y,nullptr);
    }

void PreviewWindow::setDpi(UINT value) {
        dpi=value?value:96;
        HFONT next=CreateFontW(-scaled(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(next) {
            for(const auto& entry:controlRects) SendMessageW(entry.control,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);
            if(tooltip) SendMessageW(tooltip,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);
            if(controlFont) DeleteObject(controlFont);controlFont=next;
        }
        arrange();
    }

} // namespace triggerlab::win32

namespace triggerlab::win32 {
LRESULT CALLBACK PreviewWindow::windowProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {auto* create=reinterpret_cast<CREATESTRUCTW*>(lp); app=static_cast<PreviewWindow*>(create->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));return TRUE;}
    try {
        switch(message) {
            case WM_CREATE:app->createControls();return 0;
            case WM_COMMAND:if(HIWORD(wp)==EN_CHANGE) app->onEditChange(reinterpret_cast<HWND>(lp));else app->command(LOWORD(wp),HIWORD(wp));return 0;
            case WM_TIMER:
                if(wp==timer::recovery) {if(app->syntheticDown && !app->autoClick) app->finishClick(false);app->refreshTargetButton();app->syncActions();InvalidateRect(window,nullptr,FALSE);return 0;}
                if(wp==timer::selection) app->selectionTick(GetTickCount64());
                else if(wp==timer::capture) app->update();return 0;
            case WM_HOTKEY:if(wp==hotkey::stop) app->stop();return 0;
            case WM_KEYDOWN:
                if(wp==VK_ESCAPE) app->stop();
                else if(wp=='C') app->update(true);
                else if(wp=='H' && !(lp & (1LL<<30))) {app->cancelSelection();app->disableClicks();app->verified=false;app->color.space=app->color.space==triggerlab::ColorSpace::RGB?triggerlab::ColorSpace::HSV:triggerlab::ColorSpace::RGB;app->notice=L"Режим цвета изменён. Нажмите «Сохранить всё»";app->syncActions();app->update();InvalidateRect(window,nullptr,FALSE);}return 0;
            case WM_DRAWITEM:if(app && wp) {app->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}break;
            case WM_SIZE:app->arrange();return 0;
            case WM_DPICHANGED: {
                app->setDpi(HIWORD(wp));const auto* r=reinterpret_cast<RECT*>(lp);
                SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->arrange();return 0;
            }
            case WM_HSCROLL:case WM_VSCROLL:app->scroll(message==WM_HSCROLL?SB_HORZ:SB_VERT,LOWORD(wp));return 0;
            case WM_MOUSEWHEEL: {
                app->wheelRemainder+=GET_WHEEL_DELTA_WPARAM(wp);
                const int steps=app->wheelRemainder/WHEEL_DELTA;app->wheelRemainder%=WHEEL_DELTA;
                if(wp&MK_SHIFT) app->scrollX-=steps*app->scaled(108);else app->scrollY-=steps*app->scaled(108);
                app->arrange();return 0;
            }
            case WM_GETMINMAXINFO: {auto* size=reinterpret_cast<MINMAXINFO*>(lp);size->ptMinTrackSize={480,360};
#ifdef TRIGGERLAB_UI_TESTS
if(app && app->allowOversizedTestWindow) size->ptMaxTrackSize={10000,10000};
#endif
return 0;}
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN: {
                const auto p=themeColors(app->settings.darkTheme);HDC dc=reinterpret_cast<HDC>(wp);
                if(reinterpret_cast<HWND>(lp)==app->headerStatus || reinterpret_cast<HWND>(lp)==app->homeHint) {SetTextColor(dc,app->feedbackColor(app->currentFeedback().tone));SetBkColor(dc,p.card);return reinterpret_cast<LRESULT>(app->cardBrush);}
                const bool input=message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX || reinterpret_cast<HWND>(lp)==app->modeControl;
                SetTextColor(dc,p.text);SetBkColor(dc,input?p.input:p.card);
                return reinterpret_cast<LRESULT>(input?app->inputBrush:app->cardBrush);
            }
            case WM_ERASEBKGND:return 1;
            case WM_PAINT: {PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);try {app->paint(dc,rect);} catch(...) {EndPaint(window,&paint);throw;} EndPaint(window,&paint);return 0;}
            case WM_CLOSE:
                app->stop();
                if(app->syntheticDown) {
                    MessageBoxW(window,L"Windows пока отклоняет отпускание кнопки. Дождитесь восстановления ввода и повторите закрытие.",L"TriggerLab",MB_OK|MB_ICONWARNING);
                    return 0;
                }
                DestroyWindow(window);return 0;
            case WM_DESTROY:KillTimer(window,timer::recovery);app->stop();PostQuitMessage(0);return 0;
        }
    } catch(const std::exception&) {app->stop();}
    return DefWindowProcW(window,message,wp,lp);
}

} // namespace triggerlab::win32

namespace triggerlab::win32 {
bool PreviewWindow::createWindow(HINSTANCE instance) {
    WNDCLASSW type{};
    type.lpfnWndProc = windowProcedure;
    type.hInstance = instance;
    type.lpszClassName = L"TriggerLabCapturePreviewClass";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        reportStartupError(L"RegisterClass");
        return false;
    }
    window = CreateWindowExW(WS_EX_COMPOSITED, type.lpszClassName,
        L"TriggerLab — тренировки Kovaak’s / Aimlabs", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1120, 980, nullptr, nullptr, instance, this);
    if (!window) { reportStartupError(L"CreateWindow"); return false; }
    return true;
}
void PreviewWindow::show(int command) {
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const RECT work = monitor.rcWork;
        const int width = std::min(scaled(1120), static_cast<int>(work.right - work.left));
        const int height = std::min(scaled(1000), static_cast<int>(work.bottom - work.top));
        SetWindowPos(window, nullptr, work.left + (work.right - work.left - width) / 2,
            work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(window, command);
}
int PreviewWindow::messageLoop() {
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { stop(); continue; }
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (result < 0) reportStartupError(L"GetMessage");
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
} // namespace triggerlab::win32
