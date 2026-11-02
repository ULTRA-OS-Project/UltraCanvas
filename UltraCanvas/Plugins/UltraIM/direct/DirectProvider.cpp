// Plugins/UltraIM/direct/DirectProvider.cpp
// ChatCube Direct - the ULTRA OS native messenger as an UltraIM provider
// Version: 0.1.5 - the e-mail and phone confirmed by the relay's codes; account recovery
// Version: 0.1.4 - the account's profile (name, e-mail, phone); a password and/or an authenticator code to sign in
// Version: 0.1.3 - a file that is a picture (an SVG drawing) goes with a preview picture, as a photo does
// Version: 0.1.2 - pictures, videos and files sent and received in parts; DownloadAttachment
// Version: 0.1.1 - FetchSharedCredentials: the Telegram application's keys from the relay
// Version: 0.1.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#include "DirectProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/OtpAuthUri.h"
#include "UltraCrypt/UltraOtp.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraRTC/UltraRTCCore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

// ---- the account's sign-in -------------------------------------------------------
constexpr size_t kMinPasswordLength = 8;
constexpr int    kFreeCodeAttempts = 5;         // wrong codes before the first pause
constexpr int64_t kCodePauseSeconds = 30;       // doubled with each further wrong one
constexpr int64_t kMaxCodePauseSeconds = 15 * 60;

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

long long SettingNumber(const std::string& text) {
    return text.empty() ? 0 : std::strtoll(text.c_str(), nullptr, 10);
}

std::string Trimmed(const std::string& text) {
    size_t first = 0, last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

// The same rules as the relay's (chatcube_relay/app.py): plausible, not
// proven - an address is proven by the mail that reaches it.
bool PlausibleEmail(const std::string& email) {
    const size_t at = email.find('@');
    if (at == std::string::npos || at == 0 || email.find('@', at + 1) != std::string::npos) return false;
    const size_t dot = email.find('.', at + 2);
    if (dot == std::string::npos || dot + 1 >= email.size()) return false;
    for (const char c : email)
        if (std::isspace(static_cast<unsigned char>(c))) return false;
    return email.size() <= 254;
}

// International form: "+" and 7 to 15 digits, spaces, dashes and brackets
// as people write them.
bool PlausiblePhone(const std::string& phone) {
    if (phone.empty() || phone[0] != '+' || phone.size() > 32) return false;
    int digits = 0;
    for (size_t i = 1; i < phone.size(); ++i) {
        const char c = phone[i];
        if (std::isdigit(static_cast<unsigned char>(c))) ++digits;
        else if (c != ' ' && c != '-' && c != '(' && c != ')') return false;
    }
    return digits >= 7 && digits <= 15;
}

void Wipe(std::string& text) {
    if (!text.empty()) UltraCrypt_SecureZero(text.data(), text.size());
    text.clear();
}

std::string SecretToText(const UltraCryptSecureBuffer& secret) {
    const std::vector<uint8_t> bytes(secret.Data(), secret.Data() + secret.GetSize());
    std::string text = Base64(bytes);
    std::vector<uint8_t> copy = bytes;
    if (!copy.empty()) UltraCrypt_SecureZero(copy.data(), copy.size());
    return text;
}

UltraCryptSecureBuffer SecretFromText(const std::string& text) {
    std::vector<uint8_t> bytes = FromBase64(text);
    UltraCryptSecureBuffer secret(bytes.data(), bytes.size());
    if (!bytes.empty()) UltraCrypt_SecureZero(bytes.data(), bytes.size());
    return secret;
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
           F::CreateGroup | F::VoiceCalls | F::VideoCalls | F::ImageMessages | F::FileTransfer |
           F::AccountProfile | F::SignInProtection | F::AccountRecovery;
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
    if (recoveryRelay_) {
        recoveryRelay_->Stop();
        recoveryRelay_.reset();
    }
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
// Behind a password it is opened with the password it was opened with at
// sign-in (UseWithPassphrase), which the provider keeps while it runs - as it
// keeps the identity's key itself.
bool DirectProvider::WithVault(const std::function<void(UltraVault::DeviceKeyVault& vault)>& use) {
    std::string password;
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        password = vaultPassword_;
    }
    const bool ok = vault_.UseWithPassphrase(password, use) == UltraVault::UnlockStatus::Ok;
    Wipe(password);
    return ok;
}

DirectProvider::VaultOpen DirectProvider::OpenVault(const std::string& password, std::string& error) {
    std::string secret, totp, profile;
    auto read = [&](UltraVault::DeviceKeyVault& v) {
        v.Retrieve("identity", secret);
        v.Retrieve("totp", totp);
        v.Retrieve("profile", profile);
    };
    bool behindPassword = vault_.NeedsPassphrase();
    if (behindPassword && password.empty()) return VaultOpen::NeedsPassword;
    UltraVault::UnlockStatus status = vault_.UseWithPassphrase(behindPassword ? password : std::string(), read);
    if (!behindPassword && status == UltraVault::UnlockStatus::WrongPassphrase) {
        // A device key left by a change to a password that did not finish:
        // it no longer opens the file, the password does.
        if (password.empty()) return VaultOpen::NeedsPassword;
        status = vault_.UseWithPassphrase(password, read);
        if (status == UltraVault::UnlockStatus::Ok) {
            vault_.ForgetDeviceKey();
            behindPassword = true;
        }
    }
    if (status == UltraVault::UnlockStatus::WrongPassphrase) return VaultOpen::WrongPassword;
    if (status != UltraVault::UnlockStatus::Ok) {
        error = "The key vault cannot be opened: " + UltraVault::DeviceKeyVault::DescribeUnlockStatus(status);
        return VaultOpen::Failed;
    }
    if (secret.empty()) {
        error.clear();
        return VaultOpen::Empty;
    }
    UltraCryptSecureBuffer buffer = SecretFromText(secret);
    Wipe(secret);
    if (!IdentityFromSecret(buffer, identity_, error)) return VaultOpen::Failed;
    std::lock_guard<std::mutex> lock(accountMutex_);
    vaultPassword_ = behindPassword ? password : std::string();
    totpSecret_ = totp.empty() ? UltraCryptSecureBuffer() : SecretFromText(totp);
    Wipe(totp);
    codeAccepted_ = totpSecret_.IsEmpty();
    if (!profile.empty()) {
        UltraCanvas::JSONParseResult parsed;
        const JSONValue json = UltraCanvas::JSON::Parse(profile, &parsed);
        if (parsed.success) {
            email_ = json["email"].GetString();
            phone_ = json["phone"].GetString();
        }
    }
    return VaultOpen::Opened;
}

bool DirectProvider::SaveIdentity() {
    const std::string secret = SecretToText(identity_.signSecret);
    bool stored = false;
    WithVault([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("identity", secret); });
    std::string wipe = secret;
    Wipe(wipe);
    return stored;
}

bool DirectProvider::SaveProfile() {
    JSONValue json = JSONValue::MakeObject();
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        json.Set("email", JSONValue(email_));
        json.Set("phone", JSONValue(phone_));
    }
    const std::string text = UltraCanvas::JSON::Serialize(json);
    bool stored = false;
    WithVault([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("profile", text); });
    return stored;
}

