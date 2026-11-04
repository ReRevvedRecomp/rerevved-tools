#include "inspect/document_workspace.h"

#include <algorithm>
#include <array>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace rerevved::tools
{
namespace
{

DocumentStatus statusFor(DocumentStage stage, std::string_view error = {})
{
    return DocumentStatus{
        .stage = stage,
        .label = std::string(DocumentStageName(stage)),
        .error = std::string(error),
    };
}

DocumentStatus loadStatus(std::string_view error)
{
    return DocumentStatus{
        .stage = DocumentStage::LoadFailure,
        .label = "Inspection only. Load failed (stage unavailable)",
        .error = std::string(error),
    };
}

DocumentStatus ioStatus(std::string_view error = {})
{
    return DocumentStatus{
        .stage = DocumentStage::LoadFailure,
        .label = "I/O rejected",
        .error = std::string(error),
    };
}

DocumentStatus parseStatus(std::string_view error)
{
    return DocumentStatus{
        .stage = DocumentStage::ParseFailure,
        .label = "Inspection only. Parse failed",
        .error = std::string(error),
    };
}

std::filesystem::path resolvedSourcePath(const std::filesystem::path& path)
{
    std::error_code error;
    const auto      absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute;
}

DocumentStatus metadataStatus(FileKind kind)
{
    if (kind == FileKind::GfxMovie)
    {
        return DocumentStatus{ .stage = DocumentStage::Ready,
                               .label = "Metadata only",
                               .error = {} };
    }
    if (kind == FileKind::BikVideo)
    {
        return DocumentStatus{ .stage = DocumentStage::CapabilityUnavailable,
                               .label = "Inspection only. BIK preview unsupported",
                               .error = {} };
    }
    if (kind == FileKind::Unknown)
    {
        return DocumentStatus{ .stage = DocumentStage::CapabilityUnavailable,
                               .label = "Inspection only. No preview capability",
                               .error = {} };
    }
    return statusFor(DocumentStage::Ready);
}

DocumentStatus parserStatus(FileKind kind, std::string_view error)
{
    switch (kind)
    {
        case FileKind::DdsTexture:
        {
            constexpr std::array metadataErrors{
                DdsMetadataError::Truncated,
                DdsMetadataError::InvalidSignature,
                DdsMetadataError::InvalidHeaderSize,
                DdsMetadataError::InvalidPixelFormatSize,
            };
            for (const auto candidate : metadataErrors)
            {
                if (DdsDocumentErrorMessage(DdsDocumentError{ candidate }) == error)
                {
                    return parseStatus(error);
                }
            }

            constexpr std::array previewErrors{
                DdsPreviewError::UnsupportedFormat,
                DdsPreviewError::InvalidDimensions,
                DdsPreviewError::SizeOverflow,
                DdsPreviewError::TruncatedPixelData,
                DdsPreviewError::InvalidMipCount,
                DdsPreviewError::LayoutRequiresPowerOfTwo,
                DdsPreviewError::PackedMipTailUnsupported,
                DdsPreviewError::PixelLimitExceeded,
            };
            for (const auto candidate : previewErrors)
            {
                if (DdsDocumentErrorMessage(DdsDocumentError{ candidate }) != error)
                {
                    continue;
                }
                // Unsupported encodings, layouts and sizes are limits of this tool, not damaged files.
                if (candidate == DdsPreviewError::UnsupportedFormat || candidate == DdsPreviewError::LayoutRequiresPowerOfTwo ||
                    candidate == DdsPreviewError::PackedMipTailUnsupported || candidate == DdsPreviewError::PixelLimitExceeded)
                {
                    return DocumentStatus{ .stage = DocumentStage::CapabilityUnavailable,
                                           .label = "Inspection only. Preview unsupported",
                                           .error = std::string(error) };
                }
                return statusFor(DocumentStage::PreviewDecodeFailure, error);
            }
            break;
        }
        case FileKind::GfxMovie:
        {
            constexpr std::array errors{
                GfxDocumentError::Truncated,
                GfxDocumentError::InvalidSignature,
                GfxDocumentError::UnsupportedCompression,
                GfxDocumentError::LengthMismatch,
                GfxDocumentError::MissingExporterInfo,
                GfxDocumentError::UnsupportedExporterVersion,
                GfxDocumentError::InvalidTagStream,
            };
            for (const auto candidate : errors)
            {
                if (GfxDocumentErrorMessage(candidate) == error)
                {
                    return parseStatus(error);
                }
            }
            break;
        }
        case FileKind::MapRecord:
        {
            constexpr std::array errors{
                MapDocumentError::Truncated,
                MapDocumentError::UnsupportedLength,
            };
            for (const auto candidate : errors)
            {
                if (MapDocumentErrorMessage(candidate) == error)
                {
                    return parseStatus(error);
                }
            }
            break;
        }
        case FileKind::Mp3Audio:
        {
            constexpr std::array errors{ Mp3DocumentError::DecodeFailed };
            for (const auto candidate : errors)
            {
                if (Mp3DocumentErrorMessage(candidate) == error)
                {
                    return parseStatus(error);
                }
            }
            break;
        }
        case FileKind::NifContainer:
        {
            constexpr std::array errors{
                NifDocumentError::Truncated,
                NifDocumentError::InvalidSignature,
                NifDocumentError::UnsupportedVersion,
                NifDocumentError::UnsupportedEndian,
                NifDocumentError::UnsupportedUserVersion,
                NifDocumentError::InvalidLayout,
            };
            for (const auto candidate : errors)
            {
                if (NifDocumentErrorMessage(candidate) == error)
                {
                    return parseStatus(error);
                }
            }
            break;
        }
        case FileKind::FpkArchive:
        case FileKind::BikVideo:
        case FileKind::Unknown:
            break;
    }

    if (kind == FileKind::NifContainer)
    {
        constexpr std::array previewErrors{
            NifPreviewError::NonFinitePosition,
            NifPreviewError::UnrepresentableTransformedPosition,
            NifPreviewError::EmptyProjectedBounds,
            NifPreviewError::DegenerateProjection,
            NifPreviewError::UnavailableRegion,
        };
        for (const auto candidate : previewErrors)
        {
            if (NifPreviewErrorMessage(candidate) == error)
            {
                return statusFor(DocumentStage::PresentationFailure, error);
            }
        }

        constexpr std::array assemblyErrors{
            NifModelError::SceneCycle,
            NifModelError::WrongDataBlockType,
            NifModelError::InconsistentGeometry,
            NifModelError::TriangleSelectorOutOfRange,
            NifModelError::InvalidTransform,
            NifModelError::NoSupportedMeshes,
        };
        for (const auto candidate : assemblyErrors)
        {
            if (NifModelErrorMessage(candidate) == error)
            {
                return statusFor(DocumentStage::AssemblyFailure, error);
            }
        }
    }

    return loadStatus(error);
}

FileKind embeddedKind(FpkEntryFormat format)
{
    switch (format)
    {
        case FpkEntryFormat::Dds:
            return FileKind::DdsTexture;
        case FpkEntryFormat::Gfx:
            return FileKind::GfxMovie;
        case FpkEntryFormat::Nif:
            return FileKind::NifContainer;
        case FpkEntryFormat::Mp3:
            return FileKind::Mp3Audio;
        case FpkEntryFormat::Map:
            return FileKind::MapRecord;
    }
    return FileKind::Unknown;
}

OpenOutcomeKind outcomeFor(const DocumentStatus& status)
{
    switch (status.stage)
    {
        case DocumentStage::Ready:
            return OpenOutcomeKind::SupportedOpened;
        case DocumentStage::InspectionOnly:
        case DocumentStage::ParseFailure:
            return OpenOutcomeKind::InspectionOpened;
        case DocumentStage::PreviewDecodeFailure:
            return OpenOutcomeKind::PreviewUnavailable;
        case DocumentStage::AssemblyFailure:
        case DocumentStage::PresentationFailure:
        case DocumentStage::CapabilityUnavailable:
            return OpenOutcomeKind::PreviewUnavailable;
        case DocumentStage::LoadFailure:
            return OpenOutcomeKind::InspectionOpened;
    }
    return OpenOutcomeKind::IoRejected;
}

} // namespace

std::string_view DocumentStageName(DocumentStage stage)
{
    switch (stage)
    {
        case DocumentStage::Ready:
            return "Ready";
        case DocumentStage::InspectionOnly:
            return "Inspection only";
        case DocumentStage::ParseFailure:
            return "Inspection only. Parse failed";
        case DocumentStage::PreviewDecodeFailure:
            return "Inspection only. Preview decoding failed";
        case DocumentStage::AssemblyFailure:
            return "Assembly failure";
        case DocumentStage::PresentationFailure:
            return "Presentation failure";
        case DocumentStage::CapabilityUnavailable:
            return "Capability unavailable";
        case DocumentStage::LoadFailure:
            return "Load failure";
    }
    return "Unknown";
}

std::string_view OpenOriginName(OpenOrigin origin)
{
    switch (origin)
    {
        case OpenOrigin::TypedPath:
            return "Typed path";
        case OpenOrigin::Drop:
            return "Drop";
        case OpenOrigin::CommandLine:
            return "Command line";
        case OpenOrigin::Sources:
            return "Sources";
        case OpenOrigin::Archive:
            return "Archive";
        case OpenOrigin::Presentation:
            return "Presentation";
        case OpenOrigin::Creator:
            return "Creator";
    }
    return "Unknown";
}

std::string_view OpenOutcomeName(OpenOutcomeKind outcome)
{
    switch (outcome)
    {
        case OpenOutcomeKind::IoRejected:
            return "I/O rejected";
        case OpenOutcomeKind::ExistingSelected:
            return "Existing document selected";
        case OpenOutcomeKind::InspectionOpened:
            return "Inspection opened";
        case OpenOutcomeKind::SupportedOpened:
            return "Supported document opened";
        case OpenOutcomeKind::PreviewUnavailable:
            return "Preview unavailable";
        case OpenOutcomeKind::ExportPublished:
            return "Map pack exported";
        case OpenOutcomeKind::ExportFailed:
            return "Map pack export failed";
    }
    return "Unknown";
}

std::string SourceLabel(const AssetDocument& document)
{
    const auto& sourcePath = document.sourcePath.empty() ? document.inspection.path
                                                         : document.sourcePath;
    return "#" + std::to_string(document.id) + " " +
           sourcePath.filename().string() + " - " + sourcePath.string();
}

DocumentStatus ClassifyDocumentFailure(FileKind kind, std::string_view error)
{
    return parserStatus(kind, error);
}

DocumentStatus DescribeEmbeddedDocument(const ArchiveExplorerState& state)
{
    if (!state.presentationError.empty())
    {
        return DocumentStatus{ .stage = DocumentStage::PresentationFailure,
                               .label = "Parsed. Preview presentation failed",
                               .error = state.presentationError };
    }
    if (state.nifPreview && !state.nifPreview->navigation.presentationError.empty())
    {
        return DocumentStatus{ .stage = DocumentStage::PresentationFailure,
                               .label = "Parsed. Preview presentation failed",
                               .error = state.nifPreview->navigation.presentationError };
    }
    if (!state.openError.empty())
    {
        return state.selectedFormat ? ClassifyDocumentFailure(embeddedKind(*state.selectedFormat), state.openError) : loadStatus(state.openError);
    }

    if (!state.openedDocument)
    {
        return statusFor(DocumentStage::InspectionOnly);
    }

    return std::visit(
        [&](const auto& document) -> DocumentStatus
        {
            using Document = std::decay_t<decltype(document)>;
            if constexpr (std::is_same_v<Document, NifDocument>)
            {
                if (state.nifPreview && state.nifPreview->assemblyError)
                {
                    return statusFor(DocumentStage::AssemblyFailure,
                                     NifModelErrorMessage(*state.nifPreview->assemblyError));
                }
                return statusFor(DocumentStage::Ready);
            }
            else if constexpr (std::is_same_v<Document, GfxDocument>)
            {
                return DocumentStatus{ .stage = DocumentStage::Ready,
                                       .label = "Metadata only",
                                       .error = {} };
            }
            else
            {
                return statusFor(DocumentStage::Ready);
            }
        },
        state.openedDocument->data);
}

std::vector<AssetDocument>& DocumentWorkspace::Documents() noexcept
{
    return documents;
}

const std::vector<AssetDocument>& DocumentWorkspace::Documents() const noexcept
{
    return documents;
}

AssetDocument* DocumentWorkspace::Active() noexcept
{
    if (!activeIndex || *activeIndex >= documents.size())
    {
        return nullptr;
    }
    return &documents[*activeIndex];
}

const AssetDocument* DocumentWorkspace::Active() const noexcept
{
    if (!activeIndex || *activeIndex >= documents.size())
    {
        return nullptr;
    }
    return &documents[*activeIndex];
}

bool DocumentWorkspace::Select(std::uint64_t id) noexcept
{
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        if (documents[index].id != id)
        {
            continue;
        }
        activeIndex = index;
        return true;
    }
    return false;
}

