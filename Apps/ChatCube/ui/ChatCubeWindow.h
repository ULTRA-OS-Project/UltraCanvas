// Apps/ChatCube/ui/ChatCubeWindow.h
// The ChatCube window. Two views, one shown at a time:
//
// Sign-in page:
//                        ChatCube
//      Chat directly with the people you know - no account, no password
//                 [ Your name               ]
//                 [ Relay server            ]
//                 [ Create my ChatCube ID   ]   (or: Sign in)
//                       status line
//
// Chat view:
//
//   | My ID 3f2a 9c01 ... [Copy]   | Bob            ●  online          [Call] [Video] |
//   | [ Contact's ChatCube ID ][Add]| ---------------- 21.10.2026 ---------------   |
//   | [New group]                   |  ( hello )                                    |
//   | ● Bob              14:05      |                          ( hi, how are you ) |
//   |   hi, how are you        [2]  |                                    14:06 ✓✓   |
//   | # Team             13:40      |                                               |
//   |                               | [ Message...                       ] [Send]   |
//   | status line                                                                   |
//
// A call shows a bar across the top of the chat view:
//
//   | Incoming call from Bob                              [Accept] [Decline] |
//   | In call with Bob   01:42                              [Mute] [Hang up] |
//
// Everything comes from UltraIM (provider "direct", ChatCube Direct), through
// its dispatcher on the UI thread: this window never takes a lock.
// Sending is optimistic: the bubble appears at once (…), and the provider's
// MessageDelivered events move its ticks (✓ in the relay's queue, ✓✓ on the
// other device, green ✓✓ read).
//
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "ChatCubeViews.h"

#include "UltraCanvasWindow.h"
#include "UltraIM/UltraIMTypes.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
class UltraCanvasButton;
class UltraCanvasContainer;
class UltraCanvasLabel;
class UltraCanvasListView;
class UltraCanvasTextArea;
class UltraCanvasTextInput;
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
    void ToggleGroupForm();
    void CreateGroup();
    void CopyMyId();
    void OpenConversation(const std::string& conversationId);
    void SendCurrentMessage();
    void UpdateHeader();
    void SetStatus(const std::string& text);
    void SetSignInStatus(const std::string& text);
    void RefreshConversations();
    void ScrollToEnd();

    // ---- calls ---------------------------------------------------------------
    void StartVoiceCall();
    void AcceptCall();
    void EndCall();
    void ToggleMute();
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
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    signIn_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     signInStatus_;
    bool hasIdentity_ = false;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> chatView_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     myId_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> contactId_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> groupForm_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> groupTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> groupMembers_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView>  conversationList_;
    std::shared_ptr<ConversationListModel>             conversations_;

    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     title_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     subtitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    voiceCall_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    videoCall_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     emptyHint_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView>  messageList_;
    std::shared_ptr<MessageListModel>                  messages_;
    std::shared_ptr<MessageBubbleDelegate>             bubbles_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  input_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    send_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     status_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> callBar_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     callText_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    callAccept_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    callMute_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    callEnd_;
    std::string      callId_;               // empty: no call
    std::string      callContact_;
    bool             callIncoming_ = false;
    bool             callMuted_ = false;
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
