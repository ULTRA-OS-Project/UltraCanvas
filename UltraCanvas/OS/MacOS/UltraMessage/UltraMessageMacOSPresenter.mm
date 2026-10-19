// UltraCanvas/OS/MacOS/UltraMessage/UltraMessageMacOSPresenter.mm
// The macos-presenter adapter: the way out to the screen on macOS. A
// `system.notification` an application posts on the bus (UltraMail's "new
// mail", ...) is handed to Notification Center through the UserNotifications
// framework, so it shows as a banner and stays in the Notification Center
// list like every other application's. What the user does with it comes
// back, naming the bus message, so the application that posted it can act
// (UltraMail opens the mail): a click as the notification's `default` action,
// one of its buttons as that action, a dismissal as
// `system.notification.dismissed`. A dismissal or action posted on the bus
// takes the notification out of Notification Center; an update
// (UltraMsgFlag_Replace) changes it in place.
//
// macOS names a notification after the application that hands it over - the
// application bundle that hosts the broker - and asks the user once whether
// that application may show notifications. A notification another
// application posted names that one in its subtitle.
//
// UserNotifications works only in an application bundle. Where the broker
// runs in a process that is none (an executable started from the build tree,
// the ultramsgd daemon, a test), or where macOS refuses the bundle its
// notifications, they are shown through osascript's `display notification`
// instead: under Script Editor's name, without buttons, and a click on one
// opens nothing. Where the user did not allow notifications, nothing is
// shown and the state says where to allow them.
//
// A serial dispatch queue of its own does everything the adapter does with
// Notification Center; Present only hands it the notification and returns.
// The platform-neutral half - what the notification says, which update shows
// where, what a click publishes - is shared with the other presenters
// (UltraMessageAdapter.h) and tested on every platform.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include "../../../core/UltraMessage/UltraMessageAdapter.h"
#include "../../../core/UltraMessage/UltraMessageInternal.h"
#include "UltraMessage/UltraMessageEndpoint.h"

#include <crt_externs.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace UltraMessage {
namespace Internal {

namespace {

constexpr const char* kAdapterName = "macos-presenter";
constexpr const char* kOsascript = "/usr/bin/osascript";
// Notifications that arrive while the user is still being asked to allow
// them wait for the answer; of a flood, the newest wait.
constexpr size_t kMaxWaiting = 8;
// How long Start waits for macOS to say whether notifications are allowed.
constexpr int64_t kSettingsWaitNs = 2 * static_cast<int64_t>(NSEC_PER_SEC);

// What each notification carries for its click: the bus message, and whether
// a click is an action at all.
NSString* const kUserInfoNotificationId = @"ultramessage.notificationId";
NSString* const kUserInfoHasDefault = @"ultramessage.hasDefaultAction";

NSString* ToNSString(const std::string& text) {
    NSString* s = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    return s ? s : @"";
}

std::string FromNSString(NSString* text) {
    if (![text isKindOfClass:[NSString class]]) return std::string();
    const char* utf8 = text.UTF8String;
    return utf8 ? std::string(utf8) : std::string();
}

std::string StringField(const JSONValue& body, const char* key) {
    const JSONValue* v = body.Find(key);
    return v && v->IsString() ? v->GetString() : std::string();
}

} // namespace

// What the adapter shares with its Notification Center delegate, which may
// hear from macOS after the adapter stopped: the bus to answer on while it
// runs, and the notifications on screen.
class MacOSPresenterSink {
public:
    PresentedNotifications onScreen;

    void Attach(IAdapterHost* host) {
        std::lock_guard<std::mutex> lock(lifeMutex_);
        host_ = host;
        stopped_.store(false);
    }

    // No answer is published once this returns.
    void Detach() {
        std::lock_guard<std::mutex> lock(lifeMutex_);
        host_ = nullptr;
        stopped_.store(true);
    }

    bool Stopped() const { return stopped_.load(); }

    // The user clicked, pressed a button of or dismissed the notification
    // macOS shows under `requestId`. `carriedId` is the bus message the
    // notification itself names: the answer for one shown before the broker
    // last started, which onScreen no longer knows.
    void Respond(const std::string& requestId, const std::string& carriedId, PresenterResponse response,
                 const std::string& actionId, bool hasDefaultAction) {
        // Held while publishing, so Detach waits for an answer in flight. The
        // notice comes straight back through HandleAction, which reads only
        // onScreen: forgotten there first, so nothing is removed twice.
        std::lock_guard<std::mutex> lock(lifeMutex_);
        if (!host_) return;
        std::string notificationId = onScreen.TakeByNative(requestId);
        if (notificationId.empty()) notificationId = carriedId;
        PublishPresenterResponse(*host_, kAdapterName, notificationId, response, actionId, hasDefaultAction);
    }

private:
    std::mutex lifeMutex_;
    IAdapterHost* host_ = nullptr;
    std::atomic<bool> stopped_{true};
};

} // namespace Internal
} // namespace UltraMessage

