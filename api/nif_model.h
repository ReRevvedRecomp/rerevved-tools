#pragma once

#include "nif_document.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

enum class NifModelError
{
    SceneCycle,
    WrongDataBlockType,
    InconsistentGeometry,
    TriangleSelectorOutOfRange,
    InvalidTransform,
    NoSupportedMeshes,
};

struct NifModelTransform
{
    std::array<double, 3> translation{};
    std::array<double, 9> rotation{ 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
    double                scale = 1.0;
};

struct NifModelMesh
{
    std::size_t                rootIndex = 0;
    std::vector<std::size_t>   nodePath;
    std::size_t                triShapeIndex     = 0;
    std::size_t                triShapeDataIndex = 0;
    std::vector<std::uint32_t> materialPropertyBlocks;
    NifModelTransform          worldTransform;
};

struct NifModel
{
    std::vector<NifModelMesh> meshes;
};

[[nodiscard]] std::string_view NifModelErrorMessage(NifModelError error);

[[nodiscard]] std::expected<std::array<double, 3>, NifModelError>
ApplyNifModelTransform(const NifModelTransform&    transform,
                       const std::array<float, 3>& position);

[[nodiscard]] std::expected<NifModel, NifModelError>
AssembleNifModel(const NifDocument& document);

} // namespace rerevved::tools
