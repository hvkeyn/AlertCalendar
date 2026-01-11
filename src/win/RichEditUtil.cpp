#include "RichEditUtil.h"

#include <richedit.h>
#include <tom.h>

#include <algorithm>
#include <vector>
#include <string>

// Some SDKs don't expose this flag, but RichEdit supports it for streaming embedded objects.
#ifndef SFF_PERSISTVIEWS
#define SFF_PERSISTVIEWS 0x00002000
#endif

namespace {
struct InCookie {
  const wchar_t* data = nullptr;
  size_t lenChars = 0;
  size_t posChars = 0;
};

DWORD CALLBACK streamInCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
  auto* c = reinterpret_cast<InCookie*>(dwCookie);
  if (!c || !c->data || cb <= 0) {
    *pcb = 0;
    return 0;
  }

  const size_t bytesLeft = (c->lenChars - c->posChars) * sizeof(wchar_t);
  const size_t toCopy = std::min(static_cast<size_t>(cb), bytesLeft);
  if (toCopy == 0) {
    *pcb = 0;
    return 0;
  }

  const BYTE* src = reinterpret_cast<const BYTE*>(c->data + c->posChars);
  std::copy_n(src, toCopy, pbBuff);
  c->posChars += toCopy / sizeof(wchar_t);
  *pcb = static_cast<LONG>(toCopy);
  return 0;
}

// Byte version for RTF with binary/hex data (images)
struct InCookieAnsi {
  const char* data = nullptr;
  size_t len = 0;
  size_t pos = 0;
};

DWORD CALLBACK streamInCallbackAnsi(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
  auto* c = reinterpret_cast<InCookieAnsi*>(dwCookie);
  if (!c || !c->data || cb <= 0) {
    *pcb = 0;
    return 0;
  }

  const size_t bytesLeft = c->len - c->pos;
  const size_t toCopy = std::min(static_cast<size_t>(cb), bytesLeft);
  if (toCopy == 0) {
    *pcb = 0;
    return 0;
  }

  std::copy_n(c->data + c->pos, toCopy, pbBuff);
  c->pos += toCopy;
  *pcb = static_cast<LONG>(toCopy);
  return 0;
}

struct OutCookie {
  std::wstring out;
};

DWORD CALLBACK streamOutCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
  auto* c = reinterpret_cast<OutCookie*>(dwCookie);
  if (!c || !pbBuff || cb <= 0) {
    *pcb = 0;
    return 0;
  }

  // Unicode stream: pbBuff contains UTF-16LE bytes
  const size_t chars = static_cast<size_t>(cb) / sizeof(wchar_t);
  const wchar_t* ws = reinterpret_cast<const wchar_t*>(pbBuff);
  c->out.append(ws, ws + chars);
  *pcb = cb;
  return 0;
}

// ANSI version for RTF export (preserves image hex data correctly)
struct OutCookieAnsi {
  std::string out;
};

DWORD CALLBACK streamOutCallbackAnsi(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
  auto* c = reinterpret_cast<OutCookieAnsi*>(dwCookie);
  if (!c || !pbBuff || cb <= 0) {
    *pcb = 0;
    return 0;
  }

  const char* data = reinterpret_cast<const char*>(pbBuff);
  c->out.append(data, static_cast<size_t>(cb));
  *pcb = cb;
  return 0;
}

