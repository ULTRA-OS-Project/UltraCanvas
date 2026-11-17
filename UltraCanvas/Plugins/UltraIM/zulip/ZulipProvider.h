// Plugins/UltraIM/zulip/ZulipProvider.h
// Zulip - an UltraIM provider for the streams and direct messages of a Zulip
// organization, over its REST API (v1) through UltraNet.
//
// Zulip talks in streams (channels) that hold topics, and in direct
// messages to one or more people. Here:
//
//   - a direct conversation's id is "dm:" and the other people's user ids,
//     sorted, comma-joined ("dm:12" with one person, "dm:12,15" a group
//     direct message; "dm:<own id>" the notes to oneself), so a chat opened
//     before its first message (AddContact, CreateGroup) and the one its
//     messages make are the same;
//   - a stream's topic is a group conversation of its own,
//     "stream:<stream id>:<topic>", titled "#stream > topic", its members
//     the stream's subscribers;
//   - a message is its Markdown as typed (apply_markdown=false), shown as
//     it is; the first upload it links ("[name](/user_uploads/...)") is its
//     picture, video, sound or file - the link left out of the text when it
//     stands on a line of its own; a quote stays text (Zulip has no reply);
//   - news comes from Zulip's event queue (POST /api/v1/register, then
//     GET /api/v1/events long-polled on a thread of its own): a new message,
//     an edit, a delete; a queue the server forgot is registered again and
//     what came in between fetched;
//   - sending carries the queue's id and a local id, so the queue's echo of
//     our own message is known and not received again; a file is uploaded
//     (POST /api/v1/user_uploads) and sent as a link to it;
//   - edit and delete one's own (for everyone), read (the "read" flag),
//     leave (a topic muted and taken off the list, a direct chat taken off
//     the list until its next message); no reply, presence or calls;
//   - typing both ways: SetTyping posts POST /api/v1/typing ("direct" to the
//     chat's people, Zulip 7.0+ - "private" before; a stream topic by
//     stream_id and topic, Zulip 8.0+: a server that refuses it is not asked
//     again), and the queue's "typing" events (start / stop) are
//     TypingChange, the typist's user id the contactId. Zulip's clients say
//     "start" again only every ~10 s: a typist's start is told again every
//     ZulipTimings::typingRefreshMs (a window lets it go after a few
//     seconds) until their stop, their message in that conversation (the
//     stop told just before it) or typingExpireMs after their last start (as
//     Zulip's own clients give up); a sign-out or a queue lost stops them all.
//
// Sign-in (UltraIMLoginType::UsernamePassword): credentials.server is the
// organization's address ("chat.zulip.org" or a full https:// address),
// credentials.username the account's e-mail address, and either
// credentials.password (POST /api/v1/fetch_api_key gets the API key) or
// credentials.accessToken, the API key itself. Requests sign with HTTP Basic
// "e-mail:API key". The address, e-mail and key are kept in UltraVault; a
// SignIn with nothing in it resumes them.
//
// Version: 0.2.0 - typing indicators both ways (POST /api/v1/typing, the queue's "typing" events); a
//                  typist's start told again every typingRefreshMs until their stop, message or expiry
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraIM/UltraIMPlugins.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"
#include "DataFormats/UltraCanvasJSON.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace UltraIMZulip {

struct ZulipTimings {
    int longPollMs = 90000;         // how long a GET /api/v1/events may stay open (Zulip answers within ~60 s)
    int retryMinMs = 1000;          // the first wait after the event queue could not be reached
    int retryMaxMs = 30000;         // the longest
    int topicsPerStream = 30;       // the newest topics of each stream listed
    int typingRefreshMs = 4000;     // a typist's start told again this often (Zulip repeats it every ~10 s)
    int typingExpireMs = 15000;     // ... until this long after their last start with no stop: stopped
};

// The answer of one request: the status and the body as JSON.
struct ZulipResponse {
    int                    status = 0;      // 0: the request never completed
    UltraCanvas::JSONValue body;
    std::string            error;           // the server's "msg", or the transfer's
    std::string            code;            // the server's "code" (BAD_EVENT_QUEUE_ID, ...)

    bool Ok() const { return status >= 200 && status < 300; }
};

class ZulipProvider : public IUltraIMProviderPlugin {
public:
    explicit ZulipProvider(std::string providerId = "zulip");
    ~ZulipProvider() override;

