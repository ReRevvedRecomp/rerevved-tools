#include "map/map_editor.h"
#include "map/map_preview.h"
#include "ui/native_dialog.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

namespace rerevved::tools
{
namespace
{

const std::vector<MapTileWarning>& draftWarnings(MapEditorState& state);

void selectTile(CreatorWorkspace& workspace, MapEditorState& state, std::size_t offset, bool focus)
{
    auto result = state.model.SelectTile(offset);
    if (!result)
    {
        state.diagnostic = MapEditErrorMessage(result.error());
        return;
    }
    static_cast<void>(workspace.SelectMapTile(offset));
    state.focusTile = focus;
    state.focusGrid = state.focusGrid || focus;
    state.diagnostic.clear();
}

void revise(CreatorWorkspace& workspace)
{
    const auto step = workspace.Map().step;
    static_cast<void>(workspace.ReviseMap());
    static_cast<void>(workspace.SelectMapStep(step));
}

void paint(CreatorWorkspace& workspace, MapEditorState& state, std::size_t offset)
{
    auto result = state.model.Paint(offset);
    if (!result)
    {
        state.diagnostic = MapEditErrorMessage(result.error());
        return;
    }
    selectTile(workspace, state, offset, false);
    if (*result)
    {
        revise(workspace);
    }
}

bool startDraft(CreatorWorkspace& workspace, MapEditorState& state, const MapDocument& document, const std::string& identity)
{
    auto result = workspace.StartMapDraft(identity);
    if (!result)
    {
        state.diagnostic = CreatorPrerequisiteMessage(result.error().missing.front());
        return false;
    }
    static_cast<void>(state.model.StartNew(document));
    state.fit = true;
    state.diagnostic.clear();
    selectTile(workspace, state, 0, false);
    static_cast<void>(workspace.SelectMapStep(MapStep::EditGrid));
    return true;
}

void beginFrame(CreatorWorkspace& workspace, MapEditorState& state)
{
    if (!workspace.Map().started && state.model.Document())
    {
        state.model.Reset();
        state.diagnostic.clear();
    }
    if (state.lastDrawFrame + 1 != ImGui::GetFrameCount() || workspace.Map().step != MapStep::EditGrid)
    {
        CancelMapEditorStroke(workspace, state);
    }
    state.lastDrawFrame = ImGui::GetFrameCount();
}

void drawDiagnostic(MapEditorState& state)
{
    if (state.diagnostic.empty())
    {
        return;
    }
    ui::VerticalSpace(6.0F);
    ui::BeginCard("map-diagnostic", 0.0F, ui::Tone::Error);
    ui::TonedText(ui::Tone::Error, state.diagnostic);
    if (ui::GhostButton(ui::icon::Copy, "Copy"))
    {
        ImGui::SetClipboardText(state.diagnostic.c_str());
    }
    ui::EndCard();
}

void drawByteGrid(const MapEditModel& model, ImVec2 origin, float cellSize, bool lines)
{
    const auto& document = *model.Document();
    for (std::size_t row = 0; row < kMapHeight; ++row)
    {
        for (std::size_t column = 0; column < kMapWidth; ++column)
        {
            const auto   offset = *MapTileOffset(row, column);
            const ImVec2 minimum(origin.x + static_cast<float>(row) * cellSize, origin.y + static_cast<float>(column) * cellSize);
            DrawMapTile(minimum, cellSize, document.core[offset], lines, false);
        }
    }
    // Numbered in file order, the order the loader reads them. The ninth on is ignored.
    {
        auto*       drawList = ImGui::GetWindowDrawList();
        std::size_t marker   = 0;
        for (std::size_t offset = 0; offset < kMapCoreSize; ++offset)
        {
            if ((std::to_integer<unsigned int>(document.core[offset]) & 0x20U) == 0)
            {
                continue;
            }
            ++marker;
            const auto   position = *MapTileCoordinates(offset);
            const ImVec2 center(origin.x + (static_cast<float>(position.row) + 0.5F) * cellSize, origin.y + (static_cast<float>(position.column) + 0.5F) * cellSize);
            const bool   read = marker <= kMapMarkerLimit;
            if (cellSize < 12.0F)
            {
                drawList->AddCircleFilled(center, std::max(2.0F, cellSize * 0.42F), IM_COL32(20, 25, 32, 255));
                drawList->AddCircleFilled(center, std::max(1.0F, cellSize * 0.24F), read ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 255));
                continue;
            }
            const auto  label = read ? std::to_string(marker) : std::string("x");
            const float font  = std::min(ImGui::GetFontSize(), cellSize * 0.62F);
            drawList->AddCircleFilled(center, cellSize * 0.4F, read ? IM_COL32(20, 25, 32, 230) : IM_COL32(110, 110, 110, 230));
            drawList->AddCircle(center, cellSize * 0.4F, IM_COL32(255, 255, 255, 255), 0, std::max(1.0F, cellSize * 0.06F));
            const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(font, FLT_MAX, 0.0F, label.c_str());
            drawList->AddText(ImGui::GetFont(), font, ImVec2(center.x - text.x * 0.5F, center.y - text.y * 0.5F), IM_COL32(255, 255, 255, 255), label.c_str());
        }
    }
    if (const auto selected = model.SelectedTile())
    {
        const auto   position = *MapTileCoordinates(*selected);
        const ImVec2 minimum(origin.x + static_cast<float>(position.row) * cellSize, origin.y + static_cast<float>(position.column) * cellSize);
        auto*        drawList = ImGui::GetWindowDrawList();
        drawList->AddRect(ImVec2(minimum.x - 1.0F, minimum.y - 1.0F), ImVec2(minimum.x + cellSize + 1.0F, minimum.y + cellSize + 1.0F), ui::color::OnAccent, 0.0F, 0, 4.0F);
        drawList->AddRect(ImVec2(minimum.x - 1.0F, minimum.y - 1.0F), ImVec2(minimum.x + cellSize + 1.0F, minimum.y + cellSize + 1.0F), IM_COL32(255, 255, 255, 255), 0.0F, 0, 2.0F);
    }
}

void handleGridKeys(CreatorWorkspace& workspace, MapEditorState& state)
{
    const auto& io = ImGui::GetIO();
    if (!ImGui::IsWindowFocused() || io.WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        return;
    }
    // The shell handles Ctrl+Z and Ctrl+Y for the whole page.
    if (io.KeyCtrl)
    {
        return;
    }
    if (state.model.StrokeActive())
    {
        return;
    }
    const auto selected = state.model.SelectedTile().value_or(0);
    auto       position = *MapTileCoordinates(selected);
    // Arrows move on screen. In the game view file rows run left to right.
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && position.row > 0)
    {
        --position.row;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && position.row + 1 < kMapHeight)
    {
        ++position.row;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && position.column > 0)
    {
        --position.column;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && position.column + 1 < kMapWidth)
    {
        ++position.column;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home))
    {
        position.row = 0;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End))
    {
        position.row = kMapHeight - 1;
    }
    const auto offset = *MapTileOffset(position.row, position.column);
    if (offset != selected)
    {
        selectTile(workspace, state, offset, true);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false))
    {
        auto result = state.model.BeginStroke(state.brush);
        if (result)
        {
            paint(workspace, state, offset);
            static_cast<void>(state.model.EndStroke());
        }
        else
        {
            state.diagnostic = MapEditErrorMessage(result.error());
        }
    }
}

