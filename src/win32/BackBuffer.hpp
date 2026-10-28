#pragma once
#include <windows.h>

namespace triggerlab::win32 {
// Own one compatible surface; never expose the intermediate erase/draw stages.
class BackBuffer {
public:
    ~BackBuffer() { clear(); }
    BackBuffer()=default;
    BackBuffer(const BackBuffer&)=delete;
    BackBuffer& operator=(const BackBuffer&)=delete;
    bool ensure(HDC screen,int width,int height) noexcept {
        if(dc_ && width==width_ && height==height_) return true;
        if(width<=0 || height<=0) return false;
        HDC dc=CreateCompatibleDC(screen); if(!dc) return false;
        HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);
        if(!bitmap) { DeleteDC(dc); return false; }
        HGDIOBJ previous=SelectObject(dc,bitmap);
        if(!previous || previous==HGDI_ERROR) { DeleteObject(bitmap); DeleteDC(dc); return false; }
        clear(); dc_=dc; bitmap_=bitmap; previous_=previous; width_=width; height_=height; return true;
    }
    HDC dc() const noexcept {return dc_;}
private:
    void clear() noexcept { if(dc_) {SelectObject(dc_,previous_); DeleteObject(bitmap_); DeleteDC(dc_);} dc_=nullptr; }
    HDC dc_=nullptr; HBITMAP bitmap_=nullptr; HGDIOBJ previous_=nullptr;
    int width_=0,height_=0;
};
}
