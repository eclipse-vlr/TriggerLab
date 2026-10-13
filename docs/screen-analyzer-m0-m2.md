# Migration baseline — M0–M2

Дата: 2026-10-14. GitHub origin обновлён через git fetch origin.
main и feature/screen-analyzer: e86f5af217b2957262700bc76c00f30473d418cd (v0.3.2).
Работа ведётся на feature/screen-analyzer; main не изменяется.
Локальный codex_handoff_v0_4.json остаётся вне Git.

## M0 — до изменений

Прочитаны CMakeLists.txt, основной Win32 UI, Frame, TargetDetector, Config и существующие тесты.

```powershell
cmake -S . -B build-m0 -G "Visual Studio 17 2022" -A x64
cmake --build build-m0 --config Release --parallel
ctest --test-dir build-m0 -C Release --output-on-failure
```

MSVC 19.44.35229, Windows SDK 10.0.26100.0: Release x64 успешно собран.
CTest: 4/4 passed (ядро, конфигурация, сессии, Win32 GUI smoke).

## M1–M2 — границы изменения

Старый TriggerLab.exe, интерфейс, локализация и JSON остаются v0.3.2.
Добавлены ScreenCapture, Region, ColorMask и совместимое расширение TargetDetector::detectColor.
Windows capture помещён в отдельную библиотеку triggerlab_capture, Windows-заголовки не используются в переносимых заголовках/маске.

Отдельный TriggerLabCapturePreview.exe проверяет реальные BGRA-пиксели и маску. При запуске захват выключен; Start включает 17×17 ROI у курсора, Stop/ESC выключают таймер, очищают результат и освобождают GDI-буфер. C берёт текущий цвет только из свежего успешного захвата, H переключает RGB/HSV. ROI над собственным окном не захватывается. Нет внешних нажатий или автоматических действий. Билингвальные подписи служат диагностике M1–M2; полноценный UX и настройки анализатора относятся к M3/M4.

## Контракт захвата

Координаты — физические пиксели. На время запроса используется thread DPI context Per Monitor V2 с восстановлением предыдущего контекста. Требуется Windows 10 1703+ / Windows 11. Границы виртуального рабочего стола обновляются на каждый захват; cursor ROI дополнительно ограничивается реальным монитором, содержащим курсор (включая отрицательные координаты).

CaptureSample содержит status, фактический прямоугольник, optional Frame и optional cursorPixel. При ошибке Frame/cursorPixel отсутствуют: старый кадр не возвращается. DIBSection top-down BGRA32/DC переиспользуются до изменения размера; источник GetDC/ReleaseDC берётся на каждый запрос. Alpha нормализуется к 255. Доступность input desktop проверяется до копирования. Конструктор ничего не захватывает, reset и деструктор освобождают ресурсы. Объект принадлежит одному потоку.

allBlack — предупреждение о чёрном ROI, а не доказательство защиты: полностью чёрная область может быть настоящим изображением. Защищённые приложения/secure desktop могут быть недоступны. Preview не сохраняет кадры на диск и не запускается автоматически.

## Контракт маски

RGB: абсолютная разница каждого канала <= channelTolerance [0..255]. Alpha игнорируется.
HSV: H [0..360), циклическое расстояние hueTolerance [0..180]; S/V [0..1] с независимыми допусками. Для ахроматических цветов hue не определён, проверяются S/V.
Маска — top-down uint8 0/255; coverage относится к фактически пересечённому ROI; пустой ROI не обнаруживает цвет. Неверная конфигурация отклоняется, включая NaN. TargetDetector сохраняет старую пороговую API; detectColor добавляет новую маску с прежним ROI-радиусом.

## Проверка

```powershell
cmake -S . -B build-m12 -G "Visual Studio 17 2022" -A x64
cmake --build build-m12 --config Release --parallel 2
ctest --test-dir build-m12 -C Release --output-on-failure
.\build-m12\Release\TriggerLabCapturePreview.exe
```

Новые наборы: геометрия ROI/BGRA, RGB/HSV/маска, Windows live capture. Live-тест рисует известный RGB в собственном временном окне, захватывает ROI и проверяет канал BGRA, маску, ошибки и 120 изменений размера без роста GDI-ресурсов. Если interactive desktop отсутствует, тест явно SKIP (77), а не PASS. Переносимые наборы запускаются без Windows capture.

M3–M5: замена основного UI, versioned JSON migration, edge-trigger stabilizer/метрики и полноценные GUI smoke остаются следующим этапом.

## Итог текущего этапа

MSVC Release x64: успешно, предупреждений в итоговой сборке нет.
CTest build-m12: 7/7 passed, включая Windows live capture (не SKIP).
Тест подтвердил RGB(231,67,19), маску 100%, отсутствие роста capture GDI-ресурсов при 120 изменениях размера и освобождение буфера при reset/destructor. Визуально проверены запуск preview в Stop, явный Start, переключение RGB/HSV и выключение ESC. DPI/отрицательные координаты проверены геометрическими тестами; физические multi-monitor/high-DPI сценарии и hotplug вручную пока не проверены.
