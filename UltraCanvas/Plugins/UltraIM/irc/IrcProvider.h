// Plugins/UltraIM/irc/IrcProvider.h
// IRC - an UltraIM provider for the channels and private messages of an IRC
// network, with the IRCv3 extensions the server offers, over TCP and TLS
// through UltraNet.
//
// Here:
//
//   - the connection is TLS on port 6697 unless the server's address says
//     irc:// (plain TCP, 6667 by default): "irc.libera.chat",
//     "irc.libera.chat:7000", "ircs://host[:port]", "irc://host[:port]". The
//     certificate is always verified (SetTlsOptions adds a CA file to trust);
//   - signing in is the nickname (credentials.username) and, optionally, the
//     account's password (credentials.password) - SASL PLAIN as the account
//     credentials.displayName names, else the nick; only over TLS (or to this
//     machine). With no SASL on the server, the password goes to NickServ
//     (IDENTIFY). Without a password the connection is anonymous. A nick in
//     use is tried again as nick_, then nick__, and the answer's message (and
//     a line in the server conversation) says so. The server, the nick, the
//     password and the channels and private chats open are kept in UltraVault
//     (encrypted); a SignIn with no nickname and no server resumes them and
//     joins those channels again. A lost connection is tried again with a
//     growing pause (Reconnecting), rejoining the channels; a silent one is
//     pinged, and given up when it does not answer;
//   - IRCv3: CAP LS 302 / CAP REQ for sasl, server-time, message-tags,
//     echo-message, batch, away-notify, account-tag, multi-prefix,
//     labeled-response, draft/chathistory (or chathistory) and
//     draft/event-playback - each one only when the server offers it;
//   - a channel is a group: its id is the channel's name folded as the server
//     folds names (005 CASEMAPPING; rfc1459 when it says nothing) - "#chat";
//     its title the name as written; its members (memberIds: their nicks, ours
//     first) from NAMES and JOIN / PART / QUIT / KICK / NICK; no owner; the
//     topic its description, and the list's line until there is a message;
//   - a private chat's id is the other nick, folded ("bob"); directContactId
//     the nick as written. Its presence: Online while the nick is seen in a
//     channel we share or MONITOR says so, Away by away-notify, Offline when it
//     quits or MONITOR says so. A nick that changes moves its chat (removed
//     under the old id, updated under the new);
//   - "*server" is the server's conversation: notices from the server, from
//     services (NickServ, ChanServ: a notice from a nick with no private chat
//     open) and the errors the server answers with no request waiting for
//     them. What is sent to it is a raw IRC command ("MODE #chat +i",
//     a leading "/" dropped);
//   - a message is a PRIVMSG or a NOTICE: its id the server's msgid tag, or
//     one made here ("irc-..."); its time the server-time tag, else now; mIRC
//     formatting taken out; not UTF-8: read as Latin-1; a CTCP ACTION
//     ("/me waves") is "* nick waves" (sent so too from text that starts with
//     "/me "). A reply carries the +draft/reply tag (message-tags); typing is
//     +typing=active / done on a TAGMSG. A text too long for one line is sent
//     as several (on word boundaries, never inside a UTF-8 character); the
//     others get each as a message of its own. With echo-message, our message
//     comes back from the server (matched by its label with labeled-response,
//     else by its text and target): MessageDelivered Sent and a MessageUpdate
//     with the server's msgid; without it, Sent once written;
//   - history: with chathistory, CHATHISTORY LATEST <target> * 50 when a
//     channel is joined (nothing received from it: it is history - unless
//     the channel was joined before, after a lost connection, when what came
//     meanwhile is received) and FetchMessages as CHATHISTORY LATEST /
//     BEFORE; without it, what this session saw (in memory). Search reads
//     those pages (UltraIM_SearchByFetching);
//   - no edits, deletes, files, read receipts or calls (CapabilityMissing).
//
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include "IrcConnection.h"
#include "IrcMessage.h"

