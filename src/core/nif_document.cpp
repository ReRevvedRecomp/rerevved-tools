#include "nif_document.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <string_view>

namespace rerevved::tools
{
namespace
{

constexpr std::string_view kHeader                   = "Gamebryo File Format, Version 20.3.0.9\n";
constexpr std::uint32_t    kVersion                  = 0x14030009;
constexpr std::size_t      kStandardTextureSlotCount = 9;
constexpr std::size_t      kBumpTextureSlot          = 5;
constexpr std::size_t      kParallaxTextureSlot      = 7;

bool canRead(std::span<const std::byte> bytes, std::size_t offset, std::size_t count)
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

std::uint16_t readU16Be(std::span<const std::byte> bytes, std::size_t offset)
{
    return (std::to_integer<std::uint16_t>(bytes[offset]) << 8U) |
           std::to_integer<std::uint16_t>(bytes[offset + 1]);
}

std::uint32_t readU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::uint32_t readU32Be(std::span<const std::byte> bytes, std::size_t offset)
{
    return (std::to_integer<std::uint32_t>(bytes[offset]) << 24U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 8U) |
           std::to_integer<std::uint32_t>(bytes[offset + 3]);
}

bool canReadArray(std::span<const std::byte> bytes,
                  std::size_t                offset,
                  std::uint32_t              count,
                  std::size_t                elementSize)
{
    return offset <= bytes.size() && count <= (bytes.size() - offset) / elementSize;
}

bool equals(std::span<const std::byte> bytes, std::string_view value)
{
    return bytes.size() == value.size() &&
           std::ranges::equal(bytes, std::as_bytes(std::span(value.data(), value.size())));
}

bool readBlockU8(std::span<const std::byte> bytes, std::size_t& offset, std::uint8_t& value)
{
    if (!canRead(bytes, offset, 1))
    {
        return false;
    }
    value = std::to_integer<std::uint8_t>(bytes[offset]);
    ++offset;
    return true;
}

bool readBlockU16(std::span<const std::byte> bytes, std::size_t& offset, std::uint16_t& value)
{
    if (!canRead(bytes, offset, 2))
    {
        return false;
    }
    value = readU16Be(bytes, offset);
    offset += 2;
    return true;
}

bool readBlockU32(std::span<const std::byte> bytes, std::size_t& offset, std::uint32_t& value)
{
    if (!canRead(bytes, offset, 4))
    {
        return false;
    }
    value = readU32Be(bytes, offset);
    offset += 4;
    return true;
}

bool readBlockF32(std::span<const std::byte> bytes, std::size_t& offset, float& value)
{
    std::uint32_t bits = 0;
    if (!readBlockU32(bytes, offset, bits))
    {
        return false;
    }
    value = std::bit_cast<float>(bits);
    return true;
}

bool readVector3Array(std::span<const std::byte>         bytes,
                      std::size_t&                       offset,
                      std::uint32_t                      count,
                      std::vector<std::array<float, 3>>& values)
{
    if (!canReadArray(bytes, offset, count, 12))
    {
        return false;
    }
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::array<float, 3> value{};
        for (auto& component : value)
        {
            if (!readBlockF32(bytes, offset, component))
            {
                return false;
            }
        }
        values.push_back(value);
    }
    return true;
}

bool readTriangleArray(std::span<const std::byte>                 bytes,
                       std::size_t&                               offset,
                       std::uint32_t                              count,
                       std::vector<std::array<std::uint16_t, 3>>& values)
{
    if (!canReadArray(bytes, offset, count, 6))
    {
        return false;
    }
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::array<std::uint16_t, 3> value{};
        for (auto& selector : value)
        {
            if (!readBlockU16(bytes, offset, selector))
            {
                return false;
            }
        }
        values.push_back(value);
    }
    return true;
}

bool skipBlockArray(std::span<const std::byte> bytes,
                    std::size_t&               offset,
                    std::uint32_t              count,
                    std::size_t                elementSize)
{
    if (!canReadArray(bytes, offset, count, elementSize))
    {
        return false;
    }
    offset += static_cast<std::size_t>(count) * elementSize;
    return true;
}

