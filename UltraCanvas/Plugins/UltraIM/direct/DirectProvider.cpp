// Plugins/UltraIM/direct/DirectProvider.cpp
// ChatCube Direct - the ULTRA OS native messenger as an UltraIM provider
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "DirectProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraRTC/UltraRTCCore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace UltraIMDirect {

using UltraCanvas::JSONValue;

namespace {

const char* const kDirectConversation = "dm";   // the envelope's conversation for 1:1
const char* const kGroupPrefix = "group:";

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

bool IsGroupId(const std::string& id) {
    return id.rfind(kGroupPrefix, 0) == 0;
}

std::string AttemptKey(const std::string& clientId, const std::string& recipient) {
    return clientId + "|" + recipient;
}

UltraIMPresence PresenceFrom(const std::string& text) {
    if (text == "online") return UltraIMPresence::Online;
    if (text == "offline") return UltraIMPresence::Offline;
    return UltraIMPresence::Unknown;
}


// A user id typed or pasted by a person: spaces and case do not matter.
std::string NormalizeUserId(const std::string& address) {
    std::string id;
    for (char c : address) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '-') continue;
        id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (id.size() != 32) return std::string();
    for (char c : id) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::string();
    }
    return id;
}

UltraIMResult RelayError(const RelayResponse& r, UltraIMResultCode fallback) {
    UltraIMResultCode code = fallback;
    if (r.status == 0) code = UltraIMResultCode::NetworkError;
    else if (r.status == 401) code = UltraIMResultCode::NotAuthenticated;
    else if (r.status == 404) code = UltraIMResultCode::NotFound;
    else if (r.status == 403) code = UltraIMResultCode::PermissionDenied;
    else if (r.status == 429) code = UltraIMResultCode::RateLimited;
    return UltraIMResult::Error(code, r.error.empty() ? "The relay refused the request" : r.error);
}

} // namespace

// ============================================================================
// LIFECYCLE
// ============================================================================

DirectProvider::DirectProvider(std::string providerId) : providerId_(std::move(providerId)) {}

DirectProvider::~DirectProvider() {
    Shutdown();
}

UltraIMCapabilityFlags DirectProvider::GetCapabilities() const {
    using F = UltraIMCapabilityFlags;
    return F::TextMessages | F::GroupChats | F::ReadReceipts | F::TypingIndicator | F::E2EEncryption |
           F::OfflineMessages | F::PeerToPeer | F::ReplyToMessage | F::AnonymousLogin | F::AddContact |
           F::CreateGroup | F::VoiceCalls | F::VideoCalls;
}

UltraIMResult DirectProvider::Initialize(const UltraIMConfig& config, UltraIMEventSink sink) {
    config_ = config;
    sink_ = std::move(sink);
    if (config.dataDirectory.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "ChatCube Direct needs a data directory (UltraIMConfig::dataDirectory)");

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = UltraCanvas::PathFromUtf8(config.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    fs::create_directories(dir, ec);
    std::string error;
    if (!store_.Open(UltraCanvas::PathToUtf8(dir / "direct.sqlite3"), providerId_, error))
        return UltraIMResult::Error(UltraIMResultCode::PluginError, "Cannot open the message store: " + error);
    storeOpen_ = true;
    vault_ = UltraVault::DeviceKeyVault(UltraCanvas::PathToUtf8(dir),
                                        {"direct.vault", "com.cloverleaf.chatcube." + providerId_ + "."});

    auto sendSignal = [this](const std::string& to, const std::string& kind, const std::string& session,
                             const std::string& data) { return SendSignal(to, kind, session, data); };
    links_.sendSignal = sendSignal;
    calls_.sendSignal = sendSignal;
    calls_.emit = [this](const UltraIMEvent& event) { Emit(event); };
    calls_.SetProviderId(providerId_);
    lan_.onPeerUp = [this](const std::string& peer, const std::vector<uint8_t>& key, const std::string& name) {
        OnLanPeerUp(peer, key, name);
    };
    lan_.onPeerDown = [this](const std::string& peer) { EmitPresence(peer); };
    lan_.onFrame = [this](const std::string& peer, const JSONValue& frame) { OnLanFrame(peer, frame); };
    lan_.onNearbyChanged = [this]() {
        UltraIMEvent event;
        event.type = UltraIMEventType::NearbyChange;
        event.providerId = providerId_;
        Emit(event);
    };
    links_.onOpen = [this](const std::string&) { Wake(); };
    links_.onMessage = [this](const std::string& peer, const std::string& text) { OnLinkMessage(peer, text); };
    links_.onClosed = [this](const std::string& peer) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : attempts_) {
            if (entry.first.size() > peer.size() &&
                entry.first.compare(entry.first.size() - peer.size(), peer.size(), peer) == 0)
                entry.second.linkSentMs = 0;
        }
    };

    stopping_ = false;
    worker_ = std::thread([this]() { WorkerLoop(); });
    return UltraIMResult::Ok();
}

bool DirectProvider::SendSignal(const std::string& to, const std::string& kind, const std::string& session,
                                const std::string& data) {
    const std::string signature = Sign(identity_, SignalMessage(identity_.userId, to, session, kind, data));
    // The local network when they are on it: no server in between.
    if (lan_.IsConnected(to)) {
        JSONValue frame = JSONValue::MakeObject();
        frame.Set("t", JSONValue("signal"));
        frame.Set("kind", JSONValue(kind));
        frame.Set("session", JSONValue(session));
        frame.Set("d", JSONValue(data));
        frame.Set("s", JSONValue(signature));
        if (lan_.Send(to, frame)) return true;
    }
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay) return false;
    JSONValue payload = JSONValue::MakeObject();
    payload.Set("d", JSONValue(data));
    payload.Set("s", JSONValue(signature));
    JSONValue frame = JSONValue::MakeObject();
    frame.Set("type", JSONValue("signal"));
    frame.Set("to", JSONValue(to));
    frame.Set("kind", JSONValue(kind));
    frame.Set("session", JSONValue(session));
    frame.Set("payload", payload);
    return relay->SendFrame(frame);
}

