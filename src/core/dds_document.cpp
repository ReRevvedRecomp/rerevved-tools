#include "dds_document.h"

#include "xbox_dds.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace rerevved::tools
{
namespace
{

constexpr std::size_t   kPixelDataOffset        = 128;
constexpr std::uint32_t kPixelFormatAlphaPixels = 0x1;
constexpr std::uint32_t kPixelFormatRgb         = 0x40;
constexpr std::uint32_t kCaps2CubemapMask       = 0x0000FE00;
constexpr std::uint32_t kCaps2Volume            = 0x00200000;
constexpr std::uint32_t kMipMapCountFlag        = 0x20000;
constexpr std::uint64_t kPixelLimit             = 67'108'864;

bool isEightBitMask(std::uint32_t mask)
{
    if (std::popcount(mask) != 8)
    {
        return false;
    }
    const auto shift = std::countr_zero(mask);
    return mask == (0xFFU << shift);
}

std::byte readChannel(std::uint32_t pixel, std::uint32_t mask)
{
    return static_cast<std::byte>((pixel & mask) >> std::countr_zero(mask));
}

std::uint32_t readU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::string_view metadataErrorMessage(DdsMetadataError error)
{
    switch (error)
    {
        case DdsMetadataError::Truncated:
            return "DDS metadata is truncated.";
        case DdsMetadataError::InvalidSignature:
            return "The file does not have a valid DDS signature.";
        case DdsMetadataError::InvalidHeaderSize:
            return "The DDS header size is invalid.";
        case DdsMetadataError::InvalidPixelFormatSize:
            return "The DDS pixel format size is invalid.";
    }
    return "The DDS metadata is invalid.";
}

std::string_view previewErrorMessage(DdsPreviewError error)
{
    switch (error)
    {
        case DdsPreviewError::UnsupportedFormat:
            return "This DDS encoding is not supported for preview.";
        case DdsPreviewError::InvalidDimensions:
            return "The DDS preview dimensions are invalid.";
        case DdsPreviewError::SizeOverflow:
            return "The DDS preview dimensions exceed the supported size range.";
        case DdsPreviewError::PixelLimitExceeded:
            return "DDS preview levels exceed the 67,108,864 pixel (64 Mpx) limit.";
        case DdsPreviewError::InvalidMipCount:
            return "The DDS mip count exceeds the dimension chain.";
        case DdsPreviewError::LayoutRequiresPowerOfTwo:
            return "Xbox tiled DDS dimensions must be powers of two.";
        case DdsPreviewError::PackedMipTailUnsupported:
            return "Xbox DDS packed mip tail placement is unsupported.";
        case DdsPreviewError::TruncatedPixelData:
            return "The DDS top level pixel data is truncated.";
    }
    return "The DDS preview could not be decoded.";
}

} // namespace

std::string_view DdsDocumentErrorMessage(const DdsDocumentError& error)
{
    if (const auto* metadataError = std::get_if<DdsMetadataError>(&error))
    {
        return metadataErrorMessage(*metadataError);
    }
    return previewErrorMessage(std::get<DdsPreviewError>(error));
}

std::expected<DdsDocument, DdsDocumentError>
ParseDdsDocument(std::span<const std::byte> bytes)
{
    return ParseDdsDocument(bytes, DdsPayloadLayout::LinearLittleEndian);
}

std::expected<DdsDocument, DdsDocumentError>
ParseDdsDocument(std::span<const std::byte> bytes, DdsPayloadLayout layout)
{
    return detail::ParseDdsDocumentWithMipDescriber(bytes, layout, DescribeMipChain);
}

std::expected<void, DdsPreviewError>
detail::ValidateDdsPixelBudget(const DdsMetadata& metadata, std::uint32_t levelCount)
{
    std::uint64_t pixels = 0;
    auto          width  = metadata.width;
    auto          height = metadata.height;
    for (std::uint32_t mip = 0; mip < levelCount; ++mip)
    {
        const auto levelPixels = std::uint64_t(width) * height;
        // Unrepresentable RGBA sizes keep their overflow error ahead of the preview budget.
        if (levelPixels > std::vector<std::byte>().max_size() / 4)
        {
            return std::unexpected(DdsPreviewError::SizeOverflow);
        }
        if (levelPixels > kPixelLimit - pixels)
        {
            return std::unexpected(DdsPreviewError::PixelLimitExceeded);
        }
        pixels += levelPixels;
        width  = std::max(1U, width / 2);
        height = std::max(1U, height / 2);
    }
    return {};
}

std::expected<DdsDocument, DdsDocumentError>
detail::ParseDdsDocumentWithMipDescriber(std::span<const std::byte> bytes, DdsPayloadLayout layout, DdsMipDescriber describe)
{
    const auto fail = [](DdsPreviewError error) -> std::expected<DdsDocument, DdsDocumentError>
    {
        return std::unexpected(DdsDocumentError{ error });
    };
    auto metadata = ParseDdsMetadata(bytes);
    if (!metadata)
    {
        return std::unexpected(DdsDocumentError{ metadata.error() });
    }
    if (metadata->dx10Header || (metadata->caps2 & (kCaps2CubemapMask | kCaps2Volume)) != 0 ||
        (layout != DdsPayloadLayout::LinearLittleEndian && layout != DdsPayloadLayout::XboxTiledBigEndian))
    {
        return fail(DdsPreviewError::UnsupportedFormat);
    }

    const auto&       pixelFormat = metadata->pixelFormat;
    const bool        compressed  = pixelFormat.flags == 4 || pixelFormat.flags == 5;
    XboxTextureFormat format      = XboxTextureFormat::Argb8;
    if (compressed)
    {
        const auto cc = readU32Le(bytes, 84);
        switch (cc)
        {
            case 0x31545844U:
                format = XboxTextureFormat::Dxt1;
                break;
            case 0x33545844U:
                format = XboxTextureFormat::Dxt3;
                break;
            case 0x35545844U:
                format = XboxTextureFormat::Dxt5;
                break;
            default:
                return fail(DdsPreviewError::UnsupportedFormat);
        }
    }
    else
    {
        if ((pixelFormat.flags != kPixelFormatRgb && pixelFormat.flags != (kPixelFormatRgb | kPixelFormatAlphaPixels)) || pixelFormat.rgbBitCount != 32)
        {
            return fail(DdsPreviewError::UnsupportedFormat);
        }
        const bool hasAlpha = (pixelFormat.flags & kPixelFormatAlphaPixels) != 0;
        if (!isEightBitMask(pixelFormat.redMask) || !isEightBitMask(pixelFormat.greenMask) ||
            !isEightBitMask(pixelFormat.blueMask) || (pixelFormat.redMask & pixelFormat.greenMask) != 0 ||
            (pixelFormat.redMask & pixelFormat.blueMask) != 0 || (pixelFormat.greenMask & pixelFormat.blueMask) != 0 ||
            (hasAlpha && (!isEightBitMask(pixelFormat.alphaMask) ||
                          (pixelFormat.alphaMask & (pixelFormat.redMask | pixelFormat.greenMask | pixelFormat.blueMask)) != 0)))
        {
            return fail(DdsPreviewError::UnsupportedFormat);
        }
    }
    if (metadata->width == 0 || metadata->height == 0)
    {
        return fail(DdsPreviewError::InvalidDimensions);
    }
    const auto                     declaredCount = (metadata->flags & kMipMapCountFlag) != 0 ? std::max(1U, metadata->mipMapCount) : 1U;
    const auto                     maximumCount  = static_cast<std::uint32_t>(std::bit_width(std::max(metadata->width, metadata->height)));
    auto                           count         = std::min(declaredCount, maximumCount);
    std::optional<DdsPreviewError> mipIssue;
    if (declaredCount > maximumCount)
    {
        mipIssue = DdsPreviewError::InvalidMipCount;
    }
    const auto topBudget = ValidateDdsPixelBudget(*metadata, 1);
    if (!topBudget)
    {
        return fail(topBudget.error());
    }
    const auto info = *GetXboxTextureFormatInfo(format);
    if (layout == DdsPayloadLayout::XboxTiledBigEndian)
    {
        if (!std::has_single_bit(metadata->width) || !std::has_single_bit(metadata->height))
        {
            return fail(DdsPreviewError::LayoutRequiresPowerOfTwo);
        }
        // The packed tail begins when the smaller dimension reaches 16 texels.
        const auto smallerLog = static_cast<std::uint32_t>(std::bit_width(std::min(metadata->width, metadata->height))) - 1;
        const auto tailBase   = smallerLog > 4 ? smallerLog - 4 : 0;
        if (tailBase == 0)
        {
            return fail(DdsPreviewError::PackedMipTailUnsupported);
        }
        if (count > tailBase)
        {
            count = tailBase;
            if (!mipIssue)
            {
                mipIssue = DdsPreviewError::PackedMipTailUnsupported;
            }
        }
    }

    // Isolated tiled regions round both block axes up to 32, with no gaps between regions.
    std::uint64_t sourceEnd     = kPixelDataOffset;
    auto          sourceWidth   = metadata->width;
    auto          sourceHeight  = metadata->height;
    std::uint32_t completeCount = 0;
    for (; completeCount < count; ++completeCount)
    {
        const auto wb         = (std::uint64_t(sourceWidth) + info.blockWidth - 1) / info.blockWidth;
        const auto hb         = (std::uint64_t(sourceHeight) + info.blockHeight - 1) / info.blockHeight;
        const auto sourceSize = layout == DdsPayloadLayout::XboxTiledBigEndian ? ((wb + 31) / 32) * ((hb + 31) / 32) * 1024 * info.bytesPerBlock : wb * hb * info.bytesPerBlock;
        if (sourceEnd > bytes.size() || sourceSize > bytes.size() - sourceEnd)
        {
            break;
        }
        sourceEnd += sourceSize;
        sourceWidth  = std::max(1U, sourceWidth / 2);
        sourceHeight = std::max(1U, sourceHeight / 2);
    }
    const auto budget = ValidateDdsPixelBudget(*metadata, completeCount);
    if (!budget)
    {
        return fail(budget.error());
    }
    XboxMipChain chain;
    if (layout == DdsPayloadLayout::XboxTiledBigEndian)
    {
        auto described = describe(metadata->width, metadata->height, format);
        if (!described)
        {
            return fail(DdsPreviewError::SizeOverflow);
        }
        chain = std::move(*described);
    }

    DdsDocument document;
    document.metadata       = *metadata;
    document.payloadLayout  = layout;
    document.mipIssue       = mipIssue;
    std::size_t offset      = kPixelDataOffset;
    std::size_t decodedSize = 0;
    auto        width       = metadata->width;
    auto        height      = metadata->height;
    const auto  limit       = std::vector<std::byte>().max_size();
    // Bound every source range before allocating or decoding any pixel level.
    for (std::uint32_t mip = 0; mip < count; ++mip)
    {
        if (std::size_t(width) > limit / 4 / height)
        {
            return fail(DdsPreviewError::SizeOverflow);
        }
        const auto rgbaSize = std::size_t(width) * height * 4;
        if (rgbaSize > limit - decodedSize)
        {
            return fail(DdsPreviewError::SizeOverflow);
        }
        decodedSize += rgbaSize;
        const auto wb = (std::size_t(width) + info.blockWidth - 1) / info.blockWidth;
        const auto hb = (std::size_t(height) + info.blockHeight - 1) / info.blockHeight;
        if (wb > limit / info.bytesPerBlock / hb)
        {
            return fail(DdsPreviewError::SizeOverflow);
        }
        auto size = wb * hb * info.bytesPerBlock;
        if (layout == DdsPayloadLayout::XboxTiledBigEndian)
        {
            const auto& level = chain.levels[mip];
            if (level.regionOffset > limit - kPixelDataOffset)
            {
                return fail(DdsPreviewError::SizeOverflow);
            }
            offset = kPixelDataOffset + level.regionOffset;
            size   = level.tiledSize;
        }
        if (size > limit - offset)
        {
            return fail(DdsPreviewError::SizeOverflow);
        }
        if (offset > bytes.size() || size > bytes.size() - offset)
        {
            if (mip == 0)
            {
                return fail(DdsPreviewError::TruncatedPixelData);
            }
            if (!document.mipIssue)
            {
                document.mipIssue = DdsPreviewError::TruncatedPixelData;
            }
            break;
        }
        document.mipLevels.push_back({ width, height, offset, size, {} });
        offset += size;
        width  = std::max(1U, width / 2);
        height = std::max(1U, height / 2);
    }
    for (auto& level : document.mipLevels)
    {
        auto                   source = bytes.subspan(level.byteOffset, level.byteSize);
        std::vector<std::byte> linear;
        if (layout == DdsPayloadLayout::XboxTiledBigEndian)
        {
            linear.assign(source.begin(), source.end());
            const auto swapped = SwapEndian(linear, format);
            if (!swapped)
            {
                return fail(DdsPreviewError::UnsupportedFormat);
            }
            auto untiled = UntileSurface(linear, level.width, level.height, format);
            if (!untiled)
            {
                return fail(DdsPreviewError::SizeOverflow);
            }
            linear = std::move(*untiled);
            source = linear;
        }
        if (compressed)
        {
            auto image = DecodeBlocks(source, level.width, level.height, format);
            if (!image)
            {
                return fail(DdsPreviewError::SizeOverflow);
            }
            level.rgba8 = std::move(image->rgba8);
        }
        else
        {
            const bool hasAlpha = (pixelFormat.flags & kPixelFormatAlphaPixels) != 0;
            level.rgba8.resize(source.size());
            for (std::size_t i = 0; i < source.size(); i += 4)
            {
                const auto pixel   = readU32Le(source, i);
                level.rgba8[i]     = readChannel(pixel, pixelFormat.redMask);
                level.rgba8[i + 1] = readChannel(pixel, pixelFormat.greenMask);
                level.rgba8[i + 2] = readChannel(pixel, pixelFormat.blueMask);
                level.rgba8[i + 3] = hasAlpha ? readChannel(pixel, pixelFormat.alphaMask) : std::byte{ 255 };
            }
        }
    }
    document.rgba8 = document.mipLevels.front().rgba8;
    return document;
}

std::expected<DdsDocument, std::string>
LoadDdsDocument(const std::filesystem::path& path)
{
    return LoadDdsDocument(path, DdsPayloadLayout::LinearLittleEndian);
}

std::expected<DdsDocument, std::string>
LoadDdsDocument(const std::filesystem::path& path, DdsPayloadLayout layout)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        return std::unexpected("Could not open DDS file for reading.");
    }

    const auto end = input.tellg();
    if (end < 0)
    {
        return std::unexpected("Could not read DDS file size.");
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return std::unexpected("DDS file exceeds the supported size range.");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
    {
        return std::unexpected("Could not read DDS file data.");
    }
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
    {
        return std::unexpected("DDS file ended before its reported size.");
    }

    auto document = ParseDdsDocument(bytes, layout);
    if (!document)
    {
        return std::unexpected(std::string(DdsDocumentErrorMessage(document.error())));
    }
    return std::move(*document);
}

} // namespace rerevved::tools
