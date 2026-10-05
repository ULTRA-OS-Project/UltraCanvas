// UltraCanvas/core/UltraMessage/UltraMessageAdapters.cpp
// The registry of the adapters compiled into this build (§9), and the
// translation helpers the notification adapters share. Each platform or
// plugin file contributes a factory; the build defines which exist.
// Version: 0.3.0 - the presenters, and the registry of what they showed
// Version: 0.2.1 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessageAdapter.h"
#include "UltraMessageInternal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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
