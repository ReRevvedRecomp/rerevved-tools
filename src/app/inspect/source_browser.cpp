#include "inspect/source_browser.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace rerevved::tools
{
namespace
{

constexpr SourceRequestId kInvalidRequestId = 0;

constexpr std::string_view kReparseTraversalError =
    "Reparse point traversal is disabled.";
constexpr std::string_view kEntryLimitReason = "Directory entry limit reached.";
constexpr std::string_view kWorkLimitReason  = "Enumeration work limit reached.";
constexpr std::string_view kNodeLimitReason  = "Retained node limit reached.";
constexpr std::string_view kPathLimitReason  = "Retained path limit reached.";

std::error_code noSuchFileError()
{
    return std::make_error_code(std::errc::no_such_file_or_directory);
}

std::error_code notDirectoryError()
{
    return std::make_error_code(std::errc::not_a_directory);
}

std::error_code reparseError()
{
    return std::make_error_code(std::errc::operation_not_permitted);
}

std::string errorMessage(const std::error_code& error)
{
    return error ? error.message() : std::string{};
}

bool isDriveRelativePath(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto rootName = path.root_name().wstring();
    return rootName.size() == 2 && rootName[1] == L':' && !path.has_root_directory();
#else
    static_cast<void>(path);
    return false;
#endif
}

std::size_t pathBytes(const std::filesystem::path& path)
{
    const auto length = path.native().size();
    if (length > std::numeric_limits<std::size_t>::max() / sizeof(path.native()[0]))
    {
        return std::numeric_limits<std::size_t>::max();
    }
    return length * sizeof(path.native()[0]);
}

std::filesystem::path absoluteLexicalPath(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return path;
    }
    if (isDriveRelativePath(path))
    {
        return path;
    }
    if (path.is_absolute())
    {
        return path;
    }
    std::error_code error;
    const auto      current = std::filesystem::current_path(error);
    return error ? path : current / path;
}

bool nativeReparsePoint(const std::filesystem::path& path)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code error;
    const auto      status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_symlink(status);
#endif
}

bool hasReparsePointInPath(const std::filesystem::path& path)
{
    auto current = absoluteLexicalPath(path);
    if (current.empty())
    {
        return false;
    }

    while (!current.empty())
    {
        if (nativeReparsePoint(current))
        {
            return true;
        }

        const auto parent = current.parent_path();
        if (parent.empty() || parent == current)
        {
            break;
        }
        current = parent;
    }
    return false;
}

SourceNodeKind kindForStatus(const std::filesystem::file_status& status)
{
    if (std::filesystem::is_regular_file(status))
    {
        return SourceNodeKind::File;
    }
    if (std::filesystem::is_directory(status))
    {
        return SourceNodeKind::Directory;
    }
    return SourceNodeKind::Other;
}

SourceEnumerationEntry inspectEntry(const std::filesystem::path& path)
{
    SourceEnumerationEntry entry{ .path      = path,
                                  .kind      = SourceNodeKind::Other,
                                  .errorCode = {},
                                  .error     = {} };
    if (path.empty())
    {
        entry.errorCode = std::make_error_code(std::errc::invalid_argument);
        entry.error     = "The source path is empty.";
        return entry;
    }
    if (isDriveRelativePath(path))
    {
        entry.errorCode = std::make_error_code(std::errc::invalid_argument);
        entry.error     = "drive-relative source paths are not supported.";
        return entry;
    }
    if (hasReparsePointInPath(path))
    {
        entry.kind      = SourceNodeKind::ReparsePoint;
        entry.errorCode = reparseError();
        entry.error     = std::string(kReparseTraversalError);
        return entry;
    }

    std::error_code error;
    const auto      status = std::filesystem::symlink_status(path, error);
    if (error)
    {
        entry.kind      = SourceNodeKind::Other;
        entry.errorCode = error;
        entry.error     = errorMessage(error);
        return entry;
    }
    if (status.type() == std::filesystem::file_type::not_found)
    {
        entry.kind      = SourceNodeKind::Other;
        entry.errorCode = noSuchFileError();
        entry.error     = errorMessage(entry.errorCode);
        return entry;
    }

    entry.kind = kindForStatus(status);
    return entry;
}