constexpr float kChipHeight = 28.0F;
constexpr float kChipSwatch = 14.0F;
constexpr float kChipGap    = 4.0F;

float chipWidth(const std::string& label)
{
    return 8.0F + kChipSwatch + 6.0F + ImGui::CalcTextSize(label.c_str()).x + 10.0F;
}

int terrainChipRows(float left, float right)
{
    int   rows = 1;
    float x    = left;
    for (std::uint8_t code = 0; code <= kMapLastTerrainCode; ++code)
    {
        const float width = chipWidth(MapTerrainLabel(code));
        if (x != left && x + width > right)
        {
            x = left;
            ++rows;
        }
        x += width + kChipGap;
    }
    return rows;
}

void drawTerrainChips(MapEditorState& state, float right)
{
    constexpr float kHeight = kChipHeight;
    constexpr float kSwatch = kChipSwatch;
    const float     left    = ImGui::GetCursorScreenPos().x;
    float           x       = left;
    float           y       = ImGui::GetCursorScreenPos().y + 2.0F;
    auto*           draw    = ImGui::GetWindowDrawList();
    for (std::uint8_t code = 0; code <= kMapLastTerrainCode; ++code)
    {
        const auto  label = MapTerrainLabel(code);
        const float width = chipWidth(label);
        if (x != left && x + width > right)
        {
            x = left;
            y += kHeight + kChipGap;
        }
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::PushID(code);
        if (ImGui::InvisibleButton(label.c_str(), ImVec2(width, kHeight), ImGuiButtonFlags_EnableNav))
        {
            state.brush.terrainCode = code;
        }
        const bool   selected = state.brush.terrainCode == code;
        const ImVec2 maximum(x + width, y + kHeight);
        if (selected)
        {
            draw->AddRectFilled(ImVec2(x, y), maximum, ui::color::RowFill, 6.0F);
            draw->AddRect(ImVec2(x, y), maximum, ui::color::RowBorder, 6.0F);
        }
        else if (ImGui::IsItemHovered())
        {
            draw->AddRectFilled(ImVec2(x, y), maximum, ui::color::Hover, 6.0F);
        }
        const auto   color = MapTilePalette()[code];
        const ImVec2 swatch(x + 8.0F, y + (kHeight - kSwatch) * 0.5F);
        draw->AddRectFilled(swatch, ImVec2(swatch.x + kSwatch, swatch.y + kSwatch), IM_COL32(color.red, color.green, color.blue, 255), 3.0F);
        draw->AddRect(swatch, ImVec2(swatch.x + kSwatch, swatch.y + kSwatch), IM_COL32(0, 0, 0, 110), 3.0F);
        draw->AddText(ImVec2(swatch.x + kSwatch + 6.0F, y + (kHeight - ImGui::GetFontSize()) * 0.5F), selected ? ui::color::Text : ui::color::TabText, label.c_str());
        ImGui::PopID();
        x += width + kChipGap;
    }
}

