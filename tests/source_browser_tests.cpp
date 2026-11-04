#include "inspect/source_browser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{

using namespace rerevved::tools;

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (condition)
    {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool isReparsePoint(const std::filesystem::path& path)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return true;
    }
#endif
    std::error_code error;
    const auto      status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_symlink(status);
}

bool hasReparseAncestor(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return false;
    }
    std::error_code error;
    auto            current = path.is_absolute() ? path : std::filesystem::current_path(error) / path;
    if (error)
    {
        current = path;
    }
    while (!current.empty())
    {
        if (isReparsePoint(current))
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

#ifdef _WIN32
bool createJunction(const std::filesystem::path& target, const std::filesystem::path& link)
{
    if (!CreateDirectoryW(link.c_str(), nullptr))
    {
        return false;
    }

    HANDLE handle = CreateFileW(link.c_str(),
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        std::cerr << "junction CreateFile failed: " << GetLastError() << '\n';
        RemoveDirectoryW(link.c_str());
        return false;
    }

    struct MountPointBuffer
    {
        DWORD tag;
        WORD  dataLength;
        WORD  reserved;
        WORD  substituteOffset;
        WORD  substituteLength;
        WORD  printOffset;
        WORD  printLength;
        WCHAR pathBuffer[2048];
    } buffer{};

    std::wstring printName = std::filesystem::absolute(target).wstring();
    if (printName.rfind(L"\\\\?\\", 0) == 0)
    {
        printName.erase(0, 4);
    }
    std::wstring substituteName  = L"\\??\\" + printName;
    const auto   substituteBytes = substituteName.size() * sizeof(wchar_t);
    const auto   printOffset     = substituteBytes + sizeof(wchar_t);
    const auto   dataLength      = substituteBytes + sizeof(wchar_t) + 10;
    if (printOffset > sizeof(buffer.pathBuffer) || dataLength > UINT16_MAX)
    {
        CloseHandle(handle);
        RemoveDirectoryW(link.c_str());
        return false;
    }

    buffer.tag              = IO_REPARSE_TAG_MOUNT_POINT;
    buffer.dataLength       = static_cast<WORD>(dataLength);
    buffer.substituteOffset = 0;
    buffer.substituteLength = static_cast<WORD>(substituteBytes);
    buffer.printOffset      = static_cast<WORD>(printOffset);
    buffer.printLength      = 0;
    std::memcpy(buffer.pathBuffer, substituteName.data(), substituteBytes);

    DWORD      returned = 0;
    const BOOL set      = DeviceIoControl(handle,
                                          FSCTL_SET_REPARSE_POINT,
                                          &buffer,
                                          static_cast<DWORD>(8 + dataLength),
                                          nullptr,
                                          0,
                                          &returned,
                                          nullptr);
    CloseHandle(handle);
    if (!set)
    {
        RemoveDirectoryW(link.c_str());
        return false;
    }
    return true;
}
#endif

void removeOwned(const std::filesystem::path& path)
{
    if (hasReparseAncestor(path.parent_path()))
    {
        return;
    }
    std::error_code statusError;
    const auto      status = std::filesystem::symlink_status(path, statusError);
    if (statusError || status.type() == std::filesystem::file_type::not_found)
    {
        return;
    }
    if (isReparsePoint(path) || !std::filesystem::is_directory(status))
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        return;
    }

    std::error_code                     iteratorError;
    std::filesystem::directory_iterator iterator(path, iteratorError);
    if (!iteratorError)
    {
        while (iterator != std::filesystem::directory_iterator{})
        {
            const auto child = iterator->path();
            iterator.increment(iteratorError);
            if (iteratorError)
            {
                break;
            }
            removeOwned(child);
        }
    }

    std::error_code error;
    std::filesystem::remove(path, error);
}

