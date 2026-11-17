// Plugins/UltraIM/mattermost/MattermostProvider.h
// Mattermost - an UltraIM provider for the channels and direct messages of a
// Mattermost server, over its REST API (v4) through UltraNet.
//
// Here:
//
//   - a conversation's id is the Mattermost channel's id (26 letters and
//     digits), the same on every side: a direct message (type D) is a 1:1
//     chat with the other person (directContactId: their user id); a group
//     message (G) and the public and private channels (O, P) of the user's
//     teams are groups - a channel's title is "<its name> (<team name>)",
//     its owner the one who made it; a group message's title is its people's
//     names (Mattermost names it by them);
//   - a message is a post: its markdown as it is, a thread reply answering
//     the thread's first post (replyTo), its first file as the picture,
//     video, sound or file (by its type); system posts ("X joined") are left
//     out;
//   - news comes by polling (MattermostTimings::pollMs): the channel list
//     says which channels have new posts (last_post_at), and those - with
//     the channels open in the window and the ones busy lately, where an
//     edit or a delete does not move last_post_at - are read for the posts
//     changed since the last look (GET .../posts?since=): new, edited,
//     deleted;
//   - sending posts to the channel (files uploaded first, with progress),
//     edit and delete one's own, read (the channel viewed), leave (a channel
//     left; a direct or group message hidden, as Mattermost does - it comes
//     back with the next post in it); no typing, read receipts, presence or
//     calls here.
//
// Sign-in is a username (or e-mail) and password (UltraIMLoginType::
// UsernamePassword): POST /api/v4/users/login, the session's token in the
// answer's Token header. An account with multi-factor authentication asks
// for its code next: SignIn answers NeedsInput, GetAuthStep() is Code (and
// AuthStepChange says so), and the next SignIn carries credentials.otpCode -
// the server, username and password given before are remembered. A personal
// access token (credentials.accessToken) signs in directly. The session's
// token is kept in UltraVault; a SignIn with nothing in it resumes it.
//
// Version: 0.1.0
// Last Modified: 2026-11-17
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

namespace UltraIMMattermost {

struct MattermostTimings {
    int pollMs = 3000;              // how often the channels are asked for news
    // A channel with a post this recent (by the server's clock) is read at
    // every look, open or not: its edits and deletes do not move it in the list.
    int busyWindowMs = 15 * 60 * 1000;
};

// The answer of one request: the status, the body as JSON, the headers.
struct MattermostResponse {
    int                    status = 0;      // 0: the request never completed
    UltraCanvas::JSONValue body;
    std::string            error;           // the server's "message", or the transfer's
    std::string            errorId;         // the server's "id" ("api.user.login...")
    std::string            token;           // the Token header (a sign-in's answer)

    bool Ok() const { return status >= 200 && status < 300; }
};

class MattermostProvider : public IUltraIMProviderPlugin {
public:
    explicit MattermostProvider(std::string providerId = "mattermost");
    ~MattermostProvider() override;

    std::string GetProviderId() const override { return providerId_; }
    std::string GetDisplayName() const override { return "Mattermost"; }
    std::string GetVersion() const override { return "0.1.0"; }
    UltraIMCapabilityFlags GetCapabilities() const override;
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::UsernamePassword; }

    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override;
    void Shutdown() override;

    // credentials.server: the Mattermost server ("chat.example.org", or a
    // full https:// address). With username (or e-mail) and password, signed
    // in with them; with accessToken (a personal access token), with it;
    // with neither, the stored session (for that server, or any with no
    // server given). NeedsInput when the account asks for its multi-factor
    // code: GetAuthStep() Code, then SignIn again with otpCode alone.
    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override;
    // Disconnects; the session is kept for the next sign-in.
    void SignOut() override;
    UltraIMConnectionState GetConnectionState() const override { return state_.load(); }
    std::string GetLocalUserId() const override;
    // The signed-in account as people see it (its name; the e-mail where the
    // service shows it): read-only - the profile is edited on the server.
    void FetchAccountProfile(UltraIMAccountProfileCallback onResult) override;
    bool HasStoredSession() const override;
    UltraIMAuthStep GetAuthStep() const override;
    std::string GetAuthHint() const override;

