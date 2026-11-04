#include "xbox_dds.h"

#include "dds_writer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

namespace
{

using namespace rerevved::tools;
using Format = XboxTextureFormat;
using Layout = DdsPayloadLayout;
using Error  = XboxDdsWriteError;
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

void expectPreviewError(const std::expected<DdsDocument, DdsDocumentError>& result, DdsPreviewError error, std::string_view message)
{
    expect(!result && result.error() == DdsDocumentError{ error }, message);
}

void writeU32Le(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        bytes[offset + i] = static_cast<std::byte>((value >> (i * 8)) & 255U);
    }
}

std::array<std::byte, 128> argbHeader(std::uint32_t width, std::uint32_t height, std::uint32_t count = 0, bool mipFlag = false)
{
    std::array<std::byte, 128> bytes{};
    for (const auto [offset, value] : std::array{
             std::pair{ 0U, 0x20534444U }, std::pair{ 4U, 124U }, std::pair{ 8U, 0x100FU | (mipFlag ? 0x20000U : 0U) }, std::pair{ 12U, height }, std::pair{ 16U, width }, std::pair{ 28U, count }, std::pair{ 76U, 32U }, std::pair{ 80U, 0x41U }, std::pair{ 88U, 32U }, std::pair{ 92U, 0x00FF0000U }, std::pair{ 96U, 0x0000FF00U }, std::pair{ 100U, 0x000000FFU }, std::pair{ 104U, 0xFF000000U } })
    {
        writeU32Le(bytes, offset, value);
    }
    return bytes;
}

Rgba8Image image(std::uint32_t width, std::uint32_t height)
{
    Rgba8Image result;
    result.width           = width;
    result.height          = height;
    result.hasAlphaChannel = true;
    result.rgba8.resize(std::size_t(width) * height * 4);
    for (std::size_t i = 0; i < result.rgba8.size(); ++i)
    {
        result.rgba8[i] = static_cast<std::byte>((i * 37 + i / 11) & 255U);
    }
    return result;
}

std::vector<Rgba8Image> levels(std::uint32_t width, std::uint32_t height, unsigned count)
{
    std::vector<Rgba8Image> result;
    for (unsigned i = 0; i < count; ++i)
    {
        result.push_back(image(width, height));
        width  = std::max(1U, width / 2);
        height = std::max(1U, height / 2);
    }
    return result;
}

void expectHeader(std::span<const std::byte> bytes, const std::array<std::uint32_t, 32>& words)
{
    expect(bytes.size() >= 128, "complete DDS header");
    if (bytes.size() < 128)
    {
        return;
    }
    for (std::size_t i = 0; i < 128; ++i)
    {
        expect(bytes[i] == static_cast<std::byte>((words[i / 4] >> ((i % 4) * 8)) & 255U), "exact DDS header byte");
    }
}

void testHeadersAndLegacy()
{
    const auto argb = EncodeXboxDds(levels(2, 1, 1), {});
    expect(argb.has_value(), "single ARGB encodes");
    if (argb)
    {
        expectHeader(*argb, { 0x20534444, 124, 0x100F, 1, 2, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 0x1000, 0, 0, 0, 0 });
        expect(argb->size() == 136, "single ARGB total size");
    }
    const auto dxt = EncodeXboxDds(levels(8, 4, 4), { Format::Dxt1 });
    expect(dxt.has_value(), "multi-mip DXT1 encodes");
    if (dxt)
    {
        expectHeader(*dxt, { 0x20534444, 124, 0xA1007, 4, 8, 16, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 4, 0x31545844, 0, 0, 0, 0, 0, 0x401008, 0, 0, 0, 0 });
        expect(dxt->size() == 168, "DXT1 chain includes sub-block tail levels");
    }

    std::vector<std::byte> legacy(136);
    for (const auto [offset, value] : std::array{
             std::pair{ 0U, 0x20534444U }, std::pair{ 4U, 124U }, std::pair{ 12U, 1U }, std::pair{ 16U, 2U }, std::pair{ 76U, 32U }, std::pair{ 80U, 0x41U }, std::pair{ 88U, 32U }, std::pair{ 92U, 0x000000FFU }, std::pair{ 96U, 0x0000FF00U }, std::pair{ 100U, 0x00FF0000U }, std::pair{ 104U, 0xFF000000U }, std::pair{ 128U, 0x04030201U }, std::pair{ 132U, 0x08070605U } })
    {
        writeU32Le(legacy, offset, value);
    }
    const auto                   original = legacy;
    const std::vector<std::byte> expected{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 }, std::byte{ 5 }, std::byte{ 6 }, std::byte{ 7 }, std::byte{ 8 } };
    const auto                   decoded = ParseDdsDocument(legacy);
    expect(decoded && decoded->rgba8 == expected && decoded->mipLevels.size() == 1 && decoded->mipLevels[0].rgba8 == expected, "legacy ARGB top-level result unchanged with zero mip count");
    expect(legacy == original, "parser preserves input bytes");
    writeU32Le(legacy, 80, 0x40);
    writeU32Le(legacy, 104, 0);
    auto opaque = expected;
    opaque[3] = opaque[7] = std::byte{ 255 };
    expect(ParseDdsDocument(legacy)->rgba8 == opaque, "legacy RGB remains opaque");
    writeU32Le(legacy, 28, 1);
    expect(ParseDdsDocument(legacy)->rgba8 == opaque, "explicit one mip equals implicit one mip");
}

