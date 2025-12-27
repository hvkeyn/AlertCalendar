#pragma once

#include <string>
#include <vector>

namespace ImageRtf {
// Converts an image file to an RTF fragment with \pict\pngblip (bytes embedded).
// The returned RTF is a self-contained fragment (safe to stream-in with SFF_SELECTION).
// maxWidthPx: if > 0, the image is scaled down to fit this width.
std::wstring makePngPictRtfFromFile(const std::wstring& filePath, int maxWidthPx, std::wstring* errorOut = nullptr);
}


