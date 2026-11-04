#include "dds_metadata.h"

#include <algorithm>
#include <array>

namespace rerevved::tools
{
namespace
{

constexpr std::size_t              kBaseMetadataSize  = 128;
constexpr std::size_t              kDx10MetadataSize  = 148;
constexpr std::uint32_t            kDdsHeaderSize     = 124;
constexpr std::uint32_t            kPixelFormatSize   = 32;
constexpr std::uint32_t            kPixelFormatFourCc = 0x4;
constexpr std::array<std::byte, 4> kSignature{
    std::byte{ 'D' }, std::byte{ 'D' }, std::byte{ 'S' }, std::byte{ ' ' }
};
constexpr std::array<std::byte, 4> kDx10FourCc{
    std::byte{ 'D' }, std::byte{ 'X' }, std::byte{ '1' }, std::byte{ '0' }
};

std::uint32_t readU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

template <std::size_t Size>
std::array<std::byte, Size> readBytes(std::span<const std::byte> bytes, std::size_t offset)
{
    std::array<std::byte, Size> result{};
    std::ranges::copy(bytes.subspan(offset, Size), result.begin());
    return result;
}

} // namespace

std::expected<DdsMetadata, DdsMetadataError>
ParseDdsMetadata(std::span<const std::byte> bytes)
{
    if (bytes.size() < kBaseMetadataSize)
    {
        return std::unexpected(DdsMetadataError::Truncated);
    }
    if (!std::ranges::equal(bytes.first<4>(), kSignature))
    {
        return std::unexpected(DdsMetadataError::InvalidSignature);
    }
    if (readU32Le(bytes, 4) != kDdsHeaderSize)
    {
        return std::unexpected(DdsMetadataError::InvalidHeaderSize);
    }
    if (readU32Le(bytes, 76) != kPixelFormatSize)
    {
        return std::unexpected(DdsMetadataError::InvalidPixelFormatSize);
    }

    DdsMetadata metadata{};
    metadata.flags                   = readU32Le(bytes, 8);
    metadata.height                  = readU32Le(bytes, 12);
    metadata.width                   = readU32Le(bytes, 16);
    metadata.pitchOrLinearSize       = readU32Le(bytes, 20);
    metadata.depth                   = readU32Le(bytes, 24);
    metadata.mipMapCount             = readU32Le(bytes, 28);
    metadata.reserved1               = readBytes<44>(bytes, 32);
    metadata.pixelFormat.flags       = readU32Le(bytes, 80);
    metadata.pixelFormat.fourCc      = readBytes<4>(bytes, 84);
    metadata.pixelFormat.rgbBitCount = readU32Le(bytes, 88);
    metadata.pixelFormat.redMask     = readU32Le(bytes, 92);
    metadata.pixelFormat.greenMask   = readU32Le(bytes, 96);
    metadata.pixelFormat.blueMask    = readU32Le(bytes, 100);
    metadata.pixelFormat.alphaMask   = readU32Le(bytes, 104);
    metadata.caps                    = readU32Le(bytes, 108);
    metadata.caps2                   = readU32Le(bytes, 112);
    metadata.capsReserved            = readBytes<8>(bytes, 116);
    metadata.reserved2               = readBytes<4>(bytes, 124);

    const bool hasDx10Header =
        (metadata.pixelFormat.flags & kPixelFormatFourCc) != 0 &&
        metadata.pixelFormat.fourCc == kDx10FourCc;
    if (!hasDx10Header)
    {
        return metadata;
    }
    if (bytes.size() < kDx10MetadataSize)
    {
        return std::unexpected(DdsMetadataError::Truncated);
    }

    metadata.dx10Header = DdsDx10Header{
        .dxgiFormat        = readU32Le(bytes, 128),
        .resourceDimension = readU32Le(bytes, 132),
        .miscFlag          = readU32Le(bytes, 136),
        .arraySize         = readU32Le(bytes, 140),
        .miscFlags2        = readU32Le(bytes, 144),
    };
    return metadata;
}

} // namespace rerevved::tools
