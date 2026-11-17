// Plugins/UltraIM/xmpp/XmppProvider.h
// XMPP (Jabber) - an UltraIM provider for any XMPP server, over XMPP's
// WebSocket binding (RFC 7395) through UltraNet: wss://, where libcurl
// carries the TLS. The classic TCP binding (STARTTLS on port 5222, or
// direct TLS on 5223, through UltraNet_TlsWrap) is not used yet.
//
// Here:
//
//   - the server is found from the address's domain (XEP-0156: the
//     WebSocket link of https://<domain>/.well-known/host-meta.json, else of
//     host-meta's XML); credentials.server overrides it with the WebSocket's
//     address itself (wss://...) or another place to look (a domain, an
//     https:// address). ws:// without TLS only to this computer (tests);
//   - a 1:1 conversation is a chat with a contact, its id the contact's bare
//     JID ("bob@example.org"); the contacts are the roster (RFC 6121), with
//     their presence; a group is a multi-user chat room (XEP-0045), its id
//     the room's bare JID ("team@conference.example.org"), the rooms the
//     account's bookmarks (XEP-0402, else XEP-0048) and joined at sign-in;
//   - a message's id is its origin-id (XEP-0359) - our own messages' is the
//     clientMessageId they were sent under - else its id attribute, else the
//     server's stanza-id; the archive's id (stanza-id) is known alongside, for
//     paging the history (XEP-0313 MAM with RSM);
//   - receipts (XEP-0184: Delivered) and displayed markers (XEP-0333: Read;
//     sent by MarkRead), chat states (XEP-0085: typing), corrections
//     (XEP-0308: edits), retractions (XEP-0424: deleting for everyone),
//     replies (XEP-0461), message carbons (XEP-0280: what this account's other
//     clients send and receive), files by HTTP File Upload (XEP-0363, with
//     the link in an XEP-0066 oob element);
//   - not here: OMEMO - messages are protected by TLS between each client
//     and its server, not end to end, and the servers can read them;
//     Jingle calls; stream management (XEP-0198: a dropped connection
//     reconnects and signs in again).
//
// Sign-in is the bare JID (credentials.username, "erin@example.org") and
// the password, by SASL SCRAM-SHA-256, else SCRAM-SHA-1, else PLAIN (only
// over TLS, or to this computer). What is kept in UltraVault is not the
// password but SCRAM's ClientKey and ServerKey with the salt and iteration
// count they were derived with (and the JID and the WebSocket address): a
// SignIn with empty credentials signs in with them. A server that offers
// PLAIN alone gets nothing kept - the password is asked for at the next
// start. A changed password makes the kept keys useless: AuthRequire.
//
// Kept simple: a request to see our presence (subscribe) is approved, and
// someone not yet among the contacts is added and asked back; an invitation
// to a room is accepted and the room bookmarked.
//
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include "XmppScram.h"
#include "XmppStream.h"
#include "XmppXml.h"

#include "UltraIM/UltraIMPlugins.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"

#include <atomic>
#include <chrono>
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

namespace UltraIMXmpp {

struct XmppTimings {
    int requestTimeoutMs = 30000;   // an iq's answer
    int connectTimeoutMs = 15000;
    int pingIntervalMs = 30000;     // the WebSocket's pings (a silent connection checked)
    int reconnectFirstMs = 1000;    // after a dropped connection; doubled each time
    int reconnectMaxMs = 60000;
};

class XmppProvider : public IUltraIMProviderPlugin {
public:
    explicit XmppProvider(std::string providerId = "xmpp");
    ~XmppProvider() override;

    std::string GetProviderId() const override { return providerId_; }
    std::string GetDisplayName() const override { return "XMPP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    UltraIMCapabilityFlags GetCapabilities() const override;
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::UsernamePassword; }

    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override;
    void Shutdown() override;

    // credentials.username: the bare JID; password. server: "" (found from
    // the JID's domain), the WebSocket's address (wss://...), or where to
    // look for it (a domain, https://...). Empty credentials (or the stored
    // JID with no password): the stored keys.
    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override;
    // Disconnects; the stored keys stay for the next sign-in.
    void SignOut() override;
    UltraIMConnectionState GetConnectionState() const override { return state_.load(); }
    // The bare JID.
    std::string GetLocalUserId() const override;
    bool HasStoredSession() const override;

