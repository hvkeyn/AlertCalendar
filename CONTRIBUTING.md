# Contributing

Спасибо, что хотите улучшить **AlertCalendar**.

## Требования

- Windows 10/11
- Visual Studio 2022 (MSVC) + Windows 10/11 SDK
- CMake 3.21+

## Сборка

### PowerShell (рекомендуется)

```powershell
.\build.ps1 -Config Release
```

Запуск после сборки:

```powershell
.\build.ps1 -Config Release -Run
```

### Вручную CMake

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Где лежат данные

- Заметки/медиа: `%APPDATA%\AlertCalendar\`
- Настройки: `HKEY_CURRENT_USER\Software\AlertCalendar`

Если нужно “сбросить” приложение для чистого теста — удалите папку в `%APPDATA%` и ключ реестра.

## Стиль и принципы

- **Без потери данных**: любые UX‑изменения должны сохранять правки пользователя (autosave / flush перед сменой контекста).
- **Один источник правды**: редактор хранит RTF как основной формат (что видишь — то и сохраняется).
- **DPI/Zoom**: новые контролы/окна должны корректно масштабироваться.
- **Тема**: новые элементы UI должны использовать `UiTheme` и поддерживать `Premium/Minimal`.

## Pull Request

Пожалуйста, в PR укажите:

- что изменено и почему
- как это проверить
- если затрагивается UX — приложите скрин/видео (по возможности)

Шаблон: `.github/PULL_REQUEST_TEMPLATE.md`

