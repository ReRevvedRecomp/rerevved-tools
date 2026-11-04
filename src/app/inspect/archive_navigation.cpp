#include "inspect/archive_explorer.h"
#include "inspect/inspection_format.h"

#include <algorithm>
#include <string>

namespace rerevved::tools
{
namespace
{

std::string foldAscii(std::string_view text)
{
    std::string result(text);
    for (auto& character : result)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character += 'a' - 'A';
        }
    }
    return result;
}

} // namespace

std::string_view ArchiveEntryHintName(FpkEntryKind kind)
{
    switch (kind)
    {
        case FpkEntryKind::Unknown:
            return "Unknown";
        case FpkEntryKind::DdsTexture:
            return "DDS texture";
        case FpkEntryKind::GfxMovie:
            return "GFX movie";
        case FpkEntryKind::Id3TaggedAudio:
            return "Audio tagged with ID3";
        case FpkEntryKind::NifContainer:
            return "Gamebryo NIF";
    }
    std::unreachable();
}

void UpdateArchiveFilter(const FpkDocument& document, ArchiveExplorerState& state)
{
    const std::string filter(state.filter.data());
    if (state.filterInitialized && state.appliedFilter == filter)
    {
        return;
    }
    const auto count = std::min(document.index.entries.size(), kMaximumArchiveNavigationEntries);
    if (!state.filterInitialized)
    {
        state.entryHints.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            state.entryHints.push_back(InspectFpkEntryKind(document, index).value_or(FpkEntryKind::Unknown));
        }
    }
    const auto query = foldAscii(filter);
    state.visibleEntries.clear();
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& entry = document.index.entries[index];
        if (query.empty() || foldAscii(FormatFpkEntryRange(index + 1, entry) + " " +
                                       std::string(ArchiveEntryHintName(state.entryHints[index])) + " " +
                                       FormatBytes(entry.unknown0) + " " + FormatBytes(entry.unknown1))
                                 .contains(query))
        {
            state.visibleEntries.push_back(index);
        }
    }
    state.appliedFilter     = filter;
    state.filterInitialized = true;
}

std::optional<std::size_t> FindArchiveNeighbor(const ArchiveExplorerState& state, bool next)
{
    if (!state.selectedEntry)
    {
        return std::nullopt;
    }
    const auto& entries = state.visibleEntries;
    if (next)
    {
        const auto found = std::upper_bound(entries.begin(), entries.end(), *state.selectedEntry);
        return found == entries.end() ? std::nullopt : std::optional(*found);
    }
    const auto found = std::lower_bound(entries.begin(), entries.end(), *state.selectedEntry);
    return found == entries.begin() ? std::nullopt : std::optional(*std::prev(found));
}

} // namespace rerevved::tools
