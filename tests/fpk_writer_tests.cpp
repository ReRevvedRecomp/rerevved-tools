#include "fpk_document.h"
#include "fpk_index.h"
#include "fpk_writer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

using namespace rerevved::tools;

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

FpkArchive makeArchive()
{
    return { {
        { "DLCScenarioData0.xml", { std::byte{ 'X' } } },
        { "ORIGINAL.map", { std::byte{ 'M' } } },
        { "MapORIGINAL_Heights.dds", { std::byte{ 'H' } } },
        { "MapORIGINAL_MountainHill_Blends.dds", { std::byte{ 'B' } } },
        { "MapORIGINAL_Lightmap.dds", { std::byte{ 'L' } } },
    } };
}

std::vector<std::byte> fixtureBytes()
{
    // RVA-F-0074, revision b577f46, /syntheticFixture/hex. Payloads are placeholders.
    constexpr std::string_view hex =
        "0600000046504B5F00000500000014000000444C435363656E6172696F44617461302E786D6C000000000000000001000000E6000000"
        "0C0000004F524947494E414C2E6D6170000000000000000001000000E7000000170000004D61704F524947494E414C5F486569676874"
        "732E64647300000000000000000001000000E8000000230000004D61704F524947494E414C5F4D6F756E7461696E48696C6C5F426C"
        "656E64732E64647300000000000000000001000000E9000000180000004D61704F524947494E414C5F4C696768746D61702E646473"
        "000000000000000001000000EA000000584D48424C";
    const auto digit = [](char ch) -> unsigned
    {
        return ch <= '9' ? static_cast<unsigned>(ch - '0') : static_cast<unsigned>(ch - 'A' + 10);
    };
    std::vector<std::byte> bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2)
    {
        bytes.push_back(static_cast<std::byte>((digit(hex[i]) << 4U) | digit(hex[i + 1])));
    }
    return bytes;
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

void expectNames(std::span<const std::byte> bytes, const FpkArchive& archive)
{
    std::size_t cursor = 14;
    for (const auto& entry : archive.entries)
    {
        if (cursor > bytes.size() || bytes.size() - cursor < 4)
        {
            expect(false, "read-back contains each name length");
            return;
        }
        const auto length = readU32Le(bytes, cursor);
        cursor += 4;
        const auto paddedLength = (static_cast<std::uint64_t>(length) + 3) & ~std::uint64_t{ 3 };
        if (paddedLength > bytes.size() - cursor || bytes.size() - cursor - paddedLength < 16)
        {
            expect(false, "read-back name and record tail are bounded");
            return;
        }
        const std::string_view name(reinterpret_cast<const char*>(bytes.data() + cursor), length);
        expect(name == entry.name, "read-back retains each exact non-terminated name");
        cursor += static_cast<std::size_t>(paddedLength) + 16;
    }
}

void testPinnedArchive()
{
    const auto archive = makeArchive();
    const auto encoded = EncodeFpkArchive(archive);
    expect(encoded.has_value(), "encode the five wholly synthetic manifest entries");
    if (!encoded)
    {
        return;
    }
    expect(*encoded == fixtureBytes(), "all 235 bytes match the manifest fixture exactly");
    expect(EncodeFpkArchive(archive) == encoded, "encoding the same model is deterministic");
    expectNames(*encoded, archive);

    const auto parsed = ParseFpkDocument(*encoded);
    expect(parsed.has_value(), "existing document parser reads the emitted archive");
    if (!parsed)
    {
        return;
    }
    expect(parsed->bytes == *encoded, "document read-back preserves the complete archive");
    expect(parsed->index.version == 6 && parsed->index.headerUnknown == std::array<std::byte, 2>{},
           "version and both header bytes match the new-archive subset");
    expect(parsed->index.entries.size() == 5, "parser retains all five entry ranges");
    if (parsed->index.entries.size() != archive.entries.size())
    {
        return;
    }
    for (std::size_t i = 0; i < archive.entries.size(); ++i)
    {
        const auto& entry = parsed->index.entries[i];
        expect(entry.offset == 230 + i && entry.size == 1, "parser reads exact fixture sizes and absolute offsets");
        expect(entry.unknown0 == std::array<std::byte, 4>{} && entry.unknown1 == std::array<std::byte, 4>{},
               "both duplicate metadata words are zero");
        expect(std::ranges::equal(std::span<const std::byte>(parsed->bytes).subspan(entry.offset, entry.size), archive.entries[i].bytes),
               "parser-selected payload bytes match the explicit model");
    }
}

