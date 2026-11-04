#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace rerevved::tools
{

enum class DdsMetadataError
{
    Truncated,
    InvalidSignature,
    InvalidHeaderSize,
    InvalidPixelFormatSize,
};

struct DdsPixelFormat
{
    std::uint32_t            flags = 0;
    std::array<std::byte, 4> fourCc{};
    std::uint32_t            rgbBitCount = 0;
    std::uint32_t            redMask     = 0;
    std::uint32_t            greenMask   = 0;
    std::uint32_t            blueMask    = 0;
    std::uint32_t            alphaMask   = 0;
};

struct DdsDx10Header
{
    std::uint32_t dxgiFormat        = 0;
    std::uint32_t resourceDimension = 0;
    std::uint32_t miscFlag          = 0;
    std::uint32_t arraySize         = 0;
    std::uint32_t miscFlags2        = 0;
};

struct DdsMetadata
{
    std::uint32_t                flags             = 0;
    std::uint32_t                height            = 0;
    std::uint32_t                width             = 0;
    std::uint32_t                pitchOrLinearSize = 0;
    std::uint32_t                depth             = 0;
    std::uint32_t                mipMapCount       = 0;
    std::array<std::byte, 44>    reserved1{};
    DdsPixelFormat               pixelFormat{};
    std::uint32_t                caps  = 0;
    std::uint32_t                caps2 = 0;
    std::array<std::byte, 8>     capsReserved{};
    std::array<std::byte, 4>     reserved2{};
    std::optional<DdsDx10Header> dx10Header;
};

[[nodiscard]] std::expected<DdsMetadata, DdsMetadataError>
ParseDdsMetadata(std::span<const std::byte> bytes);

} // namespace rerevved::tools