    void FetchConversations(UltraIMConversationsCallback onResult) override;
    // A page of the channel's posts (GET /api/v4/channels/:id/posts), oldest
    // first; beforeMessageId the page before that post.
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override;
    // In one conversation (read page by page); every conversation at once:
    // CapabilityMissing.
    void SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) override;
    // The people of the direct messages.
    void FetchContacts(UltraIMContactsCallback onResult) override;
    // address: "@username", "username", an e-mail address or a user id; the
    // direct message with them (POST /api/v4/channels/direct).
    void AddContact(const std::string& address, UltraIMConversationCallback onResult) override;
    // A group message with 2 to 7 others (addresses as AddContact takes
    // them; one other: the direct message). Mattermost names it by its
    // people: the title is not used.
    void CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                     UltraIMConversationCallback onResult) override;
    // A channel: left. A direct or group message: hidden (the history stays;
    // it comes back with its next post).
    void LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) override;

    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override;
    // Uploaded (POST /api/v4/files, with TransferProgress), then the post
    // that carries it. Any kind of file.
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
    // One's own posts.
    UltraIMResult EditMessage(const std::string& conversationId, const std::string& messageId,
                              const std::string& newText) override;
    // For everyone, one's own: Mattermost has no "for me".
    UltraIMResult DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                UltraIMDeleteScope scope) override;
    UltraIMDeleteChoices GetDeleteChoices(const std::string& conversationId,
                                          const std::string& messageId) const override;
    UltraIMResult MarkRead(const std::string& conversationId, const std::string& upToMessageId) override;

    void SetTimings(const MattermostTimings& timings) { timings_ = timings; }
    std::string GetServerUrl() const;