void DirectProvider::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ && !worker_.joinable()) return;
        stopping_ = true;
        wake_.notify_all();
    }
    if (worker_.joinable()) worker_.join();
    SignOut();
    if (storeOpen_) {
        store_.Close();
        storeOpen_ = false;
    }
    sink_ = nullptr;
}

// ============================================================================
// SESSION
// ============================================================================

// The vault is opened for each access and closed after (DeviceKeyVault::Use):
// the process has one open store, and other modules' vaults take turns with it.
bool DirectProvider::LoadIdentity(std::string& error) {
    std::string secret;
    if (!vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("identity", secret); })) {
        error = "The key vault cannot be opened: " +
                UltraVault::DeviceKeyVault::DescribeUnlockStatus(vault_.GetLastUnlockStatus());
        return false;
    }
    if (secret.empty()) {
        error.clear();
        return false;
    }
    const std::vector<uint8_t> bytes = FromBase64(secret);
    UltraCryptSecureBuffer buffer(bytes.data(), bytes.size());
    return IdentityFromSecret(buffer, identity_, error);
}

bool DirectProvider::SaveIdentity() {
    const std::vector<uint8_t> bytes(identity_.signSecret.Data(),
                                     identity_.signSecret.Data() + identity_.signSecret.GetSize());
    bool stored = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("identity", Base64(bytes)); });
    return stored;
}

bool DirectProvider::HasStoredSession() const {
    auto* self = const_cast<DirectProvider*>(this);
    // An identity is a session: with a relay remembered, or for the local
    // network alone.
    if (!storeOpen_) return false;
    if (self->store_.GetSetting("relay_url").empty() && self->store_.GetSetting("local_network") != "1") return false;
    bool has = false;
    self->vault_.Use([&](UltraVault::DeviceKeyVault& v) { has = v.Has("identity"); });
    return has;
}

std::string DirectProvider::GetLocalUserId() const {
    return identity_.userId;
}

void DirectProvider::SetState(UltraIMConnectionState state, const std::string& error, bool always) {
    if (state_.exchange(state) == state && error.empty() && !always) return;
    UltraIMEvent event;
    event.type = UltraIMEventType::ConnectionStateChange;
    event.providerId = providerId_;
    event.connectionState = state;
    event.errorText = error;
    Emit(event);
}

void DirectProvider::Emit(const UltraIMEvent& event) {
    if (sink_) sink_(event);
}

void DirectProvider::SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    if (state_ == UltraIMConnectionState::Connected || state_ == UltraIMConnectionState::Connecting) {
        finish(UltraIMResult::Ok());
        return;
    }
    // No relay at all: the local network only.
    const std::string relayUrl = credentials.server.empty() ? store_.GetSetting("relay_url") : credentials.server;
    if (credentials.localNetwork) store_.SetSetting("local_network", "1");
    lanOn_ = relayUrl.empty() || store_.GetSetting("local_network") == "1";
    if (relayUrl.empty()) store_.SetSetting("local_network", "1");

    std::string error;
    if (!identity_.IsValid() && !LoadIdentity(error)) {
        if (!error.empty()) {
            finish(UltraIMResult::Error(UltraIMResultCode::PluginError, error));
            return;
        }
        if (!credentials.createAccount) {
            finish(UltraIMResult::Error(UltraIMResultCode::NotAuthenticated,
                                        "This device has no ChatCube identity yet - create one"));
            return;
        }
        if (!CreateIdentity(identity_, error) || !SaveIdentity()) {
            finish(UltraIMResult::Error(UltraIMResultCode::PluginError,
                                        error.empty() ? "The new identity could not be stored" : error));
            return;
        }
    }
    displayName_ = !credentials.displayName.empty() ? credentials.displayName : store_.GetSetting("display_name");
    if (displayName_.empty()) displayName_ = "ChatCube user";
    store_.SetSetting("display_name", displayName_);
    links_.SetLocalUserId(identity_.userId);
    calls_.SetLocalUserId(identity_.userId);

    if (lanOn_ && !StartLan(error)) {
        // Without a relay that is the end of it; with one, the relay still works.
        if (relayUrl.empty()) {
            SetState(UltraIMConnectionState::Error, error);
            finish(UltraIMResult::Error(UltraIMResultCode::NetworkError, error));
            return;
        }
    }
    if (relayUrl.empty()) {
        SetState(UltraIMConnectionState::Connected);
        finish(UltraIMResult::Ok());
        Wake();
        return;
    }

    auto relayOwner = std::make_shared<RelayClient>();
    {
        std::lock_guard<std::mutex> lock(relayMutex_);
        relay_ = relayOwner;
    }
    // The sign-in chain below runs inside this client's own callbacks, which
    // Stop() waits for, so a raw pointer is valid wherever it is used.
    RelayClient* relay = relayOwner.get();
    relay->SetBaseUrl(relayUrl);
    relay->onFrame = [this](const JSONValue& frame) { OnFrame(frame); };
    relay->onSocketOpen = [this]() {
        // Reported even when already Connected: what the socket carries
        // (calls, presence) is available from now, and a UI re-checks.
        SetState(UltraIMConnectionState::Connected, std::string(), true);
        FetchQueue();
        Wake();
    };
    relay->onSocketClose = [this]() {
        if (state_ == UltraIMConnectionState::Connected) SetState(UltraIMConnectionState::Reconnecting);
    };
    SetState(UltraIMConnectionState::Connecting);

    const std::string publicKey = Base64(identity_.signPublic);
    JSONValue reg = JSONValue::MakeObject();
    reg.Set("public_key", JSONValue(publicKey));
    reg.Set("display_name", JSONValue(displayName_));
    reg.Set("signature", JSONValue(Sign(identity_, RegisterMessage(publicKey, displayName_))));

    auto fail = [this, finish](const RelayResponse& r) {
        UltraIMResult result = RelayError(r, UltraIMResultCode::AuthFailed);
        SetState(UltraIMConnectionState::Error, result.message);
        finish(result);
    };
    relay->Request("POST", "/v1/register", &reg, [this, relay, finish, fail](const RelayResponse& r) {
        if (!r.Ok()) return fail(r);
        JSONValue challenge = JSONValue::MakeObject();
        challenge.Set("user_id", JSONValue(identity_.userId));
        relay->Request("POST", "/v1/login/challenge", &challenge,
                       [this, relay, finish, fail](const RelayResponse& c) {
            if (!c.Ok()) return fail(c);
            const std::string nonce = c.body["nonce"].GetString();
            JSONValue login = JSONValue::MakeObject();
            login.Set("user_id", JSONValue(identity_.userId));
            login.Set("nonce", JSONValue(nonce));
            login.Set("signature", JSONValue(Sign(identity_, LoginMessage(nonce))));
            relay->Request("POST", "/v1/login", &login, [this, relay, finish, fail](const RelayResponse& l) {
                if (!l.Ok()) return fail(l);
                relay->SetToken(l.body["token"].GetString());
                // Where the relay settled: https:// for a bare host, the
                // target of a redirect - so the next start goes straight there.
                store_.SetSetting("relay_url", relay->GetBaseUrl());
                SetState(UltraIMConnectionState::Connected);
                finish(UltraIMResult::Ok());
                AfterLogin();
            });
        });
    });
}

