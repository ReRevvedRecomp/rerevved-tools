#pragma once

#include "dds_metadata.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rerevved::tools
{

enum class DdsPreviewError
{
    UnsupportedFormat,
    InvalidDimensions,
    SizeOverflow,
    TruncatedPixelData,
    InvalidMipCount,
    LayoutRequiresPowerOfTwo,
    PackedMipTailUnsupported,
    PixelLimitExceeded,
};

enum class DdsPayloadLayout
{
    LinearLittleEndian,
    XboxTiledBigEndian,
};

struct DdsMipLevel
{
    std::uint32_t width;
    std::uint32_t height;
    // Absolute source byte range, including the tiled padding when selected.
    std::size_t            byteOffset;
    std::size_t            byteSize;
    std::vector<std::byte> rgba8;
};

struct DdsDocument
{
    DdsMetadata              metadata;
    std::vector<std::byte>   rgba8;
    std::vector<DdsMipLevel> mipLevels;
    DdsPayloadLayout         payloadLayout = DdsPayloadLayout::LinearLittleEndian;
    // Later level problems do not discard the complete preview levels.
    std::optional<DdsPreviewError> mipIssue;
};

using DdsDocumentError = std::variant<DdsMetadataError, DdsPreviewError>;

[[nodiscard]] std::string_view DdsDocumentErrorMessage(const DdsDocumentError& error);

[[nodiscard]] std::expected<DdsDocument, DdsDocumentError>
ParseDdsDocument(std::span<const std::byte> bytes);

// Layout is supplied by the caller. DDS headers do not identify Xbox tiling.
[[nodiscard]] std::expected<DdsDocument, DdsDocumentError>
ParseDdsDocument(std::span<const std::byte> bytes, DdsPayloadLayout layout);

[[nodiscard]] std::expected<DdsDocument, std::string>
LoadDdsDocument(const std::filesystem::path& path);

[[nodiscard]] std::expected<DdsDocument, std::string>
LoadDdsDocument(const std::filesystem::path& path, DdsPayloadLayout layout);

} // namespace rerevved::tools
