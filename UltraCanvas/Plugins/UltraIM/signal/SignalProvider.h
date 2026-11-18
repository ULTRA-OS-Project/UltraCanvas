// Plugins/UltraIM/signal/SignalProvider.h
// Signal - an UltraIM provider for Signal, through signal-cli
// (github.com/AsamK/signal-cli, GPL-3.0, built on Signal's own libsignal)
// running as a daemon with its HTTP JSON-RPC interface. Signal has no API
// of its own for other applications: signal-cli is a linked device of the
// user's phone, as Signal Desktop is, and this provider talks to it.
//
// The daemon: credentials.server, when given, is the address of one the
// user runs ("http://127.0.0.1:7583", or "127.0.0.1:7583": signal-cli
// daemon --http 127.0.0.1:7583 --receive-mode=manual, multi-account mode,
// no -a). Without one the provider starts its own: signal-cli is found
// (ULTRAIM_SIGNAL_CLI, signal-cli/bin/signal-cli next to the provider
// modules - UltraIMConfig::pluginDirectory -, the PATH), started with its
// data in <dataDirectory>/signal/signal-cli on a free loopback port, and
// stopped with Shutdown (SignalDaemon). No signal-cli and no daemon:
// SignIn answers CapabilityMissing. Every call is a JSON-RPC 2.0 request
// POSTed to <daemon>/api/v1/rpc naming its "account".
//
// Sign-in links this device to the phone (UltraIMLoginType::QRCode): when
// the account linked before (UltraVault: its number, and the daemon's
// address when it was given) is still in the daemon's listAccounts, SignIn
// connects at once. Otherwise startLink makes a sgnl://linkdevice?... link:
// SignIn answers NeedsInput, GetAuthStep() is OtherDevice and GetAuthHint()
// the link (an AuthStepChange with the link as errorText follows) - shown as
// a QR code for the Signal app on the phone to scan (Settings > Linked
// devices). finishLink waits for the scan on the provider's listening
// thread; once it returns the account is linked and the provider Connected
// (a ConnectionStateChange; the SignIn was answered already). A link that
// times out is replaced by a new one (a new AuthStepChange). SignOut keeps
// the link: the phone lists the device until it is removed there.
//
// Here:
//
//   - a 1:1 conversation's id is the other person's phone number
//     ("+4915112345678"), or their ACI (a UUID) when Signal gives no number;
//     directContactId the same. A group's id is "group:" + its group id as
//     signal-cli gives it (base64). Conversations come from listGroups, the
//     contacts the account talks to (listContacts: profile shared) and every
//     message received or sent;
//   - a message's id is "<author>|<timestamp>": its author's number (or
//     ACI) and the timestamp (ms) Signal knows it by - quotes, edits,
//     deletes and receipts name it so. Our own: "<own number>|<timestamp>";
//   - news comes by polling receive (manual receive mode) on the listening
//     thread: texts, attachments, replies (quotes), edits, remote deletes,
//     delivery and read receipts, typing, messages sent from the phone (sync
//     copies, isOwn);
//   - signal-cli keeps no history: FetchMessages answers what this session
//     received and sent (the newest SignalTimings::historyLimit of each
//     conversation), SearchMessages searches that;
//   - attachments go from their local path (TransferProgress 0, then all:
//     signal-cli tells no progress); received pictures are fetched by
//     themselves (a MessageUpdate with localPath), other files on
//     DownloadAttachment - from signal-cli's attachments folder when it is
//     this provider's daemon, else by getAttachment;
//   - AddContact takes an international number ("+49 151 1234567") or a
//     Signal username ("name.01"); CreateGroup makes a group (updateGroup);
//     LeaveConversation leaves a group (quitGroup), or hides a 1:1 one on
//     this device until its next message. No calls, stickers or reactions.
//
// Version: 0.1.1 - a receipt that comes before the send is recorded is kept and applied, not lost
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include "SignalDaemon.h"

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

namespace UltraIMSignal {

struct SignalTimings {
    // receive's wait for news (seconds, 1..5): it answers at once when a
    // message is there, else after this long, and is asked again.
    int receiveTimeoutSec = 5;
    // How long a daemon the provider started may take to answer /api/v1/check.
    int daemonStartMs = 90000;
    // After a failed receive (the daemon gone), the wait before the next.
    int retryMs = 2000;
    // Messages kept per conversation for FetchMessages and search.
    int historyLimit = 2000;
};

// The answer of one JSON-RPC call.
struct SignalResponse {
    int                    status = 0;      // HTTP; 0: the request never completed
    UltraCanvas::JSONValue result;
    bool                   rpcError = false;
    int64_t                errorCode = 0;   // JSON-RPC error code
    std::string            error;           // its message, or the transfer's

