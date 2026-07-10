#pragma once

#include <windows.h>

namespace besktop {

struct RuntimeOptions;

int RunStageWindow(HINSTANCE instance, int showCommand, const RuntimeOptions& options);

} // namespace besktop