void testAlignmentAndEmptyPayloads()
{
    const FpkArchive archive{ {
        { "abc", {} },
        { "a", { std::byte{ 0 }, std::byte{ 0xFF }, std::byte{ 0x80 } } },
        { "abcd", {} },
        { "ab", { std::byte{ 7 } } },
    } };
    const auto       encoded = EncodeFpkArchive(archive);
    expect(encoded.has_value(), "all four name-alignment residues and empty payloads are supported");
    if (!encoded)
    {
        return;
    }
    const auto parsed = ParseFpkIndex(*encoded);
    expect(parsed && parsed->entries.size() == 4, "parser reads all alignment residues");
    if (!parsed || parsed->entries.size() != 4)
    {
        return;
    }
    expectNames(*encoded, archive);
    expect(encoded->size() == 114, "four 24-byte records and four payload bytes have no gaps or trailer");
    expect(parsed->entries[0].offset == 110 && parsed->entries[1].offset == 110 &&
               parsed->entries[2].offset == 113 && parsed->entries[3].offset == 113,
           "payload order follows caller order without alignment, including zero-size ranges");
    expect((*encoded)[21] == std::byte{} && (*encoded)[43] == std::byte{} &&
               (*encoded)[44] == std::byte{} && (*encoded)[45] == std::byte{} &&
               (*encoded)[92] == std::byte{} && (*encoded)[93] == std::byte{},
           "all serialized name padding is zero");

    const auto empty = EncodeFpkArchive({});
    expect(empty && empty->size() == 14 && readU32Le(*empty, 10) == 0,
           "an empty archive contains only its header and zero count");
    expect(empty && ParseFpkIndex(*empty).has_value(), "existing parser accepts an empty authored archive");
}

void testRejectedNames()
{
    const auto reject = [](std::string name, FpkWriteError expected)
    {
        const FpkArchive archive{ { { name, {} } } };
        const auto       encoded = EncodeFpkArchive(archive);
        expect(!encoded && encoded.error() == expected, "invalid name returns its exact error");
        expect(archive.entries[0].name == name, "name rejection leaves the source model unchanged");
    };
    reject("", FpkWriteError::EmptyName);
    reject("a/b", FpkWriteError::InvalidName);
    reject("a\\b", FpkWriteError::InvalidName);
    reject(std::string("a\0b", 3), FpkWriteError::InvalidName);
    reject(std::string(1, static_cast<char>(0x80)), FpkWriteError::InvalidName);
    reject(std::string(1, static_cast<char>(0xFF)), FpkWriteError::InvalidName);

    for (const auto name : { "same.dds", "SAME.DDS", "Same.dds" })
    {
        const FpkArchive archive{ { { "same.dds", {} }, { name, {} } } };
        const auto       encoded = EncodeFpkArchive(archive);
        expect(!encoded && encoded.error() == FpkWriteError::DuplicateName,
               "exact and ASCII-folded duplicate names are rejected");
    }
    // These distinct lowercase ASCII strings both have raw lookup CRC B224F3DA.
    const FpkArchive collision{ { { "plumless", {} }, { "buckeroo", {} } } };
    const auto       encoded = EncodeFpkArchive(collision);
    expect(!encoded && encoded.error() == FpkWriteError::LookupCollision,
           "distinct-name raw CRC collision is rejected before serialization");
}

