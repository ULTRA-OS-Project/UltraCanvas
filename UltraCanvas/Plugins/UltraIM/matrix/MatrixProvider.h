// Plugins/UltraIM/matrix/MatrixProvider.h
// Matrix as an UltraIM provider: password sign-in (with .well-known
// discovery), the session kept in a DeviceKeyVault, /sync as a long poll,
// direct chats (m.direct) and groups, invitations, text with replies, edits
// and redactions, read receipts, typing, presence, and history paged back
// with /messages. End-to-end encrypted rooms are shown as such and cannot
// be written in yet (no Olm / Megolm).
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "MatrixHttp.h"

#include "UltraIM/UltraIMPlugins.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"

#include <atomic>
#include <condition_variable>
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
    std::string GetVersion() const override { return "0.1.0"; }
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

    // ---- for tests ----------------------------------------------------------
    void SetTimings(const MatrixTimings& timings) { timings_ = timings; }
    std::string GetHomeserverUrl() const;

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
    std::string     nextBatch_;
    std::map<std::string, Room> rooms_;
    std::map<std::string, std::vector<std::string>> direct_;    // m.direct: user -> rooms
    std::map<std::string, UltraIMPresence> presence_;
    std::map<std::string, std::string> txnToClient_;            // our transaction id -> client message id

    std::thread     syncThread_;
    std::atomic<bool> stopping_{false};
};

} // namespace UltraIMMatrix
