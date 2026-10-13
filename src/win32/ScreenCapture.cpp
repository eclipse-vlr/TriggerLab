#include <windows.h>
#include "triggerlab/ScreenCapture.hpp"
#include <cwchar>
#include <new>
namespace triggerlab {
namespace {
struct DpiScope {
    DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiScope() {if(previous) SetThreadDpiAwarenessContext(previous);}
};
struct DesktopDc {
    HDC dc=GetDC(nullptr);
    ~DesktopDc() {if(dc) ReleaseDC(nullptr,dc);}
};
Region desktopBounds() noexcept {
    return {GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}
bool desktopAvailable() noexcept {
    HDESK input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    if(!input) return false;
    wchar_t inputName[256]{},threadName[256]{}; DWORD needed=0;
    const bool ok=GetUserObjectInformationW(input,UOI_NAME,inputName,sizeof(inputName),&needed) &&
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,threadName,sizeof(threadName),&needed) &&
        std::wcscmp(inputName,threadName)==0;
    CloseDesktop(input); return ok;
}
}
struct ScreenCapture::Impl {
    HDC dc=nullptr; HBITMAP bitmap=nullptr; HGDIOBJ previous=nullptr;
    Pixel* pixels=nullptr; int width=0,height=0;
    ~Impl() {clear();}
    void clear() noexcept {if(dc) {SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);} dc=nullptr; bitmap=nullptr; previous=nullptr; pixels=nullptr; width=height=0;}
    bool ensure(HDC source,int w,int h) noexcept {
        if(dc && width==w && height==h) return true;
        clear(); dc=CreateCompatibleDC(source); if(!dc) return false;
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=w;
        info.bmiHeader.biHeight=-h; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr; bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap || !bits) {if(bitmap) DeleteObject(bitmap); DeleteDC(dc); dc=nullptr; bitmap=nullptr; return false;}
        previous=SelectObject(dc,bitmap);
        if(!previous || previous==HGDI_ERROR) {DeleteObject(bitmap); DeleteDC(dc); dc=nullptr; bitmap=nullptr; return false;}
        pixels=static_cast<Pixel*>(bits); width=w; height=h; return true;
    }
};
ScreenCapture::ScreenCapture():impl_(std::make_unique<Impl>()) {}
ScreenCapture::~ScreenCapture()=default;
void ScreenCapture::reset() noexcept {impl_->clear();}
CaptureSample ScreenCapture::capture(Region requested) {
    CaptureSample result;
    if(!validRegion(requested)) {result.status=CaptureStatus::InvalidRegion; return result;}
    DpiScope dpi;
    if(!dpi.previous || !desktopAvailable()) return result;
    const auto clipped=intersectRegion(requested,desktopBounds());
    if(!clipped) {result.status=CaptureStatus::InvalidRegion; return result;}
    DesktopDc source; if(!source.dc) return result;
    const int w=static_cast<int>(clipped->width),h=static_cast<int>(clipped->height);
    if(!impl_->ensure(source.dc,w,h)) {result.status=CaptureStatus::AllocationFailed; return result;}
    if(!BitBlt(impl_->dc,0,0,w,h,source.dc,static_cast<int>(clipped->x),static_cast<int>(clipped->y),SRCCOPY|CAPTUREBLT) || !GdiFlush()) {
        result.status=CaptureStatus::CopyFailed; return result;
    }
    try { result.frame.emplace(w,h); } catch(const std::bad_alloc&) {result.status=CaptureStatus::AllocationFailed; return result;}
    result.allBlack=true;
    for(int i=0;i<w*h;++i) {
        auto pixel=impl_->pixels[i]; pixel.alpha=255;
        result.frame->data()[i]=pixel;
        if(pixel.red || pixel.green || pixel.blue) result.allBlack=false;
    }
    result.region=*clipped; result.status=CaptureStatus::Ok; return result;
}
CaptureSample ScreenCapture::captureCursor(int side) {
    DpiScope dpi; POINT point{};
    if(!dpi.previous || !desktopAvailable() || !GetCursorPos(&point)) return {};
    if(side<1 || side>4095 || side%2==0) {CaptureSample failed; failed.status=CaptureStatus::InvalidRegion; return failed;}
    MONITORINFO monitor{sizeof(MONITORINFO)};
    HMONITOR handle=MonitorFromPoint(point,MONITOR_DEFAULTTONULL);
    if(!handle || !GetMonitorInfoW(handle,&monitor)) return {};
    Region bounds{monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top};
    const auto roi=cursorRegion(point.x,point.y,side,bounds);
    if(!roi) {CaptureSample failed; failed.status=CaptureStatus::InvalidRegion; return failed;}
    auto result=capture(*roi);
    if(result.frame) {result.cursorPixel=result.frame->at(static_cast<int>(point.x-result.region.x),static_cast<int>(point.y-result.region.y));result.cursorPosition=CapturePoint{point.x,point.y};}
    return result;
}
}
