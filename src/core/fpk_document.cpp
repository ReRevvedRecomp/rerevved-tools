#include "fpk_document.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

namespace rerevved::tools
{
namespace
{

constexpr std::array<std::byte, 4> kDdsSignature{
    std::byte{ 'D' }, std::byte{ 'D' }, std::byte{ 'S' }, std::byte{ ' ' }
};
constexpr std::array<std::byte, 3> kGfxSignature{
    std::byte{ 'G' }, std::byte{ 'F' }, std::byte{ 'X' }
};
constexpr std::array<std::byte, 3> kCompressedGfxSignature{
    std::byte{ 'C' }, std::byte{ 'F' }, std::byte{ 'X' }
};
constexpr std::array<std::byte, 3> kId3Signature{
    std::byte{ 'I' }, std::byte{ 'D' }, std::byte{ '3' }
};
constexpr std::string_view kNifSignature = "Gamebryo File Format, Version 20.3.0.9\n";

template <typename Range>
bool startsWith(std::span<const std::byte> bytes, const Range& prefix)
{
    return bytes.size() >= std::size(prefix) &&
           std::ranges::equal(bytes.first(std::size(prefix)), prefix);
}

std::string_view indexErrorMessage(FpkIndexError error)
{
    switch (error)
    {
        case FpkIndexError::Truncated:
            return "The FPK archive table is truncated.";
        case FpkIndexError::InvalidSignature:
            return "The file does not have a valid FPK signature.";
        case FpkIndexError::UnsupportedVersion:
            return "The FPK archive version is unsupported.";
        case FpkIndexError::CountOverflow:
            return "The FPK entry count exceeds the bounded archive table.";
        case FpkIndexError::EntryRangeOverflow:
            return "An FPK entry byte range overflows.";
        case FpkIndexError::EntryOutOfRange:
            return "An FPK entry points outside the archive payload region.";
    }
    std::unreachable();
}

std::expected<std::span<const std::byte>, std::string>
entryBytes(const FpkDocument& document, std::size_t selectedEntry)
{
    if (selectedEntry >= document.index.entries.size())
    {
        return std::unexpected("The selected FPK entry does not exist.");
    }

    const auto& entry = document.index.entries[selectedEntry];
    if (entry.size > std::numeric_limits<std::uint32_t>::max() - entry.offset)
    {
        return std::unexpected("The selected FPK entry range is invalid.");
    }

    const auto offset = static_cast<std::size_t>(entry.offset);
    const auto size   = static_cast<std::size_t>(entry.size);
    if (offset > document.bytes.size() || size > document.bytes.size() - offset)
    {
        return std::unexpected("The selected FPK entry range is invalid.");
    }
    return std::span(document.bytes).subspan(offset, size);
}

template <typename Document, typename Error, typename ErrorMessage>
std::expected<FpkEntryDocument, std::string>
makeEntryDocument(std::size_t                    selectedEntry,
                  FpkEntryFormat                 format,
                  std::expected<Document, Error> parsed,
                  ErrorMessage                   errorMessage)
{
    if (!parsed)
    {
        return std::unexpected(std::string(errorMessage(parsed.error())));
    }
    return FpkEntryDocument{
        .entryIndex = selectedEntry,
        .format     = format,
        .data       = std::move(*parsed),
    };
}

} // namespace

std::expected<FpkDocument, FpkIndexError>
ParseFpkDocument(std::span<const std::byte> bytes)
{
    auto index = ParseFpkIndex(bytes);
    if (!index)
    {
        return std::unexpected(index.error());
    }
    return FpkDocument{
        .bytes = std::vector<std::byte>(bytes.begin(), bytes.end()),
        .index = std::move(*index),
    };
}

std::expected<FpkDocument, std::string>
LoadFpkDocument(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        return std::unexpected("Could not open FPK file for reading.");
    }

    const auto end = input.tellg();
    if (end < 0)
    {
        return std::unexpected("Could not read FPK file size.");
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > std::numeric_limits<std::size_t>::max() ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return std::unexpected("The FPK file is too large to inspect on this platform.");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
    {
        return std::unexpected("Could not read FPK file data.");
    }
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
    {
        return std::unexpected("The FPK file ended before its reported size.");
    }

    auto index = ParseFpkIndex(bytes);
    if (!index)
    {
        return std::unexpected(std::string(indexErrorMessage(index.error())));
    }
    return FpkDocument{
        .bytes = std::move(bytes),
        .index = std::move(*index),
    };
}

std::expected<FpkEntryKind, std::string>
InspectFpkEntryKind(const FpkDocument& document, std::size_t selectedEntry)
{
    const auto bytes = entryBytes(document, selectedEntry);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }

    if (startsWith(*bytes, kDdsSignature))
    {
        return FpkEntryKind::DdsTexture;
    }
    if (startsWith(*bytes, kGfxSignature) || startsWith(*bytes, kCompressedGfxSignature))
    {
        return FpkEntryKind::GfxMovie;
    }
    if (startsWith(*bytes, kId3Signature))
    {
        return FpkEntryKind::Id3TaggedAudio;
    }
    if (startsWith(*bytes, std::as_bytes(std::span(kNifSignature))))
    {
        return FpkEntryKind::NifContainer;
    }
    return FpkEntryKind::Unknown;
}

std::expected<FpkEntryDocument, std::string>
OpenFpkEntryDocument(const FpkDocument& document,
                     std::size_t        selectedEntry,
                     FpkEntryFormat     format)
{
    auto bytes = entryBytes(document, selectedEntry);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }

    switch (format)
    {
        case FpkEntryFormat::Dds:
            return makeEntryDocument(
                selectedEntry, format, ParseDdsDocument(*bytes), DdsDocumentErrorMessage);
        case FpkEntryFormat::Gfx:
            return makeEntryDocument(
                selectedEntry, format, ParseGfxDocument(*bytes), GfxDocumentErrorMessage);
        case FpkEntryFormat::Nif:
            return makeEntryDocument(
                selectedEntry, format, ParseNifDocument(*bytes), NifDocumentErrorMessage);
        case FpkEntryFormat::Mp3:
            return makeEntryDocument(
                selectedEntry, format, ParseMp3Document(*bytes), Mp3DocumentErrorMessage);
        case FpkEntryFormat::Map:
            return makeEntryDocument(
                selectedEntry, format, ParseMapDocument(*bytes), MapDocumentErrorMessage);
    }
    return std::unexpected("The selected FPK entry format is invalid.");
}

} // namespace rerevved::tools
