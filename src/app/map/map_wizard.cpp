#include "map/map_wizard.h"

#include "map/map_pack_export.h"
#include "scenario_document.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <numeric>
#include <optional>
#include <queue>
#include <system_error>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace rerevved::tools
{
namespace
{

constexpr int kSide = static_cast<int>(kMapWidth);

constexpr std::uint8_t kSea       = 0;
constexpr std::uint8_t kGrassland = 1;
constexpr std::uint8_t kPlains    = 2;
constexpr std::uint8_t kForest    = 3;
constexpr std::uint8_t kHill      = 4;
constexpr std::uint8_t kDesert    = 5;
constexpr std::uint8_t kMountain  = 6;
constexpr std::uint8_t kPolarIce  = 7;
constexpr std::uint8_t kMarker    = 0x20;

std::uint64_t mix(std::uint64_t value) noexcept
{
    // splitmix64 finalizer: a fixed hash, so layouts match on every platform.
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

float unit(std::uint32_t seed, std::uint32_t salt, int a, int b) noexcept
{
    const auto hash = mix((std::uint64_t(seed) << 32U) ^ mix((std::uint64_t(salt) << 40U) ^ (std::uint64_t(std::uint32_t(a)) << 20U) ^ std::uint32_t(b)));
    return static_cast<float>(hash >> 40U) / static_cast<float>(1U << 24U);
}

float noise(std::uint32_t seed, std::uint32_t salt, int row, int column, float scale) noexcept
{
    const auto octave = [&](float size, std::uint32_t layer)
    {
        const float x  = static_cast<float>(row) / size;
        const float y  = static_cast<float>(column) / size;
        const int   x0 = static_cast<int>(std::floor(x));
        const int   y0 = static_cast<int>(std::floor(y));
        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
        const float sx = fx * fx * (3.0F - 2.0F * fx);
        const float sy = fy * fy * (3.0F - 2.0F * fy);
        const auto  at = [&](int i, int j)
        {
            return unit(seed, salt * 8U + layer, i, j);
        };
        const float top    = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
        const float bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
        return top + (bottom - top) * sy;
    };
    return octave(scale, 0) * 0.7F + octave(scale * 0.5F, 1) * 0.3F;
}

std::size_t at(int row, int column) noexcept
{
    return static_cast<std::size_t>(row * kSide + column);
}

bool inside(int row, int column) noexcept
{
    return row >= 0 && row < kSide && column >= 0 && column < kSide;
}

bool landAllowed(int row, int column) noexcept
{
    return row >= 1 && row <= kSide - 2 && column >= 3 && column <= kSide - 4;
}

constexpr std::array<std::array<int, 2>, 4> kSteps{ { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } } };

struct Grower
{
    std::uint32_t                   seed;
    std::array<int, kMapCoreSize>&  owner; // -1 sea, otherwise island index
    std::array<float, kMapCoreSize> shape{};

    void grow(int island, int row, int column, std::size_t target, float compactness)
    {
        if (!landAllowed(row, column) || owner[at(row, column)] != -1)
        {
            return;
        }
        owner[at(row, column)] = island;
        std::size_t size       = 1;
        while (size < target)
        {
            std::optional<std::size_t> best;
            float                      bestScore = 0.0F;
            for (int r = 1; r < kSide - 1; ++r)
            {
                for (int c = 3; c < kSide - 3; ++c)
                {
                    if (owner[at(r, c)] != -1 || !open(island, r, c))
                    {
                        continue;
                    }
                    const float distance = std::hypot(static_cast<float>(r - row), static_cast<float>(c - column));
                    const float score    = shape[at(r, c)] + 0.35F * unit(seed, 5, r, c) - compactness * distance;
                    if (!best || score > bestScore)
                    {
                        best      = at(r, c);
                        bestScore = score;
                    }
                }
            }
            if (!best)
            {
                return;
            }
            owner[*best] = island;
            ++size;
        }
    }

    bool open(int island, int row, int column) const
    {
        bool adjacent = false;
        for (int dr = -1; dr <= 1; ++dr)
        {
            for (int dc = -1; dc <= 1; ++dc)
            {
                if (!inside(row + dr, column + dc))
                {
                    continue;
                }
                const int other = owner[at(row + dr, column + dc)];
                if (other != -1 && other != island)
                {
                    return false;
                }
                adjacent = adjacent || (other == island && (dr == 0 || dc == 0));
            }
        }
        return adjacent && landAllowed(row, column);
    }
};

void fillLakes(std::array<int, kMapCoreSize>& owner)
{
    std::array<bool, kMapCoreSize>  ocean{};
    std::queue<std::pair<int, int>> queue;
    for (int r = 0; r < kSide; ++r)
    {
        for (int c = 0; c < kSide; ++c)
        {
            if ((r == 0 || c == 0 || r == kSide - 1 || c == kSide - 1) && owner[at(r, c)] == -1)
            {
                ocean[at(r, c)] = true;
                queue.emplace(r, c);
            }
        }
    }
    while (!queue.empty())
    {
        const auto [r, c] = queue.front();
        queue.pop();
        for (const auto& step : kSteps)
        {
            const int nr = r + step[0];
            const int nc = c + step[1];
            if (inside(nr, nc) && !ocean[at(nr, nc)] && owner[at(nr, nc)] == -1)
            {
                ocean[at(nr, nc)] = true;
                queue.emplace(nr, nc);
            }
        }
    }
    for (std::size_t index = 0; index < kMapCoreSize; ++index)
    {
        if (owner[index] == -1 && !ocean[index])
        {
            for (const auto& step : kSteps)
            {
                const int nr = static_cast<int>(index) / kSide + step[0];
                const int nc = static_cast<int>(index) % kSide + step[1];
                if (inside(nr, nc) && owner[at(nr, nc)] != -1)
                {
                    owner[index] = owner[at(nr, nc)];
                    break;
                }
            }
        }
    }
}

std::vector<std::size_t> ranked(const std::vector<std::size_t>& tiles, const std::array<float, kMapCoreSize>& key)
{
    auto order = tiles;
    std::ranges::stable_sort(order, [&](std::size_t left, std::size_t right)
                             {
                                 return key[left] > key[right];
                             });
    return order;
}

// North at the top: 30 file rows across by 26 file columns down. '#' is land.
constexpr std::array<std::string_view, 26> kEarth{
    "...#####..###.......#######...",
    "##########.##..###############",
    "######.....#.#.###############",
    ".#####..##.#...##############.",
    "..########...#.#############..",
    "...#######....##############..",
    "...######....###.########.#...",
    "...######....##..########.#...",
    "....####.....############.....",
    "....#####....############.....",
    ".....###.....#####.#####......",
    ".....###.....######.##.##.#...",
    ".......####..#######.#.#..#...",
    "........####.######....#.##...",
    ".......#####...####...#.#.##..",
    ".......######..####.......##..",
    "........#####..####...###.....",
    "........####...###.#.....##...",
    "........####...###.#...#####..",
    "........###....###.....#####..",
    "........###....##......#####..",
    "........##.....#.......##.##..",
    "........##................#.#.",
    "........##..................#.",
    "........#.....................",
    "........#.....................",
};

constexpr std::array<std::string_view, 26> kJapan{
    "......................##......",
    "......................###.....",
    ".....................######...",
    "....................#########.",
    "...................#########..",
    "...................########...",
    "..................#####.......",
    "..................##..........",
    "..............................",
    "...................####.......",
    "...................#####......",
    "...................#####......",
    "..................######......",
    "..................######......",
    ".................#######......",
    "...............########.......",
    "............###########.......",
    "..........############........",
    ".......##############.........",
    "....##############............",
    "...##########.................",
    ".##.........#.................",
    "####..#####...................",
    "####...###....................",
    ".###..........................",
    ".##...........................",
};

constexpr std::array<std::string_view, 26> kBritishIsles{
    "...............#..............",
    ".............#................",
    "..........#####...............",
    ".........#######..............",
    ".......#.#######..............",
    "........########..............",
    ".......#.#######..............",
    ".........######...............",
    ".........#######..............",
    ".........#######..............",
    "....#####.#######.............",
    "...######..######.............",
    "...######...#.####............",
    "..#######.....####............",
    "...######....#####............",
    "..#######...#######...........",
    "...######...########..........",
    "..######...#########..........",
    "...####....##########.........",
    "....##.....#########..........",
    "............########..........",
    "..........##########..........",
    ".........#####................",
    "..........##..................",
    "..............#...............",
    "..............................",
};

constexpr std::array<std::string_view, 26> kMediterranean{
    "..############################",
    "...###########################",
    "....#####################....#",
    ".....##########.#########....#",
    ".....##########..########.....",
    ".....#####.####...#######.....",
    "########....####..#######.....",
    "########...#.####..######.....",
    "#########..#..####.###########",
    "########...#..####.####.######",
    "########...#...###.####..#####",
    "#######.#..#...##..####.######",
    "#######.........#..####..#####",
    "#######.......###..###..######",
    "######......#.###..##...######",
    "#####.......###.........######",
    ".....##########......###.....#",
    "##############............##.#",
    "##############...............#",
    "##############..............##",
    "################..#####.....##",
    "#################..######...##",
    "#################..###########",
    "##################.###########",
    "##############################",
    "##############################",
};

constexpr std::array<std::string_view, 26> kAmericas{
    "########################......",
    "######################........",
    "#####################.........",
    "###################...........",
    "##################............",
    ".#################............",
    ".################.............",
    "..##############..............",
    "...##.####....##..............",
    "...##.####....#...............",
    "....#.####......#.............",
    ".....######.##.###............",
    ".......#######..#.##..........",
    "........#####.......#.........",
    "..........####................",
    "............##...#####........",
    ".............##########.......",
    "...............##########.....",
    "...............###########....",
    "...............############...",
    "..............##############..",
    "...............##############.",
    "...............###############",
    "...............###############",
    "................##############",
    "................##############",
};

constexpr std::array<std::string_view, 26> kEurope{
    "..............................",
    "...............######.........",
    ".............#################",
    "............##################",
    "...........#######..######..##",
    "............#####..#######.###",
    "...........#####...###########",
    "..........######..############",
    ".....#...######..#############",
    "....##...########.....########",
    "...####...#.####...###########",
    "...####....##.##..############",
    ".##.###...###....#############",
    ".##.####.#####################",
    ".##.####.#####################",
    "........######################",
    "...###########################",
    "....##########################",
    "......########################",
    "......########.#######..#..###",
    ".########.####..######......##",
    ".########.#.###.#####........#",
    "#########..#.###.#############",
    "#######.#..#..###.###.########",
    "#######......###..##..########",
    ".#####..#####.....#...########",
};

constexpr std::array<std::string_view, 26> kAfrica{
    "....#########.....#...########",
    "....##########........########",
    "...############..#############",
    "..####################.####..#",
    ".#####################.#####..",
    "#######################.#####.",
    "########################.####.",
    "#########################.####",
    "##########################....",
    ".#############################",
    "..############################",
    "....#########################.",
    "...........################...",
    "............##############....",
    ".............#############....",
    ".............############.....",
    ".............############.....",
    "..............############..#.",
    "..............############.##.",
    ".............############..##.",
    ".............###########..###.",
    "..............##########..##..",
    "..............#########...##..",
    "...............#######........",
    "...............######.........",
    "...............#####..........",
};

constexpr std::array<std::string_view, 26> kAustralia{
    "......................######..",
    "..............................",
    ".............#####...##.......",
    "............######...##.......",
    ".........#########...###......",
    "........##########...###......",
    ".......###########...####.....",
    "......###################.....",
    "....######################....",
    ".##########################...",
    ".###########################..",
    ".###########################..",
    ".############################.",
    ".############################.",
    ".############################.",
    "..###########################.",
    "..###########################.",
    "..#######.......############..",
    "..######.......##.##########..",
    "..###..............#########..",
    ".................#.#########..",
    "....................#######...",
    ".....................#####....",
    "..............................",
    "......................####....",
    ".......................###....",
};

constexpr std::array<std::string_view, 26> kCaribbean{
    ".......###.#..................",
    ".......###..#.................",
    "........##...#................",
    "...........#.#................",
    "...........#..#...............",
    "......####....................",
    "....########..................",
    "....##########................",
    "###.....#######.#.............",
    "###.........###.#.............",
    "###.............#####.........",
    "###............######.##......",
    "###.......###..#####....#.#...",
    "##........................#...",
    "##........................#...",
    "#######...................#...",
    "#######...................#...",
    "#######...................#...",
    "#######....................#..",
    "#######....................#..",
    "#######.........##.##.....#...",
    "#######......#####.#....#.....",
    "#######.....######.########...",
    "#######....#################..",
    "###########.##################",
    "###########.##################",
};

using Stencil = std::array<std::string_view, 26>;

constexpr std::array<std::pair<StartingLand, const Stencil*>, 9> kPlaces{ {
    { StartingLand::Earth, &kEarth },
    { StartingLand::JapaneseMainland, &kJapan },
    { StartingLand::BritishIsles, &kBritishIsles },
    { StartingLand::Mediterranean, &kMediterranean },
    { StartingLand::Americas, &kAmericas },
    { StartingLand::Europe, &kEurope },
    { StartingLand::Africa, &kAfrica },
    { StartingLand::Australia, &kAustralia },
    { StartingLand::Caribbean, &kCaribbean },
} };

static_assert(
    []
    {
        for (const auto& [land, stencil] : kPlaces)
        {
            for (const auto line : *stencil)
            {
                if (line.size() != 30 || line.find_first_not_of(".#") != std::string_view::npos)
                {
                    return false;
                }
            }
        }
        return true;
    }(),
    "every stencil line is 30 tiles of '.' or '#'");

void landFromStencil(std::array<int, kMapCoreSize>& owner, const Stencil& stencil)
{
    for (int column = 0; column < 26; ++column)
    {
        for (int row = 0; row < 30; ++row)
        {
            if (stencil[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] == '#')
            {
                owner[at(row + 1, column + 3)] = 0;
            }
        }
    }
}

// Near the retail DLC maps: about 9% mountain, 8% hill, 10% desert and a quarter forest.
struct Climate
{
    float mountain = 0.09F;
    float hill     = 0.08F;
    float desert   = 0.10F;
    float forest   = 0.24F;
    // Of the flat land left, the share that is grassland rather than plains.
    float grassland = 0.58F;
    bool  scattered = false;
    bool  tilted    = false;
};

Climate climateOf(StartingLand land)
{
    Climate climate;
    switch (land)
    {
        case StartingLand::JapaneseMainland:
        case StartingLand::NorthVsSouth: // gets a desert band instead
            climate.desert = 0.0F;
            break;
        case StartingLand::Highlands:
            climate.mountain = 0.16F;
            climate.hill     = 0.22F;
            break;
        case StartingLand::Arborea:
            climate.mountain = 0.04F;
            climate.hill     = 0.04F;
            climate.forest   = 0.80F;
            climate.desert   = 0.02F;
            break;
        case StartingLand::Sandstorm:
            climate.mountain  = 0.03F;
            climate.hill      = 0.03F;
            climate.desert    = 0.83F;
            climate.forest    = 0.06F;
            climate.grassland = 0.30F;
            break;
        case StartingLand::IceAge:
            climate.desert    = 0.03F;
            climate.forest    = 0.35F;
            climate.grassland = 0.45F;
            break;
        case StartingLand::Frontier:
            climate.mountain  = 0.42F;
            climate.scattered = true;
            break;
        case StartingLand::GreatPlains:
            climate.mountain  = 0.03F;
            climate.hill      = 0.06F;
            climate.desert    = 0.04F;
            climate.forest    = 0.08F;
            climate.grassland = 0.20F;
            break;
        case StartingLand::Boreal:
            climate.desert    = 0.0F;
            climate.forest    = 0.55F;
            climate.hill      = 0.21F;
            climate.grassland = 0.45F;
            break;
        case StartingLand::TiltedAxis:
            climate.tilted = true;
            break;
        default:
            break;
    }
    return climate;
}

constexpr float kForcedSea = -1e9F;

void landFromScores(std::array<int, kMapCoreSize>& owner, const std::array<float, kMapCoreSize>& score, float share)
{
    std::vector<std::size_t> tiles;
    for (int r = 0; r < kSide; ++r)
    {
        for (int c = 0; c < kSide; ++c)
        {
            if (landAllowed(r, c) && score[at(r, c)] > kForcedSea)
            {
                tiles.push_back(at(r, c));
            }
        }
    }
    const auto area  = static_cast<float>((kSide - 2) * (kSide - 6));
    const auto count = std::min(tiles.size(), static_cast<std::size_t>(share * area));
    const auto order = ranked(tiles, score);
    for (std::size_t index = 0; index < count; ++index)
    {
        owner[order[index]] = 0;
    }
}

// Across runs -1 at the west edge to 1 at the east, down -1 at the north to 1 at the south.
std::array<float, kMapCoreSize> shapeScores(StartingLand land, std::uint32_t seed)
{
    std::array<float, kMapCoreSize> score{};
    for (int r = 0; r < kSide; ++r)
    {
        for (int c = 0; c < kSide; ++c)
        {
            const float across = (static_cast<float>(r) - 15.5F) / 14.5F;
            const float down   = (static_cast<float>(c) - 15.5F) / 12.5F;
            const float wobble = noise(seed, 1, r, c, 5.0F);
            float       value  = 0.0F;
            switch (land)
            {
                case StartingLand::Hemispheres:
                {
                    const float west = std::hypot((across + 0.5F) / 0.45F, down / 0.8F);
                    const float east = std::hypot((across - 0.5F) / 0.45F, down / 0.8F);
                    value            = std::abs(across) < 0.14F ? kForcedSea : -std::min(west, east) + 0.6F * wobble;
                    break;
                }
                case StartingLand::FourCorners:
                {
                    float corner = 1e9F;
                    for (const float x : { -0.5F, 0.5F })
                    {
                        for (const float y : { -0.5F, 0.5F })
                        {
                            corner = std::min(corner, std::hypot((across - x) / 0.4F, (down - y) / 0.4F));
                        }
                    }
                    const float middle = std::hypot(across / 0.16F, down / 0.19F);
                    const bool  water  = middle > 1.0F && (middle < 2.0F || std::abs(across) < 0.14F || std::abs(down) < 0.16F);
                    value              = water ? kForcedSea : std::max(-corner, -middle) + 0.6F * wobble;
                    break;
                }
                case StartingLand::WestVsEast:
                    value = std::abs(across) < 0.14F ? kForcedSea : std::abs(across) + 0.25F * wobble;
                    break;
                case StartingLand::NorthVsSouth:
                    value = -std::max(std::abs(across), std::abs(down)) + 0.25F * wobble;
                    break;
                case StartingLand::InlandSea:
                    value = std::hypot(across / 0.8F, down / 0.75F) + 0.3F * wobble;
                    break;
                case StartingLand::Lakes:
                {
                    const bool rim = r <= 2 || r >= kSide - 3 || c <= 4 || c >= kSide - 5;
                    value          = noise(seed, 1, r, c, 3.0F) + (rim ? 0.6F : 0.0F);
                    break;
                }
                case StartingLand::Donut:
                    value = -1.5F * std::abs(std::hypot(across, down) - 0.62F) + 0.2F * wobble;
                    break;
                case StartingLand::Oval:
                    value = -std::hypot(across / 0.8F, down / 0.75F) + 0.45F * noise(seed, 1, r, c, 3.0F);
                    break;
                case StartingLand::Fractal:
                {
                    const float ridge = 1.0F - std::abs(2.0F * noise(seed, 1, r, c, 6.0F) - 1.0F);
                    value             = ridge + 0.3F * noise(seed, 2, r, c, 3.0F);
                    break;
                }
                case StartingLand::Ring:
                {
                    const float phase = 6.2831853F * unit(seed, 8, 0, 0);
                    const float angle = std::atan2(down, across);
                    value             = -1.5F * std::abs(std::hypot(across, down) - 0.6F) + 0.15F * std::cos(5.0F * angle + phase) + 0.15F * wobble;
                    break;
                }
                case StartingLand::Skirmish:
                {
                    const bool  left   = r <= 15;
                    const float mirror = noise(seed, 1, left ? r : kSide - 1 - r, c, 5.0F);
                    value              = -std::hypot(across / 0.8F, down / 0.75F) + 0.5F * mirror;
                    break;
                }
                case StartingLand::Terra:
                {
                    const float old   = std::hypot((across - 0.35F) / 0.6F, down / 0.8F);
                    const float young = std::hypot((across + 0.65F) / 0.25F, down / 0.85F);
                    const bool  ocean = across > -0.42F && across < -0.28F;
                    value             = ocean ? kForcedSea : -std::min(old, young) + 0.5F * wobble;
                    break;
                }
                case StartingLand::IceAge:
                    value = noise(seed, 1, r, c, 6.0F) - 3.0F * std::max(0.0F, std::abs(down) - 0.3F);
                    break;
                case StartingLand::Highlands:
                case StartingLand::GreatPlains:
                    value = -std::max(std::abs(across), std::abs(down)) + 0.25F * wobble;
                    break;
                default:
                    break;
            }
            score[at(r, c)] = value;
        }
    }
    return score;
}

std::string lower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c)
                           {
                               return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
                           });
    return result;
}

