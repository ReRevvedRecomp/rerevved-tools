#include "fpk_writer.h"

#include "fpk_index.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <limits>
#include <map>
#include <system_error>
#include <utility>

namespace rerevved::tools
{
namespace
{

// Research: RVA-F-0074, manifests/fpk-v6-authoring-boundary.json,
// revision 99d0b9f, /grammar and /nameLookup (unique ASCII names only).
constexpr std::uint64_t kMaximumNameLength = 0x7FFFFFFC;
constexpr std::uint64_t kMaximumSize       = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kHeaderSize        = 14;

bool checkedAdd(std::uint32_t& value, std::uint64_t increment)
{
    if (increment > kMaximumSize - value)
    {
        return false;
    }
    value += static_cast<std::uint32_t>(increment);
    return true;
}

void writeU32Le(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        bytes[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xFFU);
    }
}

std::uint32_t readU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        value |= std::to_integer<std::uint32_t>(bytes[offset + shift / 8]) << shift;
    }
    return value;
}

std::expected<void, FpkWriteError> validateNames(const FpkArchive& archive)
{
    std::map<std::uint32_t, std::string> indexedNames;
    for (const auto& entry : archive.entries)
    {
        std::string folded;
        folded.reserve(entry.name.size());
        std::uint32_t crc = 0xFFFFFFFFU;
        for (const auto ch : entry.name)
        {
            auto value = static_cast<unsigned char>(ch);
            if (value == 0 || value > 0x7F || value == '/' || value == '\\')
            {
                return std::unexpected(FpkWriteError::InvalidName);
            }
            if (value >= 'A' && value <= 'Z')
            {
                value = static_cast<unsigned char>(value + ('a' - 'A'));
            }
            folded.push_back(static_cast<char>(value));
            crc ^= value;
            for (unsigned bit = 0; bit < 8; ++bit)
            {
                crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? 0xEDB88320U : 0U);
            }
        }
        const auto [found, inserted] = indexedNames.try_emplace(crc, std::move(folded));
        if (!inserted)
        {
            return std::unexpected(found->second == folded ? FpkWriteError::DuplicateName : FpkWriteError::LookupCollision);
        }
    }
    return {};
}

bool matchesArchive(std::span<const std::byte> bytes, const FpkArchive& archive, std::span<const std::byte> encoded)
{
    const auto index = ParseFpkIndex(bytes);
    if (!index || index->version != 6 || index->entries.size() != archive.entries.size() ||
        !std::ranges::equal(bytes, encoded))
    {
        return false;
    }

    std::uint32_t expectedOffset = kHeaderSize;
    for (const auto& source : archive.entries)
    {
        const auto length = static_cast<std::uint64_t>(source.name.size());
        if (length > kMaximumNameLength || !checkedAdd(expectedOffset, 20 + ((length + 3) & ~std::uint64_t{ 3 })))
        {
            return false;
        }
    }

    // The existing parser skips names. Compare their bounded table ranges here.
    std::size_t cursor = kHeaderSize;
    for (std::size_t i = 0; i < archive.entries.size(); ++i)
    {
        const auto& source = archive.entries[i];
        const auto& entry  = index->entries[i];
        if (cursor > bytes.size() || bytes.size() - cursor < 4)
        {
            return false;
        }
        const auto length = readU32Le(bytes, cursor);
        cursor += 4;
        if (length != source.name.size() || length > bytes.size() - cursor)
        {
            return false;
        }
        for (std::size_t j = 0; j < length; ++j)
        {
            if (bytes[cursor + j] != static_cast<std::byte>(source.name[j]))
            {
                return false;
            }
        }
        const auto paddedLength = (static_cast<std::uint64_t>(length) + 3) & ~std::uint64_t{ 3 };
        if (paddedLength > bytes.size() - cursor || bytes.size() - cursor - paddedLength < 16)
        {
            return false;
        }
        cursor += static_cast<std::size_t>(paddedLength);
        cursor += 16;
        if (entry.size != source.bytes.size() || entry.offset != expectedOffset ||
            entry.offset > bytes.size() || entry.size > bytes.size() - entry.offset ||
            !std::ranges::equal(bytes.subspan(entry.offset, entry.size), source.bytes))
        {
            return false;
        }
        if (!checkedAdd(expectedOffset, source.bytes.size()))
        {
            return false;
        }
    }
    return expectedOffset == bytes.size();
}

