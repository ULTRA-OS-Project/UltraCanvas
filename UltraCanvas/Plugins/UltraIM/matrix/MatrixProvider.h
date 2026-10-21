// Plugins/UltraIM/matrix/MatrixProvider.h
// Matrix as an UltraIM provider: password sign-in (with .well-known
// discovery), the session kept in a DeviceKeyVault, /sync as a long poll,
// direct chats (m.direct) and groups, invitations, text with replies, edits
// and redactions, read receipts, typing, presence, and history paged back
// with /messages. End-to-end encrypted rooms are read and written through
// MatrixCrypto (Olm / Megolm, vodozemac), and new rooms are created
// encrypted when it is built in.
// Version: 0.2.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "MatrixCrypto.h"
#include "MatrixHttp.h"

#include "UltraIM/UltraIMPlugins.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraIMMatrix {

struct MatrixTimings {
    int syncTimeoutMs = 30000;      // how long one /sync waits on the server for news
    int initialTimelineLimit = 50;  // events per room on the first sync
};

class MatrixProvider : public IUltraIMProviderPlugin {
public:
    explicit MatrixProvider(std::string providerId = "matrix");
    ~MatrixProvider() override;

    std::string GetProviderId() const override { return providerId_; }
    std::string GetDisplayName() const override { return "Matrix"; }
    std::string GetVersion() const override { return "0.2.0"; }
    UltraIMCapabilityFlags GetCapabilities() const override;
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::UsernamePassword; }

    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override;
    void Shutdown() override;

    // credentials.server: the homeserver ("matrix.org", "https://hs.example",
    // or "" with a full "@user:server" as the username); username and
    // password. With neither, the stored session is resumed. An access token
    // (credentials.accessToken) signs in without a password.
    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override;
    // Disconnects; the session is kept for the next sign-in.
    void SignOut() override;
    UltraIMConnectionState GetConnectionState() const override { return state_.load(); }
    std::string GetLocalUserId() const override;
    bool HasStoredSession() const override;

    void FetchConversations(UltraIMConversationsCallback onResult) override;
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override;
    void FetchContacts(UltraIMContactsCallback onResult) override;
    // address: "@user:server" - opens (or finds) a direct chat with them.
    void AddContact(const std::string& address, UltraIMConversationCallback onResult) override;
    void CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                     UltraIMConversationCallback onResult) override;

    // In an invitation, sending accepts it first.
    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override;
    UltraIMResult EditMessage(const std::string& conversationId, const std::string& messageId,
                              const std::string& newText) override;
    UltraIMResult DeleteMessage(const std::string& conversationId, const std::string& messageId) override;
    UltraIMResult MarkRead(const std::string& conversationId, const std::string& upToMessageId) override;
    UltraIMResult SetTyping(const std::string& conversationId, bool isTyping) override;

    // The Element-compatible room-key export (MatrixCrypto::ExportRoomKeys).
    void ExportEncryptionKeys(const std::string& passphrase, UltraIMKeyExportCallback onResult) override;
    void ImportEncryptionKeys(const std::string& data, const std::string& passphrase,
                              UltraIMKeyImportCallback onResult) override;

    // ---- for tests ----------------------------------------------------------
    void SetTimings(const MatrixTimings& timings) { timings_ = timings; }
    std::string GetHomeserverUrl() const;
    std::string GetDeviceId() const;
    // Whether this session encrypts (vodozemac built in and the key store open).
    bool IsEncryptionReady() const { return crypto_.IsOpen(); }
    // Why it does not, when it does not.
    std::string GetEncryptionError() const;
    std::vector<MatrixDeviceInfo> GetDevicesOf(const std::string& userId) const { return crypto_.DevicesOf(userId); }
    // Rounds of the export's passphrase stretching (PBKDF2); fewer in tests.
    void SetKeyExportRounds(uint32_t rounds) { exportRounds_ = rounds; }

