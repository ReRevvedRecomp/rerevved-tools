#pragma once

#include "fpk_table.h"
#include "map_document.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

// Starting lands follow the map types of Civilization V, fitted to 32 x 32 tiles.
enum class StartingLand
{
    Blank,
    Pangaea,
    Continents,
    SmallContinents,
    Oval,
    Fractal,
    Ring,
    Donut,
    Skirmish,
    Terra,
    TiltedAxis,
    Shuffle,
    Archipelago,
    LargeIslands,
    TinyIslands,
    Hemispheres,
    FourCorners,
    InlandSea,
    Lakes,
    NorthVsSouth,
    WestVsEast,
    Highlands,
    Arborea,
    Sandstorm,
    IceAge,
    Frontier,
    GreatPlains,
    Boreal,
    Earth,
    JapaneseMainland,
    BritishIsles,
    Mediterranean,
    Americas,
    Europe,
    Africa,
    Australia,
    Caribbean,
};

enum class StartingLandGroup
{
    Landmasses,
    Islands,
    InlandWater,
    Matchups,
    Climates,
    RealWorld,
};

struct StartingLandInfo
{
    StartingLand      land;
    StartingLandGroup group;
    std::string_view  name;
};

inline constexpr std::array kStartingLands{
    StartingLandInfo{ StartingLand::Blank, StartingLandGroup::Landmasses, "Blank" },
    StartingLandInfo{ StartingLand::Pangaea, StartingLandGroup::Landmasses, "Pangaea" },
    StartingLandInfo{ StartingLand::Continents, StartingLandGroup::Landmasses, "Continents" },
    StartingLandInfo{ StartingLand::SmallContinents, StartingLandGroup::Landmasses, "Small Continents" },
    StartingLandInfo{ StartingLand::Oval, StartingLandGroup::Landmasses, "Oval" },
    StartingLandInfo{ StartingLand::Fractal, StartingLandGroup::Landmasses, "Fractal" },
    StartingLandInfo{ StartingLand::Ring, StartingLandGroup::InlandWater, "Ring" },
    StartingLandInfo{ StartingLand::Donut, StartingLandGroup::InlandWater, "Donut" },
    StartingLandInfo{ StartingLand::Skirmish, StartingLandGroup::Matchups, "Skirmish" },
    StartingLandInfo{ StartingLand::Terra, StartingLandGroup::Landmasses, "Terra" },
    StartingLandInfo{ StartingLand::TiltedAxis, StartingLandGroup::Landmasses, "Tilted Axis" },
    StartingLandInfo{ StartingLand::Shuffle, StartingLandGroup::Landmasses, "Shuffle" },
    StartingLandInfo{ StartingLand::Archipelago, StartingLandGroup::Islands, "Archipelago" },
    StartingLandInfo{ StartingLand::LargeIslands, StartingLandGroup::Islands, "Large Islands" },
    StartingLandInfo{ StartingLand::TinyIslands, StartingLandGroup::Islands, "Tiny Islands" },
    StartingLandInfo{ StartingLand::Hemispheres, StartingLandGroup::Matchups, "Hemispheres" },
    StartingLandInfo{ StartingLand::FourCorners, StartingLandGroup::Islands, "Four Corners" },
    StartingLandInfo{ StartingLand::InlandSea, StartingLandGroup::InlandWater, "Inland Sea" },
    StartingLandInfo{ StartingLand::Lakes, StartingLandGroup::InlandWater, "Lakes" },
    StartingLandInfo{ StartingLand::NorthVsSouth, StartingLandGroup::Matchups, "North vs South" },
    StartingLandInfo{ StartingLand::WestVsEast, StartingLandGroup::Matchups, "West vs East" },
    StartingLandInfo{ StartingLand::Highlands, StartingLandGroup::Climates, "Highlands" },
    StartingLandInfo{ StartingLand::Arborea, StartingLandGroup::Climates, "Arborea" },
    StartingLandInfo{ StartingLand::Sandstorm, StartingLandGroup::Climates, "Sandstorm" },
    StartingLandInfo{ StartingLand::IceAge, StartingLandGroup::Climates, "Ice Age" },
    StartingLandInfo{ StartingLand::Frontier, StartingLandGroup::Climates, "Frontier" },
    StartingLandInfo{ StartingLand::GreatPlains, StartingLandGroup::Climates, "Great Plains" },
    StartingLandInfo{ StartingLand::Boreal, StartingLandGroup::Climates, "Boreal" },
    StartingLandInfo{ StartingLand::Earth, StartingLandGroup::RealWorld, "Earth" },
    StartingLandInfo{ StartingLand::JapaneseMainland, StartingLandGroup::RealWorld, "Japanese Mainland" },
    StartingLandInfo{ StartingLand::BritishIsles, StartingLandGroup::RealWorld, "British Isles" },
    StartingLandInfo{ StartingLand::Mediterranean, StartingLandGroup::RealWorld, "Mediterranean" },
    StartingLandInfo{ StartingLand::Americas, StartingLandGroup::RealWorld, "Americas" },
    StartingLandInfo{ StartingLand::Europe, StartingLandGroup::RealWorld, "Europe" },
    StartingLandInfo{ StartingLand::Africa, StartingLandGroup::RealWorld, "Africa" },
    StartingLandInfo{ StartingLand::Australia, StartingLandGroup::RealWorld, "Australia" },
    StartingLandInfo{ StartingLand::Caribbean, StartingLandGroup::RealWorld, "Caribbean" },
};