void appendReason(std::string& destination, std::string_view reason)
{
    if (reason.empty())
    {
        return;
    }
    if (destination.find(reason) != std::string::npos)
    {
        return;
    }
    if (!destination.empty())
    {
        destination += " ";
    }
    destination += reason;
}

SourceBrowserLimits normalizedLimits(SourceBrowserLimits limits)
{
    limits.maxEntriesPerDirectory = std::max<std::size_t>(1, limits.maxEntriesPerDirectory);
    limits.maxRetainedNodes       = std::max<std::size_t>(1, limits.maxRetainedNodes);
    limits.maxRetainedPathBytes   = std::max<std::size_t>(1, limits.maxRetainedPathBytes);
    limits.maxPendingRequests     = std::max<std::size_t>(1, limits.maxPendingRequests);
    limits.maxPendingCompletions  = std::max<std::size_t>(1, limits.maxPendingCompletions);
    if (limits.maxEnumerationWork <= std::chrono::milliseconds::zero())
    {
        limits.maxEnumerationWork = std::chrono::milliseconds(1);
    }
    return limits;
}

} // namespace

SourceBrowser::SourceBrowser(SourceBrowserLimits limits, SourceEnumerator enumerator)
: limits(normalizedLimits(limits))
, enumerator(enumerator ? std::move(enumerator) : SourceEnumerator{ EnumerateDirectory })
, worker([this]
         {
             WorkerMain();
         })
{
}

SourceBrowser::~SourceBrowser()
{
    Shutdown();
}

void SourceBrowser::SetRoot(const std::filesystem::path& path)
{
    if (shutdownRequested)
    {
        return;
    }

    ++rootRevision;

    for (auto& [requestId, metadata] : requests)
    {
        static_cast<void>(requestId);
        metadata.cancelledByUi = true;
        if (metadata.cancelled)
        {
            metadata.cancelled->store(true, std::memory_order_release);
        }
    }

    std::vector<SourceRequestId> removed;
    {
        std::lock_guard lock(queueMutex);
        RemoveCancelledPendingLocked(removed);

        std::vector<SourceRequestId> completionRequests;
        while (!completions.empty())
        {
            const auto requestId = completions.front().requestId;
            if (requestId == activeRequestId)
            {
                completions.pop_front();
                continue;
            }
            completionRequests.push_back(requestId);
            completions.pop_front();
        }
        for (const auto requestId : completionRequests)
        {
            if (std::find(removed.begin(), removed.end(), requestId) == removed.end())
            {
                removed.push_back(requestId);
                if (inFlight > 0)
                {
                    --inFlight;
                }
            }
        }
    }
    for (const auto requestId : removed)
    {
        requests.erase(requestId);
    }
    completionCondition.notify_all();

    nodes.clear();
    const SourceNodeId id       = nextNodeId++;
    const auto         rootPath = absoluteLexicalPath(path);
    SourceNode         root{ .id                 = id,
                             .path               = rootPath,
                             .kind               = SourceNodeKind::Directory,
                             .children           = {},
                             .state              = DirectoryState::Unloaded,
                             .revision           = 0,
                             .request            = {},
                             .isPartial          = false,
                             .partialLimitReason = {},
                             .errorCode          = {},
                             .error              = {} };

    rootId = id;
    nodes.emplace(id, std::move(root));
    static_cast<void>(Request(rootId));
}

