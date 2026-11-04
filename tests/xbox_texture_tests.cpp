#include "xbox_texture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{

using namespace rerevved::tools;
using Format = XboxTextureFormat;
using Error  = XboxTextureError;
constexpr std::array kFormats{ Format::Argb8, Format::Dxt1, Format::Dxt3, Format::Dxt5 };
int                  failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename T>
void expectError(const std::expected<T, Error>& result, Error error, std::string_view message)
{
    expect(!result && result.error() == error, message);
}

Rgba8Image solid(std::uint32_t width, std::uint32_t height, std::array<unsigned, 4> rgba)
{
    Rgba8Image image;
    image.width           = width;
    image.height          = height;
    image.hasAlphaChannel = true;
    image.rgba8.resize(std::size_t(width) * height * 4);
    for (std::size_t i = 0; i < image.rgba8.size(); ++i)
    {
        image.rgba8[i] = static_cast<std::byte>(rgba[i % 4]);
    }
    return image;
}

void testTiling()
{
    for (const auto format : kFormats)
    {
        for (const auto size : std::array{ std::array{ 1U, 1U }, std::array{ 2U, 4U }, std::array{ 4U, 4U }, std::array{ 8U, 256U }, std::array{ 256U, 8U }, std::array{ 128U, 256U }, std::array{ 256U, 256U } })
        {
            const auto chain = DescribeMipChain(size[0], size[1], format);
            expect(chain.has_value(), "power-of-two layout accepted");
            if (!chain)
            {
                continue;
            }
            const auto info = *GetXboxTextureFormatInfo(format);
            expect(info.blockWidth == (format == Format::Argb8 ? 1U : 4U) && info.blockHeight == info.blockWidth && info.bytesPerBlock == (format == Format::Argb8 ? 4U : format == Format::Dxt1 ? 8U
                                                                                                                                                                                                 : 16U),
                   "format block geometry");
            const auto wb = (size[0] + info.blockWidth - 1) / info.blockWidth;
            const auto hb = (size[1] + info.blockHeight - 1) / info.blockHeight;
            expect(chain->levels[0].tiledSize == std::size_t((wb + 31) / 32) * ((hb + 31) / 32) * 1024 * info.bytesPerBlock, "tiled pitch and height align to 32 blocks");
            std::vector<std::byte> linear(chain->levels[0].linearSize);
            for (std::size_t i = 0; i < linear.size(); ++i)
            {
                linear[i] = static_cast<std::byte>((i * 37 + i / 251) & 255);
            }
            const auto original = linear;
            const auto tiled    = TileSurface(linear, size[0], size[1], format);
            expect(tiled && tiled->size() == chain->levels[0].tiledSize, "tile size includes padding");
            if (tiled)
            {
                const auto untiled = UntileSurface(*tiled, size[0], size[1], format);
                expect(untiled && *untiled == original, "tile then untile identity");
                expect(linear == original, "tiling preserves source");
            }
        }
    }
    std::vector<std::byte> blocks(32 * 32 * 8);
    blocks[(16 * 32) * 8] = std::byte{ 0xA1 };
    blocks[(1 * 32) * 8]  = std::byte{ 0xB2 };
    blocks[8 * 8]         = std::byte{ 0xC3 };
    const auto tiled      = TileSurface(blocks, 128, 128, Format::Dxt1);
    expect(tiled && (*tiled)[2048] == std::byte{ 0xA1 } && (*tiled)[16] == std::byte{ 0xB2 } && (*tiled)[64] == std::byte{ 0xC3 }, "fixed Xenos bank, row and pipe addresses");

    // Hand-evaluated Xenia GetTiledOffset2D: entries are (x, y, byte offset)
    // in blocks. Reference pitch is in blocks, not macro tiles.
    auto checkAddresses = [](Format format, unsigned width, unsigned height, std::span<const std::array<unsigned, 3>> addresses)
    {
        const auto             info        = *GetXboxTextureFormatInfo(format);
        const auto             widthBlocks = width / info.blockWidth;
        std::vector<std::byte> linear(std::size_t(widthBlocks) * (height / info.blockHeight) * info.bytesPerBlock);
        for (std::size_t i = 0; i < addresses.size(); ++i)
        {
            linear[(std::size_t(addresses[i][1]) * widthBlocks + addresses[i][0]) * info.bytesPerBlock] = static_cast<std::byte>(i + 1);
        }
        const auto result = TileSurface(linear, width, height, format);
        expect(result.has_value(), "golden multi-tile surface accepted");
        if (result)
        {
            for (std::size_t i = 0; i < addresses.size(); ++i)
            {
                expect(addresses[i][2] < result->size() && (*result)[addresses[i][2]] == static_cast<std::byte>(i + 1), "hand-derived multi-tile byte address");
            }
        }
    };
    // ARGB 64x128: pitch=64 (2 macro tiles), log2(bytes)=2.
    // macro=((x>>5)+(y>>5)*2)<<9; micro=((x&7)+((y&14)<<2))<<2.
    const std::array<std::array<unsigned, 3>, 8> argbTwoTiles{ { { 7, 6, 812 }, { 16, 16, 2176 }, { 0, 32, 8192 }, { 32, 32, 12288 }, { 40, 33, 12368 }, { 63, 63, 16252 }, { 0, 64, 16384 }, { 63, 95, 24444 } } };
    checkAddresses(Format::Argb8, 64, 128, argbTwoTiles);
    // ARGB 128x128: pitch=128 (4 macro tiles), log2(bytes)=2.
    // macro=((x>>5)+(y>>5)*4)<<9; micro=((x&7)+((y&14)<<2))<<2.
    const std::array<std::array<unsigned, 3>, 8> argbFourTiles{ { { 7, 6, 812 }, { 16, 16, 2176 }, { 0, 32, 16384 }, { 96, 32, 28672 }, { 104, 33, 28752 }, { 127, 63, 32636 }, { 0, 64, 32768 }, { 127, 95, 49020 } } };
    checkAddresses(Format::Argb8, 128, 128, argbFourTiles);
    // DXT5 256x512 = 64x128 blocks: pitch=64 (2 macro tiles), log2(bytes)=4.
    // macro=((x>>5)+(y>>5)*2)<<11; micro=((x&7)+((y&14)<<2))<<4.
    const std::array<std::array<unsigned, 3>, 8> bc3TwoTiles{ { { 7, 6, 5920 }, { 16, 16, 2176 }, { 0, 32, 32768 }, { 32, 32, 49152 }, { 40, 33, 49232 }, { 63, 63, 65392 }, { 0, 64, 65536 }, { 63, 95, 98160 } } };
    checkAddresses(Format::Dxt5, 256, 512, bc3TwoTiles);
    // DXT5 512x512 = 128x128 blocks: pitch=128 (4 macro tiles), log2(bytes)=4.
    // macro=((x>>5)+(y>>5)*4)<<11; micro=((x&7)+((y&14)<<2))<<4.
    const std::array<std::array<unsigned, 3>, 8> bc3FourTiles{ { { 7, 6, 5920 }, { 16, 16, 2176 }, { 0, 32, 65536 }, { 96, 32, 114688 }, { 104, 33, 114768 }, { 127, 63, 130928 }, { 0, 64, 131072 }, { 127, 95, 196464 } } };
    checkAddresses(Format::Dxt5, 512, 512, bc3FourTiles);
    const auto padded = TileSurface(std::array{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 } }, 1, 1, Format::Argb8);
    expect(padded && std::all_of(padded->begin() + 4, padded->end(), [](std::byte b)
                                 {
                                     return b == std::byte{};
                                 }),
           "unused tiled padding is zero");
}

