// Plugins/UltraIM/matrix/MatrixCrypto.cpp
// Matrix provider - end-to-end encryption (Olm, Megolm, device lists, key
// export)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "MatrixCrypto.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraIM/UltraIMCore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace UltraIMMatrix {

using UltraCanvas::JSONValue;

namespace {

const char* const kClient = "/_matrix/client/v3";
const char* const kOlm = "m.olm.v1.curve25519-aes-sha2";
const char* const kMegolm = "m.megolm.v1.aes-sha2";
const char* const kKeyAlgorithm = "signed_curve25519";
const char kStoreMagic[] = {'U', 'I', 'M', 'C', 1};     // file: magic, nonce, sealed JSON
const char* const kExportBegin = "-----BEGIN MEGOLM SESSION DATA-----";
const char* const kExportEnd = "-----END MEGOLM SESSION DATA-----";

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

void CanonicalString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void Canonical(const JSONValue& v, std::string& out) {
    switch (v.GetType()) {
        case UltraCanvas::JSONType::Null: out += "null"; break;
        case UltraCanvas::JSONType::Boolean: out += v.GetBoolean() ? "true" : "false"; break;
        case UltraCanvas::JSONType::Number:
            // Matrix signs integers only; anything else as JSON writes it.
            out += v.IsInteger() ? std::to_string(v.GetInteger()) : UltraCanvas::JSON::Serialize(v);
            break;
        case UltraCanvas::JSONType::String: CanonicalString(v.GetString(), out); break;
        case UltraCanvas::JSONType::Array: {
            out += '[';
            bool first = true;
            for (const auto& e : v.GetElements()) {
                if (!first) out += ',';
                first = false;
                Canonical(e, out);
            }
            out += ']';
            break;
        }
        case UltraCanvas::JSONType::Object: {
            // Code point order is byte order in UTF-8.
            std::vector<const JSONValue::Member*> members;
            for (const auto& m : v.GetMembers()) members.push_back(&m);
            std::sort(members.begin(), members.end(),
                      [](const JSONValue::Member* a, const JSONValue::Member* b) { return a->first < b->first; });
            out += '{';
            bool first = true;
            for (const auto* m : members) {
                if (!first) out += ',';
                first = false;
                CanonicalString(m->first, out);
                out += ':';
                Canonical(m->second, out);
            }
            out += '}';
            break;
        }
    }
}

// What a signature covers: the object without "signatures" and "unsigned".
std::string SignedPart(JSONValue object) {
    object.Remove("signatures");
    object.Remove("unsigned");
    return CanonicalJson(object);
}

JSONValue Parse(const std::string& text) {
    UltraCanvas::JSONParseResult parsed;
    return UltraCanvas::JSON::Parse(text, &parsed);
}

JSONValue Strings(const std::vector<std::string>& list) {
    JSONValue out = JSONValue::MakeArray();
    for (const auto& s : list) out.Append(JSONValue(s));
    return out;
}

// The index a Megolm message was encrypted at: after the version byte comes
// a protobuf whose field 1 (tag 0x08) is that index as a varint. -1 when the
// message is not shaped so.
int64_t MegolmMessageIndex(const std::string& ciphertext) {
    const std::vector<uint8_t> bytes = UltraCanvas::Base64Decode(ciphertext);
    if (bytes.size() < 3 || bytes[1] != 0x08) return -1;
    int64_t index = 0;
    for (size_t i = 2, shift = 0; i < bytes.size() && shift < 35; ++i, shift += 7) {
        index |= static_cast<int64_t>(bytes[i] & 0x7f) << shift;
        if (!(bytes[i] & 0x80)) return index;
    }
    return -1;
}

UltraCryptSecureBuffer Secret(const std::vector<uint8_t>& bytes) {
    return UltraCryptSecureBuffer(bytes.data(), bytes.size());
}

} // namespace

std::string CanonicalJson(const JSONValue& value) {
    std::string out;
    Canonical(value, out);
    return out;
}

// ============================================================================
// STORE
// ============================================================================

bool MatrixCrypto::StoreExists(const std::string& storePath) {
    std::error_code ec;
    return std::filesystem::exists(UltraCanvas::PathFromUtf8(storePath), ec);
}

bool MatrixCrypto::Open(const std::string& storePath, const std::vector<uint8_t>& storeKey, const std::string& userId,
                        const std::string& deviceId, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
    if (!OlmAvailable()) {
        error = "End-to-end encryption is not built in (no Rust toolchain at build time)";
        return false;
    }
    if (storeKey.size() != 32) {
        error = "The key store's key must be 32 bytes";
        return false;
    }
    // The pickles get a key of their own, derived from the store's.
    UltraCryptSecureBuffer derived;
    const std::string info = "ultraim-matrix-pickle";
    if (!UltraCrypt_DeriveKeyHkdf(UltraCryptHashAlgorithm::SHA256, Secret(storeKey), {},
                                  std::vector<uint8_t>(info.begin(), info.end()), pickleKey_.size(), derived)) {
        error = "Could not derive the pickle key";
        return false;
    }
    std::copy(derived.Data(), derived.Data() + pickleKey_.size(), pickleKey_.begin());
    path_ = storePath;
    storeKey_ = storeKey;
    userId_ = userId;
    deviceId_ = deviceId;
    olm_.clear();
    outbound_.clear();
    inbound_.clear();
    devices_.clear();
    tracked_.clear();
    seen_.clear();
    deviceKeysUploaded_ = false;

    std::ifstream in(UltraCanvas::PathFromUtf8(storePath), std::ios::binary);
    if (in) {
        const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t nonceSize = UltraCrypt_GetNonceSize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
        const size_t head = sizeof(kStoreMagic);
        if (data.size() < head + nonceSize || data.compare(0, head, std::string(kStoreMagic, head)) != 0) {
            error = "The key store is not one this provider wrote";
            return false;
        }
        UltraCryptAeadParams params;
        params.nonce.assign(data.begin() + static_cast<std::ptrdiff_t>(head),
                            data.begin() + static_cast<std::ptrdiff_t>(head + nonceSize));
        params.associatedData.assign(kStoreMagic, kStoreMagic + head);
        UltraCryptSecureBuffer plain;
        if (!UltraCrypt_AeadOpen(Secret(storeKey), params, data.data() + head + nonceSize,
                                 data.size() - head - nonceSize, plain)) {
            error = "The key store does not open with this device's key";
            return false;
        }
        if (!LoadLocked(std::string(reinterpret_cast<const char*>(plain.Data()), plain.GetSize()), error))
            return false;
    } else {
        if (!account_.Create()) {
            error = "Could not create the Olm account: " + OlmLastError();
            return false;
        }
        dirty_ = true;
    }
    open_ = true;
    // What changed while we were away is not known.
    for (auto& entry : tracked_) entry.second = true;
    SaveLocked();
    return true;
}

