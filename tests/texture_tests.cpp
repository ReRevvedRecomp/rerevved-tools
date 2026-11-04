#include "dds_document.h"
#include "dds_writer.h"
#include "texture_import.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using namespace rerevved::tools;

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (condition)
    {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void appendU32Be(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void writeU32Le(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        bytes[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xFFU);
    }
}

std::uint32_t readU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        value |= std::to_integer<std::uint32_t>(bytes[offset + shift / 8]) << shift;
    }
    return value;
}

std::uint32_t crc32(std::span<const std::byte> bytes)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto value : bytes)
    {
        crc ^= std::to_integer<std::uint32_t>(value);
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

void appendChunk(std::vector<std::byte>& bytes, std::string_view type, std::span<const std::byte> payload)
{
    appendU32Be(bytes, static_cast<std::uint32_t>(payload.size()));
    const auto start = bytes.size();
    for (const auto ch : type)
    {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    appendU32Be(bytes, crc32(std::span<const std::byte>(bytes).subspan(start)));
}

std::vector<std::byte> storedDeflate(std::span<const std::byte> raw, bool badAdler)
{
    std::vector<std::byte> zlib{ std::byte{ 0x78 }, std::byte{ 0x01 } };
    std::size_t            offset = 0;
    do
    {
        const auto length = std::min<std::size_t>(65535, raw.size() - offset);
        zlib.push_back(static_cast<std::byte>(offset + length == raw.size() ? 1 : 0));
        const auto count   = static_cast<std::uint16_t>(length);
        const auto inverse = static_cast<std::uint16_t>(~count);
        zlib.push_back(static_cast<std::byte>(count & 0xFFU));
        zlib.push_back(static_cast<std::byte>(count >> 8U));
        zlib.push_back(static_cast<std::byte>(inverse & 0xFFU));
        zlib.push_back(static_cast<std::byte>(inverse >> 8U));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    } while (offset < raw.size());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const auto value : raw)
    {
        a = (a + std::to_integer<std::uint32_t>(value)) % 65521U;
        b = (b + a) % 65521U;
    }
    appendU32Be(zlib, ((b << 16U) | a) ^ (badAdler ? 1U : 0U));
    return zlib;
}

std::array<std::byte, 4> sample(std::uint32_t x, std::uint32_t y)
{
    return { static_cast<std::byte>(x & 0xFFU), static_cast<std::byte>(y & 0xFFU), static_cast<std::byte>((x + y) & 0xFFU), static_cast<std::byte>((x * 3 + y * 5) & 0xFFU) };
}

std::vector<std::byte> expectedPixels(std::uint32_t width, std::uint32_t height)
{
    std::vector<std::byte> pixels;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto pixel = sample(x, y);
            pixels.insert(pixels.end(), pixel.begin(), pixel.end());
        }
    }
    return pixels;
}

std::vector<std::byte> makePngHeader(std::uint32_t width, std::uint32_t height, std::uint8_t colorType = 6, std::uint8_t bitDepth = 8)
{
    std::vector<std::byte> bytes{
        std::byte{ 137 }, std::byte{ 80 }, std::byte{ 78 }, std::byte{ 71 }, std::byte{ 13 }, std::byte{ 10 }, std::byte{ 26 }, std::byte{ 10 }
    };
    std::vector<std::byte> header;
    appendU32Be(header, width);
    appendU32Be(header, height);
    header.insert(header.end(), { static_cast<std::byte>(bitDepth), static_cast<std::byte>(colorType), std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } });
    appendChunk(bytes, "IHDR", header);
    return bytes;
}

