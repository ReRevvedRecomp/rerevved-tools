#pragma once

#include "inspect/dds_viewer.h"
#include "inspect/document_workspace.h"
#include "inspect/sources_pane.h"
#include "map/creator_workspace.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace rerevved::tools
{

class InspectionPane
{
public:
    ~InspectionPane();
    [[nodiscard]] bool                                 OpenPath(const std::filesystem::path& path, OpenOrigin origin = OpenOrigin::TypedPath);
    void                                               Draw(bool visible);
    void                                               RequestOpen();
    void                                               NotifyOpenFailure(const std::filesystem::path& path);
    void                                               RequestBrowse();
    [[nodiscard]] const std::string&                   Status() const noexcept;
    [[nodiscard]] const DocumentWorkspace&             Workspace() const noexcept;
    [[nodiscard]] DocumentWorkspace&                   Workspace() noexcept;
    [[nodiscard]] std::optional<InspectionSelection>   Selection() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> SourcesRoot() const;
    void                                               SelectDocument(std::uint64_t id);
    void                                               CloseDocument(std::uint64_t id);

private:
    void drawOpenFiles();
    void drawOpenPopup();
    void drawInspector();
    void drawPreview();
    void recordPresentation(AssetDocument& document, const std::string& error);

    SourcesPane            sources;
    DocumentWorkspace      workspace;
    DdsViewer              ddsViewer;
    std::array<char, 2048> pathBuffer{};
    std::string            openError;
    std::string            status           = "Open a reference file or start a creator journey.";
    bool                   requestOpenPopup = false;
};

} // namespace rerevved::tools
