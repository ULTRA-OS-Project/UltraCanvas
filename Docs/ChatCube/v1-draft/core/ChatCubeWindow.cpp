// ChatCube/core/ChatCubeWindow.cpp
// Main ChatCube application window implementation.
// Uses UltraCanvas layout managers exclusively — no manual coordinates.
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework

#include "ChatCubeWindow.h"
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasLayout.h"
#include <iostream>
#include <algorithm>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// ELEMENT ID CONSTANTS
// Unique IDs for every named UI element — no magic numbers
// ============================================================================

namespace ID {
    // Window
    constexpr long WIN_MAIN           = 1000;

    // Left panel
    constexpr long SPLITTER           = 1010;
    constexpr long LEFT_PANEL         = 1020;
    constexpr long TOP_TOOLBAR        = 1030;
    constexpr long ORDER_DROPDOWN     = 1040;
    constexpr long CONTACT_LIST       = 1050;

    // Right panel
    constexpr long RIGHT_PANEL        = 1060;
    constexpr long CHAT_HEADER        = 1070;
    constexpr long HEADER_AVATAR      = 1080;
    constexpr long HEADER_TITLE       = 1090;
    constexpr long HEADER_SUBTITLE    = 1100;
    constexpr long HEADER_ACTIONS     = 1110;
    constexpr long BTN_VOICE_CALL     = 1120;
    constexpr long BTN_VIDEO_CALL     = 1130;
    constexpr long BTN_SEARCH         = 1140;

    // Message area
    constexpr long MESSAGE_LISTVIEW   = 1200;

    // Input section
    constexpr long INPUT_SECTION      = 1300;
    constexpr long INPUT_TOOLBAR      = 1310;
    constexpr long BTN_EMOJI          = 1320;
    constexpr long BTN_ATTACH         = 1330;
    constexpr long BTN_SCREENSHOT     = 1340;
    constexpr long MSG_FILTER_DROP    = 1350;
    constexpr long MESSAGE_TEXTAREA   = 1360;
    constexpr long BTN_SEND           = 1370;

    // Welcome screen
    constexpr long WELCOME_SCREEN     = 1400;
    constexpr long WELCOME_LOGO       = 1410;
    constexpr long WELCOME_LABEL      = 1420;

