#include "fpk_table.h"

#include <algorithm>
#include <fstream>
#include <string_view>
#include <system_error>

namespace rerevved::tools
{
namespace
{

std::uint32_t readU32(const unsigned char* bytes)
{
    return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8U | std::uint32_t(bytes[2]) << 16U | std::uint32_t(bytes[3]) << 24U;
}

} // namespace

std::expected<std::vector<FpkTableEntry>, std::string> ReadFpkTable(const std::filesystem::path& archive, std::uint32_t maximumEntries, std::uint64_t* tableBudget)
{
    std::uint64_t tableBytes = 0;
    auto          table      = [&]() -> std::expected<std::vector<FpkTableEntry>, std::string>
    {
        const auto      failure = std::unexpected(archive.filename().string() + " is not a readable FPK archive.");
        std::error_code error;
        const auto      fileSize = std::filesystem::file_size(archive, error);
        std::ifstream   in(archive, std::ios::binary);
        unsigned char   header[14];
        if (error || !in || !in.read(reinterpret_cast<char*>(header), sizeof(header)) || readU32(header) != 6 || std::string_view(reinterpret_cast<const char*>(header + 4), 4) != "FPK_")
        {
            return failure;
        }
        tableBytes       = sizeof(header);
        const auto count = readU32(header + 10);
        if (count > maximumEntries || count > fileSize / 20)
        {
            return failure;
        }
        std::vector<FpkTableEntry> entries;
        entries.reserve(std::min<std::uint32_t>(count, 1U << 16));
        for (std::uint32_t k = 0; k < count; ++k)
        {
            unsigned char length[4];
            if (!in.read(reinterpret_cast<char*>(length), 4))
            {
                return failure;
            }
            const auto nameLength = readU32(length);
            if (nameLength > 4096)
            {
                return failure;
            }
            std::string   name((nameLength + 3U) & ~3U, '\0');
            unsigned char record[16];
            tableBytes += sizeof(length) + name.size() + sizeof(record);
            if (tableBudget && tableBytes > *tableBudget)
            {
                return failure;
            }
            if (!in.read(name.data(), static_cast<std::streamsize>(name.size())) || !in.read(reinterpret_cast<char*>(record), sizeof(record)))
            {
                return failure;
            }
            name.resize(std::min<std::size_t>(nameLength, name.find('\0')));
            const std::uint64_t size   = readU32(record + 8);
            const std::uint64_t offset = readU32(record + 12);
            if (offset + size > fileSize)
            {
                return failure;
            }
            entries.push_back({ std::move(name), archive, offset, size });
        }
        return entries;
    }();
    if (tableBudget)
    {
        *tableBudget -= std::min(tableBytes, *tableBudget);
    }
    return table;
}

std::expected<std::vector<std::byte>, std::string> ReadFpkTableEntry(const FpkTableEntry& entry, std::uint64_t maximumSize)
{
    if (entry.size > maximumSize)
    {
        return std::unexpected(entry.archive.filename().string() + " has an oversized " + entry.name + " entry.");
    }
    std::vector<std::byte> bytes(entry.size);
    std::ifstream          in(entry.archive, std::ios::binary);
    if (!in || !in.seekg(static_cast<std::streamoff>(entry.offset)) || !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    {
        return std::unexpected("Could not read " + entry.archive.filename().string() + ".");
    }
    return bytes;
}

} // namespace rerevved::tools
