// OS/MacOS/UltraCanvasMacOSPortableDevices.mm
// Image Capture backend for UltraCanvasPortableDevices: an iPhone's or an
// iPad's photos and videos, and a camera's card, the way Photos and Image
// Capture.app reach them (ImageCaptureCore). macOS has no MTP of its own, so
// an Android phone is not among them.
//
// Read-only: Image Capture copies files off a device; it cannot put a file on
// an iPhone, make a folder or rename one, and it does not say how full the
// device is. Those calls fail with a reason, and every device is listed with
// PortableDeviceInfo::readOnly set so a file manager does not offer them.
//
// Image Capture is asynchronous and answers on the MAIN thread: the device
// browser, a session opening, the content catalog, a download all report
// through delegates and completion blocks delivered there. A caller's thread
// therefore hands its work to the main queue and waits for the answer with a
// timeout - never on the main thread itself, which would wait for an answer
// only it could deliver: a call made there fails at once (List() excepted,
// which only reads what the browser last reported). The objects are touched
// on the main thread only.
//
// The first look at a device opens a session and waits for its catalog. On an
// iPhone that needs the phone unlocked and "Trust This Computer" accepted;
// until then the session does not open, and the error says so.
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#include "UltraCanvasPortableDevices.h"

#import <Foundation/Foundation.h>
#import <ImageCaptureCore/ImageCaptureCore.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

    // What the browser reported, readable from any thread.
    struct SharedState {
        std::mutex mutex;
        std::vector<UltraCanvas::PortableDeviceInfo> devices;   // under mutex
        std::set<std::string> ready;    // catalog complete, under mutex
        std::map<std::string, std::string> openErrors;   // under mutex
    };

    SharedState& Shared() {
        static SharedState* state = new SharedState();   // lives with the process
        return *state;
    }

    std::string ToUtf8(NSString* text) {
        const char* utf8 = text ? [text UTF8String] : nullptr;
        return utf8 ? std::string(utf8) : std::string();
    }

    NSString* ToNS(const std::string& text) {
        return [NSString stringWithUTF8String:text.c_str()] ?: @"";
    }

    UltraCanvas::PortableDeviceKind KindOf(ICDevice* device) {
        NSString* name = device.name ?: @"";
        for (NSString* word in @[@"iPhone", @"iPad", @"iPod"])
            if ([name rangeOfString:word options:NSCaseInsensitiveSearch].location != NSNotFound)
                return UltraCanvas::PortableDeviceKind::Phone;
        return UltraCanvas::PortableDeviceKind::Camera;
    }

    // Waits for `done` to come true, polling - the answer arrives on the main
    // thread, and a poll lets a caller's progress check be asked meanwhile.
    // False when `timeout` passed first (a zero timeout waits as long as it
    // takes).
    bool WaitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout,
                 const std::function<bool()>& keepGoing = {}) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!done()) {
            if (keepGoing && !keepGoing()) return false;
            if (timeout.count() > 0 && std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return true;
    }

} // namespace

// ===== THE DELEGATE (main thread) =====

@interface UCImageCaptureHelper : NSObject <ICDeviceBrowserDelegate, ICCameraDeviceDelegate>
@property (strong) ICDeviceBrowser* browser;
@property (strong) NSMutableArray<ICCameraDevice*>* cameras;
- (void)publish;
- (ICCameraDevice*)cameraWithId:(NSString*)deviceId;
@end

@implementation UCImageCaptureHelper

- (instancetype)init {
    if ((self = [super init])) {
        self.cameras = [NSMutableArray array];
        ICDeviceBrowser* browser = [[ICDeviceBrowser alloc] init];
        self.browser = browser;
#if !__has_feature(objc_arc)
        [browser release];
#endif
        self.browser.delegate = self;
        // Two option types of their own, OR-ed as the integers they are.
        self.browser.browsedDeviceTypeMask = (ICDeviceTypeMask)(
                static_cast<NSUInteger>(ICDeviceTypeMaskCamera) |
                static_cast<NSUInteger>(ICDeviceLocationTypeMaskLocal));
        [self.browser start];
    }
    return self;
}