bool DirectProvider::HasStoredSession() const {
    auto* self = const_cast<DirectProvider*>(this);
    // An identity is a session: with a relay remembered, or for the local
    // network alone.
    if (!storeOpen_) return false;
    if (self->store_.GetSetting("relay_url").empty() && self->store_.GetSetting("local_network") != "1") return false;
    // Behind a password: the identity is there, SignIn asks for the password.
    if (vault_.NeedsPassphrase()) return true;
    bool has = false;
    self->vault_.Use([&](UltraVault::DeviceKeyVault& v) { has = v.Has("identity"); });
    return has;
}

std::string DirectProvider::GetAuthHint() const {
    std::lock_guard<std::mutex> lock(accountMutex_);
    return authHint_;
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
    // A typed password is stretched (Argon2id) before the vault opens: that
    // takes a moment, so it is done on the worker thread.
    if (!identity_.IsValid() && !credentials.password.empty()) {
        Enqueue([this, credentials, onResult]() { SignInNow(credentials, onResult); });
        return;
    }
    SignInNow(credentials, onResult);
}

void DirectProvider::SignInNow(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    if (state_ == UltraIMConnectionState::Connected || state_ == UltraIMConnectionState::Connecting) {
        finish(UltraIMResult::Ok());
        return;
    }
    // A step the user is asked for: SignIn again with it.
    auto ask = [this, finish](UltraIMAuthStep step, UltraIMResultCode code, const std::string& text) {
        {
            std::lock_guard<std::mutex> lock(accountMutex_);
            authHint_ = text;
        }
        authStep_ = step;
        UltraIMEvent event;
        event.type = UltraIMEventType::AuthStepChange;
        event.providerId = providerId_;
        event.authStep = step;
        event.errorText = text;
        Emit(event);
        finish(UltraIMResult::Error(code, text));
    };
    // No relay at all: the local network only.
    const std::string relayUrl = credentials.server.empty() ? store_.GetSetting("relay_url") : credentials.server;
    if (credentials.localNetwork) store_.SetSetting("local_network", "1");
    lanOn_ = relayUrl.empty() || store_.GetSetting("local_network") == "1";
    if (relayUrl.empty()) store_.SetSetting("local_network", "1");

    std::string error;
    if (!identity_.IsValid()) {
        switch (OpenVault(credentials.password, error)) {
            case VaultOpen::Opened:
                break;
            case VaultOpen::NeedsPassword:
                ask(UltraIMAuthStep::Password, UltraIMResultCode::NeedsInput, "Type your ChatCube password");
                return;
            case VaultOpen::WrongPassword:
                ask(UltraIMAuthStep::Password, UltraIMResultCode::AuthFailed,
                    "That is not your ChatCube password - type it again");
                return;
            case VaultOpen::Failed:
                finish(UltraIMResult::Error(UltraIMResultCode::PluginError, error));
                return;
            case VaultOpen::Empty: {
                if (!credentials.createAccount) {
                    finish(UltraIMResult::Error(UltraIMResultCode::NotAuthenticated,
                                                "This device has no ChatCube identity yet - create one"));
                    return;
                }
                const std::string email = Trimmed(credentials.email);
                const std::string phone = Trimmed(credentials.phoneNumber);
                if (!email.empty() && !PlausibleEmail(email)) {
                    finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                                "That does not look like an e-mail address"));
                    return;
                }
                if (!phone.empty() && !PlausiblePhone(phone)) {
                    finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                                "Type the phone number with + and the country code (+49 151 ...)"));
                    return;
                }
                if (!CreateIdentity(identity_, error) || !SaveIdentity()) {
                    identity_ = Identity();
                    finish(UltraIMResult::Error(UltraIMResultCode::PluginError,
                                                error.empty() ? "The new identity could not be stored" : error));
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(accountMutex_);
                    email_ = email;
                    phone_ = phone;
                    codeAccepted_ = true;
                }
                if (!email.empty() || !phone.empty()) SaveProfile();
                store_.SetSetting("signin_code", "0");
                break;
            }
        }
    }
    // The authenticator's code, when one is asked for: good once.
    bool needCode = false;
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        needCode = !codeAccepted_ && !totpSecret_.IsEmpty();
    }
    if (needCode) {
        if (Trimmed(credentials.otpCode).empty()) {
            ask(UltraIMAuthStep::Code, UltraIMResultCode::NeedsInput,
                "Type the code your authenticator app (UltraAuthenticator) shows for ChatCube");
            return;
        }
        uint64_t step = 0;
        bool right = false;
        {
            std::lock_guard<std::mutex> lock(accountMutex_);
            right = CheckCode(totpSecret_, credentials.otpCode,
                              static_cast<uint64_t>(SettingNumber(store_.GetSetting("totp_last_step"))), step, error);
            if (right) codeAccepted_ = true;
        }
        if (!right) {
            ask(UltraIMAuthStep::Code, UltraIMResultCode::AuthFailed, error);
            return;
        }
        store_.SetSetting("totp_last_step", std::to_string(step));
    }
    if (authStep_ != UltraIMAuthStep::Idle) {
        {
            std::lock_guard<std::mutex> lock(accountMutex_);
            authHint_.clear();
        }
        authStep_ = UltraIMAuthStep::Idle;
        UltraIMEvent event;
        event.type = UltraIMEventType::AuthStepChange;
        event.providerId = providerId_;
        event.authStep = UltraIMAuthStep::Idle;
        Emit(event);
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
    // What the owner gave to recover the account with (the relay keeps it
    // for them alone): sent at every sign-in, so a profile made while the
    // relay was away, or before it was set, gets there.
    bool hasRecovery = false;
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        hasRecovery = !email_.empty() || !phone_.empty();
    }
    if (hasRecovery) SendProfileToRelay(nullptr);
    relay->StartSocket();
}