std::string_view trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string utf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

constexpr std::uint32_t kTableLimit = 1U << 18;

// A real file or folder: never a symlink, junction or other reparse point.
bool plainOf(const std::filesystem::path& path, std::filesystem::file_type type)
{
    std::error_code error;
    if (std::filesystem::symlink_status(path, error).type() != type)
    {
        return false;
    }
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
    return true;
#endif
}

// Charge table records read to the budget even when parsing fails.
std::expected<std::vector<FpkTableEntry>, std::string> readTable(const std::filesystem::path& archive, std::uint64_t& budget)
{
    return ReadFpkTable(archive, kTableLimit, &budget);
}

bool isRegular(const std::filesystem::path& path)
{
    return plainOf(path, std::filesystem::file_type::regular);
}

bool isDirectory(const std::filesystem::path& path)
{
    return plainOf(path, std::filesystem::file_type::directory);
}

std::optional<std::string> readSmallFile(const std::filesystem::path& path)
{
    constexpr std::size_t kLimit = 1U << 20;
    std::ifstream         input(path, std::ios::binary);
    std::string           text(kLimit + 1, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input.is_open() || input.bad() || input.gcount() > static_cast<std::streamsize>(kLimit))
    {
        return std::nullopt;
    }
    text.resize(static_cast<std::size_t>(std::max<std::streamsize>(input.gcount(), 0)));
    return text;
}

