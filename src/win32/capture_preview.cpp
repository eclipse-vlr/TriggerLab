#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#include "triggerlab/ColorProfile.hpp"
#include "triggerlab/HoverClickGate.hpp"
#include "triggerlab/ClickPermission.hpp"
#include "triggerlab/ImageFile.hpp"
#include "triggerlab/ImagePaletteDialog.hpp"
#include "triggerlab/AnalyzerSettings.hpp"
#include <cwctype>
#include <fstream>
#include <charconv>
#include <array>
#include "triggerlab/ScreenCapture.hpp"
#include "triggerlab/ColorMask.hpp"
#include <string>
#include <optional>
namespace {
struct ThemeColors {
    COLORREF background,card,input,border,text,muted,accent,button,pressed,stopFill,stopText;
};
ThemeColors themeColors(bool dark) {
    if(dark) return {RGB(48,51,57),RGB(62,66,73),RGB(73,78,86),RGB(91,98,108),RGB(241,243,247),RGB(190,198,211),RGB(147,190,255),RGB(77,83,94),RGB(91,102,120),RGB(96,65,69),RGB(255,184,184)};
    return {RGB(242,246,252),RGB(255,255,255),RGB(248,250,252),RGB(226,232,240),RGB(30,41,59),RGB(100,116,139),RGB(37,99,235),RGB(241,245,249),RGB(219,234,254),RGB(254,242,242),RGB(185,28,28)};
}
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
    HFONT uiFont=nullptr,titleFont=nullptr,headingFont=nullptr;
    HBRUSH cardBrush=CreateSolidBrush(RGB(255,255,255));
    HBRUSH inputBrush=CreateSolidBrush(RGB(248,250,252));
    HWND tooltip=nullptr;
    ~Preview() {if(uiFont) DeleteObject(uiFont);if(titleFont) DeleteObject(titleFont);if(headingFont) DeleteObject(headingFont);if(cardBrush) DeleteObject(cardBrush);if(inputBrush) DeleteObject(inputBrush);}
    triggerlab::ScreenCapture capture;
    triggerlab::CaptureSample sample;
    triggerlab::ColorMask mask;
    triggerlab::ColorMatchConfig color;
    triggerlab::AnalyzerSettings settings;
    std::filesystem::path settingsPath;
    std::array<HWND,8> settingEdits{};
    bool syntheticDown=false;
    triggerlab::Clock::time_point releaseAt{};
    HWND modeControl=nullptr,dryControl=nullptr;
    std::optional<triggerlab::CapturePoint> analysisPoint;
    std::optional<triggerlab::Region> analyzedClient;
    std::uint64_t diagnosticCount=0;
    ULONGLONG lastPaint=0;
    std::unique_ptr<PaintSurface> backBuffer;
    int bufferWidth=0,bufferHeight=0;
    HWND window=nullptr,targetWindow=nullptr,autoControl=nullptr; bool running=false,autoClick=false;
    DWORD targetProcess=0;std::wstring targetTitle;
    std::array<HWND,4> colorEdits{};
    triggerlab::HoverClickGate gate;
    std::uint64_t clickCount=0;
    std::wstring clickStatus=L"Выключен";
    ULONGLONG pickDeadline=0,targetDeadline=0,aimPickDeadline=0;
    std::filesystem::path colorPath;
    std::optional<triggerlab::Frame> importedImage;
    Preview() {
        wchar_t local[32768]{};auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
        colorPath=length>0 && length<32768?std::filesystem::path(local)/L"TriggerLab"/L"selected-color.json":std::filesystem::path(L"selected-color.json");
        const auto loaded=triggerlab::loadColorProfile(colorPath);if(loaded.ok) color=loaded.color;
        settingsPath=colorPath.parent_path()/L"analyzer-settings.json";
        settings=triggerlab::loadAnalyzerSettings(settingsPath);
        settings.dryRun=true; // Every launch starts in diagnostic mode.
        gate.setTiming(std::chrono::milliseconds{settings.dwellMs},std::chrono::milliseconds{settings.cooldownMs});
    }
    void applyTheme() {
        const auto p=themeColors(settings.darkTheme);
        auto newCard=CreateSolidBrush(p.card),newInput=CreateSolidBrush(p.input);
        if(newCard && newInput) {DeleteObject(cardBrush);DeleteObject(inputBrush);cardBrush=newCard;inputBrush=newInput;}
        else {if(newCard) DeleteObject(newCard);if(newInput) DeleteObject(newInput);}
        SetWindowTextW(GetDlgItem(window,21),settings.darkTheme?L"Серая тема: включена":L"Серая тема: выключена");
        BOOL dark=settings.darkTheme;
        DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
        DwmSetWindowAttribute(window,34,&p.background,sizeof(p.background));
        const COLORREF systemCaptionText=0xFFFFFFFF;
        DwmSetWindowAttribute(window,36,&systemCaptionText,sizeof(systemCaptionText));
        if(tooltip) {SendMessageW(tooltip,TTM_SETTIPBKCOLOR,p.card,0);SendMessageW(tooltip,TTM_SETTIPTEXTCOLOR,p.text,0);}
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    void syncActions() {
        EnableWindow(GetDlgItem(window,2),running || pickDeadline || targetDeadline || aimPickDeadline);
        const bool target=targetWindow && IsWindow(targetWindow);
        EnableWindow(autoControl,running && target);
        EnableWindow(GetDlgItem(window,20),settings.center && target);
    }
    std::optional<triggerlab::Region> clientBounds() const {
        if(!targetWindow || !IsWindow(targetWindow) || IsIconic(targetWindow)) return {};
        RECT rect{};POINT origin{};
        if(!GetClientRect(targetWindow,&rect) || !ClientToScreen(targetWindow,&origin) || rect.right<=0 || rect.bottom<=0) return {};
        return triggerlab::Region{origin.x,origin.y,rect.right,rect.bottom};
    }
    void syncSettings() {
        const int values[]={settings.side,settings.ignoreRadius,settings.minimumPercent,settings.offsetX,settings.offsetY,settings.dwellMs,settings.cooldownMs,settings.clickMs};
        for(int i=0;i<8;++i) SetWindowTextW(settingEdits[i],std::to_wstring(values[i]).c_str());
        SendMessageW(modeControl,CB_SETCURSEL,settings.center?0:1,0);
        SendMessageW(dryControl,BM_SETCHECK,settings.dryRun?BST_CHECKED:BST_UNCHECKED,0);
    }
    bool applySettings() {
        cancelSelection();disableClicks();int values[8]{};
        for(int i=0;i<8;++i) {
            char text[32]{};GetWindowTextA(settingEdits[i],text,32);const std::string value(text);
            const auto result=std::from_chars(value.data(),value.data()+value.size(),values[i]);
            if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size()) {status=L"Параметры: введите целые числа";return false;}
        }
        auto next=settings;next.side=values[0];next.ignoreRadius=values[1];next.minimumPercent=values[2];next.offsetX=values[3];next.offsetY=values[4];next.dwellMs=values[5];next.cooldownMs=values[6];next.clickMs=values[7];
        if(!triggerlab::validAnalyzerSettings(next)) {status=L"ROI 5..101 нечётный; игнор < ROI/2; порог 1..100%; задержка 1..2000; интервал 50..2000";return false;}
        settings=next;gate.setTiming(std::chrono::milliseconds{settings.dwellMs},std::chrono::milliseconds{settings.cooldownMs});
        std::string error;status=triggerlab::saveAnalyzerSettings(settingsPath,settings,error)?L"Параметры сохранены; триггер выключен":L"Параметры применены, но сохранение не удалось";
        return true;
    }
    void findTrainer(const wchar_t* name) {
        cancelSelection();disableClicks();
        struct Search {const wchar_t* name;HWND own;std::vector<HWND> found;} search{name,window,{}};
        EnumWindows([](HWND candidate,LPARAM parameter)->BOOL {
            auto& search=*reinterpret_cast<Search*>(parameter);if(candidate==search.own || !IsWindowVisible(candidate)) return TRUE;
            DWORD processId=0;GetWindowThreadProcessId(candidate,&processId);
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processId);
            if(!process) return TRUE;
            wchar_t path[32768]{};DWORD length=32768;const bool readable=QueryFullProcessImageNameW(process,0,path,&length)!=FALSE;CloseHandle(process);
            if(!readable) return TRUE;
            auto executable=std::filesystem::path(path).filename().wstring();
            for(auto& c:executable) c=static_cast<wchar_t>(std::towlower(c));
            const bool kovaak=std::wstring_view(search.name)==L"kovaak";
            if(executable.find(kovaak?L"fpsaimtrainer":L"aimlab")==std::wstring::npos) return TRUE;
            wchar_t title[512]{};GetWindowTextW(candidate,title,512);std::wstring text(title);
            for(auto& c:text) c=static_cast<wchar_t>(std::towlower(c));
            text.erase(std::remove_if(text.begin(),text.end(),[](wchar_t c) {return std::iswspace(c)!=0;}),text.end());
            if(text.find(search.name)!=std::wstring::npos) search.found.push_back(candidate);return TRUE;
        },reinterpret_cast<LPARAM>(&search));
        if(search.found.size()!=1) {status=search.found.empty()?L"Тренажёр не запущен. Откройте его, затем повторите поиск":L"Найдено несколько окон: используйте ручной выбор";return;}
        targetWindow=search.found.front();GetWindowThreadProcessId(targetWindow,&targetProcess);refreshTargetButton();settings.center=true;syncSettings();
        status=L"Тренажёр выбран. Начните с проверки без кликов";
    }
    void syncColor() {
        const int values[]={color.target.red,color.target.green,color.target.blue,color.channelTolerance};
        for(int i=0;i<4;++i) SetWindowTextW(colorEdits[i],std::to_wstring(values[i]).c_str());
    }
    void finishClick(bool count=true) {
        if(!syntheticDown) return;
        syntheticDown=false;KillTimer(window,4);
        INPUT up{};up.type=INPUT_MOUSE;up.mi.dwFlags=MOUSEEVENTF_LEFTUP;
        const bool released=SendInput(1,&up,sizeof(INPUT))==1;
        if(released && count) {gate.acknowledgeClick(triggerlab::Clock::now());++clickCount;clickStatus=L"Клик отправлен: нажатие и отпускание";}
        if(!released) {SendInput(1,&up,sizeof(INPUT));autoClick=false;UnregisterHotKey(window,9);gate.reset();SendMessageW(autoControl,BM_SETCHECK,BST_UNCHECKED,0);notice=clickStatus=L"Отпускание отклонено системой; триггер выключен";}
    }
    void disableClicks() {finishClick(false);UnregisterHotKey(window,9);autoClick=false;gate.reset();clickStatus=L"Выключен";if(autoControl) SendMessageW(autoControl,BM_SETCHECK,BST_UNCHECKED,0);}
    void cancelSelection() {
        pickDeadline=targetDeadline=aimPickDeadline=0;KillTimer(window,2);
        if(!autoClick) UnregisterHotKey(window,9);
        SetWindowTextW(GetDlgItem(window,5),L"Пипетка (3 сек.)");refreshTargetButton();
        SetWindowTextW(GetDlgItem(window,20),L"Цвет из прицела (3 сек.)");
    }
    void beginAimSelection() {
        cancelSelection();disableClicks();
        if(!settings.center || !clientBounds()) {status=L"Выберите окно и режим «Вокруг прицела окна»";return;}
        if(!RegisterHotKey(window,9,MOD_NOREPEAT,VK_F9)) {status=L"F9 занят: снимок не начат";return;}
        if(!SetTimer(window,2,100,nullptr)) {UnregisterHotKey(window,9);status=L"Ошибка таймера: снимок не начат";return;}
        aimPickDeadline=GetTickCount64()+3000;
        status=L"За 3 сек. активируйте тренировку и наведите прицел на мишень; F9 — отмена";
    }
    void finishAimSelection() {
        cancelSelection();disableClicks();
        const auto bounds=clientBounds();
        const auto roi=bounds?triggerlab::aimRegion(*bounds,settings):std::optional<triggerlab::Region>{};
        if(!roi || !permittedAt({static_cast<LONG>(roi->x+roi->width/2),static_cast<LONG>(roi->y+roi->height/2)})) {
            status=notice=L"Снимок отменён: область недоступна или тренажёр не активен";return;
        }
        auto fresh=capture.capture(*roi);
        const auto currentBounds=clientBounds();
        if(!fresh.frame || fresh.allBlack || !currentBounds || currentBounds->x!=bounds->x || currentBounds->y!=bounds->y || currentBounds->width!=bounds->width || currentBounds->height!=bounds->height || !permittedAt({static_cast<LONG>(roi->x+roi->width/2),static_cast<LONG>(roi->y+roi->height/2)}) || fresh.region.x!=roi->x || fresh.region.y!=roi->y || fresh.region.width!=roi->width || fresh.region.height!=roi->height) {
            status=notice=L"Снимок недоступен; прежний цвет сохранён";return;
        }
        // Freeze a fresh image before the modal steals focus or timer messages arrive.
        running=false;KillTimer(window,1);sample={};mask={};analysisPoint.reset();capture.reset();
        const auto selected=triggerlab::showImagePalette(window,*fresh.frame);
        if(selected) {color.target=selected->color;color.space=triggerlab::ColorSpace::RGB;syncColor();status=L"Цвет мишени выбран. Сохраните цвет и начните проверку без кликов";}
        else status=L"Выбор отменён; прежний цвет сохранён";
        notice=status;
    }
    bool applyColor() {
        cancelSelection();disableClicks();
        int values[4]{};
        for(int i=0;i<4;++i) {
            char text[32]{};GetWindowTextA(colorEdits[i],text,32);const std::string value(text);
            auto result=std::from_chars(value.data(),value.data()+value.size(),values[i]);
            if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || values[i]<0 || values[i]>255) {status=L"RGB и допуск: целые числа 0..255";return false;}
        }
        color.target={static_cast<std::uint8_t>(values[2]),static_cast<std::uint8_t>(values[1]),static_cast<std::uint8_t>(values[0]),255};color.channelTolerance=values[3];color.space=triggerlab::ColorSpace::RGB;return true;
    }
    void chooseColor() {
        cancelSelection();disableClicks();COLORREF custom[16]{};CHOOSECOLORW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=window;
        picker.rgbResult=RGB(color.target.red,color.target.green,color.target.blue);picker.lpCustColors=custom;picker.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(ChooseColorW(&picker)) {color.target={GetBValue(picker.rgbResult),GetGValue(picker.rgbResult),GetRValue(picker.rgbResult),255};color.space=triggerlab::ColorSpace::RGB;syncColor();status=L"Цвет выбран. Нажмите «Сохранить цвет»";}
    }
    void saveColor() {if(!applyColor()) return;std::string error;status=triggerlab::saveColorProfile(colorPath,color,error)?L"Цвет сохранён":L"Не удалось сохранить цвет";}
    bool importImage(const std::filesystem::path& path,bool edit=false) {
        cancelSelection();disableClicks();
        auto loaded=triggerlab::loadImagePixels(path);
        if(!loaded.image) {status=loaded.error;notice=status;return false;}
        auto selected=edit?triggerlab::showImagePalette(window,*loaded.image):triggerlab::dominantImageColor(*loaded.image);
        if(!selected) {status=edit?L"Выбор цвета отменён; прежний цвет сохранён":L"В изображении нет непрозрачных пикселей";notice=status;return false;}
        color.target=selected->color;color.space=triggerlab::ColorSpace::RGB;syncColor();
        importedImage=std::move(loaded.image);
        mask=sample.frame?triggerlab::makeColorMask(*sample.frame,color):triggerlab::ColorMask{};
        status=L"Цвет из файла определён. Нажмите «Сохранить цвет»";
        notice=L"Файл: "+path.filename().wstring().substr(0,30)+L" | Доля оттенка: "+
            std::to_wstring(static_cast<int>(selected->coverage*100))+L"%";
        return true;
    }
    void editImage() {
        cancelSelection();disableClicks();
        if(!importedImage) {status=L"Сначала загрузите изображение кнопкой «Из файла…»";return;}
        const auto selected=triggerlab::showImagePalette(window,*importedImage);
        if(!selected) {status=L"Выбор отменён; прежний цвет сохранён";return;}
        color.target=selected->color;color.space=triggerlab::ColorSpace::RGB;syncColor();
        mask=sample.frame?triggerlab::makeColorMask(*sample.frame,color):triggerlab::ColorMask{};
        status=L"Цвет области выбран. Нажмите «Сохранить цвет»";
    }
    void chooseImage() {
        cancelSelection();disableClicks();wchar_t path[32768]{};
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
        dialog.lpstrFilter=L"Изображения (PNG, JPEG, BMP, GIF, TIFF)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0Все файлы\0*.*\0\0";
        dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrTitle=L"Определить цвет из изображения";
        dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
        if(GetOpenFileNameW(&dialog)) importImage(path,true);
        else {status=CommDlgExtendedError()?L"Не удалось открыть выбор файла":L"Выбор изображения отменён";notice=status;}
    }
    void refreshTargetButton() {
        if(targetWindow) {
            DWORD process=0;GetWindowThreadProcessId(targetWindow,&process);
            if(!IsWindow(targetWindow) || process!=targetProcess) {
                targetWindow=nullptr;targetProcess=0;targetTitle.clear();
                if(autoClick) disableClicks(); // Keep the selection's F9 registration alive.
                notice=L"Выбранное окно закрыто";
            } else {
                wchar_t title[256]{};GetWindowTextW(targetWindow,title,256);
                targetTitle=title[0]?title:L"Без названия";
            }
        }
        std::wstring caption=L"Окно не выбрано\nВыбрать (3 сек.)";
        if(targetDeadline) {
            const auto now=GetTickCount64();const auto seconds=now<targetDeadline?(targetDeadline-now+999)/1000:0;
            caption=L"Выбор окна: "+std::to_wstring(seconds)+L" сек.\nНаведите курсор";
        }
        else if(targetWindow) {
            auto title=targetTitle;
            if(title.size()>25) {title.resize(24);if(title.back()>=0xD800 && title.back()<=0xDBFF) title.pop_back();title+=L"…";}
            caption=L"Окно: "+title+L"\nСменить (3 сек.)";
        }
        HWND button=GetDlgItem(window,7);wchar_t previous[256]{};GetWindowTextW(button,previous,256);
        if(caption!=previous) {SetWindowTextW(button,caption.c_str());InvalidateRect(window,nullptr,FALSE);}
    }
    void beginSelection(bool pick) {
        cancelSelection();disableClicks();
        if(!RegisterHotKey(window,9,MOD_NOREPEAT,VK_F9)) {status=L"F9 занят: выбор не начат";return;}
        if(!SetTimer(window,2,100,nullptr)) {UnregisterHotKey(window,9);status=L"Ошибка таймера: выбор не начат";return;}
        if(pick) pickDeadline=GetTickCount64()+3000;
        else targetDeadline=GetTickCount64()+3000;
        status=pick?L"Наведите курсор на цвет. Через 3 сек. — снимок; F9 — отмена":L"3 секунды: наведите курсор на целевое окно; F9 — отмена";
        selectionTick(GetTickCount64());
    }
    void selectionTick(ULONGLONG now) {
        if(aimPickDeadline && now>=aimPickDeadline) {finishAimSelection();InvalidateRect(window,nullptr,FALSE);return;}
        if(aimPickDeadline) {
            const auto caption=L"Снимок прицела: "+std::to_wstring((aimPickDeadline-now+999)/1000)+L" сек.";
            SetWindowTextW(GetDlgItem(window,20),caption.c_str());
        }
        if(pickDeadline && now>=pickDeadline) {
            cancelSelection();update(true);return;
        }
        if(targetDeadline && now>=targetDeadline) {
            cancelSelection();finishTargetSelection();notice=status;InvalidateRect(window,nullptr,FALSE);return;
        }
        if(pickDeadline) {
            const auto caption=L"Пипетка: "+std::to_wstring((pickDeadline-now+999)/1000)+L" сек.";
            wchar_t previous[128]{};GetWindowTextW(GetDlgItem(window,5),previous,128);
            if(caption!=previous) SetWindowTextW(GetDlgItem(window,5),caption.c_str());
        }
        refreshTargetButton();
    }
    void finishTargetSelection() {
        POINT point{};if(!GetCursorPos(&point)) {status=L"Курсор недоступен: выбор окна отменён";return;}
        HWND selected=GetAncestor(WindowFromPoint(point),GA_ROOT);
        if(!selected || selected==window || !IsWindowVisible(selected)) {status=L"Выбор отменён; прежнее окно сохранено";refreshTargetButton();return;}
        targetWindow=selected;GetWindowThreadProcessId(selected,&targetProcess);
        wchar_t title[256]{};GetWindowTextW(selected,title,256);targetTitle=title;
        status=L"Окно выбрано. Включите автоклик отдельно";refreshTargetButton();
    }
    bool permittedAt(POINT point) const {
        DWORD process=0;if(!targetWindow || !IsWindow(targetWindow)) return false;
        GetWindowThreadProcessId(targetWindow,&process);
        return triggerlab::ClickPermission{reinterpret_cast<std::uintptr_t>(targetWindow),reinterpret_cast<std::uintptr_t>(GetAncestor(WindowFromPoint(point),GA_ROOT)),reinterpret_cast<std::uintptr_t>(GetAncestor(GetForegroundWindow(),GA_ROOT)),targetProcess,process,true}.allowed();
    }
    void handleClick(POINT capturedCursor) {
        if(!autoClick) return;
        if(!IsWindow(targetWindow)) {disableClicks();status=L"Автоклик выключен: целевое окно закрыто";return;}
        if(inputHeld()) {gate.step(false,false,triggerlab::Clock::now());clickStatus=L"Пауза: зажата кнопка мыши или клавиша-модификатор";return;}
        if(!permittedAt(capturedCursor)) {
            // A focus change is not evidence that the cursor left the color.
            const bool outside=GetAncestor(WindowFromPoint(capturedCursor),GA_ROOT)!=targetWindow;
            gate.step(sample.frame.has_value() && outside,false,triggerlab::Clock::now());
            clickStatus=outside?L"Курсор вне выбранного окна":L"Выбранное окно не активно: активируйте его";return;
        }
        const bool match=settings.center?mask.found:(sample.cursorPixel && triggerlab::matchesColor(*sample.cursorPixel,color));
        const auto now=triggerlab::Clock::now();
        if(!gate.step(sample.frame.has_value(),match,now)) {
            clickStatus=!match?L"Ожидаю цвет в области обнаружения":gate.clickedHover()?L"Срабатывание выполнено: нужен уход с цвета":L"Совпадение: ожидание "+std::to_wstring(gate.elapsed(now).count())+L" / "+std::to_wstring(settings.dwellMs)+L" мс";
            return;
        }
        if(settings.center) {
            const auto bounds=clientBounds();
            if(!bounds || !analyzedClient || bounds->x!=analyzedClient->x || bounds->y!=analyzedClient->y || bounds->width!=analyzedClient->width || bounds->height!=analyzedClient->height) {
                gate.step(false,false,triggerlab::Clock::now());clickStatus=L"Геометрия окна изменилась: ожидание сброшено";return;
            }
        }
        if(settings.dryRun) {gate.acknowledgeClick(triggerlab::Clock::now());++diagnosticCount;clickStatus=L"Проверка: срабатывание без отправки клика";return;}
        POINT current{};
        if(!GetCursorPos(&current) || (!settings.center && (current.x!=capturedCursor.x || current.y!=capturedCursor.y)) || !permittedAt(current) || !permittedAt(capturedCursor) || inputHeld()) {
            gate.step(false,false,triggerlab::Clock::now());clickStatus=L"Ожидание прервано: курсор, окно или клавиши изменились";return;
        }
        INPUT down{};down.type=INPUT_MOUSE;down.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
        if(SendInput(1,&down,sizeof(INPUT))==1) {
            syntheticDown=true;releaseAt=triggerlab::Clock::now()+std::chrono::milliseconds{settings.clickMs};clickStatus=L"Нажатие выполняется";
            if(!SetTimer(window,4,5,nullptr)) {finishClick(false);disableClicks();notice=L"Ошибка таймера отпускания: триггер выключен";}
        } else {const auto error=GetLastError();disableClicks();status=L"Ввод отклонён системой (код "+std::to_wstring(error)+L")";clickStatus=status;notice=status;}
    }
    static bool inputHeld() {
        for(int key:{VK_CONTROL,VK_MENU,VK_SHIFT,VK_LWIN,VK_RWIN,VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2})
            if(GetAsyncKeyState(key)&0x8000) return true;
        return false;
    }
    void createControls() {
        auto font=[](int size,int weight) {return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");};
        uiFont=font(16,FW_NORMAL);titleFont=font(30,FW_SEMIBOLD);headingFont=font(18,FW_SEMIBOLD);
        auto button=[&](int id,const wchar_t* label,int x,int y,int w,int h=36) {
            return CreateWindowW(L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
        };
        auto label=[&](const wchar_t* text,int x,int y,int width) {CreateWindowW(L"STATIC",text,WS_CHILD|WS_VISIBLE,x,y,width,22,window,nullptr,nullptr,nullptr);};
        button(21,L"Серая тема: выключена",914,20,168,36);
        button(7,L"Выбрать окно (3 сек.)",40,130,260,58);
        button(18,L"Kovaak’s",316,130,135);button(19,L"Aimlabs",316,174,135,30);
        modeControl=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,40,210,410,140,window,reinterpret_cast<HMENU>(15),nullptr,nullptr);
        SendMessageW(modeControl,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Обнаружение вокруг прицела"));
        SendMessageW(modeControl,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Обнаружение под курсором"));
        button(1,L"Начать захват",548,130,250,42);button(2,L"Остановить · F9",812,130,250,42);
        dryControl=CreateWindowW(L"BUTTON",L"Проверять без кликов",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,548,190,235,28,window,reinterpret_cast<HMENU>(16),nullptr,nullptr);
        autoControl=CreateWindowW(L"BUTTON",L"Включить триггер",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,812,190,250,28,window,reinterpret_cast<HMENU>(8),nullptr,nullptr);
        const wchar_t* settingsLabels[]={L"Область, px",L"Исключить центр",L"Порог, %",L"Сдвиг X, px",L"Сдвиг Y, px",L"Задержка, мс",L"Интервал, мс",L"Нажатие, мс"};
        for(int i=0;i<8;++i) {
            label(settingsLabels[i],40+i*128,300,124);
            settingEdits[i]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,40+i*128,326,112,30,window,nullptr,nullptr,nullptr);
        }
        label(L"Область 5–101 px, нечётная. Исключите перекрестие; порог задаёт долю нужного цвета.",40,368,820);
        button(17,L"Сохранить",938,365,124,30);
        const wchar_t* colors[]={L"Красный · R",L"Зелёный · G",L"Синий · B",L"Допуск"};
        for(int i=0;i<4;++i) {
            label(colors[i],40+i*110,457,104);
            colorEdits[i]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER,40+i*110,482,96,30,window,nullptr,nullptr,nullptr);
        }
        button(3,L"Применить",482,482,120,30);button(6,L"Сохранить цвет",618,482,160,30);
        button(20,L"Цвет из прицела (3 сек.)",40,530,240);
        button(5,L"Пипетка (3 сек.)",294,530,174);
        button(4,L"Выбрать цвет…",482,530,166);
        button(10,L"Из файла…",662,530,152);
        button(14,L"Палитра / область",828,530,234);
        EnumChildWindows(window,[](HWND child,LPARAM f)->BOOL {SendMessageW(child,WM_SETFONT,static_cast<WPARAM>(f),TRUE);return TRUE;},reinterpret_cast<LPARAM>(uiFont));
        EnumChildWindows(window,[](HWND child,LPARAM)->BOOL {SetWindowTheme(child,L"",L"");return TRUE;},0);
        for(HWND control:{dryControl,autoControl}) SetWindowSubclass(control,checkProcedure,1,reinterpret_cast<DWORD_PTR>(this));
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES};InitCommonControlsEx(&common);
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(tooltip) {
            SetWindowTheme(tooltip,L"",L"");SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,360);
            SendMessageW(tooltip,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),FALSE);
            auto tip=[&](HWND control,const wchar_t* text) {TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(control);info.lpszText=const_cast<wchar_t*>(text);SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));};
            const wchar_t* tips[]={L"Размер квадратной области вокруг прицела: нечётное число от 5 до 101 пикселя.",L"Радиус исключения перекрестия. Центр не участвует в распознавании; 0 отключает исключение.",L"Какая доля области должна совпадать с цветом: от 1 до 100 процентов.",L"Смещение области вправо (+) или влево (−) от центра окна.",L"Смещение области вниз (+) или вверх (−) от центра окна.",L"Цвет должен непрерывно совпадать столько миллисекунд до срабатывания.",L"Минимальный интервал после предыдущего клика. Для повторения цвет должен исчезнуть и появиться вновь.",L"Длительность удержания левой кнопки мыши: 5–150 мс. По умолчанию 30 мс."};
            for(int i=0;i<8;++i) tip(settingEdits[i],tips[i]);
            tip(colorEdits[3],L"Допустимое отклонение каналов RGB: 0–255. Большой допуск может захватывать фон.");
            tip(dryControl,L"Проверка распознавания и задержки без отправки мышиных кликов.");
            tip(autoControl,L"Сначала выберите окно и начните захват. Один клик на непрерывное совпадение; F9 останавливает работу.");
            tip(GetDlgItem(window,20),L"Через 3 секунды откроется снимок прицела. Активируйте тренировку и выберите цвет мишени в палитре.");
            tip(GetDlgItem(window,21),L"Переключение между светлой и тёмно-серой темой. Выбор сохраняется автоматически.");
        }
        syncColor();syncSettings();refreshTargetButton();applyTheme();syncActions();SetTimer(window,3,500,nullptr);
    }
    void command(int id,int notification=BN_CLICKED) {
        if(id==1) {if(SetTimer(window,1,10,nullptr)) {running=true;status=L"Захват активен";}else status=L"Не удалось начать захват";}
        else if(id==2) stop();
        else if(id==3) {if(applyColor()) status=L"RGB применён; автоклик выключен";}
        else if(id==4) chooseColor();
        else if(id==5) beginSelection(true);
        else if(id==6) saveColor();
        else if(id==7) beginSelection(false);
        else if(id==14) editImage();
        else if(id==17) applySettings();
        else if(id==18) findTrainer(L"kovaak");
        else if(id==19) findTrainer(L"aimlab");
        else if(id==20) beginAimSelection();
        else if(id==21) {
            settings.darkTheme=!settings.darkTheme;applyTheme();std::string error;
            notice=triggerlab::saveAnalyzerSettings(settingsPath,settings,error)?L"Тема сохранена":L"Тема изменена, но не удалось сохранить выбор";
            return;
        }
        else if(id==15 || id==16) {
            if(id==15 && notification!=CBN_SELCHANGE) return;
            cancelSelection();disableClicks();settings.center=SendMessageW(modeControl,CB_GETCURSEL,0,0)==0;settings.dryRun=SendMessageW(dryControl,BM_GETCHECK,0,0)==BST_CHECKED;
            sample={};mask={};analysisPoint.reset();std::string error;
            status=triggerlab::saveAnalyzerSettings(settingsPath,settings,error)?L"Режим сохранён; включите триггер отдельно":L"Режим изменён; сохранение не удалось";
        }
        else if(id==10) {chooseImage();SetFocus(window);InvalidateRect(window,nullptr,FALSE);return;}
        else if(id==8) {cancelSelection();if(!running || !targetWindow || !IsWindow(targetWindow)) {disableClicks();status=L"Сначала включите захват и выберите окно";}else {autoClick=SendMessageW(autoControl,BM_GETCHECK,0,0)==BST_CHECKED;gate.reset();if(autoClick && !RegisterHotKey(window,9,MOD_NOREPEAT,VK_F9)) {disableClicks();status=L"F9 занят: автоклик не включён";notice=status;InvalidateRect(window,nullptr,FALSE);return;}if(!autoClick) UnregisterHotKey(window,9);status=autoClick?L"Автоклик включён только в выбранном окне":L"Автоклик выключен";}}
        else return;
        if(id==8 && autoClick) {
            // Arming is an explicit user action: activate the chosen window,
            // and do not immediately steal focus back to the analyzer.
            if(!SetForegroundWindow(targetWindow)) {
                clickStatus=L"Активируйте выбранное окно вручную";
                status=L"Автоклик включён; активируйте выбранное окно";
            } else clickStatus=L"Ожидаю цвет в выбранном окне";
        } else SetFocus(window);
        syncActions();notice=status;InvalidateRect(window,nullptr,FALSE);
    }
    std::wstring notice;
    std::wstring status=L"Захват остановлен";
    void stop() {running=false;disableClicks();cancelSelection();KillTimer(window,1);capture.reset();sample={};mask={};status=L"Захват остановлен";notice=status;syncActions();InvalidateRect(window,nullptr,FALSE);}
    void update(bool pick=false) {
        if(!running && !pick) return;
        if(syntheticDown) {
            if(GetAncestor(GetForegroundWindow(),GA_ROOT)!=targetWindow) {finishClick(false);disableClicks();return;}
            if(triggerlab::Clock::now()<releaseAt) return;
            finishClick();
        }
        if(pick) {cancelSelection();disableClicks();}
        analysisPoint.reset();analyzedClient.reset();
        if(pick || !settings.center) acceptSample(capture.captureCursor(),pick);
        else {
            const auto bounds=clientBounds();
            const auto roi=bounds?triggerlab::aimRegion(*bounds,settings):std::optional<triggerlab::Region>{};
            if(!roi || GetAncestor(GetForegroundWindow(),GA_ROOT)!=targetWindow) {
                acceptSample({},false);status=!bounds?L"Выберите окно тренажёра":!roi?L"Область прицела выходит за окно; уменьшите сдвиг / ROI":L"Активируйте выбранный тренажёр";
                clickStatus=status;
            } else {
                POINT center{static_cast<LONG>(roi->x+roi->width/2),static_cast<LONG>(roi->y+roi->height/2)};
                if(!permittedAt(center)) {acceptSample({},false);status=clickStatus=L"Область прицела перекрыта другим окном";}
                else {
                    auto fresh=capture.capture(*roi);
                    if(fresh.frame && (fresh.region.x!=roi->x || fresh.region.y!=roi->y || fresh.region.width!=roi->width || fresh.region.height!=roi->height)) fresh={};
                    analyzedClient=bounds;analysisPoint=triggerlab::CapturePoint{center.x,center.y};acceptSample(std::move(fresh),false);
                }
            }
        }
        if(!running) capture.reset();
    }
    void acceptSample(triggerlab::CaptureSample fresh,bool pick) {
        sample=std::move(fresh);mask={};
        // Use the actual window under the sampled pixel, not the analyzer's
        // rectangle: another window may cover it, and neighboring ROI pixels
        // do not determine whether the cursor color matches.
        const bool ownPixel=sample.cursorPosition &&
            GetAncestor(WindowFromPoint({sample.cursorPosition->x,sample.cursorPosition->y}),GA_ROOT)==window;
        if(ownPixel) {
            if(autoClick && sample.frame) gate.step(true,false,triggerlab::Clock::now());
            sample={};status=L"Уведите курсор за окно анализатора";
        }
        else if(sample.frame) {
            if(pick && sample.cursorPixel) {color.target=*sample.cursorPixel;color.space=triggerlab::ColorSpace::RGB;syncColor();}
            mask=settings.center && !pick?triggerlab::aimColorMask(*sample.frame,color,settings):triggerlab::makeColorMask(*sample.frame,color);
            if(sample.allBlack && settings.center) mask.found=false;
            const bool found=settings.center && !pick?mask.found:(sample.cursorPixel && triggerlab::matchesColor(*sample.cursorPixel,color));
            status=sample.allBlack?L"Чёрный ROI: содержимое окна может быть недоступно":found?L"СОВПАДЕНИЕ в области обнаружения":L"Нет совпадения";
        } else status=sample.status==triggerlab::CaptureStatus::CursorMoved?L"Курсор движется: удерживайте его на нужном цвете":L"Захват недоступен (статус "+std::to_wstring(static_cast<int>(sample.status))+L")";
        if(!settings.center || pick) analysisPoint=sample.cursorPosition;
        if(autoClick && sample.frame && analysisPoint) handleClick({analysisPoint->x,analysisPoint->y});
        else if(autoClick) {gate.step(false,false,triggerlab::Clock::now());clickStatus=ownPixel?L"Курсор над анализатором":L"Нет свежего пикселя: "+status;}
        if(pick) {notice=sample.cursorPixel?L"Цвет взят с экрана. Нажмите «Сохранить цвет»":L"Пипетка: свежий пиксель недоступен, цвет не изменён";}
        if(GetTickCount64()-lastPaint>=33) {lastPaint=GetTickCount64();InvalidateRect(window,nullptr,FALSE);}
    }
    bool smokeTest(const std::filesystem::path& path) {
        if(!triggerlab::imagePaletteDialogSmoke()) return false;
        colorPath=path;
        settingsPath=path.parent_path()/L"analyzer-settings.json";
        const bool originalTheme=settings.darkTheme;
        command(21);
        if(settings.darkTheme==originalTheme || triggerlab::loadAnalyzerSettings(settingsPath).darkTheme!=settings.darkTheme || running || autoClick) return false;
        command(21);
        if(settings.darkTheme!=originalTheme || triggerlab::loadAnalyzerSettings(settingsPath).darkTheme!=originalTheme) return false;
        const wchar_t* analyzerValues[]={L"41",L"4",L"5",L"-20",L"10",L"100",L"80",L"40"};
        for(int i=0;i<8;++i) SetWindowTextW(settingEdits[i],analyzerValues[i]);
        if(!applySettings()) return false;
        const auto savedSettings=triggerlab::loadAnalyzerSettings(settingsPath);
        if(savedSettings.side!=41 || savedSettings.offsetX!=-20 || savedSettings.dwellMs!=100 || savedSettings.cooldownMs!=80) return false;
        SetWindowTextW(settingEdits[0],L"40");if(applySettings() || settings.side!=41) return false;
        settings={};syncSettings();gate.setTiming(std::chrono::milliseconds{70},std::chrono::milliseconds{50});
        for(auto edit:colorEdits) if(!IsWindow(edit)) return false;
        const wchar_t* values[]={L"231",L"67",L"19",L"12"};
        for(int i=0;i<4;++i) SetWindowTextW(colorEdits[i],values[i]);
        command(3);command(6);
        const auto loaded=triggerlab::loadColorProfile(path);
        if(!loaded.ok || loaded.color.target.red!=231 || loaded.color.target.blue!=19 || loaded.color.channelTolerance!=12) return false;
        SetWindowTextW(colorEdits[0],L"999");command(6);
        if(triggerlab::loadColorProfile(path).color.target.red!=231) return false;
        SendMessageW(autoControl,BM_SETCHECK,BST_CHECKED,0);command(8);
        if(autoClick || SendMessageW(autoControl,BM_GETCHECK,0,0)!=BST_UNCHECKED) return false;
        command(1);if(!running) return false;command(2);
        // Exercise one-shot sampling with a known pixel, independently of Start
        // and of the desktop content / user's cursor. No OS input is sent.
        triggerlab::CaptureSample pixel;
        pixel.status=triggerlab::CaptureStatus::Ok;pixel.frame.emplace(1,1);
        pixel.region={0,0,1,1};pixel.cursorPosition=triggerlab::CapturePoint{0,0};
        pixel.cursorPixel=triggerlab::Pixel{10,20,30,255};pixel.frame->at(0,0)=*pixel.cursorPixel;
        color.space=triggerlab::ColorSpace::HSV;
        acceptSample(std::move(pixel),true);
        if(running || autoClick || color.target.red!=30 || color.target.blue!=10 || color.space!=triggerlab::ColorSpace::RGB) return false;
        wchar_t field[32]{};GetWindowTextW(colorEdits[0],field,32);
        if(std::wstring(field)!=L"30") return false;
        // Failed sampling clears the preview and preserves the previous color.
        acceptSample({},true);
        if(color.target.red!=30 || sample.frame || sample.cursorPixel || !mask.pixels.empty()) return false;
        // Deadlines are injected to test cancellation even if another process owns F9.
        pickDeadline=GetTickCount64()+3000;
        selectionTick(pickDeadline-1001);
        GetWindowTextW(GetDlgItem(window,5),field,32);
        if(std::wstring(field)!=L"Пипетка: 2 сек.") return false;
        command(6); // Saving cancels delayed overwrite.
        if(pickDeadline || targetDeadline || !triggerlab::loadColorProfile(path).ok) return false;
        pickDeadline=GetTickCount64()+3000;targetDeadline=pickDeadline;command(2);
        if(pickDeadline || targetDeadline) return false;
        // The real button can start while stopped, or fail cleanly if F9 is busy.
        command(5);
        if(running || autoClick || targetDeadline) return false;
        if(!pickDeadline && status!=L"F9 занят: выбор не начат") return false;
        command(2);
        // Decode a real file through the same import action used by the dialog.
        const auto imagePath=path.parent_path()/L"цвет.bmp";
        BITMAPFILEHEADER fileHeader{};fileHeader.bfType=0x4D42;fileHeader.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);fileHeader.bfSize=fileHeader.bfOffBits+12;
        BITMAPINFOHEADER imageHeader{};imageHeader.biSize=sizeof(imageHeader);imageHeader.biWidth=3;imageHeader.biHeight=1;imageHeader.biPlanes=1;imageHeader.biBitCount=24;imageHeader.biSizeImage=12;
        const unsigned char imagePixels[]={19,67,231,19,67,231,255,0,0,0,0,0};
        {std::ofstream out(imagePath,std::ios::binary);out.write(reinterpret_cast<const char*>(&fileHeader),sizeof(fileHeader));out.write(reinterpret_cast<const char*>(&imageHeader),sizeof(imageHeader));out.write(reinterpret_cast<const char*>(imagePixels),sizeof(imagePixels));if(!out) return false;}
        pickDeadline=GetTickCount64()+3000;
        if(!importImage(imagePath) || pickDeadline || running || autoClick || color.target.red!=231 || color.target.blue!=19) return false;
        command(6);if(triggerlab::loadColorProfile(path).color.target.red!=231) return false;
        if(importImage(path) || color.target.red!=231 || autoClick) return false; // JSON is not an image.
        HWND test=CreateWindowW(L"STATIC",L"Test window title",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!test) return false;
        targetWindow=test;GetWindowThreadProcessId(test,&targetProcess);refreshTargetButton();
        const auto previousColor=color.target;
        command(20);
        if(!aimPickDeadline || autoClick) {DestroyWindow(test);return false;}
        cancelSelection();
        if(aimPickDeadline || color.target.red!=previousColor.red || color.target.green!=previousColor.green || color.target.blue!=previousColor.blue) {DestroyWindow(test);return false;}
        settings.center=false;command(20);
        if(aimPickDeadline) {DestroyWindow(test);return false;}
        settings.center=true;
        wchar_t caption[256]{};GetWindowTextW(GetDlgItem(window,7),caption,256);
        bool labels=std::wstring(caption).find(L"Test window title")!=std::wstring::npos;
        SetWindowTextW(test,L"Renamed target");refreshTargetButton();GetWindowTextW(GetDlgItem(window,7),caption,256);
        labels=labels && std::wstring(caption).find(L"Renamed target")!=std::wstring::npos;
        DestroyWindow(test);refreshTargetButton();GetWindowTextW(GetDlgItem(window,7),caption,256);
        labels=labels && !targetWindow && std::wstring(caption).find(L"не выбрано")!=std::wstring::npos;
        command(20); // A closed target cannot start a delayed snapshot.
        return labels && !running && !autoClick && !sample.frame && !pickDeadline && !targetDeadline && !aimPickDeadline;
    }
    static LRESULT CALLBACK checkProcedure(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR context) {
        if(message==WM_PAINT) {
            auto& app=*reinterpret_cast<Preview*>(context);PAINTSTRUCT paint{};HDC dc=BeginPaint(control,&paint);app.drawCheck(control,dc);EndPaint(control,&paint);return 0;
        }
        if(message==WM_NCDESTROY) RemoveWindowSubclass(control,checkProcedure,id);
        return DefSubclassProc(control,message,wp,lp);
    }
    void drawCheck(HWND control,HDC dc) {
        const auto p=themeColors(settings.darkTheme);RECT bounds{};GetClientRect(control,&bounds);
        SetDCBrushColor(dc,p.card);FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const bool checked=SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED;
        const bool enabled=IsWindowEnabled(control)!=FALSE;
        const int top=(bounds.bottom-18)/2;
        auto oldBrush=SelectObject(dc,GetStockObject(DC_BRUSH)),oldPen=SelectObject(dc,GetStockObject(DC_PEN));
        SetDCBrushColor(dc,checked?RGB(37,99,235):p.input);SetDCPenColor(dc,checked?RGB(37,99,235):p.border);RoundRect(dc,0,top,18,top+18,5,5);
        if(checked) {HPEN pen=CreatePen(PS_SOLID,2,RGB(255,255,255));auto previous=SelectObject(dc,pen);MoveToEx(dc,4,top+9,nullptr);LineTo(dc,8,top+13);LineTo(dc,14,top+5);SelectObject(dc,previous);DeleteObject(pen);}
        SelectObject(dc,oldBrush);SelectObject(dc,oldPen);
        wchar_t text[256]{};GetWindowTextW(control,text,256);auto oldFont=SelectObject(dc,uiFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,enabled?p.text:p.muted);
        RECT label{26,0,bounds.right,bounds.bottom};DrawTextW(dc,text,-1,&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(GetFocus()==control) {InflateRect(&label,-2,-4);DrawFocusRect(dc,&label);}SelectObject(dc,oldFont);
    }
    void drawButton(const DRAWITEMSTRUCT& item) {
        const auto p=themeColors(settings.darkTheme);
        const bool primary=item.CtlID==1 || item.CtlID==20;
        const bool stop=item.CtlID==2,disabled=(item.itemState&ODS_DISABLED)!=0;
        const bool pressed=(item.itemState&ODS_SELECTED)!=0;
        const COLORREF fill=disabled?p.input:primary?(pressed?RGB(29,78,216):RGB(37,99,235)):stop?p.stopFill:pressed?p.pressed:p.button;
        SetDCBrushColor(item.hDC,fill);SetDCPenColor(item.hDC,primary && !disabled?fill:p.border);
        auto oldBrush=SelectObject(item.hDC,GetStockObject(DC_BRUSH));auto oldPen=SelectObject(item.hDC,GetStockObject(DC_PEN));
        RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,10,10);
        wchar_t caption[512]{};GetWindowTextW(item.hwndItem,caption,512);SetBkMode(item.hDC,TRANSPARENT);
        SetTextColor(item.hDC,disabled?p.muted:primary?RGB(255,255,255):stop?p.stopText:p.text);
        auto oldFont=SelectObject(item.hDC,uiFont);RECT text=item.rcItem;InflateRect(&text,-8,-4);
        if(item.CtlID==21) {
            const int left=item.rcItem.right-54,top=(item.rcItem.top+item.rcItem.bottom)/2-11;
            SetDCBrushColor(item.hDC,settings.darkTheme?RGB(37,99,235):p.border);SetDCPenColor(item.hDC,settings.darkTheme?RGB(37,99,235):p.border);
            RoundRect(item.hDC,left,top,left+44,top+22,22,22);
            SetDCBrushColor(item.hDC,RGB(255,255,255));SetDCPenColor(item.hDC,RGB(255,255,255));
            const int knob=left+(settings.darkTheme?24:3);Ellipse(item.hDC,knob,top+3,knob+16,top+19);
            text.right=left-4;DrawTextW(item.hDC,L"Серая тема",-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        } else DrawTextW(item.hDC,caption,-1,&text,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
        if(item.itemState&ODS_FOCUS) {RECT focus=item.rcItem;InflateRect(&focus,-4,-4);DrawFocusRect(item.hDC,&focus);}
        SelectObject(item.hDC,oldFont);SelectObject(item.hDC,oldBrush);SelectObject(item.hDC,oldPen);
    }
    void paint(HDC target,RECT rect) {
        const auto p=themeColors(settings.darkTheme);
        const int w=rect.right,h=rect.bottom;if(w<=0 || h<=0) return;
        if(!backBuffer || bufferWidth!=w || bufferHeight!=h) {backBuffer=std::make_unique<PaintSurface>(target,w,h);bufferWidth=w;bufferHeight=h;}
        if(!backBuffer->valid()) return;HDC dc=backBuffer->dc;
        SetDCBrushColor(dc,p.background);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));SetBkMode(dc,TRANSPARENT);
        auto oldFont=SelectObject(dc,uiFont);
        auto line=[&](int x,int y,const std::wstring& text,COLORREF color=CLR_INVALID,HFONT font=nullptr) {
            SelectObject(dc,font?font:uiFont);SetTextColor(dc,color==CLR_INVALID?p.muted:color);TextOutW(dc,x,y,text.c_str(),static_cast<int>(text.size()));
        };
        auto wrapped=[&](RECT area,const std::wstring& text,COLORREF color) {SelectObject(dc,uiFont);SetTextColor(dc,color);DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&area,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);};
        auto card=[&](int left,int top,int right,int bottom) {
            auto b=SelectObject(dc,GetStockObject(DC_BRUSH));auto oldPen=SelectObject(dc,GetStockObject(DC_PEN));
            SetDCBrushColor(dc,p.card);SetDCPenColor(dc,p.border);RoundRect(dc,left,top,right,bottom,16,16);SelectObject(dc,b);SelectObject(dc,oldPen);
        };
        line(24,14,L"TriggerLab",p.text,titleFont);
        line(210,26,L"Окно → цвет мишени → проверка → триггер");
        card(20,84,488,242);card(508,84,1082,242);card(20,260,1082,406);card(20,424,1082,584);
        line(40,100,L"1. Окно и режим обнаружения",p.text,headingFont);
        line(548,100,L"3. Проверка и запуск",p.text,headingFont);
        line(40,276,L"Область и время срабатывания",p.text,headingFont);
        line(40,436,L"2. Цвет мишени",p.text,headingFont);
        line(800,442,L"Текущий цвет");
        HBRUSH swatch=CreateSolidBrush(RGB(color.target.red,color.target.green,color.target.blue));RECT patch{800,482,1062,512};FillRect(dc,&patch,swatch);DeleteObject(swatch);
        card(20,602,360,882);card(380,602,720,882);card(740,602,1082,882);
        line(40,620,L"Область прицела",p.text,headingFont);line(400,620,L"Совпадения цвета",p.text,headingFont);
        line(760,620,L"Состояние",p.text,headingFont);
        RECT image{40,652,340,852},maskRect{400,652,700,852};SetDCBrushColor(dc,p.input);FillRect(dc,&image,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));FillRect(dc,&maskRect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if(sample.frame) {
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=sample.frame->width();info.bmiHeader.biHeight=-sample.frame->height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            const int side=200;const int x=90,mx=450;
            StretchDIBits(dc,x,652,side,side,0,0,sample.frame->width(),sample.frame->height(),sample.frame->data(),&info,DIB_RGB_COLORS,SRCCOPY);
            triggerlab::Frame maskFrame(sample.frame->width(),sample.frame->height());
            for(std::size_t i=0;i<mask.pixels.size();++i) {auto value=mask.pixels[i];maskFrame.data()[i]={value,value,value,255};}
            StretchDIBits(dc,mx,652,side,side,0,0,maskFrame.width(),maskFrame.height(),maskFrame.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        } else {wrapped({65,720,320,780},L"Начните захват и активируйте выбранное окно",p.muted);wrapped({420,720,685,780},L"Белым показан найденный цвет",p.muted);}
        RECT badge{760,651,1062,680};SetDCBrushColor(dc,p.input);FillRect(dc,&badge,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        line(770,656,settings.dryRun?L"ПРОВЕРКА · без кликов":L"РЕАЛЬНЫЕ КЛИКИ",settings.dryRun?p.accent:settings.darkTheme?RGB(255,204,143):RGB(180,83,9));
        wrapped({760,693,1062,734},running?status:L"Захват остановлен",running?p.accent:p.muted);
        line(760,742,autoClick?L"Триггер включён":L"Триггер выключен",autoClick?(settings.darkTheme?RGB(134,239,172):RGB(22,163,74)):p.muted);
        wrapped({760,771,1062,811},clickStatus,p.text);
        line(760,817,L"Клики: "+std::to_wstring(clickCount)+L"   Проверки: "+std::to_wstring(diagnosticCount));
        line(760,845,L"Найдено: "+std::to_wstring(static_cast<int>(mask.coverage*100))+L"%  /  порог "+std::to_wstring(settings.minimumPercent)+L"%");
        RECT track{760,871,1062,875};SetDCBrushColor(dc,p.border);FillRect(dc,&track,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        track.right=track.left+static_cast<LONG>(302*std::clamp(mask.coverage,0.0,1.0));SetDCBrushColor(dc,p.accent);FillRect(dc,&track,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        line(40,858,settings.center?L"Снимок вокруг центра окна":L"Снимок под курсором");
        line(400,858,settings.center?L"Перекрестие исключено":L"Проверка пикселя курсора");
        wrapped({24,894,825,932},notice,p.muted);
        line(860,894,L"F9 / Esc — остановить");
        SelectObject(dc,oldFont);BitBlt(target,0,0,w,h,dc,0,0,SRCCOPY);
    }
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<Preview*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {auto* create=reinterpret_cast<CREATESTRUCTW*>(lp); app=static_cast<Preview*>(create->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));return TRUE;}
    try {
        switch(message) {
            case WM_CREATE:app->createControls();return 0;
            case WM_COMMAND:app->command(LOWORD(wp),HIWORD(wp));return 0;
            case WM_TIMER:
                if(wp==4) {if(triggerlab::Clock::now()>=app->releaseAt) app->finishClick();return 0;}
                if(wp==3) {app->refreshTargetButton();app->syncActions();return 0;}
                if(wp==2) app->selectionTick(GetTickCount64());
                else if(wp==1) app->update();return 0;
            case WM_HOTKEY:if(wp==9) app->stop();return 0;
            case WM_KEYDOWN:
                if(wp==VK_ESCAPE) app->stop();
                else if(wp=='C') app->update(true);
                else if(wp=='H' && !(lp & (1LL<<30))) {app->cancelSelection();app->disableClicks();app->color.space=app->color.space==triggerlab::ColorSpace::RGB?triggerlab::ColorSpace::HSV:triggerlab::ColorSpace::RGB;app->update();InvalidateRect(window,nullptr,FALSE);}return 0;
            case WM_DRAWITEM:if(app && wp) {app->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}break;
            case WM_GETMINMAXINFO: {auto* size=reinterpret_cast<MINMAXINFO*>(lp);size->ptMinTrackSize={1120,980};return 0;}
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN: {
                const auto p=themeColors(app->settings.darkTheme);HDC dc=reinterpret_cast<HDC>(wp);
                const bool input=message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX || reinterpret_cast<HWND>(lp)==app->modeControl;
                SetTextColor(dc,p.text);SetBkColor(dc,input?p.input:p.card);
                return reinterpret_cast<LRESULT>(input?app->inputBrush:app->cardBrush);
            }
            case WM_ERASEBKGND:return 1;
            case WM_PAINT: {PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);try {app->paint(dc,rect);} catch(...) {EndPaint(window,&paint);throw;} EndPaint(window,&paint);return 0;}
            case WM_DESTROY:KillTimer(window,3);app->stop();PostQuitMessage(0);return 0;
        }
    } catch(const std::exception&) {app->stop();}
    return DefWindowProcW(window,message,wp,lp);
}
}
namespace {
struct ClickTestTarget {
    COLORREF color=RGB(231,67,19);
    int downs=0,ups=0;
    bool crosshair=false;
    std::vector<triggerlab::Clock::time_point> clicks;
    std::vector<triggerlab::Clock::time_point> releases;
};
LRESULT CALLBACK clickTestProcedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCCREATE) SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* state=reinterpret_cast<ClickTestTarget*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(state && message==WM_PAINT) {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rect{};GetClientRect(hwnd,&rect);
        SetDCBrushColor(dc,state->color);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if(state->crosshair) {RECT cross{rect.right/2-3,rect.bottom/2-3,rect.right/2+4,rect.bottom/2+4};FillRect(dc,&cross,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));}
        EndPaint(hwnd,&ps);return 0;
    }
    if(state && message==WM_LBUTTONDOWN) {++state->downs;state->clicks.push_back(triggerlab::Clock::now());return 0;}
    if(state && message==WM_LBUTTONUP) {++state->ups;state->releases.push_back(triggerlab::Clock::now());return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
int liveClickTest(Preview& app,bool centerMode=false) {
    using namespace std::chrono_literals;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::ofstream report(std::filesystem::path(executable).parent_path()/(centerMode?L"center-test-report.txt":L"click-test-report.txt"));
    POINT cursor{};
    if(!GetCursorPos(&cursor) || Preview::inputHeld()) {report<<"SKIP: cursor unavailable or physical input held\n";return 77;}
    HWND previous=GetForegroundWindow();ClickTestTarget state;state.crosshair=centerMode;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=clickTestProcedure;wc.lpszClassName=L"TriggerLabHoverInputTest";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 2;
    // Center mode deliberately samples away from the physical pointer. A
    // computer-use pointer highlight may cover the cursor but not this ROI.
    HWND target=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"TriggerLab click test",WS_POPUP|WS_VISIBLE,cursor.x-(centerMode?30:60),cursor.y-(centerMode?30:60),centerMode?240:120,centerMode?240:120,nullptr,nullptr,wc.hInstance,&state);
    if(!target) {report<<"SKIP: cannot create test window\n";return 77;}
    app.targetWindow=target;GetWindowThreadProcessId(target,&app.targetProcess);
    UpdateWindow(target);
    // The shell/CI may own foreground permission. Temporarily attach only for
    // activating this test window, then detach before any capture or input.
    const DWORD foregroundThread=GetWindowThreadProcessId(previous,nullptr),thread=GetCurrentThreadId();
    const bool attached=foregroundThread && foregroundThread!=thread && AttachThreadInput(thread,foregroundThread,TRUE);
    SetForegroundWindow(target);SetActiveWindow(target);SetFocus(target);
    if(attached) AttachThreadInput(thread,foregroundThread,FALSE);
    auto pump=[] {
        MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    };
    pump();
    int result=0;
    if(!app.permittedAt(cursor)) {report<<"SKIP: test window cannot receive focus\n";result=77;}
    else {
        app.settings={};app.settings.center=centerMode;app.settings.dryRun=true;
        app.running=true;app.color.target={19,67,231,255};app.color.channelTolerance=0;app.color.space=triggerlab::ColorSpace::RGB;
        app.gate.setTiming(70ms,50ms);
        GdiFlush();DwmFlush();Sleep(30);pump();app.update();
        const bool reference=centerMode?app.mask.found && app.mask.coverage==1:
            app.sample.cursorPixel && triggerlab::matchesColor(*app.sample.cursorPixel,app.color);
        if(!reference) app.running=false;
        SendMessageW(app.autoControl,BM_SETCHECK,BST_CHECKED,0);app.command(8);
        if(!reference) {report<<"SKIP: test reference pixels unavailable or covered by a desktop/pointer overlay\n";result=77;}
        else if(!app.autoClick) {report<<"SKIP: F9 unavailable\n";result=77;}
        else {
            auto observe=[&](std::chrono::milliseconds duration) {
                const auto until=triggerlab::Clock::now()+duration;
                while(triggerlab::Clock::now()<until) {
                    // Never deliver input to any window except this owned test target.
                    POINT point{};if(!GetCursorPos(&point) || !app.permittedAt(point)) return false;
                    app.update();pump();Sleep(10);
                }
                return true;
            };
            const auto before=app.diagnosticCount;
            const bool dryStable=observe(250ms);
            const bool dryPassed=dryStable && app.diagnosticCount==before+1 && state.downs==0 && state.ups==0;
            report<<"dry_run="<<dryPassed<<" center_mode="<<centerMode<<"\n";
            const auto p=app.sample.cursorPixel?*app.sample.cursorPixel:app.sample.frame?app.sample.frame->at(app.sample.frame->width()/2,app.sample.frame->height()/2):triggerlab::Pixel{};
            report<<"dry rgb="<<int(p.red)<<","<<int(p.green)<<","<<int(p.blue)<<" mask="<<app.mask.coverage<<" held="<<Preview::inputHeld()<<" diagnostics="<<app.diagnosticCount<<" foreground="<<(GetForegroundWindow()==target)<<"\n";
            const int bytes=WideCharToMultiByte(CP_UTF8,0,app.clickStatus.data(),static_cast<int>(app.clickStatus.size()),nullptr,0,nullptr,nullptr);
            std::string reason(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,app.clickStatus.data(),static_cast<int>(app.clickStatus.size()),reason.data(),bytes,nullptr,nullptr);report<<reason<<"\n";
            app.settings.dryRun=false;app.gate.reset();
            const auto start=triggerlab::Clock::now();
            bool stable=observe(300ms);
            const auto first=state.clicks.empty()?0:std::chrono::duration_cast<std::chrono::milliseconds>(state.clicks.front()-start).count();
            bool passed=dryPassed && stable && state.downs==1 && state.ups==1 && first>=70;
            report<<"first hover downs="<<state.downs<<" ups="<<state.ups<<" delay_ms="<<first<<"\n";
            state.color=RGB(50,50,50);InvalidateRect(target,nullptr,FALSE);UpdateWindow(target);
            stable=observe(100ms) && stable;
            passed=passed && state.downs==1;
            state.color=RGB(231,67,19);InvalidateRect(target,nullptr,FALSE);UpdateWindow(target);
            const auto secondStart=triggerlab::Clock::now();stable=observe(300ms) && stable;
            const auto second=state.clicks.size()<2?0:std::chrono::duration_cast<std::chrono::milliseconds>(state.clicks[1]-secondStart).count();
            passed=passed && state.downs==2 && state.ups==2 && second>=70;
            if(state.releases.size()==2 && state.clicks.size()==2) {
                const auto held=std::chrono::duration_cast<std::chrono::milliseconds>(state.releases[0]-state.clicks[0]).count();
                report<<"press_duration_ms="<<held<<"\n";passed=passed && held>=app.settings.clickMs;
            }
            report<<"reacquire downs="<<state.downs<<" ups="<<state.ups<<" delay_ms="<<second<<"\n";
            app.gate.reset();const auto holdDeadline=triggerlab::Clock::now()+500ms;
            while(!app.syntheticDown && triggerlab::Clock::now()<holdDeadline) {app.update();pump();Sleep(5);}
            const bool interrupted=app.syntheticDown;app.disableClicks();pump();
            report<<"stop_released_pending_press="<<interrupted<<" downs="<<state.downs<<" ups="<<state.ups<<"\n";
            stable=observe(100ms) && stable;passed=passed && interrupted && !app.syntheticDown && state.downs==3 && state.ups==3;
            report<<(passed?"PASS: actual SendInput received once per hover, dwell >=70ms, stop blocks input\n":stable?"FAIL: real hover input\n":"SKIP: user cursor or foreground changed during test\n");
            if(!passed) {
                report<<"capture_status="<<static_cast<int>(app.sample.status)<<" matching="<<(app.sample.cursorPixel && triggerlab::matchesColor(*app.sample.cursorPixel,app.color))<<"\n";
                result=stable?2:77;
            }
        }
    }
    app.stop();app.targetWindow=nullptr;DestroyWindow(target);if(previous && IsWindow(previous)) SetForegroundWindow(previous);
    return result;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR commandLine,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        Preview app;WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"TriggerLabCapturePreviewClass";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc)) return 1;
        HWND window=CreateWindowW(wc.lpszClassName,L"TriggerLab — тренировки Kovaak’s / Aimlabs",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1120,980,nullptr,nullptr,instance,&app);
        if(!window) return 1;
        if(std::wstring(commandLine)==L"--click-test") {const int result=liveClickTest(app);DestroyWindow(window);return result;}
        if(std::wstring(commandLine)==L"--center-click-test") {const int result=liveClickTest(app,true);DestroyWindow(window);return result;}
        if(std::wstring(commandLine)==L"--smoke-test") {
            const auto path=std::filesystem::temp_directory_path()/(L"triggerlab-preview-test-"+std::to_wstring(GetCurrentProcessId()))/L"color.json";
            const bool passed=app.smokeTest(path);DestroyWindow(window);std::filesystem::remove_all(path.parent_path());return passed?0:2;
        }
        SetWindowTextW(window,L"TriggerLab — тренировки Kovaak’s / Aimlabs");
        ShowWindow(window,show);MSG message{};BOOL status;
        while((status=GetMessageW(&message,nullptr,0,0))>0) {if(GetFocus()!=window && IsDialogMessageW(window,&message)) continue;TranslateMessage(&message);DispatchMessageW(&message);}return status<0?1:0;
    } catch(const std::exception&) {return 1;}
}
