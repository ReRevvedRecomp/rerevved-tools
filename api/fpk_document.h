#pragma once

#include "dds_document.h"
#include "fpk_index.h"
#include "gfx_document.h"
#include "map_document.h"
#include "mp3_document.h"
#include "nif_document.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace rerevved::tools
{

struct FpkDocument
{
    std::vector<std::byte> bytes;
    FpkIndex               index;
};

enum class FpkEntryFormat
{
    Dds,
    Gfx,
    Nif,
    Mp3,
    Map,
};

enum class FpkEntryKind
{
    Unknown,
    DdsTexture,
    GfxMovie,
    Id3TaggedAudio,
    NifContainer,
};

using FpkEntryDocumentData =
    std::variant<DdsDocument, GfxDocument, NifDocument, Mp3Document, MapDocument>;

struct FpkEntryDocument
{
    std::size_t          entryIndex = 0;
    FpkEntryFormat       format     = FpkEntryFormat::Dds;
    FpkEntryDocumentData data;
};

[[nodiscard]] std::expected<FpkDocument, FpkIndexError>
ParseFpkDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<FpkDocument, std::string>
LoadFpkDocument(const std::filesystem::path& path);

[[nodiscard]] std::expected<FpkEntryKind, std::string>
InspectFpkEntryKind(const FpkDocument& document, std::size_t selectedEntry);

[[nodiscard]] std::expected<FpkEntryDocument, std::string>
OpenFpkEntryDocument(const FpkDocument& document,
                     std::size_t        selectedEntry,
                     FpkEntryFormat     format);

} // namespace rerevved::tools
