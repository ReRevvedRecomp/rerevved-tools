#include "fpk_index.h"

#include <algorithm>
#include <array>
#include <limits>

namespace rerevved::tools
{
namespace
{

constexpr std::size_t              kHeaderSize        = 14;
constexpr std::size_t              kMinimumRecordSize = 20;
constexpr std::array<std::byte, 4> kSignature{
    std::byte{ 'F' }, std::byte{ 'P' }, std::byte{ 'K' }, std::byte{ '_' }
};

std::uint32_t readU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

bool checkedMultiply(std::size_t left, std::size_t right, std::size_t& result)
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
    {
        return false;
    }
    result = left * right;
    return true;
}

} // namespace

std::expected<FpkIndex, FpkIndexError>
ParseFpkIndex(std::span<const std::byte> bytes)
{
    if (bytes.size() < kHeaderSize)
    {
        return std::unexpected(FpkIndexError::Truncated);
    }

    const auto signature = bytes.subspan<4, 4>();
    if (!std::ranges::equal(signature, kSignature))
    {
        return std::unexpected(FpkIndexError::InvalidSignature);
    }

    FpkIndex index{};
    index.version       = readU32Le(bytes, 0);
    index.headerUnknown = { bytes[8], bytes[9] };
    if (index.version != 6)
    {
        return std::unexpected(FpkIndexError::UnsupportedVersion);
    }

    const auto  entryCount       = static_cast<std::size_t>(readU32Le(bytes, 10));
    std::size_t minimumTableSize = 0;
    if (!checkedMultiply(entryCount, kMinimumRecordSize, minimumTableSize))
    {
        return std::unexpected(FpkIndexError::CountOverflow);
    }
    if (minimumTableSize > bytes.size() - kHeaderSize)
    {
        return std::unexpected(FpkIndexError::Truncated);
    }

    index.entries.reserve(entryCount);
    std::size_t cursor = kHeaderSize;
    for (std::size_t entryNumber = 0; entryNumber < entryCount; ++entryNumber)
    {
        if (bytes.size() - cursor < sizeof(std::uint32_t))
        {
            return std::unexpected(FpkIndexError::Truncated);
        }
        const auto variableDataSize = static_cast<std::size_t>(readU32Le(bytes, cursor));
        cursor += sizeof(std::uint32_t);

        const auto alignedSize = (variableDataSize + 3) & ~std::size_t{ 3 };
        if (alignedSize > bytes.size() - cursor)
        {
            return std::unexpected(FpkIndexError::Truncated);
        }
        cursor += alignedSize;
        if (bytes.size() - cursor < 16)
        {
            return std::unexpected(FpkIndexError::Truncated);
        }

        FpkEntry entry{};
        std::ranges::copy(bytes.subspan(cursor, 4), entry.unknown0.begin());
        cursor += 4;
        std::ranges::copy(bytes.subspan(cursor, 4), entry.unknown1.begin());
        cursor += 4;
        entry.size = readU32Le(bytes, cursor);
        cursor += 4;
        entry.offset = readU32Le(bytes, cursor);
        cursor += 4;

        if (entry.size > std::numeric_limits<std::uint32_t>::max() - entry.offset)
        {
            return std::unexpected(FpkIndexError::EntryRangeOverflow);
        }
        const auto offset = static_cast<std::size_t>(entry.offset);
        const auto size   = static_cast<std::size_t>(entry.size);
        if (offset > bytes.size() || size > bytes.size() - offset)
        {
            return std::unexpected(FpkIndexError::EntryOutOfRange);
        }
        index.entries.push_back(entry);
    }

    if (std::ranges::any_of(index.entries,
                            [cursor](const FpkEntry& entry)
                            {
                                return entry.offset < cursor;
                            }))
    {
        return std::unexpected(FpkIndexError::EntryOutOfRange);
    }
    return index;
}

} // namespace rerevved::tools
