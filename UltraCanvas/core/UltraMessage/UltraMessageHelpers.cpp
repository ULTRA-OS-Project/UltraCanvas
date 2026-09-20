// UltraCanvas/core/UltraMessage/UltraMessageHelpers.cpp
// The C++ convenience layer of UltraMessageEndpoint.h: the RAII Endpoint and
// Subscription, and the typed helpers that build and parse the well-known
// topic bodies so no application assembles the feed's JSON by hand.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessage/UltraMessageEndpoint.h"

#include <utility>

namespace UltraMessage {

namespace {

std::string Str(const JSONValue& json, const char* key) {
    const JSONValue* v = json.Find(key);
    return v ? v->GetString() : std::string();
}
bool Bool(const JSONValue& json, const char* key, bool fallback = false) {
    const JSONValue* v = json.Find(key);
    return v ? v->GetBoolean(fallback) : fallback;
}
int64_t Int(const JSONValue& json, const char* key, int64_t fallback = 0) {
    const JSONValue* v = json.Find(key);
    return v ? v->GetInteger(fallback) : fallback;
}
const JSONValue& Obj(const JSONValue& json, const char* key) {
    const JSONValue* v = json.Find(key);
    return v ? *v : JSONValue::NullValue();
}

} // namespace

// ===========================================================================
// Subscription / Endpoint
// ===========================================================================

Subscription::Subscription(UltraMsgHandle handle) : handle_(handle) {}

Subscription::~Subscription() { Cancel(); }

Subscription::Subscription(Subscription&& other) noexcept : handle_(other.handle_) {
    other.handle_ = UltraMsgInvalidHandle;
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        Cancel();
        handle_ = other.handle_;
        other.handle_ = UltraMsgInvalidHandle;
    }
    return *this;
}

void Subscription::Cancel() {
    if (handle_ != UltraMsgInvalidHandle) {
        UltraMsg_Unsubscribe(handle_);
        handle_ = UltraMsgInvalidHandle;
    }
}

std::shared_ptr<Endpoint> Endpoint::Connect(const UltraMsgConnectOptions& options, UltraMsgResult* error) {
    UltraMsgHandle handle = UltraMsg_Connect(options, error);
    if (handle == UltraMsgInvalidHandle) return nullptr;
    return std::shared_ptr<Endpoint>(new Endpoint(handle));
}

Endpoint::~Endpoint() { Disconnect(); }

bool Endpoint::IsConnected() const { return UltraMsg_IsConnected(handle_); }

void Endpoint::Disconnect() {
    if (handle_ != UltraMsgInvalidHandle) {
        UltraMsg_Disconnect(handle_);
        handle_ = UltraMsgInvalidHandle;
    }
}

Subscription Endpoint::Subscribe(std::string_view pattern, UltraMsgCallback callback,
                                 const UltraMsgSubscribeOptions& options) {
    return Subscription(UltraMsg_Subscribe(handle_, std::string(pattern), std::move(callback), options));
}

UltraMsgResult Endpoint::Post(std::string_view topic, JSONValue body, const UltraMsgSendOptions& options,
                              std::string* outMessageId) {
    return UltraMsg_Post(handle_, std::string(topic), body, options, outMessageId);
}

UltraMsgResult Endpoint::PostRecorded(std::string_view topic, JSONValue body, UltraMsgBounceCallback onBounce,
                                      const UltraMsgSendOptions& options, std::string* outMessageId) {
    return UltraMsg_PostRecorded(handle_, std::string(topic), body, options, std::move(onBounce), outMessageId);
}

std::future<UltraMsgMessage> Endpoint::Request(std::string_view target, std::string_view topic, JSONValue body,
                                               std::chrono::milliseconds timeout,
                                               const UltraMsgSendOptions& options) {
    auto promise = std::make_shared<std::promise<UltraMsgMessage>>();
    std::future<UltraMsgMessage> future = promise->get_future();
    // The promise is thread-agnostic; resolving it on the transport thread
    // lets a UI-thread caller wait on the future without pumping.
    UltraMsgSendOptions direct = options;
    direct.replyOnWorkerThread = true;
    UltraMsgResult sent = UltraMsg_RequestAsync(
        handle_, std::string(target), std::string(topic), body, static_cast<int>(timeout.count()),
        [promise](const UltraMsgResult& result, const UltraMsgMessage& reply) {
            if (result.ok) {
                promise->set_value(reply);
                return;
            }
            UltraMsgMessage failed = reply;
            failed.envelope.kind = UltraMsgKind::Reply;
            JSONValue error = JSONValue::MakeObject();
            error.Set("code", UltraMsg_ResultCodeName(result.code));
            error.Set("message", result.message);
            if (!failed.body.IsObject()) failed.body = JSONValue::MakeObject();
            failed.body.Set("error", std::move(error));
            promise->set_value(failed);
        },
        direct);
    if (!sent) {
        UltraMsgMessage failed;
        failed.envelope.kind = UltraMsgKind::Reply;
        JSONValue error = JSONValue::MakeObject();
        error.Set("code", UltraMsg_ResultCodeName(sent.code));
        error.Set("message", sent.message);
        failed.body = JSONValue::MakeObject();
        failed.body.Set("error", std::move(error));
        promise->set_value(failed);
    }
    return future;
}

