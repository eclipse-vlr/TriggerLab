#include "WindowPickerDialog.hpp"
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace triggerlab {
namespace {
bool eligible(HWND window,HWND owner) {
    if(window==owner || !IsWindow(window) || !IsWindowVisible(window) || GetWindow(window,GW_OWNER) ||
        (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)) return false;
    DWORD cloaked=0;
    return FAILED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) || !cloaked;
}
std::vector<WindowChoice> enumerateWindows(HWND owner) {
    struct Context {HWND owner;std::vector<WindowChoice> rows;bool failed=false;} context{owner,{}};
    const BOOL complete=EnumWindows([](HWND window,LPARAM parameter)->BOOL {
        auto& context=*reinterpret_cast<Context*>(parameter);
        try {
            if(!eligible(window,context.owner)) return TRUE;
            wchar_t title[1024]{};if(!GetWindowTextW(window,title,1024)) return TRUE;
            WindowChoice row;row.window=window;row.title=title;
            if(!row.title.empty() && row.title.back()>=0xD800 && row.title.back()<=0xDBFF) row.title.pop_back();
            GetWindowThreadProcessId(window,&row.process);
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,row.process);
            if(process) {
                wchar_t path[32768]{};DWORD length=32768;
                const bool readable=QueryFullProcessImageNameW(process,0,path,&length)!=FALSE;CloseHandle(process);
                if(readable) row.application=std::filesystem::path(path).filename().wstring();
            }
            if(row.application.empty()) row.application=L"Приложение недоступно (PID "+std::to_wstring(row.process)+L")";
            if(row.process && eligible(window,context.owner)) context.rows.push_back(std::move(row));
            return TRUE;
        } catch(...) {context.failed=true;return FALSE;}
    },reinterpret_cast<LPARAM>(&context));
    if(!complete || context.failed) throw std::runtime_error("Window enumeration failed");
    std::sort(context.rows.begin(),context.rows.end(),[](const auto& a,const auto& b) {
        const auto order=CompareStringOrdinal(a.title.c_str(),-1,b.title.c_str(),-1,TRUE);
        if(order!=CSTR_EQUAL) return order==CSTR_LESS_THAN;
        return a.process<b.process;
    });
    return std::move(context.rows);
}
struct Picker {
    HWND owner=nullptr,window=nullptr,list=nullptr,hint=nullptr;
    HWND current=nullptr;DWORD currentProcess=0;bool dark=false,done=false,arranging=false;
    UINT dpi=96;HFONT font=nullptr;HBRUSH background=nullptr,input=nullptr;
    std::array<HWND,4> buttons{};
    std::vector<WindowChoice> rows;
    WindowPickerResult result;
    explicit Picker(HWND parent,bool theme,HWND selected):owner(parent),current(selected),dark(theme) {
        if(current) GetWindowThreadProcessId(current,&currentProcess);
        background=CreateSolidBrush(dark?RGB(62,66,73):RGB(255,255,255));
        input=CreateSolidBrush(dark?RGB(73,78,86):RGB(248,250,252));
        if(!background || !input) {if(background) DeleteObject(background);if(input) DeleteObject(input);throw std::runtime_error("Picker brushes unavailable");}
    }
    ~Picker() {if(!done && IsWindow(window)) DestroyWindow(window);if(font) DeleteObject(font);DeleteObject(background);DeleteObject(input);}
    int scaled(int value) const {return MulDiv(value,dpi,96);}
    void arrange() {
        if(!list || arranging) return;arranging=true;
        RECT r{};GetClientRect(window,&r);const int margin=scaled(12),gap=scaled(8),h=scaled(36);
        const int width=std::max(1,static_cast<int>(r.right)-2*margin);
        const bool stacked=width<scaled(740);
        const int footer=static_cast<int>(r.bottom)-margin-(stacked?2*h+gap:h),top=margin+scaled(48)+gap;
        SetWindowPos(hint,nullptr,margin,margin,width,scaled(48),SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(list,nullptr,margin,top,width,std::max(scaled(16),footer-gap-top),SWP_NOZORDER|SWP_NOACTIVATE);
        const int columns=stacked?2:4,cell=std::max(1,(width-(columns-1)*gap)/columns);
        const int order[]={0,3,1,2};
        for(int i=0;i<4;++i) {
            SetWindowPos(buttons[order[i]],nullptr,margin+(i%columns)*(cell+gap),footer+(i/columns)*(h+gap),cell,h,SWP_NOZORDER|SWP_NOACTIVATE);
        }
        arranging=false;
    }
    void setDpi(UINT value) {
        dpi=value?value:96;
        HFONT next=CreateFontW(-scaled(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!next) throw std::runtime_error("Picker font unavailable");
        for(HWND child:{list,hint,buttons[0],buttons[1],buttons[2],buttons[3]}) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font) DeleteObject(font);font=next;updateExtent();arrange();
    }
    void populate(std::vector<WindowChoice> value) {
        struct ListDrawing {
            HWND list;bool suspended;
            ~ListDrawing() {if(suspended) {SendMessageW(list,WM_SETREDRAW,TRUE,0);RedrawWindow(list,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);}}
        } drawing{list,IsWindowVisible(list)!=FALSE};
        if(drawing.suspended) SendMessageW(list,WM_SETREDRAW,FALSE,0);
        rows=std::move(value);SendMessageW(list,LB_RESETCONTENT,0,0);int selected=-1;
        for(std::size_t i=0;i<rows.size();++i) {
            const auto label=rows[i].title+L" — "+rows[i].application;
            const auto index=SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if(index==LB_ERR || index==LB_ERRSPACE) throw std::runtime_error("Picker list unavailable");
            if(rows[i].window==current && rows[i].process==currentProcess) selected=static_cast<int>(i);
        }
        SendMessageW(list,LB_SETCURSEL,selected,0);EnableWindow(buttons[0],selected>=0);
        SetWindowTextW(hint,rows.empty()?L"Нет окон. Откройте приложение и обновите список.":
            L"Выберите окно: название — приложение. Клики выключены.");
        updateExtent();
    }
    void refresh() {
        const auto index=SendMessageW(list,LB_GETCURSEL,0,0);
        const bool selected=index!=LB_ERR && static_cast<std::size_t>(index)<rows.size();
        const auto previous=selected?rows[index].window:nullptr;
        const auto process=selected?rows[index].process:0;
        try {
            auto next=enumerateWindows(owner);
            current=previous;currentProcess=process;populate(std::move(next));
            if(selected && SendMessageW(list,LB_GETCURSEL,0,0)==LB_ERR)
                SetWindowTextW(hint,L"Окно недоступно. Выберите другое из списка.");
        } catch(...) {
            EnableWindow(buttons[0],SendMessageW(list,LB_GETCURSEL,0,0)!=LB_ERR);
            SetWindowTextW(hint,L"Не удалось обновить список. Повторите или отмените выбор.");
        }
    }
    void updateExtent() {
        HDC dc=GetDC(list);if(dc) {const auto old=SelectObject(dc,font);int extent=0;
            for(const auto& row:rows) {const auto label=row.title+L" — "+row.application;SIZE size{};GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&size);extent=std::max(extent,static_cast<int>(size.cx));}
            SelectObject(dc,old);ReleaseDC(list,dc);SendMessageW(list,LB_SETHORIZONTALEXTENT,extent+scaled(12),0);}
    }
    void accept() {
        const auto index=SendMessageW(list,LB_GETCURSEL,0,0);
        if(index==LB_ERR || static_cast<std::size_t>(index)>=rows.size()) return;
        if(!windowChoiceAvailable(rows[index],owner)) {
            SetWindowTextW(hint,L"Это окно уже закрыто или недоступно. Выберите другое окно либо отмените выбор.");return;
        }
        result.choice=rows[index];DestroyWindow(window);
    }
    void create() {
        const auto instance=GetModuleHandleW(nullptr);
        hint=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,window,reinterpret_cast<HMENU>(5),instance,nullptr);
        list=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,
            0,0,0,0,window,reinterpret_cast<HMENU>(4),instance,nullptr);
        const wchar_t* captions[]={L"Выбрать",L"Наведением (3 сек.)",L"Отмена",L"Обновить список"};
        for(int i=0;i<4;++i) buttons[i]=CreateWindowW(L"BUTTON",captions[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(i==3?6:i+1)),instance,nullptr);
        if(!list || !hint || !buttons[0] || !buttons[1] || !buttons[2] || !buttons[3]) throw std::runtime_error("Picker controls unavailable");
        SetWindowTheme(list,L"",L"");setDpi(GetDpiForWindow(window));
        BOOL theme=dark;DwmSetWindowAttribute(window,20,&theme,sizeof(theme));
    }
    void drawButton(const DRAWITEMSTRUCT& item) const {
        const int saved=SaveDC(item.hDC);SetDCBrushColor(item.hDC,dark?RGB(77,83,94):RGB(241,245,249));
        FillRect(item.hDC,&item.rcItem,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));SelectObject(item.hDC,font);SetBkMode(item.hDC,TRANSPARENT);
        SetTextColor(item.hDC,item.itemState&ODS_DISABLED?RGB(128,128,128):dark?RGB(241,243,247):RGB(30,41,59));
        wchar_t text[64]{};GetWindowTextW(item.hwndItem,text,64);RECT r=item.rcItem;InflateRect(&r,-scaled(4),-scaled(2));
        DrawTextW(item.hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(item.itemState&(ODS_FOCUS|ODS_SELECTED)) DrawFocusRect(item.hDC,&r);RestoreDC(item.hDC,saved);
    }
};
LRESULT CALLBACK pickerProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<Picker*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<Picker*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app) return DefWindowProcW(window,message,wp,lp);
    try {
        switch(message) {
            case WM_CREATE:app->create();return 0;
            case WM_SIZE:app->arrange();return 0;
            case WM_DPICHANGED: {app->setDpi(HIWORD(wp));const auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
            case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={app->scaled(400),app->scaled(320)};return 0;
            case WM_COMMAND:
                if(LOWORD(wp)==1 || (LOWORD(wp)==4 && HIWORD(wp)==LBN_DBLCLK)) app->accept();
                else if(LOWORD(wp)==2) {app->result.manual=true;DestroyWindow(window);}
                else if(LOWORD(wp)==3) DestroyWindow(window);
                else if(LOWORD(wp)==6) app->refresh();
                else if(LOWORD(wp)==4 && HIWORD(wp)==LBN_SELCHANGE) EnableWindow(app->buttons[0],SendMessageW(app->list,LB_GETCURSEL,0,0)!=LB_ERR);
                return 0;
            case WM_DRAWITEM:app->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
            case WM_CTLCOLORSTATIC:case WM_CTLCOLORLISTBOX: {
                const auto dc=reinterpret_cast<HDC>(wp);const bool list=message==WM_CTLCOLORLISTBOX;
                SetTextColor(dc,app->dark?RGB(241,243,247):RGB(30,41,59));SetBkColor(dc,list?(app->dark?RGB(73,78,86):RGB(248,250,252)):(app->dark?RGB(62,66,73):RGB(255,255,255)));
                return reinterpret_cast<LRESULT>(list?app->input:app->background);
            }
            case WM_ERASEBKGND: {RECT r{};GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(wp),&r,app->background);return 1;}
            case WM_CLOSE:DestroyWindow(window);return 0;
            case WM_DESTROY:app->done=true;return 0;
        }
    } catch(...) {app->result.choice.reset();app->result.manual=false;app->result.error=L"Не удалось открыть список окон. Повторите выбор.";DestroyWindow(window);return message==WM_CREATE?-1:0;}
    return DefWindowProcW(window,message,wp,lp);
}
HWND createPicker(Picker& picker) {
    WNDCLASSW wc{};wc.lpfnWndProc=pickerProcedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TriggerLabWindowPicker";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    const UINT dpi=picker.owner?GetDpiForWindow(picker.owner):GetDpiForSystem();
    MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(MonitorFromWindow(picker.owner,MONITOR_DEFAULTTONEAREST),&monitor)) return nullptr;
    const auto r=monitor.rcWork;const int w=std::min(MulDiv(820,dpi,96),static_cast<int>(r.right-r.left)),h=std::min(MulDiv(520,dpi,96),static_cast<int>(r.bottom-r.top));
    return CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"Выбрать окно",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        r.left+(r.right-r.left-w)/2,r.top+(r.bottom-r.top-h)/2,w,h,picker.owner,nullptr,wc.hInstance,&picker);
}
}
bool windowChoiceAvailable(const WindowChoice& choice,HWND owner) {
    DWORD process=0;GetWindowThreadProcessId(choice.window,&process);
    return choice.process && process==choice.process && eligible(choice.window,owner);
}
WindowPickerResult showWindowPicker(HWND owner,bool dark,HWND current) {
    Picker picker(owner,dark,current);const auto window=createPicker(picker);
    if(!window) return {{},false,L"Не удалось открыть список окон. Повторите выбор."};
    try {picker.populate(enumerateWindows(owner));} catch(...) {DestroyWindow(window);return {{},false,L"Не удалось получить список окон. Повторите выбор."};}
    struct RestoreOwner {
        Picker& picker;bool enabled;
        ~RestoreOwner() {if(!picker.done && IsWindow(picker.window)) DestroyWindow(picker.window);
            if(IsWindow(picker.owner)) {if(enabled) EnableWindow(picker.owner,TRUE);SetActiveWindow(picker.owner);}}
    } restore{picker,IsWindowEnabled(owner)!=FALSE};
    if(restore.enabled) EnableWindow(owner,FALSE);
    ShowWindow(window,SW_SHOW);SetFocus(picker.list);MSG message{};
    while(!picker.done) {
        const auto status=GetMessageW(&message,nullptr,0,0);
        if(status<=0) {DestroyWindow(window);if(status==0) PostQuitMessage(static_cast<int>(message.wParam));else picker.result.error=L"Не удалось обработать выбор окна. Повторите действие.";break;}
        if(message.message==WM_KEYDOWN && (message.wParam==VK_ESCAPE || message.wParam==VK_F9)) {DestroyWindow(window);continue;}
        if(message.message==WM_KEYDOWN && message.wParam==VK_RETURN && message.hwnd==picker.list) {picker.accept();continue;}
        if(!IsDialogMessageW(window,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
    }
    return picker.result;
}
#ifdef TRIGGERLAB_UI_TESTS
#include "../../tests/win32/WindowPickerTests.inl"
#endif

}