constexpr std::array<std::string_view, 3> kMapTextureSuffixes{ "_heights.dds", "_mountainhill_blends.dds", "_lightmap.dds" };

// Signed decimal as TOML and XML write it, with an optional plus sign.
std::optional<std::int32_t> parseId(std::string_view value)
{
    if (value.starts_with('+'))
    {
        value.remove_prefix(1);
    }
    std::int32_t id     = 0;
    const auto   result = std::from_chars(value.data(), value.data() + value.size(), id);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() ? std::optional(id) : std::nullopt;
}

void addManifestIds(MapPackUsage& usage, std::string_view manifest)
{
    std::string table;
    for (std::size_t start = 0; start < manifest.size();)
    {
        const auto newline = manifest.find('\n', start);
        auto       line    = manifest.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        start              = newline == std::string_view::npos ? manifest.size() : newline + 1;
        line               = trim(line.substr(0, line.find('#')));
        if (line.starts_with('['))
        {
            table.clear();
            std::ranges::copy_if(line, std::back_inserter(table), [](char c)
                                 {
                                     return c != ' ' && c != '\t';
                                 });
            continue;
        }
        const auto equals = line.find('=');
        if (table != "[[map_pack.scenarios]]" || equals == std::string_view::npos || trim(line.substr(0, equals)) != "id")
        {
            continue;
        }
        if (const auto id = parseId(trim(line.substr(equals + 1))))
        {
            usage.scenarioIds.push_back(*id);
        }
    }
}

// Read leniently, so a file the strict scenario reader refuses still reserves its ids and names.
void addScenarioTags(MapPackUsage& usage, std::string_view source)
{
    std::string uncommented;
    for (std::size_t at = 0; at < source.size();)
    {
        const auto comment = source.find("<!--", at);
        uncommented += source.substr(at, comment == std::string_view::npos ? std::string_view::npos : comment - at);
        const auto close = comment == std::string_view::npos ? std::string_view::npos : source.find("-->", comment + 4);
        at               = close == std::string_view::npos ? source.size() : close + 3;
    }
    const std::string_view xml = uncommented;
    for (const std::string_view tag : { std::string_view("ID"), std::string_view("MAP") })
    {
        const std::string open  = "<" + std::string(tag) + ">";
        const std::string close = "</" + std::string(tag) + ">";
        // Each search resumes after the last close tag, so the scan stays linear.
        for (auto at = xml.find(open); at != std::string_view::npos; at = xml.find(open, at))
        {
            const auto first = at + open.size();
            const auto last  = xml.find(close, first);
            if (last == std::string_view::npos)
            {
                break;
            }
            at               = last + close.size();
            const auto value = trim(xml.substr(first, last - first));
            if (tag == "MAP")
            {
                if (value.size() <= kScenarioMaximumMapName)
                {
                    usage.mapNames.emplace_back(value);
                }
                continue;
            }
            if (const auto id = parseId(value))
            {
                usage.scenarioIds.push_back(*id);
            }
        }
    }
}

struct Around
{
    std::array<std::size_t, 8> tiles{};
    std::size_t                size = 0;

    [[nodiscard]] auto begin() const noexcept
    {
        return tiles.begin();
    }

    [[nodiscard]] auto end() const noexcept
    {
        return tiles.begin() + static_cast<std::ptrdiff_t>(size);
    }
};

Around neighbours(std::size_t index)
{
    Around    result;
    const int row    = static_cast<int>(index / kMapWidth);
    const int column = static_cast<int>(index % kMapWidth);
    for (int dr = -1; dr <= 1; ++dr)
    {
        for (int dc = -1; dc <= 1; ++dc)
        {
            if ((dr != 0 || dc != 0) && inside(row + dr, column + dc))
            {
                result.tiles[result.size++] = at(row + dr, column + dc);
            }
        }
    }
    return result;
}

int chebyshev(std::size_t left, std::size_t right)
{
    const int rows    = std::abs(static_cast<int>(left / kMapWidth) - static_cast<int>(right / kMapWidth));
    const int columns = std::abs(static_cast<int>(left % kMapWidth) - static_cast<int>(right % kMapWidth));
    return std::max(rows, columns);
}

std::size_t mirrored(std::size_t index, MapMirror mirror)
{
    const int row    = static_cast<int>(index / kMapWidth);
    const int column = static_cast<int>(index % kMapWidth);
    switch (mirror)
    {
        case MapMirror::LeftToRight:
            return at(kSide - 1 - row, column);
        case MapMirror::TopToBottom:
            return at(row, kSide - 1 - column);
        default:
            return index;
    }
}

// File rows run left to right on screen, and file columns top to bottom.
bool inFirstHalf(std::size_t index, MapMirror mirror)
{
    switch (mirror)
    {
        case MapMirror::LeftToRight:
            return static_cast<int>(index / kMapWidth) < kSide / 2;
        case MapMirror::TopToBottom:
            return static_cast<int>(index % kMapWidth) < kSide / 2;
        default:
            return true;
    }
}

void paintTerrain(std::array<std::uint8_t, kMapCoreSize>& terrain, const std::vector<std::size_t>& landTiles, const Climate& climate, std::uint32_t seed)
{
    std::array<bool, kMapCoreSize> land{};
    for (const auto index : landTiles)
    {
        land[index] = true;
        // Sea marks a land tile not painted yet.
        terrain[index] = kSea;
    }

    std::array<int, kMapCoreSize>   coast;
    std::queue<std::pair<int, int>> queue;
    coast.fill(0);
    for (int r = 0; r < kSide; ++r)
    {
        for (int c = 0; c < kSide; ++c)
        {
            if (!land[at(r, c)])
            {
                queue.emplace(r, c);
            }
            else
            {
                coast[at(r, c)] = -1;
            }
        }
    }
    while (!queue.empty())
    {
        const auto [r, c] = queue.front();
        queue.pop();
        for (const auto& step : kSteps)
        {
            const int nr = r + step[0];
            const int nc = c + step[1];
            if (inside(nr, nc) && coast[at(nr, nc)] == -1)
            {
                coast[at(nr, nc)] = coast[at(r, c)] + 1;
                queue.emplace(nr, nc);
            }
        }
    }
    // Land no sea reaches, as on a grid painted all land, is deep inland.
    std::ranges::replace(coast, -1, kSide);

    std::array<float, kMapCoreSize> elevation{};
    std::array<float, kMapCoreSize> dryness{};
    std::array<float, kMapCoreSize> wetness{};
    std::array<float, kMapCoreSize> grass{};
    for (const auto index : landTiles)
    {
        const int r = static_cast<int>(index / kMapWidth);
        const int c = static_cast<int>(index % kMapWidth);
        // File columns run north to south, so the equator lies between columns 15 and 16.
        const float latitude = climate.tilted ? 1.0F - static_cast<float>(r) / static_cast<float>(kSide - 1) : std::abs(static_cast<float>(c) - 15.5F) / 13.5F;
        elevation[index]     = noise(seed, 11, r, c, 5.0F) + 0.12F * static_cast<float>(std::min(coast[index], 4));
        wetness[index]       = 0.65F * noise(seed, 22, r, c, 6.0F) + 0.35F * noise(seed, 23, r, c, 2.0F);
        dryness[index]       = (1.0F - wetness[index]) - 0.9F * latitude;
        grass[index]         = 0.65F * noise(seed, 33, r, c, 4.0F) + 0.35F * noise(seed, 34, r, c, 1.5F);
    }
    const auto share = [&](float fraction)
    {
        return static_cast<std::size_t>(std::lround(fraction * static_cast<float>(landTiles.size())));
    };
    if (climate.scattered)
    {
        for (const auto index : landTiles)
        {
            elevation[index] = unit(seed, 12, static_cast<int>(index), 0);
        }
    }
    std::size_t mountains = 0;
    std::size_t hills     = 0;
    for (const auto index : ranked(landTiles, elevation))
    {
        if (mountains < share(climate.mountain) && coast[index] >= (climate.scattered ? 1 : 2))
        {
            terrain[index] = kMountain;
            ++mountains;
        }
        else if (hills < share(climate.hill))
        {
            terrain[index] = kHill;
            ++hills;
        }
    }
    std::vector<std::size_t> flat;
    std::ranges::copy_if(landTiles, std::back_inserter(flat), [&](std::size_t index)
                         {
                             return terrain[index] == kSea;
                         });
    std::size_t deserts = 0;
    for (const auto index : ranked(flat, dryness))
    {
        const float reach = 7.0F * std::max(1.0F, climate.desert / Climate{}.desert);
        const bool  hot   = climate.tilted ? index / kMapWidth >= 16 : std::abs(static_cast<float>(index % kMapWidth) - 15.5F) <= reach;
        if (deserts == share(climate.desert) || !hot)
        {
            continue;
        }
        terrain[index] = kDesert;
        ++deserts;
    }
    std::erase_if(flat, [&](std::size_t index)
                  {
                      return terrain[index] != kSea;
                  });
    const auto  forests = share(climate.forest);
    std::size_t placed  = 0;
    for (const auto index : ranked(flat, wetness))
    {
        terrain[index] = placed++ < forests ? kForest : kSea;
    }
    std::erase_if(flat, [&](std::size_t index)
                  {
                      return terrain[index] != kSea;
                  });
    const auto order = ranked(flat, grass);
    for (std::size_t rank = 0; rank < order.size(); ++rank)
    {
        terrain[order[rank]] = static_cast<float>(rank) < static_cast<float>(order.size()) * climate.grassland ? kGrassland : kPlains;
    }
}