// Notification Center's answers. A class of its own (Objective-C classes
// live outside namespaces); it forwards to the sink it shares with the
// adapter.
@interface UltraMessagePresenterDelegate : NSObject <UNUserNotificationCenterDelegate>
- (instancetype)initWithSink:(std::shared_ptr<UltraMessage::Internal::MacOSPresenterSink>)sink;
@end

@implementation UltraMessagePresenterDelegate {
    std::shared_ptr<UltraMessage::Internal::MacOSPresenterSink> _sink;
}

- (instancetype)initWithSink:(std::shared_ptr<UltraMessage::Internal::MacOSPresenterSink>)sink {
    self = [super init];
    if (self) _sink = std::move(sink);
    return self;
}

// The application that hosts the broker is in front: show its notifications
// all the same - they are mostly other applications'. Any other notification
// of this process keeps the default (not shown in front).
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    (void)center;
    using UltraMessage::Internal::kUserInfoNotificationId;
    if (![notification.request.content.userInfo[kUserInfoNotificationId] isKindOfClass:[NSString class]]) {
        completionHandler(UNNotificationPresentationOptionNone);
        return;
    }
    UNNotificationPresentationOptions options = UNNotificationPresentationOptionSound;
    if (@available(macOS 11.0, *)) {
        options |= UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList;
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options |= UNNotificationPresentationOptionAlert;
#pragma clang diagnostic pop
    }
    completionHandler(options);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler {
    (void)center;
    using namespace UltraMessage::Internal;
    UNNotificationRequest* request = response.notification.request;
    NSDictionary* info = request.content.userInfo;
    const std::string carriedId = FromNSString(info[kUserInfoNotificationId]);
    if (!carriedId.empty()) {
        NSString* action = response.actionIdentifier;
        PresenterResponse kind = PresenterResponse::Action;
        if ([action isEqualToString:UNNotificationDefaultActionIdentifier]) kind = PresenterResponse::Activated;
        else if ([action isEqualToString:UNNotificationDismissActionIdentifier]) kind = PresenterResponse::Dismissed;
        id hasDefault = info[kUserInfoHasDefault];
        const bool clickIsAction = [hasDefault isKindOfClass:[NSNumber class]] && [hasDefault boolValue];
        _sink->Respond(FromNSString(request.identifier), carriedId, kind, FromNSString(action), clickIsAction);
    }
    completionHandler();
}

@end

namespace UltraMessage {
namespace Internal {

namespace {

class MacOSPresenterAdapter final : public IAdapter {
public:
    ~MacOSPresenterAdapter() override { Stop(); }

    std::string Name() const override { return kAdapterName; }
    std::string Description() const override {
        return "Applications' notifications on screen in macOS Notification Center, with clicks, buttons and "
               "dismissals reported back (through osascript where the broker runs outside an application bundle)";
    }
    std::string Platform() const override { return "macos"; }

    UltraMsgAdapterState Start(IAdapterHost& host) override {
        Stop();
        @autoreleasepool {
            host_ = &host;
            SetState(UltraMsgAdapterStatus::Starting, "looking for Notification Center", "", "");
            auto sink = std::make_shared<MacOSPresenterSink>();
            sink->Attach(&host);
            dispatch_queue_t queue =
                dispatch_queue_create("org.ultraos.ultramessage.macos-presenter", DISPATCH_QUEUE_SERIAL);
            {
                std::lock_guard<std::mutex> lock(lifeMutex_);
                sink_ = sink;
                queue_ = queue;
            }
            const bool bundle = IsApplicationBundle();
            dispatch_semaphore_t answered = dispatch_semaphore_create(0);
            // Everything that decides how notifications are shown happens on
            // the queue, the one place that also shows them.
            dispatch_sync(queue, ^{
                permission_ = Permission::Unknown;
                waiting_.clear();
                categories_ = [NSMutableSet set];
                categoryKeys_.clear();
                if (bundle) StartNotificationCenter(answered);
                else StartScript("the broker runs in " + ProcessName() + ", which is no application bundle");
            });
            // The first answer gives the true initial state; one that takes
            // longer is applied when it comes.
            if (bundle && mode_.load() == Mode::NotificationCenter &&
                dispatch_semaphore_wait(answered, dispatch_time(DISPATCH_TIME_NOW, kSettingsWaitNs)) == 0)
                dispatch_sync(queue, ^{});
        }
        return State();
    }

