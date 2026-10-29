// include/UltraIM/UltraIMCore.h
// UltraIM - the public instant-messaging API (UltraIM_* functions)
// Version: 0.1.4 - UltraIM_SendVideo, UltraIM_DownloadAttachment
// Version: 0.1.3 - a sign-in by a QR code (credentials.loginType QRCode, UltraIMAuthStep::OtherDevice)
// Version: 0.1.2 - UltraIM_ResendAuthCode
// Version: 0.1.1 - UltraIM_FetchSharedCredentials
// Version: 0.1.0
// Last Modified: 2026-10-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraIMPlugins.h"
#include "UltraIMTypes.h"

#include <memory>
#include <string>
#include <vector>

// Threading: every callback and every event below is delivered through the
// dispatcher installed with UltraIM_SetEventDispatcher. An application with
// a UI installs one that posts to its UI thread; without one, callbacks run
// on whatever thread the provider finished on. All functions may be called
// from any thread.

// ---- Lifecycle ---------------------------------------------------------------
UltraIMResult UltraIM_Initialize(const UltraIMConfig& config = UltraIMConfig());
// Signs every provider out, shuts them down and drops all subscriptions.
void          UltraIM_Shutdown();
bool          UltraIM_IsInitialized();
const UltraIMConfig& UltraIM_GetConfig();

// ---- Providers ---------------------------------------------------------------
// A provider compiled into the application (or a test double).
UltraIMResult UltraIM_RegisterProvider(std::shared_ptr<IUltraIMProviderPlugin> provider);
UltraIMResult UltraIM_UnregisterProvider(const std::string& providerId);
// Loads every provider module in config.pluginDirectory that is not loaded
// yet. Returns how many providers were added.
int           UltraIM_RefreshProviders();
std::vector<UltraIMProviderInfo> UltraIM_ListProviders();
bool          UltraIM_GetProviderInfo(const std::string& providerId, UltraIMProviderInfo& outInfo);

// ---- Events ------------------------------------------------------------------
void UltraIM_SetEventDispatcher(UltraIMDispatcher dispatcher);
UltraIMSubscriptionId UltraIM_Subscribe(UltraIMEventCallback onEvent);
void UltraIM_Unsubscribe(UltraIMSubscriptionId subscriptionId);

// ---- Sessions ----------------------------------------------------------------
// A sign-in in steps answers NeedsInput while it waits for the next one: call
// SignIn again with what UltraIMProviderInfo::authStep asks for (the code, the
// password, ...), the rest of the credentials as before. credentials.loginType
// QRCode asks instead for a link to show as a QR code (authStep OtherDevice,
// the link in authHint, refreshed as AuthStepChange) that a signed-in app
// scans; it can be asked for while a code is awaited too.
void UltraIM_SignIn(const std::string& providerId,
                    const UltraIMCredentials& credentials,
                    UltraIMResultCallback onResult);
// A sign-in waiting for a code (authStep Code): ask for it the other way
// UltraIMProviderInfo::authResendHint names (by SMS, a call); the new hint
// arrives as AuthStepChange. CapabilityMissing when there is no other way.
void UltraIM_ResendAuthCode(const std::string& providerId, UltraIMResultCallback onResult);
void UltraIM_SignOut(const std::string& providerId);
UltraIMConnectionState UltraIM_GetConnectionState(const std::string& providerId);

// ---- Data --------------------------------------------------------------------
// Every connected provider, merged; onResult runs once, after the last one
// answered (a provider that failed contributes nothing and its error is the
// result's message).
void UltraIM_FetchConversations(UltraIMConversationsCallback onResult);
void UltraIM_FetchMessages(const std::string& providerId,
                           const std::string& conversationId,
                           const std::string& beforeMessageId,
                           int limit,
                           UltraIMMessagesCallback onResult);
void UltraIM_FetchContacts(const std::string& providerId, UltraIMContactsCallback onResult);
// People on the local network who are not contacts yet (NearbyChange: fetch again).
void UltraIM_FetchNearbyContacts(const std::string& providerId, UltraIMContactsCallback onResult);
void UltraIM_AddContact(const std::string& providerId, const std::string& address,
                        UltraIMConversationCallback onResult);
void UltraIM_CreateGroup(const std::string& providerId, const std::string& title,
                         const std::vector<std::string>& memberIds,
                         UltraIMConversationCallback onResult);

// ---- Sending -----------------------------------------------------------------
// Return at once; the outcome arrives as a MessageDelivered event carrying
// options.clientMessageId. Leave clientMessageId empty and one is generated
// (read it back from the MessageDelivered event).
UltraIMResult UltraIM_SendText(const std::string& providerId, const std::string& conversationId,
                               const std::string& text, const UltraIMSendOptions& options = UltraIMSendOptions());
