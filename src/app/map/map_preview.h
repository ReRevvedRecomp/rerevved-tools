#pragma once

#include "map_document.h"

#include <cstdint>
#include <string>
#include <string_view>

struct ImVec2;

namespace rerevved::tools
{

// rawBits adds the 0x10, 0x20, 0x40 and 0x80 marks the inspector shows.
void DrawMapTile(ImVec2 minimum, float cellSize, std::byte value, bool gridLines, bool rawBits = true);
void DrawMapThumbnail(const MapDocument& document, ImVec2 minimum, float size);
// width 0 uses the available content width.
void DrawMapTileLegend(float width = 0.0F);
void DrawMapTileTooltip(std::size_t offset, std::byte value, bool rawBits = true);
// "Plains" for the eight game terrains. Other codes have no terrain name.
[[nodiscard]] std::string MapTerrainLabel(std::uint8_t code);
void                      DrawMapPreview(const MapDocument* document, std::string_view error);

} // namespace rerevved::tools