    // Contact row base (each row ID = CONTACT_ROW_BASE + index)
    constexpr long CONTACT_ROW_BASE   = 2000;
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ChatCubeWindow::ChatCubeWindow() = default;

// ============================================================================
// INITIALIZE
// ============================================================================

bool ChatCubeWindow::Initialize(const ChatCubeWindowStyle& style) {
    style_ = style;

    // ── Create the native window ────────────────────────────────────────
    WindowConfig config;
    config.title          = "ChatCube";
    config.width          = style_.WindowWidth;
    config.height         = style_.WindowHeight;
    config.resizable      = true;
    config.backgroundColor = style_.WindowBg;

    if (!Create(config)) {
        std::cerr << "[ChatCubeWindow] Failed to create native window." << std::endl;
        return false;
    }

    // ── Build layout sections in order ──────────────────────────────────
    BuildRootLayout();
    BuildLeftPanel();
    BuildTopToolbar();
    BuildContactList();
    BuildRightPanel();
    BuildChatHeader();
    BuildMessageArea();
    BuildInputSection();
    BuildWelcomeScreen();

    // ── Wire all callbacks ───────────────────────────────────────────────
    WireManagerCallbacks();
    WireInputCallbacks();

    // ── Show welcome screen initially ────────────────────────────────────
    ShowWelcomeScreen();

    std::cout << "[ChatCubeWindow] Initialized." << std::endl;
    return true;
}

// ============================================================================
// ROOT LAYOUT — Horizontal Splitter fills the window
// ============================================================================

void ChatCubeWindow::BuildRootLayout() {
    // The window itself is a Container. Its main layout is a single VBox
    // that holds the splitter (which fills all available space).
    auto mainLayout = CreateVBoxLayout(this);
    mainLayout->SetSpacing(0);

    // Horizontal splitter: left panel | right panel
    splitter_ = CreateSplitter("Splitter", ID::SPLITTER,
                               0, 0, style_.WindowWidth, style_.WindowHeight,
                               SplitterOrientation::Horizontal);
    splitter_->SetDividerPosition(style_.LeftPanelWidth);
    splitter_->SetMinimumLeftSize(style_.LeftPanelMinWidth);
    splitter_->SetMaximumLeftSize(style_.LeftPanelMaxWidth);

    mainLayout->AddUIElement(splitter_, 1)->SetWidthMode(SizeMode::Fill);
    AddChild(splitter_);

    // Register window resize callback so bubble delegate gets updated width
    onWindowResize = [this](int w, int h) {
        OnWindowResize(w, h);
    };
}

// ============================================================================
// LEFT PANEL — Contact/Conversation list
// ============================================================================

void ChatCubeWindow::BuildLeftPanel() {
    leftPanel_ = std::make_shared<UltraCanvasContainer>(
        "LeftPanel", ID::LEFT_PANEL, 0, 0, style_.LeftPanelWidth, style_.WindowHeight);
    leftPanel_->SetBackgroundColor(style_.LeftPanelBg);

    // VBox: TopToolbar (fixed) | ContactListContainer (stretch=1)
    auto leftLayout = CreateVBoxLayout(leftPanel_.get());
    leftLayout->SetSpacing(0);

    splitter_->SetLeftPanel(leftPanel_);
}

// ============================================================================
// TOP TOOLBAR (inside left panel)
// Order by | [miniature view] | [search] | [add]
// ============================================================================

void ChatCubeWindow::BuildTopToolbar() {
    // ── Toolbar container (HBox) ─────────────────────────────────────────
    topToolbar_ = CreateToolbar("TopToolbar", ID::TOP_TOOLBAR,
                                0, 0, style_.LeftPanelWidth, style_.TopToolbarHeight);
    topToolbar_->SetFixedHeight(style_.TopToolbarHeight);

    // "Order by" label
    ToolbarItemDescriptor lblDesc;
    lblDesc.type = ToolbarItemType::Label;
    lblDesc.identifier = "OrderByLabel";
    lblDesc.text = "Order by";
    topToolbar_->AddItem(lblDesc);

    // Order-by dropdown
    orderByDropdown_ = CreateDropdown("OrderByDrop", ID::ORDER_DROPDOWN,
                                      0, 0, 120, style_.TopToolbarHeight - 6);
    orderByDropdown_->AddItem("Last message");
    orderByDropdown_->AddItem("Online");
    orderByDropdown_->AddItem("Name");
    orderByDropdown_->SetSelectedIndex(0);
    orderByDropdown_->onItemSelected = [this](int /*idx*/, const DropdownItem& item) {
        OnOrderByChange(item.text);
    };

    ToolbarItemDescriptor dropDesc;
    dropDesc.type   = ToolbarItemType::CustomWidget;
    dropDesc.widget = orderByDropdown_;
    topToolbar_->AddItem(dropDesc);

    // Stretch pushes buttons to the right
    ToolbarItemDescriptor stretchDesc;
    stretchDesc.type    = ToolbarItemType::Stretch;
    stretchDesc.stretch = 1.0f;
    topToolbar_->AddItem(stretchDesc);

    // [Miniature view] button
    topToolbar_->AddItem(UltraCanvasToolbarBuilder("TopToolbar")
        .AddButton("BtnMiniature", "", "Resources/Media/Images/icons/view_mini.png",
                   [](){})
        .Build()->GetItem("BtnMiniature")->GetDescriptor());

    // [Search] button
    topToolbar_->AddItem(UltraCanvasToolbarBuilder("TopToolbar")
        .AddButton("BtnSearch", "", "Resources/Media/Images/icons/search.png",
                   [](){})
        .Build()->GetItem("BtnSearch")->GetDescriptor());

    // [Add conversation] button
    topToolbar_->AddItem(UltraCanvasToolbarBuilder("TopToolbar")
        .AddButton("BtnAdd", "+", "",
                   [this](){ /* Open new conversation dialog */ })
        .Build()->GetItem("BtnAdd")->GetDescriptor());

    // Add toolbar to left panel layout (fixed height)
    auto* leftLayout = static_cast<UltraCanvasBoxLayout*>(leftPanel_->GetLayout());
    if (leftLayout) {
        leftLayout->AddUIElement(topToolbar_)->SetFixedHeight(style_.TopToolbarHeight)
                                              ->SetWidthMode(SizeMode::Fill);
    }
    leftPanel_->AddChild(topToolbar_);
}

// ============================================================================
// CONTACT LIST (inside left panel, below toolbar)
// Uses a scrollable Container; each conversation becomes a Template row
// ============================================================================

void ChatCubeWindow::BuildContactList() {
    contactListContainer_ = std::make_shared<UltraCanvasContainer>(
        "ContactList", ID::CONTACT_LIST, 0, 0, style_.LeftPanelWidth, 400);
    contactListContainer_->SetBackgroundColor(style_.LeftPanelBg);

    // VBox: each contact row template stacked vertically
    auto listLayout = CreateVBoxLayout(contactListContainer_.get());
    listLayout->SetSpacing(0);

    // Enable vertical scroll; no horizontal scroll
    ContainerStyle scrollStyle;
    scrollStyle.autoShowScrollbars             = true;
    scrollStyle.forceShowHorizontalScrollbar   = false;
    contactListContainer_->SetContainerStyle(scrollStyle);

    // Add to left panel with stretch=1 so it fills remaining height
    auto* leftLayout = static_cast<UltraCanvasBoxLayout*>(leftPanel_->GetLayout());
    if (leftLayout) {
        leftLayout->AddUIElement(contactListContainer_, 1)
                  ->SetWidthMode(SizeMode::Fill);
    }
    leftPanel_->AddChild(contactListContainer_);
}

// ============================================================================
// RIGHT PANEL — Chat area
// VBox: ChatHeader (fixed) | MessageListView (stretch=1) | InputSection (fixed)
// ============================================================================

void ChatCubeWindow::BuildRightPanel() {
    rightPanel_ = std::make_shared<UltraCanvasContainer>(
        "RightPanel", ID::RIGHT_PANEL, 0, 0,
        style_.WindowWidth - style_.LeftPanelWidth, style_.WindowHeight);
    rightPanel_->SetBackgroundColor(style_.RightPanelBg);

    auto rightLayout = CreateVBoxLayout(rightPanel_.get());
    rightLayout->SetSpacing(0);

    splitter_->SetRightPanel(rightPanel_);
}

// ============================================================================
// CHAT HEADER (top of right panel)
// [Avatar 36px] [Title bold] [Subtitle muted]   [voice][video][search]
// ============================================================================

void ChatCubeWindow::BuildChatHeader() {
    chatHeaderSection_ = std::make_shared<UltraCanvasContainer>(
        "ChatHeader", ID::CHAT_HEADER, 0, 0, 0, style_.ChatHeaderHeight);
    chatHeaderSection_->SetBackgroundColor(style_.ChatHeaderBg);
    chatHeaderSection_->SetBorders(0, Colors::Transparent, 0,
                                    0, 0, 0, 1, Color(210, 210, 210, 255));

    // HBox: avatar | [VBox: title + subtitle] | stretch | action buttons
    auto headerHLayout = CreateHBoxLayout(chatHeaderSection_.get());
    headerHLayout->SetSpacing(8);
    headerHLayout->SetPadding(6, 6);

    // Avatar image
    chatHeaderAvatar_ = CreateImageElement("HeaderAvatar", ID::HEADER_AVATAR,
                                           0, 0, 36, 36);
    chatHeaderAvatar_->SetScaleMode(ImageScaleMode::Uniform);

    headerHLayout->AddUIElement(chatHeaderAvatar_)
                 ->SetFixedSize(36, 36)
                 ->SetCrossAlignment(LayoutAlignment::Center);

    // Title + subtitle in a VBox
    auto titleBox = std::make_shared<UltraCanvasContainer>(
        "TitleBox", ID::HEADER_TITLE, 0, 0, 0, style_.ChatHeaderHeight);
    auto titleLayout = CreateVBoxLayout(titleBox.get());
    titleLayout->SetSpacing(2);

    chatHeaderTitle_ = std::make_shared<UltraCanvasLabel>(
        "HeaderTitle", ID::HEADER_TITLE, 0, 0, 0, 20);
    chatHeaderTitle_->SetText("Select a conversation");
    chatHeaderTitle_->SetFontSize(style_.FontSizeHeader);
    chatHeaderTitle_->SetFontWeight(FontWeight::Bold);

    chatHeaderSubtitle_ = std::make_shared<UltraCanvasLabel>(
        "HeaderSubtitle", ID::HEADER_SUBTITLE, 0, 0, 0, 16);
    chatHeaderSubtitle_->SetText("");
    chatHeaderSubtitle_->SetFontSize(11.0f);
    chatHeaderSubtitle_->SetTextColor(Color(120, 120, 120, 255));

    titleLayout->AddUIElement(chatHeaderTitle_)->SetWidthMode(SizeMode::Fill);
    titleLayout->AddUIElement(chatHeaderSubtitle_)->SetWidthMode(SizeMode::Fill);

    headerHLayout->AddUIElement(titleBox, 1)
                 ->SetWidthMode(SizeMode::Fill)
                 ->SetCrossAlignment(LayoutAlignment::Center);

    // Action buttons (voice call / video call / search)
    chatHeaderActions_ = CreateToolbar("HeaderActions", ID::HEADER_ACTIONS,
                                       0, 0, 120, style_.ChatHeaderHeight);

    chatHeaderActions_->AddItem(UltraCanvasToolbarBuilder("HeaderActions")
        .AddButton("BtnVoiceCall", "",
                   "Resources/Media/Images/icons/phone.png",
                   [this](){ OnVoiceCallButtonClick(); })
        .Build()->GetItem("BtnVoiceCall")->GetDescriptor());

    chatHeaderActions_->AddItem(UltraCanvasToolbarBuilder("HeaderActions")
        .AddButton("BtnVideoCall", "",
                   "Resources/Media/Images/icons/video.png",
                   [this](){ OnVideoCallButtonClick(); })
        .Build()->GetItem("BtnVideoCall")->GetDescriptor());

    chatHeaderActions_->AddItem(UltraCanvasToolbarBuilder("HeaderActions")
        .AddButton("BtnChatSearch", "",
                   "Resources/Media/Images/icons/search.png",
                   [](){})
        .Build()->GetItem("BtnChatSearch")->GetDescriptor());

    headerHLayout->AddUIElement(chatHeaderActions_)
                 ->SetFixedWidth(120)
                 ->SetCrossAlignment(LayoutAlignment::Center);

    // Add header to right panel (fixed height)
    auto* rightLayout = static_cast<UltraCanvasBoxLayout*>(rightPanel_->GetLayout());
    if (rightLayout) {
        rightLayout->AddUIElement(chatHeaderSection_)
                   ->SetFixedHeight(style_.ChatHeaderHeight)
                   ->SetWidthMode(SizeMode::Fill);
    }
    rightPanel_->AddChild(chatHeaderSection_);
}

// ============================================================================
// MESSAGE AREA — ListView with ChatBubbleDelegate (stretch=1)
// ============================================================================

void ChatCubeWindow::BuildMessageArea() {
    // ── Model ────────────────────────────────────────────────────────────
    messageModel_   = std::make_shared<ChatMessageModel>();

    // ── Delegate ─────────────────────────────────────────────────────────
    bubbleDelegate_ = std::make_shared<ChatBubbleDelegate>();
    bubbleDelegate_->SetViewWidth(style_.WindowWidth - style_.LeftPanelWidth - 20);

    // ── ListView ─────────────────────────────────────────────────────────
    messageListView_ = CreateListView("MessageList", ID::MESSAGE_LISTVIEW,
                                      0, 0, 0, 0);
    messageListView_->SetModel(messageModel_.get());
    messageListView_->SetDelegate(bubbleDelegate_);
    messageListView_->SetShowHeader(false);       // No column headers for chat
    messageListView_->SetBackgroundColor(style_.RightPanelBg);
    messageListView_->SetBorders(0, Colors::Transparent);

    // Disable selection highlight — bubbles render their own state
    ListViewStyle lvStyle;
    lvStyle.selectionBackgroundColor = Colors::Transparent;
    lvStyle.hoverBackgroundColor     = Color(0, 0, 0, 12); // Very subtle hover
    messageListView_->SetStyle(lvStyle);

    // Auto-scroll to bottom when a new message row is inserted
    messageModel_->onRowInserted = [this](int row) {
        messageListView_->ScrollToRow(row);
    };

    // Add to right panel with stretch=1
    auto* rightLayout = static_cast<UltraCanvasBoxLayout*>(rightPanel_->GetLayout());
    if (rightLayout) {
        rightLayout->AddUIElement(messageListView_, 1)
                   ->SetWidthMode(SizeMode::Fill);
    }
    rightPanel_->AddChild(messageListView_);
}

// ============================================================================
// INPUT SECTION — Toolbar + TextArea + Send button
// ============================================================================

void ChatCubeWindow::BuildInputSection() {
    const int totalInputH = style_.InputToolbarHeight + style_.InputAreaHeight + 1;

    inputSection_ = std::make_shared<UltraCanvasContainer>(
        "InputSection", ID::INPUT_SECTION, 0, 0, 0, totalInputH);
    inputSection_->SetBackgroundColor(style_.InputAreaBg);
    inputSection_->SetBorders(1, Color(210, 210, 210, 255),
                               0, 1, 0, 0);  // Top border only

    auto inputVLayout = CreateVBoxLayout(inputSection_.get());
    inputVLayout->SetSpacing(0);

    // ── Input toolbar row ─────────────────────────────────────────────────
    inputToolbar_ = CreateToolbar("InputToolbar", ID::INPUT_TOOLBAR,
                                  0, 0, 0, style_.InputToolbarHeight);
    inputToolbar_->SetFixedHeight(style_.InputToolbarHeight);

    // Emoji button
    inputToolbar_->AddItem(UltraCanvasToolbarBuilder("InputToolbar")
        .AddButton("BtnEmoji", "",
                   "Resources/Media/Images/icons/emoji.png",
                   [this](){ OnEmojiButtonClick(); })
        .Build()->GetItem("BtnEmoji")->GetDescriptor());

    // Attach file button
    inputToolbar_->AddItem(UltraCanvasToolbarBuilder("InputToolbar")
        .AddButton("BtnAttach", "",
                   "Resources/Media/Images/icons/attach.png",
                   [this](){ OnAttachButtonClick(); })
        .Build()->GetItem("BtnAttach")->GetDescriptor());

    // Screenshot button (RISC OS heritage feature)
    inputToolbar_->AddItem(UltraCanvasToolbarBuilder("InputToolbar")
        .AddButton("BtnScreenshot", "",
                   "Resources/Media/Images/icons/screenshot.png",
                   [this](){ OnScreenshotButtonClick(); })
        .Build()->GetItem("BtnScreenshot")->GetDescriptor());

    // Separator
    ToolbarItemDescriptor sepDesc;
    sepDesc.type = ToolbarItemType::Separator;
    inputToolbar_->AddItem(sepDesc);

    // Message filter dropdown ("All messages" / "Media" / "Files" / "Links")
    messageFilterDropdown_ = CreateDropdown("MsgFilterDrop", ID::MSG_FILTER_DROP,
                                            0, 0, 130, style_.InputToolbarHeight - 6);
    messageFilterDropdown_->AddItem("All messages");
    messageFilterDropdown_->AddItem("Media");
    messageFilterDropdown_->AddItem("Files");
    messageFilterDropdown_->AddItem("Links");
    messageFilterDropdown_->SetSelectedIndex(0);
    messageFilterDropdown_->onItemSelected = [this](int /*idx*/, const DropdownItem& item) {
        OnMessageFilterChange(item.text);
    };

    ToolbarItemDescriptor filterDesc;
    filterDesc.type   = ToolbarItemType::CustomWidget;
    filterDesc.widget = messageFilterDropdown_;
    inputToolbar_->AddItem(filterDesc);

    // Stretch to right
    ToolbarItemDescriptor stretchDesc;
    stretchDesc.type    = ToolbarItemType::Stretch;
    stretchDesc.stretch = 1.0f;
    inputToolbar_->AddItem(stretchDesc);

    inputVLayout->AddUIElement(inputToolbar_)
                ->SetFixedHeight(style_.InputToolbarHeight)
                ->SetWidthMode(SizeMode::Fill);
    inputSection_->AddChild(inputToolbar_);

    // ── Text input row: [TextArea | Send button] ──────────────────────────
    auto inputRow = std::make_shared<UltraCanvasContainer>(
        "InputRow", 1399, 0, 0, 0, style_.InputAreaHeight);
    auto inputRowLayout = CreateHBoxLayout(inputRow.get());
    inputRowLayout->SetSpacing(6);
    inputRowLayout->SetPadding(6, 4);

    // Message TextArea (multi-line, no syntax highlight)
    messageInput_ = std::make_shared<UltraCanvasTextArea>(
        "MessageInput", ID::MESSAGE_TEXTAREA, 0, 0, 0, style_.InputAreaHeight - 8);
    messageInput_->SetPlaceholder("Type a message...");
    messageInput_->SetEditingMode(TextAreaEditingMode::Plain);
    messageInput_->SetFontSize(13.0f);
    messageInput_->SetShowLineNumbers(false);
    messageInput_->SetWordWrap(true);

    // Send on Enter (Shift+Enter = newline)
    messageInput_->onTextSubmit = [this]() {
        OnSendButtonClick();
    };

    // Send button
    sendButton_ = std::make_shared<UltraCanvasButton>(
        "BtnSend", ID::BTN_SEND, 0, 0, 48, style_.InputAreaHeight - 8);
    sendButton_->SetText("➤");
    sendButton_->SetFontSize(16.0f);
    sendButton_->onClick = [this]() {
        OnSendButtonClick();
    };

    inputRowLayout->AddUIElement(messageInput_, 1)->SetWidthMode(SizeMode::Fill);
    inputRowLayout->AddUIElement(sendButton_)
                  ->SetFixedSize(48, style_.InputAreaHeight - 8)
                  ->SetCrossAlignment(LayoutAlignment::Center);

    inputRow->AddChild(messageInput_);
    inputRow->AddChild(sendButton_);

    inputVLayout->AddUIElement(inputRow)
                ->SetFixedHeight(style_.InputAreaHeight)
                ->SetWidthMode(SizeMode::Fill);
    inputSection_->AddChild(inputRow);

    // Add input section to right panel (fixed height)
    auto* rightLayout = static_cast<UltraCanvasBoxLayout*>(rightPanel_->GetLayout());
    if (rightLayout) {
        rightLayout->AddUIElement(inputSection_)
                   ->SetFixedHeight(totalInputH)
                   ->SetWidthMode(SizeMode::Fill);
    }
    rightPanel_->AddChild(inputSection_);
}

// ============================================================================
// WELCOME SCREEN (shown before any conversation is selected)
// Centred ChatCube logo + version label
// ============================================================================

void ChatCubeWindow::BuildWelcomeScreen() {
    welcomeScreen_ = std::make_shared<UltraCanvasContainer>(
        "WelcomeScreen", ID::WELCOME_SCREEN, 0, 0, 0, 0);
    welcomeScreen_->SetBackgroundColor(style_.RightPanelBg);

    // VBox centred with stretch on top and bottom
    auto wLayout = CreateVBoxLayout(welcomeScreen_.get());
    wLayout->SetDefaultMainAxisAlignment(LayoutAlignment::Center);
    wLayout->SetDefaultCrossAxisAlignment(LayoutAlignment::Center);

    // Logo image
    auto logoImg = CreateImageElement("WelcomeLogo", ID::WELCOME_LOGO,
                                      0, 0, 180, 180);
    logoImg->SetImagePath("Resources/Media/Images/ChatCube_logo.png");
    logoImg->SetScaleMode(ImageScaleMode::Uniform);

    // App name label
    auto appLabel = std::make_shared<UltraCanvasLabel>(
        "WelcomeTitle", ID::WELCOME_LABEL, 0, 0, 0, 36);
    appLabel->SetText("Chat Cube");
    appLabel->SetFontSize(28.0f);
    appLabel->SetFontWeight(FontWeight::Bold);
    appLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);

