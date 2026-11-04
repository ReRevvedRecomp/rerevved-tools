#include "map/map_images.h"

#include "heights_dds.h"
#include "terrain_generator.h"
#include "texture_import.h"
#include "xbox_texture.h"

#include <algorithm>
#include <new>
#include <utility>

namespace rerevved::tools
{

MapHeightsSource MakeHeightsSource(std::vector<std::uint16_t> samples)
{
    MapHeightsSource source;
    const auto       encoded = EncodeHeightsDds(samples);
    const auto       parsed  = encoded ? ParseHeightsDds(*encoded) : std::unexpected(encoded.error());
    source.readBackPassed    = parsed && *parsed == samples;
    source.samples           = std::move(samples);
    return source;
}

std::expected<MapTerrainSource, std::string> MakeTerrainSource(TerrainTexture role, const Rgba8Image& image)
try
{
    const auto side    = TerrainTextureSide(role);
    auto       encoded = EncodeTerrainDds(role, image);
    if (!encoded)
    {
        return std::unexpected(std::string(TerrainDdsErrorMessage(encoded.error())));
    }
    MapTerrainSource source;
    const auto       blocks = ParseTerrainDds(role, *encoded);
    source.readBackPassed   = blocks.has_value() && DecodeBlocks(*blocks, side, side, XboxTextureFormat::Dxt1).has_value();
    source.dds              = std::move(*encoded);
    return source;
}
catch (const std::bad_alloc&)
{
    return std::unexpected(std::string(TextureImportErrorMessage(TextureImportError::AllocationFailed)));
}

std::expected<GeneratedMapImages, std::string> BuildMapImages(const MapDocument& tiles)
try
{
    auto generated        = GenerateTerrainFromTiles(tiles.core);
    auto heights          = MakeHeightsSource(std::move(generated.heights));
    heights.generatedFrom = tiles.core;
    auto blends           = MakeTerrainSource(TerrainTexture::MountainHillBlends, generated.blends);
    auto lightmap         = MakeTerrainSource(TerrainTexture::Lightmap, generated.lightmap);
    if (!blends || !lightmap)
    {
        return std::unexpected(!blends ? blends.error() : lightmap.error());
    }
    blends->generatedFrom   = tiles.core;
    lightmap->generatedFrom = tiles.core;
    return GeneratedMapImages{ std::move(heights), std::move(*blends), std::move(*lightmap) };
}
catch (const std::bad_alloc&)
{
    return std::unexpected("There is not enough memory to generate the map images.");
}

void ApplyMapImages(CreatorWorkspace& workspace, GeneratedMapImages images)
{
    // Each update changes the revision, so every call must read it again.
    static_cast<void>(workspace.SetMapHeights(workspace.Map().revision, std::move(images.heights)));
    static_cast<void>(workspace.SetMapTerrain(workspace.Map().revision, TerrainTexture::MountainHillBlends, std::move(images.blends)));
    static_cast<void>(workspace.SetMapTerrain(workspace.Map().revision, TerrainTexture::Lightmap, std::move(images.lightmap)));
}

std::expected<void, std::string> GenerateMapTerrain(CreatorWorkspace& workspace, const MapDocument& tiles)
{
    auto images = BuildMapImages(tiles);
    if (!images)
    {
        return std::unexpected(std::move(images.error()));
    }
    ApplyMapImages(workspace, std::move(*images));
    return {};
}

} // namespace rerevved::tools
