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

void PreviewWindow::paintNarrow(HDC dc) {
        const auto p=themeColors(settings.darkTheme);const int left=24,right=canvasWidth-24;
        auto text=[&](int y,const std::wstring& value,COLORREF color,int height=44,HFONT font=nullptr) {
            SelectObject(dc,font?font:uiFont);SetTextColor(dc,color);RECT r{left,y,right,y+height};
            DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&r,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
        };
        auto card=[&](RECT r,const wchar_t* title) {auto brush=SelectObject(dc,GetStockObject(DC_BRUSH)),pen=SelectObject(dc,GetStockObject(DC_PEN));
            SetDCBrushColor(dc,p.card);SetDCPenColor(dc,p.border);RoundRect(dc,r.left,r.top,r.right,r.bottom,16,16);
            SelectObject(dc,brush);SelectObject(dc,pen);text(r.top+14,title,p.text,42,headingFont);};
        auto position=[&](HWND control) {for(const auto& entry:controlRects) if(entry.control==control) return entry.placed;return RECT{};};
        const int stepsY=12+(settings.advancedMode?44:0);
        const wchar_t* steps[]={L"Окно",L"Цвет мишени",L"Проверка",L"Клики"};
        const int step=wizardStep(),half=(canvasWidth-48)/2;
        for(int i=0;i<4;++i) {RECT r{left+(i%2)*half,stepsY+(i/2)*26,left+(i%2+1)*half,stepsY+26+(i/2)*26};
            SelectObject(dc,uiFont);SetTextColor(dc,i<step?(settings.darkTheme?RGB(134,239,172):RGB(22,163,74)):i==step?p.accent:p.muted);
            const auto label=(i<step?L"✓ ":std::to_wstring(i+1)+L". ")+std::wstring(steps[i]);DrawTextW(dc,label.c_str(),-1,&r,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);}
        card(windowCard,L"1. Окно и режим");card(runCard,settings.advancedMode?L"Настройка кликов":L"Следующий шаг");
        text(settings.advancedMode?position(autoControl).bottom+12:runCard.top+58,currentFeedback().detail,p.muted,64);
        const bool parameters=settings.advancedMode && !profilePane;
        card(optionsCard,parameters?L"Область и время":L"Профиль тренировки");
        if(!parameters) {
            text(position(profileChoice).top-24,profiles.empty()?L"Пока нет профилей":L"Сохранённые профили",p.muted,22);
            text(position(profileName).top-26,L"Название тренировки",p.muted,22);
            text(position(GetDlgItem(window,control::saveProfile)).bottom+10,selectedProfile.empty()?L"Сохраните цвет, область и задержки для этой тренировки":L"Профиль: "+selectedProfile+(profileChanged()?L" · изменён":L" · сохранён"),p.muted,56);
        }
        card(colorCard,L"2. Цвет мишени");
        const int swatchY=position(GetDlgItem(window,control::pickAimColor)).top-58;
        text(swatchY,L"Текущий цвет",p.muted,22);RECT swatch{left,swatchY+26,right,swatchY+50};
        HBRUSH brush=CreateSolidBrush(RGB(color.target.red,color.target.green,color.target.blue));FillRect(dc,&swatch,brush);DeleteObject(brush);
        card(qualityCard,L"Проверка качества цвета");
        card(stateCard,L"Состояние");int y=stateCard.top+58;
        text(y,settings.dryRun?L"ПРОВЕРКА · без кликов":L"РЕАЛЬНЫЕ КЛИКИ",p.accent,44);y+=48;
        text(y,nextHint(),p.muted,70);y+=74;
        text(y,!sample.frame?L"Свежего кадра нет":colorFound()?L"✓ Цвет найден":L"Цвет пока не найден",p.text,24);y+=30;
        text(y,clickStatus,p.text,56);y+=60;
        text(y,L"Клики: "+std::to_wstring(clickCount)+L"   Проверки: "+std::to_wstring(diagnosticCount),p.muted,24);y+=28;
        const auto elapsed=gate.elapsed(triggerlab::Clock::now()).count();
        text(y,L"Задержка: "+std::to_wstring(std::min<std::int64_t>(elapsed,settings.dwellMs))+L" / "+std::to_wstring(settings.dwellMs)+L" мс",p.muted,24);y+=28;
        text(y,pressTimingText(),p.muted,canvasWidth<480?112:56);
        for(int i=0;i<2;++i) {
            card(previewCards[i],i==0?L"Область прицела":L"Совпадения цвета");
            const int side=std::min(200,right-left),x=left+(right-left-side)/2,top=previewCards[i].top+58;
            RECT image{x,top,x+side,top+side};SetDCBrushColor(dc,p.input);FillRect(dc,&image,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if(displayFrame) {
                BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=displayFrame->width();info.bmiHeader.biHeight=-displayFrame->height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
                triggerlab::Frame maskFrame(displayFrame->width(),displayFrame->height());
                if(i==1) for(std::size_t n=0;n<displayMask.pixels.size();++n) {auto v=displayMask.pixels[n];maskFrame.data()[n]={v,v,v,255};}
                StretchDIBits(dc,x,top,side,side,0,0,displayFrame->width(),displayFrame->height(),i==0?displayFrame->data():maskFrame.data(),&info,DIB_RGB_COLORS,SRCCOPY);
                HBRUSH outline=CreateSolidBrush(displayFound?RGB(22,163,74):p.accent);FrameRect(dc,&image,outline);DeleteObject(outline);
                if(i==0 && displaySettings.center && displaySettings.ignoreRadius>0) {
                    const int w=displayFrame->width(),center=w/2,radius=displaySettings.ignoreRadius;
                    RECT excluded{x+(center-radius)*side/w,top+(center-radius)*side/w,x+(center+radius+1)*side/w,top+(center+radius+1)*side/w};
                    HBRUSH hatch=CreateHatchBrush(HS_DIAGCROSS,p.accent);FillRect(dc,&excluded,hatch);DeleteObject(hatch);
                }
            } else text(top+54,i==0?L"Начните захват и активируйте выбранное окно":L"Белым показан найденный цвет",p.muted,90);
            text(top+208,i==0?(displayFrame?L"Снимок: "+std::to_wstring((GetTickCount64()-displayTime)/1000)+L" сек. назад":L"Ожидаем снимок"):L"Найдено: "+std::to_wstring(static_cast<int>(mask.coverage*100))+L"% / порог "+std::to_wstring(settings.minimumPercent)+L"%",p.muted,26);
        }
        text(footerY,saveState(),p.muted,48);text(footerY+52,notice,p.muted,64);text(footerY+122,L"F9 / Esc — остановить",p.muted,28);
    }

} // namespace triggerlab::win32
