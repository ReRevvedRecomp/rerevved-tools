#include "xbox_texture.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <stb_dxt.h>

namespace rerevved::tools
{
namespace
{

using std::int32_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

struct SurfaceLayout
{
    XboxTextureFormatInfo info;
    std::size_t           widthBlocks;
    std::size_t           heightBlocks;
    std::size_t           pitchAligned;
    std::size_t           linearSize;
    std::size_t           tiledSize;
};

constexpr std::size_t kStorageAlignmentBlocks = 32;
constexpr uint32_t    kMacroTileLog2          = 5;

// A tiled level fills whole 32 x 32 block tiles, as in Xenia
// src/xenia/gpu/texture_util.cc GetGuestTextureLayout.
std::size_t alignToStorage(std::size_t blocks)
{
    return (blocks + kStorageAlignmentBlocks - 1) & ~(kStorageAlignmentBlocks - 1);
}

std::expected<SurfaceLayout, XboxTextureError> surfaceLayout(uint32_t width, uint32_t height, XboxTextureFormat format, bool tiled)
{
    const auto info = GetXboxTextureFormatInfo(format);
    if (!info)
    {
        return std::unexpected(info.error());
    }
    if (width == 0 || height == 0 || (tiled && (!std::has_single_bit(width) || !std::has_single_bit(height))))
    {
        return std::unexpected(XboxTextureError::InvalidDimensions);
    }
    const std::size_t wb            = (uint64_t(width) + info->blockWidth - 1) / info->blockWidth;
    const std::size_t hb            = (uint64_t(height) + info->blockHeight - 1) / info->blockHeight;
    const auto        pitchAligned  = alignToStorage(wb);
    const auto        heightAligned = alignToStorage(hb);
    const auto        limit         = std::vector<std::byte>().max_size();
    if (wb > limit / info->bytesPerBlock / hb || pitchAligned > limit / info->bytesPerBlock / heightAligned)
    {
        return std::unexpected(XboxTextureError::SizeOverflow);
    }
    return SurfaceLayout{ *info, wb, hb, pitchAligned, wb * hb * info->bytesPerBlock, pitchAligned * heightAligned * info->bytesPerBlock };
}

// Ported from Xenia src/xenia/gpu/texture_address.h, TiledCombine,
// commit 95a5c3ee250f80c3b9d139658649d9ffb6db3eec.
std::size_t tiledCombine(std::size_t outerInnerBytes, std::size_t bank, std::size_t pipe, std::size_t yLsb)
{
    return (yLsb << 4) | (pipe << 6) | (bank << 11) | (outerInnerBytes & 0b1111) |
           (((outerInnerBytes >> 4) & 0b1) << 5) |
           (((outerInnerBytes >> 5) & 0b111) << 8) |
           (outerInnerBytes >> 8 << 12);
}

// Ported from Xenia src/xenia/gpu/texture_address.h, Tiled2D, same commit.
std::size_t tiled2D(std::size_t x, std::size_t y, std::size_t pitchAligned, uint32_t bytesPerBlockLog2)
{
    const auto outerBlocks     = ((y >> kMacroTileLog2) * (pitchAligned >> kMacroTileLog2) + (x >> kMacroTileLog2)) << 6;
    const auto innerBlocks     = (((y >> 1) & 0b111) << 3) | (x & 0b111);
    const auto outerInnerBytes = (outerBlocks | innerBlocks) << bytesPerBlockLog2;
    const auto bank            = (y >> 4) & 0b1;
    const auto pipe            = ((x >> 3) & 0b11) ^ (((y >> 3) & 0b1) << 1);
    return tiledCombine(outerInnerBytes, bank, pipe, y & 1);
}

std::size_t tiledOffset(std::size_t x, std::size_t y, const SurfaceLayout& layout)
{
    return tiled2D(x, y, layout.pitchAligned, uint32_t(std::countr_zero(layout.info.bytesPerBlock)));
}

uint32_t log2Ceil(uint32_t value)
{
    return uint32_t(std::bit_width(value - 1));
}

// packed_mip_base in Xenia GetPackedMipOffset.
uint32_t packedMipBase(uint32_t width, uint32_t height)
{
    const auto log2Size = std::min(log2Ceil(width), log2Ceil(height));
    return log2Size > 4 ? log2Size - 4 : 0;
}

// Ported from Xenia src/xenia/gpu/texture_util.cc, GetPackedMipOffset, 2D only,
// commit 95a5c3ee250f80c3b9d139658649d9ffb6db3eec.
XboxPackedMipOffset packedMipOffset(uint32_t width, uint32_t height, uint32_t mip, const SurfaceLayout& tail)
{
    const auto log2Width  = log2Ceil(width);
    const auto log2Height = log2Ceil(height);
    const auto packedBase = packedMipBase(width, height);
    const auto packedMip  = mip - packedBase;
    uint32_t   xBlocks    = 0;
    uint32_t   yBlocks    = 0;
    if (packedMip < 3)
    {
        if (log2Width > log2Height)
        {
            yBlocks = 16U >> packedMip;
        }
        else
        {
            xBlocks = 16U >> packedMip;
        }
    }
    else if (log2Width > log2Height)
    {
        xBlocks = (1U << (log2Width - packedBase)) >> (packedMip - 2);
    }
    else
    {
        yBlocks = (1U << (log2Height - packedBase)) >> (packedMip - 2);
    }
    xBlocks /= tail.info.blockWidth;
    yBlocks /= tail.info.blockHeight;
    return { xBlocks, yBlocks, tiledOffset(xBlocks, yBlocks, tail) };
}

std::expected<std::size_t, XboxTextureError> rgbaSize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return std::unexpected(XboxTextureError::InvalidDimensions);
    }
    if (std::size_t(width) > std::vector<std::byte>().max_size() / 4 / height)
    {
        return std::unexpected(XboxTextureError::SizeOverflow);
    }
    return std::size_t(width) * height * 4;
}