// Repaints the start's neighbours with their mirrors until each quota terrain holds at least two of the eight.
template <typename Paintable>
void settle(std::array<std::uint8_t, kMapCoreSize>& terrain, std::size_t start, MapMirror mirror, std::initializer_list<std::uint8_t> quotas, Paintable&& paintable)
{
    const auto around = neighbours(start);
    const auto twin   = mirrored(start, mirror);
    const auto count  = [&](std::uint8_t code)
    {
        return std::ranges::count_if(around, [&](std::size_t next)
                                     {
                                         return terrain[next] == code;
                                     });
    };
    // A tile whose mirror is also a neighbour counts twice.
    const auto weight = [&](std::size_t next)
    {
        const auto other = mirrored(next, mirror);
        return other != next && std::ranges::find(around, other) != around.end() ? 2 : 1;
    };
    const auto spare = [&](std::size_t next)
    {
        const auto code = terrain[next];
        return std::ranges::find(quotas, code) == quotas.end() || count(code) - weight(next) >= 2;
    };
    for (const auto code : quotas)
    {
        while (count(code) < 2)
        {
            // Land becomes land and sea becomes sea where it can, so coastlines move only when they must.
            const auto pick = [&](bool sameKind)
            {
                return std::ranges::find_if(around, [&](std::size_t next)
                                            {
                                                return next != twin && paintable(next) && terrain[next] != code && spare(next) && ((terrain[next] == kSea) == (code == kSea)) == sameKind;
                                            });
            };
            auto next = pick(true);
            if (next == around.end())
            {
                next = pick(false);
            }
            if (next == around.end())
            {
                return;
            }
            terrain[*next]                   = code;
            terrain[mirrored(*next, mirror)] = code;
        }
    }
}

// Six makes a good site.
int siteScore(const std::array<std::uint8_t, kMapCoreSize>& terrain, std::size_t index)
{
    int forest = 0;
    int flat   = 0;
    int water  = 0;
    for (const auto next : neighbours(index))
    {
        const auto code = terrain[next];
        forest += code == kForest ? 1 : 0;
        flat += code == kGrassland || code == kPlains ? 1 : 0;
        water += code == kSea || code == kDesert ? 1 : 0;
    }
    return std::min(forest, 2) + std::min(flat, 2) + std::min(water, 2);
}

bool isLand(std::uint8_t code)
{
    return code >= kGrassland && code <= kMountain;
}

bool isRough(std::uint8_t code)
{
    return code == kDesert || code == kHill || code == kMountain;
}

// North vs South's desert band across the equator, columns 13-18 at most, stays
// whole: like Oasis World's desert, it is past the group and run limits.
bool inBand(bool band, const std::array<std::uint8_t, kMapCoreSize>& terrain, std::size_t index)
{
    const int column = static_cast<int>(index % kMapWidth);
    return band && column >= 13 && column <= 18 && terrain[index] == kDesert;
}

// Measured from the retail DLC maps, leaving out Oasis World's desert and the mountain walls across three maps.
// Only Oasis World has a three by three block of one terrain.
constexpr TerrainLimits kRetailLimits{ { 0, 11, 9, 10, 4, 10, 16, 0 }, { 0, 6, 7, 6, 3, 5, 7, 0 }, { 0.0F, 0.7F, 0.57F, 0.4F, 0.0F, 0.0F, 0.0F, 0.0F }, { 3, 3, 3, 3, 3, 3, 3, 3 } };

std::array<float, 8> sharesOf(const Climate& climate)
{
    const float flat = std::max(0.0F, 1.0F - climate.mountain - climate.hill - climate.desert - climate.forest);
    return { 0.0F, flat * climate.grassland, flat * (1.0F - climate.grassland), climate.forest, climate.hill, climate.desert, climate.mountain, 0.0F };
}

int groupSize(const std::array<std::uint8_t, kMapCoreSize>& terrain, std::size_t index, int limit)
{
    const auto                     code = terrain[index];
    std::array<bool, kMapCoreSize> seen{};
    std::vector<std::size_t>       stack{ index };
    seen[index] = true;
    int size    = 0;
    while (!stack.empty() && size <= limit)
    {
        const auto tile = stack.back();
        stack.pop_back();
        ++size;
        for (const auto& step : kSteps)
        {
            const int r = static_cast<int>(tile / kMapWidth) + step[0];
            const int c = static_cast<int>(tile % kMapWidth) + step[1];
            if (inside(r, c) && !seen[at(r, c)] && terrain[at(r, c)] == code)
            {
                seen[at(r, c)] = true;
                stack.push_back(at(r, c));
            }
        }
    }
    return size;
}

int runThrough(const std::array<std::uint8_t, kMapCoreSize>& terrain, std::size_t index)
{
    const int row    = static_cast<int>(index / kMapWidth);
    const int column = static_cast<int>(index % kMapWidth);
    int       best   = 0;
    for (const auto& [dr, dc] : { std::pair{ 1, 0 }, std::pair{ 0, 1 } })
    {
        int length = 1;
        for (const int sign : { -1, 1 })
        {
            for (int r = row + sign * dr, c = column + sign * dc; inside(r, c) && terrain[at(r, c)] == terrain[index]; r += sign * dr, c += sign * dc)
            {
                ++length;
            }
        }
        best = std::max(best, length);
    }
    return best;
}