struct Fixture
{
    explicit Fixture(const std::filesystem::path& base, std::string_view label)
    {
        static std::atomic_uint64_t sequence = 0;
        if (hasReparseAncestor(base))
        {
            std::cerr << "FAIL: fixture base has a reparse ancestor\n";
            ++failures;
            return;
        }
        const auto prefix = std::string(label) + "-";
        for (std::size_t attempt = 0; attempt < 100 && !owned; ++attempt)
        {
            root = base / (prefix +
                           std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            std::error_code error;
            owned = std::filesystem::create_directory(root, error);
            if (!owned && error)
            {
                std::cerr << "FAIL: fixture creation: " << error.message() << '\n';
                ++failures;
                return;
            }
        }
        if (!owned)
        {
            std::cerr << "FAIL: fixture creation found no unique child\n";
            ++failures;
        }
    }

    ~Fixture()
    {
        if (owned)
        {
            removeOwned(root);
        }
    }

    Fixture(const Fixture&)            = delete;
    Fixture& operator=(const Fixture&) = delete;

    std::filesystem::path root;
    bool                  owned = false;
};

void writeFile(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::size_t pathStorageBytes(const std::filesystem::path& path)
{
    return path.native().size() * sizeof(path.native()[0]);
}

bool waitUntil(SourceBrowser&               browser,
               const std::function<bool()>& predicate,
               std::chrono::milliseconds    timeout = std::chrono::milliseconds(3000))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        static_cast<void>(browser.Poll());
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    static_cast<void>(browser.Poll());
    return predicate();
}

const SourceNode* childWithName(const SourceBrowser& browser,
                                const SourceNode&    parent,
                                std::string_view     name)
{
    for (const auto childId : parent.children)
    {
        const auto* child = browser.Find(childId);
        if (child != nullptr && child->path.filename().string() == name)
        {
            return child;
        }
    }
    return nullptr;
}

SourceEnumerationResult listSyntheticDirectory(const std::filesystem::path& directory)
{
    SourceEnumerationResult             result;
    std::error_code                     iteratorError;
    std::filesystem::directory_iterator iterator(directory, iteratorError);
    if (iteratorError)
    {
        result.errorCode = iteratorError;
        result.error     = iteratorError.message();
        return result;
    }
    while (iterator != std::filesystem::directory_iterator{})
    {
        std::error_code statusError;
        const auto      status = std::filesystem::symlink_status(iterator->path(), statusError);
        SourceNodeKind  kind   = SourceNodeKind::Other;
        if (!statusError)
        {
            if (isReparsePoint(iterator->path()))
            {
                kind = SourceNodeKind::ReparsePoint;
            }
            else if (std::filesystem::is_directory(status))
            {
                kind = SourceNodeKind::Directory;
            }
            else if (std::filesystem::is_regular_file(status))
            {
                kind = SourceNodeKind::File;
            }
        }
        result.entries.push_back(SourceEnumerationEntry{ .path      = iterator->path(),
                                                         .kind      = kind,
                                                         .errorCode = statusError,
                                                         .error     = statusError.message() });
        iterator.increment(iteratorError);
        if (iteratorError)
        {
            result.errorCode = iteratorError;
            result.error     = iteratorError.message();
            break;
        }
    }
    std::sort(result.entries.begin(),
              result.entries.end(),
              [](const SourceEnumerationEntry& left, const SourceEnumerationEntry& right)
              {
                  return left.path.filename().string() < right.path.filename().string();
              });
    return result;
}

struct Gate
{
    std::mutex              mutex;
    std::condition_variable condition;
    bool                    started = false;
    bool                    release = false;
};

SourceEnumerator gatedEnumerator(Gate& gate)
{
    return [&gate](const std::filesystem::path& directory,
                   const SourceBrowserLimits&,
                   const std::atomic_bool& cancelled)
    {
        if (directory.filename() == "slow")
        {
            std::unique_lock lock(gate.mutex);
            gate.started = true;
            gate.condition.notify_all();
            gate.condition.wait(lock, [&gate]
                                {
                                    return gate.release;
                                });
            if (cancelled.load(std::memory_order_acquire))
            {
                SourceEnumerationResult result;
                result.cancelled = true;
                return result;
            }
        }
        return listSyntheticDirectory(directory);
    };
}

void waitForGate(Gate& gate)
{
    std::unique_lock lock(gate.mutex);
    const bool       reached = gate.condition.wait_for(
        lock, std::chrono::seconds(3), [&gate]
        {
            return gate.started;
        });
    expect(reached, "delayed enumerator started");
}

void releaseGate(Gate& gate)
{
    {
        std::lock_guard lock(gate.mutex);
        gate.release = true;
    }
    gate.condition.notify_all();
}

void testImmediateChildren(const std::filesystem::path& base)
{
    Fixture fixture(base, "children");
    if (!fixture.owned)
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directory(fixture.root / "alpha", error);
    std::filesystem::create_directory(fixture.root / "beta", error);
    writeFile(fixture.root / "z.txt", "source bytes");

    const auto    before = std::filesystem::file_size(fixture.root / "z.txt");
    SourceBrowser browser;
    browser.SetRoot(fixture.root);
    const auto rootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(rootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "root directory request completed");

    const auto* root = browser.Find(rootId);
    expect(root != nullptr && root->children.size() == 3, "immediate children retained");
    if (root != nullptr && root->children.size() == 3)
    {
        expect(root->path == std::filesystem::absolute(fixture.root),
               "root path remains the typed directory");
        expect(browser.Find(root->children[0])->path.filename() == "alpha",
               "children are sorted deterministically");
        expect(browser.Find(root->children[1])->path.filename() == "beta",
               "second child is sorted deterministically");
        expect(browser.Find(root->children[2])->kind == SourceNodeKind::File,
               "file child has file kind");
    }
    expect(std::filesystem::file_size(fixture.root / "z.txt") == before,
           "enumeration does not modify source bytes");
}

void testRootErrors(const std::filesystem::path& base)
{
    Fixture fixture(base, "errors");
    if (!fixture.owned)
    {
        return;
    }
    const auto missing = fixture.root / "missing";
    const auto file    = fixture.root / "plain.txt";
    writeFile(file, "bytes");

    SourceBrowser browser;
    browser.SetRoot(missing);
    const auto missingRootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(missingRootId);
                         return root != nullptr && root->state == DirectoryState::Failed;
                     }),
           "missing root request completes");
    const auto* missingRoot = browser.Find(browser.RootId());
    expect(missingRoot != nullptr && missingRoot->state == DirectoryState::Failed,
           "missing root remains visible as failed");
    expect(missingRoot != nullptr && !missingRoot->error.empty(),
           "missing root retains its filesystem error");

    browser.SetRoot(file);
    const auto fileRootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(fileRootId);
                         return root != nullptr && root->state == DirectoryState::Failed;
                     }),
           "file root request completes");
    const auto* fileRoot = browser.Find(browser.RootId());
    expect(fileRoot != nullptr && fileRoot->kind == SourceNodeKind::Directory,
           "invalid file root retains its directory placeholder");
    expect(fileRoot != nullptr && fileRoot->state == DirectoryState::Failed &&
               !fileRoot->error.empty(),
           "file root reports not-a-directory failure");
}

