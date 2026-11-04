#include "fpk_writer.h"
#include "map/map_pack_export.h"
#include "map/map_wizard.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

using namespace rerevved::tools;

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct TemporaryDirectory
{
    std::filesystem::path path;

    TemporaryDirectory()
    {
        static unsigned int sequence = 0;
        for (;;)
        {
            const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
            path             = std::filesystem::temp_directory_path() / ("rerevved-map-wizard-test-" + std::to_string(token) + "-" + std::to_string(sequence++));
            std::error_code error;
            if (std::filesystem::create_directory(path, error))
            {
                break;
            }
            if (error && error != std::errc::file_exists)
            {
                throw std::runtime_error("Could not create unique map wizard test directory.");
            }
        }
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        expect(!error, "test temporary directory is removed");
    }
};

std::uint8_t terrainAt(const MapDocument& map, int row, int column)
{
    return std::to_integer<std::uint8_t>(map.core[static_cast<std::size_t>(row * 32 + column)]) & 0x0FU;
}

bool markerAt(const MapDocument& map, std::size_t index)
{
    return (std::to_integer<std::uint8_t>(map.core[index]) & 0x20U) != 0;
}

// Grassland or plains under the start, with two grassland, sea and forest tiles among the eight around it.
bool fairStart(const MapDocument& map, std::size_t index)
{
    const int           row    = static_cast<int>(index / 32);
    const int           column = static_cast<int>(index % 32);
    std::array<int, 16> around{};
    for (int dr = -1; dr <= 1; ++dr)
    {
        for (int dc = -1; dc <= 1; ++dc)
        {
            if ((dr != 0 || dc != 0) && row + dr >= 0 && row + dr < 32 && column + dc >= 0 && column + dc < 32)
            {
                ++around[terrainAt(map, row + dr, column + dc)];
            }
        }
    }
    const auto under = terrainAt(map, row, column);
    return (under == 1 || under == 2) && around[1] >= 2 && around[0] >= 2 && around[3] >= 2;
}

int siteScore(const MapDocument& map, std::size_t index)
{
    const int row    = static_cast<int>(index / 32);
    const int column = static_cast<int>(index % 32);
    int       forest = 0;
    int       flat   = 0;
    int       water  = 0;
    for (int dr = -1; dr <= 1; ++dr)
    {
        for (int dc = -1; dc <= 1; ++dc)
        {
            if ((dr != 0 || dc != 0) && row + dr >= 0 && row + dr < 32 && column + dc >= 0 && column + dc < 32)
            {
                const auto code = terrainAt(map, row + dr, column + dc);
                forest += code == 3 ? 1 : 0;
                flat += code == 1 || code == 2 ? 1 : 0;
                water += code == 0 || code == 5 ? 1 : 0;
            }
        }
    }
    return std::min(forest, 2) + std::min(flat, 2) + std::min(water, 2);
}

int gapBetween(std::size_t left, std::size_t right)
{
    return std::max(std::abs(static_cast<int>(left / 32) - static_cast<int>(right / 32)), std::abs(static_cast<int>(left % 32) - static_cast<int>(right % 32)));
}

// Retail DLC maps, outside Oasis World's desert and three maps' mountain walls.
constexpr std::array<std::pair<int, int>, 7> kRetailLimits{ { { 0, 0 }, { 11, 6 }, { 9, 7 }, { 10, 6 }, { 4, 3 }, { 10, 5 }, { 16, 7 } } };

// With band, desert in columns 13-18 is North vs South's band and left out.
std::string retailGrouping(const MapDocument& map, bool band = false, const TerrainLimits& limits = TerrainLimitsOf(StartingLand::Pangaea))
{
    const auto terrainAt = [&](const MapDocument& grid, int row, int column)
    {
        const auto code = ::terrainAt(grid, row, column);
        return band && code == 5 && column >= 13 && column <= 18 ? std::uint8_t{ 0 } : code;
    };
    std::vector<bool> seen(kMapCoreSize);
    for (std::size_t start = 0; start < kMapCoreSize; ++start)
    {
        const auto code = terrainAt(map, static_cast<int>(start / 32), static_cast<int>(start % 32));
        if (seen[start] || code < 1 || code > 6)
        {
            continue;
        }
        int                      size = 0;
        std::vector<std::size_t> stack{ start };
        seen[start] = true;
        while (!stack.empty())
        {
            const auto index = stack.back();
            stack.pop_back();
            ++size;
            for (const auto [dr, dc] : { std::pair{ -1, 0 }, std::pair{ 1, 0 }, std::pair{ 0, -1 }, std::pair{ 0, 1 } })
            {
                const int r = static_cast<int>(index / 32) + dr;
                const int c = static_cast<int>(index % 32) + dc;
                if (r >= 0 && r < 32 && c >= 0 && c < 32 && !seen[static_cast<std::size_t>(r * 32 + c)] && terrainAt(map, r, c) == code)
                {
                    seen[static_cast<std::size_t>(r * 32 + c)] = true;
                    stack.push_back(static_cast<std::size_t>(r * 32 + c));
                }
            }
        }
        if (size > limits.group[code])
        {
            return "terrain " + std::to_string(code) + " group of " + std::to_string(size);
        }
    }
    for (int line = 0; line < 32; ++line)
    {
        for (const bool down : { false, true })
        {
            int run = 0;
            for (int k = 0; k < 32; ++k)
            {
                const auto code = down ? terrainAt(map, k, line) : terrainAt(map, line, k);
                const auto last = k == 0 ? 0xFF : (down ? terrainAt(map, k - 1, line) : terrainAt(map, line, k - 1));
                run             = code == last ? run + 1 : 1;
                if (code >= 1 && code <= 6 && run > limits.run[code])
                {
                    return "terrain " + std::to_string(code) + " run of " + std::to_string(run);
                }
            }
        }
    }
    std::array<int, 4> squares{};
    for (int row = 0; row < 31; ++row)
    {
        for (int column = 0; column < 31; ++column)
        {
            const auto code = terrainAt(map, row, column);
            if (code >= 1 && code <= 3 && terrainAt(map, row + 1, column) == code && terrainAt(map, row, column + 1) == code && terrainAt(map, row + 1, column + 1) == code &&
                ++squares[code] > static_cast<int>(std::ceil(limits.squares[code])))
            {
                return "terrain " + std::to_string(code) + " square";
            }
        }
    }
    for (int row = 0; row < 32; ++row)
    {
        for (int column = 0; column < 32; ++column)
        {
            const auto code  = terrainAt(map, row, column);
            const int  side  = code >= 1 && code <= 6 ? limits.block[code] : 0;
            bool       block = side > 0 && row + side <= 32 && column + side <= 32;
            for (int dr = 0; dr < side && block; ++dr)
            {
                for (int dc = 0; dc < side && block; ++dc)
                {
                    block = terrainAt(map, row + dr, column + dc) == code;
                }
            }
            if (block)
            {
                return "terrain " + std::to_string(code) + " block";
            }
        }
    }
    return {};
}

