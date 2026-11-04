#include "map/map_preview.h"
#include "map/map_edit_model.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iomanip>
#include <sstream>
#include <string>

namespace rerevved::tools
{
namespace
{

std::string formatFooter(std::span<const std::byte> footer)
{
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (std::size_t offset = 0; offset < footer.size(); ++offset)
    {
        if (offset != 0)
        {
            output << (offset % 16 == 0 ? '\n' : ' ');
        }
        output << std::setw(2) << std::to_integer<unsigned int>(footer[offset]);
    }
    return output.str();
}

void drawCore(const MapDocument& document)
{
    constexpr float kLegendWidth = 230.0F;
    const auto      region       = ImGui::GetContentRegionAvail();
    const bool      besides      = region.x >= 32.0F * 8.0F + kLegendWidth + 24.0F;
    const float     fitWidth     = besides ? region.x - kLegendWidth - 24.0F : region.x;
    const float     fitHeight    = besides ? region.y - 8.0F : region.y - 150.0F;
    const float     cellSize     = std::clamp(std::floor(std::min(fitWidth, fitHeight) / static_cast<float>(kMapWidth)),
                                              4.0F,
                                              18.0F);
    const float     gridSize     = cellSize * static_cast<float>(kMapWidth);
    const ImVec2    origin       = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRect({ origin.x - 1.0F, origin.y - 1.0F }, { origin.x + gridSize + 1.0F, origin.y + gridSize + 1.0F }, ui::color::Border);
    for (std::size_t row = 0; row < kMapHeight; ++row)
    {
        for (std::size_t column = 0; column < kMapWidth; ++column)
        {
            const auto   offset = row * kMapWidth + column;
            const ImVec2 minimum(origin.x + static_cast<float>(row) * cellSize,
                                 origin.y + static_cast<float>(column) * cellSize);
            DrawMapTile(minimum, cellSize, document.core[offset], true);
        }
    }

    ImGui::InvisibleButton("##map-core", ImVec2(gridSize, gridSize));
    if (ImGui::IsItemHovered())
    {
        const auto mouse = ImGui::GetMousePos();
        if (const auto offset = MapTileAtPoint(mouse.x - origin.x, mouse.y - origin.y, cellSize))
        {
            DrawMapTileTooltip(*offset, document.core[*offset]);
        }
    }
    if (besides)
    {
        ImGui::SameLine(0.0F, 24.0F);
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kLegendWidth);
        DrawMapTileLegend(kLegendWidth);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }
    else
    {
        ui::VerticalSpace(6.0F);
        DrawMapTileLegend();
    }
}

} // namespace

