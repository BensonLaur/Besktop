#include <windows.h>
#include <shellapi.h>

#include "besktop/core/mvp_runtime.h"

namespace {

besktop::MvpRunOptions ParseOptions(HINSTANCE instance)
{
    besktop::MvpRunOptions options;
    options.instance = instance;

    int argc = 0;
    PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return options;
    }

    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--pack-dir" && index + 1 < argc) {
            options.devPackDirectory = argv[++index];
        } else if (argument == L"--features" && index + 1 < argc) {
            options.enabledFeatures = besktop::SplitFeatureList(besktop::WideToUtf8(argv[++index]));
        }
    }

    LocalFree(argv);
    return options;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    const besktop::MvpRunOptions options = ParseOptions(instance);
    const besktop::PackLoadSummary summary = besktop::RunPackMvp(options);
    const std::wstring report = besktop::FormatPackMvpReportWide(summary);

    MessageBoxW(
        nullptr,
        report.c_str(),
        L"Besktop Pack MVP",
        MB_OK | (summary.HasErrors() ? MB_ICONWARNING : MB_ICONINFORMATION));

    return summary.HasErrors() ? 1 : 0;
}
