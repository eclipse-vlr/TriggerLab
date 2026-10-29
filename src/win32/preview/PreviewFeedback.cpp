#include "preview/PreviewWindow.hpp"
#include <algorithm>
#include <cmath>

namespace triggerlab::win32 {

const wchar_t *PreviewWindow::parameterHelp(int index) {
    static const wchar_t *texts[] = {
        L"Размер области вокруг прицела: нечётное число от 5 до 101 пикселя. Большая область помогает видеть "
        L"мишень, но клик разрешается только у центра. Слишком большая область может включать фон и соседние "
        L"цели.",
        L"Радиус исключения перекрестия в пикселях: 3 означает квадрат 7×7. Его цвет не учитывается. Мишень "
        L"должна совпасть во всех четырёх точках по сторонам либо по диагоналям от центра: диагонали обходят "
        L"лучи прицела в форме плюса. Для маленьких целей уменьшите радиус; 0 проверяет сам центральный "
        L"пиксель.",
        L"Минимальная доля цвета мишени в области без исключённого центра: от 1 до 100%. Высокий порог может "
        L"пропускать маленькие цели. Низкий порог не разрешает клик по цели у края: наведение центром всё "
        L"равно обязательно.",
        L"Сдвиг точки анализа по горизонтали от центра клиентской области выбранного окна. Плюс — вправо, "
        L"минус — влево. Оставьте 0, если прицел по центру; ошибочный сдвиг даёт ранние или пропущенные "
        L"срабатывания.",
        L"Сдвиг точки анализа по вертикали от центра клиентской области выбранного окна. Плюс — вниз, минус "
        L"— вверх. Оставьте 0 для прицела по центру.",
        L"Время наблюдаемого совпадения до клика: 1–2000 мс. Большая задержка может пропускать быстрые "
        L"наведения, маленькая сильнее реагирует на краткие совпадения. После ошибки захвата или длительной "
        L"паузы отсчёт начинается заново.",
        L"Минимальное время между началами двух кликов: 50–2000 мс. Это не автоповтор. Следующий клик "
        L"возможен только после свежего кадра без совпадения и нового наведения. Цвет продолжает проверяться "
        L"во время удержания кнопки.",
        L"Сколько удерживать левую кнопку: 5–150 мс, по умолчанию 30. Это не задержка перед кликом. "
        L"Тренажёры по-разному принимают короткие нажатия; слишком длинное может вызвать несколько "
        L"выстрелов. Stop/F9 прерывает удержание.",
        L"Допустимое отклонение каждого канала RGB: 0–255. Например, 10 допускает отличие на 10 в R, G и B. "
        L"Большой допуск может принять фон за мишень. В HSV это поле отключено: там действуют отдельные "
        L"допуски оттенка, насыщенности и яркости. H переключает RGB/HSV; после изменений сохраните "
        L"настройки и проверьте цвет."};
    return texts[index];
}

void PreviewWindow::syncActions() {
    EnableWindow(colorEdits[3], color.space == triggerlab::ColorSpace::RGB);
    SetWindowTextW(colorLabels[3],
                   color.space == triggerlab::ColorSpace::RGB ? L"Допуск RGB" : L"Допуск RGB (не HSV)");
    EnableWindow(GetDlgItem(window, control::stop),
                 running || syntheticDown || pickDeadline || targetDeadline || aimPickDeadline);
    const bool target = targetWindow && IsWindow(targetWindow);
    EnableWindow(autoControl, running && target);
    EnableWindow(GetDlgItem(window, control::pickAimColor), settings.center && target);
    const bool canCalibrate = settings.center && target && colorReady;
    EnableWindow(GetDlgItem(window, control::targetReference), canCalibrate);
    EnableWindow(GetDlgItem(window, control::backgroundReference), canCalibrate);
    if (qualityInfo) {
        const auto value = qualityText();
        wchar_t previous[2048]{};
        GetWindowTextW(qualityInfo, previous, 2048);
        if (value != previous)
            SetWindowTextW(qualityInfo, value.c_str());
    }
    updateWizard();
    auto updateText = [](HWND control, const std::wstring &value) {
        if (!control)
            return;
        wchar_t previous[2048]{};
        GetWindowTextW(control, previous, 2048);
        if (value != previous)
            SetWindowTextW(control, value.c_str());
    };
    const auto feedback = currentFeedback();
    updateText(homeHint, feedback.detail);
    updateText(homeSummary, L"Клики: " + std::to_wstring(clickCount) + L"    Проверки без ввода: " +
                                std::to_wstring(diagnosticCount) + L"\n" +
                                (!running ? L"Остановлено. Клики не отправляются."
                                 : settings.dryRun ? L"Диагностика: клики не отправляются."
                                 : autoClick ? L"Автоклики включены только в выбранном окне."
                                             : L"Захват без кликов.") +
                                L"\n" + clickStatus + L"\n" + pressTimingText());
    updateText(headerStatus,
               feedback.title + L" · " +
                   (running && autoClick && !settings.dryRun ? L"автоклики включены" : L"без кликов"));
}

int PreviewWindow::wizardStep() const {
    return !targetWindow || !IsWindow(targetWindow) ? 0 : !colorReady ? 1 : !verified ? 2 : 3;
}

bool PreviewWindow::colorFound() const {
    return sample.frame && (!settings.center || !sample.allBlack) &&
           (settings.center ? mask.found
                            : sample.cursorPixel && triggerlab::matchesColor(*sample.cursorPixel, color));
}

std::wstring PreviewWindow::saveState() const {
    const bool a = settingsDirty(), b = colorDirty();
    if (a || b)
        return L"● Несохранённые изменения: " + std::wstring(a && b ? L"параметры и цвет"
                                                             : a    ? L"параметры"
                                                                    : L"цвет");
    if (paramsOnDisk && colorOnDisk)
        return profileChanged() ? L"✓ Настройки сохранены · профиль изменён, сохраните его отдельно"
                                : L"✓ Все настройки сохранены";
    if (paramsOnDisk && !colorReady)
        return L"✓ Параметры сохранены · выберите цвет мишени";
    return L"Стандартные настройки · сохраните их для следующего запуска";
}

std::wstring PreviewWindow::percent(double fraction) {
    const auto tenths = static_cast<int>(std::lround(std::clamp(fraction, 0.0, 1.0) * 1000));
    return std::to_wstring(tenths / 10) + L"," + std::to_wstring(tenths % 10) + L"%";
}

std::wstring PreviewWindow::qualityText() const {
    if (!settings.center)
        return L"Эталоны доступны в режиме вокруг прицела окна.";
    if (!colorReady)
        return L"Сначала выберите цвет мишени. Затем снимите мишень и фон.";
    if (settingsDirty() || colorDirty())
        return L"Сохраните изменения: эталоны будут проверены с новым цветом, допуском и порогом.";
    const auto check = triggerlab::compareColorSamples(
        referencesCurrent() && targetReference ? &*targetReference : nullptr,
        referencesCurrent() && backgroundReference ? &*backgroundReference : nullptr, color, settings);
    if (!check.valid)
        return L"Размер области изменился. Переснимите мишень и фон.";
    std::wstring result =
        L"Мишень: " + (check.targetCoverage ? percent(*check.targetCoverage) : L"нет снимка") + L" · Фон: " +
        (check.backgroundCoverage ? percent(*check.backgroundCoverage) : L"нет снимка") + L" · Порог: " +
        std::to_wstring(settings.minimumPercent) + L"%\n";
    if (check.targetCoverage && !check.targetFound)
        result += L"Мишень не проходит порог. Уточните цвет/допуск или область.\n";
    if (check.backgroundFound)
        result += L"Фон тоже срабатывает. Снизьте допуск или выберите другой оттенок.\n";
    if (!check.targetCoverage || !check.backgroundCoverage)
        return result +
               L"Снимите оба эталона: сначала мишень, затем участок фона без мишени. F9 отменяет ожидание.";
    if (check.targetFound && !check.backgroundFound)
        result += L"На этих эталонах мишень проходит порог, фон — нет.\n";
    return result + L"Оценка по двум сохранённым снимкам; повторите её на других участках фона.";
}

std::wstring PreviewWindow::captureHint() const {
    switch (sample.status) {
    case triggerlab::CaptureStatus::InvalidRegion:
        return L"Область выходит за окно. Уменьшите размер или сдвиги.";
    case triggerlab::CaptureStatus::AllocationFailed:
        return L"Не хватает ресурсов для снимка. Уменьшите область или перезапустите захват.";
    case triggerlab::CaptureStatus::CopyFailed:
        return L"Windows не смогла получить снимок. Попробуйте оконный режим тренажёра.";
    case triggerlab::CaptureStatus::CursorMoved:
        return L"Курсор переместился во время снимка. Остановитесь на нужном цвете.";
    case triggerlab::CaptureStatus::DesktopUnavailable:
        return L"Свежий кадр недоступен. Проверьте, что окно тренажёра открыто и область прицела видна; "
               L"попробуйте оконный режим.";
    default:
        return L"Нет свежего снимка. Проверьте доступность выбранного окна.";
    }
}

std::wstring PreviewWindow::detectionHint() const {
    if (!sample.frame)
        return captureHint();
    if (sample.allBlack && settings.center)
        return L"Снимок чёрный. Попробуйте оконный режим тренажёра.";
    if (!colorFound()) {
        if (settings.center && mask.matchingPixels > 0 && mask.coverage * 100 >= settings.minimumPercent)
            return L"Цвет виден рядом с прицелом, но центр ещё не на мишени. Наведите центром; для маленьких "
                   L"целей уменьшите исключение перекрестия и проверьте сдвиги X/Y.";
        if (settings.center && mask.matchingPixels > 0)
            return L"Цвет есть: " + percent(mask.coverage) + L", нужно " +
                   std::to_wstring(settings.minimumPercent) +
                   L"%. Наведите прицел ближе к мишени или уточните область.";
        if (settings.center && triggerlab::excludedColorPixels(*sample.frame, color, settings) > 0)
            return L"Цвет только под исключённым перекрестием. Выберите цвет мишени вне перекрестия.";
        return L"Цвет не найден. Уточните оттенок и допуск; сравните эталоны мишени и фона.";
    }
    if (!autoClick)
        return L"Цвет найден, но триггер выключен. Запустите проверку либо включите триггер в расширенном "
               L"режиме.";
    if (syntheticDown)
        return L"Кнопка ещё удерживается; ожидаем отпускание.";
    if (inputHeld())
        return L"Срабатывание приостановлено: отпустите кнопки мыши и клавиши Shift/Ctrl/Alt.";
    if (gate.clickedHover())
        return settings.dryRun ? L"Проверка пройдена. Уведите прицел с цвета для следующей проверки."
                               : L"Клик уже выполнен. Уведите прицел с цвета и наведите снова.";
    const auto elapsed = std::max<std::int64_t>(0, gate.elapsed(triggerlab::Clock::now()).count());
    return elapsed >= settings.dwellMs ? L"Задержка пройдена; ожидаем допустимый интервал и свежий кадр."
                                       : L"Цвет найден. Ожидание задержки: " + std::to_wstring(elapsed) +
                                             L" / " + std::to_wstring(settings.dwellMs) + L" мс.";
}

std::wstring PreviewWindow::nextHint() const {
    if (aimPickDeadline)
        return snapshotPurpose == SnapshotPurpose::Background
                   ? L"Наведите прицел на фон без мишени. F9 — отмена"
                   : L"Наведите прицел на мишень. F9 — отмена";
    if (pickDeadline || targetDeadline)
        return L"Выбор идёт: активируйте тренировку. F9 — отмена";
    if (!validationMessage.empty())
        return validationMessage;
    if (wizardStep() == 0)
        return L"Выберите окно из списка либо найдите Kovaak’s / Aimlabs кнопкой ниже";
    if (!colorReady)
        return L"Возьмите цвет мишени с экрана или из изображения";
    if (settingsDirty() || colorDirty())
        return L"Сохраните изменения перед проверкой обнаружения";
    if (!running)
        return verified ? L"Цвет проверен. Можно включить клики или повторить проверку"
                        : L"Нажмите «Проверить цвет» и наведите прицел на мишень";
    if (GetAncestor(GetForegroundWindow(), GA_ROOT) != targetWindow)
        return L"Активируйте выбранную тренировку";
    if (settings.center) {
        const auto bounds = clientBounds();
        if (bounds && !triggerlab::aimRegion(*bounds, settings))
            return L"Область выходит за окно. Уменьшите размер или сдвиги.";
    }
    if (!sample.frame && (status == L"Область прицела перекрыта другим окном" ||
                          status == L"Уведите курсор за окно анализатора"))
        return status;
    return detectionHint();
}

PreviewWindow::Feedback PreviewWindow::feedback(bool foreground, bool held) const {
    if (syntheticDown && !autoClick)
        return {FeedbackTone::Error, L"Ожидаем отпускания кнопки",
                L"Windows пока отклоняет отпускание. Новые клики заблокированы; Stop/F9 повторяет попытку."};
    if (!validationMessage.empty())
        return {FeedbackTone::Error, L"Ошибка параметров", validationMessage};
    if (!actionError.empty())
        return {FeedbackTone::Error, L"Действие не выполнено", actionError};
    if (!settingsLoadWarning.empty())
        return {FeedbackTone::Waiting, L"Стандартные параметры восстановлены", settingsLoadWarning};
    if (pickDeadline || targetDeadline || aimPickDeadline)
        return {FeedbackTone::Info, L"Выбор · F9 отменяет", nextHint()};
    if (wizardStep() == 0)
        return {FeedbackTone::Neutral, L"Выберите окно", nextHint()};
    if (!colorReady)
        return {FeedbackTone::Neutral, L"Выберите цвет", nextHint()};
    if (settingsDirty() || colorDirty())
        return {FeedbackTone::Waiting, L"Сохраните изменения", nextHint()};
    if (!running)
        return {verified ? FeedbackTone::Good : FeedbackTone::Neutral,
                verified ? L"Готово к запуску" : L"Нужна проверка цвета", nextHint()};
    if (!foreground)
        return {FeedbackTone::Waiting, L"Пауза: окно не активно",
                L"Вернитесь в выбранную тренировку. Если открыто меню, возобновите игру. Для полной "
                L"остановки нажмите F9."};
    if (settings.center) {
        const auto bounds = clientBounds();
        if (bounds && !triggerlab::aimRegion(*bounds, settings))
            return {FeedbackTone::Error, L"Область за пределами окна",
                    L"В расширенных настройках уменьшите размер области или сдвиги X/Y, сохраните и "
                    L"повторите проверку."};
    }
    if (!sample.frame) {
        const bool error = sample.status == triggerlab::CaptureStatus::InvalidRegion ||
                           sample.status == triggerlab::CaptureStatus::CopyFailed ||
                           sample.status == triggerlab::CaptureStatus::AllocationFailed;
        const bool covered = status == L"Область прицела перекрыта другим окном" ||
                             status == L"Уведите курсор за окно анализатора";
        return {error ? FeedbackTone::Error : FeedbackTone::Waiting, L"Нет свежего кадра",
                covered ? status + L". Освободите область прицела и вернитесь в тренировку." : captureHint()};
    }
    if (sample.allBlack && settings.center)
        return {FeedbackTone::Waiting, L"Снимок чёрный", captureHintForBlack()};
    if (!autoClick)
        return {FeedbackTone::Neutral, L"Захват без кликов",
                L"Для диагностики нажмите «Проверить цвет» в простом режиме или включите триггер в "
                L"расширенном. Клики сейчас не отправляются."};
    if (syntheticDown)
        return {FeedbackTone::Info, L"Выполняется нажатие",
                L"Ожидаем отпускание кнопки. F9 остановит ввод и отпустит её."};
    if (held)
        return {
            FeedbackTone::Waiting, L"Пауза: удерживается ввод",
            L"Отпустите кнопки мыши и клавиши Shift, Ctrl, Alt или Win. Для полной остановки нажмите F9."};
    if (!colorFound())
        return {FeedbackTone::Waiting, L"Ожидание мишени", detectionHint()};
    if (gate.clickedHover())
        return {FeedbackTone::Good, settings.dryRun ? L"Проверка пройдена" : L"Клик завершён",
                settings.dryRun
                    ? L"Цвет распознан без отправки клика. Можно остановить проверку F9 и включить автоклики."
                    : L"На это непрерывное совпадение клик уже выполнен. Уведите прицел с цвета и наведите "
                      L"снова."};
    return {FeedbackTone::Waiting, L"Цвет найден: выдержка задержки", detectionHint()};
}

std::wstring PreviewWindow::captureHintForBlack() {
    return L"В области только чёрные пиксели. Наведите на видимую мишень; если она не появляется на снимке, "
           L"попробуйте оконный режим тренажёра.";
}

PreviewWindow::Feedback PreviewWindow::currentFeedback() const {
    return feedback(GetAncestor(GetForegroundWindow(), GA_ROOT) == targetWindow, inputHeld());
}

COLORREF PreviewWindow::feedbackColor(FeedbackTone tone) const {
    switch (tone) {
    case FeedbackTone::Error:
        return settings.darkTheme ? RGB(255, 184, 184) : RGB(185, 28, 28);
    case FeedbackTone::Waiting:
        return settings.darkTheme ? RGB(255, 211, 139) : RGB(146, 82, 8);
    case FeedbackTone::Good:
        return settings.darkTheme ? RGB(134, 239, 172) : RGB(21, 128, 61);
    case FeedbackTone::Info:
        return themeColors(settings.darkTheme).accent;
    default:
        return themeColors(settings.darkTheme).muted;
    }
}

void PreviewWindow::updateWizard() {
    const int step = wizardStep();
    const wchar_t *labels[] = {L"Выбрать окно", L"Взять цвет мишени", L"Проверить цвет", L"Включить клики"};
    const auto caption = autoClick && !settings.dryRun ? L"Остановить клики" : labels[step];
    wchar_t previous[128]{};
    GetWindowTextW(GetDlgItem(window, control::guidedAction), previous, 128);
    if (std::wstring(previous) != caption)
        SetWindowTextW(GetDlgItem(window, control::guidedAction), caption);
}

std::wstring PreviewWindow::pressTimingText() const {
    if (!lastPress)
        return L"Нажатие: ещё не измерено";
    // This measures our SendInput call interval, not delivery to the game.
    const auto tenths = lastPress->elapsed.count() / 100;
    return L"Нажатие: " + std::to_wstring(tenths / 10) + L"," + std::to_wstring(tenths % 10) +
           L" мс (задано " + std::to_wstring(lastPress->requestedMs) + L")" +
           (!lastPress->releaseAccepted ? L" · ошибка отпускания"
            : lastPress->interrupted    ? L" · прервано"
                                        : L"");
}

} // namespace triggerlab::win32