std::vector<std::byte> makePng(std::uint32_t width, std::uint32_t height, std::uint8_t colorType = 6, std::uint8_t bitDepth = 8, bool badAdler = false, bool badPaletteIndex = false)
{
    auto bytes = makePngHeader(width, height, colorType, bitDepth);
    if (colorType == 3)
    {
        const std::array palette{ std::byte{ 10 }, std::byte{ 20 }, std::byte{ 30 }, std::byte{ 200 }, std::byte{ 150 }, std::byte{ 100 } };
        const std::array alpha{ std::byte{ 0 }, std::byte{ 255 } };
        appendChunk(bytes, "PLTE", palette);
        appendChunk(bytes, "tRNS", alpha);
    }
    const auto             channels = colorType == 6 ? 4U : colorType == 2 ? 3U
                                                        : colorType == 4   ? 2U
                                                                           : 1U;
    std::vector<std::byte> raw;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        raw.push_back(std::byte{ 0 });
        if (colorType == 3)
        {
            const auto pixelsPerByte = 8U / bitDepth;
            for (std::uint32_t x = 0; x < width; x += pixelsPerByte)
            {
                unsigned packed = 0;
                for (unsigned index = 0; index < pixelsPerByte && x + index < width; ++index)
                {
                    packed |= ((x + index + y) & 1U) << (8U - bitDepth * (index + 1));
                }
                raw.push_back(static_cast<std::byte>(badPaletteIndex ? 0xFFU : packed));
            }
            continue;
        }
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto pixel = sample(x, y);
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                const auto value = pixel[channel];
                raw.push_back(value);
                if (bitDepth == 16)
                {
                    raw.push_back(value);
                }
            }
        }
    }
    appendChunk(bytes, "IDAT", storedDeflate(raw, badAdler));
    appendChunk(bytes, "IEND", {});
    return bytes;
}

void testRgbaRoundTrip()
{
    const auto png      = makePng(1024, 256);
    const auto original = png;
    const auto imported = ImportTexture(png, TextureSourceKind::Png);
    expect(imported.has_value(), "1024x256 RGBA PNG imports");
    expect(png == original, "PNG import leaves source bytes unchanged");
    if (!imported)
    {
        return;
    }
    const auto contract = MainMenuLogoTextureContract();
    expect(contract.key == "file-data/GFX_MainMenu_logo.dds" && contract.width == 1024 &&
               contract.height == 256 && contract.encoding == TextureEncoding::LegacyA8R8G8B8 &&
               contract.mipLevelCount == 1 && contract.requiresAlphaChannel,
           "main-menu logo contract is exact");
    expect(imported->rgba8 == expectedPixels(1024, 256), "RGBA PNG decoding preserves every channel byte");
    const auto* layout = std::get_if<PngSourceLayout>(&imported->sourceLayout);
    expect(layout && layout->bitDepth == 8 && layout->colorType == 6, "PNG source layout retains color type and bit depth");
    expect(ValidateTexture(*imported, contract).empty(), "RGBA PNG satisfies logo contract");
    const auto encoded = EncodeTextureDds(*imported, contract);
    expect(encoded.has_value(), "valid RGBA image encodes as legacy DDS");
    if (!encoded)
    {
        return;
    }
    expect(encoded->size() == 1048704, "encoded logo DDS equals the title's kLogoDdsSize");
    const auto parsed = ParseDdsDocument(*encoded);
    expect(parsed && parsed->rgba8 == imported->rgba8, "encoded DDS parser read-back is byte-exact");
    const auto ddsImported = ImportTexture(*encoded, TextureSourceKind::Dds);
    expect(ddsImported && ddsImported->rgba8 == imported->rgba8 &&
               ValidateTexture(*ddsImported, contract).empty(),
           "DDS input imports without pixel drift");
    if (ddsImported)
    {
        const auto* ddsLayout = std::get_if<DdsSourceLayout>(&ddsImported->sourceLayout);
        expect(ddsLayout && ddsLayout->metadata.pixelFormat.flags == 0x41 &&
                   ddsLayout->metadata.pixelFormat.rgbBitCount == 32 && ddsLayout->metadata.mipMapCount == 0,
               "DDS source layout retains format summary");
    }
    const auto wrongKind = ImportTexture(png, TextureSourceKind::Dds);
    expect(!wrongKind && wrongKind.error() == TextureImportFailure{ DdsDocumentError{ DdsMetadataError::InvalidSignature } } &&
               TextureImportErrorMessage(wrongKind.error()) == "The file does not have a valid DDS signature.",
           "explicit DDS kind does not accept PNG bytes");
    const auto otherWrongKind = ImportTexture(*encoded, TextureSourceKind::Png);
    expect(!otherWrongKind && otherWrongKind.error() == TextureImportFailure{ TextureImportError::InvalidPng },
           "explicit PNG kind does not accept DDS bytes");
}

