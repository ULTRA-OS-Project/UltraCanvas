// UltraCanvas/core/UltraMessage/UltraMessageAdapters.cpp
// The registry of the adapters compiled into this build (§9), and the
// translation helpers the notification adapters share. Each platform or
// plugin file contributes a factory; the build defines which exist.
// Version: 0.4.0 - the presenters' shared half (content, responses, what is on screen); macos-presenter
// Version: 0.3.0 - the presenters, and the registry of what they showed
// Version: 0.2.1 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessageAdapter.h"
#include "UltraMessageInternal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>

namespace UltraMessage {
namespace Internal {

#ifdef ULTRAMESSAGE_HAVE_GIO
// OS/Linux/UltraMessage/UltraMessageFreedesktopNotifications.cpp
std::unique_ptr<IAdapter> CreateFreedesktopNotificationsAdapter();
#endif
#ifdef ULTRAMESSAGE_HAVE_GIO
// OS/Linux/UltraMessage/UltraMessageFreedesktopPresenter.cpp
std::unique_ptr<IAdapter> CreateFreedesktopPresenterAdapter();
#endif
#ifdef ULTRAMESSAGE_HAVE_WINRT
// OS/MSWindows/UltraMessage/UltraMessageWindowsNotificationListener.cpp
std::unique_ptr<IAdapter> CreateWindowsNotificationListenerAdapter();
#endif
#ifdef _WIN32
// OS/MSWindows/UltraMessage/UltraMessageWindowsPresenter.cpp
std::unique_ptr<IAdapter> CreateWindowsPresenterAdapter();
#endif
#ifdef ULTRAMESSAGE_HAVE_MACOS_PRESENTER
// OS/MacOS/UltraMessage/UltraMessageMacOSPresenter.mm
std::unique_ptr<IAdapter> CreateMacOSPresenterAdapter();
#endif

std::vector<std::unique_ptr<IAdapter>> CreateBuiltinAdapters() {
    std::vector<std::unique_ptr<IAdapter>> adapters;
#ifdef ULTRAMESSAGE_HAVE_GIO
    adapters.push_back(CreateFreedesktopNotificationsAdapter());
    adapters.push_back(CreateFreedesktopPresenterAdapter());
#endif
#ifdef ULTRAMESSAGE_HAVE_WINRT
    adapters.push_back(CreateWindowsNotificationListenerAdapter());
#endif
#ifdef _WIN32
    adapters.push_back(CreateWindowsPresenterAdapter());
#endif
#ifdef ULTRAMESSAGE_HAVE_MACOS_PRESENTER
    adapters.push_back(CreateMacOSPresenterAdapter());
#endif
    return adapters;
}

UltraMsgSender AdapterSender(const std::string& adapterName) {
    UltraMsgSender sender;
    sender.appId = "org.ultraos.ultramessage.adapter." + adapterName;
    sender.instanceId = "adapter:" + adapterName;
    sender.processId = CurrentProcessId();
    sender.verified = true;
    sender.displayName = adapterName;
    return sender;
}

// ---------------------------------------------------------------------------
// Shared translation
// ---------------------------------------------------------------------------

std::string Lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string FirstLine(const std::string& text) {
    const size_t nl = text.find('\n');
    return nl == std::string::npos ? text : text.substr(0, nl);
}

namespace {

bool StartsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

// Substrings of a lowercased identity or display name. Heuristic by design:
// a platform without a category hint (Windows, macOS) and the many Linux
// applications that set none get their chats and mail into the feed's
// groups this way; anything else stays a plain notification.
constexpr const char* kMessengerMarks[] = {
    "telegram", "whatsapp", "signal", "discord", "slack", "teams", "messenger", "skype",
    "viber", "threema", "wechat", "matrix", "rocket.chat", "rocketchat", "mattermost",
    "zulip", "pidgin", "hexchat", "konversation", "fractal", "dino", "nheko", "ultrasocial",
};
constexpr const char* kMailMarks[] = {
    "thunderbird", "betterbird", "outlook", "windowscommunicationsapps", "evolution", "geary",
    "kmail", "mailspring", "proton", "gmail", "claws", "ultramail", "mail",
};

} // namespace

AppKind GuessAppKind(const std::string& appId, const std::string& appName) {
    const std::string haystack = Lowercase(appId) + "\n" + Lowercase(appName);
    for (const char* mark : kMessengerMarks)
        if (haystack.find(mark) != std::string::npos) return AppKind::Messenger;
    for (const char* mark : kMailMarks)
        if (haystack.find(mark) != std::string::npos) return AppKind::Mail;
    return AppKind::Unknown;
}

std::string CategoryForAppKind(AppKind kind) {
    switch (kind) {
        case AppKind::Messenger: return "im.received";
        case AppKind::Mail: return "email.arrived";
        case AppKind::Unknown: break;
    }
    return "";
}

// ---------------------------------------------------------------------------
// What the presenters showed
// ---------------------------------------------------------------------------

namespace {

struct PresentedEntry {
    std::string title;
    std::string text;
    std::chrono::steady_clock::time_point at;
};

constexpr auto kPresentedMemory = std::chrono::minutes(5);
constexpr size_t kPresentedMax = 64;

std::mutex& PresentedMutex() {
    static std::mutex mutex;
    return mutex;
}

std::deque<PresentedEntry>& PresentedEntries() {
    static std::deque<PresentedEntry> entries;
    return entries;
}

void ForgetOldPresentedLocked(std::chrono::steady_clock::time_point now) {
    auto& entries = PresentedEntries();
    while (!entries.empty() && (now - entries.front().at > kPresentedMemory || entries.size() > kPresentedMax))
        entries.pop_front();
}

} // namespace

void NotePresented(const std::string& title, const std::string& text) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(PresentedMutex());
    PresentedEntries().push_back({title, text, now});
    ForgetOldPresentedLocked(now);
}

bool WasPresented(const std::string& title, const std::string& text) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(PresentedMutex());
    ForgetOldPresentedLocked(now);
    for (const auto& entry : PresentedEntries())
        if (entry.title == title && entry.text == text) return true;
    return false;
}

