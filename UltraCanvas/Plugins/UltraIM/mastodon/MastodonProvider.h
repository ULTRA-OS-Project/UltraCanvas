// Plugins/UltraIM/mastodon/MastodonProvider.h
// Mastodon - an UltraIM provider for the direct messages of a Mastodon (or
// compatible) server, over its REST API through UltraNet.
//
// Mastodon has no messenger of its own: a "chat" is a thread of statuses
// whose visibility is "direct", seen only by the accounts they mention; the
// server groups them into conversations (GET /api/v1/conversations). Here:
//
//   - a conversation's id is the set of the other accounts in it (their full
//     addresses, "user@server" in lower case, sorted, space-joined), so a
//     chat opened before its first message
//     (AddContact, CreateGroup) and the conversation that message makes are
//     the same; the server's conversations of the same people are one chat;
//   - a message is a status: its HTML content as plain text, the mentions it
//     starts with left out; its first media attachment as the picture,
//     video, sound or file;
//   - sending posts a direct status that mentions everyone in the chat,
//     replying to the chat's last status so the thread stays one (or to the
//     message answered: then it shows as a reply);
//   - news comes by polling the conversations (MastodonTimings::pollMs); a
//     conversation whose last status changed brings every status of its
//     threads not seen yet (a status that answers nothing starts a thread;
//     a chat is its threads together, the newest eight read);
//   - edit (PUT /api/v1/statuses/:id, Mastodon 4.0+), delete for everyone,
//     read (POST /api/v1/conversations/:id/read), leave (the conversation
//     taken off the list); no typing, read receipts, presence or calls - the
//     server has none.
//
// Sign-in is OAuth in the browser (UltraIMLoginType::SsoOidc): ChatCube is
// registered with the server once (POST /api/v1/apps), the server's consent
// page is opened - AuthStepChange with UltraIMAuthStep::Browser, the page's
// address in errorText - and the answer comes back to a loopback address
// (http://127.0.0.1:47819/callback). The access token is kept in UltraVault;
// credentials.accessToken signs in with one directly.
//
// Version: 0.1.1 - the accounts' pictures fetched and shown (QueueAvatar, AvatarOf)
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraIM/UltraIMPlugins.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"
#include "DataFormats/UltraCanvasJSON.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraIMMastodon {

struct MastodonTimings {
    int pollMs = 4000;              // how often the conversations are asked for news
    int mediaWaitMs = 60000;        // how long an upload may stay "processing"
    int oauthTimeoutMs = 300000;    // how long the browser sign-in may take
};

// The answer of one request: the status and the body as JSON.
struct MastodonResponse {
    int                    status = 0;      // 0: the request never completed
    UltraCanvas::JSONValue body;
    std::string            error;           // the server's "error", or the transfer's

    bool Ok() const { return status >= 200 && status < 300; }
};

class MastodonProvider : public IUltraIMProviderPlugin {
public:
    explicit MastodonProvider(std::string providerId = "mastodon");
    ~MastodonProvider() override;

    std::string GetProviderId() const override { return providerId_; }
    std::string GetDisplayName() const override { return "Mastodon"; }
    std::string GetVersion() const override { return "0.1.0"; }
    UltraIMCapabilityFlags GetCapabilities() const override;
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::SsoOidc; }

    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override;
    void Shutdown() override;

    // credentials.server: the Mastodon server ("mastodon.social", or a full
    // https:// address). With credentials.accessToken, signed in with it;
    // without, the stored session for that server, else the browser sign-in
    // (AuthStepChange Browser with the consent page in errorText; onResult
    // once the server has answered, or the wait ended). With no server, the
    // stored session.
    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override;
    // Disconnects; the session is kept for the next sign-in.
    void SignOut() override;
    UltraIMConnectionState GetConnectionState() const override { return state_.load(); }
    std::string GetLocalUserId() const override;
    bool HasStoredSession() const override;

    void FetchConversations(UltraIMConversationsCallback onResult) override;
    // The chat's thread (GET /api/v1/statuses/:id/context of its last status),
    // its direct statuses oldest first; beforeMessageId pages are empty (the
    // thread comes whole).
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override;
    void SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) override;
    void FetchContacts(UltraIMContactsCallback onResult) override;
    // address: "@user@server", "user@server", "@user" (this server) or the
    // profile's address ("https://server/@user"); a chat with them, empty
    // until the first message.
    void AddContact(const std::string& address, UltraIMConversationCallback onResult) override;
    // A chat with several accounts (addresses as AddContact takes them). A
    // Mastodon conversation has no name: the title is its people's.
    void CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                     UltraIMConversationCallback onResult) override;
    // Off the list (DELETE /api/v1/conversations/:id); the statuses stay.
    void LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) override;

    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override;
    // Uploaded (POST /api/v2/media, waited for while it is processed), then
    // the status that carries it. Mastodon takes pictures, videos and sound
    // only: another file answers CapabilityMissing.
    UltraIMResult SendImage(const std::string& conversationId, const std::string& localPath,
                            const std::string& caption, const UltraIMSendOptions& options) override;
    UltraIMResult SendVideo(const std::string& conversationId, const std::string& localPath,
                            const std::string& caption, const UltraIMSendOptions& options) override;
    UltraIMResult SendVoiceNote(const std::string& conversationId, const std::string& localPath,
                                const UltraIMSendOptions& options) override;
    UltraIMResult SendFile(const std::string& conversationId, const std::string& localPath,
                           const UltraIMSendOptions& options) override;
    void DownloadAttachment(const std::string& conversationId, const std::string& messageId,
                            UltraIMAttachmentCallback onResult) override;
    UltraIMResult EditMessage(const std::string& conversationId, const std::string& messageId,
                              const std::string& newText) override;
    // For everyone, one's own: Mastodon has no "for me".
    UltraIMResult DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                UltraIMDeleteScope scope) override;
    UltraIMDeleteChoices GetDeleteChoices(const std::string& conversationId,
                                          const std::string& messageId) const override;
    UltraIMResult MarkRead(const std::string& conversationId, const std::string& upToMessageId) override;

    void SetTimings(const MastodonTimings& timings) { timings_ = timings; }
    std::string GetServerUrl() const;
    // The loopback address the browser sign-in is answered on (the
    // application registers it with the server); tests move it.
    void SetRedirectUri(const std::string& uri) { redirectUri_ = uri; }

