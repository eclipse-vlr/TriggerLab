#include <windows.h>
#include <dwmapi.h>
#include "triggerlab/ScreenCapture.hpp"
#include "triggerlab/ColorMask.hpp"
#include <iostream>
using namespace triggerlab;
LRESULT CALLBACK testProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rectangle{};GetClientRect(window,&rectangle);
        HBRUSH brush=CreateSolidBrush(RGB(231,67,19));FillRect(dc,&rectangle,brush);DeleteObject(brush);EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
int main() {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const DWORD initial=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    bool ok=true;
    {
        ScreenCapture capture;
        auto invalid=capture.capture({0,0,0,17});
        if(invalid.status!=CaptureStatus::InvalidRegion || invalid.frame) return 1;
        WNDCLASSW wc{}; wc.lpfnWndProc=testProcedure; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"TriggerLabCaptureTest";
        RegisterClassW(&wc);
        const int x=GetSystemMetrics(SM_XVIRTUALSCREEN)+30,y=GetSystemMetrics(SM_YVIRTUALSCREEN)+30;
        HWND window=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"Capture test",WS_POPUP|WS_VISIBLE,x,y,64,64,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window) return 77;
        UpdateWindow(window); GdiFlush(); DwmFlush();
        const DWORD baseline=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        auto sample=capture.capture({x+20,y+20,17,17});
        if(sample.status==CaptureStatus::DesktopUnavailable) {DestroyWindow(window); std::cout<<"SKIP: interactive input desktop unavailable\n"; return 77;}
        ok=sample.frame.has_value() && sample.status==CaptureStatus::Ok; std::cout<<"capture status="<<static_cast<int>(sample.status)<<"\n";
        if(ok) {
            auto p=sample.frame->at(8,8); std::cout<<"pixel RGB="<<int(p.red)<<","<<int(p.green)<<","<<int(p.blue)<<"\n"; ok=p.red==231 && p.green==67 && p.blue==19 && p.alpha==255;
            ColorMatchConfig color; color.target={19,67,231,255};color.channelTolerance=0;
            ok=ok && makeColorMask(*sample.frame,color).coverage==1;
        }
        const DWORD allocated=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for(int i=0;i<120 && ok;++i) {auto next=capture.capture({x+20,y+20,17+(i%2)*2,17});ok=next.status==CaptureStatus::Ok;}
        std::cout<<"GDI allocated="<<allocated<<" after resize="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<"\n";
        ok=ok && GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==allocated;
        auto failed=capture.capture({2147483647,0,17,17});ok=ok && !failed.frame && failed.status==CaptureStatus::InvalidRegion;
        capture.reset();
        ok=ok && GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline;
        {
            ScreenCapture transient; auto last=transient.capture({x+20,y+20,17,17});
            ok=ok && last.status==CaptureStatus::Ok;
        }
        ok=ok && GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline;
        DestroyWindow(window); UnregisterClassW(wc.lpszClassName,wc.hInstance);
    }
    std::cout<<"GDI initial="<<initial<<" final="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<"\n";
    // Win32 lazily caches stock fonts/brushes. Capture ownership is checked against
    // the initialized window baseline above, rather than an uninitialized process.
    std::cout<<(ok?"PASS: real ROI BGRA, mask and GDI lifecycle\n":"FAIL: live capture\n");return ok?0:1;
}