void testOverflowGuards()
{
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    const auto     reject  = [](std::span<const FpkEntrySize> sizes, FpkWriteError expected)
    {
        const auto layout = CalculateFpkArchiveLayout(sizes);
        expect(!layout && layout.error() == expected, "layout rejects an unrepresentable size before allocation");
    };
    const std::array<FpkEntrySize, 1> emptyName{ { { 0, 0 } } };
    const std::array<FpkEntrySize, 1> longName{ { { 0x7FFFFFFD, 0 } } };
    const std::array<FpkEntrySize, 1> wrapName{ { { std::numeric_limits<std::uint64_t>::max(), 0 } } };
    const std::array<FpkEntrySize, 2> tableOverflow{ { { 0x7FFFFFFC, 0 }, { 0x7FFFFFFC, 0 } } };
    const std::array<FpkEntrySize, 1> largePayload{ { { 1, maximum + 1 } } };
    const std::array<FpkEntrySize, 1> wrapPayload{ { { 1, std::numeric_limits<std::uint64_t>::max() } } };
    const std::array<FpkEntrySize, 1> rangeOverflow{ { { 1, maximum - 37 } } };
    const std::array<FpkEntrySize, 2> combinedOverflow{ { { 1, maximum - 62 }, { 1, 1 } } };
    reject(emptyName, FpkWriteError::EmptyName);
    reject(longName, FpkWriteError::NameTooLong);
    reject(wrapName, FpkWriteError::NameTooLong);
    reject(tableOverflow, FpkWriteError::SizeOverflow);
    reject(largePayload, FpkWriteError::SizeOverflow);
    reject(wrapPayload, FpkWriteError::SizeOverflow);
    reject(rangeOverflow, FpkWriteError::SizeOverflow);
    reject(combinedOverflow, FpkWriteError::SizeOverflow);

    const std::array<FpkEntrySize, 1> maximumName{ { { 0x7FFFFFFC, 0 } } };
    const auto                        nameLayout = CalculateFpkArchiveLayout(maximumName);
    expect(nameLayout && nameLayout->tableEnd == 0x8000001E,
           "manifest maximum name aligns without signed or unsigned wrap");
    const std::array<FpkEntrySize, 2> lastOffset{ { { 1, maximum - 62 }, { 1, 0 } } };
    const auto                        maximumLayout = CalculateFpkArchiveLayout(lastOffset);
    expect(maximumLayout && maximumLayout->archiveSize == maximum &&
               maximumLayout->payloadOffsets[0] == 62 && maximumLayout->payloadOffsets[1] == maximum,
           "uint32 maximum archive size and zero-size final range remain representable");
    const auto empty = CalculateFpkArchiveLayout({});
    expect(empty && empty->tableEnd == 14 && empty->archiveSize == 14 && empty->payloadOffsets.empty(),
           "empty layout has no record or payload allocation");
}

void testErrorMessages()
{
    constexpr std::array messages{
        std::pair{ FpkWriteError::EmptyName, "FPK entry names must not be empty." },
        std::pair{ FpkWriteError::InvalidName, "FPK entry names must be ASCII without separators or zero bytes." },
        std::pair{ FpkWriteError::NameTooLong, "FPK entry names must not exceed 0x7FFFFFFC bytes." },
        std::pair{ FpkWriteError::DuplicateName, "FPK entry names must be unique after ASCII case folding." },
        std::pair{ FpkWriteError::LookupCollision, "Distinct FPK entry names have the same registered lookup CRC." },
        std::pair{ FpkWriteError::SizeOverflow, "The FPK entry count, table, payload, or archive size exceeds its supported range." },
        std::pair{ FpkWriteError::DestinationExists, "The FPK destination already exists." },
        std::pair{ FpkWriteError::OpenFailed, "Could not create a temporary FPK file beside the destination." },
        std::pair{ FpkWriteError::WriteFailed, "Could not write the complete FPK file." },
        std::pair{ FpkWriteError::FinalizationFailed, "Could not finalize the new FPK file. The destination volume must support hard links." },
        std::pair{ FpkWriteError::ReadBackFailed, "FPK parser readback did not match the archive names, ranges, and bytes exactly." },
    };
    for (const auto& [error, message] : messages)
    {
        expect(FpkWriteErrorMessage(error) == message, "each typed writer error has its exact diagnostic");
    }
}

