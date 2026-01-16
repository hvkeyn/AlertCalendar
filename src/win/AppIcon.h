#pragma once

#include "win/UiTheme.h"

#include <windows.h>

namespace AppIcon {
  HICON createIcon(int sizePx, UiThemeStyle style);
}