std::size_t landRegions(const MapDocument& map)
{
    std::vector<bool> seen(kMapCoreSize);
    std::size_t       regions = 0;
    for (std::size_t start = 0; start < kMapCoreSize; ++start)
    {
        const auto code = terrainAt(map, static_cast<int>(start / 32), static_cast<int>(start % 32));
        if (seen[start] || code == 0 || code == 7)
        {
            continue;
        }
        ++regions;
        std::vector<std::size_t> stack{ start };
        seen[start] = true;
        while (!stack.empty())
        {
            const auto index = stack.back();
            stack.pop_back();
            const int row    = static_cast<int>(index / 32);
            const int column = static_cast<int>(index % 32);
            for (const auto [dr, dc] : { std::pair{ -1, 0 }, std::pair{ 1, 0 }, std::pair{ 0, -1 }, std::pair{ 0, 1 } })
            {
                const int r = row + dr;
                const int c = column + dc;
                if (r < 0 || r >= 32 || c < 0 || c >= 32)
                {
                    continue;
                }
                const auto next = static_cast<std::size_t>(r * 32 + c);
                const auto kind = terrainAt(map, r, c);
                if (!seen[next] && kind != 0 && kind != 7)
                {
                    seen[next] = true;
                    stack.push_back(next);
                }
            }
        }
    }
    return regions;
}

std::size_t landCount(const MapDocument& map)
{
    std::size_t count = 0;
    for (int row = 0; row < 32; ++row)
    {
        for (int column = 2; column < 30; ++column)
        {
            count += terrainAt(map, row, column) != 0 ? 1U : 0U;
        }
    }
    return count;
}

std::size_t enclosedWater(const MapDocument& map)
{
    std::vector<bool>        open(kMapCoreSize);
    std::vector<std::size_t> stack;
    for (std::size_t index = 0; index < kMapCoreSize; ++index)
    {
        const int row = static_cast<int>(index / 32);
        if ((row == 0 || row == 31) && terrainAt(map, row, static_cast<int>(index % 32)) == 0)
        {
            open[index] = true;
            stack.push_back(index);
        }
    }
    while (!stack.empty())
    {
        const auto index = stack.back();
        stack.pop_back();
        for (const auto [dr, dc] : { std::pair{ -1, 0 }, std::pair{ 1, 0 }, std::pair{ 0, -1 }, std::pair{ 0, 1 } })
        {
            const int  r    = static_cast<int>(index / 32) + dr;
            const int  c    = static_cast<int>(index % 32) + dc;
            const auto next = static_cast<std::size_t>(r * 32 + c);
            if (r >= 0 && r < 32 && c >= 0 && c < 32 && !open[next] && (terrainAt(map, r, c) == 0 || terrainAt(map, r, c) == 7))
            {
                open[next] = true;
                stack.push_back(next);
            }
        }
    }
    std::size_t count = 0;
    for (std::size_t index = 0; index < kMapCoreSize; ++index)
    {
        count += !open[index] && terrainAt(map, static_cast<int>(index / 32), static_cast<int>(index % 32)) == 0 ? 1U : 0U;
    }
    return count;
}

