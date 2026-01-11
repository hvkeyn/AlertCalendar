#pragma once

#include <string>
#include <windows.h>

namespace RichEditUtil {
// Loads msftedit.dll (RichEdit 5.0). Safe to call multiple times.
bool ensureLoaded();

// Set/Read RTF as raw bytes (binary-safe: images can use \bin blocks).
bool setRtfBytes(HWND hwndRichEdit, const std::string& rtf);
std::string getRtfBytes(HWND hwndRichEdit);

// Convenience: Unicode RTF (for generated RTF from Markdown/HTML converters).
bool setRtfW(HWND hwndRichEdit, const std::wstring& rtf);

// Insert RTF at current selection (replaces selection).
bool insertRtfAtSelectionBytes(HWND hwndRichEdit, const std::string& rtf);

// Formatting helpers (WYSIWYG)
void toggleBold(HWND hwndRichEdit);
void toggleItalic(HWND hwndRichEdit);
void toggleUnderline(HWND hwndRichEdit);
void toggleBullet(HWND hwndRichEdit);
void setFontSizePt(HWND hwndRichEdit, int pt);
}