    // Version / tagline
    auto versionLabel = std::make_shared<UltraCanvasLabel>(
        "WelcomeVersion", ID::WELCOME_LABEL + 1, 0, 0, 0, 22);
    versionLabel->SetText(std::string("Version ") + CHATCUBE_VERSION
                          + "  —  www.RISCOSCloverleaf.com");
    versionLabel->SetFontSize(12.0f);
    versionLabel->SetTextColor(Color(120, 120, 120, 255));
    versionLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);

    wLayout->AddStretch(1);
    wLayout->AddUIElement(logoImg)->SetFixedSize(180, 180)
                                   ->SetCrossAlignment(LayoutAlignment::Center);
    wLayout->AddSpacing(16);
    wLayout->AddUIElement(appLabel)->SetWidthMode(SizeMode::Fill);
    wLayout->AddSpacing(6);
    wLayout->AddUIElement(versionLabel)->SetWidthMode(SizeMode::Fill);
    wLayout->AddStretch(1);

    welcomeScreen_->AddChild(logoImg);
    welcomeScreen_->AddChild(appLabel);
    welcomeScreen_->AddChild(versionLabel);

    rightPanel_->AddChild(welcomeScreen_);
}

// ============================================================================
// CONTACT ROW — One Template per ChatConversation
// Avatar | [VBox: name + snippet] | [VBox: time + unread badge]
// ============================================================================