bool inBlock(const std::array<std::uint8_t, kMapCoreSize>& terrain, std::size_t index, int side)
{
    const int row    = static_cast<int>(index / kMapWidth);
    const int column = static_cast<int>(index % kMapWidth);
    for (int top = row - side + 1; top <= row; ++top)
    {
        for (int left = column - side + 1; left <= column; ++left)
        {
            bool block = inside(top, left) && inside(top + side - 1, left + side - 1);
            for (int r = top; block && r < top + side; ++r)
            {
                for (int c = left; block && c < left + side; ++c)
                {
                    block = terrain[at(r, c)] == terrain[index];
                }
            }
            if (block)
            {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::size_t> squares(const std::array<std::uint8_t, kMapCoreSize>& terrain, std::uint8_t code)
{
    std::vector<std::size_t> found;
    for (int r = 0; r + 1 < kSide; ++r)
    {
        for (int c = 0; c + 1 < kSide; ++c)
        {
            if (terrain[at(r, c)] == code && terrain[at(r + 1, c)] == code && terrain[at(r, c + 1)] == code && terrain[at(r + 1, c + 1)] == code)
            {
                found.push_back(at(r, c));
            }
        }
    }
    return found;
}

bool inSquare(const std::array<std::uint8_t, kMapCoreSize>& terrain, std::size_t index)
{
    const auto code   = terrain[index];
    const int  row    = static_cast<int>(index / kMapWidth);
    const int  column = static_cast<int>(index % kMapWidth);
    if (code < kGrassland || code > kForest)
    {
        return false;
    }
    for (int top = row - 1; top <= row; ++top)
    {
        for (int left = column - 1; left <= column; ++left)
        {
            if (inside(top, left) && inside(top + 1, left + 1) && terrain[at(top, left)] == code && terrain[at(top + 1, left)] == code && terrain[at(top, left + 1)] == code &&
                terrain[at(top + 1, left + 1)] == code)
            {
                return true;
            }
        }
    }
    return false;
}

template <typename Keeps>
void limitTerrain(std::array<std::uint8_t, kMapCoreSize>& terrain, const TerrainLimits& limits, MapMirror mirror, std::uint32_t seed, Keeps&& keeps)
{
    // A favoured terrain is also the likelier repaint, so its share holds.
    std::array<float, 8> favour{};
    for (std::size_t code = 0; code < favour.size(); ++code)
    {
        favour[code] = kRetailLimits.group[code] == 0 ? 1.0F : static_cast<float>(limits.group[code]) / static_cast<float>(kRetailLimits.group[code]);
    }
    // The allowance follows the coastline and seed, which the limits never change.
    std::uint32_t layout = seed;
    for (std::size_t index = 0; index < kMapCoreSize; ++index)
    {
        layout = isLand(terrain[index]) ? layout * 31U + static_cast<std::uint32_t>(index) : layout;
    }
    std::array<std::size_t, 4> allowed{};
    for (const std::uint8_t code : { kGrassland, kPlains, kForest })
    {
        const float odds = limits.squares[code];
        allowed[code]    = static_cast<std::size_t>(odds) + (unit(layout, 60, code, 0) < odds - std::floor(odds) ? 1 : 0);
    }
    const auto repaint = [&](std::size_t index)
    {
        const auto                  code   = terrain[index];
        const auto                  twin   = mirrored(index, mirror);
        const auto                  around = neighbours(index);
        std::optional<std::uint8_t> best;
        float                       bestScore = 0.0F;
        for (const std::uint8_t other : { kGrassland, kPlains, kForest, kHill, kDesert, kMountain })
        {
            const auto beside = std::ranges::count_if(around, [&](std::size_t next)
                                                      {
                                                          return terrain[next] == other;
                                                      });
            if (other == code || ((other == kDesert || other == kMountain) && beside == 0 && !isRough(code)) || limits.group[other] == 0 || !keeps(index, other))
            {
                continue;
            }
            terrain[index]    = other;
            terrain[twin]     = other;
            const bool fits   = groupSize(terrain, index, limits.group[other]) <= limits.group[other] && runThrough(terrain, index) <= limits.run[other] && !inBlock(terrain, index, limits.block[other]) &&
                                !inSquare(terrain, index);
            terrain[index]    = code;
            terrain[twin]     = code;
            const float score = static_cast<float>(beside + 1) / favour[other] + 0.01F * unit(seed, 41, static_cast<int>(index), other);
            if (fits && (!best || score < bestScore))
            {
                best      = other;
                bestScore = score;
            }
        }
        if (best)
        {
            terrain[index] = *best;
            terrain[twin]  = *best;
        }
        return best.has_value();
    };
    for (int pass = 0; pass < 48; ++pass)
    {
        std::vector<std::size_t> marked;
        bool                     grouped = false;
        for (std::size_t index = 0; index < kMapCoreSize; ++index)
        {
            const auto code = terrain[index];
            if (!isLand(code))
            {
                continue;
            }
            const auto same      = std::ranges::count_if(kSteps, [&](const auto& step)
                                                         {
                                                        const int r = static_cast<int>(index / kMapWidth) + step[0];
                                                        const int c = static_cast<int>(index % kMapWidth) + step[1];
                                                        return inside(r, c) && terrain[at(r, c)] == code;
                                                         });
            const bool oversized = same >= 2 && groupSize(terrain, index, limits.group[code]) > limits.group[code];
            grouped              = grouped || oversized;
            const int  spacing   = static_cast<int>(std::sqrt(static_cast<float>(limits.group[code])));
            const int  shift     = static_cast<int>(unit(seed, 44, pass, 0) * static_cast<float>(spacing));
            const bool line      = limits.group[code] > kRetailLimits.group[code]
                                       ? ((static_cast<int>(index / kMapWidth) + shift) % spacing == 0 || (static_cast<int>(index % kMapWidth) + shift) % spacing == 0)
                                       : unit(seed, 40U + static_cast<std::uint32_t>(pass), static_cast<int>(index), 0) < 0.4F;
            if ((oversized && line) || inBlock(terrain, index, limits.block[code]))
            {
                marked.push_back(index);
            }
        }
        for (const std::uint8_t code : { kGrassland, kPlains, kForest })
        {
            const auto found = squares(terrain, code);
            grouped          = grouped || found.size() > allowed[code];
            for (std::size_t k = allowed[code]; k < found.size(); ++k)
            {
                const int corner = pass % 4;
                marked.push_back(found[k] + static_cast<std::size_t>((corner / 2) * kMapWidth + corner % 2));
            }
        }
        // Shifted each pass so a kept tile does not stall the run.
        for (int line = 0; line < kSide; ++line)
        {
            for (const bool down : { false, true })
            {
                const auto tile = [&](int k)
                {
                    return down ? at(k, line) : at(line, k);
                };
                for (int begin = 0; begin < kSide;)
                {
                    int end = begin + 1;
                    while (end < kSide && terrain[tile(end)] == terrain[tile(begin)])
                    {
                        ++end;
                    }
                    const auto code = terrain[tile(begin)];
                    if (isLand(code) && end - begin > limits.run[code])
                    {
                        for (int k = begin + limits.run[code] - pass % limits.run[code]; k < end; k += limits.run[code])
                        {
                            marked.push_back(tile(k));
                        }
                    }
                    begin = end;
                }
            }
        }
        if (marked.empty() && !grouped)
        {
            return;
        }
        std::ranges::sort(marked);
        const auto [first, last] = std::ranges::unique(marked);
        marked.erase(first, last);
        // The hash picks other tiles of an oversized group on the next pass.
        for (const auto index : marked)
        {
            repaint(index);
        }
    }
}

// Sites keep at least three tiles from any start or other site. Only land changes, so coastlines stay.
void settleSites(std::array<std::uint8_t, kMapCoreSize>& terrain, const TerrainLimits& limits, bool band, const std::vector<std::size_t>& markers, MapMirror mirror, std::uint32_t seed)
{
    using Terrain = std::array<std::uint8_t, kMapCoreSize>;
    // Break ties between equally near starts by file order.
    auto ordered = markers;
    std::ranges::sort(ordered);
    const auto nearest = [&](std::size_t index)
    {
        return *std::ranges::min_element(ordered, {}, [&](std::size_t marker)
                                         {
                                             return chebyshev(index, marker);
                                         });
    };
    std::vector<std::size_t> sites;
    const auto               nearby = [&](std::size_t index, int distance)
    {
        return std::ranges::any_of(markers, [&](std::size_t other)
                                   {
                                       return chebyshev(index, other) <= distance;
                                   }) ||
               std::ranges::any_of(sites, [&](std::size_t other)
                                   {
                                       return chebyshev(index, other) <= distance || chebyshev(index, mirrored(other, mirror)) <= distance;
                                   });
    };

    struct Need
    {
        bool (*holds)(std::uint8_t);
        std::uint8_t paint;
    };

    // A tile gives way when its kind keeps two without it. Hills and mountains give way last.
    const auto repair = [&](Terrain& map, std::size_t site)
    {
        const auto       around = neighbours(site);
        const std::array needs{
            Need{ [](std::uint8_t code)
                  {
                      return code == kForest;
                  },
                  kForest },
            Need{ [](std::uint8_t code)
                  {
                      return code == kGrassland || code == kPlains;
                  },
                  unit(seed, 9, static_cast<int>(site), 0) < 0.6F ? kGrassland : kPlains },
            Need{ [](std::uint8_t code)
                  {
                      return code == kSea || code == kDesert;
                  },
                  kDesert },
        };
        const auto count = [&](const Need& need)
        {
            return std::ranges::count_if(around, [&](std::size_t next)
                                         {
                                             return need.holds(map[next]);
                                         });
        };
        const auto paintable = [&](std::size_t next, bool rough)
        {
            const auto code = map[next];
            const bool kept = code == kHill || code == kMountain ? rough
                                                                 : std::ranges::all_of(needs, [&](const Need& need)
                                                                                       {
                                                                                           return !need.holds(code) || count(need) > 2;
                                                                                       });
            return kept && isLand(code) && !inBand(band, map, next) && !nearby(next, 1) && !nearby(mirrored(next, mirror), 1);
        };
        for (const auto& need : needs)
        {
            while (count(need) < 2)
            {
                std::optional<std::size_t> pick;
                for (const bool rough : { false, true })
                {
                    for (const auto next : around)
                    {
                        if (!pick && !need.holds(map[next]) && paintable(next, rough))
                        {
                            pick = next;
                        }
                    }
                }
                // A climate without desert leaves sea the only water.
                if (!pick || limits.group[need.paint] == 0)
                {
                    return false;
                }
                map[*pick]                   = need.paint;
                map[mirrored(*pick, mirror)] = need.paint;
            }
        }
        return true;
    };
    for (const auto start : markers)
    {
        std::size_t spare = 0;
        std::size_t made  = 0;
        for (std::size_t index = 0; index < kMapCoreSize; ++index)
        {
            spare += isLand(terrain[index]) && nearest(index) == start && chebyshev(index, start) > 2 ? 1 : 0;
        }
        for (const auto site : sites)
        {
            made += nearest(site) == start ? 1 : 0;
            made += mirrored(site, mirror) != site && nearest(mirrored(site, mirror)) == start ? 1 : 0;
        }
        for (; made < std::min<std::size_t>(3, spare / 20); ++made)
        {
            std::vector<std::pair<float, std::size_t>> candidates;
            for (std::size_t index = 0; index < kMapCoreSize; ++index)
            {
                const int row    = static_cast<int>(index / kMapWidth);
                const int column = static_cast<int>(index % kMapWidth);
                if (isLand(terrain[index]) && terrain[index] != kMountain && nearest(index) == start && row >= 1 && row <= kSide - 2 && column >= 3 && column <= kSide - 4 &&
                    !nearby(index, 2) && !nearby(mirrored(index, mirror), 2) && (mirror == MapMirror::Off || chebyshev(index, mirrored(index, mirror)) >= 3))
                {
                    candidates.emplace_back(static_cast<float>(siteScore(terrain, index)) + 0.01F * unit(seed, 8, static_cast<int>(index), 0), index);
                }
            }
            std::ranges::sort(candidates, std::greater{});
            bool placed = false;
            for (const auto& [score, site] : candidates)
            {
                auto map = terrain;
                if (repair(map, site))
                {
                    terrain = map;
                    sites.push_back(site);
                    placed = true;
                    break;
                }
            }
            if (!placed)
            {
                break;
            }
        }
    }
    // Starts and sites keep their two of each need while this breaks up terrain again.
    std::vector<std::size_t> centres = markers;
    for (const auto site : sites)
    {
        centres.push_back(site);
        centres.push_back(mirrored(site, mirror));
    }
    const auto worth = [&](std::size_t centre)
    {
        if (std::ranges::find(markers, centre) == markers.end())
        {
            return siteScore(terrain, centre);
        }
        int grass  = 0;
        int sea    = 0;
        int forest = 0;
        for (const auto next : neighbours(centre))
        {
            grass += terrain[next] == kGrassland ? 1 : 0;
            sea += terrain[next] == kSea ? 1 : 0;
            forest += terrain[next] == kForest ? 1 : 0;
        }
        return std::min(grass, 2) + std::min(sea, 2) + std::min(forest, 2);
    };
    limitTerrain(terrain, limits, mirror, seed, [&](std::size_t index, std::uint8_t code)
                 {
                     const auto twin = mirrored(index, mirror);
                     if (std::ranges::find(centres, index) != centres.end() || std::ranges::find(centres, twin) != centres.end() ||
                         inBand(band, terrain, index))
                     {
                         return false;
                     }
                     std::vector<std::pair<std::size_t, int>> was;
                     for (const auto centre : centres)
                     {
                         if (chebyshev(centre, index) == 1 || chebyshev(centre, twin) == 1)
                         {
                             was.emplace_back(centre, worth(centre));
                         }
                     }
                     const auto before = terrain[index];
                     terrain[index]    = code;
                     terrain[twin]     = code;
                     const bool kept   = std::ranges::all_of(was, [&](const auto& place)
                                                             {
                                                               return worth(place.first) >= place.second;
                                                             });
                     terrain[index]    = before;
                     terrain[twin]     = before;
                     return kept;
                 });
}

// On a mirrored map the starts lie in the first half, each with a mirrored partner except an odd player's on the mirror line.
// Starts keep at least three tiles apart, so settling one never undoes another.
std::vector<std::size_t> placeStarts(std::array<std::uint8_t, kMapCoreSize>& terrain, StartingLand land, MapMirror mirror, int players, bool teams, std::uint32_t seed)
{
    std::vector<std::size_t> chosen;
    const bool               mirroredMap = mirror != MapMirror::Off;
    const bool               onLine      = mirroredMap && players % 2 == 1;
    const auto               wanted      = static_cast<std::size_t>(mirroredMap ? players / 2 + (onLine ? 1 : 0) : players);
    const auto               landTiles   = std::ranges::count_if(terrain, isLand);
    const int                spacing     = std::clamp(static_cast<int>(0.9F * std::sqrt(static_cast<float>(landTiles) / static_cast<float>(players))), 4, 10);
    const auto               roomy       = [](std::size_t index)
    {
        const int row    = static_cast<int>(index / kMapWidth);
        const int column = static_cast<int>(index % kMapWidth);
        return row >= 2 && row <= kSide - 3 && column >= 4 && column <= kSide - 5;
    };
    const auto line = [&](std::size_t index)
    {
        return roomy(index) && inFirstHalf(index, mirror) && chebyshev(index, mirrored(index, mirror)) <= 1;
    };
    const auto open = [&](std::size_t index)
    {
        const int row    = static_cast<int>(index / kMapWidth);
        const int column = static_cast<int>(index % kMapWidth);
        // Terra keeps the new world in the west empty.
        if (!roomy(index) || (land == StartingLand::Terra && row < 12))
        {
            return false;
        }
        // North vs South keeps its desert band, columns 13-18 at most, out of every neighbourhood.
        if (land == StartingLand::NorthVsSouth && column >= 12 && column <= 19)
        {
            return false;
        }
        // File order runs down each file row, so one row puts a top half team before the bottom one.
        if (teams && mirror == MapMirror::TopToBottom && !chosen.empty() && row != static_cast<int>(chosen.front() / kMapWidth))
        {
            return false;
        }
        return !mirroredMap || (inFirstHalf(index, mirror) && chebyshev(index, mirrored(index, mirror)) >= 3);
    };
    const float aimRow    = unit(seed, 6, 0, 0) * static_cast<float>(kSide);
    const float aimColumn = unit(seed, 6, 0, 1) * static_cast<float>(kSide);
    const auto  distance  = [](std::size_t left, std::size_t right)
    {
        return std::hypot(static_cast<float>(static_cast<int>(left / kMapWidth) - static_cast<int>(right / kMapWidth)),
                          static_cast<float>(static_cast<int>(left % kMapWidth) - static_cast<int>(right % kMapWidth)));
    };
    const auto inKind = [&](int kind, std::size_t index)
    {
        if (kind == 2)
        {
            return true;
        }
        const auto sea = std::ranges::count_if(neighbours(index), [&](std::size_t next)
                                               {
                                                   return terrain[next] == kSea;
                                               });
        return isLand(terrain[index]) && (kind == 1 || (sea >= 2 && sea <= 4));
    };
    // A fixed coastline tries the coast at every gap before other land.
    const bool fixedCoast = StartingLandOf(land).group == StartingLandGroup::RealWorld;
    const auto choose     = [&](const auto& fits, std::size_t count, int top)
    {
        std::vector<std::pair<int, int>> passes;
        for (int gap = top; gap >= 3; --gap)
        {
            passes.emplace_back(gap, 0);
            if (!fixedCoast)
            {
                passes.emplace_back(gap, 1);
            }
        }
        for (int gap = top; fixedCoast && gap >= 3; --gap)
        {
            passes.emplace_back(gap, 1);
        }
        for (int gap = top; gap >= 3; --gap)
        {
            passes.emplace_back(gap, 2);
        }
        for (const auto& [gap, kind] : passes)
        {
            while (chosen.size() < count)
            {
                std::optional<std::size_t> best;
                float                      bestScore = 0.0F;
                for (std::size_t index = 0; index < kMapCoreSize; ++index)
                {
                    if (!fits(index) || !inKind(kind, index))
                    {
                        continue;
                    }
                    std::vector<std::size_t> rivals;
                    std::vector<std::size_t> mates;
                    if (chebyshev(index, mirrored(index, mirror)) > 1)
                    {
                        rivals.push_back(mirrored(index, mirror));
                    }
                    for (const auto taken : chosen)
                    {
                        (teams ? mates : rivals).push_back(taken);
                        if (chebyshev(taken, mirrored(taken, mirror)) > 1)
                        {
                            rivals.push_back(mirrored(taken, mirror));
                        }
                    }
                    const bool clear = std::ranges::all_of(rivals, [&](std::size_t other)
                                                           {
                                                               return chebyshev(index, other) >= gap;
                                                           }) &&
                                       std::ranges::all_of(mates, [&](std::size_t other)
                                                           {
                                                               return chebyshev(index, other) >= std::min(gap, 4);
                                                           });
                    if (!clear)
                    {
                        continue;
                    }
                    float spread = rivals.empty() ? -0.25F * std::hypot(static_cast<float>(static_cast<int>(index / kMapWidth)) - aimRow,
                                                                        static_cast<float>(static_cast<int>(index % kMapWidth)) - aimColumn)
                                                  : std::max(2.0F * static_cast<float>(spacing), 16.0F);
                    for (const auto other : rivals)
                    {
                        spread = std::min(spread, distance(index, other));
                    }
                    for (const auto other : mates)
                    {
                        spread -= 0.5F * distance(index, other);
                    }
                    const float score = spread + 1.5F * static_cast<float>(siteScore(terrain, index)) + 0.01F * unit(seed, 7, static_cast<int>(index), 0);
                    if (!best || score > bestScore)
                    {
                        best      = index;
                        bestScore = score;
                    }
                }
                if (!best)
                {
                    break;
                }
                chosen.push_back(*best);
            }
        }
    };
    // A wide spacing can leave too little room for the last starts, so it narrows until all fit.
    for (int top = spacing; top >= 3; --top)
    {
        chosen.clear();
        if (onLine)
        {
            choose(line, 1, top);
        }
        choose(open, wanted, top);
        if (chosen.size() == wanted)
        {
            break;
        }
    }
    for (const auto start : chosen)
    {
        if (terrain[start] != kGrassland && terrain[start] != kPlains)
        {
            terrain[start] = kGrassland;
        }
        terrain[mirrored(start, mirror)] = terrain[start];
        settle(terrain, start, mirror, { kSea, kGrassland, kForest }, [](std::size_t)
               {
                   return true;
               });
    }
    auto markers = chosen;
    if (mirroredMap)
    {
        for (std::size_t index = onLine ? 1 : 0; index < chosen.size(); ++index)
        {
            markers.push_back(mirrored(chosen[index], mirror));
        }
    }
    settleSites(terrain, TerrainLimitsOf(land), land == StartingLand::NorthVsSouth, markers, mirror, seed);
    return markers;
}

} // namespace

MapDocument GenerateStartingLand(StartingLand land, LandSize size, std::uint32_t seed, MapMirror mirror, int players, bool teams)
{
    if (land == StartingLand::Shuffle)
    {
        std::vector<StartingLand> shuffled;
        for (const auto& info : kStartingLands)
        {
            if (StartingLandUsesSize(info.land) && (mirror == MapMirror::Off || StartingLandMirrors(info.land)))
            {
                shuffled.push_back(info.land);
            }
        }
        const auto pick   = static_cast<std::size_t>(unit(seed, 50, 0, 0) * static_cast<float>(shuffled.size()));
        const auto picked = static_cast<LandSize>(static_cast<int>(unit(seed, 50, 0, 1) * 3.0F));
        return GenerateStartingLand(shuffled[pick], picked, seed, mirror, players, teams);
    }

    MapDocument document{};
    document.footer.fill(std::byte{ 0xFF });
    std::array<int, kMapCoreSize> owner;
    owner.fill(-1);
    const auto sizeIndex = static_cast<std::size_t>(size);

    const auto scatter = [&](Grower& grower, int count, float spacing, std::size_t target, int first = 0, std::uint32_t salt = 3)
    {
        std::vector<std::array<int, 2>> centres;
        for (int attempt = 0; attempt < 400 && static_cast<int>(centres.size()) < count; ++attempt)
        {
            const int r = 3 + static_cast<int>(unit(seed, salt, attempt, 0) * static_cast<float>(kSide - 6));
            const int c = 5 + static_cast<int>(unit(seed, salt, attempt, 1) * static_cast<float>(kSide - 10));
            if (std::ranges::all_of(centres, [&](const auto& other)
                                    {
                                        return std::hypot(static_cast<float>(r - other[0]), static_cast<float>(c - other[1])) >= spacing;
                                    }))
            {
                centres.push_back({ r, c });
            }
        }
        for (std::size_t island = 0; island < centres.size(); ++island)
        {
            const float weight = 0.6F + 0.8F * unit(seed, 4, static_cast<int>(island), 0);
            const auto  share  = static_cast<std::size_t>(static_cast<float>(target) / static_cast<float>(centres.size()) * weight);
            grower.grow(first + static_cast<int>(island), centres[island][0], centres[island][1], std::max<std::size_t>(share, 4), 0.12F);
        }
    };

    switch (land)
    {
        case StartingLand::Pangaea:
        case StartingLand::Frontier:
        case StartingLand::Archipelago:
        case StartingLand::LargeIslands:
        case StartingLand::TinyIslands:
        case StartingLand::Continents:
        case StartingLand::SmallContinents:
        case StartingLand::TiltedAxis:
        case StartingLand::Arborea:
        case StartingLand::Sandstorm:
        case StartingLand::Boreal:
        {
            constexpr std::array kShare{ 0.22F, 0.32F, 0.44F };
            constexpr std::array kArchipelagoShare{ 0.16F, 0.21F, 0.26F };
            constexpr std::array kTinyShare{ 0.08F, 0.11F, 0.14F };
            constexpr std::array kContinentShare{ 0.26F, 0.34F, 0.42F };
            const bool           pangaea    = land == StartingLand::Pangaea || land == StartingLand::Frontier;
            const bool           continents = !pangaea && land != StartingLand::Archipelago && land != StartingLand::LargeIslands && land != StartingLand::TinyIslands;
            const float          share      = land == StartingLand::Archipelago   ? kArchipelagoShare[sizeIndex]
                                              : land == StartingLand::TinyIslands ? kTinyShare[sizeIndex]
                                              : continents                        ? kContinentShare[sizeIndex]
                                                                                  : kShare[sizeIndex];
            const auto           target     = static_cast<std::size_t>(share * static_cast<float>(kSide * (kSide - 4)));
            Grower               grower{ seed, owner };
            for (int r = 0; r < kSide; ++r)
            {
                for (int c = 0; c < kSide; ++c)
                {
                    grower.shape[at(r, c)] = noise(seed, 1, r, c, 7.0F);
                }
            }
            if (pangaea)
            {
                const int row    = 16 + static_cast<int>(unit(seed, 2, 0, 0) * 7.0F) - 3;
                const int column = 16 + static_cast<int>(unit(seed, 2, 0, 1) * 5.0F) - 2;
                grower.grow(0, row, column, target, 0.045F);
            }
            else if (land == StartingLand::Archipelago)
            {
                constexpr std::array kCount{ 9, 12, 15 };
                scatter(grower, kCount[sizeIndex], 5.5F, target);
            }
            else if (land == StartingLand::TinyIslands)
            {
                constexpr std::array kCount{ 16, 20, 24 };
                scatter(grower, kCount[sizeIndex], 4.0F, target);
            }
            else if (continents)
            {
                const bool several = land == StartingLand::SmallContinents;
                const auto large   = target * 85 / 100;
                const int  masses  = several ? 6 : 3;
                scatter(grower, masses, several ? 8.0F : 11.0F, large);
                scatter(grower, 4, 5.0F, target - large, masses, 9);
            }
            else
            {
                constexpr std::array kCount{ 4, 6, 8 };
                scatter(grower, kCount[sizeIndex], 8.0F, target);
            }
            break;
        }
        case StartingLand::Earth:
        case StartingLand::JapaneseMainland:
        case StartingLand::BritishIsles:
        case StartingLand::Mediterranean:
        case StartingLand::Americas:
        case StartingLand::Europe:
        case StartingLand::Africa:
        case StartingLand::Australia:
        case StartingLand::Caribbean:
            landFromStencil(owner, *std::ranges::find(kPlaces, land, &std::pair<StartingLand, const Stencil*>::first)->second);
            break;
        case StartingLand::Blank:
        case StartingLand::Shuffle:
            break;
        default:
        {
            struct Shares
            {
                StartingLand         land;
                std::array<float, 3> share;
            };

            constexpr std::array kShapes{
                Shares{ StartingLand::Hemispheres, { 0.30F, 0.40F, 0.50F } },
                Shares{ StartingLand::FourCorners, { 0.28F, 0.36F, 0.44F } },
                Shares{ StartingLand::InlandSea, { 0.55F, 0.65F, 0.75F } },
                Shares{ StartingLand::Lakes, { 0.80F, 0.86F, 0.92F } },
                Shares{ StartingLand::NorthVsSouth, { 0.70F, 0.80F, 0.90F } },
                Shares{ StartingLand::WestVsEast, { 0.50F, 0.60F, 0.70F } },
                Shares{ StartingLand::Donut, { 0.30F, 0.38F, 0.46F } },
                Shares{ StartingLand::Oval, { 0.35F, 0.45F, 0.55F } },
                Shares{ StartingLand::Fractal, { 0.30F, 0.38F, 0.46F } },
                Shares{ StartingLand::Ring, { 0.32F, 0.40F, 0.48F } },
                Shares{ StartingLand::Skirmish, { 0.35F, 0.45F, 0.55F } },
                Shares{ StartingLand::Terra, { 0.35F, 0.45F, 0.55F } },
                Shares{ StartingLand::IceAge, { 0.22F, 0.28F, 0.34F } },
                Shares{ StartingLand::Highlands, { 0.60F, 0.70F, 0.80F } },
                Shares{ StartingLand::GreatPlains, { 0.60F, 0.70F, 0.80F } },
            };
            const auto shape = std::ranges::find(kShapes, land, &Shares::land);
            landFromScores(owner, shapeScores(land, seed), shape->share[sizeIndex]);
            break;
        }
    }
    const bool keepsInlandWater = land == StartingLand::InlandSea || land == StartingLand::Lakes || land == StartingLand::Donut || land == StartingLand::Ring ||
                                  StartingLandOf(land).group == StartingLandGroup::RealWorld;
    if (!keepsInlandWater)
    {
        fillLakes(owner);
    }

    std::array<std::uint8_t, kMapCoreSize> terrain{};
    std::vector<std::size_t>               landTiles;
    for (std::size_t index = 0; index < kMapCoreSize; ++index)
    {
        if (owner[index] != -1)
        {
            landTiles.push_back(index);
        }
    }
    paintTerrain(terrain, landTiles, climateOf(land), seed);
    if (land == StartingLand::NorthVsSouth)
    {
        for (const auto index : landTiles)
        {
            const int   row   = static_cast<int>(index / kMapWidth);
            const float depth = 1.0F + 2.0F * noise(seed, 41, row, 0, 4.0F);
            if (std::abs(static_cast<float>(index % kMapWidth) - 15.5F) < depth)
            {
                terrain[index] = kDesert;
            }
        }
    }
    const auto axis = land == StartingLand::Skirmish ? MapMirror::LeftToRight : StartingLandMirrors(land) ? mirror
                                                                                                          : MapMirror::Off;
    if (axis != MapMirror::Off)
    {
        for (std::size_t index = 0; index < kMapCoreSize; ++index)
        {
            if (!inFirstHalf(index, axis))
            {
                terrain[index] = terrain[mirrored(index, axis)];
            }
        }
    }
    for (int r = 0; r < kSide; ++r)
    {
        for (int c : { 0, 1, kSide - 2, kSide - 1 })
        {
            terrain[at(r, c)] = kPolarIce;
        }
    }
    // Polar ice spreads two to four more tiles over open sea, the same on both sides of either mirror.
    if (land == StartingLand::IceAge)
    {
        for (int r = 0; r < kSide; ++r)
        {
            const float drift = noise(seed, 43, std::min(r, kSide - 1 - r), 0, 3.0F);
            const int   reach = 2 + (drift > 0.45F ? 1 : 0) + (drift > 0.8F ? 1 : 0);
            for (int c = 2; c < kSide - 2; ++c)
            {
                const auto index = at(r, c);
                const bool polar = c < 2 + reach || c >= kSide - 2 - reach;
                if (polar && terrain[index] == kSea && std::ranges::none_of(neighbours(index), [&](std::size_t next)
                                                                            {
                                                                                return isLand(terrain[next]);
                                                                            }))
                {
                    terrain[index] = kPolarIce;
                }
            }
        }
    }
    limitTerrain(terrain, TerrainLimitsOf(land), axis, seed, [&](std::size_t index, std::uint8_t)
                 {
                     return !inBand(land == StartingLand::NorthVsSouth, terrain, index);
                 });
    const auto markers = land == StartingLand::Blank ? std::vector<std::size_t>() : placeStarts(terrain, land, axis, std::clamp(players, kMinimumPlayers, kMaximumPlayers), teams, seed);
    for (std::size_t index = 0; index < kMapCoreSize; ++index)
    {
        const bool marker    = std::ranges::find(markers, index) != markers.end();
        document.core[index] = static_cast<std::byte>(terrain[index] | (marker ? kMarker : 0U));
    }
    return document;
}

void RandomizeLandTerrain(MapDocument& document, std::uint32_t seed)
{
    std::array<std::uint8_t, kMapCoreSize> terrain{};
    std::array<bool, kMapCoreSize>         land{};
    std::vector<std::size_t>               landTiles;
    for (std::size_t index = 0; index < kMapCoreSize; ++index)
    {
        terrain[index] = std::to_integer<std::uint8_t>(document.core[index]) & 0x0FU;
        if (terrain[index] >= kGrassland && terrain[index] <= kMountain)
        {
            land[index] = true;
            landTiles.push_back(index);
        }
    }
    const auto symmetric = [&](MapMirror axis)
    {
        return !landTiles.empty() && std::ranges::all_of(landTiles, [&](std::size_t index)
                                                         {
                                                             return land[mirrored(index, axis)];
                                                         });
    };
    const auto axis = symmetric(MapMirror::LeftToRight) ? MapMirror::LeftToRight : symmetric(MapMirror::TopToBottom) ? MapMirror::TopToBottom
                                                                                                                     : MapMirror::Off;
    paintTerrain(terrain, landTiles, Climate{}, seed);
    for (const auto index : landTiles)
    {
        if (!inFirstHalf(index, axis))
        {
            terrain[index] = terrain[mirrored(index, axis)];
        }
    }
    const auto marker = [&](std::size_t index)
    {
        return (std::to_integer<std::uint8_t>(document.core[index]) & kMarker) != 0;
    };
    limitTerrain(terrain, kRetailLimits, axis, seed, [&](std::size_t index, std::uint8_t)
                 {
                     return !marker(index) && !marker(mirrored(index, axis));
                 });
    for (const auto start : landTiles)
    {
        const auto twin = mirrored(start, axis);
        if (!marker(start) || (!inFirstHalf(start, axis) && marker(twin)))
        {
            continue;
        }
        if (terrain[start] != kGrassland && terrain[start] != kPlains)
        {
            terrain[start] = kGrassland;
            terrain[twin]  = kGrassland;
        }
        // Hand placed starts may share neighbours. A shared tile gives way only
        // when every other start beside it keeps two of its terrain.
        const auto keepsOthers = [&](std::size_t next)
        {
            const auto code = terrain[next];
            if (code != kGrassland && code != kForest)
            {
                return true;
            }
            return std::ranges::all_of(neighbours(next), [&](std::size_t other)
                                       {
                                           const auto around = neighbours(other);
                                           return other == start || !marker(other) || !land[other] ||
                                                  std::ranges::count_if(around, [&](std::size_t tile)
                                                                        {
                                                                            return terrain[tile] == code;
                                                                        }) >= 3;
                                       });
        };
        settle(terrain, start, axis, { kGrassland, kForest }, [&](std::size_t next)
               {
                   const auto other = mirrored(next, axis);
                   return land[next] && !marker(next) && !marker(other) && keepsOthers(next) && keepsOthers(other);
               });
    }
    std::vector<std::size_t> markers;
    for (const auto index : landTiles)
    {
        if (marker(index))
        {
            markers.push_back(index);
        }
    }
    settleSites(terrain, kRetailLimits, false, markers, axis, seed);
    for (const auto index : landTiles)
    {
        document.core[index] = static_cast<std::byte>((std::to_integer<std::uint8_t>(document.core[index]) & 0xF0U) | terrain[index]);
    }
}

TerrainLimits TerrainLimitsOf(StartingLand land)
{
    auto limits = kRetailLimits;
    if (StartingLandOf(land).group != StartingLandGroup::Climates)
    {
        return limits;
    }
    const auto shares   = sharesOf(climateOf(land));
    const auto standard = sharesOf(Climate{});
    // The terrain a land is named for forms as many two by two blocks as it likes.
    const bool         forested = land == StartingLand::Arborea || land == StartingLand::Boreal;
    const std::uint8_t themed   = forested ? kForest : (land == StartingLand::GreatPlains ? kPlains : kSea);
    for (std::size_t code = kGrassland; code <= kMountain; ++code)
    {
        // A share r times the usual lengthens runs r times, widens groups r squared and makes r to the fourth more two by two blocks.
        const float ratio    = std::max(1.0F, shares[code] / standard[code]);
        limits.run[code]     = static_cast<int>(std::lround(static_cast<float>(kRetailLimits.run[code]) * ratio));
        limits.group[code]   = shares[code] == 0.0F && code >= kForest ? 0 : static_cast<int>(std::lround(static_cast<float>(kRetailLimits.group[code]) * ratio * ratio));
        limits.squares[code] = code == themed ? static_cast<float>(kMapCoreSize) : kRetailLimits.squares[code] * ratio * ratio * ratio * ratio;
        // Solid squares may grow to four by four, never five by five.
        limits.block[code] = std::min(5, static_cast<int>(std::lround(3.0F * std::sqrt(ratio))));
    }
    return limits;
}

std::uint32_t NextLayoutSeed(std::uint32_t seed) noexcept
{
    return static_cast<std::uint32_t>(mix(seed) % 9000U) + 1000U;
}

void AddMapPackUsage(MapPackUsage& usage, const std::filesystem::path& mapsRoot)
{
    constexpr std::size_t kEntries = 4096;
    std::error_code       error;
    const auto            root = std::filesystem::status(mapsRoot, error).type();
    if (root != std::filesystem::file_type::directory)
    {
        usage.complete = usage.complete && root == std::filesystem::file_type::not_found;
        return;
    }
    std::size_t visited = 0;
    // One budget for every table record and file byte read limits what a hostile folder can make the page read.
    std::uint64_t budget = 64ULL << 20;
    const auto    afford = [&](std::uint64_t cost)
    {
        if (cost > budget)
        {
            budget         = 0;
            usage.complete = false;
            return false;
        }
        budget -= cost;
        return true;
    };
    for (std::filesystem::directory_iterator it(mapsRoot, error), end; !error && it != end; it.increment(error))
    {
        if (++visited > kEntries)
        {
            usage.complete = false;
            break;
        }
        // Export refuses any existing name, so files and links reserve theirs too.
        usage.packIds.push_back(utf8(it->path().filename()));
        if (!isDirectory(it->path()) || budget == 0)
        {
            continue;
        }
        const auto pack = it->path();
        // No component below the pack folder may be a link or junction.
        const auto      manifestPath = pack / "asset-pack.toml";
        std::error_code sizeError;
        if (isRegular(manifestPath) && afford(std::filesystem::file_size(manifestPath, sizeError)))
        {
            const auto manifest = readSmallFile(manifestPath);
            if (!manifest)
            {
                usage.complete = false;
            }
            addManifestIds(usage, manifest ? *manifest : std::string());
        }
        const auto archive = pack / "assets" / "map-pack" / "dlc.fpk";
        if (!isDirectory(pack / "assets") || !isDirectory(pack / "assets" / "map-pack") || !isRegular(archive))
        {
            continue;
        }
        const auto entries = readTable(archive, budget);
        if (!entries)
        {
            usage.complete = false;
            continue;
        }
        for (const auto& entry : *entries)
        {
            const auto name = lower(entry.name);
            if (name.ends_with(".map"))
            {
                usage.mapNames.push_back(entry.name.substr(0, entry.name.size() - 4));
            }
            else if (const auto suffix = std::ranges::find_if(kMapTextureSuffixes, [&](std::string_view ending)
                                                              {
                                                                  return name.size() > 3 + ending.size() && name.starts_with("map") && name.ends_with(ending);
                                                              });
                     suffix != kMapTextureSuffixes.end())
            {
                // Map<MAP>_Heights.dds and its siblings claim the name as well.
                usage.mapNames.push_back(entry.name.substr(3, entry.name.size() - 3 - suffix->size()));
            }
            else if (name.starts_with("dlcscenariodata") && name.ends_with(".xml") && afford(entry.size))
            {
                if (const auto slot = parseId(std::string_view(name).substr(15, name.size() - 19)); slot && *slot >= 0)
                {
                    usage.slots.push_back(static_cast<std::uint32_t>(*slot));
                }
                const auto bytes = ReadFpkTableEntry(entry, kScenarioMaximumXmlBytes);
                if (!bytes)
                {
                    usage.complete = false;
                    continue;
                }
                addScenarioTags(usage, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
            }
        }
    }
    if (error)
    {
        usage.complete = false;
    }
}

std::string MapPackIdFromName(std::string_view name)
{
    std::string result;
    for (const char character : name)
    {
        const char c = character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            result += c;
        }
        else if (!result.empty() && result.back() != '-')
        {
            result += '-';
        }
    }
    result = result.substr(0, kMapPackMaxIdLength);
    while (!result.empty() && result.back() == '-')
    {
        result.pop_back();
    }
    // Windows cannot create folders with device names.
    constexpr std::array<std::string_view, 4> kDevices{ "con", "prn", "aux", "nul" };
    const bool                                numbered = result.size() == 4 && (result.starts_with("com") || result.starts_with("lpt")) && result[3] >= '1' && result[3] <= '9';
    if (std::ranges::contains(kDevices, result) || numbered)
    {
        result += "-map";
    }
    return result;
}

std::string MapNameProblem(std::string_view name)
{
    if (name.size() > kScenarioMaximumLiteralBytes)
    {
        return "Use at most " + std::to_string(kScenarioMaximumLiteralBytes) + " characters.";
    }
    const bool plain = std::ranges::all_of(name, [](unsigned char c)
                                           {
                                               return c >= 0x20 && c <= 0x7E && c != '&' && c != '<' && c != '>';
                                           });
    return plain ? std::string()
                 : std::string("Use plain English letters, digits and punctuation, "
                               "without & < or >. "
                               "The game's scenario files allow no others.");
}

MapIdentity SuggestMapIdentity(std::string_view name, const MapPackUsage& usage)
{
    MapIdentity identity;
    // Scenario titles may not hold runs of spaces, so they collapse to one.
    for (const char character : trim(name))
    {
        const char c = character == '\t' ? ' ' : character;
        if (c != ' ' || identity.title.empty() || identity.title.back() != ' ')
        {
            identity.title += c;
        }
    }

    std::unordered_set<std::string> packIds;
    std::unordered_set<std::string> mapNames;
    for (const auto& id : usage.packIds)
    {
        packIds.insert(lower(id));
    }
    for (const auto& map : usage.mapNames)
    {
        mapNames.insert(lower(map));
    }
    const std::unordered_set<std::int32_t> scenarioIds(usage.scenarioIds.begin(), usage.scenarioIds.end());

    auto base = MapPackIdFromName(identity.title);
    if (base.empty())
    {
        base = "my-map";
    }
    identity.packId = base;
    for (int suffix = 2; packIds.contains(identity.packId); ++suffix)
    {
        const auto tail = "-" + std::to_string(suffix);
        identity.packId = base.substr(0, kMapPackMaxIdLength - tail.size());
        while (!identity.packId.empty() && identity.packId.back() == '-')
        {
            identity.packId.pop_back();
        }
        identity.packId += tail;
        identity.packIdSuffixed = true;
    }

    identity.scenarioId = kMapPackFirstScenarioId;
    while (scenarioIds.contains(identity.scenarioId))
    {
        ++identity.scenarioId;
    }

    std::string letters;
    for (const char character : identity.title)
    {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z'))
        {
            letters += static_cast<char>(character >= 'a' ? character - 'a' + 'A' : character);
        }
    }
    for (char letter = 'A'; letter <= 'Z'; ++letter)
    {
        letters += letter;
    }
    const auto free = std::ranges::find_if(letters, [&](char letter)
                                           {
                                               return !mapNames.contains(std::string(1, static_cast<char>(letter - 'A' + 'a')));
                                           });
    if (free != letters.end())
    {
        identity.mapName = std::string(1, *free);
    }

    const std::unordered_set<std::uint32_t> slots(usage.slots.begin(), usage.slots.end());
    for (auto slot = kMapPackFirstSlot; slot <= kMapPackLastSlot; ++slot)
    {
        if (!slots.contains(slot))
        {
            identity.slot = slot;
            break;
        }
    }
    return identity;
}

MapIdentity RepickClashingIdentity(const MapIdentity& current, const MapPackUsage& folder, const MapPackUsage& everything)
{
    auto avoided = everything;
    avoided.packIds.insert(avoided.packIds.end(), folder.packIds.begin(), folder.packIds.end());
    avoided.scenarioIds.insert(avoided.scenarioIds.end(), folder.scenarioIds.begin(), folder.scenarioIds.end());
    avoided.mapNames.insert(avoided.mapNames.end(), folder.mapNames.begin(), folder.mapNames.end());
    avoided.slots.insert(avoided.slots.end(), folder.slots.begin(), folder.slots.end());
    const auto picked = SuggestMapIdentity(current.title, avoided);

    const auto holdsText = [](const std::vector<std::string>& values, const std::string& wanted)
    {
        return std::ranges::any_of(values, [&](const std::string& value)
                                   {
                                       return lower(value) == lower(wanted);
                                   });
    };
    auto result = current;
    if (std::ranges::contains(folder.slots, *current.slot))
    {
        result.slot = picked.slot;
    }
    if (std::ranges::contains(folder.scenarioIds, current.scenarioId))
    {
        result.scenarioId = picked.scenarioId;
    }
    if (!current.mapName.empty() && holdsText(folder.mapNames, current.mapName))
    {
        result.mapName = picked.mapName;
    }
    if (holdsText(folder.packIds, current.packId))
    {
        result.packId = picked.packId;
    }
    return result;
}

} // namespace rerevved::tools