void testStartingLand()
{
    for (const auto& info : kStartingLands)
    {
        const auto land = info.land;
        for (const auto size : { LandSize::Small, LandSize::Medium, LandSize::Large })
        {
            for (const std::uint32_t seed : { 1U, 3776U, 4242U, 9999U })
            {
                const auto map   = GenerateStartingLand(land, size, seed);
                const auto again = GenerateStartingLand(land, size, seed);
                expect(map.core == again.core, "a layout is repeatable for its seed");
                bool        codes   = true;
                bool        ice     = true;
                std::size_t markers = 0;
                bool        placed  = true;
                for (std::size_t index = 0; index < kMapCoreSize; ++index)
                {
                    const auto value  = std::to_integer<std::uint8_t>(map.core[index]);
                    const int  column = static_cast<int>(index % 32);
                    codes             = codes && (value & 0xD8U) == 0;
                    // Ice Age's ice may also spread over the sea in columns 2-5 and 26-29.
                    const bool polar  = column < 2 || column > 29;
                    const bool spread = land == StartingLand::IceAge && (column < 6 || column > 25);
                    ice               = ice && (polar ? (value & 0x0FU) == 7 : spread || (value & 0x0FU) != 7);
                    if (markerAt(map, index))
                    {
                        ++markers;
                        placed = placed && fairStart(map, index);
                    }
                }
                expect(codes, "only terrain codes 0-7 and the start marker are written");
                expect(ice, "polar ice fills file columns 0-1 and 30-31 only");
                if (land == StartingLand::Blank)
                {
                    expect(landCount(map) == 0 && markers == 0, "a blank map has no land and no markers");
                    continue;
                }
                expect(markers == static_cast<std::size_t>(kStartingLandPlayers), "five start markers are placed");
                expect(placed, "start markers sit on grassland or plains with two grassland, sea and forest neighbours");
                const auto regions = landRegions(map);
                switch (land)
                {
                    case StartingLand::Pangaea:
                        expect(regions == 1, "Pangaea is one landmass");
                        break;
                    case StartingLand::Archipelago:
                        expect(regions >= 6, "an archipelago is many islands");
                        break;
                    case StartingLand::LargeIslands:
                    case StartingLand::Continents:
                        expect(regions >= 3, "the land is several landmasses");
                        break;
                    case StartingLand::TinyIslands:
                        expect(regions >= 10, "tiny islands are many landmasses");
                        break;
                    case StartingLand::Skirmish:
                    {
                        std::array<bool, kMapCoreSize> settled{};
                        for (std::size_t index = 0; index < kMapCoreSize; ++index)
                        {
                            for (int dr = -1; dr <= 1 && markerAt(map, index); ++dr)
                            {
                                for (int dc = -1; dc <= 1; ++dc)
                                {
                                    settled[static_cast<std::size_t>((static_cast<int>(index / 32) + dr) * 32 + static_cast<int>(index % 32) + dc)] = true;
                                }
                            }
                        }
                        for (int row = 0; row < 32; ++row)
                        {
                            for (int column = 0; column < 32; ++column)
                            {
                                const auto mirror = static_cast<std::size_t>((31 - row) * 32 + column);
                                if (!settled[static_cast<std::size_t>(row * 32 + column)] && !settled[mirror])
                                {
                                    expect(terrainAt(map, row, column) == terrainAt(map, 31 - row, column), "skirmish halves mirror each other");
                                }
                            }
                        }
                        break;
                    }
                    case StartingLand::Terra:
                        for (std::size_t index = 0; index < kMapCoreSize; ++index)
                        {
                            expect(!markerAt(map, index) || index / 32 >= 12, "Terra starts every player in the old world");
                        }
                        break;
                    case StartingLand::IceAge:
                        for (int row = 0; row < 32; ++row)
                        {
                            for (const int column : { 3, 4, 5, 6, 25, 26, 27, 28 })
                            {
                                expect(terrainAt(map, row, column) == 0 || terrainAt(map, row, column) == 7, "ice age land keeps near the equator");
                            }
                        }
                        break;
                    case StartingLand::Hemispheres:
                    case StartingLand::WestVsEast:
                        expect(regions >= 2, "a channel splits the west from the east");
                        for (int column = 0; column < 32; ++column)
                        {
                            expect(terrainAt(map, 15, column) == 0 || terrainAt(map, 15, column) == 7, "the middle file row is open water");
                        }
                        break;
                    case StartingLand::FourCorners:
                        expect(regions >= 4, "four corners are separate landmasses");
                        break;
                    case StartingLand::InlandSea:
                        expect(enclosedWater(map) >= 20, "an inland sea is enclosed by land");
                        break;
                    case StartingLand::Lakes:
                    case StartingLand::Donut:
                    case StartingLand::Ring:
                        expect(enclosedWater(map) > 0, "the land encloses water");
                        break;
                    case StartingLand::NorthVsSouth:
                        for (int row = 0; row < 32; ++row)
                        {
                            const auto middle = terrainAt(map, row, 15);
                            expect(middle == 0 || middle == 5, "desert crosses the equator");
                        }
                        break;
                    default:
                        break;
                }
                for (int row = 0; row < 32; ++row)
                {
                    for (int column = 2; column < 30; ++column)
                    {
                        // Ice Age's polar ice spreads over open sea, never onto land or beside it.
                        bool coast = false;
                        for (int dr = -1; dr <= 1; ++dr)
                        {
                            for (int dc = -1; dc <= 1; ++dc)
                            {
                                const auto next = row + dr >= 0 && row + dr < 32 ? terrainAt(map, row + dr, column + dc) : 0;
                                coast           = coast || (next >= 1 && next <= 6);
                            }
                        }
                        const bool ice = terrainAt(map, row, column) == 7;
                        expect(!ice || (land == StartingLand::IceAge && !coast), "land keeps a sea row beside the ice");
                    }
                    expect(land == StartingLand::IceAge || (terrainAt(map, row, 2) == 0 && terrainAt(map, row, 29) == 0), "land keeps a sea row beside the ice");
                }
                for (int column = 0; column < 32; ++column)
                {
                    const bool edgeSea = (terrainAt(map, 0, column) == 0 || terrainAt(map, 0, column) == 7) && (terrainAt(map, 31, column) == 0 || terrainAt(map, 31, column) == 7);
                    expect(edgeSea, "land keeps a sea row at the grid edge");
                }
            }
        }
        if (StartingLandUsesSize(land))
        {
            const auto small = landCount(GenerateStartingLand(land, LandSize::Small, 7));
            const auto large = landCount(GenerateStartingLand(land, LandSize::Large, 7));
            expect(small < large, "a larger size makes more land");
        }
        else if (info.group == StartingLandGroup::RealWorld)
        {
            const auto first  = GenerateStartingLand(land, LandSize::Small, 1);
            const auto second = GenerateStartingLand(land, LandSize::Large, 2);
            bool       same   = true;
            for (int row = 0; row < 32; ++row)
            {
                for (int column = 0; column < 32; ++column)
                {
                    same = same && ((terrainAt(first, row, column) == 0) == (terrainAt(second, row, column) == 0));
                }
            }
            for (const std::uint32_t seed : { 1000U, 3020U })
            {
                const auto crowded = GenerateStartingLand(land, LandSize::Medium, seed, MapMirror::Off, 7);
                for (int row = 0; row < 32; ++row)
                {
                    for (int column = 0; column < 32; ++column)
                    {
                        same = same && ((terrainAt(first, row, column) == 0) == (terrainAt(crowded, row, column) == 0));
                    }
                }
            }
            expect(same, "a fixed coastline ignores the size, seed and players");
            expect(first.core != second.core, "the seed still varies a fixed coastline's terrain");
        }
    }
    expect(GenerateStartingLand(StartingLand::Pangaea, LandSize::Medium, 1).core != GenerateStartingLand(StartingLand::Pangaea, LandSize::Medium, 2).core,
           "seeds change the layout");
    const auto next = NextLayoutSeed(1234);
    expect(next != 1234 && next >= 1000 && next <= 9999, "the next layout number has four digits");
}