void testRoundTrips()
{
    for (const auto format : kFormats)
    {
        for (const auto layout : { Layout::LinearLittleEndian, Layout::XboxTiledBigEndian })
        {
            for (const bool wide : { false, true })
            {
                const auto source  = layout == Layout::LinearLittleEndian ? levels(wide ? 7U : 8U, wide ? 3U : 4U, wide ? 3 : 4) : levels(wide ? 128U : 64U, wide ? 64U : 128U, 2);
                const auto encoded = EncodeXboxDds(source, { format, layout });
                expect(encoded.has_value(), "encode explicit levels in both layouts");
                if (!encoded)
                {
                    continue;
                }
                const auto parsed = ParseDdsDocument(*encoded, layout);
                expect(parsed && !parsed->mipIssue && parsed->mipLevels.size() == source.size() && parsed->payloadLayout == layout, "decode every declared level without a mip issue");
                if (!parsed || parsed->mipLevels.size() != source.size())
                {
                    continue;
                }
                for (std::size_t mip = 0; mip < source.size(); ++mip)
                {
                    const auto  blocks    = EncodeBlocks(source[mip], format);
                    const auto  reference = DecodeBlocks(*blocks, source[mip].width, source[mip].height, format);
                    const auto& actual    = parsed->mipLevels[mip];
                    expect(actual.width == source[mip].width && actual.height == source[mip].height && actual.rgba8 == reference->rgba8, "mip pixels equal codec round trip");
                    if (format == Format::Argb8)
                    {
                        expect(actual.rgba8 == source[mip].rgba8, "ARGB pixels are exact");
                    }
                    const auto truncated = std::span<const std::byte>(*encoded).first(actual.byteOffset + actual.byteSize - 1);
                    const auto partial   = ParseDdsDocument(truncated, layout);
                    if (mip == 0)
                    {
                        expectPreviewError(partial, DdsPreviewError::TruncatedPixelData, "truncated level zero is fatal");
                    }
                    else
                    {
                        expect(partial && partial->mipIssue == DdsPreviewError::TruncatedPixelData && partial->mipLevels.size() == mip && partial->rgba8 == parsed->rgba8, "later truncation preserves every complete level and top-level pixels");
                    }
                }
                expect(parsed->rgba8 == parsed->mipLevels.front().rgba8, "compatibility pixels equal level zero");
            }
        }
    }
    const auto source = levels(64, 64, 1);
    const auto linear = *EncodeXboxDds(source, { Format::Dxt1, Layout::LinearLittleEndian });
    const auto tiled  = *EncodeXboxDds(source, { Format::Dxt1, Layout::XboxTiledBigEndian });
    expect(!std::ranges::equal(std::span(linear).subspan(128), std::span(tiled).subspan(128)), "64x64 DXT1 layouts have different payloads");
    expect(ParseDdsDocument(linear)->rgba8 == ParseDdsDocument(tiled, Layout::XboxTiledBigEndian)->rgba8, "64x64 layouts decode identically");
    expect(ParseDdsDocument(tiled)->payloadLayout == Layout::LinearLittleEndian && ParseDdsDocument(tiled)->rgba8 != ParseDdsDocument(tiled, Layout::XboxTiledBigEndian)->rgba8, "layout is explicit and never auto-detected");
}