void MatrixCrypto::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_) SaveLocked();
    open_ = false;
    account_ = OlmAccount();
    olm_.clear();
    outbound_.clear();
    inbound_.clear();
    devices_.clear();
    tracked_.clear();
    seen_.clear();
    UltraCrypt_SecureZero(storeKey_.data(), storeKey_.size());
    storeKey_.clear();
    UltraCrypt_SecureZero(pickleKey_.data(), pickleKey_.size());
}

bool MatrixCrypto::IsOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
}

std::string MatrixCrypto::GetUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return userId_;
}

std::string MatrixCrypto::GetDeviceId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceId_;
}

std::string MatrixCrypto::GetCurve25519() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return account_.Curve25519();
}

std::string MatrixCrypto::GetEd25519() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return account_.Ed25519();
}

bool MatrixCrypto::LoadLocked(const std::string& text, std::string& error) {
    const JSONValue store = Parse(text);
    if (store["user_id"].GetString() != userId_ || store["device_id"].GetString() != deviceId_) {
        error = "The key store belongs to another device";
        return false;
    }
    if (!account_.Unpickle(store["account"].GetString(), pickleKey_)) {
        error = "The Olm account does not unpickle: " + OlmLastError();
        return false;
    }
    deviceKeysUploaded_ = store["device_keys_uploaded"].GetBoolean(false);
    for (const auto& e : store["olm"].GetElements()) {
        StoredOlm s;
        if (!s.session.Unpickle(e["pickle"].GetString(), pickleKey_)) continue;
        s.lastUsedMs = e["used"].GetInteger(0);
        olm_[e["key"].GetString()].push_back(std::move(s));
    }
    for (const auto& e : store["outbound"].GetElements()) {
        Outbound o;
        if (!o.session.Unpickle(e["pickle"].GetString(), pickleKey_)) continue;
        o.createdMs = e["created"].GetInteger(0);
        o.messages = static_cast<int>(e["messages"].GetInteger(0));
        for (const auto& d : e["shared"].GetElements()) o.sharedWith.insert(d.GetString());
        outbound_[e["room"].GetString()] = std::move(o);
    }
    for (const auto& e : store["inbound"].GetElements()) {
        Inbound i;
        if (!i.session.Unpickle(e["pickle"].GetString(), pickleKey_)) continue;
        i.roomId = e["room"].GetString();
        i.senderKey = e["sender_key"].GetString();
        i.claimedEd25519 = e["ed25519"].GetString();
        i.senderUser = e["sender"].GetString();
        inbound_[i.roomId + "|" + i.session.Id()] = std::move(i);
    }
    for (const auto& user : store["devices"].GetMembers()) {
        for (const auto& dev : user.second.GetMembers()) {
            MatrixDeviceInfo d;
            d.userId = user.first;
            d.deviceId = dev.first;
            d.curve25519 = dev.second["curve25519"].GetString();
            d.ed25519 = dev.second["ed25519"].GetString();
            d.displayName = dev.second["name"].GetString();
            d.conflicted = dev.second["conflicted"].GetBoolean(false);
            devices_[user.first][dev.first] = d;
        }
    }
    for (const auto& user : store["tracked"].GetMembers()) tracked_[user.first] = user.second.GetBoolean(true);
    for (const auto& s : store["seen"].GetMembers()) seen_[s.first] = s.second.GetString();
    return true;
}