private:
    struct Room {
        std::string id;
        std::string name;                       // m.room.name
        std::string alias;                      // m.room.canonical_alias
        std::map<std::string, std::string> members;    // joined / invited user -> display name
        std::set<std::string> joined;
        std::vector<std::string> heroes;
        bool invited = false;                   // an invitation to us, not joined yet
        std::string inviter;
        bool encrypted = false;
        MatrixRoomEncryption encryption;        // m.room.encryption's rotation settings
        // Everyone joined or invited, from /members: whom an encrypted
        // message's key goes to (the sync's members are lazily loaded).
        std::set<std::string> recipients;
        bool recipientsKnown = false;
        std::vector<UltraIMMessage> timeline;   // oldest first
        std::string prevBatch;                  // where /messages pages back from
        bool historyComplete = false;
        int unread = 0;
        std::map<std::string, std::string> receipts;   // user -> event read up to
        std::set<std::string> typing;
    };

    // ---- helpers --------------------------------------------------------------
    void Emit(const UltraIMEvent& event);
    void SetState(UltraIMConnectionState state, const std::string& error = std::string());
    std::shared_ptr<MatrixHttp> Http() const;
    bool SaveSession(const std::string& url, const std::string& userId, const std::string& deviceId,
                     const std::string& token);
    bool LoadSession(std::string& url, std::string& userId, std::string& deviceId, std::string& token) const;
    // Finds the client-server API for a server name or URL (.well-known).
    std::string DiscoverBaseUrl(const std::string& server);
    void StartSession(const std::string& url, const std::string& userId, const std::string& token,
                      UltraIMResultCallback onResult);

    // ---- sync -----------------------------------------------------------------
    void SyncLoop();
    void ApplySync(const UltraCanvas::JSONValue& body, bool initial);
    // Mutex held: one room's part of a sync; events to emit are appended.
    void ApplyRoom(const std::string& roomId, const UltraCanvas::JSONValue& room, bool invited, bool initial,
                   std::vector<UltraIMEvent>& events);
    // Mutex held: a timeline event into the room; false when it was not a message.
    bool ApplyEvent(Room& room, const UltraCanvas::JSONValue& event, bool live, std::vector<UltraIMEvent>& events);
    void ApplyState(Room& room, const UltraCanvas::JSONValue& event);
    bool MessageFrom(const Room& room, const UltraCanvas::JSONValue& event, UltraIMMessage& out) const;
    void ApplyReceipts(Room& room, const UltraCanvas::JSONValue& content, std::vector<UltraIMEvent>& events);

    // ---- mutex held -------------------------------------------------------------
    UltraIMConversation ConversationOf(const Room& room) const;
    std::string DirectUserOf(const Room& room) const;
    std::string NameOf(const Room& room, const std::string& userId) const;

    void SetDirect(const std::string& userId, const std::string& roomId);

    // ---- encryption -------------------------------------------------------------
    void OpenCrypto(const std::string& userId, const std::string& deviceId);
    std::string CryptoStorePath(const std::string& userId, const std::string& deviceId) const;
    // Mutex held: an encrypted event in the clear (true), or the placeholder
    // text to show for it; one whose key may still come is kept for then.
    bool DecryptForTimeline(Room& room, const UltraCanvas::JSONValue& event, UltraCanvas::JSONValue& clear,
                            std::string& placeholder);
    // The events held for keys that have now arrived.
    void RetryPending(const std::vector<MatrixSessionRef>& arrived);
    // On the send thread: (type, content) into an encrypted room, waiting.
    bool SendEncrypted(const std::shared_ptr<MatrixHttp>& http, const std::string& roomId, const std::string& txnId,
                       const std::string& type, const UltraCanvas::JSONValue& content, std::string& eventId,
                       std::string& error);
    // Jobs that wait (key sharing, joining before sending) run one after
    // another, in order, on the send thread.
    void Enqueue(std::function<void()> job);
    void SendLoop();

    std::string     providerId_;
    UltraIMConfig   config_;
    UltraIMEventSink sink_;
    MatrixTimings   timings_;
    std::atomic<UltraIMConnectionState> state_{UltraIMConnectionState::Disconnected};

    mutable UltraVault::DeviceKeyVault vault_;
    bool            vaultReady_ = false;

    mutable std::mutex httpMutex_;
    std::shared_ptr<MatrixHttp> http_;      // one per session

    mutable std::mutex mutex_;
    std::string     userId_;
    std::string     deviceId_;
    std::string     cryptoError_;
    std::string     nextBatch_;
    std::map<std::string, Room> rooms_;
    std::map<std::string, std::vector<std::string>> direct_;    // m.direct: user -> rooms
    std::map<std::string, UltraIMPresence> presence_;
    std::map<std::string, std::string> txnToClient_;            // our transaction id -> client message id

    std::thread     syncThread_;
    std::atomic<bool> stopping_{false};

    MatrixCrypto    crypto_;
    uint32_t        exportRounds_ = 500000;
    // room "|" session id -> events waiting for that key (mutex_)
    std::map<std::string, std::vector<UltraCanvas::JSONValue>> pendingDecrypt_;

    std::mutex      jobMutex_;
    std::condition_variable jobChanged_;
    std::deque<std::function<void()>> jobs_;
    bool            jobsStopping_ = false;
    std::thread     sendThread_;
};

} // namespace UltraIMMatrix