void testRanges()
{
    const auto source = levels(256, 128, 4);
    const auto bytes  = *EncodeXboxDds(source, { Format::Dxt5 });
    const auto parsed = ParseDdsDocument(bytes);
    expect(parsed && parsed->mipLevels.size() == 4 && !parsed->mipIssue && bytes.size() == 43648, "256x128 DXT5 four-level file extent");
    if (!parsed || parsed->mipLevels.size() != 4)
    {
        return;
    }
    // DXT5 uses 16 bytes per 4x4 block: (64*32, 32*16, 16*8, 8*4)*16.
    // Start at 128 and cumulatively add 32768, 8192, 2048, 512 bytes.
    constexpr std::array<std::size_t, 4> offsets{ 128, 32896, 41088, 43136 };
    constexpr std::array<std::size_t, 4> sizes{ 32768, 8192, 2048, 512 };
    for (std::size_t mip = 0; mip < 4; ++mip)
    {
        const auto& level = parsed->mipLevels[mip];
        expect(level.byteOffset == offsets[mip] && level.byteSize == sizes[mip], "exact absolute linear mip byte range");
        const auto encoded = *EncodeBlocks(source[mip], Format::Dxt5);
        expect(std::ranges::equal(std::span(bytes).subspan(offsets[mip], sizes[mip]), encoded), "range contains exactly this level's blocks");
    }
    const auto tiled    = *EncodeXboxDds(std::span(source).first(3), { Format::Dxt5, Layout::XboxTiledBigEndian });
    const auto document = ParseDdsDocument(tiled, Layout::XboxTiledBigEndian);
    expect(document && document->mipLevels.size() == 3 && !document->mipIssue && tiled.size() == 65664, "tiled payload ends after last declared region");
    if (!document || document->mipLevels.size() != 3)
    {
        return;
    }
    // Round each DXT5 block grid (64x32, 32x16, 16x8) up to 32x32 tiles.
    // Each tile is 32*32*16=16384 bytes: sizes 32768,16384,16384. Offsets are prefix sums +128.
    constexpr std::array<std::size_t, 3> tiledOffsets{ 128, 32896, 49280 };
    constexpr std::array<std::size_t, 3> tiledSizes{ 32768, 16384, 16384 };
    for (std::size_t mip = 0; mip < 3; ++mip)
    {
        expect(document->mipLevels[mip].byteOffset == tiledOffsets[mip] && document->mipLevels[mip].byteSize == tiledSizes[mip], "hand-derived tiled offsets and padded sizes");
    }

    // Inject a level-1 size ending at 65665, one byte past EOF. Level 2 still ends at 65664.
    // DDS has no interior length markers: malformed descriptor injection isolates each range check.
    const auto interiorShort = detail::ParseDdsDocumentWithMipDescriber(tiled, Layout::XboxTiledBigEndian, [](std::uint32_t, std::uint32_t, Format) -> std::expected<XboxMipChain, XboxTextureError>
                                                                        {
                                                                            return XboxMipChain{ { { 256, 128, 32768, 32768, 0, {} }, { 128, 64, 8192, 32769, 32768, {} }, { 64, 32, 2048, 16384, 49152, {} } }, 3, 65536 };
                                                                        });
    expect(interiorShort && interiorShort->mipIssue == DdsPreviewError::TruncatedPixelData && interiorShort->mipLevels.size() == 1 && interiorShort->rgba8 == document->rgba8, "interior short level is caught even when the final level fits");
}

