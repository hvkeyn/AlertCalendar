#include "UiTheme.h"

UiTheme UiTheme::fromStyle(UiThemeStyle s) {
  UiTheme t;
  t.style = s;

  if (s == UiThemeStyle::Minimal) {
    // Clean white minimal theme
    t.windowBg = RGB(252, 252, 254);
    t.panelBg = RGB(255, 255, 255);
    t.headerBg = RGB(248, 249, 252);
    t.gridLine = RGB(225, 228, 235);

    t.text = RGB(24, 26, 32);
    t.mutedText = RGB(120, 125, 140);

    t.accent = RGB(59, 130, 246);  // Nice blue
    t.accentSoft = RGB(239, 246, 255);

    t.weekend = RGB(239, 68, 68);

    t.badgeNormal = t.accent;
    t.badgeImportant = RGB(245, 158, 11);
    t.badgeUrgent = RGB(239, 68, 68);

    t.editorBg = RGB(255, 255, 255);
    t.editorText = RGB(24, 26, 32);
    return t;
  }

  // Premium - elegant with subtle gradients feel
  t.windowBg = RGB(245, 247, 250);
  t.panelBg = RGB(255, 255, 255);
  t.headerBg = RGB(238, 242, 248);
  t.gridLine = RGB(215, 220, 230);

  t.text = RGB(17, 24, 39);
  t.mutedText = RGB(107, 114, 128);

  t.accent = RGB(79, 70, 229);   // Indigo/violet - premium feel
  t.accentSoft = RGB(238, 242, 255);

  t.weekend = RGB(220, 38, 38);

  t.badgeNormal = RGB(59, 130, 246);
  t.badgeImportant = RGB(245, 158, 11);
  t.badgeUrgent = RGB(220, 38, 38);

  t.editorBg = RGB(253, 253, 255);
  t.editorText = RGB(17, 24, 39);
  return t;
}

