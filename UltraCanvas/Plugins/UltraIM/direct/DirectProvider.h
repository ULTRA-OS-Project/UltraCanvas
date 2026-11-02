// Plugins/UltraIM/direct/DirectProvider.h
// ChatCube Direct - the ULTRA OS native messenger as an UltraIM provider:
// key-pair identity, end-to-end encrypted messages peer to peer over WebRTC
// data channels, the relay for rendezvous, the offline queue and groups, and
// the local network for all of that between two ChatCubes with no relay
// A picture, a video or a file travels as sealed envelopes like a text: a
// "file" body with its name, size and preview picture, then the bytes in
// numbered parts (32 KiB each, one envelope per part), peer to peer over
// the link when it is up, else through the relay's queue - so a file reaches
// someone offline, and a group, with nothing added to the relay.
//
// The identity's key vault opens by itself with the device key, or - as its
// owner chooses (SetSignInProtection) - only with a password (the vault
// encrypted under it: Argon2id, XChaCha20-Poly1305), and/or once the code
// of an authenticator app (UltraAuthenticator; TOTP, RFC 6238) is typed.
// SignIn then goes in steps, as Telegram's does: NeedsInput with the auth
// step Password, then Code. The e-mail address and phone number the owner
// gives (to recover the account) are kept in the vault and on the relay,
// for the owner alone.
// Version: 0.1.3 - the account's profile (name, e-mail, phone); a password and/or an authenticator code to sign in
// Version: 0.1.2 - pictures, videos and files sent and received in parts; DownloadAttachment
// Version: 0.1.1 - FetchSharedCredentials: the Telegram application's keys from the relay
// Version: 0.1.0
// Last Modified: 2026-10-29
// Author: UltraCanvas Framework
#pragma once

#include "DirectCalls.h"
#include "DirectCrypto.h"
#include "DirectLan.h"
#include "DirectLinks.h"
#include "DirectRelayClient.h"
#include "DirectStore.h"

#include "UltraIM/UltraIMPlugins.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraIMDirect {

// Timings, overridable for tests.
struct ProviderTimings {
    int64_t linkTimeoutMs = 10000;      // a data channel that is not open by then is abandoned
    int64_t relayFallbackMs = 8000;     // an unconfirmed 1:1 message goes to the relay queue after this
    int64_t linkAckTimeoutMs = 10000;   // a copy sent on a channel and not acknowledged is resent
    int     tickMs = 250;
    int     lanDiscoveryMs = 4000;      // how often the local network is browsed (0: never)
    int64_t lanAddTimeoutMs = 10000;    // adding someone nearby waits this long for the handshake
};

class DirectProvider : public IUltraIMProviderPlugin {
public:
    explicit DirectProvider(std::string providerId = "direct");
    ~DirectProvider() override;

