#include "besktop/app/runtime_options.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace {

bool ReadTruthyEnvironmentFlag(const wchar_t* name)
{
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(name, value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) {
        return false;
    }

    return value[0] == L'1' ||
        value[0] == L't' ||
        value[0] == L'T' ||
        value[0] == L'y' ||
        value[0] == L'Y' ||
        value[0] == L'o' ||
        value[0] == L'O';
}

double ReadEnvironmentDouble(const wchar_t* name, double fallback, double minimum, double maximum)
{
    wchar_t value[64]{};
    const DWORD length = GetEnvironmentVariableW(name, value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) {
        return fallback;
    }

    wchar_t* end = nullptr;
    const double parsed = wcstod(value, &end);
    if (end == value || !std::isfinite(parsed)) {
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

} // namespace

namespace besktop {

RuntimeOptions LoadRuntimeOptions()
{
    RuntimeOptions options;
#if defined(BESKTOP_DEVELOPER_BUILD)
    options.developerBuild = true;
#endif
    options.diagnosticsEnabled = options.developerBuild ||
        ReadTruthyEnvironmentFlag(L"BESKTOP_ENABLE_DIAGNOSTICS");
    options.verboseInfoLogging = options.diagnosticsEnabled;

    if (!options.diagnosticsEnabled) {
        return options;
    }

    options.frameStatsEnabled = ReadTruthyEnvironmentFlag(L"BESKTOP_FRAME_STATS");
    options.frameTraceEnabled = ReadTruthyEnvironmentFlag(L"BESKTOP_FRAME_TRACE");
    options.debugIconPlaneEnabled = ReadTruthyEnvironmentFlag(L"BESKTOP_DEBUG_ICON_PLANE");
    options.renderShadowsEnabled = ReadTruthyEnvironmentFlag(L"BESKTOP_RENDER_SHADOWS");
    options.animationSpeed = ReadEnvironmentDouble(L"BESKTOP_ANIMATION_SPEED", 1.0, 0.05, 8.0);
    options.animationOffsetSeconds = ReadEnvironmentDouble(L"BESKTOP_ANIMATION_OFFSET", 0.0, 0.0, 3600.0);
    return options;
}

const RuntimeOptions& GetRuntimeOptions()
{
    static const RuntimeOptions options = LoadRuntimeOptions();
    return options;
}

} // namespace besktop