void ChatCubeWindow::AddContactRow(const ChatConversation& conv) {
    const long rowId = ID::CONTACT_ROW_BASE
                      + static_cast<long>(conv.Summary.UnreadCount) // unique enough for demo
                      + static_cast<long>(conv.ConversationId.size());

    // Each row is a Template with horizontal flow layout
    UltraCanvasTemplateBuilder builder("ContactRow_" + conv.ConversationId);

    TemplateAppearance rowAppearance;
    rowAppearance.backgroundColor = style_.LeftPanelBg;
    rowAppearance.SetPadding(8.0f);
    rowAppearance.borderWidth = 0.0f;

    // Template dimensions: full width of left panel, fixed row height
    TemplateDimensions dims = TemplateDimensions::Fixed(style_.LeftPanelWidth, 64);

    builder.SetAppearance(rowAppearance)
           .SetDimensions(dims)
           .SetPlacementRule(TemplatePlacementRule::Flow(LayoutDirection::Horizontal, 8.0f));

    // Avatar element (Image)
    TemplateElementDescriptor avatarDesc;
    avatarDesc.elementType = "Image";
    avatarDesc.identifier  = "avatar";
    avatarDesc.iconPath    = conv.AvatarPath;
    avatarDesc.constraints.size = { 40, 40 };
    builder.AddElement(avatarDesc);

    // Name label
    builder.AddLabel("name", conv.Summary.DisplayName);

    // Last message snippet label (muted)
    builder.AddLabel("snippet", conv.Summary.LastMessageSnippet);

    // Timestamp label
    builder.AddLabel("time", conv.Summary.LastMessageTime);

    // Provider icon (small, top-right of avatar)
    TemplateElementDescriptor providerDesc;
    providerDesc.elementType = "Image";
    providerDesc.identifier  = "providerIcon";
    providerDesc.iconPath    = conv.Summary.ProviderIconPath;
    providerDesc.constraints.size = { 14, 14 };
    builder.AddElement(providerDesc);

    auto rowTemplate = builder.Build();

    // Click on this row opens the conversation
    const std::string providerId     = conv.ProviderId;
    const std::string conversationId = conv.ConversationId;
    const std::string title          = conv.Summary.DisplayName;
    const std::string avatar         = conv.AvatarPath;

    rowTemplate->onClick = [this, providerId, conversationId, title, avatar]() {
        OpenConversation(providerId, conversationId, title, avatar);
    };

    // Highlight if this is the active conversation
    if (conversationId == activeConversationId_) {
        rowAppearance.backgroundColor = style_.SelectedContactBg;
        rowTemplate->SetBackgroundColor(style_.SelectedContactBg);
    }

    auto* listLayout = static_cast<UltraCanvasBoxLayout*>(
        contactListContainer_->GetLayout());
    if (listLayout) {
        listLayout->AddUIElement(rowTemplate)
                  ->SetFixedHeight(64)
                  ->SetWidthMode(SizeMode::Fill);
    }
    contactListContainer_->AddChild(std::move(rowTemplate));
}

