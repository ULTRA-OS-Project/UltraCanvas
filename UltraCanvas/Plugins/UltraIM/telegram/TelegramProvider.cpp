// Plugins/UltraIM/telegram/TelegramProvider.cpp
// Telegram as an UltraIM provider, on TDLib's JSON interface
// Version: 0.1.1 - TDLib's connection state reported while signing in; a step TDLib does not
//                  answer times out instead of hanging; TDLib's own log in the data folder
// Version: 0.1.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework

#include "TelegramProvider.h"
#include "TelegramRequests.h"

#include "UltraCanvasDebug.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraIM/UltraIMCore.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>

// The application's API credentials (my.telegram.org), when the build has
// them (CMake: ULTRAIM_TELEGRAM_API_ID / ULTRAIM_TELEGRAM_API_HASH).
#ifndef ULTRAIM_TELEGRAM_API_ID
#define ULTRAIM_TELEGRAM_API_ID 0
#endif
#ifndef ULTRAIM_TELEGRAM_API_HASH
#define ULTRAIM_TELEGRAM_API_HASH ""
#endif

namespace UltraIMTelegram {

using UltraCanvas::JSONValue;

namespace {

constexpr size_t kRecentCap = 300;      // messages kept per chat in memory

bool IsError(const TdObject& o) {
    return TdType(o) == "error";
}

std::string ErrorText(const TdObject& o) {
    return o["message"].GetString("TDLib error " + std::to_string(o["code"].GetInteger(0)));
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

int64_t ToId(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int64_t>(v) : 0;
}

UltraIMPresence PresenceFrom(const TdObject& status) {
    const std::string t = TdType(status);
    if (t == "userStatusOnline") return UltraIMPresence::Online;
    if (t == "userStatusRecently") return UltraIMPresence::Away;
    if (t == "userStatusOffline" || t == "userStatusLastWeek" || t == "userStatusLastMonth")
        return UltraIMPresence::Offline;
    return UltraIMPresence::Unknown;
}

std::string SnippetOf(const UltraIMMessage& m) {
    std::string text = m.text;
    switch (m.type) {
        case UltraIMMessageType::Image: text = "[photo] " + m.text; break;
        case UltraIMMessageType::File:  text = "[file] " + m.text; break;
        case UltraIMMessageType::Audio: text = "[audio] " + m.text; break;
        case UltraIMMessageType::Video: text = "[video] " + m.text; break;
        case UltraIMMessageType::Sticker: text = m.text + " (sticker)"; break;
        default: break;
    }
    const size_t nl = text.find('\n');
    if (nl != std::string::npos) text = text.substr(0, nl);
    return text.size() > 120 ? text.substr(0, 120) : text;
}

std::string SystemName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

std::string LibraryName() {
#if defined(_WIN32)
    return "tdjson.dll";
#elif defined(__APPLE__)
    return "libtdjson.dylib";
#else
    return "libtdjson.so";
#endif
}

// An address: "+44 20 ..." -> a phone number (digits and the plus), else a
// username without "@" or a t.me link.
bool IsPhone(const std::string& address) {
    if (address.empty() || (address[0] != '+' && !std::isdigit(static_cast<unsigned char>(address[0])))) return false;
    for (char c : address) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '+' && c != ' ' && c != '-' && c != '(' && c != ')')
            return false;
    }
    return true;
}

std::string Username(std::string address) {
    for (const char* prefix : {"https://t.me/", "http://t.me/", "t.me/", "@"}) {
        if (address.rfind(prefix, 0) == 0) address = address.substr(std::string(prefix).size());
    }
    return address;
}

} // namespace

// ============================================================================
// LIFECYCLE
// ============================================================================

TelegramProvider::TelegramProvider(std::string providerId) : providerId_(std::move(providerId)) {}

TelegramProvider::~TelegramProvider() {
    Shutdown();
}

UltraIMCapabilityFlags TelegramProvider::GetCapabilities() const {
    using F = UltraIMCapabilityFlags;
    return F::TextMessages | F::GroupChats | F::ReadReceipts | F::TypingIndicator | F::EditMessage |
           F::DeleteMessage | F::ReplyToMessage | F::OfflineMessages | F::MultiDevice | F::AddContact |
           F::CreateGroup | F::Channels;
}

UltraIMResult TelegramProvider::Initialize(const UltraIMConfig& config, UltraIMEventSink sink) {
    config_ = config;
    sink_ = std::move(sink);
    if (config.dataDirectory.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "The Telegram provider needs a data directory (UltraIMConfig::dataDirectory)");
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(DataPath(""));
    std::filesystem::create_directories(dir, ec);
    vault_ = UltraVault::DeviceKeyVault(UltraCanvas::PathToUtf8(dir), {"telegram.vault", config.appId + "." + providerId_ + "."});
    vaultReady_ = true;
    {
        std::lock_guard<std::mutex> lock(watchMutex_);
        watchStop_ = false;
        watchArmed_ = false;
    }
    if (!watchdog_.joinable()) watchdog_ = std::thread([this]() { WatchdogLoop(); });
    debugOutput << "[Telegram] provider '" << providerId_ << "' initialised; data in " << DataPath("") << std::endl;
    return UltraIMResult::Ok();
}

void TelegramProvider::Shutdown() {
    SignOut();
    {
        std::lock_guard<std::mutex> lock(watchMutex_);
        watchStop_ = true;
        watchArmed_ = false;
    }
    watchCv_.notify_all();
    if (watchdog_.joinable()) watchdog_.join();
    sink_ = nullptr;
}

// ============================================================================
// THE WATCHDOG OVER A SIGN-IN STEP
// TDLib answers a step only once Telegram's servers have: with no network it
// retries in silence for as long as it takes, and the application would
// wait forever with its button disabled. So every step is timed, and one
// that gets no answer fails with Timeout and says what TDLib's connection
// was doing - the step itself may still complete later, and then its state
// change reaches the application as an event.
// ============================================================================

void TelegramProvider::ArmStep(const std::string& what) {
    {
        std::lock_guard<std::mutex> lock(watchMutex_);
        watchArmed_ = true;
        watchWhat_ = what;
        watchDeadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(stepTimeoutMs_);
    }
    watchCv_.notify_all();
    debugOutput << "[Telegram] waiting for TDLib: " << what << " (up to " << stepTimeoutMs_ << " ms)" << std::endl;
}

void TelegramProvider::DisarmStep() {
    {
        std::lock_guard<std::mutex> lock(watchMutex_);
        watchArmed_ = false;
    }
    watchCv_.notify_all();
}

void TelegramProvider::WatchdogLoop() {
    std::unique_lock<std::mutex> lock(watchMutex_);
    while (!watchStop_) {
        if (!watchArmed_) {
            watchCv_.wait(lock, [this]() { return watchStop_ || watchArmed_; });
            continue;
        }
        if (watchCv_.wait_until(lock, watchDeadline_, [this]() { return watchStop_ || !watchArmed_; })) continue;
        // Still armed and the deadline has passed.
        const std::string what = watchWhat_;
        watchArmed_ = false;
        lock.unlock();
        OnStepTimeout(what);
        lock.lock();
    }
}