    void Stop() override {
        @autoreleasepool {
            std::shared_ptr<MacOSPresenterSink> sink;
            dispatch_queue_t queue = nil;
            UNUserNotificationCenter* center = nil;
            UltraMessagePresenterDelegate* delegate = nil;
            {
                std::lock_guard<std::mutex> lock(lifeMutex_);
                sink.swap(sink_);
                queue = queue_;
                queue_ = nil;
                center = center_;
                center_ = nil;
                delegate = delegate_;
                delegate_ = nil;
            }
            mode_.store(Mode::None);
            if (sink) sink->Detach();
            // Whatever the queue was doing is done; what is queued after this
            // finds the sink detached and does nothing.
            if (queue) dispatch_sync(queue, ^{});
            if (center && delegate && center.delegate == delegate) center.delegate = nil;
        }
        host_ = nullptr;
        SetState(UltraMsgAdapterStatus::Disabled, "", "", "");
    }

    UltraMsgAdapterState State() const override {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return state_;
    }

    bool Present(const UltraMsgMessage& notification) override {
        if (mode_.load() == Mode::None) return false;
        PresentedContent content;
        if (!ReadPresentedContent(notification, CurrentProcessId(), content)) return false;
        dispatch_queue_t queue = nil;
        std::shared_ptr<MacOSPresenterSink> sink;
        {
            std::lock_guard<std::mutex> lock(lifeMutex_);
            queue = queue_;
            sink = sink_;
        }
        if (!queue || !sink || sink->Stopped()) return false;
        RunOn(queue, sink, [this, content] { Show(content); });
        return true;
    }

    bool HandleAction(const UltraMsgMessage& action) override {
        if (!action.body.IsObject()) return false;
        const std::string notificationId = StringField(action.body, "notificationId");
        if (notificationId.empty()) return false;
        std::shared_ptr<MacOSPresenterSink> sink;
        UNUserNotificationCenter* center = nil;
        {
            std::lock_guard<std::mutex> lock(lifeMutex_);
            sink = sink_;
            center = center_;
        }
        if (!sink) return false;
        const std::string requestId = sink->onScreen.TakeById(notificationId);
        if (requestId.empty()) return false;
        // The application, or the feed, acted on it: it has served its
        // purpose in Notification Center too.
        @autoreleasepool {
            if (center) {
                NSArray<NSString*>* identifiers = @[ ToNSString(requestId) ];
                [center removeDeliveredNotificationsWithIdentifiers:identifiers];
                [center removePendingNotificationRequestsWithIdentifiers:identifiers];
            }
        }
        return true;
    }

private:
    enum class Mode { None, NotificationCenter, Script };
    enum class Permission { Unknown, Asking, Granted, Denied };

    // Runs `work` on the queue unless the adapter stopped in the meantime.
    // Completion handlers of Notification Center use it too, so they never
    // touch an adapter that is gone.
    static void RunOn(dispatch_queue_t queue, std::shared_ptr<MacOSPresenterSink> sink, std::function<void()> work) {
        if (!queue || !sink) return;
        dispatch_async(queue, ^{
            if (!sink->Stopped()) work();
        });
    }

    void SetState(UltraMsgAdapterStatus status, const std::string& message, const std::string& remedy,
                  const std::string& mode) {
        UltraMsgAdapterState state;
        state.status = status;
        state.message = message;
        state.remedy = remedy;
        state.mode = mode;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_ = state;
        }
        if (host_ && status != UltraMsgAdapterStatus::Starting) host_->ReportState(kAdapterName, state);
    }

    UNUserNotificationCenter* Center() {
        std::lock_guard<std::mutex> lock(lifeMutex_);
        return center_;
    }

    dispatch_queue_t Queue() {
        std::lock_guard<std::mutex> lock(lifeMutex_);
        return queue_;
    }

    std::shared_ptr<MacOSPresenterSink> Sink() {
        std::lock_guard<std::mutex> lock(lifeMutex_);
        return sink_;
    }

