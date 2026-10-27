# Защита main

Для владельца репозитория: Settings → Rules → Rulesets → New branch ruleset.

- Name: main; Enforcement: Active; Target: default branch.
- Require a pull request before merging.
- Require status checks: windows, portable-core, Portable core with ASan and UBSan.
- Block force pushes и Restrict deletions.
- Не добавляйте постоянный bypass для обычного рабочего аккаунта.

Правило должно быть включено в настройках GitHub; этот файл не активирует его.
Подключённые инструменты GitHub не предоставляют операцию записи branch protection/rulesets.
Нельзя исправить отсутствие защиты одним коммитом в исходниках.
