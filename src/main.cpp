#include <windows.h>
#include <shellscalingapi.h>

#include <string>

#include "besktop/app/stage_window.h"
#include "besktop/app/runtime_options.h"
#include "besktop/logging/logger.h"

namespace {

std::wstring FormatHResult(HRESULT result)
{
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(result));
    return buffer;
}

UINT GetDpiForSystemCompat()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using GetDpiForSystemFn = UINT(WINAPI*)();
        auto* getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(
            GetProcAddress(user32, "GetDpiForSystem"));
        if (getDpiForSystem != nullptr) {
            return getDpiForSystem();
        }
    }

    HDC screenDc = GetDC(nullptr);
    const UINT dpi = screenDc != nullptr ? static_cast<UINT>(GetDeviceCaps(screenDc, LOGPIXELSX)) : 96;
    if (screenDc != nullptr) {
        ReleaseDC(nullptr, screenDc);
    }
    return dpi > 0 ? dpi : 96;
}

void ConfigureDpiAwareness()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto* setProcessDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setProcessDpiAwarenessContext != nullptr) {
            SetLastError(ERROR_SUCCESS);
            if (setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                besktop::LogInfo(L"dpi awareness set: SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)");
                besktop::LogInfo(L"dpi GetDpiForSystem: " + std::to_wstring(GetDpiForSystemCompat()));
                return;
            }
            besktop::LogWarning(
                L"dpi awareness SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2) failed: " +
                std::to_wstring(GetLastError()));
        } else {
            besktop::LogWarning(L"dpi awareness SetProcessDpiAwarenessContext unavailable");
        }
    }

    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore != nullptr) {
        using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
        auto* setProcessDpiAwareness = reinterpret_cast<SetProcessDpiAwarenessFn>(
            GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (setProcessDpiAwareness != nullptr) {
            const HRESULT result = setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
            if (SUCCEEDED(result) || result == E_ACCESSDENIED) {
                besktop::LogInfo(
                    L"dpi awareness set: SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE), result " +
                    FormatHResult(result));
                FreeLibrary(shcore);
                besktop::LogInfo(L"dpi GetDpiForSystem: " + std::to_wstring(GetDpiForSystemCompat()));
                return;
            }
            besktop::LogWarning(
                L"dpi awareness SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE) failed: " +
                FormatHResult(result));
        } else {
            besktop::LogWarning(L"dpi awareness SetProcessDpiAwareness unavailable");
        }
        FreeLibrary(shcore);
    } else {
        besktop::LogWarning(L"dpi awareness Shcore.dll unavailable");
    }

    if (SetProcessDPIAware()) {
        besktop::LogInfo(L"dpi awareness set: SetProcessDPIAware");
    } else {
        besktop::LogWarning(L"dpi awareness SetProcessDPIAware failed: " + std::to_wstring(GetLastError()));
    }
    besktop::LogInfo(L"dpi GetDpiForSystem: " + std::to_wstring(GetDpiForSystemCompat()));
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    const besktop::RuntimeOptions& options = besktop::GetRuntimeOptions();
    besktop::ConfigureLogging(options.verboseInfoLogging ? besktop::LogLevel::Info : besktop::LogLevel::Warning);
    ConfigureDpiAwareness();
    return besktop::RunStageWindow(instance, showCommand, options);
}