    void FetchConversations(UltraIMConversationsCallback onResult) override;
    // The server's archive (MAM), oldest first; beforeMessageId the page before it.
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override;
    // In one conversation (the archive read page by page); all at once: CapabilityMissing.
    void SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) override;
    // The roster, with presence.
    void FetchContacts(UltraIMContactsCallback onResult) override;
    // address: a JID ("bob@example.org", "xmpp:bob@example.org"). A person is
    // added to the roster and asked to share presence; a room (it answers
    // service discovery as a conference) is joined and bookmarked.
    void AddContact(const std::string& address, UltraIMConversationCallback onResult) override;
    // A members-only room on the server's MUC service, named title; the
    // members (bare JIDs) made members and invited; bookmarked.
    void CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                     UltraIMConversationCallback onResult) override;
    // A room: left and its bookmark removed. A chat: hidden on this device
    // (it comes back with its next message; the archive keeps it).
    void LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) override;

    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override;
    // Uploaded to the server's HTTP upload service (TransferProgress), then
    // the link sent (with an oob element; a caption as its description).
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
    // One's own messages (a correction).
    UltraIMResult EditMessage(const std::string& conversationId, const std::string& messageId,
                              const std::string& newText) override;
    // For everyone, one's own (a retraction): XMPP has no "for me".
    UltraIMResult DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                UltraIMDeleteScope scope) override;
    UltraIMDeleteChoices GetDeleteChoices(const std::string& conversationId,
                                          const std::string& messageId) const override;
    // A displayed marker for the last message received (or upToMessageId).
    UltraIMResult MarkRead(const std::string& conversationId, const std::string& upToMessageId) override;
    UltraIMResult SetTyping(const std::string& conversationId, bool isTyping) override;

    void SetTimings(const XmppTimings& timings) { timings_ = timings; }
    // The WebSocket's address in use (or last used); the full JID bound.
    std::string GetWebSocketUrl() const;
    std::string GetBoundJid() const;

