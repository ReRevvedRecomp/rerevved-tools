#include "terrain_dds.h"
#include "terrain_generator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{

using namespace rerevved::tools;
int failures = 0;

void expectValue(double value, bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << " (" << value << ")\n";
        ++failures;
    }
}

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

using Core = std::array<std::byte, kMapCoreSize>;

Core filled(std::uint8_t code)
{
    Core core;
    core.fill(std::byte{ code });
    return core;
}

double tileHeight(const GeneratedTerrain& terrain, int i, int j)
{
    double sum = 0;
    for (int y = i * 16; y < i * 16 + 16; ++y)
    {
        for (int x = j * 16; x < j * 16 + 16; ++x)
        {
            sum += terrain.heights[std::size_t(y) * 512 + x];
        }
    }
    return sum / 256.0;
}

double tileChannel(const Rgba8Image& image, int i, int j, int channel)
{
    const int per = static_cast<int>(image.width) / 32;
    double    sum = 0;
    for (int y = i * per; y < (i + 1) * per; ++y)
    {
        for (int x = j * per; x < (j + 1) * per; ++x)
        {
            sum += std::to_integer<int>(image.rgba8[(std::size_t(y) * image.width + x) * 4 + channel]);
        }
    }
    return sum / (per * per);
}

void testSizesAndDeterminism()
{
    const auto land  = filled(2);
    const auto first = GenerateTerrainFromTiles(land);
    const auto again = GenerateTerrainFromTiles(land);
    expect(first.heights.size() == 512 * 512 && first.blends.width == kMountainHillBlendsSide && first.blends.height == kMountainHillBlendsSide &&
               first.blends.rgba8.size() == std::size_t(2048) * 2048 * 4 && first.lightmap.width == kLightmapSide && first.lightmap.rgba8.size() == std::size_t(4096) * 4096 * 4,
           "generated images have the shipped sides");
    expect(first.heights == again.heights && first.blends.rgba8 == again.blends.rgba8 && first.lightmap.rgba8 == again.lightmap.rgba8, "the same tiles generate the same images");
    expect(EncodeTerrainDds(TerrainTexture::MountainHillBlends, first.blends).has_value() && EncodeTerrainDds(TerrainTexture::Lightmap, first.lightmap).has_value(),
           "generated images encode as terrain DDS");
    expect(std::ranges::all_of(std::array{ 5, 16, 26 }, [&](int i)
                               {
                                   return std::abs(tileHeight(first, i, i) - 28500) < 2000;
                               }),
           "flat land sits near the shipped land level");
    expect(tileChannel(first.blends, 16, 16, 0) < 1 && tileChannel(first.blends, 16, 16, 1) < 1, "flat land carries no blend");
    const auto light = tileChannel(first.lightmap, 16, 16, 0);
    expect(light > 175 && light < 215, "flat land lightmap stays near the shipped 194");
}

void testSeaDepthAndCoast()
{
    const auto sea = GenerateTerrainFromTiles(filled(0));
    expect(std::ranges::all_of(sea.heights, [](std::uint16_t h)
                               {
                                   return h < 4000;
                               }),
           "open sea far from land is deep");

    auto core = filled(0);
    for (std::size_t r = 0; r < 32; ++r)
    {
        for (std::size_t c = 0; c < 16; ++c)
        {
            core[r * 32 + c] = std::byte{ 1 };
        }
    }
    const auto coast = GenerateTerrainFromTiles(core);
    expect(tileHeight(coast, 8, 10) > 26000, "land rows stay above the sea");
    expectValue(tileHeight(coast, 16, 10), std::abs(tileHeight(coast, 16, 10) - 20500) < 2500, "sea next to land is shallow");
    expectValue(tileHeight(coast, 17, 10), std::abs(tileHeight(coast, 17, 10) - 11000) < 3500, "sea two tiles out is deeper");
    expect(tileHeight(coast, 25, 10) < 4000, "sea three or more tiles out is deep");
}

void testPeaksMasksAndTranspose()
{
    auto core = filled(2);
    // High bits are set so only the low nibble selects the kind.
    core[5 * 32 + 20]  = std::byte{ 0x16 };
    core[24 * 32 + 9]  = std::byte{ 0x34 };
    const auto terrain = GenerateTerrainFromTiles(core);
    expectValue(tileHeight(terrain, 20, 5), tileHeight(terrain, 20, 5) > 37000, "a lone mountain lands at image row = file column, image column = file row");
    expect(tileHeight(terrain, 5, 20) < 31000, "the untransposed position stays flat");
    const auto hill = tileHeight(terrain, 9, 24);
    expect(hill > 30500 && hill < 37000, "a hill rises between flat land and mountain");
    expect(tileChannel(terrain.blends, 9, 24, 0) > 100 && tileChannel(terrain.blends, 9, 24, 1) < 10, "hills blend red");
    expectValue(tileChannel(terrain.blends, 20, 5, 1), tileChannel(terrain.blends, 20, 5, 1) > 120 && tileChannel(terrain.blends, 20, 5, 0) < 10, "a lone mountain blends green");
    expect(tileChannel(terrain.lightmap, 20, 5, 0) < tileChannel(terrain.lightmap, 16, 16, 0), "mountains are shaded darker than flat land");
}

void testMountainRanges()
{
    auto core = filled(2);
    for (int row = 10; row < 13; ++row)
    {
        for (int column = 10; column < 13; ++column)
        {
            core[std::size_t(row) * 32 + column] = std::byte{ 6 };
        }
    }
    const auto terrain = GenerateTerrainFromTiles(core);
    expectValue(tileChannel(terrain.blends, 11, 11, 2), tileChannel(terrain.blends, 11, 11, 2) > 100, "the middle of a range blends blue");
    expectValue(tileChannel(terrain.blends, 10, 10, 1), tileChannel(terrain.blends, 10, 10, 1) > 100, "the rim of a range blends green");
    expect(tileHeight(terrain, 11, 11) > tileHeight(terrain, 10, 10), "a range is highest in the middle");
}

} // namespace

int main()
{
    testSizesAndDeterminism();
    testSeaDepthAndCoast();
    testPeaksMasksAndTranspose();
    testMountainRanges();
    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "terrain generator tests passed\n";
    return 0;
}
