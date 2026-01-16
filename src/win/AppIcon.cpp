#include "AppIcon.h"

#include "win/UiTheme.h"

#include <objbase.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <memory>

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
  ULONG_PTR token_ = 0;
  bool ok_ = false;
};

Gdiplus::Color toColor(COLORREF c, BYTE a = 255) {
  return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

void addRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float radius) {
  const float d = radius * 2.0f;
  path.StartFigure();
  path.AddArc(r.X, r.Y, d, d, 180, 90);
  path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
  path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
  path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
  path.CloseFigure();
}
} // namespace

namespace AppIcon {
HICON createIcon(int sizePx, UiThemeStyle style) {
  static GdiplusSession gdip;
  if (!gdip.ok() || sizePx <= 0) return nullptr;

  UiTheme theme = UiTheme::fromStyle(style);
  Gdiplus::Bitmap bmp(sizePx, sizePx, PixelFormat32bppARGB);
  Gdiplus::Graphics g(&bmp);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  g.Clear(Gdiplus::Color(0, 0, 0, 0));

  const float pad = sizePx * 0.08f;
  const float radius = sizePx * 0.12f;
  Gdiplus::RectF body(pad, pad, sizePx - pad * 2.0f, sizePx - pad * 2.0f);

  Gdiplus::GraphicsPath bodyPath;
  addRoundRect(bodyPath, body, radius);
  Gdiplus::SolidBrush bodyBrush(toColor(theme.panelBg));
  Gdiplus::Pen borderPen(toColor(theme.gridLine), std::max(1.0f, sizePx * 0.02f));
  g.FillPath(&bodyBrush, &bodyPath);
  g.DrawPath(&borderPen, &bodyPath);

  const float headerH = body.Height * 0.28f;
  Gdiplus::RectF header(body.X, body.Y, body.Width, headerH);
  Gdiplus::SolidBrush headerBrush(toColor(theme.accent));
  g.FillRectangle(&headerBrush, header);

  const float ringSize = sizePx * 0.12f;
  const float ringY = header.Y + headerH * 0.2f;
  Gdiplus::SolidBrush ringBrush(toColor(theme.panelBg));
  g.FillEllipse(&ringBrush, header.X + body.Width * 0.2f, ringY, ringSize, ringSize);
  g.FillEllipse(&ringBrush, header.X + body.Width * 0.6f, ringY, ringSize, ringSize);

  const float gridTop = header.Y + headerH + sizePx * 0.05f;
  const float gridLeft = body.X + sizePx * 0.08f;
  const float gridRight = body.GetRight() - sizePx * 0.08f;
  const float gridBottom = body.GetBottom() - sizePx * 0.08f;
  Gdiplus::Pen gridPen(toColor(theme.gridLine), std::max(1.0f, sizePx * 0.015f));
  for (int i = 1; i <= 2; ++i) {
    const float x = gridLeft + (gridRight - gridLeft) * (i / 3.0f);
    g.DrawLine(&gridPen, x, gridTop, x, gridBottom);
  }
  for (int i = 1; i <= 2; ++i) {
    const float y = gridTop + (gridBottom - gridTop) * (i / 3.0f);
    g.DrawLine(&gridPen, gridLeft, y, gridRight, y);
  }

  Gdiplus::FontFamily fontFamily(L"Segoe UI");
  const float fontSize = sizePx * 0.34f;
  Gdiplus::Font font(&fontFamily, fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush textBrush(toColor(theme.text));
  Gdiplus::RectF textRect(body.X, body.Y + headerH, body.Width, body.Height - headerH);
  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  g.DrawString(L"15", -1, &font, textRect, &fmt, &textBrush);

  HICON icon = nullptr;
  if (bmp.GetHICON(&icon) != Gdiplus::Ok) {
    return nullptr;
  }
  return icon;
}
} // namespace AppIcon
