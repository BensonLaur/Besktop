#include "besktop/app/runtime_options.h"

#include <windows.h>

#include <iostream>

int main()
{
    SetEnvironmentVariableW(L"BESKTOP_ENABLE_DIAGNOSTICS", nullptr);
    SetEnvironmentVariableW(L"BESKTOP_COMBAT_PREVIEW", L"lead_parry");
    SetEnvironmentVariableW(L"BESKTOP_COMBAT_DIRECTOR_PREVIEW", L"1");
    SetEnvironmentVariableW(L"BESKTOP_ACTION_PREVIEW", L"lead_straight");
    SetEnvironmentVariableW(L"BESKTOP_TURN_PREVIEW", L"1");
    const besktop::RuntimeOptions gatedOptions = besktop::LoadRuntimeOptions();
    const bool gated = !gatedOptions.diagnosticsEnabled &&
        gatedOptions.combatPreview == besktop::CombatScenarioId::None &&
        !gatedOptions.combatDirectorPreviewEnabled &&
        gatedOptions.actionPreview == besktop::ActionId::None &&
        !gatedOptions.turnPreviewEnabled;
    if (!gated) {
        std::cerr << "release diagnostics gate did not ignore preview variables\n";
        return 1;
    }

    SetEnvironmentVariableW(L"BESKTOP_ENABLE_DIAGNOSTICS", L"1");
    const besktop::RuntimeOptions diagnosticOptions = besktop::LoadRuntimeOptions();
    SetEnvironmentVariableW(L"BESKTOP_ENABLE_DIAGNOSTICS", nullptr);
    SetEnvironmentVariableW(L"BESKTOP_COMBAT_DIRECTOR_PREVIEW", nullptr);
    if (!diagnosticOptions.diagnosticsEnabled || !diagnosticOptions.combatDirectorPreviewEnabled) {
        std::cerr << "diagnostics did not enable combat director preview\n";
        return 1;
    }
    std::cout << "besktop_runtime_options_tests: all checks passed\n";
    return 0;
}