void toolbarDivider()
{
    ImGui::SameLine(0.0F, 10.0F);
    const auto position = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(position.x, position.y + 6.0F), ImVec2(position.x, position.y + 26.0F), ui::color::Border);
    ImGui::Dummy(ImVec2(1.0F, 32.0F));
    ImGui::SameLine(0.0F, 10.0F);
}

void randomizeTerrain(CreatorWorkspace& workspace, MapEditorState& state)
{
    auto document     = *state.model.Document();
    state.terrainSeed = NextLayoutSeed(state.terrainSeed);
    RandomizeLandTerrain(document, state.terrainSeed);
    auto result = state.model.ReplaceTiles(document.core);
    if (!result)
    {
        state.diagnostic = MapEditErrorMessage(result.error());
        return;
    }
    if (*result)
    {
        revise(workspace);
    }
}

void drawToolbar(CreatorWorkspace& workspace, MapEditorState& state)
{
    const auto  origin   = ImGui::GetCursorScreenPos();
    const float width    = ImGui::GetContentRegionAvail().x;
    auto*       drawList = ImGui::GetWindowDrawList();
    // The bar's height is known only after layout, so its background goes on a lower channel.
    ImDrawListSplitter splitter;
    splitter.Split(drawList, 2);
    splitter.SetCurrentChannel(drawList, 1);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 6.0F, origin.y + 6.0F));

    struct Tool
    {
        MapBrush    brush;
        const char* id;
        const char* icon;
        const char* tip;
    };

    constexpr std::array kTools{
        Tool{ MapBrush::SelectTile, "select-tool", ui::icon::Pointer, "Select tiles (arrow keys move, Enter applies the brush)" },
        Tool{ MapBrush::TerrainCode, "terrain-tool", ui::icon::Brush, "Terrain brush: paints one of the eight game terrains" },
        Tool{ MapBrush::StartMarker, "marker-tool", ui::icon::MapPin, "Start marker brush: the game reads the first eight markers as civilization start positions" },
    };
    ui::PushFont(ui::FontRole::Strong);
    const float randomizeWidth = ImGui::CalcTextSize("Randomize Terrain").x + 28.0F;
    ui::PopFont();
    const float randomizeLeft = origin.x + width - 6.0F - randomizeWidth;
    for (const auto& tool : kTools)
    {
        if (ui::IconButton(tool.id, tool.icon, tool.tip, state.brush.tool == tool.brush))
        {
            state.brush.tool = tool.brush;
        }
        ImGui::SameLine(0.0F, 2.0F);
    }
    // Sized for the terrain chips whichever tool is on, so switching tools never moves the grid.
    // The chips start past the divider: 10 + 1 + 10 pixels from the last tool, less its 2.
    const int   rows   = terrainChipRows(ImGui::GetCursorScreenPos().x + 19.0F, randomizeLeft - 10.0F);
    const float bottom = std::max(origin.y + 38.0F, origin.y + 8.0F + rows * (kChipHeight + kChipGap) - kChipGap) + 6.0F;
    if (state.brush.tool == MapBrush::TerrainCode)
    {
        toolbarDivider();
        drawTerrainChips(state, randomizeLeft - 10.0F);
    }
    else if (state.brush.tool == MapBrush::StartMarker)
    {
        toolbarDivider();
        int                                  mode = state.brush.set ? 0 : 1;
        constexpr std::array<const char*, 2> kModes{ "Add", "Remove" };
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.0F);
        if (ui::Segmented("marker-mode", kModes, mode))
        {
            state.brush.set = mode == 0;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(randomizeLeft, origin.y + 6.0F));
    if (ui::SecondaryButton(nullptr, "Randomize Terrain", true, {}, randomizeWidth))
    {
        randomizeTerrain(workspace, state);
    }
    splitter.SetCurrentChannel(drawList, 0);
    // The bar sits on the column's top edge, where the column's clip rect can round past its top line.
    drawList->PushClipRect(ImVec2(origin.x - 1.0F, origin.y - 1.0F), ImVec2(origin.x + width + 1.0F, bottom + 1.0F));
    drawList->AddRectFilled(origin, ImVec2(origin.x + width, bottom), IM_COL32(18, 48, 111, 255));
    drawList->AddRect(origin, ImVec2(origin.x + width, bottom), ui::color::Border);
    drawList->PopClipRect();
    splitter.Merge(drawList);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
    ImGui::Dummy(ImVec2(width, 0.0F));
}

