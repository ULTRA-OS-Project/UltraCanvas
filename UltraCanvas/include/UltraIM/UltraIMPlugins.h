// include/UltraIM/UltraIMPlugins.h
// UltraIM - the provider plug-in interface and the module (DSO) host contract
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraIMTypes.h"

#include <memory>
#include <string>
#include <vector>

// The manager hands each provider one sink; the provider calls it from any
// thread whenever something happens on the network. The manager forwards the
// event through the application's dispatcher, so the provider never needs to
// know about the UI thread.
using UltraIMEventSink = std::function<void(const UltraIMEvent& event)>;

// ============================================================================
// IUltraIMProviderPlugin
// One messenger network. Every method returns at once: network work runs on
// the provider's own (UltraNet / UltraRTC) threads and finishes through the
// callback or an event. Callbacks may be called from any thread - the
// manager re-dispatches them. Returned UltraIMResults are synchronous
// rejections only (not connected, capability missing, bad argument).
// ============================================================================

class IUltraIMProviderPlugin {
public:
    virtual ~IUltraIMProviderPlugin() = default;

    // ---- Identity ------------------------------------------------------------
    virtual std::string GetProviderId() const = 0;      // stable: "direct", "matrix", ...
    virtual std::string GetDisplayName() const = 0;
    virtual std::string GetVersion() const = 0;
    virtual std::string GetIconPath() const { return std::string(); }
    virtual UltraIMCapabilityFlags GetCapabilities() const = 0;
    virtual UltraIMLoginType GetLoginType() const = 0;