void testMirrorsAndPlayers()
{
    const auto mirrorOf = [](MapMirror mirror, int row, int column)
    {
        const int across = mirror == MapMirror::TopToBottom ? row : 31 - row;
        const int down   = mirror == MapMirror::LeftToRight ? column : 31 - column;
        return static_cast<std::size_t>(across * 32 + down);
    };
    for (const auto& info : kStartingLands)
    {
        if (!StartingLandMirrors(info.land))
        {
            continue;
        }
        for (const auto mirror : { MapMirror::LeftToRight, MapMirror::TopToBottom })
        {
            for (const int players : { 2, 3, 4, 5, 7 })
            {
                for (const std::uint32_t seed : { 1U, 3776U })
                {
                    const auto  map       = GenerateStartingLand(info.land, LandSize::Medium, seed, mirror, players);
                    std::size_t markers   = 0;
                    std::size_t unpaired  = 0;
                    bool        fair      = true;
                    bool        symmetric = true;
                    for (int row = 0; row < 32; ++row)
                    {
                        for (int column = 0; column < 32; ++column)
                        {
                            const auto index = static_cast<std::size_t>(row * 32 + column);
                            const auto twin  = mirrorOf(mirror, row, column);
                            symmetric        = symmetric && terrainAt(map, row, column) == terrainAt(map, static_cast<int>(twin / 32), static_cast<int>(twin % 32));
                            if (markerAt(map, index))
                            {
                                ++markers;
                                fair = fair && fairStart(map, index);
                                unpaired += markerAt(map, twin) ? 0U : 1U;
                            }
                        }
                    }
                    expect(markers == static_cast<std::size_t>(players), "a mirrored map has one start per player");
                    expect(fair, "mirrored starts keep two grassland, sea and forest neighbours");
                    expect(symmetric, "a mirrored map's halves match tile for tile");
                    expect(unpaired == static_cast<std::size_t>(players % 2), "starts pair up, and an odd player starts alone on the mirror line");
                }
            }
        }
    }
    for (const int players : { 2, 7 })
    {
        const auto  map     = GenerateStartingLand(StartingLand::Pangaea, LandSize::Small, 1, MapMirror::Off, players);
        std::size_t markers = 0;
        bool        fair    = true;
        for (std::size_t index = 0; index < kMapCoreSize; ++index)
        {
            if (markerAt(map, index))
            {
                ++markers;
                fair = fair && fairStart(map, index);
            }
        }
        expect(markers == static_cast<std::size_t>(players) && fair, "an unmirrored map places one fair start per player");
    }
    expect(GenerateStartingLand(StartingLand::Earth, LandSize::Medium, 1, MapMirror::LeftToRight).core == GenerateStartingLand(StartingLand::Earth, LandSize::Medium, 1).core,
           "places ignore the mirror");
    // Teams: in file order, the first half's markers before every mirror.
    for (const auto& [land, mirror] : { std::pair{ StartingLand::NorthVsSouth, MapMirror::TopToBottom }, std::pair{ StartingLand::WestVsEast, MapMirror::LeftToRight }, std::pair{ StartingLand::Pangaea, MapMirror::TopToBottom } })
    {
        for (const int players : { 2, 4, 6 })
        {
            const auto       map = GenerateStartingLand(land, LandSize::Medium, kFirstLayoutSeed, mirror, players, true);
            std::vector<int> sides;
            for (std::size_t index = 0; index < kMapCoreSize; ++index)
            {
                if (markerAt(map, index))
                {
                    sides.push_back(static_cast<int>(mirror == MapMirror::TopToBottom ? index % 32 : index / 32) < 16 ? 0 : 1);
                }
            }
            expect(sides.size() == static_cast<std::size_t>(players) && std::ranges::is_sorted(sides) && sides[players / 2 - 1] == 0 && sides[players / 2] == 1,
                   "team markers fill the first half before the second in file order");
        }
    }
}

