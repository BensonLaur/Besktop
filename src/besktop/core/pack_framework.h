#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace besktop {

enum class PackSourceKind {
    Embedded,
    DevFolder,
    File,
};

struct PackBlob {
    std::string idHint;
    std::string sourceName;
    std::vector<std::uint8_t> bytes;
    PackSourceKind sourceKind = PackSourceKind::File;
};

struct PackManifest {
    std::string id;
    std::string name;
    std::string version;
    int schemaVersion = 0;
    std::string publisher;
    std::string minBesktopVersion;
    std::vector<std::string> entitlements;
    std::string interactionsEntry;
};

struct LoadedPack {
    PackManifest manifest;
    std::string sourceName;
    PackSourceKind sourceKind = PackSourceKind::File;
    bool signatureTrusted = false;
    bool entitlementSatisfied = false;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

struct PackLoadSummary {
    std::vector<LoadedPack> packs;
    std::vector<std::string> providerErrors;

    bool HasErrors() const noexcept;
};

class IPackProvider {
public:
    virtual ~IPackProvider() = default;
    virtual std::vector<PackBlob> EnumeratePacks(std::vector<std::string>& errors) const = 0;
};

class EmbeddedPackProvider final : public IPackProvider {
public:
    EmbeddedPackProvider(HINSTANCE instance, std::vector<int> resourceIds);

    std::vector<PackBlob> EnumeratePacks(std::vector<std::string>& errors) const override;

private:
    HINSTANCE instance_ = nullptr;
    std::vector<int> resourceIds_;
};

class DevFolderPackProvider final : public IPackProvider {
public:
    explicit DevFolderPackProvider(std::filesystem::path root);

    std::vector<PackBlob> EnumeratePacks(std::vector<std::string>& errors) const override;

private:
    std::filesystem::path root_;
};

class IEntitlementService {
public:
    virtual ~IEntitlementService() = default;
    virtual bool HasFeature(std::string_view featureCode) const = 0;
};

class FeatureSetEntitlementService final : public IEntitlementService {
public:
    FeatureSetEntitlementService() = default;
    explicit FeatureSetEntitlementService(std::vector<std::string> features);

    bool HasFeature(std::string_view featureCode) const override;

private:
    std::vector<std::string> features_;
};

struct PackLoaderOptions {
    int maxSchemaVersion = 1;
    bool allowUnsignedDevPacks = true;
};

class PackLoader final {
public:
    explicit PackLoader(PackLoaderOptions options = {});

    PackLoadSummary Load(
        const std::vector<std::unique_ptr<IPackProvider>>& providers,
        const IEntitlementService& entitlementService) const;

    LoadedPack LoadBlob(
        const PackBlob& blob,
        const IEntitlementService& entitlementService) const;

private:
    PackLoaderOptions options_;
};

std::string ToString(PackSourceKind kind);
std::wstring Utf8ToWide(std::string_view text);

} // namespace besktop
