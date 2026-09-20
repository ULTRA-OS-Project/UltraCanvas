// UltraCanvas/include/UltraMessage/UltraMessage.h
// UltraMessage — the cross-platform message channel of the ULTRA OS stack
// (Masterfile_modules.md §13). One per-user broker, one wire protocol, RISC OS
// Wimp semantics: post, recorded post that bounces when nobody acknowledged
// it, request/reply, topic subscriptions, and a journal of the persistent
// topics the desktop feed lists.
//
// C-style surface (`UltraMsg_*` free functions, `UltraMsgResult` for blocking
// calls, opaque `UltraMsgHandle`s), the same shape as UltraNet and
// UltraDatabase. The C++ convenience layer is UltraMessageEndpoint.h.
//
// Phase 1 scope (Docs/Research/UltraMessageDesignProposal.md §13): the channel
// and the journal. Commands (§6.2 "Commands") arrive with Phase 3.
//
// Threading: every callback is delivered through the dispatcher installed with
// UltraMsg_SetUIDispatcher — an UltraCanvas application installs
// PostToUIThread through UltraMessageUltraCanvas.h — or, when none is
// installed, waits until UltraMsg_ProcessPending drains it on the caller's
// thread. Subscriptions created with onWorkerThread, and endpoints connected
// with deliverOnUIThread = false, run their callbacks on the transport thread.
// The blocking UltraMsg_Request never depends on the dispatcher, so it is safe
// to call from the UI thread.
//
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessageTypes.h"

#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Optional. Endpoints initialize the module on first use; Shutdown disconnects
// every endpoint and stops an in-process broker this process hosts.
UltraMsgResult UltraMsg_Initialize();
void           UltraMsg_Shutdown();
std::string    UltraMsg_GetVersion();

// True when a broker answers on the bus (or this process hosts one).
bool UltraMsg_IsAvailable(const std::string& busPath = std::string());

// The dispatcher every UI-thread callback goes through. Installed once per
// process; UltraMessageUltraCanvas.h supplies the UltraCanvas one.
using UltraMsgDispatcher = std::function<void(std::function<void()>)>;
void UltraMsg_SetUIDispatcher(UltraMsgDispatcher dispatcher);
bool UltraMsg_HasUIDispatcher();

// Runs the callbacks queued for endpoints that deliver on the UI thread while
// no dispatcher is installed (tools, tests, programs without an event loop).
// Returns how many callbacks ran. Pass UltraMsgInvalidHandle for every
// endpoint of this process.
int UltraMsg_ProcessPending(UltraMsgHandle endpoint = UltraMsgInvalidHandle);

// The platform default bus location for this user (§7.1), or the override
// given to the first endpoint that connected.
std::string UltraMsg_GetDefaultBusPath();
std::string UltraMsg_GetDefaultJournalPath();

// ---------------------------------------------------------------------------
// Endpoint
// ---------------------------------------------------------------------------

// Connects to the per-user broker, starting one in this process when none
// answers and options.startBrokerIfAbsent is set. Returns
// UltraMsgInvalidHandle on failure; `error` receives the reason.
UltraMsgHandle UltraMsg_Connect(const UltraMsgConnectOptions& options,
                                UltraMsgResult* error = nullptr);
UltraMsgResult UltraMsg_Disconnect(UltraMsgHandle endpoint);
bool           UltraMsg_IsConnected(UltraMsgHandle endpoint);

UltraMsgResult UltraMsg_GetBrokerInfo(UltraMsgHandle endpoint, UltraMsgBrokerInfo& out);
UltraMsgResult UltraMsg_GetEndpointInfo(UltraMsgHandle endpoint, UltraMsgEndpointInfo& out);

// Who is on the bus, and the instance ids of one running application.
UltraMsgResult UltraMsg_ListEndpoints(UltraMsgHandle endpoint,
                                      std::vector<UltraMsgEndpointInfo>& out);
UltraMsgResult UltraMsg_ResolveApp(UltraMsgHandle endpoint, const std::string& appId,
                                   std::vector<std::string>& outInstanceIds);

// ---------------------------------------------------------------------------
// Sending (the Wimp set, §6.2)
// ---------------------------------------------------------------------------

// Fire and forget, to everyone subscribed or to one target. `outMessageId`
// receives the broker-independent id assigned to the message.
UltraMsgResult UltraMsg_Post(UltraMsgHandle endpoint, const std::string& topic,
                             const UltraCanvas::JSONValue& body,
                             const UltraMsgSendOptions& options = {},
                             std::string* outMessageId = nullptr);

// Recorded delivery: the broker expects an acknowledgement from at least one
// subscriber within ttl (default 5 s); with none, `onBounce` runs with the
// original message (User_Message_Recorded semantics).
UltraMsgResult UltraMsg_PostRecorded(UltraMsgHandle endpoint, const std::string& topic,
                                     const UltraCanvas::JSONValue& body,
                                     const UltraMsgSendOptions& options,
                                     UltraMsgBounceCallback onBounce,
                                     std::string* outMessageId = nullptr);

// Blocking request/reply. `target` is an app id or an instance id; an app id
// with several instances reaches the one that connected first.
UltraMsgResult UltraMsg_Request(UltraMsgHandle endpoint, const std::string& target,
                                const std::string& topic,
                                const UltraCanvas::JSONValue& body,
                                int timeoutMs, UltraMsgMessage& outReply,
                                const UltraMsgSendOptions& options = {});

