#pragma once

#include "dds_document.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace rerevved::tools
{

enum class TextureSourceKind
{
    Png,
    Dds,
};

enum class TextureEncoding
{
    LegacyA8R8G8B8,
    Unsupported,
};

struct TextureKeyContract
{
    std::string_view key;
    std::uint32_t    width                = 0;
    std::uint32_t    height               = 0;
    TextureEncoding  encoding             = TextureEncoding::LegacyA8R8G8B8;
    std::uint32_t    mipLevelCount        = 1;
    bool             requiresAlphaChannel = true;
};

struct PngSourceLayout
{
    std::uint8_t colorType = 0;
    std::uint8_t bitDepth  = 0;
};

struct DdsSourceLayout
{
    DdsMetadata metadata;
    // A parsed header remains inspectable when the document decoder rejects it.
    std::optional<DdsDocumentError> decodeError;
};

struct Rgba8Image
{
    std::uint32_t                                  width           = 0;
    std::uint32_t                                  height          = 0;
    bool                                           hasAlphaChannel = false;
    std::variant<PngSourceLayout, DdsSourceLayout> sourceLayout;
    std::vector<std::byte>                         rgba8;
};

enum class TextureImportError
{
    InvalidPng,
    Png16BitUnsupported,
    PngGrayscaleUnsupported,
    PngColorTypeUnsupported,
    PngBitDepthUnsupported,
    SourceTooLarge,
    PngPixelLimitExceeded,
    AllocationFailed,
    InvalidSourceKind,
};

using TextureImportFailure = std::variant<TextureImportError, DdsDocumentError>;

enum class TextureValidationIssueKind
{
    Width,
    Height,
    MipLevelCount,
    Encoding,
    MissingAlphaChannel,
    PixelDataSize,
};

struct TextureValidationIssue
{
    TextureValidationIssueKind kind;
    std::uint64_t              expected = 0;
    std::uint64_t              actual   = 0;
};

[[nodiscard]] TextureKeyContract MainMenuLogoTextureContract();

[[nodiscard]] std::string_view TextureImportErrorMessage(TextureImportError error);

[[nodiscard]] std::string_view TextureImportErrorMessage(const TextureImportFailure& error);

// DDS header failures are errors. Decode failures retain metadata and no pixels.
[[nodiscard]] std::expected<Rgba8Image, TextureImportFailure>
ImportTexture(std::span<const std::byte> bytes, TextureSourceKind sourceKind);

[[nodiscard]] std::vector<TextureValidationIssue>
ValidateTexture(const Rgba8Image& image, const TextureKeyContract& contract);

} // namespace rerevved::tools
