// UltraCanvas/include/UltraMessage/UltraMessageEndpoint.h
// C++ convenience layer over the UltraMsg_* API: an RAII endpoint whose
// subscriptions die with it, plus typed helpers for the well-known topics so
// no application builds the feed's JSON by hand.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessage.h"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace UltraMessage {

using UltraCanvas::JSONValue;

class Endpoint;

// Unsubscribes when the last copy goes away.
class Subscription {
public:
    Subscription() = default;
    explicit Subscription(UltraMsgHandle handle);
    ~Subscription();
    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    UltraMsgHandle Handle() const { return handle_; }
    bool IsActive() const { return handle_ != UltraMsgInvalidHandle; }
    void Cancel();

private:
    UltraMsgHandle handle_ = UltraMsgInvalidHandle;
};

class Endpoint : public std::enable_shared_from_this<Endpoint> {
public:
    // Returns nullptr on failure; `error` receives the reason.
    static std::shared_ptr<Endpoint> Connect(const UltraMsgConnectOptions& options,
                                             UltraMsgResult* error = nullptr);
    ~Endpoint();
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    UltraMsgHandle Handle() const { return handle_; }
    bool IsConnected() const;
    void Disconnect();

    Subscription Subscribe(std::string_view pattern, UltraMsgCallback callback,
                           const UltraMsgSubscribeOptions& options = {});

    UltraMsgResult Post(std::string_view topic, JSONValue body,
                        const UltraMsgSendOptions& options = {},
                        std::string* outMessageId = nullptr);
    UltraMsgResult PostRecorded(std::string_view topic, JSONValue body,
                                UltraMsgBounceCallback onBounce,
                                const UltraMsgSendOptions& options = {},
                                std::string* outMessageId = nullptr);

    // Resolves with the reply, or with a message whose envelope.kind is Reply
    // and whose body carries {"error": {"code", "message"}} on failure. The
    // future is completed on the transport thread, so waiting on it from the
    // UI thread is safe.
    std::future<UltraMsgMessage> Request(std::string_view target, std::string_view topic,
                                         JSONValue body,
                                         std::chrono::milliseconds timeout,
                                         const UltraMsgSendOptions& options = {});
    UltraMsgResult RequestBlocking(std::string_view target, std::string_view topic,
                                   JSONValue body, std::chrono::milliseconds timeout,
                                   UltraMsgMessage& outReply,
                                   const UltraMsgSendOptions& options = {});

    UltraMsgResult Reply(const UltraMsgMessage& request, JSONValue body);
    UltraMsgResult ReplyError(const UltraMsgMessage& request, std::string_view code,
                              std::string_view message);
    UltraMsgResult Acknowledge(const UltraMsgMessage& message);

    UltraMsgResult Query(const UltraMsgQuery& query, std::vector<UltraMsgMessage>& out);
    UltraMsgResult MarkRead(const std::vector<std::string>& ids);
    UltraMsgResult Dismiss(const std::vector<std::string>& ids);

private:
    explicit Endpoint(UltraMsgHandle handle) : handle_(handle) {}
    UltraMsgHandle handle_ = UltraMsgInvalidHandle;
};

// ---------------------------------------------------------------------------
// Typed helpers for the well-known topics (§5.3)
// ---------------------------------------------------------------------------

struct MessagingParty {
    std::string id;
    std::string name;
    std::string avatar;
};

struct MessagingAttachmentRef {
    std::string name;
    std::string mimeType;
    int64_t size = 0;
    std::string path;
};

struct MessagingMessage {
    std::string service;        // "telegram", "signal", "whatsapp", "matrix", "sms", ...
    std::string account;
    std::string conversationId;
    std::string conversationTitle;
    bool isGroup = false;
    MessagingParty sender;
    std::string text;
    std::string html;
    bool incoming = true;       // direction: "in" / "out"
    bool read = false;
    std::string externalId;
    std::string replyTo;
    std::vector<MessagingAttachmentRef> attachments;
};

struct MailAddress {
    std::string name;
    std::string address;
};

struct MailMessage {
    std::string account;
    std::string folder;
    MailAddress from;
    std::vector<MailAddress> to;
    std::string subject;
    std::string snippet;        // first ~200 characters, plain text
    bool hasAttachments = false;
    bool read = false;
    bool flagged = false;
    std::string externalId;     // Message-ID or UID
    std::string threadId;
};

struct NotificationAction {
    std::string id;
    std::string label;
};

struct SystemNotification {
    std::string appId;
    std::string appName;
    std::string category;       // freedesktop vocabulary: "im.received", "email.arrived", ...
    std::string summary;
    std::string body;
    std::string icon;
    std::string urgency = "normal"; // "low" | "normal" | "critical"
    std::vector<NotificationAction> actions;
    std::string origin = "ultramessage";
};

JSONValue MakeMessagingMessage(const MessagingMessage& m);
bool      ParseMessagingMessage(const JSONValue& body, MessagingMessage& out);
JSONValue MakeMailMessage(const MailMessage& m);
bool      ParseMailMessage(const JSONValue& body, MailMessage& out);
JSONValue MakeSystemNotification(const SystemNotification& n);
bool      ParseSystemNotification(const JSONValue& body, SystemNotification& out);

// The conversation key the feed groups by: "<service>:<conversationId>".
std::string ConversationKey(const MessagingMessage& m);

} // namespace UltraMessage
