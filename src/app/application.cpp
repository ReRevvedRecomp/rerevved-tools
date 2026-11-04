#include "application.h"
#include "map/map_draft_store.h"
#include "map/map_edit_model.h"
#include "map/map_editor.h"
#include "map/map_images.h"
#include "map/map_pack_export.h"
#include "map/scenario_form.h"
#include "ui/native_dialog.h"
#include "ui/ui_widgets.h"
#include "ui/window_chrome.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rerevved::tools
{

namespace
{

constexpr std::array kReferenceWindows{ "Sources", "Preview", "Inspector" };
constexpr char       kUnsavedSheet[]  = "Save changes##sheet";
constexpr char       kRecoverySheet[] = "Restore map##sheet";
constexpr char       kAboutSheet[]    = "About##sheet";

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

constexpr ImGuiWindowFlags kFixedFlags  = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
constexpr ImGuiWindowFlags kStaticFlags = ImGuiWindowFlags_NoInputs | kFixedFlags;

constexpr float kMargin = 16.0F;
// Every page and tab shares this panel width, so switching never resizes it.
constexpr float kPanelWidth = 1240.0F;

constexpr std::array             kSteps{ MapStep::EditGrid, MapStep::Review };
const std::array<std::string, 2> kStepLabels{ "Grid", "Export" };
const std::array<std::string, 2> kStartLabels{ "Create", "Recent" };

struct Rect
{
    ImVec2 min;
    ImVec2 max;
};

bool beginPanel(const char* name, Rect rect, bool framed, ImVec2 padding, ImGuiWindowFlags extra = 0)
{
    ImGui::SetNextWindowPos(rect.min);
    ImGui::SetNextWindowSize(ImVec2(rect.max.x - rect.min.x, rect.max.y - rect.min.y));
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, framed ? 1.0F : 0.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, framed ? ui::color::Panel : IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::color::PanelBorder);
    const bool open = ImGui::Begin(name, nullptr, kPanelFlags | extra);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return open;
}

void drawBackdrop()
{
    const auto*     viewport = ImGui::GetMainViewport();
    auto*           drawList = ImGui::GetBackgroundDrawList(const_cast<ImGuiViewport*>(viewport));
    const auto      min      = viewport->Pos;
    const auto      max      = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    const auto      mid      = (min.y + max.y) * 0.5F;
    constexpr ImU32 kEdge    = IM_COL32(8, 22, 54, 255);
    constexpr ImU32 kCentre  = IM_COL32(22, 56, 116, 255);
    drawList->AddRectFilledMultiColor(min, ImVec2(max.x, mid), kEdge, kEdge, kCentre, kCentre);
    drawList->AddRectFilledMultiColor(ImVec2(min.x, mid), max, kCentre, kCentre, kEdge, kEdge);
    constexpr float kSpacing = 64.0F;
    for (float x = min.x + kSpacing; x < max.x; x += kSpacing)
    {
        drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(120, 170, 240, 10));
    }
    for (float y = min.y + kSpacing; y < max.y; y += kSpacing)
    {
        drawList->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), IM_COL32(120, 170, 240, 10));
    }
}

template <std::size_t N>
void copyText(std::array<char, N>& buffer, std::string_view text)
{
    buffer = {};
    std::memcpy(buffer.data(), text.data(), std::min(text.size(), N - 1));
}

MapPackUsage collectMapPackUsage(std::span<const MapDraftListing> maps)
{
    MapPackUsage                       usage;
    std::vector<std::filesystem::path> roots;
    for (const auto& listing : maps)
    {
        if (!listing.error.empty())
        {
            // An unreadable map may hold values this one would clash with.
            usage.complete = false;
            continue;
        }
        const auto& draft = listing.draft;
        for (const auto& entry : draft.scenario ? draft.scenario->entries : std::vector<ScenarioEntry>{})
        {
            usage.scenarioIds.push_back(entry.id);
            usage.mapNames.push_back(entry.map);
        }
        if (!draft.exportForm.id.empty())
        {
            usage.packIds.push_back(draft.exportForm.id);
        }
        usage.slots.push_back(static_cast<std::uint32_t>(draft.exportForm.slot));
        if (!draft.exportForm.outputParent.empty() && std::ranges::find(roots, draft.exportForm.outputParent) == roots.end())
        {
            roots.push_back(draft.exportForm.outputParent);
        }
    }
    for (const auto& root : roots)
    {
        AddMapPackUsage(usage, root);
    }
    return usage;
}

} // namespace

Application::Application()
{
    ui::ClearToasts();
}

Application::~Application()
{
    auto& io = ImGui::GetIO();
    if (io.IniFilename)
    {
        ImGui::SaveIniSettingsToDisk(io.IniFilename);
        // Context shutdown must not overwrite the final save after this handler is removed.
        io.IniFilename = nullptr;
    }
}

void Application::selectPage(Page target)
{
    page = target;
    creator.SetInspectionVisible(target == Page::Files);
    transitionFailure.reset();
}

bool Application::OpenPath(const std::filesystem::path& path, OpenOrigin origin)
{
    const bool opened = inspection.OpenPath(path, origin);
    if (opened && (origin == OpenOrigin::Drop || origin == OpenOrigin::CommandLine))
    {
        selectPage(Page::Files);
    }
    if (!opened)
    {
        inspection.NotifyOpenFailure(path);
    }
    return opened;
}

void Application::DropPaths(std::span<const std::filesystem::path> paths)
{
    for (const auto& path : paths)
    {
        static_cast<void>(OpenPath(path, OpenOrigin::Drop));
    }
}

void Application::requestDiscard()
{
    CancelMapEditorStroke(creator, mapEditor);
    transitionFailure.reset();
    creator.RequestDiscard();
    requestDiscardPopup = true;
}

void Application::cancelDiscard()
{
    creator.CancelDiscard();
    afterClose = AfterMapClose::Create;
    afterCloseFile.clear();
    requestDiscardPopup = false;
    transitionFailure.reset();
}

bool Application::confirmDiscard()
{
    auto       result    = creator.ConfirmDiscard();
    const bool confirmed = result.has_value();
    recordTransition(std::move(result));
    if (!confirmed)
    {
        return false;
    }
    removeRecovery();
    mapFile.clear();
    savedContent.clear();
    recentListed   = false;
    mapEditor      = MapEditorState{};
    scenarioEditor = ScenarioEditorState{};
    exportPanel    = ExportPanelState{};
    imagesFailedFor.reset();
    return true;
}

void Application::RequestClose()
{
    CancelMapEditorStroke(creator, mapEditor);
    closeMap(AfterMapClose::Exit);
}

void Application::WaitForMapImages(bool wait)
{
    waitForImages = wait;
}