void testEndian()
{
    for (const auto format : kFormats)
    {
        std::array bytes{ std::byte{ 0x12 }, std::byte{ 0x34 }, std::byte{ 0x56 }, std::byte{ 0x78 } };
        const auto original = bytes;
        expect(SwapEndian(bytes, format).has_value(), "endian swap accepted");
        const auto expected = format == Format::Argb8 ? std::array{ std::byte{ 0x78 }, std::byte{ 0x56 }, std::byte{ 0x34 }, std::byte{ 0x12 } } : std::array{ std::byte{ 0x34 }, std::byte{ 0x12 }, std::byte{ 0x78 }, std::byte{ 0x56 } };
        expect(bytes == expected, "known endian word");
        expect(SwapEndian(bytes, format).has_value() && bytes == original, "endian swap involution");
    }
    std::span<std::byte> empty;
    expect(SwapEndian(empty, Format::Dxt1).has_value(), "empty endian span accepted");
}

void testSolidsAndEdges()
{
    for (const auto format : kFormats)
    {
        for (const auto rgba : std::array{ std::array{ 255U, 0U, 0U, 255U }, std::array{ 0U, 255U, 0U, 255U }, std::array{ 0U, 0U, 255U, 255U }, std::array{ 0U, 0U, 0U, 255U }, std::array{ 255U, 255U, 255U, 255U }, std::array{ 132U, 130U, 66U, 255U } })
        {
            for (const auto size : std::array{ std::array{ 4U, 4U }, std::array{ 1U, 1U }, std::array{ 3U, 7U } })
            {
                const auto image   = solid(size[0], size[1], rgba);
                const auto encoded = EncodeBlocks(image, format);
                expect(encoded.has_value(), "encode solid and partial blocks");
                if (encoded)
                {
                    const auto decoded = DecodeBlocks(*encoded, size[0], size[1], format);
                    expect(decoded && decoded->rgba8 == image.rgba8 && decoded->width == size[0] && decoded->height == size[1] && decoded->hasAlphaChannel, "solid exact RGBA and dimensions");
                }
            }
        }
    }
    const auto image = solid(1, 1, { 12, 34, 56, 78 });
    const auto argb  = EncodeBlocks(image, Format::Argb8);
    expect(argb && *argb == std::vector{ std::byte{ 56 }, std::byte{ 34 }, std::byte{ 12 }, std::byte{ 78 } }, "ARGB word is BGRA in little endian");
    const auto transparent = solid(4, 4, { 0, 0, 0, 0 });
    const auto bc1         = EncodeBlocks(transparent, Format::Dxt1);
    expect(bc1 && DecodeBlocks(*bc1, 4, 4, Format::Dxt1)->rgba8 == transparent.rgba8, "BC1 transparent black");
    auto mixed = solid(4, 4, { 255, 0, 0, 255 });
    for (std::size_t i = 0; i < 8; ++i)
    {
        std::fill_n(mixed.rgba8.begin() + i * 4, 4, std::byte{});
    }
    const auto mixedBc1 = EncodeBlocks(mixed, Format::Dxt1);
    expect(mixedBc1 && DecodeBlocks(*mixedBc1, 4, 4, Format::Dxt1)->rgba8 == mixed.rgba8, "BC1 mixed opaque and punchthrough texels");
}