bool isValidReference(std::uint32_t value, std::size_t blockCount)
{
    return value == std::numeric_limits<std::uint32_t>::max() || value < blockCount;
}

bool isValidStringIndex(std::uint32_t value, std::size_t stringCount)
{
    return value == std::numeric_limits<std::uint32_t>::max() || value < stringCount;
}

bool readReference(std::span<const std::byte> bytes,
                   std::size_t&               offset,
                   std::size_t                blockCount,
                   std::uint32_t&             value)
{
    return readBlockU32(bytes, offset, value) && isValidReference(value, blockCount);
}

bool readReferences(std::span<const std::byte>  bytes,
                    std::size_t&                offset,
                    std::uint32_t               count,
                    std::size_t                 blockCount,
                    std::vector<std::uint32_t>& values)
{
    if (!canReadArray(bytes, offset, count, 4))
    {
        return false;
    }
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t value = 0;
        if (!readReference(bytes, offset, blockCount, value))
        {
            return false;
        }
        values.push_back(value);
    }
    return true;
}

bool readStringIndices(std::span<const std::byte>  bytes,
                       std::size_t&                offset,
                       std::uint32_t               count,
                       std::size_t                 stringCount,
                       std::vector<std::uint32_t>& values)
{
    if (!canReadArray(bytes, offset, count, 4))
    {
        return false;
    }
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t value = 0;
        if (!readBlockU32(bytes, offset, value) || !isValidStringIndex(value, stringCount))
        {
            return false;
        }
        values.push_back(value);
    }
    return true;
}

bool readSignedValues(std::span<const std::byte> bytes,
                      std::size_t&               offset,
                      std::uint32_t              count,
                      std::vector<std::int32_t>& values)
{
    if (!canReadArray(bytes, offset, count, 4))
    {
        return false;
    }
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t value = 0;
        if (!readBlockU32(bytes, offset, value))
        {
            return false;
        }
        values.push_back(std::bit_cast<std::int32_t>(value));
    }
    return true;
}

bool parseObjectNet(std::span<const std::byte>  bytes,
                    std::size_t&                offset,
                    std::size_t                 blockCount,
                    std::size_t                 stringCount,
                    std::uint32_t&              nameIndex,
                    std::vector<std::uint32_t>& extraData,
                    std::uint32_t&              controller)
{
    std::uint32_t count = 0;
    return readBlockU32(bytes, offset, nameIndex) &&
           isValidStringIndex(nameIndex, stringCount) &&
           readBlockU32(bytes, offset, count) &&
           readReferences(bytes, offset, count, blockCount, extraData) &&
           readReference(bytes, offset, blockCount, controller);
}

bool parseAvObject(std::span<const std::byte> bytes,
                   std::size_t&               offset,
                   std::size_t                blockCount,
                   std::size_t                stringCount,
                   NifAvObject&               object)
{
    std::uint32_t count = 0;
    if (!parseObjectNet(bytes,
                        offset,
                        blockCount,
                        stringCount,
                        object.nameIndex,
                        object.extraData,
                        object.controller) ||
        !readBlockU16(bytes, offset, object.flags))
    {
        return false;
    }

    for (auto& value : object.translation)
    {
        if (!readBlockF32(bytes, offset, value))
        {
            return false;
        }
    }
    for (auto& value : object.rotation)
    {
        if (!readBlockF32(bytes, offset, value))
        {
            return false;
        }
    }
    if (!readBlockF32(bytes, offset, object.scale))
    {
        return false;
    }

    return readBlockU32(bytes, offset, count) &&
           readReferences(bytes, offset, count, blockCount, object.properties) &&
           readReference(bytes, offset, blockCount, object.collisionObject);
}

bool parseNode(std::span<const std::byte> bytes,
               std::uint32_t              blockIndex,
               std::size_t                blockCount,
               std::size_t                stringCount,
               NifNode&                   node)
{
    std::size_t offset  = 0;
    node.blockIndex     = blockIndex;
    std::uint32_t count = 0;
    return parseAvObject(bytes, offset, blockCount, stringCount, node.object) &&
           readBlockU32(bytes, offset, count) &&
           readReferences(bytes, offset, count, blockCount, node.children) &&
           readBlockU32(bytes, offset, count) &&
           readReferences(bytes, offset, count, blockCount, node.effects) &&
           offset == bytes.size();
}

