// UltraCanvas/include/UltraMessage/UltraMessageTypes.h
// Value types of the UltraMessage channel: results, handles, the envelope,
// the message, connect / send / subscribe options, journal queries.
// Design: Docs/Research/UltraMessageDesignProposal.md (§5, §6.1).
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DataFormats/UltraCanvasJSON.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// X11 headers leak these as macros; the enums below use the names.
#ifdef Success
#undef Success
#endif
#ifdef None
#undef None
#endif

// ---------------------------------------------------------------------------
// Results and handles
// ---------------------------------------------------------------------------

enum class UltraMsgResultCode {
    Success = 0,
    InvalidArgument,     // bad app id, empty topic, malformed pattern, ...
    NotConnected,        // the endpoint is not (or no longer) connected
    BrokerUnavailable,   // no broker reachable and none could be started
    NoSuchTarget,        // Request / targeted Post to an app nobody runs
    NotHandled,          // the target runs but has no subscription for the topic
    Timeout,             // no reply within the timeout
    Bounced,             // recorded notice that nobody acknowledged
    SchemaViolation,     // body does not match the topic's schema (debug builds)
    JournalError,        // the journal database refused the operation
    TooLarge,            // frame or inline attachment over the limit
    Internal,
    Unknown
};

struct UltraMsgResult {
    bool ok = true;
    UltraMsgResultCode code = UltraMsgResultCode::Success;
    std::string message;

    explicit operator bool() const { return ok; }
    static UltraMsgResult Ok() { return UltraMsgResult{}; }
    static UltraMsgResult Error(UltraMsgResultCode c, const std::string& msg) {
        UltraMsgResult r;
        r.ok = false;
        r.code = c;
        r.message = msg;
        return r;
    }
};

const char* UltraMsg_ResultCodeName(UltraMsgResultCode code);

// Endpoints, subscriptions and (later) registered commands are opaque handles.
using UltraMsgHandle = uint64_t;
constexpr UltraMsgHandle UltraMsgInvalidHandle = 0;

// ---------------------------------------------------------------------------
// Envelope and message (§5.1)
// ---------------------------------------------------------------------------

enum class UltraMsgKind {
    Notice = 0,       // fire and forget
    RecordedNotice,   // at least one subscriber must acknowledge, else Bounce
    Request,          // exactly one Reply or error
    Reply,
    Bounce            // a RecordedNotice returned to its sender, unacknowledged
};

const char* UltraMsg_KindName(UltraMsgKind kind);
bool UltraMsg_KindFromName(const std::string& name, UltraMsgKind& out);

// Envelope flags.
enum : uint32_t {
    UltraMsgFlag_None       = 0,
    UltraMsgFlag_Persistent = 1u << 0,   // journal it even if the topic's schema does not
    UltraMsgFlag_Urgent     = 1u << 1,
    UltraMsgFlag_Silent     = 1u << 2,   // no toast / no badge; feed still lists it
    UltraMsgFlag_NoJournal  = 1u << 3,   // never journal, whatever the schema says
    UltraMsgFlag_Replace    = 1u << 4    // supersedes the message named in `replaces`
};

struct UltraMsgSender {
    std::string appId;        // "org.ultraos.ultramail" (reverse DNS)
    std::string instanceId;   // one per process, broker-assigned
    int         processId = 0;// as verified by the broker on connect where the OS allows
    bool        verified = false;
    std::string displayName;  // "UltraMail"
};

struct UltraMsgEnvelope {
    std::string    id;             // ULID: time-ordered, unique per broker
    UltraMsgKind   kind = UltraMsgKind::Notice;
    std::string    topic;          // "messaging.message"
    UltraMsgSender from;
    std::string    to = "*";       // "*" broadcast | appId | instanceId
    std::string    correlationId;  // Reply / Bounce: the id being answered
    std::string    conversation;   // optional: groups messages in the feed
    int64_t        timestampMs = 0;// wall clock, UTC, milliseconds
    int            ttlSeconds = 0; // 0 = no expiry (RecordedNotice: default 5 s)
    uint32_t       flags = UltraMsgFlag_None;
    std::string    replaces;       // id of a message this one supersedes
};

struct UltraMsgAttachment {
    std::string name;
    std::string mimeType;
    std::vector<uint8_t> bytes;    // inline, up to UltraMsgMaxInlineAttachment ...
    std::string filePath;          // ... or a path the receiver may read instead
};