void testEndpointOrder()
{
    auto word = [](std::span<const std::byte> bytes, unsigned offset)
    {
        return std::to_integer<unsigned>(bytes[offset]) | (std::to_integer<unsigned>(bytes[offset + 1]) << 8);
    };
    for (const auto format : { Format::Dxt1, Format::Dxt3, Format::Dxt5 })
    {
        const auto encoded = EncodeBlocks(solid(4, 4, { 255, 0, 0, 255 }), format);
        expect(encoded.has_value(), "opaque endpoint-order block encoded");
        if (encoded)
        {
            const unsigned offset = format == Format::Dxt1 ? 0 : 8;
            expect(word(*encoded, offset) > word(*encoded, offset + 2), "opaque colour endpoints have c0 > c1");
        }
    }
    auto image             = solid(4, 4, { 255, 0, 0, 255 });
    image.rgba8[5 * 4 + 3] = std::byte{ 127 };
    image.rgba8[6 * 4 + 3] = std::byte{ 128 };
    const auto encoded     = EncodeBlocks(image, Format::Dxt1);
    expect(encoded.has_value(), "punchthrough endpoint-order block encoded");
    if (encoded)
    {
        expect(word(*encoded, 0) <= word(*encoded, 2), "punchthrough endpoints have c0 <= c1");
        const auto indices = std::to_integer<unsigned>((*encoded)[5]);
        expect(((indices >> 2) & 3) == 3, "alpha 127 texel uses BC1 index 3");
        expect(((indices >> 4) & 3) != 3, "alpha 128 texel remains opaque");
    }
}

void testGradients()
{
    auto image = solid(4, 4, { 0, 0, 0, 255 });
    for (unsigned i = 0; i < 16; ++i)
    {
        for (unsigned c = 0; c < 3; ++c)
        {
            image.rgba8[i * 4 + c] = static_cast<std::byte>(i * 17);
        }
    }
    for (const auto format : { Format::Dxt1, Format::Dxt3, Format::Dxt5 })
    {
        const auto encoded = EncodeBlocks(image, format);
        const auto decoded = encoded ? DecodeBlocks(*encoded, 4, 4, format) : std::expected<Rgba8Image, Error>(std::unexpected(encoded.error()));
        expect(decoded.has_value(), "gradient encode and decode accepted");
        if (decoded)
        {
            for (std::size_t i = 0; i < image.rgba8.size(); ++i)
            {
                const int delta = std::to_integer<int>(image.rgba8[i]) - std::to_integer<int>(decoded->rgba8[i]);
                expect(delta >= -34 && delta <= 34, "gradient maximum per-channel error is 34");
            }
        }
    }
}

