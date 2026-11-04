#include "texture_import.h"

#include "dds_document.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include <stb_image.h>

namespace rerevved::tools
{
namespace
{

std::uint32_t readU32Be(std::span<const std::byte> bytes, std::size_t offset)
{
    return (std::to_integer<std::uint32_t>(bytes[offset]) << 24U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 8U) |
           std::to_integer<std::uint32_t>(bytes[offset + 3]);
}

std::uint32_t crc32(std::span<const std::byte> bytes)
{
    static constexpr auto kTable = []
    {
        std::array<std::uint32_t, 256> table{};
        for (std::size_t index = 0; index < table.size(); ++index)
        {
            auto crc = static_cast<std::uint32_t>(index);
            for (unsigned bit = 0; bit < 8; ++bit)
            {
                crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? 0xEDB88320U : 0U);
            }
            table[index] = crc;
        }
        return table;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto value : bytes)
    {
        crc = (crc >> 8U) ^ kTable[(crc ^ std::to_integer<std::uint32_t>(value)) & 0xFFU];
    }
    return ~crc;
}

struct PngHeader
{
    std::uint32_t          width  = 0;
    std::uint32_t          height = 0;
    PngSourceLayout        layout;
    bool                   hasAlphaChannel = false;
    bool                   interlaced      = false;
    std::size_t            paletteEntries  = 0;
    std::vector<std::byte> compressed;
};

std::expected<PngHeader, TextureImportError> inspectPng(std::span<const std::byte> bytes)
{
    constexpr std::array<std::byte, 8> kSignature{
        std::byte{ 137 }, std::byte{ 80 }, std::byte{ 78 }, std::byte{ 71 }, std::byte{ 13 }, std::byte{ 10 }, std::byte{ 26 }, std::byte{ 10 }
    };
    if (bytes.size() < kSignature.size() ||
        std::memcmp(bytes.data(), kSignature.data(), kSignature.size()) != 0)
    {
        return std::unexpected(TextureImportError::InvalidPng);
    }

    PngHeader   header;
    bool        sawHeader       = false;
    bool        sawPalette      = false;
    bool        sawTransparency = false;
    bool        sawData         = false;
    bool        dataEnded       = false;
    std::size_t offset          = kSignature.size();
    while (offset < bytes.size())
    {
        if (bytes.size() - offset < 12)
        {
            return std::unexpected(TextureImportError::InvalidPng);
        }
        const auto length = static_cast<std::size_t>(readU32Be(bytes, offset));
        if (length > bytes.size() - offset - 12)
        {
            return std::unexpected(TextureImportError::InvalidPng);
        }
        const auto type = bytes.subspan(offset + 4, 4);
        for (const auto value : type)
        {
            const auto ch = std::to_integer<unsigned>(value);
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')))
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
        }
        if ((std::to_integer<unsigned>(type[2]) & 0x20U) != 0 ||
            crc32(bytes.subspan(offset + 4, length + 4)) != readU32Be(bytes, offset + 8 + length))
        {
            return std::unexpected(TextureImportError::InvalidPng);
        }
        const std::string_view name(reinterpret_cast<const char*>(type.data()), type.size());
        const auto             payload = bytes.subspan(offset + 8, length);
        if (!sawHeader && name != "IHDR")
        {
            return std::unexpected(TextureImportError::InvalidPng);
        }
        if (name == "IHDR")
        {
            if (sawHeader || length != 13)
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
            sawHeader               = true;
            header.width            = readU32Be(payload, 0);
            header.height           = readU32Be(payload, 4);
            header.layout.bitDepth  = std::to_integer<std::uint8_t>(payload[8]);
            header.layout.colorType = std::to_integer<std::uint8_t>(payload[9]);
            if (header.width == 0 || header.height == 0 || payload[10] != std::byte{ 0 } ||
                payload[11] != std::byte{ 0 } || std::to_integer<unsigned>(payload[12]) > 1)
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
            constexpr std::uint64_t kMaximumPngPixels = 67108864;
            if (static_cast<std::uint64_t>(header.width) * header.height > kMaximumPngPixels)
            {
                return std::unexpected(TextureImportError::PngPixelLimitExceeded);
            }
            const auto depth = header.layout.bitDepth;
            if (depth == 16)
            {
                return std::unexpected(TextureImportError::Png16BitUnsupported);
            }
            if (header.layout.colorType == 0 || header.layout.colorType == 4)
            {
                return std::unexpected(TextureImportError::PngGrayscaleUnsupported);
            }
            if (header.layout.colorType != 2 && header.layout.colorType != 3 && header.layout.colorType != 6)
            {
                return std::unexpected(TextureImportError::PngColorTypeUnsupported);
            }
            if ((header.layout.colorType != 3 && depth != 8) ||
                (header.layout.colorType == 3 && depth != 1 && depth != 2 && depth != 4 && depth != 8))
            {
                return std::unexpected(TextureImportError::PngBitDepthUnsupported);
            }
            header.hasAlphaChannel = header.layout.colorType == 6;
            header.interlaced      = payload[12] == std::byte{ 1 };
        }
        else if (name == "PLTE")
        {
            if (sawPalette || sawTransparency || sawData || length == 0 || length > 768 || length % 3 != 0)
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
            sawPalette            = true;
            header.paletteEntries = length / 3;
            if (header.layout.colorType == 3 && header.paletteEntries > (1U << header.layout.bitDepth))
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
        }
        else if (name == "tRNS")
        {
            if (sawTransparency || sawData || header.layout.colorType == 6 ||
                (header.layout.colorType == 2 && length != 6) ||
                (header.layout.colorType == 3 && (!sawPalette || length == 0 || length > header.paletteEntries)))
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
            if (header.layout.colorType == 2 &&
                (payload[0] != std::byte{ 0 } || payload[2] != std::byte{ 0 } || payload[4] != std::byte{ 0 }))
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
            sawTransparency        = true;
            header.hasAlphaChannel = true;
        }
        else if (name == "IDAT")
        {
            if (dataEnded || (header.layout.colorType == 3 && !sawPalette))
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
            sawData = true;
            header.compressed.insert(header.compressed.end(), payload.begin(), payload.end());
        }
        else if (name == "IEND")
        {
            if (length != 0 || !sawData || offset + 12 != bytes.size())
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
            return header;
        }
        else if ((std::to_integer<unsigned>(type[0]) & 0x20U) == 0)
        {
            return std::unexpected(TextureImportError::InvalidPng);
        }
        if (sawData && name != "IDAT")
        {
            dataEnded = true;
        }
        offset += length + 12;
    }
    return std::unexpected(TextureImportError::InvalidPng);
}

// Palette expansion assumes that every index addresses an existing PLTE entry.
bool validatePalettePass(std::span<const char> raw, std::size_t width, std::size_t height, const PngHeader& header)
{
    const auto                 rowSize = (width * header.layout.bitDepth + 7) / 8;
    std::vector<unsigned char> previous(rowSize);
    std::vector<unsigned char> current(rowSize);
    for (std::size_t y = 0; y < height; ++y)
    {
        const auto row    = raw.subspan(y * (rowSize + 1), rowSize + 1);
        const auto filter = static_cast<unsigned char>(row[0]);
        if (filter > 4)
        {
            return false;
        }
        for (std::size_t x = 0; x < rowSize; ++x)
        {
            const int left      = x == 0 ? 0 : current[x - 1];
            const int above     = previous[x];
            const int upperLeft = x == 0 ? 0 : previous[x - 1];
            int       predictor = 0;
            switch (filter)
            {
                case 1:
                    predictor = left;
                    break;
                case 2:
                    predictor = above;
                    break;
                case 3:
                    predictor = (left + above) / 2;
                    break;
                case 4:
                {
                    const auto p = left + above - upperLeft;
                    const auto a = std::abs(p - left);
                    const auto b = std::abs(p - above);
                    const auto c = std::abs(p - upperLeft);
                    predictor    = a <= b && a <= c ? left : b <= c ? above
                                                                    : upperLeft;
                    break;
                }
                default:
                    break;
            }
            current[x] = static_cast<unsigned char>(static_cast<unsigned char>(row[x + 1]) + predictor);
        }
        for (std::size_t x = 0; x < width; ++x)
        {
            const auto bit   = x * header.layout.bitDepth;
            const auto shift = 8 - header.layout.bitDepth - bit % 8;
            const auto index = (current[bit / 8] >> shift) & ((1U << header.layout.bitDepth) - 1U);
            if (index >= header.paletteEntries)
            {
                return false;
            }
        }
        previous.swap(current);
    }
    return true;
}

std::expected<void, TextureImportError> validatePngStream(const PngHeader& header)
{
    const auto                        channels = header.layout.colorType == 2 ? 3U : header.layout.colorType == 6 ? 4U
                                                                                                                  : 1U;
    constexpr std::array<unsigned, 7> kStartX{ 0, 4, 0, 2, 0, 1, 0 };
    constexpr std::array<unsigned, 7> kStartY{ 0, 0, 4, 0, 2, 0, 1 };
    constexpr std::array<unsigned, 7> kStepX{ 8, 8, 4, 4, 2, 2, 1 };
    constexpr std::array<unsigned, 7> kStepY{ 8, 8, 8, 4, 4, 2, 2 };
    std::uint64_t                     rawSize = 0;
    std::array<std::uint64_t, 7>      passWidths{};
    std::array<std::uint64_t, 7>      passHeights{};
    for (std::size_t pass = 0; pass < (header.interlaced ? 7U : 1U); ++pass)
    {
        const auto startX = header.interlaced ? kStartX[pass] : 0U;
        const auto startY = header.interlaced ? kStartY[pass] : 0U;
        const auto stepX  = header.interlaced ? kStepX[pass] : 1U;
        const auto stepY  = header.interlaced ? kStepY[pass] : 1U;
        if (header.width <= startX || header.height <= startY)
        {
            continue;
        }
        const auto width  = (static_cast<std::uint64_t>(header.width) - startX + stepX - 1) / stepX;
        const auto height = (static_cast<std::uint64_t>(header.height) - startY + stepY - 1) / stepY;
        passWidths[pass]  = width;
        passHeights[pass] = height;
        rawSize += ((width * channels * header.layout.bitDepth + 7) / 8 + 1) * height;
    }
    if (rawSize > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected(TextureImportError::SourceTooLarge);
    }
    if (header.compressed.size() < 6)
    {
        return std::unexpected(TextureImportError::InvalidPng);
    }

    // Deflate cannot expand beyond this bound. Reject impossible IHDR sizes before allocating.
    // Encoded input is limited to INT_MAX, so the uint64_t multiplication cannot overflow.
    if (rawSize > 1032ULL * header.compressed.size() + 1024ULL)
    {
        return std::unexpected(TextureImportError::InvalidPng);
    }

    // stb_image does not check PNG CRCs or the zlib Adler-32 trailer.
    std::vector<char> raw(static_cast<std::size_t>(rawSize));
    const auto        decoded = stbi_zlib_decode_buffer(raw.data(), static_cast<int>(raw.size()), reinterpret_cast<const char*>(header.compressed.data()), static_cast<int>(header.compressed.size()));
    if (decoded < 0 || static_cast<std::size_t>(decoded) != raw.size())
    {
        return std::unexpected(TextureImportError::InvalidPng);
    }
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const auto value : raw)
    {
        a = (a + static_cast<unsigned char>(value)) % 65521U;
        b = (b + a) % 65521U;
    }
    if ((b << 16U | a) != readU32Be(header.compressed, header.compressed.size() - 4))
    {
        return std::unexpected(TextureImportError::InvalidPng);
    }
    if (header.layout.colorType == 3)
    {
        std::size_t offset = 0;
        for (std::size_t pass = 0; pass < passWidths.size(); ++pass)
        {
            const auto width  = static_cast<std::size_t>(passWidths[pass]);
            const auto height = static_cast<std::size_t>(passHeights[pass]);
            const auto size   = ((width * header.layout.bitDepth + 7) / 8 + 1) * height;
            if (!validatePalettePass(std::span<const char>(raw).subspan(offset, size), width, height, header))
            {
                return std::unexpected(TextureImportError::InvalidPng);
            }
            offset += size;
        }
    }
    return {};
}