void testHandTiledTexel()
{
    const auto             header = argbHeader(64, 64);
    std::vector<std::byte> fixture(128 + 16384);
    std::ranges::copy(header, fixture.begin());
    // Xenia 95a5c3ee250f80c3b9d139658649d9ffb6db3eec, src/xenia/gpu/texture_address.h, Tiled2D.
    // Texel (8,2), pitch=2, bpp=4: outer=0, inner=8, oib=32, bank=0, pipe=1 => 64|256=320.
    constexpr std::array expectedBytes{ std::byte{ 0x78 }, std::byte{ 0x12 }, std::byte{ 0x34 }, std::byte{ 0x56 } };
    std::ranges::copy(expectedBytes, fixture.begin() + 448);
    auto source = image(64, 64);
    std::ranges::fill(source.rgba8, std::byte{});
    constexpr std::size_t pixelOffset = (2 * 64 + 8) * 4;
    constexpr std::array  expectedPixel{ std::byte{ 0x12 }, std::byte{ 0x34 }, std::byte{ 0x56 }, std::byte{ 0x78 } };
    std::ranges::copy(expectedPixel, source.rgba8.begin() + pixelOffset);
    const auto encoded = EncodeXboxDds(std::span(&source, 1), { Format::Argb8, Layout::XboxTiledBigEndian });
    expect(encoded && encoded->size() == fixture.size() && std::ranges::equal(std::span(*encoded).subspan(128), std::span(fixture).subspan(128)), "tiled writer payload matches a hand-placed texel and zero padding");
    const auto parsed = ParseDdsDocument(fixture, Layout::XboxTiledBigEndian);
    expect(parsed && parsed->rgba8 == source.rgba8, "parser decodes hand-placed tiled texel independently of writer");
}

void testLegacyMipTolerance()
{
    std::vector<std::byte> expected(16384);
    for (std::size_t i = 0; i < expected.size(); i += 4)
    {
        expected[i]     = std::byte{ 0x40 };
        expected[i + 1] = std::byte{ 0x20 };
        expected[i + 2] = std::byte{ 0x10 };
        expected[i + 3] = std::byte{ 0x80 };
    }
    for (const auto count : { 2U, 8U })
    {
        for (const bool flag : { false, true })
        {
            const auto             header = argbHeader(64, 64, count, flag);
            std::vector<std::byte> bytes(128 + 16384);
            std::ranges::copy(header, bytes.begin());
            for (std::size_t offset = 128; offset < bytes.size(); offset += 4)
            {
                writeU32Le(bytes, offset, 0x80402010U);
            }
            const auto                     parsed = ParseDdsDocument(bytes);
            std::optional<DdsPreviewError> issue;
            if (flag)
            {
                issue = count == 8 ? DdsPreviewError::InvalidMipCount : DdsPreviewError::TruncatedPixelData;
            }
            expect(parsed && parsed->rgba8 == expected && parsed->mipLevels.size() == 1 && parsed->mipLevels[0].rgba8 == expected && parsed->mipIssue == issue, "64x64 legacy count 2/8 with/without flag preserves exact HEAD pixels");
        }
    }
    for (const auto format : kFormats)
    {
        for (const auto layout : { Layout::LinearLittleEndian, Layout::XboxTiledBigEndian })
        {
            auto       bytes    = *EncodeXboxDds(levels(64, 64, 2), { format, layout });
            const auto original = ParseDdsDocument(bytes, layout);
            writeU32Le(bytes, 8, 0x100FU);
            expect(ParseDdsDocument(bytes, layout)->mipLevels.size() == 1, "absent mip flag ignores stored later levels in every format/layout");
            writeU32Le(bytes, 8, 0x2100FU);
            writeU32Le(bytes, 28, 8);
            const auto excessive = ParseDdsDocument(bytes, layout);
            expect(excessive && excessive->mipIssue == DdsPreviewError::InvalidMipCount && excessive->mipLevels.size() == 2 && excessive->rgba8 == original->rgba8, "excessive count retains complete levels for every format/layout");
        }
    }
    auto complete = *EncodeXboxDds(levels(64, 64, 7), {});
    writeU32Le(complete, 28, 8);
    const auto excessive = ParseDdsDocument(complete);
    expect(excessive && excessive->mipIssue == DdsPreviewError::InvalidMipCount && excessive->mipLevels.size() == 7, "excessive count retains a complete chain through 1x1");
}

