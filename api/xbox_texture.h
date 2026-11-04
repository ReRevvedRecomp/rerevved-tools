#pragma once

#include "texture_import.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

enum class XboxTextureFormat
{
    Argb8,
    Dxt1,
    Dxt3,
    Dxt5,
};

struct XboxTextureFormatInfo
{
    std::uint32_t blockWidth;
    std::uint32_t blockHeight;
    std::uint32_t bytesPerBlock;
};

enum class XboxTextureError
{
    UnsupportedFormat,
    InvalidDimensions,
    SizeMismatch,
    SizeOverflow,
};

[[nodiscard]] std::string_view                                       XboxTextureErrorMessage(XboxTextureError error);
[[nodiscard]] std::expected<XboxTextureFormatInfo, XboxTextureError> GetXboxTextureFormatInfo(XboxTextureFormat format);

// Layout operations require nonzero dimensions that are powers of two. Padding is zeroed.
// These operations do not change endian order. Linear levels contain no padding.
[[nodiscard]] std::expected<std::vector<std::byte>, XboxTextureError>
TileSurface(std::span<const std::byte> bytes, std::uint32_t width, std::uint32_t height, XboxTextureFormat format);
[[nodiscard]] std::expected<std::vector<std::byte>, XboxTextureError>
UntileSurface(std::span<const std::byte> bytes, std::uint32_t width, std::uint32_t height, XboxTextureFormat format);

// ARGB uses 8-in-32. DXT uses 8-in-16. Rejects incomplete swap units.
[[nodiscard]] std::expected<void, XboxTextureError> SwapEndian(std::span<std::byte> bytes, XboxTextureFormat format);

// Little endian BC blocks, or A8R8G8B8 words (B,G,R,A bytes).
// Any nonzero dimensions are allowed. Partial BC blocks replicate edge texels.
// Decoding returns pixel data without source file provenance.
[[nodiscard]] std::expected<std::vector<std::byte>, XboxTextureError> EncodeBlocks(const Rgba8Image& image, XboxTextureFormat format);
[[nodiscard]] std::expected<Rgba8Image, XboxTextureError>
DecodeBlocks(std::span<const std::byte> bytes, std::uint32_t width, std::uint32_t height, XboxTextureFormat format);

struct XboxPackedMipOffset
{
    std::uint32_t xBlocks;
    std::uint32_t yBlocks;
    std::size_t   byteOffset;
};

struct XboxMipLevel
{
    std::uint32_t width;
    std::uint32_t height;
    std::size_t   linearSize;
    // Storage for this level in isolation. Packed levels share the base region.
    std::size_t tiledSize;
    // Tail levels share a region, and packedOffset is relative to that region.
    std::size_t                        regionOffset;
    std::optional<XboxPackedMipOffset> packedOffset;
};

struct XboxMipChain
{
    std::vector<XboxMipLevel> levels;
    std::uint32_t             packedTailBaseLevel;
    std::size_t               tiledSize;
};

// A packed level is addressed by tiling (xBlocks + x, yBlocks + y) with the
// base tail pitch, not by adding byteOffset to a separately tiled level.
[[nodiscard]] std::expected<XboxMipChain, XboxTextureError>
DescribeMipChain(std::uint32_t width, std::uint32_t height, XboxTextureFormat format);

} // namespace rerevved::tools