UltraMsgResult UltraMsg_RequestAsync(UltraMsgHandle endpoint, const std::string& target,
                                     const std::string& topic,
                                     const UltraCanvas::JSONValue& body,
                                     int timeoutMs, UltraMsgReplyCallback onReply,
                                     const UltraMsgSendOptions& options = {});

// Answer a Request received through a subscription.
UltraMsgResult UltraMsg_Reply(UltraMsgHandle endpoint, const UltraMsgMessage& request,
                              const UltraCanvas::JSONValue& body,
                              const std::vector<UltraMsgAttachment>& attachments = {});
UltraMsgResult UltraMsg_ReplyError(UltraMsgHandle endpoint, const UltraMsgMessage& request,
                                   const std::string& code, const std::string& message);

// Explicit acknowledgement of a RecordedNotice, for subscriptions created with
// manualAck. Otherwise the acknowledgement is sent when the callback returns.
UltraMsgResult UltraMsg_Acknowledge(UltraMsgHandle endpoint, const UltraMsgMessage& message);

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------

// Topic patterns: exact ("mail.message"), one segment ("mail.*", "*.message"),
// or everything ("#"). Returns UltraMsgInvalidHandle on failure.
UltraMsgHandle UltraMsg_Subscribe(UltraMsgHandle endpoint, const std::string& topicPattern,
                                  UltraMsgCallback callback,
                                  const UltraMsgSubscribeOptions& options = {},
                                  UltraMsgResult* error = nullptr);
UltraMsgResult UltraMsg_Unsubscribe(UltraMsgHandle subscription);

// ---------------------------------------------------------------------------
// Journal (§8) — served by the broker, which alone opens the database
// ---------------------------------------------------------------------------

UltraMsgResult UltraMsg_Query(UltraMsgHandle endpoint, const UltraMsgQuery& query,
                              std::vector<UltraMsgMessage>& out);
UltraMsgResult UltraMsg_Count(UltraMsgHandle endpoint, const UltraMsgQuery& query, int64_t& out);
UltraMsgResult UltraMsg_GetMessage(UltraMsgHandle endpoint, const std::string& id,
                                   UltraMsgMessage& out);
UltraMsgResult UltraMsg_MarkRead(UltraMsgHandle endpoint, const std::vector<std::string>& ids);
UltraMsgResult UltraMsg_MarkUnread(UltraMsgHandle endpoint, const std::vector<std::string>& ids);
UltraMsgResult UltraMsg_Dismiss(UltraMsgHandle endpoint, const std::vector<std::string>& ids);
UltraMsgResult UltraMsg_Delete(UltraMsgHandle endpoint, const std::vector<std::string>& ids);
UltraMsgResult UltraMsg_ListConversations(UltraMsgHandle endpoint, const UltraMsgQuery& query,
                                          std::vector<UltraMsgConversation>& out);
// Retention per topic pattern: keep at most `days` of history and `maxRows`
// rows; 0 leaves that limit unset. Applied by the broker on its housekeeping
// tick and on Connect.
UltraMsgResult UltraMsg_SetRetention(UltraMsgHandle endpoint, const std::string& topicPattern,
                                     int days, int64_t maxRows);
// JSON lines, one message per line, to `path`.
UltraMsgResult UltraMsg_Export(UltraMsgHandle endpoint, const UltraMsgQuery& query,
                               const std::string& path, int64_t* outCount = nullptr);

// ---------------------------------------------------------------------------
// Schemas (§5.3)
// ---------------------------------------------------------------------------

// The well-known topics are registered by the module; applications add their
// own under a vendor prefix. Registration is per process (the broker and every
// endpoint carry the table); the journal decides persistence from it.
UltraMsgResult UltraMsg_RegisterSchema(const UltraMsgTopicSchema& schema);
bool           UltraMsg_GetSchema(const std::string& topic, UltraMsgTopicSchema& out);
std::vector<UltraMsgTopicSchema> UltraMsg_ListSchemas();
// Validates the body against the topic's schema; Success for unknown topics.
UltraMsgResult UltraMsg_Validate(const std::string& topic, const UltraCanvas::JSONValue& body);
// True when the topic's schema (or the message's flags) say "journal it".
bool UltraMsg_IsPersistentTopic(const std::string& topic);

// Topic pattern matching, exposed for adapters and tests.
bool UltraMsg_TopicMatches(const std::string& pattern, const std::string& topic);
bool UltraMsg_IsValidTopic(const std::string& topic);
bool UltraMsg_IsValidAppId(const std::string& appId);

// ---------------------------------------------------------------------------
// Well-known topic names
// ---------------------------------------------------------------------------

namespace UltraMsgTopics {
    constexpr const char* MessagingMessage        = "messaging.message";
    constexpr const char* MailMessage             = "mail.message";
    constexpr const char* SystemNotification      = "system.notification";
    constexpr const char* SystemNotificationAction    = "system.notification.action";
    constexpr const char* SystemNotificationDismissed = "system.notification.dismissed";
    constexpr const char* FeedRead                = "feed.read";
    constexpr const char* FeedDismissed           = "feed.dismissed";
    constexpr const char* AppLifecycleStarted     = "app.lifecycle.started";
    constexpr const char* AppLifecycleStopping    = "app.lifecycle.stopping";
    constexpr const char* AppOpenRequest          = "app.open.request";
    constexpr const char* AppCommandList          = "app.command.list";
    constexpr const char* AppCommandInvoke        = "app.command.invoke";
    constexpr const char* AppCommandEcho          = "app.command.echo";
    constexpr const char* FileChanged             = "file.changed";
    constexpr const char* ClipboardChanged        = "clipboard.changed";
    constexpr const char* ControlPrefix           = "ultramessage.control.";
}
