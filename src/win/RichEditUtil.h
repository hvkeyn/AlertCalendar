#pragma once

#include <string>
#include <windows.h>

namespace RichEditUtil {
// Loads msftedit.dll (RichEdit 5.0). Safe to call multiple times.
bool ensureLoaded();

// Set/Read RTF using EM_STREAMIN/EM_STREAMOUT in Unicode mode.
bool setRtf(HWND hwndRichEdit, const std::wstring& rtf);
std::wstring getRtf(HWND hwndRichEdit);

// Insert RTF at current selection (replaces selection).
bool insertRtfAtSelection(HWND hwndRichEdit, const std::wstring& rtf);

// Formatting helpers (WYSIWYG)
void toggleBold(HWND hwndRichEdit);
void toggleItalic(HWND hwndRichEdit);
void toggleUnderline(HWND hwndRichEdit);
void toggleBullet(HWND hwndRichEdit);
void setFontSizePt(HWND hwndRichEdit, int pt);
}


