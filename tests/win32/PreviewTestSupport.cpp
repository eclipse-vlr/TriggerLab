#include "PreviewTestHarness.hpp"
#include "ImagePaletteDialog.hpp"
#include "triggerlab/ImageFile.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>

namespace triggerlab::win32 {
void pumpTestMessages() {
    // A capture timer can replenish the queue faster than a slow desktop drains
    // it. Yield so the caller can check its deadline and foreground/input state.
    const auto deadline=triggerlab::Clock::now()+std::chrono::milliseconds{20};
    MSG message{};
    for(int count=0;count<64 && triggerlab::Clock::now()<deadline;++count) {
        if(!PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) break;
        TranslateMessage(&message);DispatchMessageW(&message);
    }
}
bool testMessagePumpYields() {
    int dispatched=0;
    HWND queue=CreateWindowW(L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!queue) return false;
    const auto callback=+[](HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context)->LRESULT {
        if(message==WM_APP) {
            auto& count=*reinterpret_cast<int*>(context);
            if(++count<256) PostMessageW(window,WM_APP,0,0);
            return 0;
        }
        return DefSubclassProc(window,message,wp,lp);
    };
    const bool installed=SetWindowSubclass(queue,callback,1,reinterpret_cast<DWORD_PTR>(&dispatched))!=FALSE;
    const bool posted=installed && PostMessageW(queue,WM_APP,0,0);
    if(posted) pumpTestMessages();
    MSG pending{};
    const bool yielded=posted && dispatched>0 && dispatched<=64 && PeekMessageW(&pending,queue,WM_APP,WM_APP,PM_NOREMOVE);
    if(installed) RemoveWindowSubclass(queue,callback,1);
    while(PeekMessageW(&pending,queue,0,0,PM_REMOVE)) {}
    DestroyWindow(queue);return yielded;
}
// Test-only sender: exercise recovery without generating desktop input.
std::atomic<int> recoveryDowns=0,recoveryUps=0;
std::atomic<bool> rejectRecoveryUp=false;
UINT WINAPI recoverySender(UINT count,LPINPUT input,int size) {
    if(count!=1 || size!=sizeof(INPUT)) return 0;
    if(input->mi.dwFlags==MOUSEEVENTF_LEFTDOWN) {++recoveryDowns;return 1;}
    if(input->mi.dwFlags==MOUSEEVENTF_LEFTUP) {
        if(rejectRecoveryUp) return 0;
        ++recoveryUps;return 1;
    }
    return 0;
}

}
