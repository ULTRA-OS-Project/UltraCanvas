// OS/iOS/UltraCanvasiOSMain.mm
// The process entry for UltraCanvas applications on iOS.
//
// UIKit must own the main thread (UIApplicationMain never returns), while an
// UltraCanvas program's main() creates its windows and then blocks in
// Run(). So, as on Android, an application targeting iOS renames its main():
//
//     extern "C" int ultracanvas_app_main(int argc, char** argv);
//
// (a thin #if around the existing main body), and the framework provides
// main() here. When the application has finished launching, the delegate
// puts the root view in the key window and schedules ultracanvas_app_main()
// with performSelector:afterDelay: - a run-loop timer, not a dispatch block,
// so the nested run loop Run() pumps (CollectAndProcessNativeEvents) still
// services the main dispatch queue, and the launch callback itself returns,
// as iOS requires. By then HOME/XDG_* point into the app's container and the
// resources are the bundle's share/ (core/UltraCanvasConfig.cpp's
// exeDir + "/share/"), so path-based resource, settings and font code works
// unchanged. See Docs/UltraCanvas/iOSPortInvestigation.md 3.2 (option A).
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasiOSApplication.h"
#include "UltraCanvasiOSWindow.h"
#include "UltraCanvasDebug.h"

#import <UIKit/UIKit.h>

#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unordered_map>

// Provided by the application (its main() under a different name).
extern "C" int ultracanvas_app_main(int argc, char** argv);

namespace {
    int g_argc = 0;
    char** g_argv = nullptr;

    // Where settings, caches and data go: the container's Library, which iOS
    // keeps (Caches may be purged), set only where nothing set them already.
    void ExportSandboxEnvironment() {
        const char* home = std::getenv("HOME");
        if (!home || !*home) return;
        const std::string library = std::string(home) + "/Library";
        const std::string support = library + "/Application Support";
        const std::string caches = library + "/Caches";
        mkdir(library.c_str(), 0700);
        mkdir(support.c_str(), 0700);
        mkdir(caches.c_str(), 0700);
        setenv("XDG_CONFIG_HOME", support.c_str(), 0);
        setenv("XDG_DATA_HOME", support.c_str(), 0);
        setenv("XDG_CACHE_HOME", caches.c_str(), 0);
    }

    UltraCanvas::UltraCanvasiOSApplication* App() {
        return UltraCanvas::UltraCanvasiOSApplication::GetInstance();
    }
}

// ===== THE VIEW: touches and size changes =====

@interface UltraCanvasiOSView : UIView
@end

@implementation UltraCanvasiOSView {
    // Each finger on the screen and the small id it keeps for its touch
    // (the lowest free one, like Android's pointer ids).
    std::unordered_map<void*, int> _fingerIds;
    CGSize _lastSize;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.multipleTouchEnabled = YES;
        self.opaque = YES;
        self.backgroundColor = [UIColor blackColor];
        _lastSize = CGSizeZero;
    }
    return self;
}

- (int)idForTouch:(UITouch*)touch create:(BOOL)create {
    void* key = (__bridge void*)touch;
    auto it = _fingerIds.find(key);
    if (it != _fingerIds.end()) return it->second;
    if (!create) return -1;
    int candidate = 0;
    for (bool taken = true; taken; ) {
        taken = false;
        for (const auto& entry : _fingerIds) {
            if (entry.second == candidate) { taken = true; ++candidate; break; }
        }
    }
    _fingerIds[key] = candidate;
    return candidate;
}

- (void)deliver:(NSSet<UITouch*>*)touches phase:(int)phase {
    auto* app = App();
    for (UITouch* touch in touches) {
        const int fingerId = [self idForTouch:touch create:(phase == 0)];
        if (fingerId < 0) continue;
        if (phase >= 2) _fingerIds.erase((__bridge void*)touch);
        const CGPoint p = [touch locationInView:self];
        float pressure = 1.0f;
        if (touch.maximumPossibleForce > 0) {
            pressure = static_cast<float>(touch.force / touch.maximumPossibleForce);
        }
        if (app) {
            app->HandleTouch(phase, fingerId, static_cast<float>(p.x), static_cast<float>(p.y),
                             pressure, static_cast<int>(_fingerIds.size()), touch.timestamp);
        }
    }
    if (app) app->WakeUpEventLoopFromView();
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches phase:0];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches phase:1];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches phase:2];
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches phase:3];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    const CGSize size = self.bounds.size;
    if (CGSizeEqualToSize(size, _lastSize)) return;
    _lastSize = size;
    if (auto* app = App()) {
        if (auto* win = app->GetPrimaryWindow()) {
            win->HandleViewResized(static_cast<int>(size.width), static_cast<int>(size.height));
        }
    }
}

@end

// ===== THE VIEW CONTROLLER =====

@interface UltraCanvasiOSViewController : UIViewController
@end

@implementation UltraCanvasiOSViewController

- (void)loadView {
    self.view = [[UltraCanvasiOSView alloc] initWithFrame:[UIScreen mainScreen].bounds];
}

// The whole screen is the window's: the status bar and the home indicator
// stay out of the way until the safe area reaches the core
// (iOSPortInvestigation.md 3.3).
- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}

@end

// ===== THE APPLICATION DELEGATE =====

@interface UltraCanvasiOSAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@end

@implementation UltraCanvasiOSAppDelegate

- (BOOL)application:(UIApplication*)application
        didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    ExportSandboxEnvironment();

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    UltraCanvasiOSViewController* controller = [[UltraCanvasiOSViewController alloc] init];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    UltraCanvas::SetiOSRootView((__bridge void*)controller.view);

    // After this callback has returned: see the head of the file.
    [self performSelector:@selector(runApplication) withObject:nil afterDelay:0];
    return YES;
}

- (void)runApplication {
    const int status = ultracanvas_app_main(g_argc, g_argv);
    debugOutput << "UltraCanvas iOS: the application's main returned " << status << std::endl;
    // An iOS app does not quit itself; when its main body is over there is
    // nothing left to show.
    std::exit(status);
}

- (void)applicationDidEnterBackground:(UIApplication*)application {
    if (auto* app = App()) app->HandleDidEnterBackground();
}

- (void)applicationWillEnterForeground:(UIApplication*)application {
    if (auto* app = App()) app->HandleWillEnterForeground();
}

- (void)applicationDidBecomeActive:(UIApplication*)application {
    if (auto* app = App()) app->HandleDidBecomeActive();
}

- (void)applicationWillResignActive:(UIApplication*)application {
    if (auto* app = App()) app->HandleWillResignActive();
}

@end

int main(int argc, char* argv[]) {
    g_argc = argc;
    g_argv = argv;
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([UltraCanvasiOSAppDelegate class]));
    }
}