uint16_t readU16(const uint8_t* bytes)
{
    return uint16_t(uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8));
}

void writeU16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = uint8_t(value & 0xFF);
    bytes[1] = uint8_t(value >> 8);
}

void unpackRgb565(uint16_t value, int32_t rgb[3])
{
    const int32_t red   = value >> 11;
    const int32_t green = (value >> 5) & 0x3F;
    const int32_t blue  = value & 0x1F;
    rgb[0]              = (red << 3) | (red >> 2);
    rgb[1]              = (green << 2) | (green >> 4);
    rgb[2]              = (blue << 3) | (blue >> 2);
}

// Equal endpoints select the three colour mode, so a single colour block gets a
// second endpoint that no texel uses.
void separateEndpoints(uint8_t colour[8])
{
    const auto value = readU16(colour);
    if (value != readU16(colour + 2))
    {
        return;
    }
    writeU16(colour, value == 0 ? 1 : value);
    writeU16(colour + 2, value == 0 ? 0 : uint16_t(value - 1));
    std::memset(colour + 4, value == 0 ? 0x55 : 0, 4);
}

// stb_dxt spots single colour blocks by comparing whole texels, alpha included.
void encodeOpaqueColour(const uint8_t block[64], uint8_t out[8])
{
    uint8_t opaque[64];
    std::memcpy(opaque, block, sizeof(opaque));
    for (uint32_t i = 0; i < 16; ++i)
    {
        opaque[i * 4 + 3] = 255;
    }
    stb_compress_dxt_block(out, opaque, 0, STB_DXT_HIGHQUAL);
    separateEndpoints(out);
}