std::expected<Rgba8Image, TextureImportError> importPng(std::span<const std::byte> bytes)
{
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected(TextureImportError::SourceTooLarge);
    }
    const auto header = inspectPng(bytes);
    if (!header)
    {
        return std::unexpected(header.error());
    }
    const auto pixelCount = static_cast<std::uint64_t>(header->width) * header->height;

    const auto stream = validatePngStream(*header);
    if (!stream)
    {
        return std::unexpected(stream.error());
    }

    int                                                        width  = 0;
    int                                                        height = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                              static_cast<int>(bytes.size()),
                              &width,
                              &height,
                              nullptr,
                              4),
        &stbi_image_free);
    if (!pixels || width <= 0 || height <= 0 ||
        static_cast<std::uint32_t>(width) != header->width ||
        static_cast<std::uint32_t>(height) != header->height)
    {
        return std::unexpected(TextureImportError::InvalidPng);
    }
    Rgba8Image image{
        .width           = header->width,
        .height          = header->height,
        .hasAlphaChannel = header->hasAlphaChannel,
        .sourceLayout    = header->layout,
        .rgba8           = std::vector<std::byte>(static_cast<std::size_t>(pixelCount) * 4),
    };
    std::memcpy(image.rgba8.data(), pixels.get(), image.rgba8.size());
    return image;
}

} // namespace