void DirectProvider::AfterLogin() {
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay) return;
    relay->Request("GET", "/v1/contacts", nullptr, [this](const RelayResponse& r) {
        if (!r.Ok()) return;
        for (const auto& c : r.body["contacts"].GetElements()) {
            StoredContact contact;
            contact.userId = c["user_id"].GetString();
            contact.displayName = c["display_name"].GetString();
            contact.publicKey = c["public_key"].GetString();
            contact.presence = PresenceFrom(c["presence"].GetString());
            if (UserIdFor(FromBase64(contact.publicKey)) != contact.userId) continue;
            store_.UpsertContact(contact);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                presence_[contact.userId] = contact.presence;
            }
            EnsureDirectConversation(contact.userId);
        }
        Wake();
    });
    RefreshGroups();
    relay->Request("GET", "/v1/turn-credentials", nullptr, [this](const RelayResponse& r) {
        if (!r.Ok()) return;
        std::vector<UltraRtcIceServer> servers;
        for (const auto& s : r.body["ice_servers"].GetElements()) {
            UltraRtcIceServer server;
            server.url = s["url"].GetString();
            server.username = s["username"].GetString();
            server.credential = s["credential"].GetString();
            if (!server.url.empty()) servers.push_back(server);
        }
        links_.SetIceServers(servers);
        calls_.SetIceServers(servers);
    });
    relay->StartSocket();
}

void DirectProvider::SignOut() {
    calls_.EndAll("signed out");      // while the relay and the local network can still carry the hang-ups
    links_.CloseAll();
    lan_.Stop();
    std::shared_ptr<RelayClient> relay;
    {
        std::lock_guard<std::mutex> lock(relayMutex_);
        relay.swap(relay_);
    }
    if (relay) relay->Stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        attempts_.clear();
        presence_.clear();
        lanKeys_.clear();
        for (auto& pending : pendingAdds_) {
            if (pending.onResult) pending.onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Signed out"), {});
        }
        pendingAdds_.clear();
    }
    keyRing_.Clear();
    SetState(UltraIMConnectionState::Disconnected);
}

// ============================================================================
// CONTACTS, CONVERSATIONS, GROUPS
// ============================================================================

std::vector<uint8_t> DirectProvider::KeyOf(const std::string& userId) {
    StoredContact contact;
    if (store_.GetContact(userId, contact)) return FromBase64(contact.publicKey);
    // Someone on the local network who proved their key but is no contact.
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lanKeys_.find(userId);
    return it == lanKeys_.end() ? std::vector<uint8_t>() : it->second.first;
}

UltraIMPresence DirectProvider::EffectivePresence(const std::string& userId) {
    if (lan_.IsConnected(userId)) return UltraIMPresence::Online;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = presence_.find(userId);
    if (it != presence_.end()) return it->second;
    return Relay() ? UltraIMPresence::Unknown : UltraIMPresence::Offline;
}

void DirectProvider::EmitPresence(const std::string& userId) {
    UltraIMEvent event;
    event.type = UltraIMEventType::PresenceChange;
    event.providerId = providerId_;
    event.contactId = userId;
    event.conversationId = userId;
    event.presence = EffectivePresence(userId);
    Emit(event);
    if (event.presence == UltraIMPresence::Online) Wake();
}

// ============================================================================
// THE LOCAL NETWORK
// ============================================================================

bool DirectProvider::StartLan(std::string& error) {
    if (!lan_.Start(&identity_, displayName_, true, timings_.lanDiscoveryMs, error)) return false;
    std::set<std::string> wanted;
    for (const auto& c : store_.ListContacts()) wanted.insert(c.userId);
    lan_.SetWanted(wanted);
    return true;
}

void DirectProvider::OnLanPeerUp(const std::string& peerId, const std::vector<uint8_t>& key, const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lanKeys_[peerId] = {key, name};
    }
    // A contact's record learns its name if the relay never gave one.
    StoredContact contact;
    if (store_.GetContact(peerId, contact) && contact.displayName.empty() && !name.empty()) {
        contact.displayName = name;
        store_.UpsertContact(contact);
    }
    EmitPresence(peerId);
    UltraIMEvent event;
    event.type = UltraIMEventType::NearbyChange;
    event.providerId = providerId_;
    Emit(event);
}

void DirectProvider::OnLanFrame(const std::string& peerId, const JSONValue& frame) {
    if (frame["t"].GetString() != "signal") return;
    const std::string kind = frame["kind"].GetString();
    const std::string session = frame["session"].GetString();
    const std::string data = frame["d"].GetString();
    // Signed like the relay's signals: the connection proved who it is, the
    // signature proves this message.
    if (!Verify(KeyOf(peerId), SignalMessage(peerId, identity_.userId, session, kind, data), frame["s"].GetString()))
        return;
    if (CallManager::IsCallSession(session)) calls_.HandleSignal(peerId, kind, session, data);
    else links_.HandleSignal(peerId, kind, session, data);
}