void testRgbAndDimensions()
{
    const auto rgb = ImportTexture(makePng(1024, 256, 2), TextureSourceKind::Png);
    expect(rgb.has_value(), "RGB PNG imports with opaque expansion");
    if (rgb)
    {
        const auto issues = ValidateTexture(*rgb, MainMenuLogoTextureContract());
        expect(!rgb->hasAlphaChannel && rgb->rgba8[3] == std::byte{ 255 }, "RGB import retains absence of source alpha");
        expect(issues.size() == 1 && issues[0].kind == TextureValidationIssueKind::MissingAlphaChannel &&
                   issues[0].expected == 1 && issues[0].actual == 0,
               "RGB reports the exact missing-alpha mismatch");
        const auto encoded = EncodeTextureDds(*rgb, MainMenuLogoTextureContract());
        expect(!encoded && encoded.error() == DdsWriteError::ContractMismatch, "writer refuses missing source alpha");
    }
    const auto wrongSize = ImportTexture(makePng(7, 3, 2), TextureSourceKind::Png);
    expect(wrongSize.has_value(), "wrong-size RGB imports without resampling");
    if (!wrongSize)
    {
        return;
    }
    const auto issues = ValidateTexture(*wrongSize, MainMenuLogoTextureContract());
    expect(issues.size() == 3, "validation reports width, height, and alpha together");
    if (issues.size() == 3)
    {
        expect(issues[0].kind == TextureValidationIssueKind::Width && issues[0].expected == 1024 && issues[0].actual == 7,
               "width mismatch preserves expected and actual");
        expect(issues[1].kind == TextureValidationIssueKind::Height && issues[1].expected == 256 && issues[1].actual == 3,
               "height mismatch preserves expected and actual");
        expect(issues[2].kind == TextureValidationIssueKind::MissingAlphaChannel && issues[2].expected == 1 && issues[2].actual == 0,
               "missing alpha remains visible with dimension failures");
    }
    expect(wrongSize->rgba8.size() == 7 * 3 * 4, "wrong-size pixels are not cropped or padded");
}

void testPalettePng()
{
    const auto                   imported = ImportTexture(makePng(2, 1, 3), TextureSourceKind::Png);
    const std::vector<std::byte> expected{ std::byte{ 10 }, std::byte{ 20 }, std::byte{ 30 }, std::byte{ 0 }, std::byte{ 200 }, std::byte{ 150 }, std::byte{ 100 }, std::byte{ 255 } };
    expect(imported && imported->rgba8 == expected && imported->hasAlphaChannel,
           "palette PNG expands RGB and tRNS alpha exactly");
    if (imported)
    {
        const auto* layout = std::get_if<PngSourceLayout>(&imported->sourceLayout);
        expect(layout && layout->colorType == 3 && layout->bitDepth == 8, "palette source layout is retained");
    }
    for (const auto depth : { std::uint8_t{ 1 }, std::uint8_t{ 2 }, std::uint8_t{ 4 }, std::uint8_t{ 8 } })
    {
        const auto             packed = ImportTexture(makePng(9, 2, 3, depth), TextureSourceKind::Png);
        std::vector<std::byte> packedExpected;
        for (unsigned y = 0; y < 2; ++y)
        {
            for (unsigned x = 0; x < 9; ++x)
            {
                const auto start = ((x + y) & 1U) * 4;
                packedExpected.insert(packedExpected.end(), expected.begin() + start, expected.begin() + start + 4);
            }
        }
        expect(packed && packed->rgba8 == packedExpected, "all legal palette bit depths expand without row-padding pixels");
    }
    const auto badIndex = ImportTexture(makePng(2, 1, 3, 8, false, true), TextureSourceKind::Png);
    expect(!badIndex && badIndex.error() == TextureImportFailure{ TextureImportError::InvalidPng },
           "palette index outside PLTE is rejected before decoding");
}

void testUnsupportedPng()
{
    const auto sixteen = ImportTexture(makePng(2, 1, 6, 16), TextureSourceKind::Png);
    expect(!sixteen && sixteen.error() == TextureImportFailure{ TextureImportError::Png16BitUnsupported },
           "16-bit PNG has its exact rejection reason");
    expect(TextureImportErrorMessage(TextureImportError::Png16BitUnsupported) ==
               "16 bit PNG input is not supported. Use 8 bit color.",
           "16-bit PNG error message is exact");
    for (const auto colorType : { std::uint8_t{ 0 }, std::uint8_t{ 4 } })
    {
        const auto grayscale = ImportTexture(makePng(2, 1, colorType), TextureSourceKind::Png);
        expect(!grayscale && grayscale.error() == TextureImportFailure{ TextureImportError::PngGrayscaleUnsupported },
               "grayscale PNG with or without alpha has its exact rejection reason");
    }
    expect(TextureImportErrorMessage(TextureImportError::PngGrayscaleUnsupported) ==
               "Grayscale PNG input is not supported. Use truecolor or palette color.",
           "grayscale PNG error message is exact");
}

