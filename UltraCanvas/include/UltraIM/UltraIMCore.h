// include/UltraIM/UltraIMCore.h
// UltraIM - the public instant-messaging API (UltraIM_* functions)
// Version: 0.1.10 - UltraIM_DeleteMessage takes a scope; UltraIM_GetDeleteChoices
// Version: 0.1.9 - UltraIM_SearchMessages; for providers and lists: UltraIM_MessageMatchesSearch,
//                  UltraIM_ExtractLinks, UltraIM_FoldCase, UltraIM_SearchByFetching
// Version: 0.1.8 - account recovery by an e-mailed link: UltraIM_FinishRecovery asks whether it was approved
// Version: 0.1.7 - UltraIM_VerifyContact, _ResendContactCode, _SetRecoveryEnabled, _StartRecovery, _FinishRecovery
// Version: 0.1.6 - the account's profile (UltraIM_FetchAccountProfile, UltraIM_UpdateAccountProfile) and its
//                  sign-in protection (UltraIM_GetSignInProtection, _StartAuthenticatorSetup, _SetSignInProtection)
// Version: 0.1.5 - UltraIM_SetPreviewMaker, UltraIM_MakePreview
// Version: 0.1.4 - UltraIM_SendVideo, UltraIM_DownloadAttachment
// Version: 0.1.3 - a sign-in by a QR code (credentials.loginType QRCode, UltraIMAuthStep::OtherDevice)
// Version: 0.1.2 - UltraIM_ResendAuthCode
// Version: 0.1.1 - UltraIM_FetchSharedCredentials
// Version: 0.1.0
// Last Modified: 2026-11-11
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
// ---- Searching the history -----------------------------------------------------
// The messages of providerId matching search - words, or everything of one
// kind (pictures, videos, files, links) - newest first, a page at a time:
// pass the page's nextCursor as search.cursor for the older ones, until it
// comes back empty. CapabilityMissing for a provider without the
// SearchMessages capability, or one asked for every conversation at once
// (search.conversationId "") that can only search one.
//
// Where the answer comes from differs, and so does how words match: ChatCube
// Direct and Matrix match on this device - the words anywhere in the text,
// in any case (UltraIM_MessageMatchesSearch); Telegram asks Telegram, which
// matches whole words as its own apps do.
void UltraIM_SearchMessages(const std::string& providerId, const UltraIMMessageSearch& search,
                            UltraIMSearchCallback onResult);
// Whether a message is one the search is for: its kind, and every word of
// search.text in its text or its attachment's file name (conversationId,
// cursor and limit are not looked at). The one matcher the providers that
// search on the device share, and what a list filters its loaded rows with.
bool UltraIM_MessageMatchesSearch(const UltraIMMessage& message, const UltraIMMessageSearch& search);
// The links in a text, in order: what starts with http:// or https://, or
// with www. at the start of a word, up to the next space, without the
// punctuation a sentence puts after it.
std::vector<std::string> UltraIM_ExtractLinks(const std::string& text);
// UTF-8 text in lower case for comparing: the Latin, Greek and Cyrillic
// letters folded one to one (never by the locale), everything else as it is.
std::string UltraIM_FoldCase(const std::string& text);
// For providers: SearchMessages answered by reading one conversation's
// history through fetchPage (the provider's own FetchMessages), newest page
// first, and keeping what UltraIM_MessageMatchesSearch accepts. Stops when
// the page is full, the history ends, or after about two thousand messages
// (nextCursor then says where to go on). fetchPage must not be called again
// after the provider shut down: the provider's FetchMessages already holds
// to that.
void UltraIM_SearchByFetching(UltraIMFetchPage fetchPage, const UltraIMMessageSearch& search,
                              UltraIMSearchCallback onResult);

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
// Deletes a message for everyone, or for this account alone; a
// MessageDelete event follows where it is gone. Each messenger has its own
// rules for who may do which and until when: UltraIM_GetDeleteChoices says
// what is possible for one message now (nothing for a provider without the
// DeleteMessage capability), and a delete outside them answers
// PermissionDenied.
UltraIMResult UltraIM_DeleteMessage(const std::string& providerId, const std::string& conversationId,
                                    const std::string& messageId,
                                    UltraIMDeleteScope scope = UltraIMDeleteScope::ForEveryone);
