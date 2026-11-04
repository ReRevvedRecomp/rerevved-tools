#include "xbox_dds.h"

#include "dds_writer.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

namespace rerevved::tools
{
namespace
{

using Error = XboxDdsWriteError;

Error codecError(XboxTextureError error)
{
    switch (error)
    {
        case XboxTextureError::UnsupportedFormat:
            return Error::UnsupportedFormat;
        case XboxTextureError::InvalidDimensions:
            return Error::LayoutRequiresPowerOfTwo;
        case XboxTextureError::SizeMismatch:
            return Error::LevelDimensionMismatch;
        case XboxTextureError::SizeOverflow:
            return Error::SizeOverflow;
    }
    return Error::UnsupportedFormat;
}

std::expected<void, Error> validateImage(const Rgba8Image& image)
{
    if (image.width == 0 || image.height == 0)
    {
        return std::unexpected(Error::LevelDimensionMismatch);
    }
    const auto limit = std::vector<std::byte>().max_size();
    if (std::size_t(image.width) > limit / 4 / image.height)
    {
        return std::unexpected(Error::SizeOverflow);
    }
    if (image.rgba8.size() != std::size_t(image.width) * image.height * 4)
    {
        return std::unexpected(Error::LevelDimensionMismatch);
    }
    return {};
}

void writeU32Le(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        bytes[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 255U);
    }
}

Error publicationError(DdsWriteError error)
{
    switch (error)
    {
        case DdsWriteError::DestinationExists:
            return Error::DestinationExists;
        case DdsWriteError::OpenFailed:
            return Error::OpenFailed;
        case DdsWriteError::WriteFailed:
            return Error::WriteFailed;
        case DdsWriteError::FinalizationFailed:
            return Error::FinalizationFailed;
        case DdsWriteError::ReadBackFailed:
            return Error::ReadBackFailed;
        case DdsWriteError::UnsupportedContract:
        case DdsWriteError::ContractMismatch:
            return Error::UnsupportedFormat;
    }
    return Error::UnsupportedFormat;
}

} // namespace

std::string_view XboxDdsWriteErrorMessage(XboxDdsWriteError error)
{
    switch (error)
    {
        case Error::NoLevels:
            return "Xbox DDS writing requires at least one level.";
        case Error::LevelDimensionMismatch:
            return "DDS levels must contain exact RGBA8 pixels and follow the halving dimension chain.";
        case Error::LayoutRequiresPowerOfTwo:
            return "Xbox tiled DDS layouts and mip generation require dimensions "
                   "that are powers of two.";
        case Error::UnsupportedFormat:
            return "The DDS format or payload layout is unsupported.";
        case Error::PackedMipTailUnsupported:
            return "Xbox DDS packed mip tail placement is unsupported.";
        case Error::SizeOverflow:
            return "The DDS layout exceeds the supported size range.";
        case Error::PixelLimitExceeded:
            return DdsDocumentErrorMessage(DdsPreviewError::PixelLimitExceeded);
        case Error::DestinationExists:
            return DdsWriteErrorMessage(DdsWriteError::DestinationExists);
        case Error::OpenFailed:
            return DdsWriteErrorMessage(DdsWriteError::OpenFailed);
        case Error::WriteFailed:
            return DdsWriteErrorMessage(DdsWriteError::WriteFailed);
        case Error::FinalizationFailed:
            return DdsWriteErrorMessage(DdsWriteError::FinalizationFailed);
        case Error::ReadBackFailed:
            return "DDS parser readback did not match every encoded mip level.";
    }
    return "The Xbox DDS file could not be written.";
}