void testPixelLimit()
{
    for (const auto format : kFormats)
    {
        for (const auto layout : { Layout::LinearLittleEndian, Layout::XboxTiledBigEndian })
        {
            for (const bool over : { false, true })
            {
                auto header = over ? argbHeader(67108865, 1) : argbHeader(8192, 8192);
                if (format != Format::Argb8)
                {
                    writeU32Le(header, 80, 4);
                    writeU32Le(header, 84, format == Format::Dxt1 ? 0x31545844U : format == Format::Dxt3 ? 0x33545844U
                                                                                                         : 0x35545844U);
                }
                expectPreviewError(ParseDdsDocument(header, layout), over ? DdsPreviewError::PixelLimitExceeded : DdsPreviewError::TruncatedPixelData, "every format accepts the exact 64 Mpx budget and rejects limit plus one before payload allocation");
            }
            expectPreviewError(ParseDdsDocument(argbHeader(8192, 8192, 2, true), layout), DdsPreviewError::TruncatedPixelData, "missing flagged levels do not consume the decoded pixel budget");
            expectPreviewError(ParseDdsDocument(argbHeader(8192, 8192, 2, false), layout), DdsPreviewError::TruncatedPixelData, "unflagged count does not contribute to pixel budget");
        }
    }
    // 44739243+22369621=67108864; 26843546*2+13421773=67108865 after dimension halving.
    const auto exact = *ParseDdsMetadata(argbHeader(44739243, 1, 2, true));
    const auto over  = *ParseDdsMetadata(argbHeader(26843546, 2, 2, true));
    expect(detail::ValidateDdsPixelBudget(exact, 2).has_value(), "two complete levels at the exact pixel limit pass the budget");
    const auto rejected = detail::ValidateDdsPixelBudget(over, 2);
    expect(!rejected && rejected.error() == DdsPreviewError::PixelLimitExceeded, "two complete levels at limit plus one reject without payload allocation");
    expect(detail::ValidateDdsPixelBudget(exact, 1).has_value(), "a missing later level does not consume pixel budget");
}

void testMipGenerator()
{
    auto small = image(2, 2);
    for (unsigned i = 0; i < 4; ++i)
    {
        for (unsigned c = 0; c < 4; ++c)
        {
            small.rgba8[i * 4 + c] = static_cast<std::byte>(i + c);
        }
    }
    const auto pair = GenerateXboxMipLevels(small, 2);
    expect(pair && pair->size() == 2 && (*pair)[0].rgba8 == small.rgba8 && (*pair)[1].rgba8 == std::vector<std::byte>{ std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 }, std::byte{ 5 } }, "2x2 averages round exact halves up in every channel");
    auto large = image(4, 4);
    for (unsigned i = 0; i < 16; ++i)
    {
        large.rgba8[i * 4]     = static_cast<std::byte>(i);
        large.rgba8[i * 4 + 1] = static_cast<std::byte>(2 * i);
        large.rgba8[i * 4 + 2] = static_cast<std::byte>(3 * i);
        large.rgba8[i * 4 + 3] = static_cast<std::byte>(255 - i);
    }
    const auto                     mips = GenerateXboxMipLevels(large, 3);
    const std::array<unsigned, 16> expected{ 3, 5, 8, 253, 5, 9, 14, 251, 11, 21, 32, 245, 13, 25, 38, 243 };
    expect(mips && mips->size() == 3, "4x4 generator produces requested chain");
    if (mips && mips->size() == 3)
    {
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            expect((*mips)[1].rgba8[i] == static_cast<std::byte>(expected[i]), "hand-pinned 4x4 to 2x2 channel average");
        }
        expect((*mips)[2].rgba8 == std::vector<std::byte>{ std::byte{ 8 }, std::byte{ 15 }, std::byte{ 23 }, std::byte{ 248 } }, "hand-pinned 2x2 to 1x1 channel average");
    }
    auto narrow     = image(1, 2);
    narrow.rgba8    = { std::byte{ 0 }, std::byte{ 2 }, std::byte{ 4 }, std::byte{ 6 }, std::byte{ 1 }, std::byte{ 3 }, std::byte{ 5 }, std::byte{ 7 } };
    const auto last = GenerateXboxMipLevels(narrow, 2);
    expect(last && last->back().rgba8 == std::vector<std::byte>{ std::byte{ 1 }, std::byte{ 3 }, std::byte{ 5 }, std::byte{ 7 } }, "one-pixel axis replicates its edge");
}