// ---------------------------------------------------------------------------
// The presenters' shared half
// ---------------------------------------------------------------------------

namespace {

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string PercentDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && HexDigit(text[i + 1]) >= 0 && HexDigit(text[i + 2]) >= 0) {
            out += static_cast<char>(HexDigit(text[i + 1]) * 16 + HexDigit(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

// The absolute path an icon names: itself, or a file:// URI decoded. An
// icon-theme name ("mail-unread") or a relative path names no file.
std::string IconFilePath(const std::string& icon) {
    std::string path = icon;
    if (StartsWith(path, "file://")) {
        path = PercentDecode(path.substr(7));
        if (StartsWith(path, "localhost/")) path = path.substr(9);
    }
    if (path.empty()) return std::string();
    if (path[0] == '/') return path;
    if (path.size() > 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/'))
        return path;
    return std::string();
}

constexpr size_t kPresentedOnScreenMax = 256;

} // namespace

bool ReadPresentedContent(const UltraMsgMessage& notification, int presenterProcessId, PresentedContent& out) {
    SystemNotification n;
    if (!ParseSystemNotification(notification.body, n)) return false;
    out = PresentedContent{};
    out.notificationId = notification.envelope.id;
    if (notification.envelope.flags & UltraMsgFlag_Replace) out.replacesId = notification.envelope.replaces;
    const UltraMsgSender& from = notification.envelope.from;
    out.appId = !n.appId.empty() ? n.appId : from.appId;
    out.appName = !n.appName.empty() ? n.appName : !from.displayName.empty() ? from.displayName : from.appId;
    out.title = !n.summary.empty() ? n.summary : out.appName;
    out.body = n.body;
    if (from.processId > 0 && from.processId != presenterProcessId && out.appName != out.title)
        out.subtitle = out.appName;
    out.iconFile = IconFilePath(n.icon);
    out.urgency = (n.urgency == "low" || n.urgency == "critical") ? n.urgency : "normal";
    for (const auto& action : n.actions) {
        if (action.id.empty()) continue;
        if (action.id == "default") {
            out.hasDefaultAction = true;
        } else if (out.buttons.size() < kMaxPresentedButtons) {
            out.buttons.push_back({action.id, action.label.empty() ? action.id : action.label});
        }
    }
    return true;
}

std::string ButtonSetKey(const std::vector<NotificationAction>& buttons) {
    if (buttons.empty()) return "plain";
    // FNV-1a over the ids and labels: the same set gets the same key in every
    // run, so a set registered again after a restart matches the
    // notifications still on screen from before.
    uint64_t hash = 1469598103934665603ULL;
    auto mix = [&hash](const std::string& text) {
        for (unsigned char c : text) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        hash ^= 0x1f;
        hash *= 1099511628211ULL;
    };
    for (const auto& button : buttons) {
        mix(button.id);
        mix(button.label);
    }
    static const char* digits = "0123456789abcdef";
    std::string key = "actions-";
    for (int shift = 60; shift >= 0; shift -= 4) key += digits[(hash >> shift) & 0xf];
    return key;
}

bool PublishPresenterResponse(IAdapterHost& host, const std::string& adapterName,
                              const std::string& notificationId, PresenterResponse response,
                              const std::string& actionId, bool hasDefaultAction) {
    if (notificationId.empty()) return false;
    JSONValue body = JSONValue::MakeObject();
    body.Set("notificationId", notificationId);
    body.Set("adapter", adapterName);
    std::string topic = UltraMsgTopics::SystemNotificationAction;
    switch (response) {
        case PresenterResponse::Activated:
            // A click on a notification that declares no default action has
            // served its purpose: it is off the screen, the feed keeps it.
            if (!hasDefaultAction) return false;
            body.Set("actionId", "default");
            break;
        case PresenterResponse::Action:
            if (actionId.empty()) return false;
            body.Set("actionId", actionId);
            break;
        case PresenterResponse::Dismissed:
            body.Set("reason", "dismissed");
            topic = UltraMsgTopics::SystemNotificationDismissed;
            break;
    }
    return !host.Publish(adapterName, topic, body).empty();
}

std::string PresentedNotifications::Show(const std::string& notificationId, const std::string& replacesId,
                                         const std::string& proposed) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string native = proposed;
    if (!replacesId.empty()) {
        auto it = byId_.find(replacesId);
        if (it != byId_.end()) {
            native = it->second;
            byId_.erase(it);
        }
    }
    // Whatever showed under that identifier is replaced on screen, and the
    // notification itself shown again moves to it.
    if (auto old = byNative_.find(native); old != byNative_.end()) byId_.erase(old->second);
    if (auto again = byId_.find(notificationId); again != byId_.end()) byNative_.erase(again->second);
    byId_[notificationId] = native;
    byNative_[native] = notificationId;
    order_.push_back(notificationId);
    // A platform that keeps notifications until the user clears them may
    // never say so; the oldest are forgotten rather than kept for ever.
    while (byId_.size() > kPresentedOnScreenMax && !order_.empty()) {
        const std::string oldest = order_.front();
        order_.pop_front();
        auto it = byId_.find(oldest);
        if (it == byId_.end()) continue;
        byNative_.erase(it->second);
        byId_.erase(it);
    }
    if (order_.size() > 2 * kPresentedOnScreenMax) {
        std::deque<std::string> live;
        for (const auto& id : order_)
            if (byId_.count(id)) live.push_back(id);
        order_.swap(live);
    }
    return native;
}

std::string PresentedNotifications::TakeByNative(const std::string& nativeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byNative_.find(nativeId);
    if (it == byNative_.end()) return std::string();
    std::string notificationId = it->second;
    byId_.erase(notificationId);
    byNative_.erase(it);
    return notificationId;
}

std::string PresentedNotifications::TakeById(const std::string& notificationId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byId_.find(notificationId);
    if (it == byId_.end()) return std::string();
    std::string native = it->second;
    byNative_.erase(native);
    byId_.erase(it);
    return native;
}

size_t PresentedNotifications::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return byId_.size();
}

void PresentedNotifications::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    byId_.clear();
    byNative_.clear();
    order_.clear();
}