std::string MatrixCrypto::SerializeLocked() const {
    JSONValue store = JSONValue::MakeObject();
    store.Set("version", JSONValue(1));
    store.Set("user_id", JSONValue(userId_));
    store.Set("device_id", JSONValue(deviceId_));
    store.Set("account", JSONValue(account_.Pickle(pickleKey_)));
    store.Set("device_keys_uploaded", JSONValue(deviceKeysUploaded_));
    JSONValue olm = JSONValue::MakeArray();
    for (const auto& entry : olm_) {
        for (const auto& s : entry.second) {
            JSONValue e = JSONValue::MakeObject();
            e.Set("key", JSONValue(entry.first));
            e.Set("pickle", JSONValue(s.session.Pickle(pickleKey_)));
            e.Set("used", JSONValue(s.lastUsedMs));
            olm.Append(e);
        }
    }
    store.Set("olm", olm);
    JSONValue outbound = JSONValue::MakeArray();
    for (const auto& entry : outbound_) {
        JSONValue e = JSONValue::MakeObject();
        e.Set("room", JSONValue(entry.first));
        e.Set("pickle", JSONValue(entry.second.session.Pickle(pickleKey_)));
        e.Set("created", JSONValue(entry.second.createdMs));
        e.Set("messages", JSONValue(entry.second.messages));
        e.Set("shared", Strings(std::vector<std::string>(entry.second.sharedWith.begin(), entry.second.sharedWith.end())));
        outbound.Append(e);
    }
    store.Set("outbound", outbound);
    JSONValue inbound = JSONValue::MakeArray();
    for (const auto& entry : inbound_) {
        JSONValue e = JSONValue::MakeObject();
        e.Set("room", JSONValue(entry.second.roomId));
        e.Set("pickle", JSONValue(entry.second.session.Pickle(pickleKey_)));
        e.Set("sender_key", JSONValue(entry.second.senderKey));
        e.Set("ed25519", JSONValue(entry.second.claimedEd25519));
        e.Set("sender", JSONValue(entry.second.senderUser));
        inbound.Append(e);
    }
    store.Set("inbound", inbound);
    JSONValue devices = JSONValue::MakeObject();
    for (const auto& user : devices_) {
        JSONValue list = JSONValue::MakeObject();
        for (const auto& dev : user.second) {
            JSONValue d = JSONValue::MakeObject();
            d.Set("curve25519", JSONValue(dev.second.curve25519));
            d.Set("ed25519", JSONValue(dev.second.ed25519));
            d.Set("name", JSONValue(dev.second.displayName));
            if (dev.second.conflicted) d.Set("conflicted", JSONValue(true));
            list.Set(dev.first, d);
        }
        devices.Set(user.first, list);
    }
    store.Set("devices", devices);
    JSONValue tracked = JSONValue::MakeObject();
    for (const auto& t : tracked_) tracked.Set(t.first, JSONValue(t.second));
    store.Set("tracked", tracked);
    JSONValue seen = JSONValue::MakeObject();
    for (const auto& s : seen_) seen.Set(s.first, JSONValue(s.second));
    store.Set("seen", seen);
    return UltraCanvas::JSON::Serialize(store);
}

void MatrixCrypto::SaveLocked() {
    if (path_.empty() || storeKey_.empty() || !account_.IsValid()) return;
    const std::string text = SerializeLocked();
    UltraCryptAeadParams params;
    params.associatedData.assign(kStoreMagic, kStoreMagic + sizeof(kStoreMagic));
    std::vector<uint8_t> sealed;
    if (!UltraCrypt_AeadSeal(Secret(storeKey_), params, text.data(), text.size(), sealed)) return;
    namespace fs = std::filesystem;
    const fs::path target = UltraCanvas::PathFromUtf8(path_);
    const fs::path temp = UltraCanvas::PathFromUtf8(path_ + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(kStoreMagic, sizeof(kStoreMagic));
        out.write(reinterpret_cast<const char*>(params.nonce.data()), static_cast<std::streamsize>(params.nonce.size()));
        out.write(reinterpret_cast<const char*>(sealed.data()), static_cast<std::streamsize>(sealed.size()));
        if (!out) return;
    }
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (!ec) dirty_ = false;
}

void MatrixCrypto::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_ && dirty_) SaveLocked();
}

// ============================================================================
// KEYS ON THE SERVER
// ============================================================================

JSONValue MatrixCrypto::SignedLocked(JSONValue object) const {
    const std::string signature = account_.Sign(SignedPart(object));
    JSONValue mine = JSONValue::MakeObject();
    mine.Set("ed25519:" + deviceId_, JSONValue(signature));
    JSONValue signatures = JSONValue::MakeObject();
    signatures.Set(userId_, mine);
    object.Set("signatures", signatures);
    return object;
}

bool MatrixCrypto::UploadKeys(MatrixHttp& http, int64_t serverKeyCount, bool fallbackUnused, std::string& error) {
    JSONValue body = JSONValue::MakeObject();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) return false;
        if (!deviceKeysUploaded_) {
            JSONValue keys = JSONValue::MakeObject();
            keys.Set("user_id", JSONValue(userId_));
            keys.Set("device_id", JSONValue(deviceId_));
            keys.Set("algorithms", Strings({kOlm, kMegolm}));
            JSONValue list = JSONValue::MakeObject();
            list.Set("curve25519:" + deviceId_, JSONValue(account_.Curve25519()));
            list.Set("ed25519:" + deviceId_, JSONValue(account_.Ed25519()));
            keys.Set("keys", list);
            body.Set("device_keys", SignedLocked(keys));
        }
        // Half the account's capacity on the server, the other half free for
        // the keys that arrive while the server's count is on its way.
        const int64_t target = static_cast<int64_t>(account_.MaxOneTimeKeys() / 2);
        const int64_t onServer = serverKeyCount >= 0 ? serverKeyCount : (deviceKeysUploaded_ ? target : 0);
        const int64_t waiting = static_cast<int64_t>(account_.OneTimeKeys().size());
        if (onServer + waiting < target) {
            account_.GenerateOneTimeKeys(static_cast<size_t>(target - onServer - waiting));
            dirty_ = true;
        }
        if ((!fallbackUnused || !deviceKeysUploaded_) && account_.FallbackKey().empty()) {
            account_.GenerateFallbackKey();
            dirty_ = true;
        }
        JSONValue oneTime = JSONValue::MakeObject();
        for (const auto& k : account_.OneTimeKeys()) {
            JSONValue key = JSONValue::MakeObject();
            key.Set("key", JSONValue(k.second));
            oneTime.Set(std::string(kKeyAlgorithm) + ":" + k.first, SignedLocked(key));
        }
        JSONValue fallback = JSONValue::MakeObject();
        for (const auto& k : account_.FallbackKey()) {
            JSONValue key = JSONValue::MakeObject();
            key.Set("key", JSONValue(k.second));
            key.Set("fallback", JSONValue(true));
            fallback.Set(std::string(kKeyAlgorithm) + ":" + k.first, SignedLocked(key));
        }
        if (oneTime.GetSize()) body.Set("one_time_keys", oneTime);
        if (fallback.GetSize()) body.Set("fallback_keys", fallback);
        if (!body.GetSize()) return true;
        // The private halves on disk before the public ones leave: a key
        // someone claims has to be there to answer them.
        SaveLocked();
    }
    MatrixResponse r = http.Await("POST", std::string(kClient) + "/keys/upload", &body, 20000);
    if (!r.Ok()) {
        error = r.error.empty() ? "The homeserver refused our keys" : r.error;
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    deviceKeysUploaded_ = true;
    account_.MarkKeysAsPublished();
    SaveLocked();
    return true;
}

