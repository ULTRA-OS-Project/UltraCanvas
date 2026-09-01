// OS/MacOS/UltraCanvasMacOSVolumeMonitor.mm
// NSWorkspace backend for UltraCanvasVolumeMonitor: macOS posts a workspace
// notification when a volume is mounted, unmounted or renamed, so /Volumes is
// re-listed exactly when it changed.
//
// NSWorkspace's notification centre is not the default one - the mount
// notifications are posted only there - and it delivers on the main thread,
// which is where the observer must be added and removed as well. The
// documented contract of the callback ("runs on the monitor's own thread") is
// satisfied either way: it does nothing but hand the news over.
//
// No DiskArbitration dependency: AppKit is already linked, and the workspace
// notifications carry exactly the events a volume list cares about.
//
// Written to be correct with AND without ARC. The CMake rule that means to
// give OS/MacOS/*.mm `-fobjc-arc` passes an unexpanded glob PATTERN to
// set_source_files_properties, which matches no file - so this directory is
// in fact compiled with manual retain/release, where an autoreleased object
// stored in a member is a dangling pointer as soon as the pool drains. That
// mismatch compiles cleanly either way, so nothing catches it but a crash;
// the ownership below is therefore explicit rather than assumed.
// Version: 1.0.0
// Last Modified: 2026-09-01
// Author: UltraCanvas Framework

#include "UltraCanvasVolumeMonitor.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

// Ownership of the Objective-C objects this file keeps in C++ members. Under
// ARC the compiler already holds them; without it they must be retained by
// hand, because everything a convenience constructor hands back is
// autoreleased.
#if __has_feature(objc_arc)
#define ULTRACANVAS_OBJC_RETAIN(object)  (object)
#define ULTRACANVAS_OBJC_RELEASE(object) ((void)0)
#else
#define ULTRACANVAS_OBJC_RETAIN(object)  [(object) retain]
#define ULTRACANVAS_OBJC_RELEASE(object) [(object) release]
#endif

#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <sys/mount.h>
#include <sys/param.h>

namespace UltraCanvas {

    namespace {

        // What an observer block holds. Heap-owned and shared with the block
        // rather than captured through `this`, so an observation still in
        // flight cannot reach a destroyed backend; `armed` is what Stop()
        // turns off, and it is an atomic because the block may be running on
        // the main thread while Stop() is called from another one. `report`
        // is written once, before any block exists, and only read after -
        // so there is nothing to race over.
        struct ObserverState {
            std::function<void()> report;
            std::atomic<bool> armed{true};
        };

        class WorkspaceVolumeBackend final : public IVolumeMonitorBackend {
        public:
            ~WorkspaceVolumeBackend() override { Stop(); }

            bool Start(std::function<void()> onChanged) override {
                Stop();
                if (!onChanged) return false;
                state = std::make_shared<ObserverState>();
                state->report = std::move(onChanged);

                // NSWorkspace posts the mount notifications on ITS OWN centre;
                // the default notification centre never carries them.
                NSNotificationCenter* centre =
                        [[NSWorkspace sharedWorkspace] notificationCenter];
                NSArray<NSNotificationName>* names = @[
                    NSWorkspaceDidMountNotification,
                    NSWorkspaceDidUnmountNotification,
                    NSWorkspaceDidRenameVolumeNotification,
                ];

                auto shared = state;
                NSMutableArray* added = [NSMutableArray array];
                for (NSNotificationName name in names) {
                    id token = [centre addObserverForName:name
                                                   object:nil
                                                    queue:[NSOperationQueue mainQueue]
                                               usingBlock:^(NSNotification*) {
                        if (shared->armed.load() && shared->report) shared->report();
                    }];
                    if (token) [added addObject:token];
                }
                if ([added count] == 0) {
                    state.reset();
                    return false;
                }
                // `added` is autoreleased: without ARC this member would be
                // dangling by the time Stop() reads it.
                observers = ULTRACANVAS_OBJC_RETAIN(added);
                return true;
            }

            void Stop() override {
                // Disarmed first: after this no block reports, whether or not
                // the observer has been taken off the centre yet - the
                // contract Stop() owes its caller.
                if (state) state->armed.store(false);
                if (observers) {
                    NSNotificationCenter* centre =
                            [[NSWorkspace sharedWorkspace] notificationCenter];
                    for (id token in observers) [centre removeObserver:token];
                    // The centre held the tokens; the array held them too, and
                    // this gives back the reference Start() took.
                    ULTRACANVAS_OBJC_RELEASE(observers);
                    observers = nil;
                }
                state.reset();   // the blocks hold their own reference
            }

        private:
            NSMutableArray* observers = nil;
            std::shared_ptr<ObserverState> state;
        };

    } // namespace

    std::unique_ptr<IVolumeMonitorBackend> CreateNativeVolumeMonitorBackend() {
        return std::make_unique<WorkspaceVolumeBackend>();
    }

    std::set<std::string> ListPlatformMountPoints() {
        // getmntinfo() answers from the kernel's own table with no I/O to any
        // volume - which matters here, because the alternative (stat'ing each
        // candidate) blocks on a wedged network mount. MNT_NOWAIT keeps it
        // from asking the filesystems to update their statistics first.
        std::set<std::string> points;
        struct statfs* mounts = nullptr;
        const int count = ::getmntinfo(&mounts, MNT_NOWAIT);
        for (int i = 0; i < count && mounts; ++i) {
            if (mounts[i].f_mntonname[0] != '\0')
                points.insert(mounts[i].f_mntonname);
        }
        return points;   // the buffer belongs to the library; nothing to free
    }

} // namespace UltraCanvas