void testPartialLimits(const std::filesystem::path& base)
{
    Fixture fixture(base, "partial");
    if (!fixture.owned)
    {
        return;
    }
    std::error_code error;
    for (int index = 0; index < 8; ++index)
    {
        writeFile(fixture.root / ("entry-" + std::to_string(index)), "x");
    }

    SourceBrowserLimits entryLimits;
    entryLimits.maxEntriesPerDirectory = 3;
    entryLimits.maxRetainedNodes       = 32;
    SourceBrowser browser(entryLimits);
    browser.SetRoot(fixture.root);
    const auto rootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(rootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "entry-limited request completed");
    const auto* entryRoot = browser.Find(rootId);
    expect(entryRoot != nullptr && entryRoot->children.size() == 3,
           "entry limit bounds retained children");
    expect(entryRoot != nullptr && entryRoot->isPartial &&
               entryRoot->partialLimitReason.find("entry") != std::string::npos,
           "entry limit exposes a clear partial reason");

    SourceBrowserLimits nodeLimits;
    nodeLimits.maxEntriesPerDirectory = 32;
    nodeLimits.maxRetainedNodes       = 3;
    SourceBrowser bounded(nodeLimits);
    bounded.SetRoot(fixture.root);
    const auto boundedRootId = bounded.RootId();
    expect(waitUntil(bounded,
                     [&]
                     {
                         const auto* root = bounded.Find(boundedRootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "node-limited request completed");
    const auto* boundedRoot = bounded.Find(boundedRootId);
    expect(boundedRoot != nullptr && boundedRoot->children.size() == 2,
           "total retained-node limit bounds children");
    expect(boundedRoot != nullptr && boundedRoot->partialLimitReason.find("Retained") !=
                                         std::string::npos,
           "retained-node limit is visible");

    Fixture siblingFixture(base, "partial-siblings");
    if (!siblingFixture.owned)
    {
        return;
    }
    const auto left  = siblingFixture.root / "left";
    const auto right = siblingFixture.root / "right";
    std::filesystem::create_directory(left, error);
    std::filesystem::create_directory(right, error);
    const auto leftLeaf  = left / "left-leaf.txt";
    const auto rightLeaf = right / "right-leaf.txt";
    writeFile(leftLeaf, "x");
    writeFile(rightLeaf, "x");
    SourceBrowserLimits pathLimits;
    pathLimits.maxRetainedPathBytes = pathStorageBytes(siblingFixture.root) +
                                      pathStorageBytes(left) + pathStorageBytes(right) +
                                      pathStorageBytes(leftLeaf) + 1;
    SourceBrowser pathBrowser(pathLimits);
    pathBrowser.SetRoot(siblingFixture.root);
    const auto pathRootId = pathBrowser.RootId();
    expect(waitUntil(pathBrowser,
                     [&]
                     {
                         const auto* root = pathBrowser.Find(pathRootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "global path-limit root request completed");
    const auto* pathRoot  = pathBrowser.Find(pathRootId);
    const auto* leftNode  = pathRoot == nullptr ? nullptr : childWithName(pathBrowser, *pathRoot, "left");
    const auto* rightNode = pathRoot == nullptr ? nullptr : childWithName(pathBrowser, *pathRoot, "right");
    expect(leftNode != nullptr && rightNode != nullptr, "global path-limit siblings retained");
    if (leftNode != nullptr && rightNode != nullptr)
    {
        expect(pathBrowser.Request(leftNode->id).has_value(), "left sibling request accepted");
        expect(waitUntil(pathBrowser,
                         [&]
                         {
                             const auto* node = pathBrowser.Find(leftNode->id);
                             return node != nullptr && node->state == DirectoryState::Ready;
                         }),
               "left sibling request completed");
        expect(pathBrowser.Request(rightNode->id).has_value(), "right sibling request accepted");
        expect(waitUntil(pathBrowser,
                         [&]
                         {
                             const auto* node = pathBrowser.Find(rightNode->id);
                             return node != nullptr && node->state == DirectoryState::Ready;
                         }),
               "right sibling request completed");
        const auto* rightAfter = pathBrowser.Find(rightNode->id);
        expect(rightAfter != nullptr && rightAfter->isPartial &&
                   rightAfter->partialLimitReason.find("path") != std::string::npos,
               "retained path limit applies across sibling expansions");
    }
}

void testRetry(const std::filesystem::path& base)
{
    Fixture fixture(base, "retry");
    if (!fixture.owned)
    {
        return;
    }
    const auto      retryPath = fixture.root / "retry-dir";
    std::error_code error;
    std::filesystem::create_directory(retryPath, error);

    SourceBrowser browser;
    browser.SetRoot(fixture.root);
    const auto rootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(rootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "retry root request completed");
    const auto* retryNode = childWithName(browser, *browser.Find(rootId), "retry-dir");
    expect(retryNode != nullptr, "retry directory retained");
    if (retryNode == nullptr)
    {
        return;
    }
    const auto retryId = retryNode->id;
    expect(browser.Request(retryId).has_value(), "first retry directory request accepted");
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* node = browser.Find(retryId);
                         return node != nullptr && node->state == DirectoryState::Ready;
                     }),
           "first retry directory request completed");

    std::filesystem::remove(retryPath, error);
    expect(browser.Request(retryId).has_value(), "retry after removal accepted");
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* node = browser.Find(retryId);
                         return node != nullptr && node->state == DirectoryState::Failed;
                     }),
           "retry reports the removed directory error");
    std::filesystem::create_directory(retryPath, error);
    expect(browser.Request(retryId).has_value(), "retry after repair accepted");
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* node = browser.Find(retryId);
                         return node != nullptr && node->state == DirectoryState::Ready;
                     }),
           "retry after repair succeeds");
}