UltraIMConversation DirectProvider::StoreLanContact(const std::string& userId) {
    StoredContact contact;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lanKeys_.find(userId);
        if (it == lanKeys_.end()) return {};
        contact.userId = userId;
        contact.publicKey = Base64(it->second.first);
        contact.displayName = it->second.second;
    }
    StoredContact existing;
    if (store_.GetContact(userId, existing) && !existing.displayName.empty()) contact.displayName = existing.displayName;
    contact.presence = UltraIMPresence::Online;
    store_.UpsertContact(contact);
    lan_.AddWanted(userId);
    return EnsureDirectConversation(userId);
}

void DirectProvider::CompletePendingAdds(int64_t nowMs) {
    std::vector<PendingAdd> done, expired;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = pendingAdds_.begin(); it != pendingAdds_.end();) {
            if (lanKeys_.count(it->userId)) {
                done.push_back(*it);
                it = pendingAdds_.erase(it);
            } else if (nowMs > it->deadlineMs) {
                expired.push_back(*it);
                it = pendingAdds_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& p : done) p.onResult(UltraIMResult::Ok(), StoreLanContact(p.userId));
    for (auto& p : expired)
        p.onResult(UltraIMResult::Error(UltraIMResultCode::Timeout, "That ChatCube did not answer on this network"), {});
}

void DirectProvider::FetchNearbyContacts(UltraIMContactsCallback onResult) {
    std::vector<UltraIMContact> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& id, const std::string& name) {
        StoredContact existing;
        if (id == identity_.userId || seen.count(id) || store_.GetContact(id, existing)) return;
        seen.insert(id);
        UltraIMContact contact;
        contact.contactId = id;
        contact.providerId = providerId_;
        contact.displayName = name.empty() ? id.substr(0, 8) : name;
        contact.presence = UltraIMPresence::Online;
        out.push_back(contact);
    };
    std::vector<std::pair<std::string, std::string>> proven;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : lanKeys_) proven.push_back({entry.first, entry.second.second});
    }
    for (const auto& p : proven) {
        if (lan_.IsConnected(p.first)) add(p.first, p.second);
    }
    for (const LanPeer& peer : lan_.Nearby()) add(peer.userId, peer.displayName);
    onResult(UltraIMResult::Ok(), out);
}

void DirectProvider::FetchUser(const std::string& userId, std::function<void(bool known)> onDone) {
    if (!KeyOf(userId).empty()) {
        onDone(true);
        return;
    }
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay) {
        onDone(false);
        return;
    }
    relay->Request("GET", "/v1/users/" + userId, nullptr, [this, userId, onDone](const RelayResponse& r) {
        if (!r.Ok()) return onDone(false);
        StoredContact contact;
        contact.userId = r.body["user_id"].GetString();
        contact.displayName = r.body["display_name"].GetString();
        contact.publicKey = r.body["public_key"].GetString();
        // The relay serves keys; the id proves them.
        if (contact.userId != userId || UserIdFor(FromBase64(contact.publicKey)) != userId) return onDone(false);
        StoredContact existing;
        if (store_.GetContact(userId, existing)) contact.presence = existing.presence;
        store_.UpsertContact(contact);
        onDone(true);
    });
}

UltraIMConversation DirectProvider::EnsureDirectConversation(const std::string& userId) {
    UltraIMConversation conversation;
    if (store_.GetConversation(userId, conversation)) return conversation;
    StoredContact contact;
    store_.GetContact(userId, contact);
    conversation.conversationId = userId;
    conversation.providerId = providerId_;
    conversation.title = contact.displayName.empty() ? userId.substr(0, 8) : contact.displayName;
    conversation.directContactId = userId;
    conversation.memberIds = {identity_.userId, userId};
    store_.UpsertConversation(conversation);
    store_.GetConversation(userId, conversation);
    conversation.presence = EffectivePresence(userId);
    UltraIMEvent event;
    event.type = UltraIMEventType::ConversationUpdate;
    event.providerId = providerId_;
    event.conversationId = userId;
    event.conversation = conversation;
    Emit(event);
    return conversation;
}

void DirectProvider::UpsertGroup(const JSONValue& group) {
    const std::string id = group["conversation"].GetString();
    if (!IsGroupId(id)) return;
    if (group["removed"].GetBoolean(false)) {
        store_.RemoveConversation(id);
        UltraIMEvent event;
        event.type = UltraIMEventType::ConversationRemove;
        event.providerId = providerId_;
        event.conversationId = id;
        Emit(event);
        return;
    }
    UltraIMConversation conversation;
    store_.GetConversation(id, conversation);
    conversation.conversationId = id;
    conversation.providerId = providerId_;
    conversation.title = group["title"].GetString();
    conversation.isGroup = true;
    conversation.memberIds.clear();
    for (const auto& m : group["members"].GetElements()) conversation.memberIds.push_back(m.GetString());
    store_.UpsertConversation(conversation);
    store_.GetConversation(id, conversation);
    for (const auto& member : conversation.memberIds) {
        if (member != identity_.userId) FetchUser(member, [](bool) {});
    }
    UltraIMEvent event;
    event.type = UltraIMEventType::ConversationUpdate;
    event.providerId = providerId_;
    event.conversationId = id;
    event.conversation = conversation;
    Emit(event);
}

void DirectProvider::RefreshGroups() {
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay) return;
    relay->Request("GET", "/v1/groups", nullptr, [this](const RelayResponse& r) {
        if (!r.Ok()) return;
        for (const auto& g : r.body["groups"].GetElements()) UpsertGroup(g);
        Wake();
    });
}

void DirectProvider::FetchConversations(UltraIMConversationsCallback onResult) {
    std::vector<UltraIMConversation> list = store_.ListConversations();
    for (auto& c : list) {
        if (!c.isGroup && !c.directContactId.empty()) c.presence = EffectivePresence(c.directContactId);
    }
    onResult(UltraIMResult::Ok(), list);
}

void DirectProvider::FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                                   UltraIMMessagesCallback onResult) {
    onResult(UltraIMResult::Ok(), store_.Messages(conversationId, beforeMessageId, limit));
}

