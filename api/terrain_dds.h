#pragma once

#include "texture_import.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

// The two DXT1 named map terrain roles. Every shipped file of a role has one
// square size, one linear little endian DXT1 level and a 128 byte header.
enum class TerrainTexture
{
    MountainHillBlends,
    Lightmap,
};

inline constexpr std::uint32_t kMountainHillBlendsSide = 2048;
inline constexpr std::uint32_t kLightmapSide           = 4096;

enum class TerrainDdsError
{
    DimensionMismatch,
    EncodeFailed,
    LengthMismatch,
    HeaderMismatch,
    DestinationExists,
    OpenFailed,
    WriteFailed,
    FinalizationFailed,
    ReadBackFailed,
};

[[nodiscard]] std::string_view TerrainDdsErrorMessage(TerrainDdsError error);
[[nodiscard]] std::string_view TerrainTextureLabel(TerrainTexture role);
[[nodiscard]] std::uint32_t    TerrainTextureSide(TerrainTexture role);
[[nodiscard]] std::size_t      TerrainDdsByteCount(TerrainTexture role);

// Encodes an opaque image of the role's exact side. Alpha is ignored because
// shipped files carry no transparency.
[[nodiscard]] std::expected<std::vector<std::byte>, TerrainDdsError>
EncodeTerrainDds(TerrainTexture role, const Rgba8Image& image);

// Accepts the role's length and the header most shipped files use.
[[nodiscard]] std::expected<std::span<const std::byte>, TerrainDdsError>
ParseTerrainDds(TerrainTexture role, std::span<const std::byte> bytes);

// Publishes encoded bytes without overwriting, verifying them by readback.
[[nodiscard]] std::expected<void, TerrainDdsError>
WriteTerrainDds(TerrainTexture role, std::span<const std::byte> bytes, const std::filesystem::path& destination);

} // namespace rerevved::tools