    bool Ok() const { return status >= 200 && status < 300 && !rpcError; }
};

class SignalProvider : public IUltraIMProviderPlugin {
public:
    explicit SignalProvider(std::string providerId = "signal");
    ~SignalProvider() override;

    std::string GetProviderId() const override { return providerId_; }
    std::string GetDisplayName() const override { return "Signal"; }
    std::string GetVersion() const override { return "0.1.0"; }
    UltraIMCapabilityFlags GetCapabilities() const override;
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::QRCode; }

    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override;
    // Signs out, stops the threads and the daemon the provider started.
    void Shutdown() override;

    // credentials.server: a signal-cli daemon's address ("" = the one used
    // before, else the provider's own). credentials.phoneNumber (optional):
    // an account the daemon has linked already, used without a QR code
    // (signal-cli linked or registered by hand). Nothing else is asked: the
    // link is a QR code (see the file header).
    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override;
    // Disconnects; the link stays (on the phone and here).
    void SignOut() override;
    UltraIMConnectionState GetConnectionState() const override { return state_.load(); }
    // The account's phone number (its ACI when it has none).
    std::string GetLocalUserId() const override;
    void FetchAccountProfile(UltraIMAccountProfileCallback onResult) override;
    bool HasStoredSession() const override;
    UltraIMAuthStep GetAuthStep() const override;
    std::string GetAuthHint() const override;

    void FetchConversations(UltraIMConversationsCallback onResult) override;
    // What this session received and sent, oldest first; beforeMessageId the
    // page before that message.
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override;
    // One conversation at a time; every conversation at once: CapabilityMissing.
    void SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) override;
    void FetchContacts(UltraIMContactsCallback onResult) override;
    // "+4915112345678" (spaces, dashes and brackets allowed; 00 for +) or a
    // Signal username ("name.01", "@name.01"); NotFound when not on Signal.
    void AddContact(const std::string& address, UltraIMConversationCallback onResult) override;
    // A new group; members as AddContact takes them, or contact ids.
    void CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                     UltraIMConversationCallback onResult) override;
    // A group: left (quitGroup). A 1:1 conversation: hidden here with its
    // history, back with its next message.
    void LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) override;

    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override;
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
    // One's own messages (send with editTimestamp).
    UltraIMResult EditMessage(const std::string& conversationId, const std::string& messageId,
                              const std::string& newText) override;
    // For everyone (remoteDelete), one's own, within a day as Signal allows;
    // "for me" is not offered (CapabilityMissing).
    UltraIMResult DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                UltraIMDeleteScope scope) override;
    UltraIMDeleteChoices GetDeleteChoices(const std::string& conversationId,
                                          const std::string& messageId) const override;
    // Read receipts (sendReceipt type read) to the senders of what was unread.
    UltraIMResult MarkRead(const std::string& conversationId, const std::string& upToMessageId) override;
    UltraIMResult SetTyping(const std::string& conversationId, bool isTyping) override;

    void SetTimings(const SignalTimings& timings) { timings_ = timings; }
    // The daemon in use ("http://127.0.0.1:<port>"; "" before SignIn).
    std::string GetDaemonUrl() const;
    // The process id of the daemon this provider started (0 when it uses
    // someone else's, or none runs).
    int64_t GetDaemonProcessId() const;

