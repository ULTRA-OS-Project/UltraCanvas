// Apps/ChatCube/ui/ChatCubeWindow.h
// The ChatCube window. Two views, one shown at a time:
//
// Sign-in page:
//                        ChatCube
//      Chat directly with the people you know - no account, no password
//                 [ Your name               ]
//                 [ Relay server (optional) ]
//                 [x] Also find people on this network
//                 [ Create my ChatCube ID   ]   (or: Sign in)
//      No relay at all: ChatCube works on the local network only.
//                       status line
//
// Chat view:
//
//   | My ID 3f2a 9c01 ... [Copy]   | Bob            ●  online          [Call] [Video] |
//   | [ Contact's ChatCube ID ][Add]| ---------------- 21.10.2026 ---------------   |
//   | [New group]                   |  ( hello )                                    |
//   | On this network               |                                               |
//   |   Carol            [Add]      |                                               |
//   | ● Bob              14:05      |                          ( hi, how are you ) |
//   |   hi, how are you        [2]  |                                    14:06 ✓✓   |
//   | # Team             13:40      |                                               |
//   |                               | [+] [ Message...                   ] [Send]   |
//   | status line                                                                   |
//
// Next to ChatCube Direct, a Matrix account (the "matrix" provider) can be
// added from the sidebar; its rooms join the same list, and everything done
// in a conversation goes to that conversation's provider.
//
// A call shows a bar across the top of the chat view:
//
//   | Incoming video call from Bob                        [Accept] [Decline] |
//   | In call with Bob   01:42                 [Camera off] [Mute] [Hang up] |
//
// and a video call the two pictures under it - the other side large, the
// self view small and mirrored (UltraCanvasVideoTile; the provider pushes
// the pictures into them from its media threads).
//
// Everything comes from UltraIM (provider "direct", ChatCube Direct), through
// its dispatcher on the UI thread: this window never takes a lock.
// Sending is optimistic: the bubble appears at once (…), and the provider's
// MessageDelivered events move its ticks (✓ in the relay's queue, ✓✓ on the
// other device, green ✓✓ read).
//
// Version: 0.2.2 - pictures: avatars from the person's photo, photos and videos in the bubbles and a
//                  viewer for them, a paperclip that sends a picture, a video or a file
// Version: 0.2.1 - Telegram signs in by a QR code too, scanned from the phone's Telegram app
// Version: 0.2.0 - the look from ChatCubeTheme: avatars, icon buttons, a rounded composer, --dark
// Version: 0.1.3 - the Telegram form says where the code went and asks for it another way
// Version: 0.1.2 - the status line shows every network's state, one entry each, beside the last action
// Version: 0.1.1 - the Telegram application's keys from the relay; TDLib's connection on the status line
// Version: 0.1.0
// Last Modified: 2026-10-29
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "ChatCubeViews.h"

#include "UltraCanvasMediaViewerWindow.h"
#include "UltraCanvasWindow.h"
#include "UltraIM/UltraIMTypes.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace UltraCanvas {
class UltraCanvasButton;
class UltraCanvasCheckbox;
class UltraCanvasContainer;
class UltraCanvasImageElement;
class UltraCanvasLabel;
class UltraCanvasListView;
class UltraCanvasQRCode;
class UltraCanvasTextArea;
class UltraCanvasTextInput;
class UltraCanvasVideoTile;
}

namespace ChatCube {

class ChatCubeWindow {
public:
    ChatCubeWindow(std::string version, std::string providerId);
    ~ChatCubeWindow();

    bool Create();
    void Show();
    std::function<void()> onClosed;

private:
    // ---- views ---------------------------------------------------------------
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildSignInPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildChatView();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildSidebar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildConversationPane();
    void ShowSignInPage();
    void ShowChatView();

    // ---- actions -------------------------------------------------------------
    void SignIn();
    void AddContact();
    void AddContactById(const std::string& id);
    void RefreshNearby();           // the "On this network" list
    void ToggleMatrixForm();
    void SignInToMatrix(bool stored);
    // The keys to encrypted Matrix rooms to a passphrase-protected file (the
    // format Element reads), and back from one.
    void ExportMatrixKeys();
    void ImportMatrixKeys();
    // Telegram signs in in steps: the form shows the field the next one needs.
    void ToggleTelegramForm();
    void SignInToTelegram(bool stored);
    // A QR code instead of the number and the code: the form shows one for
    // the phone's Telegram app to scan.
    void SignInToTelegramByQrCode();
    // The credentials to the provider; `stored` when nothing was typed (the
    // stored session resumed).
    void SendTelegramSignIn(UltraIMCredentials credentials, bool stored);
    void UpdateTelegramForm();
    // The Telegram application's API ID and hash the relay keeps, so nobody
    // types them; then `then` (the stored Telegram session resumed, say).
    void FetchTelegramCredentials(std::function<void()> then);
    // The provider an address belongs to: @name:server Matrix, +phone /
    // @username / t.me/... Telegram, anything else a ChatCube ID.
    std::string ProviderOfAddress(const std::string& address) const;
    // The provider of a conversation (Direct, or Matrix for its rooms).
    std::string ProviderOf(const std::string& conversationId) const;
    void ToggleGroupForm();
    void CreateGroup();
    void CopyMyId();
    void OpenConversation(const std::string& conversationId);
    void SendCurrentMessage();
    // The paperclip: a picture, a video or any file, chosen through
    // FileLoader's dialog, sent to the open conversation with the
    // composer's text as its caption.
    void AttachFile();
    void SendAttachment(const std::string& path);
    // A click on a bubble's picture or file: shown in the media viewer, fetched
    // first when the provider has not yet (a video, a file).
    void OpenAttachment(int row);
    void ShowAttachment(const std::string& path);
    void UpdateHeader();
    // The status line: what the last action said, then every network's own
    // state - "Added Bob · ChatCube: connected · Telegram: waiting for the
    // network…" - so one network's news never hides another's.
    void SetStatus(const std::string& text);                                      // the action's part
    void SetProviderStatus(const std::string& providerId, const std::string& text);   // "" removes the entry
    void RenderStatus();
    void SetSignInStatus(const std::string& text);
    void RefreshConversations();
    void ScrollToEnd();