void DirectProvider::FetchContacts(UltraIMContactsCallback onResult) {
    std::vector<UltraIMContact> out;
    for (const auto& c : store_.ListContacts()) {
        UltraIMContact contact;
        contact.contactId = c.userId;
        contact.providerId = providerId_;
        contact.displayName = c.displayName;
        contact.presence = EffectivePresence(c.userId);
        out.push_back(contact);
    }
    onResult(UltraIMResult::Ok(), out);
}

void DirectProvider::AddContact(const std::string& address, UltraIMConversationCallback onResult) {
    const std::string userId = NormalizeUserId(address);
    if (userId.empty()) {
        onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                      "A ChatCube id is 32 hexadecimal characters"), {});
        return;
    }
    if (state_ != UltraIMConnectionState::Connected) {
        onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in"), {});
        return;
    }
    std::shared_ptr<RelayClient> relay = Relay();
    // On the local network and proven: no server needed. (The relay, when
    // there is one, still learns the contact for presence.)
    bool proven = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        proven = lanKeys_.count(userId) > 0;
    }
    if (proven) {
        if (relay) relay->Request("PUT", "/v1/contacts/" + userId, nullptr, [](const RelayResponse&) {});
        onResult(UltraIMResult::Ok(), StoreLanContact(userId));
        return;
    }
    // Seen nearby but not connected yet: connect, and finish once its
    // handshake proves the key (CompletePendingAdds).
    if (lanOn_ && (!relay || !relay->IsSocketOpen())) {
        bool nearby = false;
        for (const LanPeer& peer : lan_.Nearby()) nearby = nearby || peer.userId == userId;
        if (!nearby && !relay) {
            onResult(UltraIMResult::Error(UltraIMResultCode::NotFound,
                                          "That ChatCube is not on this network (no relay server is set)"), {});
            return;
        }
        if (nearby) {
            lan_.AddWanted(userId);
            std::lock_guard<std::mutex> lock(mutex_);
            pendingAdds_.push_back({userId, onResult, NowMs() + timings_.lanAddTimeoutMs});
            return;
        }
    }
    relay->Request("PUT", "/v1/contacts/" + userId, nullptr, [this, userId, onResult](const RelayResponse& r) {
        if (!r.Ok()) return onResult(RelayError(r, UltraIMResultCode::Unknown), {});
        StoredContact contact;
        contact.userId = r.body["user_id"].GetString();
        contact.displayName = r.body["display_name"].GetString();
        contact.publicKey = r.body["public_key"].GetString();
        contact.presence = PresenceFrom(r.body["presence"].GetString());
        if (contact.userId != userId || UserIdFor(FromBase64(contact.publicKey)) != userId)
            return onResult(UltraIMResult::Error(UltraIMResultCode::PermissionDenied,
                                                 "The relay returned a key that does not match the id"), {});
        store_.UpsertContact(contact);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            presence_[userId] = contact.presence;
        }
        if (lanOn_) lan_.AddWanted(userId);
        onResult(UltraIMResult::Ok(), EnsureDirectConversation(userId));
    });
}

void DirectProvider::CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                                 UltraIMConversationCallback onResult) {
    std::shared_ptr<RelayClient> relay = Relay();
    if (state_ != UltraIMConnectionState::Connected) {
        onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in"), {});
        return;
    }
    if (!relay) {
        onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                      "Groups need a relay server - this ChatCube uses the local network only"), {});
        return;
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("title", JSONValue(title));
    JSONValue members = JSONValue::MakeArray();
    for (const auto& m : memberIds) members.Append(JSONValue(m));
    body.Set("members", members);
    relay->Request("POST", "/v1/groups", &body, [this, onResult](const RelayResponse& r) {
        if (!r.Ok()) return onResult(RelayError(r, UltraIMResultCode::Unknown), {});
        UpsertGroup(r.body);
        UltraIMConversation conversation;
        store_.GetConversation(r.body["conversation"].GetString(), conversation);
        onResult(UltraIMResult::Ok(), conversation);
    });
}

// ============================================================================
// INCOMING
// ============================================================================

void DirectProvider::FetchQueue() {
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay) return;
    relay->Request("GET", "/v1/messages?since=0", nullptr, [this](const RelayResponse& r) {
        if (!r.Ok()) return;
        for (const auto& m : r.body["messages"].GetElements())
            ProcessEnvelope(m["envelope"].GetString(), m["id"].GetInteger(0), false);
    });
}

void DirectProvider::OnFrame(const JSONValue& frame) {
    const std::string type = frame["type"].GetString();
    if (type == "message.new") {
        const JSONValue& m = frame["message"];
        ProcessEnvelope(m["envelope"].GetString(), m["id"].GetInteger(0), false);
    } else if (type == "presence") {
        const std::string from = frame["from"].GetString();
        const UltraIMPresence presence = PresenceFrom(frame["status"].GetString());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            presence_[from] = presence;
        }
        UltraIMEvent event;
        event.type = UltraIMEventType::PresenceChange;
        event.providerId = providerId_;
        event.contactId = from;
        event.conversationId = from;
        event.presence = presence;
        Emit(event);
        if (presence == UltraIMPresence::Online) Wake();
    } else if (type == "signal") {
        const std::string from = frame["from"].GetString();
        const std::string kind = frame["kind"].GetString();
        const std::string session = frame["session"].GetString();
        const std::string data = frame["payload"]["d"].GetString();
        const std::string signature = frame["payload"]["s"].GetString();
        FetchUser(from, [this, from, kind, session, data, signature](bool known) {
            if (!known) return;
            // Only the contact's own key may set up a link with us.
            if (!Verify(KeyOf(from), SignalMessage(from, identity_.userId, session, kind, data), signature)) return;
            if (CallManager::IsCallSession(session)) calls_.HandleSignal(from, kind, session, data);
            else links_.HandleSignal(from, kind, session, data);
        });
    } else if (type == "signal.undeliverable") {
        const std::string session = frame["session"].GetString();
        if (CallManager::IsCallSession(session)) calls_.HandleUndeliverable(frame["to"].GetString(), session);
    } else if (type == "typing") {
        const std::string from = frame["from"].GetString();
        const std::string conversation = frame["conversation"].GetString();
        UltraIMEvent event;
        event.type = UltraIMEventType::TypingChange;
        event.providerId = providerId_;
        event.contactId = from;
        event.conversationId = IsGroupId(conversation) ? conversation : from;
        event.isTyping = frame["typing"].GetBoolean(false);
        Emit(event);
    } else if (type == "group.update") {
        UpsertGroup(frame["group"]);
    }
}