std::optional<SourceRequestId> SourceBrowser::Request(SourceNodeId directoryId)
{
    if (shutdownRequested)
    {
        return std::nullopt;
    }

    const auto nodeIt = nodes.find(directoryId);
    if (nodeIt == nodes.end() || nodeIt->second.kind != SourceNodeKind::Directory)
    {
        return std::nullopt;
    }
    if (nodeIt->second.state == DirectoryState::Queued ||
        nodeIt->second.state == DirectoryState::Loading)
    {
        return std::nullopt;
    }

    std::unique_lock queueLock(queueMutex);
    if (stopping || pending.size() >= limits.maxPendingRequests)
    {
        return std::nullopt;
    }

    CancelSubtree(directoryId);

    std::vector<SourceRequestId> removed;
    RemoveCancelledPendingLocked(removed);
    for (const auto requestId : removed)
    {
        requests.erase(requestId);
    }

    const SourceRequestId requestId = nextRequestId++;
    const auto            cancelled = std::make_shared<std::atomic_bool>(false);
    const auto            revision  = ++nodeIt->second.revision;

    requests.emplace(requestId,
                     RequestMetadata{ .nodeId       = directoryId,
                                      .revision     = revision,
                                      .rootRevision = rootRevision,
                                      .cancelled    = cancelled });

    nodeIt->second.request   = requestId;
    nodeIt->second.state     = DirectoryState::Queued;
    nodeIt->second.isPartial = false;
    nodeIt->second.partialLimitReason.clear();
    nodeIt->second.errorCode.clear();
    nodeIt->second.error.clear();
    nodeIt->second.children.clear();

    pending.push_back(WorkItem{ .requestId    = requestId,
                                .nodeId       = directoryId,
                                .revision     = revision,
                                .rootRevision = rootRevision,
                                .directory    = nodeIt->second.path,
                                .cancelled    = cancelled });
    ++inFlight;
    queueLock.unlock();
    queueCondition.notify_one();
    return requestId;
}

SourceEnumerationEntry SourceBrowser::InspectForOpen(const std::filesystem::path& path) const
{
    return inspectEntry(path);
}

bool SourceBrowser::Cancel(SourceRequestId requestId)
{
    if (shutdownRequested)
    {
        return false;
    }

    const auto requestIt = requests.find(requestId);
    if (requestIt == requests.end() || requestIt->second.cancelledByUi)
    {
        return false;
    }

    requestIt->second.cancelledByUi = true;
    if (requestIt->second.cancelled)
    {
        requestIt->second.cancelled->store(true, std::memory_order_release);
    }

    const auto nodeIt = nodes.find(requestIt->second.nodeId);
    if (nodeIt != nodes.end() && nodeIt->second.request == requestId)
    {
        nodeIt->second.state     = DirectoryState::Cancelled;
        nodeIt->second.isPartial = false;
        nodeIt->second.partialLimitReason.clear();
        nodeIt->second.errorCode.clear();
        nodeIt->second.error.clear();
    }

    std::vector<SourceRequestId> removed;
    {
        std::lock_guard lock(queueMutex);
        RemoveCancelledPendingLocked(removed);
        for (auto completionIt = completions.begin(); completionIt != completions.end();)
        {
            if (completionIt->requestId != requestId || completionIt->requestId == activeRequestId)
            {
                ++completionIt;
                continue;
            }
            completionIt = completions.erase(completionIt);
            if (std::find(removed.begin(), removed.end(), requestId) == removed.end())
            {
                if (inFlight > 0)
                {
                    --inFlight;
                }
                removed.push_back(requestId);
            }
        }
    }
    for (const auto removedId : removed)
    {
        if (removedId != requestId)
        {
            requests.erase(removedId);
        }
    }
    if (std::find(removed.begin(), removed.end(), requestId) != removed.end())
    {
        requests.erase(requestId);
        if (nodeIt != nodes.end() && nodeIt->second.request == requestId)
        {
            nodeIt->second.request.reset();
        }
    }
    completionCondition.notify_all();
    return true;
}

