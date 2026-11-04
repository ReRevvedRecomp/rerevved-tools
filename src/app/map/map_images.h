#pragma once

#include "map/creator_workspace.h"
#include "map_document.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace rerevved::tools
{

// The caller sets the origin.
[[nodiscard]] MapHeightsSource MakeHeightsSource(std::vector<std::uint16_t> samples);
// The caller sets the origin.
[[nodiscard]] std::expected<MapTerrainSource, std::string> MakeTerrainSource(TerrainTexture role, const Rgba8Image& image);

struct GeneratedMapImages
{
    MapHeightsSource heights;
    MapTerrainSource blends;
    MapTerrainSource lightmap;
};

// It reads only tiles, so it can run on any thread against a copy.
[[nodiscard]] std::expected<GeneratedMapImages, std::string> BuildMapImages(const MapDocument& tiles);
void                                                         ApplyMapImages(CreatorWorkspace& workspace, GeneratedMapImages images);

// BuildMapImages then ApplyMapImages on the calling thread.
[[nodiscard]] std::expected<void, std::string> GenerateMapTerrain(CreatorWorkspace& workspace, const MapDocument& tiles);

} // namespace rerevved::tools
