// UltraCanvas/core/UltraMessage/UltraMessageCodec.cpp
// The codec half of UltraMessageInternal.h: identifiers, base64, the JSON
// forms of every public type, frame encoding and decoding, topic matching and
// the per-platform default paths.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessageInternal.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <random>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <pwd.h>
#endif

namespace UltraMessage {
namespace Internal {

using UltraCanvas::JSON::Parse;
using UltraCanvas::JSON::Serialize;
using UltraCanvas::JSONParseResult;
using UltraCanvas::JSONSerializeOptions;

// ===========================================================================
// Time, identifiers, entropy
// ===========================================================================

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int CurrentProcessId() {
#ifdef _WIN32
    return static_cast<int>(::GetCurrentProcessId());
#else
    return static_cast<int>(::getpid());
#endif
}

namespace {

std::mt19937_64& Rng() {
    thread_local std::mt19937_64 engine = [] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(),
                           static_cast<unsigned>(NowMs() & 0xffffffffu),
                           static_cast<unsigned>(CurrentProcessId())};
        return std::mt19937_64(seed);
    }();
    return engine;
}

constexpr char kCrockford[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

} // namespace

std::string GenerateUlid() {
    // 128 bits: 48 of time, 80 of randomness, written as 26 base-32 digits
    // (the first digit carries only 3 of the time's bits).
    uint64_t time = static_cast<uint64_t>(NowMs()) & 0xFFFFFFFFFFFFull;
    uint64_t randomHigh = Rng()();
    uint64_t randomLow = Rng()();

    std::string out(26, '0');
    // Time: 10 characters, most significant first.
    for (int i = 9; i >= 0; --i) {
        out[static_cast<size_t>(i)] = kCrockford[time & 0x1F];
        time >>= 5;
    }
    // Randomness: 16 characters from 80 bits (64 + 16).
    uint64_t bits = randomHigh;
    int available = 64;
    for (int i = 25; i >= 10; --i) {
        if (available < 5) {
            // Refill from the second word.
            uint64_t next = randomLow;
            bits |= (next << available);
            randomLow = next >> (5 - available);
            available += 5;
        }
        out[static_cast<size_t>(i)] = kCrockford[bits & 0x1F];
        bits >>= 5;
        available -= 5;
    }
    return out;
}

std::string GenerateToken(size_t hexChars) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(hexChars);
    uint64_t bits = 0;
    int available = 0;
    for (size_t i = 0; i < hexChars; ++i) {
        if (available < 4) {
            bits = Rng()();
            available = 64;
        }
        out.push_back(kHex[bits & 0xF]);
        bits >>= 4;
        available -= 4;
    }
    return out;
}

// ===========================================================================
// Base64 (RFC 4648, no line breaks)
// ===========================================================================

namespace {
constexpr char kBase64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int Base64Index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
} // namespace