void testErrorsAndImport()
{
    expectError(EncodeXboxDds({}, {}), Error::NoLevels, "no levels");
    const std::array<Rgba8Image, 1> zero{};
    expectError(EncodeXboxDds(zero, {}), Error::LevelDimensionMismatch, "writer zero-dimension level zero");
    auto wrong     = levels(8, 4, 2);
    wrong[1].width = 3;
    expectError(EncodeXboxDds(wrong, {}), Error::LevelDimensionMismatch, "mismatched level dimensions");
    wrong = levels(8, 4, 1);
    wrong[0].rgba8.pop_back();
    expectError(EncodeXboxDds(wrong, {}), Error::LevelDimensionMismatch, "mismatched RGBA size");
    expectError(EncodeXboxDds(levels(1, 1, 2), {}), Error::LevelDimensionMismatch, "mip chain cannot continue after 1x1");
    expectError(EncodeXboxDds(levels(7, 3, 1), { Format::Dxt1, Layout::XboxTiledBigEndian }), Error::LayoutRequiresPowerOfTwo, "tiled dimensions require powers of two");
    expectError(EncodeXboxDds(levels(4, 4, 1), { static_cast<Format>(-1) }), Error::UnsupportedFormat, "unsupported codec format");
    expectError(EncodeXboxDds(levels(4, 4, 1), { Format::Argb8, static_cast<Layout>(-1) }), Error::UnsupportedFormat, "unsupported layout");
    expectError(EncodeXboxDds(levels(64, 64, 3), { Format::Dxt1, Layout::XboxTiledBigEndian }), Error::PackedMipTailUnsupported, "chain reaching packed tail is refused");
    expectError(EncodeXboxDds(levels(16, 16, 1), { Format::Argb8, Layout::XboxTiledBigEndian }), Error::PackedMipTailUnsupported, "packed base level is refused");
    Rgba8Image huge;
    huge.width  = 0x20000001U;
    huge.height = 0xFFFFFFFFU;
    expectError(EncodeXboxDds(std::span(&huge, 1), {}), Error::SizeOverflow, "RGBA size overflow");
    // 16384 x 8192 is 2^27 pixels, twice the 2^26 read-back budget. Rejecting it needs no pixel storage.
    Rgba8Image overBudget;
    overBudget.width  = 16384;
    overBudget.height = 8192;
    expectError(EncodeXboxDds(std::span(&overBudget, 1), {}), Error::PixelLimitExceeded, "encoder rejects a chain its read-back budget would reject");
    expectError(GenerateXboxMipLevels(huge, 1), Error::SizeOverflow, "generator size overflow");
    huge.width  = 0x40000000U;
    huge.height = 1;
    expectError(EncodeXboxDds(std::span(&huge, 1), {}), Error::SizeOverflow, "ARGB pitch 4294967296 exceeds UINT32_MAX before checking pixels");
    huge.width = huge.height = 65536;
    expectError(EncodeXboxDds(std::span(&huge, 1), { Format::Dxt5 }), Error::SizeOverflow, "DXT5 linear size 4294967296 exceeds UINT32_MAX before checking pixels");
    expectError(GenerateXboxMipLevels(image(4, 4), 0), Error::NoLevels, "generator zero count");
    expectError(GenerateXboxMipLevels(image(4, 4), 4), Error::LevelDimensionMismatch, "generator excessive count");
    expectError(GenerateXboxMipLevels(image(7, 4), 1), Error::LayoutRequiresPowerOfTwo, "generator requires power-of-two source");
    expectError(GenerateXboxMipLevels({}, 1), Error::LevelDimensionMismatch, "generator zero dimensions");

    const auto valid     = *EncodeXboxDds(levels(64, 64, 1), {});
    auto       malformed = valid;
    writeU32Le(malformed, 16, 0);
    expectPreviewError(ParseDdsDocument(malformed), DdsPreviewError::InvalidDimensions, "zero parser dimensions");
    malformed = valid;
    writeU32Le(malformed, 16, 63);
    expectPreviewError(ParseDdsDocument(malformed, Layout::XboxTiledBigEndian), DdsPreviewError::LayoutRequiresPowerOfTwo, "parser tiled non-power-of-two");
    malformed = valid;
    writeU32Le(malformed, 8, 0x2100FU);
    writeU32Le(malformed, 28, 3);
    const auto packed = ParseDdsDocument(malformed, Layout::XboxTiledBigEndian);
    expect(packed && packed->mipIssue == DdsPreviewError::PackedMipTailUnsupported && packed->mipLevels.size() == 1, "parser retains base level before unsupported packed tail");
    expectPreviewError(ParseDdsDocument(argbHeader(16, 16), Layout::XboxTiledBigEndian), DdsPreviewError::PackedMipTailUnsupported, "packed base level remains unsupported");
    malformed = valid;
    writeU32Le(malformed, 16, 0x80000000U);
    writeU32Le(malformed, 12, 0x80000000U);
    expectPreviewError(ParseDdsDocument(malformed), DdsPreviewError::SizeOverflow, "an unrepresentable RGBA size keeps the legacy overflow error");
    expectPreviewError(detail::ParseDdsDocumentWithMipDescriber(argbHeader(64, 64), Layout::XboxTiledBigEndian, [](std::uint32_t, std::uint32_t, Format) -> std::expected<XboxMipChain, XboxTextureError>
                                                                {
                                                                    return std::unexpected(XboxTextureError::SizeOverflow);
                                                                }),
                       DdsPreviewError::SizeOverflow,
                       "parser maps a failed DescribeMipChain without decoding pixels");
    expectPreviewError(ParseDdsDocument(std::span(valid).first(128)), DdsPreviewError::TruncatedPixelData, "parser truncated top level");
    for (const auto [offset, value] : std::array{ std::pair{ 80U, 0x44U }, std::pair{ 112U, 0x200U }, std::pair{ 112U, 0x200000U }, std::pair{ 88U, 24U } })
    {
        malformed = valid;
        writeU32Le(malformed, offset, value);
        expectPreviewError(ParseDdsDocument(malformed), DdsPreviewError::UnsupportedFormat, "unsupported parser flags, volume, cube or bit count");
    }
    malformed = valid;
    writeU32Le(malformed, 80, 4);
    writeU32Le(malformed, 84, 0x30315844);
    expectPreviewError(ParseDdsDocument(malformed), DdsPreviewError::UnsupportedFormat, "DX10 remains unsupported for preview");
    writeU32Le(malformed, 84, 0x32545844);
    expectPreviewError(ParseDdsDocument(malformed), DdsPreviewError::UnsupportedFormat, "DXT2 remains unsupported");
    expectPreviewError(ParseDdsDocument(valid, static_cast<Layout>(-1)), DdsPreviewError::UnsupportedFormat, "unknown layout is refused");

    const auto logo     = MainMenuLogoTextureContract();
    const auto dxt      = *EncodeXboxDds(levels(logo.width, logo.height, 1), { Format::Dxt1 });
    const auto imported = ImportTexture(dxt, TextureSourceKind::Dds);
    expect(imported && !imported->rgba8.empty(), "DXT source imports preview pixels");
    if (imported)
    {
        const auto issues = ValidateTexture(*imported, logo);
        expect(std::ranges::any_of(issues, [](const auto& issue)
                                   {
                                       return issue.kind == TextureValidationIssueKind::Encoding;
                                   }),
               "DXT still fails logo encoding validation");
        const auto refused = EncodeTextureDds(*imported, logo);
        expect(!refused && refused.error() == DdsWriteError::ContractMismatch, "legacy writer still refuses DXT source");
    }
    for (const auto error : { Error::NoLevels, Error::LevelDimensionMismatch, Error::LayoutRequiresPowerOfTwo, Error::UnsupportedFormat, Error::PackedMipTailUnsupported, Error::SizeOverflow, Error::DestinationExists, Error::OpenFailed, Error::WriteFailed, Error::FinalizationFailed, Error::ReadBackFailed })
    {
        expect(!XboxDdsWriteErrorMessage(error).empty(), "every writer error has a diagnostic");
    }
}

