// OS/MSWindows/UltraCanvasWindowsFolderWatcher.cpp
// ReadDirectoryChangesW backend for UltraCanvasFolderWatcher: watches one
// directory (not its subtree) and reports every change to its entries.
//
// The directory handle is opened with FILE_SHARE_DELETE alongside read and
// write sharing, so watching a folder never stops anyone - this process
// included - from renaming or deleting it. The read loop runs on its own
// thread and waits on two events at once, the overlapped read's and a stop
// event, so Stop() returns promptly instead of waiting out the next change.
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework

#include "UltraCanvasFolderWatcher.h"
#include "UltraCanvasUtils.h"   // Utf8ToWide

#include <atomic>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace UltraCanvas {

    namespace {

        // Everything that changes what a file display shows: names appearing,
        // disappearing and changing, sizes, write times and attributes.
        constexpr DWORD kWatchFilter =
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE;

        class WindowsFolderWatchBackend final : public IFolderWatchBackend {
        public:
            ~WindowsFolderWatchBackend() override { Stop(); }

            bool Start(const std::string& path,
                       std::function<void()> onChanged,
                       std::function<void()> onFailed) override {
                Stop();

                // FILE_LIST_DIRECTORY + BACKUP_SEMANTICS is what opening a
                // directory handle requires; OVERLAPPED lets the read be
                // cancelled by the stop event instead of blocking forever.
                directory = ::CreateFileW(
                        Utf8ToWide(path).c_str(),
                        FILE_LIST_DIRECTORY,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr,
                        OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                        nullptr);
                if (directory == INVALID_HANDLE_VALUE) return false;

                // Manual-reset, initially unsignalled - both are waited on
                // together and neither may auto-reset behind the other.
                readEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!readEvent || !stopEvent) {
                    CloseHandles();
                    return false;
                }

                callback = std::move(onChanged);
                failedCallback = std::move(onFailed);
                failureReported = false;
                stopping.store(false);
                worker = std::thread([this]() { Run(); });
                return true;
            }

            void Stop() override {
                if (worker.joinable()) {
                    stopping.store(true);
                    if (stopEvent) ::SetEvent(stopEvent);
                    // Break a read that is already in flight; the thread then
                    // sees the stop event rather than waiting for a change.
                    if (directory != INVALID_HANDLE_VALUE) ::CancelIoEx(directory, nullptr);
                    worker.join();
                }
                CloseHandles();
                callback = nullptr;
                failedCallback = nullptr;
                failureReported = false;
            }

        private:
            void CloseHandles() {
                if (directory != INVALID_HANDLE_VALUE) {
                    ::CloseHandle(directory);
                    directory = INVALID_HANDLE_VALUE;
                }
                if (readEvent) { ::CloseHandle(readEvent); readEvent = nullptr; }
                if (stopEvent) { ::CloseHandle(stopEvent); stopEvent = nullptr; }
            }

            // Tell the caller the watch is over, exactly once, and never
            // because of Stop() - a failure it asked for is a failure it can
            // act on, a Stop() it performed is not news.
            void ReportFailure() {
                if (failureReported || stopping.load() || !failedCallback) return;
                failureReported = true;
                failedCallback();
            }

            void Run() {
                // DWORD-aligned, as ReadDirectoryChangesW requires of its
                // buffer. 64 KiB is the documented ceiling that still works on
                // a network share; a batch too large to fit simply arrives as
                // an overflow, which we treat as "something changed" anyway.
                std::vector<DWORD> buffer(64 * 1024 / sizeof(DWORD));

                while (!stopping.load()) {
                    OVERLAPPED overlapped{};
                    overlapped.hEvent = readEvent;
                    ::ResetEvent(readEvent);

                    DWORD bytes = 0;
                    if (!::ReadDirectoryChangesW(
                                directory, buffer.data(),
                                static_cast<DWORD>(buffer.size() * sizeof(DWORD)),
                                FALSE,          // this folder only, not the subtree
                                kWatchFilter, &bytes, &overlapped, nullptr)) {
                        // The handle went bad under us: the volume was
                        // removed, the share dropped, the folder deleted.
                        ReportFailure();
                        return;
                    }

                    HANDLE waits[2] = {readEvent, stopEvent};
                    const DWORD signalled =
                            ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                    if (signalled != WAIT_OBJECT_0) {
                        // Stop, or the wait itself failed: cancel the pending
                        // read and let it drain before the buffer goes away.
                        ::CancelIoEx(directory, &overlapped);
                        ::GetOverlappedResult(directory, &overlapped, &bytes, TRUE);
                        ReportFailure();        // no-op when this was Stop()
                        return;
                    }

                    DWORD transferred = 0;
                    if (!::GetOverlappedResult(directory, &overlapped, &transferred, FALSE)) {
                        ReportFailure();        // cancelled or broken
                        return;
                    }
                    if (stopping.load()) return;

                    // One report per batch: a single save produces several
                    // records, and `transferred == 0` is the overflow case -
                    // the folder changed, we just do not know how.
                    if (callback) callback();
                }
            }

            HANDLE directory = INVALID_HANDLE_VALUE;
            HANDLE readEvent = nullptr;
            HANDLE stopEvent = nullptr;
            std::atomic<bool> stopping{false};
            std::thread worker;
            std::function<void()> callback;
            std::function<void()> failedCallback;
            bool failureReported = false;   // worker thread only
        };

    } // namespace

    std::unique_ptr<IFolderWatchBackend> CreateNativeFolderWatchBackend() {
        return std::make_unique<WindowsFolderWatchBackend>();
    }

} // namespace UltraCanvas