UltraIMResult UltraIM_SendFile(const std::string& providerId, const std::string& conversationId,
                               const std::string& localPath, const UltraIMSendOptions& options = UltraIMSendOptions());
UltraIMResult UltraIM_SendImage(const std::string& providerId, const std::string& conversationId,
                                const std::string& localPath, const std::string& caption,
                                const UltraIMSendOptions& options = UltraIMSendOptions());
// A video with a caption (the provider's FileTransfer capability covers
// it; a provider without sends it as a file or answers CapabilityMissing).
UltraIMResult UltraIM_SendVideo(const std::string& providerId, const std::string& conversationId,
                                const std::string& localPath, const std::string& caption,
                                const UltraIMSendOptions& options = UltraIMSendOptions());
UltraIMResult UltraIM_SendSticker(const std::string& providerId, const std::string& conversationId,
                                  const std::string& packId, const std::string& stickerId,
                                  const UltraIMSendOptions& options = UltraIMSendOptions());
// A message's attachment fetched to disk - a video, a file, a voice note;
// pictures and preview pictures the provider fetches by itself and reports
// with MessageUpdate. The callback brings the attachment with localPath set,
// and a MessageUpdate with the same follows; one already on disk answers at
// once. NotFound for a message without one.
void          UltraIM_DownloadAttachment(const std::string& providerId, const std::string& conversationId,
                                         const std::string& messageId, UltraIMAttachmentCallback onResult);
UltraIMResult UltraIM_SendVoiceNote(const std::string& providerId, const std::string& conversationId,
                                    const std::string& localPath,
                                    const UltraIMSendOptions& options = UltraIMSendOptions());
UltraIMResult UltraIM_DeleteMessage(const std::string& providerId, const std::string& conversationId,
                                    const std::string& messageId);
UltraIMResult UltraIM_EditMessage(const std::string& providerId, const std::string& conversationId,
                                  const std::string& messageId, const std::string& newText);
UltraIMResult UltraIM_MarkRead(const std::string& providerId, const std::string& conversationId,
                               const std::string& upToMessageId = std::string());
UltraIMResult UltraIM_SetTyping(const std::string& providerId, const std::string& conversationId,
                                bool isTyping);
void          UltraIM_FetchStickerPacks(const std::string& providerId, UltraIMStickerPacksCallback onResult);

// ---- Calls -------------------------------------------------------------------
bool          UltraIM_CanCall(const std::string& providerId, const std::string& conversationId, bool video);
UltraIMResult UltraIM_StartCall(const std::string& providerId, const std::string& conversationId,
                                bool video, std::string& outCallId);
UltraIMResult UltraIM_AcceptCall(const std::string& providerId, const std::string& callId, bool video);
// Ends a call in any state: hangs up, cancels an outgoing call, declines an
// incoming one.
UltraIMResult UltraIM_EndCall(const std::string& providerId, const std::string& callId);
// A muted call sends no audio; the other side hears silence.
UltraIMResult UltraIM_SetCallMuted(const std::string& providerId, const std::string& callId, bool muted);
// Video calls: the camera on or off, and where the pictures go (see
// UltraIMVideoFrameCallback for the thread they arrive on).
UltraIMResult UltraIM_SetCallCamera(const std::string& providerId, const std::string& callId, bool on);
UltraIMResult UltraIM_SetCallVideoSinks(const std::string& providerId, const std::string& callId,
                                        UltraIMVideoFrameCallback remote, UltraIMVideoFrameCallback local);

// ---- End-to-end encryption keys ------------------------------------------------
// A passphrase-protected export of the keys to the provider's encrypted
// conversations (Matrix: the format Element and other clients read), and an
// import of one; a provider without encryption answers CapabilityMissing.
// Write the export with UltraCanvasFileLoader like any file the user saves.
void UltraIM_ExportEncryptionKeys(const std::string& providerId, const std::string& passphrase,
                                  UltraIMKeyExportCallback onResult);
void UltraIM_ImportEncryptionKeys(const std::string& providerId, const std::string& data,
                                  const std::string& passphrase, UltraIMKeyImportCallback onResult);

// ---- Credentials one provider's service keeps for another ---------------------
// What providerId's service (ChatCube Direct: the relay) keeps for
// forProviderId's sign-in - "telegram": the application's API ID and hash
// (credentials.apiId / apiHash) - to pass to UltraIM_SignIn(forProviderId,
// ...). NotFound when the service keeps none, CapabilityMissing when the
// provider has no such service; providerId must be signed in.
void UltraIM_FetchSharedCredentials(const std::string& providerId, const std::string& forProviderId,
                                    UltraIMSharedCredentialsCallback onResult);

// ---- Helpers -----------------------------------------------------------------
// A fresh client message id (a UUID v4).
std::string   UltraIM_NewClientMessageId();