std::string TelegramProvider::ConnectionText() const {
    std::string connection;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection = connection_;
    }
    if (connection == "connectionStateWaitingForNetwork") return "TDLib is waiting for the network";
    if (connection == "connectionStateConnectingToProxy") return "TDLib is connecting to the proxy";
    if (connection == "connectionStateConnecting") return "TDLib is still connecting to Telegram's servers";
    if (connection == "connectionStateUpdating") return "TDLib is fetching updates";
    if (connection == "connectionStateReady") return "TDLib is connected to Telegram";
    return "TDLib has not reported its connection yet";
}

void TelegramProvider::OnStepTimeout(const std::string& what) {
    bool pending = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending = pendingSignIn_ != nullptr;
    }
    const std::string why = "Telegram has not answered in " + std::to_string(stepTimeoutMs_ / 1000) + " s (" + what +
                            "): " + ConnectionText() + ". Check the connection and try again.";
    debugOutput << "[Telegram] " << why << (pending ? "" : " (nobody waiting)") << std::endl;
    if (!pending) return;
    Finish(UltraIMResult::Error(UltraIMResultCode::Timeout, why));
    SetState(UltraIMConnectionState::Error, why);
}

std::string TelegramProvider::DataPath(const std::string& name) const {
    std::filesystem::path dir = UltraCanvas::PathFromUtf8(config_.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    if (!name.empty()) dir /= UltraCanvas::PathFromUtf8(name);
    return UltraCanvas::PathToUtf8(dir);
}

void TelegramProvider::Emit(const UltraIMEvent& event) {
    if (sink_) sink_(event);
}

void TelegramProvider::SetState(UltraIMConnectionState state, const std::string& error) {
    if (state_.exchange(state) == state && error.empty()) return;
    debugOutput << "[Telegram] state " << static_cast<int>(state) << (error.empty() ? "" : ": " + error) << std::endl;
    UltraIMEvent event;
    event.type = UltraIMEventType::ConnectionStateChange;
    event.providerId = providerId_;
    event.connectionState = state;
    event.errorText = error;
    Emit(event);
}

void TelegramProvider::Finish(const UltraIMResult& result) {
    DisarmStep();
    UltraIMResultCallback done;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done = std::move(pendingSignIn_);
        pendingSignIn_ = nullptr;
    }
    if (done) {
        debugOutput << "[Telegram] sign-in step answered: " << (result ? "ok" : result.message)
                    << " (code " << static_cast<int>(result.code) << ")" << std::endl;
        done(result);
    }
}

std::shared_ptr<TdClient> TelegramProvider::Client() const {
    static const std::shared_ptr<TdClient> closed = std::make_shared<TdClient>();
    std::lock_guard<std::mutex> lock(clientMutex_);
    return client_ ? client_ : closed;
}

std::string TelegramProvider::GetLocalUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return myId_ ? std::to_string(myId_) : std::string();
}

UltraIMAuthStep TelegramProvider::GetAuthStep() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return authStep_;
}

std::string TelegramProvider::GetAuthHint() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return authHint_;
}

// ============================================================================
// SESSION
// ============================================================================

bool TelegramProvider::SaveSession(int64_t userId) {
    if (!vaultReady_) return false;
    JSONValue session = JSONValue::MakeObject();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session.Set("api_id", JSONValue(static_cast<int64_t>(apiId_)));
        session.Set("api_hash", JSONValue(apiHash_));
        session.Set("test_dc", JSONValue(testDc_));
        session.Set("phone", JSONValue(phone_));
    }
    session.Set("user_id", JSONValue(userId));
    bool stored = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("session", UltraCanvas::JSON::Serialize(session)); });
    return stored;
}

bool TelegramProvider::LoadSession(int32_t& apiId, std::string& apiHash, bool& testDc, std::string& phone) const {
    if (!vaultReady_) return false;
    std::string text;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("session", text); });
    if (text.empty()) return false;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue session = UltraCanvas::JSON::Parse(text, &parsed);
    apiId = static_cast<int32_t>(session["api_id"].GetInteger(0));
    apiHash = session["api_hash"].GetString();
    testDc = session["test_dc"].GetBoolean(false);
    phone = session["phone"].GetString();
    return apiId != 0 && !apiHash.empty();
}

bool TelegramProvider::HasStoredSession() const {
    int32_t apiId = 0;
    std::string apiHash, phone;
    bool testDc = false;
    std::error_code ec;
    return LoadSession(apiId, apiHash, testDc, phone) &&
           std::filesystem::exists(UltraCanvas::PathFromUtf8(DataPath("td")), ec);
}

// TDLib's database is encrypted with a key of ours, kept in the vault.
std::string TelegramProvider::DatabaseKey() {
    std::string encoded;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("database_key", encoded); });
    std::vector<uint8_t> key = UltraCanvas::Base64Decode(encoded);
    if (key.size() != 32) {
        if (!UltraCrypt_RandomBytes(key, 32)) return std::string();
        const std::string fresh = UltraCanvas::Base64Encode(key, false);
        bool stored = false;
        vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("database_key", fresh); });
        if (!stored) return std::string();
    }
    std::string out(key.begin(), key.end());
    UltraCrypt_SecureZero(key.data(), key.size());
    return out;
}

