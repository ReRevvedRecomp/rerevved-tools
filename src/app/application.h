#pragma once

#include "inspect/inspection_pane.h"
#include "map/creator_workspace.h"
#include "map/export_panel.h"
#include "map/map_draft_store.h"
#include "map/map_editor.h"
#include "map/map_images.h"
#include "map/scenario_editor.h"
#include "ui/wordmark.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rerevved::tools
{

enum class Page
{
    Map,
    Files,
};

enum class AfterMapClose
{
    Create, // the start page's Create tab
    Exit,
    OpenDialog,
    OpenFile,
};

class Application
{
public:
    Application();
    ~Application();
    [[nodiscard]] bool OpenPath(const std::filesystem::path& path, OpenOrigin origin = OpenOrigin::TypedPath);
    void               DropPaths(std::span<const std::filesystem::path> paths);
    void               RequestClose();
    // Recovery copies and the recent map list live in folder. Empty turns both off.
    // Without dialogs, as in smoke runs, Save As writes into folder instead of asking.
    void               SetDataFolder(std::filesystem::path folder, bool dialogs = true);
    void               Draw();
    [[nodiscard]] bool SetSmokeTestState(unsigned int frame);
    [[nodiscard]] bool CheckSmokeTestRestart(bool referenceOpened) const;
    [[nodiscard]] bool ShouldClose() const;
    // Makes each frame wait for a running map image job, so scripted runs need no timing.
    void WaitForMapImages(bool wait);

private:
    void                                              selectPage(Page target);
    void                                              handleShortcuts();
    [[nodiscard]] int                                 mapStepIndex() const;
    void                                              selectMapStep(int index);
    [[nodiscard]] std::string                         title() const;
    [[nodiscard]] std::pair<std::string, std::string> helpText() const;
    void                                              drawShell();
    void                                              drawTitleBand(float top, float height);
    void                                              drawTabBand(float top, float height);
    void                                              drawPanel(float top, float bottom);
    void                                              drawHelpStrip(float top, float height, const std::string& line, const std::string& note);
    void                                              drawFooter(float top, float height);
    void                                              drawAppMenu();
    void                                              drawMapPage();
    void                                              drawGridView();
    // Generate missing images in the background, or stale ones after leaving the grid.
    // Accept the result only if its tiles still match.
    void                        syncMapImages();
    void                        drawFilesPage();
    void                        drawAboutPopup();
    void                        drawUnsavedPopup();
    void                        drawRecoveryPopup();
    void                        requestDiscard();
    void                        cancelDiscard();
    [[nodiscard]] bool          confirmDiscard();
    void                        recordTransition(CreatorTransition result);
    void                        refreshProtectedPaths();
    [[nodiscard]] SavedMapDraft captureMapDraft() const;
    // Saved content without the save time or open tab, for detecting unsaved changes.
    [[nodiscard]] std::string mapContent() const;
    [[nodiscard]] bool        mapUnsaved() const;
    // Asks for a file when the map has none or choosePath is set. True when saved.
    bool               saveMap(bool choosePath);
    void               closeMap(AfterMapClose then, std::filesystem::path file = {});
    void               finishMapClose();
    void               openMapDialog();
    void               openMap(const std::filesystem::path& file);
    [[nodiscard]] bool loadMap(SavedMapDraft saved);
    void               rememberMap(const std::filesystem::path& file);
    void               updateRecovery();
    void               removeRecovery();
    void               applyMapIdentity(const MapIdentity& identity);
    void               repickForSaveFolder();

    CreatorWorkspace                        creator;
    InspectionPane                          inspection;
    Wordmark                                wordmark;
    MapEditorState                          mapEditor;
    ScenarioEditorState                     scenarioEditor;
    ExportPanelState                        exportPanel;
    std::optional<CreatorTransitionFailure> transitionFailure;
    std::filesystem::path                   dataFolder;
    bool                                    fileDialogs = true;
    std::vector<std::filesystem::path>      recentMaps;
    std::vector<MapDraftListing>            recentListings;
    MapPackUsage                            mapPackUsage;
    bool                                    recentListed = false;
    std::chrono::steady_clock::time_point   recentListedAt;
    // The open map's file, empty until it is first saved, and its content as last saved or opened.
    std::filesystem::path mapFile;
    std::string           savedContent;
    AfterMapClose         afterClose = AfterMapClose::Create;
    // Set when a map opens, so its saved content is taken again after the first draw.
    bool                  snapshotOpened = false;
    std::filesystem::path afterCloseFile;
    // This session's copy of unsaved changes, and copies an earlier session left behind.
    std::filesystem::path        recoveryFile;
    std::string                  recoveryContent;
    std::string                  recoveryError;
    double                       recoveryChangedAt = 0.0;
    bool                         recoveryPending   = false;
    std::vector<MapDraftListing> recoveries;
    // The tiles whose image generation failed, so it is not retried every frame.
    std::optional<std::array<std::byte, kMapCoreSize>> imagesFailedFor;
    Page                                               page                = Page::Map;
    float                                              panelLeft           = 0.0F;
    float                                              panelRight          = 0.0F;
    bool                                               requestMenu         = false;
    bool                                               popupOpenLastFrame  = false;
    bool                                               requestAboutPopup   = false;
    bool                                               requestDiscardPopup = false;
    bool                                               shouldClose         = false;
    bool                                               waitForImages       = false;
    // The running image job and the tile core it started from. Destroying a running job waits for it.
    std::future<std::expected<GeneratedMapImages, std::string>> imageJob;
    std::array<std::byte, kMapCoreSize>                         imageJobCore{};
};

} // namespace rerevved::tools
