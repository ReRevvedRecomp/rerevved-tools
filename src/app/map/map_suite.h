#pragma once

#include "map/map_pack_export.h"
#include "map/map_wizard.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

struct MapSuiteMap
{
    std::size_t                 line    = 0;
    StartingLand                land    = StartingLand::Pangaea;
    LandSize                    size    = LandSize::Medium;
    MapMirror                   mirror  = MapMirror::Off;
    int                         players = kStartingLandPlayers;
    std::uint32_t               seed    = kFirstLayoutSeed;
    std::int32_t                id      = 0;
    std::string                 map;
    std::string                 title;
    std::string                 description;
    std::optional<std::int16_t> displayCard;   // the DISPLAYCARD variator
    bool                        teams = false; // each half's starts come first in file order
    std::optional<bool>         multiplayer;   // also list the map in multiplayer
    // Imported instead of generating a map. Its grid, MAP, ID and texts stay unless the plan sets them.
    std::filesystem::path        source;
    std::optional<std::uint32_t> randomize; // rerolls an imported map's land terrain with this seed
    std::optional<MapDocument>   sourceTiles;
    std::optional<ScenarioEntry> sourceEntry;
};

struct MapSuitePack
{
    std::size_t              line = 0;
    MapPackSettings          settings;
    std::vector<MapSuiteMap> maps;
};

// One key = value per line, [pack] sections each followed by their [map] sections. Lines starting with #
// are comments. teams = yes needs a mirror and an even player count.
[[nodiscard]] std::expected<std::vector<MapSuitePack>, std::string> ParseMapSuitePlan(std::string_view text);

// Relative to base. ExportMapSuite needs them read.
[[nodiscard]] std::expected<void, std::string> ResolveMapSuiteSources(std::span<MapSuitePack> plan, const std::filesystem::path& base);

// Repeats within the plan or already in use. Empty when there are none.
[[nodiscard]] std::vector<std::string> MapSuiteClashes(std::span<const MapSuitePack> plan, const MapPackUsage& usage);

// Keeps every pack under GitHub's 100 MiB file limit.
inline constexpr std::size_t kMapSuiteMaxPackBytes = 95 * 1024 * 1024;

// Assembles every pack before writing any. Never replaces a folder or file. With a maps folder, also saves
// each map there as <pack id>--<MAP>.rrmap for the editor.
[[nodiscard]] std::expected<std::vector<MapPackReceipt>, std::string> ExportMapSuite(std::span<const MapSuitePack> plan, const std::filesystem::path& outputRoot, const MapPackUsage& usage, const std::filesystem::path& mapsFolder = {});

} // namespace rerevved::tools
