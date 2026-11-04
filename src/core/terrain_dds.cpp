#include "terrain_dds.h"

#include "dds_writer.h"
#include "xbox_texture.h"

#include <algorithm>
#include <array>
#include <new>
#include <utility>

namespace rerevved::tools
{

namespace
{

using Error  = TerrainDdsError;
using Header = std::array<std::byte, 128>;

// The header shared byte for byte by every shipped blends file and by most
// shipped lightmaps (a few also set the linear size, which this writer omits).
Header makeHeader(std::uint32_t side)
{
    Header bytes{};
    for (const auto [offset, value] : std::array{
             std::pair{ 0U, 0x20534444U },
             std::pair{ 4U, 124U },
             std::pair{ 8U, 0x1007U },
             std::pair{ 12U, side },
             std::pair{ 16U, side },
             std::pair{ 76U, 32U },
             std::pair{ 80U, 0x4U },
             std::pair{ 84U, 0x31545844U },
             std::pair{ 108U, 0x1000U } })
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            bytes[offset + i] = static_cast<std::byte>((value >> (i * 8)) & 255U);
        }
    }
    return bytes;
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
        case DdsWriteError::UnsupportedContract:
        case DdsWriteError::ContractMismatch:
            return Error::ReadBackFailed;
    }
    return Error::ReadBackFailed;
}

} // namespace

std::string_view TerrainDdsErrorMessage(TerrainDdsError error)
{
    switch (error)
    {
        case Error::DimensionMismatch:
            return "The image is not the size this terrain texture needs.";
        case Error::EncodeFailed:
            return "DXT1 encoding failed.";
        case Error::LengthMismatch:
            return "Terrain DDS length does not match its role.";
        case Error::HeaderMismatch:
            return "Terrain DDS header differs from the shipped DXT1 layout.";
        case Error::DestinationExists:
            return DdsWriteErrorMessage(DdsWriteError::DestinationExists);
        case Error::OpenFailed:
            return DdsWriteErrorMessage(DdsWriteError::OpenFailed);
        case Error::WriteFailed:
            return DdsWriteErrorMessage(DdsWriteError::WriteFailed);
        case Error::FinalizationFailed:
            return DdsWriteErrorMessage(DdsWriteError::FinalizationFailed);
        case Error::ReadBackFailed:
            return "Terrain DDS readback did not match the encoded blocks.";
    }
    return "Unknown terrain DDS error.";
}

std::string_view TerrainTextureLabel(TerrainTexture role)
{
    return role == TerrainTexture::MountainHillBlends ? "Mountain-hill blends" : "Lightmap";
}

std::uint32_t TerrainTextureSide(TerrainTexture role)
{
    return role == TerrainTexture::MountainHillBlends ? kMountainHillBlendsSide : kLightmapSide;
}

std::size_t TerrainDdsByteCount(TerrainTexture role)
{
    const std::size_t side = TerrainTextureSide(role);
    return 128 + side * side / 2;
}

std::expected<std::vector<std::byte>, TerrainDdsError>
EncodeTerrainDds(TerrainTexture role, const Rgba8Image& image)
try
{
    const auto side = TerrainTextureSide(role);
    if (image.width != side || image.height != side || image.rgba8.size() != std::size_t(side) * side * 4)
    {
        return std::unexpected(Error::DimensionMismatch);
    }
    Rgba8Image opaque = image;
    for (std::size_t i = 3; i < opaque.rgba8.size(); i += 4)
    {
        opaque.rgba8[i] = std::byte{ 255 };
    }
    auto blocks = EncodeBlocks(opaque, XboxTextureFormat::Dxt1);
    if (!blocks || blocks->size() + 128 != TerrainDdsByteCount(role))
    {
        return std::unexpected(Error::EncodeFailed);
    }
    const auto             header = makeHeader(side);
    std::vector<std::byte> bytes(header.begin(), header.end());
    bytes.insert(bytes.end(), blocks->begin(), blocks->end());
    return bytes;
}
catch (const std::bad_alloc&)
{
    return std::unexpected(Error::EncodeFailed);
}

std::expected<std::span<const std::byte>, TerrainDdsError>
ParseTerrainDds(TerrainTexture role, std::span<const std::byte> bytes)
{
    if (bytes.size() != TerrainDdsByteCount(role))
    {
        return std::unexpected(Error::LengthMismatch);
    }
    if (!std::ranges::equal(bytes.first(128), makeHeader(TerrainTextureSide(role))))
    {
        return std::unexpected(Error::HeaderMismatch);
    }
    return bytes.subspan(128);
}

std::expected<void, TerrainDdsError>
WriteTerrainDds(TerrainTexture role, std::span<const std::byte> bytes, const std::filesystem::path& destination)
{
    const auto blocks = ParseTerrainDds(role, bytes);
    if (!blocks)
    {
        return std::unexpected(blocks.error());
    }
    const auto published = detail::PublishDds(bytes, destination, [&](std::span<const std::byte> readBack)
                                              {
                                                  const auto parsed = ParseTerrainDds(role, readBack);
                                                  return parsed && std::ranges::equal(*parsed, *blocks);
                                              });
    if (!published)
    {
        return std::unexpected(publicationError(published.error()));
    }
    return {};
}

} // namespace rerevved::tools