struct TemporaryFile
{
    std::filesystem::path path;

    ~TemporaryFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

} // namespace

std::string_view FpkWriteErrorMessage(FpkWriteError error)
{
    switch (error)
    {
        case FpkWriteError::EmptyName:
            return "FPK entry names must not be empty.";
        case FpkWriteError::InvalidName:
            return "FPK entry names must be ASCII without separators or zero bytes.";
        case FpkWriteError::NameTooLong:
            return "FPK entry names must not exceed 0x7FFFFFFC bytes.";
        case FpkWriteError::DuplicateName:
            return "FPK entry names must be unique after ASCII case folding.";
        case FpkWriteError::LookupCollision:
            return "Distinct FPK entry names have the same registered lookup CRC.";
        case FpkWriteError::SizeOverflow:
            return "The FPK entry count, table, payload, or archive size exceeds its supported range.";
        case FpkWriteError::DestinationExists:
            return "The FPK destination already exists.";
        case FpkWriteError::OpenFailed:
            return "Could not create a temporary FPK file beside the destination.";
        case FpkWriteError::WriteFailed:
            return "Could not write the complete FPK file.";
        case FpkWriteError::FinalizationFailed:
            return "Could not finalize the new FPK file. The destination volume "
                   "must support hard links.";
        case FpkWriteError::ReadBackFailed:
            return "FPK parser readback did not match the archive names, ranges, "
                   "and bytes exactly.";
    }
    return "The FPK file could not be written.";
}

std::expected<FpkArchiveLayout, FpkWriteError>
CalculateFpkArchiveLayout(std::span<const FpkEntrySize> entries)
{
    FpkArchiveLayout layout;
    if (entries.size() > kMaximumSize || entries.size() > layout.payloadOffsets.max_size())
    {
        return std::unexpected(FpkWriteError::SizeOverflow);
    }
    for (const auto& entry : entries)
    {
        if (entry.nameLength == 0)
        {
            return std::unexpected(FpkWriteError::EmptyName);
        }
        if (entry.nameLength > kMaximumNameLength)
        {
            return std::unexpected(FpkWriteError::NameTooLong);
        }
        const auto alignedName = (entry.nameLength + 3) & ~std::uint64_t{ 3 };
        if (entry.payloadSize > kMaximumSize || !checkedAdd(layout.tableEnd, 20 + alignedName))
        {
            return std::unexpected(FpkWriteError::SizeOverflow);
        }
    }
    layout.archiveSize = layout.tableEnd;
    layout.payloadOffsets.reserve(entries.size());
    for (const auto& entry : entries)
    {
        layout.payloadOffsets.push_back(layout.archiveSize);
        if (!checkedAdd(layout.archiveSize, entry.payloadSize))
        {
            return std::unexpected(FpkWriteError::SizeOverflow);
        }
    }
    if (layout.archiveSize > std::vector<std::byte>{}.max_size() ||
        static_cast<std::uint64_t>(layout.archiveSize) > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return std::unexpected(FpkWriteError::SizeOverflow);
    }
    return layout;
}