std::vector<MatrixSessionRef> MatrixCrypto::HandleSync(MatrixHttp& http, const JSONValue& sync) {
    std::vector<MatrixSessionRef> arrived;
    size_t maxKeys = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) return arrived;
        maxKeys = account_.MaxOneTimeKeys();
        for (const auto& u : sync["device_lists"]["changed"].GetElements()) {
            auto t = tracked_.find(u.GetString());
            if (t != tracked_.end() && !t->second) {
                t->second = true;
                dirty_ = true;
            }
        }
        for (const auto& u : sync["device_lists"]["left"].GetElements()) {
            if (tracked_.erase(u.GetString()) + devices_.erase(u.GetString())) dirty_ = true;
        }
        for (const auto& event : sync["to_device"]["events"].GetElements()) {
            if (event["type"].GetString() != "m.room.encrypted") continue;
            std::string error;
            const JSONValue payload = DecryptToDeviceLocked(event, error);
            if (!payload.IsObject()) continue;
            if (payload["type"].GetString() != "m.room_key") continue;     // m.dummy and the rest
            const JSONValue& content = payload["content"];
            if (content["algorithm"].GetString() != kMegolm) continue;
            const std::string roomId = content["room_id"].GetString();
            const std::string sessionId = content["session_id"].GetString();
            if (AddInboundLocked(roomId, content["session_key"].GetString(), false, sessionId,
                                 event["content"]["sender_key"].GetString(), payload["keys"]["ed25519"].GetString(),
                                 event["sender"].GetString()))
                arrived.push_back({roomId, sessionId});
        }
        if (dirty_) SaveLocked();
    }
    // Our one-time keys, as the server counts them: more when they run low.
    const JSONValue* counts = sync.Find("device_one_time_keys_count");
    const int64_t count = counts && counts->IsObject() ? (*counts)[kKeyAlgorithm].GetInteger(0) : -1;
    bool fallbackUnused = true;
    if (const JSONValue* unused = sync.Find("device_unused_fallback_key_types")) {
        fallbackUnused = false;
        for (const auto& t : unused->GetElements()) fallbackUnused = fallbackUnused || t.GetString() == kKeyAlgorithm;
    }
    if ((count >= 0 && count < static_cast<int64_t>(maxKeys / 2)) || !fallbackUnused) {
        std::string error;
        UploadKeys(http, count, fallbackUnused, error);
    }
    return arrived;
}

// ============================================================================
// DEVICE LISTS
// ============================================================================

bool MatrixCrypto::VerifyDeviceKeysLocked(const JSONValue& keys, const std::string& userId,
                                          const std::string& deviceId, MatrixDeviceInfo& out) const {
    if (keys["user_id"].GetString() != userId || keys["device_id"].GetString() != deviceId) return false;
    out.userId = userId;
    out.deviceId = deviceId;
    out.ed25519 = keys["keys"]["ed25519:" + deviceId].GetString();
    out.curve25519 = keys["keys"]["curve25519:" + deviceId].GetString();
    out.displayName = keys["unsigned"]["device_display_name"].GetString();
    if (out.ed25519.empty() || out.curve25519.empty()) return false;
    const std::string signature = keys["signatures"][userId]["ed25519:" + deviceId].GetString();
    return OlmVerifyEd25519(out.ed25519, SignedPart(keys), signature);
}

bool MatrixCrypto::UpdateDeviceLists(MatrixHttp& http, const std::vector<std::string>& users, std::string& error) {
    JSONValue query = JSONValue::MakeObject();
    std::vector<std::string> wanted;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) return false;
        for (const auto& u : users) {
            auto t = tracked_.find(u);
            if (t != tracked_.end() && !t->second) continue;
            if (std::find(wanted.begin(), wanted.end(), u) != wanted.end()) continue;
            wanted.push_back(u);
            query.Set(u, JSONValue::MakeArray());
        }
    }
    if (wanted.empty()) return true;
    JSONValue body = JSONValue::MakeObject();
    body.Set("device_keys", query);
    body.Set("timeout", JSONValue(10000));
    MatrixResponse r = http.Await("POST", std::string(kClient) + "/keys/query", &body, 20000);
    if (!r.Ok()) {
        error = r.error.empty() ? "The homeserver did not give the device lists" : r.error;
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& u : wanted) {
        const JSONValue& list = r.body["device_keys"][u];
        if (!list.IsObject()) continue;         // its server did not answer: still outdated
        auto& known = devices_[u];
        std::map<std::string, MatrixDeviceInfo> fresh;
        for (const auto& dev : list.GetMembers()) {
            MatrixDeviceInfo d;
            if (!VerifyDeviceKeysLocked(dev.second, u, dev.first, d)) continue;
            if (u == userId_ && dev.first == deviceId_) continue;
            auto k = known.find(dev.first);
            if (k != known.end() && (k->second.ed25519 != d.ed25519 || k->second.curve25519 != d.curve25519)) {
                // Trusted on first use: a device's keys do not change. The
                // ones we had stay, and the device is not trusted any more.
                d = k->second;
                d.conflicted = true;
            } else if (k != known.end()) {
                d.conflicted = k->second.conflicted;
            }
            fresh[dev.first] = d;
        }
        known = fresh;
        tracked_[u] = false;
    }
    dirty_ = true;
    SaveLocked();
    return true;
}

