#include "besktop/core/pack_framework.h"

#include "besktop/core/json_value.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

#include <windows.h>

namespace besktop {
namespace {

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open " + path.string());
    }

    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string ReadString(const JsonValue& object, const char* key, std::vector<std::string>& errors)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr || !value->IsString()) {
        errors.push_back(std::string("missing string field: ") + key);
        return {};
    }
    return value->stringValue;
}

std::string ReadOptionalString(const JsonValue& object, const char* key)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr || !value->IsString()) {
        return {};
    }
    return value->stringValue;
}

int ReadInt(const JsonValue& object, const char* key, std::vector<std::string>& errors)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr || !value->IsNumber()) {
        errors.push_back(std::string("missing number field: ") + key);
        return 0;
    }
    return static_cast<int>(value->numberValue);
}

std::vector<std::string> ReadStringArray(const JsonValue& object, const char* key)
{
    std::vector<std::string> result;

    const JsonValue* value = object.Find(key);
    if (value == nullptr || !value->IsArray()) {
        return result;
    }

    for (const JsonValue& item : value->arrayValue) {
        if (item.IsString()) {
            result.push_back(item.stringValue);
        }
    }
    return result;
}

const JsonValue& SelectManifestObject(const JsonValue& root)
{
    const JsonValue* manifest = root.Find("manifest");
    if (manifest != nullptr && manifest->IsObject()) {
        return *manifest;
    }
    return root;
}

PackManifest ParseManifest(const JsonValue& root, std::vector<std::string>& errors)
{
    const JsonValue& manifestObject = SelectManifestObject(root);
    if (!manifestObject.IsObject()) {
        errors.push_back("manifest root must be an object");
        return {};
    }

    PackManifest manifest;
    manifest.id = ReadString(manifestObject, "id", errors);
    manifest.name = ReadString(manifestObject, "name", errors);
    manifest.version = ReadString(manifestObject, "version", errors);
    manifest.schemaVersion = ReadInt(manifestObject, "schemaVersion", errors);
    manifest.publisher = ReadOptionalString(manifestObject, "publisher");

    const JsonValue* requiresObject = manifestObject.Find("requires");
    if (requiresObject != nullptr && requiresObject->IsObject()) {
        manifest.minBesktopVersion = ReadOptionalString(*requiresObject, "besktopMinVersion");
        manifest.entitlements = ReadStringArray(*requiresObject, "entitlements");
    }

    const JsonValue* entry = manifestObject.Find("entry");
    if (entry != nullptr && entry->IsObject()) {
        manifest.interactionsEntry = ReadOptionalString(*entry, "interactions");
    }

    return manifest;
}

bool HasSignatureField(const JsonValue& root)
{
    if (root.Find("signature") != nullptr) {
        return true;
    }

    const JsonValue* manifest = root.Find("manifest");
    return manifest != nullptr && manifest->IsObject() && manifest->Find("signature") != nullptr;
}

bool HasAllEntitlements(
    const std::vector<std::string>& entitlements,
    const IEntitlementService& entitlementService)
{
    return std::all_of(
        entitlements.begin(),
        entitlements.end(),
        [&entitlementService](const std::string& feature) {
            return entitlementService.HasFeature(feature);
        });
}

std::string JoinMissingEntitlements(
    const std::vector<std::string>& entitlements,
    const IEntitlementService& entitlementService)
{
    std::ostringstream stream;
    bool first = true;
    for (const std::string& feature : entitlements) {
        if (entitlementService.HasFeature(feature)) {
            continue;
        }
        if (!first) {
            stream << ", ";
        }
        first = false;
        stream << feature;
    }
    return stream.str();
}

} // namespace

bool PackLoadSummary::HasErrors() const noexcept
{
    if (!providerErrors.empty()) {
        return true;
    }
    return std::any_of(packs.begin(), packs.end(), [](const LoadedPack& pack) {
        return !pack.errors.empty();
    });
}

EmbeddedPackProvider::EmbeddedPackProvider(HINSTANCE instance, std::vector<int> resourceIds)
    : instance_(instance)
    , resourceIds_(std::move(resourceIds))
{
}

std::vector<PackBlob> EmbeddedPackProvider::EnumeratePacks(std::vector<std::string>& errors) const
{
    std::vector<PackBlob> blobs;
    HINSTANCE instance = instance_ != nullptr ? instance_ : GetModuleHandleW(nullptr);

    for (int resourceId : resourceIds_) {
        HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
        if (resource == nullptr) {
            continue;
        }

        HGLOBAL loaded = LoadResource(instance, resource);
        if (loaded == nullptr) {
            errors.push_back("failed to load embedded pack resource " + std::to_string(resourceId));
            continue;
        }

        const DWORD size = SizeofResource(instance, resource);
        const void* data = LockResource(loaded);
        if (data == nullptr || size == 0) {
            errors.push_back("embedded pack resource is empty " + std::to_string(resourceId));
            continue;
        }

        const auto* begin = static_cast<const std::uint8_t*>(data);
        PackBlob blob;
        blob.idHint = std::to_string(resourceId);
        blob.sourceName = "embedded:" + std::to_string(resourceId);
        blob.bytes.assign(begin, begin + size);
        blob.sourceKind = PackSourceKind::Embedded;
        blobs.push_back(std::move(blob));
    }

    return blobs;
}