    // UserNotifications needs a bundle LaunchServices knows; a process that
    // is none has no notification center and the framework raises.
    static bool IsApplicationBundle() {
        NSBundle* main = [NSBundle mainBundle];
        return main.bundleIdentifier.length > 0 &&
               [main.bundlePath.pathExtension.lowercaseString isEqualToString:@"app"];
    }

    static std::string ApplicationName() {
        NSBundle* main = [NSBundle mainBundle];
        id name = [main objectForInfoDictionaryKey:@"CFBundleDisplayName"];
        if (![name isKindOfClass:[NSString class]] || [name length] == 0)
            name = [main objectForInfoDictionaryKey:@"CFBundleName"];
        if (![name isKindOfClass:[NSString class]] || [name length] == 0) name = [NSProcessInfo processInfo].processName;
        return FromNSString(name);
    }

    static std::string ProcessName() { return FromNSString([NSProcessInfo processInfo].processName); }

    // ---- on the queue -------------------------------------------------------

    void StartNotificationCenter(dispatch_semaphore_t answered) {
        UNUserNotificationCenter* center = nil;
        @try {
            center = [UNUserNotificationCenter currentNotificationCenter];
        } @catch (NSException* exception) {
            StartScript("macOS has no notification center for " + ApplicationName() + " (" +
                        FromNSString(exception.reason) + ")");
            return;
        }
        UltraMessagePresenterDelegate* delegate = [[UltraMessagePresenterDelegate alloc] initWithSink:Sink()];
        // A delegate another part of the application installed stays; its
        // notifications' clicks are then that part's to hear.
        clicksReported_ = center.delegate == nil;
        if (clicksReported_) center.delegate = delegate;
        {
            std::lock_guard<std::mutex> lock(lifeMutex_);
            center_ = center;
            delegate_ = delegate;
        }
        mode_.store(Mode::NotificationCenter);
        SetState(UltraMsgAdapterStatus::Running, "asking macOS whether " + ApplicationName() +
                 " may show notifications", "", "notification-center");
        dispatch_queue_t queue = Queue();
        std::shared_ptr<MacOSPresenterSink> sink = Sink();
        [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* settings) {
            const UNAuthorizationStatus status = settings.authorizationStatus;
            RunOn(queue, sink, [this, status] { OnSettings(status); });
            dispatch_semaphore_signal(answered);
        }];
    }

    void OnSettings(UNAuthorizationStatus status) {
        if (mode_.load() != Mode::NotificationCenter) return;
        if (status == UNAuthorizationStatusAuthorized || status == UNAuthorizationStatusProvisional) {
            Granted();
        } else if (status == UNAuthorizationStatusDenied) {
            Denied();
        } else {
            Ask();
        }
    }

    void Ask() {
        if (permission_ == Permission::Asking) return;
        permission_ = Permission::Asking;
        SetState(UltraMsgAdapterStatus::Running, "asking the user to allow notifications for " + ApplicationName(), "",
                 "notification-center");
        UNUserNotificationCenter* center = Center();
        dispatch_queue_t queue = Queue();
        std::shared_ptr<MacOSPresenterSink> sink = Sink();
        if (!center) return;
        [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                              completionHandler:^(BOOL granted, NSError* error) {
            const bool allowed = granted == YES;
            const bool refused = error != nil;
            const std::string why = error ? FromNSString(error.localizedDescription) : std::string();
            RunOn(queue, sink, [this, allowed, refused, why] { OnAnswer(allowed, refused, why); });
        }];
    }

    void OnAnswer(bool allowed, bool refused, const std::string& why) {
        if (mode_.load() != Mode::NotificationCenter) return;
        if (allowed) {
            Granted();
        } else if (refused) {
            // macOS refuses the application itself (an unsigned bundle, one
            // LaunchServices does not know) - not the user's decision.
            StartScript("macOS refuses " + ApplicationName() + " its notifications" +
                        (why.empty() ? std::string() : " (" + why + ")"));
        } else {
            Denied();
        }
    }

    void Granted() {
        permission_ = Permission::Granted;
        std::string message = "applications' notifications are shown in Notification Center as " +
                              ApplicationName() + "'s";
        if (!clicksReported_)
            message += "; another part of " + ApplicationName() + " receives their clicks, so none are reported";
        SetState(UltraMsgAdapterStatus::Running, message, "", "notification-center");
        std::deque<PresentedContent> waiting;
        waiting.swap(waiting_);
        for (const auto& content : waiting) Deliver(content);
    }

