#include "gfx_document.h"

#include <array>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace rerevved::tools
{
namespace
{

constexpr std::uint16_t kEndTag           = 0;
constexpr std::uint16_t kExporterInfoTag  = 1000;
constexpr std::uint16_t kExternalImageTag = 1001;

bool canRead(std::span<const std::byte> bytes, std::size_t offset, std::size_t count)
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

std::uint16_t readU16Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint16_t>(bytes[offset]) |
           (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::uint32_t readU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

class BitReader
{
public:
    BitReader(std::span<const std::byte> bytes, std::size_t byteOffset)
    : bytes(bytes)
    , bitOffset(byteOffset * 8)
    {
    }

    std::optional<std::uint32_t> readUnsigned(unsigned int bitCount)
    {
        if (bitCount > bytes.size() * 8 - bitOffset)
        {
            return std::nullopt;
        }

        std::uint32_t value = 0;
        for (unsigned int index = 0; index < bitCount; ++index)
        {
            const auto byte = std::to_integer<std::uint8_t>(bytes[bitOffset / 8]);
            value           = (value << 1U) | ((byte >> (7U - (bitOffset % 8))) & 1U);
            ++bitOffset;
        }
        return value;
    }

    std::optional<std::int32_t> readSigned(unsigned int bitCount)
    {
        const auto value = readUnsigned(bitCount);
        if (!value)
        {
            return std::nullopt;
        }
        if (bitCount == 0)
        {
            return 0;
        }
        if (bitCount < 32 && (*value & (1U << (bitCount - 1U))) != 0)
        {
            return static_cast<std::int32_t>(*value | (~0U << bitCount));
        }
        return static_cast<std::int32_t>(*value);
    }

    [[nodiscard]] std::size_t alignedByteOffset() const
    {
        return (bitOffset + 7) / 8;
    }

private:
    std::span<const std::byte> bytes;
    std::size_t                bitOffset = 0;
};

struct GfxTag
{
    std::uint16_t              code = 0;
    std::span<const std::byte> payload;
    std::size_t                nextOffset = 0;
};

std::expected<GfxTag, GfxDocumentError>
readTag(std::span<const std::byte> bytes, std::size_t offset)
{
    if (!canRead(bytes, offset, 2))
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }

    const auto header = readU16Le(bytes, offset);
    offset += 2;
    std::uint32_t length = header & 0x3FU;
    if (length == 0x3FU)
    {
        if (!canRead(bytes, offset, 4))
        {
            return std::unexpected(GfxDocumentError::Truncated);
        }
        length = readU32Le(bytes, offset);
        offset += 4;
    }
    if (!canRead(bytes, offset, length))
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }

    return GfxTag{
        .code       = static_cast<std::uint16_t>(header >> 6U),
        .payload    = bytes.subspan(offset, length),
        .nextOffset = offset + length,
    };
}

std::expected<std::vector<std::byte>, GfxDocumentError>
readByteString(std::span<const std::byte> bytes, std::size_t& offset)
{
    if (!canRead(bytes, offset, 1))
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }
    const auto length = std::to_integer<std::uint8_t>(bytes[offset++]);
    if (!canRead(bytes, offset, length))
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }

    std::vector<std::byte> value(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
    return value;
}

// GFX tag 1000 and the exporter 2.x gate are documented by the pinned
// Scaleform GFx v2 source. Only layout facts are used. No source code is copied.
// https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxLoaderImpl.cpp
std::expected<GfxExporterInfo, GfxDocumentError>
parseExporterInfo(std::span<const std::byte> payload)
{
    if (!canRead(payload, 0, 2))
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }

    GfxExporterInfo exporter{};
    exporter.version = readU16Le(payload, 0);
    if ((exporter.version & 0xFF00U) != 0x0200U)
    {
        return std::unexpected(GfxDocumentError::UnsupportedExporterVersion);
    }
    if (!canRead(payload, 2, 6))
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }

    exporter.flags        = readU32Le(payload, 2);
    exporter.bitmapFormat = readU16Le(payload, 6);
    std::size_t offset    = 8;
    auto        prefix    = readByteString(payload, offset);
    if (!prefix)
    {
        return std::unexpected(prefix.error());
    }
    auto swfName = readByteString(payload, offset);
    if (!swfName)
    {
        return std::unexpected(swfName.error());
    }
    if (offset != payload.size())
    {
        return std::unexpected(GfxDocumentError::InvalidTagStream);
    }

    exporter.prefix  = std::move(*prefix);
    exporter.swfName = std::move(*swfName);
    return exporter;
}

// GFX tag 1001 is documented by the same pinned source at:
// https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxTagLoaders.cpp
std::expected<GfxExternalImage, GfxDocumentError>
parseExternalImage(std::span<const std::byte> payload)
{
    if (!canRead(payload, 0, 8))
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }

    GfxExternalImage image{
        .characterId  = readU16Le(payload, 0),
        .bitmapFormat = readU16Le(payload, 2),
        .targetWidth  = readU16Le(payload, 4),
        .targetHeight = readU16Le(payload, 6),
        .exportName   = {},
        .fileName     = {},
    };
    std::size_t offset     = 8;
    auto        exportName = readByteString(payload, offset);
    if (!exportName)
    {
        return std::unexpected(exportName.error());
    }
    auto fileName = readByteString(payload, offset);
    if (!fileName)
    {
        return std::unexpected(fileName.error());
    }
    if (offset != payload.size())
    {
        return std::unexpected(GfxDocumentError::InvalidTagStream);
    }

    image.exportName = std::move(*exportName);
    image.fileName   = std::move(*fileName);
    return image;
}

} // namespace