void testSpreadAndSites()
{
    struct Layout
    {
        MapMirror mirror;
        int       players;
        bool      teams;
    };

    for (const auto& info : kStartingLands)
    {
        if (info.land == StartingLand::Blank || info.land == StartingLand::Shuffle)
        {
            continue;
        }
        std::vector<Layout> layouts{ { MapMirror::Off, kStartingLandPlayers, false } };
        if (StartingLandMirrors(info.land) || info.land == StartingLand::Skirmish)
        {
            layouts.push_back({ MapMirror::LeftToRight, 2, false });
            layouts.push_back({ MapMirror::LeftToRight, 4, true });
        }
        for (const auto& layout : layouts)
        {
            for (const std::uint32_t seed : { kFirstLayoutSeed, 3776U })
            {
                const auto               map = GenerateStartingLand(info.land, LandSize::Medium, seed, layout.mirror, layout.players, layout.teams);
                std::vector<std::size_t> starts;
                for (std::size_t index = 0; index < kMapCoreSize; ++index)
                {
                    if (markerAt(map, index))
                    {
                        starts.push_back(index);
                    }
                }
                int gap = 32;
                for (std::size_t a = 0; a < starts.size(); ++a)
                {
                    for (std::size_t b = a + 1; b < starts.size(); ++b)
                    {
                        gap = std::min(gap, gapBetween(starts[a], starts[b]));
                    }
                }
                expect(gap >= 4, "starts keep at least four tiles apart");
                if (info.land == StartingLand::Pangaea && layout.mirror == MapMirror::Off)
                {
                    expect(landRegions(map) == 1, "Pangaea stays one landmass with its starts and sites");
                }
                if (layout.players == 2)
                {
                    expect(starts.size() == 2 && gapBetween(starts[0], starts[1]) >= 12, "a duel's starts lie far apart");
                }
                if (layout.teams && starts.size() == 4)
                {
                    expect(gapBetween(starts[0], starts[1]) < std::min(gapBetween(starts[0], starts[2]), gapBetween(starts[0], starts[3])),
                           "teammates start closer to each other than to their opponents");
                }
                const auto nearest = [&](std::size_t index)
                {
                    return *std::ranges::min_element(starts, {}, [&](std::size_t start)
                                                     {
                                                         return gapBetween(index, start);
                                                     });
                };
                bool sited = true;
                for (const auto start : starts)
                {
                    std::size_t              land = 0;
                    std::vector<std::size_t> sites;
                    for (std::size_t index = 0; index < kMapCoreSize; ++index)
                    {
                        const auto code = terrainAt(map, static_cast<int>(index / 32), static_cast<int>(index % 32));
                        if (code == 0 || code == 7 || nearest(index) != start)
                        {
                            continue;
                        }
                        land += gapBetween(index, start) > 2 ? 1 : 0;
                        const bool apart = std::ranges::all_of(starts, [&](std::size_t other)
                                                               {
                                                                   return gapBetween(index, other) >= 3;
                                                               });
                        if (code != 6 && apart && siteScore(map, index) == 6)
                        {
                            sites.push_back(index);
                        }
                    }
                    const auto fits = [&](const auto& self, std::size_t from, std::vector<std::size_t>& taken, std::size_t wanted) -> bool
                    {
                        if (taken.size() == wanted)
                        {
                            return true;
                        }
                        for (std::size_t next = from; next < sites.size(); ++next)
                        {
                            if (std::ranges::all_of(taken, [&](std::size_t other)
                                                    {
                                                        return gapBetween(sites[next], other) >= 3;
                                                    }))
                            {
                                taken.push_back(sites[next]);
                                if (self(self, next + 1, taken, wanted))
                                {
                                    return true;
                                }
                                taken.pop_back();
                            }
                        }
                        return false;
                    };
                    std::vector<std::size_t> taken;
                    sited = sited && fits(fits, 0, taken, std::min<std::size_t>(3, land / 20));
                }
                expect(sited, "every start's region holds good city sites besides the start");
                const auto grouped = retailGrouping(map, info.land == StartingLand::NorthVsSouth, TerrainLimitsOf(info.land));
                expect(grouped.empty(), "land terrain stays within the retail group, run and block limits: " + grouped);
            }
        }
    }
}

void testClimateShares()
{
    for (const auto& info : kStartingLands)
    {
        const auto limits = TerrainLimitsOf(info.land);
        for (int code = 1; code <= 6; ++code)
        {
            const auto retail = kRetailLimits[static_cast<std::size_t>(code)];
            expect(limits.run[static_cast<std::size_t>(code)] >= retail.second && (limits.group[static_cast<std::size_t>(code)] >= retail.first || limits.group[static_cast<std::size_t>(code)] == 0),
                   "every land keeps at least the retail runs and groups, or leaves the terrain out");
            expect(info.group == StartingLandGroup::Climates || (limits.group[static_cast<std::size_t>(code)] == retail.first && limits.run[static_cast<std::size_t>(code)] == retail.second),
                   "lands outside the climates keep the retail groups and runs");
        }
    }

    // Each climate's own terrain, as a share of the tiles that are not sea, polar ice included.
    struct Target
    {
        StartingLand       land;
        std::array<int, 3> codes;
        float              low;
        float              high;
        const char*        what;
    };

    static constexpr std::array targets{
        Target{ StartingLand::Sandstorm, { 5, 0, 0 }, 0.35F, 0.50F, "Sandstorm is mostly desert" },
        Target{ StartingLand::GreatPlains, { 2, 0, 0 }, 0.22F, 0.45F, "Great Plains is heavy with plains" },
        Target{ StartingLand::GreatPlains, { 1, 2, 0 }, 0.42F, 0.60F, "Great Plains is about half grassland and plains" },
        Target{ StartingLand::Arborea, { 3, 0, 0 }, 0.30F, 0.60F, "Arborea is heavy with forest" },
        Target{ StartingLand::Highlands, { 4, 6, 0 }, 0.40F, 0.50F, "Highlands is about 45% hills and mountains" },
        Target{ StartingLand::Frontier, { 6, 0, 0 }, 0.25F, 0.35F, "Frontier is about 30% mountains" },
        Target{ StartingLand::Boreal, { 3, 4, 0 }, 0.45F, 0.55F, "Boreal is about 50% forest and hills" },
        Target{ StartingLand::Boreal, { 5, 0, 0 }, 0.0F, 0.0F, "Boreal has no desert" },
        Target{ StartingLand::IceAge, { 7, 0, 0 }, 0.50F, 0.60F, "Ice Age is 50-60% ice" },
    };
    for (const std::uint32_t seed : { kFirstLayoutSeed, 3776U })
    {
        for (const auto& target : targets)
        {
            const auto           map = GenerateStartingLand(target.land, LandSize::Medium, seed);
            std::array<float, 8> tiles{};
            float                ground = 0.0F;
            for (std::size_t index = 0; index < kMapCoreSize; ++index)
            {
                const auto code = terrainAt(map, static_cast<int>(index / 32), static_cast<int>(index % 32));
                if (code >= 1 && code <= 7)
                {
                    tiles[code] += 1.0F;
                    ground += 1.0F;
                }
            }
            float share = 0.0F;
            for (const int code : target.codes)
            {
                share += code == 0 ? 0.0F : tiles[static_cast<std::size_t>(code)] / ground;
            }
            expect(share >= target.low && share <= target.high, std::string(target.what) + ": " + std::to_string(share) + " at seed " + std::to_string(seed));
        }
        const auto plains = GenerateStartingLand(StartingLand::GreatPlains, LandSize::Medium, seed);
        const auto count  = [&](int code)
        {
            return std::ranges::count_if(plains.core, [&](std::byte value)
                                         {
                                             return (std::to_integer<int>(value) & 0x0F) == code;
                                         });
        };
        expect(2 * count(2) > 3 * count(3), "Great Plains has clearly more plains than forest");
    }
}

