#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

struct FpkArchiveEntry
{
    std::string            name;
    std::vector<std::byte> bytes;
};

struct FpkArchive
{
    std::vector<FpkArchiveEntry> entries;
};

enum class FpkWriteError
{
    EmptyName,
    InvalidName,
    NameTooLong,
    DuplicateName,
    LookupCollision,
    SizeOverflow,
    DestinationExists,
    OpenFailed,
    WriteFailed,
    FinalizationFailed,
    ReadBackFailed,
};

// Allows size preflight without allocating names or payloads.
struct FpkEntrySize
{
    std::uint64_t nameLength  = 0;
    std::uint64_t payloadSize = 0;
};

struct FpkArchiveLayout
{
    std::uint32_t              tableEnd    = 14;
    std::uint32_t              archiveSize = 14;
    std::vector<std::uint32_t> payloadOffsets;
};

[[nodiscard]] std::string_view FpkWriteErrorMessage(FpkWriteError error);

[[nodiscard]] std::expected<FpkArchiveLayout, FpkWriteError>
CalculateFpkArchiveLayout(std::span<const FpkEntrySize> entries);

// Preserves caller entry order and emits contiguous, unaligned payloads.
[[nodiscard]] std::expected<std::vector<std::byte>, FpkWriteError>
EncodeFpkArchive(const FpkArchive& archive);

// Uses the parser to verify a temporary sibling before publishing a new hard link.
[[nodiscard]] std::expected<void, FpkWriteError>
WriteFpkArchive(const FpkArchive& archive, const std::filesystem::path& destination);

} // namespace rerevved::tools