void TelegramProvider::SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    if (state_ == UltraIMConnectionState::Connected) {
        finish(UltraIMResult::Ok());
        return;
    }
    // A sign-in under way: this is the next step's answer.
    {
        const bool open = Client()->IsOpen();
        std::unique_lock<std::mutex> lock(mutex_);
        if (open && authStep_ != UltraIMAuthStep::Idle) {
            const UltraIMAuthStep step = authStep_;
            TdObject request;
            if (step == UltraIMAuthStep::PhoneNumber && !Trim(credentials.phoneNumber).empty()) {
                phone_ = Trim(credentials.phoneNumber);
                request = Request::SetPhoneNumber(phone_);
            } else if (step == UltraIMAuthStep::Code && !Trim(credentials.otpCode).empty()) {
                request = Request::CheckCode(Trim(credentials.otpCode));
            } else if (step == UltraIMAuthStep::Password && !credentials.password.empty()) {
                request = Request::CheckPassword(credentials.password);
            } else if (step == UltraIMAuthStep::Registration && !Trim(credentials.displayName).empty()) {
                const std::string name = Trim(credentials.displayName);
                const size_t space = name.find(' ');
                request = Request::RegisterUser(name.substr(0, space),
                                                space == std::string::npos ? std::string() : Trim(name.substr(space)));
            } else {
                lock.unlock();
                finish(UltraIMResult::Error(UltraIMResultCode::NeedsInput, GetAuthHint()));
                return;
            }
            UltraIMResultCallback earlier = std::move(pendingSignIn_);
            pendingSignIn_ = onResult;
            lock.unlock();
            if (earlier) earlier(UltraIMResult::Error(UltraIMResultCode::InvalidState, "Superseded by a newer sign-in"));
            debugOutput << "[Telegram] sign-in step " << static_cast<int>(step) << ": sending " << TdType(request) << std::endl;
            ArmStep(TdType(request));
            Client()->Send(request, [this](const TdObject& answer) { OnAuthAnswer(answer); });
            return;
        }
    }
    SignOut();      // whatever is left of an earlier one

    // The application's API credentials: given, kept from the last sign-in,
    // built in, or from the environment.
    int32_t storedId = 0;
    std::string storedHash, storedPhone;
    bool storedTest = false;
    const bool stored = LoadSession(storedId, storedHash, storedTest, storedPhone);
    int32_t apiId = static_cast<int32_t>(ToId(Trim(credentials.apiId)));
    std::string apiHash = Trim(credentials.apiHash);
    if (!apiId || apiHash.empty()) {
        apiId = storedId;
        apiHash = storedHash;
    }
    if (!apiId || apiHash.empty()) {
        apiId = ULTRAIM_TELEGRAM_API_ID;
        apiHash = ULTRAIM_TELEGRAM_API_HASH;
    }
    if (!apiId || apiHash.empty()) {
        apiId = static_cast<int32_t>(ToId(UltraCanvas::GetEnvUtf8("ULTRAIM_TELEGRAM_API_ID")));
        apiHash = UltraCanvas::GetEnvUtf8("ULTRAIM_TELEGRAM_API_HASH");
    }
    if (!apiId || apiHash.empty()) {
        finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "Telegram needs this application's API ID and API hash (from my.telegram.org)"));
        return;
    }
    const std::string phone = Trim(credentials.phoneNumber);
    if (phone.empty() && !HasStoredSession()) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotAuthenticated, "Sign in to Telegram with your phone number"));
        return;
    }
    debugOutput << "[Telegram] sign-in: " << (phone.empty() ? "resuming the stored session" : "a phone number given")
                << ", API ID " << apiId << " (" << (credentials.apiId.empty() ? "" : "given, ")
                << (stored ? "stored, " : "") << (ULTRAIM_TELEGRAM_API_ID ? "built in" : "not built in") << ")"
                << (credentials.server == "test" ? ", test servers" : "") << std::endl;

    std::string error;
    std::vector<std::string> candidates;
    if (!libraryPath_.empty()) candidates.push_back(libraryPath_);
    if (!config_.pluginDirectory.empty()) {
        const std::filesystem::path plugins = UltraCanvas::PathFromUtf8(config_.pluginDirectory);
        candidates.push_back(UltraCanvas::PathToUtf8(plugins / "tdlib" / UltraCanvas::PathFromUtf8(LibraryName())));
        candidates.push_back(UltraCanvas::PathToUtf8(plugins / UltraCanvas::PathFromUtf8(LibraryName())));
    }
    if (!TdJsonLibrary::Instance().Load(candidates, error)) {
        finish(UltraIMResult::Error(UltraIMResultCode::PluginError, error));
        return;
    }
    // TDLib's own log, next to its database: warnings and errors, or more
    // when the framework's debug stream is on (ULTRACANVAS_DEBUG_LOG,
    // ChatCube --debug).
    TdJsonLibrary::Instance().SetLogFile(DataPath("tdlib.log"), UltraCanvas::IsDebugOutputEnabled() ? 3 : 2);
    auto client = std::make_shared<TdClient>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apiId_ = apiId;
        apiHash_ = apiHash;
        testDc_ = credentials.server == "test" || (credentials.server.empty() && phone.empty() && storedTest);
        phone_ = phone.empty() ? storedPhone : phone;
        authStep_ = UltraIMAuthStep::Idle;
        authHint_.clear();
        ready_ = false;
        closing_ = false;
        connection_.clear();
        pendingSignIn_ = onResult;
    }
    if (!client->Open([this](const TdObject& update) { OnUpdate(update); }, error)) {
        Finish(UltraIMResult::Error(UltraIMResultCode::PluginError, error));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        client_ = client;
    }
    SetState(UltraIMConnectionState::Connecting, "Starting TDLib");
    // The first request starts the client; its first update asks for the
    // parameters.
    ArmStep("starting TDLib");
    Client()->Send(Request::GetVersion());
}

void TelegramProvider::SignOut() {
    DisarmStep();
    std::shared_ptr<TdClient> client;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_ = true;
    }
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        client = std::move(client_);
        client_.reset();
    }
    // After Close no handler of it runs any more.
    if (client) client->Close();
    client.reset();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        chats_.clear();
        users_.clear();
        tempToClient_.clear();
        authStep_ = UltraIMAuthStep::Idle;
        authHint_.clear();
        ready_ = false;
        myId_ = 0;
    }
    Finish(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Signed out"));
    SetState(UltraIMConnectionState::Disconnected);
}

void TelegramProvider::SetStep(UltraIMAuthStep step, const std::string& hint) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        authStep_ = step;
        authHint_ = hint;
    }
    UltraIMEvent event;
    event.type = UltraIMEventType::AuthStepChange;
    event.providerId = providerId_;
    event.authStep = step;
    event.errorText = hint;
    Emit(event);
}

void TelegramProvider::OnAuthAnswer(const TdObject& answer) {
    // Ok answers say nothing: the next state does. An error is this step's.
    if (IsError(answer)) {
        debugOutput << "[Telegram] the step was refused: " << answer["code"].GetInteger(0) << " " << ErrorText(answer)
                    << std::endl;
        Finish(UltraIMResult::Error(UltraIMResultCode::AuthFailed, ErrorText(answer)));
    }
}