void testCancellation(const std::filesystem::path& base)
{
    Fixture fixture(base, "cancel");
    if (!fixture.owned)
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directory(fixture.root / "slow", error);
    Gate          gate;
    SourceBrowser browser(SourceBrowserLimits{}, gatedEnumerator(gate));
    browser.SetRoot(fixture.root);
    const auto rootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(rootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "cancellation root request completed");
    const auto* slow = childWithName(browser, *browser.Find(rootId), "slow");
    expect(slow != nullptr, "slow directory retained");
    if (slow == nullptr)
    {
        return;
    }
    const auto slowId     = slow->id;
    const auto slowTicket = browser.Request(slowId);
    expect(slowTicket.has_value(), "delayed request accepted");
    waitForGate(gate);
    expect(slowTicket.has_value() && browser.Cancel(*slowTicket),
           "active delayed request cancelled");
    releaseGate(gate);
    expect(waitUntil(browser, [&]
                     {
                         return browser.BusyCount() == 0;
                     }),
           "cancelled request drains its stale completion");
    const auto* cancelled = browser.Find(slowId);
    expect(cancelled != nullptr && cancelled->state == DirectoryState::Cancelled,
           "cancelled directory exposes cancelled state");
    expect(cancelled != nullptr && cancelled->children.empty(),
           "cancelled directory retains no stale children");
}