std::string Base64Encode(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < bytes.size()) {
        uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out.push_back(kBase64[(n >> 18) & 63]);
        out.push_back(kBase64[(n >> 12) & 63]);
        out.push_back(kBase64[(n >> 6) & 63]);
        out.push_back(kBase64[n & 63]);
        i += 3;
    }
    if (i < bytes.size()) {
        uint32_t n = bytes[i] << 16;
        if (i + 1 < bytes.size()) n |= bytes[i + 1] << 8;
        out.push_back(kBase64[(n >> 18) & 63]);
        out.push_back(kBase64[(n >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? kBase64[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

bool Base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve((text.size() / 4) * 3);
    uint32_t accumulator = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int value = Base64Index(c);
        if (value < 0) return false;
        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
        }
    }
    return true;
}

// ===========================================================================
// JSON forms
// ===========================================================================

namespace {

std::string Str(const JSONValue& json, const char* key) {
    const JSONValue* v = json.Find(key);
    return v ? v->GetString() : std::string();
}
int64_t Int(const JSONValue& json, const char* key, int64_t fallback = 0) {
    const JSONValue* v = json.Find(key);
    return v ? v->GetInteger(fallback) : fallback;
}
bool Bool(const JSONValue& json, const char* key, bool fallback = false) {
    const JSONValue* v = json.Find(key);
    return v ? v->GetBoolean(fallback) : fallback;
}

} // namespace

JSONValue SenderToJson(const UltraMsgSender& sender) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("app", sender.appId);
    j.Set("instance", sender.instanceId);
    j.Set("pid", static_cast<int64_t>(sender.processId));
    j.Set("verified", sender.verified);
    j.Set("name", sender.displayName);
    return j;
}

bool SenderFromJson(const JSONValue& json, UltraMsgSender& out) {
    if (!json.IsObject()) return false;
    out.appId = Str(json, "app");
    out.instanceId = Str(json, "instance");
    out.processId = static_cast<int>(Int(json, "pid"));
    out.verified = Bool(json, "verified");
    out.displayName = Str(json, "name");
    return true;
}

JSONValue EnvelopeToJson(const UltraMsgEnvelope& e) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("v", static_cast<int64_t>(kWireVersion));
    j.Set("id", e.id);
    j.Set("kind", UltraMsg_KindName(e.kind));
    j.Set("topic", e.topic);
    j.Set("from", SenderToJson(e.from));
    j.Set("to", e.to.empty() ? std::string("*") : e.to);
    if (!e.correlationId.empty()) j.Set("corr", e.correlationId);
    if (!e.conversation.empty()) j.Set("conversation", e.conversation);
    j.Set("time", e.timestampMs);
    if (e.ttlSeconds) j.Set("ttl", static_cast<int64_t>(e.ttlSeconds));
    if (e.flags) {
        JSONValue flags = JSONValue::MakeArray();
        if (e.flags & UltraMsgFlag_Persistent) flags.Append("persistent");
        if (e.flags & UltraMsgFlag_Urgent) flags.Append("urgent");
        if (e.flags & UltraMsgFlag_Silent) flags.Append("silent");
        if (e.flags & UltraMsgFlag_NoJournal) flags.Append("nojournal");
        if (e.flags & UltraMsgFlag_Replace) flags.Append("replace");
        j.Set("flags", std::move(flags));
    }
    if (!e.replaces.empty()) j.Set("replaces", e.replaces);
    return j;
}

bool EnvelopeFromJson(const JSONValue& json, UltraMsgEnvelope& out) {
    if (!json.IsObject()) return false;
    out.id = Str(json, "id");
    if (!UltraMsg_KindFromName(Str(json, "kind"), out.kind)) return false;
    out.topic = Str(json, "topic");
    if (const JSONValue* from = json.Find("from")) SenderFromJson(*from, out.from);
    out.to = Str(json, "to");
    if (out.to.empty()) out.to = "*";
    out.correlationId = Str(json, "corr");
    out.conversation = Str(json, "conversation");
    out.timestampMs = Int(json, "time");
    out.ttlSeconds = static_cast<int>(Int(json, "ttl"));
    out.flags = UltraMsgFlag_None;
    if (const JSONValue* flags = json.Find("flags"); flags && flags->IsArray()) {
        for (const JSONValue& f : flags->GetElements()) {
            const std::string name = f.GetString();
            if (name == "persistent") out.flags |= UltraMsgFlag_Persistent;
            else if (name == "urgent") out.flags |= UltraMsgFlag_Urgent;
            else if (name == "silent") out.flags |= UltraMsgFlag_Silent;
            else if (name == "nojournal") out.flags |= UltraMsgFlag_NoJournal;
            else if (name == "replace") out.flags |= UltraMsgFlag_Replace;
        }
    }
    out.replaces = Str(json, "replaces");
    return !out.id.empty() && !out.topic.empty();
}

JSONValue AttachmentToJson(const UltraMsgAttachment& a) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("name", a.name);
    j.Set("mime", a.mimeType);
    if (!a.bytes.empty()) {
        j.Set("size", static_cast<int64_t>(a.bytes.size()));
        j.Set("bytes", Base64Encode(a.bytes));
    }
    if (!a.filePath.empty()) j.Set("path", a.filePath);
    return j;
}

