#include <windows.h>
#include <windowsx.h>
#include "triggerlab/ImagePaletteDialog.hpp"
#include <array>
#include <cmath>
#include <string>
#include <stdexcept>

namespace triggerlab {
namespace {
struct Editor {
    const Frame& image;
    HWND window=nullptr;
    Region display{},region;
    std::vector<ImageColor> palette;
    std::array<HWND,5> buttons{};
    int selected=0;POINT start{},end{};bool dragging=false,done=false,accepted=false;
    HDC imageDc=nullptr,bufferDc=nullptr;
    HBITMAP imageBitmap=nullptr,bufferBitmap=nullptr;
    HGDIOBJ imagePrevious=nullptr,bufferPrevious=nullptr;
    explicit Editor(const Frame& value):image(value),region{0,0,value.width(),value.height()} {
        const double scale=std::min(720./image.width(),480./image.height());
        const auto w=std::max(1,static_cast<int>(std::lround(image.width()*scale))),h=std::max(1,static_cast<int>(std::lround(image.height()*scale)));
        display={20+(720-w)/2,60+(480-h)/2,w,h};
    }
    ~Editor() {
        if(imageDc && imagePrevious) SelectObject(imageDc,imagePrevious);
        if(bufferDc && bufferPrevious) SelectObject(bufferDc,bufferPrevious);
        if(imageBitmap) DeleteObject(imageBitmap);if(bufferBitmap) DeleteObject(bufferBitmap);
        if(imageDc) DeleteDC(imageDc);if(bufferDc) DeleteDC(bufferDc);
    }
    void refresh() {
        palette=imagePalette(image,region);selected=0;
        for(int i=0;i<5;++i) {
            std::wstring label=L"—";
            if(i<static_cast<int>(palette.size())) {
                const auto p=palette[i].color;
                label=L"RGB "+std::to_wstring(p.red)+L", "+std::to_wstring(p.green)+L", "+std::to_wstring(p.blue)+
                    L"\n"+std::to_wstring(static_cast<int>(palette[i].coverage*100))+L"% области";
            }
            SetWindowTextW(buttons[i],label.c_str());EnableWindow(buttons[i],i<static_cast<int>(palette.size()));
        }
        EnableWindow(GetDlgItem(window,1),!palette.empty());InvalidateRect(window,nullptr,FALSE);
    }
    void create() {
        for(int i=0;i<5;++i) buttons[i]=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_MULTILINE,
            790,65+i*86,175,64,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(10+i)),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Всё изображение",WS_CHILD|WS_VISIBLE|WS_TABSTOP,20,610,190,32,window,reinterpret_cast<HMENU>(3),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Применить цвет",WS_CHILD|WS_VISIBLE|WS_TABSTOP,590,610,180,32,window,reinterpret_cast<HMENU>(1),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Отмена",WS_CHILD|WS_VISIBLE|WS_TABSTOP,790,610,175,32,window,reinterpret_cast<HMENU>(2),nullptr,nullptr);
        refresh();
    }
    void render(HDC dc) {
        RECT client{};GetClientRect(window,&client);FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetTextColor(dc,RGB(240,240,240));SetBkMode(dc,TRANSPARENT);
        auto text=[&](int x,int y,const std::wstring& s) {TextOutW(dc,x,y,s.c_str(),static_cast<int>(s.size()));};
        text(20,20,L"Выделите область мышкой. Выберите один из основных оттенков справа.");
        // Checkerboard makes transparency visible instead of implying a black background.
        for(int y=static_cast<int>(display.y);y<display.y+display.height;y+=16) for(int x=static_cast<int>(display.x);x<display.x+display.width;x+=16) {
            RECT tile{x,y,static_cast<LONG>(std::min<std::int64_t>(x+16,display.x+display.width)),static_cast<LONG>(std::min<std::int64_t>(y+16,display.y+display.height))};
            SetDCBrushColor(dc,((x-static_cast<int>(display.x))/16+(y-static_cast<int>(display.y))/16)%2?RGB(65,65,65):RGB(110,110,110));FillRect(dc,&tile,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        // Alpha-blend a premultiplied DIB over the checkerboard.
        if(!imageDc) {
            imageDc=CreateCompatibleDC(dc);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=image.width();info.bmiHeader.biHeight=-image.height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            void* bits=nullptr;imageBitmap=imageDc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
            if(!imageBitmap || !bits) throw std::runtime_error("Image surface unavailable");
            imagePrevious=SelectObject(imageDc,imageBitmap);
            if(!imagePrevious || imagePrevious==HGDI_ERROR) {imagePrevious=nullptr;throw std::runtime_error("Image bitmap selection failed");}
            auto* pixels=static_cast<Pixel*>(bits);
            for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {auto p=image.at(x,y);p.red=static_cast<std::uint8_t>(unsigned(p.red)*p.alpha/255);p.green=static_cast<std::uint8_t>(unsigned(p.green)*p.alpha/255);p.blue=static_cast<std::uint8_t>(unsigned(p.blue)*p.alpha/255);pixels[y*image.width()+x]=p;}
        }
        AlphaBlend(dc,static_cast<int>(display.x),static_cast<int>(display.y),static_cast<int>(display.width),static_cast<int>(display.height),imageDc,0,0,image.width(),image.height(),{AC_SRC_OVER,0,255,AC_SRC_ALPHA});
        Region outline=region;
        if(dragging) outline=*imageSelection(image.width(),image.height(),display,start.x,start.y,end.x,end.y);
        RECT border{static_cast<LONG>(display.x+outline.x*display.width/image.width()),static_cast<LONG>(display.y+outline.y*display.height/image.height()),
            static_cast<LONG>(display.x+(outline.x+outline.width)*display.width/image.width()),static_cast<LONG>(display.y+(outline.y+outline.height)*display.height/image.height())};
        FrameRect(dc,&border,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        for(int i=0;i<static_cast<int>(palette.size());++i) {
            const auto p=palette[i].color;SetDCBrushColor(dc,RGB(p.red,p.green,p.blue));RECT swatch{755,70+i*86,782,118+i*86};FillRect(dc,&swatch,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if(i==selected) {InflateRect(&swatch,2,2);FrameRect(dc,&swatch,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));}
        }
        text(20,555,L"Область: "+std::to_wstring(region.x)+L", "+std::to_wstring(region.y)+L" | "+std::to_wstring(region.width)+L" × "+std::to_wstring(region.height)+L" пикселей");
        text(20,580,palette.empty()?L"Нет непрозрачных пикселей в области. Выделите другой фрагмент.":L"Применить — выбрать цвет; сохранение выполняется в основном окне.");
    }
    void paint(HDC dc) {
        RECT client{};GetClientRect(window,&client);
        if(!bufferDc) {
            bufferDc=CreateCompatibleDC(dc);bufferBitmap=bufferDc?CreateCompatibleBitmap(dc,client.right,client.bottom):nullptr;
            if(!bufferBitmap) throw std::runtime_error("Paint buffer unavailable");
            bufferPrevious=SelectObject(bufferDc,bufferBitmap);
            if(!bufferPrevious || bufferPrevious==HGDI_ERROR) {bufferPrevious=nullptr;throw std::runtime_error("Paint bitmap selection failed");}
        }
        render(bufferDc);BitBlt(dc,0,0,client.right,client.bottom,bufferDc,0,0,SRCCOPY);
    }
};
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);app->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app) return DefWindowProcW(hwnd,message,wp,lp);
    try {
        switch(message) {
            case WM_CREATE:app->create();return 0;
            case WM_COMMAND:
                if(LOWORD(wp)==1 && !app->palette.empty()) {app->accepted=true;DestroyWindow(hwnd);}
                else if(LOWORD(wp)==2) DestroyWindow(hwnd);
                else if(LOWORD(wp)==3) {app->region={0,0,app->image.width(),app->image.height()};app->refresh();}
                else if(LOWORD(wp)>=10 && LOWORD(wp)<10+app->palette.size()) {app->selected=LOWORD(wp)-10;InvalidateRect(hwnd,nullptr,FALSE);}
                return 0;
            case WM_LBUTTONDOWN: {
                POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const auto r=app->display;
                if(p.x>=r.x && p.y>=r.y && p.x<r.x+r.width && p.y<r.y+r.height) {app->start=app->end=p;app->dragging=true;SetCapture(hwnd);}return 0;
            }
            case WM_MOUSEMOVE:if(app->dragging) {app->end={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};InvalidateRect(hwnd,nullptr,FALSE);}return 0;
            case WM_LBUTTONUP:
                if(app->dragging) {app->end={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};app->region=*imageSelection(app->image.width(),app->image.height(),app->display,app->start.x,app->start.y,app->end.x,app->end.y);app->dragging=false;ReleaseCapture();app->refresh();}return 0;
            case WM_CAPTURECHANGED:case WM_CANCELMODE:app->dragging=false;InvalidateRect(hwnd,nullptr,FALSE);return 0;
            case WM_ERASEBKGND:return 1;
            case WM_PAINT: {PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);try {app->paint(dc);} catch(...) {EndPaint(hwnd,&ps);throw;}EndPaint(hwnd,&ps);return 0;}
            case WM_CLOSE:DestroyWindow(hwnd);return 0;
            case WM_DESTROY:app->done=true;return 0;
        }
    } catch(const std::exception&) {app->accepted=false;DestroyWindow(hwnd);return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
HWND createEditor(HWND owner,Editor& editor) {
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TriggerLabImagePalette";wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    RECT bounds{0,0,990,665};AdjustWindowRectEx(&bounds,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
    return CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"Область изображения и палитра",WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,owner,nullptr,wc.hInstance,&editor);
}
}
std::optional<ImageColor> showImagePalette(HWND owner,const Frame& image) {
    Editor editor(image);HWND window=createEditor(owner,editor);if(!window) return {};
    const bool enabled=IsWindowEnabled(owner)!=FALSE;if(enabled) EnableWindow(owner,FALSE);
    ShowWindow(window,SW_SHOW);MSG message{};
    while(!editor.done) {
        const auto status=GetMessageW(&message,nullptr,0,0);
        if(status<=0) {DestroyWindow(window);if(status==0) PostQuitMessage(static_cast<int>(message.wParam));break;}
        if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE) {DestroyWindow(window);continue;}
        if(!IsDialogMessageW(window,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
    }
    if(enabled) EnableWindow(owner,TRUE);SetActiveWindow(owner);
    if(!editor.accepted || editor.palette.empty()) return {};
    return editor.palette[editor.selected];
}
bool imagePaletteDialogSmoke() {
    Frame image(2,1);image.at(0,0)={0,0,255,255};image.at(1,0)={255,0,0,255};
    Editor editor(image);auto window=createEditor(nullptr,editor);if(!window) return false;
    bool ok=editor.palette.size()==2 && IsWindowEnabled(GetDlgItem(window,1));
    SendMessageW(window,WM_COMMAND,11,0);ok=ok && editor.selected==1;
    const int x=static_cast<int>(editor.display.x),y=static_cast<int>(editor.display.y);
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    ok=ok && editor.region.width==1 && editor.palette.size()==1 && editor.palette[0].color.red==255;
    SendMessageW(window,WM_COMMAND,3,0);ok=ok && editor.palette.size()==2;
    HDC dc=GetDC(window);editor.paint(dc);
    const DWORD resources=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<20;++i) editor.paint(dc);
    ok=ok && GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==resources;ReleaseDC(window,dc);
    SendMessageW(window,WM_COMMAND,2,0);return ok && editor.done && !editor.accepted;
}
}