void drawGridCanvas(CreatorWorkspace& workspace, MapEditorState& state, float height)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::color::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0F);
    const bool visible = ImGui::BeginChild("##map-grid-scroll", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (visible)
    {
        if (state.focusGrid)
        {
            ImGui::SetWindowFocus();
            state.focusGrid = false;
        }
        if (state.fit)
        {
            ImGui::SetScrollX(0);
            ImGui::SetScrollY(0);
            state.fit = false;
        }
        constexpr float kGutter   = 26.0F;
        const auto      available = ImGui::GetContentRegionAvail();
        const float     fitted    = std::max(4.0F, std::floor((std::min(available.x, available.y) - kGutter - 8.0F) / static_cast<float>(kMapWidth)));
        const float     cellSize  = fitted;
        const float     gridSize  = cellSize * static_cast<float>(kMapWidth);
        const ImVec2    topLeft   = ImGui::GetCursorScreenPos();
        const float     offsetX   = std::max(0.0F, (available.x - gridSize - kGutter) * 0.5F);
        const ImVec2    origin(topLeft.x + offsetX + kGutter, topLeft.y + kGutter);
        auto*           drawList = ImGui::GetWindowDrawList();
        ui::PushFont(ui::FontRole::Caption);
        for (std::size_t index = 0; index < kMapWidth; index += 4)
        {
            const auto label = std::to_string(index);
            drawList->AddText(ImVec2(origin.x + static_cast<float>(index) * cellSize + 2.0F, topLeft.y + 4.0F), ui::color::Faint, label.c_str());
            drawList->AddText(ImVec2(topLeft.x + offsetX + 2.0F, origin.y + static_cast<float>(index) * cellSize + 1.0F), ui::color::Faint, label.c_str());
        }
        ui::PopFont();
        if (state.focusTile && state.model.SelectedTile())
        {
            const auto position = *MapTileCoordinates(*state.model.SelectedTile());
            ImGui::SetScrollX(std::max(0.0F, static_cast<float>(position.row) * cellSize - ImGui::GetWindowWidth() * 0.5F));
            ImGui::SetScrollY(std::max(0.0F, static_cast<float>(position.column) * cellSize - ImGui::GetWindowHeight() * 0.5F));
            state.focusTile = false;
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##map-grid", ImVec2(gridSize, gridSize));
        const bool hovered     = ImGui::IsItemHovered();
        const bool active      = ImGui::IsItemActive();
        const auto mouse       = ImGui::GetMousePos();
        const auto offset      = hovered ? MapTileAtPoint(mouse.x - origin.x, mouse.y - origin.y, cellSize) : std::nullopt;
        const bool escape      = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool captureLost = !ImGui::IsWindowFocused() || ImGui::GetIO().AppFocusLost || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        if (state.model.StrokeActive() && (escape || captureLost))
        {
            if (state.model.CancelStroke())
            {
                revise(workspace);
            }
        }
        else if (state.model.StrokeActive() && (!hovered || !active || !ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        {
            static_cast<void>(state.model.EndStroke());
        }
        if (offset && !escape && !captureLost)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                auto result = state.model.BeginStroke(state.brush);
                if (!result)
                {
                    state.diagnostic = MapEditErrorMessage(result.error());
                }
            }
            if (state.model.StrokeActive() && active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                paint(workspace, state, *offset);
            }
        }
        handleGridKeys(workspace, state);
        drawByteGrid(state.model, origin, cellSize, cellSize >= 10.0F);
        for (const auto& warning : draftWarnings(state))
        {
            // Only edge writes are outlined. The other checks can cover most of an imported map.
            if (warning.tileOffset && warning.kind == MapTileWarningKind::DirectionalPairOutsideGrid)
            {
                const auto   position = *MapTileCoordinates(*warning.tileOffset);
                const ImVec2 minimum(origin.x + static_cast<float>(position.row) * cellSize, origin.y + static_cast<float>(position.column) * cellSize);
                drawList->AddRect(ImVec2(minimum.x + 1.0F, minimum.y + 1.0F), ImVec2(minimum.x + cellSize - 1.0F, minimum.y + cellSize - 1.0F), ui::color::Warning, 0.0F, 0, 2.0F);
            }
        }
        if (offset)
        {
            const auto   position = *MapTileCoordinates(*offset);
            const ImVec2 minimum(origin.x + static_cast<float>(position.row) * cellSize, origin.y + static_cast<float>(position.column) * cellSize);
            drawList->AddRect(minimum, ImVec2(minimum.x + cellSize, minimum.y + cellSize), IM_COL32(255, 255, 255, 140), 0.0F, 0, 1.5F);
            DrawMapTileTooltip(*offset, state.model.Document()->core[*offset], false);
        }
        ImGui::Dummy(ImVec2(0.0F, 8.0F));
    }
    else if (state.model.StrokeActive())
    {
        if (state.model.CancelStroke())
        {
            revise(workspace);
        }
    }
    ImGui::EndChild();
}

const std::vector<MapTileWarning>& draftWarnings(MapEditorState& state)
{
    const auto* document = state.model.Document();
    if (document == nullptr)
    {
        state.warnings.clear();
        state.warningsCore.reset();
    }
    else if (!state.warningsCore || *state.warningsCore != document->core)
    {
        state.warnings     = MapDraftWarnings(*document);
        state.warningsCore = document->core;
    }
    return state.warnings;
}

bool listRow(const char* button, const std::function<void(ImVec2, float)>& thumbnail, std::string_view name, std::string_view caption, bool primary)
{
    constexpr float kHeight = 64.0F;
    constexpr float kThumb  = 48.0F;
    constexpr float kButton = 132.0F;
    const auto      start   = ImGui::GetCursorScreenPos();
    const float     width   = ImGui::GetContentRegionAvail().x;
    const auto      maximum = ImVec2(start.x + width, start.y + kHeight);
    auto*           draw    = ImGui::GetWindowDrawList();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(start, maximum))
    {
        draw->AddRectFilled(start, maximum, ui::color::RowFill);
        draw->AddRect(start, maximum, ui::color::RowBorder);
    }
    thumbnail(ImVec2(start.x + 12.0F, start.y + (kHeight - kThumb) * 0.5F), kThumb);
    const float textX = start.x + 12.0F + kThumb + 14.0F;
    draw->PushClipRect(start, ImVec2(maximum.x - kButton - 24.0F, maximum.y), true);
    ui::PushFont(ui::FontRole::Strong);
    draw->AddText(ImVec2(textX, start.y + 13.0F), ui::color::Text, name.data(), name.data() + name.size());
    ui::PopFont();
    ui::PushFont(ui::FontRole::Caption);
    draw->AddText(ImVec2(textX, start.y + 35.0F), ui::color::Muted, caption.data(), caption.data() + caption.size());
    ui::PopFont();
    draw->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(std::max(textX, maximum.x - 12.0F - kButton), start.y + (kHeight - 36.0F) * 0.5F));
    const bool clicked = primary ? ui::PrimaryButton(nullptr, button, true, {}, kButton) : ui::SecondaryButton(nullptr, button, true, {}, kButton);
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(width, kHeight - ImGui::GetStyle().ItemSpacing.y + 2.0F));
    return clicked;
}

