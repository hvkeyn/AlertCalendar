#include "ScheduleJson.h"

#include "core/TimeUtils.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <sstream>

namespace {

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolValue = false;
  double numberValue = 0.0;
  bool numberIsInt = false;
  int64_t numberInt = 0;
  std::wstring stringValue;
  std::vector<JsonValue> arrayValue;
  std::vector<std::pair<std::wstring, JsonValue>> objectValue;

  const JsonValue* find(const std::wstring& key) const {
    if (type != Type::Object) return nullptr;
    for (auto it = objectValue.rbegin(); it != objectValue.rend(); ++it) {
      if (it->first == key) return &it->second;
    }
    return nullptr;
  }
};

bool isWs(wchar_t ch) {
  return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

std::wstring posInfo(const std::wstring& s, size_t pos) {
  int line = 1;
  int col = 1;
  for (size_t i = 0; i < pos && i < s.size(); ++i) {
    if (s[i] == L'\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  return L" (строка " + std::to_wstring(line) + L", позиция " + std::to_wstring(col) + L")";
}

class JsonParser {
public:
  explicit JsonParser(const std::wstring& text) : m_text(text) {}

  bool parse(JsonValue* out) {
    if (!out) return false;
    skipWs();
    if (!parseValue(out)) return false;
    skipWs();
    if (m_pos != m_text.size()) {
      setError(L"Лишние символы после JSON" + posInfo(m_text, m_pos));
      return false;
    }
    return true;
  }

  const std::wstring& error() const { return m_error; }

private:
  const std::wstring& m_text;
  size_t m_pos = 0;
  std::wstring m_error;

  void setError(const std::wstring& msg) {
    if (m_error.empty()) m_error = msg;
  }

  void skipWs() {
    while (m_pos < m_text.size() && isWs(m_text[m_pos])) ++m_pos;
  }

  bool parseValue(JsonValue* out) {
    skipWs();
    if (m_pos >= m_text.size()) {
      setError(L"Неожиданный конец JSON" + posInfo(m_text, m_pos));
      return false;
    }

    const wchar_t ch = m_text[m_pos];
    if (ch == L'{') return parseObject(out);
    if (ch == L'[') return parseArray(out);
    if (ch == L'"') {
      out->type = JsonValue::Type::String;
      return parseString(&out->stringValue);
    }
    if (ch == L'-' || (ch >= L'0' && ch <= L'9')) {
      return parseNumber(out);
    }
    if (ch == L't') return parseLiteral(L"true", JsonValue::Type::Bool, out, true);
    if (ch == L'f') return parseLiteral(L"false", JsonValue::Type::Bool, out, false);
    if (ch == L'n') return parseLiteral(L"null", JsonValue::Type::Null, out, false);

    setError(L"Недопустимый символ JSON" + posInfo(m_text, m_pos));
    return false;
  }

  bool parseLiteral(const wchar_t* lit, JsonValue::Type type, JsonValue* out, bool boolVal) {
    const size_t len = wcslen(lit);
    if (m_pos + len > m_text.size() || m_text.compare(m_pos, len, lit) != 0) {
      setError(L"Ожидалось " + std::wstring(lit) + posInfo(m_text, m_pos));
      return false;
    }
    m_pos += len;
    out->type = type;
    out->boolValue = boolVal;
    return true;
  }

  bool parseString(std::wstring* out) {
    if (m_text[m_pos] != L'"') {
      setError(L"Ожидалась строка" + posInfo(m_text, m_pos));
      return false;
    }
    ++m_pos; // skip quote
    out->clear();

    while (m_pos < m_text.size()) {
      wchar_t ch = m_text[m_pos++];
      if (ch == L'"') {
        return true;
      }
      if (ch == L'\\') {
        if (m_pos >= m_text.size()) {
          setError(L"Незавершенная escape-последовательность" + posInfo(m_text, m_pos));
          return false;
        }
        wchar_t esc = m_text[m_pos++];
        switch (esc) {
          case L'"': out->push_back(L'"'); break;
          case L'\\': out->push_back(L'\\'); break;
          case L'/': out->push_back(L'/'); break;
          case L'b': out->push_back(L'\b'); break;
          case L'f': out->push_back(L'\f'); break;
          case L'n': out->push_back(L'\n'); break;
          case L'r': out->push_back(L'\r'); break;
          case L't': out->push_back(L'\t'); break;
          case L'u': {
            if (m_pos + 4 > m_text.size()) {
              setError(L"Некорректный \\u escape" + posInfo(m_text, m_pos));
              return false;
            }
            int value = 0;
            for (int i = 0; i < 4; ++i) {
              wchar_t c = m_text[m_pos++];
              value <<= 4;
              if (c >= L'0' && c <= L'9') value += (c - L'0');
              else if (c >= L'a' && c <= L'f') value += (c - L'a' + 10);
              else if (c >= L'A' && c <= L'F') value += (c - L'A' + 10);
              else {
                setError(L"Некорректный \\u escape" + posInfo(m_text, m_pos - 1));
                return false;
              }
            }
            const wchar_t wc = static_cast<wchar_t>(value);
            // If this is a high surrogate, try to parse the next low surrogate.
            if (wc >= 0xD800 && wc <= 0xDBFF) {
              const size_t saved = m_pos;
              if (m_pos + 6 <= m_text.size() && m_text[m_pos] == L'\\' && m_text[m_pos + 1] == L'u') {
                m_pos += 2;
                int low = 0;
                bool ok = true;
                for (int i = 0; i < 4; ++i) {
                  wchar_t c = m_text[m_pos++];
                  low <<= 4;
                  if (c >= L'0' && c <= L'9') low += (c - L'0');
                  else if (c >= L'a' && c <= L'f') low += (c - L'a' + 10);
                  else if (c >= L'A' && c <= L'F') low += (c - L'A' + 10);
                  else { ok = false; break; }
                }
                if (ok) {
                  out->push_back(wc);
                  out->push_back(static_cast<wchar_t>(low));
                  break;
                }
              }
              m_pos = saved;
            }
            out->push_back(wc);
          } break;
          default:
            setError(L"Некорректный escape" + posInfo(m_text, m_pos - 1));
            return false;
        }
        continue;
      }
      if (ch < 0x20) {
        setError(L"Недопустимый управляющий символ в строке" + posInfo(m_text, m_pos - 1));
        return false;
      }
      out->push_back(ch);
    }

    setError(L"Незакрытая строка" + posInfo(m_text, m_pos));
    return false;
  }

  bool parseNumber(JsonValue* out) {
    const size_t start = m_pos;
    if (m_text[m_pos] == L'-') ++m_pos;
    if (m_pos >= m_text.size()) {
      setError(L"Некорректное число" + posInfo(m_text, m_pos));
      return false;
    }
    if (m_text[m_pos] == L'0') {
      ++m_pos;
    } else if (m_text[m_pos] >= L'1' && m_text[m_pos] <= L'9') {
      while (m_pos < m_text.size() && m_text[m_pos] >= L'0' && m_text[m_pos] <= L'9') ++m_pos;
    } else {
      setError(L"Некорректное число" + posInfo(m_text, m_pos));
      return false;
    }

    bool hasFraction = false;
    if (m_pos < m_text.size() && m_text[m_pos] == L'.') {
      hasFraction = true;
      ++m_pos;
      if (m_pos >= m_text.size() || m_text[m_pos] < L'0' || m_text[m_pos] > L'9') {
        setError(L"Некорректное число" + posInfo(m_text, m_pos));
        return false;
      }
      while (m_pos < m_text.size() && m_text[m_pos] >= L'0' && m_text[m_pos] <= L'9') ++m_pos;
    }

    if (m_pos < m_text.size() && (m_text[m_pos] == L'e' || m_text[m_pos] == L'E')) {
      hasFraction = true;
      ++m_pos;
      if (m_pos < m_text.size() && (m_text[m_pos] == L'+' || m_text[m_pos] == L'-')) ++m_pos;
      if (m_pos >= m_text.size() || m_text[m_pos] < L'0' || m_text[m_pos] > L'9') {
        setError(L"Некорректное число" + posInfo(m_text, m_pos));
        return false;
      }
      while (m_pos < m_text.size() && m_text[m_pos] >= L'0' && m_text[m_pos] <= L'9') ++m_pos;
    }

    const std::wstring num = m_text.substr(start, m_pos - start);
    out->type = JsonValue::Type::Number;
    out->numberIsInt = !hasFraction;
    try {
      if (out->numberIsInt) {
        out->numberInt = std::stoll(num);
        out->numberValue = static_cast<double>(out->numberInt);
      } else {
        out->numberValue = std::stod(num);
      }
    } catch (...) {
      setError(L"Некорректное число" + posInfo(m_text, start));
      return false;
    }
    return true;
  }

  bool parseArray(JsonValue* out) {
    if (m_text[m_pos] != L'[') return false;
    ++m_pos;
    out->type = JsonValue::Type::Array;
    out->arrayValue.clear();
    skipWs();
    if (m_pos < m_text.size() && m_text[m_pos] == L']') {
      ++m_pos;
      return true;
    }

    while (m_pos < m_text.size()) {
      JsonValue v;
      if (!parseValue(&v)) return false;
      out->arrayValue.push_back(std::move(v));
      skipWs();
      if (m_pos >= m_text.size()) break;
      if (m_text[m_pos] == L',') {
        ++m_pos;
        continue;
      }
      if (m_text[m_pos] == L']') {
        ++m_pos;
        return true;
      }
      setError(L"Ожидалась ',' или ']'" + posInfo(m_text, m_pos));
      return false;
    }

    setError(L"Незакрытый массив" + posInfo(m_text, m_pos));
    return false;
  }

  bool parseObject(JsonValue* out) {
    if (m_text[m_pos] != L'{') return false;
    ++m_pos;
    out->type = JsonValue::Type::Object;
    out->objectValue.clear();
    skipWs();
    if (m_pos < m_text.size() && m_text[m_pos] == L'}') {
      ++m_pos;
      return true;
    }

    while (m_pos < m_text.size()) {
      skipWs();
      std::wstring key;
      if (!parseString(&key)) return false;
      skipWs();
      if (m_pos >= m_text.size() || m_text[m_pos] != L':') {
        setError(L"Ожидался ':'" + posInfo(m_text, m_pos));
        return false;
      }
      ++m_pos;
      JsonValue val;
      if (!parseValue(&val)) return false;
      out->objectValue.push_back({ key, std::move(val) });
      skipWs();
      if (m_pos >= m_text.size()) break;
      if (m_text[m_pos] == L',') {
        ++m_pos;
        continue;
      }
      if (m_text[m_pos] == L'}') {
        ++m_pos;
        return true;
      }
      setError(L"Ожидалась ',' или '}'" + posInfo(m_text, m_pos));
      return false;
    }

    setError(L"Незакрытый объект" + posInfo(m_text, m_pos));
    return false;
  }
};

std::wstring lowerCopy(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
  return s;
}

bool parseDate(const std::wstring& s, SYSTEMTIME* out, std::wstring* err) {
  if (!out) return false;
  if (s.size() != 10 || s[4] != L'-' || s[7] != L'-') {
    if (err) *err = L"Дата должна быть в формате YYYY-MM-DD";
    return false;
  }
  auto toInt = [&](size_t pos, size_t len, int* value) -> bool {
    int v = 0;
    for (size_t i = 0; i < len; ++i) {
      wchar_t ch = s[pos + i];
      if (ch < L'0' || ch > L'9') return false;
      v = v * 10 + (ch - L'0');
    }
    *value = v;
    return true;
  };
  int y = 0, m = 0, d = 0;
  if (!toInt(0, 4, &y) || !toInt(5, 2, &m) || !toInt(8, 2, &d)) {
    if (err) *err = L"Дата должна быть в формате YYYY-MM-DD";
    return false;
  }
  if (y < 1900 || y > 3000) {
    if (err) *err = L"Год вне допустимого диапазона";
    return false;
  }
  if (m < 1 || m > 12) {
    if (err) *err = L"Месяц вне диапазона 1..12";
    return false;
  }
  auto isLeap = [&](int year) -> bool {
    if (year % 400 == 0) return true;
    if (year % 100 == 0) return false;
    return (year % 4) == 0;
  };
  auto daysInMonth = [&](int year, int month) -> int {
    static const int kDays[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (month == 2 && isLeap(year)) return 29;
    return kDays[month - 1];
  };
  const int dim = daysInMonth(y, m);
  if (d < 1 || d > dim) {
    if (err) *err = L"День вне диапазона для месяца";
    return false;
  }
  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(y);
  st.wMonth = static_cast<WORD>(m);
  st.wDay = static_cast<WORD>(d);
  st.wHour = 0;
  st.wMinute = 0;
  st.wSecond = 0;
  st.wMilliseconds = 0;
  // Normalize to fill wDayOfWeek
  FILETIME ft{};
  if (SystemTimeToFileTime(&st, &ft)) {
    SYSTEMTIME normalized{};
    if (FileTimeToSystemTime(&ft, &normalized)) {
      st = normalized;
    }
  }
  *out = st;
  return true;
}

bool parseTime(const std::wstring& s, int* outHour, int* outMinute, bool* clamped, std::wstring* err) {
  if (!outHour || !outMinute) return false;
  if (clamped) *clamped = false;
  if (s.size() != 5 || s[2] != L':') {
    if (err) *err = L"Время должно быть в формате HH:MM";
    return false;
  }
  auto toInt = [&](size_t pos) -> int {
    return (s[pos] - L'0') * 10 + (s[pos + 1] - L'0');
  };
  if (s[0] < L'0' || s[0] > L'9' || s[1] < L'0' || s[1] > L'9' ||
      s[3] < L'0' || s[3] > L'9' || s[4] < L'0' || s[4] > L'9') {
    if (err) *err = L"Время должно быть в формате HH:MM";
    return false;
  }
  int hour = toInt(0);
  int minute = toInt(3);
  if (hour < 0 || minute < 0) {
    if (err) *err = L"Время должно быть в формате HH:MM";
    return false;
  }
  if (hour > 23 || (hour == 23 && minute > 59)) {
    if (clamped) *clamped = true;
    *outHour = 23;
    *outMinute = 59;
    return true;
  }
  if (minute > 59) {
    if (err) *err = L"Минуты должны быть в диапазоне 00..59";
    return false;
  }
  *outHour = hour;
  *outMinute = minute;
  return true;
}

int weekdayMaskFromWDayOfWeek(WORD dow) {
  switch (dow) {
    case 1: return kRepeatDayMon;
    case 2: return kRepeatDayTue;
    case 3: return kRepeatDayWed;
    case 4: return kRepeatDayThu;
    case 5: return kRepeatDayFri;
    case 6: return kRepeatDaySat;
    case 0: return kRepeatDaySun;
    default: return 0;
  }
}

int weekdayMaskFromName(const std::wstring& name) {
  const std::wstring n = lowerCopy(name);
  if (n == L"mon") return kRepeatDayMon;
  if (n == L"tue") return kRepeatDayTue;
  if (n == L"wed") return kRepeatDayWed;
  if (n == L"thu") return kRepeatDayThu;
  if (n == L"fri") return kRepeatDayFri;
  if (n == L"sat") return kRepeatDaySat;
  if (n == L"sun") return kRepeatDaySun;
  return 0;
}

bool getString(const JsonValue* v, std::wstring* out) {
  if (!v || v->type != JsonValue::Type::String) return false;
  if (out) *out = v->stringValue;
  return true;
}

bool getBool(const JsonValue* v, bool* out) {
  if (!v || v->type != JsonValue::Type::Bool) return false;
  if (out) *out = v->boolValue;
  return true;
}

bool getInt(const JsonValue* v, int* out) {
  if (!v || v->type != JsonValue::Type::Number || !v->numberIsInt) return false;
  if (!out) return true;
  if (v->numberInt < std::numeric_limits<int>::min() || v->numberInt > std::numeric_limits<int>::max()) return false;
  *out = static_cast<int>(v->numberInt);
  return true;
}

std::string toAnsiBytes(const std::wstring& ws) {
  if (ws.empty()) return {};
  const int needed = WideCharToMultiByte(CP_ACP, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out;
  out.resize(static_cast<size_t>(needed));
  WideCharToMultiByte(CP_ACP, 0, ws.c_str(), static_cast<int>(ws.size()), out.data(), needed, nullptr, nullptr);
  return out;
}

} // namespace

const wchar_t* ScheduleJson::importModeLabel(ImportMode mode) {
  switch (mode) {
    case ImportMode::Replace: return L"replace";
    case ImportMode::Merge:
    default: return L"merge";
  }
}

const wchar_t* ScheduleJson::importOpLabel(ImportOp op) {
  switch (op) {
    case ImportOp::Update: return L"update";
    case ImportOp::Delete: return L"delete";
    case ImportOp::Create:
    default: return L"create";
  }
}

bool ScheduleJson::parseSchedule(const std::wstring& jsonText, ImportResult* out, std::wstring* errorOut) {
  if (!out) return false;
  *out = ImportResult{};

  JsonParser parser(jsonText);
  JsonValue root;
  if (!parser.parse(&root)) {
    if (errorOut) *errorOut = parser.error();
    return false;
  }
  if (root.type != JsonValue::Type::Object) {
    if (errorOut) *errorOut = L"Корневой JSON должен быть объектом.";
    return false;
  }

  if (const JsonValue* modeVal = root.find(L"mode")) {
    std::wstring modeStr;
    if (getString(modeVal, &modeStr)) {
      const std::wstring low = lowerCopy(modeStr);
      if (low == L"merge") out->mode = ImportMode::Merge;
      else if (low == L"replace") out->mode = ImportMode::Replace;
      else out->errors.push_back(L"mode должен быть merge или replace.");
    } else {
      out->errors.push_back(L"mode должен быть строкой.");
    }
  }

  const JsonValue* notesVal = root.find(L"notes");
  if (!notesVal || notesVal->type != JsonValue::Type::Array) {
    out->errors.push_back(L"Поле notes должно быть массивом.");
    return true;
  }

  for (size_t i = 0; i < notesVal->arrayValue.size(); ++i) {
    const JsonValue& itemVal = notesVal->arrayValue[i];
    ImportItem item;
    item.index = static_cast<int>(i);

    if (itemVal.type != JsonValue::Type::Object) {
      item.errors.push_back(L"Элемент должен быть объектом.");
      out->items.push_back(std::move(item));
      continue;
    }

    // op
    if (const JsonValue* opVal = itemVal.find(L"op")) {
      std::wstring opStr;
      if (getString(opVal, &opStr)) {
        const std::wstring low = lowerCopy(opStr);
        if (low == L"create") item.op = ImportOp::Create;
        else if (low == L"update") item.op = ImportOp::Update;
        else if (low == L"delete") item.op = ImportOp::Delete;
        else item.errors.push_back(L"op должен быть create, update или delete.");
      } else {
        item.errors.push_back(L"op должен быть строкой.");
      }
    }

    // id
    if (const JsonValue* idVal = itemVal.find(L"id")) {
      std::wstring id;
      if (getString(idVal, &id)) {
        item.note.id = id;
      } else {
        item.errors.push_back(L"id должен быть строкой.");
      }
    }
    if ((item.op == ImportOp::Update || item.op == ImportOp::Delete) && item.note.id.empty()) {
      item.errors.push_back(L"id обязателен для update/delete.");
    }

    // For create/update, parse core fields
    if (item.op != ImportOp::Delete) {
      // title
      if (const JsonValue* titleVal = itemVal.find(L"title")) {
        std::wstring title;
        if (getString(titleVal, &title)) {
          item.note.title = title;
        } else {
          item.errors.push_back(L"title должен быть строкой.");
        }
      } else {
        item.errors.push_back(L"title обязателен.");
      }

      // date/time
      std::wstring dateStr;
      std::wstring timeStr;
      SYSTEMTIME date{};
      bool dateOk = false;
      bool timeOk = false;
      bool timeClamped = false;
      if (const JsonValue* dateVal = itemVal.find(L"date")) {
        if (getString(dateVal, &dateStr)) {
          std::wstring dateErr;
          dateOk = parseDate(dateStr, &date, &dateErr);
          if (!dateOk) item.errors.push_back(dateErr);
        } else {
          item.errors.push_back(L"date должен быть строкой.");
        }
      } else {
        item.errors.push_back(L"date обязателен.");
      }

      int hour = 0;
      int minute = 0;
      if (const JsonValue* timeVal = itemVal.find(L"time")) {
        if (getString(timeVal, &timeStr)) {
          std::wstring timeErr;
          timeOk = parseTime(timeStr, &hour, &minute, &timeClamped, &timeErr);
          if (!timeOk) item.errors.push_back(timeErr);
        } else {
          item.errors.push_back(L"time должен быть строкой.");
        }
      } else {
        item.errors.push_back(L"time обязателен.");
      }

      if (dateOk && timeOk) {
        SYSTEMTIME st = date;
        st.wHour = static_cast<WORD>(hour);
        st.wMinute = static_cast<WORD>(minute);
        st.wSecond = 0;
        st.wMilliseconds = 0;
        item.hasDateTime = true;
        item.localDateTime = st;
        item.note.scheduledAtUtcMs = TimeUtils::localSystemTimeToUnixMsUtc(st);
        if (timeClamped) {
          item.warnings.push_back(L"Время было ограничено до 23:59.");
        }
      }

      // reminderMinutesBefore
      int reminder = 0;
      if (const JsonValue* remVal = itemVal.find(L"reminderMinutesBefore")) {
        if (getInt(remVal, &reminder)) {
          if (reminder < 0) {
            item.errors.push_back(L"reminderMinutesBefore не может быть отрицательным.");
            reminder = 0;
          }
        } else {
          item.errors.push_back(L"reminderMinutesBefore должен быть целым числом.");
        }
      }
      item.note.reminderMinutesBefore = reminder;

      // importance
      int importance = 0;
      if (const JsonValue* impVal = itemVal.find(L"importance")) {
        if (impVal->type == JsonValue::Type::String) {
          std::wstring impStr = lowerCopy(impVal->stringValue);
          if (impStr == L"normal") importance = 0;
          else if (impStr == L"important") importance = 1;
          else if (impStr == L"urgent") importance = 2;
          else item.errors.push_back(L"importance должен быть normal, important или urgent.");
        } else if (impVal->type == JsonValue::Type::Number && impVal->numberIsInt) {
          importance = static_cast<int>(impVal->numberInt);
          if (importance < 0 || importance > 2) {
            item.errors.push_back(L"importance вне диапазона 0..2.");
            importance = std::clamp(importance, 0, 2);
          }
        } else {
          item.errors.push_back(L"importance должен быть строкой или числом.");
        }
      }
      item.note.importance = importance;

      // category
      int category = 0;
      if (const JsonValue* catVal = itemVal.find(L"category")) {
        if (getInt(catVal, &category)) {
          if (category < 0 || category > 6) {
            item.errors.push_back(L"category вне диапазона 0..6.");
            category = std::clamp(category, 0, 6);
          }
        } else {
          item.errors.push_back(L"category должен быть целым числом.");
        }
      }
      item.note.category = category;

      // repeat
      item.note.repeatType = RepeatType::None;
      item.note.repeatWeekdaysMask = 0;
      if (const JsonValue* repVal = itemVal.find(L"repeat")) {
        if (repVal->type == JsonValue::Type::Object) {
          std::wstring typeStr;
          if (const JsonValue* typeVal = repVal->find(L"type")) {
            if (getString(typeVal, &typeStr)) {
              const std::wstring low = lowerCopy(typeStr);
              if (low == L"none") item.note.repeatType = RepeatType::None;
              else if (low == L"daily") item.note.repeatType = RepeatType::Daily;
              else if (low == L"weekly") item.note.repeatType = RepeatType::Weekly;
              else item.errors.push_back(L"repeat.type должен быть none, daily или weekly.");
            } else {
              item.errors.push_back(L"repeat.type должен быть строкой.");
            }
          }

          if (item.note.repeatType == RepeatType::Weekly) {
            int mask = 0;
            if (const JsonValue* daysVal = repVal->find(L"weekdays")) {
              if (daysVal->type == JsonValue::Type::Array) {
                for (const auto& dayVal : daysVal->arrayValue) {
                  if (dayVal.type != JsonValue::Type::String) {
                    item.errors.push_back(L"repeat.weekdays должен быть массивом строк.");
                    mask = 0;
                    break;
                  }
                  const int bit = weekdayMaskFromName(dayVal.stringValue);
                  if (bit == 0) {
                    item.errors.push_back(L"repeat.weekdays содержит неизвестный день.");
                    mask = 0;
                    break;
                  }
                  mask |= bit;
                }
              } else {
                item.errors.push_back(L"repeat.weekdays должен быть массивом.");
              }
            }
            if (mask == 0) {
              if (item.hasDateTime) {
                mask = weekdayMaskFromWDayOfWeek(item.localDateTime.wDayOfWeek);
                item.warnings.push_back(L"repeat.weekdays не задан; использован день даты.");
              } else {
                item.errors.push_back(L"repeat.weekdays обязателен для weekly.");
              }
            }
            item.note.repeatWeekdaysMask = mask;
          }
        } else {
          item.errors.push_back(L"repeat должен быть объектом.");
        }
      }

      // content
      if (const JsonValue* contentVal = itemVal.find(L"content")) {
        if (contentVal->type == JsonValue::Type::Object) {
          std::wstring mode;
          std::wstring text;
          bool haveMode = false;
          bool haveText = false;
          if (const JsonValue* modeVal = contentVal->find(L"mode")) {
            haveMode = getString(modeVal, &mode);
            if (!haveMode) item.errors.push_back(L"content.mode должен быть строкой.");
          }
          if (const JsonValue* textVal = contentVal->find(L"text")) {
            haveText = getString(textVal, &text);
            if (!haveText) item.errors.push_back(L"content.text должен быть строкой.");
          }
          if (haveMode && haveText) {
            const std::wstring low = lowerCopy(mode);
            if (low == L"markdown") {
              item.note.contentMode = NoteContentMode::Markdown;
              item.note.contentMarkdown = text;
            } else if (low == L"html") {
              item.note.contentMode = NoteContentMode::Html;
              item.note.contentHtml = text;
            } else if (low == L"rtf") {
              item.note.contentMode = NoteContentMode::VisualRtf;
              item.note.contentRtf = toAnsiBytes(text);
            } else {
              item.errors.push_back(L"content.mode должен быть markdown, html или rtf.");
            }
          }
        } else {
          item.errors.push_back(L"content должен быть объектом.");
        }
      }

      // autoHide
      item.note.autoHideEnabled = false;
      item.note.autoHideSeconds = 0;
      if (const JsonValue* autoVal = itemVal.find(L"autoHide")) {
        if (autoVal->type == JsonValue::Type::Object) {
          bool enabled = false;
          if (const JsonValue* enabledVal = autoVal->find(L"enabled")) {
            if (!getBool(enabledVal, &enabled)) {
              item.errors.push_back(L"autoHide.enabled должен быть boolean.");
            }
          }
          int seconds = 5;
          if (const JsonValue* secVal = autoVal->find(L"seconds")) {
            if (getInt(secVal, &seconds)) {
              if (seconds < 1) {
                item.errors.push_back(L"autoHide.seconds должен быть >= 1.");
                seconds = 5;
              }
            } else {
              item.errors.push_back(L"autoHide.seconds должен быть числом.");
            }
          }
          item.note.autoHideEnabled = enabled;
          item.note.autoHideSeconds = enabled ? std::clamp(seconds, 1, 3600) : 0;
        } else {
          item.errors.push_back(L"autoHide должен быть объектом.");
        }
      }
    }

    out->items.push_back(std::move(item));
  }

  return true;
}

bool ScheduleJson::extractChatCompletionContent(const std::wstring& responseJson, std::wstring* contentOut, std::wstring* errorOut) {
  if (!contentOut) return false;
  contentOut->clear();

  JsonParser parser(responseJson);
  JsonValue root;
  if (!parser.parse(&root)) {
    if (errorOut) *errorOut = parser.error();
    return false;
  }
  if (root.type != JsonValue::Type::Object) {
    if (errorOut) *errorOut = L"Некорректный JSON ответа.";
    return false;
  }

  if (const JsonValue* errVal = root.find(L"error")) {
    if (errVal->type == JsonValue::Type::Object) {
      if (const JsonValue* msg = errVal->find(L"message")) {
        if (msg->type == JsonValue::Type::String) {
          if (errorOut) *errorOut = msg->stringValue;
          return false;
        }
      }
    }
  }

  const JsonValue* choices = root.find(L"choices");
  if (!choices || choices->type != JsonValue::Type::Array || choices->arrayValue.empty()) {
    if (errorOut) *errorOut = L"Ответ не содержит choices.";
    return false;
  }

  const JsonValue& first = choices->arrayValue[0];
  if (first.type == JsonValue::Type::Object) {
    if (const JsonValue* msg = first.find(L"message")) {
      if (msg->type == JsonValue::Type::Object) {
        if (const JsonValue* content = msg->find(L"content")) {
          if (content->type == JsonValue::Type::String) {
            *contentOut = content->stringValue;
            return true;
          }
        }
      }
    }
    if (const JsonValue* text = first.find(L"text")) {
      if (text->type == JsonValue::Type::String) {
        *contentOut = text->stringValue;
        return true;
      }
    }
  }

  if (errorOut) *errorOut = L"Не удалось извлечь content.";
  return false;
}

std::wstring ScheduleJson::buildSchemaLegend() {
  std::wstringstream ss;
  ss << L"{\n"
     << L"  \"mode\": \"merge|replace\",\n"
     << L"  \"notes\": [\n"
     << L"    {\n"
     << L"      \"op\": \"create|update|delete\",\n"
     << L"      \"id\": \"optional (required for update/delete)\",\n"
     << L"      \"date\": \"YYYY-MM-DD\",\n"
     << L"      \"time\": \"HH:MM\",\n"
     << L"      \"title\": \"...\",\n"
     << L"      \"reminderMinutesBefore\": 0,\n"
     << L"      \"importance\": \"normal|important|urgent\" ,\n"
     << L"      \"category\": 0,\n"
     << L"      \"repeat\": { \"type\": \"none|daily|weekly\", \"weekdays\": [\"mon\", \"tue\", \"wed\"] },\n"
     << L"      \"content\": { \"mode\": \"markdown|html|rtf\", \"text\": \"...\" },\n"
     << L"      \"autoHide\": { \"enabled\": true, \"seconds\": 5 }\n"
     << L"    }\n"
     << L"  ]\n"
     << L"}\n\n"
     << L"Пример:\n"
     << L"{\n"
     << L"  \"mode\": \"merge\",\n"
     << L"  \"notes\": [\n"
     << L"    {\n"
     << L"      \"op\": \"create\",\n"
     << L"      \"date\": \"2026-01-24\",\n"
     << L"      \"time\": \"23:30\",\n"
     << L"      \"title\": \"Поздняя встреча\",\n"
     << L"      \"importance\": \"important\",\n"
     << L"      \"content\": { \"mode\": \"markdown\", \"text\": \"**Текст**\" }\n"
     << L"    }\n"
     << L"  ]\n"
     << L"}";
  return ss.str();
}

