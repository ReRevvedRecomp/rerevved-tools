#pragma once

#include "map_document.h"
#include "texture_import.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rerevved::tools
{

// The three named map terrain images built from a tile core. Image tile (row i,
// column j) covers file tile (row j, column i), as in the shipped DLC maps.
struct GeneratedTerrain
{
    std::vector<std::uint16_t> heights;  // 512 x 512, row major
    Rgba8Image                 blends;   // 2048 x 2048, opaque
    Rgba8Image                 lightmap; // 4096 x 4096, opaque
};

// Deterministic for a given core. Throws std::bad_alloc when the images cannot be allocated.
[[nodiscard]] GeneratedTerrain GenerateTerrainFromTiles(std::span<const std::byte, kMapCoreSize> core);

} // namespace rerevved::tools
