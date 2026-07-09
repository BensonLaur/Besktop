#include "besktop/core/mvp_runtime.h"

#include "besktop/resources/resource.h"
#include "besktop/version.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <windows.h>

namespace besktop {
namespace {

std::string Trim(std::string_view text)
{
    const auto isSpace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };

    std::size_t begin = 0;
    while (begin < text.size() && isSpace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && isSpace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return std::string(text.substr(begin, end - begin));
}

void AppendMessages(std::ostringstream& stream, const char* label, const std::vector<std::string>& messages)
{
    for (const std::string& message : messages) {
        stream << "    " << label << ": " << message << "\n";
    }
}

} // namespace

PackLoadSummary RunPackMvp(const MvpRunOptions& options)
{
    std::vector<std::unique_ptr<IPackProvider>> providers;
    providers.push_back(std::make_unique<EmbeddedPackProvider>(
        options.instance,
        std::vector<int> {
            IDR_BESKTOP_PACK_BASIC,
            IDR_BESKTOP_PACK_PLUS_COUPLE,
            IDR_BESKTOP_PACK_PLUS_FRIENDS,
            IDR_BESKTOP_PACK_PLUS_CREATOR,
        }));

    if (!options.devPackDirectory.empty()) {
        providers.push_back(std::make_unique<DevFolderPackProvider>(options.devPackDirectory));
    }

    FeatureSetEntitlementService entitlementService(options.enabledFeatures);
    PackLoader loader;
    return loader.Load(providers, entitlementService);
}

std::string FormatPackMvpReport(const PackLoadSummary& summary)
{
    std::ostringstream stream;
    stream << BESKTOP_PRODUCT_NAME << " " << BESKTOP_VERSION_MAJOR << "."
           << BESKTOP_VERSION_MINOR << "." << BESKTOP_VERSION_PATCH << "\n";
    stream << "Pack framework MVP report\n";
    stream << "Loaded packs: " << summary.packs.size() << "\n";

    AppendMessages(stream, "provider-error", summary.providerErrors);

    for (const LoadedPack& pack : summary.packs) {
        const bool usable = pack.errors.empty() && pack.entitlementSatisfied;
        stream << "\n";
        stream << "- " << (pack.manifest.name.empty() ? "(unknown pack)" : pack.manifest.name)
               << " [" << (pack.manifest.id.empty() ? "unknown" : pack.manifest.id) << "]\n";
        stream << "  source: " << pack.sourceName << " (" << ToString(pack.sourceKind) << ")\n";
        stream << "  version: " << (pack.manifest.version.empty() ? "unknown" : pack.manifest.version)
               << ", schema: " << pack.manifest.schemaVersion << "\n";
        stream << "  signature: " << (pack.signatureTrusted ? "trusted-or-present" : "not-trusted") << "\n";
        stream << "  entitlement: " << (pack.entitlementSatisfied ? "satisfied" : "locked") << "\n";
        stream << "  status: " << (usable ? "enabled" : "not-enabled") << "\n";

        AppendMessages(stream, "warning", pack.warnings);
        AppendMessages(stream, "error", pack.errors);
    }

    if (summary.packs.empty() && summary.providerErrors.empty()) {
        stream << "\nNo packs were discovered.\n";
    }

    return stream.str();
}

std::wstring FormatPackMvpReportWide(const PackLoadSummary& summary)
{
    return Utf8ToWide(FormatPackMvpReport(summary));
}

std::vector<std::string> SplitFeatureList(std::string_view text)
{
    std::vector<std::string> features;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        std::string item = Trim(text.substr(start, end - start));
        if (!item.empty()) {
            features.push_back(std::move(item));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return features;
}

std::string WideToUtf8(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        utf8.data(),
        required,
        nullptr,
        nullptr);
    return utf8;
}

} // namespace besktop
