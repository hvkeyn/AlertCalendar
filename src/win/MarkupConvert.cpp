#include "MarkupConvert.h"

#include <algorithm>

namespace {
std::wstring markdownInlineToHtml(const std::wstring& line) {
  std::wstring out;
  out.reserve(line.size() * 2);

  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool code = false;

  for (size_t i = 0; i < line.size();) {
    if (!code && i + 1 < line.size() && line[i] == L'*' && line[i + 1] == L'*') {
      out += bold ? L"</strong>" : L"<strong>";
      bold = !bold;
      i += 2;
      continue;
    }
    if (!code && i + 1 < line.size() && line[i] == L'_' && line[i + 1] == L'_') {
      out += underline ? L"</u>" : L"<u>";
      underline = !underline;
      i += 2;
      continue;
    }
    if (!code && line[i] == L'*') {
      // italic toggle (single *)
      const size_t next = line.find(L'*', i + 1);
      if (next != std::wstring::npos) {
        out += italic ? L"</em>" : L"<em>";
        italic = !italic;
        ++i;
        continue;
      }
    }
    if (line[i] == L'`') {
      out += code ? L"</code>" : L"<code>";
      code = !code;
      ++i;
      continue;
    }

    // Escape current character
    const wchar_t ch = line[i];
    if (ch == L'&') out += L"&amp;";
    else if (ch == L'<') out += L"&lt;";
    else if (ch == L'>') out += L"&gt;";
    else if (ch == L'"') out += L"&quot;";
    else if (ch == L'\'') out += L"&#39;";
    else out.push_back(ch);
    ++i;
  }

  // Close any unclosed tags to keep HTML valid.
  if (code) out += L"</code>";
  if (underline) out += L"</u>";
  if (italic) out += L"</em>";
  if (bold) out += L"</strong>";

  return out;
}

void rtfAppendEscaped(std::wstring& out, wchar_t ch) {
  switch (ch) {
    case L'\\': out += L"\\\\"; return;
    case L'{': out += L"\\{"; return;
    case L'}': out += L"\\}"; return;
    case L'\r': return;
    case L'\n': out += L"\\par\n"; return;
    default:
      if (ch < 0x20) {
        return;
      }
      out.push_back(ch);
      return;
  }
}

void rtfAppendText(std::wstring& out, const std::wstring& s) {
  for (wchar_t ch : s) {
    rtfAppendEscaped(out, ch);
  }
}

std::wstring rtfWrapBody(const std::wstring& body) {
  // NOTE: Keep RTF compatible with RichEdit (Msftedit). Provide 2 fonts for nicer preview:
  // f0 = UI, f1 = monospace for inline code.
  std::wstring out =
    L"{\\rtf1\\ansi\\deff0"
    L"{\\fonttbl{\\f0 Segoe UI;}{\\f1 Consolas;}}"
    // Page-like defaults: comfortable margins + line/paragraph spacing
    L"\\viewkind4\\uc1"
    L"\\margl720\\margr720"
    L"\\pard\\fs22\\sl276\\slmult1\\sa120\n";
  out += body;
  out += L"}";
  return out;
}

bool ieq(wchar_t a, wchar_t b) {
  if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a - L'A' + L'a');
  if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b - L'A' + L'a');
  return a == b;
}

bool startsWithI(const std::wstring& s, size_t pos, const wchar_t* lit) {
  size_t i = 0;
  while (lit[i] != 0) {
    if (pos + i >= s.size()) return false;
    if (!ieq(s[pos + i], lit[i])) return false;
    ++i;
  }
  return true;
}
} // namespace