void DirectProvider::OnLinkMessage(const std::string& peerId, const std::string& text) {
    UltraCanvas::JSONParseResult parsed;
    JSONValue frame = UltraCanvas::JSON::Parse(text, &parsed);
    if (!parsed.success) return;
    const std::string t = frame["t"].GetString();
    if (t == "env") {
        const JSONValue& env = frame["env"];
        if (env["from"].GetString() != peerId) return;      // a channel speaks only for its peer
        ProcessEnvelope(UltraCanvas::JSON::Serialize(env), 0, true);
        JSONValue ack = JSONValue::MakeObject();
        ack.Set("t", JSONValue("ack"));
        ack.Set("id", JSONValue(env["id"].GetString()));
        links_.Send(peerId, UltraCanvas::JSON::Serialize(ack));
    } else if (t == "ack") {
        const std::string id = frame["id"].GetString();
        store_.RemoveOutbox(id, peerId);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            attempts_.erase(AttemptKey(id, peerId));
        }
        MarkDelivered(peerId, id, UltraIMDeliveryStatus::Delivered);
    } else if (t == "typing") {
        UltraIMEvent event;
        event.type = UltraIMEventType::TypingChange;
        event.providerId = providerId_;
        event.contactId = peerId;
        event.conversationId = peerId;
        event.isTyping = frame["on"].GetBoolean(false);
        Emit(event);
    }
}

void DirectProvider::ProcessEnvelope(const std::string& envelopeJson, int64_t relayId, bool overLink) {
    auto acknowledge = [this, relayId]() {
        std::shared_ptr<RelayClient> relay = Relay();
        if (relayId <= 0 || !relay) return;
        JSONValue body = JSONValue::MakeObject();
        JSONValue ids = JSONValue::MakeArray();
        ids.Append(JSONValue(static_cast<int64_t>(relayId)));
        body.Set("ids", ids);
        relay->Request("POST", "/v1/messages/ack", &body, [](const RelayResponse&) {});
    };

    Envelope envelope;
    if (!Envelope::FromJson(envelopeJson, envelope) || envelope.to != identity_.userId) {
        acknowledge();          // not for us or not an envelope: drop it from the queue
        return;
    }
    const std::vector<uint8_t> key = KeyOf(envelope.from);
    if (key.empty()) {
        FetchUser(envelope.from, [this, envelopeJson, relayId, overLink, acknowledge](bool known) {
            if (known) ProcessEnvelope(envelopeJson, relayId, overLink);
            else acknowledge();
        });
        return;
    }
    std::string bodyText, error;
    if (!keyRing_.Open(identity_, key, envelope, bodyText, error)) {
        acknowledge();
        return;
    }
    UltraCanvas::JSONParseResult parsed;
    JSONValue body = UltraCanvas::JSON::Parse(bodyText, &parsed);
    if (parsed.success && body.IsObject()) HandleBody(envelope, body);
    acknowledge();
}

void DirectProvider::HandleBody(const Envelope& envelope, const JSONValue& body) {
    const std::string type = body["type"].GetString();
    if (type == "receipt") {
        const UltraIMDeliveryStatus status = body["status"].GetString() == "read"
                                                 ? UltraIMDeliveryStatus::Read
                                                 : UltraIMDeliveryStatus::Delivered;
        for (const auto& id : body["ids"].GetElements()) MarkDelivered(envelope.from, id.GetString(), status);
        return;
    }
    if (type != "text") return;

    const bool group = IsGroupId(envelope.conversation);
    UltraIMConversation conversation;
    if (group) {
        if (!store_.GetConversation(envelope.conversation, conversation)) {
            conversation.conversationId = envelope.conversation;
            conversation.providerId = providerId_;
            conversation.title = body["group_title"].GetString("Group");
            conversation.isGroup = true;
            store_.UpsertConversation(conversation);
            RefreshGroups();
        }
        if (!store_.GetConversation(envelope.conversation, conversation)) return;
        // A member list we know must contain the sender.
        if (!conversation.memberIds.empty() &&
            std::find(conversation.memberIds.begin(), conversation.memberIds.end(), envelope.from) ==
                conversation.memberIds.end())
            return;
    } else {
        conversation = EnsureDirectConversation(envelope.from);
    }

    StoredContact sender;
    store_.GetContact(envelope.from, sender);
    UltraIMMessage message;
    message.providerId = providerId_;
    message.conversationId = conversation.conversationId;
    message.clientMessageId = envelope.id;
    message.messageId = envelope.id;
    message.senderId = envelope.from;
    message.senderName = sender.displayName.empty() ? body["sender_name"].GetString() : sender.displayName;
    message.timestampMs = envelope.timestampMs;
    message.type = UltraIMMessageType::Text;
    message.text = body["text"].GetString();
    message.replyTo.messageId = body["reply_to"].GetString();
    message.deliveryStatus = UltraIMDeliveryStatus::Delivered;
    message.isOwn = false;
    if (!store_.AddMessage(message)) return;    // seen already (it came by two paths)

    UltraIMEvent received;
    received.type = UltraIMEventType::MessageReceive;
    received.providerId = providerId_;
    received.conversationId = conversation.conversationId;
    received.contactId = envelope.from;
    received.message = message;
    Emit(received);

    UltraIMEvent updated;
    updated.type = UltraIMEventType::ConversationUpdate;
    updated.providerId = providerId_;
    updated.conversationId = conversation.conversationId;
    store_.GetConversation(conversation.conversationId, updated.conversation);
    Emit(updated);

    if (!group) SendReceipt(envelope.from, {envelope.id}, "delivered");
}

// ============================================================================
// OUTGOING
// ============================================================================