std::optional<std::filesystem::path> drawRecent(std::span<const MapDraftListing> recent)
{
    std::optional<std::filesystem::path> chosen;
    const auto                           now        = MapDraftClockNow();
    std::size_t                          unreadable = 0;
    if (recent.empty())
    {
        ui::Muted("No recent maps yet.");
    }
    for (const auto& listing : recent)
    {
        if (!listing.error.empty())
        {
            ++unreadable;
            continue;
        }
        // Rows are keyed by file, so a relist cannot move a click to another map.
        ImGui::PushID(PathToUtf8(listing.path).c_str());
        const auto caption = PathToUtf8(listing.path.parent_path()) + ", saved " + FormatMapDraftAge(listing.draft.savedAt, now);
        const auto thumb   = [&](ImVec2 at, float size)
        {
            DrawMapThumbnail(listing.draft.tiles, at, size);
        };
        if (listRow("Open##open-recent", thumb, PathToUtf8(listing.path.stem()), caption, true))
        {
            chosen = listing.path;
        }
        ImGui::PopID();
    }
    if (unreadable != 0)
    {
        ui::VerticalSpace(4.0F);
        ui::Caption(std::to_string(unreadable) + (unreadable == 1 ? " recent map could not be read." : " recent maps could not be read."));
    }
    return chosen;
}