#include "UltraIM/UltraIMPlugins.h"
#include "UltraNet/UltraNetTls.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraIMIrc {

struct IrcTimings {
    int pingAfterMs = 90000;        // silence from the server before we ping it
    int pingTimeoutMs = 60000;      // no answer to that ping: the connection is gone
    int reconnectMinMs = 2000;      // the first pause before connecting again; doubles each time
    int reconnectMaxMs = 120000;
    int connectTimeoutMs = 20000;   // TCP and TLS
    int registerTimeoutMs = 30000;  // from connecting to the server's welcome
    int requestTimeoutMs = 20000;   // a JOIN, a WHOIS, a CHATHISTORY, an echo awaited
    int readSliceMs = 100;          // how long one read waits (writes go between reads)
};

// Where to connect: "irc.libera.chat" / "host:port" / "ircs://host[:port]"
// (TLS, 6697) or "irc://host[:port]" (plain, 6667).
struct IrcServerAddress {
    std::string host;
    int port = 6697;
    bool tls = true;

    static bool Parse(const std::string& text, IrcServerAddress& out, std::string& error);
    // "ircs://host:port" / "irc://host:port".
    std::string ToString() const;
    // 127.0.0.1, ::1 or localhost.
    bool IsLocal() const;
};

class IrcProvider : public IUltraIMProviderPlugin {
public:
    explicit IrcProvider(std::string providerId = "irc");
    ~IrcProvider() override;

    std::string GetProviderId() const override { return providerId_; }
    std::string GetDisplayName() const override { return "IRC"; }
    std::string GetVersion() const override { return "0.1.0"; }
    // OfflineMessages while the server keeps history (chathistory).
    UltraIMCapabilityFlags GetCapabilities() const override;
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::UsernamePassword; }

    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override;
    void Shutdown() override;

    // credentials.server: the server (see IrcServerAddress); username: the
    // nickname; password: the account's password (optional; SASL PLAIN);
    // displayName: the account's name when it is not the nick. Nothing in
    // username and server: the stored session. The answer's message says
    // when the server gave another nick (the one asked for was taken).
    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override;
    // QUIT; the session stays stored for the next sign-in.
    void SignOut() override;
    UltraIMConnectionState GetConnectionState() const override { return state_.load(); }
    // The nick in use now.
    std::string GetLocalUserId() const override;
    bool HasStoredSession() const override;
    // The nick (and the account, when signed in with one): read-only.
    void FetchAccountProfile(UltraIMAccountProfileCallback onResult) override;

    void FetchConversations(UltraIMConversationsCallback onResult) override;
    // The server's history (chathistory) when it keeps one, else what this
    // session saw. beforeMessageId: the page before that message.
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override;
    // In one conversation; every conversation at once: CapabilityMissing.
    void SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) override;
    // The nicks of the private chats, with their presence.
    void FetchContacts(UltraIMContactsCallback onResult) override;
    // "#channel" (or "irc:#channel", "#channel key"): joined - the
    // conversation answers once the server has said who is in it; refused
    // with the server's reason (no such channel, invite-only, banned, a key
    // needed, full). A nick ("bob", "irc:bob"): a private chat, once WHOIS
    // finds it (NotFound when no one has it).
    void AddContact(const std::string& address, UltraIMConversationCallback onResult) override;
    // Joins "#<title without spaces>" (made by joining it, when it is new)
    // and invites the members (their nicks).
    void CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                     UltraIMConversationCallback onResult) override;
    // A channel: PART. A private chat (or the server's): hidden here; it
    // comes back with the next message.
    void LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) override;

    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override;
    // IRC carries no files.
    UltraIMResult SendImage(const std::string& conversationId, const std::string& localPath,
                            const std::string& caption, const UltraIMSendOptions& options) override;
    UltraIMResult SendVideo(const std::string& conversationId, const std::string& localPath,
                            const std::string& caption, const UltraIMSendOptions& options) override;
    UltraIMResult SendVoiceNote(const std::string& conversationId, const std::string& localPath,
                                const UltraIMSendOptions& options) override;
    UltraIMResult SendFile(const std::string& conversationId, const std::string& localPath,
                           const UltraIMSendOptions& options) override;
    // Read here (the count goes to 0): IRC has no read receipts.
    UltraIMResult MarkRead(const std::string& conversationId, const std::string& upToMessageId) override;
    // +typing on a TAGMSG (message-tags); nothing without it.
    UltraIMResult SetTyping(const std::string& conversationId, bool isTyping) override;

    void SetTimings(const IrcTimings& timings) { timings_ = timings; }
    // The TLS settings for the next connections: a CA file to trust
    // (caBundlePath, as for a server with its own certificate authority), a
    // client certificate. The certificate and the host name are verified
    // whatever these say.
    void SetTlsOptions(const UltraNetTlsOptions& options);
    // "ircs://host:port" of the session ("" before one).
    std::string GetServerAddress() const;
    // The IRCv3 capabilities the server enabled for this connection.
    std::vector<std::string> GetEnabledCaps() const;

