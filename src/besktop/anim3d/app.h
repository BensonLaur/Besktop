#pragma once
#include <windows.h>

namespace besktop::anim3d
{
// Ordinary lab never captures the desktop. Real desktop read is opt-in through stage mode.
int RunAnimationApp(HINSTANCE instance, int showCommand, bool desktopStage);
} // namespace besktop::anim3d