// Group tabs, in StartingLandGroup order.
const std::array<std::string, 6>     kGroupLabels{ "Landmasses", "Islands", "Inland Water", "Matchups", "Climates", "Real World" };
constexpr std::array<const char*, 3> kSizeLabels{ "Small", "Medium", "Large" };
// In MapMirror order.
constexpr std::array<const char*, 3> kMirrorLabels{ "Off", "Left to Right", "Top to Bottom" };
constexpr std::array<const char*, 6> kPlayerLabels{ "2", "3", "4", "5", "6", "7" };
static_assert(kPlayerLabels.size() == kMaximumPlayers - kMinimumPlayers + 1);

std::string trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t");
    return first == std::string_view::npos ? std::string() : std::string(text.substr(first, text.find_last_not_of(" \t") - first + 1));
}

const MapDocument& preview(MapEditorState& state, StartingLand land)
{
    const auto index = static_cast<std::size_t>(land);
    const auto key   = MapPreviewKey{ state.landSize, state.layoutSeed, state.mirror, state.players };
    if (state.previewKeys[index] != key)
    {
        state.previews[index]    = GenerateStartingLand(land, state.landSize, state.layoutSeed, state.mirror, state.players);
        state.previewKeys[index] = key;
    }
    return state.previews[index];
}

bool landOption(const char* label, const MapDocument& preview, bool selected, float size, float width)
{
    constexpr float pad     = 10.0F;
    const float     text    = ImGui::GetTextLineHeight();
    const ImVec2    min     = ImGui::GetCursorScreenPos();
    const ImVec2    box     = ImVec2(width, size + 2.0F * pad + text + 6.0F);
    const bool      clicked = ImGui::InvisibleButton(label, box);
    const bool      hovered = ImGui::IsItemHovered();
    const ImVec2    max(min.x + box.x, min.y + box.y);
    auto*           draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, selected ? ui::color::RowFill : hovered ? ui::color::Hover
                                                                          : IM_COL32(18, 48, 111, 255));
    draw->AddRect(min, max, selected ? ui::color::RowBorder : ui::color::Border);
    DrawMapThumbnail(preview, ImVec2(min.x + (box.x - size) * 0.5F, min.y + pad), size);
    const std::string_view shown      = std::string_view(label).substr(0, std::string_view(label).find("##"));
    const float            shownWidth = ImGui::CalcTextSize(shown.data(), shown.data() + shown.size()).x;
    draw->AddText(ImVec2(min.x + (box.x - shownWidth) * 0.5F, min.y + pad + size + 6.0F), selected || hovered ? ui::color::Text : ui::color::TabText, shown.data(), shown.data() + shown.size());
    return clicked;
}

} // namespace