    // ---- calls ---------------------------------------------------------------
    void StartCall(bool video);
    void AcceptCall();
    void EndCall();
    void ToggleMute();
    void ToggleCamera();
    void ConnectVideo();            // the call's pictures into the tiles
    void OnCallEvent(const UltraIMEvent& event);
    void UpdateCallBar();
    std::string ContactName(const std::string& contactId) const;

    // ---- UltraIM -------------------------------------------------------------
    void OnEvent(const UltraIMEvent& event);

    std::string version_;
    std::string providerId_;
    uint64_t    subscription_ = 0;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> signInPage_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> name_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> relay_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  localNetwork_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    signIn_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     signInStatus_;
    bool hasIdentity_ = false;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> chatView_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     myId_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> contactId_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> groupForm_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> groupTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> groupMembers_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> nearbyBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> matrixForm_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> matrixServer_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> matrixUser_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> matrixPassword_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    matrixSignIn_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> matrixKeysForm_;   // once signed in
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    telegramToggle_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> telegramForm_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> telegramPhone_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> telegramApiId_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> telegramApiHash_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     telegramHint_;     // where the code went, in the form
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> telegramCode_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    telegramResend_;   // the code another way, when offered
    std::shared_ptr<UltraCanvas::UltraCanvasQRCode>    telegramQr_;       // the link to scan, while Telegram waits for that
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    telegramQrButton_; // asks for the QR code instead
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> telegramPassword_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> telegramName_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    telegramNext_;
    std::string telegramProviderId_ = "telegram";
    std::string telegramRelayApiId_;        // from the relay; "" = the user types them
    std::string telegramRelayApiHash_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    matrixToggle_;
    std::string matrixProviderId_ = "matrix";
    std::string activeProvider_;    // the open conversation's provider
    std::shared_ptr<UltraCanvas::UltraCanvasListView>  conversationList_;
    std::shared_ptr<ConversationListModel>             conversations_;

    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     headerAvatar_;   // initials on the person's colour
    std::shared_ptr<UltraCanvas::UltraCanvasImageElement> headerPhoto_; // or their picture, when there is one
    std::string headerPhotoPath_;                                       // what headerPhoto_ shows
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     title_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     subtitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    voiceCall_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    videoCall_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> emptyHint_;   // the logo and the words before a conversation is open
    std::shared_ptr<UltraCanvas::UltraCanvasListView>  messageList_;
    std::shared_ptr<MessageListModel>                  messages_;
    std::shared_ptr<MessageBubbleDelegate>             bubbles_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  input_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    attach_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    send_;
    UltraCanvas::UltraCanvasMediaViewerWindow          viewer_;         // a picture or a video, full size
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     status_;
    std::string actionStatus_;
    std::map<std::string, std::string> providerStatus_;   // provider id -> its state

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> callBar_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     callText_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    callAccept_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    callMute_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    callCamera_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> videoPanel_;
    std::shared_ptr<UltraCanvas::UltraCanvasVideoTile> remoteVideo_;
    std::shared_ptr<UltraCanvas::UltraCanvasVideoTile> selfVideo_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    callEnd_;
    std::string      callId_;               // empty: no call
    std::string      callContact_;
    bool             callIncoming_ = false;
    bool             callMuted_ = false;
    bool             callVideo_ = false;
    bool             cameraOn_ = false;
    UltraIMCallState callState_ = UltraIMCallState::Ended;
    int64_t          callActiveSinceMs_ = 0;
    uint64_t         callTimer_ = 0;        // the duration ticker (0: none)

    std::string activeConversation_;
    std::string typingContact_;
    int64_t     typingUntilMs_ = 0;
    int64_t     lastTypingSentMs_ = 0;
    bool        relayoutPending_ = false;
    bool        fillingList_ = false;
};

} // namespace ChatCube
