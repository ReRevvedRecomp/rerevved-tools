#pragma once

#include "nif_document.h"
#include "nif_model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rerevved::tools
{

enum class NifProjection
{
    Xy,
    Xz,
    Yz,
};

enum class NifProjectionMode
{
    Automatic,
    Xy,
    Xz,
    Yz,
};

enum class NifPreviewError
{
    NonFinitePosition,
    UnrepresentableTransformedPosition,
    EmptyProjectedBounds,
    DegenerateProjection,
    UnavailableRegion,
};

struct NifPreviewMeshView
{
    std::span<const std::array<float, 3>>         positions;
    std::span<const std::array<std::uint16_t, 3>> triangles;
    NifModelTransform                             transform;
};

struct NifWireframeLayout
{
    NifProjection         projection = NifProjection::Xy;
    double                scale      = 1.0;
    std::array<double, 2> sourceCenter{};
    std::array<double, 2> targetCenter{};
};

struct NifPreviewState
{
    std::size_t          selectedMesh = 0;
    NifProjectionMode    projection   = NifProjectionMode::Automatic;
    std::array<float, 2> pan{};
    double               zoom = 1.0;
    std::string          presentationError;
};

[[nodiscard]] std::string_view NifProjectionName(NifProjection projection);
[[nodiscard]] std::string_view NifProjectionModeName(NifProjectionMode projection);
[[nodiscard]] std::string_view NifPreviewErrorMessage(NifPreviewError error);

void               ResetNifPreviewView(NifPreviewState& state);
[[nodiscard]] bool SelectNifPreviewMesh(NifPreviewState& state,
                                        std::size_t      meshCount,
                                        std::size_t      requestedMesh);
void               SelectNifPreviewProjection(NifPreviewState&  state,
                                              NifProjectionMode projection);
void               PanNifPreview(NifPreviewState& state, float horizontal, float vertical);
void               ZoomNifPreview(NifPreviewState& state, float wheelDelta);

[[nodiscard]] std::expected<NifWireframeLayout, NifPreviewError>
CalculateNifWireframeLayout(
    std::span<const NifPreviewMeshView> meshes,
    float                               regionWidth,
    float                               regionHeight,
    NifProjectionMode                   projection = NifProjectionMode::Automatic);

[[nodiscard]] std::expected<std::array<double, 3>, NifPreviewError>
TransformNifPreviewPosition(const NifPreviewMeshView&   mesh,
                            const std::array<float, 3>& position);

[[nodiscard]] NifWireframeLayout
ApplyNifPreviewView(const NifWireframeLayout& layout, const NifPreviewState& state);

[[nodiscard]] std::array<float, 2>
ProjectNifPosition(const NifWireframeLayout& layout, const std::array<double, 3>& position);

void DrawNifPreview(const NifDocument*           document,
                    const NifModel*              model,
                    std::optional<NifModelError> assemblyError,
                    std::string_view             documentError,
                    NifPreviewState&             state);

} // namespace rerevved::tools
