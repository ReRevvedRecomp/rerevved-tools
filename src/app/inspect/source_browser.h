#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rerevved::tools
{

using SourceNodeId    = std::uint64_t;
using SourceRequestId = std::uint64_t;

enum class SourceNodeKind
{
    File,
    Directory,
    ReparsePoint,
    Other,
};

enum class DirectoryState
{
    Unloaded,
    Queued,
    Loading,
    Ready,
    Failed,
    Cancelled,
};

struct SourceBrowserLimits
{
    std::size_t               maxEntriesPerDirectory = 4096;
    std::size_t               maxRetainedNodes       = 32768;
    std::size_t               maxRetainedPathBytes   = 8 * 1024 * 1024;
    std::size_t               maxPendingRequests     = 32;
    std::size_t               maxPendingCompletions  = 32;
    std::chrono::milliseconds maxEnumerationWork{ 250 };
};

struct SourceEnumerationEntry
{
    std::filesystem::path path;
    SourceNodeKind        kind = SourceNodeKind::Other;
    std::error_code       errorCode;
    std::string           error;
};

struct SourceEnumerationResult
{
    std::vector<SourceEnumerationEntry> entries;
    std::error_code                     errorCode;
    std::string                         error;
    bool                                cancelled = false;
    bool                                partial   = false;
    std::string                         partialLimitReason;
};

// The constructor supplies a filesystem enumerator when no callback is given.
using SourceEnumerator = std::function<SourceEnumerationResult(
    const std::filesystem::path&, const SourceBrowserLimits&, const std::atomic_bool&)>;

struct SourceNode
{
    SourceNodeId                   id = 0;
    std::filesystem::path          path;
    SourceNodeKind                 kind = SourceNodeKind::Other;
    std::vector<SourceNodeId>      children;
    DirectoryState                 state    = DirectoryState::Unloaded;
    std::uint64_t                  revision = 0;
    std::optional<SourceRequestId> request;
    bool                           isPartial = false;
    std::string                    partialLimitReason;
    std::error_code                errorCode;
    std::string                    error;
};

class SourceBrowser
{
public:
    explicit SourceBrowser(SourceBrowserLimits limits = {}, SourceEnumerator enumerator = {});
    ~SourceBrowser();

    SourceBrowser(const SourceBrowser&)            = delete;
    SourceBrowser& operator=(const SourceBrowser&) = delete;
    SourceBrowser(SourceBrowser&&)                 = delete;
    SourceBrowser& operator=(SourceBrowser&&)      = delete;

    // Root replacement keeps the typed path visible even when inspection fails.
    void SetRoot(const std::filesystem::path& path);

    // A null result means the node is not a directory, is already busy, or a pending limit applies.
    [[nodiscard]] std::optional<SourceRequestId> Request(SourceNodeId directoryId);

    // Rechecks metadata and reparse points without reading file bytes or changing state.
    [[nodiscard]] SourceEnumerationEntry InspectForOpen(const std::filesystem::path& path) const;

    // Cancellation is cooperative. An active filesystem call may finish before
    // its stale completion is discarded by Poll().
    [[nodiscard]] bool Cancel(SourceRequestId requestId);

    [[nodiscard]] std::size_t Poll();

    // Shutdown joins the one persistent worker. It is safe to call repeatedly.
    void Shutdown();

    [[nodiscard]] SourceNodeId               RootId() const noexcept;
    [[nodiscard]] const SourceNode*          Find(SourceNodeId id) const noexcept;
    [[nodiscard]] const SourceBrowserLimits& Limits() const noexcept;
    [[nodiscard]] std::size_t                BusyCount() const;

private:
    struct RequestMetadata
    {
        SourceNodeId                      nodeId       = 0;
        std::uint64_t                     revision     = 0;
        std::uint64_t                     rootRevision = 0;
        std::shared_ptr<std::atomic_bool> cancelled;
        bool                              cancelledByUi = false;
    };

    struct WorkItem
    {
        SourceRequestId                   requestId    = 0;
        SourceNodeId                      nodeId       = 0;
        std::uint64_t                     revision     = 0;
        std::uint64_t                     rootRevision = 0;
        std::filesystem::path             directory;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    struct Completion
    {
        SourceRequestId         requestId    = 0;
        SourceNodeId            nodeId       = 0;
        std::uint64_t           revision     = 0;
        std::uint64_t           rootRevision = 0;
        bool                    started      = false;
        SourceEnumerationResult result;
    };

    static SourceEnumerationResult EnumerateDirectory(const std::filesystem::path& directory,
                                                      const SourceBrowserLimits&   limits,
                                                      const std::atomic_bool&      cancelled);

    void WorkerMain();
    void CancelSubtree(SourceNodeId nodeId);
    void RemoveCancelledPendingLocked(std::vector<SourceRequestId>& removed);
    void FinishRequest(SourceRequestId requestId);

    SourceBrowserLimits limits;
    SourceEnumerator    enumerator;

    std::unordered_map<SourceNodeId, SourceNode>         nodes;
    std::unordered_map<SourceRequestId, RequestMetadata> requests;
    SourceNodeId                                         rootId            = 0;
    SourceNodeId                                         nextNodeId        = 1;
    SourceRequestId                                      nextRequestId     = 1;
    std::uint64_t                                        rootRevision      = 0;
    bool                                                 shutdownRequested = false;

    mutable std::mutex                queueMutex;
    std::condition_variable           queueCondition;
    std::condition_variable           completionCondition;
    std::deque<WorkItem>              pending;
    std::deque<Completion>            completions;
    std::size_t                       inFlight        = 0;
    SourceRequestId                   activeRequestId = 0;
    std::shared_ptr<std::atomic_bool> activeCancellation;
    bool                              stopping = false;
    std::jthread                      worker;
};

} // namespace rerevved::tools