void ChatCubeWindow::RebuildContactList() {
    // Clear existing rows
    contactListContainer_->ClearChildren();

    // Rebuild from cached conversations_
    for (const auto& conv : conversations_) {
        AddContactRow(conv);
    }
}

// ============================================================================
// OPEN CONVERSATION
// ============================================================================

void ChatCubeWindow::OpenConversation(const std::string& providerId,
                                       const std::string& conversationId,
                                       const std::string& title,
                                       const std::string& avatarPath) {
    activeProviderId_     = providerId;
    activeConversationId_ = conversationId;
    conversationOpen_     = true;

    // ── Update header ─────────────────────────────────────────────────────
    chatHeaderTitle_->SetText(title);
    if (!avatarPath.empty()) {
        chatHeaderAvatar_->SetImagePath(avatarPath);
    }

    // ── Show chat UI, hide welcome screen ─────────────────────────────────
    welcomeScreen_->SetVisible(false);
    chatHeaderSection_->SetVisible(true);
    messageListView_->SetVisible(true);
    inputSection_->SetVisible(true);

    // ── Tell manager which conversation is active ─────────────────────────
    auto& mgr = ChatCubeProviderManager::GetInstance();
    mgr.SetActiveConversation(providerId, conversationId, messageModel_.get());

    // ── Mark as read ──────────────────────────────────────────────────────
    mgr.MarkConversationRead(providerId, conversationId);

    // ── Load message history ──────────────────────────────────────────────
    mgr.LoadConversation(providerId, conversationId, messageModel_.get());

    // Refresh contact list to update selection highlight
    RebuildContactList();

    // Focus the input field
    messageInput_->SetFocus(true);

    std::cout << "[ChatCubeWindow] Opened: " << title << std::endl;
}