    // ---- Lifecycle -----------------------------------------------------------
    // Called once by the manager when the provider is registered.
    virtual UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) = 0;
    // Called once before the provider is released: stop every thread and
    // cancel every request; no callback or event may follow.
    virtual void Shutdown() = 0;

    // ---- Session -------------------------------------------------------------
    virtual void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) = 0;
    virtual void SignOut() = 0;
    virtual UltraIMConnectionState GetConnectionState() const = 0;
    virtual std::string GetLocalUserId() const { return std::string(); }
    // A saved session the provider can resume without asking the user.
    virtual bool HasStoredSession() const { return false; }

    // ---- Data ----------------------------------------------------------------
    virtual void FetchConversations(UltraIMConversationsCallback onResult) = 0;
    // beforeMessageId "" = the most recent page. Messages come oldest first.
    virtual void FetchMessages(const std::string& conversationId,
                               const std::string& beforeMessageId,
                               int limit,
                               UltraIMMessagesCallback onResult) = 0;
    virtual void FetchContacts(UltraIMContactsCallback onResult) {
        if (onResult) onResult(UltraIMResult::Ok(), {});
    }
    // People on the local network who are not contacts yet (a provider that
    // finds them there; NearbyChange says when the list changed). Each can be
    // added with AddContact(contactId).
    virtual void FetchNearbyContacts(UltraIMContactsCallback onResult) {
        if (onResult) onResult(UltraIMResult::Ok(), {});
    }

    // ---- Contacts and groups -------------------------------------------------
    // address: whatever identifies a person on this network (a ChatCube user
    // id, @user:server, a phone number).
    virtual void AddContact(const std::string& address, UltraIMConversationCallback onResult) {
        (void)address;
        if (onResult) onResult(Unsupported("adding contacts"), {});
    }
    virtual void CreateGroup(const std::string& title,
                             const std::vector<std::string>& memberIds,
                             UltraIMConversationCallback onResult) {
        (void)title; (void)memberIds;
        if (onResult) onResult(Unsupported("groups"), {});
    }

    // ---- Sending (the outcome arrives as a MessageDelivered event) ----------
    virtual UltraIMResult SendText(const std::string& conversationId,
                                   const std::string& text,
                                   const UltraIMSendOptions& options) = 0;
    virtual UltraIMResult SendFile(const std::string& conversationId,
                                   const std::string& localPath,
                                   const UltraIMSendOptions& options) {
        (void)conversationId; (void)localPath; (void)options;
        return Unsupported("file transfer");
    }
    virtual UltraIMResult SendImage(const std::string& conversationId,
                                    const std::string& localPath,
                                    const std::string& caption,
                                    const UltraIMSendOptions& options) {
        (void)conversationId; (void)localPath; (void)caption; (void)options;
        return Unsupported("images");
    }
    virtual UltraIMResult SendSticker(const std::string& conversationId,
                                      const std::string& packId,
                                      const std::string& stickerId,
                                      const UltraIMSendOptions& options) {
        (void)conversationId; (void)packId; (void)stickerId; (void)options;
        return Unsupported("stickers");
    }
    virtual UltraIMResult SendVoiceNote(const std::string& conversationId,
                                        const std::string& localPath,
                                        const UltraIMSendOptions& options) {
        (void)conversationId; (void)localPath; (void)options;
        return Unsupported("voice notes");
    }

    // ---- Message actions -----------------------------------------------------
    virtual UltraIMResult DeleteMessage(const std::string& conversationId,
                                        const std::string& messageId) {
        (void)conversationId; (void)messageId;
        return Unsupported("deleting messages");
    }
    virtual UltraIMResult EditMessage(const std::string& conversationId,
                                      const std::string& messageId,
                                      const std::string& newText) {
        (void)conversationId; (void)messageId; (void)newText;
        return Unsupported("editing messages");
    }
    // upToMessageId "" = everything in the conversation.
    virtual UltraIMResult MarkRead(const std::string& conversationId,
                                   const std::string& upToMessageId) {
        (void)conversationId; (void)upToMessageId;
        return UltraIMResult::Ok();
    }
    virtual UltraIMResult SetTyping(const std::string& conversationId, bool isTyping) {
        (void)conversationId; (void)isTyping;
        return UltraIMResult::Ok();
    }

    // ---- Calls ---------------------------------------------------------------
    // Whether a call can be placed in this conversation now (Direct: never in
    // a group in v1). A call reports itself with CallReceive (incoming) and
    // CallStateChange events; Ended and Failed are final, with errorText
    // saying why ("declined", "busy", "no answer", ...).
    virtual bool CanCall(const std::string& conversationId, bool video) const {
        (void)conversationId; (void)video;
        return false;
    }
    virtual UltraIMResult StartCall(const std::string& conversationId, bool video,
                                    std::string& outCallId) {
        (void)conversationId; (void)video; (void)outCallId;
        return Unsupported("calls");
    }
    virtual UltraIMResult AcceptCall(const std::string& callId, bool video) {
        (void)callId; (void)video;
        return Unsupported("calls");
    }
    virtual UltraIMResult EndCall(const std::string& callId) {
        (void)callId;
        return Unsupported("calls");
    }
    virtual UltraIMResult SetCallMuted(const std::string& callId, bool muted) {
        (void)callId; (void)muted;
        return Unsupported("calls");
    }
    // A video call's camera, on or off, during the call.
    virtual UltraIMResult SetCallCamera(const std::string& callId, bool on) {
        (void)callId; (void)on;
        return Unsupported("video calls");
    }
    // Where a video call's pictures go: the other side's and our own camera
    // (the self view). nullptr stops them; once this returns, the callbacks
    // it replaced are not running and are not called again.
    virtual UltraIMResult SetCallVideoSinks(const std::string& callId, UltraIMVideoFrameCallback remote,
                                            UltraIMVideoFrameCallback local) {
        (void)callId; (void)remote; (void)local;
        return Unsupported("video calls");
    }

    // ---- Stickers ------------------------------------------------------------
    virtual void FetchStickerPacks(UltraIMStickerPacksCallback onResult) {
        if (onResult) onResult(Unsupported("stickers"), {});
    }

    // ---- End-to-end encryption keys ---------------------------------------------
    // The keys to encrypted conversations as one passphrase-protected text
    // (Matrix: the room-key export file other clients read and write), to keep
    // or to carry to another device; and back. The passphrase is stretched on
    // purpose, so this takes a moment: do it off the caller's thread.
    virtual void ExportEncryptionKeys(const std::string& passphrase, UltraIMKeyExportCallback onResult) {
        (void)passphrase;
        if (onResult) onResult(Unsupported("exporting encryption keys"), std::string());
    }
    virtual void ImportEncryptionKeys(const std::string& data, const std::string& passphrase,
                                      UltraIMKeyImportCallback onResult) {
        (void)data; (void)passphrase;
        if (onResult) onResult(Unsupported("importing encryption keys"), 0);
    }

protected:
    static UltraIMResult Unsupported(const std::string& what) {
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                    "This provider does not support " + what);
    }
};

// ============================================================================
// MODULE (DSO) HOST CONTRACT
// A provider module exports
//
//     extern "C" ULTRAIM_PLUGIN_EXPORT void UltraIM_PluginInit(const UltraIMPluginHost* host);
//
// and, when host->abiVersion is one it was built for, calls
// host->RegisterProvider for each provider it carries. Modules link the
// shared UltraCanvas core like any other library; the table is the version
// gate, so an outdated module is skipped instead of crashing. Append new
// members only, and bump the version when you do.
// ============================================================================

constexpr int ULTRAIM_PLUGIN_HOST_ABI_VERSION = 1;

struct UltraIMPluginHost {
    int abiVersion;
    void (*RegisterProvider)(std::shared_ptr<IUltraIMProviderPlugin> provider);
};

using UltraIM_PluginInitFn = void (*)(const UltraIMPluginHost* host);

#if defined(_WIN32)
#define ULTRAIM_PLUGIN_EXPORT __declspec(dllexport)
#else
#define ULTRAIM_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