std::size_t SourceBrowser::Poll()
{
    if (shutdownRequested)
    {
        return 0;
    }

    std::vector<Completion> ready;
    {
        std::lock_guard lock(queueMutex);
        ready.reserve(completions.size());
        while (!completions.empty())
        {
            ready.push_back(std::move(completions.front()));
            completions.pop_front();
        }
    }
    completionCondition.notify_all();

    std::size_t consumed = 0;
    for (auto& completion : ready)
    {
        ++consumed;
        const auto requestIt = requests.find(completion.requestId);
        if (requestIt == requests.end())
        {
            continue;
        }

        const auto& metadata    = requestIt->second;
        const bool  current     = metadata.rootRevision == rootRevision &&
                                  !metadata.cancelledByUi &&
                                  completion.rootRevision == rootRevision;
        const auto  nodeIt      = nodes.find(metadata.nodeId);
        const bool  nodeMatches = nodeIt != nodes.end() &&
                                  nodeIt->second.request == completion.requestId &&
                                  nodeIt->second.revision == metadata.revision &&
                                  completion.revision == metadata.revision;

        if (completion.started)
        {
            if (current && nodeMatches)
            {
                nodeIt->second.state = DirectoryState::Loading;
            }
            continue;
        }

        if (current && nodeMatches && !completion.result.cancelled)
        {
            auto& node = nodeIt->second;
            node.children.clear();
            node.isPartial          = completion.result.partial;
            node.partialLimitReason = completion.result.partialLimitReason;
            node.errorCode          = completion.result.errorCode;
            node.error              = completion.result.error;

            std::size_t available = 0;
            if (nodes.size() < limits.maxRetainedNodes)
            {
                available = limits.maxRetainedNodes - nodes.size();
            }
            std::size_t retainedPathBytes = 0;
            for (const auto& [retainedId, retainedNode] : nodes)
            {
                static_cast<void>(retainedId);
                const auto bytes = pathBytes(retainedNode.path);
                if (bytes > limits.maxRetainedPathBytes -
                                std::min(retainedPathBytes, limits.maxRetainedPathBytes))
                {
                    retainedPathBytes = limits.maxRetainedPathBytes;
                    break;
                }
                retainedPathBytes += bytes;
            }

            std::size_t retainedEntries = 0;
            for (const auto& entry : completion.result.entries)
            {
                if (retainedEntries >= limits.maxEntriesPerDirectory)
                {
                    node.isPartial = true;
                    appendReason(node.partialLimitReason, kEntryLimitReason);
                    break;
                }
                if (available == 0)
                {
                    node.isPartial = true;
                    appendReason(node.partialLimitReason, kNodeLimitReason);
                    break;
                }
                const auto entryBytes = pathBytes(entry.path);
                const auto pathBudget = retainedPathBytes >= limits.maxRetainedPathBytes
                                            ? 0
                                            : limits.maxRetainedPathBytes - retainedPathBytes;
                if (entryBytes > pathBudget)
                {
                    node.isPartial = true;
                    appendReason(node.partialLimitReason, kPathLimitReason);
                    break;
                }

                const SourceNodeId childId = nextNodeId++;
                SourceNode         child{ .id                 = childId,
                                          .path               = entry.path,
                                          .kind               = entry.kind,
                                          .children           = {},
                                          .state              = DirectoryState::Unloaded,
                                          .revision           = 0,
                                          .request            = {},
                                          .isPartial          = false,
                                          .partialLimitReason = {},
                                          .errorCode          = entry.errorCode,
                                          .error              = entry.error };
                nodes.emplace(childId, std::move(child));
                node.children.push_back(childId);
                --available;
                ++retainedEntries;
                retainedPathBytes += entryBytes;
            }

            node.state = completion.result.errorCode || !completion.result.error.empty()
                             ? DirectoryState::Failed
                             : DirectoryState::Ready;
            node.request.reset();
        }
        else if (nodeIt != nodes.end() && nodeIt->second.request == completion.requestId)
        {
            nodeIt->second.request.reset();
            if (nodeIt->second.state == DirectoryState::Queued ||
                nodeIt->second.state == DirectoryState::Loading)
            {
                nodeIt->second.state = DirectoryState::Cancelled;
            }
        }

        requests.erase(requestIt);
        FinishRequest(completion.requestId);
    }
    return consumed;
}

