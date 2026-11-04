#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace rerevved::tools
{

struct FpkTableEntry
{
    std::string           name;
    std::filesystem::path archive;
    std::uint64_t         offset = 0;
    std::uint64_t         size   = 0;
};

// Reads only the table, never payloads. Tables over maximumEntries are refused.
// tableBudget is charged the table bytes read, success or not, and an overrun is refused.
[[nodiscard]] std::expected<std::vector<FpkTableEntry>, std::string> ReadFpkTable(const std::filesystem::path& archive,
                                                                                  std::uint32_t                maximumEntries = UINT32_MAX,
                                                                                  std::uint64_t*               tableBudget    = nullptr);

[[nodiscard]] std::expected<std::vector<std::byte>, std::string> ReadFpkTableEntry(const FpkTableEntry& entry, std::uint64_t maximumSize);

} // namespace rerevved::tools
