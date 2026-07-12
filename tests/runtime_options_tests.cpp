#include "besktop/app/runtime_options.h"

#include <windows.h>

#include <iostream>

int main()
{
    SetEnvironmentVariableW(L"BESKTOP_ENABLE_DIAGNOSTICS", nullptr);
    SetEnvironmentVariableW(L"BESKTOP_COMBAT_PREVIEW", L"lead_parry");
    SetEnvironmentVariableW(L"BESKTOP_ACTION_PREVIEW", L"lead_straight");
    SetEnvironmentVariableW(L"BESKTOP_TURN_PREVIEW", L"1");
    const besktop::RuntimeOptions& options = besktop::GetRuntimeOptions();
    const bool passed = !options.diagnosticsEnabled &&
        options.combatPreview == besktop::CombatScenarioId::None &&
        options.actionPreview == besktop::ActionId::None &&
        !options.turnPreviewEnabled;
    if (!passed) {
        std::cerr << "release diagnostics gate did not ignore preview variables\n";
        return 1;
    }
    std::cout << "besktop_runtime_options_tests: all checks passed\n";
    return 0;
}