    void Denied() {
        permission_ = Permission::Denied;
        waiting_.clear();
        mode_.store(Mode::None);
        const std::string app = ApplicationName();
        SetState(UltraMsgAdapterStatus::NeedsPermission,
                 "notifications are not allowed for " + app + ": applications' notifications reach the feed only",
                 "allow notifications for " + app + " in System Settings > Notifications, then switch the "
                 "macos-presenter adapter off and on again",
                 "none");
    }

    void StartScript(const std::string& why) {
        if (access(kOsascript, X_OK) != 0) {
            mode_.store(Mode::None);
            waiting_.clear();
            SetState(UltraMsgAdapterStatus::Unavailable,
                     why + ", and there is no " + kOsascript + " to show notifications with",
                     "start the applications from their bundles (package-macos.sh)", "none");
            return;
        }
        mode_.store(Mode::Script);
        SetState(UltraMsgAdapterStatus::Running,
                 why + ": notifications are shown through osascript, under Script Editor's name, and a click on "
                       "one opens nothing",
                 "start the applications from their bundles (package-macos.sh) for notifications under their own "
                 "name that open what they announce; where none appear, allow notifications for Script Editor in "
                 "System Settings > Notifications",
                 "script");
        std::deque<PresentedContent> waiting;
        waiting.swap(waiting_);
        for (const auto& content : waiting) ShowWithScript(content);
    }

    void Show(const PresentedContent& content) {
        switch (mode_.load()) {
            case Mode::Script:
                ShowWithScript(content);
                break;
            case Mode::NotificationCenter:
                if (permission_ == Permission::Granted) {
                    Deliver(content);
                } else {
                    waiting_.push_back(content);
                    while (waiting_.size() > kMaxWaiting) waiting_.pop_front();
                }
                break;
            case Mode::None:
                break;
        }
    }

    // One category per set of buttons, each asking for the dismissal to be
    // reported. Notification Center keeps one list per application, so the
    // whole list is set again when a set is new.
    std::string RegisterCategory(const std::vector<NotificationAction>& buttons, bool& added) {
        const std::string key = ButtonSetKey(buttons);
        added = categoryKeys_.insert(key).second;
        if (added) {
            NSMutableArray<UNNotificationAction*>* actions = [NSMutableArray array];
            for (const auto& button : buttons)
                [actions addObject:[UNNotificationAction actionWithIdentifier:ToNSString(button.id)
                                                                        title:ToNSString(button.label)
                                                                      options:UNNotificationActionOptionNone]];
            [categories_ addObject:[UNNotificationCategory
                                       categoryWithIdentifier:ToNSString(key)
                                                      actions:actions
                                            intentIdentifiers:@[]
                                                      options:UNNotificationCategoryOptionCustomDismissAction]];
            [Center() setNotificationCategories:[categories_ copy]];
        }
        return key;
    }

    // The icon as an attachment, for an image file Notification Center
    // takes. The attachment store moves the file it is given, so it gets a
    // copy. Anything else (an SVG, a theme name) shows no picture beside the
    // application's icon.
    static UNNotificationAttachment* IconAttachment(const std::string& iconFile) {
        if (iconFile.empty()) return nil;
        NSString* source = ToNSString(iconFile);
        NSString* extension = source.pathExtension.lowercaseString;
        if (![@[ @"png", @"jpg", @"jpeg", @"gif" ] containsObject:extension]) return nil;
        NSFileManager* files = [NSFileManager defaultManager];
        BOOL directory = NO;
        if (![files fileExistsAtPath:source isDirectory:&directory] || directory) return nil;
        NSString* copy = [NSTemporaryDirectory()
            stringByAppendingPathComponent:[NSString stringWithFormat:@"ultramessage-%@.%@",
                                                                      [NSUUID UUID].UUIDString, extension]];
        if (![files copyItemAtPath:source toPath:copy error:nil]) return nil;
        NSError* error = nil;
        UNNotificationAttachment* attachment =
            [UNNotificationAttachment attachmentWithIdentifier:@"icon"
                                                           URL:[NSURL fileURLWithPath:copy]
                                                       options:nil
                                                         error:&error];
        if (!attachment) [files removeItemAtPath:copy error:nil];
        return attachment;
    }