UltraIMDeleteChoices UltraIM_GetDeleteChoices(const std::string& providerId, const std::string& conversationId,
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

// ---- The account's own profile ----------------------------------------------------
// Name, e-mail address and phone number of providerId's signed-in account
// (UltraIMAccountProfile). The name goes to the contacts; the e-mail and the
// phone number to the provider's service, for the owner alone (recovering
// the account). CapabilityMissing for a provider without one.
void UltraIM_FetchAccountProfile(const std::string& providerId, UltraIMAccountProfileCallback onResult);
void UltraIM_UpdateAccountProfile(const std::string& providerId, const UltraIMAccountProfile& profile,
                                  UltraIMResultCallback onResult);

// ---- How signing in on this device is protected -------------------------------------
// The protection now (DeviceKey for a provider without the capability).
UltraIMSignInProtection UltraIM_GetSignInProtection(const std::string& providerId);
// A new authenticator secret as an otpauth:// link, to show as a QR code
// for UltraAuthenticator (or any authenticator app) to scan.
UltraIMResult UltraIM_StartAuthenticatorSetup(const std::string& providerId, std::string& outOtpAuthUri);
// The protection changed: proven with the current password and code, the
// new password set, a new authenticator proven with its first code. AuthFailed
// when a current factor is wrong, InvalidArgument when something the new one
// needs is missing.
void UltraIM_SetSignInProtection(const std::string& providerId, const UltraIMProtectionChange& change,
                                 UltraIMResultCallback onResult);

// ---- Confirming the e-mail and phone; account recovery ---------------------------
// A changed e-mail address gets a link from the service that confirms it
// when opened; a changed phone number gets a code (emailVerified /
// phoneVerified in the profile say when): UltraIM_VerifyContact sends the
// phone's code back, UltraIM_ResendContactCode asks for a new link or code.
// UltraIM_SetRecoveryEnabled turns recovery on - the address confirmed - or
// off. On a computer without the account, UltraIM_StartRecovery(provider,
// server, the e-mail address) has a link e-mailed to it; UltraIM_FinishRecovery
// then asks whether the owner opened it and said yes - NeedsInput while not
// yet (ask again in a few seconds), Ok once the account is on this computer,
// AuthFailed when the recovery expired - and UltraIM_SignIn signs in with
// what it stored. The challenge looks the same for an address the service
// does not know; such a recovery simply never gets approved.
void UltraIM_VerifyContact(const std::string& providerId, UltraIMContactChannel channel, const std::string& code,
                           UltraIMResultCallback onResult);
void UltraIM_ResendContactCode(const std::string& providerId, UltraIMContactChannel channel,
                               UltraIMResultCallback onResult);
void UltraIM_SetRecoveryEnabled(const std::string& providerId, bool enabled, UltraIMResultCallback onResult);
void UltraIM_StartRecovery(const std::string& providerId, const std::string& server, const std::string& address,
                           UltraIMRecoveryChallengeCallback onResult);
void UltraIM_FinishRecovery(const std::string& providerId, const UltraIMRecoveryAnswer& answer,
                            UltraIMResultCallback onResult);

// ---- Preview pictures -----------------------------------------------------------
// The application's way of making a preview picture (UltraIMPreviewMaker):
// installed once, with its image and video libraries, before anything is
// sent. Without one, files go without a preview.
void          UltraIM_SetPreviewMaker(UltraIMPreviewMaker maker);
// For providers: a JPEG preview of the file, its longer side at most maxSide,
// written under the cache directory ("" when no maker is installed or the
// file cannot be read); its size in outWidth / outHeight.
std::string   UltraIM_MakePreview(const std::string& path, const std::string& mimeType, int maxSide,
                                  int& outWidth, int& outHeight);

// ---- Helpers -----------------------------------------------------------------
// A fresh client message id (a UUID v4).
std::string   UltraIM_NewClientMessageId();
