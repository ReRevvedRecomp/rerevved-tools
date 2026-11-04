#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

inline constexpr std::string_view kMapBundleManifestName = "bundle-manifest.json";

using MapBundleVerifier = std::function<std::expected<void, std::string>(std::span<const std::byte>)>;

struct MapBundleArtifact
{
    std::string            name;
    std::vector<std::byte> bytes;
    MapBundleVerifier      verify;
};

enum class MapBundleWriteError
{
    InvalidName,
    DuplicateName,
    InvalidToolVersion,
    MissingVerifier,
    InvalidParent,
    ReparsePoint,
    NonRegularFile,
    DestinationExists,
    CreateDirectoryFailed,
    InvalidStaging,
    OpenFailed,
    WriteFailed,
    FinalizationFailed,
    ReadFailed,
    ReadBackMismatch,
    VerificationFailed,
    PublishFailed,
    CleanupFailed,
};

[[nodiscard]] std::string_view MapBundleWriteErrorMessage(MapBundleWriteError error);
[[nodiscard]] std::string      CalculateMapBundleSha256(std::span<const std::byte> bytes);
// Printable ASCII without characters reserved by Windows, device names or a trailing dot/space.
[[nodiscard]] bool IsValidMapBundleName(std::string_view name);

// Deterministic UTF-8 JSON with LF endings. Does not invoke artifact verifiers.
[[nodiscard]] std::expected<std::vector<std::byte>, MapBundleWriteError>
EncodeMapBundleManifest(std::span<const MapBundleArtifact> artifacts, std::string_view toolVersion);

// Owns one fresh, flat staging directory.
// The destructor attempts cleanup of only created files and the empty directory.
class MapBundleFileSession
{
public:
    MapBundleFileSession() = default;
    ~MapBundleFileSession();
    MapBundleFileSession(const MapBundleFileSession&)            = delete;
    MapBundleFileSession& operator=(const MapBundleFileSession&) = delete;

    [[nodiscard]] std::expected<void, MapBundleWriteError>                   CreateDirectoryNew(const std::filesystem::path& staging);
    [[nodiscard]] std::expected<void, MapBundleWriteError>                   WriteNew(const std::filesystem::path& path, std::span<const std::byte> bytes);
    [[nodiscard]] std::expected<std::vector<std::byte>, MapBundleWriteError> Read(const std::filesystem::path& path) const;
    [[nodiscard]] std::expected<void, MapBundleWriteError>                   PublishDirectoryNew(const std::filesystem::path& staging, const std::filesystem::path& destination);
    [[nodiscard]] std::expected<void, MapBundleWriteError>                   RemoveOwnedStaging(const std::filesystem::path& staging);

private:
    std::filesystem::path              stagingPath;
    std::vector<std::filesystem::path> ownedFiles;
};

// Writes and compares the bytes of every artifact, invokes its verifier, then writes and
// reads the manifest last. Publishes only the complete directory, without replacement.
[[nodiscard]] std::expected<std::filesystem::path, MapBundleWriteError>
WriteMapBundle(const std::filesystem::path& parent, std::string_view directoryName, std::span<const MapBundleArtifact> artifacts, std::string_view toolVersion);

} // namespace rerevved::tools