void testFileWriting(const std::filesystem::path& scratch)
{
    const auto      root = scratch / ("rerevved-tools-xbox-dds-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    const auto      created = std::filesystem::create_directory(root, error);
    expect(created && !error, "create fresh DDS test directory");
    if (!created || error)
    {
        return;
    }
    const auto destination = root / "texture.dds";
    const auto occupied    = root / "texture.dds.tmp-0";
    {
        std::ofstream output(occupied, std::ios::binary | std::ios::noreplace);
        output << "caller sentinel";
        expect(static_cast<bool>(output), "occupy first temporary sibling");
    }
    const auto        source = levels(128, 64, 2);
    const XboxDdsSpec spec{ Format::Dxt5, Layout::XboxTiledBigEndian };
    expect(WriteXboxDds(source, spec, destination).has_value(), "publish verified multi-mip tiled DXT5");
    const auto parsed = LoadDdsDocument(destination, spec.payloadLayout);
    expect(parsed && parsed->mipLevels.size() == 2 && parsed->mipLevels[1].rgba8 == DecodeBlocks(*EncodeBlocks(source[1], spec.format), 64, 32, spec.format)->rgba8, "disk parser verifies every mip");
    const auto existing = root / "existing.dds";
    {
        std::ofstream output(existing, std::ios::binary | std::ios::noreplace);
        output << "keep";
    }
    expectError(WriteXboxDds(source, spec, existing), Error::DestinationExists, "existing file refused");
    {
        std::ifstream     input(existing, std::ios::binary);
        const std::string sentinel{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        expect(sentinel == "keep", "existing bytes preserved");
    }
    {
        std::ifstream     input(occupied, std::ios::binary);
        const std::string sentinel{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        expect(sentinel == "caller sentinel", "occupied temporary is preserved");
    }
    expectError(WriteXboxDds(source, spec, root / "missing" / "texture.dds"), Error::OpenFailed, "missing parent fails open");
    expectError(WriteXboxDds({}, spec, root / "invalid.dds"), Error::NoLevels, "file writer validates before creating output");
    const auto proof       = root / "proof.dds";
    const auto failedProof = detail::WriteXboxDdsWithPublisher(source, {}, proof, [](std::span<const std::byte> bytes, const std::filesystem::path& destination, const std::function<bool(std::span<const std::byte>)>& verify)
                                                               {
                                                                   return detail::PublishDds(bytes, destination, [&](std::span<const std::byte> readBack)
                                                                                             {
                                                                                                 std::vector<std::byte> corrupted(readBack.begin(), readBack.end());
                                                                                                 corrupted[corrupted.size() - 4] ^= std::byte{ 1 };
                                                                                                 return verify(corrupted);
                                                                                             });
                                                               });
    expectError(failedProof, Error::ReadBackFailed, "Xbox writer maps a mismatched parser proof to ReadBackFailed");
    expect(!std::filesystem::exists(proof), "failed Xbox parser proof prevents publication");
    std::size_t entries = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(root))
    {
        ++entries;
    }
    expect(entries == 3, "only destination and caller-owned sentinels remain");
    std::filesystem::remove(destination, error);
    std::filesystem::remove(existing, error);
    std::filesystem::remove(occupied, error);
    expect(std::filesystem::remove(root, error) && !error, "remove exact owned test files and empty directory");
}

} // namespace

int main(int argc, char** argv)
{
    const auto scratch = argc == 3 && std::string_view(argv[1]) == "--scratch-root" ? std::filesystem::path(argv[2]) : std::filesystem::temp_directory_path();
    testHeadersAndLegacy();
    testLegacyMipTolerance();
    testPixelLimit();
    testRoundTrips();
    testRanges();
    testHandTiledTexel();
    testMipGenerator();
    testErrorsAndImport();
    testFileWriting(scratch);
    if (failures != 0)
    {
        std::cerr << failures << " Xbox DDS test(s) failed.\n";
        return 1;
    }
    std::cout << "Xbox DDS tests passed.\n";
    return 0;
}