bool AttachmentFromJson(const JSONValue& json, UltraMsgAttachment& out) {
    if (!json.IsObject()) return false;
    out.name = Str(json, "name");
    out.mimeType = Str(json, "mime");
    out.filePath = Str(json, "path");
    out.bytes.clear();
    if (const JSONValue* bytes = json.Find("bytes"); bytes && bytes->IsString()) {
        if (!Base64Decode(bytes->GetString(), out.bytes)) return false;
    }
    return true;
}

JSONValue MessageToJson(const UltraMsgMessage& m) {
    JSONValue j = EnvelopeToJson(m.envelope);
    j.Set("body", m.body);
    if (!m.attachments.empty()) {
        JSONValue list = JSONValue::MakeArray();
        for (const auto& a : m.attachments) list.Append(AttachmentToJson(a));
        j.Set("attachments", std::move(list));
    }
    if (m.read) j.Set("read", true);
    if (m.dismissed) j.Set("dismissed", true);
    return j;
}

bool MessageFromJson(const JSONValue& json, UltraMsgMessage& out) {
    if (!EnvelopeFromJson(json, out.envelope)) return false;
    if (const JSONValue* body = json.Find("body")) out.body = *body;
    else out.body = JSONValue::MakeObject();
    out.attachments.clear();
    if (const JSONValue* list = json.Find("attachments"); list && list->IsArray()) {
        for (const JSONValue& a : list->GetElements()) {
            UltraMsgAttachment attachment;
            if (AttachmentFromJson(a, attachment)) out.attachments.push_back(std::move(attachment));
        }
    }
    out.read = Bool(json, "read");
    out.dismissed = Bool(json, "dismissed");
    return true;
}

JSONValue StringsToJson(const std::vector<std::string>& values) {
    JSONValue list = JSONValue::MakeArray();
    for (const auto& v : values) list.Append(v);
    return list;
}

std::vector<std::string> StringsFromJson(const JSONValue& json) {
    std::vector<std::string> out;
    if (json.IsArray())
        for (const JSONValue& v : json.GetElements()) out.push_back(v.GetString());
    return out;
}

JSONValue QueryToJson(const UltraMsgQuery& q) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("topics", StringsToJson(q.topics));
    j.Set("conversation", q.conversation);
    j.Set("app", q.appId);
    j.Set("service", q.service);
    j.Set("since", q.sinceMs);
    j.Set("until", q.untilMs);
    j.Set("unreadOnly", q.unreadOnly);
    j.Set("includeDismissed", q.includeDismissed);
    j.Set("text", q.textContains);
    j.Set("limit", static_cast<int64_t>(q.limit));
    j.Set("offset", static_cast<int64_t>(q.offset));
    return j;
}

bool QueryFromJson(const JSONValue& json, UltraMsgQuery& out) {
    if (!json.IsObject()) return false;
    if (const JSONValue* topics = json.Find("topics")) out.topics = StringsFromJson(*topics);
    out.conversation = Str(json, "conversation");
    out.appId = Str(json, "app");
    out.service = Str(json, "service");
    out.sinceMs = Int(json, "since");
    out.untilMs = Int(json, "until");
    out.unreadOnly = Bool(json, "unreadOnly");
    out.includeDismissed = Bool(json, "includeDismissed");
    out.textContains = Str(json, "text");
    out.limit = static_cast<int>(Int(json, "limit", 100));
    out.offset = static_cast<int>(Int(json, "offset", 0));
    return true;
}

JSONValue EndpointInfoToJson(const UltraMsgEndpointInfo& info) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("app", info.appId);
    j.Set("instance", info.instanceId);
    j.Set("name", info.displayName);
    j.Set("icon", info.iconPath);
    j.Set("pid", static_cast<int64_t>(info.processId));
    j.Set("verified", info.verified);
    j.Set("connectedAt", info.connectedAtMs);
    return j;
}

bool EndpointInfoFromJson(const JSONValue& json, UltraMsgEndpointInfo& out) {
    if (!json.IsObject()) return false;
    out.appId = Str(json, "app");
    out.instanceId = Str(json, "instance");
    out.displayName = Str(json, "name");
    out.iconPath = Str(json, "icon");
    out.processId = static_cast<int>(Int(json, "pid"));
    out.verified = Bool(json, "verified");
    out.connectedAtMs = Int(json, "connectedAt");
    return true;
}