UltraMsgResult Endpoint::RequestBlocking(std::string_view target, std::string_view topic, JSONValue body,
                                         std::chrono::milliseconds timeout, UltraMsgMessage& outReply,
                                         const UltraMsgSendOptions& options) {
    return UltraMsg_Request(handle_, std::string(target), std::string(topic), body,
                            static_cast<int>(timeout.count()), outReply, options);
}

UltraMsgResult Endpoint::Reply(const UltraMsgMessage& request, JSONValue body) {
    return UltraMsg_Reply(handle_, request, body);
}

UltraMsgResult Endpoint::ReplyError(const UltraMsgMessage& request, std::string_view code, std::string_view message) {
    return UltraMsg_ReplyError(handle_, request, std::string(code), std::string(message));
}

UltraMsgResult Endpoint::Acknowledge(const UltraMsgMessage& message) { return UltraMsg_Acknowledge(handle_, message); }

UltraMsgResult Endpoint::Query(const UltraMsgQuery& query, std::vector<UltraMsgMessage>& out) {
    return UltraMsg_Query(handle_, query, out);
}

UltraMsgResult Endpoint::MarkRead(const std::vector<std::string>& ids) { return UltraMsg_MarkRead(handle_, ids); }

UltraMsgResult Endpoint::Dismiss(const std::vector<std::string>& ids) { return UltraMsg_Dismiss(handle_, ids); }

// ===========================================================================
// Typed helpers
// ===========================================================================

std::string ConversationKey(const MessagingMessage& m) {
    return m.service + ":" + m.conversationId;
}

JSONValue MakeMessagingMessage(const MessagingMessage& m) {
    JSONValue body = JSONValue::MakeObject();
    body.Set("service", m.service);
    body.Set("account", m.account);
    JSONValue conversation = JSONValue::MakeObject();
    conversation.Set("id", m.conversationId);
    conversation.Set("title", m.conversationTitle);
    conversation.Set("isGroup", m.isGroup);
    body.Set("conversation", std::move(conversation));
    JSONValue sender = JSONValue::MakeObject();
    sender.Set("id", m.sender.id);
    sender.Set("name", m.sender.name);
    if (!m.sender.avatar.empty()) sender.Set("avatar", m.sender.avatar);
    body.Set("sender", std::move(sender));
    body.Set("text", m.text);
    if (!m.html.empty()) body.Set("html", m.html);
    body.Set("direction", m.incoming ? "in" : "out");
    body.Set("read", m.read);
    if (!m.externalId.empty()) body.Set("externalId", m.externalId);
    if (!m.replyTo.empty()) body.Set("replyTo", m.replyTo);
    JSONValue attachments = JSONValue::MakeArray();
    for (const auto& a : m.attachments) {
        JSONValue item = JSONValue::MakeObject();
        item.Set("name", a.name);
        item.Set("mime", a.mimeType);
        item.Set("size", a.size);
        if (!a.path.empty()) item.Set("path", a.path);
        attachments.Append(std::move(item));
    }
    body.Set("attachments", std::move(attachments));
    return body;
}

