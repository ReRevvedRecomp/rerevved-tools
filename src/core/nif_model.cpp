#include "nif_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rerevved::tools
{
namespace
{

constexpr std::uint32_t kNullReference = std::numeric_limits<std::uint32_t>::max();

bool equals(std::span<const std::byte> bytes, std::string_view value)
{
    return bytes.size() == value.size() &&
           std::ranges::equal(bytes, std::as_bytes(std::span(value.data(), value.size())));
}

bool blockHasType(const NifDocument& document,
                  std::uint32_t      blockIndex,
                  std::string_view   expectedType)
{
    if (blockIndex >= document.blocks.size())
    {
        return false;
    }
    const auto typeIndex = document.blocks[blockIndex].typeIndex;
    return typeIndex < document.blockTypes.size() &&
           equals(document.blockTypes[typeIndex], expectedType);
}

template <typename Value>
std::optional<std::size_t> findParsedBlock(const std::vector<Value>& values,
                                           std::uint32_t             blockIndex)
{
    const auto found = std::ranges::find(values, blockIndex, &Value::blockIndex);
    if (found == values.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(values.begin(), found));
}

std::optional<NifModelError> validateGeometry(const NifTriShapeDataInventory& data)
{
    if (data.hasVertices == 0 || data.vertexPositions.empty() ||
        data.vertexPositions.size() != data.vertexCount || data.hasTriangles == 0 ||
        data.triangles.empty() || data.triangles.size() != data.triangleCount ||
        (data.hasNormals == 0 && !data.normalVectors.empty()) ||
        (data.hasNormals != 0 &&
         data.normalVectors.size() != data.vertexPositions.size()))
    {
        return NifModelError::InconsistentGeometry;
    }

    for (const auto& triangle : data.triangles)
    {
        for (const auto selector : triangle)
        {
            if (selector >= data.vertexPositions.size())
            {
                return NifModelError::TriangleSelectorOutOfRange;
            }
        }
    }
    return std::nullopt;
}

std::array<double, 3> rotate(const std::array<double, 9>& rotation,
                             const std::array<double, 3>& value)
{
    return {
        rotation[0] * value[0] + rotation[3] * value[1] + rotation[6] * value[2],
        rotation[1] * value[0] + rotation[4] * value[1] + rotation[7] * value[2],
        rotation[2] * value[0] + rotation[5] * value[1] + rotation[8] * value[2],
    };
}

bool isFinite(const NifModelTransform& transform)
{
    return std::ranges::all_of(transform.translation, [](double value)
                               {
                                   return std::isfinite(value);
                               }) &&
           std::ranges::all_of(transform.rotation, [](double value)
                               {
                                   return std::isfinite(value);
                               }) &&
           std::isfinite(transform.scale);
}

std::expected<NifModelTransform, NifModelError> localTransform(const NifAvObject& object)
{
    NifModelTransform transform;
    std::ranges::copy(object.translation, transform.translation.begin());
    std::ranges::copy(object.rotation, transform.rotation.begin());
    transform.scale = object.scale;
    if (!isFinite(transform))
    {
        return std::unexpected(NifModelError::InvalidTransform);
    }
    return transform;
}

std::expected<NifModelTransform, NifModelError>
compose(const NifModelTransform& parent, const NifModelTransform& local)
{
    NifModelTransform result;
    result.scale = parent.scale * local.scale;
    for (std::size_t column = 0; column < 3; ++column)
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            result.rotation[column * 3 + row] =
                parent.rotation[row] * local.rotation[column * 3] +
                parent.rotation[3 + row] * local.rotation[column * 3 + 1] +
                parent.rotation[6 + row] * local.rotation[column * 3 + 2];
        }
    }

    const auto rotatedTranslation = rotate(parent.rotation, local.translation);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        result.translation[axis] =
            parent.translation[axis] + parent.scale * rotatedTranslation[axis];
    }
    if (!isFinite(result))
    {
        return std::unexpected(NifModelError::InvalidTransform);
    }
    return result;
}

struct TraversalItem
{
    std::uint32_t blockIndex  = kNullReference;
    std::size_t   nodeIndex   = 0;
    bool          leavingNode = false;
};

} // namespace

std::string_view NifModelErrorMessage(NifModelError error)
{
    switch (error)
    {
        case NifModelError::SceneCycle:
            return "The NIF scene graph contains a cycle.";
        case NifModelError::WrongDataBlockType:
            return "A NiTriShape data reference does not target NiTriShapeData.";
        case NifModelError::InconsistentGeometry:
            return "The retained NIF geometry arrays are inconsistent.";
        case NifModelError::TriangleSelectorOutOfRange:
            return "A NIF triangle selector is outside the retained vertex positions.";
        case NifModelError::InvalidTransform:
            return "A NIF scene transform is nonfinite or unrepresentable.";
        case NifModelError::NoSupportedMeshes:
            return "The NIF scene contains no supported meshes.";
    }
    return "The NIF model could not be assembled.";
}

