#pragma once

#include <windows.h>

enum class UiThemeStyle : int {
  Premium = 0,
  Minimal = 1
};

struct UiTheme {
  UiThemeStyle style = UiThemeStyle::Premium;

  COLORREF windowBg{};
  COLORREF panelBg{};
  COLORREF headerBg{};
  COLORREF gridLine{};

  COLORREF text{};
  COLORREF mutedText{};

  COLORREF accent{};
  COLORREF accentSoft{};

  COLORREF weekend{};

  COLORREF badgeNormal{};
  COLORREF badgeImportant{};
  COLORREF badgeUrgent{};

  COLORREF editorBg{};
  COLORREF editorText{};

  static UiTheme fromStyle(UiThemeStyle s);
};