void testRandomizeTerrain()
{
    auto        map     = GenerateStartingLand(StartingLand::Pangaea, LandSize::Medium, 1);
    std::size_t flagged = 0;
    while (terrainAt(map, static_cast<int>(flagged / 32), static_cast<int>(flagged % 32)) == 0 || terrainAt(map, static_cast<int>(flagged / 32), static_cast<int>(flagged % 32)) == 7 ||
           markerAt(map, flagged))
    {
        ++flagged;
    }
    map.core[flagged] |= std::byte{ 0x10 };
    map.core[40]      = std::byte{ 0x09 };
    const auto before = map;
    RandomizeLandTerrain(map, 77);
    auto again = before;
    RandomizeLandTerrain(again, 77);
    expect(map.core == again.core, "randomizing is repeatable for its seed");
    expect(map.core != before.core, "randomizing changes the land's terrain");
    bool                 kept = true;
    std::array<bool, 16> kinds{};
    for (std::size_t index = 0; index < kMapCoreSize; ++index)
    {
        const auto oldValue     = std::to_integer<std::uint8_t>(before.core[index]);
        const auto newValue     = std::to_integer<std::uint8_t>(map.core[index]);
        const bool wasLand      = (oldValue & 0x0FU) >= 1 && (oldValue & 0x0FU) <= 6;
        const bool isLand       = (newValue & 0x0FU) >= 1 && (newValue & 0x0FU) <= 6;
        kept                    = kept && wasLand == isLand && (oldValue & 0xF0U) == (newValue & 0xF0U) && (wasLand || oldValue == newValue);
        kinds[newValue & 0x0FU] = kinds[newValue & 0x0FU] || isLand;
    }
    expect(kept, "randomizing keeps the coastline, other codes and every tile's other bits");
    expect(std::ranges::count(kinds, true) == 6, "randomized land holds all six land terrains");
    for (std::size_t index = 0; index < kMapCoreSize; ++index)
    {
        if (markerAt(map, index))
        {
            const auto under = terrainAt(map, static_cast<int>(index / 32), static_cast<int>(index % 32));
            expect(under == 1 || under == 2, "a randomized start stays on grassland or plains");
        }
    }

    for (const auto mirror : { MapMirror::LeftToRight, MapMirror::TopToBottom })
    {
        for (const std::uint32_t seed : { 3U, 77U })
        {
            auto mirroredMap = GenerateStartingLand(StartingLand::Pangaea, LandSize::Medium, 1, mirror);
            RandomizeLandTerrain(mirroredMap, seed);
            bool same = true;
            for (int row = 0; row < 32; ++row)
            {
                for (int column = 0; column < 32; ++column)
                {
                    const int across = mirror == MapMirror::TopToBottom ? row : 31 - row;
                    const int down   = mirror == MapMirror::LeftToRight ? column : 31 - column;
                    same             = same && terrainAt(mirroredMap, row, column) == terrainAt(mirroredMap, across, down);
                }
            }
            expect(same, "randomizing a mirrored map keeps its terrain mirrored");
            expect(retailGrouping(mirroredMap).empty(), "randomized mirrored terrain stays within the retail limits");
            for (std::size_t index = 0; index < kMapCoreSize; ++index)
            {
                if (markerAt(mirroredMap, index))
                {
                    const auto under = terrainAt(mirroredMap, static_cast<int>(index / 32), static_cast<int>(index % 32));
                    expect(under == 1 || under == 2, "a mirrored start stays on grassland or plains");
                }
            }
        }
    }

    for (const auto& info : kStartingLands)
    {
        if (info.land == StartingLand::Blank || info.land == StartingLand::Shuffle)
        {
            continue;
        }
        for (const std::uint32_t seed : { kFirstLayoutSeed, 3776U })
        {
            auto randomized = GenerateStartingLand(info.land, LandSize::Medium, kFirstLayoutSeed);
            RandomizeLandTerrain(randomized, seed);
            const auto grouped = retailGrouping(randomized);
            expect(grouped.empty(), "randomized terrain stays within the retail group, run and block limits: " + grouped);
        }
    }

    MapDocument solid{};
    for (int row = 0; row < 32; ++row)
    {
        for (int column = 0; column < 32; ++column)
        {
            solid.core[static_cast<std::size_t>(row * 32 + column)] = std::byte{ static_cast<std::uint8_t>(column < 2 || column > 29 ? 7 : 1) };
        }
    }
    RandomizeLandTerrain(solid, 5);
    expect(std::ranges::any_of(solid.core, [](std::byte value)
                               {
                                   return (std::to_integer<std::uint8_t>(value) & 0x0FU) == 6;
                               }),
           "an all-land grid gets mountains");
    expect(retailGrouping(solid).empty(), "a randomized all-land grid stays within the retail limits");

    const auto landQuota = [](const MapDocument& map, int row, int column)
    {
        int grass  = 0;
        int forest = 0;
        for (int dr = -1; dr <= 1; ++dr)
        {
            for (int dc = -1; dc <= 1; ++dc)
            {
                if ((dr != 0 || dc != 0) && row + dr >= 0 && row + dr < 32 && column + dc >= 0 && column + dc < 32)
                {
                    grass += terrainAt(map, row + dr, column + dc) == 1 ? 1 : 0;
                    forest += terrainAt(map, row + dr, column + dc) == 3 ? 1 : 0;
                }
            }
        }
        return grass >= 2 && forest >= 2;
    };
    for (const std::uint32_t seed : { 1U, 18U, 77U })
    {
        MapDocument patch{};
        for (int row = 10; row <= 13; ++row)
        {
            for (int column = 10; column <= 13; ++column)
            {
                patch.core[static_cast<std::size_t>(row * 32 + column)] = std::byte{ 0x05 };
            }
        }
        for (int row = 0; row <= 1; ++row)
        {
            for (int column = 9; column <= 11; ++column)
            {
                patch.core[static_cast<std::size_t>(row * 32 + column)] = std::byte{ 0x06 };
            }
        }
        for (const auto [row, column] : { std::pair{ 10, 11 }, std::pair{ 11, 13 }, std::pair{ 0, 10 } })
        {
            patch.core[static_cast<std::size_t>(row * 32 + column)] |= std::byte{ 0x20 };
        }
        RandomizeLandTerrain(patch, seed);
        expect(landQuota(patch, 10, 11) && landQuota(patch, 11, 13), "neighbouring starts each keep two grassland and two forest");
        expect(landQuota(patch, 0, 10), "a start on the grid's edge keeps two grassland and two forest");
    }
}

