#include "preview/PreviewWindow.hpp"
#include "triggerlab/ImageFile.hpp"
#include "ImagePaletteDialog.hpp"
#include <algorithm>
#include <commdlg.h>
#include <cwctype>
#include <utility>

namespace triggerlab::win32 {

void PreviewWindow::clearReferences() {
    targetReference.reset();
    backgroundReference.reset();
    referenceWindow = nullptr;
    referenceProcess = 0;
}

bool PreviewWindow::referencesCurrent() const {
    return referenceWindow && referenceWindow == targetWindow && IsWindow(targetWindow) &&
           referenceProcess == targetProcess;
}

bool PreviewWindow::recordReference(triggerlab::CaptureSample fresh, SnapshotPurpose purpose) {
    if (purpose == SnapshotPurpose::Color || fresh.status != triggerlab::CaptureStatus::Ok || !fresh.frame ||
        fresh.allBlack || !settings.center || !targetWindow || !IsWindow(targetWindow) ||
        fresh.frame->width() != settings.side || fresh.frame->height() != settings.side)
        return false;
    if (!referencesCurrent())
        clearReferences();
    referenceWindow = targetWindow;
    referenceProcess = targetProcess;
    if (purpose == SnapshotPurpose::Target)
        targetReference = std::move(fresh.frame);
    else
        backgroundReference = std::move(fresh.frame);
    return true; // Reference images never enter sample/displayFrame or the click gate.
}

std::optional<triggerlab::Region> PreviewWindow::clientBounds() const {
    if (!targetWindow || !IsWindow(targetWindow) || IsIconic(targetWindow))
        return {};
    RECT rect{};
    POINT origin{};
    if (!GetClientRect(targetWindow, &rect) || !ClientToScreen(targetWindow, &origin) || rect.right <= 0 ||
        rect.bottom <= 0)
        return {};
    return triggerlab::Region{origin.x, origin.y, rect.right, rect.bottom};
}

void PreviewWindow::findTrainer(const wchar_t *name) {
    verified = false;
    cancelSelection();
    disableClicks();
    struct Search {
        const wchar_t *name;
        HWND own;
        std::vector<HWND> found;
    } search{name, window, {}};
    EnumWindows(
        [](HWND candidate, LPARAM parameter) -> BOOL {
            auto &search = *reinterpret_cast<Search *>(parameter);
            if (candidate == search.own || !IsWindowVisible(candidate))
                return TRUE;
            DWORD processId = 0;
            GetWindowThreadProcessId(candidate, &processId);
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
            if (!process)
                return TRUE;
            wchar_t path[32768]{};
            DWORD length = 32768;
            const bool readable = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
            CloseHandle(process);
            if (!readable)
                return TRUE;
            auto executable = std::filesystem::path(path).filename().wstring();
            for (auto &c : executable)
                c = static_cast<wchar_t>(std::towlower(c));
            const bool kovaak = std::wstring_view(search.name) == L"kovaak";
            if (executable.find(kovaak ? L"fpsaimtrainer" : L"aimlab") == std::wstring::npos)
                return TRUE;
            wchar_t title[512]{};
            GetWindowTextW(candidate, title, 512);
            std::wstring text(title);
            for (auto &c : text)
                c = static_cast<wchar_t>(std::towlower(c));
            text.erase(
                std::remove_if(text.begin(), text.end(), [](wchar_t c) { return std::iswspace(c) != 0; }),
                text.end());
            if (text.find(search.name) != std::wstring::npos)
                search.found.push_back(candidate);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    if (search.found.size() != 1) {
        status = search.found.empty() ? L"Тренажёр не запущен. Откройте его, затем повторите поиск"
                                      : L"Найдено несколько окон: используйте ручной выбор";
        return;
    }
    clearReferences();
    displayFrame.reset();
    displayMask = {};
    targetWindow = search.found.front();
    GetWindowThreadProcessId(targetWindow, &targetProcess);
    refreshTargetButton();
    settings.center = true;
    syncSettings();
    status = L"Тренажёр выбран. Начните с проверки без кликов";
}

void PreviewWindow::cancelSelection() {
    pickDeadline = targetDeadline = aimPickDeadline = 0;
    snapshotPurpose = SnapshotPurpose::Color;
    KillTimer(window, timer::selection);
    if (!autoClick && !syntheticDown)
        UnregisterHotKey(window, hotkey::stop);
    SetWindowTextW(GetDlgItem(window, control::pickCursorColor), L"Пипетка (3 сек.)");
    refreshTargetButton();
    SetWindowTextW(GetDlgItem(window, control::pickAimColor), L"Цвет из прицела (3 сек.)");
    SetWindowTextW(GetDlgItem(window, control::targetReference), L"Снять мишень (3 сек.)");
    SetWindowTextW(GetDlgItem(window, control::backgroundReference), L"Снять фон (3 сек.)");
}

void PreviewWindow::beginAimSelection(SnapshotPurpose purpose) {
    cancelSelection();
    disableClicks();
    if (purpose != SnapshotPurpose::Color && (!colorReady || settingsDirty() || colorDirty())) {
        status = notice = L"Выберите цвет и сохраните изменения перед съёмкой эталона";
        return;
    }
    if (!settings.center || !clientBounds()) {
        status = L"Выберите окно и режим «Вокруг прицела окна»";
        return;
    }
    DWORD process = 0;
    GetWindowThreadProcessId(targetWindow, &process);
    if (process != targetProcess || !IsWindowVisible(targetWindow) || !IsWindowEnabled(targetWindow)) {
        status = notice = L"Выбранное окно недоступно для активации";
        return;
    }
    if (!RegisterHotKey(window, hotkey::stop, MOD_NOREPEAT, VK_F9)) {
        status = L"F9 занят: снимок не начат";
        actionError = status + L". Освободите F9 в другом приложении и повторите действие.";
        return;
    }
    if (!SetTimer(window, timer::selection, 100, nullptr)) {
        UnregisterHotKey(window, hotkey::stop);
        status = L"Ошибка таймера: снимок не начат";
        return;
    }
    if (GetAncestor(GetForegroundWindow(), GA_ROOT) != targetWindow && !SetForegroundWindow(targetWindow) &&
        GetAncestor(GetForegroundWindow(), GA_ROOT) != targetWindow) {
        cancelSelection();
        status = notice = L"Не удалось активировать окно. Активируйте тренировку вручную и повторите снимок";
        return;
    }
    // Start the full aiming interval after requesting foreground activation.
    // Completion still checks focus, geometry and a fresh capture.
    snapshotPurpose = purpose;
    aimPickDeadline = GetTickCount64() + 3000;
    status = notice = purpose == SnapshotPurpose::Background
                          ? L"Окно активировано. Наведите прицел на фон без мишени за 3 сек.; F9 — отмена"
                          : L"Окно активировано. Наведите прицел на мишень за 3 сек.; F9 — отмена";
}

void PreviewWindow::finishAimSelection() {
    const auto purpose = snapshotPurpose;
    cancelSelection();
    disableClicks();
    const auto bounds = clientBounds();
    const auto roi = bounds ? triggerlab::aimRegion(*bounds, settings) : std::optional<triggerlab::Region>{};
    if (!roi || !permittedRegion(*roi)) {
        status = notice = L"Снимок отменён: область недоступна или тренажёр не активен";
        return;
    }
    auto fresh = capture.capture(*roi);
    const auto currentBounds = clientBounds();
    if (fresh.allBlack) {
        status = notice =
            L"Снимок полностью чёрный. Проверьте режим окна; предыдущий эталон и цвет сохранены";
        return;
    }
    if (!fresh.frame || fresh.allBlack || !currentBounds || currentBounds->x != bounds->x ||
        currentBounds->y != bounds->y || currentBounds->width != bounds->width ||
        currentBounds->height != bounds->height || !permittedRegion(*roi) || fresh.region.x != roi->x ||
        fresh.region.y != roi->y || fresh.region.width != roi->width || fresh.region.height != roi->height) {
        status = notice = L"Снимок недоступен; прежний цвет сохранён";
        return;
    }
    // Freeze a fresh image before the modal steals focus or timer messages arrive.
    running = false;
    KillTimer(window, timer::capture);
    sample = {};
    mask = {};
    analysisPoint.reset();
    capture.reset();
    if (purpose != SnapshotPurpose::Color) {
        const bool recorded = recordReference(std::move(fresh), purpose);
        status = notice =
            recorded ? (purpose == SnapshotPurpose::Target ? L"Эталон мишени снят. Теперь снимите фон."
                                                           : L"Эталон фона снят. Оценка обновлена.")
                     : L"Эталон недоступен; предыдущий снимок сохранён";
        syncActions();
        SetForegroundWindow(window);
        SetFocus(window);
        return;
    }
    const auto selected = triggerlab::showImagePalette(window, *fresh.frame, settings.darkTheme);
    if (selected) {
        color.target = selected->color;
        color.space = triggerlab::ColorSpace::RGB;
        syncColor();
        status = L"Цвет мишени выбран. Нажмите «Сохранить всё», затем начните проверку без кликов";
    } else
        status = L"Выбор отменён; прежний цвет сохранён";
    notice = status;
}

bool PreviewWindow::importImage(const std::filesystem::path &path, bool edit) {
    cancelSelection();
    disableClicks();
    auto loaded = triggerlab::loadImagePixels(path);
    if (!loaded.image) {
        status = loaded.error;
        notice = status;
        return false;
    }
    auto selected = edit ? triggerlab::showImagePalette(window, *loaded.image, settings.darkTheme)
                         : triggerlab::dominantImageColor(*loaded.image);
    if (!selected) {
        status =
            edit ? L"Выбор цвета отменён; прежний цвет сохранён" : L"В изображении нет непрозрачных пикселей";
        notice = status;
        return false;
    }
    color.target = selected->color;
    color.space = triggerlab::ColorSpace::RGB;
    syncColor();
    importedImage = std::move(loaded.image);
    mask = sample.frame ? triggerlab::makeColorMask(*sample.frame, color) : triggerlab::ColorMask{};
    status = L"Цвет из файла определён. Нажмите «Сохранить всё»";
    notice = L"Файл: " + path.filename().wstring().substr(0, 30) + L" | Доля оттенка: " +
             std::to_wstring(static_cast<int>(selected->coverage * 100)) + L"%";
    return true;
}

void PreviewWindow::editImage() {
    cancelSelection();
    disableClicks();
    if (!importedImage) {
        status = L"Сначала загрузите изображение кнопкой «Из файла…»";
        return;
    }
    const auto selected = triggerlab::showImagePalette(window, *importedImage, settings.darkTheme);
    if (!selected) {
        status = L"Выбор отменён; прежний цвет сохранён";
        return;
    }
    color.target = selected->color;
    color.space = triggerlab::ColorSpace::RGB;
    syncColor();
    mask = sample.frame ? triggerlab::makeColorMask(*sample.frame, color) : triggerlab::ColorMask{};
    status = L"Цвет области выбран. Нажмите «Сохранить всё»";
}

void PreviewWindow::chooseImage() {
    cancelSelection();
    disableClicks();
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"Изображения (PNG, JPEG, BMP, GIF, "
                         L"TIFF)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0Все файлы\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle = L"Определить цвет из изображения";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog))
        importImage(path, true);
    else {
        status = CommDlgExtendedError() ? L"Не удалось открыть выбор файла" : L"Выбор изображения отменён";
        notice = status;
    }
}

void PreviewWindow::refreshTargetButton() {
    if (targetWindow) {
        DWORD process = 0;
        GetWindowThreadProcessId(targetWindow, &process);
        if (!IsWindow(targetWindow) || process != targetProcess) {
            clearReferences();
            targetWindow = nullptr;
            targetProcess = 0;
            targetTitle.clear();
            if (autoClick)
                disableClicks(); // Keep the selection's F9 registration alive.
            notice = L"Выбранное окно закрыто";
        } else {
            wchar_t title[256]{};
            GetWindowTextW(targetWindow, title, 256);
            targetTitle = title[0] ? title : L"Без названия";
        }
    }
    std::wstring caption = L"Окно не выбрано\nВыбрать из списка…";
    if (targetDeadline) {
        const auto now = GetTickCount64();
        const auto seconds = now < targetDeadline ? (targetDeadline - now + 999) / 1000 : 0;
        caption = L"Выбор окна: " + std::to_wstring(seconds) + L" сек.\nНаведите курсор";
    } else if (targetWindow) {
        auto title = targetTitle;
        if (title.size() > 25) {
            title.resize(24);
            if (title.back() >= 0xD800 && title.back() <= 0xDBFF)
                title.pop_back();
            title += L"…";
        }
        caption = L"Окно: " + title + L"\nСменить окно…";
    }
    HWND button = GetDlgItem(window, control::chooseWindow);
    wchar_t previous[256]{};
    GetWindowTextW(button, previous, 256);
    if (caption != previous) {
        SetWindowTextW(button, caption.c_str());
        InvalidateRect(window, nullptr, FALSE);
    }
}

bool PreviewWindow::applyWindowChoice(const triggerlab::WindowChoice &choice) {
    stop();
    if (!triggerlab::windowChoiceAvailable(choice, window)) {
        status = notice = L"Окно уже закрыто или недоступно. Прежний выбор сохранён";
        return false;
    }
    clearReferences();
    displayFrame.reset();
    displayMask = {};
    analysisPoint.reset();
    analyzedClient.reset();
    targetWindow = choice.window;
    targetProcess = choice.process;
    verified = false;
    settings.dryRun = true;
    SendMessageW(dryControl, BM_SETCHECK, BST_CHECKED, 0);
    refreshTargetButton();
    status = notice = L"Окно выбрано. Начните с проверки без кликов";
    syncActions();
    return true;
}

void PreviewWindow::chooseTargetWindow() {
    stop();
    const auto picked = triggerlab::showWindowPicker(window, settings.darkTheme, targetWindow);
    if (picked.choice)
        applyWindowChoice(*picked.choice);
    else if (picked.manual)
        beginSelection(false);
    else {
        status = notice = picked.error.empty() ? L"Выбор отменён; прежнее окно сохранено" : picked.error;
        actionError = picked.error;
    }
    syncActions();
    InvalidateRect(window, nullptr, FALSE);
}

void PreviewWindow::beginSelection(bool pick) {
    cancelSelection();
    disableClicks();
    if (!RegisterHotKey(window, hotkey::stop, MOD_NOREPEAT, VK_F9)) {
        status = L"F9 занят: выбор не начат";
        actionError = status + L". Освободите F9 в другом приложении и повторите действие.";
        return;
    }
    if (!SetTimer(window, timer::selection, 100, nullptr)) {
        UnregisterHotKey(window, hotkey::stop);
        status = L"Ошибка таймера: выбор не начат";
        return;
    }
    if (pick)
        pickDeadline = GetTickCount64() + 3000;
    else
        targetDeadline = GetTickCount64() + 3000;
    status = pick ? L"Наведите курсор на цвет. Через 3 сек. — снимок; F9 — отмена"
                  : L"3 секунды: наведите курсор на целевое окно; F9 — отмена";
    selectionTick(GetTickCount64());
}

void PreviewWindow::selectionTick(ULONGLONG now) {
    if (aimPickDeadline && now >= aimPickDeadline) {
        finishAimSelection();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (aimPickDeadline) {
        const auto caption =
            L"Снимок прицела: " + std::to_wstring((aimPickDeadline - now + 999) / 1000) + L" сек.";
        SetWindowTextW(GetDlgItem(window, snapshotPurpose == SnapshotPurpose::Color    ? 20
                                          : snapshotPurpose == SnapshotPurpose::Target ? 31
                                                                                       : 32),
                       caption.c_str());
    }
    if (pickDeadline && now >= pickDeadline) {
        cancelSelection();
        update(true);
        return;
    }
    if (targetDeadline && now >= targetDeadline) {
        cancelSelection();
        finishTargetSelection();
        notice = status;
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (pickDeadline) {
        const auto caption = L"Пипетка: " + std::to_wstring((pickDeadline - now + 999) / 1000) + L" сек.";
        wchar_t previous[128]{};
        GetWindowTextW(GetDlgItem(window, control::pickCursorColor), previous, 128);
        if (caption != previous)
            SetWindowTextW(GetDlgItem(window, control::pickCursorColor), caption.c_str());
    }
    refreshTargetButton();
}

void PreviewWindow::finishTargetSelection() {
    POINT point{};
    if (!GetCursorPos(&point)) {
        status = L"Курсор недоступен: выбор окна отменён";
        return;
    }
    HWND selected = GetAncestor(WindowFromPoint(point), GA_ROOT);
    if (!selected || selected == window || !IsWindowVisible(selected)) {
        status = L"Выбор отменён; прежнее окно сохранено";
        refreshTargetButton();
        return;
    }
    clearReferences();
    targetWindow = selected;
    verified = false;
    displayFrame.reset();
    displayMask = {};
    GetWindowThreadProcessId(selected, &targetProcess);
    wchar_t title[256]{};
    GetWindowTextW(selected, title, 256);
    targetTitle = title;
    status = L"Окно выбрано. Включите автоклик отдельно";
    refreshTargetButton();
}

void PreviewWindow::update(bool pick) {
    if (!running && !pick)
        return;
    if (syntheticDown) {
        if (!autoClick || GetAncestor(GetForegroundWindow(), GA_ROOT) != targetWindow) {
            disableClicks();
            return;
        }
        finishClick();
        if (!autoClick)
            return;
    }
    if (pick) {
        cancelSelection();
        disableClicks();
    }
    analysisPoint.reset();
    analyzedClient.reset();
    if (pick || !settings.center)
        acceptSample(capture.captureCursor(), pick);
    else {
        const auto bounds = clientBounds();
        const auto roi =
            bounds ? triggerlab::aimRegion(*bounds, settings) : std::optional<triggerlab::Region>{};
        if (!roi || GetAncestor(GetForegroundWindow(), GA_ROOT) != targetWindow) {
            acceptSample({}, false);
            status = !bounds ? L"Выберите окно тренажёра"
                     : !roi ? L"Область прицела выходит за окно; уменьшите сдвиг / ROI"
                            : L"Активируйте выбранный тренажёр";
            clickStatus = status;
        } else {
            POINT center{static_cast<LONG>(roi->x + roi->width / 2),
                         static_cast<LONG>(roi->y + roi->height / 2)};
            if (!permittedRegion(*roi)) {
                acceptSample({}, false);
                status = clickStatus = L"Область прицела перекрыта другим окном";
            } else {
                auto fresh = capture.capture(*roi);
                if (fresh.frame && (fresh.region.x != roi->x || fresh.region.y != roi->y ||
                                    fresh.region.width != roi->width || fresh.region.height != roi->height ||
                                    !permittedRegion(*roi)))
                    fresh = {};
                analyzedClient = bounds;
                analysisPoint = triggerlab::CapturePoint{center.x, center.y};
                acceptSample(std::move(fresh), false);
            }
        }
    }
    if (running && !pick) {
        if (sample.frame) {
            ++runFrames;
            if (mask.found)
                ++runAlignedFrames;
            runBestCoverage = std::max(runBestCoverage, mask.coverage);
        } else {
            ++runUnavailableFrames;
            runLastIssue = status;
        }
    }
    if (!running)
        capture.reset();
}

void PreviewWindow::acceptSample(triggerlab::CaptureSample fresh, bool pick) {
    sample = std::move(fresh);
    mask = {};
    // Use the actual window under the sampled pixel, not the analyzer's
    // rectangle: another window may cover it, and neighboring ROI pixels
    // do not determine whether the cursor color matches.
    const bool ownPixel =
        sample.cursorPosition &&
        GetAncestor(WindowFromPoint({sample.cursorPosition->x, sample.cursorPosition->y}), GA_ROOT) == window;
    if (ownPixel) {
        if (autoClick && sample.frame)
            gate.step(true, false, triggerlab::Clock::now());
        sample = {};
        status = L"Уведите курсор за окно анализатора";
    } else if (sample.frame) {
        if (pick && sample.cursorPixel) {
            color.target = *sample.cursorPixel;
            color.space = triggerlab::ColorSpace::RGB;
            syncColor();
        }
        mask = settings.center && !pick ? triggerlab::aimColorMask(*sample.frame, color, settings)
                                        : triggerlab::makeColorMask(*sample.frame, color);
        if (sample.allBlack && settings.center)
            mask.found = false;
        const bool found = settings.center && !pick
                               ? mask.found
                               : (sample.cursorPixel && triggerlab::matchesColor(*sample.cursorPixel, color));
        status = sample.allBlack ? L"Чёрный ROI: содержимое окна может быть недоступно"
                 : found ? L"СОВПАДЕНИЕ в области обнаружения"
                         : L"Нет совпадения";
        // Display-only copy: it cannot be used by the gate or SendInput.
        displayFrame = *sample.frame;
        displayMask = mask;
        displaySettings = settings;
        displaySettings.center = settings.center && !pick;
        displayTime = GetTickCount64();
        displayFound = found;
    } else
        status =
            sample.status == triggerlab::CaptureStatus::CursorMoved
                ? L"Курсор движется: удерживайте его на нужном цвете"
                : L"Захват недоступен (статус " + std::to_wstring(static_cast<int>(sample.status)) + L")";
    if (!settings.center || pick)
        analysisPoint = sample.cursorPosition;
    if (autoClick && sample.frame && analysisPoint)
        handleClick({analysisPoint->x, analysisPoint->y});
    else if (autoClick) {
        gate.step(false, false, triggerlab::Clock::now());
        clickStatus = ownPixel ? L"Курсор над анализатором" : L"Нет свежего пикселя: " + status;
    }
    if (pick) {
        notice = sample.cursorPixel ? L"Цвет взят с экрана. Нажмите «Сохранить всё»"
                                    : L"Пипетка: свежий пиксель недоступен, цвет не изменён";
    }
    if (GetTickCount64() - lastPaint >= 33) {
        lastPaint = GetTickCount64();
        InvalidateRect(window, nullptr, FALSE);
    }
}

} // namespace triggerlab::win32