JSONValue ConversationToJson(const UltraMsgConversation& c) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("id", c.id);
    j.Set("service", c.service);
    j.Set("title", c.title);
    j.Set("isGroup", c.isGroup);
    j.Set("lastTime", c.lastTimeMs);
    j.Set("unread", static_cast<int64_t>(c.unreadCount));
    j.Set("count", static_cast<int64_t>(c.messageCount));
    j.Set("lastMessageId", c.lastMessageId);
    return j;
}

bool ConversationFromJson(const JSONValue& json, UltraMsgConversation& out) {
    if (!json.IsObject()) return false;
    out.id = Str(json, "id");
    out.service = Str(json, "service");
    out.title = Str(json, "title");
    out.isGroup = Bool(json, "isGroup");
    out.lastTimeMs = Int(json, "lastTime");
    out.unreadCount = static_cast<int>(Int(json, "unread"));
    out.messageCount = static_cast<int>(Int(json, "count"));
    out.lastMessageId = Str(json, "lastMessageId");
    return true;
}

JSONValue BrokerInfoToJson(const UltraMsgBrokerInfo& info) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("busPath", info.busPath);
    j.Set("journalPath", info.journalPath);
    j.Set("hostPid", static_cast<int64_t>(info.hostProcessId));
    j.Set("startedAt", info.startedAtMs);
    j.Set("endpoints", static_cast<int64_t>(info.endpointCount));
    j.Set("version", info.version);
    return j;
}

bool BrokerInfoFromJson(const JSONValue& json, UltraMsgBrokerInfo& out) {
    if (!json.IsObject()) return false;
    out.busPath = Str(json, "busPath");
    out.journalPath = Str(json, "journalPath");
    out.hostProcessId = static_cast<int>(Int(json, "hostPid"));
    out.startedAtMs = Int(json, "startedAt");
    out.endpointCount = static_cast<int>(Int(json, "endpoints"));
    out.version = Str(json, "version");
    out.inProcess = out.hostProcessId == CurrentProcessId();
    return true;
}

JSONValue ResultToJson(const UltraMsgResult& result) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("ok", result.ok);
    j.Set("code", UltraMsg_ResultCodeName(result.code));
    j.Set("message", result.message);
    return j;
}

UltraMsgResult ResultFromJson(const JSONValue& json) {
    if (!json.IsObject()) return UltraMsgResult::Error(UltraMsgResultCode::Internal, "malformed result");
    if (Bool(json, "ok", false)) return UltraMsgResult::Ok();
    const std::string code = Str(json, "code");
    UltraMsgResultCode c = UltraMsgResultCode::Unknown;
    static const struct { const char* name; UltraMsgResultCode code; } kCodes[] = {
        {"InvalidArgument", UltraMsgResultCode::InvalidArgument},
        {"NotConnected", UltraMsgResultCode::NotConnected},
        {"BrokerUnavailable", UltraMsgResultCode::BrokerUnavailable},
        {"NoSuchTarget", UltraMsgResultCode::NoSuchTarget},
        {"NotHandled", UltraMsgResultCode::NotHandled},
        {"Timeout", UltraMsgResultCode::Timeout},
        {"Bounced", UltraMsgResultCode::Bounced},
        {"SchemaViolation", UltraMsgResultCode::SchemaViolation},
        {"JournalError", UltraMsgResultCode::JournalError},
        {"TooLarge", UltraMsgResultCode::TooLarge},
        {"Internal", UltraMsgResultCode::Internal},
    };
    for (const auto& k : kCodes) if (code == k.name) c = k.code;
    return UltraMsgResult::Error(c, Str(json, "message"));
}

// ===========================================================================
// Frames
// ===========================================================================

JSONValue MakeFrame(const char* type) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("t", type);
    return j;
}

std::string EncodeFrame(const JSONValue& frame) {
    JSONSerializeOptions options;
    options.pretty = false;
    std::string payload = Serialize(frame, options);
    const uint32_t length = static_cast<uint32_t>(payload.size());
    std::string out;
    out.reserve(payload.size() + 4);
    out.push_back(static_cast<char>(length & 0xFF));
    out.push_back(static_cast<char>((length >> 8) & 0xFF));
    out.push_back(static_cast<char>((length >> 16) & 0xFF));
    out.push_back(static_cast<char>((length >> 24) & 0xFF));
    out += payload;
    return out;
}