std::expected<std::array<double, 3>, NifModelError>
ApplyNifModelTransform(const NifModelTransform&    transform,
                       const std::array<float, 3>& position)
{
    const std::array<double, 3> source{ position[0], position[1], position[2] };
    const auto                  rotated = rotate(transform.rotation, source);
    std::array<double, 3>       result{};
    for (std::size_t axis = 0; axis < result.size(); ++axis)
    {
        result[axis] = transform.translation[axis] + transform.scale * rotated[axis];
    }
    if (!std::ranges::all_of(result, [](double value)
                             {
                                 return std::isfinite(value);
                             }))
    {
        return std::unexpected(NifModelError::InvalidTransform);
    }
    return result;
}

std::expected<NifModel, NifModelError> AssembleNifModel(const NifDocument& document)
{
    NifModel          model;
    std::vector<bool> activeNodes(document.nodes.size());

    for (std::size_t rootIndex = 0; rootIndex < document.roots.size(); ++rootIndex)
    {
        std::vector<std::size_t>   nodePath;
        std::vector<TraversalItem> stack{ { document.roots[rootIndex], 0, false } };

        while (!stack.empty())
        {
            const auto item = stack.back();
            stack.pop_back();
            if (item.leavingNode)
            {
                activeNodes[item.nodeIndex] = false;
                nodePath.pop_back();
                continue;
            }
            if (item.blockIndex == kNullReference)
            {
                continue;
            }

            if (blockHasType(document, item.blockIndex, "NiNode"))
            {
                const auto nodeIndex = findParsedBlock(document.nodes, item.blockIndex);
                if (!nodeIndex)
                {
                    continue;
                }
                if (activeNodes[*nodeIndex])
                {
                    return std::unexpected(NifModelError::SceneCycle);
                }

                activeNodes[*nodeIndex] = true;
                nodePath.push_back(*nodeIndex);
                stack.push_back({ kNullReference, *nodeIndex, true });
                const auto& children = document.nodes[*nodeIndex].children;
                for (auto child = children.rbegin(); child != children.rend(); ++child)
                {
                    stack.push_back({ *child, 0, false });
                }
                continue;
            }

            if (!blockHasType(document, item.blockIndex, "NiTriShape"))
            {
                continue;
            }
            const auto shapeIndex = findParsedBlock(document.triShapes, item.blockIndex);
            if (!shapeIndex)
            {
                continue;
            }
            const auto& shape = document.triShapes[*shapeIndex];
            if (shape.data == kNullReference)
            {
                continue;
            }
            if (!blockHasType(document, shape.data, "NiTriShapeData"))
            {
                return std::unexpected(NifModelError::WrongDataBlockType);
            }
            const auto dataIndex = findParsedBlock(document.triShapeData, shape.data);
            if (!dataIndex)
            {
                return std::unexpected(NifModelError::InconsistentGeometry);
            }
            if (const auto error = validateGeometry(document.triShapeData[*dataIndex]))
            {
                return std::unexpected(*error);
            }

            NifModelMesh mesh;
            mesh.rootIndex         = rootIndex;
            mesh.nodePath          = nodePath;
            mesh.triShapeIndex     = *shapeIndex;
            mesh.triShapeDataIndex = *dataIndex;
            auto worldTransform    = localTransform(shape.object);
            if (!worldTransform)
            {
                return std::unexpected(worldTransform.error());
            }
            for (auto node = nodePath.rbegin(); node != nodePath.rend(); ++node)
            {
                const auto parentTransform = localTransform(document.nodes[*node].object);
                if (!parentTransform)
                {
                    return std::unexpected(parentTransform.error());
                }
                worldTransform = compose(*parentTransform, *worldTransform);
                if (!worldTransform)
                {
                    return std::unexpected(worldTransform.error());
                }
            }
            mesh.worldTransform = *worldTransform;
            for (const auto property : shape.object.properties)
            {
                if (property != kNullReference &&
                    blockHasType(document, property, "NiMaterialProperty"))
                {
                    mesh.materialPropertyBlocks.push_back(property);
                }
            }
            model.meshes.push_back(std::move(mesh));
        }
    }

    if (model.meshes.empty())
    {
        return std::unexpected(NifModelError::NoSupportedMeshes);
    }
    return model;
}

} // namespace rerevved::tools
