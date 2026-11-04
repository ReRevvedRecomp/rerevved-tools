#include "inspect/nif_inspector.h"
#include "inspect/inspector_navigation_ui.h"
#include "inspect/nif_inspector_format.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{
namespace
{

std::string formatString(const NifDocument& document, std::uint32_t index)
{
    if (index == std::numeric_limits<std::uint32_t>::max())
    {
        return "<none>";
    }
    return FormatNifStringBytes(document.strings[index]);
}

std::string formatFloat(float value)
{
    return FormatNifInspectorFloat(value);
}

void drawWrapped(std::string_view text)
{
    ui::Field(text);
}

void drawRecordHeading(std::string_view text)
{
    ui::Heading(text);
}

void drawReference(std::string_view label, std::uint32_t value)
{
    std::string text(label);
    text += ": ";
    if (value == std::numeric_limits<std::uint32_t>::max())
    {
        text += "none";
    }
    else
    {
        text += std::to_string(value);
    }
    drawWrapped(text);
}

template <typename DrawRow>
void drawClippedRows(const char* childId, std::size_t count, DrawRow&& drawRow)
{
    if (count >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        ui::TonedText(ui::Tone::Warn, "The retained array is too large for indexed presentation.");
        return;
    }

    const auto height = std::max(ImGui::GetTextLineHeightWithSpacing() * 2.0F, ImGui::GetContentRegionAvail().y * 0.4F);
    ImGui::BeginChild(childId,
                      ImVec2(0.0F, height),
                      ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(count));
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            drawRow(static_cast<std::size_t>(row));
        }
    }
    clipper.End();
    ImGui::EndChild();
}

void drawReferences(std::string_view label, std::span<const std::uint32_t> values)
{
    if (ImGui::TreeNode(label.data(), "%.*s (%zu)", static_cast<int>(label.size()), label.data(), values.size()))
    {
        drawClippedRows("reference-rows", values.size(), [&](std::size_t index)
                        {
                            const auto text = std::to_string(index) + ": " +
                                              (values[index] == std::numeric_limits<std::uint32_t>::max() ? std::string("none") : std::to_string(values[index]));
                            ImGui::TextUnformatted(text.c_str());
                        });
        ImGui::TreePop();
    }
}

void drawAvObject(const NifDocument& document, const NifAvObject& object)
{
    drawWrapped("Name: " + formatString(document, object.nameIndex));
    ui::FieldF("Flags: 0x%04X", static_cast<unsigned int>(object.flags));
    drawWrapped("Translation: " + formatFloat(object.translation[0]) + ", " +
                formatFloat(object.translation[1]) + ", " + formatFloat(object.translation[2]));
    drawWrapped("Rotation: " + formatFloat(object.rotation[0]) + ", " +
                formatFloat(object.rotation[1]) + ", " + formatFloat(object.rotation[2]));
    drawWrapped("          " + formatFloat(object.rotation[3]) + ", " +
                formatFloat(object.rotation[4]) + ", " + formatFloat(object.rotation[5]));
    drawWrapped("          " + formatFloat(object.rotation[6]) + ", " +
                formatFloat(object.rotation[7]) + ", " + formatFloat(object.rotation[8]));
    drawWrapped("Scale: " + formatFloat(object.scale));
    drawReferences("Extra data", object.extraData);
    drawReference("Controller", object.controller);
    drawReferences("Properties", object.properties);
    drawReference("Collision object", object.collisionObject);
}

