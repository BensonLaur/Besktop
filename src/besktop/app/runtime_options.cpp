#include "besktop/app/runtime_options.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <string>

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

unsigned int ReadEnvironmentUnsigned(const wchar_t* name, unsigned int fallback, unsigned int maximum)
{
    wchar_t value[32]{};
    const DWORD length = GetEnvironmentVariableW(name, value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) {
        return fallback;
    }

    if (value[0] == L'-') {
        return fallback;
    }

    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(value, &end, 10);
    if (end == value || *end != L'\0') {
        return fallback;
    }
    return static_cast<unsigned int>(std::min<unsigned long>(parsed, maximum));
}

std::wstring ReadEnvironmentString(const wchar_t* name)
{
    wchar_t value[64]{};
    const DWORD length = GetEnvironmentVariableW(name, value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) {
        return {};
    }
    return std::wstring(value, length);
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
    options.maxActors = ReadEnvironmentUnsigned(L"BESKTOP_MAX_ACTORS", 0, 10000);
    options.animationSpeed = ReadEnvironmentDouble(L"BESKTOP_ANIMATION_SPEED", 1.0, 0.05, 8.0);
    options.animationOffsetSeconds = ReadEnvironmentDouble(L"BESKTOP_ANIMATION_OFFSET", 0.0, 0.0, 3600.0);
    const std::wstring combatPreviewName = ReadEnvironmentString(L"BESKTOP_COMBAT_PREVIEW");
    options.combatPreview = ParseCombatScenarioId(combatPreviewName);
    options.invalidCombatPreview = !combatPreviewName.empty() &&
        options.combatPreview == CombatScenarioId::None;
    options.combatDirectorDiagnosticsEnabled =
        ReadTruthyEnvironmentFlag(L"BESKTOP_COMBAT_DIRECTOR_PREVIEW");
    const std::wstring actionPreviewName = ReadEnvironmentString(L"BESKTOP_ACTION_PREVIEW");
    options.actionPreview = ParseActionId(actionPreviewName);
    options.invalidActionPreview = !actionPreviewName.empty() && options.actionPreview == ActionId::None;
    options.actionOrbitCameraEnabled = ReadTruthyEnvironmentFlag(L"BESKTOP_ACTION_ORBIT_CAMERA");
    options.turnPreviewEnabled = ReadTruthyEnvironmentFlag(L"BESKTOP_TURN_PREVIEW");
    return options;
}

const RuntimeOptions& GetRuntimeOptions()
{
    static const RuntimeOptions options = LoadRuntimeOptions();
    return options;
}

RuntimeExperienceMode ResolveRuntimeExperienceMode(const RuntimeOptions& options)
{
    if (options.combatPreview != CombatScenarioId::None) {
        return RuntimeExperienceMode::FixedCombatPreview;
    }
    if (options.turnPreviewEnabled) {
        return RuntimeExperienceMode::TurnPreview;
    }
    if (options.actionPreview != ActionId::None) {
        return RuntimeExperienceMode::ActionPreview;
    }
    if (options.combatDirectorEnabled || options.combatDirectorDiagnosticsEnabled) {
        return RuntimeExperienceMode::CombatDirector;
    }
    return RuntimeExperienceMode::Wandering;
}

} // namespace besktop