bool FrameDecoder::Push(const uint8_t* data, size_t length, std::vector<JSONValue>& out,
                        std::string& error) {
    buffer_.insert(buffer_.end(), data, data + length);
    size_t offset = 0;
    while (buffer_.size() - offset >= 4) {
        const uint32_t frameLength = static_cast<uint32_t>(buffer_[offset]) |
                                     (static_cast<uint32_t>(buffer_[offset + 1]) << 8) |
                                     (static_cast<uint32_t>(buffer_[offset + 2]) << 16) |
                                     (static_cast<uint32_t>(buffer_[offset + 3]) << 24);
        if (frameLength > UltraMsgMaxFrameBytes) {
            error = "frame exceeds the size limit";
            buffer_.clear();
            return false;
        }
        if (buffer_.size() - offset - 4 < frameLength) break;
        std::string text(reinterpret_cast<const char*>(buffer_.data() + offset + 4), frameLength);
        JSONParseResult result;
        JSONValue frame = Parse(text, &result);
        if (!result.success || !frame.IsObject()) {
            error = "malformed frame: " + result.errorMessage;
            buffer_.clear();
            return false;
        }
        out.push_back(std::move(frame));
        offset += 4 + frameLength;
    }
    if (offset) buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

// ===========================================================================
// Topics and identifiers
// ===========================================================================

namespace {

bool IsSegmentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == '.') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

bool ValidSegments(const std::string& text, bool allowWildcards) {
    if (text.empty()) return false;
    for (const std::string& segment : Split(text)) {
        if (segment.empty()) return false;
        if (allowWildcards && (segment == "*" || segment == "#")) continue;
        for (char c : segment) if (!IsSegmentChar(c)) return false;
    }
    return true;
}

} // namespace

bool IsValidTopic(const std::string& topic) { return ValidSegments(topic, false); }

bool IsValidPattern(const std::string& pattern) {
    if (!ValidSegments(pattern, true)) return false;
    // '#' may only be the last segment.
    const auto parts = Split(pattern);
    for (size_t i = 0; i + 1 < parts.size(); ++i) if (parts[i] == "#") return false;
    return true;
}

bool TopicMatches(const std::string& pattern, const std::string& topic) {
    if (pattern == "#") return !topic.empty();
    if (pattern == topic) return true;
    const auto p = Split(pattern);
    const auto t = Split(topic);
    size_t i = 0;
    for (; i < p.size(); ++i) {
        if (p[i] == "#") return i < t.size();      // one or more remaining segments
        if (i >= t.size()) return false;
        if (p[i] != "*" && p[i] != t[i]) return false;
    }
    return i == t.size();
}

std::string PatternLiteralPrefix(const std::string& pattern) {
    std::string prefix;
    for (const std::string& segment : Split(pattern)) {
        if (segment == "*" || segment == "#") break;
        if (!prefix.empty()) prefix.push_back('.');
        prefix += segment;
    }
    return prefix;
}

bool IsValidAppId(const std::string& appId) {
    if (appId.empty() || appId.size() > 255) return false;
    for (const std::string& segment : Split(appId)) {
        if (segment.empty()) return false;
        for (char c : segment) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) return false;
        }
    }
    return true;
}

bool IsControlTopic(const std::string& topic) {
    return topic.rfind(UltraMsgTopics::ControlPrefix, 0) == 0;
}

// ===========================================================================
// Platform defaults
// ===========================================================================

namespace {

std::string EnvOrEmpty(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || !value) return std::string();
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

#ifndef _WIN32
std::string HomeDirectory() {
    std::string home = EnvOrEmpty("HOME");
    if (!home.empty()) return home;
    if (const passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_dir) return pw->pw_dir;
    return "/tmp";
}
#endif

} // namespace

std::string ParentDirectory(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

bool EnsureDirectory(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec && !std::filesystem::is_directory(path, ec)) return false;
#ifndef _WIN32
    ::chmod(path.c_str(), 0700);
#endif
    return true;
}