void Application::SetDataFolder(std::filesystem::path folder, bool dialogs)
{
    dataFolder   = std::move(folder);
    fileDialogs  = dialogs;
    recentListed = false;
    recentMaps   = dataFolder.empty() ? std::vector<std::filesystem::path>{} : LoadRecentMaps(RecentMapsFile(dataFolder));
    recentListings.clear();
    recoveries = dataFolder.empty() ? std::vector<MapDraftListing>{} : ListMapDrafts(MapRecoveryFolder(dataFolder));
    std::erase_if(recoveries, [](const MapDraftListing& listing)
                  {
                      return !listing.error.empty();
                  });
}

SavedMapDraft Application::captureMapDraft() const
{
    const auto&   draft = creator.Map();
    SavedMapDraft saved;
    saved.identity  = draft.identity;
    saved.step      = draft.step == MapStep::Start ? MapStep::EditGrid : draft.step;
    saved.synthetic = mapEditor.model.Synthetic();
    if (const auto* source = mapEditor.model.Source())
    {
        saved.source = *source;
    }
    if (const auto* tiles = mapEditor.model.Document())
    {
        saved.tiles = *tiles;
    }
    saved.scenario     = draft.scenario;
    auto& form         = saved.exportForm;
    form.name          = exportPanel.packName.data();
    form.id            = exportPanel.packId.data();
    form.version       = exportPanel.packVersion.data();
    form.author        = exportPanel.packAuthor.data();
    form.description   = exportPanel.packDescription.data();
    form.idEdited      = exportPanel.packIdEdited;
    form.namePrefilled = exportPanel.namePrefilled;
    form.slot          = exportPanel.slot;
    form.outputParent  = exportPanel.outputParent;
    return saved;
}

std::string Application::mapContent() const
{
    auto saved = captureMapDraft();
    saved.step = MapStep::EditGrid;
    return EncodeMapDraft(saved);
}

bool Application::mapUnsaved() const
{
    return creator.Map().started && (mapFile.empty() || mapContent() != savedContent);
}

bool Application::saveMap(bool choosePath)
{
    static constexpr std::array<FileDialogFilter, 1> kFilters{ { { L"ReRevved maps", L"*.rrmap" } } };
    auto                                             target = mapFile;
    if (choosePath || target.empty())
    {
        const auto name   = MapFileName(creator.Map().identity);
        const auto chosen = fileDialogs ? SaveFileDialog(L"Save Map", kFilters, mapFile.empty() ? name : mapFile.parent_path() / name)
                                        : NativeDialogResult(std::unexpected(NativeDialogError::Unsupported));
        if (chosen && !*chosen)
        {
            return false;
        }
        if (chosen)
        {
            target = **chosen;
        }
        else if (chosen.error() == NativeDialogError::Unsupported && !dataFolder.empty())
        {
            target = dataFolder / "Maps" / name;
            std::error_code error;
            for (int copy = 2; target != mapFile && std::filesystem::exists(target, error) && copy < 1000; ++copy)
            {
                target = dataFolder / "Maps" / (PathToUtf8(name.stem()) + " " + std::to_string(copy) + std::string(kMapDraftExtension));
            }
        }
        else
        {
            ui::PushToast(ui::Tone::Error, chosen.error() == NativeDialogError::Unsupported ? "There is no folder to save the map in." : "The save dialog could not open.");
            return false;
        }
    }
    // A map kept with the recovery copies would be offered, and deleted, as one.
    std::error_code error;
    if (!dataFolder.empty() && std::filesystem::equivalent(target.parent_path(), MapRecoveryFolder(dataFolder), error))
    {
        ui::PushToast(
            ui::Tone::Error,
            "Tools keeps its recovery copies in that folder. Save the map somewhere else.");
        return false;
    }
    auto saved    = captureMapDraft();
    saved.savedAt = MapDraftClockNow();
    if (const auto written = SaveMapDraft(target, EncodeMapDraft(saved)); !written)
    {
        ui::PushToast(ui::Tone::Error, "The map was not saved. " + written.error());
        return false;
    }
    mapFile      = std::move(target);
    savedContent = mapContent();
    removeRecovery();
    rememberMap(mapFile);
    return true;
}

void Application::closeMap(AfterMapClose then, std::filesystem::path file)
{
    afterClose     = then;
    afterCloseFile = std::move(file);
    if (creator.PendingDiscard())
    {
        return;
    }
    if (!creator.Map().started)
    {
        finishMapClose();
        return;
    }
    if (mapUnsaved())
    {
        requestDiscard();
        return;
    }
    creator.RequestDiscard();
    if (confirmDiscard())
    {
        finishMapClose();
    }
    else
    {
        cancelDiscard();
    }
}

void Application::finishMapClose()
{
    const auto then = std::exchange(afterClose, AfterMapClose::Create);
    const auto file = std::exchange(afterCloseFile, {});
    switch (then)
    {
        case AfterMapClose::Create:
            mapEditor.startTab = 0;
            break;
        case AfterMapClose::Exit:
            shouldClose = true;
            break;
        case AfterMapClose::OpenDialog:
            openMapDialog();
            break;
        case AfterMapClose::OpenFile:
            openMap(file);
            break;
    }
}

void Application::openMapDialog()
{
    static constexpr std::array<FileDialogFilter, 1> kFilters{ { { L"ReRevved maps and map packs", L"*.rrmap;asset-pack.toml;dlc.fpk" } } };
    const auto                                       chosen = fileDialogs ? OpenFileDialog(L"Open Map", kFilters) : NativeDialogResult(std::unexpected(NativeDialogError::Unsupported));
    if (!chosen)
    {
        ui::PushToast(ui::Tone::Error,
                      chosen.error() == NativeDialogError::Unsupported
                          ? "There is no file dialog here. Open a map from the Recent tab."
                          : "The open dialog could not open.");
    }
    else if (*chosen)
    {
        openMap(**chosen);
    }
}

void Application::openMap(const std::filesystem::path& file)
{
    recentListed   = false;
    auto extension = PathToUtf8(file.extension());
    std::ranges::transform(extension, extension.begin(), [](char character)
                           {
                               return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
                           });
    const bool isMap = extension == kMapDraftExtension;
    auto       saved = isMap ? LoadMapDraft(file) : LoadMapPack(file);
    if (!saved)
    {
        ui::PushToast(ui::Tone::Error, PathToUtf8(file.filename()) + " could not be opened. " + saved.error());
        return;
    }
    saved->file.clear();
    if (!loadMap(std::move(*saved)))
    {
        return;
    }
    if (!isMap)
    {
        ui::PushToast(ui::Tone::Ok, "Opened the map from its pack. Save it to keep your changes as a map file.");
        return;
    }
    mapFile        = file;
    savedContent   = mapContent();
    snapshotOpened = true;
    rememberMap(file);
}

