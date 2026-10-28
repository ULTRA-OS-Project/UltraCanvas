// Plugins/UltraIM/telegram/TelegramProvider.h
// Telegram as an UltraIM provider, on TDLib (Telegram Database Library,
// Telegram's own client library)
// through its JSON interface, loaded at run time (TdJson.h). Sign-in in steps
// - phone number, the code Telegram sends, the two-step password when there
// is one, a name for a new account - and TDLib's database (encrypted with a
// key the vault keeps) for the session, the chats and the history. Chats of
// the main list, private chats and groups, text with replies, edits,
// deletion, read receipts, typing, presence, history, finding people by
// username or phone number, new groups.
// Version: 0.1.1 - TDLib's connection state reported while signing in; a step TDLib does not
//                  answer times out instead of hanging; TDLib's own log in the data folder
// Version: 0.1.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework
#pragma once

#include "TdJson.h"

#include "UltraIM/UltraIMPlugins.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraIMTelegram {

class TelegramProvider : public IUltraIMProviderPlugin {
public:
    explicit TelegramProvider(std::string providerId = "telegram");
    ~TelegramProvider() override;

    std::string GetProviderId() const override { return providerId_; }
    std::string GetDisplayName() const override { return "Telegram"; }
    std::string GetVersion() const override { return "0.1.0"; }
    UltraIMCapabilityFlags GetCapabilities() const override;
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::PhoneOTP; }

    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override;
    void Shutdown() override;

    // In steps, each answering NeedsInput until the last answers Ok:
    // credentials.phoneNumber ("+44..."), then otpCode, then password (two-step
    // verification), or displayName for a new account - GetAuthStep() says
    // which. credentials.apiId / apiHash: the application's, from
    // my.telegram.org (else the ones built in or kept from the last sign-in).
    // credentials.server "test" uses Telegram's test servers. With nothing
    // but the stored session it resumes.
    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override;
    // Closes TDLib; the session stays for the next sign-in.
    void SignOut() override;
    UltraIMConnectionState GetConnectionState() const override { return state_.load(); }
    std::string GetLocalUserId() const override;
    bool HasStoredSession() const override;
    UltraIMAuthStep GetAuthStep() const override;
    std::string GetAuthHint() const override;

    void FetchConversations(UltraIMConversationsCallback onResult) override;
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override;
    void FetchContacts(UltraIMContactsCallback onResult) override;
    // address: "@username", "username", "t.me/username" or "+phone".
    void AddContact(const std::string& address, UltraIMConversationCallback onResult) override;
    // memberIds: addresses as AddContact takes them, or user ids.
    void CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                     UltraIMConversationCallback onResult) override;

    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override;
    UltraIMResult EditMessage(const std::string& conversationId, const std::string& messageId,
                              const std::string& newText) override;
    UltraIMResult DeleteMessage(const std::string& conversationId, const std::string& messageId) override;
    UltraIMResult MarkRead(const std::string& conversationId, const std::string& upToMessageId) override;
    UltraIMResult SetTyping(const std::string& conversationId, bool isTyping) override;

    // ---- for tests ----------------------------------------------------------------
    // Where libtdjson is (tried before the usual places).
    void SetLibraryPath(const std::string& path) { libraryPath_ = path; }
    // How long a sign-in step waits for TDLib's answer before it fails with
    // Timeout (the network is down, say, and TDLib keeps trying in silence).
    void SetStepTimeout(int ms) { stepTimeoutMs_ = ms; }