void ChatCubeWindow::ShowWelcomeScreen() {
    conversationOpen_ = false;
    welcomeScreen_->SetVisible(true);
    chatHeaderSection_->SetVisible(false);
    messageListView_->SetVisible(false);
    inputSection_->SetVisible(false);
}

void ChatCubeWindow::RefreshContactList() {
    auto& mgr    = ChatCubeProviderManager::GetInstance();
    conversations_ = mgr.GetConversations();
    RebuildContactList();
}

// ============================================================================
// MANAGER CALLBACK WIRING
// ============================================================================

void ChatCubeWindow::WireManagerCallbacks() {
    auto& mgr = ChatCubeProviderManager::GetInstance();

    mgr.onMessageReceive = [this](const std::string& providerId,
                                   const ChatMessage& message) {
        OnIncomingMessage(providerId, message);
    };

    mgr.onMessageStatusChange = [this](const std::string& /*providerId*/,
                                        const std::string& messageId,
                                        ChatDeliveryStatus status) {
        messageModel_->UpdateDeliveryStatus(messageId, status);
    };

    mgr.onTypingStatusChange = [this](const std::string& providerId,
                                       const std::string& conversationId,
                                       const std::string& contactId,
                                       bool isTyping) {
        OnTypingStatusChange(providerId, conversationId, contactId, isTyping);
    };

    mgr.onContactListUpdate = [this]() {
        OnContactListUpdate();
    };

    mgr.onConnectionStateChange = [this](const std::string& providerId,
                                          ChatConnectionState state) {
        OnConnectionStateChange(providerId, state);
    };

    mgr.onConversationUpdate = [this](const ChatConversation& conv) {
        // Update the unread badge for this conversation row
        // Simplified: full rebuild
        conversations_ = ChatCubeProviderManager::GetInstance().GetConversations();
        RebuildContactList();
        (void)conv;
    };
}