UltraIMResult DirectProvider::SendText(const std::string& conversationId, const std::string& text,
                                       const UltraIMSendOptions& options) {
    if (!identity_.IsValid() || state_ == UltraIMConnectionState::Disconnected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in");
    UltraIMConversation conversation;
    if (!store_.GetConversation(conversationId, conversation))
        return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");

    std::vector<std::string> recipients;
    if (conversation.isGroup) {
        for (const auto& m : conversation.memberIds) {
            if (m != identity_.userId) recipients.push_back(m);
        }
        if (recipients.empty())
            return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "The group has no other members");
    } else {
        recipients.push_back(conversationId);
    }

    const int64_t now = NowMs();
    JSONValue body = JSONValue::MakeObject();
    body.Set("type", JSONValue("text"));
    body.Set("text", JSONValue(text));
    body.Set("sender_name", JSONValue(displayName_));
    if (!options.replyToMessageId.empty()) body.Set("reply_to", JSONValue(options.replyToMessageId));
    if (conversation.isGroup) body.Set("group_title", JSONValue(conversation.title));

    UltraIMMessage message;
    message.providerId = providerId_;
    message.conversationId = conversationId;
    message.clientMessageId = options.clientMessageId;
    message.messageId = options.clientMessageId;
    message.senderId = identity_.userId;
    message.senderName = displayName_;
    message.timestampMs = now;
    message.text = text;
    message.replyTo.messageId = options.replyToMessageId;
    message.deliveryStatus = UltraIMDeliveryStatus::Sending;
    message.isOwn = true;
    if (!store_.AddMessage(message))
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "This client message id was already sent");

    QueueBody(conversation.isGroup ? conversationId : kDirectConversation, recipients, options.clientMessageId,
              UltraCanvas::JSON::Serialize(body), now);

    UltraIMEvent updated;
    updated.type = UltraIMEventType::ConversationUpdate;
    updated.providerId = providerId_;
    updated.conversationId = conversationId;
    store_.GetConversation(conversationId, updated.conversation);
    Emit(updated);
    return UltraIMResult::Ok();
}

UltraIMResult DirectProvider::MarkRead(const std::string& conversationId, const std::string& upToMessageId) {
    (void)upToMessageId;
    UltraIMConversation conversation;
    if (!store_.GetConversation(conversationId, conversation))
        return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
    const std::vector<std::string> ids = store_.UnreadIncoming(conversationId);
    store_.ClearUnread(conversationId);
    if (!conversation.isGroup && !ids.empty()) SendReceipt(conversationId, ids, "read");
    UltraIMEvent updated;
    updated.type = UltraIMEventType::ConversationUpdate;
    updated.providerId = providerId_;
    updated.conversationId = conversationId;
    store_.GetConversation(conversationId, updated.conversation);
    Emit(updated);
    return UltraIMResult::Ok();
}

UltraIMResult DirectProvider::SetTyping(const std::string& conversationId, bool isTyping) {
    UltraIMConversation conversation;
    if (!store_.GetConversation(conversationId, conversation))
        return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
    if (!conversation.isGroup && links_.IsOpen(conversationId)) {
        JSONValue frame = JSONValue::MakeObject();
        frame.Set("t", JSONValue("typing"));
        frame.Set("on", JSONValue(isTyping));
        links_.Send(conversationId, UltraCanvas::JSON::Serialize(frame));
        return UltraIMResult::Ok();
    }
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay) return UltraIMResult::Ok();
    std::vector<std::string> targets;
    if (conversation.isGroup) {
        for (const auto& m : conversation.memberIds) {
            if (m != identity_.userId) targets.push_back(m);
        }
    } else {
        targets.push_back(conversationId);
    }
    for (const auto& to : targets) {
        JSONValue frame = JSONValue::MakeObject();
        frame.Set("type", JSONValue("typing"));
        frame.Set("to", JSONValue(to));
        frame.Set("conversation", JSONValue(conversation.isGroup ? conversationId : std::string(kDirectConversation)));
        frame.Set("typing", JSONValue(isTyping));
        relay->SendFrame(frame);
    }
    return UltraIMResult::Ok();
}

bool DirectProvider::CanCall(const std::string& conversationId, bool video) const {
    // 1:1, voice or video, with the relay's socket up (it carries the
    // signalling); group calls come later.
    if (conversationId.empty() || IsGroupId(conversationId)) return false;
    const UltraRtcCapabilities caps = UltraRtc_GetCapabilities();
    if (state_.load() != UltraIMConnectionState::Connected || !caps.audio || (video && !caps.video)) return false;
    if (lan_.IsConnected(conversationId)) return true;
    std::shared_ptr<RelayClient> relay = Relay();
    return relay && relay->IsSocketOpen();
}

UltraIMResult DirectProvider::StartCall(const std::string& conversationId, bool video, std::string& outCallId) {
    outCallId.clear();
    if (!CanCall(conversationId, video))
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Calls are not available in this conversation");
    return calls_.Start(conversationId, video, outCallId);
}

UltraIMResult DirectProvider::AcceptCall(const std::string& callId, bool video) {
    return calls_.Accept(callId, video);
}

UltraIMResult DirectProvider::SetCallCamera(const std::string& callId, bool on) {
    return calls_.SetCamera(callId, on);
}

UltraIMResult DirectProvider::SetCallVideoSinks(const std::string& callId, UltraIMVideoFrameCallback remote,
                                                UltraIMVideoFrameCallback local) {
    return calls_.SetVideoSinks(callId, std::move(remote), std::move(local));
}

UltraIMResult DirectProvider::EndCall(const std::string& callId) {
    return calls_.End(callId);
}

UltraIMResult DirectProvider::SetCallMuted(const std::string& callId, bool muted) {
    return calls_.SetMuted(callId, muted);
}

bool DirectProvider::IsLinkOpen(const std::string& userId) {
    return links_.IsOpen(userId);
}

void DirectProvider::QueueBody(const std::string& conversation, const std::vector<std::string>& recipients,
                               const std::string& clientId, const std::string& bodyJson, int64_t timestampMs) {
    for (const auto& recipient : recipients) {
        OutboxEntry entry;
        entry.clientId = clientId;
        entry.conversation = conversation;
        entry.recipient = recipient;
        entry.body = bodyJson;
        entry.timestampMs = timestampMs;
        store_.AddOutbox(entry);
    }
    Wake();
}

