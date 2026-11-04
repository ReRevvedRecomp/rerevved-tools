#pragma once

#include "inspect/inspector_navigation.h"
#include "nif_document.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rerevved::tools
{

enum class NifInspectorSection : std::uint8_t
{
    BlockTypes,
    Blocks,
    Strings,
    Groups,
    Roots,
    Nodes,
    TriangleShapes,
    TriangleData,
    MaterialProperties,
    TexturingProperties,
    SourceTextures,
};

struct NifInspectorState
{
    NifInspectorSection      currentSection = NifInspectorSection::BlockTypes;
    InspectorNavigationState blockTypes;
    InspectorNavigationState blocks;
    InspectorNavigationState strings;
    InspectorNavigationState groups;
    InspectorNavigationState roots;
    InspectorNavigationState nodes;
    InspectorNavigationState triangleShapes;
    InspectorNavigationState triangleData;
    InspectorNavigationState materialProperties;
    InspectorNavigationState texturingProperties;
    InspectorNavigationState sourceTextures;
};

inline void ResetNifInspectorState(NifInspectorState& state) noexcept
{
    state = {};
}

void DrawNifInspector(const NifDocument* document,
                      std::string_view   error,
                      NifInspectorState& state);

} // namespace rerevved::tools