void SourceBrowser::Shutdown()
{
    if (shutdownRequested)
    {
        return;
    }
    shutdownRequested = true;

    for (auto& [requestId, metadata] : requests)
    {
        static_cast<void>(requestId);
        metadata.cancelledByUi = true;
        if (metadata.cancelled)
        {
            metadata.cancelled->store(true, std::memory_order_release);
        }
    }

    std::vector<SourceRequestId> removed;
    {
        std::lock_guard lock(queueMutex);
        stopping = true;
        RemoveCancelledPendingLocked(removed);
        while (!completions.empty())
        {
            completions.pop_front();
        }
    }
    for (const auto requestId : removed)
    {
        requests.erase(requestId);
    }

    queueCondition.notify_all();
    completionCondition.notify_all();
    worker.request_stop();
    if (worker.joinable())
    {
        worker.join();
    }

    {
        std::lock_guard lock(queueMutex);
        pending.clear();
        completions.clear();
        inFlight        = 0;
        activeRequestId = kInvalidRequestId;
        activeCancellation.reset();
    }
    requests.clear();
}

SourceNodeId SourceBrowser::RootId() const noexcept
{
    return rootId;
}

const SourceNode* SourceBrowser::Find(SourceNodeId id) const noexcept
{
    const auto nodeIt = nodes.find(id);
    return nodeIt == nodes.end() ? nullptr : &nodeIt->second;
}

const SourceBrowserLimits& SourceBrowser::Limits() const noexcept
{
    return limits;
}

std::size_t SourceBrowser::BusyCount() const
{
    std::lock_guard lock(queueMutex);
    return inFlight;
}

SourceEnumerationResult SourceBrowser::EnumerateDirectory(
    const std::filesystem::path& directory,
    const SourceBrowserLimits&   limits,
    const std::atomic_bool&      cancelled)
{
    SourceEnumerationResult result;
    const auto              started = std::chrono::steady_clock::now();

    if (cancelled.load(std::memory_order_acquire))
    {
        result.cancelled = true;
        return result;
    }
    if (directory.empty())
    {
        result.errorCode = std::make_error_code(std::errc::invalid_argument);
        result.error     = "The source path is empty.";
        return result;
    }
    if (isDriveRelativePath(directory))
    {
        result.errorCode = std::make_error_code(std::errc::invalid_argument);
        result.error     = "drive-relative source paths are not supported.";
        return result;
    }
    if (hasReparsePointInPath(directory))
    {
        result.errorCode = reparseError();
        result.error     = std::string(kReparseTraversalError);
        return result;
    }

    std::error_code statusError;
    const auto      status = std::filesystem::symlink_status(directory, statusError);
    if (statusError)
    {
        result.errorCode = statusError;
        result.error     = errorMessage(statusError);
        return result;
    }
    if (!std::filesystem::is_directory(status))
    {
        result.errorCode = notDirectoryError();
        result.error     = errorMessage(result.errorCode);
        return result;
    }

    std::error_code                     iteratorError;
    std::filesystem::directory_iterator iterator(
        directory, std::filesystem::directory_options::none, iteratorError);
    if (iteratorError)
    {
        result.errorCode = iteratorError;
        result.error     = errorMessage(iteratorError);
        return result;
    }

    std::size_t retainedPathBytes = 0;
    while (iterator != std::filesystem::directory_iterator{})
    {
        if (cancelled.load(std::memory_order_acquire))
        {
            result.cancelled = true;
            return result;
        }
        if (result.entries.size() >= limits.maxEntriesPerDirectory)
        {
            result.partial            = true;
            result.partialLimitReason = std::string(kEntryLimitReason);
            break;
        }
        if (std::chrono::steady_clock::now() - started >= limits.maxEnumerationWork)
        {
            result.partial            = true;
            result.partialLimitReason = std::string(kWorkLimitReason);
            break;
        }

        const auto entryPath  = iterator->path();
        const auto entryBytes = pathBytes(entryPath);
        if (entryBytes > limits.maxRetainedPathBytes -
                             std::min(retainedPathBytes, limits.maxRetainedPathBytes))
        {
            result.partial            = true;
            result.partialLimitReason = std::string(kPathLimitReason);
            break;
        }
        result.entries.push_back(inspectEntry(entryPath));
        retainedPathBytes += entryBytes;
        iterator.increment(iteratorError);
        if (iteratorError)
        {
            result.errorCode = iteratorError;
            result.error     = errorMessage(iteratorError);
            break;
        }
    }
    std::sort(result.entries.begin(),
              result.entries.end(),
              [](const SourceEnumerationEntry& left, const SourceEnumerationEntry& right)
              {
                  const auto leftName  = left.path.filename().generic_string();
                  const auto rightName = right.path.filename().generic_string();
                  if (leftName != rightName)
                  {
                      return leftName < rightName;
                  }
                  return left.path.generic_string() < right.path.generic_string();
              });
    return result;
}

