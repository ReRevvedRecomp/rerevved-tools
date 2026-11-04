#include "terrain_dds.h"
#include "xbox_texture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace rerevved::tools;
using Error  = TerrainDdsError;
int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename T>
void expectError(const std::expected<T, Error>& result, Error error, std::string_view message)
{
    expect(!result && result.error() == error, message);
}

std::uint32_t u32(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8);
    }
    return value;
}

Rgba8Image quadrants(std::uint32_t side)
{
    Rgba8Image image;
    image.width           = side;
    image.height          = side;
    image.hasAlphaChannel = true;
    image.sourceLayout    = PngSourceLayout{ 6, 8 };
    image.rgba8.resize(std::size_t(side) * side * 4);
    constexpr std::array<std::array<std::uint8_t, 3>, 4> kColors{ { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 255 } } };
    for (std::uint32_t y = 0; y < side; ++y)
    {
        for (std::uint32_t x = 0; x < side; ++x)
        {
            const auto& color = kColors[(y >= side / 2 ? 2 : 0) + (x >= side / 2 ? 1 : 0)];
            const auto  pixel = (std::size_t(y) * side + x) * 4;
            for (unsigned c = 0; c < 3; ++c)
            {
                image.rgba8[pixel + c] = std::byte{ color[c] };
            }
            image.rgba8[pixel + 3] = std::byte{ 0 };
        }
    }
    return image;
}

std::array<std::uint8_t, 4> texel(const Rgba8Image& image, std::uint32_t x, std::uint32_t y)
{
    const auto pixel = (std::size_t(y) * image.width + x) * 4;
    return { std::to_integer<std::uint8_t>(image.rgba8[pixel]), std::to_integer<std::uint8_t>(image.rgba8[pixel + 1]), std::to_integer<std::uint8_t>(image.rgba8[pixel + 2]), std::to_integer<std::uint8_t>(image.rgba8[pixel + 3]) };
}

void testEncoding(TerrainTexture role, std::uint32_t side, std::size_t byteCount)
{
    const std::string label(TerrainTextureLabel(role));
    expect(TerrainTextureSide(role) == side && TerrainDdsByteCount(role) == byteCount, label + " size constants match the census");
    const auto image = quadrants(side);
    const auto bytes = EncodeTerrainDds(role, image);
    expect(bytes && bytes->size() == byteCount, label + " is one DXT1 level after the header");
    if (!bytes || bytes->size() != byteCount)
    {
        return;
    }
    // Header words restated independently from the shipped-file census.
    for (const auto [offset, value] : { std::pair{ 0U, 0x20534444U }, std::pair{ 4U, 124U }, std::pair{ 8U, 0x1007U }, std::pair{ 12U, side }, std::pair{ 16U, side }, std::pair{ 76U, 32U }, std::pair{ 80U, 4U }, std::pair{ 84U, 0x31545844U }, std::pair{ 108U, 0x1000U } })
    {
        expect(u32(*bytes, offset) == value, label + " header word at " + std::to_string(offset));
    }
    constexpr std::array kNamed{ 0U, 4U, 8U, 12U, 16U, 76U, 80U, 84U, 108U };
    for (std::size_t offset = 0; offset < 128; offset += 4)
    {
        expect(std::ranges::find(kNamed, offset) != kNamed.end() || u32(*bytes, offset) == 0, label + " unnamed header word " + std::to_string(offset) + " is zero");
    }
    const auto blocks = ParseTerrainDds(role, *bytes);
    expect(blocks && blocks->size() == byteCount - 128, label + " parser returns the block payload");
    if (!blocks)
    {
        return;
    }
    // Opaque four-colour DXT1 keeps c0 > c1 in every block, as shipped files do.
    bool fourColour = true;
    for (std::size_t block = 0; block < blocks->size(); block += 8)
    {
        fourColour = fourColour && (u32(*blocks, block) & 0xFFFFU) > (u32(*blocks, block) >> 16);
    }
    expect(fourColour, label + " uses opaque four-colour blocks only");
    const auto decoded = DecodeBlocks(*blocks, side, side, XboxTextureFormat::Dxt1);
    expect(decoded.has_value(), label + " decodes as DXT1");
    if (!decoded)
    {
        return;
    }
    const auto q = side / 4;
    expect(texel(*decoded, q, q) == std::array<std::uint8_t, 4>{ 255, 0, 0, 255 }, label + " top-left stays red and opaque");
    expect(texel(*decoded, side - q, q) == std::array<std::uint8_t, 4>{ 0, 255, 0, 255 }, label + " top-right stays green");
    expect(texel(*decoded, q, side - q) == std::array<std::uint8_t, 4>{ 0, 0, 255, 255 }, label + " bottom-left stays blue");
    expect(texel(*decoded, side - q, side - q) == std::array<std::uint8_t, 4>{ 255, 255, 255, 255 }, label + " bottom-right stays white");
}

