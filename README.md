# AlertCalendar

Windows 10/11 приложение‑календарь на **C++20 + CMake + WinAPI (Windows SDK)** с **богатыми заметками (WYSIWYG RichEdit)** и напоминаниями.

## Возможности

- **Календарь**
  - кастомный `CalendarView`
  - метаданные в ячейке дня: **важность (цвет)** + **короткое превью**
- **Заметки**
  - **единый WYSIWYG‑редактор** (`MSFTEDIT_CLASS`) как основной источник правды
  - **вставка изображений** прямо в RTF (`\pict\pngblip`) + хранение медиа в папке заметки
  - автосохранение с debounce (защита от потери правок при смене даты/обновлении/уходе в трей)
- **Напоминания**
  - всплывающее окно уведомления с темой и корректным масштабированием
  - **кнопка “Отложить” (5/10/30/60 мин)** — переносит `scheduledAtUtcMs` вперёд, чтобы напоминание сработало снова
  - опциональное автоскрытие + прогресс‑бар
- **Звук**
  - общий переключатель **вкл/выкл**
  - выбор **стандартных системных звуков** или **своего WAV** для уровней важности (обычно/важно/срочно)
  - кнопка **“Тест звука”**
- **UI/UX**
  - темы **Premium / Minimal**
  - адекватное DPI/Zoom‑масштабирование
  - трей + автозапуск (Windows)

## Быстрый старт (Windows / Visual Studio / MSVC)

Нужно: **Visual Studio 2022** (MSVC), **Windows 10/11 SDK**, **CMake**.

### Вариант 1: через `build.ps1`

Сборка:

```powershell
.\build.ps1 -Config Release
```

Сборка + запуск:

```powershell
.\build.ps1 -Config Release -Run
```

Очистка и пересборка:

```powershell
.\build.ps1 -Clean -Config Release
```

### Вариант 2: вручную CMake

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\AlertCalendar.exe
```

## Где хранятся данные и настройки

- **Заметки и медиа**: `%APPDATA%\AlertCalendar\` (Roaming AppData)
- **Настройки**: реестр `HKEY_CURRENT_USER\Software\AlertCalendar`

## Структура проекта

- `src/win/` — окна/контролы WinAPI (MainWindow, NotificationWindow, CalendarView, темы, RichEdit утилиты)
- `src/model/` — модель заметок + файловый репозиторий
- `src/settings/` — настройки (реестр, автозапуск)
- `src/core/` — время/утилиты

## Разработка

См. `CONTRIBUTING.md`.