void testMalformedPng()
{
    const auto valid = makePng(2, 1);
    for (const auto length : { std::size_t{ 0 }, std::size_t{ 7 }, std::size_t{ 32 }, valid.size() - 1 })
    {
        const auto result = ImportTexture(std::span<const std::byte>(valid).first(length), TextureSourceKind::Png);
        expect(!result && result.error() == TextureImportFailure{ TextureImportError::InvalidPng }, "truncated PNG is rejected");
    }
    auto corrupt = valid;
    corrupt[29] ^= std::byte{ 1 };
    const auto crcFailure = ImportTexture(corrupt, TextureSourceKind::Png);
    expect(!crcFailure && crcFailure.error() == TextureImportFailure{ TextureImportError::InvalidPng }, "corrupt PNG CRC is rejected");
    const auto badAdler = ImportTexture(makePng(2, 1, 6, 8, true), TextureSourceKind::Png);
    expect(!badAdler && badAdler.error() == TextureImportFailure{ TextureImportError::InvalidPng },
           "corrupt zlib Adler-32 with valid PNG CRC is rejected");
    auto trailing = valid;
    trailing.push_back(std::byte{ 0 });
    const auto trailingResult = ImportTexture(trailing, TextureSourceKind::Png);
    expect(!trailingResult && trailingResult.error() == TextureImportFailure{ TextureImportError::InvalidPng }, "PNG trailing bytes are rejected");
    expect(TextureImportErrorMessage(TextureImportError::InvalidPng) == "The PNG data is malformed or truncated.",
           "malformed PNG message is exact");
}

void testPngDecodeRejections()
{
    const auto color = ImportTexture(makePngHeader(1, 1, 5), TextureSourceKind::Png);
    expect(!color && color.error() == TextureImportFailure{ TextureImportError::PngColorTypeUnsupported },
           "unsupported PNG color layout keeps its distinct rejection");
    const auto depth = ImportTexture(makePngHeader(1, 1, 2, 4), TextureSourceKind::Png);
    expect(!depth && depth.error() == TextureImportFailure{ TextureImportError::PngBitDepthUnsupported },
           "unsupported truecolor PNG bit depth keeps its distinct rejection");
    for (const auto& raw : { std::vector<std::byte>(4), std::vector<std::byte>(6), std::vector<std::byte>{ std::byte{ 5 }, std::byte{}, std::byte{}, std::byte{}, std::byte{} } })
    {
        auto png = makePngHeader(1, 1);
        appendChunk(png, "IDAT", storedDeflate(raw, false));
        appendChunk(png, "IEND", {});
        const auto result = ImportTexture(png, TextureSourceKind::Png);
        expect(!result && result.error() == TextureImportFailure{ TextureImportError::InvalidPng },
               "CRC-valid PNG with wrong scanline length or invalid filter is rejected");
    }
    auto png = makePngHeader(1, 1);
    appendChunk(png, "ABCD", {});
    const auto critical = ImportTexture(png, TextureSourceKind::Png);
    expect(!critical && critical.error() == TextureImportFailure{ TextureImportError::InvalidPng },
           "unknown critical PNG chunk is rejected");
}

void testPngSizeLimits()
{
    // Stay below the pixel cap so an impossible expansion exercises the ratio guard.
    auto                           tiny = makePngHeader(4096, 4096);
    const std::array<std::byte, 5> onePixel{};
    appendChunk(tiny, "IDAT", storedDeflate(onePixel, false));
    appendChunk(tiny, "IEND", {});
    expect(tiny.size() < 100, "oversized scanline fixture has only a tiny CRC-valid PNG stream");
    const auto impossible = ImportTexture(tiny, TextureSourceKind::Png);
    expect(!impossible && impossible.error() == TextureImportFailure{ TextureImportError::InvalidPng },
           "impossible deflate expansion is rejected below the PNG pixel cap");

    // No IDAT or IEND: the cap must reject from IHDR before decoding or stream validation.
    const auto overLimit = ImportTexture(makePngHeader(67108865, 1), TextureSourceKind::Png);
    expect(!overLimit && overLimit.error() == TextureImportFailure{ TextureImportError::PngPixelLimitExceeded },
           "PNG header one pixel over 64 Mpx has a distinct size rejection");
    const auto atLimit = ImportTexture(makePngHeader(8192, 8192), TextureSourceKind::Png);
    expect(!atLimit && atLimit.error() == TextureImportFailure{ TextureImportError::InvalidPng },
           "PNG header at 64 Mpx passes the pixel cap and fails missing stream validation");
    const auto huge = ImportTexture(makePngHeader(40000, 10000), TextureSourceKind::Png);
    expect(!huge && huge.error() == TextureImportFailure{ TextureImportError::PngPixelLimitExceeded },
           "40000x10000 PNG dimensions are rejected from IHDR alone");
    expect(TextureImportErrorMessage(TextureImportError::PngPixelLimitExceeded) ==
               "PNG input exceeds the limit of 67,108,864 pixels (64 Mpx).",
           "PNG pixel cap message states the exact limit");
    expect(TextureImportErrorMessage(TextureImportError::AllocationFailed) ==
               "There is not enough memory to import the texture.",
           "import allocation failure message is exact");
}