void drawTriShapeData(const NifTriShapeDataInventory& data)
{
    ui::FieldF("Group ID: %d", static_cast<int>(data.groupId));
    ui::FieldF("Vertices: %u", static_cast<unsigned int>(data.vertexCount));
    ui::FieldF("Keep flags: 0x%02X", static_cast<unsigned int>(data.keepFlags));
    ui::FieldF("Compress flags: 0x%02X", static_cast<unsigned int>(data.compressFlags));
    ui::FieldF("Has vertices: 0x%02X", static_cast<unsigned int>(data.hasVertices));
    ui::FieldF("Data flags: 0x%04X", static_cast<unsigned int>(data.dataFlags));
    ui::FieldF("UV sets: %u", static_cast<unsigned int>(data.dataFlags & 0x003FU));
    ui::FieldF("Has normals: 0x%02X", static_cast<unsigned int>(data.hasNormals));
    ui::FieldF("Has tangents: %s",
               data.hasNormals != 0 && (data.dataFlags & 0x1000U) != 0 ? "yes" : "no");
    drawWrapped("Bound center: " + formatFloat(data.boundCenter[0]) + ", " +
                formatFloat(data.boundCenter[1]) + ", " + formatFloat(data.boundCenter[2]));
    drawWrapped("Bound radius: " + formatFloat(data.boundRadius));
    ui::FieldF("Has vertex colors: 0x%02X",
               static_cast<unsigned int>(data.hasVertexColors));
    ui::FieldF("Consistency flags: 0x%04X",
               static_cast<unsigned int>(data.consistencyFlags));
    drawReference("Additional data", data.additionalData);
    ui::FieldF("Triangles: %u", static_cast<unsigned int>(data.triangleCount));
    ui::FieldF("Triangle points: %u", static_cast<unsigned int>(data.trianglePointCount));
    ui::FieldF("Has triangles: 0x%02X", static_cast<unsigned int>(data.hasTriangles));
    ui::FieldF("Match groups: %u", static_cast<unsigned int>(data.matchGroupCount));

    if (!ImGui::TreeNode("Raw Arrays"))
    {
        return;
    }
    if (ImGui::TreeNode("vertex-positions", "Vertex Positions (%zu)", data.vertexPositions.size()))
    {
        drawClippedRows("vertex-position-rows", data.vertexPositions.size(), [&](std::size_t index)
                        {
                            const auto& vertex = data.vertexPositions[index];
                            const auto  text   = std::to_string(index) + ": " +
                                                 formatFloat(vertex[0]) + ", " +
                                                 formatFloat(vertex[1]) + ", " +
                                                 formatFloat(vertex[2]);
                            ImGui::TextUnformatted(text.data(), text.data() + text.size());
                        });
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("normal-vectors", "Normal Vectors (%zu)", data.normalVectors.size()))
    {
        drawClippedRows("normal-vector-rows", data.normalVectors.size(), [&](std::size_t index)
                        {
                            const auto& normal = data.normalVectors[index];
                            const auto  text   = std::to_string(index) + ": " +
                                                 formatFloat(normal[0]) + ", " +
                                                 formatFloat(normal[1]) + ", " +
                                                 formatFloat(normal[2]);
                            ImGui::TextUnformatted(text.data(), text.data() + text.size());
                        });
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("triangles", "Triangles (%zu)", data.triangles.size()))
    {
        drawClippedRows("triangle-rows", data.triangles.size(), [&](std::size_t index)
                        {
                            const auto& triangle = data.triangles[index];
                            const auto  text     = std::to_string(index) + ": " +
                                                   std::to_string(triangle[0]) + ", " +
                                                   std::to_string(triangle[1]) + ", " +
                                                   std::to_string(triangle[2]);
                            ImGui::TextUnformatted(text.data(), text.data() + text.size());
                        });
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("normal-sharing-groups",
                        "Normal sharing groups (%zu)",
                        data.normalSharingGroups.size()))
    {
        drawClippedRows("normal-sharing-group-rows",
                        data.normalSharingGroups.size(),
                        [&](std::size_t groupIndex)
                        {
                            const auto& group = data.normalSharingGroups[groupIndex];
                            std::string text  = std::to_string(groupIndex) + ": " +
                                                std::to_string(group.size()) + " indices";
                            for (const auto vertex : group)
                            {
                                text += ", " + std::to_string(vertex);
                            }
                            ImGui::TextUnformatted(text.data(), text.data() + text.size());
                        });
        ImGui::TreePop();
    }
    ImGui::TreePop();
}

template <typename DrawDetails>
void drawInventorySection(std::string_view                title,
                          const std::vector<std::string>& labels,
                          InspectorNavigationState&       state,
                          const char*                     identityLabel,
                          std::size_t                     indexBase,
                          DrawDetails&&                   drawDetails)
{
    ui::VerticalSpace(6.0F);
    ui::Heading(title);
    if (labels.empty())
    {
        ui::Muted("None retained.");
        return;
    }

    const auto selected = DrawInspectorNavigation(labels, state, identityLabel, indexBase);
    if (selected && *selected < labels.size())
    {
        ui::Divider();
        ImGui::PushID(std::to_string(*selected).c_str());
        drawDetails(*selected);
        ImGui::PopID();
    }
}

std::vector<std::string> makeBlockTypeLabels(const NifDocument& document)
{
    std::vector<std::string> labels;
    labels.reserve(document.blockTypes.size());
    for (std::size_t index = 0; index < document.blockTypes.size(); ++index)
    {
        labels.push_back("Type " + std::to_string(index) + ": " +
                         FormatNifStringBytes(document.blockTypes[index]));
    }
    return labels;
}