// ============================================================================
// THE ACCOUNT: PROFILE AND SIGN-IN PROTECTION
// ============================================================================

void DirectProvider::Enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    Wake();
}

void DirectProvider::SendProfileToRelay(std::function<void(const UltraIMResult&)> onDone) {
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay || !HasRelaySession()) {
        if (onDone) onDone(UltraIMResult::Ok());     // the next sign-in sends it
        return;
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("display_name", JSONValue(displayName_));
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        body.Set("email", JSONValue(email_));
        body.Set("phone", JSONValue(phone_));
    }
    relay->Request("PUT", "/v1/profile", &body, [onDone](const RelayResponse& r) {
        if (!onDone) return;
        onDone(r.Ok() ? UltraIMResult::Ok() : RelayError(r, UltraIMResultCode::InvalidArgument));
    });
}

void DirectProvider::FetchAccountProfile(UltraIMAccountProfileCallback onResult) {
    if (!onResult) return;
    if (!identity_.IsValid()) {
        onResult(UltraIMResult::Error(UltraIMResultCode::NotAuthenticated, "Not signed in"), UltraIMAccountProfile());
        return;
    }
    UltraIMAccountProfile profile;
    profile.displayName = displayName_;
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        profile.email = email_;
        profile.phoneNumber = phone_;
    }
    // What only the relay knows: whether the codes came back, and recovery.
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay || !HasRelaySession()) {
        onResult(UltraIMResult::Ok(), profile);
        return;
    }
    relay->Request("GET", "/v1/profile", nullptr, [onResult, profile](const RelayResponse& r) mutable {
        if (r.Ok()) {
            profile.emailVerified = r.body["email_verified"].GetBoolean();
            profile.phoneVerified = r.body["phone_verified"].GetBoolean();
            profile.recoveryAvailable = r.body["recovery_available"].GetBoolean();
            profile.recoveryEnabled = r.body["recovery_enabled"].GetBoolean();
        }
        onResult(UltraIMResult::Ok(), profile);
    });
}

// ============================================================================
// CONFIRMING THE E-MAIL AND PHONE; ACCOUNT RECOVERY
// ============================================================================

namespace {
const char* ChannelName(UltraIMContactChannel channel) {
    return channel == UltraIMContactChannel::Email ? "email" : "phone";
}
} // namespace

void DirectProvider::VerifyContact(UltraIMContactChannel channel, const std::string& code,
                                   UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay || !HasRelaySession()) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotConnected, "The relay is not connected"));
        return;
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("channel", JSONValue(ChannelName(channel)));
    body.Set("code", JSONValue(Trimmed(code)));
    relay->Request("POST", "/v1/profile/verify", &body, [finish](const RelayResponse& r) {
        finish(r.Ok() ? UltraIMResult::Ok() : RelayError(r, UltraIMResultCode::AuthFailed));
    });
}

void DirectProvider::ResendContactCode(UltraIMContactChannel channel, UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay || !HasRelaySession()) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotConnected, "The relay is not connected"));
        return;
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("channel", JSONValue(ChannelName(channel)));
    relay->Request("POST", "/v1/profile/verify/resend", &body, [finish](const RelayResponse& r) {
        finish(r.Ok() ? UltraIMResult::Ok() : RelayError(r, UltraIMResultCode::InvalidArgument));
    });
}

void DirectProvider::SetRecoveryEnabled(bool enabled, UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    std::shared_ptr<RelayClient> relay = Relay();
    if (!identity_.IsValid() || !relay || !HasRelaySession()) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotConnected, "The relay is not connected"));
        return;
    }
    if (!enabled) {
        relay->Request("DELETE", "/v1/recovery/escrow", nullptr, [finish](const RelayResponse& r) {
            finish(r.Ok() ? UltraIMResult::Ok() : RelayError(r, UltraIMResultCode::InvalidArgument));
        });
        return;
    }
    // The relay keeps this copy encrypted with its own recovery key - and can
    // read it: that is what turning recovery on means (README).
    std::string secret = SecretToText(identity_.signSecret);
    JSONValue body = JSONValue::MakeObject();
    body.Set("secret_key", JSONValue(secret));
    Wipe(secret);
    relay->Request("PUT", "/v1/recovery/escrow", &body, [finish](const RelayResponse& r) {
        finish(r.Ok() ? UltraIMResult::Ok() : RelayError(r, UltraIMResultCode::InvalidArgument));
    });
}

// Signed in to the relay: a session token, whether or not the socket is up
// right now (it reconnects by itself; REST calls do not need it).
bool DirectProvider::HasRelaySession() const {
    const UltraIMConnectionState state = state_.load();
    return state == UltraIMConnectionState::Connected || state == UltraIMConnectionState::Reconnecting;
}

bool DirectProvider::HasIdentityHere() {
    if (identity_.IsValid() || vault_.NeedsPassphrase()) return true;
    bool has = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { has = v.Has("identity"); });
    return has;
}