bool ParseMessagingMessage(const JSONValue& body, MessagingMessage& out) {
    if (!body.IsObject()) return false;
    out.service = Str(body, "service");
    out.account = Str(body, "account");
    const JSONValue& conversation = Obj(body, "conversation");
    out.conversationId = Str(conversation, "id");
    out.conversationTitle = Str(conversation, "title");
    out.isGroup = Bool(conversation, "isGroup");
    const JSONValue& sender = Obj(body, "sender");
    out.sender.id = Str(sender, "id");
    out.sender.name = Str(sender, "name");
    out.sender.avatar = Str(sender, "avatar");
    out.text = Str(body, "text");
    out.html = Str(body, "html");
    out.incoming = Str(body, "direction") != "out";
    out.read = Bool(body, "read");
    out.externalId = Str(body, "externalId");
    out.replyTo = Str(body, "replyTo");
    out.attachments.clear();
    const JSONValue& attachments = Obj(body, "attachments");
    if (attachments.IsArray()) {
        for (const JSONValue& item : attachments.GetElements()) {
            MessagingAttachmentRef ref;
            ref.name = Str(item, "name");
            ref.mimeType = Str(item, "mime");
            ref.size = Int(item, "size");
            ref.path = Str(item, "path");
            out.attachments.push_back(std::move(ref));
        }
    }
    return !out.service.empty();
}

JSONValue MakeMailMessage(const MailMessage& m) {
    JSONValue body = JSONValue::MakeObject();
    body.Set("account", m.account);
    body.Set("folder", m.folder);
    JSONValue from = JSONValue::MakeObject();
    from.Set("name", m.from.name);
    from.Set("address", m.from.address);
    body.Set("from", std::move(from));
    JSONValue to = JSONValue::MakeArray();
    for (const auto& address : m.to) {
        JSONValue item = JSONValue::MakeObject();
        item.Set("name", address.name);
        item.Set("address", address.address);
        to.Append(std::move(item));
    }
    body.Set("to", std::move(to));
    body.Set("subject", m.subject);
    body.Set("snippet", m.snippet);
    body.Set("hasAttachments", m.hasAttachments);
    body.Set("read", m.read);
    body.Set("flagged", m.flagged);
    if (!m.externalId.empty()) body.Set("externalId", m.externalId);
    if (!m.threadId.empty()) body.Set("threadId", m.threadId);
    return body;
}

bool ParseMailMessage(const JSONValue& body, MailMessage& out) {
    if (!body.IsObject()) return false;
    out.account = Str(body, "account");
    out.folder = Str(body, "folder");
    const JSONValue& from = Obj(body, "from");
    out.from.name = Str(from, "name");
    out.from.address = Str(from, "address");
    out.to.clear();
    const JSONValue& to = Obj(body, "to");
    if (to.IsArray()) {
        for (const JSONValue& item : to.GetElements()) {
            MailAddress address;
            address.name = Str(item, "name");
            address.address = Str(item, "address");
            out.to.push_back(std::move(address));
        }
    }
    out.subject = Str(body, "subject");
    out.snippet = Str(body, "snippet");
    out.hasAttachments = Bool(body, "hasAttachments");
    out.read = Bool(body, "read");
    out.flagged = Bool(body, "flagged");
    out.externalId = Str(body, "externalId");
    out.threadId = Str(body, "threadId");
    return !out.account.empty();
}

JSONValue MakeSystemNotification(const SystemNotification& n) {
    JSONValue body = JSONValue::MakeObject();
    body.Set("appId", n.appId);
    body.Set("appName", n.appName);
    body.Set("category", n.category);
    body.Set("summary", n.summary);
    body.Set("body", n.body);
    if (!n.icon.empty()) body.Set("icon", n.icon);
    body.Set("urgency", n.urgency);
    JSONValue actions = JSONValue::MakeArray();
    for (const auto& a : n.actions) {
        JSONValue item = JSONValue::MakeObject();
        item.Set("id", a.id);
        item.Set("label", a.label);
        actions.Append(std::move(item));
    }
    body.Set("actions", std::move(actions));
    body.Set("origin", n.origin);
    return body;
}

bool ParseSystemNotification(const JSONValue& body, SystemNotification& out) {
    if (!body.IsObject()) return false;
    out.appId = Str(body, "appId");
    out.appName = Str(body, "appName");
    out.category = Str(body, "category");
    out.summary = Str(body, "summary");
    out.body = Str(body, "body");
    out.icon = Str(body, "icon");
    out.urgency = Str(body, "urgency");
    if (out.urgency.empty()) out.urgency = "normal";
    out.origin = Str(body, "origin");
    out.actions.clear();
    const JSONValue& actions = Obj(body, "actions");
    if (actions.IsArray()) {
        for (const JSONValue& item : actions.GetElements()) {
            NotificationAction action;
            action.id = Str(item, "id");
            action.label = Str(item, "label");
            out.actions.push_back(std::move(action));
        }
    }
    return !out.summary.empty() || !out.appName.empty();
}

} // namespace UltraMessage
