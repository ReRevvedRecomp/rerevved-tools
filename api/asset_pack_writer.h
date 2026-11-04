#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

struct AssetPackAsset
{
    std::string            keyPath;
    std::vector<std::byte> bytes;
};

// Display metadata for a map pack's [map_pack] table. The pack's dlc.fpk stays authoritative.
struct MapPackScenarioInfo
{
    std::int32_t id = 0;
    std::string  name;
};

struct MapPackInfo
{
    std::uint32_t                    slot = 0;
    std::vector<MapPackScenarioInfo> scenarios;
};

struct AssetPackDescription
{
    std::int64_t                manifestVersion = 1;
    std::string                 id;
    std::string                 name;
    std::string                 version;
    std::optional<std::string>  author;
    std::optional<std::string>  description;
    std::vector<AssetPackAsset> assets;
    std::optional<MapPackInfo>  mapPack;
};

enum class AssetPackError
{
    InvalidManifestVersion,
    MissingPackTable,
    MissingField,
    InvalidId,
    IdMismatch,
    InvalidName,
    InvalidVersion,
    InvalidAuthor,
    InvalidDescription,
    InvalidEncoding,
    InvalidManifest,
    UnsupportedManifestSyntax,
    UnknownField,
    InvalidKeyPath,
    DuplicateKeyPath,
    EmptyAssets,
    InvalidPackDirectory,
    MissingManifest,
    InvalidAssetsDirectory,
    ReparsePoint,
    NonRegularFile,
    UnsupportedFile,
    DestinationExists,
    CreateDirectoryFailed,
    OpenFailed,
    ReadFailed,
    WriteFailed,
    FinalizationFailed,
    ReadBackMismatch,
    PublishFailed,
    CleanupFailed,
};

struct AssetPackViolation
{
    AssetPackError        error;
    std::filesystem::path path;
    std::string           field;
    bool                  warning = false;
};

// Metadata strings and key paths use UTF-8. Unknown manifest fields are warnings.
[[nodiscard]] std::vector<AssetPackViolation>
ValidateAssetPack(const std::filesystem::path& packDirectory);

[[nodiscard]] std::expected<AssetPackDescription, AssetPackError>
ReadAssetPackManifest(const std::filesystem::path& packDirectory);

// The destination root must already be a regular directory. Publication never replaces a pack.
// On every platform, keys must avoid Windows normalization, device names and forbidden characters.
[[nodiscard]] std::expected<void, AssetPackError>
WriteAssetPack(const AssetPackDescription& description, const std::filesystem::path& destinationRoot);

[[nodiscard]] std::string_view AssetPackErrorMessage(AssetPackError error);

} // namespace rerevved::tools