// ============================================================================
// INPUT CALLBACK WIRING
// ============================================================================

void ChatCubeWindow::WireInputCallbacks() {
    // Typing indicator: fire SetTypingStatus while user is typing
    messageInput_->onTextChange = [this](const std::string& text) {
        if (!activeProviderId_.empty() && !activeConversationId_.empty()) {
            ChatCubeProviderManager::GetInstance().SetTypingStatus(
                activeProviderId_, activeConversationId_, !text.empty());
        }
    };
}

// ============================================================================
// SEND BUTTON
// ============================================================================

void ChatCubeWindow::OnSendButtonClick() {
    if (!conversationOpen_) return;

    const std::string text = messageInput_->GetText();
    if (text.empty()) return;

    // Build an optimistic local message so it appears immediately
    ChatMessage localMsg;
    localMsg.MessageId       = "local_" + std::to_string(
                                   std::chrono::steady_clock::now()
                                       .time_since_epoch().count());
    localMsg.ConversationId  = activeConversationId_;
    localMsg.SenderId        = "self";
    localMsg.SenderName      = "You";
    localMsg.Type            = ChatMessageType::Text;
    localMsg.Text            = text;
    localMsg.IsOwn           = true;
    localMsg.DeliveryStatus  = ChatDeliveryStatus::Sending;
    localMsg.TimestampDisplay = "...";

    messageModel_->AppendMessage(localMsg);

    // Clear input immediately
    messageInput_->SetText("");
    messageInput_->SetFocus(true);

    // Fire off to provider
    const std::string msgId = localMsg.MessageId;
    auto result = ChatCubeProviderManager::GetInstance()
        .SendTextMessage(activeProviderId_, activeConversationId_, text);

    if (!result.IsSuccess()) {
        // Mark as failed
        messageModel_->UpdateDeliveryStatus(msgId, ChatDeliveryStatus::Failed);
    } else {
        messageModel_->UpdateDeliveryStatus(msgId, ChatDeliveryStatus::Sent);
    }
}

// ============================================================================
// ATTACH BUTTON — opens file dialog via FileLoader
// ============================================================================

void ChatCubeWindow::OnAttachButtonClick() {
    if (!conversationOpen_) return;

    // Use UltraCanvas standard open dialog (Dialogs module — obligatory)
    UltraCanvasDialogManager::GetInstance().ShowOpenFileDialog(
        "Attach file",
        { FileFilter("All files", "*.*"),
          FileFilter("Images", "*.jpg;*.png;*.gif;*.webp") },
        "",
        [this](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) {
                // Determine if image or generic file
                const std::string ext = path.substr(path.find_last_of('.') + 1);
                const bool isImage = (ext == "jpg" || ext == "jpeg"
                                   || ext == "png" || ext == "gif"
                                   || ext == "webp");
                if (isImage) {
                    ChatCubeProviderManager::GetInstance()
                        .SendImage(activeProviderId_, activeConversationId_, path);
                } else {
                    ChatCubeProviderManager::GetInstance()
                        .SendFile(activeProviderId_, activeConversationId_, path);
                }
            }
        },
        this);
}

// ============================================================================
// EMOJI BUTTON — placeholder (sticker picker dialog goes here in Step 7)
// ============================================================================

void ChatCubeWindow::OnEmojiButtonClick() {
    // TODO Step 7: open sticker picker dialog
    std::cout << "[ChatCubeWindow] Emoji/sticker picker (Step 7)." << std::endl;
}

// ============================================================================
// SCREENSHOT BUTTON — RISC OS heritage feature
// Uses IODeviceManager to capture screen, then sends as image
// ============================================================================