bool Application::loadMap(SavedMapDraft saved)
{
    if (!creator.StartMapDraft(saved.identity) || !mapEditor.model.StartSaved(saved.source, saved.tiles, saved.synthetic))
    {
        ui::PushToast(ui::Tone::Error, "Close the open map first.");
        return false;
    }
    static_cast<void>(mapEditor.model.SelectTile(0));
    static_cast<void>(creator.SelectMapTile(0));
    mapEditor.fit = true;
    auto scenario = saved.scenario.value_or(ScenarioDocument{});
    SingleMapScenario(scenario);
    static_cast<void>(creator.SetMapScenario(creator.Map().revision, std::move(scenario)));
    const auto& form = saved.exportForm;
    copyText(exportPanel.packName, form.name);
    copyText(exportPanel.packId, form.id);
    copyText(exportPanel.packVersion, form.version);
    copyText(exportPanel.packAuthor, form.author);
    copyText(exportPanel.packDescription, form.description);
    exportPanel.packIdEdited  = form.idEdited;
    exportPanel.namePrefilled = form.namePrefilled;
    exportPanel.slot          = std::clamp(form.slot, static_cast<std::int32_t>(kMapPackFirstSlot), static_cast<std::int32_t>(kMapPackLastSlot));
    exportPanel.outputParent  = form.outputParent;
    // Older files can name the Scenario step, which now lives on the Export tab, or an image tab, which is now the grid.
    const bool exporting = saved.step == MapStep::Export || saved.step == MapStep::Scenario;
    const auto step      = exporting ? MapStep::Review : std::ranges::find(kSteps, saved.step) != kSteps.end() ? saved.step
                                                                                                               : MapStep::EditGrid;
    if (!creator.SelectMapStep(step))
    {
        static_cast<void>(creator.SelectMapStep(MapStep::EditGrid));
    }
    return true;
}

void Application::rememberMap(const std::filesystem::path& file)
{
    RememberRecentMap(recentMaps, file);
    recentListed = false;
    if (dataFolder.empty())
    {
        return;
    }
    if (const auto written = SaveRecentMaps(RecentMapsFile(dataFolder), recentMaps); !written)
    {
        ui::PushToast(ui::Tone::Warn, "The recent map list was not saved. " + written.error());
    }
}

void Application::updateRecovery()
{
    if (dataFolder.empty() || !creator.Map().started || !mapEditor.model.Document())
    {
        return;
    }
    if (!mapUnsaved())
    {
        removeRecovery();
        return;
    }
    const double now = ImGui::GetTime();
    if (auto content = mapContent(); content != recoveryContent)
    {
        recoveryContent   = std::move(content);
        recoveryPending   = true;
        recoveryChangedAt = now;
    }
    // Wait for a pause so a brush stroke writes once, not once per tile.
    if (!recoveryPending || mapEditor.model.StrokeActive() || now - recoveryChangedAt < 1.0)
    {
        return;
    }
    auto saved    = captureMapDraft();
    saved.file    = mapFile;
    saved.savedAt = MapDraftClockNow();
    if (recoveryFile.empty())
    {
        recoveryFile = NewMapDraftPath(MapRecoveryFolder(dataFolder), saved.savedAt);
    }
    const auto written = recoveryFile.empty() ? std::unexpected(std::string("Could not check the recovery folder."))
                                              : SaveMapDraft(recoveryFile, EncodeMapDraft(saved));
    if (!written)
    {
        if (written.error() != recoveryError)
        {
            ui::PushToast(ui::Tone::Warn, "Unsaved changes could not be copied for recovery. " + written.error());
        }
        recoveryError     = written.error();
        recoveryChangedAt = now;
        return;
    }
    recoveryError.clear();
    recoveryPending = false;
}

void Application::removeRecovery()
{
    if (!recoveryFile.empty())
    {
        std::error_code error;
        std::filesystem::remove(recoveryFile, error);
        if (error)
        {
            ui::PushToast(ui::Tone::Warn, "The recovery copy " + PathToUtf8(recoveryFile) + " could not be deleted, so the next launch may offer it. " + error.message());
        }
    }
    recoveryFile.clear();
    recoveryContent.clear();
    recoveryError.clear();
    recoveryPending = false;
}

void Application::applyMapIdentity(const MapIdentity& identity)
{
    ScenarioEntry entry;
    entry.map = identity.mapName.empty() ? "NONE" : identity.mapName;
    entry.id  = identity.scenarioId;
    entry.title.fill(identity.title);
    static_cast<void>(creator.SetMapScenario(creator.Map().revision, ScenarioDocument{ { std::move(entry) } }));
    copyText(exportPanel.packName, identity.title);
    copyText(exportPanel.packId, identity.packId);
    exportPanel.namePrefilled = true;
    // A pack ID that had to avoid a used one stays put when the name changes.
    exportPanel.packIdEdited = identity.packIdSuffixed;
    if (identity.mapName.empty())
    {
        ui::PushToast(ui::Tone::Warn, "All 26 map letters are in use. Choose one under Advanced on the Export tab.");
    }
    if (identity.slot)
    {
        exportPanel.slot = static_cast<int>(*identity.slot);
    }
    else
    {
        ui::PushToast(ui::Tone::Warn, "All scenario file slots are in use. Choose one under Advanced on the Export tab.");
    }
}

void Application::repickForSaveFolder()
{
    const auto& draft = creator.Map();
    if (!draft.scenario || draft.scenario->entries.empty())
    {
        return;
    }
    MapPackUsage folder;
    AddMapPackUsage(folder, exportPanel.outputParent);
    const auto  everything = collectMapPackUsage(ListMapFiles(recentMaps));
    auto        scenario   = *draft.scenario;
    auto&       entry      = scenario.entries.front();
    MapIdentity current;
    current.title       = exportPanel.packName.data();
    current.packId      = exportPanel.packId.data();
    current.slot        = static_cast<std::uint32_t>(exportPanel.slot);
    current.scenarioId  = entry.id;
    current.mapName     = entry.map == "NONE" ? std::string() : entry.map;
    const auto repicked = RepickClashingIdentity(current, folder, everything);

    std::string changes;
    const auto  change = [&](const std::string& what, const std::string& from, const std::string& to)
    {
        changes += (changes.empty() ? "" : ", ") + what + " " + from + " is now " + to;
    };
    if (repicked.slot && repicked.slot != current.slot)
    {
        change("slot", std::to_string(*current.slot), std::to_string(*repicked.slot));
        exportPanel.slot = static_cast<int>(*repicked.slot);
    }
    bool moved = false;
    if (repicked.scenarioId != current.scenarioId)
    {
        change("scenario ID", std::to_string(current.scenarioId), std::to_string(repicked.scenarioId));
        entry.id = repicked.scenarioId;
        moved    = true;
    }
    if (!repicked.mapName.empty() && repicked.mapName != current.mapName)
    {
        change("map letter", current.mapName, repicked.mapName);
        entry.map = repicked.mapName;
        moved     = true;
    }
    if (moved)
    {
        static_cast<void>(creator.SetMapScenario(draft.revision, std::move(scenario)));
    }
    if (repicked.packId != current.packId)
    {
        change("pack ID", current.packId, repicked.packId);
        copyText(exportPanel.packId, repicked.packId);
        exportPanel.packIdEdited = true;
    }
    if (!changes.empty())
    {
        ui::PushToast(ui::Tone::Neutral, "This folder already uses some of these values: " + changes + ".");
    }
    if (!repicked.slot || (repicked.mapName.empty() && !current.mapName.empty()))
    {
        ui::PushToast(ui::Tone::Warn, "No free slot or map letter is left for this folder. Choose them under Advanced on the Export tab.");
    }
    if (!folder.complete || !everything.complete)
    {
        ui::PushToast(ui::Tone::Warn, "Some map packs or recent maps could not be read in full, so the slot, scenario ID, map letter or pack ID may clash with them.");
    }
}