    // ---- identity -----------------------------------------------------------
    std::string GetProviderId() const override { return providerId_; }
    std::string GetDisplayName() const override { return "ChatCube Direct"; }
    std::string GetVersion() const override { return "0.1.0"; }
    UltraIMCapabilityFlags GetCapabilities() const override;
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::KeyPair; }

    // ---- lifecycle ----------------------------------------------------------
    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override;
    void Shutdown() override;

    // ---- session ------------------------------------------------------------
    // credentials.server: the relay URL (remembered) - none at all: the
    // local network only; createAccount makes a new identity on this device
    // when it has none; displayName is shown to contacts; localNetwork also
    // finds and reaches contacts on the local network (remembered).
    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override;
    void SignOut() override;
    UltraIMConnectionState GetConnectionState() const override { return state_.load(); }
    std::string GetLocalUserId() const override;
    bool HasStoredSession() const override;
    // Password, then Code, while a protected sign-in waits for them.
    UltraIMAuthStep GetAuthStep() const override { return authStep_.load(); }
    std::string GetAuthHint() const override;

    // ---- the account's profile and its sign-in protection -------------------------
    void FetchAccountProfile(UltraIMAccountProfileCallback onResult) override;
    // The name to the relay and the local network's next announcement, the
    // e-mail and phone into the vault and to the relay (PUT /v1/profile).
    void UpdateAccountProfile(const UltraIMAccountProfile& profile, UltraIMResultCallback onResult) override;
    UltraIMSignInProtection GetSignInProtection() const override;
    UltraIMResult StartAuthenticatorSetup(std::string& outOtpAuthUri) override;
    // On the worker thread (the password is stretched, twice when it changes).
    void SetSignInProtection(const UltraIMProtectionChange& change, UltraIMResultCallback onResult) override;

    // ---- data ---------------------------------------------------------------
    void FetchConversations(UltraIMConversationsCallback onResult) override;
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override;
    void FetchContacts(UltraIMContactsCallback onResult) override;
    void FetchNearbyContacts(UltraIMContactsCallback onResult) override;
    void AddContact(const std::string& address, UltraIMConversationCallback onResult) override;
    void CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                     UltraIMConversationCallback onResult) override;
    // "telegram": the Telegram application's API ID and hash the relay keeps
    // (GET /v1/telegram-credentials; NotFound when it keeps none).
    void FetchSharedCredentials(const std::string& forProviderId, UltraIMSharedCredentialsCallback onResult) override;

    // ---- sending ------------------------------------------------------------
    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override;
    // A file of ours, in parts (up to 64 MiB), with its preview picture when
    // the application makes one (UltraIM_SetPreviewMaker). The outcome as
    // for a text; our own row is in the store at once, pointing at the file.
    UltraIMResult SendImage(const std::string& conversationId, const std::string& localPath,
                            const std::string& caption, const UltraIMSendOptions& options) override;
    UltraIMResult SendVideo(const std::string& conversationId, const std::string& localPath,
                            const std::string& caption, const UltraIMSendOptions& options) override;
    UltraIMResult SendFile(const std::string& conversationId, const std::string& localPath,
                           const UltraIMSendOptions& options) override;
    // Everything arrives by itself (the sender pushes the parts): this waits
    // for the file when it is still on its way, and answers from disk when
    // it is here.
    void DownloadAttachment(const std::string& conversationId, const std::string& messageId,
                            UltraIMAttachmentCallback onResult) override;
    UltraIMResult MarkRead(const std::string& conversationId, const std::string& upToMessageId) override;
    UltraIMResult SetTyping(const std::string& conversationId, bool isTyping) override;

    // ---- calls (1:1 voice and video) --------------------------------------
    bool CanCall(const std::string& conversationId, bool video) const override;
    UltraIMResult StartCall(const std::string& conversationId, bool video, std::string& outCallId) override;
    UltraIMResult AcceptCall(const std::string& callId, bool video) override;
    UltraIMResult EndCall(const std::string& callId) override;
    UltraIMResult SetCallMuted(const std::string& callId, bool muted) override;
    UltraIMResult SetCallCamera(const std::string& callId, bool on) override;
    UltraIMResult SetCallVideoSinks(const std::string& callId, UltraIMVideoFrameCallback remote,
                                    UltraIMVideoFrameCallback local) override;

    // ---- for tests ----------------------------------------------------------
    void SetTimings(const ProviderTimings& timings) { timings_ = timings; }
    // Whether 1:1 copies have gone over a data channel (true) or the relay.
    int  CountSentOverLink() const { return sentOverLink_.load(); }
    int  CountSentOverRelay() const { return sentOverRelay_.load(); }
    bool IsLinkOpen(const std::string& userId);
    void SetCallTimings(const CallTimings& timings) { calls_.SetTimings(timings); }
    // false: calls do not open the microphone and speaker.
    void SetCallUseDevices(bool useDevices) { calls_.SetUseDevices(useDevices); }
    UltraRtcHandle GetCallAudioTrack(const std::string& callId) { return calls_.GetAudioTrack(callId); }
    UltraRtcHandle GetCallVideoTrack(const std::string& callId) { return calls_.GetVideoTrack(callId); }
    void SetCallVideoConfig(const UltraRtcVideoConfig& config) { calls_.SetVideoConfig(config); }
    // The local network: the port we listen on (0: not running), and an
    // address to reach without mDNS (a person typed it in; tests).
    int  GetLanPort() const { return lan_.GetPort(); }
    void AddLanAddress(const std::string& host, int port) { lan_.AddAddress(host, port); }
    bool IsOnLan(const std::string& userId) { return lan_.IsConnected(userId); }