void DirectProvider::StartRecovery(const std::string& server, const std::string& address,
                                   UltraIMRecoveryChallengeCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r, const UltraIMRecoveryChallenge& c) {
        if (onResult) onResult(r, c);
    };
    if (HasIdentityHere()) {
        finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "This computer has a ChatCube ID already - recovery puts one on a computer "
                                    "without one"), {});
        return;
    }
    const std::string url = Trimmed(server).empty() ? store_.GetSetting("relay_url") : Trimmed(server);
    if (url.empty()) {
        finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "Recovery goes through your relay - type its address"), {});
        return;
    }
    if (Trimmed(address).empty()) {
        finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "Type the e-mail address or the phone number of your ID"), {});
        return;
    }
    // A client of its own: there is no session yet. The one before is stopped
    // here, on the caller's thread, never inside its own callback.
    if (recoveryRelay_) recoveryRelay_->Stop();
    recoveryRelay_ = std::make_shared<RelayClient>();
    recoveryRelay_->SetBaseUrl(url);
    JSONValue body = JSONValue::MakeObject();
    body.Set("address", JSONValue(Trimmed(address)));
    recoveryRelay_->Request("POST", "/v1/recovery/start", &body, [finish](const RelayResponse& r) {
        if (!r.Ok()) {
            if (r.status == 503)
                return finish(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                                   "This relay offers no account recovery"), {});
            return finish(RelayError(r, UltraIMResultCode::InvalidArgument), {});
        }
        UltraIMRecoveryChallenge challenge;
        challenge.recoveryId = r.body["recovery_id"].GetString();
        challenge.expiresInSeconds = static_cast<int>(r.body["expires_in"].GetInteger());
        finish(UltraIMResult::Ok(), challenge);
    });
}

void DirectProvider::FinishRecovery(const UltraIMRecoveryAnswer& answer, UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    if (HasIdentityHere()) {
        finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "This computer has a ChatCube ID already"));
        return;
    }
    const std::string url = Trimmed(answer.server).empty() ? store_.GetSetting("relay_url") : Trimmed(answer.server);
    // The client StartRecovery made (it has followed any redirect), else one now.
    if (!recoveryRelay_) {
        if (url.empty()) {
            finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Start the recovery first"));
            return;
        }
        recoveryRelay_ = std::make_shared<RelayClient>();
        recoveryRelay_->SetBaseUrl(url);
    }
    RelayClient* relay = recoveryRelay_.get();      // its callbacks run before Stop() returns
    JSONValue body = JSONValue::MakeObject();
    body.Set("recovery_id", JSONValue(answer.recoveryId));
    body.Set("email_code", JSONValue(Trimmed(answer.emailCode)));
    body.Set("phone_code", JSONValue(Trimmed(answer.phoneCode)));
    relay->Request("POST", "/v1/recovery/finish", &body, [this, relay, finish](const RelayResponse& r) {
        if (!r.Ok()) {
            if (r.status == 401)
                return finish(UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                                   "Those are not the codes, or they have expired - start again"));
            return finish(RelayError(r, UltraIMResultCode::AuthFailed));
        }
        std::string secret = r.body["secret_key"].GetString();
        UltraCryptSecureBuffer buffer = SecretFromText(secret);
        Wipe(secret);
        Identity recovered;
        std::string error;
        if (buffer.GetSize() != 64 || !IdentityFromSecret(buffer, recovered, error) ||
            recovered.userId != r.body["user_id"].GetString()) {
            return finish(UltraIMResult::Error(UltraIMResultCode::PluginError,
                                               "The relay gave back a key that is not this ID's"));
        }
        if (HasIdentityHere())
            return finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                               "This computer has a ChatCube ID already"));
        // Stored as if it had been made here: the device key opens the vault,
        // no password and no code yet - those are this computer's to choose.
        identity_ = std::move(recovered);
        if (!SaveIdentity()) {
            identity_ = Identity();
            return finish(UltraIMResult::Error(UltraIMResultCode::PluginError, "The ID could not be stored"));
        }
        {
            std::lock_guard<std::mutex> lock(accountMutex_);
            email_ = r.body["email"].GetString();
            phone_ = r.body["phone"].GetString();
            vaultPassword_.clear();
            codeAccepted_ = true;
        }
        SaveProfile();
        displayName_ = r.body["display_name"].GetString();
        store_.SetSetting("display_name", displayName_);
        store_.SetSetting("relay_url", relay->GetBaseUrl());
        store_.SetSetting("signin_code", "0");
        store_.SetSetting("totp_last_step", "0");
        finish(UltraIMResult::Ok());
    });
}

void DirectProvider::UpdateAccountProfile(const UltraIMAccountProfile& profile, UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    if (!identity_.IsValid()) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotAuthenticated, "Not signed in"));
        return;
    }
    const std::string name = Trimmed(profile.displayName);
    const std::string email = Trimmed(profile.email);
    const std::string phone = Trimmed(profile.phoneNumber);
    if (name.empty() || name.size() > 64) {
        finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "The name must be 1 to 64 characters"));
        return;
    }
    if (!email.empty() && !PlausibleEmail(email)) {
        finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "That does not look like an e-mail address"));
        return;
    }
    if (!phone.empty() && !PlausiblePhone(phone)) {
        finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "Type the phone number with + and the country code (+49 151 ...)"));
        return;
    }
    displayName_ = name;
    store_.SetSetting("display_name", name);
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        email_ = email;
        phone_ = phone;
    }
    if (!SaveProfile()) {
        finish(UltraIMResult::Error(UltraIMResultCode::PluginError, "The profile could not be stored in the key vault"));
        return;
    }
    SendProfileToRelay(finish);
}

UltraIMSignInProtection DirectProvider::GetSignInProtection() const {
    const bool password = vault_.NeedsPassphrase();
    bool code = false;
    if (identity_.IsValid()) {
        std::lock_guard<std::mutex> lock(accountMutex_);
        code = !totpSecret_.IsEmpty();
    } else {
        // Before the vault is open: what the last change said (the vault
        // itself decides - a code is asked for whenever it holds a secret).
        code = const_cast<DirectProvider*>(this)->store_.GetSetting("signin_code") == "1";
    }
    if (password && code) return UltraIMSignInProtection::PasswordAndAuthenticatorCode;
    if (password) return UltraIMSignInProtection::Password;
    if (code) return UltraIMSignInProtection::AuthenticatorCode;
    return UltraIMSignInProtection::DeviceKey;
}