void Application::recordTransition(CreatorTransition result)
{
    transitionFailure = result ? std::nullopt : std::optional(result.error());
}

bool Application::SetSmokeTestState(unsigned int frame)
{
    // Odd frames only draw, so each state has a frame to settle before its capture.
    // The last frame is odd because exit ends the run as soon as it is confirmed.
    if (frame % 2 != 0 && frame != 15)
    {
        return true;
    }
    switch (frame)
    {
        case 0:
            selectPage(Page::Map);
            return !creator.Map().started;
        case 2:
            selectPage(Page::Files);
            return creator.InspectionVisible();
        case 4:
        {
            selectPage(Page::Map);
            MapDocument document;
            for (std::size_t tile = 0; tile < document.core.size(); ++tile)
            {
                const auto row      = tile / 32;
                const auto column   = tile % 32;
                const auto terrain  = (column / 2 + row / 2) % 16;
                const auto flags    = (row % 4 == 0 ? 0x40U : 0U) | (column % 5 == 0 ? 0x80U : 0U);
                const auto marker   = tile == 70 || tile == 300 || tile == 590 ? 0x20U : 0U;
                document.core[tile] = static_cast<std::byte>(0x10U | marker | terrain | flags);
            }
            document.core[1023] = std::byte{ 0x30 };
            document.footer.fill(std::byte{ 0xA5 });
            if (!creator.StartMapDraft("Synthetic smoke MAP") || !mapEditor.model.StartImported(std::move(document)) || !mapEditor.model.BeginStroke({ MapBrush::TerrainCode, 7, true }) || !mapEditor.model.Paint(1023))
            {
                return false;
            }
            mapEditor.brush.tool = MapBrush::TerrainCode;
            return mapEditor.model.EndStroke() && mapEditor.model.SelectTile(1023).has_value() && creator.SelectMapTile(1023).has_value() && creator.ReviseMap().has_value() && creator.SelectMapStep(MapStep::EditGrid).has_value();
        }
        case 6:
        {
            ScenarioDocument document;
            document.entries.emplace_back();
            document.entries.front().title.fill("Synthetic smoke scenario");
            return creator.SetMapScenario(creator.Map().revision, std::move(document)).has_value() && creator.SelectMapStep(MapStep::Review).has_value();
        }
        case 8:
            syncMapImages();
            RefreshMapReadiness(creator, scenarioEditor, mapEditor.model.Document());
            return std::ranges::any_of(MapPackBlockers(creator.Map(), mapEditor.model.Document(), {}), [](const std::string& blocker)
                                       {
                                           return blocker.starts_with("Map Letter:");
                                       }) &&
                   creator.SelectMapStep(MapStep::Review).has_value();
        case 10:
            return !exportPanel.receipt;
        case 12:
        {
            const bool retained = creator.Map().step == MapStep::Review && creator.Map().readiness.ready < kMapArtifactCount && mapEditor.model.Source()->core[1023] == std::byte{ 0x30 } && mapEditor.model.Document()->core[1023] == std::byte{ 0x37 } && mapEditor.model.Document()->footer == mapEditor.model.Source()->footer;
            RequestClose();
            return retained && !ShouldClose() && creator.PendingDiscard();
        }
        case 14:
            cancelDiscard();
            if (ShouldClose() || creator.PendingDiscard() || !creator.Map().edited || !mapEditor.model.Document())
            {
                return false;
            }
            RequestClose();
            return !ShouldClose() && creator.PendingDiscard();
        case 15:
            if (!confirmDiscard())
            {
                return false;
            }
            RequestClose();
            return ShouldClose() && !creator.PendingDiscard() && !creator.Map().started && !mapEditor.model.Document() && !mapEditor.model.Source() && scenarioEditor.readiness.revision == 0 && !exportPanel.receipt && exportPanel.outputParent.empty();
        default:
            return false;
    }
}

bool Application::CheckSmokeTestRestart(bool referenceOpened) const
{
    return page == (referenceOpened ? Page::Files : Page::Map) && !creator.Map().started;
}

void Application::refreshProtectedPaths()
{
    // Reapplied every frame so reference changes and discard resets cannot leave stale protection.
    std::vector<std::filesystem::path> protectedPaths;
    for (const auto& document : inspection.Workspace().Documents())
    {
        if (!document.sourcePath.empty())
        {
            protectedPaths.push_back(document.sourcePath);
        }
    }
    if (auto root = inspection.SourcesRoot())
    {
        protectedPaths.push_back(std::move(*root));
    }
    SetProtectedPaths(exportPanel, std::move(protectedPaths));
}

void Application::Draw()
{
    handleShortcuts();

    // Reset discarded preview resources before any page submits draw commands.
    drawUnsavedPopup();
    drawRecoveryPopup();
    refreshProtectedPaths();

    drawBackdrop();
    drawShell();
    if (mapEditor.lastDrawFrame != ImGui::GetFrameCount())
    {
        CancelMapEditorStroke(creator, mapEditor);
    }
    inspection.Draw(page == Page::Files);
    creator.SetInspection(inspection.Selection());
    drawAboutPopup();
    // Sync before taking the saved snapshot so opening a map does not mark it as unsaved.
    SyncPackWithScenario(exportPanel, creator.Map());
    if (std::exchange(snapshotOpened, false) && creator.Map().started)
    {
        savedContent = mapContent();
    }
    updateRecovery();
    ui::DrawToasts();
}

bool Application::ShouldClose() const
{
    return shouldClose;
}

void Application::handleShortcuts()
{
    const auto& io = ImGui::GetIO();
    // ImGui closes a menu on Esc before this runs, and that press must not open the menu again.
    const bool popupOpen    = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool popupWasOpen = std::exchange(popupOpenLastFrame, popupOpen);
    if (popupOpen)
    {
        return;
    }
    const bool typing  = io.WantTextInput || ImGui::IsAnyItemActive();
    const bool started = creator.Map().started;
    if (io.KeyCtrl)
    {
        if (typing)
        {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_1, false))
        {
            selectPage(Page::Map);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_2, false))
        {
            selectPage(Page::Files);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O, false))
        {
            if (page == Page::Files)
            {
                inspection.RequestOpen();
            }
            else
            {
                closeMap(AfterMapClose::OpenDialog);
            }
        }
        if (page == Page::Map && started && ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            static_cast<void>(saveMap(io.KeyShift));
        }
        if (page == Page::Files && ImGui::IsKeyPressed(ImGuiKey_W, false))
        {
            if (const auto* active = inspection.Workspace().Active())
            {
                inspection.CloseDocument(active->id);
            }
        }
        if (page == Page::Map && started && mapStepIndex() == 0)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                UndoMapEdit(creator, mapEditor);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            {
                RedoMapEdit(creator, mapEditor);
            }
        }
        return;
    }
    if (typing)
    {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !popupWasOpen)
    {
        requestMenu = true;
    }
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enter && !ImGui::GetCurrentContext()->NavCursorVisible && page == Page::Map)
    {
        if (!started && mapEditor.startTab == 0)
        {
            mapEditor.requestCreate = mapEditor.canCreate;
        }
        else if (started && mapStepIndex() == static_cast<int>(kSteps.size()) - 1)
        {
            exportPanel.requestExport = exportPanel.canExport;
        }
    }
}

