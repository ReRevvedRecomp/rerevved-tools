#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace rerevved::tools
{

enum class FpkIndexError
{
    Truncated,
    InvalidSignature,
    UnsupportedVersion,
    CountOverflow,
    EntryRangeOverflow,
    EntryOutOfRange,
};

struct FpkEntry
{
    std::uint32_t            offset = 0;
    std::uint32_t            size   = 0;
    std::array<std::byte, 4> unknown0{};
    std::array<std::byte, 4> unknown1{};
};

struct FpkIndex
{
    std::uint32_t            version = 0;
    std::array<std::byte, 2> headerUnknown{};
    std::vector<FpkEntry>    entries;
};

[[nodiscard]] std::expected<FpkIndex, FpkIndexError>
ParseFpkIndex(std::span<const std::byte> bytes);

} // namespace rerevved::tools