void DrawMapTile(ImVec2 minimum, float cellSize, std::byte value, bool gridLines, bool rawBits)
{
    const auto   color = MapTilePaletteColor(value);
    const auto   bits  = std::to_integer<unsigned int>(value);
    const ImVec2 maximum(minimum.x + cellSize, minimum.y + cellSize);
    auto*        drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(minimum, maximum, IM_COL32(color.red, color.green, color.blue, 255));
    if (gridLines)
    {
        drawList->AddRect(minimum, maximum, IM_COL32(28, 32, 38, 180));
    }
    if (!rawBits)
    {
        return;
    }
    const float inset = cellSize * 0.18F;
    // Raw bit marks are unreadable at minimap scale. The tooltip still reports them.
    const bool showRaw = cellSize >= 8.0F;
    if (showRaw && (bits & 0x10U) != 0)
    {
        drawList->AddRect(ImVec2(minimum.x + inset, minimum.y + inset), ImVec2(maximum.x - inset, maximum.y - inset), IM_COL32(20, 25, 32, 110), 0.0F, 0, std::min(1.0F, cellSize * 0.12F));
    }
    if (showRaw && (bits & 0x20U) != 0)
    {
        drawList->AddLine(ImVec2(minimum.x + inset, maximum.y - inset), ImVec2(maximum.x - inset, minimum.y + inset), IM_COL32(20, 25, 32, 110), std::min(1.0F, cellSize * 0.12F));
    }
    const float marker = std::max(1.0F, cellSize * 0.4F);
    if ((bits & 0x40U) != 0)
    {
        const ImVec2 center(minimum.x + marker * 0.5F, minimum.y + marker * 0.5F);
        drawList->AddCircleFilled(center, marker * 0.5F, IM_COL32(20, 25, 32, 255));
        drawList->AddCircleFilled(center, std::max(0.5F, marker * 0.3F), IM_COL32(255, 255, 255, 255));
    }
    if ((bits & 0x80U) != 0)
    {
        const ImVec2 top(maximum.x, minimum.y);
        const ImVec2 left(maximum.x - marker, minimum.y);
        const ImVec2 bottom(maximum.x, minimum.y + marker);
        drawList->AddTriangleFilled(top, left, bottom, IM_COL32(20, 25, 32, 255));
        drawList->AddTriangleFilled(ImVec2(maximum.x - marker * 0.15F, minimum.y + marker * 0.15F), ImVec2(maximum.x - marker * 0.65F, minimum.y + marker * 0.15F), ImVec2(maximum.x - marker * 0.15F, minimum.y + marker * 0.65F), IM_COL32(255, 255, 255, 255));
    }
}

void DrawMapThumbnail(const MapDocument& document, ImVec2 minimum, float size)
{
    auto*       drawList = ImGui::GetWindowDrawList();
    const float cell     = size / static_cast<float>(kMapWidth);
    for (std::size_t offset = 0; offset < kMapCoreSize; ++offset)
    {
        // Game view: file row runs along x and file column along y.
        const auto   color = MapTilePaletteColor(document.core[offset]);
        const ImVec2 tile(minimum.x + static_cast<float>(offset / kMapWidth) * cell, minimum.y + static_cast<float>(offset % kMapWidth) * cell);
        drawList->AddRectFilled(tile, ImVec2(tile.x + cell, tile.y + cell), IM_COL32(color.red, color.green, color.blue, 255));
    }
    for (std::size_t offset = 0; offset < kMapCoreSize; ++offset)
    {
        if ((std::to_integer<unsigned int>(document.core[offset]) & 0x20U) != 0)
        {
            const ImVec2 center(minimum.x + (static_cast<float>(offset / kMapWidth) + 0.5F) * cell, minimum.y + (static_cast<float>(offset % kMapWidth) + 0.5F) * cell);
            drawList->AddCircleFilled(center, std::max(1.5F, cell * 0.75F), IM_COL32(20, 25, 32, 255));
            drawList->AddCircleFilled(center, std::max(1.0F, cell * 0.45F), IM_COL32(255, 255, 255, 255));
        }
    }
    drawList->AddRect(minimum, ImVec2(minimum.x + size, minimum.y + size), ui::color::Border);
}