std::wstring MarkupConvert::markdownToRtf(const std::wstring& markdown) {
  std::wstring body;
  body.reserve(markdown.size() * 2);

  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool code = false;

  for (size_t i = 0; i < markdown.size();) {
    // Headings: "# ", "## ", ... at start of line.
    if (!code && (i == 0 || markdown[i - 1] == L'\n') && markdown[i] == L'#') {
      size_t j = i;
      int level = 0;
      while (j < markdown.size() && markdown[j] == L'#' && level < 6) {
        ++level;
        ++j;
      }
      if (level > 0 && j < markdown.size() && markdown[j] == L' ') {
        // Close any inline states to keep output predictable.
        if (bold) { body += L"\\b0 "; bold = false; }
        if (italic) { body += L"\\i0 "; italic = false; }
        if (underline) { body += L"\\ul0 "; underline = false; }

        int fs = 22; // default
        if (level == 1) fs = 36;
        else if (level == 2) fs = 32;
        else if (level == 3) fs = 28;
        else if (level == 4) fs = 24;
        else fs = 22;

        if (!body.empty()) body += L"\\par\n";
        body += L"\\pard\\sb120\\sa120\\b\\fs";
        body += std::to_wstring(fs);
        body += L" ";

        i = j + 1; // skip hashes and the space
        while (i < markdown.size() && markdown[i] != L'\n') {
          rtfAppendEscaped(body, markdown[i]);
          ++i;
        }

        body += L"\\b0\\fs22\\par\n\\pard ";
        if (i < markdown.size() && markdown[i] == L'\n') ++i;
        continue;
      }
    }

    if (!code && i + 1 < markdown.size() && markdown[i] == L'*' && markdown[i + 1] == L'*') {
      body += bold ? L"\\b0 " : L"\\b ";
      bold = !bold;
      i += 2;
      continue;
    }
    if (!code && i + 1 < markdown.size() && markdown[i] == L'_' && markdown[i + 1] == L'_') {
      body += underline ? L"\\ul0 " : L"\\ul ";
      underline = !underline;
      i += 2;
      continue;
    }
    if (!code && markdown[i] == L'*') {
      // italic toggle: only if there's another '*' ahead on the same line
      const size_t next = markdown.find(L'*', i + 1);
      const size_t eol = markdown.find(L'\n', i + 1);
      if (next != std::wstring::npos && (eol == std::wstring::npos || next < eol)) {
        body += italic ? L"\\i0 " : L"\\i ";
        italic = !italic;
        ++i;
        continue;
      }
    }
    if (markdown[i] == L'`') {
      if (!code) {
        body += L"\\f1\\fs20 ";
      } else {
        body += L"\\f0\\fs22 ";
      }
      code = !code;
      ++i;
      continue;
    }

    // bullet list (very lightweight): lines starting with "- " or "* "
    if ((i == 0 || markdown[i - 1] == L'\n') && i + 1 < markdown.size()) {
      if ((markdown[i] == L'-' || markdown[i] == L'*') && markdown[i + 1] == L' ') {
        body += L"\\tab \\u8226? ";
        i += 2;
        continue;
      }
    }

    rtfAppendEscaped(body, markdown[i]);
    ++i;
  }

  if (bold) body += L"\\b0 ";
  if (italic) body += L"\\i0 ";
  if (underline) body += L"\\ul0 ";
  if (code) body += L"\\f0\\fs22 ";

  return rtfWrapBody(body);
}