// stb_dxt has no three colour mode, so a block with cut out texels uses the
// bounding box of its visible texels. Index 3 is transparent black.
void encodeCutoutColour(const uint8_t block[64], uint8_t out[8])
{
    uint8_t low[3]{ 255, 255, 255 };
    uint8_t high[3]{};
    for (uint32_t i = 0; i < 16; ++i)
    {
        if (block[i * 4 + 3] >= 128)
        {
            for (uint32_t c = 0; c < 3; ++c)
            {
                low[c]  = std::min(low[c], block[i * 4 + c]);
                high[c] = std::max(high[c], block[i * 4 + c]);
            }
        }
    }
    const auto pack = [](const uint8_t rgb[3])
    {
        return uint16_t(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
    };
    const auto c0 = std::min(pack(low), pack(high));
    const auto c1 = std::max(pack(low), pack(high));
    int32_t    palette[3][3];
    unpackRgb565(c0, palette[0]);
    unpackRgb565(c1, palette[1]);
    for (uint32_t c = 0; c < 3; ++c)
    {
        palette[2][c] = (palette[0][c] + palette[1][c]) / 2;
    }
    uint32_t indices = 0;
    for (uint32_t i = 0; i < 16; ++i)
    {
        uint32_t index = 3;
        if (block[i * 4 + 3] >= 128)
        {
            int32_t closest = INT32_MAX;
            for (uint32_t p = 0; p < 3; ++p)
            {
                int32_t distance = 0;
                for (uint32_t c = 0; c < 3; ++c)
                {
                    const int32_t delta = block[i * 4 + c] - palette[p][c];
                    distance += delta * delta;
                }
                if (distance < closest)
                {
                    closest = distance;
                    index   = p;
                }
            }
        }
        indices |= index << (i * 2);
    }
    writeU16(out, c0);
    writeU16(out + 2, c1);
    writeU16(out + 4, uint16_t(indices & 0xFFFF));
    writeU16(out + 6, uint16_t(indices >> 16));
}

// A level that ends inside a block repeats its last row and column to fill it.
void gatherBlock(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t blockX, uint32_t blockY, uint8_t out[64])
{
    for (uint32_t row = 0; row < 4; ++row)
    {
        const std::size_t y = std::min(blockY * 4 + row, height - 1);
        for (uint32_t column = 0; column < 4; ++column)
        {
            const std::size_t x = std::min(blockX * 4 + column, width - 1);
            std::memcpy(out + (row * 4 + column) * 4, pixels + (y * width + x) * 4, 4);
        }
    }
}

void encodeBlock(const uint8_t block[64], XboxTextureFormat format, uint8_t* out)
{
    switch (format)
    {
        case XboxTextureFormat::Dxt1:
        {
            bool cutout = false;
            for (uint32_t i = 0; i < 16; ++i)
            {
                cutout = cutout || block[i * 4 + 3] < 128;
            }
            if (cutout)
            {
                encodeCutoutColour(block, out);
            }
            else
            {
                encodeOpaqueColour(block, out);
            }
            break;
        }
        case XboxTextureFormat::Dxt3:
            std::memset(out, 0, 8);
            for (uint32_t i = 0; i < 16; ++i)
            {
                out[i / 2] |= uint8_t((block[i * 4 + 3] >> 4) << (i % 2 * 4));
            }
            encodeOpaqueColour(block, out + 8);
            break;
        case XboxTextureFormat::Dxt5:
            stb_compress_dxt_block(out, block, 1, STB_DXT_HIGHQUAL);
            separateEndpoints(out + 8);
            break;
        default:
            break;
    }
}

void decodeBlock(const uint8_t* bytes, XboxTextureFormat format, uint8_t block[64])
{
    const auto* colour       = bytes + (format == XboxTextureFormat::Dxt1 ? 0 : 8);
    const auto  c0           = readU16(colour);
    const auto  c1           = readU16(colour + 2);
    const bool  punchthrough = format == XboxTextureFormat::Dxt1 && c0 <= c1;
    int32_t     palette[4][3]{};
    unpackRgb565(c0, palette[0]);
    unpackRgb565(c1, palette[1]);
    for (uint32_t c = 0; c < 3; ++c)
    {
        palette[2][c] = punchthrough ? (palette[0][c] + palette[1][c]) / 2 : (2 * palette[0][c] + palette[1][c]) / 3;
        palette[3][c] = punchthrough ? 0 : (palette[0][c] + 2 * palette[1][c]) / 3;
    }
    uint8_t alpha[8]{ bytes[0], bytes[1] };
    if (format == XboxTextureFormat::Dxt5)
    {
        if (alpha[0] > alpha[1])
        {
            for (uint32_t i = 2; i < 8; ++i)
            {
                alpha[i] = uint8_t(((8 - i) * alpha[0] + (i - 1) * alpha[1]) / 7);
            }
        }
        else
        {
            for (uint32_t i = 2; i < 6; ++i)
            {
                alpha[i] = uint8_t(((6 - i) * alpha[0] + (i - 1) * alpha[1]) / 5);
            }
            alpha[6] = 0;
            alpha[7] = 255;
        }
    }
    uint64_t alphaIndices = 0;
    for (uint32_t i = 0; i < 6; ++i)
    {
        alphaIndices |= uint64_t(bytes[2 + i]) << (8 * i);
    }
    for (uint32_t i = 0; i < 16; ++i)
    {
        const auto index = (colour[4 + i / 4] >> (2 * (i % 4))) & 3;
        for (uint32_t c = 0; c < 3; ++c)
        {
            block[i * 4 + c] = uint8_t(palette[index][c]);
        }
        uint8_t a = punchthrough && index == 3 ? 0 : 255;
        if (format == XboxTextureFormat::Dxt3)
        {
            a = uint8_t(((bytes[i / 2] >> (4 * (i % 2))) & 15) * 17);
        }
        else if (format == XboxTextureFormat::Dxt5)
        {
            a = alpha[(alphaIndices >> (3 * i)) & 7];
        }
        block[i * 4 + 3] = a;
    }
}

std::expected<std::vector<std::byte>, XboxTextureError> transformSurface(std::span<const std::byte> bytes, uint32_t width, uint32_t height, XboxTextureFormat format, bool tile)
{
    const auto layout = surfaceLayout(width, height, format, true);
    if (!layout)
    {
        return std::unexpected(layout.error());
    }
    if (bytes.size() != (tile ? layout->linearSize : layout->tiledSize))
    {
        return std::unexpected(XboxTextureError::SizeMismatch);
    }
    std::vector<std::byte> out(tile ? layout->tiledSize : layout->linearSize);
    for (std::size_t y = 0; y < layout->heightBlocks; ++y)
    {
        for (std::size_t x = 0; x < layout->widthBlocks; ++x)
        {
            const auto linear = (y * layout->widthBlocks + x) * layout->info.bytesPerBlock;
            const auto tiled  = tiledOffset(x, y, *layout);
            std::memcpy(out.data() + (tile ? tiled : linear), bytes.data() + (tile ? linear : tiled), layout->info.bytesPerBlock);
        }
    }
    return out;
}

} // namespace