void testFileWriting(const std::filesystem::path& scratch)
{
    const auto      root = scratch / ("rerevved-tools-fpk-" +
                                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    const auto      created = std::filesystem::create_directory(root, error);
    expect(created && !error, "create a fresh FPK test directory");
    if (!created || error)
    {
        return;
    }
    const auto destination       = root / "archive.fpk";
    const auto occupiedTemporary = root / "archive.fpk.tmp-0";
    const auto existing          = root / "existing.fpk";
    for (const auto& path : { occupiedTemporary, existing })
    {
        std::ofstream output(path, std::ios::binary | std::ios::noreplace);
        output.write("keep", 4);
        expect(static_cast<bool>(output), "create an exclusive sentinel file");
    }

    const auto archive = makeArchive();
    const auto written = WriteFpkArchive(archive, destination);
    expect(written.has_value(), "write a new archive after parser read-back despite an occupied temp name");
    const auto loaded = LoadFpkDocument(destination);
    expect(loaded && loaded->bytes == fixtureBytes(), "on-disk archive matches every pinned byte");
    if (loaded)
    {
        expectNames(loaded->bytes, archive);
    }
    const auto refused = WriteFpkArchive(archive, existing);
    expect(!refused && refused.error() == FpkWriteError::DestinationExists, "existing file destination is refused");
    const auto directoryRefused = WriteFpkArchive(archive, root);
    expect(!directoryRefused && directoryRefused.error() == FpkWriteError::DestinationExists, "existing directory destination is refused");
    const auto missingParent = WriteFpkArchive(archive, root / "missing" / "archive.fpk");
    expect(!missingParent && missingParent.error() == FpkWriteError::OpenFailed, "missing destination parent is an open failure");
    const auto invalid = WriteFpkArchive({ { { "a/b", {} } } }, root / "invalid.fpk");
    expect(!invalid && invalid.error() == FpkWriteError::InvalidName, "invalid model is rejected before filesystem output");
    expect(!std::filesystem::exists(root / "invalid.fpk"), "invalid archive creates no destination");

    for (const auto& path : { occupiedTemporary, existing })
    {
        std::ifstream       input(path, std::ios::binary);
        std::array<char, 5> sentinel{};
        input.read(sentinel.data(), static_cast<std::streamsize>(sentinel.size()));
        expect(input.gcount() == 4 && std::string_view(sentinel.data(), 4) == "keep",
               "existing destinations and unowned temp siblings retain every byte");
    }
    std::size_t entryCount = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(root))
    {
        ++entryCount;
    }
    expect(entryCount == 3, "writer removes only its own temporary sibling");
    for (const auto& path : { destination, occupiedTemporary, existing })
    {
        std::filesystem::remove(path, error);
    }
    expect(std::filesystem::remove(root, error) && !error, "remove only the named FPK test outputs and empty owned directory");
}

} // namespace

int main(int argc, char** argv)
{
    const auto scratch = argc == 3 && std::string_view(argv[1]) == "--scratch-root"
                             ? std::filesystem::path(argv[2])
                             : std::filesystem::temp_directory_path();
    testPinnedArchive();
    testAlignmentAndEmptyPayloads();
    testRejectedNames();
    testOverflowGuards();
    testErrorMessages();
    testFileWriting(scratch);
    return failures == 0 ? 0 : 1;
}
