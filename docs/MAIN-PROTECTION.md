# Защита main

На 27 октября 2026 защита включена: [активное правило main](https://github.com/eclipse-vlr/TriggerLab/rules/25941001).
Проверено через API: `protected=true`, `enforcement=active`, цель — default branch,
список обходов пуст. Обязательны PR и три проверки GitHub Actions, запрещены
удаление и force-push. Дополнительные одобрения ревью не обязательны (0).
Правило сохранено через браузер после подтверждения доступа владельцем;
подключение GitHub по-прежнему не предоставляет операцию записи этих настроек.

Для владельца репозитория: Settings → Rules → Rulesets → New branch ruleset.

- Name: main; Enforcement: Active; Target: default branch.
- Require a pull request before merging.
- Require status checks: windows, portable-core, Portable core with ASan and UBSan.
- Block force pushes и Restrict deletions.
- Не добавляйте постоянный bypass для обычного рабочего аккаунта.

Правило должно быть включено в настройках GitHub; этот файл не активирует его.
Подключённые инструменты GitHub не предоставляют операцию записи branch protection/rulesets.
Нельзя исправить отсутствие защиты одним коммитом в исходниках.