void testStaleRootReplacement(const std::filesystem::path& base)
{
    Fixture fixture(base, "stale-root");
    if (!fixture.owned)
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directory(fixture.root / "slow", error);
    const auto replacement = fixture.root / "replacement";
    std::filesystem::create_directory(replacement, error);

    Gate          gate;
    SourceBrowser browser(SourceBrowserLimits{}, gatedEnumerator(gate));
    browser.SetRoot(fixture.root);
    const auto oldRootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(oldRootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "stale-root listing completed");
    const auto* slow = childWithName(browser, *browser.Find(oldRootId), "slow");
    expect(slow != nullptr, "stale-root slow child retained");
    if (slow == nullptr)
    {
        return;
    }
    expect(browser.Request(slow->id).has_value(), "stale-root delayed request accepted");
    waitForGate(gate);

    browser.SetRoot(replacement);
    const auto newRootId = browser.RootId();
    releaseGate(gate);
    expect(waitUntil(browser, [&]
                     {
                         return browser.BusyCount() == 0;
                     }),
           "root replacement drains stale completion");
    expect(newRootId != oldRootId && browser.Find(oldRootId) == nullptr,
           "root replacement rejects the old node model");
    expect(browser.Find(newRootId) != nullptr && browser.Find(newRootId)->path == replacement,
           "replacement root remains visible");
}

void testCompletionAccounting(const std::filesystem::path& base)
{
    Fixture fixture(base, "completion-accounting");
    if (!fixture.owned)
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directory(fixture.root / "first", error);
    std::filesystem::create_directory(fixture.root / "second", error);
    const auto replacement = fixture.root / "replacement";
    std::filesystem::create_directory(replacement, error);
    Gate                secondGate;
    SourceBrowserLimits limits;
    limits.maxPendingCompletions = 4;
    SourceBrowser browser(
        limits,
        [&secondGate](const std::filesystem::path& directory,
                      const SourceBrowserLimits&,
                      const std::atomic_bool& cancelled)
        {
            if (directory.filename() == "second")
            {
                std::unique_lock lock(secondGate.mutex);
                secondGate.started = true;
                secondGate.condition.notify_all();
                secondGate.condition.wait(lock, [&secondGate]
                                          {
                                              return secondGate.release;
                                          });
                if (cancelled.load(std::memory_order_acquire))
                {
                    SourceEnumerationResult result;
                    result.cancelled = true;
                    return result;
                }
            }
            return listSyntheticDirectory(directory);
        });
    browser.SetRoot(fixture.root);
    const auto oldRootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(oldRootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "completion-accounting root request completed");
    const auto* oldRoot = browser.Find(oldRootId);
    const auto* first   = oldRoot == nullptr ? nullptr : childWithName(browser, *oldRoot, "first");
    const auto* second  = oldRoot == nullptr ? nullptr : childWithName(browser, *oldRoot, "second");
    expect(first != nullptr && second != nullptr, "completion-accounting children retained");
    if (first == nullptr || second == nullptr)
    {
        return;
    }

    expect(browser.Request(first->id).has_value(), "first completion request accepted");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expect(browser.Request(second->id).has_value(), "second active request accepted");
    waitForGate(secondGate);

    browser.SetRoot(replacement);
    expect(browser.BusyCount() == 2,
           "started and final completion markers release one request exactly once");
    releaseGate(secondGate);
    const auto newRootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(newRootId);
                         return root != nullptr && root->state == DirectoryState::Ready &&
                                browser.BusyCount() == 0;
                     }),
           "completion-accounting replacement drains all work");
}