std::vector<std::string> AppleScriptNotificationCommand(const PresentedContent& content) {
    // `display notification` needs a text; one with a title only shows it as
    // the text under the application's name.
    std::string text = content.body;
    std::string title = content.title;
    std::string subtitle = content.subtitle;
    if (text.empty()) {
        text = title;
        title = !subtitle.empty() ? subtitle : content.appName;
        subtitle.clear();
        if (title == text) title.clear();
    }
    std::string script = "display notification (item 1 of argv)";
    if (!title.empty()) script += " with title (item 2 of argv)";
    if (!subtitle.empty()) script += " subtitle (item 3 of argv)";
    return {"/usr/bin/osascript", "-e", "on run argv", "-e", script, "-e", "end run", text, title, subtitle};
}

std::string PublishMirror(IAdapterHost& host, const std::string& adapterName,
                          const SystemNotification& n, const std::string& notificationId) {
    if (StartsWith(n.category, "im.received")) {
        const std::string service = !n.appId.empty() ? Lowercase(n.appId) : Lowercase(n.appName);
        MessagingMessage m;
        m.service = service.empty() ? "notification" : service;
        m.account = n.appName;
        m.conversationId = n.summary;
        m.conversationTitle = n.summary;
        m.sender.name = n.summary;
        m.text = n.body;
        m.incoming = true;
        m.externalId = notificationId;
        UltraMsgSendOptions options;
        options.conversation = ConversationKey(m);
        JSONValue body = MakeMessagingMessage(m);
        body.Set("mirrorOf", notificationId);
        return host.Publish(adapterName, UltraMsgTopics::MessagingMessage, body, options);
    }
    if (StartsWith(n.category, "email")) {
        MailMessage m;
        m.account = n.appName;
        m.from.name = n.summary;
        const std::string first = FirstLine(n.body);
        m.subject = first.empty() ? n.summary : first;
        m.snippet = n.body;
        m.externalId = notificationId;
        JSONValue body = MakeMailMessage(m);
        body.Set("mirrorOf", notificationId);
        return host.Publish(adapterName, UltraMsgTopics::MailMessage, body);
    }
    return "";
}

} // namespace Internal
} // namespace UltraMessage
