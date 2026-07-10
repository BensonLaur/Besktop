#include "besktop/render/taskbar_renderer.h"

#include <algorithm>

namespace besktop {

bool TaskbarRenderer::Draw(HDC hdc, const RECT& clientRect, const DesktopSnapshot& snapshot) const
{
    const TaskbarSnapshot& taskbar = snapshot.taskbar;
    const int monitorWidth = snapshot.monitorBounds.right - snapshot.monitorBounds.left;
    const int monitorHeight = snapshot.monitorBounds.bottom - snapshot.monitorBounds.top;
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = clientRect.bottom - clientRect.top;
    if (!taskbar.captureSucceeded || taskbar.bgraPixels.empty() ||
        taskbar.pixelWidth <= 0 || taskbar.pixelHeight <= 0 ||
        monitorWidth <= 0 || monitorHeight <= 0 || clientWidth <= 0 || clientHeight <= 0) {
        return false;
    }

    const auto mapX = [&](LONG value) {
        return clientRect.left + static_cast<int>(
            static_cast<long long>(value - snapshot.monitorBounds.left) * clientWidth / monitorWidth);
    };
    const auto mapY = [&](LONG value) {
        return clientRect.top + static_cast<int>(
            static_cast<long long>(value - snapshot.monitorBounds.top) * clientHeight / monitorHeight);
    };
    const int left = mapX(taskbar.screenBounds.left);
    const int top = mapY(taskbar.screenBounds.top);
    const int right = mapX(taskbar.screenBounds.right);
    const int bottom = mapY(taskbar.screenBounds.bottom);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = taskbar.pixelWidth;
    info.bmiHeader.biHeight = -taskbar.pixelHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return StretchDIBits(
        hdc, left, top, right - left, bottom - top,
        0, 0, taskbar.pixelWidth, taskbar.pixelHeight,
        taskbar.bgraPixels.data(), &info, DIB_RGB_COLORS, SRCCOPY) != GDI_ERROR;
}

} // namespace besktop