private:
    struct Contact {
        std::string key;            // the number, else the ACI
        std::string number;
        std::string uuid;
        std::string username;
        std::string name;           // contact name, nickname or profile name
        std::string seenName;       // the sourceName of their messages
    };
    struct Chat {
        std::string id;
        bool        isGroup = false;
        std::string groupId;                    // groups: signal-cli's (base64)
        std::string contactKey;                 // 1:1: the other person
        std::string groupName;
        std::vector<std::string> members;       // groups: their keys (with ours)
        UltraIMMessage last;
        bool        hasLast = false;
        int         unread = 0;
    };
    enum class ListenMode { Idle, Link, Receive };

    // ---- threads ---------------------------------------------------------------
    void Enqueue(std::function<void()> job);
    void WorkLoop();
    void ListenLoop();
    void SetListenMode(ListenMode mode);

    // ---- the daemon ------------------------------------------------------------
    SignalResponse Rpc(const std::string& method, const UltraCanvas::JSONValue& params, int timeoutMs = 30000);
    bool DaemonAnswers(const std::string& url, int timeoutMs);
    UltraIMResult EnsureDaemon(const std::string& server);
    std::string DataPath(const std::string& name) const;
    std::string ConfigDirectory() const;

    // ---- signing in --------------------------------------------------------------
    void Emit(const UltraIMEvent& event);
    void SetState(UltraIMConnectionState state, const std::string& error = std::string());
    void SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult);
    UltraIMResult StartLink(std::string& uri);
    void AskForLink(const std::string& uri, const UltraIMResultCallback& onResult);
    void LinkOnListener(uint64_t generation);
    void Connect(const std::string& number, const std::string& aci, const UltraIMResultCallback& onResult);
    bool SaveSession();
    bool LoadSession(std::string& daemon, std::string& number, std::string& aci,
                     std::set<std::string>* hidden = nullptr) const;

    // ---- conversations and messages ------------------------------------------------
    void LoadDirectory(std::vector<UltraIMEvent>& events);
    bool RefreshGroup(const std::string& groupId, std::vector<UltraIMEvent>& events);
    void ReadGroup(const UltraCanvas::JSONValue& g, std::vector<UltraIMEvent>& events);   // mutex_ held
    void ReceiveOnListener(uint64_t generation);
    void HandleEnvelope(const UltraCanvas::JSONValue& item, std::vector<UltraIMEvent>& events,
                        std::vector<UltraIMMessage>& media);
    std::string KeyOf(const std::string& number, const std::string& uuid);            // mutex_ held
    std::string NameOf(const std::string& key) const;                                 // mutex_ held
    Chat& ChatFor(const std::string& conversationId, bool isGroup, const std::string& key,
                  std::vector<UltraIMEvent>& events);                                 // mutex_ held
    UltraIMMessage MessageFrom(const UltraCanvas::JSONValue& dm, const std::string& author, int64_t timestamp,
                               const std::string& conversationId, bool isOwn) const;  // mutex_ held
    UltraIMConversation ConversationOf(const Chat& chat) const;                       // mutex_ held
    void Remember(const UltraIMMessage& m);                                           // mutex_ held
    UltraIMMessage* FindMessage(const std::string& messageId);                        // mutex_ held
    const UltraIMMessage* FindMessage(const std::string& messageId) const;            // mutex_ held
    void RefreshLast(Chat& chat);                                                     // mutex_ held
    void Target(const Chat& chat, UltraCanvas::JSONValue& params) const;              // mutex_ held
    UltraIMResult ResolveAddress(const std::string& address, Contact& out);
    void QueueMedia(const UltraIMMessage& message);
    UltraIMResult FetchFile(UltraIMMessage& m);
    std::string MediaPath(const std::string& id, const std::string& fileName) const;
    UltraIMResult SendSignal(const std::string& conversationId, const std::string& text, const std::string& localPath,
                              UltraIMMessageType type, const UltraIMSendOptions& options);

    std::string       providerId_;
    UltraIMConfig     config_;
    UltraIMEventSink  sink_;
    SignalTimings     timings_;
    std::atomic<UltraIMConnectionState> state_{UltraIMConnectionState::Disconnected};

    mutable UltraVault::DeviceKeyVault vault_;
    bool vaultReady_ = false;

    SignalDaemon daemon_;               // the provider's own signal-cli, when it started one
    std::atomic<uint64_t> rpcId_{0};

    mutable std::mutex mutex_;          // everything below
    std::string daemonUrl_;             // "http://127.0.0.1:7583"
    bool        ownDaemon_ = false;     // daemonUrl_ is daemon_'s
    std::string account_;               // the number, else the ACI: every call's "account"
    std::string number_, aci_;
    UltraIMAuthStep authStep_ = UltraIMAuthStep::Idle;
    std::string authHint_;              // the sgnl://linkdevice link while linking
    std::map<std::string, Contact> contacts_;               // key -> contact
    std::map<std::string, std::string> numberOfUuid_;       // ACI -> number, where both are known
    std::map<std::string, Chat> chats_;                     // conversation id -> chat
    std::set<std::string> hidden_;                          // 1:1 conversations left here
    std::map<std::string, std::deque<UltraIMMessage>> history_;  // conversation id -> its messages, oldest first
    std::map<std::string, std::string> conversationOf_;     // message id -> conversation id
    std::map<std::string, std::string> clientOf_;           // our message id -> the client id it was sent under
    // A receipt for one of our messages that came before the send was
    // recorded (the other side answered while "send" was still returning):
    // applied when it is. Our message id -> what the receipt said.
    std::map<std::string, UltraIMDeliveryStatus> earlyReceipts_;
    std::set<std::string> receipted_;                       // received message ids whose read receipt went

    // The worker: every call but receive and finishLink, in order.
    std::mutex workMutex_;
    std::condition_variable workWake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::atomic<bool> stopFlag_{false};
    std::thread worker_;

    // The listening thread: finishLink while linking, receive while connected.
    std::mutex listenMutex_;
    std::condition_variable listenWake_;
    ListenMode listenMode_ = ListenMode::Idle;
    std::atomic<uint64_t> listenGeneration_{0};
    std::thread listener_;
};

} // namespace UltraIMSignal