void testRejections()
{
    const auto role = TerrainTexture::MountainHillBlends;
    expectError(EncodeTerrainDds(role, quadrants(1024)), Error::DimensionMismatch, "half-size blends image rejected");
    expectError(EncodeTerrainDds(TerrainTexture::Lightmap, quadrants(2048)), Error::DimensionMismatch, "blends-size lightmap image rejected");
    auto wrongBuffer = quadrants(2048);
    wrongBuffer.rgba8.pop_back();
    expectError(EncodeTerrainDds(role, wrongBuffer), Error::DimensionMismatch, "short pixel buffer rejected");

    const auto bytes = *EncodeTerrainDds(role, quadrants(2048));
    expectError(ParseTerrainDds(role, std::span(bytes).first(bytes.size() - 1)), Error::LengthMismatch, "truncated payload rejected");
    expectError(ParseTerrainDds(TerrainTexture::Lightmap, bytes), Error::LengthMismatch, "blends file is not a lightmap");
    for (std::size_t offset = 0; offset < 128; ++offset)
    {
        auto changed = bytes;
        changed[offset] ^= std::byte{ 1 };
        expectError(ParseTerrainDds(role, changed), Error::HeaderMismatch, "header byte " + std::to_string(offset) + " is checked");
    }
}

void testFileWriting(const std::filesystem::path& scratch)
{
    const auto      root = scratch / ("rerevved-tools-terrain-dds-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    const auto      created = std::filesystem::create_directory(root, error);
    expect(created && !error, "create fresh terrain test directory");
    if (!created || error)
    {
        return;
    }
    const auto role        = TerrainTexture::MountainHillBlends;
    const auto bytes       = *EncodeTerrainDds(role, quadrants(2048));
    const auto destination = root / "MapA_MountainHill_Blends.dds";
    expect(WriteTerrainDds(role, bytes, destination).has_value(), "publish verified blends file");
    {
        std::ifstream          input(destination, std::ios::binary);
        std::vector<std::byte> read(bytes.size() + 1);
        input.read(reinterpret_cast<char*>(read.data()), static_cast<std::streamsize>(read.size()));
        read.resize(static_cast<std::size_t>(input.gcount()));
        expect(read == bytes, "published bytes equal the encoder output");
    }
    expect(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator()) == 1, "publication leaves only the destination");
    expectError(WriteTerrainDds(role, bytes, destination), Error::DestinationExists, "existing blends file is not replaced");
    expectError(WriteTerrainDds(role, bytes, root / "missing" / "MapA_MountainHill_Blends.dds"), Error::OpenFailed, "missing parent fails open");
    expectError(WriteTerrainDds(TerrainTexture::Lightmap, bytes, root / "invalid.dds"), Error::LengthMismatch, "validation precedes output");
    expect(!std::filesystem::exists(root / "invalid.dds"), "rejected input leaves no file");
    expect(std::filesystem::remove(destination, error) && std::filesystem::remove(root, error), "remove exactly the synthetic terrain file and directory");
}

} // namespace

int main(int argc, char** argv)
{
    const auto scratch = argc == 3 && std::string_view(argv[1]) == "--scratch-root" ? std::filesystem::path(argv[2]) : std::filesystem::temp_directory_path();
    testEncoding(TerrainTexture::MountainHillBlends, 2048, 2097280);
    testEncoding(TerrainTexture::Lightmap, 4096, 8388736);
    testRejections();
    testFileWriting(scratch);
    if (failures != 0)
    {
        std::cerr << failures << " terrain DDS test(s) failed.\n";
        return 1;
    }
    std::cout << "Terrain DDS tests passed.\n";
    return 0;
}