bool parseTriShape(std::span<const std::byte> bytes,
                   std::uint32_t              blockIndex,
                   std::size_t                blockCount,
                   std::size_t                stringCount,
                   NifTriShape&               shape)
{
    std::size_t offset           = 0;
    shape.blockIndex             = blockIndex;
    std::uint32_t materialCount  = 0;
    std::uint32_t activeMaterial = 0;
    if (!parseAvObject(bytes, offset, blockCount, stringCount, shape.object) ||
        !readReference(bytes, offset, blockCount, shape.data) ||
        !readReference(bytes, offset, blockCount, shape.skinInstance) ||
        !readBlockU32(bytes, offset, materialCount) ||
        !readStringIndices(
            bytes, offset, materialCount, stringCount, shape.material.nameIndices) ||
        !readSignedValues(bytes, offset, materialCount, shape.material.extraData) ||
        !readBlockU32(bytes, offset, activeMaterial) ||
        !readBlockU8(bytes, offset, shape.material.materialNeedsUpdate) ||
        offset != bytes.size())
    {
        return false;
    }
    shape.material.activeMaterial = std::bit_cast<std::int32_t>(activeMaterial);
    return true;
}

bool parseMaterialProperty(std::span<const std::byte>    bytes,
                           std::uint32_t                 blockIndex,
                           std::size_t                   blockCount,
                           std::size_t                   stringCount,
                           NifMaterialPropertyInventory& material)
{
    std::size_t                offset    = 0;
    std::uint32_t              nameIndex = 0;
    std::vector<std::uint32_t> extraData;
    std::uint32_t              controller = 0;
    material.blockIndex                   = blockIndex;
    if (!parseObjectNet(bytes,
                        offset,
                        blockCount,
                        stringCount,
                        nameIndex,
                        extraData,
                        controller))
    {
        return false;
    }

    for (auto* color : { &material.ambientColor,
                         &material.diffuseColor,
                         &material.specularColor,
                         &material.emissiveColor })
    {
        for (auto& component : *color)
        {
            if (!readBlockF32(bytes, offset, component))
            {
                return false;
            }
        }
    }
    return readBlockF32(bytes, offset, material.glossiness) &&
           readBlockF32(bytes, offset, material.alpha) && offset == bytes.size();
}

bool parseTextureTransform(std::span<const std::byte>    bytes,
                           std::size_t&                  offset,
                           NifTextureTransformInventory& transform)
{
    for (auto& component : transform.translation)
    {
        if (!readBlockF32(bytes, offset, component))
        {
            return false;
        }
    }
    for (auto& component : transform.scale)
    {
        if (!readBlockF32(bytes, offset, component))
        {
            return false;
        }
    }
    if (!readBlockF32(bytes, offset, transform.rotation) ||
        !readBlockU32(bytes, offset, transform.transformMethod))
    {
        return false;
    }
    for (auto& component : transform.center)
    {
        if (!readBlockF32(bytes, offset, component))
        {
            return false;
        }
    }
    return true;
}

bool parseTexDesc(std::span<const std::byte> bytes,
                  std::size_t&               offset,
                  std::size_t                blockCount,
                  NifTexDescInventory&       descriptor)
{
    if (!readReference(bytes, offset, blockCount, descriptor.source) ||
        !readBlockU16(bytes, offset, descriptor.flags) ||
        !readBlockU8(bytes, offset, descriptor.hasTextureTransform))
    {
        return false;
    }
    if (descriptor.hasTextureTransform == 1)
    {
        return parseTextureTransform(bytes, offset, descriptor.transform.emplace());
    }
    return true;
}

