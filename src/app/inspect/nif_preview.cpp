#include "inspect/nif_preview.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace rerevved::tools
{
namespace
{

std::string formatMeshLabel(const NifDocument&  document,
                            const NifModelMesh& mesh,
                            std::size_t         meshIndex)
{
    const auto& shape = document.triShapes[mesh.triShapeIndex];
    const auto& data  = document.triShapeData[mesh.triShapeDataIndex];
    return "Mesh " + std::to_string(meshIndex + 1) + " - shape block " +
           std::to_string(shape.blockIndex) + " - data block " +
           std::to_string(data.blockIndex);
}

} // namespace

void DrawNifPreview(const NifDocument*           document,
                    const NifModel*              model,
                    std::optional<NifModelError> assemblyError,
                    std::string_view             documentError,
                    NifPreviewState&             state)
{
    state.presentationError.clear();
    const auto failure = [](std::string_view title, std::string_view message)
    {
        ui::EmptyState("nif-failure", ui::icon::XCircle, title, message, nullptr, nullptr, std::max(160.0F, ImGui::GetContentRegionAvail().y));
    };
    if (document == nullptr)
    {
        failure("This model could not be read", documentError);
        return;
    }
    if (assemblyError)
    {
        failure("The model could not be assembled", NifModelErrorMessage(*assemblyError));
        return;
    }

    const auto selectionCount = model->meshes.size() + 1;
    (void)SelectNifPreviewMesh(state, selectionCount, state.selectedMesh);
    const bool allMeshes        = state.selectedMesh == model->meshes.size();
    const auto currentMeshLabel = allMeshes ? std::string("All Meshes") : formatMeshLabel(*document, model->meshes[state.selectedMesh], state.selectedMesh);

    constexpr std::array                   kModes{ NifProjectionMode::Automatic, NifProjectionMode::Xy, NifProjectionMode::Xz, NifProjectionMode::Yz };
    std::array<const char*, kModes.size()> modeLabels{};
    int                                    modeIndex = 0;
    for (std::size_t index = 0; index < kModes.size(); ++index)
    {
        modeLabels[index] = NifProjectionModeName(kModes[index]).data();
        if (kModes[index] == state.projection)
        {
            modeIndex = static_cast<int>(index);
        }
    }
    // Mirrors ui::Segmented sizing so the mesh combo leaves exact room for projection and fit.
    ui::PushFont(ui::FontRole::Strong);
    float segmentedWidth = 8.0F;
    for (const auto* label : modeLabels)
    {
        segmentedWidth += ImGui::CalcTextSize(label).x + 32.0F;
    }
    ui::PopFont();
    const auto spacing        = ImGui::GetStyle().ItemSpacing.x;
    const auto groupWidth     = segmentedWidth + spacing * 2.0F + 40.0F;
    const auto availableWidth = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    const bool narrow         = availableWidth < groupWidth + 200.0F;
    ImGui::SetNextItemWidth(narrow ? availableWidth : std::min(320.0F, availableWidth - groupWidth));
    if (ImGui::BeginCombo("##nif-mesh", currentMeshLabel.c_str()))
    {
        for (std::size_t index = 0; index < model->meshes.size(); ++index)
        {
            const auto label    = formatMeshLabel(*document, model->meshes[index], index);
            const bool selected = index == state.selectedMesh;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                (void)SelectNifPreviewMesh(state, selectionCount, index);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        const bool allSelected = state.selectedMesh == model->meshes.size();
        if (ImGui::Selectable("All Meshes", allSelected))
        {
            (void)SelectNifPreviewMesh(state, selectionCount, model->meshes.size());
        }
        if (allSelected)
        {
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Mesh");
    if (!narrow)
    {
        ImGui::SameLine();
    }
    const auto projectionStart = ImGui::GetCursorScreenPos();
    if (ui::Segmented("nif-projection", modeLabels, modeIndex))
    {
        SelectNifPreviewProjection(state, kModes[static_cast<std::size_t>(modeIndex)]);
    }
    if (ImGui::IsMouseHoveringRect(projectionStart, { projectionStart.x + segmentedWidth, ImGui::GetItemRectMax().y + 4.0F }))
    {
        ImGui::SetTooltip("Projection");
    }
    ImGui::SameLine();
    if (ui::IconButton("nif-fit", ui::icon::Scan, "Fit/Reset"))
    {
        ResetNifPreviewView(state);
    }

    const bool                      drawAllMeshes = state.selectedMesh == model->meshes.size();
    std::vector<NifPreviewMeshView> meshViews;
    meshViews.reserve(drawAllMeshes ? model->meshes.size() : 1);
    const auto appendMeshView = [&](std::size_t meshIndex)
    {
        const auto& mesh     = model->meshes[meshIndex];
        const auto& geometry = document->triShapeData[mesh.triShapeDataIndex];
        meshViews.push_back({ geometry.vertexPositions,
                              geometry.triangles,
                              mesh.worldTransform });
    };
    if (drawAllMeshes)
    {
        for (std::size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
        {
            appendMeshView(meshIndex);
        }
        ui::Caption("All " + std::to_string(model->meshes.size()) + " meshes in common transformed space");
    }
    else
    {
        appendMeshView(state.selectedMesh);
        const auto& mesh     = model->meshes[state.selectedMesh];
        const auto& shape    = document->triShapes[mesh.triShapeIndex];
        const auto& geometry = document->triShapeData[mesh.triShapeDataIndex];
        ui::Caption("Mesh " + std::to_string(state.selectedMesh + 1) + " of " + std::to_string(model->meshes.size()) + "  -  " + std::to_string(geometry.vertexPositions.size()) + " vertices  -  " + std::to_string(geometry.triangles.size()) + " triangles  -  shape block " + std::to_string(shape.blockIndex) + ", data block " + std::to_string(geometry.blockIndex));
    }

    ui::VerticalSpace(4.0F);
    const auto   remaining = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize{ remaining.x, std::max(120.0F, remaining.y - 4.0F) };
    // The projection label owns a band above the fitted geometry so it never covers a vertex.
    constexpr float kLabelBand   = 28.0F;
    const auto      fittedLayout = CalculateNifWireframeLayout(meshViews,
                                                               canvasSize.x,
                                                               canvasSize.y - kLabelBand,
                                                               state.projection);
    if (!fittedLayout)
    {
        const auto message      = NifPreviewErrorMessage(fittedLayout.error());
        state.presentationError = message;
        failure("Presentation failed", message);
        return;
    }

    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##nif-wireframe",
                           canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemHovered())
    {
        const auto& input = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            PanNifPreview(state, input.MouseDelta.x, input.MouseDelta.y);
        }
        if (input.MouseWheel != 0.0F)
        {
            ZoomNifPreview(state, input.MouseWheel);
        }
    }

    const auto   viewedLayout = ApplyNifPreviewView(*fittedLayout, state);
    auto*        drawList     = ImGui::GetWindowDrawList();
    const auto   color        = ui::color::Accent;
    const ImVec2 limit{ origin.x + canvasSize.x, origin.y + canvasSize.y };
    drawList->AddRectFilled(origin, limit, ui::color::Surface, 10.0F);
    for (float x = origin.x + 24.0F; x < limit.x; x += 24.0F)
    {
        drawList->AddLine({ x, origin.y + 1.0F }, { x, limit.y - 1.0F }, IM_COL32(255, 255, 255, 8));
    }
    for (float y = origin.y + 24.0F; y < limit.y; y += 24.0F)
    {
        drawList->AddLine({ origin.x + 1.0F, y }, { limit.x - 1.0F, y }, IM_COL32(255, 255, 255, 8));
    }
    drawList->AddRect(origin, limit, ui::color::Border, 10.0F);
    const auto projectionName = NifProjectionName(fittedLayout->projection);
    ui::PushFont(ui::FontRole::Caption);
    drawList->AddText({ origin.x + 12.0F, origin.y + 10.0F }, ui::color::Muted, projectionName.data(), projectionName.data() + projectionName.size());
    ui::PopFont();
    const ImVec2 geometryOrigin{ origin.x, origin.y + kLabelBand };
    drawList->PushClipRect(geometryOrigin, limit, true);
    for (const auto& meshView : meshViews)
    {
        for (const auto& triangle : meshView.triangles)
        {
            const auto first = ProjectNifPosition(
                viewedLayout,
                *TransformNifPreviewPosition(meshView,
                                             meshView.positions[triangle[0]]));
            const auto second = ProjectNifPosition(
                viewedLayout,
                *TransformNifPreviewPosition(meshView,
                                             meshView.positions[triangle[1]]));
            const auto third = ProjectNifPosition(
                viewedLayout,
                *TransformNifPreviewPosition(meshView,
                                             meshView.positions[triangle[2]]));
            const ImVec2 firstPoint{ geometryOrigin.x + first[0], geometryOrigin.y + first[1] };
            const ImVec2 secondPoint{ geometryOrigin.x + second[0], geometryOrigin.y + second[1] };
            const ImVec2 thirdPoint{ geometryOrigin.x + third[0], geometryOrigin.y + third[1] };
            drawList->AddLine(firstPoint, secondPoint, color);
            drawList->AddLine(secondPoint, thirdPoint, color);
            drawList->AddLine(thirdPoint, firstPoint, color);
        }
    }
    drawList->PopClipRect();
}

} // namespace rerevved::tools