std::expected<std::vector<std::byte>, XboxDdsWriteError>
EncodeXboxDds(std::span<const Rgba8Image> levels, const XboxDdsSpec& spec)
{
    if (levels.empty())
    {
        return std::unexpected(Error::NoLevels);
    }
    const auto info = GetXboxTextureFormatInfo(spec.format);
    if (!info || (spec.payloadLayout != DdsPayloadLayout::LinearLittleEndian && spec.payloadLayout != DdsPayloadLayout::XboxTiledBigEndian))
    {
        return std::unexpected(Error::UnsupportedFormat);
    }
    const bool tiled  = spec.payloadLayout == DdsPayloadLayout::XboxTiledBigEndian;
    auto       width  = levels.front().width;
    auto       height = levels.front().height;
    if (width == 0 || height == 0 || levels.size() > static_cast<std::size_t>(std::bit_width(std::max(width, height))))
    {
        return std::unexpected(Error::LevelDimensionMismatch);
    }
    // Reject an unrepresentable header field before requiring any pixel storage.
    const bool argb            = spec.format == XboxTextureFormat::Argb8;
    const auto topWidthBlocks  = (std::uint64_t(width) + info->blockWidth - 1) / info->blockWidth;
    const auto topHeightBlocks = (std::uint64_t(height) + info->blockHeight - 1) / info->blockHeight;
    const auto headerLimit     = std::numeric_limits<std::uint32_t>::max();
    if ((argb && std::uint64_t(width) * 4 > headerLimit) ||
        (!argb && topWidthBlocks > headerLimit / info->bytesPerBlock / topHeightBlocks))
    {
        return std::unexpected(Error::SizeOverflow);
    }
    // Readback uses the preview budget, so larger chains cannot be published.
    DdsMetadata budgetShape;
    budgetShape.width  = width;
    budgetShape.height = height;
    if (const auto budget = detail::ValidateDdsPixelBudget(budgetShape, static_cast<std::uint32_t>(levels.size())); !budget)
    {
        return std::unexpected(budget.error() == DdsPreviewError::PixelLimitExceeded ? Error::PixelLimitExceeded : Error::SizeOverflow);
    }
    const auto   pitchOrSize = argb ? std::uint64_t(width) * 4 : topWidthBlocks * topHeightBlocks * info->bytesPerBlock;
    XboxMipChain chain;
    if (tiled)
    {
        if (!std::has_single_bit(width) || !std::has_single_bit(height))
        {
            return std::unexpected(Error::LayoutRequiresPowerOfTwo);
        }
        auto described = DescribeMipChain(width, height, spec.format);
        if (!described)
        {
            return std::unexpected(codecError(described.error()));
        }
        chain = std::move(*described);
        if (levels.size() > chain.packedTailBaseLevel)
        {
            return std::unexpected(Error::PackedMipTailUnsupported);
        }
    }
    const auto  limit       = std::min(std::vector<std::byte>().max_size(), static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()));
    std::size_t payloadSize = 0;
    for (std::size_t mip = 0; mip < levels.size(); ++mip)
    {
        const auto& image = levels[mip];
        if (image.width != width || image.height != height)
        {
            return std::unexpected(Error::LevelDimensionMismatch);
        }
        const auto valid = validateImage(image);
        if (!valid)
        {
            return std::unexpected(valid.error());
        }
        const auto wb = (std::size_t(width) + info->blockWidth - 1) / info->blockWidth;
        const auto hb = (std::size_t(height) + info->blockHeight - 1) / info->blockHeight;
        if (wb > limit / info->bytesPerBlock / hb)
        {
            return std::unexpected(Error::SizeOverflow);
        }
        const auto linearSize = wb * hb * info->bytesPerBlock;
        const auto offset     = tiled ? chain.levels[mip].regionOffset : payloadSize;
        const auto size       = tiled ? chain.levels[mip].tiledSize : linearSize;
        if (offset > limit - 128 || size > limit - 128 - offset)
        {
            return std::unexpected(Error::SizeOverflow);
        }
        payloadSize = offset + size;
        width       = std::max(1U, width / 2);
        height      = std::max(1U, height / 2);
    }
    std::vector<std::byte> bytes(128 + payloadSize);
    writeU32Le(bytes, 0, 0x20534444U);
    writeU32Le(bytes, 4, 124);
    writeU32Le(bytes, 8, (argb ? 0x100FU : 0x81007U) | (levels.size() > 1 ? 0x20000U : 0U));
    writeU32Le(bytes, 12, levels.front().height);
    writeU32Le(bytes, 16, levels.front().width);
    writeU32Le(bytes, 20, static_cast<std::uint32_t>(pitchOrSize));
    writeU32Le(bytes, 28, levels.size() > 1 ? static_cast<std::uint32_t>(levels.size()) : 0U);
    writeU32Le(bytes, 76, 32);
    writeU32Le(bytes, 80, argb ? 0x41U : 4U);
    if (argb)
    {
        writeU32Le(bytes, 88, 32);
        writeU32Le(bytes, 92, 0x00FF0000U);
        writeU32Le(bytes, 96, 0x0000FF00U);
        writeU32Le(bytes, 100, 0x000000FFU);
        writeU32Le(bytes, 104, 0xFF000000U);
    }
    else
    {
        writeU32Le(bytes, 84, spec.format == XboxTextureFormat::Dxt1 ? 0x31545844U : spec.format == XboxTextureFormat::Dxt3 ? 0x33545844U
                                                                                                                            : 0x35545844U);
    }
    writeU32Le(bytes, 108, 0x1000U | (levels.size() > 1 ? 0x400008U : 0U));
    std::size_t offset = 128;
    for (std::size_t mip = 0; mip < levels.size(); ++mip)
    {
        const auto& image   = levels[mip];
        auto        payload = EncodeBlocks(image, spec.format);
        if (!payload)
        {
            return std::unexpected(codecError(payload.error()));
        }
        if (tiled)
        {
            payload = TileSurface(*payload, image.width, image.height, spec.format);
            if (!payload)
            {
                return std::unexpected(codecError(payload.error()));
            }
            const auto swapped = SwapEndian(*payload, spec.format);
            if (!swapped)
            {
                return std::unexpected(codecError(swapped.error()));
            }
            offset = 128 + chain.levels[mip].regionOffset;
        }
        std::ranges::copy(*payload, bytes.begin() + offset);
        offset += payload->size();
    }
    return bytes;
}