bool parseTexturingProperty(std::span<const std::byte>     bytes,
                            std::uint32_t                  blockIndex,
                            std::size_t                    blockCount,
                            std::size_t                    stringCount,
                            NifTexturingPropertyInventory& property)
{
    std::size_t                offset    = 0;
    std::uint32_t              nameIndex = 0;
    std::vector<std::uint32_t> extraData;
    std::uint32_t              controller = 0;
    property.blockIndex                   = blockIndex;
    if (!parseObjectNet(bytes,
                        offset,
                        blockCount,
                        stringCount,
                        nameIndex,
                        extraData,
                        controller) ||
        !readBlockU16(bytes, offset, property.flags) ||
        !readBlockU32(bytes, offset, property.textureCount) ||
        property.textureCount != kStandardTextureSlotCount)
    {
        return false;
    }

    for (std::size_t slotIndex = 0; slotIndex < property.standardSlots.size();
         ++slotIndex)
    {
        auto& slot = property.standardSlots[slotIndex];
        if (!readBlockU8(bytes, offset, slot.presence))
        {
            return false;
        }
        if (slot.presence != 1)
        {
            continue;
        }
        if (!parseTexDesc(bytes, offset, blockCount, slot.descriptor.emplace()))
        {
            return false;
        }
        if (slotIndex == kBumpTextureSlot)
        {
            auto& bump = slot.bump.emplace();
            if (!readBlockF32(bytes, offset, bump.lumaScale) ||
                !readBlockF32(bytes, offset, bump.lumaOffset))
            {
                return false;
            }
            for (auto& component : bump.bumpMatrix)
            {
                if (!readBlockF32(bytes, offset, component))
                {
                    return false;
                }
            }
        }
        else if (slotIndex == kParallaxTextureSlot &&
                 !readBlockF32(bytes, offset, slot.parallaxOffset.emplace()))
        {
            return false;
        }
    }

    if (!readBlockU32(bytes, offset, property.shaderTextureCount) ||
        !canReadArray(bytes, offset, property.shaderTextureCount, 1))
    {
        return false;
    }
    property.shaderTextures.reserve(property.shaderTextureCount);
    for (std::uint32_t index = 0; index < property.shaderTextureCount; ++index)
    {
        NifShaderTextureInventory shader{};
        if (!readBlockU8(bytes, offset, shader.hasMap))
        {
            return false;
        }
        if (shader.hasMap == 1 &&
            (!parseTexDesc(bytes, offset, blockCount, shader.descriptor.emplace()) ||
             !readBlockU32(bytes, offset, shader.mapId.emplace())))
        {
            return false;
        }
        property.shaderTextures.push_back(std::move(shader));
    }
    return offset == bytes.size();
}

bool parseSourceTexture(std::span<const std::byte> bytes,
                        std::uint32_t              blockIndex,
                        std::size_t                blockCount,
                        std::size_t                stringCount,
                        NifSourceTextureInventory& source)
{
    std::size_t                offset    = 0;
    std::uint32_t              nameIndex = 0;
    std::vector<std::uint32_t> extraData;
    std::uint32_t              controller = 0;
    source.blockIndex                     = blockIndex;
    return parseObjectNet(bytes,
                          offset,
                          blockCount,
                          stringCount,
                          nameIndex,
                          extraData,
                          controller) &&
           readBlockU8(bytes, offset, source.useExternal) &&
           readBlockU32(bytes, offset, source.fileNameIndex) &&
           isValidStringIndex(source.fileNameIndex, stringCount) &&
           readReference(bytes, offset, blockCount, source.pixelData) &&
           readBlockU32(bytes, offset, source.pixelLayout) &&
           readBlockU32(bytes, offset, source.useMipmaps) &&
           readBlockU32(bytes, offset, source.alphaFormat) &&
           readBlockU8(bytes, offset, source.isStatic) &&
           readBlockU8(bytes, offset, source.directRender) &&
           readBlockU8(bytes, offset, source.persistRenderData) && offset == bytes.size();
}