Rgba8Image makeLogoImage()
{
    return { .width = 1024, .height = 256, .hasAlphaChannel = true, .sourceLayout = PngSourceLayout{ 6, 8 }, .rgba8 = expectedPixels(1024, 256) };
}

void testDdsHeaderPins()
{
    const auto image   = makeLogoImage();
    const auto encoded = EncodeTextureDds(image, MainMenuLogoTextureContract());
    expect(encoded.has_value(), "header fixture encodes");
    if (!encoded)
    {
        return;
    }
    expect(encoded->size() == 1048704, "DDS total byte size equals logo validator requirement");
    expect((*encoded)[0] == std::byte{ 'D' } && (*encoded)[1] == std::byte{ 'D' } &&
               (*encoded)[2] == std::byte{ 'S' } && (*encoded)[3] == std::byte{ ' ' },
           "DDS magic equals DDS-space");
    const std::array<std::pair<std::size_t, std::uint32_t>, 18> pins{ { { 4, 124 }, { 8, 0x100F }, { 12, 256 }, { 16, 1024 }, { 20, 4096 }, { 24, 0 }, { 28, 0 }, { 76, 32 }, { 80, 0x41 }, { 84, 0 }, { 88, 32 }, { 92, 0x00FF0000 }, { 96, 0x0000FF00 }, { 100, 0x000000FF }, { 104, 0xFF000000 }, { 108, 0x1000 }, { 112, 0 }, { 124, 0 } } };
    for (const auto& [offset, value] : pins)
    {
        expect(readU32Le(*encoded, offset) == value, "DDS header field equals pin at byte " + std::to_string(offset));
    }
    for (std::size_t offset = 0; offset < image.rgba8.size(); offset += 4)
    {
        if ((*encoded)[128 + offset] != image.rgba8[offset + 2] ||
            (*encoded)[128 + offset + 1] != image.rgba8[offset + 1] ||
            (*encoded)[128 + offset + 2] != image.rgba8[offset] ||
            (*encoded)[128 + offset + 3] != image.rgba8[offset + 3])
        {
            expect(false, "DDS payload is BGRA for every pixel");
            break;
        }
    }
}

