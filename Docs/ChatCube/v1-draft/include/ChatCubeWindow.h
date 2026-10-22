// ChatCube/include/ChatCubeWindow.h
// Main ChatCube application window.
// Layout (mirrors RISC OS ChatCube screenshots):
//
//  ┌─────────────────────────────────────────────────────────┐
//  │  Window title bar: "ChatCube"                           │
//  ├──────────────────┬──────────────────────────────────────┤
//  │  LEFT PANEL      │  RIGHT PANEL                         │
//  │  ┌────────────┐  │  ┌────────────────────────────────┐  │
//  │  │ TopToolbar │  │  │ ChatHeader (toolbar)            │  │
//  │  │ Order by   │  │  │ [Avatar] Name  [call][video]   │  │
//  │  │ [⊞][🔍][+] │  │  ├────────────────────────────────┤  │
//  │  ├────────────┤  │  │ ListView (ChatBubbleDelegate)   │  │
//  │  │            │  │  │  ... message bubbles ...        │  │
//  │  │  Template  │  │  │  ... date separators ...        │  │
//  │  │  contact   │  │  │                                 │  │
//  │  │  list rows │  │  ├────────────────────────────────┤  │
//  │  │            │  │  │ InputToolbar                    │  │
//  │  │            │  │  │ [😀][📎][🖥] Filter: AllMsgs   │  │
//  │  │            │  │  ├────────────────────────────────┤  │
//  │  │            │  │  │ TextArea (message input)  [➤]  │  │
//  │  └────────────┘  │  └────────────────────────────────┘  │
//  └──────────────────┴──────────────────────────────────────┘
//
// All positioning via layout managers — NO manual coordinates (Rule 15.1).
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSplitter.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTemplate.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasImageElement.h"

#include "ChatCubeTypes.h"
#include "ChatMessageModel.h"
#include "ChatBubbleDelegate.h"
#include "ChatCubeProviderManager.h"

#include <string>
#include <memory>
#include <vector>
#include <functional>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// WINDOW STYLE
// ============================================================================

struct ChatCubeWindowStyle {
    // Dimensions
    int WindowWidth       = 900;
    int WindowHeight      = 600;
    int LeftPanelWidth    = 280;   // Initial splitter position
    int LeftPanelMinWidth = 180;
    int LeftPanelMaxWidth = 420;
    int TopToolbarHeight  = 36;
    int ChatHeaderHeight  = 48;
    int InputToolbarHeight = 36;
    int InputAreaHeight   = 60;

    // Colours
    Color WindowBg           = Color(245, 245, 245, 255);
    Color LeftPanelBg        = Color(240, 240, 240, 255);
    Color RightPanelBg       = Color(255, 255, 255, 255);
    Color ChatHeaderBg       = Color(250, 250, 250, 255);
    Color InputAreaBg        = Color(252, 252, 252, 255);
    Color SplitterColor      = Color(210, 210, 210, 255);
    Color SelectedContactBg  = Color(210, 225, 245, 255);

    // Fonts
    float FontSizeContact  = 13.0f;
    float FontSizeSnippet  = 11.5f;
    float FontSizeHeader   = 14.0f;
};

// ============================================================================
// CHATCUBE MAIN WINDOW
// ============================================================================

class ChatCubeWindow : public UltraCanvasWindow {
public:
    ChatCubeWindow();
    ~ChatCubeWindow() override = default;

    // ── Initialise the window, build layout, wire all callbacks ──────────
    bool Initialize(const ChatCubeWindowStyle& style = {});

    // ── Open a specific conversation in the right panel ──────────────────
    void OpenConversation(const std::string& providerId,
                          const std::string& conversationId,
                          const std::string& title,
                          const std::string& avatarPath);

    // ── Refresh the contact/conversation list ────────────────────────────
    void RefreshContactList();

    // ── Show the welcome/splash screen (no conversation selected) ────────
    void ShowWelcomeScreen();

private:
    // =========================================================================
    // UI COMPONENT DECLARATIONS
    // Each section uses its own container with its own layout manager.
    // NO manual coordinate calculations anywhere (Rule 15.1).
    // =========================================================================

    // ── Root horizontal splitter ─────────────────────────────────────────
    std::shared_ptr<UltraCanvasSplitter>    splitter_;