TextureKeyContract MainMenuLogoTextureContract()
{
    return {
        .key                  = "file-data/GFX_MainMenu_logo.dds",
        .width                = 1024,
        .height               = 256,
        .encoding             = TextureEncoding::LegacyA8R8G8B8,
        .mipLevelCount        = 1,
        .requiresAlphaChannel = true,
    };
}

std::string_view TextureImportErrorMessage(TextureImportError error)
{
    switch (error)
    {
        case TextureImportError::InvalidPng:
            return "The PNG data is malformed or truncated.";
        case TextureImportError::Png16BitUnsupported:
            return "16 bit PNG input is not supported. Use 8 bit color.";
        case TextureImportError::PngGrayscaleUnsupported:
            return "Grayscale PNG input is not supported. Use truecolor or palette color.";
        case TextureImportError::PngColorTypeUnsupported:
            return "The PNG color type is not supported.";
        case TextureImportError::PngBitDepthUnsupported:
            return "The PNG bit depth is not supported for its color type.";
        case TextureImportError::SourceTooLarge:
            return "The texture source exceeds the supported size range.";
        case TextureImportError::PngPixelLimitExceeded:
            return "PNG input exceeds the limit of 67,108,864 pixels (64 Mpx).";
        case TextureImportError::AllocationFailed:
            return "There is not enough memory to import the texture.";
        case TextureImportError::InvalidSourceKind:
            return "The texture source kind is invalid.";
    }
    return "The texture could not be imported.";
}

