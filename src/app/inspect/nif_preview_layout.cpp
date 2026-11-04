#include "inspect/nif_preview.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace rerevved::tools
{
namespace
{

std::array<std::size_t, 2> projectionAxes(NifProjection projection)
{
    switch (projection)
    {
        case NifProjection::Xy:
            return { 0, 1 };
        case NifProjection::Xz:
            return { 0, 2 };
        case NifProjection::Yz:
            return { 1, 2 };
    }
    return { 0, 1 };
}

} // namespace

std::string_view NifProjectionName(NifProjection projection)
{
    switch (projection)
    {
        case NifProjection::Xy:
            return "Raw XY projection";
        case NifProjection::Xz:
            return "Raw XZ projection";
        case NifProjection::Yz:
            return "Raw YZ projection";
    }
    return "Raw projection";
}

std::string_view NifProjectionModeName(NifProjectionMode projection)
{
    switch (projection)
    {
        case NifProjectionMode::Automatic:
            return "Auto";
        case NifProjectionMode::Xy:
            return "XY";
        case NifProjectionMode::Xz:
            return "XZ";
        case NifProjectionMode::Yz:
            return "YZ";
    }
    return "Auto";
}

std::string_view NifPreviewErrorMessage(NifPreviewError error)
{
    switch (error)
    {
        case NifPreviewError::NonFinitePosition:
            return "The NIF mesh contains a nonfinite vertex position.";
        case NifPreviewError::UnrepresentableTransformedPosition:
            return "A transformed NIF vertex position is nonfinite or unrepresentable.";
        case NifPreviewError::EmptyProjectedBounds:
            return "The NIF mesh has no projected bounds to display.";
        case NifPreviewError::DegenerateProjection:
            return "The NIF mesh has a degenerate raw axis projection.";
        case NifPreviewError::UnavailableRegion:
            return "The NIF Preview region has no drawable area.";
    }
    return "The NIF mesh could not be previewed.";
}

void ResetNifPreviewView(NifPreviewState& state)
{
    state.pan  = {};
    state.zoom = 1.0;
}

bool SelectNifPreviewMesh(NifPreviewState& state,
                          std::size_t      meshCount,
                          std::size_t      requestedMesh)
{
    const bool valid    = requestedMesh < meshCount;
    const auto selected = valid ? requestedMesh : std::size_t{ 0 };
    if (!valid || state.selectedMesh != selected)
    {
        state.selectedMesh = selected;
        ResetNifPreviewView(state);
    }
    return valid;
}

void SelectNifPreviewProjection(NifPreviewState& state, NifProjectionMode projection)
{
    if (state.projection == projection)
    {
        return;
    }
    state.projection = projection;
    ResetNifPreviewView(state);
}

void PanNifPreview(NifPreviewState& state, float horizontal, float vertical)
{
    state.pan[0] += horizontal;
    state.pan[1] += vertical;
}

void ZoomNifPreview(NifPreviewState& state, float wheelDelta)
{
    constexpr double kMinimumZoom = 0.1;
    constexpr double kMaximumZoom = 20.0;
    state.zoom                    = std::clamp(state.zoom * std::pow(1.2, wheelDelta),
                                               kMinimumZoom,
                                               kMaximumZoom);
}

std::expected<std::array<double, 3>, NifPreviewError>
TransformNifPreviewPosition(const NifPreviewMeshView&   mesh,
                            const std::array<float, 3>& position)
{
    if (!std::ranges::all_of(position, [](float value)
                             {
                                 return std::isfinite(value);
                             }))
    {
        return std::unexpected(NifPreviewError::NonFinitePosition);
    }
    const auto transformed = ApplyNifModelTransform(mesh.transform, position);
    if (!transformed)
    {
        return std::unexpected(NifPreviewError::UnrepresentableTransformedPosition);
    }
    return *transformed;
}

std::expected<NifWireframeLayout, NifPreviewError> CalculateNifWireframeLayout(
    std::span<const NifPreviewMeshView> meshes,
    float                               regionWidth,
    float                               regionHeight,
    NifProjectionMode                   requestedProjection)
{
    if (meshes.empty() || std::ranges::any_of(meshes, [](const NifPreviewMeshView& mesh)
                                              {
                                                  return mesh.positions.empty() ||
                                                         mesh.triangles.empty();
                                              }))
    {
        return std::unexpected(NifPreviewError::EmptyProjectedBounds);
    }
    if (!std::isfinite(regionWidth) || !std::isfinite(regionHeight) ||
        regionWidth <= 0.0F || regionHeight <= 0.0F)
    {
        return std::unexpected(NifPreviewError::UnavailableRegion);
    }

    std::array<double, 3> minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    std::array<double, 3> maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    for (const auto& mesh : meshes)
    {
        for (const auto& position : mesh.positions)
        {
            const auto transformed = TransformNifPreviewPosition(mesh, position);
            if (!transformed)
            {
                return std::unexpected(transformed.error());
            }
            for (std::size_t axis = 0; axis < transformed->size(); ++axis)
            {
                minimum[axis] = std::min(minimum[axis], (*transformed)[axis]);
                maximum[axis] = std::max(maximum[axis], (*transformed)[axis]);
            }
        }
    }

    const std::array ranges{
        maximum[0] - minimum[0],
        maximum[1] - minimum[1],
        maximum[2] - minimum[2],
    };
    if (!std::ranges::all_of(ranges, [](double range)
                             {
                                 return std::isfinite(range);
                             }))
    {
        return std::unexpected(NifPreviewError::UnrepresentableTransformedPosition);
    }
    const std::array areas{
        ranges[0] * ranges[1],
        ranges[0] * ranges[2],
        ranges[1] * ranges[2],
    };
    if (!std::ranges::all_of(areas, [](double area)
                             {
                                 return std::isfinite(area);
                             }))
    {
        return std::unexpected(NifPreviewError::UnrepresentableTransformedPosition);
    }
    if (std::ranges::all_of(ranges,
                            [](double range)
                            {
                                return range == 0.0;
                            }))
    {
        return std::unexpected(NifPreviewError::EmptyProjectedBounds);
    }
    std::size_t projectionIndex = 0;
    if (requestedProjection == NifProjectionMode::Automatic)
    {
        for (std::size_t index = 1; index < areas.size(); ++index)
        {
            if (areas[index] > areas[projectionIndex])
            {
                projectionIndex = index;
            }
        }
    }
    else
    {
        projectionIndex = static_cast<std::size_t>(requestedProjection) - 1;
    }
    constexpr std::array kProjections{
        NifProjection::Xy,
        NifProjection::Xz,
        NifProjection::Yz,
    };
    const auto projection = kProjections[projectionIndex];
    const auto axes       = projectionAxes(projection);
    const auto width      = ranges[axes[0]];
    const auto height     = ranges[axes[1]];
    if (width <= 0.0 || height <= 0.0)
    {
        return std::unexpected(NifPreviewError::DegenerateProjection);
    }

    bool hasArea = false;
    for (const auto& mesh : meshes)
    {
        for (const auto& triangle : mesh.triangles)
        {
            const auto first = TransformNifPreviewPosition(
                mesh, mesh.positions[triangle[0]]);
            const auto second = TransformNifPreviewPosition(
                mesh, mesh.positions[triangle[1]]);
            const auto third = TransformNifPreviewPosition(
                mesh, mesh.positions[triangle[2]]);
            if (!first || !second || !third)
            {
                const auto error = !first ? first.error() : !second ? second.error()
                                                                    : third.error();
                return std::unexpected(error);
            }
            const auto twiceArea =
                ((*second)[axes[0]] - (*first)[axes[0]]) *
                    ((*third)[axes[1]] - (*first)[axes[1]]) -
                ((*second)[axes[1]] - (*first)[axes[1]]) *
                    ((*third)[axes[0]] - (*first)[axes[0]]);
            if (!std::isfinite(twiceArea))
            {
                return std::unexpected(
                    NifPreviewError::UnrepresentableTransformedPosition);
            }
            hasArea = hasArea || twiceArea != 0.0;
        }
    }
    if (!hasArea)
    {
        return std::unexpected(NifPreviewError::DegenerateProjection);
    }

    const auto padding = std::min(
        16.0F, std::min(regionWidth, regionHeight) * 0.1F);
    const auto drawableWidth  = regionWidth - padding * 2.0F;
    const auto drawableHeight = regionHeight - padding * 2.0F;
    if (drawableWidth <= 0.0F || drawableHeight <= 0.0F)
    {
        return std::unexpected(NifPreviewError::UnavailableRegion);
    }

    NifWireframeLayout layout;
    layout.projection   = projection;
    layout.scale        = std::min(drawableWidth / width, drawableHeight / height);
    layout.sourceCenter = {
        minimum[axes[0]] + ranges[axes[0]] * 0.5,
        minimum[axes[1]] + ranges[axes[1]] * 0.5,
    };
    layout.targetCenter = { regionWidth * 0.5F, regionHeight * 0.5F };
    if (!std::isfinite(layout.scale) || layout.scale <= 0.0 ||
        !std::ranges::all_of(layout.sourceCenter, [](double value)
                             {
                                 return std::isfinite(value);
                             }) ||
        !std::ranges::all_of(layout.targetCenter, [](double value)
                             {
                                 return std::isfinite(value);
                             }))
    {
        return std::unexpected(NifPreviewError::UnrepresentableTransformedPosition);
    }
    return layout;
}

NifWireframeLayout ApplyNifPreviewView(const NifWireframeLayout& layout,
                                       const NifPreviewState&    state)
{
    auto result = layout;
    result.scale *= state.zoom;
    result.targetCenter[0] += state.pan[0];
    result.targetCenter[1] += state.pan[1];
    return result;
}

std::array<float, 2> ProjectNifPosition(const NifWireframeLayout&    layout,
                                        const std::array<double, 3>& position)
{
    const auto axes = projectionAxes(layout.projection);
    return {
        static_cast<float>(layout.targetCenter[0] +
                           (position[axes[0]] - layout.sourceCenter[0]) * layout.scale),
        static_cast<float>(layout.targetCenter[1] -
                           (position[axes[1]] - layout.sourceCenter[1]) * layout.scale),
    };
}

} // namespace rerevved::tools
