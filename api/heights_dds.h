#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

// Every shipped named map heights file uses this square side.
inline constexpr std::uint32_t kHeightsDdsSide        = 512;
inline constexpr std::size_t   kHeightsDdsSampleCount = std::size_t(kHeightsDdsSide) * kHeightsDdsSide;
inline constexpr std::size_t   kHeightsDdsByteCount   = 128 + kHeightsDdsSampleCount * 2;

enum class HeightsDdsError
{
    SampleCountMismatch,
    LengthMismatch,
    HeaderMismatch,
    DestinationExists,
    OpenFailed,
    WriteFailed,
    FinalizationFailed,
    ReadBackFailed,
};

[[nodiscard]] std::string_view HeightsDdsErrorMessage(HeightsDdsError error);

// Samples are row major in file row order. Each is written as little endian u16.
[[nodiscard]] std::expected<std::vector<std::byte>, HeightsDdsError>
EncodeHeightsDds(std::span<const std::uint16_t> samples);

// Accepts only the exact retail header and payload length.
[[nodiscard]] std::expected<std::vector<std::uint16_t>, HeightsDdsError>
ParseHeightsDds(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<void, HeightsDdsError>
WriteHeightsDds(std::span<const std::uint16_t> samples, const std::filesystem::path& destination);

} // namespace rerevved::tools
