#include "ImageRtf.h"

#include <windows.h>
#include <objbase.h>

#include <gdiplus.h>

#include <algorithm>
#include <sstream>

namespace {
class GdiplusSession {
public:
  GdiplusSession() {
    Gdiplus::GdiplusStartupInput in{};
    if (Gdiplus::GdiplusStartup(&token_, &in, nullptr) == Gdiplus::Ok) {
      ok_ = true;
    }
  }
  ~GdiplusSession() {
    if (ok_) {
      Gdiplus::GdiplusShutdown(token_);
    }
  }
  bool ok() const { return ok_; }

private:
  ULONG_PTR token_{};
  bool ok_ = false;
};

const CLSID* pngEncoderClsid() {
  static CLSID clsid{};
  static bool inited = false;
  static bool ok = false;
  if (inited) return ok ? &clsid : nullptr;
  inited = true;

  UINT num = 0;
  UINT size = 0;
  if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) {
    return nullptr;
  }
  auto buf = std::make_unique<BYTE[]>(size);
  auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.get());
  if (Gdiplus::GetImageEncoders(num, size, encoders) != Gdiplus::Ok) {
    return nullptr;
  }
  for (UINT i = 0; i < num; ++i) {
    if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, L"image/png") == 0) {
      clsid = encoders[i].Clsid;
      ok = true;
      return &clsid;
    }
  }
  return nullptr;
}

std::wstring winErr(const wchar_t* prefix, HRESULT hr) {
  std::wstringstream ss;
  ss << prefix << L" (0x" << std::hex << static_cast<unsigned long>(hr) << L")";
  return ss.str();
}

std::wstring bytesToHex(const std::vector<uint8_t>& bytes) {
  static const wchar_t* hex = L"0123456789ABCDEF";
  std::wstring out;
  out.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) {
    out.push_back(hex[(b >> 4) & 0xF]);
    out.push_back(hex[b & 0xF]);
  }
  return out;
}
} // namespace

std::wstring ImageRtf::makePngPictRtfFromFile(const std::wstring& filePath, int maxWidthPx, std::wstring* errorOut) {
  static GdiplusSession gdip;
  if (!gdip.ok()) {
    if (errorOut) *errorOut = L"GDI+ не удалось инициализировать (gdiplus).";
    return {};
  }

  Gdiplus::Bitmap bmp(filePath.c_str(), FALSE);
  if (bmp.GetLastStatus() != Gdiplus::Ok) {
    if (errorOut) *errorOut = L"Не удалось открыть изображение.";
    return {};
  }

  const UINT w = bmp.GetWidth();
  const UINT h = bmp.GetHeight();
  if (w == 0 || h == 0) {
    if (errorOut) *errorOut = L"Некорректные размеры изображения.";
    return {};
  }

  double dpiX = bmp.GetHorizontalResolution();
  double dpiY = bmp.GetVerticalResolution();
  if (dpiX <= 1.0) dpiX = 96.0;
  if (dpiY <= 1.0) dpiY = 96.0;

  // Scale down to fit maxWidthPx
  double scale = 1.0;
  if (maxWidthPx > 0 && static_cast<int>(w) > maxWidthPx) {
    scale = static_cast<double>(maxWidthPx) / static_cast<double>(w);
  }
  const UINT sw = static_cast<UINT>(std::max(1.0, w * scale));
  const UINT sh = static_cast<UINT>(std::max(1.0, h * scale));

  // Encode to PNG in memory
  const CLSID* pngClsid = pngEncoderClsid();
  if (!pngClsid) {
    if (errorOut) *errorOut = L"Не найден PNG encoder в GDI+.";
    return {};
  }

  IStream* stream = nullptr;
  const HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
  if (FAILED(hr) || !stream) {
    if (errorOut) *errorOut = winErr(L"CreateStreamOnHGlobal failed", hr);
    return {};
  }

  std::unique_ptr<IStream, void (*)(IStream*)> streamGuard(stream, [](IStream* s) { s->Release(); });

  // If we scaled, draw into a new bitmap to reduce bytes
  std::unique_ptr<Gdiplus::Bitmap> scaled;
  Gdiplus::Bitmap* toSave = &bmp;
  if (sw != w || sh != h) {
    scaled = std::make_unique<Gdiplus::Bitmap>(sw, sh, PixelFormat32bppARGB);
    Gdiplus::Graphics g(scaled.get());
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(&bmp, 0, 0, sw, sh);
    toSave = scaled.get();
  }

  if (toSave->Save(stream, pngClsid, nullptr) != Gdiplus::Ok) {
    if (errorOut) *errorOut = L"Не удалось закодировать PNG.";
    return {};
  }

  // Extract bytes
  HGLOBAL hglob = nullptr;
  const HRESULT hr2 = GetHGlobalFromStream(stream, &hglob);
  if (FAILED(hr2) || !hglob) {
    if (errorOut) *errorOut = winErr(L"GetHGlobalFromStream failed", hr2);
    return {};
  }

  const SIZE_T size = GlobalSize(hglob);
  void* ptr = GlobalLock(hglob);
  if (!ptr || size == 0) {
    if (errorOut) *errorOut = L"GlobalLock/GlobalSize failed.";
    if (ptr) GlobalUnlock(hglob);
    return {};
  }

  std::vector<uint8_t> bytes;
  bytes.resize(size);
  memcpy(bytes.data(), ptr, size);
  GlobalUnlock(hglob);

  // RTF sizes:
  // \picw/\pich in pixels; \picwgoal/\pichgoal in twips.
  const int picw = static_cast<int>(sw);
  (void)picw;
  const int pich = static_cast<int>(sh);
  (void)pich;
  const int picwgoal = static_cast<int>(std::lround(static_cast<double>(sw) * 1440.0 / dpiX));
  const int pichgoal = static_cast<int>(std::lround(static_cast<double>(sh) * 1440.0 / dpiY));

  const std::wstring hex = bytesToHex(bytes);

  std::wstringstream rtf;
  rtf << L"{\\rtf1\\ansi\\deff0";
  rtf << L"{\\pict\\pngblip"
      << L"\\picw" << sw << L"\\pich" << sh
      << L"\\picwgoal" << picwgoal << L"\\pichgoal" << pichgoal << L"\n";
  // Insert hex in lines for readability (optional)
  const size_t line = 120;
  for (size_t i = 0; i < hex.size(); i += line) {
    rtf << hex.substr(i, std::min(line, hex.size() - i)) << L"\n";
  }
  rtf << L"}\\par}";

  return rtf.str();
}