    void Deliver(const PresentedContent& content) {
        UNUserNotificationCenter* center = Center();
        std::shared_ptr<MacOSPresenterSink> sink = Sink();
        dispatch_queue_t queue = Queue();
        if (!center || !sink) return;
        const std::string requestId =
            sink->onScreen.Show(content.notificationId, content.replacesId, content.notificationId);

        UNMutableNotificationContent* body = [[UNMutableNotificationContent alloc] init];
        body.title = ToNSString(content.title);
        if (!content.subtitle.empty()) body.subtitle = ToNSString(content.subtitle);
        body.body = ToNSString(content.body);
        if (!content.appId.empty()) body.threadIdentifier = ToNSString(content.appId);
        body.userInfo = @{
            kUserInfoNotificationId : ToNSString(content.notificationId),
            kUserInfoHasDefault : @(content.hasDefaultAction),
        };
        if (content.urgency != "low") body.sound = [UNNotificationSound defaultSound];
        if (@available(macOS 12.0, *)) {
            body.interruptionLevel = content.urgency == "low"        ? UNNotificationInterruptionLevelPassive
                                     : content.urgency == "critical" ? UNNotificationInterruptionLevelTimeSensitive
                                                                     : UNNotificationInterruptionLevelActive;
        }
        if (UNNotificationAttachment* icon = IconAttachment(content.iconFile)) body.attachments = @[ icon ];
        bool newCategory = false;
        body.categoryIdentifier = ToNSString(RegisterCategory(content.buttons, newCategory));

        UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:ToNSString(requestId)
                                                                              content:body
                                                                              trigger:nil];
        NotePresented(content.title, content.body);
        const std::string notificationId = content.notificationId;
        void (^add)(void) = ^{
            [center addNotificationRequest:request
                     withCompletionHandler:^(NSError* error) {
                if (!error) return;
                const std::string why = FromNSString(error.localizedDescription);
                RunOn(queue, sink, [this, sink, notificationId, why] {
                    sink->onScreen.TakeById(notificationId);
                    SetState(UltraMsgAdapterStatus::Running, "macOS refused a notification: " + why, "",
                             "notification-center");
                });
            }];
        };
        if (newCategory) {
            // A category set a moment ago may not be in place yet for a
            // notification added at once - its buttons would be missing.
            // Reading the categories back waits for it.
            [center getNotificationCategoriesWithCompletionHandler:^(NSSet<UNNotificationCategory*>* categories) {
                (void)categories;
                add();
            }];
        } else {
            add();
        }
    }

    void ShowWithScript(const PresentedContent& content) {
        std::vector<std::string> args = AppleScriptNotificationCommand(content);
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);

        // Only stdin/stdout/stderr (to /dev/null) reach osascript, none of the
        // host application's descriptors.
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
        pid_t pid = 0;
        NotePresented(content.title, content.body);
        // (_NSGetEnviron: a shared library cannot link `environ` on macOS.)
        const int spawned = posix_spawn(&pid, kOsascript, &actions, &attributes, argv.data(), *_NSGetEnviron());
        posix_spawnattr_destroy(&attributes);
        posix_spawn_file_actions_destroy(&actions);
        if (spawned != 0) {
            SetState(UltraMsgAdapterStatus::Running, std::string("cannot run osascript: ") + std::strerror(spawned),
                     "", "script");
            return;
        }
        int status = 0;
        pid_t waited = 0;
        do {
            waited = waitpid(pid, &status, 0);
        } while (waited < 0 && errno == EINTR);
        // (ECHILD: the host application reaps its children itself.)
        if (waited == pid && WIFEXITED(status) && WEXITSTATUS(status) != 0)
            SetState(UltraMsgAdapterStatus::Running,
                     "osascript could not show a notification (exit " + std::to_string(WEXITSTATUS(status)) + ")",
                     "allow notifications for Script Editor in System Settings > Notifications", "script");
    }

    IAdapterHost* host_ = nullptr;
    std::atomic<Mode> mode_{Mode::None};

    mutable std::mutex stateMutex_;
    UltraMsgAdapterState state_;

    // Read by broker threads (Present, HandleAction) while Start and Stop
    // change them.
    std::mutex lifeMutex_;
    std::shared_ptr<MacOSPresenterSink> sink_;
    dispatch_queue_t queue_ = nil;
    UNUserNotificationCenter* center_ = nil;
    UltraMessagePresenterDelegate* delegate_ = nil;   // the center holds it weakly

    // The queue's own.
    Permission permission_ = Permission::Unknown;
    bool clicksReported_ = true;
    std::deque<PresentedContent> waiting_;
    NSMutableSet<UNNotificationCategory*>* categories_ = nil;
    std::set<std::string> categoryKeys_;
};

} // namespace

std::unique_ptr<IAdapter> CreateMacOSPresenterAdapter() {
    return std::make_unique<MacOSPresenterAdapter>();
}

} // namespace Internal
} // namespace UltraMessage