void testAlpha()
{
    auto image = solid(4, 4, { 255, 0, 0, 255 });
    for (unsigned i = 0; i < 16; ++i)
    {
        image.rgba8[i * 4 + 3] = static_cast<std::byte>(i * 16 + 7);
    }
    const auto bc2 = EncodeBlocks(image, Format::Dxt3);
    expect(bc2.has_value(), "BC2 alpha encode");
    if (bc2)
    {
        const auto decoded = DecodeBlocks(*bc2, 4, 4, Format::Dxt3);
        expect(decoded.has_value(), "BC2 alpha decode");
        for (unsigned i = 0; i < 8; ++i)
        {
            expect((*bc2)[i] == static_cast<std::byte>(2 * i | ((2 * i + 1) << 4)), "BC2 exact packed nibbles");
        }
        if (decoded)
        {
            for (unsigned i = 0; i < 16; ++i)
            {
                expect(decoded->rgba8[i * 4 + 3] == static_cast<std::byte>(i * 17), "BC2 exact floor-to-4-bit alpha quantization");
            }
        }
    }
    for (unsigned i = 0; i < 16; ++i)
    {
        image.rgba8[i * 4 + 3] = static_cast<std::byte>(i % 2 == 0 ? 204 : 51);
    }
    const auto bc3 = EncodeBlocks(image, Format::Dxt5);
    expect(bc3.has_value(), "BC3 two-level alpha encode");
    if (bc3)
    {
        // Alternating indices 0,1 repeated: 001000 001000 = 0x208208208208.
        const std::array<unsigned, 8> expected{ 204, 51, 0x08, 0x82, 0x20, 0x08, 0x82, 0x20 };
        for (unsigned i = 0; i < 8; ++i)
        {
            expect((*bc3)[i] == static_cast<std::byte>(expected[i]), "BC3 exact endpoints and 48-bit indices");
        }
        const auto decoded = DecodeBlocks(*bc3, 4, 4, Format::Dxt5);
        expect(decoded && decoded->rgba8 == image.rgba8, "BC3 two-level alpha exact RGBA");
    }
    for (const bool eightValues : { false, true })
    {
        std::vector<std::byte> block(16);
        block[0]              = static_cast<std::byte>(eightValues ? 210 : 10);
        block[1]              = static_cast<std::byte>(eightValues ? 0 : 110);
        std::uint64_t indices = 0;
        for (unsigned i = 0; i < 16; ++i)
        {
            indices |= std::uint64_t(i % 8) << (3 * i);
        }
        for (unsigned i = 0; i < 6; ++i)
        {
            block[2 + i] = static_cast<std::byte>((indices >> (8 * i)) & 255);
        }
        const auto decoded = DecodeBlocks(block, 4, 4, Format::Dxt5);
        const auto palette = eightValues ? std::array{ 210, 0, 180, 150, 120, 90, 60, 30 } : std::array{ 10, 110, 30, 50, 70, 90, 0, 255 };
        expect(decoded.has_value(), "golden BC3 alpha block decoded");
        if (decoded)
        {
            for (unsigned i = 0; i < 16; ++i)
            {
                expect(decoded->rgba8[i * 4 + 3] == static_cast<std::byte>(palette[i % 8]), "BC3 exact alpha palette and indices");
            }
        }
    }
    // Equal colour endpoints select transparency only in BC1, not BC2/BC3.
    for (const auto format : { Format::Dxt1, Format::Dxt3, Format::Dxt5 })
    {
        std::vector<std::byte> block(format == Format::Dxt1 ? 8 : 16, std::byte{ 0xFF });
        const unsigned         offset = format == Format::Dxt1 ? 0 : 8;
        std::fill_n(block.begin() + offset, 4, std::byte{});
        const auto decoded = DecodeBlocks(block, 4, 4, format);
        expect(decoded && decoded->rgba8[3] == (format == Format::Dxt1 ? std::byte{} : std::byte{ 255 }), "equal endpoints use format-specific colour mode");
    }
}

