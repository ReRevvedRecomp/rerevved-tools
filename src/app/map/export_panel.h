#pragma once

#include "map/map_pack_export.h"
#include "map/scenario_form.h"
#include "ui/path_modal.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rerevved::tools
{

class DocumentWorkspace;

struct ExportPanelState
{
    std::array<char, 128>              packName{};
    std::array<char, 64>               packId{};
    std::array<char, 32>               packVersion{ '1', '.', '0' };
    std::array<char, 128>              packAuthor{};
    std::array<char, 512>              packDescription{};
    bool                               packIdEdited  = false;
    bool                               namePrefilled = false;
    int                                slot          = static_cast<int>(kMapPackFirstSlot);
    std::filesystem::path              outputParent;
    PathModalState                     pathModal;
    std::vector<std::filesystem::path> protectedPaths;
    std::optional<MapPackReceipt>      receipt;
    bool                               requestExport = false;
    bool                               canExport     = false;
    std::string                        exportBlocker;

    // Choose sets this to ask the application to replace values that clash with the folder's packs.
    bool folderPicked = false;
};

void SetProtectedPaths(ExportPanelState& state, std::vector<std::filesystem::path> paths);
// The pack's name and description follow the scenario title and description. Its ID follows the
// name until edited.
void SyncPackWithScenario(ExportPanelState& state, const MapDraft& draft);
void DrawExportPanel(CreatorWorkspace& workspace, ExportPanelState& state, ScenarioEditorState& scenario, DocumentWorkspace& activity, const MapDocument* map, bool fileDialogs);

} // namespace rerevved::tools