    std::string GetProviderId() const override { return providerId_; }
    std::string GetDisplayName() const override { return "Zulip"; }
    std::string GetVersion() const override { return "0.2.0"; }
    UltraIMCapabilityFlags GetCapabilities() const override;
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::UsernamePassword; }

    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override;
    void Shutdown() override;

    // credentials.server: the organization ("chat.zulip.org", or a full
    // https:// address); credentials.username: the account's e-mail;
    // credentials.password: its password, or credentials.accessToken: an API
    // key (Personal settings > Account & privacy > API key). With neither,
    // the stored session (for that server, or any when there is no server).
    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override;
    // Disconnects; the session is kept for the next sign-in.
    void SignOut() override;
    UltraIMConnectionState GetConnectionState() const override { return state_.load(); }
    // The account's Zulip user id, as text ("12").
    std::string GetLocalUserId() const override;
    // The signed-in account as people see it (its name; the e-mail where the
    // service shows it): read-only - the profile is edited on the server.
    void FetchAccountProfile(UltraIMAccountProfileCallback onResult) override;
    bool HasStoredSession() const override;

    // The subscribed streams' newest topics and the direct conversations of
    // the newest messages, read from the server again.
    void FetchConversations(UltraIMConversationsCallback onResult) override;
    // A page of the conversation, oldest first: the newest `limit`, or the
    // `limit` before beforeMessageId.
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override;
    void SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) override;
    // The organization's active people, not its bots, not oneself; the
    // contact id is the user id.
    void FetchContacts(UltraIMContactsCallback onResult) override;
    // address: an e-mail address ("bob@example.org") or a user id; the
    // direct chat with them, empty until the first message.
    void AddContact(const std::string& address, UltraIMConversationCallback onResult) override;
    // A group direct message (e-mails or user ids, as AddContact takes them).
    // Zulip's group direct messages have no name: the title is ignored.
    void CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                     UltraIMConversationCallback onResult) override;
    // A topic: muted (POST /api/v1/user_topics) and off the list. A direct
    // chat: off the list on this device until its next message.
    void LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) override;

    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override;
    // Uploaded (TransferProgress), then a message with the link to it, the
    // caption on the line before. Any kind of file.
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
    // For everyone, one's own: Zulip has no "for me".
    UltraIMResult DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                UltraIMDeleteScope scope) override;
    UltraIMDeleteChoices GetDeleteChoices(const std::string& conversationId,
                                          const std::string& messageId) const override;
    UltraIMResult MarkRead(const std::string& conversationId, const std::string& upToMessageId) override;
    // POST /api/v1/typing, on the worker: start, and stop when a start was
    // sent. Notes to oneself: nothing to tell.
    UltraIMResult SetTyping(const std::string& conversationId, bool isTyping) override;

    void SetTimings(const ZulipTimings& timings) { timings_ = timings; }
    std::string GetServerUrl() const;