void testDdsInputLimits()
{
    auto encoded = EncodeTextureDds(makeLogoImage(), MainMenuLogoTextureContract());
    expect(encoded.has_value(), "DDS input fixture encodes");
    if (!encoded)
    {
        return;
    }
    writeU32Le(*encoded, 28, 2);
    writeU32Le(*encoded, 16, 7);
    writeU32Le(*encoded, 12, 3);
    const auto mipmapped = ImportTexture(*encoded, TextureSourceKind::Dds);
    expect(mipmapped.has_value(), "wrong-size mipmapped DDS retains an import result");
    if (mipmapped)
    {
        const auto* layout = std::get_if<DdsSourceLayout>(&mipmapped->sourceLayout);
        expect(layout && layout->metadata.width == 7 && layout->metadata.height == 3 &&
                   layout->metadata.mipMapCount == 2 && !layout->decodeError,
               "mipmapped DDS retains exact dimensions and mip count");
        const auto issues = ValidateTexture(*mipmapped, MainMenuLogoTextureContract());
        expect(issues.size() == 3, "one validation result reports width, height, and mip mismatches");
        if (issues.size() == 3)
        {
            expect(issues[0].kind == TextureValidationIssueKind::Width && issues[0].expected == 1024 && issues[0].actual == 7,
                   "mipmapped width mismatch retains expected and actual");
            expect(issues[1].kind == TextureValidationIssueKind::Height && issues[1].expected == 256 && issues[1].actual == 3,
                   "mipmapped height mismatch retains expected and actual");
            expect(issues[2].kind == TextureValidationIssueKind::MipLevelCount && issues[2].expected == 1 && issues[2].actual == 2,
                   "mip mismatch retains expected and actual");
        }
        const auto refused = EncodeTextureDds(*mipmapped, MainMenuLogoTextureContract());
        expect(!refused && refused.error() == DdsWriteError::ContractMismatch, "writer refuses mipmapped input");
    }
    writeU32Le(*encoded, 80, 4);
    const auto unsupported  = ImportTexture(*encoded, TextureSourceKind::Dds);
    const auto parserResult = ParseDdsDocument(*encoded);
    expect(unsupported.has_value(), "undecodable DDS retains an import result");
    if (unsupported)
    {
        const auto* layout = std::get_if<DdsSourceLayout>(&unsupported->sourceLayout);
        expect(layout && layout->metadata.width == 7 && layout->metadata.height == 3 &&
                   layout->metadata.mipMapCount == 2 && layout->metadata.pixelFormat.flags == 4 &&
                   layout->metadata.pixelFormat.rgbBitCount == 32 && layout->decodeError &&
                   !parserResult && *layout->decodeError == parserResult.error() &&
                   DdsDocumentErrorMessage(*layout->decodeError) == "This DDS encoding is not supported for preview.",
               "undecodable DDS retains metadata and the exact parser error and message");
        expect(unsupported->rgba8.empty(), "undecodable DDS has no pixels");
        const auto issues = ValidateTexture(*unsupported, MainMenuLogoTextureContract());
        expect(std::ranges::any_of(issues, [](const auto& issue)
                                   {
                                       return issue.kind == TextureValidationIssueKind::Width;
                                   }) &&
                   std::ranges::any_of(issues, [](const auto& issue)
                                       {
                                           return issue.kind == TextureValidationIssueKind::Height;
                                       }) &&
                   std::ranges::any_of(issues, [](const auto& issue)
                                       {
                                           return issue.kind == TextureValidationIssueKind::MipLevelCount;
                                       }) &&
                   std::ranges::any_of(issues, [](const auto& issue)
                                       {
                                           return issue.kind == TextureValidationIssueKind::Encoding &&
                                                  issue.expected == static_cast<std::uint64_t>(TextureEncoding::LegacyA8R8G8B8) &&
                                                  issue.actual == static_cast<std::uint64_t>(TextureEncoding::Unsupported);
                                       }),
               "undecodable DDS reports dimensions, mip count, and encoding together");
        const auto refused = EncodeTextureDds(*unsupported, MainMenuLogoTextureContract());
        expect(!refused && refused.error() == DdsWriteError::ContractMismatch, "writer refuses undecodable input");
    }
    const auto truncated = ImportTexture(std::span<const std::byte>(*encoded).first(80), TextureSourceKind::Dds);
    expect(!truncated && truncated.error() == TextureImportFailure{ DdsDocumentError{ DdsMetadataError::Truncated } } &&
               TextureImportErrorMessage(truncated.error()) == "DDS metadata is truncated.",
           "truncated DDS retains the exact metadata error and message");

    writeU32Le(*encoded, 80, 0x41);
    writeU32Le(*encoded, 16, 1024);
    writeU32Le(*encoded, 12, 256);
    writeU32Le(*encoded, 28, 0);
    const auto missingPixels = ImportTexture(std::span<const std::byte>(*encoded).first(128), TextureSourceKind::Dds);
    expect(missingPixels.has_value(), "DDS with truncated pixels retains its parsed header");
    if (missingPixels)
    {
        const auto* layout = std::get_if<DdsSourceLayout>(&missingPixels->sourceLayout);
        expect(layout && layout->metadata.width == 1024 && layout->metadata.height == 256 &&
                   layout->decodeError && *layout->decodeError == DdsDocumentError{ DdsPreviewError::TruncatedPixelData } &&
                   DdsDocumentErrorMessage(*layout->decodeError) == "The DDS top level pixel data is truncated." &&
                   missingPixels->rgba8.empty(),
               "truncated pixels preserve metadata and the distinct exact decoder diagnostic");
        const auto refused = EncodeTextureDds(*missingPixels, MainMenuLogoTextureContract());
        expect(!refused && refused.error() == DdsWriteError::ContractMismatch, "writer refuses missing DDS pixels");
    }
}