bool DocumentWorkspace::Close(std::uint64_t id) noexcept
{
    const auto found = std::find_if(documents.begin(),
                                    documents.end(),
                                    [&](const AssetDocument& document)
                                    {
                                        return document.id == id;
                                    });
    if (found == documents.end())
    {
        return false;
    }

    const auto closedIndex = static_cast<std::size_t>(found - documents.begin());
    const bool wasActive   = activeIndex && *activeIndex == closedIndex;
    documents.erase(found);

    if (documents.empty())
    {
        activeIndex.reset();
        return true;
    }
    if (!activeIndex)
    {
        return true;
    }
    if (!wasActive)
    {
        if (closedIndex < *activeIndex)
        {
            --*activeIndex;
        }
        return true;
    }
    if (closedIndex < documents.size())
    {
        activeIndex = closedIndex;
    }
    else
    {
        activeIndex = documents.size() - 1;
    }
    return true;
}

void ReloadDdsDocument(AssetDocument& document, DdsPayloadLayout layout)
{
    document.ddsLayout = layout;
    document.dds.reset();
    document.documentError.clear();
    document.status = metadataStatus(document.inspection.kind);
    auto loaded     = LoadDdsDocument(document.inspection.path, layout);
    if (loaded)
    {
        document.dds = std::move(*loaded);
    }
    else
    {
        document.status        = ClassifyDocumentFailure(document.inspection.kind, loaded.error());
        document.documentError = loaded.error();
    }
}