private:
    struct Account {
        std::string id;
        std::string acct;           // "user" here, "user@server" elsewhere
        std::string name;           // display name, else the username
        std::string avatarUrl;
    };
    struct Chat {
        std::string key;                            // the others' addresses, sorted, space-joined
        std::vector<Account> accounts;              // the others
        std::set<std::string> serverIds;            // the server's conversations of these people
        std::string lastStatusId;
        std::vector<std::string> roots;             // the first statuses of its threads, once read
        std::map<std::string, std::string> heads;   // server conversation -> its last status
        std::map<std::string, std::string> headEdited;  // server conversation -> its last status's edited_at
        std::map<std::string, std::string> rootOf;  // a head -> its thread's first status
        UltraIMMessage last;                        // for the list's line
        bool unread = false;
        bool hasLast = false;
        std::vector<UltraIMMessage> messages;       // the thread, oldest first (when fetched)
    };

    // ---- the worker: every request runs here, in order --------------------
    void Enqueue(std::function<void()> job);
    void WorkLoop();
    MastodonResponse Request(const std::string& method, const std::string& path, const UltraCanvas::JSONValue* body,
                             int timeoutMs = 30000);
    MastodonResponse Upload(const std::string& path, const std::string& localPath, const std::string& mimeType,
                            const std::function<void(int64_t, int64_t)>& onProgress);
    bool Download(const std::string& url, const std::string& toPath);

    void Emit(const UltraIMEvent& event);
    void SetState(UltraIMConnectionState state, const std::string& error = std::string());
    void SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult);
    UltraIMResult BrowserSignIn(const std::string& base, std::string& token);
    UltraIMResult StartSession(const std::string& base, const std::string& token);
    bool SaveSession(const std::string& base, const std::string& token);
    bool LoadSession(std::string& base, std::string& token) const;

    // ---- chats and statuses ------------------------------------------------
    void Poll(bool first);
    std::string KeyOf(const std::vector<Account>& others) const;
    Account AccountFrom(const UltraCanvas::JSONValue& account) const;
    bool MessageFrom(const UltraCanvas::JSONValue& status, const std::string& key, UltraIMMessage& out);
    UltraIMConversation ConversationOf(const Chat& chat) const;
    // The chat's thread, oldest first; unseen gets the ids not told about before.
    std::vector<UltraIMMessage> ThreadOf(Chat& chat, std::set<std::string>* unseen = nullptr);
    UltraIMResult ResolveAccount(const std::string& address, Account& out);
    std::string MentionsOf(const Chat& chat) const;
    void QueueMedia(const UltraIMMessage& message);
    // An account's picture (avatar_static) fetched once into the media
    // folder - not Mastodon's grey default, so the initials show instead -
    // and its chats told (ConversationUpdate). With mutex_ held.
    void QueueAvatar(const Account& account);
    // The account's picture here, "" until fetched. With mutex_ held.
    std::string AvatarOf(const std::string& accountId) const;
    std::string MediaPath(const std::string& url, const std::string& fileName) const;
    UltraIMResult SendStatus(const std::string& conversationId, const std::string& text, const std::string& localPath,
                             UltraIMMessageType type, const UltraIMSendOptions& options);

    std::string     providerId_;
    UltraIMConfig   config_;
    UltraIMEventSink sink_;
    MastodonTimings timings_;
    std::string     redirectUri_ = "http://127.0.0.1:47819/callback";
    std::atomic<UltraIMConnectionState> state_{UltraIMConnectionState::Disconnected};

    mutable UltraVault::DeviceKeyVault vault_;
    bool vaultReady_ = false;

    mutable std::mutex mutex_;          // everything below
    std::string base_;                  // "https://mastodon.social"
    std::string token_;
    Account     me_;
    std::map<std::string, Chat> chats_;                 // key -> chat
    std::map<std::string, std::string> clientOfStatus_; // our status id -> the client id it was sent under
    std::set<std::string> seen_;                        // status ids told about
    std::map<std::string, std::string> keyOfStatus_;    // status id -> chat key
    std::map<std::string, UltraIMMessage> byId_;        // status id -> message (for edit, delete, download)
    std::map<std::string, std::string> avatars_;        // account id -> its picture's file
    std::set<std::string> avatarAsked_;                 // "id url" fetched or on its way

    // The worker.
    std::mutex workMutex_;
    std::condition_variable workWake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    bool polling_ = false;
    std::thread worker_;
    std::atomic<bool> cancelSignIn_{false};
};

} // namespace UltraIMMastodon