UltraIMResult DirectProvider::StartAuthenticatorSetup(std::string& outOtpAuthUri) {
    outOtpAuthUri.clear();
    if (!identity_.IsValid())
        return UltraIMResult::Error(UltraIMResultCode::NotAuthenticated, "Not signed in");
    namespace Otp = UltraCanvas::Otp;
    UltraCryptSecureBuffer secret;
    const Otp::Result made = Otp::NewTotpSecret(secret);
    if (!made) return UltraIMResult::Error(UltraIMResultCode::PluginError, "No authenticator secret: " + made.message);
    Otp::Parameters params;                 // TOTP, SHA-1, 6 digits, 30 s: what every authenticator reads
    params.issuer = "ChatCube";
    // The name the authenticator lists it under: the person's, and the
    // start of the ID so two identities on one phone differ.
    std::string account;
    for (const char c : displayName_)
        if (c != ':' && static_cast<unsigned char>(c) >= 0x20) account += c;
    account = Trimmed(account);
    account += (account.empty() ? "" : " ") + ("(" + identity_.userId.substr(0, 8) + ")");
    params.accountName = account;
    const Otp::Result built = Otp::BuildOtpAuthUri(params, secret, outOtpAuthUri);
    if (!built) return UltraIMResult::Error(UltraIMResultCode::PluginError, built.message);
    std::lock_guard<std::mutex> lock(accountMutex_);
    pendingTotp_ = std::move(secret);
    return UltraIMResult::Ok();
}

bool DirectProvider::CheckCode(const UltraCryptSecureBuffer& secret, const std::string& code, uint64_t lastStep,
                               uint64_t& outStep, std::string& error) {
    const int64_t now = NowSeconds();
    const int64_t pausedUntil = SettingNumber(store_.GetSetting("code_paused_until"));
    if (now < pausedUntil) {
        error = "Too many wrong codes - try again in " + std::to_string(pausedUntil - now) + " seconds";
        return false;
    }
    UltraCanvas::Otp::Parameters params;
    const UltraCanvas::Otp::Result r = UltraCanvas::Otp::VerifyTotp(secret, params, now, code, 1, lastStep, outStep);
    if (r) {
        store_.SetSetting("code_failures", "0");
        return true;
    }
    const long long failures = SettingNumber(store_.GetSetting("code_failures")) + 1;
    store_.SetSetting("code_failures", std::to_string(failures));
    if (failures >= kFreeCodeAttempts) {
        const long long extra = std::min<long long>(failures - kFreeCodeAttempts, 10);
        const int64_t pause = std::min<int64_t>(kCodePauseSeconds << extra, kMaxCodePauseSeconds);
        store_.SetSetting("code_paused_until", std::to_string(now + pause));
        error = "That is not the code - try again in " + std::to_string(pause) + " seconds";
    } else {
        error = "That is not the code your authenticator shows for ChatCube - type the one it shows now";
    }
    return false;
}

void DirectProvider::SetSignInProtection(const UltraIMProtectionChange& change, UltraIMResultCallback onResult) {
    if (!identity_.IsValid()) {
        if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotAuthenticated, "Not signed in"));
        return;
    }
    Enqueue([this, change, onResult]() { ChangeProtection(change, onResult); });
}

void DirectProvider::ChangeProtection(const UltraIMProtectionChange& change, UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    auto refuse = [&finish](UltraIMResultCode code, const std::string& text) {
        finish(UltraIMResult::Error(code, text));
    };
    namespace UV = UltraVault;
    const bool hadPassword = vault_.NeedsPassphrase();
    const bool hasPassword = UltraIM_ProtectionHasPassword(change.protection);
    const bool hasCode = UltraIM_ProtectionHasCode(change.protection);
    std::string error;

    // ---- what it is now, proven -------------------------------------------------
    std::string current;
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        current = vaultPassword_;
    }
    if (hadPassword) {
        if (change.currentPassword.empty()) return refuse(UltraIMResultCode::InvalidArgument, "Type your current password");
        if (vault_.UseWithPassphrase(change.currentPassword, nullptr) != UV::UnlockStatus::Ok)
            return refuse(UltraIMResultCode::AuthFailed, "That is not your current password");
        current = change.currentPassword;
    }
    bool hadCode = false;
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        hadCode = !totpSecret_.IsEmpty();
        if (hadCode) {
            if (Trimmed(change.currentCode).empty()) {
                error = "Type the code your authenticator shows now";
            } else {
                uint64_t step = 0;
                const uint64_t last = static_cast<uint64_t>(SettingNumber(store_.GetSetting("totp_last_step")));
                if (CheckCode(totpSecret_, change.currentCode, last, step, error))
                    store_.SetSetting("totp_last_step", std::to_string(step));
                else if (error.empty())
                    error = "That is not the code";
                else
                    error = "Current code: " + error;
            }
        }
    }
    if (!error.empty()) return refuse(UltraIMResultCode::AuthFailed, error);

    // ---- what it becomes ----------------------------------------------------------
    std::string password;
    if (hasPassword) {
        password = change.newPassword.empty() && hadPassword ? current : change.newPassword;
        if (password.empty()) return refuse(UltraIMResultCode::InvalidArgument, "Choose a password");
        if (password.size() < kMinPasswordLength)
            return refuse(UltraIMResultCode::InvalidArgument,
                          "The password needs at least " + std::to_string(kMinPasswordLength) + " characters");
    }
    UltraCryptSecureBuffer newSecret;
    bool newAuthenticator = false;
    uint64_t newStep = 0;
    if (hasCode) {
        std::lock_guard<std::mutex> lock(accountMutex_);
        if (!pendingTotp_.IsEmpty()) {
            if (Trimmed(change.newCode).empty()) {
                error = "Type the code the authenticator shows for ChatCube, to show it was added";
            } else if (!CheckCode(pendingTotp_, change.newCode, 0, newStep, error)) {
                error = "New authenticator: " + error;
            } else {
                newSecret = pendingTotp_.Clone();
                newAuthenticator = true;
            }
        } else if (hadCode) {
            newSecret = totpSecret_.Clone();
        } else {
            error = "Set up the authenticator first: scan the QR code with UltraAuthenticator";
        }
    }
    if (!error.empty()) return refuse(UltraIMResultCode::InvalidArgument, error);

    // ---- written: the vault re-keyed, then the authenticator into it ---------
    if (hasPassword && (!hadPassword || password != current)) {
        const UV::UnlockStatus s = vault_.ChangePassphrase(hadPassword ? current : std::string(), password, false);
        vault_.Lock();
        if (s != UV::UnlockStatus::Ok)
            return refuse(UltraIMResultCode::PluginError,
                          "The password could not be set: " + UV::DeviceKeyVault::DescribeUnlockStatus(s));
    } else if (!hasPassword && hadPassword) {
        const UV::UnlockStatus s = vault_.ChangePassphrase(current, UV::DeviceKeyVault::NewDeviceKey(), true);
        vault_.Lock();
        if (s != UV::UnlockStatus::Ok)
            return refuse(UltraIMResultCode::PluginError,
                          "The password could not be removed: " + UV::DeviceKeyVault::DescribeUnlockStatus(s));
    }
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        vaultPassword_ = hasPassword ? password : std::string();
    }
    Wipe(password);
    Wipe(current);
    const std::string secretText = hasCode ? SecretToText(newSecret) : std::string();
    bool written = false;
    WithVault([&](UV::DeviceKeyVault& v) {
        written = hasCode ? v.Store("totp", secretText) : (v.Remove("totp") || !v.Has("totp"));
    });
    std::string wipe = secretText;
    Wipe(wipe);
    if (!written) return refuse(UltraIMResultCode::PluginError, "The authenticator could not be stored in the key vault");
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        totpSecret_ = std::move(newSecret);
        if (newAuthenticator) pendingTotp_.Clear();
        codeAccepted_ = true;
    }
    store_.SetSetting("signin_code", hasCode ? "1" : "0");
    if (newAuthenticator) store_.SetSetting("totp_last_step", std::to_string(newStep));
    if (!hasCode) store_.SetSetting("totp_last_step", "0");
    finish(UltraIMResult::Ok());
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