std::string_view XboxTextureErrorMessage(XboxTextureError error)
{
    switch (error)
    {
        case XboxTextureError::UnsupportedFormat:
            return "The Xbox texture format is unsupported.";
        case XboxTextureError::InvalidDimensions:
            return "Texture dimensions must be nonzero and tiled dimensions must be powers of two.";
        case XboxTextureError::SizeMismatch:
            return "The texture byte count does not match its layout.";
        case XboxTextureError::SizeOverflow:
            return "The texture layout exceeds the addressable byte range.";
    }
    return "The Xbox texture operation failed.";
}

std::expected<XboxTextureFormatInfo, XboxTextureError> GetXboxTextureFormatInfo(XboxTextureFormat format)
{
    switch (format)
    {
        case XboxTextureFormat::Argb8:
            return XboxTextureFormatInfo{ 1, 1, 4 };
        case XboxTextureFormat::Dxt1:
            return XboxTextureFormatInfo{ 4, 4, 8 };
        case XboxTextureFormat::Dxt3:
        case XboxTextureFormat::Dxt5:
            return XboxTextureFormatInfo{ 4, 4, 16 };
    }
    return std::unexpected(XboxTextureError::UnsupportedFormat);
}

std::expected<std::vector<std::byte>, XboxTextureError> TileSurface(std::span<const std::byte> bytes, uint32_t width, uint32_t height, XboxTextureFormat format)
{
    return transformSurface(bytes, width, height, format, true);
}

std::expected<std::vector<std::byte>, XboxTextureError> UntileSurface(std::span<const std::byte> bytes, uint32_t width, uint32_t height, XboxTextureFormat format)
{
    return transformSurface(bytes, width, height, format, false);
}

std::expected<void, XboxTextureError> SwapEndian(std::span<std::byte> bytes, XboxTextureFormat format)
{
    const auto info = GetXboxTextureFormatInfo(format);
    if (!info)
    {
        return std::unexpected(info.error());
    }
    const std::size_t unit = format == XboxTextureFormat::Argb8 ? 4 : 2;
    if (bytes.size() % unit != 0)
    {
        return std::unexpected(XboxTextureError::SizeMismatch);
    }
    for (std::size_t i = 0; i < bytes.size(); i += unit)
    {
        std::reverse(bytes.begin() + i, bytes.begin() + i + unit);
    }
    return {};
}

std::expected<std::vector<std::byte>, XboxTextureError> EncodeBlocks(const Rgba8Image& image, XboxTextureFormat format)
{
    const auto layout = surfaceLayout(image.width, image.height, format, false);
    if (!layout)
    {
        return std::unexpected(layout.error());
    }
    const auto size = rgbaSize(image.width, image.height);
    if (!size)
    {
        return std::unexpected(size.error());
    }
    if (image.rgba8.size() != *size)
    {
        return std::unexpected(XboxTextureError::SizeMismatch);
    }
    std::vector<std::byte> out(layout->linearSize);
    const auto*            pixels = reinterpret_cast<const uint8_t*>(image.rgba8.data());
    auto*                  dest   = reinterpret_cast<uint8_t*>(out.data());
    if (format == XboxTextureFormat::Argb8)
    {
        for (std::size_t i = 0; i < *size; i += 4)
        {
            dest[i]     = pixels[i + 2];
            dest[i + 1] = pixels[i + 1];
            dest[i + 2] = pixels[i];
            dest[i + 3] = pixels[i + 3];
        }
        return out;
    }
    for (uint32_t y = 0; y < layout->heightBlocks; ++y)
    {
        for (uint32_t x = 0; x < layout->widthBlocks; ++x)
        {
            uint8_t block[64];
            gatherBlock(pixels, image.width, image.height, x, y, block);
            encodeBlock(block, format, dest + (std::size_t(y) * layout->widthBlocks + x) * layout->info.bytesPerBlock);
        }
    }
    return out;
}