void testColourPalettes()
{
    for (const auto format : { Format::Dxt1, Format::Dxt3, Format::Dxt5 })
    {
        for (const bool reversed : { false, true })
        {
            std::vector<std::byte> block(format == Format::Dxt1 ? 8 : 16, std::byte{ 0xFF });
            const unsigned         offset = format == Format::Dxt1 ? 0 : 8;
            block[offset]                 = static_cast<std::byte>(reversed ? 0x1F : 0x00);
            block[offset + 1]             = static_cast<std::byte>(reversed ? 0x00 : 0xF8);
            block[offset + 2]             = static_cast<std::byte>(reversed ? 0x00 : 0x1F);
            block[offset + 3]             = static_cast<std::byte>(reversed ? 0xF8 : 0x00);
            std::fill_n(block.begin() + offset + 4, 4, std::byte{ 0xE4 });
            const auto decoded = DecodeBlocks(block, 4, 4, format);
            expect(decoded.has_value(), "golden colour block accepted");
            if (!decoded)
            {
                continue;
            }
            std::array<std::array<unsigned, 4>, 4> palette{};
            if (!reversed)
            {
                palette = { { { 255, 0, 0, 255 }, { 0, 0, 255, 255 }, { 170, 0, 85, 255 }, { 85, 0, 170, 255 } } };
            }
            else if (format == Format::Dxt1)
            {
                palette = { { { 0, 0, 255, 255 }, { 255, 0, 0, 255 }, { 127, 0, 127, 255 }, { 0, 0, 0, 0 } } };
            }
            else
            {
                palette = { { { 0, 0, 255, 255 }, { 255, 0, 0, 255 }, { 85, 0, 170, 255 }, { 170, 0, 85, 255 } } };
            }
            for (unsigned i = 0; i < 16; ++i)
            {
                for (unsigned c = 0; c < 4; ++c)
                {
                    expect(decoded->rgba8[i * 4 + c] == static_cast<std::byte>(palette[i % 4][c]), "golden BC colour palette RGBA");
                }
            }
        }
    }
}