int Application::mapStepIndex() const
{
    return static_cast<int>(std::ranges::find(kSteps, creator.Map().step) - kSteps.begin()) % static_cast<int>(kSteps.size());
}

void Application::selectMapStep(int index)
{
    // A visible cursor on the old tab's focused item would keep Enter from the new one.
    ImGui::SetNavCursorVisible(false);
    recordTransition(creator.SelectMapStep(kSteps[static_cast<std::size_t>(index)]));
}

std::string Application::title() const
{
    if (page == Page::Files)
    {
        const auto* active = inspection.Workspace().Active();
        return active ? SourceLabel(*active) : std::string();
    }
    if (!creator.Map().started)
    {
        return {};
    }
    const auto name = mapFile.empty() ? creator.Map().identity : PathToUtf8(mapFile.stem());
    return mapUnsaved() ? name + " *" : name;
}

std::pair<std::string, std::string> Application::helpText() const
{
    if (page == Page::Files)
    {
        return { inspection.Workspace().Active() ? "Pick an entry to preview it. Extracting writes a new file and never changes the archive."
                                                 : "Open a game file or browse a folder to look inside it. Drop files anywhere in the window.",
                 {} };
    }
    const auto& draft = creator.Map();
    if (!draft.started)
    {
        switch (mapEditor.startTab)
        {
            case 0:
                return { "Name the map, pick a starting land, then press Create Map.", mapEditor.createProblem };
            default:
                return { "Map files you open or save appear here. Open Map finds any other, or an exported map pack.", {} };
        }
    }
    switch (mapStepIndex())
    {
        case 0:
            return { "Pick a terrain, then click or drag on the grid. Arrow keys move the selected tile.", {} };
        default:
            return { "Pick ReRevved's maps folder, next to rerevved.exe, to install the pack as you export. ReRevved loads packs listed in map_order.txt after a restart.", {} };
    }
}

void Application::drawShell()
{
    const auto* viewport = ImGui::GetMainViewport();
    const float width    = std::min(viewport->WorkSize.x - 2.0F * kMargin, kPanelWidth);
    panelLeft            = viewport->WorkPos.x + std::floor((viewport->WorkSize.x - width) * 0.5F);
    panelRight           = panelLeft + width;

    const auto [line, note]  = helpText();
    const float helpHeight   = ImGui::GetTextLineHeight() * 2.0F + 2.0F + 20.0F;
    const float titleHeight  = 48.0F;
    const float tabsHeight   = 82.0F;
    const float footerHeight = 60.0F;
    const float gap          = 16.0F;
    const float top          = viewport->WorkPos.y;
    const float bottom       = viewport->WorkPos.y + viewport->WorkSize.y;

    drawTitleBand(top, titleHeight);
    drawTabBand(top + titleHeight, tabsHeight);
    drawPanel(top + titleHeight + tabsHeight + gap, bottom - footerHeight - helpHeight - gap);
    drawHelpStrip(bottom - footerHeight - helpHeight, helpHeight, line, note);
    drawFooter(bottom - footerHeight, footerHeight);
}

void Application::drawTitleBand(float top, float height)
{
    const auto* viewport = ImGui::GetMainViewport();
    const Rect  rect{ ImVec2(viewport->WorkPos.x, top), ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, top + height) };
    // The window buttons take clicks. Dragging is handled by the system before ImGui sees it.
    if (beginPanel("##TitleBand", rect, false, ImVec2(0.0F, 0.0F), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(rect.min, rect.max, ui::color::TitleBand);
        drawList->AddLine(ImVec2(rect.min.x, rect.max.y - 1.0F), ImVec2(rect.max.x, rect.max.y - 1.0F), IM_COL32(36, 72, 154, 255));
        constexpr float kMarkHeight = 24.0F;
        const ImVec2    brandAt(rect.min.x + 14.0F, rect.min.y + (height - kMarkHeight) * 0.5F);
        float           brandRight = brandAt.x + wordmark.Draw(*drawList, brandAt, kMarkHeight);
        if (brandRight == brandAt.x)
        {
            ui::PushFont(ui::FontRole::Caption);
            drawList->AddText(ImVec2(brandAt.x, rect.min.y + (height - ImGui::GetFontSize()) * 0.5F), IM_COL32(169, 195, 238, 255), "ReRevved Tools");
            brandRight += ImGui::CalcTextSize("ReRevved Tools").x;
            ui::PopFont();
        }
        float buttonsLeft = rect.max.x;
        if (ChromeActive())
        {
            constexpr float kButtonWidth = 46.0F;
            buttonsLeft                  = rect.max.x - 3.0F * kButtonWidth;
            const auto button            = [&](const char* id, int index, ImU32 hoverFill)
            {
                const ImVec2 minimum(buttonsLeft + static_cast<float>(index) * kButtonWidth, rect.min.y);
                const ImVec2 maximum(minimum.x + kButtonWidth, rect.max.y - 1.0F);
                ImGui::SetCursorScreenPos(minimum);
                const bool clicked = ImGui::InvisibleButton(id, ImVec2(kButtonWidth, maximum.y - minimum.y));
                if (ImGui::IsItemHovered())
                {
                    drawList->AddRectFilled(minimum, maximum, ImGui::IsItemActive() ? (hoverFill & 0x00FFFFFFU) | 0xB0000000U : hoverFill);
                }
                return std::pair{ clicked, ImVec2(std::floor((minimum.x + maximum.x) * 0.5F) + 0.5F, std::floor((minimum.y + maximum.y) * 0.5F) + 0.5F) };
            };
            constexpr ImU32 kGlyph = IM_COL32(219, 230, 250, 255);
            constexpr ImU32 kHover = IM_COL32(255, 255, 255, 28);
            {
                const auto [clicked, centre] = button("##window-minimize", 0, kHover);
                drawList->AddLine(ImVec2(centre.x - 5.0F, centre.y), ImVec2(centre.x + 5.0F, centre.y), kGlyph);
                if (clicked)
                {
                    MinimizeWindow();
                }
            }
            {
                const auto [clicked, centre] = button("##window-maximize", 1, kHover);
                if (WindowMaximized())
                {
                    drawList->AddRect(ImVec2(centre.x - 5.0F, centre.y - 3.0F), ImVec2(centre.x + 3.0F, centre.y + 5.0F), kGlyph);
                    drawList->AddLine(ImVec2(centre.x - 3.0F, centre.y - 5.0F), ImVec2(centre.x + 5.0F, centre.y - 5.0F), kGlyph);
                    drawList->AddLine(ImVec2(centre.x + 5.0F, centre.y - 5.0F), ImVec2(centre.x + 5.0F, centre.y + 3.0F), kGlyph);
                }
                else
                {
                    drawList->AddRect(ImVec2(centre.x - 5.0F, centre.y - 5.0F), ImVec2(centre.x + 5.0F, centre.y + 5.0F), kGlyph);
                }
                if (clicked)
                {
                    ToggleMaximizeWindow();
                }
            }
            {
                const auto [clicked, centre] = button("##window-close", 2, IM_COL32(196, 43, 28, 255));
                drawList->AddLine(ImVec2(centre.x - 5.0F, centre.y - 5.0F), ImVec2(centre.x + 5.0F, centre.y + 5.0F), kGlyph);
                drawList->AddLine(ImVec2(centre.x + 5.0F, centre.y - 5.0F), ImVec2(centre.x - 5.0F, centre.y + 5.0F), kGlyph);
                if (clicked)
                {
                    RequestWindowClose();
                }
            }
            SetChromeDragArea(rect.max.y - viewport->Pos.y, buttonsLeft - viewport->Pos.x, ImGui::GetIO().DisplayFramebufferScale.x);
        }
        const auto text = title();
        ui::PushFont(ui::FontRole::Title);
        const auto size = ImGui::CalcTextSize(text.c_str());
        drawList->PushClipRect(rect.min, ImVec2(buttonsLeft - 8.0F, rect.max.y), true);
        drawList->AddText(ImVec2(std::max(brandRight + 24.0F, rect.min.x + (rect.max.x - rect.min.x - size.x) * 0.5F), rect.min.y + (height - size.y) * 0.5F), ui::color::Text, text.c_str());
        drawList->PopClipRect();
        ui::PopFont();
    }
    ImGui::End();
}