void testDdsRejectionReasons()
{
    const auto valid = EncodeTextureDds(makeLogoImage(), MainMenuLogoTextureContract());
    expect(valid.has_value(), "encode DDS rejection fixture");
    if (!valid)
    {
        return;
    }
    for (const auto& [offset, value, expected] : std::array{
             std::tuple{ 4U, 123U, DdsMetadataError::InvalidHeaderSize },
             std::tuple{ 76U, 31U, DdsMetadataError::InvalidPixelFormatSize } })
    {
        auto malformed = *valid;
        writeU32Le(malformed, offset, value);
        const auto result = ImportTexture(malformed, TextureSourceKind::Dds);
        expect(!result && result.error() == TextureImportFailure{ DdsDocumentError{ expected } },
               "invalid DDS structure size retains the exact metadata rejection");
    }
    for (const auto& [offset, value] : std::array{
             std::pair{ 112U, 0x200U }, std::pair{ 112U, 0x200000U }, std::pair{ 88U, 24U }, std::pair{ 92U, 0x0000FF00U }, std::pair{ 92U, 0x00F0000FU }, std::pair{ 104U, 0U } })
    {
        auto unsupported = *valid;
        writeU32Le(unsupported, offset, value);
        const auto result = ImportTexture(unsupported, TextureSourceKind::Dds);
        expect(result.has_value(), "unsupported DDS keeps its parsed header");
        if (result)
        {
            const auto* layout = std::get_if<DdsSourceLayout>(&result->sourceLayout);
            expect(layout && layout->decodeError == DdsDocumentError{ DdsPreviewError::UnsupportedFormat } && result->rgba8.empty(),
                   "cubemap, volume, bit-count and mask failures retain exact decoder reason without pixels");
            const auto written = EncodeTextureDds(*result, MainMenuLogoTextureContract());
            expect(!written && written.error() == DdsWriteError::ContractMismatch, "unsupported DDS cannot be written as a logo");
        }
    }
    auto dx10 = *valid;
    writeU32Le(dx10, 80, 4);
    writeU32Le(dx10, 84, 0x30315844);
    const auto extended = ImportTexture(dx10, TextureSourceKind::Dds);
    expect(extended.has_value(), "DX10 DDS retains parsed metadata");
    if (extended)
    {
        const auto* layout = std::get_if<DdsSourceLayout>(&extended->sourceLayout);
        expect(layout && layout->metadata.dx10Header && layout->decodeError == DdsDocumentError{ DdsPreviewError::UnsupportedFormat } && extended->rgba8.empty(),
               "DX10 DDS keeps extension metadata and the unsupported decoder reason");
    }
    const auto truncatedDx10 = ImportTexture(std::span<const std::byte>(dx10).first(147), TextureSourceKind::Dds);
    expect(!truncatedDx10 && truncatedDx10.error() == TextureImportFailure{ DdsDocumentError{ DdsMetadataError::Truncated } },
           "truncated DX10 extension keeps the metadata truncation reason");
    auto rgb = *valid;
    writeU32Le(rgb, 80, 0x40);
    writeU32Le(rgb, 104, 0);
    const auto opaque = ImportTexture(rgb, TextureSourceKind::Dds);
    expect(opaque && !opaque->hasAlphaChannel && opaque->rgba8[3] == std::byte{ 255 }, "DDS without declared alpha decodes opaque");
    if (opaque)
    {
        const auto issues = ValidateTexture(*opaque, MainMenuLogoTextureContract());
        expect(std::ranges::any_of(issues, [](const auto& issue)
                                   {
                                       return issue.kind == TextureValidationIssueKind::MissingAlphaChannel && issue.expected == 1 && issue.actual == 0;
                                   }),
               "DDS missing alpha reports expected and actual availability");
    }
}

void testWriterValidation()
{
    auto image = makeLogoImage();
    image.rgba8.pop_back();
    const auto issues = ValidateTexture(image, MainMenuLogoTextureContract());
    expect(issues.size() == 1 && issues[0].kind == TextureValidationIssueKind::PixelDataSize &&
               issues[0].expected == 1048576 && issues[0].actual == 1048575,
           "invalid RGBA storage is reported before encoding");
    const auto invalid = EncodeTextureDds(image, MainMenuLogoTextureContract());
    expect(!invalid && invalid.error() == DdsWriteError::ContractMismatch, "writer rejects inconsistent RGBA storage");
    auto otherContract     = MainMenuLogoTextureContract();
    otherContract.key      = "file-data/other.dds";
    const auto unsupported = EncodeTextureDds(makeLogoImage(), otherContract);
    expect(!unsupported && unsupported.error() == DdsWriteError::UnsupportedContract, "writer rejects an unsupported runtime key");
    expect(DdsWriteErrorMessage(DdsWriteError::ReadBackFailed) ==
               "DDS parser readback did not match the source image exactly.",
           "read-back error message is exact");
    expect(DdsWriteErrorMessage(DdsWriteError::FinalizationFailed) ==
               "Could not finalize the new DDS file. The destination volume must support hard links.",
           "finalization failure explains the hard-link volume requirement");
}

