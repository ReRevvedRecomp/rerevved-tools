#include "inspect/nif_inspector_format.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

namespace rerevved::tools
{
namespace
{

std::string formatColor(std::string_view label, const std::array<float, 3>& color)
{
    return std::string(label) + ": " + FormatNifInspectorFloat(color[0]) + ", " +
           FormatNifInspectorFloat(color[1]) + ", " + FormatNifInspectorFloat(color[2]);
}

std::string formatRawByte(std::string_view label, std::uint8_t value)
{
    char hexadecimal[5]{};
    std::snprintf(hexadecimal, sizeof(hexadecimal), "0x%02X", static_cast<unsigned int>(value));
    return std::string(label) + ": " + std::to_string(value) + " (" + hexadecimal + ")";
}

std::string formatRawU32(std::string_view label, std::uint32_t value)
{
    char hexadecimal[11]{};
    std::snprintf(hexadecimal, sizeof(hexadecimal), "0x%08X", value);
    return std::string(label) + ": " + std::to_string(value) + " (" + hexadecimal + ")";
}

std::string formatRawU16Hex(std::string_view label, std::uint16_t value)
{
    char hexadecimal[7]{};
    std::snprintf(hexadecimal, sizeof(hexadecimal), "0x%04X", static_cast<unsigned int>(value));
    return std::string(label) + ": " + hexadecimal;
}

std::string formatReference(std::string_view label, std::uint32_t value)
{
    if (value == UINT32_MAX)
    {
        return std::string(label) + ": none (0xFFFFFFFF)";
    }
    return formatRawU32(label, value);
}

std::string formatFloatPair(std::string_view label, const std::array<float, 2>& values)
{
    return std::string(label) + ": " + FormatNifInspectorFloat(values[0]) + ", " +
           FormatNifInspectorFloat(values[1]);
}

void appendTexDesc(std::vector<std::string>&  fields,
                   std::string_view           prefix,
                   const NifTexDescInventory& descriptor)
{
    const auto fieldPrefix = std::string(prefix) + " ";
    fields.push_back(formatReference(fieldPrefix + "Source block reference", descriptor.source));
    fields.push_back(formatRawU16Hex(fieldPrefix + "descriptor Flags", descriptor.flags));
    fields.push_back(
        formatRawByte(fieldPrefix + "Has Texture Transform", descriptor.hasTextureTransform));
    const bool hasTransform = descriptor.hasTextureTransform == 1 && descriptor.transform;
    fields.push_back(fieldPrefix + "Texture Transform: " +
                     (hasTransform ? "present" : "absent"));
    if (!hasTransform)
    {
        return;
    }

    fields.push_back(formatFloatPair(fieldPrefix + "Translation X/Y",
                                     descriptor.transform->translation));
    fields.push_back(formatFloatPair(fieldPrefix + "Scale X/Y", descriptor.transform->scale));
    fields.push_back(fieldPrefix + "Rotation: " +
                     FormatNifInspectorFloat(descriptor.transform->rotation));
    fields.push_back(formatRawU32(fieldPrefix + "Transform Method",
                                  descriptor.transform->transformMethod));
    fields.push_back(formatFloatPair(fieldPrefix + "Center X/Y", descriptor.transform->center));
}

} // namespace

std::string FormatNifInspectorFloat(float value)
{
    if (std::isnan(value))
    {
        return "NaN";
    }
    if (std::isinf(value))
    {
        return std::signbit(value) ? "-infinity" : "+infinity";
    }
    if (value == 0.0F)
    {
        return std::signbit(value) ? "-0" : "0";
    }

    std::array<char, 64> buffer{};
    const auto           result = std::to_chars(buffer.data(),
                                                buffer.data() + buffer.size(),
                                                value,
                                                std::chars_format::general,
                                                std::numeric_limits<float>::max_digits10);
    return std::string(buffer.data(), result.ptr);
}

std::string FormatNifStringBytes(std::span<const std::byte> bytes)
{
    std::string result;
    for (const auto value : bytes)
    {
        const auto byte = std::to_integer<unsigned char>(value);
        if (byte >= 0x20 && byte <= 0x7E && byte != '\\')
        {
            result.push_back(static_cast<char>(byte));
            continue;
        }
        if (byte == '\\')
        {
            result += "\\\\";
            continue;
        }
        char escaped[5]{};
        std::snprintf(escaped, sizeof(escaped), "\\x%02X", byte);
        result += escaped;
    }
    return result;
}

std::vector<NifMaterialPropertyText>
FormatNifMaterialProperties(std::span<const NifMaterialPropertyInventory> materials)
{
    std::vector<NifMaterialPropertyText> result;
    result.reserve(materials.size());
    for (const auto& material : materials)
    {
        result.push_back({
            "NiMaterialProperty block " + std::to_string(material.blockIndex),
            {
                formatColor("Ambient color", material.ambientColor),
                formatColor("Diffuse color", material.diffuseColor),
                formatColor("Specular color", material.specularColor),
                formatColor("Emissive color", material.emissiveColor),
                "Glossiness: " + FormatNifInspectorFloat(material.glossiness),
                "Alpha: " + FormatNifInspectorFloat(material.alpha),
            },
        });
    }
    return result;
}

std::vector<NifTexturingPropertyText>
FormatNifTexturingProperties(std::span<const NifTexturingPropertyInventory> properties)
{
    constexpr std::array<std::string_view, 9> kSlotNames{
        "Base", "Dark", "Detail", "Gloss", "Glow", "Bump", "Normal", "Parallax", "Decal 0"
    };

    std::vector<NifTexturingPropertyText> result;
    result.reserve(properties.size());
    for (const auto& property : properties)
    {
        NifTexturingPropertyText text{
            "NiTexturingProperty block " + std::to_string(property.blockIndex),
            {
                formatRawU16Hex("Flags", property.flags),
                "Texture Count: " + std::to_string(property.textureCount),
            },
        };
        for (std::size_t slotIndex = 0; slotIndex < property.standardSlots.size();
             ++slotIndex)
        {
            const auto& slot     = property.standardSlots[slotIndex];
            const auto  slotName = kSlotNames[slotIndex];
            text.fields.push_back(formatRawByte(std::string(slotName) + " presence",
                                                slot.presence));
            const bool hasDescriptor = slot.presence == 1 && slot.descriptor;
            text.fields.push_back(std::string(slotName) + " descriptor: " +
                                  (hasDescriptor ? "present" : "absent"));
            if (hasDescriptor)
            {
                appendTexDesc(text.fields, slotName, *slot.descriptor);
            }
            if (slot.presence == 1 && slot.bump)
            {
                text.fields.push_back("Bump Luma Scale: " +
                                      FormatNifInspectorFloat(slot.bump->lumaScale));
                text.fields.push_back("Bump Luma Offset: " +
                                      FormatNifInspectorFloat(slot.bump->lumaOffset));
                text.fields.push_back(
                    "Bump Matrix: " + FormatNifInspectorFloat(slot.bump->bumpMatrix[0]) +
                    ", " + FormatNifInspectorFloat(slot.bump->bumpMatrix[1]) + ", " +
                    FormatNifInspectorFloat(slot.bump->bumpMatrix[2]) + ", " +
                    FormatNifInspectorFloat(slot.bump->bumpMatrix[3]));
            }
            if (slot.presence == 1 && slot.parallaxOffset)
            {
                text.fields.push_back("Parallax Offset: " +
                                      FormatNifInspectorFloat(*slot.parallaxOffset));
            }
        }
        text.fields.push_back("Shader Texture Count: " +
                              std::to_string(property.shaderTextureCount));
        for (std::size_t shaderIndex = 0; shaderIndex < property.shaderTextures.size();
             ++shaderIndex)
        {
            const auto  prefix = "Shader " + std::to_string(shaderIndex + 1);
            const auto& shader = property.shaderTextures[shaderIndex];
            text.fields.push_back(formatRawByte(prefix + " Has Map", shader.hasMap));
            const bool hasDescriptor = shader.hasMap == 1 && shader.descriptor;
            text.fields.push_back(prefix + " descriptor: " +
                                  (hasDescriptor ? "present" : "absent"));
            if (hasDescriptor)
            {
                appendTexDesc(text.fields, prefix, *shader.descriptor);
            }
            if (shader.hasMap == 1 && shader.mapId)
            {
                text.fields.push_back(formatRawU32(prefix + " Map ID", *shader.mapId));
            }
        }
        result.push_back(std::move(text));
    }
    return result;
}

std::vector<NifSourceTextureText> FormatNifSourceTextures(const NifDocument& document)
{
    std::vector<NifSourceTextureText> result;
    result.reserve(document.sourceTextures.size());
    for (const auto& source : document.sourceTextures)
    {
        const bool           hasFileName = source.fileNameIndex != UINT32_MAX &&
                                           source.fileNameIndex < document.strings.size();
        NifSourceTextureText text{
            "NiSourceTexture block " + std::to_string(source.blockIndex),
            {
                formatRawByte("Use External", source.useExternal),
                formatReference("File Name string index", source.fileNameIndex),
            },
        };
        if (hasFileName)
        {
            const auto escaped = FormatNifStringBytes(document.strings[source.fileNameIndex]);
            text.fields.push_back("File Name bytes: " +
                                  (escaped.empty() ? std::string("<empty>") : escaped));
        }
        text.fields.push_back(formatReference("Pixel Data block reference", source.pixelData));
        text.fields.push_back(formatRawU32("Pixel Layout", source.pixelLayout));
        text.fields.push_back(formatRawU32("Use Mipmaps", source.useMipmaps));
        text.fields.push_back(formatRawU32("Alpha Format", source.alphaFormat));
        text.fields.push_back(formatRawByte("Is Static", source.isStatic));
        text.fields.push_back(formatRawByte("Direct Render", source.directRender));
        text.fields.push_back(formatRawByte("Persist Render Data", source.persistRenderData));
        text.fields.push_back(hasFileName && source.IsSupportedExternalSource()
                                  ? "Supported external source"
                                  : "Unsupported source combination");
        result.push_back(std::move(text));
    }
    return result;
}

} // namespace rerevved::tools