std::expected<std::vector<Rgba8Image>, XboxDdsWriteError>
GenerateXboxMipLevels(const Rgba8Image& image, std::uint32_t count)
{
    if (count == 0)
    {
        return std::unexpected(Error::NoLevels);
    }
    const auto valid = validateImage(image);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    if (!std::has_single_bit(image.width) || !std::has_single_bit(image.height))
    {
        return std::unexpected(Error::LayoutRequiresPowerOfTwo);
    }
    if (count > static_cast<std::uint32_t>(std::bit_width(std::max(image.width, image.height))))
    {
        return std::unexpected(Error::LevelDimensionMismatch);
    }
    // Generated pixels carry no source file contract or stale DDS mip metadata.
    std::vector<Rgba8Image> levels;
    levels.push_back({ .width = image.width, .height = image.height, .hasAlphaChannel = image.hasAlphaChannel, .sourceLayout = {}, .rgba8 = image.rgba8 });
    for (std::uint32_t mip = 1; mip < count; ++mip)
    {
        const auto& previous = levels.back();
        Rgba8Image  next;
        next.width           = std::max(1U, previous.width / 2);
        next.height          = std::max(1U, previous.height / 2);
        next.hasAlphaChannel = image.hasAlphaChannel;
        next.rgba8.resize(std::size_t(next.width) * next.height * 4);
        for (std::uint32_t y = 0; y < next.height; ++y)
        {
            for (std::uint32_t x = 0; x < next.width; ++x)
            {
                for (std::size_t c = 0; c < 4; ++c)
                {
                    unsigned sum = 0;
                    for (std::uint32_t dy = 0; dy < 2; ++dy)
                    {
                        for (std::uint32_t dx = 0; dx < 2; ++dx)
                        {
                            const auto sx = std::min(previous.width - 1, x * 2 + dx);
                            const auto sy = std::min(previous.height - 1, y * 2 + dy);
                            sum += std::to_integer<unsigned>(previous.rgba8[(std::size_t(sy) * previous.width + sx) * 4 + c]);
                        }
                    }
                    next.rgba8[(std::size_t(y) * next.width + x) * 4 + c] = static_cast<std::byte>((sum + 2) / 4);
                }
            }
        }
        levels.push_back(std::move(next));
    }
    return levels;
}

std::expected<void, XboxDdsWriteError>
WriteXboxDds(std::span<const Rgba8Image> levels, const XboxDdsSpec& spec, const std::filesystem::path& destination)
{
    return detail::WriteXboxDdsWithPublisher(levels, spec, destination, detail::PublishDds);
}

std::expected<void, XboxDdsWriteError>
detail::WriteXboxDdsWithPublisher(std::span<const Rgba8Image> levels, const XboxDdsSpec& spec, const std::filesystem::path& destination, DdsPublisher publish)
{
    const auto bytes = EncodeXboxDds(levels, spec);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    const auto published = publish(*bytes, destination, [&](std::span<const std::byte> readBackBytes)
                                   {
                                       const auto document = ParseDdsDocument(readBackBytes, spec.payloadLayout);
                                       if (!document || document->mipIssue || document->mipLevels.size() != levels.size())
                                       {
                                           return false;
                                       }
                                       for (std::size_t mip = 0; mip < levels.size(); ++mip)
                                       {
                                           const auto& source  = levels[mip];
                                           const auto& decoded = document->mipLevels[mip];
                                           if (decoded.width != source.width || decoded.height != source.height)
                                           {
                                               return false;
                                           }
                                           if (spec.format == XboxTextureFormat::Argb8)
                                           {
                                               if (decoded.rgba8 != source.rgba8)
                                               {
                                                   return false;
                                               }
                                           }
                                           else
                                           {
                                               const auto blocks = EncodeBlocks(source, spec.format);
                                               if (!blocks)
                                               {
                                                   return false;
                                               }
                                               const auto expected = DecodeBlocks(*blocks, source.width, source.height, spec.format);
                                               if (!expected || decoded.rgba8 != expected->rgba8)
                                               {
                                                   return false;
                                               }
                                           }
                                       }
                                       return true;
                                   });
    if (!published)
    {
        return std::unexpected(publicationError(published.error()));
    }
    return {};
}

} // namespace rerevved::tools
