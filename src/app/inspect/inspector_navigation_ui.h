#pragma once

#include "inspect/inspector_navigation.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace rerevved::tools
{

[[nodiscard]] std::optional<std::size_t> DrawInspectorNavigation(
    std::span<const std::string> labels,
    InspectorNavigationState&    state,
    const char*                  identityLabel,
    std::size_t                  indexBase = 0);

} // namespace rerevved::tools