void TelegramProvider::OnAuthorizationState(const TdObject& state) {
    const std::string type = TdType(state);
    debugOutput << "[Telegram] authorization state: " << type << std::endl;
    if (type == "authorizationStateWaitTdlibParameters") {
        Request::Parameters p;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            p.useTestDc = testDc_;
            p.apiId = apiId_;
            p.apiHash = apiHash_;
        }
        p.databaseDirectory = DataPath("td");
        p.filesDirectory = DataPath("files");
        p.databaseKey = DatabaseKey();
        p.systemVersion = SystemName();
        p.applicationVersion = config_.appId;
        if (p.databaseKey.empty()) {
            Finish(UltraIMResult::Error(UltraIMResultCode::PluginError, "The vault does not keep Telegram's key"));
            SetState(UltraIMConnectionState::Error, "The vault does not keep Telegram's key");
            return;
        }
        debugOutput << "[Telegram] setTdlibParameters: database " << p.databaseDirectory
                    << (p.useTestDc ? " (test servers)" : "") << std::endl;
        ArmStep("setTdlibParameters");
        Client()->Send(Request::SetTdlibParameters(p), [this](const TdObject& answer) {
            if (!IsError(answer)) return;
            Finish(UltraIMResult::Error(UltraIMResultCode::PluginError, ErrorText(answer)));
            SetState(UltraIMConnectionState::Error, ErrorText(answer));
        });
    } else if (type == "authorizationStateWaitPhoneNumber") {
        std::string phone;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            phone = phone_;
        }
        if (phone.empty()) {
            SetStep(UltraIMAuthStep::PhoneNumber, "Your phone number, with the country code");
            Finish(UltraIMResult::Error(UltraIMResultCode::NeedsInput, "Your phone number, with the country code"));
            return;
        }
        // Asked for, as the step to come back to if Telegram refuses it.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            authStep_ = UltraIMAuthStep::PhoneNumber;
            authHint_ = "Your phone number, with the country code";
        }
        ArmStep("setAuthenticationPhoneNumber - Telegram sending the code");
        Client()->Send(Request::SetPhoneNumber(phone), [this](const TdObject& answer) { OnAuthAnswer(answer); });
    } else if (type == "authorizationStateWaitCode") {
        const TdObject& info = state["code_info"];
        const std::string how = TdType(info["type"]);
        std::string hint = "The code Telegram sent";
        if (how == "authenticationCodeTypeTelegramMessage") hint = "The code Telegram sent to your Telegram app";
        else if (how == "authenticationCodeTypeSms") hint = "The code sent by SMS to " + info["phone_number"].GetString();
        else if (how == "authenticationCodeTypeCall") hint = "The code you are told in a call to " + info["phone_number"].GetString();
        SetStep(UltraIMAuthStep::Code, hint);
        Finish(UltraIMResult::Error(UltraIMResultCode::NeedsInput, hint));
    } else if (type == "authorizationStateWaitPassword") {
        std::string hint = "Your two-step verification password";
        const std::string given = state["password_hint"].GetString();
        if (!given.empty()) hint += " (hint: " + given + ")";
        SetStep(UltraIMAuthStep::Password, hint);
        Finish(UltraIMResult::Error(UltraIMResultCode::NeedsInput, hint));
    } else if (type == "authorizationStateWaitRegistration") {
        SetStep(UltraIMAuthStep::Registration, "A new Telegram account: your name");
        Finish(UltraIMResult::Error(UltraIMResultCode::NeedsInput, "A new Telegram account: your name"));
    } else if (type == "authorizationStateWaitOtherDeviceConfirmation" || type == "authorizationStateWaitEmailAddress" ||
               type == "authorizationStateWaitEmailCode" || type == "authorizationStateWaitPremiumPurchase") {
        const std::string why = "Telegram asks for a sign-in step ChatCube cannot do yet (" + type.substr(18) + ")";
        Finish(UltraIMResult::Error(UltraIMResultCode::Unsupported, why));
        SetState(UltraIMConnectionState::Error, why);
    } else if (type == "authorizationStateReady") {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            authStep_ = UltraIMAuthStep::Idle;
            authHint_.clear();
            ready_ = true;
        }
        ArmStep("getMe");
        Client()->Send(Request::GetMe(), [this](const TdObject& me) {
            if (IsError(me)) {
                Finish(UltraIMResult::Error(UltraIMResultCode::NetworkError, ErrorText(me)));
                return;
            }
            const int64_t id = TdInt(me["id"]);
            std::vector<UltraIMEvent> none;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                myId_ = id;
                User& u = users_[id];
                u.id = id;
                u.firstName = me["first_name"].GetString();
                u.lastName = me["last_name"].GetString();
                u.phone = me["phone_number"].GetString();
                if (!u.phone.empty() && phone_.empty()) phone_ = "+" + u.phone;
            }
            SaveSession(id);
            SetState(UltraIMConnectionState::Connected);
            Finish(UltraIMResult::Ok());
            // The main list: loadChats until TDLib says it has all (404).
            auto more = std::make_shared<std::function<void()>>();
            *more = [this, more]() {
                Client()->Send(Request::LoadChats(100), [more](const TdObject& answer) {
                    if (!IsError(answer)) (*more)();
                    else *more = nullptr;
                });
            };
            (*more)();
        });
    } else if (type == "authorizationStateLoggingOut") {
        // Ended from elsewhere (another device, the account deleted).
        vault_.Use([](UltraVault::DeviceKeyVault& v) { v.Remove("session"); });
        UltraIMEvent event;
        event.type = UltraIMEventType::AuthRequire;
        event.providerId = providerId_;
        Emit(event);
    } else if (type == "authorizationStateClosed") {
        bool ours = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ours = closing_;
            ready_ = false;
        }
        if (!ours) {
            Finish(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Telegram closed the session"));
            SetState(UltraIMConnectionState::Disconnected);
        }
    }
}

// ============================================================================
// UPDATES (receive thread)
// ============================================================================