void SourceBrowser::WorkerMain()
{
    for (;;)
    {
        WorkItem work;
        {
            std::unique_lock lock(queueMutex);
            queueCondition.wait(lock, [this]
                                {
                                    return stopping || !pending.empty();
                                });
            if (stopping && pending.empty())
            {
                return;
            }

            work = std::move(pending.front());
            pending.pop_front();
            activeRequestId    = work.requestId;
            activeCancellation = work.cancelled;
        }

        auto enqueueCompletion = [this](Completion completion)
        {
            std::unique_lock lock(queueMutex);
            completionCondition.wait(lock,
                                     [this]
                                     {
                                         return stopping ||
                                                completions.size() < limits.maxPendingCompletions;
                                     });
            if (stopping)
            {
                return false;
            }
            completions.push_back(std::move(completion));
            return true;
        };

        if (!enqueueCompletion(Completion{ .requestId    = work.requestId,
                                           .nodeId       = work.nodeId,
                                           .revision     = work.revision,
                                           .rootRevision = work.rootRevision,
                                           .started      = true,
                                           .result       = {} }))
        {
            std::lock_guard lock(queueMutex);
            activeRequestId = kInvalidRequestId;
            activeCancellation.reset();
            return;
        }

        SourceEnumerationResult result;
        if (work.cancelled && work.cancelled->load(std::memory_order_acquire))
        {
            result.cancelled = true;
        }
        else
        {
            try
            {
                result = enumerator(work.directory, limits, *work.cancelled);
            }
            catch (...)
            {
                result.errorCode = std::make_error_code(std::errc::io_error);
                result.error     = "The directory enumeration callback failed.";
            }
        }

        {
            std::lock_guard lock(queueMutex);
            activeRequestId = kInvalidRequestId;
            activeCancellation.reset();
        }

        if (!enqueueCompletion(Completion{ .requestId    = work.requestId,
                                           .nodeId       = work.nodeId,
                                           .revision     = work.revision,
                                           .rootRevision = work.rootRevision,
                                           .result       = std::move(result) }))
        {
            return;
        }
    }
}

void SourceBrowser::CancelSubtree(SourceNodeId nodeId)
{
    const auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end())
    {
        return;
    }

    std::vector<SourceNodeId> toVisit = nodeIt->second.children;
    while (!toVisit.empty())
    {
        const SourceNodeId childId = toVisit.back();
        toVisit.pop_back();

        const auto childIt = nodes.find(childId);
        if (childIt == nodes.end())
        {
            continue;
        }
        toVisit.insert(toVisit.end(), childIt->second.children.begin(), childIt->second.children.end());

        if (childIt->second.request)
        {
            const auto requestIt = requests.find(*childIt->second.request);
            if (requestIt != requests.end())
            {
                requestIt->second.cancelledByUi = true;
                if (requestIt->second.cancelled)
                {
                    requestIt->second.cancelled->store(true, std::memory_order_release);
                }
            }
        }
        nodes.erase(childIt);
    }
}

void SourceBrowser::RemoveCancelledPendingLocked(std::vector<SourceRequestId>& removed)
{
    for (auto pendingIt = pending.begin(); pendingIt != pending.end();)
    {
        if (!pendingIt->cancelled || !pendingIt->cancelled->load(std::memory_order_acquire))
        {
            ++pendingIt;
            continue;
        }
        removed.push_back(pendingIt->requestId);
        pendingIt = pending.erase(pendingIt);
        if (inFlight > 0)
        {
            --inFlight;
        }
    }
}

void SourceBrowser::FinishRequest(SourceRequestId requestId)
{
    std::lock_guard lock(queueMutex);
    static_cast<void>(requestId);
    if (inFlight > 0)
    {
        --inFlight;
    }
}

} // namespace rerevved::tools