private:
    struct User {
        std::string id;
        std::string username;
        std::string name;           // "first last", else the nickname, else the username
    };
    struct Chat {
        std::string id;                         // the channel's
        char        type = 'O';                 // D, G, O, P
        std::string teamName;                   // O, P: the team's display name
        std::string displayName;                // O, P: the channel's
        std::string creatorId;
        std::string otherId;                    // D: the other person
        std::vector<std::string> memberIds;     // with the local user
        int         memberCount = 0;            // when memberIds is a first page
        int64_t     lastPostAt = 0;
        int64_t     lastRootPostAt = 0;
        int64_t     totalMsgCount = 0;
        int64_t     msgCount = 0;               // the local user's, read
        int64_t     sinceMs = 0;                // the newest change read in it (the server's clock)
        UltraIMMessage last;                    // for the list's line
        bool        hasLast = false;
    };

    // ---- the worker: every request runs here, in order --------------------
    void Enqueue(std::function<void()> job);
    void WorkLoop();
    MattermostResponse Request(const std::string& method, const std::string& path, const UltraCanvas::JSONValue* body,
                               int timeoutMs = 30000);
    MattermostResponse Upload(const std::string& channelId, const std::string& localPath, const std::string& mimeType,
                              const std::function<void(int64_t, int64_t)>& onProgress);
    bool Download(const std::string& url, const std::string& toPath);

    void Emit(const UltraIMEvent& event);
    void SetState(UltraIMConnectionState state, const std::string& error = std::string());
    void SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult);
    UltraIMResult PasswordSignIn(const std::string& base, const std::string& login, const std::string& password,
                                 const std::string& code, std::string& token);
    UltraIMResult StartSession(const std::string& base, const std::string& token);
    void AskForCode(const std::string& hint, const UltraIMResultCallback& onResult);
    bool SaveSession(const std::string& base, const std::string& token);
    bool LoadSession(std::string& base, std::string& token) const;
    void ForgetSession();
    void SessionEnded();

    // ---- channels and posts -------------------------------------------------
    void Poll(bool first);
    // The channel, new to the list: its people, its last posts; those newer
    // than receiveAfter (a server time, >= 0) are received - none with -1.
    bool AddChannel(const UltraCanvas::JSONValue& channel, int64_t msgCount, int64_t receiveAfter,
                    std::vector<UltraIMEvent>& events, std::vector<UltraIMMessage>& media);
    // The posts of a channel changed since its last read: new, edited, deleted.
    void ReadChanges(const std::string& channelId, std::vector<UltraIMEvent>& events, std::vector<UltraIMMessage>& media);
    // A channel's posts as Mattermost answers a list ({order, posts}), oldest first.
    std::vector<UltraCanvas::JSONValue> PostsOf(const UltraCanvas::JSONValue& list) const;
    // What turning these posts into messages needs from the server first:
    // their senders, their files' details, the first posts of their threads.
    void Prepare(const std::vector<UltraCanvas::JSONValue>& posts);
    void EnsureUsers(const std::set<std::string>& ids);
    bool MessageFrom(const UltraCanvas::JSONValue& post, UltraIMMessage& out) const;
    void FillReply(UltraIMMessage& m) const;
    UltraIMConversation ConversationOf(const Chat& chat) const;
    void LoadMembers(Chat& chat);
    UltraIMResult ResolveUser(const std::string& address, User& out);
    UltraIMConversation OpenChannel(const UltraCanvas::JSONValue& channel);
    std::string PostIdOf(const std::string& messageId) const;  // a client id -> the post's
    void QueueMedia(const UltraIMMessage& message);
    std::string MediaPath(const std::string& url, const std::string& fileName) const;
    void Hide(const Chat& chat, bool hidden);
    UltraIMResult SendPost(const std::string& conversationId, const std::string& text, const std::string& localPath,
                           UltraIMMessageType type, const UltraIMSendOptions& options);

    std::string       providerId_;
    UltraIMConfig     config_;
    UltraIMEventSink  sink_;
    MattermostTimings timings_;
    std::atomic<UltraIMConnectionState> state_{UltraIMConnectionState::Disconnected};

    mutable UltraVault::DeviceKeyVault vault_;
    bool vaultReady_ = false;

    mutable std::mutex mutex_;          // everything below
    std::string base_;                  // "https://chat.example.org"
    std::string token_;
    User        me_;
    // A sign-in waiting for its multi-factor code.
    UltraIMAuthStep authStep_ = UltraIMAuthStep::Idle;
    std::string authHint_;
    std::string pendingBase_, pendingLogin_, pendingPassword_;

    std::map<std::string, std::string> teams_;          // team id -> display name
    std::map<std::string, Chat> chats_;                 // channel id -> chat
    std::map<std::string, int64_t> hidden_;             // a hidden D / G channel -> its last_post_at then
    std::map<std::string, User> users_;                 // user id -> user
    std::map<std::string, UltraCanvas::JSONValue> files_;   // file id -> its info
    std::map<std::string, int64_t> known_;              // post id -> its update_at, as told
    std::set<std::string> deleted_;                     // post ids whose delete was told
    std::map<std::string, std::string> clientOfPost_;   // our post id -> the client id it was sent under
    std::set<std::string> sentHere_;                    // the client ids sent from here (pending_post_id)
    std::map<std::string, UltraIMMessage> byId_;        // post id -> message (replies, edit, delete, download)
    std::deque<std::string> open_;                      // the channels last fetched (read at every look)
    int64_t clock_ = 0;                                 // the newest time the server has told (ms)

    // The worker.
    std::mutex workMutex_;
    std::condition_variable workWake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    bool polling_ = false;
    std::thread worker_;
};

} // namespace UltraIMMattermost