MapStartChoice DrawMapStart(CreatorWorkspace& workspace, MapEditorState& state, std::span<const MapDraftListing> recent, const MapPackUsage& usage)
{
    ImGui::PushID(&state);
    beginFrame(workspace, state);
    MapStartChoice    choice;
    const bool        create  = std::exchange(state.requestCreate, false);
    const std::string name    = trimmed(state.mapName.data());
    const auto        problem = MapNameProblem(name);
    if (state.identityName != name)
    {
        state.identity     = SuggestMapIdentity(name, usage);
        state.identityName = name;
    }
    const auto& identity = state.identity;
    state.canCreate      = !name.empty() && problem.empty();
    state.createBlocker  = name.empty() ? "Name the map first." : problem.empty() ? std::string()
                                                                                  : "Change the name first.";
    state.createProblem  = problem;
    if (problem.empty() && !name.empty() && identity.mapName.empty())
    {
        state.createProblem = "All 26 map letters are in use. Choose one under Advanced on the Export tab.";
    }
    else if (problem.empty() && !usage.complete)
    {
        state.createProblem = "Some map packs or recent maps could not be read in full, so these values may clash with them.";
    }

    if (state.startTab == 1)
    {
        choice.open = drawRecent(recent);
    }
    else
    {
        ui::BeginRow("name", "Map Name");
        const bool entered = ImGui::InputText("##map-name", state.mapName.data(), state.mapName.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ui::EndRow();
        if ((entered || create) && state.canCreate)
        {
            if (startDraft(workspace, state, preview(state, state.land), identity.title))
            {
                choice.started = identity;
            }
        }

        ui::VerticalSpace(6.0F);
        ui::SectionHeading("Starting Land");
        ui::VerticalSpace(2.0F);
        ui::TextTabs("land-group", kGroupLabels, state.landGroup, true);
        ui::VerticalSpace(6.0F);
        // No group has more than two rows.
        constexpr int kPerRow = 6;
        const float   spacing = 10.0F;
        const float   column  = std::floor((ImGui::GetContentRegionAvail().x - static_cast<float>(kPerRow - 1) * spacing) / static_cast<float>(kPerRow));
        const float   thumb   = std::min(column - 24.0F, 84.0F);
        int           shown   = 0;
        for (const auto& info : kStartingLands)
        {
            if (static_cast<int>(info.group) != state.landGroup)
            {
                continue;
            }
            if (shown % kPerRow != 0)
            {
                ImGui::SameLine(0.0F, spacing);
            }
            else if (shown != 0)
            {
                ui::VerticalSpace(4.0F);
            }
            ++shown;
            const auto label = std::string(info.name) + "##land";
            if (landOption(label.c_str(), preview(state, info.land), state.land == info.land, thumb, column))
            {
                state.land = info.land;
            }
        }
        ui::VerticalSpace(2.0F);
        const bool blank = state.land == StartingLand::Blank;
        ui::BeginRow("size", "Land Size");
        ImGui::BeginDisabled(!StartingLandUsesSize(state.land));
        int size = static_cast<int>(state.landSize);
        if (ui::Stepper("land-size", kSizeLabels, size))
        {
            state.landSize = static_cast<LandSize>(size);
        }
        ImGui::EndDisabled();
        ui::EndRow();
        ui::BeginRow("player-count", "Players");
        ImGui::BeginDisabled(blank);
        int players = state.players - kMinimumPlayers;
        if (ui::Stepper("players", kPlayerLabels, players))
        {
            state.players = players + kMinimumPlayers;
        }
        ImGui::EndDisabled();
        ui::EndRow();
        ui::BeginRow("mirror-axis", "Mirror");
        {
            // A land with a fixed mirror shows it greyed out, so the rows below never move.
            const bool fixed  = !StartingLandMirrors(state.land);
            int        mirror = static_cast<int>(fixed ? state.land == StartingLand::Skirmish ? MapMirror::LeftToRight : MapMirror::Off : state.mirror);
            // Centred like the values above. Segmented pads each label by 32 and the bar by 8.
            ui::PushFont(ui::FontRole::Strong);
            float group = 8.0F;
            for (const auto* label : kMirrorLabels)
            {
                group += ImGui::CalcTextSize(label).x + 32.0F;
            }
            ui::PopFont();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::CalcItemWidth() - group) * 0.5F));
            ImGui::BeginDisabled(fixed);
            if (ui::Segmented("mirror", kMirrorLabels, mirror) && !fixed)
            {
                state.mirror = static_cast<MapMirror>(mirror);
            }
            ImGui::EndDisabled();
            if (fixed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            {
                ui::Tooltip(state.land == StartingLand::Skirmish ? "Skirmish always mirrors left to right." : "This land does not mirror.");
            }
        }
        ui::EndRow();
        ui::BeginRow("layout", "Layout");
        if (blank)
        {
            ui::RowText("Polar ice at the top and bottom", ui::color::Muted);
        }
        else
        {
            ui::PushFont(ui::FontRole::Strong);
            const float buttonWidth = ImGui::CalcTextSize("Randomize").x + 28.0F;
            ui::PopFont();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::CalcItemWidth() - buttonWidth) * 0.5F));
            if (ui::SecondaryButton(nullptr, "Randomize"))
            {
                state.layoutSeed = NextLayoutSeed(state.layoutSeed);
            }
        }
        ui::EndRow();

        ui::VerticalSpace(6.0F);
        ui::SectionHeading("Pack");
        ui::VerticalSpace(2.0F);
        const bool named = !name.empty() && problem.empty();
        ui::ValueRow("scenario-id", "Scenario ID", named ? std::to_string(identity.scenarioId) : std::string("-"));
        ui::ValueRow("map-letter", "Map Letter", !named ? std::string("-") : identity.mapName.empty() ? std::string("None free")
                                                                                                      : identity.mapName);
        ui::ValueRow("pack-id", "Pack ID", named ? identity.packId : std::string("-"));
    }
    // Closing a new map returns to Create, not the tab it came from.
    if (choice.started || choice.open)
    {
        state.startTab = 0;
    }
    drawDiagnostic(state);
    ImGui::PopID();
    return choice;
}