std::expected<std::vector<std::byte>, FpkWriteError>
EncodeFpkArchive(const FpkArchive& archive)
{
    std::vector<FpkEntrySize> sizes;
    if (archive.entries.size() > kMaximumSize || archive.entries.size() > sizes.max_size())
    {
        return std::unexpected(FpkWriteError::SizeOverflow);
    }
    sizes.reserve(archive.entries.size());
    for (const auto& entry : archive.entries)
    {
        sizes.push_back({ entry.name.size(), entry.bytes.size() });
    }
    const auto layout = CalculateFpkArchiveLayout(sizes);
    if (!layout)
    {
        return std::unexpected(layout.error());
    }
    if (const auto names = validateNames(archive); !names)
    {
        return std::unexpected(names.error());
    }

    std::vector<std::byte> bytes(layout->archiveSize);
    writeU32Le(bytes, 0, 6);
    bytes[4] = std::byte{ 'F' };
    bytes[5] = std::byte{ 'P' };
    bytes[6] = std::byte{ 'K' };
    bytes[7] = std::byte{ '_' };
    writeU32Le(bytes, 10, static_cast<std::uint32_t>(archive.entries.size()));
    std::size_t cursor = kHeaderSize;
    for (std::size_t i = 0; i < archive.entries.size(); ++i)
    {
        const auto& entry = archive.entries[i];
        writeU32Le(bytes, cursor, static_cast<std::uint32_t>(entry.name.size()));
        cursor += 4;
        for (const auto ch : entry.name)
        {
            bytes[cursor++] = static_cast<std::byte>(ch);
        }
        // Alignment applies to the name length, not the absolute table cursor.
        cursor += (4 - entry.name.size() % 4) % 4;
        cursor += 8;
        writeU32Le(bytes, cursor, static_cast<std::uint32_t>(entry.bytes.size()));
        cursor += 4;
        writeU32Le(bytes, cursor, layout->payloadOffsets[i]);
        cursor += 4;
        std::ranges::copy(entry.bytes, bytes.begin() + layout->payloadOffsets[i]);
    }
    return bytes;
}

std::expected<void, FpkWriteError>
WriteFpkArchive(const FpkArchive& archive, const std::filesystem::path& destination)
{
    const auto bytes = EncodeFpkArchive(archive);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::error_code error;
    const auto      destinationStatus = std::filesystem::symlink_status(destination, error);
    if (!error && destinationStatus.type() != std::filesystem::file_type::not_found)
    {
        return std::unexpected(FpkWriteError::DestinationExists);
    }
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return std::unexpected(FpkWriteError::OpenFailed);
    }

    static std::atomic<std::uint64_t> sequence{ 0 };
    std::ofstream                     output;
    std::filesystem::path             temporary;
    for (unsigned attempt = 0; attempt < 128; ++attempt)
    {
        temporary = destination;
        temporary += ".tmp-" + std::to_string(sequence.fetch_add(1));
        output.open(temporary, std::ios::binary | std::ios::noreplace);
        if (output)
        {
            break;
        }
        error.clear();
        const auto status = std::filesystem::symlink_status(temporary, error);
        if (error || status.type() == std::filesystem::file_type::not_found)
        {
            return std::unexpected(FpkWriteError::OpenFailed);
        }
        output.clear();
    }
    if (!output.is_open())
    {
        return std::unexpected(FpkWriteError::OpenFailed);
    }
    const TemporaryFile cleanup{ temporary };
    output.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    output.close();
    if (output.fail())
    {
        return std::unexpected(FpkWriteError::WriteFailed);
    }

    std::ifstream          input(temporary, std::ios::binary);
    std::vector<std::byte> readBack(bytes->size());
    input.read(reinterpret_cast<char*>(readBack.data()), static_cast<std::streamsize>(readBack.size()));
    if (!input || input.peek() != std::char_traits<char>::eof() || input.bad() ||
        !matchesArchive(readBack, archive, *bytes))
    {
        return std::unexpected(FpkWriteError::ReadBackFailed);
    }
    input.close();

    error.clear();
    std::filesystem::create_hard_link(temporary, destination, error);
    if (error)
    {
        std::error_code statusError;
        const auto      status = std::filesystem::symlink_status(destination, statusError);
        if (!statusError && status.type() != std::filesystem::file_type::not_found)
        {
            return std::unexpected(FpkWriteError::DestinationExists);
        }
        return std::unexpected(FpkWriteError::FinalizationFailed);
    }
    return {};
}

} // namespace rerevved::tools