void DirectProvider::SendReceipt(const std::string& to, const std::vector<std::string>& ids, const char* status) {
    JSONValue body = JSONValue::MakeObject();
    body.Set("type", JSONValue("receipt"));
    body.Set("status", JSONValue(status));
    JSONValue list = JSONValue::MakeArray();
    for (const auto& id : ids) list.Append(JSONValue(id));
    body.Set("ids", list);
    QueueBody(kDirectConversation, {to}, "r-" + UltraIM_NewClientMessageId(), UltraCanvas::JSON::Serialize(body),
              NowMs());
}

void DirectProvider::MarkDelivered(const std::string& conversation, const std::string& clientId,
                                   UltraIMDeliveryStatus status) {
    if (!store_.UpdateStatus(conversation, clientId, status)) return;
    UltraIMEvent event;
    event.type = UltraIMEventType::MessageDelivered;
    event.providerId = providerId_;
    event.conversationId = conversation;
    event.message.providerId = providerId_;
    event.message.conversationId = conversation;
    event.message.clientMessageId = clientId;
    event.message.messageId = clientId;
    event.deliveryStatus = status;
    event.message.deliveryStatus = status;
    Emit(event);
}

void DirectProvider::PostToRelay(const std::vector<OutboxEntry>& copies) {
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay || copies.empty()) return;
    JSONValue list = JSONValue::MakeArray();
    std::vector<OutboxEntry> sealed;
    for (const auto& copy : copies) {
        Envelope envelope;
        std::string error;
        if (!keyRing_.Seal(identity_, KeyOf(copy.recipient), copy.recipient, copy.conversation, copy.clientId,
                           copy.timestampMs, copy.body, envelope, error))
            continue;
        JSONValue m = JSONValue::MakeObject();
        m.Set("to", JSONValue(copy.recipient));
        m.Set("message_id", JSONValue(copy.clientId));
        m.Set("conversation", JSONValue(copy.conversation));
        m.Set("envelope", JSONValue(envelope.ToJson()));
        list.Append(m);
        sealed.push_back(copy);
    }
    if (sealed.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& copy : sealed) attempts_[AttemptKey(copy.clientId, copy.recipient)].relayInFlight = true;
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("messages", list);
    relay->Request("POST", "/v1/messages", &body, [this, sealed](const RelayResponse& r) {
        const bool retry = r.status == 0 || r.status >= 500 || r.status == 401 || r.status == 429;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& copy : sealed) {
                if (r.Ok() || !retry) attempts_.erase(AttemptKey(copy.clientId, copy.recipient));
                else attempts_[AttemptKey(copy.clientId, copy.recipient)].relayInFlight = false;
            }
        }
        if (!r.Ok() && retry) return;      // stays in the outbox for the next pass
        for (const auto& copy : sealed) store_.RemoveOutbox(copy.clientId, copy.recipient);
        const OutboxEntry& first = sealed.front();
        const std::string local = IsGroupId(first.conversation) ? first.conversation : first.recipient;
        if (r.Ok()) {
            sentOverRelay_ += static_cast<int>(sealed.size());
            MarkDelivered(local, first.clientId, UltraIMDeliveryStatus::Sent);
        } else {
            MarkDelivered(local, first.clientId, UltraIMDeliveryStatus::Failed);
        }
    });
}

std::shared_ptr<RelayClient> DirectProvider::Relay() const {
    std::lock_guard<std::mutex> lock(relayMutex_);
    return relay_;
}

void DirectProvider::Pump() {
    std::shared_ptr<RelayClient> relay = Relay();
    if (!identity_.IsValid() || (!relay && !lan_.IsRunning())) return;
    const int64_t now = NowMs();
    links_.ExpireStale(now, timings_.linkTimeoutMs);
    calls_.ExpireStale(now);
    CompletePendingAdds(now);

    std::map<std::string, std::vector<OutboxEntry>> groupCopies;   // client id -> copies
    for (const auto& entry : store_.Outbox()) {
        if (KeyOf(entry.recipient).empty()) {
            FetchUser(entry.recipient, [this](bool known) { if (known) Wake(); });
            continue;
        }
        Attempt attempt;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            attempt = attempts_[AttemptKey(entry.clientId, entry.recipient)];
        }
        if (attempt.relayInFlight) continue;

        if (IsGroupId(entry.conversation)) {
            groupCopies[entry.clientId].push_back(entry);
            continue;
        }

        if (links_.IsOpen(entry.recipient)) {
            if (attempt.linkSentMs == 0 || now - attempt.linkSentMs > timings_.linkAckTimeoutMs) {
                Envelope envelope;
                std::string error;
                if (keyRing_.Seal(identity_, KeyOf(entry.recipient), entry.recipient, entry.conversation,
                                  entry.clientId, entry.timestampMs, entry.body, envelope, error)) {
                    UltraCanvas::JSONParseResult parsed;
                    JSONValue frame = JSONValue::MakeObject();
                    frame.Set("t", JSONValue("env"));
                    frame.Set("env", UltraCanvas::JSON::Parse(envelope.ToJson(), &parsed));
                    if (links_.Send(entry.recipient, UltraCanvas::JSON::Serialize(frame))) {
                        ++sentOverLink_;
                        std::lock_guard<std::mutex> lock(mutex_);
                        attempts_[AttemptKey(entry.clientId, entry.recipient)].linkSentMs = now;
                    }
                }
            }
            continue;
        }

        const bool online = EffectivePresence(entry.recipient) == UltraIMPresence::Online;
        if (online) links_.Ensure(entry.recipient);
        // Without a relay a message waits in the outbox until the link is up.
        if (relay && (!online || now - entry.timestampMs > timings_.relayFallbackMs)) PostToRelay({entry});
    }
    if (relay) {
        for (const auto& group : groupCopies) PostToRelay(group.second);
    }
}

void DirectProvider::Wake() {
    std::lock_guard<std::mutex> lock(mutex_);
    woken_ = true;
    wake_.notify_all();
}

void DirectProvider::WorkerLoop() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(timings_.tickMs), [this]() { return stopping_ || woken_; });
            if (stopping_) return;
            woken_ = false;
        }
        Pump();
    }
}

} // namespace UltraIMDirect