void DirectProvider::FetchSharedCredentials(const std::string& forProviderId,
                                            UltraIMSharedCredentialsCallback onResult) {
    if (forProviderId != "telegram") {
        onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                      "The relay keeps no credentials for '" + forProviderId + "'"),
                 UltraIMCredentials());
        return;
    }
    std::shared_ptr<RelayClient> relay = Relay();
    if (!relay) {
        onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected,
                                      "Not signed in to a relay - the Telegram keys come from it"),
                 UltraIMCredentials());
        return;
    }
    relay->Request("GET", "/v1/telegram-credentials", nullptr, [onResult](const RelayResponse& r) {
        if (!r.Ok()) return onResult(RelayError(r, UltraIMResultCode::Unknown), UltraIMCredentials());
        UltraIMCredentials credentials;
        credentials.loginType = UltraIMLoginType::PhoneOTP;
        credentials.apiId = std::to_string(r.body["api_id"].GetInteger(0));
        credentials.apiHash = r.body["api_hash"].GetString();
        if (credentials.apiId == "0" || credentials.apiHash.empty()) {
            return onResult(UltraIMResult::Error(UltraIMResultCode::NotFound,
                                                 "The relay keeps no Telegram application keys"),
                            UltraIMCredentials());
        }
        onResult(UltraIMResult::Ok(), credentials);
    });
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
    if (type == "chunk") {
        HandleChunkBody(body);
        return;
    }
    if (type == "file") {
        HandleFileBody(envelope, body);
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
// FILES
// A picture, a video or a file goes out as one "file" body - the message
// with the file's name, size, kind and preview picture - followed by its
// bytes in "chunk" bodies, each a sealed envelope of its own on the same
// path as a text (the link, else the relay's queue). The other side keeps
// the parts until all are here, then puts the file together.
// ============================================================================

namespace {

constexpr size_t kChunkBytes = 32 * 1024;          // raw bytes per part: base64 and sealed, under the relay's 64 KiB
constexpr int64_t kMaxFileBytes = 64 * 1024 * 1024;
constexpr size_t kMaxPreviewBytes = 24 * 1024;     // a preview picture that still fits the file body
constexpr int kPreviewSide = 320;

int ChunkCount(int64_t size) {
    return size <= 0 ? 0 : static_cast<int>((size + static_cast<int64_t>(kChunkBytes) - 1) / static_cast<int64_t>(kChunkBytes));
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buffer[65536];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.insert(out.end(), buffer, buffer + n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

bool WriteWholeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "wb");
    if (!f) return false;
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return (std::fclose(f) == 0) && ok;
}

std::string LowerExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size() || name.find('/', dot) != std::string::npos) return std::string();
    std::string ext = name.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

std::string MimeFor(const std::string& path) {
    static const std::pair<const char*, const char*> kTypes[] = {
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".png", "image/png"},   {".gif", "image/gif"},
        {".webp", "image/webp"}, {".bmp", "image/bmp"},  {".heic", "image/heic"},
        {".svg", "image/svg+xml"}, {".svgz", "image/svg+xml"},
        {".mp4", "video/mp4"},  {".m4v", "video/mp4"},   {".mov", "video/quicktime"}, {".webm", "video/webm"},
        {".mkv", "video/x-matroska"}, {".avi", "video/x-msvideo"},
        {".mp3", "audio/mpeg"}, {".ogg", "audio/ogg"},   {".opus", "audio/ogg"},   {".wav", "audio/wav"},
        {".pdf", "application/pdf"}, {".txt", "text/plain"}, {".zip", "application/zip"},
    };
    const std::string ext = LowerExtension(path);
    for (const auto& entry : kTypes) {
        if (ext == entry.first) return entry.second;
    }
    return "application/octet-stream";
}

const char* KindOf(UltraIMMessageType type) {
    switch (type) {
        case UltraIMMessageType::Image: return "image";
        case UltraIMMessageType::Video: return "video";
        case UltraIMMessageType::Audio: return "audio";
        default: return "file";
    }
}

UltraIMMessageType TypeOfKind(const std::string& kind) {
    if (kind == "image") return UltraIMMessageType::Image;
    if (kind == "video") return UltraIMMessageType::Video;
    if (kind == "audio") return UltraIMMessageType::Audio;
    return UltraIMMessageType::File;
}

} // namespace