bool parseTriShapeData(std::span<const std::byte> bytes,
                       std::uint32_t              blockIndex,
                       std::size_t                blockCount,
                       NifTriShapeDataInventory&  data)
{
    std::size_t offset = 0;
    data.blockIndex    = blockIndex;

    std::uint32_t groupId = 0;
    if (!readBlockU32(bytes, offset, groupId) ||
        !readBlockU16(bytes, offset, data.vertexCount) ||
        !readBlockU8(bytes, offset, data.keepFlags) ||
        !readBlockU8(bytes, offset, data.compressFlags) ||
        !readBlockU8(bytes, offset, data.hasVertices) ||
        (data.hasVertices != 0 &&
         !readVector3Array(bytes, offset, data.vertexCount, data.vertexPositions)) ||
        !readBlockU16(bytes, offset, data.dataFlags) ||
        !readBlockU8(bytes, offset, data.hasNormals) ||
        (data.hasNormals != 0 &&
         !readVector3Array(bytes, offset, data.vertexCount, data.normalVectors)))
    {
        return false;
    }
    data.groupId = std::bit_cast<std::int32_t>(groupId);

    if (data.hasNormals != 0 && (data.dataFlags & 0x1000U) != 0 &&
        (!skipBlockArray(bytes, offset, data.vertexCount, 12) ||
         !skipBlockArray(bytes, offset, data.vertexCount, 12)))
    {
        return false;
    }

    for (auto& value : data.boundCenter)
    {
        if (!readBlockF32(bytes, offset, value))
        {
            return false;
        }
    }
    if (!readBlockF32(bytes, offset, data.boundRadius) ||
        !readBlockU8(bytes, offset, data.hasVertexColors) ||
        (data.hasVertexColors != 0 &&
         !skipBlockArray(bytes, offset, data.vertexCount, 16)))
    {
        return false;
    }

    const auto uvSetCount = static_cast<std::uint32_t>(data.dataFlags & 0x003FU);
    if (!skipBlockArray(bytes,
                        offset,
                        static_cast<std::uint32_t>(data.vertexCount) * uvSetCount,
                        8) ||
        !readBlockU16(bytes, offset, data.consistencyFlags) ||
        !readReference(bytes, offset, blockCount, data.additionalData) ||
        !readBlockU16(bytes, offset, data.triangleCount) ||
        !readBlockU32(bytes, offset, data.trianglePointCount) ||
        !readBlockU8(bytes, offset, data.hasTriangles) ||
        (data.hasTriangles != 0 &&
         !readTriangleArray(bytes, offset, data.triangleCount, data.triangles)) ||
        !readBlockU16(bytes, offset, data.matchGroupCount) ||
        !canReadArray(bytes, offset, data.matchGroupCount, 2))
    {
        return false;
    }

    data.normalSharingGroups.reserve(data.matchGroupCount);
    for (std::uint16_t index = 0; index < data.matchGroupCount; ++index)
    {
        std::uint16_t selectorCount = 0;
        if (!readBlockU16(bytes, offset, selectorCount) ||
            !canReadArray(bytes, offset, selectorCount, 2))
        {
            return false;
        }
        std::vector<std::uint16_t> group;
        group.reserve(selectorCount);
        for (std::uint16_t selectorIndex = 0; selectorIndex < selectorCount;
             ++selectorIndex)
        {
            std::uint16_t selector = 0;
            if (!readBlockU16(bytes, offset, selector))
            {
                return false;
            }
            group.push_back(selector);
        }
        data.normalSharingGroups.push_back(std::move(group));
    }
    return offset == bytes.size();
}

std::expected<std::vector<std::byte>, NifDocumentError>
readSizedBytes(std::span<const std::byte> bytes, std::size_t& offset)
{
    if (!canRead(bytes, offset, 4))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    const auto size = static_cast<std::size_t>(readU32Be(bytes, offset));
    offset += 4;
    if (!canRead(bytes, offset, size))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    std::vector<std::byte> result(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
    offset += size;
    return result;
}

} // namespace

std::string_view NifDocumentErrorMessage(NifDocumentError error)
{
    switch (error)
    {
        case NifDocumentError::Truncated:
            return "The NIF file is truncated.";
        case NifDocumentError::InvalidSignature:
            return "The file does not have a valid Gamebryo NIF signature.";
        case NifDocumentError::UnsupportedVersion:
            return "The NIF file version is not supported.";
        case NifDocumentError::UnsupportedEndian:
            return "Only the observed big endian NIF layout is supported.";
        case NifDocumentError::UnsupportedUserVersion:
            return "The NIF user version is not supported.";
        case NifDocumentError::InvalidLayout:
            return "The NIF header, block table, or footer is inconsistent.";
    }
    return "The NIF file is invalid.";
}