std::string_view TextureImportErrorMessage(const TextureImportFailure& error)
{
    if (const auto* importError = std::get_if<TextureImportError>(&error))
    {
        return TextureImportErrorMessage(*importError);
    }
    return DdsDocumentErrorMessage(std::get<DdsDocumentError>(error));
}

std::expected<Rgba8Image, TextureImportFailure>
ImportTexture(std::span<const std::byte> bytes, TextureSourceKind sourceKind)
try
{
    if (sourceKind == TextureSourceKind::Png)
    {
        return importPng(bytes);
    }
    if (sourceKind != TextureSourceKind::Dds)
    {
        return std::unexpected(TextureImportError::InvalidSourceKind);
    }
    const auto metadata = ParseDdsMetadata(bytes);
    if (!metadata)
    {
        return std::unexpected(TextureImportFailure{ DdsDocumentError{ metadata.error() } });
    }
    Rgba8Image image{
        .width           = metadata->width,
        .height          = metadata->height,
        .hasAlphaChannel = (metadata->pixelFormat.flags & 1U) != 0,
        .sourceLayout    = DdsSourceLayout{ *metadata, std::nullopt },
        .rgba8           = {},
    };
    auto document = ParseDdsDocument(bytes);
    if (!document)
    {
        std::get<DdsSourceLayout>(image.sourceLayout).decodeError = document.error();
        return image;
    }
    image.rgba8 = std::move(document->rgba8);
    return image;
}
catch (const std::bad_alloc&)
{
    return std::unexpected(TextureImportError::AllocationFailed);
}

std::vector<TextureValidationIssue>
ValidateTexture(const Rgba8Image& image, const TextureKeyContract& contract)
{
    std::vector<TextureValidationIssue> issues;
    if (image.width != contract.width)
    {
        issues.push_back({ TextureValidationIssueKind::Width, contract.width, image.width });
    }
    if (image.height != contract.height)
    {
        issues.push_back({ TextureValidationIssueKind::Height, contract.height, image.height });
    }
    std::uint32_t mipLevelCount = 1;
    auto          encoding      = TextureEncoding::LegacyA8R8G8B8;
    if (const auto* dds = std::get_if<DdsSourceLayout>(&image.sourceLayout))
    {
        const auto& metadata = dds->metadata;
        mipLevelCount        = metadata.mipMapCount == 0 ? 1 : metadata.mipMapCount;
        const auto& format   = metadata.pixelFormat;
        if (metadata.dx10Header || (metadata.caps2 & 0x0020FE00U) != 0 ||
            format.flags != 0x41U || format.fourCc != std::array<std::byte, 4>{} ||
            format.rgbBitCount != 32 || format.redMask != 0x00FF0000U ||
            format.greenMask != 0x0000FF00U || format.blueMask != 0x000000FFU ||
            format.alphaMask != 0xFF000000U)
        {
            encoding = TextureEncoding::Unsupported;
        }
    }
    if (mipLevelCount != contract.mipLevelCount)
    {
        issues.push_back({ TextureValidationIssueKind::MipLevelCount, contract.mipLevelCount, mipLevelCount });
    }
    if (encoding != contract.encoding)
    {
        issues.push_back({ TextureValidationIssueKind::Encoding,
                           static_cast<std::uint64_t>(contract.encoding),
                           static_cast<std::uint64_t>(encoding) });
    }
    if (contract.requiresAlphaChannel && !image.hasAlphaChannel)
    {
        issues.push_back({ TextureValidationIssueKind::MissingAlphaChannel, 1, 0 });
    }
    const auto pixelCount   = static_cast<std::uint64_t>(image.width) * image.height;
    const auto expectedSize = pixelCount > std::numeric_limits<std::uint64_t>::max() / 4
                                  ? std::numeric_limits<std::uint64_t>::max()
                                  : pixelCount * 4;
    if (image.rgba8.size() != expectedSize)
    {
        issues.push_back({ TextureValidationIssueKind::PixelDataSize, expectedSize, image.rgba8.size() });
    }
    return issues;
}

} // namespace rerevved::tools