void testIdentity()
{
    expect(MapPackIdFromName("  Isle of Dogs!  ") == "isle-of-dogs", "a name becomes a lowercase dashed pack ID");
    expect(MapPackIdFromName(std::string(60, 'a')).size() == kMapPackMaxIdLength, "pack IDs are capped");
    expect(MapPackIdFromName("---") == "", "punctuation alone gives no pack ID");
    expect(MapPackIdFromName("Con") == "con-map" && MapPackIdFromName("LPT1") == "lpt1-map" && MapPackIdFromName("com10") == "com10", "Windows device names are avoided");

    expect(MapNameProblem("Isle of Dogs, part 2!").empty(), "plain punctuation is a valid title");
    expect(!MapNameProblem("Fire & Ice").empty() && !MapNameProblem("<b>").empty(), "markup characters are refused");
    expect(!MapNameProblem("Caf\xC3\xA9").empty() && !MapNameProblem("Tab\there").empty(), "non-ASCII and control characters are refused");

    MapPackUsage empty;
    const auto   fresh = SuggestMapIdentity("  Shattered Isles ", empty);
    expect(fresh.title == "Shattered Isles", "the title is the trimmed name");
    expect(fresh.packId == "shattered-isles", "the pack ID follows the name");
    expect(fresh.scenarioId == 100, "the first scenario ID is 100");
    expect(fresh.mapName == "S", "the MAP letter is the name's first letter");
    expect(!fresh.packIdSuffixed, "a free pack ID is not suffixed");
    expect(SuggestMapIdentity("Twin   Peaks", empty).title == "Twin Peaks", "runs of spaces collapse in the title");

    MapPackUsage used;
    used.packIds     = { "Shattered-Isles", "shattered-isles-2" };
    used.scenarioIds = { 100, 101, 103, 7 };
    used.mapNames    = { "s", "H", "Mountains_of_Madness" };
    const auto taken = SuggestMapIdentity("Shattered Isles", used);
    expect(taken.packId == "shattered-isles-3" && taken.packIdSuffixed, "a used pack ID gets the next free suffix");
    expect(taken.scenarioId == 102, "the lowest free scenario ID of 100 or more is used");
    expect(taken.mapName == "A", "MAP letters compare without case and skip used ones");
    expect(SuggestMapIdentity("", empty).packId == "my-map", "an empty name still gets a pack ID");

    MapPackUsage full;
    for (char letter = 'a'; letter <= 'z'; ++letter)
    {
        full.mapNames.emplace_back(1, letter);
    }
    expect(SuggestMapIdentity("Crowded", full).mapName.empty(), "no MAP letter is suggested when all are used");

    expect(fresh.slot == 5U, "the first scenario file slot is 5 when none is used");
    MapPackUsage shipped;
    shipped.slots = { 5, 6, 7, 8, 9, 10 };
    expect(SuggestMapIdentity("Shipped", shipped).slot == 11U, "the lowest free slot after the shipped packs is used");
    MapPackUsage recent;
    recent.slots = { 5 };
    expect(SuggestMapIdentity("Recent", recent).slot == 6U, "a recent map's saved slot is skipped");
    MapPackUsage slotsFull;
    for (std::uint32_t slot = kMapPackFirstSlot; slot <= kMapPackLastSlot; ++slot)
    {
        slotsFull.slots.push_back(slot);
    }
    expect(!SuggestMapIdentity("Crowded", slotsFull).slot.has_value(), "no slot is suggested when all are used");

    MapPackUsage folder;
    folder.packIds     = { "My-Map" };
    folder.scenarioIds = { 100 };
    folder.mapNames    = { "a" };
    folder.slots       = { 5 };
    MapPackUsage everything;
    everything.scenarioIds = { 101 };
    everything.mapNames    = { "B" };
    everything.slots       = { 6 };
    MapIdentity current;
    current.title      = "Atlas";
    current.packId     = "my-map";
    current.scenarioId = 100;
    current.mapName    = "A";
    current.slot       = 5;
    const auto moved   = RepickClashingIdentity(current, folder, everything);
    expect(moved.slot == 7U && moved.scenarioId == 102 && moved.mapName == "T", "clashing slot, scenario ID and letter avoid the folder and everything else");
    expect(moved.packId == "atlas", "a clashing pack ID is replaced");
    expect(moved.title == "Atlas", "other fields are kept");

    current.packId     = "atlas";
    current.scenarioId = 200;
    current.mapName    = "Z";
    current.slot       = 12;
    const auto kept    = RepickClashingIdentity(current, folder, everything);
    expect(kept.slot == 12U && kept.scenarioId == 200 && kept.mapName == "Z" && kept.packId == "atlas" && !kept.packIdSuffixed, "values that do not clash stay exactly as they are");

    MapPackUsage       nothingFree;
    const MapPackUsage noRecent;
    nothingFree.slots = slotsFull.slots;
    for (char letter = 'a'; letter <= 'z'; ++letter)
    {
        nothingFree.mapNames.emplace_back(1, letter);
    }
    current.slot     = 5;
    current.mapName  = "A";
    const auto stuck = RepickClashingIdentity(current, nothingFree, noRecent);
    expect(!stuck.slot.has_value(), "a clashing slot is left empty when the folder uses every slot");
    expect(stuck.mapName.empty(), "a clashing MAP letter is left empty when the folder uses every letter");
}