void testPendingBounds(const std::filesystem::path& base)
{
    Fixture fixture(base, "pending");
    if (!fixture.owned)
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directory(fixture.root / "slow", error);
    std::filesystem::create_directory(fixture.root / "fast", error);
    std::filesystem::create_directory(fixture.root / "extra", error);
    Gate                gate;
    SourceBrowserLimits limits;
    limits.maxPendingRequests = 1;
    SourceBrowser browser(limits, gatedEnumerator(gate));
    browser.SetRoot(fixture.root);
    const auto rootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(rootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "pending-bound root request completed");

    const auto* slow = childWithName(browser, *browser.Find(rootId), "slow");
    expect(slow != nullptr, "pending-bound slow child retained");
    if (slow == nullptr)
    {
        return;
    }
    const auto slowTicket = browser.Request(slow->id);
    expect(slowTicket.has_value(), "pending-bound active request accepted");
    waitForGate(gate);

    const auto* fast = childWithName(browser, *browser.Find(rootId), "fast");
    expect(fast != nullptr && browser.Request(fast->id).has_value(),
           "one pending request accepted");
    const auto* extra = childWithName(browser, *browser.Find(rootId), "extra");
    expect(extra != nullptr && !browser.Request(extra->id).has_value(),
           "pending request limit rejects excess work");
    if (slowTicket)
    {
        static_cast<void>(browser.Cancel(*slowTicket));
    }
    releaseGate(gate);
}

