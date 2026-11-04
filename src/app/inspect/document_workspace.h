#pragma once

#include "dds_document.h"
#include "file_inspection.h"
#include "fpk_document.h"
#include "gfx_document.h"
#include "inspect/archive_explorer.h"
#include "inspect/nif_inspector.h"
#include "inspect/nif_preview.h"
#include "map_document.h"
#include "mp3_document.h"
#include "nif_model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rerevved::tools
{

enum class DocumentStage
{
    Ready,
    InspectionOnly,
    ParseFailure,
    PreviewDecodeFailure,
    AssemblyFailure,
    PresentationFailure,
    CapabilityUnavailable,
    LoadFailure,
};

struct DocumentStatus
{
    DocumentStage stage = DocumentStage::Ready;
    std::string   label = "Ready";
    std::string   error;

    [[nodiscard]] friend bool operator==(const DocumentStatus&, const DocumentStatus&) =
        default;
};

[[nodiscard]] std::string_view DocumentStageName(DocumentStage stage);

enum class OpenOrigin
{
    TypedPath,
    Drop,
    CommandLine,
    Sources,
    Archive,
    Presentation,
    Creator,
};

[[nodiscard]] std::string_view OpenOriginName(OpenOrigin origin);

enum class OpenOutcomeKind
{
    IoRejected,
    ExistingSelected,
    InspectionOpened,
    SupportedOpened,
    PreviewUnavailable,
    ExportPublished,
    ExportFailed,
};

[[nodiscard]] std::string_view OpenOutcomeName(OpenOutcomeKind outcome);

struct AssetDocument
{
    std::uint64_t                id = 0;
    std::filesystem::path        sourcePath;
    FileInspection               inspection;
    std::optional<FpkDocument>   fpk;
    ArchiveExplorerState         archive;
    std::optional<DdsDocument>   dds;
    DdsPayloadLayout             ddsLayout = DdsPayloadLayout::LinearLittleEndian;
    std::optional<GfxDocument>   gfx;
    std::optional<MapDocument>   map;
    std::optional<Mp3Document>   mp3;
    std::optional<NifDocument>   nif;
    std::optional<NifModel>      nifModel;
    std::optional<NifModelError> nifModelError;
    NifPreviewState              nifPreview;
    NifInspectorState            nifInspector;
    InspectorNavigationState     gfxInspector;
    InspectorNavigationState     mapInspector;
    DocumentStatus               status;
    std::string                  documentError;
};

struct OpenOutcome
{
    OpenOrigin                   origin = OpenOrigin::TypedPath;
    OpenOutcomeKind              kind   = OpenOutcomeKind::IoRejected;
    std::filesystem::path        source;
    std::optional<std::uint64_t> documentId;
    DocumentStatus               status;
};

[[nodiscard]] std::string SourceLabel(const AssetDocument& document);

[[nodiscard]] DocumentStatus ClassifyDocumentFailure(FileKind         kind,
                                                     std::string_view error);

[[nodiscard]] DocumentStatus DescribeEmbeddedDocument(const ArchiveExplorerState& state);

// DDS headers do not record Xbox tiling.
void ReloadDdsDocument(AssetDocument& document, DdsPayloadLayout layout);

class DocumentWorkspace
{
public:
    [[nodiscard]] OpenOutcome Open(const std::filesystem::path& path,
                                   OpenOrigin                   origin = OpenOrigin::TypedPath);

    [[nodiscard]] std::vector<AssetDocument>&       Documents() noexcept;
    [[nodiscard]] const std::vector<AssetDocument>& Documents() const noexcept;

    [[nodiscard]] AssetDocument*       Active() noexcept;
    [[nodiscard]] const AssetDocument* Active() const noexcept;

    [[nodiscard]] bool Select(std::uint64_t id) noexcept;
    [[nodiscard]] bool Close(std::uint64_t id) noexcept;

    [[nodiscard]] const std::vector<OpenOutcome>& Activity() const noexcept
    {
        return activity;
    }

    // Total outcomes ever recorded. Unlike Activity().size(), it keeps growing past the cap.
    [[nodiscard]] std::uint64_t ActivityRecorded() const noexcept
    {
        return activityRecorded;
    }

    void RecordActivity(OpenOutcome outcome)
    {
        constexpr std::size_t kMaximumActivity = 256;
        ++activityRecorded;
        activity.push_back(std::move(outcome));
        if (activity.size() > kMaximumActivity)
        {
            activity.erase(activity.begin());
        }
    }

    void RecordCreatorActivity(OpenOutcomeKind kind, std::filesystem::path source, std::string label, std::string error = {})
    {
        OpenOutcome outcome;
        outcome.origin       = OpenOrigin::Creator;
        outcome.kind         = kind;
        outcome.source       = std::move(source);
        outcome.status.label = std::move(label);
        outcome.status.error = std::move(error);
        switch (kind)
        {
            case OpenOutcomeKind::ExportFailed:
                outcome.status.stage = DocumentStage::CapabilityUnavailable;
                break;
            default:
                outcome.status.stage = DocumentStage::Ready;
                break;
        }
        RecordActivity(std::move(outcome));
    }

private:
    std::vector<AssetDocument> documents;
    std::optional<std::size_t> activeIndex;
    std::vector<OpenOutcome>   activity;
    std::uint64_t              activityRecorded = 0;
    std::uint64_t              nextId           = 1;
};

} // namespace rerevved::tools