private:
    struct Person {
        std::string id;             // the user id, as text
        std::string email;
        std::string name;
    };
    struct Chat {
        std::string id;                         // "dm:12,15" or "stream:1:topic"
        bool isStream = false;
        std::string streamId;
        std::string streamName;
        std::string topic;
        std::vector<std::string> others;        // a direct chat's people (user ids), sorted
        UltraIMMessage last;                    // for the list's line
        bool hasLast = false;
        std::set<int64_t> unread;               // the messages not read yet, that we know of
    };

    // ---- the worker: every request but the long-poll runs here, in order --
    void Enqueue(std::function<void()> job);
    void WorkLoop();
    using Params = std::vector<std::pair<std::string, std::string>>;
    ZulipResponse Request(const std::string& method, const std::string& path, const Params& params,
                          int timeoutMs = 30000, bool withAuth = true);
    ZulipResponse Upload(const std::string& localPath, const std::string& mimeType,
                         const std::function<void(int64_t, int64_t)>& onProgress);
    bool Download(const std::string& url, const std::string& toPath);
    std::string AuthHeader() const;

    void Emit(const UltraIMEvent& event);
    void SetState(UltraIMConnectionState state, const std::string& error = std::string());
    void AuthLost(const std::string& why);
    void SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult);
    UltraIMResult StartSession(const std::string& base, const std::string& email, const std::string& key);
    bool SaveSession(const std::string& base, const std::string& email, const std::string& key);
    bool LoadSession(std::string& base, std::string& email, std::string& key) const;
    void SaveHidden();
    void LoadHidden();

    // ---- the event queue (a thread of its own) ------------------------------
    bool RegisterQueue();
    void EventLoop();
    void StopEvents();
    void WaitEvents(int ms);
    void HandleEvent(const UltraCanvas::JSONValue& event);
    void CatchUp();
    void Typing(const UltraCanvas::JSONValue& event);
    // The typists: told again, expired, stopped (each with emitMutex_ taken first).
    void TypingTimerLoop();
    void TypingStopped(const std::string& conversationId, const std::string& userId);
    void StopAllTyping();
    void Received(const UltraCanvas::JSONValue& message, const std::vector<UltraCanvas::JSONValue>& flags,
                  const std::string& localId);

    // ---- conversations and messages -----------------------------------------
    void LoadPeople();
    void LoadConversations();
    Chat& ChatFor(const UltraCanvas::JSONValue& message);         // with mutex_ held
    Chat* ChatOf(const std::string& conversationId);              // with mutex_ held; created for a well-formed id
    UltraIMConversation ConversationOf(const Chat& chat) const;   // with mutex_ held
    bool MessageFrom(const UltraCanvas::JSONValue& message, UltraIMMessage& out);   // with mutex_ held
    std::string NarrowOf(const Chat& chat) const;
    std::string ServerIdOf(const std::string& messageId) const;  // with mutex_ held: a client id -> the server's
    void Remember(const UltraIMMessage& m);                       // with mutex_ held
    void UpdateLast(Chat& chat, const UltraIMMessage& m);         // with mutex_ held
    UltraIMResult ResolvePerson(const std::string& address, Person& out);
    void QueueMedia(const UltraIMMessage& message);
    std::string MediaPath(const std::string& url, const std::string& fileName) const;
    UltraIMResult SendPost(const std::string& conversationId, const std::string& text, const std::string& localPath,
                              UltraIMMessageType type, const UltraIMSendOptions& options);
    void EmitConversation(const std::string& conversationId);

    std::string     providerId_;
    UltraIMConfig   config_;
    UltraIMEventSink sink_;
    std::recursive_mutex emitMutex_;    // one event at a time, from the worker or the queue's thread
    ZulipTimings    timings_;
    std::atomic<UltraIMConnectionState> state_{UltraIMConnectionState::Disconnected};

    mutable UltraVault::DeviceKeyVault vault_;
    bool vaultReady_ = false;

    mutable std::mutex mutex_;          // everything below
    std::string base_;                  // "https://chat.zulip.org"
    std::string email_;
    std::string apiKey_;
    Person      me_;
    std::map<std::string, Person> people_;               // user id -> person
    std::map<std::string, std::vector<std::string>> streamMembers_;   // stream id -> subscribers
    std::map<std::string, std::string> streamNames_;     // stream id -> its name
    std::map<std::string, Chat> chats_;                  // conversation id -> chat
    std::set<std::string> hidden_;                       // conversations left (kept in the vault)
    std::map<int64_t, UltraIMMessage> byId_;             // message id -> message (for edit, delete, download)
    std::set<int64_t> deletedIds_;                       // message ids whose delete was told
    std::map<int64_t, std::string> linkLines_;           // message id -> the upload's link line left out of its text
    std::map<std::string, std::string> clientOfMessage_; // our message id -> the client id it was sent under
    std::map<std::string, std::string> pendingLocal_;    // local id sent -> its content, the echo not met yet
    int64_t     maxSeenId_ = 0;                          // the newest message told about
    std::string queueId_;
    int64_t     lastEventId_ = -1;
    bool        authLost_ = false;
    std::set<std::string> typingTo_;                     // conversations told "start" and not "stop" yet
    bool        streamTypingRefused_ = false;            // the server takes no typing in a topic (before Zulip 8)
    bool        directTypingOld_ = false;                // the server wants type=private (before Zulip 7)

    // The worker.
    std::mutex workMutex_;
    std::condition_variable workWake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::atomic<bool> workAbort_{false};    // Shutdown: the worker's request in flight ends now
    std::thread worker_;

    // The event queue's thread.
    std::mutex eventsMutex_;
    std::condition_variable eventsWake_;
    std::atomic<bool> stopEvents_{false};
    std::thread events_;

    // Who types where: (conversation, user) -> their last start, and when it was last told.
    struct Typist {
        std::chrono::steady_clock::time_point lastStart;
        std::chrono::steady_clock::time_point lastTold;
    };
    std::mutex typingMutex_;
    std::condition_variable typingWake_;
    bool typingTimerStop_ = false;
    std::map<std::pair<std::string, std::string>, Typist> typists_;
    std::thread typingTimer_;
};

} // namespace UltraIMZulip