private:
    struct Contact {
        std::string jid;                // bare
        std::string name;               // the roster's name ("" = none given)
        std::string subscription = "none";
        bool        asked = false;      // our subscribe is pending
        bool        inRoster = false;
        std::map<std::string, UltraIMPresence> resources;   // online resources
        UltraIMPresence presence = UltraIMPresence::Offline;
    };
    struct Occupant {
        std::string nick;
        std::string jid;                // the real bare JID, when the room tells it
        UltraIMPresence presence = UltraIMPresence::Online;
    };
    struct Chat {
        std::string id;                 // bare JID of the contact or the room
        bool        isGroup = false;
        std::string title;              // a room's name
        // A room.
        std::string nick;
        bool        joined = false;
        std::map<std::string, Occupant> occupants;          // nick -> occupant
        std::map<std::string, std::string> affiliations;    // bare JID -> owner / admin / member
        int64_t     lastSeenMs = 0;     // the newest message seen in it (to ask a rejoin's history since)
        // Both.
        UltraIMMessage last;
        bool        hasLast = false;
        int         unread = 0;
        std::string lastReceivedId;     // the newest message from someone else
    };
    // A message as seen, with what the wire knows it by.
    struct Known {
        UltraIMMessage message;
        std::string wireId;             // the stanza's id attribute
        std::string archiveId;          // the server's stanza-id (MAM)
        std::string from;               // the full JID it came from
    };
    // What signs in (again).
    struct Account {
        std::string jid;                // bare
        std::string url;                // the WebSocket's
        std::string password;           // in memory only, for a reconnect; "" with keys
        ScramKeys   keys;
        bool        hasKeys = false;
    };

    // ---- the worker -------------------------------------------------------
    void Enqueue(std::function<void()> job);
    void EnqueueAfter(int delayMs, std::function<void()> job);
    void WorkLoop();
    void Emit(const UltraIMEvent& event);
    void SetState(UltraIMConnectionState state, const std::string& error = std::string());

    // ---- signing in -------------------------------------------------------
    void SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult);
    std::string FindWebSocket(const std::string& domain, const std::string& server, std::string& error);
    std::string Discover(const std::string& base);
    UltraIMResult Connect(Account& account, bool fresh);
    UltraIMResult Authenticate(XmppStream& stream, const XmlElement& features, Account& account, bool secure);
    void AfterBind(std::shared_ptr<XmppStream> stream, bool fresh);
    void Dropped(uint64_t generation, const std::string& why);
    void Reconnect(uint64_t generation);
    bool SaveAccount(const Account& account);
    bool LoadAccount(Account& account) const;
    void ForgetAccount();
    void LoadLocalState();
    void SaveLocalState();
    std::shared_ptr<XmppStream> Stream() const;
    bool Send(const XmlElement& element);
    bool Iq(const XmlElement& iq, XmlElement& reply);

    // ---- what arrives (the socket's thread) -----------------------------------
    void HandleElement(uint64_t generation, const XmlElement& element);
    void HandleMessage(const XmlElement& message);
    void HandlePresence(const XmlElement& presence);
    void HandleIq(const XmlElement& iq);
    void HandleRoomPresence(const XmlElement& presence, const std::string& room);
    void HandleChatMessage(const XmlElement& message, const std::string& conversationId, bool isOwn,
                           bool isGroup, const std::string& archiveId, int64_t timestampMs, bool live,
                           std::vector<UltraIMEvent>& events, std::vector<UltraIMMessage>& media,
                           std::vector<XmlElement>& replies);
    void ApplyRosterItem(const XmlElement& item, std::vector<UltraIMEvent>& events);
    void Receipt(const std::string& conversationId, const std::string& id, UltraIMDeliveryStatus status,
                 std::vector<UltraIMEvent>& events);

    // ---- messages ------------------------------------------------------------
    // A stanza turned into a message (false: not one to show - a marker, a
    // chat state, a retraction). (mutex_ held.)
    bool MessageFrom(const XmlElement& m, const std::string& conversationId, bool isOwn, bool isGroup,
                     const std::string& archiveId, int64_t timestampMs, Known& out) const;
    std::string Resolve(const std::string& id) const;                       // any id -> the message's id
    UltraIMConversation ConversationOf(const Chat& chat) const;
    Chat& ChatFor(const std::string& conversationId, bool isGroup, bool& created);
    std::string ContactName(const std::string& jid) const;
    void Remember(const Known& known);
    void QueueMedia(const UltraIMMessage& message);
    std::string MediaPath(const std::string& url, const std::string& fileName) const;
    bool Download(const std::string& url, const std::string& toPath, const UltraIMMessage& forMessage);
    UltraIMResult SendContent(const std::string& conversationId, const std::string& text, const std::string& localPath,
                              UltraIMMessageType type, const UltraIMSendOptions& options);
    XmlElement NewMessage(const std::string& conversationId, const std::string& id, bool& isGroup) const;
    void Delivered(const std::string& conversationId, const UltraIMMessage& sent);
    void Failed(const std::string& conversationId, const std::string& clientId, const std::string& why);

    // ---- services, rooms, bookmarks -------------------------------------------
    void DiscoverServices();
    bool IsConference(const std::string& jid, std::string& name);
    UltraIMResult JoinRoom(const std::string& room, std::string nick, bool bookmark);
    void LoadRoomDetails(const std::string& room);
    void LoadBookmarks(std::vector<std::pair<std::string, std::string>>& rooms);  // room -> nick
    void Bookmark(const std::string& room, const std::string& name, const std::string& nick, bool add);
    void EmitConversation(const std::string& conversationId);

    std::string       providerId_;
    UltraIMConfig     config_;
    UltraIMEventSink  sink_;
    XmppTimings       timings_;
    std::atomic<UltraIMConnectionState> state_{UltraIMConnectionState::Disconnected};

    mutable UltraVault::DeviceKeyVault vault_;
    bool vaultReady_ = false;

    mutable std::mutex mutex_;          // everything below
    Account     account_;               // the signed-in (or signing-in) account
    std::string boundJid_;              // full
    std::string resource_;
    std::shared_ptr<XmppStream> stream_;
    uint64_t    generation_ = 0;        // the stream's; a drop of an older one is old news
    bool        signedIn_ = false;      // reconnect after a drop
    int         backoffMs_ = 0;
    std::string uploadService_;
    int64_t     uploadMaxBytes_ = 0;
    std::string mucService_;
    bool        bookmarksLegacy_ = false;   // XEP-0048 private storage, not PEP
    std::map<std::string, std::pair<std::string, std::string>> bookmarks_;  // room -> (name, nick)
    std::map<std::string, Contact> contacts_;
    std::map<std::string, Chat> chats_;
    std::set<std::string> hidden_;      // chats left (hidden) on this device
    std::map<std::string, Known> byId_;
    std::map<std::string, std::string> aliases_;    // wire id / archive id -> message id
    std::map<std::string, std::vector<XmlElement>> mam_;    // a running archive query -> what it found
    std::map<std::string, std::string> pendingSends_;       // wire id -> conversation (an error fails it)

    // The worker.
    std::mutex workMutex_;
    std::condition_variable workWake_;
    std::deque<std::function<void()>> jobs_;
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> timers_;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace UltraIMXmpp