void MatrixCrypto::InvalidateDeviceLists() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& t : tracked_) t.second = true;
    dirty_ = true;
}

std::vector<MatrixDeviceInfo> MatrixCrypto::DevicesOf(const std::string& userId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MatrixDeviceInfo> out;
    auto it = devices_.find(userId);
    if (it != devices_.end()) {
        for (const auto& d : it->second) out.push_back(d.second);
    }
    return out;
}

const MatrixDeviceInfo* MatrixCrypto::DeviceByCurveLocked(const std::string& curve) const {
    for (const auto& user : devices_) {
        for (const auto& d : user.second) {
            if (d.second.curve25519 == curve) return &d.second;
        }
    }
    return nullptr;
}

bool MatrixCrypto::HasSession(const std::string& roomId, const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inbound_.count(roomId + "|" + sessionId) != 0;
}

// ============================================================================
// OLM (TO-DEVICE)
// ============================================================================

OlmSession* MatrixCrypto::SessionForLocked(const std::string& curve) {
    auto it = olm_.find(curve);
    if (it == olm_.end() || it->second.empty()) return nullptr;
    StoredOlm* best = &it->second.front();
    for (auto& s : it->second) {
        if (s.lastUsedMs > best->lastUsedMs) best = &s;
    }
    best->lastUsedMs = NowMs();
    return &best->session;
}

JSONValue MatrixCrypto::DecryptToDeviceLocked(const JSONValue& event, std::string& error) {
    const JSONValue& content = event["content"];
    if (content["algorithm"].GetString() != kOlm) {
        error = "Not an Olm message";
        return JSONValue();
    }
    const std::string senderKey = content["sender_key"].GetString();
    const JSONValue& mine = content["ciphertext"][account_.Curve25519()];
    if (!mine.IsObject()) {
        error = "Not encrypted for this device";
        return JSONValue();
    }
    const int type = static_cast<int>(mine["type"].GetInteger(-1));
    const std::string body = mine["body"].GetString();
    std::string plaintext;
    bool decrypted = false;
    auto& sessions = olm_[senderKey];
    for (auto& s : sessions) {
        if (type == 0 && !s.session.Matches(body)) continue;
        if (s.session.Decrypt(type, body, plaintext)) {
            s.lastUsedMs = NowMs();
            decrypted = true;
            break;
        }
        if (type == 0) break;       // its session, and it does not decrypt
    }
    if (!decrypted && type == 0) {
        bool known = false;
        for (auto& s : sessions) known = known || s.session.Matches(body);
        if (!known) {
            OlmSession fresh = account_.InboundSession(senderKey, body, plaintext);
            if (fresh.IsValid()) {
                StoredOlm s;
                s.session = std::move(fresh);
                s.lastUsedMs = NowMs();
                sessions.push_back(std::move(s));
                decrypted = true;
            }
        }
    }
    if (sessions.empty()) olm_.erase(senderKey);
    if (!decrypted) {
        error = "The Olm message does not decrypt: " + OlmLastError();
        return JSONValue();
    }
    dirty_ = true;
    JSONValue payload = Parse(plaintext);
    // Who it says it is from and to must be who it is from and to.
    const std::string claimedEd = payload["keys"]["ed25519"].GetString();
    if (payload["sender"].GetString() != event["sender"].GetString() || payload["recipient"].GetString() != userId_ ||
        payload["recipient_keys"]["ed25519"].GetString() != account_.Ed25519() || claimedEd.empty()) {
        error = "The Olm message is not addressed as it was sent";
        return JSONValue();
    }
    if (const MatrixDeviceInfo* d = DeviceByCurveLocked(senderKey)) {
        if (d->userId != event["sender"].GetString() || d->ed25519 != claimedEd || d->conflicted) {
            error = "The Olm message's keys are not its sender's";
            return JSONValue();
        }
    }
    return payload;
}

// ============================================================================
// MEGOLM (ROOMS)
// ============================================================================

bool MatrixCrypto::AddInboundLocked(const std::string& roomId, const std::string& sessionKey, bool exported,
                                    const std::string& sessionId, const std::string& senderKey,
                                    const std::string& claimedEd, const std::string& senderUser) {
    if (roomId.empty() || sessionId.empty()) return false;
    MegolmInbound session;
    if (!(exported ? session.Import(sessionKey) : session.Create(sessionKey))) return false;
    if (session.Id() != sessionId) return false;
    const std::string id = roomId + "|" + sessionId;
    auto existing = inbound_.find(id);
    // One that reaches further back is worth more.
    if (existing != inbound_.end() && existing->second.session.FirstKnownIndex() <= session.FirstKnownIndex())
        return false;
    Inbound i;
    i.session = std::move(session);
    i.roomId = roomId;
    i.senderKey = senderKey;
    i.claimedEd25519 = claimedEd;
    i.senderUser = existing != inbound_.end() && !existing->second.senderUser.empty() ? existing->second.senderUser
                                                                                      : senderUser;
    inbound_[id] = std::move(i);
    dirty_ = true;
    return true;
}