    // ── Left panel ───────────────────────────────────────────────────────
    std::shared_ptr<UltraCanvasContainer>   leftPanel_;
    std::shared_ptr<UltraCanvasToolbar>     topToolbar_;        // Order-by + search + add
    std::shared_ptr<UltraCanvasDropdown>    orderByDropdown_;
    std::shared_ptr<UltraCanvasContainer>   contactListContainer_;
    // Contact list uses the Template system for repeated rows
    // Each visible conversation is a Template instance inside contactListContainer_
    // Scrolling provided by contactListContainer_'s built-in scrollbars

    // ── Right panel ──────────────────────────────────────────────────────
    std::shared_ptr<UltraCanvasContainer>   rightPanel_;

    // Chat header (visible when a conversation is open)
    std::shared_ptr<UltraCanvasContainer>   chatHeaderSection_;
    std::shared_ptr<UltraCanvasImageElement> chatHeaderAvatar_;
    std::shared_ptr<UltraCanvasLabel>       chatHeaderTitle_;
    std::shared_ptr<UltraCanvasLabel>       chatHeaderSubtitle_;  // "online" / member count
    std::shared_ptr<UltraCanvasToolbar>     chatHeaderActions_;   // call / video / search

    // Message area
    std::shared_ptr<UltraCanvasListView>    messageListView_;
    std::shared_ptr<ChatMessageModel>       messageModel_;
    std::shared_ptr<ChatBubbleDelegate>     bubbleDelegate_;

    // Input area
    std::shared_ptr<UltraCanvasContainer>   inputSection_;
    std::shared_ptr<UltraCanvasToolbar>     inputToolbar_;        // emoji / attach / screenshot / filter
    std::shared_ptr<UltraCanvasDropdown>    messageFilterDropdown_;
    std::shared_ptr<UltraCanvasTextArea>    messageInput_;
    std::shared_ptr<UltraCanvasButton>      sendButton_;

    // Welcome screen (shown before first conversation is selected)
    std::shared_ptr<UltraCanvasContainer>   welcomeScreen_;

    // ── State ─────────────────────────────────────────────────────────────
    ChatCubeWindowStyle                     style_;
    std::string                             activeProviderId_;
    std::string                             activeConversationId_;
    std::vector<ChatConversation>           conversations_;  // Cached for contact list
    bool                                    conversationOpen_ = false;

    // =========================================================================
    // LAYOUT BUILDERS
    // Each method constructs one section and adds it to its parent.
    // =========================================================================

    void BuildRootLayout();
    void BuildLeftPanel();
    void BuildTopToolbar();
    void BuildContactList();
    void BuildRightPanel();
    void BuildChatHeader();
    void BuildMessageArea();
    void BuildInputSection();
    void BuildWelcomeScreen();

    // ── Contact list row builder ──────────────────────────────────────────
    // Creates one Template row for a conversation entry and adds it to
    // contactListContainer_
    void AddContactRow(const ChatConversation& conv);

    // Clears and fully rebuilds the contact list from conversations_
    void RebuildContactList();

    // =========================================================================
    // CALLBACK WIRING
    // All events use the existing UltraCanvas callback system.
    // No manual OnEvent() forwarding.
    // =========================================================================

    void WireManagerCallbacks();
    void WireInputCallbacks();

    // ── Send helpers ─────────────────────────────────────────────────────
    void OnSendButtonClick();
    void OnAttachButtonClick();
    void OnEmojiButtonClick();
    void OnScreenshotButtonClick();
    void OnVoiceCallButtonClick();
    void OnVideoCallButtonClick();
    void OnOrderByChange(const std::string& selection);
    void OnMessageFilterChange(const std::string& filter);

    // ── Incoming event handlers (called from manager callbacks) ──────────
    void OnIncomingMessage(const std::string& providerId,
                           const ChatMessage& message);
    void OnTypingStatusChange(const std::string& providerId,
                              const std::string& conversationId,
                              const std::string& contactId,
                              bool isTyping);
    void OnContactListUpdate();
    void OnConnectionStateChange(const std::string& providerId,
                                 ChatConnectionState state);

    // ── Window resize handler ─────────────────────────────────────────────
    void OnWindowResize(int newWidth, int newHeight);
};

} // namespace ChatCube
} // namespace UltraCanvas