DevFolderPackProvider::DevFolderPackProvider(std::filesystem::path root)
    : root_(std::move(root))
{
}

std::vector<PackBlob> DevFolderPackProvider::EnumeratePacks(std::vector<std::string>& errors) const
{
    std::vector<PackBlob> blobs;
    if (root_.empty()) {
        return blobs;
    }

    std::error_code existsError;
    if (!std::filesystem::exists(root_, existsError)) {
        errors.push_back("pack directory does not exist: " + root_.string());
        return blobs;
    }

    const auto addFile = [&blobs, &errors](const std::filesystem::path& path, PackSourceKind kind) {
        try {
            PackBlob blob;
            blob.idHint = path.stem().string();
            blob.sourceName = path.string();
            blob.bytes = ReadBinaryFile(path);
            blob.sourceKind = kind;
            blobs.push_back(std::move(blob));
        } catch (const std::exception& exception) {
            errors.push_back(exception.what());
        }
    };

    const std::filesystem::path rootManifest = root_ / "manifest.json";
    if (std::filesystem::exists(rootManifest)) {
        addFile(rootManifest, PackSourceKind::DevFolder);
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root_)) {
        const std::filesystem::path path = entry.path();
        if (entry.is_directory()) {
            const std::filesystem::path manifest = path / "manifest.json";
            if (std::filesystem::exists(manifest)) {
                addFile(manifest, PackSourceKind::DevFolder);
            }
            continue;
        }

        if (entry.is_regular_file() && path.extension() == ".bpack") {
            addFile(path, PackSourceKind::File);
        }
    }

    return blobs;
}

FeatureSetEntitlementService::FeatureSetEntitlementService(std::vector<std::string> features)
    : features_(std::move(features))
{
}

bool FeatureSetEntitlementService::HasFeature(std::string_view featureCode) const
{
    return std::find(features_.begin(), features_.end(), featureCode) != features_.end();
}

PackLoader::PackLoader(PackLoaderOptions options)
    : options_(options)
{
}

PackLoadSummary PackLoader::Load(
    const std::vector<std::unique_ptr<IPackProvider>>& providers,
    const IEntitlementService& entitlementService) const
{
    PackLoadSummary summary;

    for (const std::unique_ptr<IPackProvider>& provider : providers) {
        if (!provider) {
            continue;
        }

        std::vector<std::string> providerErrors;
        const std::vector<PackBlob> blobs = provider->EnumeratePacks(providerErrors);
        summary.providerErrors.insert(
            summary.providerErrors.end(),
            providerErrors.begin(),
            providerErrors.end());

        for (const PackBlob& blob : blobs) {
            summary.packs.push_back(LoadBlob(blob, entitlementService));
        }
    }

    return summary;
}

LoadedPack PackLoader::LoadBlob(
    const PackBlob& blob,
    const IEntitlementService& entitlementService) const
{
    LoadedPack result;
    result.sourceName = blob.sourceName;
    result.sourceKind = blob.sourceKind;

    try {
        const std::string text(blob.bytes.begin(), blob.bytes.end());
        const JsonValue root = ParseJson(text);
        result.manifest = ParseManifest(root, result.errors);

        if (result.manifest.schemaVersion > options_.maxSchemaVersion) {
            result.errors.push_back(
                "unsupported schemaVersion " + std::to_string(result.manifest.schemaVersion));
        }

        const bool hasSignature = HasSignatureField(root);
        result.signatureTrusted = result.sourceKind == PackSourceKind::Embedded || hasSignature;
        if (!result.signatureTrusted) {
            if (options_.allowUnsignedDevPacks && result.sourceKind == PackSourceKind::DevFolder) {
                result.warnings.push_back("unsigned dev folder pack allowed for MVP");
            } else {
                result.errors.push_back("pack is not signed");
            }
        }

        result.entitlementSatisfied = HasAllEntitlements(
            result.manifest.entitlements,
            entitlementService);

        if (!result.entitlementSatisfied) {
            result.warnings.push_back(
                "locked; missing entitlement(s): " +
                JoinMissingEntitlements(result.manifest.entitlements, entitlementService));
        }
    } catch (const std::exception& exception) {
        result.errors.push_back(exception.what());
    }

    return result;
}

std::string ToString(PackSourceKind kind)
{
    switch (kind) {
    case PackSourceKind::Embedded:
        return "embedded";
    case PackSourceKind::DevFolder:
        return "dev-folder";
    case PackSourceKind::File:
        return "file";
    }
    return "unknown";
}

std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        required);
    return wide;
}

} // namespace besktop