void DrawMapTileLegend(float width)
{
    ui::Caption("Terrain, bits 0-3");
    // Each cell holds a swatch and a name, about six font sizes wide.
    const float legendWidth = width > 0.0F ? width : ImGui::GetContentRegionAvail().x;
    const float cellWidth   = ImGui::GetFontSize() * 6.0F + 12.0F;
    const int   columns     = legendWidth >= cellWidth * 4.0F ? 4 : 2;
    if (ImGui::BeginTable("##map-palette", columns, ImGuiTableFlags_SizingStretchSame, ImVec2(std::min(legendWidth, cellWidth * static_cast<float>(columns)), 0.0F)))
    {
        for (std::size_t code = 0; code <= kMapLastTerrainCode; ++code)
        {
            ImGui::TableNextColumn();
            const auto color = MapTilePalette()[code];
            ImGui::PushID(static_cast<int>(code));
            ImGui::BeginGroup();
            ImGui::ColorButton("##color", ImVec4(color.red / 255.0F, color.green / 255.0F, color.blue / 255.0F, 1.0F), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
            ImGui::SameLine(0.0F, 3.0F);
            ImGui::TextUnformatted(MapTerrainLabel(static_cast<std::uint8_t>(code)).c_str());
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ui::Caption("Codes 0-7 in order. Codes 8-15 have no game terrain.");
    ui::VerticalSpace(4.0F);
    ui::Caption("Corner dot: 0x40, which also sets an edge flag on the tile to the right. Corner triangle: 0x80, the same for the tile below.");
    ui::Caption("Inset outline: raw bit 0x10. Diagonal hatch: start marker 0x20, numbered on the grid in the order the game reads them.");
}

std::string MapTerrainLabel(std::uint8_t code)
{
    const auto name = MapTerrainName(code).or_else([code]
                                                   {
                                                       return MapTerrainInferredName(code);
                                                   });
    if (!name)
    {
        return std::format("Code {}", code);
    }
    std::string label(*name);
    for (std::size_t index = 0; index < label.size(); ++index)
    {
        if (index == 0 || label[index - 1] == ' ')
        {
            label[index] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[index])));
        }
    }
    return label;
}

void DrawMapTileTooltip(std::size_t offset, std::byte value, bool rawBits)
{
    const auto position = MapTileCoordinates(offset);
    if (!position)
    {
        return;
    }
    const auto native = *MapNativeCell(position->row, position->column);
    const auto bits   = std::to_integer<unsigned int>(value);
    ImGui::BeginTooltip();
    ui::PushFont(ui::FontRole::Strong);
    ImGui::Text("Row %zu, column %zu", position->row, position->column);
    ui::PopFont();
    ImGui::TextUnformatted(MapTerrainLabel(static_cast<std::uint8_t>(bits & 0x0FU)).c_str());
    if (!rawBits)
    {
        if ((bits & 0x20U) != 0)
        {
            ImGui::TextUnformatted("Start marker");
        }
        ImGui::EndTooltip();
        return;
    }
    std::string flags;
    for (const auto& [mask, label] : { std::pair{ 0x10U, "0x10" }, std::pair{ 0x20U, "0x20" }, std::pair{ 0x40U, "0x40" }, std::pair{ 0x80U, "0x80" } })
    {
        if ((bits & mask) != 0)
        {
            flags += (flags.empty() ? "" : ", ") + std::string(label);
        }
    }
    ImGui::Text("Flags %s", flags.empty() ? "none" : flags.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ui::color::Muted);
    ImGui::Text("Offset %zu, byte 0x%02X. Native x %zu, y %zu", offset, bits, native.x, native.y);
    ImGui::PopStyleColor();
    ImGui::EndTooltip();
}

void DrawMapPreview(const MapDocument* document, std::string_view error)
{
    if (!document)
    {
        ui::EmptyState("map-failure", ui::icon::XCircle, "This map could not be inspected", error.empty() ? std::string_view("The Xbox DLC map record could not be inspected.") : error, nullptr, nullptr, std::max(160.0F, ImGui::GetContentRegionAvail().y));
        return;
    }

    ui::Heading("Map core - 1024 bytes");
    ui::VerticalSpace(4.0F);
    drawCore(*document);

    ui::VerticalSpace(8.0F);
    const bool allFf = std::ranges::all_of(document->footer,
                                           [](std::byte value)
                                           {
                                               return value == std::byte{ 0xFF };
                                           });
    if (ui::Section("Footer - 64 bytes", true))
    {
        ui::TonedText(allFf ? ui::Tone::Neutral : ui::Tone::Warn,
                      allFf ? "Observed pattern: all 0xFF."
                            : "Contains bytes other than 0xFF. Preserved without validation.");
        ui::Caption(formatFooter(document->footer));
    }
}

} // namespace rerevved::tools
