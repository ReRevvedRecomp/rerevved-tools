#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

enum class NifDocumentError
{
    Truncated,
    InvalidSignature,
    UnsupportedVersion,
    UnsupportedEndian,
    UnsupportedUserVersion,
    InvalidLayout,
};

struct NifBlock
{
    std::uint16_t typeIndex = 0;
    std::uint32_t size      = 0;
    std::size_t   offset    = 0;
};

struct NifAvObject
{
    std::uint32_t              nameIndex = UINT32_MAX;
    std::vector<std::uint32_t> extraData;
    std::uint32_t              controller = UINT32_MAX;
    std::uint16_t              flags      = 0;
    std::array<float, 3>       translation{};
    std::array<float, 9>       rotation{};
    float                      scale = 1.0F;
    std::vector<std::uint32_t> properties;
    std::uint32_t              collisionObject = UINT32_MAX;
};

struct NifNode
{
    std::uint32_t              blockIndex = 0;
    NifAvObject                object;
    std::vector<std::uint32_t> children;
    std::vector<std::uint32_t> effects;
};

struct NifMaterialData
{
    std::vector<std::uint32_t> nameIndices;
    std::vector<std::int32_t>  extraData;
    std::int32_t               activeMaterial      = -1;
    std::uint8_t               materialNeedsUpdate = 0;
};

struct NifMaterialPropertyInventory
{
    std::uint32_t        blockIndex = 0;
    std::array<float, 3> ambientColor{};
    std::array<float, 3> diffuseColor{};
    std::array<float, 3> specularColor{};
    std::array<float, 3> emissiveColor{};
    float                glossiness = 0.0F;
    float                alpha      = 0.0F;
};

struct NifTextureTransformInventory
{
    std::array<float, 2> translation{};
    std::array<float, 2> scale{};
    float                rotation        = 0.0F;
    std::uint32_t        transformMethod = 0;
    std::array<float, 2> center{};
};

struct NifTexDescInventory
{
    std::uint32_t                               source              = UINT32_MAX;
    std::uint16_t                               flags               = 0;
    std::uint8_t                                hasTextureTransform = 0;
    std::optional<NifTextureTransformInventory> transform;
};

struct NifBumpTextureInventory
{
    float                lumaScale  = 0.0F;
    float                lumaOffset = 0.0F;
    std::array<float, 4> bumpMatrix{};
};

struct NifTextureSlotInventory
{
    std::uint8_t                           presence = 0;
    std::optional<NifTexDescInventory>     descriptor;
    std::optional<NifBumpTextureInventory> bump;
    std::optional<float>                   parallaxOffset;
};

struct NifShaderTextureInventory
{
    std::uint8_t                       hasMap = 0;
    std::optional<NifTexDescInventory> descriptor;
    std::optional<std::uint32_t>       mapId;
};

struct NifTexturingPropertyInventory
{
    std::uint32_t                          blockIndex   = 0;
    std::uint16_t                          flags        = 0;
    std::uint32_t                          textureCount = 0;
    std::array<NifTextureSlotInventory, 9> standardSlots;
    std::uint32_t                          shaderTextureCount = 0;
    std::vector<NifShaderTextureInventory> shaderTextures;
};

struct NifSourceTextureInventory
{
    std::uint32_t blockIndex        = 0;
    std::uint8_t  useExternal       = 0;
    std::uint32_t fileNameIndex     = UINT32_MAX;
    std::uint32_t pixelData         = UINT32_MAX;
    std::uint32_t pixelLayout       = 0;
    std::uint32_t useMipmaps        = 0;
    std::uint32_t alphaFormat       = 0;
    std::uint8_t  isStatic          = 0;
    std::uint8_t  directRender      = 0;
    std::uint8_t  persistRenderData = 0;

    [[nodiscard]] bool IsSupportedExternalSource() const noexcept
    {
        return useExternal == 1 && fileNameIndex != UINT32_MAX && pixelData == UINT32_MAX;
    }
};

struct NifTriShape
{
    std::uint32_t   blockIndex = 0;
    NifAvObject     object;
    std::uint32_t   data         = UINT32_MAX;
    std::uint32_t   skinInstance = UINT32_MAX;
    NifMaterialData material;
};

struct NifTriShapeDataInventory
{
    std::uint32_t                             blockIndex    = 0;
    std::int32_t                              groupId       = 0;
    std::uint16_t                             vertexCount   = 0;
    std::uint8_t                              keepFlags     = 0;
    std::uint8_t                              compressFlags = 0;
    std::uint8_t                              hasVertices   = 0;
    std::vector<std::array<float, 3>>         vertexPositions;
    std::uint16_t                             dataFlags  = 0;
    std::uint8_t                              hasNormals = 0;
    std::vector<std::array<float, 3>>         normalVectors;
    std::array<float, 3>                      boundCenter{};
    float                                     boundRadius        = 0.0F;
    std::uint8_t                              hasVertexColors    = 0;
    std::uint16_t                             consistencyFlags   = 0;
    std::uint32_t                             additionalData     = UINT32_MAX;
    std::uint16_t                             triangleCount      = 0;
    std::uint32_t                             trianglePointCount = 0;
    std::uint8_t                              hasTriangles       = 0;
    std::vector<std::array<std::uint16_t, 3>> triangles;
    std::uint16_t                             matchGroupCount = 0;
    std::vector<std::vector<std::uint16_t>>   normalSharingGroups;
};

struct NifDocument
{
    std::uint32_t                              version         = 0;
    std::uint8_t                               endian          = 0;
    std::uint32_t                              userVersion     = 0;
    std::size_t                                headerSize      = 0;
    std::uint32_t                              maxStringLength = 0;
    std::vector<std::vector<std::byte>>        blockTypes;
    std::vector<NifBlock>                      blocks;
    std::vector<std::vector<std::byte>>        strings;
    std::vector<std::uint32_t>                 groups;
    std::vector<std::uint32_t>                 roots;
    std::vector<NifNode>                       nodes;
    std::vector<NifTriShape>                   triShapes;
    std::vector<NifTriShapeDataInventory>      triShapeData;
    std::vector<NifMaterialPropertyInventory>  materialProperties;
    std::vector<NifTexturingPropertyInventory> texturingProperties;
    std::vector<NifSourceTextureInventory>     sourceTextures;
};

[[nodiscard]] std::string_view NifDocumentErrorMessage(NifDocumentError error);

[[nodiscard]] std::expected<NifDocument, NifDocumentError>
ParseNifDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<NifDocument, std::string>
LoadNifDocument(const std::filesystem::path& path);

} // namespace rerevved::tools