OpenOutcome DocumentWorkspace::Open(const std::filesystem::path& path, OpenOrigin origin)
{
    OpenOutcome outcome{ .origin     = origin,
                         .kind       = OpenOutcomeKind::IoRejected,
                         .source     = resolvedSourcePath(path),
                         .documentId = std::nullopt,
                         .status     = ioStatus() };

    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        if (documents[index].inspection.path != path)
        {
            continue;
        }
        activeIndex        = index;
        outcome.kind       = OpenOutcomeKind::ExistingSelected;
        outcome.documentId = documents[index].id;
        outcome.source     = documents[index].sourcePath;
        outcome.status     = documents[index].status;
        RecordActivity(outcome);
        return outcome;
    }

    auto inspection = InspectFile(path);
    if (!inspection)
    {
        outcome.status = ioStatus(inspection.error());
        RecordActivity(outcome);
        return outcome;
    }

    AssetDocument document{};
    document.id         = nextId++;
    document.sourcePath = resolvedSourcePath(path);
    document.inspection = std::move(*inspection);
    document.status     = metadataStatus(document.inspection.kind);
    if (document.inspection.kind == FileKind::FpkArchive)
    {
        auto loaded = LoadFpkDocument(document.inspection.path);
        if (loaded)
        {
            document.fpk = std::move(*loaded);
            SelectInitialArchiveEntry(document.archive, *document.fpk);
        }
        else
        {
            document.status        = ClassifyDocumentFailure(document.inspection.kind, loaded.error());
            document.documentError = loaded.error();
        }
    }
    else if (document.inspection.kind == FileKind::DdsTexture)
    {
        ReloadDdsDocument(document, document.ddsLayout);
    }
    else if (document.inspection.kind == FileKind::GfxMovie)
    {
        auto loaded = LoadGfxDocument(document.inspection.path);
        if (loaded)
        {
            document.gfx = std::move(*loaded);
        }
        else
        {
            document.status        = ClassifyDocumentFailure(document.inspection.kind, loaded.error());
            document.documentError = loaded.error();
        }
    }
    else if (document.inspection.kind == FileKind::Mp3Audio)
    {
        auto loaded = LoadMp3Document(document.inspection.path);
        if (loaded)
        {
            document.mp3 = std::move(*loaded);
        }
        else
        {
            document.status        = ClassifyDocumentFailure(document.inspection.kind, loaded.error());
            document.documentError = loaded.error();
        }
    }
    else if (document.inspection.kind == FileKind::MapRecord)
    {
        auto loaded = LoadMapDocument(document.inspection.path);
        if (loaded)
        {
            document.map = std::move(*loaded);
        }
        else
        {
            document.status        = ClassifyDocumentFailure(document.inspection.kind, loaded.error());
            document.documentError = loaded.error();
        }
    }
    else if (document.inspection.kind == FileKind::NifContainer)
    {
        auto loaded = LoadNifDocument(document.inspection.path);
        if (loaded)
        {
            document.nif = std::move(*loaded);
            auto model   = AssembleNifModel(*document.nif);
            if (model)
            {
                document.nifModel = std::move(*model);
            }
            else
            {
                document.nifModelError = model.error();
                document.status        = statusFor(DocumentStage::AssemblyFailure,
                                                   NifModelErrorMessage(*document.nifModelError));
                document.documentError = document.status.error;
            }
        }
        else
        {
            document.status        = ClassifyDocumentFailure(document.inspection.kind, loaded.error());
            document.documentError = loaded.error();
        }
    }

    if (!document.documentError.empty() && document.status.error.empty())
    {
        document.status.error = document.documentError;
    }

    documents.push_back(std::move(document));
    activeIndex        = documents.size() - 1;
    outcome.documentId = documents.back().id;
    outcome.status     = documents.back().status;
    outcome.kind       = outcomeFor(outcome.status);
    RecordActivity(outcome);
    return outcome;
}

} // namespace rerevved::tools