void testShutdownJoin(const std::filesystem::path& base)
{
    Fixture fixture(base, "shutdown");
    if (!fixture.owned)
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directory(fixture.root / "slow", error);
    Gate          gate;
    SourceBrowser browser(SourceBrowserLimits{}, [&gate](const std::filesystem::path& directory, const SourceBrowserLimits&, const std::atomic_bool&)
                          {
                              if (directory.filename() == "slow")
                              {
                                  std::unique_lock lock(gate.mutex);
                                  gate.started = true;
                                  gate.condition.notify_all();
                                  gate.condition.wait(lock, [&gate]
                                                      {
                                                          return gate.release;
                                                      });
                              }
                              return listSyntheticDirectory(directory);
                          });
    browser.SetRoot(fixture.root);
    const auto rootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(rootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "shutdown root request completed");
    const auto* slow = childWithName(browser, *browser.Find(rootId), "slow");
    expect(slow != nullptr, "shutdown slow child retained");
    if (slow == nullptr)
    {
        return;
    }
    expect(browser.Request(slow->id).has_value(), "shutdown delayed request accepted");
    waitForGate(gate);

    std::atomic_bool shutdownDone = false;
    std::thread      shutdownThread([&]
                                    {
                                   browser.Shutdown();
                                   shutdownDone.store(true, std::memory_order_release);
                                    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    expect(!shutdownDone.load(std::memory_order_acquire),
           "shutdown waits for the active enumeration to return");
    releaseGate(gate);
    shutdownThread.join();
    expect(shutdownDone.load(std::memory_order_acquire),
           "shutdown joins the persistent worker");
    expect(browser.BusyCount() == 0, "shutdown clears in-flight work");
}

void testReparseHandling(const std::filesystem::path& base)
{
    Fixture fixture(base, "reparse");
    if (!fixture.owned)
    {
        return;
    }
    std::error_code error;
    const auto      target            = fixture.root / "target";
    const auto      link              = fixture.root / "link";
    const auto      replacementTarget = fixture.root / "replacement-target";
    const auto      replacementFile   = fixture.root / "replace-me";
    std::filesystem::create_directory(target, error);
    std::filesystem::create_directory(replacementTarget, error);
    writeFile(replacementFile, "cached file bytes");
#ifdef _WIN32
    const bool linkCreated = createJunction(target, link);
#else
    std::filesystem::create_directory_symlink(target, link, error);
    const bool linkCreated = !error;
#endif
    if (!linkCreated)
    {
        std::cerr << "FAIL: positive reparse fixture creation failed\n";
        ++failures;
        return;
    }

    SourceBrowser browser;
    browser.SetRoot(fixture.root);
    const auto rootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(rootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "reparse root request completed");
    const auto* linkNode = childWithName(browser, *browser.Find(rootId), "link");
    expect(linkNode != nullptr && linkNode->kind == SourceNodeKind::ReparsePoint,
           "reparse child is classified without traversal");
    expect(linkNode == nullptr || !browser.Request(linkNode->id).has_value(),
           "reparse child cannot be requested");

    const auto* replacementNode = childWithName(browser, *browser.Find(rootId), "replace-me");
    expect(replacementNode != nullptr && replacementNode->kind == SourceNodeKind::File,
           "cached regular file retained before replacement");
    std::filesystem::remove(replacementFile, error);
#ifdef _WIN32
    const bool replacementLinkCreated = createJunction(replacementTarget, replacementFile);
#else
    std::filesystem::create_directory_symlink(replacementTarget, replacementFile, error);
    const bool replacementLinkCreated = !error;
#endif
    expect(replacementLinkCreated, "replacement reparse fixture created");
    if (replacementLinkCreated)
    {
        const auto revalidated = browser.InspectForOpen(replacementFile);
        expect(revalidated.kind == SourceNodeKind::ReparsePoint && !revalidated.error.empty(),
               "explicit open revalidates a replaced cached file");
    }

    browser.SetRoot(link);
    const auto* linkRoot   = browser.Find(browser.RootId());
    const auto  linkRootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(linkRootId);
                         return root != nullptr && root->state == DirectoryState::Failed;
                     }),
           "reparse root request completes");
    linkRoot = browser.Find(linkRootId);
    expect(linkRoot != nullptr && linkRoot->kind == SourceNodeKind::Directory &&
               linkRoot->state == DirectoryState::Failed &&
               linkRoot->error.find("Reparse") != std::string::npos,
           "reparse root is visible and blocked");

    browser.SetRoot(link / ".." / "target");
    const auto ancestorRootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(ancestorRootId);
                         return root != nullptr && root->state == DirectoryState::Failed;
                     }),
           "reparse ancestor request completes");
    const auto* ancestorRoot = browser.Find(ancestorRootId);
    expect(ancestorRoot != nullptr &&
               ancestorRoot->error.find("Reparse") != std::string::npos,
           "reparse ancestor and lexical dot path are blocked");
}

void testSourceImmutability(const std::filesystem::path& base)
{
    Fixture fixture(base, "immutable");
    if (!fixture.owned)
    {
        return;
    }
    const auto source = fixture.root / "payload.bin";
    writeFile(source, "immutable payload bytes");
    std::ifstream     beforeFile(source, std::ios::binary);
    const std::string before((std::istreambuf_iterator<char>(beforeFile)), {});

    SourceBrowser browser;
    browser.SetRoot(fixture.root);
    const auto rootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(rootId);
                         return root != nullptr && root->state == DirectoryState::Ready;
                     }),
           "immutability request completed");
    std::ifstream     afterFile(source, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(afterFile)), {});
    expect(before == after, "source bytes remain immutable after browsing");
}

#ifdef _WIN32
void testDriveRelativeRoot()
{
    const std::filesystem::path driveRelative = L"D:source-folder";
    SourceBrowser               browser;
    browser.SetRoot(driveRelative);
    const auto rootId = browser.RootId();
    expect(waitUntil(browser,
                     [&]
                     {
                         const auto* root = browser.Find(rootId);
                         return root != nullptr && root->state == DirectoryState::Failed;
                     }),
           "drive-relative root request completes");
    const auto* root = browser.Find(rootId);
    expect(root != nullptr && root->path == driveRelative &&
               root->error.find("drive-relative") != std::string::npos,
           "drive-relative root is retained with a visible path-form error");
    const auto inspected = browser.InspectForOpen(driveRelative);
    expect(inspected.error.find("drive-relative") != std::string::npos,
           "drive-relative explicit open is rejected before filesystem access");
}
#endif

