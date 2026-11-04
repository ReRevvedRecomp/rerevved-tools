#include "heights_dds.h"

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
using Error  = HeightsDdsError;
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

std::uint32_t u32(const std::vector<std::byte>& bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8);
    }
    return value;
}

std::vector<std::uint16_t> ramp()
{
    std::vector<std::uint16_t> samples(kHeightsDdsSampleCount);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = static_cast<std::uint16_t>(i * 257 + 3);
    }
    return samples;
}

void testEncoding()
{
    const auto samples = ramp();
    const auto bytes   = EncodeHeightsDds(samples);
    expect(bytes && bytes->size() == 524416, "one tight 512 x 512 16-bit level after the header");
    if (!bytes)
    {
        return;
    }
    // Header words restated independently from the shipped-file census.
    for (const auto [offset, value] : { std::pair{ 0U, 0x20534444U }, std::pair{ 4U, 124U }, std::pair{ 8U, 0x1007U }, std::pair{ 12U, 512U }, std::pair{ 16U, 512U }, std::pair{ 20U, 0U }, std::pair{ 28U, 0U }, std::pair{ 76U, 32U }, std::pair{ 80U, 0x20000U }, std::pair{ 84U, 0U }, std::pair{ 88U, 16U }, std::pair{ 92U, 0xFFFFU }, std::pair{ 96U, 0U }, std::pair{ 104U, 0U }, std::pair{ 108U, 0x1000U }, std::pair{ 112U, 0U } })
    {
        expect(u32(*bytes, offset) == value, "retail header word at " + std::to_string(offset));
    }
    constexpr std::array kNamed{ 0U, 4U, 8U, 12U, 16U, 76U, 80U, 88U, 92U, 108U };
    for (std::size_t offset = 0; offset < 128; offset += 4)
    {
        expect(std::ranges::find(kNamed, offset) != kNamed.end() || u32(*bytes, offset) == 0, "unnamed header word " + std::to_string(offset) + " is zero");
    }
    expect((*bytes)[128] == std::byte{ 0x03 } && (*bytes)[129] == std::byte{ 0x00 }, "first sample is little-endian");
    expect((*bytes)[130] == std::byte{ 0x04 } && (*bytes)[131] == std::byte{ 0x01 }, "second sample 0x0104 is little-endian");
    const auto last = samples.back();
    expect((*bytes)[524414] == static_cast<std::byte>(last & 255U) && (*bytes)[524415] == static_cast<std::byte>(last >> 8), "last sample closes the payload");
    const auto parsed = ParseHeightsDds(*bytes);
    expect(parsed && *parsed == samples, "parser returns every sample");
}

void testRejections()
{
    expectError(EncodeHeightsDds(std::vector<std::uint16_t>(kHeightsDdsSampleCount - 1)), Error::SampleCountMismatch, "short sample set rejected");
    expectError(EncodeHeightsDds(std::vector<std::uint16_t>(kHeightsDdsSampleCount + 1)), Error::SampleCountMismatch, "long sample set rejected");
    const auto bytes = *EncodeHeightsDds(ramp());
    expectError(ParseHeightsDds(std::span(bytes).first(bytes.size() - 1)), Error::LengthMismatch, "truncated payload rejected");
    auto trailing = bytes;
    trailing.push_back(std::byte{ 0 });
    expectError(ParseHeightsDds(trailing), Error::LengthMismatch, "trailing byte rejected");
    for (std::size_t offset = 0; offset < 128; ++offset)
    {
        auto changed = bytes;
        changed[offset] ^= std::byte{ 1 };
        expectError(ParseHeightsDds(changed), Error::HeaderMismatch, "header byte " + std::to_string(offset) + " is checked");
    }
}

void testFileWriting(const std::filesystem::path& scratch)
{
    const auto      root = scratch / ("rerevved-tools-heights-dds-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    const auto      created = std::filesystem::create_directory(root, error);
    expect(created && !error, "create fresh heights test directory");
    if (!created || error)
    {
        return;
    }
    const auto samples     = ramp();
    const auto destination = root / "MapA_Heights.dds";
    expect(WriteHeightsDds(samples, destination).has_value(), "publish verified heights file");
    {
        std::ifstream          input(destination, std::ios::binary);
        std::vector<std::byte> bytes(kHeightsDdsByteCount + 1);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(input.gcount()));
        expect(bytes == *EncodeHeightsDds(samples), "published bytes equal the encoder output");
    }
    expect(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator()) == 1, "publication leaves only the destination");
    expectError(WriteHeightsDds(samples, destination), Error::DestinationExists, "existing heights file is not replaced");
    expectError(WriteHeightsDds(samples, root / "missing" / "MapA_Heights.dds"), Error::OpenFailed, "missing parent fails open");
    expectError(WriteHeightsDds({}, root / "invalid.dds"), Error::SampleCountMismatch, "validation precedes output");
    expect(!std::filesystem::exists(root / "invalid.dds"), "rejected input leaves no file");
    expect(std::filesystem::remove(destination, error) && std::filesystem::remove(root, error), "remove exactly the synthetic heights file and directory");
}

} // namespace

int main(int argc, char** argv)
{
    const auto scratch = argc == 3 && std::string_view(argv[1]) == "--scratch-root" ? std::filesystem::path(argv[2]) : std::filesystem::temp_directory_path();
    testEncoding();
    testRejections();
    testFileWriting(scratch);
    if (failures != 0)
    {
        std::cerr << failures << " heights DDS test(s) failed.\n";
        return 1;
    }
    std::cout << "Heights DDS tests passed.\n";
    return 0;
}