std::expected<Rgba8Image, XboxTextureError> DecodeBlocks(std::span<const std::byte> bytes, uint32_t width, uint32_t height, XboxTextureFormat format)
{
    const auto layout = surfaceLayout(width, height, format, false);
    if (!layout)
    {
        return std::unexpected(layout.error());
    }
    const auto size = rgbaSize(width, height);
    if (!size)
    {
        return std::unexpected(size.error());
    }
    if (bytes.size() != layout->linearSize)
    {
        return std::unexpected(XboxTextureError::SizeMismatch);
    }
    Rgba8Image out;
    out.width           = width;
    out.height          = height;
    out.hasAlphaChannel = true;
    out.rgba8.resize(*size);
    const auto* source = reinterpret_cast<const uint8_t*>(bytes.data());
    auto*       pixels = reinterpret_cast<uint8_t*>(out.rgba8.data());
    if (format == XboxTextureFormat::Argb8)
    {
        for (std::size_t i = 0; i < *size; i += 4)
        {
            pixels[i]     = source[i + 2];
            pixels[i + 1] = source[i + 1];
            pixels[i + 2] = source[i];
            pixels[i + 3] = source[i + 3];
        }
        return out;
    }
    for (std::size_t y = 0; y < layout->heightBlocks; ++y)
    {
        for (std::size_t x = 0; x < layout->widthBlocks; ++x)
        {
            uint8_t block[64];
            decodeBlock(source + (y * layout->widthBlocks + x) * layout->info.bytesPerBlock, format, block);
            for (std::size_t by = 0; by < 4 && y * 4 + by < height; ++by)
            {
                for (std::size_t bx = 0; bx < 4 && x * 4 + bx < width; ++bx)
                {
                    std::memcpy(pixels + ((y * 4 + by) * width + x * 4 + bx) * 4, block + (by * 4 + bx) * 4, 4);
                }
            }
        }
    }
    return out;
}

// Each level above the packed base fills its own tiles, and the base and smaller
// levels share one region, as in Xenia GetGuestTextureLayout. No pixel generation.
std::expected<XboxMipChain, XboxTextureError> DescribeMipChain(uint32_t width, uint32_t height, XboxTextureFormat format)
{
    const auto first = surfaceLayout(width, height, format, true);
    if (!first)
    {
        return std::unexpected(first.error());
    }
    XboxMipChain chain;
    chain.packedTailBaseLevel = packedMipBase(width, height);
    chain.tiledSize           = 0;
    const auto  tail          = surfaceLayout(std::max(1U, width >> chain.packedTailBaseLevel), std::max(1U, height >> chain.packedTailBaseLevel), format, true);
    std::size_t tailRegion    = 0;
    uint32_t    w = width, h = height;
    for (uint32_t mip = 0;; ++mip)
    {
        const auto   layout = surfaceLayout(w, h, format, true);
        XboxMipLevel level{ w, h, layout->linearSize, layout->tiledSize, chain.tiledSize, std::nullopt };
        if (mip <= chain.packedTailBaseLevel)
        {
            if (chain.tiledSize > std::vector<std::byte>().max_size() - layout->tiledSize)
            {
                return std::unexpected(XboxTextureError::SizeOverflow);
            }
            tailRegion = chain.tiledSize;
            chain.tiledSize += layout->tiledSize;
        }
        if (mip >= chain.packedTailBaseLevel)
        {
            level.regionOffset = tailRegion;
            level.packedOffset = packedMipOffset(width, height, mip, *tail);
        }
        chain.levels.push_back(level);
        if (w == 1 && h == 1)
        {
            break;
        }
        w = std::max(1U, w / 2);
        h = std::max(1U, h / 2);
    }
    return chain;
}

} // namespace rerevved::tools