void writeMeasurementFixture(const std::filesystem::path& directory, std::size_t count)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    for (std::size_t index = 0; index < count; ++index)
    {
        writeFile(directory / ("entry-" + std::to_string(index)), "x");
    }
}

void runMeasurement(const std::filesystem::path& directory,
                    std::size_t                  count,
                    std::string_view             profile,
                    SourceBrowserLimits          limits)
{
    SourceBrowser browser(limits);
    browser.SetRoot(directory);
    const auto               rootId = browser.RootId();
    const auto               start  = std::chrono::steady_clock::now();
    std::chrono::nanoseconds pollTime{};
    while (browser.BusyCount() != 0)
    {
        const auto pollStart = std::chrono::steady_clock::now();
        static_cast<void>(browser.Poll());
        pollTime += std::chrono::steady_clock::now() - pollStart;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    static_cast<void>(browser.Poll());
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    const auto* measurementRoot = browser.Find(rootId);
    std::cout << "measurement profile=" << profile << " entries=" << count
              << " max_entries=" << limits.maxEntriesPerDirectory
              << " max_nodes=" << limits.maxRetainedNodes
              << " max_path_bytes=" << limits.maxRetainedPathBytes
              << " max_work_ms=" << limits.maxEnumerationWork.count()
              << " end_to_end_ms=" << elapsed.count()
              << " poll_apply_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(pollTime).count()
              << " retained=" << (measurementRoot == nullptr ? 0 : measurementRoot->children.size())
              << " partial=" << (measurementRoot != nullptr && measurementRoot->isPartial)
              << " reason="
              << (measurementRoot == nullptr ? std::string{} : measurementRoot->partialLimitReason)
              << '\n';
}

void measureDirectory(const std::filesystem::path& base, std::size_t count)
{
    const auto directory = base / ("measure-" + std::to_string(count));
    writeMeasurementFixture(directory, count);
    SourceBrowserLimits fullLimits;
    fullLimits.maxEntriesPerDirectory = count + 1;
    fullLimits.maxRetainedNodes       = count + 2;
    fullLimits.maxRetainedPathBytes   = std::numeric_limits<std::size_t>::max();
    fullLimits.maxEnumerationWork     = std::chrono::seconds(30);
    runMeasurement(directory, count, "full", fullLimits);
    runMeasurement(directory, count, "default", SourceBrowserLimits{});
}

int runMeasurements(const std::filesystem::path& base)
{
    Fixture fixture(base, "measurements");
    if (!fixture.owned)
    {
        return 1;
    }
#ifdef _WIN32
    constexpr std::string_view platform = "windows";
#else
    constexpr std::string_view platform = "non-windows";
#endif
    const auto started = std::chrono::steady_clock::now();
    measureDirectory(fixture.root, 1000);
    measureDirectory(fixture.root, 10000);
    measureDirectory(fixture.root, 50000);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::cout << "measurement_run=single platform=" << platform
              << " fixture=one-directory-files-entry-N-payload-1B"
              << " measurement_fixture_total_ms=" << elapsed.count() << '\n';
    return failures == 0 ? 0 : 1;
}

int runTests(const std::filesystem::path& base)
{
    testImmediateChildren(base);
    testRootErrors(base);
    testPartialLimits(base);
    testRetry(base);
    testCancellation(base);
    testStaleRootReplacement(base);
    testCompletionAccounting(base);
    testPendingBounds(base);
    testShutdownJoin(base);
    testReparseHandling(base);
    testSourceImmutability(base);
#ifdef _WIN32
    testDriveRelativeRoot();
#endif
    if (failures == 0)
    {
        std::cout << "source_browser_tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path base;
    if (argc >= 3 && std::string_view(argv[1]) == "--measure")
    {
        base = std::filesystem::path(argv[2]);
    }
    else if (argc >= 2)
    {
        base = std::filesystem::path(argv[1]);
    }
    else
    {
        base = std::filesystem::temp_directory_path() / "rerevved-tools-source-browser";
    }

    std::error_code error;
    std::filesystem::create_directories(base, error);
    if (error)
    {
        std::cerr << "FAIL: test base creation: " << error.message() << '\n';
        return 1;
    }
    return argc >= 3 && std::string_view(argv[1]) == "--measure" ? runMeasurements(base)
                                                                 : runTests(base);
}
