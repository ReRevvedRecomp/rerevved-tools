#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

// Resource limits of this canonical reader/writer, not recovered game limits.
inline constexpr std::size_t kScenarioMaximumXmlBytes     = 1024 * 1024;
inline constexpr std::size_t kScenarioMaximumMapName      = 20;
inline constexpr std::size_t kScenarioMaximumEntries      = 4096;
inline constexpr std::size_t kScenarioMaximumLiteralBytes = 4096;
inline constexpr std::size_t kScenarioNoEntry             = static_cast<std::size_t>(-1);

struct ScenarioVariator
{
    std::string_view name;
    std::size_t      slot;
    std::int16_t     defaultLevel;
};

[[nodiscard]] std::span<const ScenarioVariator> ScenarioVariators();
[[nodiscard]] std::array<std::int16_t, 64>      DefaultScenarioVariators();

// ENG, FRA, ITA, DEU, ESP, in that order. Empty strings omit that child.
using ScenarioLiterals = std::array<std::string, 5>;

struct ScenarioEntry
{
    std::string                  map         = "NONE";
    bool                         multiplayer = false;
    std::int32_t                 id          = -1;
    ScenarioLiterals             title;
    ScenarioLiterals             description;
    std::array<std::int16_t, 64> variators = DefaultScenarioVariators();

    bool operator==(const ScenarioEntry&) const = default;
};

struct ScenarioDocument
{
    std::vector<ScenarioEntry> entries;

    bool operator==(const ScenarioDocument&) const = default;
};

enum class ScenarioError
{
    ResourceLimit,
    MapNameUnproved,
    InvalidLiteral,
    DuplicateId,
    UnnamedVariatorSlot,
    UnknownField,
    ExcludedField,
    UnknownAttribute,
    UnknownVariator,
    NoncanonicalDecimal,
    NumericOutOfRange,
    EmptyScalar,
    MalformedXml,
    UnsupportedXml,
    DestinationExists,
    OpenFailed,
    WriteFailed,
    FinalizationFailed,
    ReadBackFailed,
    DuplicateMapName,
};

struct ScenarioIssue
{
    ScenarioError code;
    std::size_t   entryIndex = kScenarioNoEntry;
    std::string   field;
};

[[nodiscard]] std::string_view           ScenarioErrorMessage(ScenarioError error);
[[nodiscard]] std::vector<ScenarioIssue> ValidateScenarioDocument(const ScenarioDocument& document);

[[nodiscard]] std::string_view SelectScenarioLiteral(const ScenarioLiterals& literals, std::int32_t localeId);
[[nodiscard]] std::size_t      ScenarioEnumerationCount(const ScenarioDocument& document, bool multiplayer);

// NONE suppresses named map handoff.
[[nodiscard]] std::expected<std::optional<std::array<std::string, 4>>, ScenarioIssue>
ScenarioMapFiles(const ScenarioEntry& entry);

// This is a strict plain ASCII subset reader, not a general retail XML reader.
[[nodiscard]] std::expected<ScenarioDocument, ScenarioIssue>
ParseScenarioDocument(std::span<const std::byte> bytes);

// Uses root S, fixed field/locale/slot order, canonical decimals, and LF newlines.
[[nodiscard]] std::expected<std::vector<std::byte>, ScenarioIssue>
EncodeScenarioXml(const ScenarioDocument& document);

// The volume must support hard links.
[[nodiscard]] std::expected<void, ScenarioIssue>
WriteScenarioXml(const ScenarioDocument& document, const std::filesystem::path& destination);

} // namespace rerevved::tools