void TelegramProvider::OnUpdate(const TdObject& update) {
    const std::string type = TdType(update);
    if (type == "updateAuthorizationState") {
        OnAuthorizationState(update["authorization_state"]);
        return;
    }
    if (type == "updateConnectionState") {
        const std::string s = TdType(update["state"]);
        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready = ready_ && myId_ != 0;
            connection_ = s;
        }
        if (!ready) {
            // Still signing in: say what TDLib is doing, so a sign-in that
            // waits on the network is seen to wait rather than hang.
            if (state_ == UltraIMConnectionState::Connecting) SetState(UltraIMConnectionState::Connecting, ConnectionText());
            return;
        }
        if (s == "connectionStateReady") SetState(UltraIMConnectionState::Connected);
        else if (s == "connectionStateWaitingForNetwork") SetState(UltraIMConnectionState::Reconnecting, "Waiting for the network");
        else SetState(UltraIMConnectionState::Reconnecting);
        return;
    }

    std::vector<UltraIMEvent> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (type == "updateUser") {
            const TdObject& u = update["user"];
            User& user = users_[TdInt(u["id"])];
            user.id = TdInt(u["id"]);
            user.firstName = u["first_name"].GetString();
            user.lastName = u["last_name"].GetString();
            user.phone = u["phone_number"].GetString();
            const auto& names = u["usernames"]["active_usernames"].GetElements();
            user.username = names.empty() ? u["username"].GetString() : names.front().GetString();
            user.presence = PresenceFrom(u["status"]);
            for (const auto& entry : chats_) {
                if (entry.second.userId == user.id) ConversationChanged(entry.second, events);
            }
        } else if (type == "updateUserStatus") {
            const int64_t id = TdInt(update["user_id"]);
            User& user = users_[id];
            user.id = id;
            const UltraIMPresence presence = PresenceFrom(update["status"]);
            if (presence != user.presence) {
                user.presence = presence;
                for (const auto& entry : chats_) {
                    if (entry.second.userId != id || entry.second.order == 0) continue;
                    UltraIMEvent e;
                    e.type = UltraIMEventType::PresenceChange;
                    e.providerId = providerId_;
                    e.contactId = std::to_string(id);
                    e.conversationId = std::to_string(entry.first);
                    e.presence = presence;
                    events.push_back(e);
                }
            }
        } else if (type == "updateNewChat") {
            StoreChat(update["chat"], events);
        } else if (type == "updateChatTitle") {
            auto it = chats_.find(TdInt(update["chat_id"]));
            if (it != chats_.end()) {
                it->second.title = update["title"].GetString();
                ConversationChanged(it->second, events);
            }
        } else if (type == "updateChatPosition" || type == "updateChatLastMessage") {
            auto it = chats_.find(TdInt(update["chat_id"]));
            if (it != chats_.end()) {
                Chat& chat = it->second;
                const bool wasListed = chat.order != 0;
                std::vector<TdObject> positions;
                if (type == "updateChatPosition") positions.push_back(update["position"]);
                else for (const auto& p : update["positions"].GetElements()) positions.push_back(p);
                for (const auto& p : positions) {
                    if (TdType(p["list"]) == "chatListMain") chat.order = TdInt(p["order"]);
                }
                if (type == "updateChatLastMessage") {
                    UltraIMMessage last;
                    chat.hasLast = update["last_message"].IsObject() && MessageFrom(update["last_message"], last);
                    if (chat.hasLast) chat.last = last;
                }
                if (wasListed && chat.order == 0) {
                    UltraIMEvent e;
                    e.type = UltraIMEventType::ConversationRemove;
                    e.providerId = providerId_;
                    e.conversationId = std::to_string(chat.id);
                    events.push_back(e);
                } else {
                    ConversationChanged(chat, events);
                }
            }
        } else if (type == "updateChatReadInbox") {
            auto it = chats_.find(TdInt(update["chat_id"]));
            if (it != chats_.end()) {
                it->second.unread = static_cast<int>(update["unread_count"].GetInteger(0));
                ConversationChanged(it->second, events);
            }
        } else if (type == "updateChatReadOutbox") {
            auto it = chats_.find(TdInt(update["chat_id"]));
            if (it != chats_.end()) {
                Chat& chat = it->second;
                chat.lastReadOutbox = TdInt(update["last_read_outbox_message_id"]);
                // Our messages up to there are read.
                for (auto& m : chat.recent) {
                    if (!m.isOwn || m.deliveryStatus == UltraIMDeliveryStatus::Read ||
                        ToId(m.messageId) > chat.lastReadOutbox)
                        continue;
                    m.deliveryStatus = UltraIMDeliveryStatus::Read;
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageDelivered;
                    e.providerId = providerId_;
                    e.conversationId = std::to_string(chat.id);
                    e.message = m;
                    e.deliveryStatus = UltraIMDeliveryStatus::Read;
                    events.push_back(e);
                }
            }
        } else if (type == "updateNewMessage") {
            const TdObject& message = update["message"];
            UltraIMMessage m;
            auto it = chats_.find(TdInt(message["chat_id"]));
            if (it != chats_.end() && MessageFrom(message, m)) {
                Remember(it->second, m);
                // Ours on its way out: the optimistic row stands for it until
                // updateMessageSendSucceeded.
                if (m.deliveryStatus != UltraIMDeliveryStatus::Sending) {
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageReceive;
                    e.providerId = providerId_;
                    e.conversationId = m.conversationId;
                    e.contactId = m.senderId;
                    e.message = m;
                    events.push_back(e);
                }
            }
        } else if (type == "updateMessageSendSucceeded" || type == "updateMessageSendFailed") {
            const TdObject& message = update["message"];
            const int64_t oldId = TdInt(update["old_message_id"]);
            UltraIMMessage m;
            auto it = chats_.find(TdInt(message["chat_id"]));
            if (it != chats_.end() && MessageFrom(message, m)) {
                Chat& chat = it->second;
                auto client = tempToClient_.find(oldId);
                const bool known = client != tempToClient_.end();
                m.clientMessageId = known ? client->second : m.messageId;
                if (known) tempToClient_.erase(client);
                const bool failed = type == "updateMessageSendFailed";
                if (failed) m.deliveryStatus = UltraIMDeliveryStatus::Failed;
                for (auto r = chat.recent.begin(); r != chat.recent.end(); ++r) {
                    if (r->messageId == std::to_string(oldId)) {
                        chat.recent.erase(r);
                        break;
                    }
                }
                Remember(chat, m);
                UltraIMEvent e;
                e.type = UltraIMEventType::MessageDelivered;
                e.providerId = providerId_;
                e.conversationId = m.conversationId;
                e.message = m;
                e.deliveryStatus = m.deliveryStatus;
                events.push_back(e);
                if (!failed) {
                    // And the message itself, as the echo of our own.
                    UltraIMEvent echo;
                    echo.type = UltraIMEventType::MessageReceive;
                    echo.providerId = providerId_;
                    echo.conversationId = m.conversationId;
                    echo.contactId = m.senderId;
                    echo.message = m;
                    events.push_back(echo);
                }
            }
        } else if (type == "updateMessageContent") {
            auto it = chats_.find(TdInt(update["chat_id"]));
            const std::string id = std::to_string(TdInt(update["message_id"]));
            if (it != chats_.end()) {
                for (auto& m : it->second.recent) {
                    if (m.messageId != id) continue;
                    const TdObject& content = update["new_content"];
                    const std::string c = TdType(content);
                    if (c == "messageText") m.text = content["text"]["text"].GetString();
                    else m.text = content["caption"]["text"].GetString(m.text);
                    m.isEdited = true;
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageUpdate;
                    e.providerId = providerId_;
                    e.conversationId = m.conversationId;
                    e.message = m;
                    events.push_back(e);
                    if (it->second.hasLast && it->second.last.messageId == id) it->second.last = m;
                    break;
                }
            }
        } else if (type == "updateDeleteMessages") {
            // Gone for good, not dropped from TDLib's cache.
            if (update["is_permanent"].GetBoolean(false) && !update["from_cache"].GetBoolean(false)) {
                const int64_t chatId = TdInt(update["chat_id"]);
                auto it = chats_.find(chatId);
                for (const auto& raw : update["message_ids"].GetElements()) {
                    const std::string id = std::to_string(TdInt(raw));
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageDelete;
                    e.providerId = providerId_;
                    e.conversationId = std::to_string(chatId);
                    e.message.messageId = id;
                    e.message.conversationId = e.conversationId;
                    if (it != chats_.end()) {
                        auto& recent = it->second.recent;
                        for (auto r = recent.begin(); r != recent.end(); ++r) {
                            if (r->messageId != id) continue;
                            e.message = *r;
                            recent.erase(r);
                            break;
                        }
                    }
                    events.push_back(e);
                }
            }
        } else if (type == "updateChatAction") {
            const TdObject& sender = update["sender_id"];
            const int64_t user = TdInt(sender["user_id"]);
            if (TdType(sender) == "messageSenderUser" && user != myId_) {
                UltraIMEvent e;
                e.type = UltraIMEventType::TypingChange;
                e.providerId = providerId_;
                e.conversationId = std::to_string(TdInt(update["chat_id"]));
                e.contactId = std::to_string(user);
                e.isTyping = TdType(update["action"]) == "chatActionTyping";
                events.push_back(e);
            }
        }
    }
    for (const auto& e : events) Emit(e);
}

