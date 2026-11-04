#pragma once

#include "gfx_document.h"
#include "inspect/inspector_navigation.h"

#include <string_view>

namespace rerevved::tools
{

void DrawGfxInspector(const GfxDocument*        document,
                      std::string_view          error,
                      InspectorNavigationState& state);

} // namespace rerevved::tools