static_assert(
    []
    {
        for (std::size_t index = 0; index < kStartingLands.size(); ++index)
        {
            if (static_cast<std::size_t>(kStartingLands[index].land) != index)
            {
                return false;
            }
        }
        return true;
    }(),
    "kStartingLands lists every land in StartingLand order");

[[nodiscard]] constexpr const StartingLandInfo& StartingLandOf(StartingLand land) noexcept
{
    return kStartingLands[static_cast<std::size_t>(land)];
}

// Places have fixed coastlines, Shuffle picks its own size and Blank has no
// land. The others grow with LandSize.
[[nodiscard]] constexpr bool StartingLandUsesSize(StartingLand land) noexcept
{
    return land != StartingLand::Blank && land != StartingLand::Shuffle && StartingLandOf(land).group != StartingLandGroup::RealWorld;
}

enum class LandSize
{
    Small,
    Medium,
    Large,
};

// Every retail map has five start markers.
inline constexpr int kStartingLandPlayers = 5;
inline constexpr int kMinimumPlayers      = 2;
inline constexpr int kMaximumPlayers      = 7;

// Directions are as the game shows the map, not file order.
enum class MapMirror
{
    Off,
    LeftToRight,
    TopToBottom,
};

inline constexpr std::uint32_t kFirstLayoutSeed = 1000;

// Whether the player picks the mirror. Skirmish always mirrors left to right.
[[nodiscard]] constexpr bool StartingLandMirrors(StartingLand land) noexcept
{
    return land == StartingLand::Shuffle || (StartingLandUsesSize(land) && land != StartingLand::Terra && land != StartingLand::Skirmish);
}

// Measured from the retail maps per terrain code, then widened for terrains the
// land's climate favours. A group limit of 0 means the climate leaves it out.
struct TerrainLimits
{
    std::array<int, 8>   group{};   // largest four-connected group
    std::array<int, 8>   run{};     // longest straight run
    std::array<float, 8> squares{}; // two by two blocks per map on average
    std::array<int, 8>   block{};   // side of the smallest solid square not allowed
};

[[nodiscard]] TerrainLimits TerrainLimitsOf(StartingLand land);

// Deterministic for its arguments. Polar ice fills file columns 0-1 and 30-31,
// as on retail maps.
[[nodiscard]] MapDocument   GenerateStartingLand(StartingLand land, LandSize size, std::uint32_t seed, MapMirror mirror = MapMirror::Off, int players = kStartingLandPlayers, bool teams = false);
[[nodiscard]] std::uint32_t NextLayoutSeed(std::uint32_t seed) noexcept;

// Keeps sea, polar ice, unnamed codes and every tile's other bits.
void RandomizeLandTerrain(MapDocument& document, std::uint32_t seed);

struct MapPackUsage
{
    std::vector<std::string>   packIds;
    std::vector<std::int32_t>  scenarioIds;
    std::vector<std::string>   mapNames;
    std::vector<std::uint32_t> slots;
    // False when a folder, manifest or archive could not be read in full.
    bool complete = true;
};

// Links and junctions inside mapsRoot are not followed.
void AddMapPackUsage(MapPackUsage& usage, const std::filesystem::path& mapsRoot);

struct MapIdentity
{
    std::string  title;
    std::string  packId;
    std::int32_t scenarioId = 100;
    std::string  mapName; // empty when all 26 letters are in use
    bool         packIdSuffixed = false;
    // Empty when every scenario file slot is in use.
    std::optional<std::uint32_t> slot;
};

[[nodiscard]] std::string MapPackIdFromName(std::string_view name);
// Empty when the name can be a scenario title.
[[nodiscard]] std::string MapNameProblem(std::string_view name);
[[nodiscard]] MapIdentity SuggestMapIdentity(std::string_view name, const MapPackUsage& usage);
[[nodiscard]] MapIdentity RepickClashingIdentity(const MapIdentity& current, const MapPackUsage& folder, const MapPackUsage& everything);

} // namespace rerevved::tools