std::string DirectProvider::MediaPath(const std::string& fileId, const std::string& suffix) const {
    namespace fs = std::filesystem;
    const std::string base = config_.cacheDirectory.empty() ? config_.dataDirectory : config_.cacheDirectory;
    const fs::path dir = UltraCanvas::PathFromUtf8(base) / UltraCanvas::PathFromUtf8(providerId_) / "media";
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string name;
    for (char c : fileId) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') name += c;
    }
    return UltraCanvas::PathToUtf8(dir / UltraCanvas::PathFromUtf8(name + suffix));
}

UltraIMResult DirectProvider::SendImage(const std::string& conversationId, const std::string& localPath,
                                        const std::string& caption, const UltraIMSendOptions& options) {
    return SendMedia(conversationId, localPath, caption, UltraIMMessageType::Image, options);
}

UltraIMResult DirectProvider::SendVideo(const std::string& conversationId, const std::string& localPath,
                                        const std::string& caption, const UltraIMSendOptions& options) {
    return SendMedia(conversationId, localPath, caption, UltraIMMessageType::Video, options);
}

UltraIMResult DirectProvider::SendFile(const std::string& conversationId, const std::string& localPath,
                                       const UltraIMSendOptions& options) {
    return SendMedia(conversationId, localPath, std::string(), UltraIMMessageType::File, options);
}

UltraIMResult DirectProvider::SendMedia(const std::string& conversationId, const std::string& localPath,
                                        const std::string& caption, UltraIMMessageType type,
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
    std::vector<uint8_t> bytes;
    if (localPath.empty() || !ReadWholeFile(localPath, bytes))
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "There is no file " + localPath);
    if (static_cast<int64_t>(bytes.size()) > kMaxFileBytes)
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "ChatCube sends files up to 64 MiB");

    const int64_t now = NowMs();
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    const std::string fileId = UltraIM_NewClientMessageId();
    const std::string name = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(localPath).filename());
    const std::string mime = MimeFor(localPath);
    const int parts = ChunkCount(static_cast<int64_t>(bytes.size()));

    // The preview picture, when the application makes one and it is small
    // enough to ride in the file body.
    UltraIMMessage message;
    message.attachment.localPath = localPath;
    message.attachment.isDownloaded = true;
    message.attachment.remoteUrl = fileId;
    message.attachment.fileName = name;
    message.attachment.mimeType = mime;
    message.attachment.fileSizeBytes = static_cast<int64_t>(bytes.size());
    // A file that is a picture (an SVG drawing goes as a file) gets one too,
    // so the other side sees the drawing before the file is here.
    std::string previewBase64;
    if (type == UltraIMMessageType::Image || type == UltraIMMessageType::Video || mime.rfind("image/", 0) == 0) {
        int w = 0, h = 0;
        const std::string preview = UltraIM_MakePreview(localPath, mime, kPreviewSide, w, h);
        std::vector<uint8_t> previewBytes;
        if (!preview.empty() && ReadWholeFile(preview, previewBytes) && previewBytes.size() <= kMaxPreviewBytes) {
            previewBase64 = Base64(previewBytes);
            message.attachment.thumbnailPath = preview;
            if (type == UltraIMMessageType::Image && message.attachment.width == 0) {
                // The preview keeps the picture's shape: good enough for the bubble.
                message.attachment.width = w;
                message.attachment.height = h;
            }
        }
    }

    JSONValue body = JSONValue::MakeObject();
    body.Set("type", JSONValue("file"));
    body.Set("kind", JSONValue(KindOf(type)));
    body.Set("file", JSONValue(fileId));
    body.Set("name", JSONValue(name));
    body.Set("mime", JSONValue(mime));
    body.Set("size", JSONValue(static_cast<int64_t>(bytes.size())));
    body.Set("parts", JSONValue(static_cast<int64_t>(parts)));
    body.Set("text", JSONValue(caption));
    body.Set("w", JSONValue(static_cast<int64_t>(message.attachment.width)));
    body.Set("h", JSONValue(static_cast<int64_t>(message.attachment.height)));
    if (!previewBase64.empty()) body.Set("preview", JSONValue(previewBase64));
    body.Set("sender_name", JSONValue(displayName_));
    if (!options.replyToMessageId.empty()) body.Set("reply_to", JSONValue(options.replyToMessageId));
    if (conversation.isGroup) body.Set("group_title", JSONValue(conversation.title));

    message.providerId = providerId_;
    message.conversationId = conversationId;
    message.clientMessageId = clientId;
    message.messageId = clientId;
    message.senderId = identity_.userId;
    message.senderName = displayName_;
    message.timestampMs = now;
    message.type = type;
    message.text = caption;
    message.replyTo.messageId = options.replyToMessageId;
    message.deliveryStatus = UltraIMDeliveryStatus::Sending;
    message.isOwn = true;
    if (!store_.AddMessage(message))
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "This client message id was already sent");

    const std::string wire = conversation.isGroup ? conversationId : kDirectConversation;
    QueueBody(wire, recipients, clientId, UltraCanvas::JSON::Serialize(body), now);
    // The parts, in order, each under an id of its own (their delivery is
    // nobody's business but the outbox's).
    for (int seq = 0; seq < parts; ++seq) {
        const size_t at = static_cast<size_t>(seq) * kChunkBytes;
        const size_t n = std::min(kChunkBytes, bytes.size() - at);
        JSONValue chunk = JSONValue::MakeObject();
        chunk.Set("type", JSONValue("chunk"));
        chunk.Set("file", JSONValue(fileId));
        chunk.Set("seq", JSONValue(static_cast<int64_t>(seq)));
        chunk.Set("parts", JSONValue(static_cast<int64_t>(parts)));
        chunk.Set("data", JSONValue(Base64(std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                                                                bytes.begin() + static_cast<std::ptrdiff_t>(at + n)))));
        QueueBody(wire, recipients, "c-" + fileId + "-" + std::to_string(seq), UltraCanvas::JSON::Serialize(chunk), now);
    }

    UltraIMEvent updated;
    updated.type = UltraIMEventType::ConversationUpdate;
    updated.providerId = providerId_;
    updated.conversationId = conversationId;
    store_.GetConversation(conversationId, updated.conversation);
    Emit(updated);
    return UltraIMResult::Ok();
}

