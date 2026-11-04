#pragma once

#include "texture_import.h"

#include <array>
#include <string>

namespace rerevved::tools
{

enum class PathModalKind
{
    TextureSource,
    OutputParent,
};

enum class PathModalAction
{
    None,
    Submit,
    Cancel,
};

struct PathModalState
{
    PathModalKind          kind       = PathModalKind::TextureSource;
    TextureSourceKind      sourceKind = TextureSourceKind::Png;
    std::array<char, 4096> path{};
    std::string            diagnostic;
    bool                   requested      = false;
    bool                   closeRequested = false;
};

void                          OpenPathModal(PathModalState& state, PathModalKind kind, TextureSourceKind sourceKind = TextureSourceKind::Png);
[[nodiscard]] PathModalAction DrawPathModal(PathModalState& state);
void                          ClosePathModal(PathModalState& state);

} // namespace rerevved::tools