MatrixDecrypted MatrixCrypto::DecryptRoomEvent(const std::string& roomId, const JSONValue& event) {
    MatrixDecrypted out;
    const JSONValue& content = event["content"];
    out.sessionId = content["session_id"].GetString();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) {
        out.error = "End-to-end encryption is not set up on this device";
        return out;
    }
    if (content["algorithm"].GetString() != kMegolm) {
        out.error = "Encrypted with " + content["algorithm"].GetString("an unknown algorithm");
        return out;
    }
    auto it = inbound_.find(roomId + "|" + out.sessionId);
    if (it == inbound_.end()) {
        out.state = MatrixDecryptState::MissingKey;
        out.error = "The key to this message has not arrived";
        return out;
    }
    Inbound& session = it->second;
    std::string plaintext;
    uint32_t index = 0;
    if (!session.session.Decrypt(content["ciphertext"].GetString(), plaintext, index)) {
        const int64_t at = MegolmMessageIndex(content["ciphertext"].GetString());
        if (at >= 0 && at < static_cast<int64_t>(session.session.FirstKnownIndex())) {
            // From before our copy of the session starts: an older copy (a
            // key export, another device) may still come.
            out.state = MatrixDecryptState::MissingKey;
            out.error = "The key to this message has not arrived (only to later ones)";
            return out;
        }
        out.error = "The message does not decrypt: " + OlmLastError();
        return out;
    }
    // The same index twice is the same message, or someone replaying it.
    const std::string eventId = event["event_id"].GetString();
    const std::string seenKey = out.sessionId + "|" + std::to_string(index);
    auto seen = seen_.find(seenKey);
    if (seen != seen_.end() && seen->second != eventId) {
        out.error = "The message repeats an earlier one (a replay)";
        return out;
    }
    if (seen == seen_.end()) {
        seen_[seenKey] = eventId;
        dirty_ = true;
    }
    JSONValue payload = Parse(plaintext);
    if (payload["room_id"].GetString() != roomId) {
        out.error = "The message was encrypted for another room";
        return out;
    }
    const std::string sender = event["sender"].GetString();
    std::string keyOwner = session.senderUser;
    if (keyOwner.empty()) {
        if (const MatrixDeviceInfo* d = DeviceByCurveLocked(session.senderKey)) keyOwner = d->userId;
        else if (session.senderKey == account_.Curve25519()) keyOwner = userId_;
    }
    if (!keyOwner.empty() && keyOwner != sender) {
        out.error = "The message's key is not its sender's";
        return out;
    }
    out.event = event;
    out.event.Set("type", payload["type"]);
    JSONValue clear = payload["content"];
    if (!clear.Contains("m.relates_to") && content.Contains("m.relates_to")) clear.Set("m.relates_to", content["m.relates_to"]);
    out.event.Set("content", clear);
    out.state = MatrixDecryptState::Ok;
    return out;
}