void DirectProvider::HandleFileBody(const Envelope& envelope, const JSONValue& body) {
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
        if (!conversation.memberIds.empty() &&
            std::find(conversation.memberIds.begin(), conversation.memberIds.end(), envelope.from) ==
                conversation.memberIds.end())
            return;
    } else {
        conversation = EnsureDirectConversation(envelope.from);
    }
    const std::string fileId = body["file"].GetString();
    if (fileId.empty()) return;

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
    message.type = TypeOfKind(body["kind"].GetString());
    message.text = body["text"].GetString();
    message.replyTo.messageId = body["reply_to"].GetString();
    message.deliveryStatus = UltraIMDeliveryStatus::Delivered;
    message.isOwn = false;
    message.attachment.remoteUrl = fileId;
    message.attachment.fileName = body["name"].GetString();
    message.attachment.mimeType = body["mime"].GetString();
    message.attachment.fileSizeBytes = body["size"].GetInteger(0);
    message.attachment.width = static_cast<int>(body["w"].GetInteger(0));
    message.attachment.height = static_cast<int>(body["h"].GetInteger(0));
    const std::string preview = body["preview"].GetString();
    if (!preview.empty()) {
        const std::string path = MediaPath(fileId, ".preview.jpg");
        if (WriteWholeFile(path, FromBase64(preview))) message.attachment.thumbnailPath = path;
    }
    if (!store_.AddMessage(message)) return;    // seen already

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
    TryAssemble(fileId);        // the parts may have come first
}

void DirectProvider::HandleChunkBody(const JSONValue& body) {
    const std::string fileId = body["file"].GetString();
    const int64_t seq = body["seq"].GetInteger(-1);
    const std::string data = body["data"].GetString();
    if (fileId.empty() || seq < 0 || seq >= 1000000 || data.empty()) return;
    store_.AddPart(fileId, static_cast<int>(seq), data);
    TryAssemble(fileId);
}

void DirectProvider::TryAssemble(const std::string& fileId) {
    UltraIMMessage message;
    if (!store_.FindMessageByFile(fileId, message) || message.attachment.isDownloaded) return;
    const int parts = ChunkCount(message.attachment.fileSizeBytes);
    if (store_.CountParts(fileId) < parts) return;
    std::vector<uint8_t> bytes;
    for (const std::string& part : store_.Parts(fileId)) {
        const std::vector<uint8_t> raw = FromBase64(part);
        bytes.insert(bytes.end(), raw.begin(), raw.end());
    }
    if (static_cast<int64_t>(bytes.size()) != message.attachment.fileSizeBytes) {
        // Parts of another size than said: not this file. Start over.
        store_.RemoveParts(fileId);
        return;
    }
    const std::string path = MediaPath(fileId, LowerExtension(message.attachment.fileName));
    if (!WriteWholeFile(path, bytes)) return;
    store_.RemoveParts(fileId);
    message.attachment.localPath = path;
    message.attachment.isDownloaded = true;
    store_.UpdateAttachment(message.conversationId, message.clientMessageId, message.attachment);

    std::vector<UltraIMAttachmentCallback> waiters;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mediaWaiters_.find(fileId);
        if (it != mediaWaiters_.end()) {
            waiters = std::move(it->second);
            mediaWaiters_.erase(it);
        }
    }
    UltraIMEvent updated;
    updated.type = UltraIMEventType::MessageUpdate;
    updated.providerId = providerId_;
    updated.conversationId = message.conversationId;
    updated.contactId = message.senderId;
    updated.message = message;
    Emit(updated);
    for (const auto& waiter : waiters) waiter(UltraIMResult::Ok(), message.attachment);
}

void DirectProvider::DownloadAttachment(const std::string& conversationId, const std::string& messageId,
                                        UltraIMAttachmentCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r, const UltraIMAttachment& a) { if (onResult) onResult(r, a); };
    UltraIMMessage message;
    if (!store_.FindMessage(conversationId, messageId, message)) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message"), UltraIMAttachment());
        return;
    }
    if (message.attachment.remoteUrl.empty()) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotFound, "That message has no attachment"), UltraIMAttachment());
        return;
    }
    if (message.attachment.isDownloaded && !message.attachment.localPath.empty()) {
        finish(UltraIMResult::Ok(), message.attachment);
        return;
    }
    // On its way: the sender pushes the parts; told when the last is here.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mediaWaiters_[message.attachment.remoteUrl].push_back(finish);
    }
    TryAssemble(message.attachment.remoteUrl);
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
        if (r.Ok()) sentOverRelay_ += static_cast<int>(sealed.size());
        // One mark per message (a group's copies share the client id; a
        // file's parts have ids of their own, which no message carries).
        std::set<std::string> marked;
        for (const auto& copy : sealed) {
            if (!marked.insert(copy.clientId).second) continue;
            const std::string local = IsGroupId(copy.conversation) ? copy.conversation : copy.recipient;
            MarkDelivered(local, copy.clientId, r.Ok() ? UltraIMDeliveryStatus::Sent : UltraIMDeliveryStatus::Failed);
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
    std::vector<OutboxEntry> relayCopies;                          // 1:1 copies for the relay, one request
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
        if (relay && (!online || now - entry.timestampMs > timings_.relayFallbackMs)) relayCopies.push_back(entry);
    }
    if (relay) {
        // A file's parts go in a few requests, not one each.
        constexpr size_t kBatch = 64;
        for (size_t at = 0; at < relayCopies.size(); at += kBatch) {
            PostToRelay(std::vector<OutboxEntry>(relayCopies.begin() + static_cast<std::ptrdiff_t>(at),
                                                 relayCopies.begin() + static_cast<std::ptrdiff_t>(std::min(relayCopies.size(), at + kBatch))));
        }
        for (const auto& group : groupCopies) PostToRelay(group.second);
    }
}

void DirectProvider::Wake() {
    std::lock_guard<std::mutex> lock(mutex_);
    woken_ = true;
    wake_.notify_all();
}

void DirectProvider::WorkerLoop() {
    std::vector<std::function<void()>> jobs;   // SignIn with a password, SetSignInProtection
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(timings_.tickMs), [this]() { return stopping_ || woken_; });
            if (stopping_) return;
            woken_ = false;
            jobs.swap(jobs_);
        }
        for (auto& job : jobs) job();
        jobs.clear();
        Pump();
    }
}

} // namespace UltraIMDirect
