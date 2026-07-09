#include <iostream>
#include <string>
#include <vector>

#include <windows.h>

#include "besktop/core/mvp_runtime.h"

namespace {

void PrintUsage()
{
    std::cout
        << "Besktop pack framework MVP\n"
        << "\n"
        << "Usage:\n"
        << "  besktop_mvp_cli [--pack-dir <dir>] [--features plus.couple,plus.friends]\n"
        << "\n"
        << "Examples:\n"
        << "  besktop_mvp_cli\n"
        << "  besktop_mvp_cli --pack-dir D:\\Projects\\Benson\\Besktop-Plus\\packs\n"
        << "  besktop_mvp_cli --pack-dir D:\\Projects\\Benson\\Besktop-Plus\\packs --features plus.couple\n";
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    besktop::MvpRunOptions options;
    options.instance = GetModuleHandleW(nullptr);

    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--help" || argument == L"-h") {
            PrintUsage();
            return 0;
        }
        if (argument == L"--pack-dir" && index + 1 < argc) {
            options.devPackDirectory = argv[++index];
            continue;
        }
        if (argument == L"--features" && index + 1 < argc) {
            options.enabledFeatures = besktop::SplitFeatureList(besktop::WideToUtf8(argv[++index]));
            continue;
        }

        std::wcerr << L"Unknown or incomplete argument: " << argument << L"\n";
        PrintUsage();
        return 2;
    }

    const besktop::PackLoadSummary summary = besktop::RunPackMvp(options);
    std::cout << besktop::FormatPackMvpReport(summary);
    return summary.HasErrors() ? 1 : 0;
}
