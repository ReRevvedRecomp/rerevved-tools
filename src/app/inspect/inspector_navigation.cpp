#include "inspect/inspector_navigation.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>

namespace rerevved::tools
{
namespace
{

char foldAscii(char value) noexcept
{
    if (value >= 'A' && value <= 'Z')
    {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

bool containsAsciiInsensitive(std::string_view text, std::string_view needle) noexcept
{
    if (needle.empty())
    {
        return true;
    }
    if (needle.size() > text.size())
    {
        return false;
    }

    for (std::size_t offset = 0; offset + needle.size() <= text.size(); ++offset)
    {
        bool matches = true;
        for (std::size_t index = 0; index < needle.size(); ++index)
        {
            if (foldAscii(text[offset + index]) != foldAscii(needle[index]))
            {
                matches = false;
                break;
            }
        }
        if (matches)
        {
            return true;
        }
    }
    return false;
}

} // namespace

void ResetInspectorNavigationState(InspectorNavigationState& state) noexcept
{
    state = {};
}

std::vector<std::size_t> FilterInspectorIndices(std::span<const std::string> labels,
                                                std::string_view             filter)
{
    std::vector<std::size_t> result;
    result.reserve(labels.size());
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        if (containsAsciiInsensitive(labels[index], filter))
        {
            result.push_back(index);
        }
    }
    return result;
}

bool SelectInspectorIndex(InspectorNavigationState& state,
                          std::size_t               originalIndex,
                          std::size_t               itemCount) noexcept
{
    if (originalIndex >= itemCount)
    {
        return false;
    }
    state.selectedOriginalIndex = originalIndex;
    return true;
}

std::optional<std::size_t> FindInspectorNeighbor(
    std::span<const std::size_t> matchingIndices,
    std::optional<std::size_t>   selectedOriginalIndex,
    bool                         next) noexcept
{
    if (matchingIndices.empty())
    {
        return std::nullopt;
    }
    if (!selectedOriginalIndex)
    {
        return next ? matchingIndices.front() : matchingIndices.back();
    }

    const auto position = std::lower_bound(matchingIndices.begin(),
                                           matchingIndices.end(),
                                           *selectedOriginalIndex);
    if (position != matchingIndices.end() && *position == *selectedOriginalIndex)
    {
        if (next)
        {
            return std::next(position) != matchingIndices.end()
                       ? std::optional<std::size_t>{ *std::next(position) }
                       : std::nullopt;
        }
        if (position != matchingIndices.begin())
        {
            return *std::prev(position);
        }
        return std::nullopt;
    }

    if (next)
    {
        return position == matchingIndices.end()
                   ? std::nullopt
                   : std::optional<std::size_t>{ *position };
    }
    return position == matchingIndices.begin()
               ? std::nullopt
               : std::optional<std::size_t>{ *std::prev(position) };
}

bool JumpInspectorIndex(InspectorNavigationState& state,
                        std::string_view          enteredIndex,
                        std::size_t               indexBase,
                        std::size_t               itemCount) noexcept
{
    if (enteredIndex.empty())
    {
        return false;
    }

    std::size_t parsed = 0;
    const auto  result = std::from_chars(enteredIndex.data(),
                                         enteredIndex.data() + enteredIndex.size(),
                                         parsed);
    if (result.ec != std::errc{} || result.ptr != enteredIndex.data() + enteredIndex.size() ||
        parsed < indexBase)
    {
        return false;
    }

    const auto originalIndex = parsed - indexBase;
    return SelectInspectorIndex(state, originalIndex, itemCount);
}

bool InspectorSelectionHidden(const InspectorNavigationState& state,
                              std::span<const std::size_t>    matchingIndices) noexcept
{
    if (!state.selectedOriginalIndex)
    {
        return false;
    }
    return !std::binary_search(matchingIndices.begin(),
                               matchingIndices.end(),
                               *state.selectedOriginalIndex);
}

} // namespace rerevved::tools