std::vector<std::string> makeBlockLabels(const NifDocument& document)
{
    std::vector<std::string> labels;
    labels.reserve(document.blocks.size());
    for (std::size_t index = 0; index < document.blocks.size(); ++index)
    {
        const auto& block = document.blocks[index];
        const auto  type  = block.typeIndex < document.blockTypes.size()
                                ? FormatNifStringBytes(document.blockTypes[block.typeIndex])
                                : "<invalid type index>";
        labels.push_back("Record " + std::to_string(index) + " / Block " +
                         std::to_string(index) + ": " + type);
    }
    return labels;
}

std::vector<std::string> makeStringLabels(const NifDocument& document)
{
    std::vector<std::string> labels;
    labels.reserve(document.strings.size());
    for (std::size_t index = 0; index < document.strings.size(); ++index)
    {
        labels.push_back("String " + std::to_string(index) + ": " +
                         FormatNifStringBytes(document.strings[index]));
    }
    return labels;
}

template <typename Value>
std::vector<std::string> makeValueLabels(std::span<const Value> values,
                                         std::string_view       prefix)
{
    std::vector<std::string> labels;
    labels.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        labels.push_back(std::string(prefix) + " " + std::to_string(index) + ": " +
                         std::to_string(values[index]));
    }
    return labels;
}

std::vector<std::string> makeNodeLabels(const NifDocument& document)
{
    std::vector<std::string> labels;
    labels.reserve(document.nodes.size());
    for (std::size_t index = 0; index < document.nodes.size(); ++index)
    {
        const auto& node = document.nodes[index];
        labels.push_back("Record " + std::to_string(index) + " / Block " +
                         std::to_string(node.blockIndex) + ": NiNode / Name " +
                         formatString(document, node.object.nameIndex));
    }
    return labels;
}

std::vector<std::string> makeTriangleShapeLabels(const NifDocument& document)
{
    std::vector<std::string> labels;
    labels.reserve(document.triShapes.size());
    for (std::size_t index = 0; index < document.triShapes.size(); ++index)
    {
        const auto& shape = document.triShapes[index];
        labels.push_back("Record " + std::to_string(index) + " / Block " +
                         std::to_string(shape.blockIndex) + ": NiTriShape / Name " +
                         formatString(document, shape.object.nameIndex));
    }
    return labels;
}

std::vector<std::string> makeTriangleDataLabels(const NifDocument& document)
{
    std::vector<std::string> labels;
    labels.reserve(document.triShapeData.size());
    for (std::size_t index = 0; index < document.triShapeData.size(); ++index)
    {
        const auto& data = document.triShapeData[index];
        labels.push_back("Record " + std::to_string(index) + " / Block " +
                         std::to_string(data.blockIndex) + ": NiTriShapeData");
    }
    return labels;
}

std::vector<std::string> makeMaterialLabels(const NifDocument& document)
{
    std::vector<std::string> labels;
    labels.reserve(document.materialProperties.size());
    for (std::size_t index = 0; index < document.materialProperties.size(); ++index)
    {
        const auto& material = document.materialProperties[index];
        labels.push_back("Record " + std::to_string(index) + " / Block " +
                         std::to_string(material.blockIndex) + ": NiMaterialProperty");
    }
    return labels;
}

std::vector<std::string> makeTexturingLabels(const NifDocument& document)
{
    std::vector<std::string> labels;
    labels.reserve(document.texturingProperties.size());
    for (std::size_t index = 0; index < document.texturingProperties.size(); ++index)
    {
        const auto& property = document.texturingProperties[index];
        labels.push_back("Record " + std::to_string(index) + " / Block " +
                         std::to_string(property.blockIndex) + ": NiTexturingProperty");
    }
    return labels;
}

std::vector<std::string> makeSourceTextureLabels(const NifDocument& document)
{
    std::vector<std::string> labels;
    labels.reserve(document.sourceTextures.size());
    for (std::size_t index = 0; index < document.sourceTextures.size(); ++index)
    {
        const auto& source   = document.sourceTextures[index];
        const auto  fileName = source.fileNameIndex < document.strings.size()
                                   ? FormatNifStringBytes(document.strings[source.fileNameIndex])
                                   : "<none>";
        labels.push_back("Record " + std::to_string(index) + " / Block " +
                         std::to_string(source.blockIndex) + ": NiSourceTexture / File " +
                         fileName);
    }
    return labels;
}

} // namespace