void Application::drawTabBand(float top, float height)
{
    const auto* viewport = ImGui::GetMainViewport();
    const Rect  rect{ ImVec2(viewport->WorkPos.x, top), ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, top + height) };
    if (beginPanel("##TabBand", rect, false, ImVec2(0.0F, 0.0F), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilledMultiColor(rect.min, rect.max, ui::color::TabBandTop, ui::color::TabBandTop, ui::color::TabBandFoot, ui::color::TabBandFoot);
        drawList->AddLine(ImVec2(rect.min.x, rect.max.y - 1.0F), ImVec2(rect.max.x, rect.max.y - 1.0F), IM_COL32(10, 28, 80, 255));
        const float width = rect.max.x - rect.min.x;

        static const std::array<std::string, 2> kPages{ "Map", "Files" };
        int                                     selectedPage = page == Page::Map ? 0 : 1;
        const float                             pagesTop     = top + 8.0F;
        ImGui::SetCursorScreenPos(ImVec2(rect.min.x + std::max(0.0F, (width - ui::TextTabsWidth(kPages, true)) * 0.5F), pagesTop));
        if (ui::TextTabs("pages", kPages, selectedPage, true))
        {
            selectPage(selectedPage == 0 ? Page::Map : Page::Files);
        }

        // Files leaves the second row empty, so the panel never moves between pages.
        if (page == Page::Map)
        {
            const bool started  = creator.Map().started;
            int        selected = started ? mapStepIndex() : mapEditor.startTab;
            const auto labels   = started ? std::span<const std::string>(kStepLabels) : std::span<const std::string>(kStartLabels);
            ImGui::SetCursorScreenPos(ImVec2(rect.min.x + std::max(0.0F, (width - ui::TextTabsWidth(labels, false)) * 0.5F), pagesTop + 33.0F));
            if (ui::TextTabs("steps", labels, selected, false))
            {
                if (started)
                {
                    selectMapStep(selected);
                }
                else
                {
                    mapEditor.startTab = selected;
                }
            }
        }
    }
    ImGui::End();
}

void Application::drawPanel(float top, float bottom)
{
    const Rect rect{ ImVec2(panelLeft, top), ImVec2(panelRight, std::max(top + 40.0F, bottom)) };
    if (beginPanel("##Canvas", rect, true, ImVec2(0.0F, 0.0F), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (page == Page::Files)
        {
            drawFilesPage();
        }
        else
        {
            // The Grid tab fits the panel by design. A pixel of rounding must not let it scroll
            // and clip the toolbar.
            const bool             fits  = creator.Map().started && creator.Map().step == MapStep::EditGrid;
            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground | (fits ? kFixedFlags : 0);
            if (fits)
            {
                ImGui::SetNextWindowScroll(ImVec2(0.0F, 0.0F));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0F, 14.0F));
            const bool open = ImGui::BeginChild("##panel-body", ImVec2(0.0F, 0.0F), ImGuiChildFlags_AlwaysUseWindowPadding, flags);
            ImGui::PopStyleVar();
            if (open)
            {
                drawMapPage();
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
    // A request left over would fire after a later tab change.
    mapEditor.requestCreate   = false;
    exportPanel.requestExport = false;
}

void Application::drawHelpStrip(float top, float height, const std::string& line, const std::string& note)
{
    if (height <= 0.0F)
    {
        return;
    }
    const auto* viewport = ImGui::GetMainViewport();
    const Rect  rect{ ImVec2(viewport->WorkPos.x, top), ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, top + height) };
    if (beginPanel("##HelpStrip", rect, false, ImVec2(0.0F, 0.0F), kStaticFlags))
    {
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(rect.min, rect.max, ui::color::HelpBand);
        drawList->AddLine(rect.min, ImVec2(rect.max.x, rect.min.y), IM_COL32(42, 82, 164, 255));
        const float wrap = panelRight - panelLeft - 24.0F;
        const auto  at   = ImVec2(panelLeft + 12.0F, top + 10.0F);
        drawList->PushClipRect(rect.min, ImVec2(rect.max.x, rect.max.y - 6.0F), true);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, IM_COL32(228, 236, 251, 255), line.c_str(), nullptr, wrap);
        if (!note.empty())
        {
            const float below = ImGui::CalcTextSize(line.c_str(), nullptr, false, wrap).y + 2.0F;
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x, at.y + below), ui::color::Highlight, note.c_str(), nullptr, wrap);
        }
        drawList->PopClipRect();
    }
    ImGui::End();
}