private:
    struct Attempt {
        int64_t linkSentMs = 0;     // when the copy went on the data channel (0: not yet)
        bool    relayInFlight = false;
    };

    // ---- helpers --------------------------------------------------------------
    void Emit(const UltraIMEvent& event);
    // always: emit the event even when the state does not change.
    void SetState(UltraIMConnectionState state, const std::string& error = std::string(), bool always = false);
    // Signs a signalling message and sends it through the relay.
    bool SendSignal(const std::string& to, const std::string& kind, const std::string& session,
                    const std::string& data);
    // What opening the identity's vault came to.
    enum class VaultOpen { Opened, Empty, NeedsPassword, WrongPassword, Failed };
    // The vault opened (with `password` when it has one), the identity, the
    // authenticator secret and the profile read from it.
    VaultOpen OpenVault(const std::string& password, std::string& error);
    bool SaveIdentity();
    bool SaveProfile();
    // The vault used with the password it was opened with ("" : the device key).
    bool WithVault(const std::function<void(UltraVault::DeviceKeyVault& vault)>& use);
    // `code` checked against `secret`: once per code, after a pause when too
    // many were wrong (kept across starts). `lastStep` the step accepted
    // last (0: none); the one accepted now in `outStep`.
    bool CheckCode(const UltraCryptSecureBuffer& secret, const std::string& code, uint64_t lastStep,
                   uint64_t& outStep, std::string& error);
    void SignInNow(const UltraIMCredentials& credentials, UltraIMResultCallback onResult);
    void ChangeProtection(const UltraIMProtectionChange& change, UltraIMResultCallback onResult);
    void SendProfileToRelay(std::function<void(const UltraIMResult&)> onDone);
    // Runs `job` on the worker thread, between two pumps.
    void Enqueue(std::function<void()> job);
    void AfterLogin();
    void FetchUser(const std::string& userId, std::function<void(bool known)> onDone);
    std::vector<uint8_t> KeyOf(const std::string& userId);
    UltraIMConversation EnsureDirectConversation(const std::string& userId);
    void RefreshGroups();
    void UpsertGroup(const UltraCanvas::JSONValue& group);
    void FetchQueue();
    // Online if the relay says so or a local connection to them is up.
    UltraIMPresence EffectivePresence(const std::string& userId);
    void EmitPresence(const std::string& userId);
    bool StartLan(std::string& error);
    void OnLanPeerUp(const std::string& peerId, const std::vector<uint8_t>& key, const std::string& name);
    void OnLanFrame(const std::string& peerId, const UltraCanvas::JSONValue& frame);
    // A contact added from the local network: known once the handshake proves its key.
    UltraIMConversation StoreLanContact(const std::string& userId);
    void CompletePendingAdds(int64_t nowMs);

    // ---- incoming ---------------------------------------------------------------
    void OnFrame(const UltraCanvas::JSONValue& frame);
    void OnLinkMessage(const std::string& peerId, const std::string& text);
    // envelopeJson from a channel (relayId 0) or the relay queue.
    void ProcessEnvelope(const std::string& envelopeJson, int64_t relayId, bool overLink);
    void HandleBody(const Envelope& envelope, const UltraCanvas::JSONValue& body);

    // ---- files ---------------------------------------------------------------------
    UltraIMResult SendMedia(const std::string& conversationId, const std::string& localPath, const std::string& caption,
                            UltraIMMessageType type, const UltraIMSendOptions& options);
    // Where a file that arrived (or its preview picture) is kept.
    std::string MediaPath(const std::string& fileId, const std::string& suffix) const;
    // All parts here and the message known: the file is put together, the
    // message updated, and whoever waits for it told.
    void TryAssemble(const std::string& fileId);
    // The "file" and "chunk" bodies.
    void HandleFileBody(const Envelope& envelope, const UltraCanvas::JSONValue& body);
    void HandleChunkBody(const UltraCanvas::JSONValue& body);

    // ---- outgoing ---------------------------------------------------------------
    void QueueBody(const std::string& conversation, const std::vector<std::string>& recipients,
                   const std::string& clientId, const std::string& bodyJson, int64_t timestampMs);
    void SendReceipt(const std::string& to, const std::vector<std::string>& ids, const char* status);
    void Pump();                     // one pass over the outbox
    void PostToRelay(const std::vector<OutboxEntry>& copies);
    void MarkDelivered(const std::string& conversation, const std::string& clientId, UltraIMDeliveryStatus status);
    void WorkerLoop();
    void Wake();

    std::string     providerId_;
    UltraIMConfig   config_;
    UltraIMEventSink sink_;
    ProviderTimings timings_;

    std::atomic<UltraIMConnectionState> state_{UltraIMConnectionState::Disconnected};
    Store           store_;
    bool            storeOpen_ = false;
    UltraVault::DeviceKeyVault vault_;
    bool            vaultOpen_ = false;
    Identity        identity_;
    std::string     displayName_;
    // The account (accountMutex_): what was read from the vault, the
    // password it was opened with ("" : the device key), the authenticator's
    // secret (empty: no code asked) and one being set up.
    mutable std::mutex accountMutex_;
    std::string     vaultPassword_;
    UltraCryptSecureBuffer totpSecret_;
    UltraCryptSecureBuffer pendingTotp_;
    bool            codeAccepted_ = false;      // this start's code was right
    std::string     email_;
    std::string     phone_;
    std::string     authHint_;
    std::atomic<UltraIMAuthStep> authStep_{UltraIMAuthStep::Idle};
    std::vector<std::function<void()>> jobs_;   // under mutex_
    KeyRing         keyRing_;
    LinkManager     links_;
    CallManager     calls_;
    LanTransport    lan_;
    bool            lanOn_ = false;
    std::shared_ptr<RelayClient> relay_;   // replaced per sign-in; read through Relay()
    mutable std::mutex relayMutex_;
    std::shared_ptr<RelayClient> Relay() const;

    mutable std::mutex mutex_;
    std::map<std::string, Attempt> attempts_;           // clientId|recipient
    std::map<std::string, UltraIMPresence> presence_;   // user id -> presence
    std::set<std::string> fetchingUsers_;
    // Proven by a local connection's handshake: user id -> (key, name).
    std::map<std::string, std::pair<std::vector<uint8_t>, std::string>> lanKeys_;
    struct PendingAdd {
        std::string userId;
        UltraIMConversationCallback onResult;
        int64_t deadlineMs = 0;
    };
    std::vector<PendingAdd> pendingAdds_;
    std::map<std::string, std::vector<UltraIMAttachmentCallback>> mediaWaiters_;    // file id -> DownloadAttachment's callbacks

    std::thread     worker_;
    std::condition_variable wake_;
    bool            stopping_ = false;
    bool            woken_ = false;

    std::atomic<int> sentOverLink_{0};
    std::atomic<int> sentOverRelay_{0};
};

} // namespace UltraIMDirect