std::string_view GfxDocumentErrorMessage(GfxDocumentError error)
{
    switch (error)
    {
        case GfxDocumentError::Truncated:
            return "The GFX movie is truncated.";
        case GfxDocumentError::InvalidSignature:
            return "The file does not have a valid GFX signature.";
        case GfxDocumentError::UnsupportedCompression:
            return "Compressed GFX movies are not supported.";
        case GfxDocumentError::LengthMismatch:
            return "The GFX declared length does not match the file size.";
        case GfxDocumentError::MissingExporterInfo:
            return "The GFX movie does not begin with exporter information.";
        case GfxDocumentError::UnsupportedExporterVersion:
            return "The GFX exporter version is not supported.";
        case GfxDocumentError::InvalidTagStream:
            return "The GFX tag stream is invalid.";
    }
    return "The GFX movie is invalid.";
}

std::expected<GfxDocument, GfxDocumentError>
ParseGfxDocument(std::span<const std::byte> bytes)
{
    if (bytes.size() < 3)
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }
    const std::array signature{ bytes[0], bytes[1], bytes[2] };
    if (signature == std::array{ std::byte{ 'C' }, std::byte{ 'F' }, std::byte{ 'X' } })
    {
        return std::unexpected(GfxDocumentError::UnsupportedCompression);
    }
    if (signature != std::array{ std::byte{ 'G' }, std::byte{ 'F' }, std::byte{ 'X' } })
    {
        return std::unexpected(GfxDocumentError::InvalidSignature);
    }
    if (bytes.size() < 8)
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }

    GfxDocument document{
        .fileVersion    = std::to_integer<std::uint8_t>(bytes[3]),
        .declaredLength = readU32Le(bytes, 4),
        .frame          = {},
        .frameRateRaw   = 0,
        .frameCount     = 0,
        .exporter       = {},
        .externalImages = {},
    };
    if (document.declaredLength != bytes.size())
    {
        return std::unexpected(GfxDocumentError::LengthMismatch);
    }

    // RECT and tag record framing follow the Adobe SWF file format specification.
    // https://open-flash.github.io/mirrors/swf-spec-19.pdf
    BitReader  bits(bytes, 8);
    const auto coordinateBits = bits.readUnsigned(5);
    if (!coordinateBits)
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }
    const auto xMin = bits.readSigned(*coordinateBits);
    const auto xMax = bits.readSigned(*coordinateBits);
    const auto yMin = bits.readSigned(*coordinateBits);
    const auto yMax = bits.readSigned(*coordinateBits);
    if (!xMin || !xMax || !yMin || !yMax)
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }
    document.frame = GfxRect{
        .xMinTwips = *xMin,
        .xMaxTwips = *xMax,
        .yMinTwips = *yMin,
        .yMaxTwips = *yMax,
    };

    std::size_t offset = bits.alignedByteOffset();
    if (!canRead(bytes, offset, 4))
    {
        return std::unexpected(GfxDocumentError::Truncated);
    }
    document.frameRateRaw = readU16Le(bytes, offset);
    document.frameCount   = readU16Le(bytes, offset + 2);
    offset += 4;

    auto exporterTag = readTag(bytes, offset);
    if (!exporterTag)
    {
        return std::unexpected(exporterTag.error());
    }
    if (exporterTag->code != kExporterInfoTag)
    {
        return std::unexpected(GfxDocumentError::MissingExporterInfo);
    }
    auto exporter = parseExporterInfo(exporterTag->payload);
    if (!exporter)
    {
        return std::unexpected(exporter.error());
    }
    document.exporter = std::move(*exporter);
    offset            = exporterTag->nextOffset;

    while (offset < bytes.size())
    {
        auto tag = readTag(bytes, offset);
        if (!tag)
        {
            return std::unexpected(tag.error());
        }
        if (tag->code == kEndTag)
        {
            if (!tag->payload.empty() || tag->nextOffset != bytes.size())
            {
                return std::unexpected(GfxDocumentError::InvalidTagStream);
            }
            return document;
        }
        if (tag->code == kExternalImageTag)
        {
            auto image = parseExternalImage(tag->payload);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            document.externalImages.push_back(std::move(*image));
        }
        offset = tag->nextOffset;
    }

    return std::unexpected(GfxDocumentError::InvalidTagStream);
}

std::expected<GfxDocument, std::string>
LoadGfxDocument(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        return std::unexpected("Could not open GFX file for reading.");
    }

    const auto end = input.tellg();
    if (end < 0)
    {
        return std::unexpected("Could not read GFX file size.");
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return std::unexpected("GFX file exceeds the supported size range.");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
    {
        return std::unexpected("Could not read GFX file data.");
    }
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
    {
        return std::unexpected("GFX file ended before its reported size.");
    }

    auto document = ParseGfxDocument(bytes);
    if (!document)
    {
        return std::unexpected(std::string(GfxDocumentErrorMessage(document.error())));
    }
    return std::move(*document);
}

} // namespace rerevved::tools