private:
    enum class ChatKind { Channel, Private, Server };
    struct Chat {
        std::string id;                         // folded
        std::string name;                       // as written: "#Chat", "Bob"
        ChatKind    kind = ChatKind::Private;
        bool        joined = false;             // a channel we are in
        bool        hidden = false;             // a private chat left here
        bool        historyAsked = false;       // CHATHISTORY LATEST sent since joining
        bool        wanted = false;             // a channel to be in: joined again after a lost connection
        std::string topic;
        std::map<std::string, std::string> members;     // folded nick -> nick
        std::map<std::string, std::string> names;       // NAMES coming in
        bool        namesOpen = false;
        std::vector<UltraIMMessage> history;    // oldest first
        int         unread = 0;
        UltraIMPresence presence = UltraIMPresence::Unknown;
    };
    struct IrcTask {
        std::function<void()> run;
        bool locked = true;                     // run holding mutex_
    };
    using Clock = std::chrono::steady_clock;

    struct PendingJoin {
        std::string channel;                    // folded
        std::vector<std::string> invite;        // CreateGroup's members
        UltraIMConversationCallback onResult;
        Clock::time_point deadline;
    };
    struct PendingWhois {
        std::string nick;                       // folded
        std::string written;
        bool found = false;
        UltraIMConversationCallback onResult;
        Clock::time_point deadline;
    };
    struct PendingHistory {
        std::string target;                     // folded
        std::string label;
        std::string beforeId;
        int limit = 50;
        bool onJoin = false;
        UltraIMMessagesCallback onResult;
        Clock::time_point deadline;
    };
    struct PendingPart {
        std::string channel;
        UltraIMResultCallback onResult;
        Clock::time_point deadline;
    };
    struct Outgoing {
        std::string clientId;
        std::string conversationId;
        std::string target;                     // folded
        // The lines not echoed yet, in order: (label - with labeled-response, the text).
        std::deque<std::pair<std::string, std::string>> lines;
        std::string serverId;                   // the first line's msgid
        int64_t     serverTimeMs = 0;
        UltraIMMessage message;
        Clock::time_point deadline;
        bool silent = false;                    // not shown, its echo swallowed (IDENTIFY to NickServ)
    };
    struct Batch {
        std::string type;
        std::vector<std::string> params;
        std::string label;
        std::string parent;
        std::vector<IrcMessage> lines;
    };
    struct Session {
        std::string server;                     // "ircs://host:port"
        std::string nick;
        std::string password;
        std::string account;
        std::vector<std::string> channels;      // as written
        std::vector<std::string> queries;       // nicks of the private chats open
    };

    // ---- the thread: every line read and written here, in order ------------
    void Enqueue(std::function<void()> job, bool locked = true);
    void Run();
    void Post(std::function<void()> fn);        // run after the current line, without the lock
    void Queue(const UltraIMEvent& event);
    void QueueState(UltraIMConnectionState state, const std::string& error = std::string());
    void SetState(UltraIMConnectionState state, const std::string& error = std::string());
    void Flush();
    void Emit(const UltraIMEvent& event);

    // ---- connecting --------------------------------------------------------
    void SignInOnThread(UltraIMCredentials credentials, UltraIMResultCallback onResult);
    UltraIMResult ConnectAndRegister();         // without the lock
    void ResetConnectionState();
    void BeginRegistration();
    void OnReady();
    void ConnectionLost(const std::string& why);
    void Reconnect();
    void Housekeeping();
    bool SendLine(const std::string& line);
    bool SendCommand(const std::string& command, const std::vector<std::string>& params,
                     const std::map<std::string, std::string>& tags = {});
    std::string NewLabel();
    bool CapOn(const std::string& cap) const { return enabledCaps_.count(cap) != 0; }
    bool HasChatHistory() const;
    void FinishCaps();
    void SendSaslPlain();
    void TryNextNick(const std::string& taken);

    bool SaveSession();
    bool LoadSession(Session& out) const;
    Session CurrentSession() const;

    // ---- lines -------------------------------------------------------------
    void HandleLine(const std::string& raw);
    void HandleMessage(const IrcMessage& m);
    void OnCap(const IrcMessage& m);
    void OnJoin(const IrcMessage& m);
    void OnPart(const std::string& nick, const std::string& channel, bool kicked, const std::string& by,
                const std::string& reason);
    void OnQuit(const IrcMessage& m);
    void OnNick(const IrcMessage& m);
    void OnPrivmsg(const IrcMessage& m, bool notice);
    void OnTagmsg(const IrcMessage& m);
    void OnBatchEnd(const std::string& ref);
    void OnServerReply(const IrcMessage& m);
    bool OnJoinError(const IrcMessage& m);
    void ResolveHistory(PendingHistory pending, const std::vector<IrcMessage>& lines);
    void FailHistory(const std::string& target);
    bool MatchEcho(const IrcMessage& m, const std::string& target, const std::string& text);
    void FailOutgoing(const std::string& target, const std::string& why);

    // ---- conversations and messages ------------------------------------------
    std::string Fold(const std::string& name) const { return IrcFold(name, caseMapping_); }
    bool IsChannelName(const std::string& name) const;
    bool IsMe(const std::string& nick) const;
    Chat& ChatFor(const std::string& name, ChatKind kind, bool* created = nullptr);
    Chat* FindChat(const std::string& id);
    Chat& ServerChat();
    UltraIMConversation ConversationOf(const Chat& chat) const;
    void QueueConversation(const Chat& chat);
    // A message from a line (PRIVMSG / NOTICE); false for one not to show (a CTCP request).
    // live: as it comes (a CTCP request is answered); else from history.
    bool MessageFrom(const IrcMessage& m, bool notice, bool live, UltraIMMessage& out, std::string& conversationName,
                     ChatKind& kind);
    std::vector<UltraIMMessage> MemoryPage(const Chat& chat, const std::string& beforeId, int limit) const;
    std::string EffectiveLabel(const IrcMessage& m) const;
    void DropPending(const UltraIMResult& why);
    void FillReply(const Chat& chat, UltraIMMessage& message) const;
    void Remember(Chat& chat, const UltraIMMessage& message);
    void Receive(Chat& chat, const UltraIMMessage& message, bool created);
    void ServerNote(const std::string& text, const std::string& from = std::string());
    void SystemLine(Chat& chat, const std::string& text);
    std::string NewMessageId();
    // Now by the server's clock (as its server-time tags tell it), else this machine's.
    int64_t ServerNow() const;
    void UpdatePresence(const std::string& foldedNick);
    UltraIMPresence PresenceOf(const std::string& foldedNick) const;
    void Monitor(const std::string& nick, bool add);
    void StartJoin(const std::string& channel, const std::string& key, std::vector<std::string> invite,
                   UltraIMConversationCallback onResult);
    void AskLatestHistory(Chat& chat);
    void SendOn(const std::string& conversationId, const std::string& text, const UltraIMSendOptions& options,
                const std::string& clientId);
    size_t PayloadLimit(const std::string& target, bool action) const;
    UltraIMResult NoFiles() const;

    std::string      providerId_;
    UltraIMConfig    config_;
    UltraIMEventSink sink_;
    IrcTimings       timings_;
    UltraNetTlsOptions tlsOptions_;
    std::atomic<UltraIMConnectionState> state_{UltraIMConnectionState::Disconnected};
    std::atomic<bool> chatHistoryOn_{false};

    mutable UltraVault::DeviceKeyVault vault_;
    bool vaultReady_ = false;

    // Everything below: mutex_ (the thread holds it while it handles a line
    // or runs a job; the callers' threads while they read).
    mutable std::mutex mutex_;
    IrcConnection conn_;
    IrcServerAddress address_;
    bool        haveSession_ = false;
    std::string wantedNick_;
    std::string password_;
    std::string account_;
    std::string nick_;                          // in use
    std::string myUser_, myHost_;               // as the server shows us (our JOIN)
    std::string nickNote_;                      // "alice was taken: you are alice_"
    int         nickTries_ = 0;
    std::map<std::string, std::string> offeredCaps_;    // name -> value
    std::set<std::string> enabledCaps_;
    bool        capLsDone_ = false;
    int         capReqsOpen_ = 0;
    bool        capEnded_ = false;
    bool        saslOk_ = false;
    bool        registered_ = false;
    bool        ready_ = false;
    bool        regFailed_ = false;
    UltraIMResult regFailure_;
    Clock::time_point welcomedAt_{};
    IrcCaseMapping caseMapping_ = IrcCaseMapping::Rfc1459;
    std::string chanTypes_ = "#&";
    std::string prefixSymbols_ = "~&@%+";
    std::string network_;
    int         monitorLimit_ = 0;
    int         historyLimit_ = 0;              // 005 CHATHISTORY (0: not said)
    Clock::time_point lastHeard_{};
    bool        pingOut_ = false;
    Clock::time_point pingSentAt_{};
    bool        wanted_ = false;                // stay connected: reconnect when lost
    int         reconnectTries_ = 0;
    Clock::time_point reconnectAt_{};
    std::vector<std::string> resumeQueries_;    // private chats of the stored session, opened once ready
    std::string serverError_;                   // the last ERROR line's text
    int64_t     clockOffsetMs_ = 0;             // the server's clock minus ours (server-time)
    uint64_t    counter_ = 0;
    std::map<std::string, Chat> chats_;         // id -> chat
    std::map<std::string, std::chrono::steady_clock::time_point> typingSent_;
    std::set<std::string> away_;                // folded nicks
    std::map<std::string, bool> monitorOnline_; // folded nick -> online (MONITOR said)
    std::set<std::string> quit_;                // folded nicks seen quitting
    std::set<std::string> monitored_;           // folded
    std::set<std::string> serverIds_;           // message ids the server gave (msgid)
    std::vector<PendingJoin> joins_;
    std::vector<PendingWhois> whois_;
    std::deque<PendingHistory> history_;
    std::vector<PendingPart> parts_;
    std::vector<Outgoing> outgoing_;
    std::map<std::string, Batch> batches_;
    std::vector<std::function<void()>> outbox_;

    // The thread.
    std::mutex workMutex_;
    std::condition_variable workWake_;
    std::deque<IrcTask> jobs_;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace UltraIMIrc