void DrawNifInspector(const NifDocument* document,
                      std::string_view   error,
                      NifInspectorState& state)
{
    ui::Heading("NIF container");
    if (!document)
    {
        ui::TonedText(ui::Tone::Error, error.empty() ? std::string_view("The NIF container could not be inspected.") : error);
        return;
    }

    ui::Caption(std::to_string(document->blocks.size()) + " blocks  -  " + std::to_string(document->strings.size()) + " strings  -  " + std::to_string(document->groups.size()) + " groups  -  " + std::to_string(document->roots.size()) + " roots");
    if (ui::Section("Container Details"))
    {
        ui::FieldF("Version: %u.%u.%u.%u",
                   static_cast<unsigned int>((document->version >> 24U) & 0xFFU),
                   static_cast<unsigned int>((document->version >> 16U) & 0xFFU),
                   static_cast<unsigned int>((document->version >> 8U) & 0xFFU),
                   static_cast<unsigned int>(document->version & 0xFFU));
        drawWrapped("Endian: big");
        ui::FieldF("Endian raw: 0x%02X", static_cast<unsigned int>(document->endian));
        ui::FieldF("User version: %u", static_cast<unsigned int>(document->userVersion));
        ui::FieldF("Header size: %zu bytes", document->headerSize);
        ui::FieldF("Maximum string length: %u",
                   static_cast<unsigned int>(document->maxStringLength));
    }

    constexpr std::array sectionNames{
        "Block types",
        "Blocks",
        "Strings",
        "Groups",
        "Roots",
        "Nodes",
        "Triangle Shapes",
        "Triangle Data",
        "Material Properties",
        "Texturing Properties",
        "Source Textures",
    };
    auto sectionIndex = static_cast<std::size_t>(state.currentSection);
    if (sectionIndex >= sectionNames.size())
    {
        state.currentSection = NifInspectorSection::BlockTypes;
        sectionIndex         = 0;
    }
    ui::VerticalSpace(4.0F);
    ImGui::AlignTextToFramePadding();
    ui::Muted("Inventory");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(1.0F, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##inventory", sectionNames[sectionIndex]))
    {
        for (std::size_t index = 0; index < sectionNames.size(); ++index)
        {
            const bool selected = index == sectionIndex;
            if (ImGui::Selectable(sectionNames[index], selected))
            {
                state.currentSection = static_cast<NifInspectorSection>(index);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    switch (state.currentSection)
    {
        case NifInspectorSection::BlockTypes:
        {
            const auto               labels = makeBlockTypeLabels(*document);
            std::vector<std::size_t> counts(document->blockTypes.size());
            for (const auto& block : document->blocks)
            {
                if (block.typeIndex < counts.size())
                {
                    ++counts[block.typeIndex];
                }
            }
            drawInventorySection("Retained Block Types",
                                 labels,
                                 state.blockTypes,
                                 "nif-block-types",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     ui::FieldF("Original index: %zu", index);
                                     drawWrapped("Type bytes: " + FormatNifStringBytes(document->blockTypes[index]));
                                     ui::FieldF("Block count: %zu", counts[index]);
                                 });
            break;
        }
        case NifInspectorSection::Blocks:
        {
            const auto labels = makeBlockLabels(*document);
            drawInventorySection("Blocks",
                                 labels,
                                 state.blocks,
                                 "nif-blocks",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     const auto& block = document->blocks[index];
                                     ui::FieldF("Original index: %zu", index);
                                     ui::FieldF("Type index: %u",
                                                static_cast<unsigned int>(block.typeIndex));
                                     if (block.typeIndex < document->blockTypes.size())
                                     {
                                         drawWrapped("Type: " + FormatNifStringBytes(document->blockTypes[block.typeIndex]));
                                     }
                                     else
                                     {
                                         drawWrapped("Type: <invalid type index>");
                                     }
                                     ui::FieldF("Size: %u bytes",
                                                static_cast<unsigned int>(block.size));
                                     ui::FieldF("Offset: %zu", block.offset);
                                 });
            break;
        }
        case NifInspectorSection::Strings:
        {
            const auto labels = makeStringLabels(*document);
            drawInventorySection("Strings",
                                 labels,
                                 state.strings,
                                 "nif-strings",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     ui::FieldF("Original index: %zu", index);
                                     const auto escaped = FormatNifStringBytes(document->strings[index]);
                                     drawWrapped("Escaped bytes: " + (escaped.empty() ? std::string("<empty>") : escaped));
                                 });
            break;
        }
        case NifInspectorSection::Groups:
        {
            const auto labels = makeValueLabels<std::uint32_t>(document->groups, "Group");
            drawInventorySection("Groups",
                                 labels,
                                 state.groups,
                                 "nif-groups",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     ui::FieldF("Original index: %zu", index);
                                     ui::FieldF("Raw group value: %u",
                                                static_cast<unsigned int>(document->groups[index]));
                                 });
            break;
        }
        case NifInspectorSection::Roots:
        {
            const auto labels = makeValueLabels<std::uint32_t>(document->roots, "Root");
            drawInventorySection("Roots",
                                 labels,
                                 state.roots,
                                 "nif-roots",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     ui::FieldF("Original index: %zu", index);
                                     ui::FieldF("Block index: %u",
                                                static_cast<unsigned int>(document->roots[index]));
                                 });
            break;
        }
        case NifInspectorSection::Nodes:
        {
            const auto labels = makeNodeLabels(*document);
            drawInventorySection("Nodes",
                                 labels,
                                 state.nodes,
                                 "nif-nodes",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     const auto& node = document->nodes[index];
                                     ui::FieldF("Original index: %zu", index);
                                     ui::FieldF("Block %u: NiNode",
                                                static_cast<unsigned int>(node.blockIndex));
                                     drawAvObject(*document, node.object);
                                     drawReferences("Children", node.children);
                                     drawReferences("Effects", node.effects);
                                 });
            break;
        }
        case NifInspectorSection::TriangleShapes:
        {
            const auto labels = makeTriangleShapeLabels(*document);
            drawInventorySection("Triangle Shapes",
                                 labels,
                                 state.triangleShapes,
                                 "nif-triangle-shapes",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     const auto& shape = document->triShapes[index];
                                     ui::FieldF("Original index: %zu", index);
                                     ui::FieldF("Block %u: NiTriShape",
                                                static_cast<unsigned int>(shape.blockIndex));
                                     drawAvObject(*document, shape.object);
                                     drawReference("Data", shape.data);
                                     drawReference("Skin instance", shape.skinInstance);
                                     if (ImGui::TreeNode("raw-materials", "Raw Materials (%zu)", shape.material.nameIndices.size()))
                                     {
                                         drawClippedRows("material-rows", shape.material.nameIndices.size(), [&](std::size_t materialIndex)
                                                         {
                                                             const auto text = std::to_string(materialIndex) + ": " + formatString(*document, shape.material.nameIndices[materialIndex]) + ", extra " + std::to_string(shape.material.extraData[materialIndex]);
                                                             ImGui::TextUnformatted(text.c_str());
                                                         });
                                         ImGui::TreePop();
                                     }
                                     ui::FieldF("Active material: %d",
                                                static_cast<int>(shape.material.activeMaterial));
                                     ui::FieldF("Material needs update: %u",
                                                static_cast<unsigned int>(
                                                    shape.material.materialNeedsUpdate));
                                 });
            break;
        }
        case NifInspectorSection::TriangleData:
        {
            const auto labels = makeTriangleDataLabels(*document);
            drawInventorySection("Triangle Data",
                                 labels,
                                 state.triangleData,
                                 "nif-triangle-data",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     ui::FieldF("Original index: %zu", index);
                                     ui::FieldF("Block %u: NiTriShapeData",
                                                static_cast<unsigned int>(
                                                    document->triShapeData[index].blockIndex));
                                     drawTriShapeData(document->triShapeData[index]);
                                 });
            break;
        }
        case NifInspectorSection::MaterialProperties:
        {
            const auto labels = makeMaterialLabels(*document);
            const auto text   = FormatNifMaterialProperties(document->materialProperties);
            drawInventorySection("Material Properties",
                                 labels,
                                 state.materialProperties,
                                 "nif-material-properties",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     drawRecordHeading(text[index].heading);
                                     for (const auto& field : text[index].fields)
                                     {
                                         drawWrapped(field);
                                     }
                                 });
            break;
        }
        case NifInspectorSection::TexturingProperties:
        {
            const auto labels = makeTexturingLabels(*document);
            const auto text   = FormatNifTexturingProperties(document->texturingProperties);
            drawInventorySection("Texturing Properties",
                                 labels,
                                 state.texturingProperties,
                                 "nif-texturing-properties",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     drawRecordHeading(text[index].heading);
                                     for (const auto& field : text[index].fields)
                                     {
                                         drawWrapped(field);
                                     }
                                 });
            break;
        }
        case NifInspectorSection::SourceTextures:
        {
            const auto labels = makeSourceTextureLabels(*document);
            const auto text   = FormatNifSourceTextures(*document);
            drawInventorySection("Source Textures",
                                 labels,
                                 state.sourceTextures,
                                 "nif-source-textures",
                                 0,
                                 [&](std::size_t index)
                                 {
                                     drawRecordHeading(text[index].heading);
                                     for (const auto& field : text[index].fields)
                                     {
                                         drawWrapped(field);
                                     }
                                 });
            break;
        }
    }
}

} // namespace rerevved::tools