// ============================================================================
// VIEWS OF THE STATE (mutex held)
// ============================================================================

TelegramProvider::Chat& TelegramProvider::StoreChat(const TdObject& object, std::vector<UltraIMEvent>& events) {
    const int64_t id = TdInt(object["id"]);
    Chat& chat = chats_[id];
    chat.id = id;
    chat.title = object["title"].GetString(chat.title);
    const TdObject& type = object["type"];
    const std::string t = TdType(type);
    if (t == "chatTypePrivate") {
        chat.kind = ChatKind::Private;
        chat.userId = TdInt(type["user_id"]);
    } else if (t == "chatTypeSecret") {
        chat.kind = ChatKind::Secret;
        chat.userId = TdInt(type["user_id"]);
    } else if (t == "chatTypeBasicGroup") {
        chat.kind = ChatKind::BasicGroup;
    } else if (t == "chatTypeSupergroup") {
        chat.kind = type["is_channel"].GetBoolean(false) ? ChatKind::Channel : ChatKind::Supergroup;
    }
    for (const auto& p : object["positions"].GetElements()) {
        if (TdType(p["list"]) == "chatListMain") chat.order = TdInt(p["order"]);
    }
    chat.unread = static_cast<int>(object["unread_count"].GetInteger(chat.unread));
    chat.lastReadOutbox = TdInt(object["last_read_outbox_message_id"], chat.lastReadOutbox);
    UltraIMMessage last;
    if (object["last_message"].IsObject() && MessageFrom(object["last_message"], last)) {
        chat.last = last;
        chat.hasLast = true;
    }
    ConversationChanged(chat, events);
    return chat;
}

void TelegramProvider::ConversationChanged(const Chat& chat, std::vector<UltraIMEvent>& events) const {
    if (chat.order == 0) return;        // not in the list (yet)
    UltraIMEvent e;
    e.type = UltraIMEventType::ConversationUpdate;
    e.providerId = providerId_;
    e.conversationId = std::to_string(chat.id);
    e.conversation = ConversationOf(chat);
    events.push_back(e);
}

std::string TelegramProvider::NameOf(int64_t userId) const {
    auto it = users_.find(userId);
    if (it == users_.end()) return std::to_string(userId);
    std::string name = it->second.firstName;
    if (!it->second.lastName.empty()) name += (name.empty() ? "" : " ") + it->second.lastName;
    if (name.empty()) name = it->second.username.empty() ? std::to_string(userId) : it->second.username;
    return name;
}

bool TelegramProvider::MessageFrom(const TdObject& message, UltraIMMessage& out) const {
    out = UltraIMMessage();
    const int64_t chatId = TdInt(message["chat_id"]);
    out.messageId = std::to_string(TdInt(message["id"]));
    out.clientMessageId = out.messageId;
    out.providerId = providerId_;
    out.conversationId = std::to_string(chatId);
    const TdObject& sender = message["sender_id"];
    if (TdType(sender) == "messageSenderUser") {
        const int64_t user = TdInt(sender["user_id"]);
        out.senderId = std::to_string(user);
        out.senderName = NameOf(user);
    } else {
        const int64_t from = TdInt(sender["chat_id"]);
        out.senderId = std::to_string(from);
        auto c = chats_.find(from);
        out.senderName = c != chats_.end() ? c->second.title : out.senderId;
    }
    out.timestampMs = message["date"].GetInteger(0) * 1000;
    out.isEdited = message["edit_date"].GetInteger(0) > 0;
    out.isOwn = message["is_outgoing"].GetBoolean(false);
    const std::string sending = TdType(message["sending_state"]);
    out.deliveryStatus = sending == "messageSendingStatePending" ? UltraIMDeliveryStatus::Sending
                       : sending == "messageSendingStateFailed"  ? UltraIMDeliveryStatus::Failed
                                                                 : UltraIMDeliveryStatus::Sent;
    auto chat = chats_.find(chatId);
    if (out.isOwn && out.deliveryStatus == UltraIMDeliveryStatus::Sent && chat != chats_.end() &&
        TdInt(message["id"]) <= chat->second.lastReadOutbox)
        out.deliveryStatus = UltraIMDeliveryStatus::Read;

    const TdObject& content = message["content"];
    const std::string type = TdType(content);
    const std::string caption = content["caption"]["text"].GetString();
    const std::string who = out.senderName;
    if (type == "messageText") {
        out.text = content["text"]["text"].GetString();
    } else if (type == "messagePhoto") {
        out.type = UltraIMMessageType::Image;
        out.text = caption;
    } else if (type == "messageDocument") {
        out.type = UltraIMMessageType::File;
        out.attachment.fileName = content["document"]["file_name"].GetString();
        out.attachment.mimeType = content["document"]["mime_type"].GetString();
        out.text = caption.empty() ? out.attachment.fileName : caption;
    } else if (type == "messageVideo" || type == "messageAnimation" || type == "messageVideoNote") {
        out.type = UltraIMMessageType::Video;
        out.text = caption;
    } else if (type == "messageAudio") {
        out.type = UltraIMMessageType::Audio;
        out.text = content["audio"]["title"].GetString(caption);
    } else if (type == "messageVoiceNote") {
        out.type = UltraIMMessageType::Audio;
        out.text = caption.empty() ? std::string("voice message") : caption;
    } else if (type == "messageSticker") {
        out.type = UltraIMMessageType::Sticker;
        out.text = content["sticker"]["emoji"].GetString();
    } else if (type == "messageBasicGroupChatCreate" || type == "messageSupergroupChatCreate") {
        out.type = UltraIMMessageType::SystemEvent;
        out.text = who + " created the group \"" + content["title"].GetString() + "\"";
    } else if (type == "messageChatChangeTitle") {
        out.type = UltraIMMessageType::SystemEvent;
        out.text = who + " named the group \"" + content["title"].GetString() + "\"";
    } else if (type == "messageChatAddMembers") {
        out.type = UltraIMMessageType::SystemEvent;
        std::string names;
        for (const auto& u : content["member_user_ids"].GetElements())
            names += (names.empty() ? "" : ", ") + NameOf(TdInt(u));
        out.text = who + " added " + names;
    } else if (type == "messageChatJoinByLink" || type == "messageChatJoinByRequest") {
        out.type = UltraIMMessageType::SystemEvent;
        out.text = who + " joined";
    } else if (type == "messageChatDeleteMember") {
        out.type = UltraIMMessageType::SystemEvent;
        const int64_t user = TdInt(content["user_id"]);
        out.text = user == TdInt(sender["user_id"]) ? who + " left" : who + " removed " + NameOf(user);
    } else if (type == "messageContactRegistered") {
        out.type = UltraIMMessageType::SystemEvent;
        out.text = who + " joined Telegram";
    } else {
        out.text = "[" + (type.size() > 7 ? type.substr(7) : type) + " - not shown in ChatCube yet]";
    }

    const TdObject& reply = message["reply_to"];
    if (TdType(reply) == "messageReplyToMessage" && TdInt(reply["chat_id"], chatId) == chatId) {
        out.replyTo.messageId = std::to_string(TdInt(reply["message_id"]));
        if (chat != chats_.end()) {
            for (const auto& m : chat->second.recent) {
                if (m.messageId != out.replyTo.messageId) continue;
                out.replyTo.senderId = m.senderId;
                out.replyTo.senderName = m.senderName;
                out.replyTo.textSnippet = SnippetOf(m).substr(0, 80);
                break;
            }
        }
    }
    return true;
}

