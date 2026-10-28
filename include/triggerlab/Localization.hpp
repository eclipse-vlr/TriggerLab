#pragma once
#include <string_view>
namespace triggerlab {
enum class Language { English=0, Russian=1 };
struct Translation { std::wstring_view english; std::wstring_view russian; };
inline constexpr Translation translations[] = {
    {L"IDLE",L"ОЖИДАНИЕ"}, {L"ARMED",L"АКТИВЕН"}, {L"DETECTED",L"ОБНАРУЖЕНО"}, {L"COOLDOWN",L"ПЕРЕЗАРЯДКА"}, {L"UNKNOWN",L"НЕИЗВЕСТНО"},
    {L"Crossing",L"Пересечение"}, {L"Multiple targets",L"Несколько мишеней"}, {L"Precision",L"Точность"}, {L"Unknown",L"Неизвестно"},
    {L"Crossing (classic)",L"Пересечение (классика)"}, {L"Precision (small target)",L"Точность (малая мишень)"},
    {L"Cooldown ms [50..2000]",L"Задержка, мс [50..2000]"}, {L"ROI radius [0..30]",L"Радиус ROI [0..30]"},
    {L"Stable frames [1..20]",L"Подтверждение, кадры [1..20]"}, {L"Coverage [0.01..1]",L"Покрытие [0.01..1]"},
    {L"Minimum red [0..255]",L"Мин. красный [0..255]"}, {L"Minimum blue [0..255]",L"Мин. синий [0..255]"}, {L"Maximum green [0..255]",L"Макс. зелёный [0..255]"},
    {L"Apply and save",L"Применить и сохранить"}, {L"Reset statistics / scene",L"Сброс статистики и сцены"},
    {L"Scene / speed [0.25..3]",L"Сцена / скорость [0.25..3]"}, {L"Apply scene and save",L"Применить сцену"},
    {L"Pause scene (P)",L"Пауза (P)"}, {L"Resume scene (P)",L"Продолжить (P)"}, {L"Export event log (CSV)",L"Экспорт журнала (CSV)"},
    {L"Language / Язык",L"Язык / Language"},
    {L"Settings saved",L"Настройки сохранены"}, {L"Save failed",L"Не удалось сохранить настройки"}, {L"Settings loaded",L"Настройки загружены"},
    {L"Cannot load settings; defaults applied",L"Настройки не загружены; применены значения по умолчанию"},
    {L"Invalid scene settings: not changed",L"Параметры сцены неверны: изменения не применены"},
    {L"Invalid values: settings were not changed",L"Значения неверны: настройки не изменены"},
    {L"Event log saved beside settings: events.csv",L"Журнал сохранён рядом с настройками: events.csv"}, {L"Event log export failed",L"Не удалось экспортировать журнал"},
    {L"SETTINGS / SCENE",L"НАСТРОЙКИ / СЦЕНА"},
    {L"TRIGGERLAB  |  LOCAL PRACTICE SANDBOX",L"TRIGGERLAB  |  ЛОКАЛЬНЫЙ ТРЕНАЖЁР"},
    {L"MOUSE: aim | HOLD LMB / SPACE: arm | RMB: center | P: pause | R: reset",L"Мышь: прицел | ЛКМ / SPACE: активация | ПКМ: центр | P: пауза | R: сброс"},
    {L"UP/DOWN: cooldown  |  LEFT/RIGHT: detection ROI",L"Вверх/вниз: задержка | Влево/вправо: радиус ROI"},
    {L"Engine: ",L"Состояние: "}, {L" (holding)",L" (активен)"}, {L" (inactive)",L" (неактивен)"},
    {L"Target: ",L"Цель: "}, {L"YES",L"ДА"}, {L"NO",L"НЕТ"}, {L"  |  Coverage: ",L"  |  Покрытие: "},
    {L"Simulated shots: ",L"Симулированные выстрелы: "}, {L"  |  Cooldown: ",L"  |  Задержка: "}, {L"ms",L"мс"}, {L"ms ",L"мс "},
    {L"ROI radius: ",L"Радиус ROI: "}, {L"px  |  FPS: ",L"пикс. | FPS: "}, {L"Stable frames: ",L"Подтверждение, кадры: "},
    {L"Detection to shot: P50 ",L"Обнаружение → выстрел: P50 "}, {L" ms / P95 ",L" мс / P95 "}, {L" ms | Samples: ",L" мс | Измерений: "},
    {L"Scene: ",L"Сцена: "}, {L" | Speed: ",L" | Скорость: "}, {L" | PAUSED",L" | ПАУЗА"}, {L" | RUNNING",L" | РАБОТАЕТ"},
    {L"Recent events: ",L"Событий в журнале: "}, {L" SIMULATED SHOT",L" СИМУЛИРОВАННЫЙ ВЫСТРЕЛ"}, {L" | target ",L" | цель "}, {L"SIMULATED HIT",L"ПОПАДАНИЕ"},
    {L"Simulated shots only | Local scene | No external input",L"Только симуляция | Локальная сцена | Без внешнего ввода"},
    {L"TriggerLab - Local Practice Sandbox",L"TriggerLab — локальный тренажёр"},
    {L"Could not start UI timer.",L"Не удалось запустить таймер интерфейса."}, {L"Could not register window class.",L"Не удалось зарегистрировать класс окна."},
    {L"Could not create practice window.",L"Не удалось создать окно тренажёра."}, {L"Unexpected initialization error.",L"Ошибка инициализации приложения."}
};
inline std::wstring_view translate(Language language,std::wstring_view english) noexcept {
    if(language==Language::Russian) for(const auto& entry:translations) if(entry.english==english) return entry.russian;
    return english;
}
}