bool EnsureParentDirectory(const std::string& path) {
    return EnsureDirectory(ParentDirectory(path));
}

std::string DefaultBusPath() {
#ifdef _WIN32
    DWORD session = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &session);
    return "\\\\.\\pipe\\UltraMessage-" + std::to_string(session);
#elif defined(__APPLE__)
    std::string base = EnvOrEmpty("TMPDIR");
    if (base.empty()) base = "/tmp";
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    return base + "/ultramessage/bus.sock";
#else
    std::string base = EnvOrEmpty("XDG_RUNTIME_DIR");
    if (base.empty()) base = "/tmp/ultramessage-" + std::to_string(::getuid());
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    return base + "/ultramessage/bus.sock";
#endif
}

std::string DefaultJournalPath() {
#ifdef _WIN32
    std::string base = EnvOrEmpty("LOCALAPPDATA");
    if (base.empty()) base = EnvOrEmpty("TEMP");
    return base + "\\UltraMessage\\journal.db";
#elif defined(__APPLE__)
    return HomeDirectory() + "/Library/Application Support/UltraMessage/journal.db";
#else
    std::string base = EnvOrEmpty("XDG_DATA_HOME");
    if (base.empty()) base = HomeDirectory() + "/.local/share";
    return base + "/ultramessage/journal.db";
#endif
}

} // namespace Internal
} // namespace UltraMessage

// ===========================================================================
// Public enum names and topic helpers
// ===========================================================================

const char* UltraMsg_ResultCodeName(UltraMsgResultCode code) {
    switch (code) {
        case UltraMsgResultCode::Success: return "Success";
        case UltraMsgResultCode::InvalidArgument: return "InvalidArgument";
        case UltraMsgResultCode::NotConnected: return "NotConnected";
        case UltraMsgResultCode::BrokerUnavailable: return "BrokerUnavailable";
        case UltraMsgResultCode::NoSuchTarget: return "NoSuchTarget";
        case UltraMsgResultCode::NotHandled: return "NotHandled";
        case UltraMsgResultCode::Timeout: return "Timeout";
        case UltraMsgResultCode::Bounced: return "Bounced";
        case UltraMsgResultCode::SchemaViolation: return "SchemaViolation";
        case UltraMsgResultCode::JournalError: return "JournalError";
        case UltraMsgResultCode::TooLarge: return "TooLarge";
        case UltraMsgResultCode::Internal: return "Internal";
        case UltraMsgResultCode::Unknown: break;
    }
    return "Unknown";
}

const char* UltraMsg_KindName(UltraMsgKind kind) {
    switch (kind) {
        case UltraMsgKind::Notice: return "notice";
        case UltraMsgKind::RecordedNotice: return "recorded";
        case UltraMsgKind::Request: return "request";
        case UltraMsgKind::Reply: return "reply";
        case UltraMsgKind::Bounce: return "bounce";
    }
    return "notice";
}

bool UltraMsg_KindFromName(const std::string& name, UltraMsgKind& out) {
    if (name == "notice") out = UltraMsgKind::Notice;
    else if (name == "recorded") out = UltraMsgKind::RecordedNotice;
    else if (name == "request") out = UltraMsgKind::Request;
    else if (name == "reply") out = UltraMsgKind::Reply;
    else if (name == "bounce") out = UltraMsgKind::Bounce;
    else return false;
    return true;
}

bool UltraMsg_TopicMatches(const std::string& pattern, const std::string& topic) {
    return UltraMessage::Internal::TopicMatches(pattern, topic);
}

bool UltraMsg_IsValidTopic(const std::string& topic) {
    return UltraMessage::Internal::IsValidTopic(topic);
}

bool UltraMsg_IsValidAppId(const std::string& appId) {
    return UltraMessage::Internal::IsValidAppId(appId);
}

std::string UltraMsg_GetDefaultBusPath() { return UltraMessage::Internal::DefaultBusPath(); }
std::string UltraMsg_GetDefaultJournalPath() { return UltraMessage::Internal::DefaultJournalPath(); }