std::expected<NifDocument, NifDocumentError>
ParseNifDocument(std::span<const std::byte> bytes)
{
    constexpr std::string_view kPrefix = "Gamebryo File Format, Version ";
    if (bytes.size() < kPrefix.size())
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    if (!std::ranges::equal(bytes.first(kPrefix.size()),
                            std::as_bytes(std::span(kPrefix.data(), kPrefix.size()))))
    {
        return std::unexpected(NifDocumentError::InvalidSignature);
    }
    if (bytes.size() < kHeader.size())
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    if (!std::ranges::equal(bytes.first(kHeader.size()),
                            std::as_bytes(std::span(kHeader.data(), kHeader.size()))))
    {
        return std::unexpected(NifDocumentError::UnsupportedVersion);
    }

    std::size_t offset = kHeader.size();
    if (!canRead(bytes, offset, 13))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }

    NifDocument document{};
    document.version = readU32Le(bytes, offset);
    offset += 4;
    if (document.version != kVersion)
    {
        return std::unexpected(NifDocumentError::UnsupportedVersion);
    }
    document.endian = std::to_integer<std::uint8_t>(bytes[offset]);
    ++offset;
    if (document.endian != 0)
    {
        return std::unexpected(NifDocumentError::UnsupportedEndian);
    }
    document.userVersion = readU32Le(bytes, offset);
    offset += 4;
    if (document.userVersion != 0)
    {
        return std::unexpected(NifDocumentError::UnsupportedUserVersion);
    }
    const auto blockCount = readU32Le(bytes, offset);
    offset += 4;

    // NifTools nif.xml defines this mixed endian header: version, user version,
    // and block count are little endian. Later numeric fields follow Endian Type.
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1963-L1982
    if (!canRead(bytes, offset, 2))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    const auto blockTypeCount = readU16Be(bytes, offset);
    offset += 2;
    if (!canReadArray(bytes, offset, blockTypeCount, 4))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    document.blockTypes.reserve(blockTypeCount);
    for (std::uint16_t index = 0; index < blockTypeCount; ++index)
    {
        auto blockType = readSizedBytes(bytes, offset);
        if (!blockType)
        {
            return std::unexpected(blockType.error());
        }
        document.blockTypes.push_back(std::move(*blockType));
    }

    if (!canReadArray(bytes, offset, blockCount, 2))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    document.blocks.resize(blockCount);
    for (auto& block : document.blocks)
    {
        block.typeIndex = readU16Be(bytes, offset);
        offset += 2;
        if (block.typeIndex >= document.blockTypes.size())
        {
            return std::unexpected(NifDocumentError::InvalidLayout);
        }
    }
    if (!canReadArray(bytes, offset, blockCount, 4))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    for (auto& block : document.blocks)
    {
        block.size = readU32Be(bytes, offset);
        offset += 4;
    }

    if (!canRead(bytes, offset, 8))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    const auto stringCount = readU32Be(bytes, offset);
    offset += 4;
    document.maxStringLength = readU32Be(bytes, offset);
    offset += 4;
    if (!canReadArray(bytes, offset, stringCount, 4))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    document.strings.reserve(stringCount);
    for (std::uint32_t index = 0; index < stringCount; ++index)
    {
        auto value = readSizedBytes(bytes, offset);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (value->size() > document.maxStringLength)
        {
            return std::unexpected(NifDocumentError::InvalidLayout);
        }
        document.strings.push_back(std::move(*value));
    }

    if (!canRead(bytes, offset, 4))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    const auto groupCount = readU32Be(bytes, offset);
    offset += 4;
    if (!canReadArray(bytes, offset, groupCount, 4))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    document.groups.reserve(groupCount);
    for (std::uint32_t index = 0; index < groupCount; ++index)
    {
        document.groups.push_back(readU32Be(bytes, offset));
        offset += 4;
    }
    document.headerSize = offset;

    for (auto& block : document.blocks)
    {
        block.offset = offset;
        if (!canRead(bytes, offset, block.size))
        {
            return std::unexpected(NifDocumentError::Truncated);
        }
        offset += block.size;
    }

    // The selected object layouts are the standard 20.3.0.9 inheritance chains.
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3359-L3367
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3440-L3494
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3840-L3869
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3876-L3925
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L4384-L4389
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L5278-L5280
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L5282-L5289
    for (std::uint32_t blockIndex = 0; blockIndex < document.blocks.size(); ++blockIndex)
    {
        const auto& block     = document.blocks[blockIndex];
        const auto& blockType = document.blockTypes[block.typeIndex];
        const auto  payload   = bytes.subspan(block.offset, block.size);
        if (equals(blockType, "NiNode"))
        {
            NifNode node{};
            if (!parseNode(payload,
                           blockIndex,
                           document.blocks.size(),
                           document.strings.size(),
                           node))
            {
                return std::unexpected(NifDocumentError::InvalidLayout);
            }
            document.nodes.push_back(std::move(node));
        }
        else if (equals(blockType, "NiTriShape"))
        {
            NifTriShape shape{};
            if (!parseTriShape(payload,
                               blockIndex,
                               document.blocks.size(),
                               document.strings.size(),
                               shape))
            {
                return std::unexpected(NifDocumentError::InvalidLayout);
            }
            document.triShapes.push_back(std::move(shape));
        }
        else if (equals(blockType, "NiTriShapeData"))
        {
            NifTriShapeDataInventory data{};
            if (!parseTriShapeData(payload, blockIndex, document.blocks.size(), data))
            {
                return std::unexpected(NifDocumentError::InvalidLayout);
            }
            document.triShapeData.push_back(std::move(data));
        }
        else if (equals(blockType, "NiMaterialProperty"))
        {
            NifMaterialPropertyInventory material{};
            if (!parseMaterialProperty(payload,
                                       blockIndex,
                                       document.blocks.size(),
                                       document.strings.size(),
                                       material))
            {
                return std::unexpected(NifDocumentError::InvalidLayout);
            }
            document.materialProperties.push_back(std::move(material));
        }
        else if (equals(blockType, "NiTexturingProperty"))
        {
            NifTexturingPropertyInventory property{};
            if (!parseTexturingProperty(payload,
                                        blockIndex,
                                        document.blocks.size(),
                                        document.strings.size(),
                                        property))
            {
                return std::unexpected(NifDocumentError::InvalidLayout);
            }
            document.texturingProperties.push_back(std::move(property));
        }
        else if (equals(blockType, "NiSourceTexture"))
        {
            NifSourceTextureInventory source{};
            if (!parseSourceTexture(payload,
                                    blockIndex,
                                    document.blocks.size(),
                                    document.strings.size(),
                                    source))
            {
                return std::unexpected(NifDocumentError::InvalidLayout);
            }
            document.sourceTextures.push_back(std::move(source));
        }
    }

    if (!canRead(bytes, offset, 4))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    const auto rootCount = readU32Be(bytes, offset);
    offset += 4;
    if (!canReadArray(bytes, offset, rootCount, 4))
    {
        return std::unexpected(NifDocumentError::Truncated);
    }
    document.roots.reserve(rootCount);
    for (std::uint32_t index = 0; index < rootCount; ++index)
    {
        const auto root = readU32Be(bytes, offset);
        offset += 4;
        if (root != std::numeric_limits<std::uint32_t>::max() && root >= document.blocks.size())
        {
            return std::unexpected(NifDocumentError::InvalidLayout);
        }
        document.roots.push_back(root);
    }
    if (offset != bytes.size())
    {
        return std::unexpected(NifDocumentError::InvalidLayout);
    }
    return document;
}

std::expected<NifDocument, std::string>
LoadNifDocument(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        return std::unexpected("Could not open NIF file for reading.");
    }

    const auto end = input.tellg();
    if (end < 0)
    {
        return std::unexpected("Could not read NIF file size.");
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return std::unexpected("NIF file exceeds the supported size range.");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
    {
        return std::unexpected("Could not read NIF file data.");
    }
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
    {
        return std::unexpected("NIF file ended before its reported size.");
    }

    auto document = ParseNifDocument(bytes);
    if (!document)
    {
        return std::unexpected(std::string(NifDocumentErrorMessage(document.error())));
    }
    return std::move(*document);
}

} // namespace rerevved::tools
