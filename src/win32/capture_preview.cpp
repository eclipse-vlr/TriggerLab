#include <windows.h>
#include "triggerlab/ScreenCapture.hpp"
#include "triggerlab/ColorMask.hpp"
#include <string>
#include <optional>
namespace {
struct PaintSurface {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;
    PaintSurface(HDC target,int w,int h) {
        dc=CreateCompatibleDC(target);if(!dc) return;
        bitmap=CreateCompatibleBitmap(target,w,h);if(!bitmap) return;
        previous=SelectObject(dc,bitmap);
    }
    ~PaintSurface() {if(dc && previous && previous!=HGDI_ERROR) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);}
    bool valid() const noexcept {return dc && bitmap && previous && previous!=HGDI_ERROR;}
};
struct Preview {
    triggerlab::ScreenCapture capture;
    triggerlab::CaptureSample sample;
    triggerlab::ColorMask mask;
    triggerlab::ColorMatchConfig color;
    HWND window=nullptr; bool running=false;
    std::wstring status=L"Stopped / Остановлено";
    void stop() {running=false; KillTimer(window,1);capture.reset();sample={};mask={};status=L"Stopped / Остановлено";InvalidateRect(window,nullptr,FALSE);}
    void update(bool pick=false) {
        if(!running) return;
        POINT cursor{}; RECT own{};
        if(!GetCursorPos(&cursor)) {sample={};mask={};status=L"Cursor unavailable / Курсор недоступен";InvalidateRect(window,nullptr,FALSE);return;}
        GetWindowRect(window,&own);
        if(cursor.x+8>=own.left && cursor.x-8<own.right && cursor.y+8>=own.top && cursor.y-8<own.bottom) {
            sample={};mask={};status=L"Move cursor outside preview / Уведите курсор за окно";
        } else {
            sample=capture.captureCursor(); mask={};
            if(sample.frame) {
                if(pick && sample.cursorPixel) color.target=*sample.cursorPixel;
                mask=triggerlab::makeColorMask(*sample.frame,color);
                status=sample.allBlack?L"Black ROI: may be protected / Чёрный ROI: возможна защита":(mask.found?L"MATCH / СОВПАДЕНИЕ":L"No match / Нет совпадения");
            } else status=L"Capture unavailable (status " +std::to_wstring(static_cast<int>(sample.status))+L") / Захват недоступен";
        }
        InvalidateRect(window,nullptr,FALSE);
    }
    void paint(HDC target,RECT rect) {
        const int w=rect.right,h=rect.bottom;if(w<=0 || h<=0) return;
        PaintSurface surface(target,w,h);if(!surface.valid()) return;HDC dc=surface.dc;
        FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(235,235,235));
        auto text=[&](int y,const std::wstring& line) {TextOutW(dc,15,y,line.c_str(),static_cast<int>(line.size()));};
        text(58,status);text(82,L"C: sample current cursor color / взять цвет; H: RGB/HSV; ESC: Stop");
        text(106,L"RGB: "+std::to_wstring(color.target.red)+L", "+std::to_wstring(color.target.green)+L", "+std::to_wstring(color.target.blue)+L" | "+(color.space==triggerlab::ColorSpace::RGB?L"RGB":L"HSV")+L" | coverage "+std::to_wstring(static_cast<int>(mask.coverage*100))+L"%");
        if(sample.frame) {
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=sample.frame->width();info.bmiHeader.biHeight=-sample.frame->height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            StretchDIBits(dc,15,145,210,210,0,0,sample.frame->width(),sample.frame->height(),sample.frame->data(),&info,DIB_RGB_COLORS,SRCCOPY);
            triggerlab::Frame maskFrame(sample.frame->width(),sample.frame->height());
            for(std::size_t i=0;i<mask.pixels.size();++i) {auto value=mask.pixels[i];maskFrame.data()[i]={value,value,value,255};}
            StretchDIBits(dc,245,145,210,210,0,0,maskFrame.width(),maskFrame.height(),maskFrame.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        }
        text(375,L"Read-only ROI, no external actions / Только чтение, без внешних действий");
        BitBlt(target,0,0,w,h,dc,0,0,SRCCOPY);
    }
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<Preview*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {auto* create=reinterpret_cast<CREATESTRUCTW*>(lp); app=static_cast<Preview*>(create->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));return TRUE;}
    try {
        switch(message) {
            case WM_CREATE:
                CreateWindowW(L"BUTTON",L"Start / Начать",WS_CHILD|WS_VISIBLE|WS_TABSTOP,15,15,150,30,window,reinterpret_cast<HMENU>(1),nullptr,nullptr);
                CreateWindowW(L"BUTTON",L"Stop / Остановить",WS_CHILD|WS_VISIBLE|WS_TABSTOP,180,15,170,30,window,reinterpret_cast<HMENU>(2),nullptr,nullptr);return 0;
            case WM_COMMAND:
                if(LOWORD(wp)==1) {if(SetTimer(window,1,33,nullptr)) {app->running=true;app->status=L"Active / Захват активен";}else app->status=L"Timer failed / Ошибка таймера";SetFocus(window);InvalidateRect(window,nullptr,FALSE);}
                if(LOWORD(wp)==2) {app->stop();SetFocus(window);}return 0;
            case WM_TIMER: app->update();return 0;
            case WM_KEYDOWN:
                if(wp==VK_ESCAPE) app->stop();
                else if(wp=='C') app->update(true);
                else if(wp=='H') {app->color.space=app->color.space==triggerlab::ColorSpace::RGB?triggerlab::ColorSpace::HSV:triggerlab::ColorSpace::RGB;app->update();}return 0;
            case WM_ERASEBKGND:return 1;
            case WM_PAINT: {PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);try {app->paint(dc,rect);} catch(...) {EndPaint(window,&paint);throw;} EndPaint(window,&paint);return 0;}
            case WM_DESTROY:app->stop();PostQuitMessage(0);return 0;
        }
    } catch(const std::exception&) {app->stop();}
    return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        Preview app;WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"TriggerLabCapturePreviewClass";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc)) return 1;
        HWND window=CreateWindowW(wc.lpszClassName,L"TriggerLab Capture Preview — M1/M2",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,620,460,nullptr,nullptr,instance,&app);
        if(!window) return 1;ShowWindow(window,show);MSG message{};BOOL status;
        while((status=GetMessageW(&message,nullptr,0,0))>0) {TranslateMessage(&message);DispatchMessageW(&message);}return status<0?1:0;
    } catch(const std::exception&) {return 1;}
}
