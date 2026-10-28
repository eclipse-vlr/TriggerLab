# Разработка TriggerLab

Проект использует C++20, стандартную библиотеку и Windows API. Дополнительный фреймворк интерфейса не требуется.

## Сборка

Windows: CMake 3.20+, Visual Studio/Build Tools 2022, MSVC v143 и Windows SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DTRIGGERLAB_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

Linux: GCC или Clang с поддержкой C++20, CMake и Python 3 для проверки сценария публикации.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTRIGGERLAB_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure --no-tests=error
python3 tests/release_workflow_tests.py
```

Для выпуска без тестовых целей задайте `-DTRIGGERLAB_BUILD_TESTS=OFF`. Получаются два приложения: `TriggerLab.exe` и `TriggerLabTrainer.exe`. Диагностические сценарии находятся в отдельных тестовых EXE.

## Стиль

Форматирование определяет [.clang-format](.clang-format); используется **clang-format 18**.

```bash
python3 scripts/format.py --fix
python3 scripts/format.py
```

В Windows можно указать путь: `python scripts/format.py --formatter "C:\Program Files\LLVM\bin\clang-format.exe" --fix`. Нужна версия 18.

Имена типов — PascalCase, функции и поля — camelCase; существующие закрытые поля тренажёра сохраняют суффикс `_`. Комментарий объясняет ограничение или причину решения. Идентификаторы контролов анализатора определены в `src/win32/preview/ControlIds.hpp`.

## Границы изменений

Переносимая логика относится к `triggerlab_core`. Декодирование и захват Windows — к `triggerlab_capture`. Изменения интерфейса остаются в `src/win32`; описание зависимостей — в [архитектуре](docs/ARCHITECTURE.md).

При изменении формата настроек сохраняйте чтение поддерживаемых старых схем. При изменении ввода проверяйте повторное наведение, потерю фокуса, отказ Up и Stop/F9. Тест должен проверять наблюдаемое поведение либо воспроизводить дефект.

Windows-интеграционные проверки используют собственные окна и временные настройки. Часть из них отправляет реальный системный ввод в эти окна: запускайте весь CTest на свободном тестовом рабочем столе.

## CI и публикация

CI проверяет форматирование, MSVC/Windows, GCC/Linux и Clang с ASan/UBSan. Предупреждения компилятора считаются ошибками. Windows отдельно собирает приложения с выключенными тестами и упаковывает эту сборку.

Версия задаётся в `project(... VERSION ...)` в CMake. Заголовок версии и упаковщик получают её оттуда. Для нового выпуска нужны запись в CHANGELOG и `docs/releases/<версия>.md`. После слияния в main публикация ждёт успешного CI; существующий опубликованный релиз не перезаписывается.

В PR опишите проблему, итоговое поведение, проведённые проверки и оставшиеся ограничения. Исторические результаты ручных испытаний находятся в `docs/history/`; они не заменяют проверку текущего изменения.