std::wstring MarkupConvert::htmlToRtf(const std::wstring& html) {
  std::wstring body;
  body.reserve(html.size() * 2);

  bool bold = false;
  bool italic = false;
  bool underline = false;

  for (size_t i = 0; i < html.size();) {
    if (html[i] == L'<') {
      // find end tag
      const size_t end = html.find(L'>', i + 1);
      if (end == std::wstring::npos) {
        break;
      }
      const bool closing = (i + 1 < html.size() && html[i + 1] == L'/');
      const size_t namePos = closing ? i + 2 : i + 1;

      if (startsWithI(html, namePos, L"br") || startsWithI(html, namePos, L"br/")) {
        body += L"\\par\n";
      } else if (startsWithI(html, namePos, L"h1")) {
        if (closing) {
          body += L"\\b0\\fs22\\par\n\\pard ";
        } else {
          body += L"\\par\\pard\\sb120\\sa120\\b\\fs36 ";
        }
      } else if (startsWithI(html, namePos, L"h2")) {
        if (closing) {
          body += L"\\b0\\fs22\\par\n\\pard ";
        } else {
          body += L"\\par\\pard\\sb120\\sa120\\b\\fs32 ";
        }
      } else if (startsWithI(html, namePos, L"h3")) {
        if (closing) {
          body += L"\\b0\\fs22\\par\n\\pard ";
        } else {
          body += L"\\par\\pard\\sb120\\sa120\\b\\fs28 ";
        }
      } else if (startsWithI(html, namePos, L"p")) {
        if (closing) body += L"\\par\n";
      } else if (startsWithI(html, namePos, L"div")) {
        if (closing) body += L"\\par\n";
      } else if (startsWithI(html, namePos, L"b") || startsWithI(html, namePos, L"strong")) {
        body += closing ? L"\\b0 " : L"\\b ";
        bold = !closing;
      } else if (startsWithI(html, namePos, L"i") || startsWithI(html, namePos, L"em")) {
        body += closing ? L"\\i0 " : L"\\i ";
        italic = !closing;
      } else if (startsWithI(html, namePos, L"u")) {
        body += closing ? L"\\ul0 " : L"\\ul ";
        underline = !closing;
      } else if (startsWithI(html, namePos, L"li")) {
        if (!closing) body += L"\\tab \\u8226? ";
        else body += L"\\par\n";
      } else if (startsWithI(html, namePos, L"ul") || startsWithI(html, namePos, L"ol")) {
        if (closing) body += L"\\par\n";
      }

      i = end + 1;
      continue;
    }

    // Basic HTML entities
    if (html[i] == L'&') {
      const size_t end = html.find(L';', i + 1);
      if (end != std::wstring::npos && end > i && (end - i) <= 10) {
        const std::wstring ent = html.substr(i, end - i + 1);
        if (ent == L"&amp;") { rtfAppendEscaped(body, L'&'); i = end + 1; continue; }
        if (ent == L"&lt;") { rtfAppendEscaped(body, L'<'); i = end + 1; continue; }
        if (ent == L"&gt;") { rtfAppendEscaped(body, L'>'); i = end + 1; continue; }
        if (ent == L"&quot;") { rtfAppendEscaped(body, L'"'); i = end + 1; continue; }
        if (ent == L"&#39;") { rtfAppendEscaped(body, L'\''); i = end + 1; continue; }
        if (ent == L"&nbsp;") { rtfAppendEscaped(body, L' '); i = end + 1; continue; }
      }
    }

    rtfAppendEscaped(body, html[i]);
    ++i;
  }

  if (bold) body += L"\\b0 ";
  if (italic) body += L"\\i0 ";
  if (underline) body += L"\\ul0 ";

  return rtfWrapBody(body);
}

std::wstring MarkupConvert::markdownToHtml(const std::wstring& markdown) {
  // Simple, readable HTML that can be styled by the outer page (WebView2).
  std::wstring out;
  out.reserve(markdown.size() * 2);

  bool inList = false;

  size_t pos = 0;
  while (pos <= markdown.size()) {
    size_t end = markdown.find(L'\n', pos);
    if (end == std::wstring::npos) end = markdown.size();
    std::wstring line = markdown.substr(pos, end - pos);
    if (!line.empty() && line.back() == L'\r') line.pop_back();

    // Trim right spaces
    while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) line.pop_back();

    if (line.empty()) {
      if (inList) {
        out += L"</ul>";
        inList = false;
      }
      out += L"<div class=\"spacer\"></div>";
      pos = end + 1;
      continue;
    }

    // Headings (# .. ######)
    if (line[0] == L'#') {
      int level = 0;
      size_t i = 0;
      while (i < line.size() && line[i] == L'#' && level < 6) {
        ++level;
        ++i;
      }
      if (level > 0 && i < line.size() && line[i] == L' ') {
        if (inList) {
          out += L"</ul>";
          inList = false;
        }
        const std::wstring text = line.substr(i + 1);
        out += L"<h";
        out += std::to_wstring(level);
        out += L">";
        out += markdownInlineToHtml(text);
        out += L"</h";
        out += std::to_wstring(level);
        out += L">";
        pos = end + 1;
        continue;
      }
    }

    // Bullet list
    if (line.size() >= 2 && (line[0] == L'-' || line[0] == L'*') && line[1] == L' ') {
      if (!inList) {
        out += L"<ul>";
        inList = true;
      }
      const std::wstring item = line.substr(2);
      out += L"<li>";
      out += markdownInlineToHtml(item);
      out += L"</li>";
      pos = end + 1;
      continue;
    }

    // Normal paragraph
    if (inList) {
      out += L"</ul>";
      inList = false;
    }

    out += L"<p>";
    out += markdownInlineToHtml(line);
    out += L"</p>";
    pos = end + 1;
  }

  if (inList) out += L"</ul>";
  return out;
}