void testMipChain()
{
    const auto chain = DescribeMipChain(256, 256, Format::Dxt1);
    expect(chain && chain->levels.size() == 9 && chain->packedTailBaseLevel == 4 && chain->tiledSize == 65536, "256x256 BC1 full chain and tail storage");
    if (!chain || chain->levels.size() != 9)
    {
        return;
    }
    const std::array<unsigned, 9>    dimensions{ 256, 128, 64, 32, 16, 8, 4, 2, 1 };
    const std::array<std::size_t, 9> linear{ 32768, 8192, 2048, 512, 128, 32, 8, 8, 8 };
    const std::array<std::size_t, 9> tiled{ 32768, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192 };
    const std::array<std::size_t, 9> regions{ 0, 32768, 40960, 49152, 57344, 57344, 57344, 57344, 57344 };
    // Xenia GetPackedMipOffset, square 256: base = log2(256)-4 = 4.
    // Tail is 16x16 texels: /4 then align to 32 blocks gives pitch=32 (1 macro tile).
    const std::array<unsigned, 5>    x{ 4, 2, 1, 0, 0 }, y{ 0, 0, 0, 2, 1 };
    const std::array<std::size_t, 5> offsets{ 256, 32, 8, 512, 16 };
    for (unsigned i = 0; i < 9; ++i)
    {
        const auto& level = chain->levels[i];
        expect(level.width == dimensions[i] && level.height == dimensions[i] && level.linearSize == linear[i] && level.tiledSize == tiled[i] && level.regionOffset == regions[i], "exact mip dimensions, sizes and region");
        expect(level.packedOffset.has_value() == (i >= 4), "only tail levels have packed origins");
        if (i >= 4 && level.packedOffset)
        {
            expect(level.packedOffset->xBlocks == x[i - 4] && level.packedOffset->yBlocks == y[i - 4] && level.packedOffset->byteOffset == offsets[i - 4], "hand-derived packed tail origins");
        }
    }
    const auto wide = DescribeMipChain(256, 128, Format::Dxt1);
    expect(wide && wide->levels.size() == 9 && wide->packedTailBaseLevel == 3 && wide->tiledSize == 40960, "256x128 BC1 full chain and tail storage");
    // Xenia GetPackedMipOffset: logs=(8,7), base=7-4=3, packed=0..5.
    // Texel origins: (0,16>>p) for p<3, then (32>>(p-2),0); divide by 4.
    // Tail 32x16 texels aligns from 8x4 to 32x32 blocks: pitch=32, log2(bytes)=3.
    const std::array<XboxPackedMipOffset, 6> wideOffsets{ { { 0, 4, 1024 }, { 0, 2, 512 }, { 0, 1, 16 }, { 4, 0, 256 }, { 2, 0, 32 }, { 1, 0, 8 } } };
    if (wide && wide->levels.size() == 9)
    {
        for (unsigned p = 0; p < wideOffsets.size(); ++p)
        {
            const auto& level    = wide->levels[3 + p];
            const auto& expected = wideOffsets[p];
            expect(level.regionOffset == 32768 && level.tiledSize == 8192 && level.packedOffset.has_value(), "wide tail shares one aligned region");
            if (level.packedOffset)
            {
                expect(level.packedOffset->xBlocks == expected.xBlocks && level.packedOffset->yBlocks == expected.yBlocks && level.packedOffset->byteOffset == expected.byteOffset, "hand-derived wide packed tail origins and bytes");
            }
        }
    }
    const auto argb = DescribeMipChain(128, 128, Format::Argb8);
    expect(argb && argb->levels.size() == 8 && argb->packedTailBaseLevel == 3 && argb->tiledSize == 90112, "128x128 ARGB full chain and tail storage");
    // Xenia GetPackedMipOffset: logs=(7,7), base=7-4=3, packed=0..4.
    // Origins: (16>>p,0) for p<3, then (0,16>>(p-2)); ARGB blocks are texels.
    // Tail 16x16 aligns to 32x32 blocks: pitch=32, log2(bytes)=2.
    const std::array<XboxPackedMipOffset, 5> argbOffsets{ { { 16, 0, 128 }, { 8, 0, 64 }, { 4, 0, 32 }, { 0, 8, 1152 }, { 0, 4, 512 } } };
    if (argb && argb->levels.size() == 8)
    {
        for (unsigned p = 0; p < argbOffsets.size(); ++p)
        {
            const auto& level    = argb->levels[3 + p];
            const auto& expected = argbOffsets[p];
            expect(level.regionOffset == 86016 && level.tiledSize == 4096 && level.packedOffset.has_value(), "ARGB tail shares one aligned region");
            if (level.packedOffset)
            {
                expect(level.packedOffset->xBlocks == expected.xBlocks && level.packedOffset->yBlocks == expected.yBlocks && level.packedOffset->byteOffset == expected.byteOffset, "hand-derived ARGB packed tail origins and bytes");
            }
        }
    }
    const auto tall = DescribeMipChain(128, 256, Format::Argb8);
    expect(tall && tall->packedTailBaseLevel == 3 && tall->levels[3].packedOffset->xBlocks == 16 && tall->levels[6].packedOffset->yBlocks == 16, "tall uncompressed tail uses texel origins");
    const auto tiny = DescribeMipChain(1, 1, Format::Dxt1);
    expect(tiny && tiny->levels.size() == 1 && tiny->packedTailBaseLevel == 0 && tiny->levels[0].packedOffset->xBlocks == 4, "whole smallest chain is packed");
}

