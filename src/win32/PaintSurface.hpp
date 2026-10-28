#pragma once
#include <windows.h>

namespace triggerlab::win32 {
struct PaintSurface {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;
    PaintSurface(HDC target,int w,int h) {
        dc=CreateCompatibleDC(target);if(!dc) return;
        bitmap=CreateCompatibleBitmap(target,w,h);if(!bitmap) return;
        previous=SelectObject(dc,bitmap);
    }
    PaintSurface(const PaintSurface&)=delete;
    PaintSurface& operator=(const PaintSurface&)=delete;
    ~PaintSurface() {if(dc && previous && previous!=HGDI_ERROR) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);}
    bool valid() const noexcept {return dc && bitmap && previous && previous!=HGDI_ERROR;}
};

}
