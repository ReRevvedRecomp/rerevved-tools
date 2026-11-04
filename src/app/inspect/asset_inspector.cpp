#include "inspect/asset_inspector.h"
#include "inspect/asset_inspector_format.h"
#include "inspect/inspector_navigation_ui.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace rerevved::tools
{
namespace
{

void drawFields(std::span<const std::string> fields)
{
    for (const auto& field : fields)
    {
        ui::Field(field);
    }
}

} // namespace

void DrawDdsInspector(const DdsDocument& document)
{
    ImGui::PushID("dds-metadata");
    ui::Heading("DDS summary");
    ui::FieldF("Dimensions: %u x %u", document.metadata.width, document.metadata.height);
    ui::FieldF("Decoded RGBA8 bytes: %zu", document.rgba8.size());
    ui::FieldF("Payload layout: %s",
               document.payloadLayout == DdsPayloadLayout::XboxTiledBigEndian
                   ? "Xbox tiled big endian"
                   : "Linear little endian");
    ui::FieldF("Decoded levels: %zu", std::max<std::size_t>(document.mipLevels.size(), 1));
    std::vector<std::string> details;
    std::vector<std::string> raw;
    for (const auto& field : FormatDdsInspection(document))
    {
        if (field.starts_with("Reserved ") || field.starts_with("Caps reserved:") || field.starts_with("FourCC bytes:"))
        {
            raw.push_back(field);
        }
        else
        {
            details.push_back(field);
        }
    }
    if (ui::Section("Details"))
    {
        drawFields(details);
    }
    if (ui::Section("Raw Reserved Fields"))
    {
        drawFields(raw);
    }
    ImGui::PopID();
}

void DrawMp3Inspector(const Mp3Document& document)
{
    ImGui::PushID("mp3-metadata");
    ui::Heading("MP3 decoded summary");
    const auto fields = FormatMp3Inspection(document);
    drawFields(fields);
    ImGui::PopID();
}

void DrawMapInspector(const MapDocument& document, InspectorNavigationState& state)
{
    ImGui::PushID("map-envelope");
    ui::Heading("MAP envelope");
    const auto fields = FormatMapInspection(document);
    drawFields(std::span(fields).first(2));
    ui::Caption(fields[2]);
    if (ui::Section("Raw File-Order Bytes"))
    {
        const auto rows = FormatMapByteRows(document);
        if (const auto selected = DrawInspectorNavigation(rows, state, "File offset"); selected && *selected < rows.size())
        {
            ui::Divider();
            ui::Field(rows[*selected]);
        }
    }
    ImGui::PopID();
}

} // namespace rerevved::tools