// The connected cameras as the C++ side reads them.
- (void)publish {
    std::vector<UltraCanvas::PortableDeviceInfo> devices;
    for (ICCameraDevice* camera in self.cameras) {
        UltraCanvas::PortableDeviceInfo info;
        info.id = ToUtf8(camera.UUIDString);
        info.name = ToUtf8(camera.name);
        if (info.name.empty()) info.name = "Camera";
        info.kind = KindOf(camera);
        info.readOnly = true;
        devices.push_back(std::move(info));
    }
    std::lock_guard<std::mutex> lk(Shared().mutex);
    Shared().devices = std::move(devices);
}

- (ICCameraDevice*)cameraWithId:(NSString*)deviceId {
    for (ICCameraDevice* camera in self.cameras)
        if ([camera.UUIDString isEqualToString:deviceId]) return camera;
    return nil;
}

- (void)forget:(ICDevice*)device {
    const std::string id = ToUtf8(device.UUIDString);
    std::lock_guard<std::mutex> lk(Shared().mutex);
    Shared().ready.erase(id);
    Shared().openErrors.erase(id);
}

// ---- ICDeviceBrowserDelegate ----

- (void)deviceBrowser:(ICDeviceBrowser*)browser didAddDevice:(ICDevice*)device moreComing:(BOOL)moreComing {
    (void)browser; (void)moreComing;
    if (![device isKindOfClass:[ICCameraDevice class]]) return;
    device.delegate = self;
    [self.cameras addObject:(ICCameraDevice*)device];
    [self publish];
}

- (void)deviceBrowser:(ICDeviceBrowser*)browser didRemoveDevice:(ICDevice*)device moreGoing:(BOOL)moreGoing {
    (void)browser; (void)moreGoing;
    [self forget:device];
    [self.cameras removeObject:(ICCameraDevice*)device];
    [self publish];
}

// ---- ICDeviceDelegate ----

- (void)didRemoveDevice:(ICDevice*)device {
    [self forget:device];
    [self.cameras removeObject:(ICCameraDevice*)device];
    [self publish];
}

- (void)device:(ICDevice*)device didOpenSessionWithError:(NSError*)error {
    if (!error) return;
    std::lock_guard<std::mutex> lk(Shared().mutex);
    Shared().openErrors[ToUtf8(device.UUIDString)] = ToUtf8(error.localizedDescription);
}

- (void)device:(ICDevice*)device didCloseSessionWithError:(NSError*)error {
    (void)error;
    [self forget:device];
}

// ---- ICCameraDeviceDelegate ----

- (void)deviceDidBecomeReadyWithCompleteContentCatalog:(ICCameraDevice*)device {
    std::lock_guard<std::mutex> lk(Shared().mutex);
    Shared().ready.insert(ToUtf8(device.UUIDString));
}

- (void)cameraDevice:(ICCameraDevice*)camera didAddItems:(NSArray<ICCameraItem*>*)items { (void)camera; (void)items; }
- (void)cameraDevice:(ICCameraDevice*)camera didRemoveItems:(NSArray<ICCameraItem*>*)items { (void)camera; (void)items; }
- (void)cameraDevice:(ICCameraDevice*)camera didRenameItems:(NSArray<ICCameraItem*>*)items { (void)camera; (void)items; }
- (void)cameraDevice:(ICCameraDevice*)camera didReceiveThumbnail:(CGImageRef)thumbnail
             forItem:(ICCameraItem*)item error:(NSError*)error {
    (void)camera; (void)thumbnail; (void)item; (void)error;
}
- (void)cameraDevice:(ICCameraDevice*)camera didReceiveMetadata:(NSDictionary*)metadata
             forItem:(ICCameraItem*)item error:(NSError*)error {
    (void)camera; (void)metadata; (void)item; (void)error;
}
- (void)cameraDeviceDidChangeCapability:(ICCameraDevice*)camera { (void)camera; }
- (void)cameraDevice:(ICCameraDevice*)camera didReceivePTPEvent:(NSData*)eventData { (void)camera; (void)eventData; }
- (void)cameraDeviceDidRemoveAccessRestriction:(ICDevice*)device { (void)device; }
- (void)cameraDeviceDidEnableAccessRestriction:(ICDevice*)device { (void)device; }