bool MatrixCrypto::EncryptRoomEvent(MatrixHttp& http, const std::string& roomId,
                                    const std::vector<std::string>& members, const MatrixRoomEncryption& settings,
                                    const std::string& type, const JSONValue& content, JSONValue& encryptedContent,
                                    std::string& error) {
    std::lock_guard<std::mutex> share(shareMutex_);
    if (!UpdateDeviceLists(http, members, error)) return false;

    std::map<std::string, MatrixDeviceInfo> recipients;     // "user|device"
    std::vector<MatrixDeviceInfo> needSession;
    std::string sessionId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) {
            error = "End-to-end encryption is not set up on this device";
            return false;
        }
        for (const auto& u : members) {
            auto list = devices_.find(u);
            if (list == devices_.end()) continue;
            for (const auto& d : list->second) {
                if (d.second.conflicted || (u == userId_ && d.first == deviceId_)) continue;
                recipients[u + "|" + d.first] = d.second;
            }
        }
        auto current = outbound_.find(roomId);
        bool rotate = current == outbound_.end() || !current->second.session.IsValid();
        if (!rotate) {
            const Outbound& o = current->second;
            rotate = o.messages >= std::max(1, settings.rotationPeriodMessages) ||
                     NowMs() - o.createdMs >= std::max<int64_t>(3600000, settings.rotationPeriodMs);
            // Someone (or a device) is gone: what we send next is not for them.
            for (const auto& d : o.sharedWith) rotate = rotate || !recipients.count(d);
        }
        if (rotate) {
            Outbound fresh;
            if (!fresh.session.Create()) {
                error = "Could not start a room session: " + OlmLastError();
                return false;
            }
            fresh.createdMs = NowMs();
            // We read our own messages with it too.
            AddInboundLocked(roomId, fresh.session.SessionKey(), false, fresh.session.Id(), account_.Curve25519(),
                             account_.Ed25519(), userId_);
            outbound_[roomId] = std::move(fresh);
            dirty_ = true;
        }
        const Outbound& o = outbound_[roomId];
        sessionId = o.session.Id();
        for (const auto& r : recipients) {
            if (o.sharedWith.count(r.first)) continue;
            auto sessions = olm_.find(r.second.curve25519);
            if (sessions == olm_.end() || sessions->second.empty()) needSession.push_back(r.second);
        }
    }

    // Devices we have no Olm session with: one of their one-time keys each.
    if (!needSession.empty()) {
        std::map<std::string, JSONValue> perUser;
        for (const auto& d : needSession) {
            JSONValue& devices = perUser[d.userId];
            if (devices.IsNull()) devices = JSONValue::MakeObject();
            devices.Set(d.deviceId, JSONValue(kKeyAlgorithm));
        }
        JSONValue claim = JSONValue::MakeObject();
        for (const auto& u : perUser) claim.Set(u.first, u.second);
        JSONValue body = JSONValue::MakeObject();
        body.Set("one_time_keys", claim);
        body.Set("timeout", JSONValue(10000));
        MatrixResponse r = http.Await("POST", std::string(kClient) + "/keys/claim", &body, 20000);
        if (r.Ok()) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& d : needSession) {
                for (const auto& k : r.body["one_time_keys"][d.userId][d.deviceId].GetMembers()) {
                    if (k.first.rfind(std::string(kKeyAlgorithm) + ":", 0) != 0) continue;
                    const std::string signature = k.second["signatures"][d.userId]["ed25519:" + d.deviceId].GetString();
                    if (!OlmVerifyEd25519(d.ed25519, SignedPart(k.second), signature)) continue;
                    OlmSession session = account_.OutboundSession(d.curve25519, k.second["key"].GetString());
                    if (!session.IsValid()) continue;
                    StoredOlm s;
                    s.session = std::move(session);
                    s.lastUsedMs = NowMs();
                    olm_[d.curve25519].push_back(std::move(s));
                    dirty_ = true;
                    break;
                }
            }
        }
    }

    // The room key to each device that has not got it.
    std::map<std::string, JSONValue> messages;      // user -> {device: content}
    std::vector<std::string> sentTo;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto current = outbound_.find(roomId);
        if (current == outbound_.end() || current->second.session.Id() != sessionId) {
            error = "The room's session changed while it was being shared - send again";
            return false;
        }
        Outbound& o = current->second;
        JSONValue key = JSONValue::MakeObject();
        key.Set("algorithm", JSONValue(kMegolm));
        key.Set("room_id", JSONValue(roomId));
        key.Set("session_id", JSONValue(sessionId));
        key.Set("session_key", JSONValue(o.session.SessionKey()));
        JSONValue ownKeys = JSONValue::MakeObject();
        ownKeys.Set("ed25519", JSONValue(account_.Ed25519()));
        for (const auto& r : recipients) {
            if (o.sharedWith.count(r.first)) continue;
            OlmSession* session = SessionForLocked(r.second.curve25519);
            if (!session) continue;         // no key of theirs to be had: they cannot read it
            JSONValue payload = JSONValue::MakeObject();
            payload.Set("type", JSONValue("m.room_key"));
            payload.Set("content", key);
            payload.Set("sender", JSONValue(userId_));
            payload.Set("sender_device", JSONValue(deviceId_));
            payload.Set("keys", ownKeys);
            payload.Set("recipient", JSONValue(r.second.userId));
            JSONValue theirKeys = JSONValue::MakeObject();
            theirKeys.Set("ed25519", JSONValue(r.second.ed25519));
            payload.Set("recipient_keys", theirKeys);
            std::string body;
            const int messageType = session->Encrypt(UltraCanvas::JSON::Serialize(payload), body);
            if (messageType < 0) continue;
            JSONValue cipher = JSONValue::MakeObject();
            JSONValue part = JSONValue::MakeObject();
            part.Set("type", JSONValue(messageType));
            part.Set("body", JSONValue(body));
            cipher.Set(r.second.curve25519, part);
            JSONValue encrypted = JSONValue::MakeObject();
            encrypted.Set("algorithm", JSONValue(kOlm));
            encrypted.Set("sender_key", JSONValue(account_.Curve25519()));
            encrypted.Set("ciphertext", cipher);
            JSONValue& devices = messages[r.second.userId];
            if (devices.IsNull()) devices = JSONValue::MakeObject();
            devices.Set(r.second.deviceId, encrypted);
            sentTo.push_back(r.first);
            dirty_ = true;
        }
        // The ratchets moved: on disk before the messages leave.
        if (dirty_) SaveLocked();
    }
    if (!messages.empty()) {
        JSONValue all = JSONValue::MakeObject();
        for (const auto& m : messages) all.Set(m.first, m.second);
        JSONValue body = JSONValue::MakeObject();
        body.Set("messages", all);
        MatrixResponse r = http.Await("PUT", std::string(kClient) + "/sendToDevice/m.room.encrypted/" +
                                                 PathSegment(UltraIM_NewClientMessageId()),
                                      &body, 20000);
        if (!r.Ok()) {
            error = r.error.empty() ? "The room key could not be sent" : r.error;
            return false;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto current = outbound_.find(roomId);
    if (current == outbound_.end() || current->second.session.Id() != sessionId) {
        error = "The room's session changed while it was being shared - send again";
        return false;
    }
    Outbound& o = current->second;
    o.sharedWith.insert(sentTo.begin(), sentTo.end());
    JSONValue payload = JSONValue::MakeObject();
    payload.Set("type", JSONValue(type));
    payload.Set("content", content);
    payload.Set("room_id", JSONValue(roomId));
    const std::string ciphertext = o.session.Encrypt(UltraCanvas::JSON::Serialize(payload));
    if (ciphertext.empty()) {
        error = "The message does not encrypt: " + OlmLastError();
        return false;
    }
    ++o.messages;
    encryptedContent = JSONValue::MakeObject();
    encryptedContent.Set("algorithm", JSONValue(kMegolm));
    encryptedContent.Set("sender_key", JSONValue(account_.Curve25519()));
    encryptedContent.Set("ciphertext", JSONValue(ciphertext));
    encryptedContent.Set("session_id", JSONValue(sessionId));
    encryptedContent.Set("device_id", JSONValue(deviceId_));
    // Relations stay readable to the server, which aggregates edits by them.
    if (content.Contains("m.relates_to")) encryptedContent.Set("m.relates_to", content["m.relates_to"]);
    dirty_ = true;
    SaveLocked();
    return true;
}

// ============================================================================
// KEY EXPORT FILES
// ============================================================================
// version 1 | salt (16) | IV (16) | rounds (4, big-endian) | AES-256-CTR
// ciphertext | HMAC-SHA-256 of everything before, base64 between the BEGIN
// and END lines. The keys: PBKDF2-HMAC-SHA-512 of the passphrase, 64 bytes,
// the first half for AES and the second for the HMAC.

namespace {

bool ExportKeys(const std::string& passphrase, const std::vector<uint8_t>& salt, uint32_t rounds,
                UltraCryptSecureBuffer& aesKey, UltraCryptSecureBuffer& macKey) {
    UltraCryptSecureBuffer derived;
    const UltraCryptSecureBuffer password(passphrase.data(), passphrase.size());
    if (!UltraCrypt_DeriveKeyPbkdf2(UltraCryptHashAlgorithm::SHA512, password, salt, rounds, 64, derived)) return false;
    aesKey = UltraCryptSecureBuffer(derived.Data(), 32);
    macKey = UltraCryptSecureBuffer(derived.Data() + 32, 32);
    return true;
}

} // namespace

bool MatrixCrypto::ExportRoomKeys(const std::string& passphrase, uint32_t rounds, std::string& out,
                                  std::string& error) {
    out.clear();
    if (passphrase.empty()) {
        error = "A key export needs a passphrase";
        return false;
    }
    JSONValue sessions = JSONValue::MakeArray();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : inbound_) {
            Inbound& i = entry.second;
            JSONValue s = JSONValue::MakeObject();
            s.Set("algorithm", JSONValue(kMegolm));
            s.Set("room_id", JSONValue(i.roomId));
            s.Set("sender_key", JSONValue(i.senderKey));
            s.Set("session_id", JSONValue(i.session.Id()));
            s.Set("session_key", JSONValue(i.session.ExportAt(i.session.FirstKnownIndex())));
            JSONValue claimed = JSONValue::MakeObject();
            claimed.Set("ed25519", JSONValue(i.claimedEd25519));
            s.Set("sender_claimed_keys", claimed);
            s.Set("forwarding_curve25519_key_chain", JSONValue::MakeArray());
            sessions.Append(s);
        }
    }
    const std::string plaintext = UltraCanvas::JSON::Serialize(sessions);
    std::vector<uint8_t> salt;
    UltraCryptCtrParams ctr;
    if (!UltraCrypt_RandomBytes(salt, 16) || !UltraCrypt_RandomBytes(ctr.iv, 16)) {
        error = "No randomness for the export";
        return false;
    }
    ctr.iv[8] &= 0x7f;      // the 64-bit counter of WebCrypto never wraps
    UltraCryptSecureBuffer aesKey, macKey;
    if (!ExportKeys(passphrase, salt, rounds, aesKey, macKey)) {
        error = "Could not derive the export's keys";
        return false;
    }
    std::vector<uint8_t> ciphertext;
    if (!UltraCrypt_AesCtrEncrypt(aesKey, ctr, plaintext.data(), plaintext.size(), ciphertext)) {
        error = "Could not encrypt the export";
        return false;
    }
    std::vector<uint8_t> data;
    data.push_back(1);
    data.insert(data.end(), salt.begin(), salt.end());
    data.insert(data.end(), ctr.iv.begin(), ctr.iv.end());
    for (int shift = 24; shift >= 0; shift -= 8) data.push_back(static_cast<uint8_t>(rounds >> shift));
    data.insert(data.end(), ciphertext.begin(), ciphertext.end());
    std::vector<uint8_t> mac;
    if (!UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA256, macKey, data.data(), data.size(), mac)) {
        error = "Could not sign the export";
        return false;
    }
    data.insert(data.end(), mac.begin(), mac.end());
    const std::string encoded = UltraCanvas::Base64Encode(data, false);
    out = std::string(kExportBegin) + "\n";
    for (size_t at = 0; at < encoded.size(); at += 96) out += encoded.substr(at, 96) + "\n";
    out += std::string(kExportEnd) + "\n";
    return true;
}

