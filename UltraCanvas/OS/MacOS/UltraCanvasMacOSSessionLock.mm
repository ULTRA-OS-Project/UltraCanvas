// OS/MacOS/UltraCanvasMacOSSessionLock.mm
// The session-lock backend of UltraCanvasSessionLock.h for macOS: the
// loginwindow posts com.apple.screenIsLocked when the screen locks (the menu
// bar's Lock Screen, Ctrl+Cmd+Q, the screensaver or display sleep asking for
// the password) and com.apple.screenIsUnlocked when the user is back. They
// arrive on the distributed notification centre, delivered here on the main
// queue - the UI thread.
//
// Written to be correct with and without ARC, for the reason
// UltraCanvasMacOSVolumeMonitor.mm explains: this directory is in fact built
// with manual retain/release.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "UltraCanvasSessionLock.h"

#import <Foundation/Foundation.h>

#if __has_feature(objc_arc)
#define ULTRACANVAS_OBJC_RETAIN(object)  (object)
#define ULTRACANVAS_OBJC_RELEASE(object) ((void)0)
#else
#define ULTRACANVAS_OBJC_RETAIN(object)  [(object) retain]
#define ULTRACANVAS_OBJC_RELEASE(object) [(object) release]
#endif

#include <atomic>
#include <memory>

namespace UltraCanvas {

    namespace {
        // Shared with the blocks rather than reached through a global, so a
        // notification already queued when Stop() runs finds `armed` off.
        struct ObserverState {
            std::atomic<bool> armed{true};
        };

        NSMutableArray* gObservers = nil;
        std::shared_ptr<ObserverState> gState;
    }

    namespace SessionLockBackend {

        bool Start() {
            if (gObservers) return true;
            gState = std::make_shared<ObserverState>();
            auto shared = gState;
            NSDistributedNotificationCenter* centre = [NSDistributedNotificationCenter defaultCenter];
            NSMutableArray* added = [NSMutableArray array];
            const struct { NSString* name; bool locked; } kinds[] = {
                {@"com.apple.screenIsLocked", true},
                {@"com.apple.screenIsUnlocked", false},
            };
            for (const auto& kind : kinds) {
                const bool locked = kind.locked;
                id token = [centre addObserverForName:kind.name
                                               object:nil
                                                queue:[NSOperationQueue mainQueue]
                                           usingBlock:^(NSNotification*) {
                    if (shared->armed.load()) NotifySessionLockChanged(locked);
                }];
                if (token) [added addObject:token];
            }
            if ([added count] == 0) {
                gState.reset();
                return false;
            }
            gObservers = ULTRACANVAS_OBJC_RETAIN(added);
            return true;
        }

        void Stop() {
            if (gState) gState->armed.store(false);
            if (gObservers) {
                NSDistributedNotificationCenter* centre = [NSDistributedNotificationCenter defaultCenter];
                for (id token in gObservers) [centre removeObserver:token];
                ULTRACANVAS_OBJC_RELEASE(gObservers);
                gObservers = nil;
            }
            gState.reset();
        }

    } // namespace SessionLockBackend

} // namespace UltraCanvas
