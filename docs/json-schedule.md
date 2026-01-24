# Импорт расписания (JSON)

Этот формат используется в диалоге **«Импорт JSON»**. Можно вставить JSON вручную, загрузить файл или сгенерировать через AI.

## Схема

```json
{
  "mode": "merge|replace",
  "notes": [
    {
      "op": "create|update|delete",
      "id": "optional (required for update/delete)",
      "date": "YYYY-MM-DD",
      "time": "HH:MM",
      "title": "…",
      "reminderMinutesBefore": 0,
      "importance": "normal|important|urgent",
      "category": 0,
      "repeat": { "type": "none|daily|weekly", "weekdays": ["mon", "tue"] },
      "content": { "mode": "markdown|html|rtf", "text": "..." },
      "autoHide": { "enabled": true, "seconds": 5 }
    }
  ]
}
```

## Пример

```json
{
  "mode": "merge",
  "notes": [
    {
      "op": "create",
      "date": "2026-01-24",
      "time": "23:30",
      "title": "Поздняя встреча",
      "importance": "important",
      "content": { "mode": "markdown", "text": "**Текст**" }
    }
  ]
}
```

## AI-генерация

В диалоге импорта есть секция **AI**:
- **Endpoint** — базовый URL (например, `https://api.openai.com`)
- **Модель** — имя модели (`gpt-4o-mini` или ваша совместимая)
- **API ключ** — токен доступа
- **Timeout** — таймаут запроса (секунды)

AI должен вернуть **только JSON** по схеме выше (без пояснений).

