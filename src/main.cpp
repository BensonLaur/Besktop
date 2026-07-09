#include <windows.h>

#include "besktop/app/stage_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    return besktop::RunStageWindow(instance, showCommand);
}