void ChatCubeWindow::OnScreenshotButtonClick() {
    if (!conversationOpen_) return;

    // Show the screenshot context menu matching the original RISC OS ChatCube
    // Options: Full Screen | Select Area | Full Screen (hide chat) | Select Area (hide chat)
    auto ctxMenu = CreateContextMenu("ScreenshotMenu", 9900);
    ctxMenu->AddItem("Full Screen", [this]() {
        // IODeviceManager: capture screen → temp file → SendImage
        std::cout << "[ChatCubeWindow] Screenshot: Full Screen" << std::endl;
    });
    ctxMenu->AddItem("Select Area", [this]() {
        std::cout << "[ChatCubeWindow] Screenshot: Select Area" << std::endl;
    });
    ctxMenu->AddSeparator();
    ctxMenu->AddItem("Full Screen (hide chat)", [this]() {
        Hide();
        std::cout << "[ChatCubeWindow] Screenshot: Full Screen (hidden)" << std::endl;
    });
    ctxMenu->AddItem("Select Area (hide chat)", [this]() {
        Hide();
        std::cout << "[ChatCubeWindow] Screenshot: Select Area (hidden)" << std::endl;
    });
    ctxMenu->ShowAtMousePosition(this);
}

// ============================================================================
// CALL BUTTONS
// ============================================================================

void ChatCubeWindow::OnVoiceCallButtonClick() {
    if (!conversationOpen_) return;
    ChatCubeProviderManager::GetInstance()
        .StartVoiceCall(activeProviderId_, activeConversationId_);
}

void ChatCubeWindow::OnVideoCallButtonClick() {
    if (!conversationOpen_) return;
    ChatCubeProviderManager::GetInstance()
        .StartVideoCall(activeProviderId_, activeConversationId_);
}

// ============================================================================
// ORDER BY / FILTER
// ============================================================================

void ChatCubeWindow::OnOrderByChange(const std::string& selection) {
    // Sort conversations_ accordingly and rebuild list
    if (selection == "Name") {
        std::sort(conversations_.begin(), conversations_.end(),
                  [](const ChatConversation& a, const ChatConversation& b) {
                      return a.Title < b.Title;
                  });
    } else if (selection == "Online") {
        std::sort(conversations_.begin(), conversations_.end(),
                  [](const ChatConversation& a, const ChatConversation& b) {
                      return a.Summary.Presence < b.Summary.Presence;
                  });
    }
    // "Last message" is the default order from the provider
    RebuildContactList();
}

void ChatCubeWindow::OnMessageFilterChange(const std::string& filter) {
    // In production: filter messageModel_ to show only messages matching type
    // For now: log the selection
    std::cout << "[ChatCubeWindow] Message filter: " << filter << std::endl;
}

// ============================================================================
// INCOMING EVENT HANDLERS
// ============================================================================

void ChatCubeWindow::OnIncomingMessage(const std::string& providerId,
                                        const ChatMessage& message) {
    // The manager already appended to messageModel_ if it's the active conv.
    // Here we update the contact list row's snippet and unread badge.
    for (auto& conv : conversations_) {
        if (conv.ConversationId == message.ConversationId) {
            conv.Summary.LastMessageSnippet = message.Text.empty()
                ? "[" + std::to_string(static_cast<int>(message.Type)) + "]"
                : message.Text.substr(0, 60);
            conv.Summary.LastMessageTime = message.TimestampDisplay;
            if (conv.ConversationId != activeConversationId_) {
                ++conv.Summary.UnreadCount;
            }
            break;
        }
    }
    RebuildContactList();
    (void)providerId;
}

void ChatCubeWindow::OnTypingStatusChange(const std::string& /*providerId*/,
                                           const std::string& conversationId,
                                           const std::string& contactId,
                                           bool isTyping) {
    // Show "X is typing..." in the chat header subtitle
    if (conversationId == activeConversationId_) {
        chatHeaderSubtitle_->SetText(isTyping ? contactId + " is typing..." : "");
    }
}

void ChatCubeWindow::OnContactListUpdate() {
    conversations_ = ChatCubeProviderManager::GetInstance().GetConversations();
    RebuildContactList();
}

void ChatCubeWindow::OnConnectionStateChange(const std::string& providerId,
                                              ChatConnectionState state) {
    // Update window title to reflect connection state
    std::string suffix;
    switch (state) {
        case ChatConnectionState::Connecting:   suffix = " [Connecting...]"; break;
        case ChatConnectionState::Reconnecting: suffix = " [Reconnecting...]"; break;
        case ChatConnectionState::Error:        suffix = " [Connection error]"; break;
        default: break;
    }
    SetWindowTitle("ChatCube" + suffix);
    std::cout << "[ChatCubeWindow] " << providerId << " state: "
              << static_cast<int>(state) << std::endl;
}

void ChatCubeWindow::OnWindowResize(int newWidth, int newHeight) {
    // Update bubble delegate view width when window is resized
    if (bubbleDelegate_ && splitter_) {
        const int rightW = newWidth - splitter_->GetDividerPosition() - 20;
        bubbleDelegate_->SetViewWidth(std::max(200, rightW));
    }
    (void)newHeight;
}

} // namespace ChatCube
} // namespace UltraCanvas
