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

void PreviewWindow::finishClick(bool count) {
        if(!count) inputPulse.cancel();
        auto completed=inputPulse.takeResult();if(!completed) return;
        syntheticDown=inputPulse.pendingRelease();lastPress=*completed;
        // A pulse already released by the worker still counts if Stop arrives
        // before the UI consumes completion. Only an actual cancellation is partial.
        if(completed->releaseAccepted && !completed->interrupted) {++clickCount;clickStatus=L"Клик отправлен: нажатие и отпускание";}
        if(!completed->releaseAccepted) {autoClick=false;gate.reset();SendMessageW(autoControl,BM_SETCHECK,BST_UNCHECKED,0);notice=clickStatus=L"Отпускание отклонено системой; новые клики заблокированы, Stop/F9 повторяет отпускание";}
        if(!syntheticDown && !autoClick && !pickDeadline && !targetDeadline && !aimPickDeadline) UnregisterHotKey(window,hotkey::stop);
    }

void PreviewWindow::disableClicks() {finishClick(false);if(!syntheticDown) UnregisterHotKey(window,hotkey::stop);autoClick=false;gate.reset();clickStatus=syntheticDown?L"Отпускание не подтверждено; новые клики заблокированы":L"Выключен";if(autoControl) SendMessageW(autoControl,BM_SETCHECK,BST_UNCHECKED,0);}

bool PreviewWindow::permittedAt(POINT point) const {
        DWORD process=0;if(!targetWindow || !IsWindow(targetWindow)) return false;
        GetWindowThreadProcessId(targetWindow,&process);
        return triggerlab::ClickPermission{reinterpret_cast<std::uintptr_t>(targetWindow),reinterpret_cast<std::uintptr_t>(GetAncestor(WindowFromPoint(point),GA_ROOT)),reinterpret_cast<std::uintptr_t>(GetAncestor(GetForegroundWindow(),GA_ROOT)),targetProcess,process,true}.allowed();
    }

bool PreviewWindow::permittedRegion(triggerlab::Region region) const {
        if(!triggerlab::validRegion(region)) return false;
        POINT center{static_cast<LONG>(region.x+region.width/2),static_cast<LONG>(region.y+region.height/2)};
        return permittedAt(center) && triggerlab::isWindowRegionVisible(reinterpret_cast<std::uintptr_t>(targetWindow),region);
    }

void PreviewWindow::handleClick(POINT capturedCursor) {
        if(!autoClick) return;
        if(!inputAuthorized()) {disableClicks();notice=L"Сохраните настройки и проверьте цвет перед включением кликов";return;}
        if(!IsWindow(targetWindow)) {disableClicks();status=L"Автоклик выключен: целевое окно закрыто";return;}
        if(inputHeld(!syntheticDown)) {gate.step(false,false,triggerlab::Clock::now());clickStatus=L"Пауза: зажата кнопка мыши или клавиша-модификатор";return;}
        if(!permittedAt(capturedCursor)) {
            // A focus change is not evidence that the cursor left the color.
            const bool outside=GetAncestor(WindowFromPoint(capturedCursor),GA_ROOT)!=targetWindow;
            gate.step(sample.frame.has_value() && outside,false,triggerlab::Clock::now());
            clickStatus=outside?L"Курсор вне выбранного окна":L"Выбранное окно не активно: активируйте его";return;
        }
        const bool match=settings.center?mask.found:(sample.cursorPixel && triggerlab::matchesColor(*sample.cursorPixel,color));
        const auto now=triggerlab::Clock::now();
        if(syntheticDown) {
            // Observe a short target departure even while our own Down is held.
            // Only the pulse owner may release it; never submit another Down here.
            gate.step(sample.frame.has_value(),match,now);clickStatus=L"Нажатие выполняется";return;
        }
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
            gate.acknowledgeClick(triggerlab::Clock::now());syntheticDown=true;clickStatus=L"Нажатие выполняется";
        } else {const auto error=GetLastError();disableClicks();status=L"Ввод отклонён системой (код "+std::to_wstring(error)+L")";clickStatus=status;notice=status;actionError=status+L". Проверьте доступность выбранного окна и повторите проверку цвета.";}
    }

bool PreviewWindow::inputHeld(bool includeLeftButton) {
        for(int key:{VK_CONTROL,VK_MENU,VK_SHIFT,VK_LWIN,VK_RWIN,VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2})
            if((includeLeftButton || key!=VK_LBUTTON) && (GetAsyncKeyState(key)&0x8000)) return true;
        return false;
    }

void PreviewWindow::stop() {
        const bool wasRunning=running;
        const auto previousResult=clickStatus;
        running=false;disableClicks();cancelSelection();KillTimer(window,timer::capture);capture.reset();sample={};mask={};actionError.clear();
        if(!syntheticDown) {
            if(wasRunning) clickStatus=L"Кадры: "+std::to_wstring(runFrames)+L"; в центре: "+std::to_wstring(runAlignedFrames)+
                L"; недоступны: "+std::to_wstring(runUnavailableFrames)+L"; макс. цвет: "+std::to_wstring(static_cast<int>(runBestCoverage*100))+L"%"+
                (runLastIssue.empty()?L"":L"\nПоследняя пауза: "+runLastIssue);
            else clickStatus=previousResult; // Repeated F9 must not erase the completed run.
        }
        status=syntheticDown?L"Захват остановлен; ожидаем подтверждения отпускания кнопки":L"Захват остановлен";notice=status;syncActions();InvalidateRect(window,nullptr,FALSE);
    }

} // namespace triggerlab::win32