bool MatrixCrypto::ImportRoomKeys(const std::string& text, const std::string& passphrase,
                                  std::vector<MatrixSessionRef>& imported, std::string& error) {
    imported.clear();
    const size_t begin = text.find(kExportBegin);
    const size_t end = text.find(kExportEnd);
    if (begin == std::string::npos || end == std::string::npos || end < begin) {
        error = "Not a Matrix key export file";
        return false;
    }
    const size_t from = begin + std::string(kExportBegin).size();
    const std::vector<uint8_t> data = UltraCanvas::Base64Decode(text.substr(from, end - from));
    const size_t header = 1 + 16 + 16 + 4;
    if (data.size() < header + 32 || data[0] != 1) {
        error = "The key export is damaged, or of a version this provider does not read";
        return false;
    }
    const std::vector<uint8_t> salt(data.begin() + 1, data.begin() + 17);
    UltraCryptCtrParams ctr;
    ctr.iv.assign(data.begin() + 17, data.begin() + 33);
    const uint32_t rounds = (static_cast<uint32_t>(data[33]) << 24) | (static_cast<uint32_t>(data[34]) << 16) |
                            (static_cast<uint32_t>(data[35]) << 8) | data[36];
    UltraCryptSecureBuffer aesKey, macKey;
    if (rounds == 0 || !ExportKeys(passphrase, salt, rounds, aesKey, macKey)) {
        error = "Could not derive the export's keys";
        return false;
    }
    // The MAC first: nothing is decrypted that it does not vouch for.
    std::vector<uint8_t> mac;
    UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA256, macKey, data.data(), data.size() - 32, mac);
    if (!UltraCrypt_ConstantTimeEquals(mac.data(), mac.size(), data.data() + data.size() - 32, 32)) {
        error = "Wrong passphrase, or the key export was altered";
        return false;
    }
    UltraCryptSecureBuffer plain;
    if (!UltraCrypt_AesCtrDecrypt(aesKey, ctr, data.data() + header, data.size() - header - 32, plain)) {
        error = "Could not decrypt the export";
        return false;
    }
    const JSONValue sessions = Parse(std::string(reinterpret_cast<const char*>(plain.Data()), plain.GetSize()));
    if (!sessions.IsArray()) {
        error = "The key export holds no sessions";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) {
        error = "End-to-end encryption is not set up on this device";
        return false;
    }
    for (const auto& s : sessions.GetElements()) {
        if (s["algorithm"].GetString() != kMegolm) continue;
        const std::string roomId = s["room_id"].GetString();
        const std::string sessionId = s["session_id"].GetString();
        if (AddInboundLocked(roomId, s["session_key"].GetString(), true, sessionId, s["sender_key"].GetString(),
                             s["sender_claimed_keys"]["ed25519"].GetString(), std::string()))
            imported.push_back({roomId, sessionId});
    }
    if (dirty_) SaveLocked();
    return true;
}

} // namespace UltraIMMatrix
