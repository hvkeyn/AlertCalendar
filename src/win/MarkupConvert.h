#pragma once

#include <string>

namespace MarkupConvert {
// Very lightweight converters for preview/conversion (not full spec).
std::wstring markdownToRtf(const std::wstring& markdown);
std::wstring htmlToRtf(const std::wstring& html);

// Lightweight Markdown -> HTML (for WebView2 preview). Not full spec.
std::wstring markdownToHtml(const std::wstring& markdown);
}


