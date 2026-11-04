#include "inspect/inspection_pane.h"
#include "inspect/archive_explorer.h"
#include "inspect/asset_inspector.h"
#include "inspect/document_presentation.h"
#include "inspect/gfx_inspector.h"
#include "inspect/inspection_format.h"
#include "inspect/mp3_preview.h"
#include "inspect/nif_inspector.h"
#include "inspect/nif_preview.h"
#include "map/map_preview.h"
#include "ui/native_dialog.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace rerevved::tools
{

namespace
{

std::string utf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return { text.begin(), text.end() };
}

std::vector<std::filesystem::path> foldersOf(const std::filesystem::path& file)
{
    std::vector<std::filesystem::path> folders;
    const auto                         parent = file.parent_path();
    for (const auto& part : parent)
    {
        if (part != parent.root_directory())
        {
            folders.push_back(part);
        }
    }
    std::ranges::reverse(folders);
    return folders;
}

using Folders = std::vector<std::filesystem::path>;

// The nearest folder that no twin with the same nearer folders shares, or empty when none does.
std::filesystem::path distinctFolder(const Folders& mine, std::vector<const Folders*> twins)
{
    for (std::size_t depth = 0; depth < mine.size(); ++depth)
    {
        std::erase_if(twins, [&](const Folders* twin)
                      {
                          return depth >= twin->size() || (*twin)[depth] != mine[depth];
                      });
        if (twins.empty())
        {
            return mine[depth];
        }
    }
    return {};
}

std::filesystem::path trailingFolders(const Folders& mine, const std::vector<const Folders*>& twins, const std::filesystem::path& whole)
{
    for (std::size_t count = 1; count <= mine.size(); ++count)
    {
        const auto shared = [&](const Folders* twin)
        {
            return twin->size() >= count && std::equal(mine.begin(), mine.begin() + static_cast<std::ptrdiff_t>(count), twin->begin());
        };
        if (std::ranges::none_of(twins, shared))
        {
            std::filesystem::path folder;
            for (auto part = mine.rend() - static_cast<std::ptrdiff_t>(count); part != mine.rend(); ++part)
            {
                folder /= *part;
            }
            return folder;
        }
    }
    return whole;
}

std::string openFileLabel(const DocumentWorkspace& workspace, const AssetDocument& document)
{
    const auto&          path = document.inspection.path;
    std::vector<Folders> group;
    std::size_t          self = 0;
    for (const auto& other : workspace.Documents())
    {
        if (other.inspection.path.filename() == path.filename())
        {
            self = other.id == document.id ? group.size() : self;
            group.push_back(foldersOf(other.inspection.path));
        }
    }
    if (group.size() < 2)
    {
        return utf8(path.filename());
    }
    const auto othersThan = [&](std::size_t index)
    {
        std::vector<const Folders*> others;
        for (std::size_t other = 0; other < group.size(); ++other)
        {
            if (other != index)
            {
                others.push_back(&group[other]);
            }
        }
        return others;
    };
    const auto mine  = distinctFolder(group[self], othersThan(self));
    bool       clash = mine.empty();
    for (std::size_t other = 0; other < group.size() && !clash; ++other)
    {
        clash = other != self && distinctFolder(group[other], othersThan(other)) == mine;
    }
    const auto folder = clash ? trailingFolders(group[self], othersThan(self), path.parent_path()) : mine;
    return utf8(path.filename()) + " - " + utf8(folder);
}

std::string archiveSource(const AssetDocument& document)
{
    auto source = document.sourcePath.string();
    if (document.archive.selectedEntry && document.fpk)
    {
        const auto index = *document.archive.selectedEntry;
        source += " / " + FormatFpkEntryRange(index + 1, document.fpk->index.entries[index]);
        source += " / Open As " + std::string(document.archive.selectedFormat ? ArchiveFormatName(*document.archive.selectedFormat) : "Choose parser");
    }
    return source;
}

const char* kindIcon(FileKind kind)
{
    switch (kind)
    {
        case FileKind::FpkArchive:
            return ui::icon::FileArchive;
        case FileKind::DdsTexture:
            return ui::icon::FileImage;
        case FileKind::Mp3Audio:
            return ui::icon::FileAudio;
        case FileKind::NifContainer:
            return ui::icon::Shapes;
        case FileKind::MapRecord:
            return ui::icon::Map;
        default:
            return ui::icon::File;
    }
}

ui::Tone documentTone(const DocumentStatus& status)
{
    if (!status.error.empty())
    {
        return ui::Tone::Error;
    }
    return status.stage == DocumentStage::Ready ? ui::Tone::Ok : ui::Tone::Neutral;
}

} // namespace