void Application::drawFooter(float top, float height)
{
    const auto* viewport = ImGui::GetMainViewport();
    const Rect  rect{ ImVec2(viewport->WorkPos.x, top), ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, top + height) };
    if (beginPanel("##Footer", rect, false, ImVec2(0.0F, 0.0F), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::GetWindowDrawList()->AddRectFilled(rect.min, rect.max, ui::color::PromptBand);
        const float y = top + std::floor((height - 40.0F) * 0.5F);
        ImGui::SetCursorScreenPos(ImVec2(rect.min.x + kMargin, y));
        if (ui::PromptButton("Menu##menu", "Esc"))
        {
            requestMenu = true;
        }
        if (std::exchange(requestMenu, false))
        {
            ImGui::OpenPopup("##AppMenu");
        }
        // Only an open menu takes the position. A closed one would pass it to the next window.
        if (ImGui::IsPopupOpen("##AppMenu"))
        {
            ImGui::SetNextWindowPos(ImVec2(rect.min.x + kMargin, y - 6.0F), ImGuiCond_Appearing, ImVec2(0.0F, 1.0F));
        }
        drawAppMenu();

        struct Prompt
        {
            const char*           label;
            std::string_view      key;
            bool                  primary = false;
            bool                  enabled = true;
            std::string           reason;
            std::function<void()> action;
        };

        std::vector<Prompt> prompts;
        const auto&         draft = creator.Map();
        if (page == Page::Files)
        {
            if (const auto* active = inspection.Workspace().Active())
            {
                prompts.push_back({ "Close File", "Ctrl+W", false, true, {}, [this, id = active->id]
                                    {
                                        inspection.CloseDocument(id);
                                    } });
            }
            prompts.push_back({ "Browse Folder", {}, false, true, {}, [this]
                                {
                                    inspection.RequestBrowse();
                                } });
            prompts.push_back({ "Open File", "Ctrl+O", true, true, {}, [this]
                                {
                                    inspection.RequestOpen();
                                } });
        }
        else if (!draft.started)
        {
            if (mapEditor.startTab == 0)
            {
                prompts.push_back({ "Create Map", "Enter", true, mapEditor.canCreate, mapEditor.createProblem, [this]
                                    {
                                        mapEditor.requestCreate = true;
                                    } });
            }
            else
            {
                prompts.push_back({ "Open Map", "Ctrl+O", true, true, {}, [this]
                                    {
                                        openMapDialog();
                                    } });
            }
        }
        else if (const int step = mapStepIndex(); step == 0)
        {
            prompts.push_back({ "Undo", "Ctrl+Z", false, mapEditor.model.CanUndo(), {}, [this]
                                {
                                    UndoMapEdit(creator, mapEditor);
                                } });
            prompts.push_back({ "Redo", "Ctrl+Y", false, mapEditor.model.CanRedo(), {}, [this]
                                {
                                    RedoMapEdit(creator, mapEditor);
                                } });
        }
        else
        {
            prompts.push_back({ "Export Map Pack", "Enter", true, exportPanel.canExport, exportPanel.exportBlocker, [this]
                                {
                                    exportPanel.requestExport = true;
                                } });
        }
        const auto totalWidth = [&prompts]
        {
            float sum = 0.0F;
            for (const auto& prompt : prompts)
            {
                sum += ui::PromptButtonWidth(prompt.label, prompt.key, prompt.primary) + 10.0F;
            }
            return sum;
        };
        const float room  = rect.max.x - rect.min.x - 2.0F * kMargin - ui::PromptButtonWidth("Menu##menu", "Esc") - 10.0F;
        float       total = totalWidth();
        while (!prompts.empty() && total - 10.0F > room)
        {
            prompts.erase(prompts.begin());
            total = totalWidth();
        }
        float x = rect.max.x - kMargin - std::max(0.0F, total - 10.0F);
        for (const auto& prompt : prompts)
        {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            if (ui::PromptButton(prompt.label, prompt.key, prompt.primary, prompt.enabled, prompt.reason))
            {
                prompt.action();
            }
            x += ui::PromptButtonWidth(prompt.label, prompt.key, prompt.primary) + 10.0F;
        }
    }
    ImGui::End();
}

