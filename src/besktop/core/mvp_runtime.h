#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>

#include "besktop/core/pack_framework.h"

namespace besktop {

struct MvpRunOptions {
    HINSTANCE instance = nullptr;
    std::filesystem::path devPackDirectory;
    std::vector<std::string> enabledFeatures;
};

PackLoadSummary RunPackMvp(const MvpRunOptions& options);
std::string FormatPackMvpReport(const PackLoadSummary& summary);
std::wstring FormatPackMvpReportWide(const PackLoadSummary& summary);

std::vector<std::string> SplitFeatureList(std::string_view text);
std::string WideToUtf8(std::wstring_view text);

} // namespace besktop