InspectionPane::~InspectionPane()
{
    sources.Shutdown();
}

bool InspectionPane::OpenPath(const std::filesystem::path& path, OpenOrigin origin)
{
    const auto outcome = workspace.Open(path, origin);
    status             = outcome.source.string() + ": " + outcome.status.label;
    if (!outcome.status.error.empty())
    {
        status += " - " + outcome.status.error;
    }
    if (outcome.documentId)
    {
        ddsViewer.Clear();
        return true;
    }
    return false;
}

void InspectionPane::SelectDocument(std::uint64_t id)
{
    const auto* active = workspace.Active();
    if ((!active || active->id != id) && workspace.Select(id))
    {
        ddsViewer.Clear();
    }
}

void InspectionPane::CloseDocument(std::uint64_t id)
{
    const auto* active = workspace.Active();
    if (active && active->id == id)
    {
        ddsViewer.Clear();
    }
    if (workspace.Close(id))
    {
        status = "Closed document.";
    }
}

void InspectionPane::drawOpenFiles()
{
    ui::SectionHeading("Open Files");
    const auto& documents = workspace.Documents();
    if (documents.empty())
    {
        ui::Muted("No files open.");
        ui::VerticalSpace(8.0F);
        return;
    }
    const auto*                  active = workspace.Active();
    std::optional<std::uint64_t> select;
    std::optional<std::uint64_t> close;
    const float                  row    = ImGui::GetFrameHeight();
    const float                  height = std::min(static_cast<float>(documents.size()), 6.0F) * ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("Open files", ImVec2(0.0F, height), ImGuiChildFlags_NavFlattened);
    for (const auto& document : documents)
    {
        ImGui::PushID(static_cast<int>(document.id));
        const auto rowWidth = ImGui::GetContentRegionAvail().x - row - ImGui::GetStyle().ItemSpacing.x;
        const auto textAt   = ImGui::GetCursorScreenPos();
        // The label is drawn as text, so a file name never acts as an ImGui ID.
        if (ImGui::Selectable("##file", active && active->id == document.id, ImGuiSelectableFlags_None, ImVec2(rowWidth, row)))
        {
            select = document.id;
        }
        const auto rowMin = ImGui::GetItemRectMin();
        const auto rowMax = ImGui::GetItemRectMax();
        const auto label  = openFileLabel(workspace, document);
        ImGui::GetWindowDrawList()->PushClipRect(rowMin, rowMax, true);
        ImGui::GetWindowDrawList()->AddText(ImVec2(textAt.x, textAt.y + (row - ImGui::GetFontSize()) * 0.5F), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        ImGui::GetWindowDrawList()->PopClipRect();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", utf8(document.inspection.path).c_str());
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::color::Muted);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
        if (ImGui::Button((std::string(ui::icon::Close) + "##close").c_str(), ImVec2(row, row)))
        {
            close = document.id;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Close");
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ui::VerticalSpace(8.0F);
    // Defer closing or selecting a document until the list has finished drawing.
    if (close)
    {
        CloseDocument(*close);
    }
    else if (select)
    {
        SelectDocument(*select);
    }
}

void InspectionPane::drawOpenPopup()
{
    const bool focusPath = requestOpenPopup;
    if (requestOpenPopup)
    {
        ImGui::OpenPopup("Open path");
        openError.clear();
        requestOpenPopup = false;
    }

    if (!ImGui::BeginPopupModal("Open path", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    const auto& style = ImGui::GetStyle();
    const auto  inputWidth =
        std::max(1.0F,
                 std::min(600.0F,
                          ImGui::GetMainViewport()->WorkSize.x - style.WindowPadding.x * 2.0F));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inputWidth);
    ImGui::TextUnformatted("Enter a path to a game file you own.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(inputWidth);
    if (focusPath)
    {
        ImGui::SetKeyboardFocusHere();
    }
    const bool submitted = ImGui::InputText(
        "##path", pathBuffer.data(), pathBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

    const auto buttonWidth =
        std::min(120.0F, std::max(1.0F, (inputWidth - style.ItemSpacing.x) / 2.0F));
    if (submitted || ImGui::Button("Open", ImVec2(buttonWidth, 0.0F)))
    {
        if (OpenPath(std::filesystem::path(pathBuffer.data())))
        {
            pathBuffer.fill('\0');
            openError.clear();
            ImGui::CloseCurrentPopup();
        }
        else
        {
            openError = workspace.Activity().back().status.error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0F)))
    {
        openError.clear();
        ImGui::CloseCurrentPopup();
    }
    if (!openError.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inputWidth);
        ImGui::TextUnformatted(openError.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndPopup();
}

void InspectionPane::drawInspector()
{
    ImGui::Begin("Inspector");
    auto* document = workspace.Active();
    if (!document)
    {
        ui::Muted("Nothing selected.");
        ui::Caption("Details about the open file appear here.");
        ImGui::End();
        return;
    }
    const auto& inspection = document->inspection;
    ImGui::PushID(std::to_string(document->id).c_str());
    ui::Heading(inspection.path.filename().string());
    ui::TonedText(documentTone(document->status), document->status.label);
    if (!document->status.error.empty())
    {
        ui::Caption(std::string(DocumentStageName(document->status.stage)) + ": " + document->status.error);
    }
    ui::VerticalSpace(4.0F);
    ui::KeyValue("Kind", FileKindName(inspection.kind), 72.0F);
    ui::KeyValue("Size", FormatByteSize(inspection.size) + " (" + std::to_string(inspection.size) + " bytes)", 72.0F);
    ui::KeyValue("Path", document->sourcePath.string(), 72.0F);
    ui::Divider();
    if (ui::Section("Raw Header"))
    {
        ImGui::TextWrapped("%s", FormatHeader(inspection).c_str());
    }
    if (inspection.kind == FileKind::GfxMovie)
    {
        DrawGfxInspector(document->gfx ? &*document->gfx : nullptr, document->documentError, document->gfxInspector);
    }
    if (inspection.kind == FileKind::NifContainer)
    {
        DrawNifInspector(document->nif ? &*document->nif : nullptr, document->documentError, document->nifInspector);
    }
    if (document->dds)
    {
        DrawDdsInspector(*document->dds);
    }
    if (document->mp3)
    {
        DrawMp3Inspector(*document->mp3);
    }
    if (document->map)
    {
        DrawMapInspector(*document->map, document->mapInspector);
    }
    if (inspection.kind == FileKind::FpkArchive)
    {
        const auto attempts = document->archive.openAttempts;
        DrawArchiveInspector(document->fpk ? &*document->fpk : nullptr, document->archive, document->documentError, ddsViewer);
        if (document->archive.openAttempts != attempts)
        {
            const auto result = DescribeEmbeddedDocument(document->archive);
            workspace.RecordActivity({ OpenOrigin::Archive,
                                       result.stage == DocumentStage::Ready ? OpenOutcomeKind::SupportedOpened : result.stage == DocumentStage::ParseFailure || result.stage == DocumentStage::LoadFailure ? OpenOutcomeKind::InspectionOpened
                                                                                                                                                                                                           : OpenOutcomeKind::PreviewUnavailable,
                                       std::filesystem::path(archiveSource(*document)),
                                       document->id,
                                       result });
            ddsViewer.Clear();
        }
    }
    ImGui::PopID();
    ImGui::End();
}

void InspectionPane::recordPresentation(AssetDocument& document, const std::string& error)
{
    if (document.inspection.kind == FileKind::FpkArchive)
    {
        if (document.archive.presentationError == error)
        {
            return;
        }
        document.archive.presentationError = error;
    }
    else
    {
        if (error.empty())
        {
            if (document.status.stage == DocumentStage::PresentationFailure && (document.dds || document.nifModel))
            {
                document.status = { DocumentStage::Ready, "Parsed. Preview available", {} };
            }
            return;
        }
        if (document.status.stage == DocumentStage::PresentationFailure && document.status.error == error)
        {
            return;
        }
        document.status = { DocumentStage::PresentationFailure,
                            "Parsed. Preview presentation failed",
                            error };
    }
    if (!error.empty())
    {
        workspace.RecordActivity({ OpenOrigin::Presentation, OpenOutcomeKind::PreviewUnavailable, std::filesystem::path(archiveSource(document)), document.id, { DocumentStage::PresentationFailure, "Parsed. Preview presentation failed", error } });
    }
}

void InspectionPane::drawPreview()
{
    ImGui::Begin("Preview");
    auto* document = workspace.Active();
    if (!document)
    {
        ddsViewer.Clear();
        const auto avail = ImGui::GetContentRegionAvail();
        if (ui::EmptyState("preview-empty", ui::icon::ScanSearch, "Open a Game File", "Archives, textures, models, maps and audio open read only. "
                                                                                      "Drop files anywhere in the window.",
                           ui::icon::FolderOpen,
                           "Open file",
                           std::max(200.0F, avail.y),
                           true))
        {
            RequestOpen();
        }
        ImGui::End();
        return;
    }
    if (document->inspection.kind == FileKind::FpkArchive && document->fpk)
    {
        DrawArchiveWorkArea(document->sourcePath, *document->fpk, document->archive, ddsViewer);
        auto result = DescribeEmbeddedDocument(document->archive);
        if (!document->archive.presentationError.empty())
        {
            result = { DocumentStage::PresentationFailure, "Parsed. Preview presentation failed", document->archive.presentationError };
        }
        ui::VerticalSpace(6.0F);
        // Success details live in the Inspector. Only failures repeat here beside the preview.
        if (!result.error.empty())
        {
            DrawDocumentContext(archiveSource(*document), result);
        }
        DrawArchivePreview(document->inspection.path, document->archive, ddsViewer);
        const auto error = document->archive.nifPreview ? document->archive.nifPreview->navigation.presentationError : std::string(ddsViewer.Error());
        recordPresentation(*document, error);
    }
    else
    {
        if (document->inspection.kind == FileKind::DdsTexture)
        {
            auto       layout   = document->ddsLayout;
            const bool tiled    = layout == DdsPayloadLayout::XboxTiledBigEndian;
            const auto sourceId = document->inspection.path.string() + (tiled ? "|xbox-tiled" : "|linear");
            ddsViewer.Draw(document->dds ? &*document->dds : nullptr, sourceId, document->documentError, &layout);
            if (layout != document->ddsLayout)
            {
                ReloadDdsDocument(*document, layout);
                const auto outcome = document->status.stage == DocumentStage::Ready ? OpenOutcomeKind::SupportedOpened : OpenOutcomeKind::PreviewUnavailable;
                workspace.RecordActivity({ OpenOrigin::Presentation, outcome, document->inspection.path, document->id, document->status });
            }
            else
            {
                recordPresentation(*document, std::string(ddsViewer.Error()));
            }
        }
        else
        {
            ddsViewer.Clear();
            if (document->map)
            {
                DrawMapPreview(&*document->map, {});
            }
            else if (document->mp3)
            {
                DrawMp3Preview(&*document->mp3, {});
            }
            else if (document->nif)
            {
                DrawNifPreview(&*document->nif, document->nifModel ? &*document->nifModel : nullptr, document->nifModelError, document->documentError, document->nifPreview);
                recordPresentation(*document, document->nifPreview.presentationError);
            }
            else
            {
                const auto avail = ImGui::GetContentRegionAvail();
                const auto body  = document->status.error.empty() ? std::string("This file has no visual preview. Its details are in the Inspector.") : document->status.error;
                ui::EmptyState("preview-none", document->status.error.empty() ? kindIcon(document->inspection.kind) : ui::icon::XCircle, document->status.label, body, nullptr, nullptr, std::max(200.0F, avail.y));
            }
        }
    }
    ImGui::End();
}

void InspectionPane::RequestOpen()
{
    static constexpr FileDialogFilter filters[] = {
        { L"Game files", L"*.fpk;*.dds;*.gfx;*.bik;*.mp3;*.nif;*.map" },
        { L"All files", L"*.*" },
    };
    const auto chosen = OpenFileDialog(L"Open a Game File", filters);
    if (!chosen)
    {
        requestOpenPopup = true;
    }
    else if (*chosen && !OpenPath(**chosen))
    {
        NotifyOpenFailure(**chosen);
    }
}

void InspectionPane::NotifyOpenFailure(const std::filesystem::path& path)
{
    // Show a toast when the action has no nearby error field.
    if (!workspace.Activity().empty())
    {
        const auto& failure = workspace.Activity().back();
        ui::PushToast(ui::Tone::Error, PathToUtf8(path.filename()) + ": " + (failure.status.error.empty() ? failure.status.label : failure.status.error));
    }
}

void InspectionPane::RequestBrowse()
{
    sources.RequestBrowse();
}

const std::string& InspectionPane::Status() const noexcept
{
    return status;
}

const DocumentWorkspace& InspectionPane::Workspace() const noexcept
{
    return workspace;
}

std::optional<std::filesystem::path> InspectionPane::SourcesRoot() const
{
    return sources.Root();
}

DocumentWorkspace& InspectionPane::Workspace() noexcept
{
    return workspace;
}

std::optional<InspectionSelection> InspectionPane::Selection() const noexcept
{
    const auto* document = workspace.Active();
    if (!document)
    {
        return std::nullopt;
    }
    return InspectionSelection{ document->id, document->archive.selectedEntry, document->archive.openAttempts };
}

void InspectionPane::Draw(bool visible)
{
    if (visible)
    {
        if (const auto path = sources.Draw([this]
                                           {
                                               drawOpenFiles();
                                           }))
        {
            if (OpenPath(*path, OpenOrigin::Sources))
            {
                sources.SetOpenError({});
            }
            else
            {
                sources.SetOpenError(workspace.Activity().back().status.error);
            }
        }
        drawInspector();
        drawPreview();
    }
    drawOpenPopup();
}

} // namespace rerevved::tools