void Application::drawAppMenu()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F, 8.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0F, 6.0F));
    if (ImGui::BeginPopup("##AppMenu"))
    {
        // Everything else has a prompt or a tab of its own.
        if (page == Page::Map && creator.Map().started && !creator.PendingDiscard())
        {
            if (ImGui::MenuItem("New Map"))
            {
                closeMap(AfterMapClose::Create);
            }
            if (ImGui::MenuItem("Open Map...", "Ctrl+O"))
            {
                closeMap(AfterMapClose::OpenDialog);
            }
            if (ImGui::MenuItem("Save", "Ctrl+S"))
            {
                static_cast<void>(saveMap(false));
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
            {
                static_cast<void>(saveMap(true));
            }
            ImGui::Separator();
        }
        if (ImGui::MenuItem("About ReRevved Tools"))
        {
            requestAboutPopup = true;
        }
        if (ImGui::MenuItem("Exit"))
        {
            RequestClose();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

void Application::drawMapPage()
{
    const auto& draft = creator.Map();
    if (!draft.started)
    {
        // Packs and maps change outside Tools too, so the page rereads them every few seconds.
        constexpr auto kRelist = std::chrono::seconds(5);
        const auto     relist  = [this]
        {
            if (!dataFolder.empty())
            {
                recentMaps = LoadRecentMaps(RecentMapsFile(dataFolder));
            }
            recentListings = ListMapFiles(recentMaps);
            mapPackUsage   = collectMapPackUsage(recentListings);
            recentListedAt = std::chrono::steady_clock::now();
        };
        if (!recentListed || std::chrono::steady_clock::now() - recentListedAt > kRelist)
        {
            relist();
            recentListed = true;
            mapEditor.identityName.reset();
        }
        const auto choice = DrawMapStart(creator, mapEditor, recentListings, mapPackUsage);
        if (choice.open)
        {
            openMap(*choice.open);
        }
        else if (choice.started)
        {
            relist();
            applyMapIdentity(SuggestMapIdentity(choice.started->title, mapPackUsage));
            if (!mapPackUsage.complete)
            {
                ui::PushToast(ui::Tone::Warn, "Some map packs or recent maps could not be read in full, so the slot, scenario ID, map letter or pack ID may clash with them.");
            }
        }
        return;
    }
    syncMapImages();
    RefreshMapReadiness(creator, scenarioEditor, mapEditor.model.Document());
    if (transitionFailure)
    {
        for (const auto prerequisite : transitionFailure->missing)
        {
            ui::TonedText(ui::Tone::Warn, CreatorPrerequisiteMessage(prerequisite));
        }
    }
    switch (mapStepIndex())
    {
        case 0:
            drawGridView();
            break;
        default:
            DrawExportPanel(creator, exportPanel, scenarioEditor, inspection.Workspace(), mapEditor.model.Document(), fileDialogs);
            if (std::exchange(exportPanel.folderPicked, false))
            {
                repickForSaveFolder();
            }
            break;
    }
}

void Application::syncMapImages()
{
    const auto* tiles = mapEditor.model.Document();
    const auto& draft = creator.Map();
    if (waitForImages && imageJob.valid())
    {
        imageJob.wait();
    }
    if (imageJob.valid() && imageJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        auto result = imageJob.get();
        if (tiles != nullptr && tiles->core == imageJobCore)
        {
            if (result)
            {
                ApplyMapImages(creator, std::move(*result));
            }
            else
            {
                // Earlier grid images must not export with this one. The next edit retries.
                static_cast<void>(creator.SetMapHeights(creator.Map().revision, std::nullopt));
                for (const auto role : { TerrainTexture::MountainHillBlends, TerrainTexture::Lightmap })
                {
                    static_cast<void>(creator.SetMapTerrain(creator.Map().revision, role, std::nullopt));
                }
                imagesFailedFor = tiles->core;
                ui::PushToast(ui::Tone::Error, result.error());
            }
        }
    }
    if (tiles == nullptr || mapEditor.model.StrokeActive())
    {
        return;
    }
    const auto current = [tiles](const auto& image)
    {
        return image && image->generatedFrom == tiles->core;
    };
    if (current(draft.heights) && std::ranges::all_of(draft.terrain, current))
    {
        return;
    }
    // Generating takes a moment, so the grid keeps its images until another tab opens.
    const bool missing = !draft.heights || std::ranges::any_of(draft.terrain, [](const auto& image)
                                                               {
                                                                   return !image.has_value();
                                                               });
    if ((!missing && mapStepIndex() == 0) || imagesFailedFor == tiles->core || imageJob.valid())
    {
        return;
    }
    imageJobCore = tiles->core;
    imageJob     = std::async(std::launch::async, [snapshot = *tiles]
                              {
                              return BuildMapImages(snapshot);
                              });
}

void Application::drawGridView()
{
    const auto      available = ImGui::GetContentRegionAvail();
    constexpr float kGap      = 20.0F;
    ImGui::SetNextWindowScroll(ImVec2(0.0F, 0.0F));
    if (ImGui::BeginChild("##grid-column", ImVec2(available.x - ui::size::TileDetails - kGap, available.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | kFixedFlags))
    {
        DrawMapGrid(creator, mapEditor);
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0F, kGap);
    ImGui::SetNextWindowScroll(ImVec2(0.0F, 0.0F));
    if (ImGui::BeginChild("##tile-column", ImVec2(0.0F, available.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | kFixedFlags))
    {
        DrawMapTileInspector(mapEditor);
    }
    ImGui::EndChild();
}

void Application::drawFilesPage()
{
    // Saved layouts use this ID. Layouts saved with the former four panes use another, so they do not apply.
    const ImGuiID dockspace = ImGui::GetID("InspectPanes");
    const auto    size      = ImGui::GetContentRegionAvail();
    if (!ImGui::DockBuilderGetNode(dockspace))
    {
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, size);
        auto       center = dockspace;
        const auto left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22F, nullptr, &center);
        const auto right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.32F, nullptr, &center);
        ImGui::DockBuilderDockWindow("Sources", left);
        ImGui::DockBuilderDockWindow("Preview", center);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderFinish(dockspace);
    }
    else
    {
        for (const auto* name : kReferenceWindows)
        {
            const auto* window = ImGui::FindWindowByName(name);
            if (window && window->DockId == 0 && !window->Hidden)
            {
                ImGui::DockBuilderDockWindow(name, dockspace);
            }
        }
    }
    // Panes have no tab bars and cannot be undocked, so the layout needs no reset.
    ImGui::DockSpace(dockspace, size, static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoTabBar) | ImGuiDockNodeFlags_NoUndocking);
}

void Application::drawAboutPopup()
{
    if (std::exchange(requestAboutPopup, false))
    {
        ImGui::OpenPopup(kAboutSheet);
    }
    if (!ui::BeginSheet(kAboutSheet, ui::icon::Layers, "ReRevved Tools", 460.0F))
    {
        return;
    }
    ui::Muted(std::string("Version ") + REREVVED_TOOLS_VERSION);
    ui::Body("A content creation companion for ReRevved.");
    ui::Caption("Free software under GPLv3, with absolutely no warranty. Retail game content is not included. Inter and Lucide are bundled under their own licences.");
    ui::SheetFooter(1);
    if (ui::SheetButton("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        ImGui::CloseCurrentPopup();
    }
    ui::EndSheet();
}

void Application::drawUnsavedPopup()
{
    if (std::exchange(requestDiscardPopup, false))
    {
        ImGui::OpenPopup(kUnsavedSheet);
    }
    if (!ui::BeginSheet(kUnsavedSheet, ui::icon::File, "Save Changes?", 480.0F))
    {
        return;
    }
    if (!creator.PendingDiscard())
    {
        ImGui::CloseCurrentPopup();
        ui::EndSheet();
        return;
    }
    const auto name = mapFile.empty() ? creator.Map().identity : PathToUtf8(mapFile.filename());
    ui::Body("Save the changes to " + name + (afterClose == AfterMapClose::Exit ? " before exiting?" : " before closing it?"));
    ui::Caption("Changes you do not save are lost.");
    if (transitionFailure)
    {
        for (const auto prerequisite : transitionFailure->missing)
        {
            ui::TonedText(ui::Tone::Warn, CreatorPrerequisiteMessage(prerequisite));
        }
    }
    ui::SheetFooter(3);
    bool closed = false;
    // Background image generation changes the revision. Confirm the revision at the time of the click.
    if (ui::SheetButton("Save##save-changes", true) && saveMap(false) && (creator.RequestDiscard(), confirmDiscard()))
    {
        closed = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::SheetButton("Don't Save") && (creator.RequestDiscard(), confirmDiscard()))
    {
        closed = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::SheetButton("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        cancelDiscard();
        ImGui::CloseCurrentPopup();
    }
    ui::EndSheet();
    if (closed)
    {
        finishMapClose();
    }
}

void Application::drawRecoveryPopup()
{
    if (recoveries.empty() || creator.Map().started)
    {
        return;
    }
    if (!ImGui::IsPopupOpen(kRecoverySheet) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        ImGui::OpenPopup(kRecoverySheet);
    }
    if (!ui::BeginSheet(kRecoverySheet, ui::icon::Clock, "Restore Unsaved Map?", 480.0F))
    {
        return;
    }
    const auto& recovery = recoveries.front();
    ui::Body("Tools closed before the changes to " + recovery.draft.identity + " were saved. Restore them?");
    ui::Caption(recovery.draft.file.empty() ? std::string("The map was never saved.") : "Its file is " + PathToUtf8(recovery.draft.file) + ".");
    ui::SheetFooter(2);
    if (ui::SheetButton("Restore", true))
    {
        selectPage(Page::Map);
        if (loadMap(recovery.draft))
        {
            mapFile = recovery.draft.file;
            savedContent.clear();
            recoveryFile    = recovery.path;
            recoveryContent = mapContent();
        }
        recoveries.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::SheetButton("Discard##discard-recovery"))
    {
        std::error_code error;
        std::filesystem::remove(recovery.path, error);
        recoveries.erase(recoveries.begin());
        ImGui::CloseCurrentPopup();
    }
    ui::EndSheet();
}

} // namespace rerevved::tools