private:
    enum class ChatKind { Private, BasicGroup, Supergroup, Channel, Secret };

    struct Chat {
        int64_t     id = 0;
        ChatKind    kind = ChatKind::Private;
        int64_t     userId = 0;          // private and secret chats: the other person
        std::string title;
        int64_t     order = 0;           // in the main list; 0 = not in it
        int         unread = 0;
        int64_t     lastReadOutbox = 0;  // our messages up to here are read
        UltraIMMessage last;
        bool        hasLast = false;
        std::deque<UltraIMMessage> recent;   // the newest, oldest first
    };

    struct User {
        int64_t     id = 0;
        std::string firstName;
        std::string lastName;
        std::string username;
        std::string phone;
        UltraIMPresence presence = UltraIMPresence::Unknown;
    };

    // ---- receive thread -----------------------------------------------------------
    void OnUpdate(const TdObject& update);
    void OnAuthorizationState(const TdObject& state);
    void OnAuthAnswer(const TdObject& answer);
    void SetStep(UltraIMAuthStep step, const std::string& hint);

    // ---- any thread -----------------------------------------------------------------
    void Emit(const UltraIMEvent& event);
    void SetState(UltraIMConnectionState state, const std::string& error = std::string());
    // Resolves the SignIn waiting for an answer.
    void Finish(const UltraIMResult& result);
    // The watchdog over a sign-in step: armed with what is waited for, it
    // fails the pending SignIn with Timeout when TDLib has not answered by
    // stepTimeoutMs_; every Finish disarms it.
    void ArmStep(const std::string& what);
    void DisarmStep();
    void WatchdogLoop();
    void OnStepTimeout(const std::string& what);
    // TDLib's connection, in words for the status line.
    std::string ConnectionText() const;
    bool SaveSession(int64_t userId);
    bool LoadSession(int32_t& apiId, std::string& apiHash, bool& testDc, std::string& phone) const;
    std::string DatabaseKey();
    std::string DataPath(const std::string& name) const;
    // address -> user id (callback on the receive thread).
    void ResolveUser(const std::string& address, std::function<void(const UltraIMResult&, int64_t)> done);
    UltraIMResult Connected(int64_t& chatId, const std::string& conversationId);

    // ---- mutex held -------------------------------------------------------------------
    Chat& StoreChat(const TdObject& chat, std::vector<UltraIMEvent>& events);
    bool MessageFrom(const TdObject& message, UltraIMMessage& out) const;
    UltraIMConversation ConversationOf(const Chat& chat) const;
    std::string NameOf(int64_t userId) const;
    void Remember(Chat& chat, const UltraIMMessage& message);
    void ConversationChanged(const Chat& chat, std::vector<UltraIMEvent>& events) const;

    std::string     providerId_;
    UltraIMConfig   config_;
    UltraIMEventSink sink_;
    std::string     libraryPath_;
    std::atomic<UltraIMConnectionState> state_{UltraIMConnectionState::Disconnected};
    int             stepTimeoutMs_ = 45000;

    std::thread     watchdog_;
    std::mutex      watchMutex_;
    std::condition_variable watchCv_;
    bool            watchStop_ = false;
    bool            watchArmed_ = false;
    std::chrono::steady_clock::time_point watchDeadline_;
    std::string     watchWhat_;

    mutable UltraVault::DeviceKeyVault vault_;
    bool            vaultReady_ = false;

    // The session's client; Client() never gives null (a closed one answers
    // every request with an error), so a handler racing SignOut is safe.
    std::shared_ptr<TdClient> Client() const;
    mutable std::mutex clientMutex_;
    std::shared_ptr<TdClient> client_;

    mutable std::mutex mutex_;
    UltraIMResultCallback pendingSignIn_;
    UltraIMAuthStep authStep_ = UltraIMAuthStep::Idle;
    std::string     authHint_;
    bool            ready_ = false;
    bool            closing_ = false;
    std::string     connection_;     // TDLib's connectionState* while not ready, "" = none yet
    std::string     phone_;
    int32_t         apiId_ = 0;
    std::string     apiHash_;
    bool            testDc_ = false;
    int64_t         myId_ = 0;
    std::map<int64_t, Chat> chats_;
    std::map<int64_t, User> users_;
    std::map<int64_t, std::string> tempToClient_;    // a message being sent -> its client id
};

} // namespace UltraIMTelegram
