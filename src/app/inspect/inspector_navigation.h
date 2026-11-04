#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

inline constexpr std::size_t kInspectorFilterCapacity     = 256;
inline constexpr std::size_t kInspectorIndexInputCapacity = 32;

struct InspectorNavigationState
{
    std::optional<std::size_t>                     selectedOriginalIndex;
    std::array<char, kInspectorFilterCapacity>     filter{};
    std::array<char, kInspectorIndexInputCapacity> originalIndexInput{};
    std::string                                    navigationError;
};

void ResetInspectorNavigationState(InspectorNavigationState& state) noexcept;

// Filtering uses an ASCII case insensitive substring over the supplied display labels. The
// caller owns the labels, so it can include an original index and any escaped retained name.
[[nodiscard]] std::vector<std::size_t> FilterInspectorIndices(
    std::span<const std::string> labels,
    std::string_view             filter);

[[nodiscard]] bool SelectInspectorIndex(InspectorNavigationState& state,
                                        std::size_t               originalIndex,
                                        std::size_t               itemCount) noexcept;

[[nodiscard]] std::optional<std::size_t> FindInspectorNeighbor(
    std::span<const std::size_t> matchingIndices,
    std::optional<std::size_t>   selectedOriginalIndex,
    bool                         next) noexcept;

// indexBase is zero for NIF indices and one for GFX ordinals.
[[nodiscard]] bool JumpInspectorIndex(InspectorNavigationState& state,
                                      std::string_view          enteredIndex,
                                      std::size_t               indexBase,
                                      std::size_t               itemCount) noexcept;

[[nodiscard]] bool InspectorSelectionHidden(
    const InspectorNavigationState& state,
    std::span<const std::size_t>    matchingIndices) noexcept;

} // namespace rerevved::tools