// Frames larger than this are refused by both ends.
constexpr size_t UltraMsgMaxFrameBytes = 8u * 1024u * 1024u;
// Inline attachment cap (§7.3); larger payloads travel by file path.
constexpr size_t UltraMsgMaxInlineAttachment = 1u * 1024u * 1024u;

struct UltraMsgMessage {
    UltraMsgEnvelope envelope;
    UltraCanvas::JSONValue body;   // validated against the topic's schema
    std::vector<UltraMsgAttachment> attachments;
    bool read = false;             // journal state, filled by queries
    bool dismissed = false;
};

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

using UltraMsgCallback      = std::function<void(const UltraMsgMessage&)>;
using UltraMsgReplyCallback = std::function<void(const UltraMsgResult&, const UltraMsgMessage& reply)>;
using UltraMsgBounceCallback = std::function<void(const UltraMsgMessage& original)>;

// ---------------------------------------------------------------------------
// Options (§6.1)
// ---------------------------------------------------------------------------

struct UltraMsgConnectOptions {
    std::string appId;               // required, reverse DNS
    std::string displayName;
    std::string iconPath;
    // true: callbacks go through the dispatcher installed with
    // UltraMsg_SetUIDispatcher, or wait in a queue that UltraMsg_ProcessPending
    // drains when none is installed. false: callbacks run on the transport
    // thread as frames arrive.
    bool deliverOnUIThread = true;
    bool startBrokerIfAbsent = true; // host an in-process broker when none answers
    int  connectTimeoutMs = 2000;
    // Overrides the per-user bus location (tests; multiple buses on one machine).
    std::string busPath;
    // Where the hosting broker keeps its journal; empty = the platform default.
    std::string journalPath;
};

struct UltraMsgSendOptions {
    std::string to = "*";            // "*" | appId | instanceId
    std::string conversation;
    int         ttlSeconds = 0;
    uint32_t    flags = UltraMsgFlag_None;
    std::string replaces;
    std::vector<UltraMsgAttachment> attachments;
    // UltraMsg_RequestAsync only: run onReply on the transport thread instead
    // of through the UI dispatcher (what a std::future waiter needs).
    bool replyOnWorkerThread = false;
};

struct UltraMsgSubscribeOptions {
    bool manualAck = false;          // caller acknowledges recorded notices itself
    bool includeOwn = false;         // also receive what this endpoint posted
    bool onWorkerThread = false;     // bypass the UI dispatcher for this subscription
    int64_t replaySinceMs = 0;       // >0: replay journaled messages since then, then live
};

// ---------------------------------------------------------------------------
// Journal (§6.2, §8)
// ---------------------------------------------------------------------------

struct UltraMsgQuery {
    std::vector<std::string> topics; // patterns; empty = every persistent topic
    std::string conversation;
    std::string appId;               // sender app id
    std::string service;             // body.service, for messaging.message
    int64_t sinceMs = 0;
    int64_t untilMs = 0;
    bool unreadOnly = false;
    bool includeDismissed = false;
    std::string textContains;
    int limit = 100;
    int offset = 0;
};

struct UltraMsgConversation {
    std::string id;
    std::string service;
    std::string title;
    bool isGroup = false;
    int64_t lastTimeMs = 0;
    int unreadCount = 0;
    int messageCount = 0;
    std::string lastMessageId;
};

struct UltraMsgEndpointInfo {
    std::string appId;
    std::string instanceId;
    std::string displayName;
    std::string iconPath;
    int processId = 0;
    bool verified = false;
    int64_t connectedAtMs = 0;
};

struct UltraMsgBrokerInfo {
    bool inProcess = false;          // this process hosts the broker
    std::string busPath;
    std::string journalPath;
    int hostProcessId = 0;
    int64_t startedAtMs = 0;
    int endpointCount = 0;
    std::string version;
};

// Schema registry (§5.3): a lightweight description, not full JSON Schema.
struct UltraMsgFieldSpec {
    std::string name;
    std::string type;                // "string" | "integer" | "number" | "boolean" | "object" | "array"
    bool required = false;
};

struct UltraMsgTopicSchema {
    std::string topic;               // exact topic or a pattern such as "app.command.*"
    int version = 1;
    bool persistent = false;         // journaled by default
    std::vector<UltraMsgFieldSpec> fields;
    std::string description;
};
