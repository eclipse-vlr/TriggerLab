#include <windows.h>
#include <commdlg.h>
#include "triggerlab/ColorProfile.hpp"
#include "triggerlab/HoverClickGate.hpp"
#include "triggerlab/ClickPermission.hpp"
#include "triggerlab/ImageFile.hpp"
#include "triggerlab/ImagePaletteDialog.hpp"
#include <fstream>
#include <charconv>
#include <array>
#include "triggerlab/ScreenCapture.hpp"
#include "triggerlab/ColorMask.hpp"
#include <string>
#include <optional>
namespace {
struct BasicColor {const wchar_t* name;triggerlab::Pixel pixel;};
constexpr std::array<BasicColor,3> basicColors{{
    {L"Красный",{0,0,255,255}},
    {L"Фиолетовый",{255,0,255,255}},
    {L"Жёлтый",{0,255,255,255}}
}};
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
    HWND window=nullptr,targetWindow=nullptr,autoControl=nullptr; bool running=false,autoClick=false;
    DWORD targetProcess=0;std::wstring targetTitle;
    std::array<HWND,4> colorEdits{};
    triggerlab::HoverClickGate gate;
    std::uint64_t clickCount=0;
    std::wstring clickStatus=L"Выключен";
    ULONGLONG pickDeadline=0,targetDeadline=0;
    std::filesystem::path colorPath;
    std::optional<triggerlab::Frame> importedImage;
    Preview() {
        wchar_t local[32768]{};auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
        colorPath=length>0 && length<32768?std::filesystem::path(local)/L"TriggerLab"/L"selected-color.json":std::filesystem::path(L"selected-color.json");
        const auto loaded=triggerlab::loadColorProfile(colorPath);if(loaded.ok) color=loaded.color;
    }
    void syncColor() {
        const int values[]={color.target.red,color.target.green,color.target.blue,color.channelTolerance};
        for(int i=0;i<4;++i) SetWindowTextW(colorEdits[i],std::to_wstring(values[i]).c_str());
    }
    void disableClicks() {UnregisterHotKey(window,9);autoClick=false;gate.reset();clickStatus=L"Выключен";if(autoControl) SendMessageW(autoControl,BM_SETCHECK,BST_UNCHECKED,0);}
    void cancelSelection() {
        pickDeadline=targetDeadline=0;KillTimer(window,2);
        if(!autoClick) UnregisterHotKey(window,9);
        SetWindowTextW(GetDlgItem(window,5),L"Пипетка (3 сек.)");refreshTargetButton();
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
        for(std::size_t i=0;i<basicColors.size();++i) {const auto p=basicColors[i].pixel;custom[i]=RGB(p.red,p.green,p.blue);}
        picker.rgbResult=RGB(color.target.red,color.target.green,color.target.blue);picker.lpCustColors=custom;picker.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(ChooseColorW(&picker)) {color.target={GetBValue(picker.rgbResult),GetGValue(picker.rgbResult),GetRValue(picker.rgbResult),255};color.space=triggerlab::ColorSpace::RGB;syncColor();status=L"Цвет выбран. Нажмите «Сохранить цвет»";}
    }
    void saveColor() {if(!applyColor()) return;std::string error;status=triggerlab::saveColorProfile(colorPath,color,error)?L"Цвет сохранён / Color saved":L"Ошибка сохранения цвета / Save failed";}
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
        const bool match=sample.cursorPixel && triggerlab::matchesColor(*sample.cursorPixel,color);
        const auto now=triggerlab::Clock::now();
        if(!gate.step(sample.frame.has_value(),match,now)) {
            clickStatus=!match?L"Ожидаю цвет под курсором":gate.clickedHover()?L"Клик уже выполнен: уйдите с цвета и наведите снова":L"Совпадение: ожидание "+std::to_wstring(gate.elapsed(now).count())+L" / 70 мс";
            return;
        }
        POINT current{};
        if(!GetCursorPos(&current) || current.x!=capturedCursor.x || current.y!=capturedCursor.y || !permittedAt(current) || inputHeld()) {
            gate.step(false,false,triggerlab::Clock::now());clickStatus=L"Ожидание прервано: курсор, окно или клавиши изменились";return;
        }
        INPUT inputs[2]{};inputs[0].type=inputs[1].type=INPUT_MOUSE;inputs[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;inputs[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
        const UINT sent=SendInput(2,inputs,sizeof(INPUT));
        if(sent==2) {gate.acknowledgeClick(triggerlab::Clock::now());++clickCount;clickStatus=L"Клик отправлен";}
        else {const auto error=GetLastError();if(sent==1) {INPUT release{};release.type=INPUT_MOUSE;release.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&release,sizeof(INPUT));}disableClicks();status=L"Ввод отклонён системой (код "+std::to_wstring(error)+L")";clickStatus=status;notice=status;}
    }
    static bool inputHeld() {
        for(int key:{VK_CONTROL,VK_MENU,VK_SHIFT,VK_LWIN,VK_RWIN,VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2})
            if(GetAsyncKeyState(key)&0x8000) return true;
        return false;
    }
    void createControls() {
        CreateWindowW(L"BUTTON",L"Start / Начать",WS_CHILD|WS_VISIBLE|WS_TABSTOP,15,15,150,30,window,reinterpret_cast<HMENU>(1),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Stop / Остановить",WS_CHILD|WS_VISIBLE|WS_TABSTOP,180,15,170,30,window,reinterpret_cast<HMENU>(2),nullptr,nullptr);
        for(int i=0;i<3;++i) CreateWindowW(L"BUTTON",basicColors[i].name,WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            365+i*105,15,100,30,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(11+i)),nullptr,nullptr);
        const wchar_t* labels[]={L"R",L"G",L"B",L"Допуск"};
        for(int i=0;i<4;++i) {CreateWindowW(L"STATIC",labels[i],WS_CHILD|WS_VISIBLE,15+i*110,60,50,20,window,nullptr,nullptr,nullptr);colorEdits[i]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER,(i==3?410:45+i*110),57,60,24,window,nullptr,nullptr,nullptr);}
        CreateWindowW(L"BUTTON",L"Применить RGB",WS_CHILD|WS_VISIBLE|WS_TABSTOP,480,55,160,28,window,reinterpret_cast<HMENU>(3),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Выбрать цвет…",WS_CHILD|WS_VISIBLE|WS_TABSTOP,15,95,150,30,window,reinterpret_cast<HMENU>(4),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Пипетка (3 сек.)",WS_CHILD|WS_VISIBLE|WS_TABSTOP,180,95,170,30,window,reinterpret_cast<HMENU>(5),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Сохранить цвет",WS_CHILD|WS_VISIBLE|WS_TABSTOP,365,95,175,30,window,reinterpret_cast<HMENU>(6),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Из файла…",WS_CHILD|WS_VISIBLE|WS_TABSTOP,550,95,120,30,window,reinterpret_cast<HMENU>(10),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Палитра / область",WS_CHILD|WS_VISIBLE|WS_TABSTOP,480,236,185,28,window,reinterpret_cast<HMENU>(14),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Окно не выбрано\nВыбрать (3 сек.)",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_MULTILINE,15,135,240,40,window,reinterpret_cast<HMENU>(7),nullptr,nullptr);
        autoControl=CreateWindowW(L"BUTTON",L"Автоклик в выбранном окне",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,270,135,350,30,window,reinterpret_cast<HMENU>(8),nullptr,nullptr);
        syncColor();refreshTargetButton();SetTimer(window,3,500,nullptr);
    }
    void command(int id) {
        if(id==1) {if(SetTimer(window,1,10,nullptr)) {running=true;status=L"Active / Захват активен";}else status=L"Timer failed / Ошибка таймера";}
        else if(id==2) stop();
        else if(id==3) {if(applyColor()) status=L"RGB применён; автоклик выключен";}
        else if(id==4) chooseColor();
        else if(id==5) beginSelection(true);
        else if(id==6) saveColor();
        else if(id==7) beginSelection(false);
        else if(id==14) editImage();
        else if(id>=11 && id<=13) {
            cancelSelection();disableClicks();color.target=basicColors[id-11].pixel;color.space=triggerlab::ColorSpace::RGB;syncColor();
            mask=sample.frame?triggerlab::makeColorMask(*sample.frame,color):triggerlab::ColorMask{};
            status=std::wstring(basicColors[id-11].name)+L": основной цвет выбран. Нажмите «Сохранить цвет»";
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
        notice=status;InvalidateRect(window,nullptr,FALSE);
    }
    std::wstring notice;
    std::wstring status=L"Stopped / Остановлено";
    void stop() {running=false;disableClicks();cancelSelection();KillTimer(window,1);capture.reset();sample={};mask={};status=L"Stopped / Остановлено";notice=status;InvalidateRect(window,nullptr,FALSE);}
    void update(bool pick=false) {
        if(!running && !pick) return;
        if(pick) {cancelSelection();disableClicks();}
        acceptSample(capture.captureCursor(),pick);
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
            mask=triggerlab::makeColorMask(*sample.frame,color);
            const bool cursorMatch=sample.cursorPixel && triggerlab::matchesColor(*sample.cursorPixel,color);
            status=sample.allBlack?L"Чёрный ROI: проверьте, доступно ли содержимое окна":(cursorMatch?L"Под курсором: СОВПАДЕНИЕ":L"Под курсором: нет совпадения");
        } else status=sample.status==triggerlab::CaptureStatus::CursorMoved?L"Курсор движется: удерживайте его на нужном цвете":L"Захват недоступен (статус "+std::to_wstring(static_cast<int>(sample.status))+L")";
        if(autoClick && sample.cursorPosition) handleClick({sample.cursorPosition->x,sample.cursorPosition->y});
        else if(autoClick) {gate.step(false,false,triggerlab::Clock::now());clickStatus=ownPixel?L"Курсор над анализатором":L"Нет свежего пикселя: "+status;}
        if(pick) {notice=sample.cursorPixel?L"Цвет взят с экрана. Нажмите «Сохранить цвет»":L"Пипетка: свежий пиксель недоступен, цвет не изменён";}
        InvalidateRect(window,nullptr,FALSE);
    }
    bool smokeTest(const std::filesystem::path& path) {
        if(!triggerlab::imagePaletteDialogSmoke()) return false;
        colorPath=path;
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
        for(int i=0;i<3;++i) {
            pickDeadline=GetTickCount64()+3000;command(11+i);
            const auto expected=basicColors[i].pixel;
            if(color.target.red!=expected.red || color.target.green!=expected.green || color.target.blue!=expected.blue ||
               color.space!=triggerlab::ColorSpace::RGB || pickDeadline || autoClick) return false;
            command(6);const auto saved=triggerlab::loadColorProfile(path);
            if(!saved.ok || saved.color.target.red!=expected.red || saved.color.target.green!=expected.green || saved.color.target.blue!=expected.blue) return false;
        }
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
        wchar_t caption[256]{};GetWindowTextW(GetDlgItem(window,7),caption,256);
        bool labels=std::wstring(caption).find(L"Test window title")!=std::wstring::npos;
        SetWindowTextW(test,L"Renamed target");refreshTargetButton();GetWindowTextW(GetDlgItem(window,7),caption,256);
        labels=labels && std::wstring(caption).find(L"Renamed target")!=std::wstring::npos;
        DestroyWindow(test);refreshTargetButton();GetWindowTextW(GetDlgItem(window,7),caption,256);
        labels=labels && !targetWindow && std::wstring(caption).find(L"не выбрано")!=std::wstring::npos;
        return labels && !running && !autoClick && !sample.frame && !pickDeadline && !targetDeadline;
    }
    void paint(HDC target,RECT rect) {
        const int w=rect.right,h=rect.bottom;if(w<=0 || h<=0) return;
        PaintSurface surface(target,w,h);if(!surface.valid()) return;HDC dc=surface.dc;
        FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(235,235,235));
        auto text=[&](int y,const std::wstring& line) {TextOutW(dc,15,y,line.c_str(),static_cast<int>(line.size()));};
        text(182,status);text(206,L"C: взять цвет сейчас; H: RGB/HSV; ESC / F9: остановить");
        text(230,L"RGB: "+std::to_wstring(color.target.red)+L", "+std::to_wstring(color.target.green)+L", "+std::to_wstring(color.target.blue)+L" | "+(color.space==triggerlab::ColorSpace::RGB?L"RGB":L"HSV")+L" | coverage "+std::to_wstring(static_cast<int>(mask.coverage*100))+L"%");
        if(sample.frame) {
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=sample.frame->width();info.bmiHeader.biHeight=-sample.frame->height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            StretchDIBits(dc,15,270,210,210,0,0,sample.frame->width(),sample.frame->height(),sample.frame->data(),&info,DIB_RGB_COLORS,SRCCOPY);
            triggerlab::Frame maskFrame(sample.frame->width(),sample.frame->height());
            for(std::size_t i=0;i<mask.pixels.size();++i) {auto value=mask.pixels[i];maskFrame.data()[i]={value,value,value,255};}
            StretchDIBits(dc,245,270,210,210,0,0,maskFrame.width(),maskFrame.height(),maskFrame.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        }
        text(515,L"Окно: "+(targetWindow?targetTitle.substr(0,65):L"не выбрано"));
        text(490,clickStatus);
        text(539,L"Автоклик: "+std::wstring(autoClick?L"ВКЛ":L"ВЫКЛ")+L" | Отправлено кликов: "+std::to_wstring(clickCount));
        text(563,L"Наведение: 70 мс. Интервал: 50 мс. Один клик до ухода с цвета.");
        text(590,notice);
        HBRUSH swatch=CreateSolidBrush(RGB(color.target.red,color.target.green,color.target.blue));RECT patch{480,270,620,410};FillRect(dc,&patch,swatch);DeleteObject(swatch);
        BitBlt(target,0,0,w,h,dc,0,0,SRCCOPY);
    }
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<Preview*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {auto* create=reinterpret_cast<CREATESTRUCTW*>(lp); app=static_cast<Preview*>(create->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));return TRUE;}
    try {
        switch(message) {
            case WM_CREATE:app->createControls();return 0;
            case WM_COMMAND:app->command(LOWORD(wp));return 0;
            case WM_TIMER:
                if(wp==3) {app->refreshTargetButton();return 0;}
                if(wp==2) app->selectionTick(GetTickCount64());
                else if(wp==1) app->update();return 0;
            case WM_HOTKEY:if(wp==9) app->stop();return 0;
            case WM_KEYDOWN:
                if(wp==VK_ESCAPE) app->stop();
                else if(wp=='C') app->update(true);
                else if(wp=='H' && !(lp & (1LL<<30))) {app->cancelSelection();app->disableClicks();app->color.space=app->color.space==triggerlab::ColorSpace::RGB?triggerlab::ColorSpace::HSV:triggerlab::ColorSpace::RGB;app->update();InvalidateRect(window,nullptr,FALSE);}return 0;
            case WM_CTLCOLORSTATIC: {HDC dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,RGB(240,240,240));SetBkColor(dc,RGB(0,0,0));return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));}
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
    std::vector<triggerlab::Clock::time_point> clicks;
};
LRESULT CALLBACK clickTestProcedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCCREATE) SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* state=reinterpret_cast<ClickTestTarget*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(state && message==WM_PAINT) {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rect{};GetClientRect(hwnd,&rect);
        SetDCBrushColor(dc,state->color);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));EndPaint(hwnd,&ps);return 0;
    }
    if(state && message==WM_LBUTTONDOWN) {++state->downs;state->clicks.push_back(triggerlab::Clock::now());return 0;}
    if(state && message==WM_LBUTTONUP) {++state->ups;return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
int liveClickTest(Preview& app) {
    using namespace std::chrono_literals;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::ofstream report(std::filesystem::path(executable).parent_path()/L"click-test-report.txt");
    POINT cursor{};
    if(!GetCursorPos(&cursor) || Preview::inputHeld()) {report<<"SKIP: cursor unavailable or physical input held\n";return 77;}
    HWND previous=GetForegroundWindow();ClickTestTarget state;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=clickTestProcedure;wc.lpszClassName=L"TriggerLabHoverInputTest";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 2;
    HWND target=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"TriggerLab click test",WS_POPUP|WS_VISIBLE,cursor.x-60,cursor.y-60,120,120,nullptr,nullptr,wc.hInstance,&state);
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
        app.running=true;app.color.target={19,67,231,255};app.color.channelTolerance=0;app.color.space=triggerlab::ColorSpace::RGB;
        SendMessageW(app.autoControl,BM_SETCHECK,BST_CHECKED,0);app.command(8);
        if(!app.autoClick) {report<<"SKIP: F9 unavailable\n";result=77;}
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
            const auto start=triggerlab::Clock::now();
            bool stable=observe(300ms);
            const auto first=state.clicks.empty()?0:std::chrono::duration_cast<std::chrono::milliseconds>(state.clicks.front()-start).count();
            bool passed=stable && state.downs==1 && state.ups==1 && first>=70;
            report<<"first hover downs="<<state.downs<<" ups="<<state.ups<<" delay_ms="<<first<<"\n";
            state.color=RGB(50,50,50);InvalidateRect(target,nullptr,FALSE);UpdateWindow(target);
            stable=observe(100ms) && stable;
            passed=passed && state.downs==1;
            state.color=RGB(231,67,19);InvalidateRect(target,nullptr,FALSE);UpdateWindow(target);
            const auto secondStart=triggerlab::Clock::now();stable=observe(300ms) && stable;
            const auto second=state.clicks.size()<2?0:std::chrono::duration_cast<std::chrono::milliseconds>(state.clicks[1]-secondStart).count();
            passed=passed && state.downs==2 && state.ups==2 && second>=70;
            report<<"reacquire downs="<<state.downs<<" ups="<<state.ups<<" delay_ms="<<second<<"\n";
            app.disableClicks();stable=observe(100ms) && stable;passed=passed && state.downs==2;
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
        HWND window=CreateWindowW(wc.lpszClassName,L"TriggerLab — выбор цвета и автоклик",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,700,680,nullptr,nullptr,instance,&app);
        if(!window) return 1;
        if(std::wstring(commandLine)==L"--click-test") {const int result=liveClickTest(app);DestroyWindow(window);return result;}
        if(std::wstring(commandLine)==L"--smoke-test") {
            const auto path=std::filesystem::temp_directory_path()/(L"triggerlab-preview-test-"+std::to_wstring(GetCurrentProcessId()))/L"color.json";
            const bool passed=app.smokeTest(path);DestroyWindow(window);std::filesystem::remove_all(path.parent_path());return passed?0:2;
        }
        SetWindowTextW(window,L"TriggerLab — выбор цвета и автоклик");
        ShowWindow(window,show);MSG message{};BOOL status;
        while((status=GetMessageW(&message,nullptr,0,0))>0) {if(GetFocus()!=window && IsDialogMessageW(window,&message)) continue;TranslateMessage(&message);DispatchMessageW(&message);}return status<0?1:0;
    } catch(const std::exception&) {return 1;}
}