void TelegramProvider::Remember(Chat& chat, const UltraIMMessage& message) {
    for (auto& m : chat.recent) {
        if (m.messageId == message.messageId) {
            m = message;
            return;
        }
    }
    auto at = std::upper_bound(chat.recent.begin(), chat.recent.end(), message,
                               [](const UltraIMMessage& a, const UltraIMMessage& b) {
                                   return a.timestampMs < b.timestampMs;
                               });
    chat.recent.insert(at, message);
    while (chat.recent.size() > kRecentCap) chat.recent.pop_front();
    if (!chat.hasLast || message.timestampMs >= chat.last.timestampMs) {
        chat.last = message;
        chat.hasLast = true;
    }
}

UltraIMConversation TelegramProvider::ConversationOf(const Chat& chat) const {
    UltraIMConversation c;
    c.conversationId = std::to_string(chat.id);
    c.providerId = providerId_;
    c.title = chat.title;
    if (chat.kind == ChatKind::Private || chat.kind == ChatKind::Secret) {
        c.directContactId = std::to_string(chat.userId);
        if (c.title.empty()) c.title = NameOf(chat.userId);
        if (myId_) c.memberIds.push_back(std::to_string(myId_));
        c.memberIds.push_back(c.directContactId);
        auto u = users_.find(chat.userId);
        if (u != users_.end()) c.presence = u->second.presence;
    }
    c.isGroup = c.directContactId.empty();
    c.isEncrypted = chat.kind == ChatKind::Secret;
    if (chat.hasLast) {
        c.lastMessageSnippet = SnippetOf(chat.last);
        c.lastMessageTimeMs = chat.last.timestampMs;
    }
    c.unreadCount = chat.unread;
    return c;
}

// ============================================================================
// DATA
// ============================================================================

void TelegramProvider::FetchConversations(UltraIMConversationsCallback onResult) {
    std::vector<std::pair<int64_t, UltraIMConversation>> listed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : chats_) {
            if (entry.second.order != 0) listed.push_back({entry.second.order, ConversationOf(entry.second)});
        }
    }
    // TDLib's own order: pinned first, then by the last message.
    std::sort(listed.begin(), listed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<UltraIMConversation> list;
    for (auto& entry : listed) list.push_back(std::move(entry.second));
    onResult(UltraIMResult::Ok(), list);
}

void TelegramProvider::FetchContacts(UltraIMContactsCallback onResult) {
    std::vector<UltraIMContact> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : chats_) {
            const Chat& chat = entry.second;
            if (chat.kind != ChatKind::Private || chat.order == 0) continue;
            UltraIMContact contact;
            contact.contactId = std::to_string(chat.userId);
            contact.providerId = providerId_;
            contact.displayName = NameOf(chat.userId);
            auto u = users_.find(chat.userId);
            if (u != users_.end()) contact.presence = u->second.presence;
            out.push_back(contact);
        }
    }
    onResult(UltraIMResult::Ok(), out);
}

UltraIMResult TelegramProvider::Connected(int64_t& chatId, const std::string& conversationId) {
    if (!Client()->IsOpen() || state_ == UltraIMConnectionState::Disconnected || GetLocalUserId().empty())
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Telegram");
    chatId = ToId(conversationId);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!chatId || !chats_.count(chatId)) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such chat");
    return UltraIMResult::Ok();
}

void TelegramProvider::FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                                     UltraIMMessagesCallback onResult) {
    int64_t chatId = 0;
    const UltraIMResult r = Connected(chatId, conversationId);
    if (!r) {
        onResult(r, {});
        return;
    }
    if (limit <= 0) limit = 50;
    // TDLib answers a page with what it has at hand, often fewer than asked:
    // ask again from the oldest until the page is full or history ends.
    struct Page {
        std::vector<UltraIMMessage> messages;   // newest first
        int64_t before = 0;
        int wanted = 0;
        int rounds = 0;
    };
    auto page = std::make_shared<Page>();
    page->before = ToId(beforeMessageId);
    page->wanted = limit;
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, chatId, page, next, onResult]() {
        const int64_t from = page->messages.empty() ? page->before : ToId(page->messages.back().messageId);
        const int ask = std::min(100, page->wanted - static_cast<int>(page->messages.size()) + 1);
        Client()->Send(Request::GetChatHistory(chatId, from, ask), [this, chatId, page, next, onResult](const TdObject& a) {
            if (IsError(a)) {
                onResult(UltraIMResult::Error(UltraIMResultCode::NetworkError, ErrorText(a)), {});
                *next = nullptr;
                return;
            }
            size_t added = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto chat = chats_.find(chatId);
                for (const auto& raw : a["messages"].GetElements()) {
                    UltraIMMessage m;
                    if (!MessageFrom(raw, m)) continue;
                    // from_message_id itself comes back too.
                    const int64_t id = ToId(m.messageId);
                    const int64_t bound =
                        page->messages.empty() ? page->before : ToId(page->messages.back().messageId);
                    if (bound && id >= bound) continue;
                    page->messages.push_back(m);
                    if (chat != chats_.end()) Remember(chat->second, m);
                    ++added;
                }
            }
            if (added && static_cast<int>(page->messages.size()) < page->wanted && ++page->rounds < 10) {
                (*next)();
                return;
            }
            std::vector<UltraIMMessage> out(page->messages.rbegin(), page->messages.rend());
            if (static_cast<int>(out.size()) > page->wanted)
                out.erase(out.begin(), out.begin() + (static_cast<std::ptrdiff_t>(out.size()) - page->wanted));
            onResult(UltraIMResult::Ok(), out);
            *next = nullptr;
        });
    };
    (*next)();
}