void DrawMapGrid(CreatorWorkspace& workspace, MapEditorState& state)
{
    ImGui::PushID(&state);
    beginFrame(workspace, state);
    if (!state.model.Document())
    {
        ui::TonedText(ui::Tone::Error, "This map has no editable bytes. Close it from the menu, then start again.");
        ImGui::PopID();
        return;
    }
    drawToolbar(workspace, state);
    ui::VerticalSpace(8.0F);
    drawGridCanvas(workspace, state, std::max(240.0F, ImGui::GetContentRegionAvail().y - 6.0F - (state.diagnostic.empty() ? 0.0F : 80.0F)));
    drawDiagnostic(state);
    ImGui::PopID();
}

void DrawMapTileInspector(MapEditorState& state)
{
    ImGui::PushID(&state);
    const auto* document = state.model.Document();
    ui::SectionHeading("Tile");
    ui::VerticalSpace(2.0F);
    if (!document)
    {
        ui::Muted("Start a map to inspect tiles.");
        ImGui::PopID();
        return;
    }
    if (const auto selected = state.model.SelectedTile())
    {
        const auto position = *MapTileCoordinates(*selected);
        const auto value    = document->core[*selected];
        const auto bits     = std::to_integer<unsigned int>(value);
        ui::ValueRow("position", "Position", "Row " + std::to_string(position.row) + ", Column " + std::to_string(position.column));
        ui::BeginRow("terrain", "Terrain");
        const auto  label  = MapTerrainLabel(static_cast<std::uint8_t>(bits & 0x0FU));
        const float swatch = 18.0F;
        const float group  = swatch + 8.0F + ImGui::CalcTextSize(label.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::CalcItemWidth() - group) * 0.5F));
        const auto at = ImGui::GetCursorScreenPos();
        DrawMapTile(ImVec2(at.x, at.y + (ImGui::GetFrameHeight() - swatch) * 0.5F), swatch, value, false, false);
        ImGui::Dummy(ImVec2(swatch, ImGui::GetFrameHeight()));
        ImGui::SameLine(0.0F, 8.0F);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());
        ui::EndRow();
        for (const auto& warning : draftWarnings(state))
        {
            if (warning.tileOffset == selected)
            {
                ui::TonedText(ui::Tone::Warn, warning.message);
            }
        }
    }
    else
    {
        ui::Muted("Click a tile to see its details.");
    }
    ImGui::PopID();
}

void UndoMapEdit(CreatorWorkspace& workspace, MapEditorState& state)
{
    if (!state.model.StrokeActive() && state.model.Undo())
    {
        revise(workspace);
    }
}

void RedoMapEdit(CreatorWorkspace& workspace, MapEditorState& state)
{
    if (!state.model.StrokeActive() && state.model.Redo())
    {
        revise(workspace);
    }
}

} // namespace rerevved::tools