@end

namespace UltraCanvas {

    namespace {

        // The helper, made on the main thread the first time anything asks
        // and kept for the life of the process.
        UCImageCaptureHelper* g_helper = nil;

        void StartBrowsing() {
            static std::once_flag once;
            std::call_once(once, []() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (!g_helper) g_helper = [[UCImageCaptureHelper alloc] init];
                });
            });
        }

        const char* kReadOnly =
                "Image Capture can copy files off this device, but not change what is on it";

        class ImageCaptureBackend final : public IPortableDeviceBackend {
        public:
            ImageCaptureBackend() { StartBrowsing(); }

            std::vector<PortableDeviceInfo> List() override {
                std::lock_guard<std::mutex> lk(Shared().mutex);
                return Shared().devices;
            }

            bool ListFolder(const std::string& deviceId, const std::string& path,
                            std::vector<PortableDeviceEntry>& out, std::string& error) override {
                if (!EnsureReady(deviceId, error)) return false;
                auto result = std::make_shared<FolderResult>();
                NSString* nsId = ToNS(deviceId);
                const std::vector<std::string> names = SplitPortableDevicePath(path);
                dispatch_async(dispatch_get_main_queue(), ^{
                    std::string failure;
                    ICCameraItem* item = Find(nsId, names, failure);
                    if (!failure.empty()) {
                        result->error = failure;
                    } else if (item && ![item isKindOfClass:[ICCameraFolder class]]) {
                        result->error = "\"" + names.back() + "\" is a file, not a folder";
                    } else {
                        for (ICCameraItem* child in ChildrenOf(nsId, item)) {
                            PortableDeviceEntry e;
                            e.name = ToUtf8(child.name);
                            e.isDirectory = [child isKindOfClass:[ICCameraFolder class]];
                            if (!e.isDirectory)
                                e.size = static_cast<uint64_t>(((ICCameraFile*)child).fileSize);
                            NSDate* when = child.modificationDate ?: child.creationDate;
                            if (when) e.modifiedTime = static_cast<std::time_t>(when.timeIntervalSince1970);
                            result->entries.push_back(std::move(e));
                        }
                    }
                    result->done.store(true);
                });
                if (!WaitFor([result]() { return result->done.load(); }, std::chrono::seconds(30))) {
                    error = "the device did not answer";
                    return false;
                }
                if (!result->error.empty()) { error = result->error; return false; }
                out = std::move(result->entries);
                return true;
            }

            bool Download(const std::string& deviceId, const std::string& path,
                          const std::string& localFile, const PortableDeviceProgress& progress,
                          std::string& error) override {
                if (!EnsureReady(deviceId, error)) return false;
                NSString* nsId = ToNS(deviceId);
                const std::vector<std::string> names = SplitPortableDevicePath(path);
                NSURL* target = [NSURL fileURLWithPath:ToNS(localFile)];
                NSURL* folder = [target URLByDeletingLastPathComponent];
                NSString* fileName = [target lastPathComponent];
                auto result = std::make_shared<DownloadResult>();
                dispatch_async(dispatch_get_main_queue(), ^{
                    std::string failure;
                    ICCameraItem* item = Find(nsId, names, failure);
                    if (!failure.empty() || ![item isKindOfClass:[ICCameraFile class]]) {
                        result->error = failure.empty() ? "that is a folder, not a file" : failure;
                        result->done.store(true);
                        return;
                    }
                    ICCameraFile* file = (ICCameraFile*)item;
                    result->total.store(static_cast<uint64_t>(file.fileSize));
                    NSDictionary* options = @{ ICDownloadsDirectoryURL: folder,
                                               ICSaveAsFilename: fileName,
                                               ICOverwrite: @YES };
                    NSProgress* running = [file requestDownloadWithOptions:options
                                                                completion:^(NSString* saved, NSError* failed) {
                        if (failed) result->error = ToUtf8(failed.localizedDescription);
                        else result->savedAs = ToUtf8(saved);
                        result->done.store(true);
                    }];
                    result->SetProgress(running);
                });
                const bool finished = WaitFor(
                        [result]() { return result->done.load(); }, std::chrono::milliseconds(0),
                        [&]() {
                            if (!progress) return true;
                            NSProgress* p = result->Progress();
                            const uint64_t total = result->total.load();
                            const uint64_t done = p && total
                                    ? static_cast<uint64_t>(p.fractionCompleted * static_cast<double>(total))
                                    : 0;
                            return progress(done, total);
                        });
                if (!finished) {
                    NSProgress* p = result->Progress();
                    if (p) dispatch_async(dispatch_get_main_queue(), ^{ [p cancel]; });
                    [[NSFileManager defaultManager] removeItemAtURL:target error:nil];
                    error = "cancelled";
                    return false;
                }
                if (!result->error.empty()) { error = result->error; return false; }
                // Saved under another name after all (a file the device keeps
                // under a different case, say): moved to the one asked for.
                if (!result->savedAs.empty() && result->savedAs != ToUtf8(fileName)) {
                    NSURL* saved = [folder URLByAppendingPathComponent:ToNS(result->savedAs)];
                    [[NSFileManager defaultManager] removeItemAtURL:target error:nil];
                    [[NSFileManager defaultManager] moveItemAtURL:saved toURL:target error:nil];
                }
                return true;
            }

            bool Upload(const std::string&, const std::string&, const std::string&,
                        const PortableDeviceProgress&, std::string& error) override {
                error = kReadOnly;
                return false;
            }
            bool MakeFolder(const std::string&, const std::string&, std::string& error) override {
                error = kReadOnly;
                return false;
            }
            bool Delete(const std::string&, const std::string&, std::string& error) override {
                error = kReadOnly;
                return false;
            }
            bool Rename(const std::string&, const std::string&, const std::string&,
                        std::string& error) override {
                error = kReadOnly;
                return false;
            }
            bool Space(const std::string&, uint64_t&, uint64_t&, std::string& error) override {
                error = "Image Capture does not say how full a device is";
                return false;
            }

        private:
            struct FolderResult {
                std::atomic<bool> done{false};
                std::string error;
                std::vector<PortableDeviceEntry> entries;
            };
            struct DownloadResult {
                std::atomic<bool> done{false};
                std::string error;      // written before `done`, read after it
                std::string savedAs;    // likewise
                std::atomic<uint64_t> total{0};

                // The download's progress: set on the main thread, read by
                // the waiting thread, hence the lock. Retained by hand where
                // ARC is off - it is handed back autoreleased.
                void SetProgress(NSProgress* p) {
                    std::lock_guard<std::mutex> lk(lock);
#if !__has_feature(objc_arc)
                    [p retain];
                    [progress release];
#endif
                    progress = p;
                }
                NSProgress* Progress() {
                    std::lock_guard<std::mutex> lk(lock);
                    return progress;
                }
                ~DownloadResult() {
#if !__has_feature(objc_arc)
                    [progress release];
#endif
                }
            private:
                std::mutex lock;
                NSProgress* progress = nil;
            };

            // What is in the device (`item` nil) or in a folder of it; main
            // thread.
            static NSArray<ICCameraItem*>* ChildrenOf(NSString* deviceId, ICCameraItem* item) {
                if (!item) return [g_helper cameraWithId:deviceId].contents;
                return [item isKindOfClass:[ICCameraFolder class]] ? ((ICCameraFolder*)item).contents
                                                                    : nil;
            }

            // The item at `names` (main thread); nil, with no failure, for the
            // device itself.
            static ICCameraItem* Find(NSString* deviceId, const std::vector<std::string>& names,
                                      std::string& failure) {
                ICCameraDevice* camera = [g_helper cameraWithId:deviceId];
                if (!camera) { failure = "the device is no longer connected"; return nil; }
                NSArray<ICCameraItem*>* level = camera.contents;
                ICCameraItem* item = nil;
                for (const std::string& name : names) {
                    NSString* wanted = ToNS(name);
                    ICCameraItem* match = nil;
                    for (ICCameraItem* candidate in level)
                        if ([candidate.name isEqualToString:wanted]) { match = candidate; break; }
                    if (!match)
                        for (ICCameraItem* candidate in level)
                            if ([candidate.name caseInsensitiveCompare:wanted] == NSOrderedSame) {
                                match = candidate;
                                break;
                            }
                    if (!match) { failure = "\"" + name + "\" is not on the device"; return nil; }
                    item = match;
                    level = [match isKindOfClass:[ICCameraFolder class]]
                            ? ((ICCameraFolder*)match).contents : nil;
                }
                return item;
            }

            // Opens the device's session and waits for its catalog, once.
            bool EnsureReady(const std::string& deviceId, std::string& error) {
                if ([NSThread isMainThread]) {
                    // The answer would have to come through this very thread.
                    error = "a portable device cannot be read on the main thread";
                    return false;
                }
                {
                    std::lock_guard<std::mutex> lk(Shared().mutex);
                    if (Shared().ready.count(deviceId)) return true;
                    Shared().openErrors.erase(deviceId);
                }
                NSString* nsId = ToNS(deviceId);
                auto missing = std::make_shared<std::atomic<bool>>(false);
                dispatch_async(dispatch_get_main_queue(), ^{
                    ICCameraDevice* camera = [g_helper cameraWithId:nsId];
                    if (!camera) { missing->store(true); return; }
                    if (camera.hasOpenSession) {
                        // Opened before: ready once the catalog is complete.
                        if (camera.contentCatalogPercentCompleted >= 100) {
                            std::lock_guard<std::mutex> lk(Shared().mutex);
                            Shared().ready.insert(ToUtf8(nsId));
                        }
                        return;
                    }
                    [camera requestOpenSessionWithOptions:@{} completion:^(NSError* failed) {
                        if (!failed) return;
                        std::lock_guard<std::mutex> lk(Shared().mutex);
                        Shared().openErrors[ToUtf8(nsId)] = ToUtf8(failed.localizedDescription);
                    }];
                });
                std::string openError;
                const bool ready = WaitFor([&]() {
                    if (missing->load()) return true;
                    std::lock_guard<std::mutex> lk(Shared().mutex);
                    auto failed = Shared().openErrors.find(deviceId);
                    if (failed != Shared().openErrors.end()) { openError = failed->second; return true; }
                    return Shared().ready.count(deviceId) != 0;
                }, std::chrono::seconds(30));
                if (missing->load()) { error = "the device is no longer connected"; return false; }
                if (!openError.empty()) {
                    error = "the device could not be opened (" + openError + "). On an iPhone or "
                            "iPad: unlock it and tap \"Trust\" when it asks.";
                    return false;
                }
                if (!ready) {
                    error = "The device did not answer. On an iPhone or iPad: unlock it and tap "
                            "\"Trust\" when it asks, then open it again.";
                    return false;
                }
                return true;
            }
        };

    } // namespace

    std::unique_ptr<IPortableDeviceBackend> CreatePortableDeviceBackend() {
        return std::make_unique<ImageCaptureBackend>();
    }

} // namespace UltraCanvas