void testMapPackUsage()
{
    TemporaryDirectory directory;
    const auto         pack = directory.path / "maps" / "frozen-north";
    std::filesystem::create_directories(pack / "assets" / "map-pack");
    {
        std::ofstream manifest(pack / "asset-pack.toml", std::ios::binary);
        manifest << "[asset_pack]\nid = \"frozen-north\"\n\n[map_pack]\nslot = 5\n\n[[ map_pack.scenarios ]] # first\n  id=104 # comment\nname = \"Frozen North\"\n[[map_pack.scenarios]]\nid = +106\n";
    }
    auto                   map = GenerateStartingLand(StartingLand::Pangaea, LandSize::Medium, 5);
    std::vector<std::byte> bytes(map.core.begin(), map.core.end());
    bytes.insert(bytes.end(), map.footer.begin(), map.footer.end());
    FpkArchive archive;
    archive.entries.push_back({ "F.map", bytes });
    archive.entries.push_back({ "Notes.txt", std::vector<std::byte>(5, std::byte{ 'x' }) });
    archive.entries.push_back({ "Short.map", std::vector<std::byte>(16) });
    archive.entries.push_back({ "MapK_Heights.dds", std::vector<std::byte>(4) });
    archive.entries.push_back({ "MapLong_Name_Lightmap.dds", std::vector<std::byte>(4) });
    const std::string_view slotXml = "<S><EntryInfo><MAP>Mountains_of_Madness</MAP><ID>107</ID></EntryInfo></S>";
    archive.entries.push_back({ "DLCScenarioData9.xml", std::vector<std::byte>(reinterpret_cast<const std::byte*>(slotXml.data()), reinterpret_cast<const std::byte*>(slotXml.data() + slotXml.size())) });
    // Lenient reading: the strict scenario reader refuses this comment.
    const std::string_view xml = "<?xml version=\"1.0\"?><!-- <MAP>Z</MAP> --><ROOT><ENTRY><MAP>Q</MAP><ID> 105 </ID></ENTRY></ROOT>";
    archive.entries.push_back({ "DLCScenarioData.xml", std::vector<std::byte>(reinterpret_cast<const std::byte*>(xml.data()), reinterpret_cast<const std::byte*>(xml.data() + xml.size())) });
    const auto encoded = EncodeFpkArchive(archive);
    expect(encoded.has_value(), "the test archive encodes");
    {
        std::ofstream output(pack / "assets" / "map-pack" / "dlc.fpk", std::ios::binary);
        output.write(reinterpret_cast<const char*>(encoded->data()), static_cast<std::streamsize>(encoded->size()));
    }
    std::filesystem::create_directories(directory.path / "maps" / "empty-pack");
    {
        std::ofstream file(directory.path / "maps" / "loose-file", std::ios::binary);
    }

    MapPackUsage usage;
    AddMapPackUsage(usage, directory.path / "maps");
    AddMapPackUsage(usage, directory.path / "missing");
    expect(usage.complete, "readable packs leave the usage complete");
    expect(std::ranges::count(usage.packIds, "frozen-north") == 1 && std::ranges::count(usage.packIds, "empty-pack") == 1, "every pack folder is in use");
    expect(std::ranges::count(usage.packIds, "loose-file") == 1, "any entry in the maps folder reserves its name");
    expect(std::ranges::count(usage.scenarioIds, 104) == 1 && std::ranges::count(usage.scenarioIds, 106) == 1, "manifest scenario IDs are in use despite comments, spacing and signs");
    expect(std::ranges::count(usage.mapNames, "Z") == 0, "MAP tags inside XML comments are not in use");
    expect(std::ranges::count(usage.scenarioIds, 105) == 1 && std::ranges::count(usage.mapNames, "Q") == 1, "scenario XML IDs and MAP names are in use");
    expect(std::ranges::count(usage.mapNames, "F") == 1, "archive MAP names are in use");
    expect(std::ranges::count(usage.mapNames, "K") == 1, "map texture names reserve their letter");
    expect(std::ranges::count(usage.mapNames, "Long_Name") == 1 && std::ranges::count(usage.mapNames, "Mountains_of_Madness") == 1, "long MAP names are in use");
    expect(usage.slots == std::vector<std::uint32_t>{ 9 }, "numbered scenario files reserve their slot");
    expect(SuggestMapIdentity("Frozen North", usage).mapName == "R", "a used MAP letter is skipped");

    expect(ReadFpkTable(pack / "assets" / "map-pack" / "dlc.fpk", 2).has_value() == false, "a table over the entry limit is refused");
    std::uint64_t small = 64;
    std::uint64_t large = 4096;
    expect(!ReadFpkTable(pack / "assets" / "map-pack" / "dlc.fpk", UINT32_MAX, &small).has_value() && small < 64, "a table over the byte budget is refused and still charged");
    expect(ReadFpkTable(pack / "assets" / "map-pack" / "dlc.fpk", UINT32_MAX, &large).has_value() && large < 4096, "a table within the budget is read and charged");
}

} // namespace

int main()
{
    testStartingLand();
    testMirrorsAndPlayers();
    testSpreadAndSites();
    testClimateShares();
    testRandomizeTerrain();
    testIdentity();
    testMapPackUsage();
    if (failures != 0)
    {
        std::cerr << failures << " map wizard assertion(s) failed.\n";
        return 1;
    }
    std::cout << "Map wizard tests passed.\n";
    return 0;
}
