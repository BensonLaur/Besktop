#include "besktop/anim3d/app.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    return besktop::anim3d::RunAnimationApp(instance, showCommand, false);
}
