#include <windows.h>

#include "besktop/app/stage_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    besktop::StageWindow stageWindow(instance);
    return stageWindow.Run(showCommand);
}
