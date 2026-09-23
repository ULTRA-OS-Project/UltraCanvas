// UltraCanvas/core/UltraMessage/UltraMessageInternal.h
// Shared internals of the UltraMessage library: the wire frames the broker
// and the endpoints exchange, the codec between the public types and JSON,
// identifiers, topic matching and the per-platform default paths. Nothing in
// this header is public API.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessage/UltraMessage.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraMessage {
namespace Internal {

using UltraCanvas::JSONValue;

// ---------------------------------------------------------------------------
// Wire protocol (§5.2). Every frame is one JSON object with a "t" field.
// ---------------------------------------------------------------------------

constexpr int kWireVersion = 1;

namespace Frame {
    // endpoint -> broker
    constexpr const char* Hello       = "hello";    // app, name, icon, pid
    constexpr const char* Ctl         = "ctl";      // rid, op, args   (control RPC)
    constexpr const char* Msg         = "msg";      // a message to route
    constexpr const char* Ack         = "ack";      // id               (recorded notice acknowledged)
    constexpr const char* Bye         = "bye";
    // broker -> endpoint
    constexpr const char* Welcome     = "welcome";  // instance, pid, verified, broker{...}
    constexpr const char* Ctlr        = "ctlr";     // rid, ok, error{code,message}, result
    constexpr const char* Deliver     = "deliver";  // sub, message
    constexpr const char* Bounce      = "bounce";   // message (the original)
    constexpr const char* Overflow    = "overflow"; // dropped (count)
    // either direction
    constexpr const char* Ping        = "ping";
    constexpr const char* Pong        = "pong";
}

namespace Op {
    constexpr const char* Subscribe        = "subscribe";
    constexpr const char* Unsubscribe      = "unsubscribe";
    constexpr const char* ListEndpoints    = "endpoints";
    constexpr const char* ResolveApp       = "resolve";
    constexpr const char* BrokerInfo       = "brokerInfo";
    constexpr const char* JournalQuery     = "journal.query";
    constexpr const char* JournalCount     = "journal.count";
    constexpr const char* JournalGet       = "journal.get";
    constexpr const char* JournalMarkRead  = "journal.markRead";
    constexpr const char* JournalMarkUnread= "journal.markUnread";
    constexpr const char* JournalDismiss   = "journal.dismiss";
    constexpr const char* JournalDelete    = "journal.delete";
    constexpr const char* JournalConversations = "journal.conversations";
    constexpr const char* JournalRetention = "journal.retention";
    constexpr const char* JournalExport    = "journal.export";
    constexpr const char* AdaptersList     = "adapters.list";
    constexpr const char* AdaptersEnable   = "adapters.enable";
    constexpr const char* AdaptersState    = "adapters.state";
}

// ---------------------------------------------------------------------------
// Codec
// ---------------------------------------------------------------------------

int64_t NowMs();
int     CurrentProcessId();

// 26-character Crockford base32 ULID: 48 bits of milliseconds, 80 of entropy.
std::string GenerateUlid();
// Short random token for instance ids and request ids.
std::string GenerateToken(size_t hexChars = 12);

std::string Base64Encode(const std::vector<uint8_t>& bytes);
bool        Base64Decode(const std::string& text, std::vector<uint8_t>& out);

JSONValue EnvelopeToJson(const UltraMsgEnvelope& envelope);
bool      EnvelopeFromJson(const JSONValue& json, UltraMsgEnvelope& out);
JSONValue SenderToJson(const UltraMsgSender& sender);
bool      SenderFromJson(const JSONValue& json, UltraMsgSender& out);
JSONValue MessageToJson(const UltraMsgMessage& message);
bool      MessageFromJson(const JSONValue& json, UltraMsgMessage& out);
JSONValue AttachmentToJson(const UltraMsgAttachment& attachment);
bool      AttachmentFromJson(const JSONValue& json, UltraMsgAttachment& out);
JSONValue QueryToJson(const UltraMsgQuery& query);
bool      QueryFromJson(const JSONValue& json, UltraMsgQuery& out);
JSONValue EndpointInfoToJson(const UltraMsgEndpointInfo& info);
bool      EndpointInfoFromJson(const JSONValue& json, UltraMsgEndpointInfo& out);
JSONValue ConversationToJson(const UltraMsgConversation& conversation);
bool      ConversationFromJson(const JSONValue& json, UltraMsgConversation& out);
JSONValue BrokerInfoToJson(const UltraMsgBrokerInfo& info);
bool      BrokerInfoFromJson(const JSONValue& json, UltraMsgBrokerInfo& out);
JSONValue ResultToJson(const UltraMsgResult& result);
UltraMsgResult ResultFromJson(const JSONValue& json);
JSONValue AdapterStateToJson(const UltraMsgAdapterState& state);
bool      AdapterStateFromJson(const JSONValue& json, UltraMsgAdapterState& out);
JSONValue AdapterInfoToJson(const UltraMsgAdapterInfo& info);
bool      AdapterInfoFromJson(const JSONValue& json, UltraMsgAdapterInfo& out);

JSONValue StringsToJson(const std::vector<std::string>& values);
std::vector<std::string> StringsFromJson(const JSONValue& json);

// Frames: 4-byte little-endian length prefix, then one UTF-8 JSON object.
std::string EncodeFrame(const JSONValue& frame);
JSONValue   MakeFrame(const char* type);

// Accumulates bytes from a stream and yields complete frames.
class FrameDecoder {
public:
    // Appends bytes; every complete frame is appended to `out`. Returns false
    // (and sets `error`) on a frame over UltraMsgMaxFrameBytes or invalid JSON;
    // the stream is then unusable.
    bool Push(const uint8_t* data, size_t length, std::vector<JSONValue>& out,
              std::string& error);
    void Reset() { buffer_.clear(); }

private:
    std::vector<uint8_t> buffer_;
};

// ---------------------------------------------------------------------------
// Topics and identifiers
// ---------------------------------------------------------------------------

bool IsValidTopic(const std::string& topic);
bool IsValidPattern(const std::string& pattern);
bool TopicMatches(const std::string& pattern, const std::string& topic);
bool IsValidAppId(const std::string& appId);
bool IsControlTopic(const std::string& topic);
// "prefix.*" / "prefix.#" / "#" -> the literal prefix for journal LIKE queries.
std::string PatternLiteralPrefix(const std::string& pattern);

// ---------------------------------------------------------------------------
// Platform defaults (§7.1, §8)
// ---------------------------------------------------------------------------

std::string DefaultBusPath();
std::string DefaultJournalPath();
// Creates every missing directory of `path`'s parent, owner-only where the
// platform has modes. Returns false when that failed.
bool EnsureParentDirectory(const std::string& path);
bool EnsureDirectory(const std::string& path);
std::string ParentDirectory(const std::string& path);

} // namespace Internal
} // namespace UltraMessage
