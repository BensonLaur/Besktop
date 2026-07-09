#pragma once

#include <windows.h>

#include "besktop/desktop/desktop_snapshot.h"

namespace besktop {

class StageWindow {
public:
    explicit StageWindow(HINSTANCE instance);

    StageWindow(const StageWindow&) = delete;
    StageWindow& operator=(const StageWindow&) = delete;

    int Run(int showCommand);

private:
    bool Create(int showCommand);
    void Close();
    void Paint();
    void RegisterForceExitHotkey();
    void UnregisterForceExitHotkey();

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    DesktopSnapshot snapshot_;
    bool forceExitHotkeyRegistered_ = false;
};

} // namespace besktop
