#pragma once

#include <windows.h>

#include "besktop/desktop/desktop_snapshot.h"

namespace besktop {

class TaskbarRenderer {
public:
    bool Draw(HDC hdc, const RECT& clientRect, const DesktopSnapshot& snapshot) const;
};

} // namespace besktop