static void applySimpleCharFormat(HWND hwnd, DWORD mask, DWORD effects) {
  CHARFORMAT2W cf{};
  cf.cbSize = sizeof(cf);
  cf.dwMask = mask;
  cf.dwEffects = effects;
  SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

static DWORD getSelectionEffects(HWND hwnd, DWORD maskEffect) {
  CHARFORMAT2W cf{};
  cf.cbSize = sizeof(cf);
  cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
  SendMessageW(hwnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
  return cf.dwEffects & maskEffect;
}
} // namespace

bool RichEditUtil::ensureLoaded() {
  static bool loaded = false;
  static bool tried = false;
  if (tried) {
    return loaded;
  }
  tried = true;
  const HMODULE h = LoadLibraryW(L"Msftedit.dll");
  loaded = (h != nullptr);
  return loaded;
}

bool RichEditUtil::setRtfBytes(HWND hwndRichEdit, const std::string& rtf) {
  if (!hwndRichEdit) return false;
  if (rtf.empty()) return true;

  InCookieAnsi cookie;
  cookie.data = rtf.data();
  cookie.len = rtf.size();
  cookie.pos = 0;

  EDITSTREAM es{};
  es.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
  es.pfnCallback = streamInCallbackAnsi;

  const LRESULT res = SendMessageW(hwndRichEdit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
  return res == 0;
}

bool RichEditUtil::setRtfW(HWND hwndRichEdit, const std::wstring& rtf) {
  if (!hwndRichEdit) return false;
  if (rtf.empty()) return true;
  InCookie cookie;
  cookie.data = rtf.c_str();
  cookie.lenChars = rtf.size();
  cookie.posChars = 0;

  EDITSTREAM es{};
  es.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
  es.pfnCallback = streamInCallback;

  const LRESULT res = SendMessageW(hwndRichEdit, EM_STREAMIN, SF_RTF | SF_UNICODE, reinterpret_cast<LPARAM>(&es));
  return res == 0;
}

bool RichEditUtil::insertRtfAtSelectionBytes(HWND hwndRichEdit, const std::string& rtf) {
  if (!hwndRichEdit) return false;
  if (rtf.empty()) return true;

  InCookieAnsi cookie;
  cookie.data = rtf.data();
  cookie.len = rtf.size();
  cookie.pos = 0;

  EDITSTREAM es{};
  es.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
  es.pfnCallback = streamInCallbackAnsi;

  const LRESULT res = SendMessageW(hwndRichEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, reinterpret_cast<LPARAM>(&es));
  return res == 0;
}

std::string RichEditUtil::getRtfBytes(HWND hwndRichEdit) {
  if (!hwndRichEdit) return {};
  OutCookieAnsi cookie;

  EDITSTREAM es{};
  es.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
  es.pfnCallback = streamOutCallbackAnsi;

  // SFF_PERSISTVIEWS is important to preserve embedded objects/images when streaming out.
  SendMessageW(hwndRichEdit, EM_STREAMOUT, SF_RTF | SFF_PERSISTVIEWS, reinterpret_cast<LPARAM>(&es));
  return cookie.out;
}

void RichEditUtil::toggleBold(HWND hwndRichEdit) {
  const bool isBold = (getSelectionEffects(hwndRichEdit, CFE_BOLD) != 0);
  applySimpleCharFormat(hwndRichEdit, CFM_BOLD, isBold ? 0 : CFE_BOLD);
}

void RichEditUtil::toggleItalic(HWND hwndRichEdit) {
  const bool isItalic = (getSelectionEffects(hwndRichEdit, CFE_ITALIC) != 0);
  applySimpleCharFormat(hwndRichEdit, CFM_ITALIC, isItalic ? 0 : CFE_ITALIC);
}

void RichEditUtil::toggleUnderline(HWND hwndRichEdit) {
  const bool isUnder = (getSelectionEffects(hwndRichEdit, CFE_UNDERLINE) != 0);
  applySimpleCharFormat(hwndRichEdit, CFM_UNDERLINE, isUnder ? 0 : CFE_UNDERLINE);
}

void RichEditUtil::toggleBullet(HWND hwndRichEdit) {
  PARAFORMAT2 pf{};
  pf.cbSize = sizeof(pf);
  pf.dwMask = PFM_NUMBERING;
  SendMessageW(hwndRichEdit, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));

  const bool isBulleted = (pf.wNumbering != 0);
  PARAFORMAT2 set{};
  set.cbSize = sizeof(set);
  set.dwMask = PFM_NUMBERING;
  set.wNumbering = isBulleted ? 0 : PFN_BULLET;
  SendMessageW(hwndRichEdit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&set));
}

void RichEditUtil::setFontSizePt(HWND hwndRichEdit, int pt) {
  if (pt < 6) pt = 6;
  if (pt > 72) pt = 72;
  CHARFORMAT2W cf{};
  cf.cbSize = sizeof(cf);
  cf.dwMask = CFM_SIZE;
  cf.yHeight = pt * 20; // twips
  SendMessageW(hwndRichEdit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}