void testInvalidInputs()
{
    constexpr auto           bad = static_cast<Format>(-1);
    std::array<std::byte, 3> bytes{};
    expectError(GetXboxTextureFormatInfo(bad), Error::UnsupportedFormat, "invalid format metadata");
    expectError(SwapEndian(bytes, bad), Error::UnsupportedFormat, "invalid endian format");
    expectError(SwapEndian(bytes, Format::Dxt1), Error::SizeMismatch, "incomplete 16-bit unit");
    expectError(SwapEndian(bytes, Format::Argb8), Error::SizeMismatch, "incomplete 32-bit unit");
    for (const auto format : kFormats)
    {
        expectError(TileSurface({}, 0, 4, format), Error::InvalidDimensions, "zero tile width");
        expectError(UntileSurface({}, 4, 0, format), Error::InvalidDimensions, "zero untile height");
        expectError(DescribeMipChain(0, 4, format), Error::InvalidDimensions, "zero mip width");
        expectError(TileSurface({}, 3, 4, format), Error::InvalidDimensions, "non-power-of-two tile width");
        expectError(UntileSurface({}, 4, 3, format), Error::InvalidDimensions, "non-power-of-two untile height");
        expectError(DescribeMipChain(3, 4, format), Error::InvalidDimensions, "non-power-of-two mip width");
        expectError(TileSurface({}, 4, 4, format), Error::SizeMismatch, "short linear surface");
        expectError(UntileSurface({}, 4, 4, format), Error::SizeMismatch, "short tiled surface");
        const auto mip = *DescribeMipChain(4, 4, format);
        expectError(TileSurface(std::vector<std::byte>(mip.levels[0].linearSize + 1), 4, 4, format), Error::SizeMismatch, "oversized linear surface");
        expectError(UntileSurface(std::vector<std::byte>(mip.levels[0].tiledSize + 1), 4, 4, format), Error::SizeMismatch, "oversized tiled surface");
        auto image = solid(4, 4, { 0, 0, 0, 255 });
        image.rgba8.pop_back();
        expectError(EncodeBlocks(image, format), Error::SizeMismatch, "short RGBA input");
        image.rgba8.resize(65);
        expectError(EncodeBlocks(image, format), Error::SizeMismatch, "oversized RGBA input");
        image.width = 0;
        expectError(EncodeBlocks(image, format), Error::InvalidDimensions, "zero encode dimensions");
        expectError(DecodeBlocks({}, 4, 0, format), Error::InvalidDimensions, "zero decode dimensions");
        expectError(DecodeBlocks({}, 4, 4, format), Error::SizeMismatch, "short encoded input");
        expectError(DecodeBlocks(std::vector<std::byte>(mip.levels[0].linearSize + 1), 4, 4, format), Error::SizeMismatch, "oversized encoded input");
    }
    expectError(TileSurface({}, 4, 4, bad), Error::UnsupportedFormat, "invalid tile format");
    expectError(UntileSurface({}, 4, 4, bad), Error::UnsupportedFormat, "invalid untile format");
    expectError(DescribeMipChain(4, 4, bad), Error::UnsupportedFormat, "invalid mip format");
    expectError(EncodeBlocks(solid(4, 4, { 0, 0, 0, 255 }), bad), Error::UnsupportedFormat, "invalid encode format");
    expectError(DecodeBlocks({}, 4, 4, bad), Error::UnsupportedFormat, "invalid decode format");
    constexpr auto huge = 0x80000000U;
    expectError(TileSurface({}, huge, huge, Format::Argb8), Error::SizeOverflow, "tile layout overflow");
    expectError(UntileSurface({}, huge, huge, Format::Argb8), Error::SizeOverflow, "untile layout overflow");
    expectError(DescribeMipChain(huge, huge, Format::Argb8), Error::SizeOverflow, "mip layout overflow");
    Rgba8Image enormous;
    enormous.width  = huge;
    enormous.height = huge;
    expectError(EncodeBlocks(enormous, Format::Argb8), Error::SizeOverflow, "encode layout overflow");
    expectError(DecodeBlocks({}, huge, huge, Format::Argb8), Error::SizeOverflow, "decode layout overflow");
    expectError(EncodeBlocks(enormous, Format::Dxt1), Error::SizeOverflow, "encode decoded RGBA size overflow");
    expectError(DecodeBlocks({}, huge, huge, Format::Dxt1), Error::SizeOverflow, "decode RGBA size overflow");
    expect(XboxTextureErrorMessage(Error::UnsupportedFormat) == "The Xbox texture format is unsupported.", "format error message");
    expect(XboxTextureErrorMessage(Error::InvalidDimensions) == "Texture dimensions must be nonzero and tiled dimensions must be powers of two.", "dimension error message");
    expect(XboxTextureErrorMessage(Error::SizeMismatch) == "The texture byte count does not match its layout.", "size error message");
    expect(XboxTextureErrorMessage(Error::SizeOverflow) == "The texture layout exceeds the addressable byte range.", "overflow error message");
    expect(!XboxTextureErrorMessage(static_cast<Error>(-1)).empty(), "unknown error has fallback message");
}

} // namespace

int main()
{
    testTiling();
    testEndian();
    testSolidsAndEdges();
    testEndpointOrder();
    testGradients();
    testAlpha();
    testColourPalettes();
    testMipChain();
    testInvalidInputs();
    if (failures != 0)
    {
        std::cerr << failures << " Xbox texture test(s) failed.\n";
        return 1;
    }
    std::cout << "Xbox texture tests passed.\n";
    return 0;
}