void testFileWriting(const std::filesystem::path& scratch)
{
    const auto      root = scratch / ("rerevved-tools-texture-" +
                                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    const auto      created = std::filesystem::create_directory(root, error);
    expect(created && !error, "create a fresh texture test directory");
    if (!created || error)
    {
        return;
    }
    const auto destination       = root / "logo.dds";
    const auto occupiedTemporary = root / "logo.dds.tmp-0";
    {
        std::ofstream output(occupiedTemporary, std::ios::binary | std::ios::noreplace);
        output << "caller sentinel";
        expect(static_cast<bool>(output), "occupy a DDS temporary name before the first file write");
    }
    const auto imported = ImportTexture(makePng(1024, 256), TextureSourceKind::Png);
    expect(imported.has_value(), "import RGBA PNG for on-disk writer round-trip");
    if (!imported)
    {
        std::filesystem::remove(root, error);
        return;
    }
    const auto& image    = *imported;
    const auto  contract = MainMenuLogoTextureContract();
    const auto  written  = WriteTextureDds(image, contract, destination);
    expect(written.has_value(), "write DDS to a new destination with parser read-back");
    const auto readBack = LoadDdsDocument(destination);
    expect(readBack && readBack->rgba8 == image.rgba8, "on-disk DDS parser read-back preserves every pixel byte");
    {
        std::ifstream     input(occupiedTemporary, std::ios::binary);
        const std::string sentinel{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        expect(sentinel == "caller sentinel", "occupied DDS temporary sibling retains every byte");
    }

    const auto existing = root / "existing.dds";
    {
        std::ofstream output(existing, std::ios::binary | std::ios::noreplace);
        output.write("keep", 4);
        expect(static_cast<bool>(output), "write existing-destination sentinel");
    }
    const auto refused = WriteTextureDds(image, contract, existing);
    expect(!refused && refused.error() == DdsWriteError::DestinationExists, "existing DDS destination is refused");
    std::ifstream       input(existing, std::ios::binary);
    std::array<char, 5> sentinel{};
    input.read(sentinel.data(), static_cast<std::streamsize>(sentinel.size()));
    expect(input.gcount() == 4 && std::string_view(sentinel.data(), 4) == "keep",
           "destination-exists refusal leaves all existing bytes untouched");
    input.close();
    expect(DdsWriteErrorMessage(DdsWriteError::DestinationExists) == "The DDS destination already exists.",
           "destination-exists message is exact");
    const auto directory = WriteTextureDds(image, contract, root);
    expect(!directory && directory.error() == DdsWriteError::DestinationExists, "DDS writer refuses an existing directory");
    const auto missingParent = WriteTextureDds(image, contract, root / "missing" / "logo.dds");
    expect(!missingParent && missingParent.error() == DdsWriteError::OpenFailed && !std::filesystem::exists(root / "missing"),
           "DDS writer refuses a missing parent without creating it");

    std::size_t entries = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(root))
    {
        ++entries;
    }
    expect(entries == 3, "DDS writing preserves the caller's sibling and leaves no owned temporary behind");
    std::filesystem::remove(destination, error);
    std::filesystem::remove(existing, error);
    std::filesystem::remove(occupiedTemporary, error);
    expect(std::filesystem::remove(root, error) && !error, "remove exactly the owned texture test output");
}

} // namespace

int main(int argc, char** argv)
{
    const auto scratch = argc == 3 && std::string_view(argv[1]) == "--scratch-root"
                             ? std::filesystem::path(argv[2])
                             : std::filesystem::temp_directory_path();
    testRgbaRoundTrip();
    testRgbAndDimensions();
    testPalettePng();
    testUnsupportedPng();
    testMalformedPng();
    testPngDecodeRejections();
    testPngSizeLimits();
    testDdsHeaderPins();
    testDdsInputLimits();
    testDdsRejectionReasons();
    testWriterValidation();
    testFileWriting(scratch);
    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed.\n";
    }
    return failures == 0 ? 0 : 1;
}