void TelegramProvider::ResolveUser(const std::string& address,
                                   std::function<void(const UltraIMResult&, int64_t)> done) {
    const std::string a = Trim(address);
    if (const int64_t id = ToId(a); id > 0 && a[0] != '+' && a.size() < 15) {
        // A user id we know already.
        std::lock_guard<std::mutex> lock(mutex_);
        if (users_.count(id)) {
            done(UltraIMResult::Ok(), id);
            return;
        }
    }
    if (IsPhone(a)) {
        std::string digits;
        for (char c : a) {
            if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
        }
        Client()->Send(Request::SearchUserByPhone(digits), [done, a](const TdObject& user) {
            if (IsError(user)) {
                done(UltraIMResult::Error(UltraIMResultCode::NotFound, "Nobody on Telegram has the number " + a), 0);
                return;
            }
            done(UltraIMResult::Ok(), TdInt(user["id"]));
        });
        return;
    }
    const std::string name = Username(a);
    if (name.empty()) {
        done(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                  "A Telegram contact is a @username or a phone number with its country code"),
             0);
        return;
    }
    Client()->Send(Request::SearchPublicChat(name), [done, name](const TdObject& chat) {
        if (IsError(chat) || TdType(chat["type"]) != "chatTypePrivate") {
            done(UltraIMResult::Error(UltraIMResultCode::NotFound, "No Telegram user is called @" + name), 0);
            return;
        }
        done(UltraIMResult::Ok(), TdInt(chat["type"]["user_id"]));
    });
}

void TelegramProvider::AddContact(const std::string& address, UltraIMConversationCallback onResult) {
    if (!Client()->IsOpen() || GetLocalUserId().empty()) {
        onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Telegram"), {});
        return;
    }
    ResolveUser(address, [this, onResult](const UltraIMResult& r, int64_t userId) {
        if (!r) return onResult(r, {});
        if (std::to_string(userId) == GetLocalUserId())
            return onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "That is you"), {});
        Client()->Send(Request::CreatePrivateChat(userId), [this, onResult](const TdObject& chat) {
            if (IsError(chat)) return onResult(UltraIMResult::Error(UltraIMResultCode::Unknown, ErrorText(chat)), {});
            std::vector<UltraIMEvent> events;
            UltraIMConversation c;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                Chat& stored = StoreChat(chat, events);
                c = ConversationOf(stored);
            }
            for (const auto& e : events) Emit(e);
            onResult(UltraIMResult::Ok(), c);
        });
    });
}

void TelegramProvider::CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                                   UltraIMConversationCallback onResult) {
    if (!Client()->IsOpen() || GetLocalUserId().empty()) {
        onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Telegram"), {});
        return;
    }
    if (Trim(title).empty()) {
        onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "A group needs a name"), {});
        return;
    }
    // Everyone resolved one after another, then the group.
    struct Members {
        std::vector<std::string> addresses;
        std::vector<int64_t> ids;
    };
    auto members = std::make_shared<Members>();
    members->addresses = memberIds;
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, title, members, next, onResult]() {
        if (members->ids.size() < members->addresses.size()) {
            ResolveUser(members->addresses[members->ids.size()], [members, next, onResult](const UltraIMResult& r, int64_t id) {
                if (!r) {
                    onResult(r, {});
                    *next = nullptr;
                    return;
                }
                members->ids.push_back(id);
                (*next)();
            });
            return;
        }
        Client()->Send(Request::CreateBasicGroup(members->ids, Trim(title)), [this, next, onResult](const TdObject& created) {
            *next = nullptr;    // (not from inside itself: its captures would go with it)
            if (IsError(created))
                return onResult(UltraIMResult::Error(UltraIMResultCode::Unknown, ErrorText(created)), {});
            // TDLib 1.8.32 and later answer createdBasicGroupChat, earlier ones the chat.
            const int64_t chatId = TdType(created) == "chat" ? TdInt(created["id"]) : TdInt(created["chat_id"]);
            Client()->Send(Request::GetChat(chatId), [this, onResult](const TdObject& chat) {
                if (IsError(chat)) return onResult(UltraIMResult::Error(UltraIMResultCode::Unknown, ErrorText(chat)), {});
                std::vector<UltraIMEvent> events;
                UltraIMConversation c;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    c = ConversationOf(StoreChat(chat, events));
                }
                for (const auto& e : events) Emit(e);
                onResult(UltraIMResult::Ok(), c);
            });
        });
    };
    (*next)();
}

// ============================================================================
// SENDING
// ============================================================================

UltraIMResult TelegramProvider::SendText(const std::string& conversationId, const std::string& text,
                                         const UltraIMSendOptions& options) {
    int64_t chatId = 0;
    const UltraIMResult r = Connected(chatId, conversationId);
    if (!r) return r;
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    Client()->Send(Request::SendText(chatId, text, ToId(options.replyToMessageId)),
                  [this, conversationId, clientId](const TdObject& sent) {
        if (IsError(sent)) {
            UltraIMEvent e;
            e.type = UltraIMEventType::MessageDelivered;
            e.providerId = providerId_;
            e.conversationId = conversationId;
            e.message.conversationId = conversationId;
            e.message.clientMessageId = clientId;
            e.message.messageId = clientId;
            e.deliveryStatus = UltraIMDeliveryStatus::Failed;
            Emit(e);
            return;
        }
        // The message as TDLib holds it while it goes out: its temporary
        // id leads updateMessageSendSucceeded back to our client id.
        std::lock_guard<std::mutex> lock(mutex_);
        tempToClient_[TdInt(sent["id"])] = clientId;
    });
    return UltraIMResult::Ok();
}

UltraIMResult TelegramProvider::EditMessage(const std::string& conversationId, const std::string& messageId,
                                            const std::string& newText) {
    int64_t chatId = 0;
    const UltraIMResult r = Connected(chatId, conversationId);
    if (!r) return r;
    // The edit arrives back as updateMessageContent: a MessageUpdate.
    Client()->Send(Request::EditText(chatId, ToId(messageId), newText));
    return UltraIMResult::Ok();
}

UltraIMResult TelegramProvider::DeleteMessage(const std::string& conversationId, const std::string& messageId) {
    int64_t chatId = 0;
    const UltraIMResult r = Connected(chatId, conversationId);
    if (!r) return r;
    Client()->Send(Request::DeleteMessages(chatId, {ToId(messageId)}, true));
    return UltraIMResult::Ok();
}

UltraIMResult TelegramProvider::MarkRead(const std::string& conversationId, const std::string& upToMessageId) {
    int64_t chatId = 0;
    const UltraIMResult r = Connected(chatId, conversationId);
    if (!r) return r;
    int64_t upTo = ToId(upToMessageId);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Chat& chat = chats_[chatId];
        if (!upTo && chat.hasLast) upTo = ToId(chat.last.messageId);
    }
    if (!upTo) return UltraIMResult::Ok();
    Client()->Send(Request::ViewMessages(chatId, {upTo}));
    return UltraIMResult::Ok();
}

UltraIMResult TelegramProvider::SetTyping(const std::string& conversationId, bool isTyping) {
    int64_t chatId = 0;
    if (!Connected(chatId, conversationId)) return UltraIMResult::Ok();
    Client()->Send(Request::SendTyping(chatId, isTyping));
    return UltraIMResult::Ok();
}

} // namespace UltraIMTelegram
