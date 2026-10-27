#include <windows.h>
#include "triggerlab/InputAuthorization.hpp"
#include "StartupDiagnostics.hpp"
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
#include "triggerlab/TrainingProfile.hpp"
#include "ClickPulse.hpp"
#include "WindowPickerDialog.hpp"
#include <cwctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <charconv>
#include <array>
#include <atomic>
#include "triggerlab/ScreenCapture.hpp"
#include "triggerlab/ColorMask.hpp"
#include <string>
#include <optional>
namespace {
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
struct Preview {
    HFONT uiFont=nullptr,titleFont=nullptr,headingFont=nullptr,controlFont=nullptr;
    UINT dpi=96;int scrollX=0,scrollY=0,wheelRemainder=0;bool arranging=false,allowOversizedTestWindow=false;
    struct ControlRect {HWND control;RECT logical;RECT placed{};};
    bool narrow=false;int canvasWidth=1102,canvasHeight=950,headerHeight=144;
    HWND headerStatus=nullptr,headerBackdrop=nullptr,qualityInfo=nullptr,homeHint=nullptr,homeSummary=nullptr;
    bool homeDetails=false;
    enum class SnapshotPurpose {Color,Target,Background};SnapshotPurpose snapshotPurpose=SnapshotPurpose::Color;
    std::optional<triggerlab::Frame> targetReference,backgroundReference;HWND referenceWindow=nullptr;DWORD referenceProcess=0;
    RECT windowCard{},runCard{},optionsCard{},colorCard{},qualityCard{},stateCard{},previewCards[2]{};int footerY=0;
    std::vector<ControlRect> controlRects;
    HBRUSH cardBrush=CreateSolidBrush(RGB(255,255,255));
    HBRUSH inputBrush=CreateSolidBrush(RGB(248,250,252));
    HWND tooltip=nullptr;
    ~Preview() {if(controlFont) DeleteObject(controlFont);if(uiFont) DeleteObject(uiFont);if(titleFont) DeleteObject(titleFont);if(headingFont) DeleteObject(headingFont);if(cardBrush) DeleteObject(cardBrush);if(inputBrush) DeleteObject(inputBrush);}
    triggerlab::ScreenCapture capture;
    triggerlab::CaptureSample sample;
    triggerlab::ColorMask mask;
    std::optional<triggerlab::Frame> displayFrame;
    triggerlab::ColorMask displayMask;
    triggerlab::AnalyzerSettings displaySettings;
    ULONGLONG displayTime=0;
    bool displayFound=false;
    triggerlab::ColorMatchConfig color;
    triggerlab::AnalyzerSettings settings;
    std::filesystem::path settingsPath;
    std::array<HWND,8> settingEdits{};
    std::array<HWND,8> settingLabels{};
    std::array<HWND,4> colorLabels{};
    HWND settingsHint=nullptr,colorHint=nullptr,profileChoice=nullptr,profileName=nullptr;
    bool uiSync=false,uiReady=false,colorReady=false,verified=false,profilePane=false;
    bool paramsOnDisk=false,colorOnDisk=false;
    triggerlab::AnalyzerSettings savedSettings;
    triggerlab::ColorMatchConfig savedColor;
    std::vector<triggerlab::NamedProfile> profiles;
    std::wstring selectedProfile;
    std::wstring validationMessage;
    std::wstring actionError;
    enum class FeedbackTone {Neutral,Info,Good,Waiting,Error};
    struct Feedback {FeedbackTone tone;std::wstring title,detail;};
    std::optional<triggerlab::TrainingProfile> profileBaseline;
    bool syntheticDown=false;
    triggerlab::ClickPulse inputPulse;
    using PressTiming=triggerlab::ClickPulse::Result;
    std::optional<PressTiming> lastPress;
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
    explicit Preview(const std::filesystem::path& dataDirectory={},triggerlab::ClickPulse::Sender sender=SendInput):inputPulse(sender) {
        wchar_t local[32768]{};auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
        colorPath=length>0 && length<32768?std::filesystem::path(local)/L"TriggerLab"/L"selected-color.json":std::filesystem::path(L"selected-color.json");
        if(!dataDirectory.empty()) colorPath=dataDirectory/L"selected-color.json";
        const auto loaded=triggerlab::loadColorProfile(colorPath);if(loaded.ok) {color=loaded.color;colorReady=true;}
        settingsPath=colorPath.parent_path()/L"analyzer-settings.json";
        settings=triggerlab::loadAnalyzerSettings(settingsPath);
        settings.dryRun=true; // Every launch starts in diagnostic mode.
        savedSettings=settings;savedColor=color;
        std::error_code settingsError;
        paramsOnDisk=std::filesystem::exists(settingsPath,settingsError);colorOnDisk=loaded.ok;
        if(settingsError) throw std::runtime_error("Cannot access settings directory");
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
    bool inputAuthorized() const {
        return triggerlab::InputAuthorization{colorReady,verified,settingsDirty() || colorDirty()}.allows(settings.dryRun);
    }
    void syncActions() {
        EnableWindow(colorEdits[3],color.space==triggerlab::ColorSpace::RGB);
        SetWindowTextW(colorLabels[3],color.space==triggerlab::ColorSpace::RGB?L"Допуск RGB":L"Допуск RGB (не HSV)");
        EnableWindow(GetDlgItem(window,2),running || syntheticDown || pickDeadline || targetDeadline || aimPickDeadline);
        const bool target=targetWindow && IsWindow(targetWindow);
        EnableWindow(autoControl,running && target);
        EnableWindow(GetDlgItem(window,20),settings.center && target);
        const bool canCalibrate=settings.center && target && colorReady;
        EnableWindow(GetDlgItem(window,31),canCalibrate);EnableWindow(GetDlgItem(window,32),canCalibrate);
        if(qualityInfo) {const auto value=qualityText();wchar_t previous[2048]{};GetWindowTextW(qualityInfo,previous,2048);if(value!=previous) SetWindowTextW(qualityInfo,value.c_str());}
        updateWizard();
        auto updateText=[](HWND control,const std::wstring& value) {if(!control) return;wchar_t previous[2048]{};GetWindowTextW(control,previous,2048);if(value!=previous) SetWindowTextW(control,value.c_str());};
        const auto feedback=currentFeedback();
        updateText(homeHint,feedback.detail);
        updateText(homeSummary,L"Клики: "+std::to_wstring(clickCount)+L"    Проверки без ввода: "+std::to_wstring(diagnosticCount)+L"\n"+
            (!running?L"Остановлено. Клики не отправляются.":settings.dryRun?L"Диагностика: клики не отправляются.":autoClick?L"Автоклики включены только в выбранном окне.":L"Захват без кликов.")+L"\n"+clickStatus+L"\n"+pressTimingText());
        updateText(headerStatus,feedback.title+L" · "+(running && autoClick && !settings.dryRun?L"автоклики включены":L"без кликов"));
    }
    bool persistSettings(std::string& error) {
        const bool ok=triggerlab::saveAnalyzerSettings(settingsPath,settings,error);if(ok) {savedSettings=settings;paramsOnDisk=true;}return ok;
    }
    static bool fieldIs(HWND field,int value) {
        char text[32]{};GetWindowTextA(field,text,32);int actual=0;const auto end=text+std::char_traits<char>::length(text);
        const auto parsed=std::from_chars(text,end,actual);return parsed.ec==std::errc{} && parsed.ptr==end && actual==value;
    }
    bool settingsDirty() const {
        const int values[]={savedSettings.side,savedSettings.ignoreRadius,savedSettings.minimumPercent,savedSettings.offsetX,savedSettings.offsetY,savedSettings.dwellMs,savedSettings.cooldownMs,savedSettings.clickMs};
        for(int i=0;i<8;++i) if(!fieldIs(settingEdits[i],values[i])) return true;
        return settings.center!=savedSettings.center || settings.darkTheme!=savedSettings.darkTheme || settings.advancedMode!=savedSettings.advancedMode;
    }
    static bool sameColor(const triggerlab::ColorMatchConfig& a,const triggerlab::ColorMatchConfig& b) noexcept {
        return a.space==b.space && a.target.red==b.target.red && a.target.green==b.target.green && a.target.blue==b.target.blue &&
            a.channelTolerance==b.channelTolerance && a.hueTolerance==b.hueTolerance &&
            a.saturationTolerance==b.saturationTolerance && a.valueTolerance==b.valueTolerance && a.minimumCoverage==b.minimumCoverage;
    }
    bool colorDirty() const {
        const int values[]={savedColor.target.red,savedColor.target.green,savedColor.target.blue,savedColor.channelTolerance};
        for(int i=0;i<4;++i) if(!fieldIs(colorEdits[i],values[i])) return true;
        return !sameColor(color,savedColor);
    }
    bool profileChanged() const {
        if(!profileBaseline) return false;
        const auto& s=profileBaseline->settings;const auto& c=profileBaseline->color;
        const int parameters[]={s.side,s.ignoreRadius,s.minimumPercent,s.offsetX,s.offsetY,s.dwellMs,s.cooldownMs,s.clickMs};
        const int rgb[]={c.target.red,c.target.green,c.target.blue,c.channelTolerance};
        for(int i=0;i<8;++i) if(!fieldIs(settingEdits[i],parameters[i])) return true;
        for(int i=0;i<4;++i) if(!fieldIs(colorEdits[i],rgb[i])) return true;
        return settings.center!=s.center || !sameColor(color,c);
    }
    bool readDraft(triggerlab::AnalyzerSettings& next,triggerlab::ColorMatchConfig& nextColor) {
        next=settings;nextColor=color;int values[8]{},rgb[4]{};
        auto invalid=[&](const std::wstring& message,HWND field,bool advanced=true) {
            status=notice=validationMessage=message;
            if(advanced) {settings.advancedMode=true;profilePane=false;layoutMode();}
            SetFocus(field);SendMessageW(field,EM_SETSEL,0,-1);InvalidateRect(window,nullptr,FALSE);return false;
        };
        auto read=[&](HWND field,int& value) {char text[32]{};GetWindowTextA(field,text,32);const auto end=text+std::char_traits<char>::length(text);const auto p=std::from_chars(text,end,value);return p.ec==std::errc{} && p.ptr==end;};
        const wchar_t* labels[]={L"Размер области",L"Исключение центра",L"Порог",L"Сдвиг X",L"Сдвиг Y",L"Задержка",L"Интервал",L"Нажатие"};
        for(int i=0;i<8;++i) if(!read(settingEdits[i],values[i])) return invalid(std::wstring(labels[i])+L": введите целое число",settingEdits[i]);
        next.side=values[0];next.ignoreRadius=values[1];next.minimumPercent=values[2];next.offsetX=values[3];next.offsetY=values[4];next.dwellMs=values[5];next.cooldownMs=values[6];next.clickMs=values[7];
        if(next.side<5 || next.side>101 || next.side%2==0) return invalid(L"Размер области: нечётное число от 5 до 101",settingEdits[0]);
        if(next.ignoreRadius<0 || next.ignoreRadius>=next.side/2) return invalid(L"Исключение центра: от 0 до "+std::to_wstring(next.side/2-1),settingEdits[1]);
        if(next.minimumPercent<1 || next.minimumPercent>100) return invalid(L"Порог: от 1 до 100 процентов",settingEdits[2]);
        for(int i=3;i<=4;++i) if(values[i]<-2000 || values[i]>2000) return invalid(std::wstring(labels[i])+L": от −2000 до 2000 пикселей",settingEdits[i]);
        if(next.dwellMs<1 || next.dwellMs>2000) return invalid(L"Задержка: от 1 до 2000 мс",settingEdits[5]);
        if(next.cooldownMs<50 || next.cooldownMs>2000) return invalid(L"Интервал: от 50 до 2000 мс",settingEdits[6]);
        if(next.clickMs<5 || next.clickMs>150) return invalid(L"Нажатие: от 5 до 150 мс",settingEdits[7]);
        const wchar_t* channels[]={L"Красный (R)",L"Зелёный (G)",L"Синий (B)",L"Допуск"};
        for(int i=0;i<4;++i) if(!read(colorEdits[i],rgb[i]) || rgb[i]<0 || rgb[i]>255) return invalid(std::wstring(channels[i])+L": целое число от 0 до 255",colorEdits[i],i!=3);
        if(rgb[0]!=color.target.red || rgb[1]!=color.target.green || rgb[2]!=color.target.blue) nextColor.space=triggerlab::ColorSpace::RGB;
        nextColor.target={static_cast<std::uint8_t>(rgb[2]),static_cast<std::uint8_t>(rgb[1]),static_cast<std::uint8_t>(rgb[0]),255};nextColor.channelTolerance=rgb[3];validationMessage.clear();return true;
    }
    bool chosenColor(const triggerlab::ColorMatchConfig& draft) const {
        return colorReady || draft.target.red!=color.target.red || draft.target.green!=color.target.green || draft.target.blue!=color.target.blue;
    }
    bool saveAll() {
        actionError.clear();
        triggerlab::AnalyzerSettings next;triggerlab::ColorMatchConfig nextColor;
        if(!readDraft(next,nextColor)) {notice=status;return false;}
        cancelSelection();disableClicks();
        if(settingsDirty() || colorDirty()) verified=false;
        colorReady=chosenColor(nextColor);settings=next;color=nextColor;gate.setTiming(std::chrono::milliseconds{settings.dwellMs},std::chrono::milliseconds{settings.cooldownMs});
        sample={};mask={};displayFrame.reset();displayMask={};analysisPoint.reset();
        std::string error;const bool savedParams=persistSettings(error),savedRgb=!colorReady || triggerlab::saveColorProfile(colorPath,color,error);
        if(savedRgb && colorReady) {savedColor=color;colorOnDisk=true;}
        status=notice=savedParams && savedRgb?(colorReady?L"Все настройки сохранены":L"Параметры сохранены. Теперь выберите цвет мишени"):!savedParams && !savedRgb?L"Не удалось сохранить настройки":savedParams?L"Параметры сохранены; цвет сохранить не удалось":L"Не удалось сохранить параметры";
        if(!savedParams || !savedRgb) actionError=status+L". Проверьте доступ к папке настроек и повторите сохранение.";
        syncActions();InvalidateRect(window,nullptr,FALSE);return savedParams && savedRgb;
    }
    void onEditChange(HWND field) {
        if(!uiReady || uiSync) return;
        const bool parameter=std::find(settingEdits.begin(),settingEdits.end(),field)!=settingEdits.end();
        const bool rgb=std::find(colorEdits.begin(),colorEdits.end(),field)!=colorEdits.end();
        if(!parameter && !rgb) return;
        validationMessage.clear();
        disableClicks();verified=false;notice=L"Есть несохранённые изменения. Нажмите «Сохранить всё»";syncActions();InvalidateRect(window,nullptr,FALSE);
    }
    static std::wstring wide(const std::string& text) {
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
        std::wstring result(length,L'\0');if(length) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),length);return result;
    }
    void refreshProfiles() {
        profiles=triggerlab::listTrainingProfiles(settingsPath.parent_path()/L"profiles");SendMessageW(profileChoice,CB_RESETCONTENT,0,0);
        for(const auto& item:profiles) {const auto name=wide(item.name);SendMessageW(profileChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
        if(!profiles.empty()) SendMessageW(profileChoice,CB_SETCURSEL,0,0);
        EnableWindow(GetDlgItem(window,26),!profiles.empty());
    }
    void saveProfile() {
        triggerlab::AnalyzerSettings next;triggerlab::ColorMatchConfig nextColor;
        if(!readDraft(next,nextColor)) {notice=status;return;}
        if(!chosenColor(nextColor)) {notice=L"Сначала выберите цвет мишени, затем сохраните профиль";return;}
        wchar_t name[128]{};GetWindowTextW(profileName,name,128);std::wstring label(name);
        while(!label.empty() && std::iswspace(label.front())) label.erase(label.begin());while(!label.empty() && std::iswspace(label.back())) label.pop_back();
        const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,label.data(),static_cast<int>(label.size()),nullptr,0,nullptr,nullptr);
        std::string utf8(length,'\0');if(length) WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,label.data(),static_cast<int>(label.size()),utf8.data(),length,nullptr,nullptr);
        if(utf8.empty() || utf8.size()>120) {notice=L"Введите название профиля: до 60 русских букв";return;}
        cancelSelection();disableClicks();std::string error;
        if(!triggerlab::saveTrainingProfile(settingsPath.parent_path()/L"profiles",{utf8,next,nextColor},error)) {notice=L"Профиль не сохранён. Проверьте название и доступ к папке";return;}
        selectedProfile=label;profileBaseline=triggerlab::TrainingProfile{utf8,next,nextColor};refreshProfiles();
        for(std::size_t i=0;i<profiles.size();++i) if(profiles[i].name==utf8) SendMessageW(profileChoice,CB_SETCURSEL,i,0);
        const bool saved=saveAll();if(saved) notice=L"Профиль «"+label+L"» и настройки сохранены";
    }
    void loadProfile() {
        const auto selected=SendMessageW(profileChoice,CB_GETCURSEL,0,0);
        if(selected<0 || static_cast<std::size_t>(selected)>=profiles.size()) {notice=L"Выберите сохранённый профиль";return;}
        const auto profile=triggerlab::loadTrainingProfile(profiles[selected].path);
        if(!profile) {notice=L"Профиль повреждён; текущие настройки сохранены";return;}
        if(settingsDirty() || colorDirty()) {notice=L"Сначала сохраните текущие изменения кнопкой «Сохранить всё»";return;}
        stop();const bool dark=settings.darkTheme,advanced=settings.advancedMode;
        settings=profile->settings;settings.darkTheme=dark;settings.advancedMode=advanced;settings.dryRun=true;color=profile->color;
        selectedProfile=wide(profile->name);profileBaseline=*profile;SetWindowTextW(profileName,selectedProfile.c_str());syncSettings();syncColor();verified=false;colorReady=true;
        gate.setTiming(std::chrono::milliseconds{settings.dwellMs},std::chrono::milliseconds{settings.cooldownMs});
        const bool saved=saveAll();if(saved) notice=L"Профиль «"+selectedProfile+L"» загружен. Начните проверку без кликов";
    }
    int scaled(int value) const {return MulDiv(value,static_cast<int>(dpi),96);}
    void mapCanvas(HDC dc,int x=0,int y=0) const {
        SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,96,96,nullptr);
        SetViewportExtEx(dc,dpi,dpi,nullptr);SetViewportOrgEx(dc,-x,-y,nullptr);
    }
    void placeLogical(HWND control,int x,int y,int width,int height) {
        for(auto& entry:controlRects) if(entry.control==control) {entry.logical={x,y,x+width,y+height};return;}
    }
    bool pinned(HWND child) const {
        const int id=GetDlgCtrlID(child);return id==1 || id==2 || id==21 || id==22 || id==23 || id==30 || (id==25 && !narrow);
    }
    void placeHeader() {
        auto put=[&](int id,int x,int y,int w,int h) {for(auto& entry:controlRects) if(entry.control==GetDlgItem(window,id)) {entry.placed={x,y,x+w,y+h};return;}};
        const int width=canvasWidth-48;
        if(canvasWidth>=620) {
            headerHeight=144;
            put(21,canvasWidth-192,12,168,36);put(23,canvasWidth-392,12,188,36);
            if(!narrow) put(25,canvasWidth-524,12,120,36);
            const int cell=std::min(250,(width-12)/2);put(1,24,60,cell,40);put(22,24,60,cell,40);put(2,36+cell,60,cell,40);
            put(30,24,110,narrow?width:470,26);
        } else {
            headerHeight=144;
            const int cell=(width-12)/2;
            put(23,24,36,cell,32);put(21,36+cell,36,cell,32);
            put(1,24,76,cell,40);put(22,24,76,cell,40);put(2,36+cell,76,cell,40);
            put(30,24,120,width,22);
        }
        SetWindowTextW(GetDlgItem(window,23),canvasWidth<360?(settings.advancedMode?L"Простой":L"Настройки"):(settings.advancedMode?L"Простой режим":L"Расширенные"));
    }
    void narrowControls() {
        const int left=24,width=canvasWidth-48;
        auto put=[&](HWND child,int x,int y,int w,int h) {for(auto& entry:controlRects) if(entry.control==child) {entry.placed={x,y,x+w,y+h};return;}};
        auto button=[&](int id,int y,int height=40) {put(GetDlgItem(window,id),left,y,width,height);};
        int y=64;
        if(settings.advancedMode) {button(25,0,36);y+=44;}
        auto begin=[&](RECT& card) {card={12,y,canvasWidth-12,0};y+=58;};
        auto end=[&](RECT& card) {y+=16;card.bottom=y;y+=16;};
        begin(windowCard);button(7,y,58);y+=66;button(18,y);y+=48;button(19,y);y+=48;
        put(modeControl,left,y,width,140);y+=40;end(windowCard);
        begin(runCard);
        if(settings.advancedMode) {put(dryControl,left,y,width,40);y+=48;put(autoControl,left,y,width,40);y+=48;}
        y+=76; // Wrapped next-action hint.
        end(runCard);
        begin(optionsCard);
        if(settings.advancedMode && !profilePane) {
            const int columns=canvasWidth>=480?2:1,cell=(width-12*(columns-1))/columns;
            for(int i=0;i<8;++i) {
                const int x=left+(i%columns)*(cell+12),top=y+(i/columns)*68;
                put(settingLabels[i],x,top,cell,22);put(settingEdits[i],x,top+24,cell,32);
            }
            y+=((8+columns-1)/columns)*68;
            put(settingsHint,left,y,width,62);y+=70;
        } else {
            // The painter supplies labels immediately above these fields.
            y+=24;put(profileChoice,left,y,width,180);y+=40;button(26,y);y+=72;
            put(profileName,left,y,width,34);y+=42;button(27,y);y+=48;y+=62;
        }
        button(24,y);y+=48;end(optionsCard);
        begin(colorCard);
        if(settings.advancedMode) {
            const int columns=canvasWidth>=360?2:1,cell=(width-12*(columns-1))/columns;
            for(int i=0;i<4;++i) {const int x=left+(i%columns)*(cell+12),top=y+(i/columns)*68;
                put(colorLabels[i],x,top,cell,22);put(colorEdits[i],x,top+24,cell,32);}
            y+=((4+columns-1)/columns)*68;button(3,y);y+=48;button(6,y);y+=48;
        } else {put(colorLabels[3],left,y,width,22);put(colorEdits[3],left,y+24,width,32);y+=68;put(colorHint,left,y,width,70);y+=78;}
        y+=58; // Current-color caption and swatch.
        for(int id:{20,5,4,10,14}) {button(id,y);y+=48;}
        end(colorCard);
        begin(qualityCard);button(31,y);y+=48;button(32,y);y+=48;const int feedbackHeight=canvasWidth<480?288:160;put(qualityInfo,left,y,width,feedbackHeight);y+=feedbackHeight+8;end(qualityCard);
        begin(stateCard);y+=280+(canvasWidth<480?112:56);end(stateCard);
        for(auto& card:previewCards) {begin(card);y+=242;end(card);}
        footerY=y;canvasHeight=y+156;
    }
    void homeControls() {
        auto put=[&](HWND child,int x,int y,int w,int h) {for(auto& entry:controlRects) if(entry.control==child) {entry.placed={x,y,x+w,y+h};return;}};
        auto button=[&](int id,int x,int y,int w,int h=38) {put(GetDlgItem(window,id),x,y,w,h);};
        const int edge=16,gap=16,full=canvasWidth-2*edge;
        const bool columns=canvasWidth>=800;
        const int cell=columns?(full-gap)/2:full;
        runCard={edge,16,canvasWidth-edge,156};
        put(homeHint,edge+20,80,full-40,60);
        windowCard={edge,172,edge+cell,486};
        const int wx=windowCard.left+16,ww=cell-32;
        button(7,wx,230,ww,58);
        if(cell>=340) {button(18,wx,300,(ww-12)/2);button(19,wx+(ww+12)/2,300,(ww-12)/2);}
        else {button(18,wx,300,ww);button(19,wx,346,ww);}
        put(modeControl,wx,cell>=340?364:408,ww,140);
        const int colorTop=columns?172:502,cx=columns?edge+cell+gap:edge;
        colorCard={cx,colorTop,cx+cell,colorTop+450};
        const int x=cx+16,width=cell-32;
        button(20,x,colorTop+58,width,42);button(10,x,colorTop+110,width);
        put(colorLabels[3],x,colorTop+166,width,22);put(colorEdits[3],x,colorTop+192,width,32);
        button(24,x,colorTop+382,width,42);
        int y=colorCard.bottom+16;
        const int summaryHeight=canvasWidth<480?272:160;
        stateCard={edge,y,canvasWidth-edge,y+66+summaryHeight};
        put(homeSummary,edge+20,y+54,full-40,summaryHeight);y=stateCard.bottom+16;
        button(34,edge,y,full,42);y+=58;
        if(homeDetails) {
            optionsCard={edge,y,canvasWidth-edge,y+340};
            put(profileChoice,edge+16,y+74,full-32,180);button(26,edge+16,y+116,full-32);
            put(profileName,edge+16,y+196,full-32,34);button(27,edge+16,y+240,full-32);y=optionsCard.bottom+16;
            // Optional color sources stay available without crowding the first run.
            for(int id:{5,4,14}) {button(id,edge,y,full);y+=46;}
            const int feedbackHeight=canvasWidth<480?288:186;
            qualityCard={edge,y,canvasWidth-edge,y+168+feedbackHeight};
            button(31,edge+16,y+56,full-32);button(32,edge+16,y+102,full-32);
            put(qualityInfo,edge+16,y+152,full-32,feedbackHeight);y=qualityCard.bottom+16;
            for(auto& card:previewCards) {card={edge,y,canvasWidth-edge,y+300};y+=316;}
        } else {optionsCard={};qualityCard={};for(auto& card:previewCards) card={};}
        footerY=y;canvasHeight=y+146;
    }
    void arrange() {
        if(!uiReady || arranging) return;
        arranging=true;
        struct LayoutDrawing {
            HWND window;bool& arranging;bool suspended;
            void finish() {if(!arranging) return;arranging=false;if(suspended) SendMessageW(window,WM_SETREDRAW,TRUE,0);}
            ~LayoutDrawing() {finish();}
        } drawing{window,arranging,IsWindowVisible(window)!=FALSE};
        if(drawing.suspended) SendMessageW(window,WM_SETREDRAW,FALSE,0);
        // Both bars are included when computing the viewport, avoiding a
        // horizontal bar hiding the final row of a nearly fitting canvas.
        RECT client{};GetClientRect(window,&client);
        const LONG style=GetWindowLongW(window,GWL_STYLE);
        const int barWidth=GetSystemMetricsForDpi(SM_CXVSCROLL,dpi),barHeight=GetSystemMetricsForDpi(SM_CYHSCROLL,dpi);
        const int availableWidth=client.right+((style&WS_VSCROLL)?barWidth:0);
        const int availableHeight=client.bottom+((style&WS_HSCROLL)?barHeight:0);
        const bool wasNarrow=narrow;
        narrow=availableWidth-barWidth<scaled(1102);
        canvasWidth=narrow?std::max(220,std::max(1,availableWidth-barWidth)*96/static_cast<int>(dpi)):1102;
        for(auto& entry:controlRects) entry.placed=entry.logical;
        if(!settings.advancedMode) homeControls();else if(narrow) narrowControls();else {
            canvasHeight=950-84+210+56;
        }
        placeHeader();
        if(narrow!=wasNarrow) scrollX=scrollY=0;
        bool horizontal=availableWidth<scaled(canvasWidth),vertical=availableHeight-scaled(headerHeight)<scaled(canvasHeight);
        if(vertical && availableWidth-barWidth<scaled(canvasWidth)) horizontal=true;
        if(horizontal && availableHeight-barHeight-scaled(headerHeight)<scaled(canvasHeight)) vertical=true;
        if(horizontal!=((style&WS_HSCROLL)!=0)) ShowScrollBar(window,SB_HORZ,horizontal);
        if(vertical!=((style&WS_VSCROLL)!=0)) ShowScrollBar(window,SB_VERT,vertical);
        GetClientRect(window,&client);
        SCROLLINFO sx{sizeof(sx),SIF_RANGE|SIF_PAGE|SIF_POS,0,scaled(canvasWidth)-1,static_cast<UINT>(client.right),scrollX};
        SCROLLINFO sy{sizeof(sy),SIF_RANGE|SIF_PAGE|SIF_POS,0,scaled(canvasHeight)-1,static_cast<UINT>(std::max(1,static_cast<int>(client.bottom)-scaled(headerHeight))),scrollY};
        SetScrollInfo(window,SB_HORZ,&sx,FALSE);SetScrollInfo(window,SB_VERT,&sy,FALSE);
        sx.fMask=sy.fMask=SIF_POS;GetScrollInfo(window,SB_HORZ,&sx);GetScrollInfo(window,SB_VERT,&sy);
        scrollX=sx.nPos;scrollY=sy.nPos;
        struct Placement {HWND child,after;int x,y,width,height;UINT flags;};
        std::vector<Placement> placements;placements.reserve(controlRects.size()+1);
        if(headerBackdrop) placements.push_back({headerBackdrop,HWND_TOP,0,0,static_cast<int>(client.right),scaled(headerHeight),SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW});
        for(const auto& entry:controlRects) {
            const auto& r=entry.placed;const bool fixed=pinned(entry.control);
            const int origin=fixed?0:scaled(headerHeight-((narrow || !settings.advancedMode)?0:84))-scrollY;
            placements.push_back({entry.control,fixed?HWND_TOP:nullptr,scaled(r.left)-(fixed?0:scrollX),scaled(r.top)+origin,
                scaled(r.right-r.left),scaled(r.bottom-r.top),SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW|(fixed?0u:SWP_NOZORDER)});
        }
        // Move the complete layout before painting any control or exposed background.
        HDWP batch=BeginDeferWindowPos(static_cast<int>(placements.size()));
        for(const auto& p:placements) {if(!batch) break;batch=DeferWindowPos(batch,p.child,p.after,p.x,p.y,p.width,p.height,p.flags);}
        const bool moved=batch && EndDeferWindowPos(batch);
        if(!moved) for(const auto& p:placements) SetWindowPos(p.child,p.after,p.x,p.y,p.width,p.height,p.flags);
        for(const auto& entry:controlRects) {
            const bool fixed=pinned(entry.control);
            // WM_SETREDRAW temporarily hides the owner from IsWindowVisible.
            if(!(GetWindowLongW(entry.control,GWL_STYLE)&WS_VISIBLE)) continue;
            if(fixed) SetWindowRgn(entry.control,nullptr,FALSE);
            else {
                // Child windows stay attached to the main command handlers, but
                // their paint and hit-test region cannot cover the fixed header.
                RECT child{},visible{},viewport{0,scaled(headerHeight),client.right,client.bottom};
                GetWindowRect(entry.control,&child);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&child),2);
                const bool intersects=IntersectRect(&visible,&child,&viewport)!=FALSE;
                HRGN clip=intersects?CreateRectRgn(visible.left-child.left,visible.top-child.top,visible.right-child.left,visible.bottom-child.top):CreateRectRgn(0,0,0,0);
                if(clip && !SetWindowRgn(entry.control,clip,FALSE)) DeleteObject(clip);
            }
        }
        drawing.finish();RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME|RDW_UPDATENOW);
    }
    void setDpi(UINT value) {
        dpi=value?value:96;
        HFONT next=CreateFontW(-scaled(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(next) {
            for(const auto& entry:controlRects) SendMessageW(entry.control,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);
            if(tooltip) SendMessageW(tooltip,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);
            if(controlFont) DeleteObject(controlFont);controlFont=next;
        }
        arrange();
    }
    void reveal(HWND control) {
        if(!uiReady || !IsWindowVisible(control) || pinned(control)) return;
        RECT r{},client{};GetWindowRect(control,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);GetClientRect(window,&client);
        const int margin=scaled(6);
        if(r.left<margin) scrollX+=r.left-margin;else if(r.right>client.right-margin) scrollX+=r.right-client.right+margin;
        if(r.top<scaled(headerHeight)+margin) scrollY+=r.top-scaled(headerHeight)-margin;else if(r.bottom>client.bottom-margin) scrollY+=r.bottom-client.bottom+margin;
        arrange();
    }
    void scroll(int bar,int request) {
        SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(window,bar,&info);
        int& position=bar==SB_HORZ?scrollX:scrollY;
        switch(request) {
            case SB_LINEUP:position-=scaled(36);break;case SB_LINEDOWN:position+=scaled(36);break;
            case SB_PAGEUP:position-=static_cast<int>(info.nPage);break;case SB_PAGEDOWN:position+=static_cast<int>(info.nPage);break;
            case SB_THUMBTRACK:case SB_THUMBPOSITION:position=info.nTrackPos;break;
            case SB_TOP:position=0;break;case SB_BOTTOM:position=info.nMax;break;
        }
        arrange();
    }
    static LRESULT CALLBACK viewportProcedure(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR context) {
        auto& app=*reinterpret_cast<Preview*>(context);
        if(message==WM_SETFOCUS) app.reveal(child);
        if(message==WM_MOUSEWHEEL && child!=app.modeControl && child!=app.profileChoice) return SendMessageW(app.window,message,wp,lp);
        if(message==WM_NCDESTROY) RemoveWindowSubclass(child,viewportProcedure,id);
        return DefSubclassProc(child,message,wp,lp);
    }
    static LRESULT CALLBACK headerProcedure(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR context) {
        auto& app=*reinterpret_cast<Preview*>(context);
        if(message==WM_PAINT) {PAINTSTRUCT ps{};HDC dc=BeginPaint(child,&ps);RECT r{};GetClientRect(child,&r);app.paintHeader(dc,r.right);EndPaint(child,&ps);return 0;}
        if(message==WM_ERASEBKGND) return 1;
        if(message==WM_NCDESTROY) RemoveWindowSubclass(child,headerProcedure,id);
        return DefSubclassProc(child,message,wp,lp);
    }
    void layoutMode() {
        const bool advanced=settings.advancedMode,parameters=advanced && !profilePane;
        for(HWND field:settingEdits) ShowWindow(field,parameters?SW_SHOW:SW_HIDE);
        for(HWND label:settingLabels) ShowWindow(label,parameters?SW_SHOW:SW_HIDE);
        ShowWindow(settingsHint,parameters?SW_SHOW:SW_HIDE);
        for(int id:{26,27,28,29}) ShowWindow(GetDlgItem(window,id),parameters?SW_HIDE:SW_SHOW);
        for(int i=0;i<3;++i) {ShowWindow(colorEdits[i],advanced?SW_SHOW:SW_HIDE);ShowWindow(colorLabels[i],advanced?SW_SHOW:SW_HIDE);}
        for(int id:{1,3,6,8,16}) ShowWindow(GetDlgItem(window,id),advanced?SW_SHOW:SW_HIDE);
        ShowWindow(GetDlgItem(window,22),advanced?SW_HIDE:SW_SHOW);ShowWindow(GetDlgItem(window,17),SW_HIDE);
        ShowWindow(colorHint,advanced?SW_HIDE:SW_SHOW);
        placeLogical(colorLabels[3],advanced?370:40,457,104,22);
        placeLogical(colorEdits[3],advanced?370:40,482,96,30);
        SetWindowTextW(GetDlgItem(window,23),advanced?L"Простой режим":L"Расширенные");
        ShowWindow(GetDlgItem(window,25),advanced?SW_SHOW:SW_HIDE);
        SetWindowTextW(GetDlgItem(window,25),profilePane?L"Параметры":L"Профили");
        ShowWindow(GetDlgItem(window,34),advanced?SW_HIDE:SW_SHOW);
        ShowWindow(homeHint,advanced?SW_HIDE:SW_SHOW);ShowWindow(homeSummary,advanced?SW_HIDE:SW_SHOW);
        SetWindowTextW(GetDlgItem(window,34),homeDetails?L"Скрыть инструменты":L"Профили и диагностика");
        if(!advanced) {
            for(int id:{26,27,28,29,5,4,14,31,32,33}) ShowWindow(GetDlgItem(window,id),homeDetails?SW_SHOW:SW_HIDE);
            ShowWindow(colorHint,SW_HIDE);
        }
        arrange();syncActions();RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    int wizardStep() const {return !targetWindow || !IsWindow(targetWindow)?0:!colorReady?1:!verified?2:3;}
    bool colorFound() const {return sample.frame && (!settings.center || !sample.allBlack) && (settings.center?mask.found:sample.cursorPixel && triggerlab::matchesColor(*sample.cursorPixel,color));}
    std::wstring saveState() const {
        const bool a=settingsDirty(),b=colorDirty();
        if(a || b) return L"● Несохранённые изменения: "+std::wstring(a && b?L"параметры и цвет":a?L"параметры":L"цвет");
        if(paramsOnDisk && colorOnDisk) return profileChanged()?L"✓ Настройки сохранены · профиль изменён, сохраните его отдельно":L"✓ Все настройки сохранены";
        if(paramsOnDisk && !colorReady) return L"✓ Параметры сохранены · выберите цвет мишени";
        return L"Стандартные настройки · сохраните их для следующего запуска";
    }
    static std::wstring percent(double fraction) {
        const auto tenths=static_cast<int>(std::lround(std::clamp(fraction,0.0,1.0)*1000));return std::to_wstring(tenths/10)+L","+std::to_wstring(tenths%10)+L"%";
    }
    void clearReferences() {targetReference.reset();backgroundReference.reset();referenceWindow=nullptr;referenceProcess=0;}
    bool referencesCurrent() const {return referenceWindow && referenceWindow==targetWindow && IsWindow(targetWindow) && referenceProcess==targetProcess;}
    std::wstring qualityText() const {
        if(!settings.center) return L"Эталоны доступны в режиме вокруг прицела окна.";
        if(!colorReady) return L"Сначала выберите цвет мишени. Затем снимите мишень и фон.";
        if(settingsDirty() || colorDirty()) return L"Сохраните изменения: эталоны будут проверены с новым цветом, допуском и порогом.";
        const auto check=triggerlab::compareColorSamples(referencesCurrent() && targetReference?&*targetReference:nullptr,referencesCurrent() && backgroundReference?&*backgroundReference:nullptr,color,settings);
        if(!check.valid) return L"Размер области изменился. Переснимите мишень и фон.";
        std::wstring result=L"Мишень: "+(check.targetCoverage?percent(*check.targetCoverage):L"нет снимка")+L" · Фон: "+(check.backgroundCoverage?percent(*check.backgroundCoverage):L"нет снимка")+L" · Порог: "+std::to_wstring(settings.minimumPercent)+L"%\n";
        if(check.targetCoverage && !check.targetFound) result+=L"Мишень не проходит порог. Уточните цвет/допуск или область.\n";
        if(check.backgroundFound) result+=L"Фон тоже срабатывает. Снизьте допуск или выберите другой оттенок.\n";
        if(!check.targetCoverage || !check.backgroundCoverage) return result+L"Снимите оба эталона: сначала мишень, затем участок фона без мишени. F9 отменяет ожидание.";
        if(check.targetFound && !check.backgroundFound) result+=L"На этих эталонах мишень проходит порог, фон — нет.\n";
        return result+L"Оценка по двум сохранённым снимкам; повторите её на других участках фона.";
    }
    bool recordReference(triggerlab::CaptureSample fresh,SnapshotPurpose purpose) {
        if(purpose==SnapshotPurpose::Color || fresh.status!=triggerlab::CaptureStatus::Ok || !fresh.frame || fresh.allBlack || !settings.center || !targetWindow || !IsWindow(targetWindow) || fresh.frame->width()!=settings.side || fresh.frame->height()!=settings.side) return false;
        if(!referencesCurrent()) clearReferences();referenceWindow=targetWindow;referenceProcess=targetProcess;
        if(purpose==SnapshotPurpose::Target) targetReference=std::move(fresh.frame);else backgroundReference=std::move(fresh.frame);
        return true; // Reference images never enter sample/displayFrame or the click gate.
    }
    std::wstring captureHint() const {
        switch(sample.status) {
            case triggerlab::CaptureStatus::InvalidRegion:return L"Область выходит за окно. Уменьшите размер или сдвиги.";
            case triggerlab::CaptureStatus::AllocationFailed:return L"Не хватает ресурсов для снимка. Уменьшите область или перезапустите захват.";
            case triggerlab::CaptureStatus::CopyFailed:return L"Windows не смогла получить снимок. Попробуйте оконный режим тренажёра.";
            case triggerlab::CaptureStatus::CursorMoved:return L"Курсор переместился во время снимка. Остановитесь на нужном цвете.";
            case triggerlab::CaptureStatus::DesktopUnavailable:return L"Свежий кадр недоступен. Проверьте, что окно тренажёра открыто и область прицела видна; попробуйте оконный режим.";
            default:return L"Нет свежего снимка. Проверьте доступность выбранного окна.";
        }
    }
    std::wstring detectionHint() const {
        if(!sample.frame) return captureHint();
        if(sample.allBlack && settings.center) return L"Снимок чёрный. Попробуйте оконный режим тренажёра.";
        if(!colorFound()) {
            if(settings.center && mask.matchingPixels>0) return L"Цвет есть: "+percent(mask.coverage)+L", нужно "+std::to_wstring(settings.minimumPercent)+L"%. Наведите прицел ближе к мишени или уточните область.";
            if(settings.center && triggerlab::excludedColorPixels(*sample.frame,color,settings)>0) return L"Цвет только под исключённым перекрестием. Выберите цвет мишени вне перекрестия.";
            return L"Цвет не найден. Уточните оттенок и допуск; сравните эталоны мишени и фона.";
        }
        if(!autoClick) return L"Цвет найден, но триггер выключен. Запустите проверку либо включите триггер в расширенном режиме.";
        if(syntheticDown) return L"Кнопка ещё удерживается; ожидаем отпускание.";
        if(inputHeld()) return L"Срабатывание приостановлено: отпустите кнопки мыши и клавиши Shift/Ctrl/Alt.";
        if(gate.clickedHover()) return settings.dryRun?L"Проверка пройдена. Уведите прицел с цвета для следующей проверки.":L"Клик уже выполнен. Уведите прицел с цвета и наведите снова.";
        const auto elapsed=std::max<std::int64_t>(0,gate.elapsed(triggerlab::Clock::now()).count());
        return elapsed>=settings.dwellMs?L"Задержка пройдена; ожидаем допустимый интервал и свежий кадр.":L"Цвет найден. Ожидание задержки: "+std::to_wstring(elapsed)+L" / "+std::to_wstring(settings.dwellMs)+L" мс.";
    }
    std::wstring nextHint() const {
        if(aimPickDeadline) return snapshotPurpose==SnapshotPurpose::Background?L"Наведите прицел на фон без мишени. F9 — отмена":L"Наведите прицел на мишень. F9 — отмена";
        if(pickDeadline || targetDeadline) return L"Выбор идёт: активируйте тренировку. F9 — отмена";
        if(!validationMessage.empty()) return validationMessage;
        if(wizardStep()==0) return L"Выберите окно из списка либо найдите Kovaak’s / Aimlabs кнопкой ниже";
        if(!colorReady) return L"Возьмите цвет мишени с экрана или из изображения";
        if(settingsDirty() || colorDirty()) return L"Сохраните изменения перед проверкой обнаружения";
        if(!running) return verified?L"Цвет проверен. Можно включить клики или повторить проверку":L"Нажмите «Проверить цвет» и наведите прицел на мишень";
        if(GetAncestor(GetForegroundWindow(),GA_ROOT)!=targetWindow) return L"Активируйте выбранную тренировку";
        if(settings.center) {const auto bounds=clientBounds();if(bounds && !triggerlab::aimRegion(*bounds,settings)) return L"Область выходит за окно. Уменьшите размер или сдвиги.";}
        if(!sample.frame && (status==L"Область прицела перекрыта другим окном" || status==L"Уведите курсор за окно анализатора")) return status;
        return detectionHint();
    }
    Feedback feedback(bool foreground,bool held) const {
        if(syntheticDown && !autoClick) return {FeedbackTone::Error,L"Ожидаем отпускания кнопки",L"Windows пока отклоняет отпускание. Новые клики заблокированы; Stop/F9 повторяет попытку."};
        if(!validationMessage.empty()) return {FeedbackTone::Error,L"Ошибка параметров",validationMessage};
        if(!actionError.empty()) return {FeedbackTone::Error,L"Действие не выполнено",actionError};
        if(pickDeadline || targetDeadline || aimPickDeadline) return {FeedbackTone::Info,L"Выбор · F9 отменяет",nextHint()};
        if(wizardStep()==0) return {FeedbackTone::Neutral,L"Выберите окно",nextHint()};
        if(!colorReady) return {FeedbackTone::Neutral,L"Выберите цвет",nextHint()};
        if(settingsDirty() || colorDirty()) return {FeedbackTone::Waiting,L"Сохраните изменения",nextHint()};
        if(!running) return {verified?FeedbackTone::Good:FeedbackTone::Neutral,verified?L"Готово к запуску":L"Нужна проверка цвета",nextHint()};
        if(!foreground) return {FeedbackTone::Waiting,L"Пауза: окно не активно",L"Вернитесь в выбранную тренировку. Если открыто меню, возобновите игру. Для полной остановки нажмите F9."};
        if(settings.center) {const auto bounds=clientBounds();if(bounds && !triggerlab::aimRegion(*bounds,settings)) return {FeedbackTone::Error,L"Область за пределами окна",L"В расширенных настройках уменьшите размер области или сдвиги X/Y, сохраните и повторите проверку."};}
        if(!sample.frame) {
            const bool error=sample.status==triggerlab::CaptureStatus::InvalidRegion || sample.status==triggerlab::CaptureStatus::CopyFailed || sample.status==triggerlab::CaptureStatus::AllocationFailed;
            const bool covered=status==L"Область прицела перекрыта другим окном" || status==L"Уведите курсор за окно анализатора";
            return {error?FeedbackTone::Error:FeedbackTone::Waiting,L"Нет свежего кадра",covered?status+L". Освободите область прицела и вернитесь в тренировку.":captureHint()};
        }
        if(sample.allBlack && settings.center) return {FeedbackTone::Waiting,L"Снимок чёрный",captureHintForBlack()};
        if(!autoClick) return {FeedbackTone::Neutral,L"Захват без кликов",L"Для диагностики нажмите «Проверить цвет» в простом режиме или включите триггер в расширенном. Клики сейчас не отправляются."};
        if(syntheticDown) return {FeedbackTone::Info,L"Выполняется нажатие",L"Ожидаем отпускание кнопки. F9 остановит ввод и отпустит её."};
        if(held) return {FeedbackTone::Waiting,L"Пауза: удерживается ввод",L"Отпустите кнопки мыши и клавиши Shift, Ctrl, Alt или Win. Для полной остановки нажмите F9."};
        if(!colorFound()) return {FeedbackTone::Waiting,L"Ожидание мишени",detectionHint()};
        if(gate.clickedHover()) return {FeedbackTone::Good,settings.dryRun?L"Проверка пройдена":L"Клик завершён",settings.dryRun?L"Цвет распознан без отправки клика. Можно остановить проверку F9 и включить автоклики.":L"На это непрерывное совпадение клик уже выполнен. Уведите прицел с цвета и наведите снова."};
        return {FeedbackTone::Waiting,L"Цвет найден: выдержка задержки",detectionHint()};
    }
    static std::wstring captureHintForBlack() {return L"В области только чёрные пиксели. Наведите на видимую мишень; если она не появляется на снимке, попробуйте оконный режим тренажёра.";}
    Feedback currentFeedback() const {return feedback(GetAncestor(GetForegroundWindow(),GA_ROOT)==targetWindow,inputHeld());}
    COLORREF feedbackColor(FeedbackTone tone) const {
        switch(tone) {
            case FeedbackTone::Error:return settings.darkTheme?RGB(255,184,184):RGB(185,28,28);
            case FeedbackTone::Waiting:return settings.darkTheme?RGB(255,211,139):RGB(146,82,8);
            case FeedbackTone::Good:return settings.darkTheme?RGB(134,239,172):RGB(21,128,61);
            case FeedbackTone::Info:return themeColors(settings.darkTheme).accent;
            default:return themeColors(settings.darkTheme).muted;
        }
    }
    void updateWizard() {
        const int step=wizardStep();const wchar_t* labels[]={L"Выбрать окно",L"Взять цвет мишени",L"Проверить цвет",L"Включить клики"};
        const auto caption=autoClick && !settings.dryRun?L"Остановить клики":labels[step];wchar_t previous[128]{};
        GetWindowTextW(GetDlgItem(window,22),previous,128);if(std::wstring(previous)!=caption) SetWindowTextW(GetDlgItem(window,22),caption);
    }
    void guidedAction() {
        if(autoClick && !settings.dryRun) {stop();return;}
        const int step=wizardStep();
        if(step==0) {chooseTargetWindow();return;}
        if(step==1) {if(settings.center) beginAimSelection();else beginSelection(true);return;}
        if(settingsDirty() || colorDirty()) {if(!saveAll()) return;}
        const bool diagnostic=wizardStep()!=3;
        disableClicks(); // Release our existing F9 registration before re-arming.
        settings.dryRun=diagnostic;syncSettings();command(1);SendMessageW(autoControl,BM_SETCHECK,BST_CHECKED,0);command(8);
    }
    std::optional<triggerlab::Region> clientBounds() const {
        if(!targetWindow || !IsWindow(targetWindow) || IsIconic(targetWindow)) return {};
        RECT rect{};POINT origin{};
        if(!GetClientRect(targetWindow,&rect) || !ClientToScreen(targetWindow,&origin) || rect.right<=0 || rect.bottom<=0) return {};
        return triggerlab::Region{origin.x,origin.y,rect.right,rect.bottom};
    }
    void syncSettings() {
        const bool wasSync=uiSync;uiSync=true;
        const int values[]={settings.side,settings.ignoreRadius,settings.minimumPercent,settings.offsetX,settings.offsetY,settings.dwellMs,settings.cooldownMs,settings.clickMs};
        for(int i=0;i<8;++i) SetWindowTextW(settingEdits[i],std::to_wstring(values[i]).c_str());
        SendMessageW(modeControl,CB_SETCURSEL,settings.center?0:1,0);
        SendMessageW(dryControl,BM_SETCHECK,settings.dryRun?BST_CHECKED:BST_UNCHECKED,0);
        uiSync=wasSync;
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
        settings=next;verified=false;displayFrame.reset();displayMask={};gate.setTiming(std::chrono::milliseconds{settings.dwellMs},std::chrono::milliseconds{settings.cooldownMs});
        std::string error;status=persistSettings(error)?L"Параметры сохранены; триггер выключен":L"Параметры применены, но сохранение не удалось";
        return true;
    }
    void findTrainer(const wchar_t* name) {
        verified=false;
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
        clearReferences();displayFrame.reset();displayMask={};targetWindow=search.found.front();GetWindowThreadProcessId(targetWindow,&targetProcess);refreshTargetButton();settings.center=true;syncSettings();
        status=L"Тренажёр выбран. Начните с проверки без кликов";
    }
    void syncColor() {
        const bool wasSync=uiSync;uiSync=true;
        const int values[]={color.target.red,color.target.green,color.target.blue,color.channelTolerance};
        for(int i=0;i<4;++i) SetWindowTextW(colorEdits[i],std::to_wstring(values[i]).c_str());
        uiSync=wasSync;if(uiReady) {colorReady=true;verified=false;validationMessage.clear();displayFrame.reset();displayMask={};}
    }
    std::wstring pressTimingText() const {
        if(!lastPress) return L"Нажатие: ещё не измерено";
        // This measures our SendInput call interval, not delivery to the game.
        const auto tenths=lastPress->elapsed.count()/100;
        return L"Нажатие: "+std::to_wstring(tenths/10)+L","+std::to_wstring(tenths%10)+L" мс (задано "+std::to_wstring(lastPress->requestedMs)+L")"+
            (!lastPress->releaseAccepted?L" · ошибка отпускания":lastPress->interrupted?L" · прервано":L"");
    }
    void finishClick(bool count=true) {
        if(!count) inputPulse.cancel();
        auto completed=inputPulse.takeResult();if(!completed) return;
        syntheticDown=inputPulse.pendingRelease();lastPress=*completed;
        // A pulse already released by the worker still counts if Stop arrives
        // before the UI consumes completion. Only an actual cancellation is partial.
        if(completed->releaseAccepted && !completed->interrupted) {gate.acknowledgeClick(completed->finishedAt);++clickCount;clickStatus=L"Клик отправлен: нажатие и отпускание";}
        if(!completed->releaseAccepted) {autoClick=false;gate.reset();SendMessageW(autoControl,BM_SETCHECK,BST_UNCHECKED,0);notice=clickStatus=L"Отпускание отклонено системой; новые клики заблокированы, Stop/F9 повторяет отпускание";}
        if(!syntheticDown && !autoClick && !pickDeadline && !targetDeadline && !aimPickDeadline) UnregisterHotKey(window,9);
    }
    void disableClicks() {finishClick(false);if(!syntheticDown) UnregisterHotKey(window,9);autoClick=false;gate.reset();clickStatus=syntheticDown?L"Отпускание не подтверждено; новые клики заблокированы":L"Выключен";if(autoControl) SendMessageW(autoControl,BM_SETCHECK,BST_UNCHECKED,0);}
    void cancelSelection() {
        pickDeadline=targetDeadline=aimPickDeadline=0;snapshotPurpose=SnapshotPurpose::Color;KillTimer(window,2);
        if(!autoClick && !syntheticDown) UnregisterHotKey(window,9);
        SetWindowTextW(GetDlgItem(window,5),L"Пипетка (3 сек.)");refreshTargetButton();
        SetWindowTextW(GetDlgItem(window,20),L"Цвет из прицела (3 сек.)");
        SetWindowTextW(GetDlgItem(window,31),L"Снять мишень (3 сек.)");SetWindowTextW(GetDlgItem(window,32),L"Снять фон (3 сек.)");
    }
    void beginAimSelection(SnapshotPurpose purpose=SnapshotPurpose::Color) {
        cancelSelection();disableClicks();
        if(purpose!=SnapshotPurpose::Color && (!colorReady || settingsDirty() || colorDirty())) {status=notice=L"Выберите цвет и сохраните изменения перед съёмкой эталона";return;}
        if(!settings.center || !clientBounds()) {status=L"Выберите окно и режим «Вокруг прицела окна»";return;}
        DWORD process=0;GetWindowThreadProcessId(targetWindow,&process);
        if(process!=targetProcess || !IsWindowVisible(targetWindow) || !IsWindowEnabled(targetWindow)) {status=notice=L"Выбранное окно недоступно для активации";return;}
        if(!RegisterHotKey(window,9,MOD_NOREPEAT,VK_F9)) {status=L"F9 занят: снимок не начат";actionError=status+L". Освободите F9 в другом приложении и повторите действие.";return;}
        if(!SetTimer(window,2,100,nullptr)) {UnregisterHotKey(window,9);status=L"Ошибка таймера: снимок не начат";return;}
        if(GetAncestor(GetForegroundWindow(),GA_ROOT)!=targetWindow && !SetForegroundWindow(targetWindow) && GetAncestor(GetForegroundWindow(),GA_ROOT)!=targetWindow) {
            cancelSelection();status=notice=L"Не удалось активировать окно. Активируйте тренировку вручную и повторите снимок";return;
        }
        // Start the full aiming interval after requesting foreground activation.
        // Completion still checks focus, geometry and a fresh capture.
        snapshotPurpose=purpose;aimPickDeadline=GetTickCount64()+3000;
        status=notice=purpose==SnapshotPurpose::Background?L"Окно активировано. Наведите прицел на фон без мишени за 3 сек.; F9 — отмена":L"Окно активировано. Наведите прицел на мишень за 3 сек.; F9 — отмена";
    }
    void finishAimSelection() {
        const auto purpose=snapshotPurpose;cancelSelection();disableClicks();
        const auto bounds=clientBounds();
        const auto roi=bounds?triggerlab::aimRegion(*bounds,settings):std::optional<triggerlab::Region>{};
        if(!roi || !permittedRegion(*roi)) {
            status=notice=L"Снимок отменён: область недоступна или тренажёр не активен";return;
        }
        auto fresh=capture.capture(*roi);
        const auto currentBounds=clientBounds();
        if(fresh.allBlack) {status=notice=L"Снимок полностью чёрный. Проверьте режим окна; предыдущий эталон и цвет сохранены";return;}
        if(!fresh.frame || fresh.allBlack || !currentBounds || currentBounds->x!=bounds->x || currentBounds->y!=bounds->y || currentBounds->width!=bounds->width || currentBounds->height!=bounds->height || !permittedRegion(*roi) || fresh.region.x!=roi->x || fresh.region.y!=roi->y || fresh.region.width!=roi->width || fresh.region.height!=roi->height) {
            status=notice=L"Снимок недоступен; прежний цвет сохранён";return;
        }
        // Freeze a fresh image before the modal steals focus or timer messages arrive.
        running=false;KillTimer(window,1);sample={};mask={};analysisPoint.reset();capture.reset();
        if(purpose!=SnapshotPurpose::Color) {
            const bool recorded=recordReference(std::move(fresh),purpose);
            status=notice=recorded?(purpose==SnapshotPurpose::Target?L"Эталон мишени снят. Теперь снимите фон.":L"Эталон фона снят. Оценка обновлена."):L"Эталон недоступен; предыдущий снимок сохранён";
            syncActions();SetForegroundWindow(window);SetFocus(window);return;
        }
        const auto selected=triggerlab::showImagePalette(window,*fresh.frame,settings.darkTheme);
        if(selected) {color.target=selected->color;color.space=triggerlab::ColorSpace::RGB;syncColor();status=L"Цвет мишени выбран. Нажмите «Сохранить всё», затем начните проверку без кликов";}
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
        color.target={static_cast<std::uint8_t>(values[2]),static_cast<std::uint8_t>(values[1]),static_cast<std::uint8_t>(values[0]),255};color.channelTolerance=values[3];color.space=triggerlab::ColorSpace::RGB;colorReady=true;verified=false;displayFrame.reset();displayMask={};return true;
    }
    void chooseColor() {
        cancelSelection();disableClicks();COLORREF custom[16]{};CHOOSECOLORW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=window;
        picker.rgbResult=RGB(color.target.red,color.target.green,color.target.blue);picker.lpCustColors=custom;picker.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(ChooseColorW(&picker)) {color.target={GetBValue(picker.rgbResult),GetGValue(picker.rgbResult),GetRValue(picker.rgbResult),255};color.space=triggerlab::ColorSpace::RGB;syncColor();status=L"Цвет выбран. Нажмите «Сохранить всё»";}
    }
    void saveColor() {if(!applyColor()) return;std::string error;const bool saved=triggerlab::saveColorProfile(colorPath,color,error);if(saved) {savedColor=color;colorOnDisk=true;}status=saved?L"Цвет сохранён":L"Не удалось сохранить цвет";}
    bool importImage(const std::filesystem::path& path,bool edit=false) {
        cancelSelection();disableClicks();
        auto loaded=triggerlab::loadImagePixels(path);
        if(!loaded.image) {status=loaded.error;notice=status;return false;}
        auto selected=edit?triggerlab::showImagePalette(window,*loaded.image,settings.darkTheme):triggerlab::dominantImageColor(*loaded.image);
        if(!selected) {status=edit?L"Выбор цвета отменён; прежний цвет сохранён":L"В изображении нет непрозрачных пикселей";notice=status;return false;}
        color.target=selected->color;color.space=triggerlab::ColorSpace::RGB;syncColor();
        importedImage=std::move(loaded.image);
        mask=sample.frame?triggerlab::makeColorMask(*sample.frame,color):triggerlab::ColorMask{};
        status=L"Цвет из файла определён. Нажмите «Сохранить всё»";
        notice=L"Файл: "+path.filename().wstring().substr(0,30)+L" | Доля оттенка: "+
            std::to_wstring(static_cast<int>(selected->coverage*100))+L"%";
        return true;
    }
    void editImage() {
        cancelSelection();disableClicks();
        if(!importedImage) {status=L"Сначала загрузите изображение кнопкой «Из файла…»";return;}
        const auto selected=triggerlab::showImagePalette(window,*importedImage,settings.darkTheme);
        if(!selected) {status=L"Выбор отменён; прежний цвет сохранён";return;}
        color.target=selected->color;color.space=triggerlab::ColorSpace::RGB;syncColor();
        mask=sample.frame?triggerlab::makeColorMask(*sample.frame,color):triggerlab::ColorMask{};
        status=L"Цвет области выбран. Нажмите «Сохранить всё»";
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
                clearReferences();targetWindow=nullptr;targetProcess=0;targetTitle.clear();
                if(autoClick) disableClicks(); // Keep the selection's F9 registration alive.
                notice=L"Выбранное окно закрыто";
            } else {
                wchar_t title[256]{};GetWindowTextW(targetWindow,title,256);
                targetTitle=title[0]?title:L"Без названия";
            }
        }
        std::wstring caption=L"Окно не выбрано\nВыбрать из списка…";
        if(targetDeadline) {
            const auto now=GetTickCount64();const auto seconds=now<targetDeadline?(targetDeadline-now+999)/1000:0;
            caption=L"Выбор окна: "+std::to_wstring(seconds)+L" сек.\nНаведите курсор";
        }
        else if(targetWindow) {
            auto title=targetTitle;
            if(title.size()>25) {title.resize(24);if(title.back()>=0xD800 && title.back()<=0xDBFF) title.pop_back();title+=L"…";}
            caption=L"Окно: "+title+L"\nСменить окно…";
        }
        HWND button=GetDlgItem(window,7);wchar_t previous[256]{};GetWindowTextW(button,previous,256);
        if(caption!=previous) {SetWindowTextW(button,caption.c_str());InvalidateRect(window,nullptr,FALSE);}
    }
    bool applyWindowChoice(const triggerlab::WindowChoice& choice) {
        stop();
        if(!triggerlab::windowChoiceAvailable(choice,window)) {status=notice=L"Окно уже закрыто или недоступно. Прежний выбор сохранён";return false;}
        clearReferences();displayFrame.reset();displayMask={};analysisPoint.reset();analyzedClient.reset();
        targetWindow=choice.window;targetProcess=choice.process;verified=false;settings.dryRun=true;
        SendMessageW(dryControl,BM_SETCHECK,BST_CHECKED,0);refreshTargetButton();
        status=notice=L"Окно выбрано. Начните с проверки без кликов";syncActions();return true;
    }
    void chooseTargetWindow() {
        stop();
        const auto picked=triggerlab::showWindowPicker(window,settings.darkTheme,targetWindow);
        if(picked.choice) applyWindowChoice(*picked.choice);
        else if(picked.manual) beginSelection(false);
        else {status=notice=picked.error.empty()?L"Выбор отменён; прежнее окно сохранено":picked.error;actionError=picked.error;}
        syncActions();InvalidateRect(window,nullptr,FALSE);
    }
    void beginSelection(bool pick) {
        cancelSelection();disableClicks();
        if(!RegisterHotKey(window,9,MOD_NOREPEAT,VK_F9)) {status=L"F9 занят: выбор не начат";actionError=status+L". Освободите F9 в другом приложении и повторите действие.";return;}
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
            SetWindowTextW(GetDlgItem(window,snapshotPurpose==SnapshotPurpose::Color?20:snapshotPurpose==SnapshotPurpose::Target?31:32),caption.c_str());
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
        clearReferences();targetWindow=selected;verified=false;displayFrame.reset();displayMask={};GetWindowThreadProcessId(selected,&targetProcess);
        wchar_t title[256]{};GetWindowTextW(selected,title,256);targetTitle=title;
        status=L"Окно выбрано. Включите автоклик отдельно";refreshTargetButton();
    }
    bool permittedAt(POINT point) const {
        DWORD process=0;if(!targetWindow || !IsWindow(targetWindow)) return false;
        GetWindowThreadProcessId(targetWindow,&process);
        return triggerlab::ClickPermission{reinterpret_cast<std::uintptr_t>(targetWindow),reinterpret_cast<std::uintptr_t>(GetAncestor(WindowFromPoint(point),GA_ROOT)),reinterpret_cast<std::uintptr_t>(GetAncestor(GetForegroundWindow(),GA_ROOT)),targetProcess,process,true}.allowed();
    }
    bool permittedRegion(triggerlab::Region region) const {
        if(!triggerlab::validRegion(region)) return false;
        POINT center{static_cast<LONG>(region.x+region.width/2),static_cast<LONG>(region.y+region.height/2)};
        return permittedAt(center) && triggerlab::isWindowRegionVisible(reinterpret_cast<std::uintptr_t>(targetWindow),region);
    }
    void handleClick(POINT capturedCursor) {
        if(!autoClick) return;
        if(!inputAuthorized()) {disableClicks();notice=L"Сохраните настройки и проверьте цвет перед включением кликов";return;}
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
        if(settings.center && !permittedRegion(sample.region)) {
            gate.step(false,false,triggerlab::Clock::now());clickStatus=L"Область прицела перекрыта другим окном";return;
        }
        if(settings.dryRun) {gate.acknowledgeClick(triggerlab::Clock::now());++diagnosticCount;verified=true;updateWizard();clickStatus=L"Проверка: срабатывание без отправки клика";return;}
        POINT current{};
        if(!GetCursorPos(&current) || (!settings.center && (current.x!=capturedCursor.x || current.y!=capturedCursor.y)) || !permittedAt(current) || !permittedAt(capturedCursor) || (settings.center && !permittedRegion(sample.region)) || inputHeld()) {
            gate.step(false,false,triggerlab::Clock::now());clickStatus=L"Ожидание прервано: курсор, окно или клавиши изменились";return;
        }
        if(inputPulse.start(settings.clickMs)) {
            syntheticDown=true;clickStatus=L"Нажатие выполняется";
        } else {const auto error=GetLastError();disableClicks();status=L"Ввод отклонён системой (код "+std::to_wstring(error)+L")";clickStatus=status;notice=status;actionError=status+L". Проверьте доступность выбранного окна и повторите проверку цвета.";}
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
        auto label=[&](const wchar_t* text,int x,int y,int width) {return CreateWindowW(L"STATIC",text,WS_CHILD|WS_VISIBLE,x,y,width,22,window,nullptr,nullptr,nullptr);};
        button(21,L"Серая тема: выключена",914,20,168,36);
        button(23,L"Расширенные",710,20,188,36);button(25,L"Профили",574,20,120,36);
        button(34,L"Профили и диагностика",24,700,500,42);
        homeHint=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,24,200,500,76,window,reinterpret_cast<HMENU>(35),nullptr,nullptr);
        homeSummary=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,24,600,500,160,window,reinterpret_cast<HMENU>(36),nullptr,nullptr);
        headerStatus=CreateWindowW(L"STATIC",L"Остановлено · без кликов",WS_CHILD|WS_VISIBLE,24,110,500,26,window,reinterpret_cast<HMENU>(30),nullptr,nullptr);
        button(7,L"Выбрать окно (3 сек.)",40,130,260,58);
        button(18,L"Kovaak’s",316,130,135);button(19,L"Aimlabs",316,174,135,30);
        modeControl=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,40,210,410,140,window,reinterpret_cast<HMENU>(15),nullptr,nullptr);
        SendMessageW(modeControl,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Обнаружение вокруг прицела"));
        SendMessageW(modeControl,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Обнаружение под курсором"));
        button(1,L"Начать захват",548,130,250,42);button(2,L"Остановить · F9",812,130,250,42);
        button(22,L"Выбрать окно",548,130,250,42);
        dryControl=CreateWindowW(L"BUTTON",L"Проверять без кликов",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,548,190,235,28,window,reinterpret_cast<HMENU>(16),nullptr,nullptr);
        autoControl=CreateWindowW(L"BUTTON",L"Включить триггер",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,812,190,250,28,window,reinterpret_cast<HMENU>(8),nullptr,nullptr);
        const wchar_t* settingsLabels[]={L"Область, px",L"Исключить центр",L"Порог, %",L"Сдвиг X, px",L"Сдвиг Y, px",L"Задержка, мс",L"Интервал, мс",L"Нажатие, мс"};
        for(int i=0;i<8;++i) {
            settingLabels[i]=label(settingsLabels[i],40+i*128,300,124);
            settingEdits[i]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,40+i*128,326,112,30,window,nullptr,nullptr,nullptr);
        }
        settingsHint=label(L"Область 5–101 px, нечётная. Исключите перекрестие; порог задаёт долю цвета.",40,368,780);
        button(17,L"Сохранить",938,365,124,30);
        button(24,L"Сохранить всё",844,365,218,30);
        profileChoice=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,40,326,366,180,window,reinterpret_cast<HMENU>(28),nullptr,nullptr);
        button(26,L"Загрузить",420,326,124,30);
        profileName=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,560,326,328,30,window,reinterpret_cast<HMENU>(29),nullptr,nullptr);
        SendMessageW(profileName,EM_LIMITTEXT,60,0);button(27,L"Сохранить профиль",902,326,160,30);
        SendMessageW(profileName,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Например: статичные мишени"));
        const wchar_t* colors[]={L"Красный · R",L"Зелёный · G",L"Синий · B",L"Допуск"};
        for(int i=0;i<4;++i) {
            colorLabels[i]=label(colors[i],40+i*110,457,104);
            colorEdits[i]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER,40+i*110,482,96,30,window,nullptr,nullptr,nullptr);
        }
        button(3,L"Применить",482,482,120,30);button(6,L"Сохранить цвет",618,482,160,30);
        button(20,L"Цвет из прицела (3 сек.)",40,530,240);
        button(5,L"Пипетка (3 сек.)",294,530,174);
        button(4,L"Выбрать цвет…",482,530,166);
        button(10,L"Из файла…",662,530,152);
        button(14,L"Палитра / область",828,530,234);
        button(31,L"Снять мишень (3 сек.)",40,652,220,40);button(32,L"Снять фон (3 сек.)",276,652,220,40);
        qualityInfo=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,520,640,540,136,window,reinterpret_cast<HMENU>(33),nullptr,nullptr);
        colorHint=label(L"Выберите цвет мишени с экрана или из файла. Сохраните изменения кнопкой выше.",160,482,625);
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
            tip(GetDlgItem(window,20),L"Окно тренажёра активируется автоматически. За 3 секунды наведите прицел на мишень, затем выберите цвет в палитре. F9 — отмена.");
            tip(GetDlgItem(window,31),L"Окно тренажёра активируется автоматически. Наведите прицел на мишень за 3 секунды. Клики выключены; F9 — отмена.");
            tip(GetDlgItem(window,32),L"Окно тренажёра активируется автоматически. Наведите прицел на фон без мишени за 3 секунды. Эталон хранится только в памяти; F9 — отмена.");
            tip(GetDlgItem(window,23),L"Простой режим показывает основные действия; расширенный открывает ручные параметры.");
            tip(GetDlgItem(window,21),L"Переключение между светлой и тёмно-серой темой. Выбор сохраняется автоматически.");
        }
        EnumChildWindows(window,[](HWND child,LPARAM context)->BOOL {
            auto& app=*reinterpret_cast<Preview*>(context);if(GetParent(child)!=app.window) return TRUE;
            SetWindowLongW(child,GWL_STYLE,GetWindowLongW(child,GWL_STYLE)|WS_CLIPSIBLINGS);
            RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,app.window,reinterpret_cast<POINT*>(&r),2);
            // A combo's window rect contains only its collapsed height.
            if(child==app.modeControl) r.bottom=r.top+140;
            if(child==app.profileChoice) r.bottom=r.top+180;
            app.controlRects.push_back({child,r});SetWindowSubclass(child,viewportProcedure,2,context);return TRUE;
        },reinterpret_cast<LPARAM>(this));
        // An opaque sibling below the pinned controls prevents standard Win32
        // fields from repainting over the header during movement or focus.
        headerBackdrop=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,0,0,window,nullptr,nullptr,nullptr);
        SetWindowSubclass(headerBackdrop,headerProcedure,3,reinterpret_cast<DWORD_PTR>(this));
        syncColor();syncSettings();refreshTargetButton();applyTheme();refreshProfiles();uiReady=true;setDpi(GetDpiForWindow(window));layoutMode();syncActions();SetTimer(window,3,500,nullptr);
    }
    void command(int id,int notification=BN_CLICKED) {
        if(id==1 || id==2 || id==5 || id==7 || id==18 || id==19 || id==20 || id==22 || id==24 || id==26 || id==27 || id==31 || id==32) actionError.clear();
        if(id==1) {if(SetTimer(window,1,10,nullptr)) {running=true;status=L"Захват активен";}else {status=L"Не удалось начать захват";actionError=L"Не удалось создать таймер захвата. Остановите анализатор и повторите запуск.";}}
        else if(id==2) stop();
        else if(id==3) {if(applyColor()) status=L"RGB применён; автоклик выключен";}
        else if(id==4) chooseColor();
        else if(id==5) beginSelection(true);
        else if(id==6) saveColor();
        else if(id==7) chooseTargetWindow();
        else if(id==14) editImage();
        else if(id==17) applySettings();
        else if(id==18) findTrainer(L"kovaak");
        else if(id==19) findTrainer(L"aimlab");
        else if(id==20) beginAimSelection();
        else if(id==31 || id==32) beginAimSelection(id==31?SnapshotPurpose::Target:SnapshotPurpose::Background);
        else if(id==22) {guidedAction();syncActions();notice=status;InvalidateRect(window,nullptr,FALSE);return;}
        else if(id==23) {
            settings.advancedMode=!settings.advancedMode;profilePane=false;layoutMode();std::string error;
            notice=persistSettings(error)?L"Режим интерфейса сохранён":L"Режим изменён; сохранение не удалось";return;
        }
        else if(id==24) {saveAll();return;}
        else if(id==25) {profilePane=!profilePane;layoutMode();return;}
        else if(id==34) {homeDetails=!homeDetails;layoutMode();reveal(GetDlgItem(window,34));return;}
        else if(id==26) {loadProfile();syncActions();InvalidateRect(window,nullptr,FALSE);return;}
        else if(id==27) {saveProfile();syncActions();InvalidateRect(window,nullptr,FALSE);return;}
        else if(id==28 && notification==CBN_SELCHANGE) {
            const auto selected=SendMessageW(profileChoice,CB_GETCURSEL,0,0);
            if(selected>=0 && static_cast<std::size_t>(selected)<profiles.size()) SetWindowTextW(profileName,wide(profiles[selected].name).c_str());return;
        }
        else if(id==21) {
            settings.darkTheme=!settings.darkTheme;applyTheme();std::string error;
            notice=persistSettings(error)?L"Тема сохранена":L"Тема изменена, но не удалось сохранить выбор";
            return;
        }
        else if(id==15 || id==16) {
            if(id==15 && notification!=CBN_SELCHANGE) return;
            cancelSelection();disableClicks();settings.center=SendMessageW(modeControl,CB_GETCURSEL,0,0)==0;settings.dryRun=SendMessageW(dryControl,BM_GETCHECK,0,0)==BST_CHECKED;
            verified=false;layoutMode();
            sample={};mask={};analysisPoint.reset();std::string error;
            status=persistSettings(error)?L"Режим сохранён; включите триггер отдельно":L"Режим изменён; сохранение не удалось";
        }
        else if(id==10) {chooseImage();SetFocus(window);InvalidateRect(window,nullptr,FALSE);return;}
        else if(id==8) {cancelSelection();if(syntheticDown || !running || !targetWindow || !IsWindow(targetWindow) || !inputAuthorized()) {disableClicks();status=L"Выберите окно и цвет, сохраните настройки; перед кликами выполните проверку";}else {autoClick=SendMessageW(autoControl,BM_GETCHECK,0,0)==BST_CHECKED;gate.reset();if(autoClick && !RegisterHotKey(window,9,MOD_NOREPEAT,VK_F9)) {disableClicks();status=L"F9 занят: автоклик не включён";notice=status;actionError=status+L". Освободите F9 в другом приложении и повторите запуск.";syncActions();InvalidateRect(window,nullptr,FALSE);return;}if(!autoClick) UnregisterHotKey(window,9);status=autoClick?(settings.dryRun?L"Проверка активна: клики не отправляются":L"Клики включены только в выбранном окне"):L"Триггер выключен";}}
        else return;
        if(id==8 && autoClick) {
            // Arming is an explicit user action: activate the chosen window,
            // and do not immediately steal focus back to the analyzer.
            if(!SetForegroundWindow(targetWindow)) {
                clickStatus=L"Активируйте выбранное окно вручную";
                status=settings.dryRun?L"Проверка активна; активируйте тренировку":L"Клики включены; активируйте тренировку";
            } else clickStatus=L"Ожидаю цвет в выбранном окне";
        } else if(!((id==20 || id==31 || id==32) && aimPickDeadline)) SetFocus(window);
        syncActions();notice=status;InvalidateRect(window,nullptr,FALSE);
    }
    std::wstring notice;
    std::wstring status=L"Захват остановлен";
    void stop() {running=false;disableClicks();cancelSelection();KillTimer(window,1);capture.reset();sample={};mask={};actionError.clear();status=syntheticDown?L"Захват остановлен; ожидаем подтверждения отпускания кнопки":L"Захват остановлен";notice=status;syncActions();InvalidateRect(window,nullptr,FALSE);}
    void update(bool pick=false) {
        if(!running && !pick) return;
        if(syntheticDown) {
            if(!autoClick || GetAncestor(GetForegroundWindow(),GA_ROOT)!=targetWindow) {disableClicks();return;}
            finishClick();
            if(syntheticDown) return;
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
                if(!permittedRegion(*roi)) {acceptSample({},false);status=clickStatus=L"Область прицела перекрыта другим окном";}
                else {
                    auto fresh=capture.capture(*roi);
                    if(fresh.frame && (fresh.region.x!=roi->x || fresh.region.y!=roi->y || fresh.region.width!=roi->width || fresh.region.height!=roi->height || !permittedRegion(*roi))) fresh={};
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
            // Display-only copy: it cannot be used by the gate or SendInput.
            displayFrame=*sample.frame;displayMask=mask;displaySettings=settings;displaySettings.center=settings.center && !pick;displayTime=GetTickCount64();displayFound=found;
        } else status=sample.status==triggerlab::CaptureStatus::CursorMoved?L"Курсор движется: удерживайте его на нужном цвете":L"Захват недоступен (статус "+std::to_wstring(static_cast<int>(sample.status))+L")";
        if(!settings.center || pick) analysisPoint=sample.cursorPosition;
        if(autoClick && sample.frame && analysisPoint) handleClick({analysisPoint->x,analysisPoint->y});
        else if(autoClick) {gate.step(false,false,triggerlab::Clock::now());clickStatus=ownPixel?L"Курсор над анализатором":L"Нет свежего пикселя: "+status;}
        if(pick) {notice=sample.cursorPixel?L"Цвет взят с экрана. Нажмите «Сохранить всё»":L"Пипетка: свежий пиксель недоступен, цвет не изменён";}
        if(GetTickCount64()-lastPaint>=33) {lastPaint=GetTickCount64();InvalidateRect(window,nullptr,FALSE);}
    }
#include "PreviewTests.inl"
    static LRESULT CALLBACK checkProcedure(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR context) {
        if(message==WM_PAINT) {
            auto& app=*reinterpret_cast<Preview*>(context);PAINTSTRUCT paint{};HDC dc=BeginPaint(control,&paint);app.drawCheck(control,dc);EndPaint(control,&paint);return 0;
        }
        if(message==WM_NCDESTROY) RemoveWindowSubclass(control,checkProcedure,id);
        return DefSubclassProc(control,message,wp,lp);
    }
    void drawCheck(HWND control,HDC dc) {
        const int savedDc=SaveDC(dc);mapCanvas(dc);
        const auto p=themeColors(settings.darkTheme);RECT bounds{};GetClientRect(control,&bounds);bounds.right=MulDiv(bounds.right,96,dpi);bounds.bottom=MulDiv(bounds.bottom,96,dpi);
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
        if(GetFocus()==control) {InflateRect(&label,-2,-4);DrawFocusRect(dc,&label);}SelectObject(dc,oldFont);RestoreDC(dc,savedDc);
    }
    void drawButton(DRAWITEMSTRUCT item) {
        const int savedDc=SaveDC(item.hDC);mapCanvas(item.hDC);
        item.rcItem.right=MulDiv(item.rcItem.right,96,dpi);item.rcItem.bottom=MulDiv(item.rcItem.bottom,96,dpi);
        const auto p=themeColors(settings.darkTheme);
        const bool primary=item.CtlID==1 || item.CtlID==20 || item.CtlID==22;
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
            text.right=left-4;DrawTextW(item.hDC,canvasWidth<360?L"Тема":L"Серая тема",-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        } else DrawTextW(item.hDC,caption,-1,&text,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
        if(item.itemState&ODS_FOCUS) {RECT focus=item.rcItem;InflateRect(&focus,-4,-4);DrawFocusRect(item.hDC,&focus);}
        SelectObject(item.hDC,oldFont);SelectObject(item.hDC,oldBrush);SelectObject(item.hDC,oldPen);RestoreDC(item.hDC,savedDc);
    }
    void paintHome(HDC dc) {
        const auto p=themeColors(settings.darkTheme);
        auto text=[&](RECT r,const std::wstring& value,COLORREF color,HFONT font=nullptr) {
            SelectObject(dc,font?font:uiFont);SetTextColor(dc,color);
            DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&r,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
        };
        auto card=[&](RECT r,const std::wstring& title) {
            auto brush=SelectObject(dc,GetStockObject(DC_BRUSH)),pen=SelectObject(dc,GetStockObject(DC_PEN));
            SetDCBrushColor(dc,p.card);SetDCPenColor(dc,p.border);RoundRect(dc,r.left,r.top,r.right,r.bottom,16,16);
            SelectObject(dc,brush);SelectObject(dc,pen);
            text({r.left+16,r.top+16,r.right-16,r.top+60},title,p.text,headingFont);
        };
        const auto feedback=currentFeedback();
        card(runCard,std::to_wstring(wizardStep()+1)+L" / 4 · "+feedback.title);
        RECT marker{runCard.left+1,runCard.top+16,runCard.left+5,runCard.bottom-16};
        SetDCBrushColor(dc,feedbackColor(feedback.tone));FillRect(dc,&marker,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        card(windowCard,L"Окно тренировки");card(colorCard,L"Цвет мишени");
        if(windowCard.right-windowCard.left>=340)
            text({windowCard.left+16,windowCard.top+238,windowCard.right-16,windowCard.bottom-12},L"Прицел перемещаете вы. Анализатор проверяет цвет в выбранном окне.",p.muted);
        text({colorCard.left+16,colorCard.top+240,colorCard.right-16,colorCard.top+264},colorReady?L"Выбранный цвет":L"Цвет ещё не выбран",p.muted);
        RECT swatch{colorCard.left+16,colorCard.top+272,colorCard.right-16,colorCard.top+302};
        SetDCBrushColor(dc,colorReady?RGB(color.target.red,color.target.green,color.target.blue):p.input);
        FillRect(dc,&swatch,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        text({colorCard.left+16,colorCard.top+314,colorCard.right-16,colorCard.top+374},L"Выберите оттенок мишени, затем сохраните его. Допуск помогает учитывать близкие оттенки.",p.muted);
        card(stateCard,L"Результат и состояние");
        if(homeDetails) {
            card(optionsCard,L"Профили тренировки");
            text({optionsCard.left+16,optionsCard.top+50,optionsCard.right-16,optionsCard.top+74},profiles.empty()?L"Пока нет сохранённых профилей":L"Сохранённые профили",p.muted);
            text({optionsCard.left+16,optionsCard.top+170,optionsCard.right-16,optionsCard.top+194},L"Название профиля",p.muted);
            text({optionsCard.left+16,optionsCard.top+286,optionsCard.right-16,optionsCard.bottom-12},selectedProfile.empty()?L"Сохраните цвет и параметры для следующего запуска":L"Профиль: "+selectedProfile+(profileChanged()?L" · изменён":L" · сохранён"),p.muted);
            card(qualityCard,L"Мишень и фон");
            for(int i=0;i<2;++i) {
                const auto r=previewCards[i];card(r,i==0?L"Последний снимок":L"Маска найденного цвета");
                const int side=std::min(200,static_cast<int>(r.right-r.left)-32),x=r.left+(r.right-r.left-side)/2,y=r.top+56;
                RECT image{x,y,x+side,y+side};SetDCBrushColor(dc,p.input);FillRect(dc,&image,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                if(displayFrame) {
                    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=displayFrame->width();info.bmiHeader.biHeight=-displayFrame->height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
                    triggerlab::Frame maskFrame(displayFrame->width(),displayFrame->height());
                    if(i==1) for(std::size_t n=0;n<displayMask.pixels.size();++n) {const auto value=displayMask.pixels[n];maskFrame.data()[n]={value,value,value,255};}
                    StretchDIBits(dc,x,y,side,side,0,0,displayFrame->width(),displayFrame->height(),i==0?displayFrame->data():maskFrame.data(),&info,DIB_RGB_COLORS,SRCCOPY);
                    HBRUSH border=CreateSolidBrush(displayFound?p.accent:p.border);FrameRect(dc,&image,border);DeleteObject(border);
                    if(i==0 && displaySettings.center && displaySettings.ignoreRadius>0) {
                        const int width=displayFrame->width(),center=width/2,radius=displaySettings.ignoreRadius;
                        RECT excluded{x+(center-radius)*side/width,y+(center-radius)*side/width,x+(center+radius+1)*side/width,y+(center+radius+1)*side/width};
                        HBRUSH hatch=CreateHatchBrush(HS_DIAGCROSS,p.accent);FillRect(dc,&excluded,hatch);DeleteObject(hatch);
                    }
                } else text({r.left+16,y+44,r.right-16,y+130},L"Снимок появится после запуска проверки",p.muted);
                text({r.left+16,r.top+264,r.right-16,r.bottom-10},displayFrame?L"Сохранённый кадр · "+std::to_wstring((GetTickCount64()-displayTime)/1000)+L" сек. назад":L"Ожидаем первый снимок",p.muted);
            }
        }
        text({24,footerY,canvasWidth-24,footerY+50},saveState(),p.muted);
        text({24,footerY+52,canvasWidth-24,footerY+132},notice,p.muted);
    }
    void paintNarrow(HDC dc) {
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
            text(position(GetDlgItem(window,27)).bottom+10,selectedProfile.empty()?L"Сохраните цвет, область и задержки для этой тренировки":L"Профиль: "+selectedProfile+(profileChanged()?L" · изменён":L" · сохранён"),p.muted,56);
        }
        card(colorCard,L"2. Цвет мишени");
        const int swatchY=position(GetDlgItem(window,20)).top-58;
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
    void paintHeader(HDC dc,int clientWidth) {
        const auto p=themeColors(settings.darkTheme);RECT area{0,0,clientWidth,scaled(headerHeight)};
        SetDCBrushColor(dc,p.card);FillRect(dc,&area,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const int saved=SaveDC(dc);mapCanvas(dc);SelectObject(dc,canvasWidth<620?headingFont:titleFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,p.text);
        RECT title{24,8,210,46};DrawTextW(dc,L"TriggerLab",-1,&title,DT_LEFT|DT_SINGLELINE);
        if(!narrow) {
            const wchar_t* stages[]={L"Окно",L"Цвет",L"Проверка",L"Клики"};const int current=wizardStep();SelectObject(dc,uiFont);
            for(int i=0;i<4;++i) {SetTextColor(dc,i<current?(settings.darkTheme?RGB(134,239,172):RGB(22,163,74)):i==current?p.accent:p.muted);
                const auto value=(i<current?L"✓ ":std::to_wstring(i+1)+L". ")+std::wstring(stages[i]);TextOutW(dc,548+i*132,112,value.c_str(),static_cast<int>(value.size()));}
        }
        SetDCPenColor(dc,p.border);auto pen=SelectObject(dc,GetStockObject(DC_PEN));MoveToEx(dc,0,headerHeight-1,nullptr);LineTo(dc,canvasWidth,headerHeight-1);SelectObject(dc,pen);RestoreDC(dc,saved);
    }
    void paint(HDC target,RECT rect) {
        const auto p=themeColors(settings.darkTheme);
        const int w=rect.right,h=rect.bottom;if(w<=0 || h<=0) return;
        if(!backBuffer || bufferWidth!=w || bufferHeight!=h) {backBuffer=std::make_unique<PaintSurface>(target,w,h);bufferWidth=w;bufferHeight=h;}
        if(!backBuffer->valid()) return;HDC dc=backBuffer->dc;
        SetDCBrushColor(dc,p.background);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));SetBkMode(dc,TRANSPARENT);
        const int savedDc=SaveDC(dc);IntersectClipRect(dc,0,scaled(headerHeight),w,h);mapCanvas(dc,scrollX,scrollY-scaled(headerHeight-((narrow || !settings.advancedMode)?0:84)));
        auto oldFont=SelectObject(dc,uiFont);
        if(!settings.advancedMode) {paintHome(dc);SelectObject(dc,oldFont);RestoreDC(dc,savedDc);paintHeader(dc,w);BitBlt(target,0,0,w,h,dc,0,0,SRCCOPY);return;}
        if(narrow) {paintNarrow(dc);SelectObject(dc,oldFont);RestoreDC(dc,savedDc);paintHeader(dc,w);BitBlt(target,0,0,w,h,dc,0,0,SRCCOPY);return;}
        auto line=[&](int x,int y,const std::wstring& text,COLORREF color=CLR_INVALID,HFONT font=nullptr) {
            SelectObject(dc,font?font:uiFont);SetTextColor(dc,color==CLR_INVALID?p.muted:color);TextOutW(dc,x,y,text.c_str(),static_cast<int>(text.size()));
        };
        auto wrapped=[&](RECT area,const std::wstring& text,COLORREF color) {SelectObject(dc,uiFont);SetTextColor(dc,color);DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&area,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);};
        auto card=[&](int left,int top,int right,int bottom) {
            auto b=SelectObject(dc,GetStockObject(DC_BRUSH));auto oldPen=SelectObject(dc,GetStockObject(DC_PEN));
            SetDCBrushColor(dc,p.card);SetDCPenColor(dc,p.border);RoundRect(dc,left,top,right,bottom,16,16);SelectObject(dc,b);SelectObject(dc,oldPen);
        };
        line(24,14,L"TriggerLab",p.text,titleFont);
        line(210,26,settings.advancedMode?L"Расширенные настройки":L"Пошаговая настройка");
        const wchar_t* steps[]={L"Окно",L"Цвет мишени",L"Проверка",L"Клики"};
        const int step=wizardStep();
        for(int i=0;i<4;++i) line(24+i*270,62,(i<step?L"✓ ":std::to_wstring(i+1)+L". ")+std::wstring(steps[i]),i<step?(settings.darkTheme?RGB(134,239,172):RGB(22,163,74)):i==step?p.accent:p.muted);
        card(20,84,488,242);card(508,84,1082,242);card(20,260,1082,406);card(20,424,1082,584);
        line(40,100,L"1. Окно и режим обнаружения",p.text,headingFont);
        line(548,100,settings.advancedMode?L"Проверка и запуск":L"Следующий шаг",p.text,headingFont);
        const bool parameters=settings.advancedMode && !profilePane;
        line(40,276,parameters?L"Область и время срабатывания":L"Профиль тренировки",p.text,headingFont);
        if(!parameters) {
            line(40,300,profiles.empty()?L"Пока нет сохранённых профилей":L"Сохранённые профили");line(560,300,L"Название сценария / тренировки");
            wrapped({40,370,825,392},selectedProfile.empty()?L"Профиль хранит цвет, область и время срабатывания":L"Профиль: "+selectedProfile+(profileChanged()?L" · изменён":L" · сохранён"),p.muted);
        }
        wrapped({548,130,1062,180},currentFeedback().detail,p.muted);
        line(40,436,L"2. Цвет мишени",p.text,headingFont);
        line(800,442,L"Текущий цвет");
        HBRUSH swatch=CreateSolidBrush(RGB(color.target.red,color.target.green,color.target.blue));RECT patch{800,482,1062,512};FillRect(dc,&patch,swatch);DeleteObject(swatch);
        card(20,602,1082,788);line(40,620,L"Проверка качества цвета",p.text,headingFont);
        const int previewDc=SaveDC(dc);OffsetViewportOrgEx(dc,0,scaled(210),nullptr);
        card(20,602,360,882);card(380,602,720,882);card(740,602,1082,938);
        line(40,620,L"Область прицела",p.text,headingFont);line(400,620,L"Совпадения цвета",p.text,headingFont);
        line(760,620,L"Состояние",p.text,headingFont);
        RECT image{40,652,340,852},maskRect{400,652,700,852};SetDCBrushColor(dc,p.input);FillRect(dc,&image,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));FillRect(dc,&maskRect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if(displayFrame) {
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=displayFrame->width();info.bmiHeader.biHeight=-displayFrame->height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            const int side=200;const int x=90,mx=450;
            StretchDIBits(dc,x,652,side,side,0,0,displayFrame->width(),displayFrame->height(),displayFrame->data(),&info,DIB_RGB_COLORS,SRCCOPY);
            triggerlab::Frame maskFrame(displayFrame->width(),displayFrame->height());
            for(std::size_t i=0;i<displayMask.pixels.size();++i) {auto value=displayMask.pixels[i];maskFrame.data()[i]={value,value,value,255};}
            StretchDIBits(dc,mx,652,side,side,0,0,maskFrame.width(),maskFrame.height(),maskFrame.data(),&info,DIB_RGB_COLORS,SRCCOPY);
            HBRUSH outline=CreateSolidBrush(displayFound?(settings.darkTheme?RGB(134,239,172):RGB(22,163,74)):p.accent);
            RECT roiBorder{x,652,x+side,852};FrameRect(dc,&roiBorder,outline);DeleteObject(outline);
            if(displaySettings.center && displaySettings.ignoreRadius>0) {
                const int width=displayFrame->width(),center=width/2,radius=displaySettings.ignoreRadius;
                RECT excluded{x+(center-radius)*side/width,652+(center-radius)*side/width,x+(center+radius+1)*side/width,652+(center+radius+1)*side/width};
                HBRUSH hatch=CreateHatchBrush(HS_DIAGCROSS,settings.darkTheme?RGB(255,204,143):RGB(180,83,9));FillRect(dc,&excluded,hatch);FrameRect(dc,&excluded,hatch);DeleteObject(hatch);
            }
        } else {wrapped({65,720,320,780},L"Начните захват и активируйте выбранное окно",p.muted);wrapped({420,720,685,780},L"Белым показан найденный цвет",p.muted);}
        RECT badge{760,651,1062,680};SetDCBrushColor(dc,p.input);FillRect(dc,&badge,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        line(770,656,settings.dryRun?L"ПРОВЕРКА · без кликов":L"РЕАЛЬНЫЕ КЛИКИ",settings.dryRun?p.accent:settings.darkTheme?RGB(255,204,143):RGB(180,83,9));
        wrapped({760,693,1062,734},nextHint(),running?p.accent:p.muted);
        const auto elapsed=gate.elapsed(triggerlab::Clock::now()).count();
        line(760,742,!sample.frame?(displayFrame?L"На паузе · прошлый кадр":L"Ожидаем первый снимок"):autoClick && colorFound() && !gate.clickedHover()?L"Ожидание: "+std::to_wstring(std::min<std::int64_t>(elapsed,settings.dwellMs))+L" / "+std::to_wstring(settings.dwellMs)+L" мс":colorFound()?L"✓ Цвет найден":L"Цвет пока не найден",colorFound()?(settings.darkTheme?RGB(134,239,172):RGB(22,163,74)):p.muted);
        RECT waiting{760,683,1062,687};SetDCBrushColor(dc,p.border);FillRect(dc,&waiting,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        waiting.right=waiting.left+static_cast<LONG>(302*std::clamp(double(elapsed)/settings.dwellMs,0.0,1.0));SetDCBrushColor(dc,p.accent);FillRect(dc,&waiting,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        wrapped({760,771,1062,811},clickStatus,p.text);
        line(760,817,L"Клики: "+std::to_wstring(clickCount)+L"   Проверки: "+std::to_wstring(diagnosticCount));
        line(760,845,sample.frame?L"Найдено: "+std::to_wstring(static_cast<int>(mask.coverage*100))+L"%  /  порог "+std::to_wstring(settings.minimumPercent)+L"%":L"Свежего кадра пока нет");
        RECT track{760,871,1062,875};SetDCBrushColor(dc,p.border);FillRect(dc,&track,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        track.right=track.left+static_cast<LONG>(302*std::clamp(mask.coverage,0.0,1.0));SetDCBrushColor(dc,p.accent);FillRect(dc,&track,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        line(40,858,displayFrame?L"Снимок: "+std::to_wstring((GetTickCount64()-displayTime)/1000)+L" сек. назад":L"Снимок появится после запуска");
        line(400,858,settings.center?L"Перекрестие исключено":L"Проверка пикселя курсора");
        wrapped({760,886,1062,934},pressTimingText(),p.muted);
        line(24,950,saveState(),settingsDirty() || colorDirty()?(settings.darkTheme?RGB(255,204,143):RGB(180,83,9)):p.muted);
        wrapped({24,974,1062,994},notice,p.muted);
        line(860,950,L"F9 / Esc — остановить");
        RestoreDC(dc,previewDc);SelectObject(dc,oldFont);RestoreDC(dc,savedDc);paintHeader(dc,w);BitBlt(target,0,0,w,h,dc,0,0,SRCCOPY);
    }
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<Preview*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {auto* create=reinterpret_cast<CREATESTRUCTW*>(lp); app=static_cast<Preview*>(create->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));return TRUE;}
    try {
        switch(message) {
            case WM_CREATE:app->createControls();return 0;
            case WM_COMMAND:if(HIWORD(wp)==EN_CHANGE) app->onEditChange(reinterpret_cast<HWND>(lp));else app->command(LOWORD(wp),HIWORD(wp));return 0;
            case WM_TIMER:
                if(wp==3) {if(app->syntheticDown && !app->autoClick) app->finishClick(false);app->refreshTargetButton();app->syncActions();InvalidateRect(window,nullptr,FALSE);return 0;}
                if(wp==2) app->selectionTick(GetTickCount64());
                else if(wp==1) app->update();return 0;
            case WM_HOTKEY:if(wp==9) app->stop();return 0;
            case WM_KEYDOWN:
                if(wp==VK_ESCAPE) app->stop();
                else if(wp=='C') app->update(true);
                else if(wp=='H' && !(lp & (1LL<<30))) {app->cancelSelection();app->disableClicks();app->verified=false;app->color.space=app->color.space==triggerlab::ColorSpace::RGB?triggerlab::ColorSpace::HSV:triggerlab::ColorSpace::RGB;app->notice=L"Режим цвета изменён. Нажмите «Сохранить всё»";app->syncActions();app->update();InvalidateRect(window,nullptr,FALSE);}return 0;
            case WM_DRAWITEM:if(app && wp) {app->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}break;
            case WM_SIZE:app->arrange();return 0;
            case WM_DPICHANGED: {
                app->setDpi(HIWORD(wp));const auto* r=reinterpret_cast<RECT*>(lp);
                SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->arrange();return 0;
            }
            case WM_HSCROLL:case WM_VSCROLL:app->scroll(message==WM_HSCROLL?SB_HORZ:SB_VERT,LOWORD(wp));return 0;
            case WM_MOUSEWHEEL: {
                app->wheelRemainder+=GET_WHEEL_DELTA_WPARAM(wp);
                const int steps=app->wheelRemainder/WHEEL_DELTA;app->wheelRemainder%=WHEEL_DELTA;
                if(wp&MK_SHIFT) app->scrollX-=steps*app->scaled(108);else app->scrollY-=steps*app->scaled(108);
                app->arrange();return 0;
            }
            case WM_GETMINMAXINFO: {auto* size=reinterpret_cast<MINMAXINFO*>(lp);size->ptMinTrackSize={480,360};if(app && app->allowOversizedTestWindow) size->ptMaxTrackSize={10000,10000};return 0;}
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN: {
                const auto p=themeColors(app->settings.darkTheme);HDC dc=reinterpret_cast<HDC>(wp);
                if(reinterpret_cast<HWND>(lp)==app->headerStatus || reinterpret_cast<HWND>(lp)==app->homeHint) {SetTextColor(dc,app->feedbackColor(app->currentFeedback().tone));SetBkColor(dc,p.card);return reinterpret_cast<LRESULT>(app->cardBrush);}
                const bool input=message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX || reinterpret_cast<HWND>(lp)==app->modeControl;
                SetTextColor(dc,p.text);SetBkColor(dc,input?p.input:p.card);
                return reinterpret_cast<LRESULT>(input?app->inputBrush:app->cardBrush);
            }
            case WM_ERASEBKGND:return 1;
            case WM_PAINT: {PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);try {app->paint(dc,rect);} catch(...) {EndPaint(window,&paint);throw;} EndPaint(window,&paint);return 0;}
            case WM_CLOSE:
                app->stop();
                if(app->syntheticDown) {
                    MessageBoxW(window,L"Windows пока отклоняет отпускание кнопки. Дождитесь восстановления ввода и повторите закрытие.",L"TriggerLab",MB_OK|MB_ICONWARNING);
                    return 0;
                }
                DestroyWindow(window);return 0;
            case WM_DESTROY:KillTimer(window,3);app->stop();PostQuitMessage(0);return 0;
        }
    } catch(const std::exception&) {app->stop();}
    return DefWindowProcW(window,message,wp,lp);
}
}
#include "PreviewIntegrationTests.inl"
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR commandLine,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        const std::wstring option(commandLine);
        const bool firstRun=option==L"--first-run-test";
        const bool recovery=option==L"--input-recovery-test";
        const bool isolated=firstRun || recovery || option==L"--window-picker-test" || option==L"--smoke-test" || option==L"--click-test" || option==L"--center-click-test" || option==L"--guided-click-test" || option==L"--reference-test" || option==L"--pulse-test";
        const auto testRoot=isolated?std::filesystem::temp_directory_path()/(L"triggerlab-first-run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(triggerlab::Clock::now().time_since_epoch().count())):std::filesystem::path{};
        Preview app(testRoot,recovery?recoverySender:SendInput);WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"TriggerLabCapturePreviewClass";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc)) {triggerlab::reportStartupError(L"RegisterClass");return 1;}
        HWND window=CreateWindowExW(WS_EX_COMPOSITED,wc.lpszClassName,L"TriggerLab — тренировки Kovaak’s / Aimlabs",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1120,980,nullptr,nullptr,instance,&app);
        if(!window) {triggerlab::reportStartupError(L"CreateWindow");return 1;}
        if(recovery) {const bool passed=app.inputRecoveryTest();DestroyWindow(window);std::filesystem::remove_all(testRoot);return passed?0:2;}
        if(firstRun) {
            ShowWindow(window,SW_SHOW);
            const bool passed=app.firstRunTest();DestroyWindow(window);std::filesystem::remove_all(testRoot);return passed?0:2;
        }
        if(option==L"--window-picker-test") {const bool passed=app.windowPickerTest();DestroyWindow(window);std::filesystem::remove_all(testRoot);return passed?0:2;}
        if(option==L"--reference-test") {const int result=referenceCaptureTest(app);DestroyWindow(window);std::filesystem::remove_all(testRoot);return result;}
        if(option==L"--pulse-test") {const int result=liveClickTest(app,true,false,true);DestroyWindow(window);std::filesystem::remove_all(testRoot);return result;}
        if(option==L"--guided-click-test") {const int result=liveClickTest(app,true,true);DestroyWindow(window);std::filesystem::remove_all(testRoot);return result;}
        if(std::wstring(commandLine)==L"--click-test") {const int result=liveClickTest(app);DestroyWindow(window);std::filesystem::remove_all(testRoot);return result;}
        if(std::wstring(commandLine)==L"--center-click-test") {const int result=liveClickTest(app,true);DestroyWindow(window);std::filesystem::remove_all(testRoot);return result;}
        if(std::wstring(commandLine)==L"--smoke-test") {
            const auto path=testRoot/L"selected-color.json";
            const bool passed=app.smokeTest(path);DestroyWindow(window);std::filesystem::remove_all(path.parent_path());return passed?0:2;
        }
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
            const RECT work=monitor.rcWork;
            const int width=std::min(app.scaled(1120),static_cast<int>(work.right-work.left));
            const int height=std::min(app.scaled(1000),static_cast<int>(work.bottom-work.top));
            SetWindowPos(window,nullptr,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        }
        SetWindowTextW(window,L"TriggerLab — тренировки Kovaak’s / Aimlabs");
        ShowWindow(window,show);MSG message{};BOOL status;
        while((status=GetMessageW(&message,nullptr,0,0))>0) {if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE) {app.stop();continue;}if(!IsDialogMessageW(window,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}}if(status<0) triggerlab::reportStartupError(L"GetMessage");return status<0?1:0;
    } catch(const std::exception& error) {triggerlab::reportStartupException(error.what());return 1;}
}
