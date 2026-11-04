#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

enum class GfxDocumentError
{
    Truncated,
    InvalidSignature,
    UnsupportedCompression,
    LengthMismatch,
    MissingExporterInfo,
    UnsupportedExporterVersion,
    InvalidTagStream,
};

struct GfxRect
{
    std::int32_t xMinTwips = 0;
    std::int32_t xMaxTwips = 0;
    std::int32_t yMinTwips = 0;
    std::int32_t yMaxTwips = 0;
};

struct GfxExporterInfo
{
    std::uint16_t          version      = 0;
    std::uint32_t          flags        = 0;
    std::uint16_t          bitmapFormat = 0;
    std::vector<std::byte> prefix;
    std::vector<std::byte> swfName;
};

struct GfxExternalImage
{
    std::uint16_t          characterId  = 0;
    std::uint16_t          bitmapFormat = 0;
    std::uint16_t          targetWidth  = 0;
    std::uint16_t          targetHeight = 0;
    std::vector<std::byte> exportName;
    std::vector<std::byte> fileName;
};

struct GfxDocument
{
    std::uint8_t                  fileVersion    = 0;
    std::uint32_t                 declaredLength = 0;
    GfxRect                       frame;
    std::uint16_t                 frameRateRaw = 0;
    std::uint16_t                 frameCount   = 0;
    GfxExporterInfo               exporter;
    std::vector<GfxExternalImage> externalImages;
};

[[nodiscard]] std::string_view GfxDocumentErrorMessage(GfxDocumentError error);

[[nodiscard]] std::expected<GfxDocument, GfxDocumentError>
ParseGfxDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<GfxDocument, std::string>
LoadGfxDocument(const std::filesystem::path& path);

} // namespace rerevved::tools
