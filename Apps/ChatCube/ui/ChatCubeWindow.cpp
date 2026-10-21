// Apps/ChatCube/ui/ChatCubeWindow.cpp
// The ChatCube window: sign-in page, conversations, messages
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListSelection.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasVideoTile.h"
#include "UltraCanvasWindow.h"
#include "UltraIM/UltraIMCore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

constexpr float kTextFont = 10.0f;
constexpr int   kControlHeight = 28;
constexpr int   kSidebarWidth = 300;
constexpr int   kInputHeight = 64;
constexpr float kFormWidth = 300.0f;
constexpr int64_t kTypingShownMs = 5000;
constexpr int64_t kTypingResendMs = 3000;

const Color kPageBackground = Color(250, 250, 248, 255);
const Color kSidebarBackground = Color(242, 244, 246, 255);
const Color kTextColor = Color(36, 36, 40, 255);
const Color kMutedText = Color(110, 110, 118, 255);
const Color kRuleColor = Color(222, 224, 228, 255);
const Color kAccent = Color(46, 150, 70, 255);          // ChatCube green
const Color kAccentHover = Color(36, 128, 58, 255);
const Color kSelection = Color(46, 150, 70, 255);
const Color kCallBackground = Color(228, 243, 232, 255);
const Color kHangUp = Color(200, 52, 52, 255);
const Color kHangUpHover = Color(170, 40, 40, 255);

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text,
                                            float fontSize = kTextFont, const Color& color = kTextColor) {
    auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
    l->SetText(text);
    l->SetFontSize(fontSize);
    l->SetTextColor(color);
    l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    l->size.width = CSSLayout::Dimension::Auto();
    l->size.height = CSSLayout::Dimension::Auto();
    return l;
}

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text, int width,
                                              bool primary = false) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, kControlHeight, text);
    b->SetFontSize(kTextFont);
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    if (primary) {
        b->SetColors(kAccent, kAccentHover);
        b->SetTextColors(Color(255, 255, 255, 255));
    }
    return b;
}

std::shared_ptr<UltraCanvasTextInput> MakeInput(const std::string& id, const std::string& placeholder, int width) {
    auto t = std::make_shared<UltraCanvasTextInput>(id, 0, 0, width, kControlHeight);
    t->SetPlaceholder(placeholder);
    t->SetFontSize(kTextFont);
    t->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return t;
}

std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id, int gap = 8) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(gap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return row;
}

std::string PresenceText(UltraIMPresence presence) {
    switch (presence) {
        case UltraIMPresence::Online:       return "online";
        case UltraIMPresence::Away:         return "away";
        case UltraIMPresence::DoNotDisturb: return "do not disturb";
        case UltraIMPresence::Offline:      return "offline - messages wait on the relay";
        case UltraIMPresence::Unknown:      break;
    }
    return std::string();
}

} // namespace

ChatCubeWindow::ChatCubeWindow(std::string version, std::string providerId)
    : version_(std::move(version)), providerId_(std::move(providerId)) {}

ChatCubeWindow::~ChatCubeWindow() {
    // The tiles go with the window: no picture may be pushed into them after.
    if (!callId_.empty()) UltraIM_SetCallVideoSinks(providerId_, callId_, nullptr, nullptr);
    if (subscription_) UltraIM_Unsubscribe(subscription_);
    if (callTimer_)
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(callTimer_);
}

bool ChatCubeWindow::Create() {
    WindowConfig wc;
    // Every app shows its version in its main window title (AGENTS.md).
    wc.title = "ChatCube " + version_;
    wc.width = 1000;
    wc.height = 680;
    wc.resizable = true;
    window_ = CreateWindow(wc);
    if (!window_ || !window_->IsCreated()) {
        window_.reset();
        return false;
    }
    window_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window_->SetBackgroundColor(kPageBackground);
    window_->AddChild(BuildSignInPage());
    window_->AddChild(BuildChatView());
    window_->onWindowClosed = [this]() { if (onClosed) onClosed(); };

    subscription_ = UltraIM_Subscribe([this](const UltraIMEvent& e) { OnEvent(e); });

    UltraIMProviderInfo info;
    if (!UltraIM_GetProviderInfo(providerId_, info)) {
        ShowSignInPage();
        SetSignInStatus("The ChatCube Direct module was not found (Plugins/UltraIM). Reinstall ChatCube.");
        signIn_->SetDisabled(true);
        return true;
    }
    hasIdentity_ = info.hasStoredSession;
    signIn_->SetText(hasIdentity_ ? "Sign in" : "Create my ChatCube ID");
    name_->SetVisible(!hasIdentity_);
    ShowSignInPage();
    if (hasIdentity_) SignIn();         // a saved identity signs in without asking
    return true;
}

void ChatCubeWindow::Show() {
    if (window_) window_->Show();
}

// ===================================================================== views

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildSignInPage() {
    signInPage_ = std::make_shared<UltraCanvasContainer>("cc-signin");
    signInPage_->layout.SetFlexColumn()
        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
        .SetFlexAlignItems(CSSLayout::AlignItems::Center)
        .SetFlexGap(12);
    signInPage_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("cc-signin-title", "ChatCube", 24.0f);
    title->SetFontWeight(FontWeight::Bold);
    signInPage_->AddChild(title);
    signInPage_->AddChild(MakeLabel("cc-signin-tagline",
                                    "Chat directly with the people you know - no account, no password",
                                    11.0f, kMutedText));
    signInPage_->AddSpacer(8);

    name_ = MakeInput("cc-signin-name", "Your name, as your contacts will see it", static_cast<int>(kFormWidth));
    name_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    signInPage_->AddChild(name_);
    relay_ = MakeInput("cc-signin-relay", "Relay server (optional), e.g. https://relay.example.org",
                       static_cast<int>(kFormWidth));
    relay_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    signInPage_->AddChild(relay_);
    localNetwork_ = std::make_shared<UltraCanvasCheckbox>("cc-signin-lan", 0, 0, static_cast<long>(kFormWidth), 22,
                                                          "Also find people on this network");
    localNetwork_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    CheckboxVisualStyle lanStyle = localNetwork_->GetVisualStyle();
    lanStyle.base.fontSize = kTextFont;                 // the size of the page's other text
    lanStyle.base.textColor = kTextColor;
    localNetwork_->SetVisualStyle(lanStyle);
    signInPage_->AddChild(localNetwork_);

    signIn_ = MakeButton("cc-signin-go", "Create my ChatCube ID", static_cast<int>(kFormWidth), true);
    signIn_->SetOnClick([this]() { SignIn(); });
    signInPage_->AddChild(signIn_);

    signInStatus_ = MakeLabel("cc-signin-status", "", kTextFont, kMutedText);
    signInStatus_->SetWrap(TextWrap::WrapWord);
    signInStatus_->SetAlignment(TextAlignment::Center);
    signInStatus_->size.width = CSSLayout::Dimension::Px(460);
    signInPage_->AddChild(signInStatus_);

    auto note = MakeLabel(
        "cc-signin-note",
        "Your ChatCube ID is a key made on this computer. Messages are end-to-end encrypted; "
        "the relay only connects you and keeps messages for people who are offline. "
        "Without one, ChatCube finds the people on your network itself.",
        9.0f, kMutedText);
    note->SetWrap(TextWrap::WrapWord);
    note->SetAlignment(TextAlignment::Center);
    note->size.width = CSSLayout::Dimension::Px(460);
    signInPage_->AddChild(note);
    return signInPage_;
}

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildSidebar() {
    auto sidebar = std::make_shared<UltraCanvasContainer>("cc-sidebar");
    sidebar->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    sidebar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    sidebar->size.width = CSSLayout::Dimension::Px(kSidebarWidth);
    sidebar->SetPadding(8, 8, 8, 8);
    sidebar->SetBorderRight(1, kRuleColor);
    sidebar->SetBackgroundColor(kSidebarBackground);

    auto idRow = MakeRow("cc-myid-row");
    myId_ = MakeLabel("cc-myid", "", 9.0f, kMutedText);
    myId_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    idRow->AddChild(myId_);
    auto copy = MakeButton("cc-myid-copy", "Copy", 56);
    copy->SetOnClick([this]() { CopyMyId(); });
    idRow->AddChild(copy);
    sidebar->AddChild(idRow);

    auto addRow = MakeRow("cc-add-row");
    contactId_ = MakeInput("cc-add-id", "Contact's ChatCube ID", kSidebarWidth - 16 - 60);
    contactId_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    contactId_->onEnterPressed = [this](const std::string&) { AddContact(); return true; };
    addRow->AddChild(contactId_);
    auto add = MakeButton("cc-add", "Add", 52, true);
    add->SetOnClick([this]() { AddContact(); });
    addRow->AddChild(add);
    sidebar->AddChild(addRow);

    auto newGroup = MakeButton("cc-new-group", "New group", kSidebarWidth - 16);
    newGroup->SetOnClick([this]() { ToggleGroupForm(); });
    sidebar->AddChild(newGroup);

    groupForm_ = std::make_shared<UltraCanvasContainer>("cc-group-form");
    groupForm_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    groupForm_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    groupTitle_ = MakeInput("cc-group-title", "Group name", kSidebarWidth - 16);
    groupForm_->AddChild(groupTitle_);
    groupMembers_ = MakeInput("cc-group-members", "Members' ChatCube IDs, separated by commas", kSidebarWidth - 16);
    groupMembers_->onEnterPressed = [this](const std::string&) { CreateGroup(); return true; };
    groupForm_->AddChild(groupMembers_);
    auto create = MakeButton("cc-group-create", "Create group", kSidebarWidth - 16, true);
    create->SetOnClick([this]() { CreateGroup(); });
    groupForm_->AddChild(create);
    groupForm_->SetVisible(false);
    sidebar->AddChild(groupForm_);

    // People on the local network who are not contacts yet (filled by RefreshNearby).
    nearbyBox_ = std::make_shared<UltraCanvasContainer>("cc-nearby");
    nearbyBox_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    nearbyBox_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    nearbyBox_->SetVisible(false);
    sidebar->AddChild(nearbyBox_);

    conversations_ = std::make_shared<ConversationListModel>();
    conversationList_ = std::make_shared<UltraCanvasListView>("cc-conversations", 0, 0, kSidebarWidth - 16, 300);
    conversationList_->SetModel(conversations_);
    conversationList_->SetDelegate(std::make_shared<ConversationDelegate>());
    conversationList_->SetRowHeight(56);
    ListViewStyle listStyle = conversationList_->GetStyle();
    listStyle.backgroundColor = kSidebarBackground;
    listStyle.selectionBackgroundColor = kSelection;
    conversationList_->SetStyle(listStyle);
    conversationList_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    conversationList_->onSelectionChanged = [this](const std::vector<int>& rows) {
        if (fillingList_ || rows.empty()) return;
        const auto& list = conversations_->Conversations();
        if (rows.front() >= 0 && rows.front() < static_cast<int>(list.size()))
            OpenConversation(list[static_cast<size_t>(rows.front())].conversationId);
    };
    sidebar->AddChild(conversationList_);
    return sidebar;
}

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildConversationPane() {
    auto pane = std::make_shared<UltraCanvasContainer>("cc-pane");
    pane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    pane->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // ----- header -----
    auto header = MakeRow("cc-header");
    header->SetPadding(8, 12, 8, 12);
    header->SetBorderBottom(1, kRuleColor);
    auto titles = std::make_shared<UltraCanvasContainer>("cc-header-titles");
    titles->layout.SetFlexColumn().SetFlexGap(2);
    titles->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    title_ = MakeLabel("cc-title", "", 12.0f);
    title_->SetFontWeight(FontWeight::Bold);
    titles->AddChild(title_);
    subtitle_ = MakeLabel("cc-subtitle", "", 9.0f, kMutedText);
    titles->AddChild(subtitle_);
    header->AddChild(titles);
    voiceCall_ = MakeButton("cc-call", "Call", 60);
    voiceCall_->SetDisabled(true);
    voiceCall_->SetOnClick([this]() { StartCall(false); });
    header->AddChild(voiceCall_);
    videoCall_ = MakeButton("cc-video", "Video", 60);
    videoCall_->SetDisabled(true);
    videoCall_->SetOnClick([this]() { StartCall(true); });
    header->AddChild(videoCall_);
    pane->AddChild(header);

    // ----- messages -----
    emptyHint_ = MakeLabel("cc-empty",
                           "Pick a conversation, or add a contact by the ChatCube ID they give you.",
                           11.0f, kMutedText);
    emptyHint_->SetAlignment(TextAlignment::Center);
    emptyHint_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    pane->AddChild(emptyHint_);

    messages_ = std::make_shared<MessageListModel>();
    bubbles_ = std::make_shared<MessageBubbleDelegate>();
    // A bubble measured while painting came out another height than laid
    // out: lay the list out again once this frame is done.
    bubbles_->onHeightsChanged = [this]() {
        if (relayoutPending_) return;
        relayoutPending_ = true;
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            app->PostToUIThread([this]() {
                relayoutPending_ = false;
                if (messageList_) {
                    messageList_->InvalidateRowHeights();
                    messageList_->RequestRedraw();
                }
            });
        }
    };
    messageList_ = std::make_shared<UltraCanvasListView>("cc-messages", 0, 0, 500, 400);
    messageList_->SetModel(messages_);
    messageList_->SetDelegate(bubbles_);
    messageList_->SetVariableRowHeights(true);
    ListViewStyle messageStyle = messageList_->GetStyle();
    messageStyle.backgroundColor = kPageBackground;
    messageStyle.selectionBackgroundColor = Colors::Transparent;   // bubbles show their own state
    messageStyle.hoverBackgroundColor = Colors::Transparent;
    messageList_->SetStyle(messageStyle);
    messageList_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    messageList_->SetVisible(false);
    pane->AddChild(messageList_);

    // ----- input -----
    auto inputRow = std::make_shared<UltraCanvasContainer>("cc-input-row");
    inputRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::End);
    inputRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    inputRow->SetPadding(8, 12, 4, 12);
    inputRow->SetBorderTop(1, kRuleColor);
    input_ = std::make_shared<UltraCanvasTextArea>("cc-input", 0, 0, 300, kInputHeight);
    input_->SetEditingMode(TextAreaEditingMode::PlainText);
    input_->SetWordWrap(true);
    input_->SetShowLineNumbers(false);
    input_->SetFontSize(11.0f);
    input_->SetPlaceholder("Message\xE2\x80\xA6  (Enter sends, Shift+Enter for a new line)");
    input_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    // Enter sends; Shift+Enter is left to the text area, which breaks the line.
    input_->onBeforeKeyDown = [this](const UCEvent& e) {
        const bool enter = e.virtualKey == UCKeys::Return || e.virtualKey == UCKeys::NumPadEnter;
        if (!enter || e.shift) return false;
        SendCurrentMessage();
        return true;
    };
    input_->SetOnTextChanged([this](const std::string& text) {
        if (activeConversation_.empty() || text.empty()) return;
        const int64_t now = NowMs();
        if (now - lastTypingSentMs_ < kTypingResendMs) return;
        lastTypingSentMs_ = now;
        UltraIM_SetTyping(providerId_, activeConversation_, true);
    });
    inputRow->AddChild(input_);
    send_ = MakeButton("cc-send", "Send", 72, true);
    send_->SetOnClick([this]() { SendCurrentMessage(); });
    inputRow->AddChild(send_);
    pane->AddChild(inputRow);
    return pane;
}

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildChatView() {
    chatView_ = std::make_shared<UltraCanvasContainer>("cc-chat");
    chatView_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    chatView_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // ----- the call bar (hidden without a call) -----
    callBar_ = MakeRow("cc-call-bar");
    callBar_->SetPadding(6, 12, 6, 12);
    callBar_->SetBackgroundColor(kCallBackground);
    callBar_->SetBorderBottom(1, kRuleColor);
    callText_ = MakeLabel("cc-call-text", "", 11.0f);
    callText_->SetFontWeight(FontWeight::Bold);
    callText_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    callBar_->AddChild(callText_);
    callAccept_ = MakeButton("cc-call-accept", "Accept", 80, true);
    callAccept_->SetOnClick([this]() { AcceptCall(); });
    callBar_->AddChild(callAccept_);
    callCamera_ = MakeButton("cc-call-camera", "Camera off", 96);
    callCamera_->SetOnClick([this]() { ToggleCamera(); });
    callBar_->AddChild(callCamera_);
    callMute_ = MakeButton("cc-call-mute", "Mute", 80);
    callMute_->SetOnClick([this]() { ToggleMute(); });
    callBar_->AddChild(callMute_);
    callEnd_ = MakeButton("cc-call-end", "Hang up", 80);
    callEnd_->SetColors(kHangUp, kHangUpHover);
    callEnd_->SetTextColors(Color(255, 255, 255, 255));
    callEnd_->SetOnClick([this]() { EndCall(); });
    callBar_->AddChild(callEnd_);
    callBar_->SetVisible(false);
    chatView_->AddChild(callBar_);

    // ----- the pictures of a video call (hidden otherwise) -----
    videoPanel_ = MakeRow("cc-video-panel", 10);
    videoPanel_->SetPadding(10, 12, 10, 12);
    videoPanel_->SetBackgroundColor(Color(16, 16, 20, 255));
    videoPanel_->layout.SetFlexAlignItems(CSSLayout::AlignItems::End);
    videoPanel_->size.height = CSSLayout::Dimension::Px(330.0f);
    videoPanel_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(330.0f));
    remoteVideo_ = std::make_shared<UltraCanvasVideoTile>("cc-remote-video", 0, 0, 400, 310);
    remoteVideo_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    VideoTileStyle remoteStyle;
    remoteStyle.cornerRadius = 6.0f;
    remoteStyle.captionFontSize = 12.0f;
    remoteVideo_->SetStyle(remoteStyle);
    videoPanel_->AddChild(remoteVideo_);
    selfVideo_ = std::make_shared<UltraCanvasVideoTile>("cc-self-video", 0, 0, 192, 144);
    selfVideo_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    selfVideo_->SetMirrored(true);
    selfVideo_->SetFit(VideoTileFit::Cover);
    VideoTileStyle selfStyle;
    selfStyle.cornerRadius = 6.0f;
    selfStyle.captionFontSize = 10.0f;
    selfStyle.borderColor = Color(90, 90, 100, 255);
    selfStyle.borderWidth = 1.0f;
    selfVideo_->SetStyle(selfStyle);
    videoPanel_->AddChild(selfVideo_);
    videoPanel_->SetVisible(false);
    chatView_->AddChild(videoPanel_);

    auto body = std::make_shared<UltraCanvasContainer>("cc-chat-body");
    body->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    body->AddChild(BuildSidebar());
    body->AddChild(BuildConversationPane());
    chatView_->AddChild(body);

    status_ = MakeLabel("cc-status", "", 9.0f, kMutedText);
    status_->SetMargin(2, 12, 6, 12);
    chatView_->AddChild(status_);
    return chatView_;
}

void ChatCubeWindow::ShowSignInPage() {
    if (chatView_) chatView_->SetVisible(false);
    if (signInPage_) signInPage_->SetVisible(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ShowChatView() {
    if (signInPage_) signInPage_->SetVisible(false);
    if (chatView_) chatView_->SetVisible(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::SetStatus(const std::string& text) {
    if (status_) status_->SetText(text);
}

void ChatCubeWindow::SetSignInStatus(const std::string& text) {
    if (signInStatus_) signInStatus_->SetText(text);
}

// =================================================================== actions

void ChatCubeWindow::SignIn() {
    UltraIMCredentials credentials;
    credentials.loginType = UltraIMLoginType::KeyPair;
    credentials.server = Trim(relay_->GetText());
    credentials.displayName = Trim(name_->GetText());
    credentials.createAccount = !hasIdentity_;
    if (!hasIdentity_ && credentials.displayName.empty()) {
        SetSignInStatus("Enter the name your contacts will see.");
        return;
    }
    // No relay: the local network only (the provider says so when there is
    // no relay remembered either).
    credentials.localNetwork = localNetwork_->IsChecked();
    signIn_->SetDisabled(true);
    SetSignInStatus("Connecting\xE2\x80\xA6");
    UltraIM_SignIn(providerId_, credentials, [this](const UltraIMResult& r) {
        signIn_->SetDisabled(false);
        if (!r) {
            SetSignInStatus(r.message);
            ShowSignInPage();
            return;
        }
        hasIdentity_ = true;
        UltraIMProviderInfo info;
        if (UltraIM_GetProviderInfo(providerId_, info))
            myId_->SetText("My ID  " + GroupUserId(info.localUserId));
        ShowChatView();
        SetStatus("Connected");
        RefreshConversations();
        RefreshNearby();
    });
}

void ChatCubeWindow::RefreshConversations() {
    UltraIM_FetchConversations([this](const UltraIMResult&, const std::vector<UltraIMConversation>& list) {
        fillingList_ = true;
        conversations_->SetConversations(list);
        const int row = conversations_->FindRow(activeConversation_);
        if (row >= 0) conversationList_->GetSelection()->Select(row);
        fillingList_ = false;
    });
}

void ChatCubeWindow::CopyMyId() {
    UltraIMProviderInfo info;
    if (!UltraIM_GetProviderInfo(providerId_, info) || info.localUserId.empty()) return;
    if (SetClipboardText(info.localUserId))
        SetStatus("Your ChatCube ID is on the clipboard - send it to the people who should reach you.");
}

void ChatCubeWindow::AddContact() {
    const std::string id = Trim(contactId_->GetText());
    if (id.empty()) return;
    AddContactById(id);
}

void ChatCubeWindow::AddContactById(const std::string& id) {
    SetStatus("Adding contact\xE2\x80\xA6");
    UltraIM_AddContact(providerId_, id, [this](const UltraIMResult& r, const UltraIMConversation& c) {
        if (!r) {
            SetStatus("Could not add the contact: " + r.message);
            return;
        }
        contactId_->SetText("");
        conversations_->Upsert(c);
        SetStatus("Added " + c.title);
        OpenConversation(c.conversationId);
        RefreshNearby();
    });
}

void ChatCubeWindow::RefreshNearby() {
    UltraIM_FetchNearbyContacts(providerId_, [this](const UltraIMResult&, const std::vector<UltraIMContact>& list) {
        nearbyBox_->ClearChildren();
        if (!list.empty()) nearbyBox_->AddChild(MakeLabel("cc-nearby-title", "On this network", 9.0f, kMutedText));
        for (const UltraIMContact& person : list) {
            auto row = MakeRow("cc-nearby-row", 6);
            auto name = MakeLabel("cc-nearby-name", person.displayName, kTextFont);
            name->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(name);
            auto add = MakeButton("cc-nearby-add", "Add", 52, true);
            const std::string id = person.contactId;
            add->SetOnClick([this, id]() { AddContactById(id); });
            row->AddChild(add);
            nearbyBox_->AddChild(row);
        }
        nearbyBox_->SetVisible(!list.empty());
        if (window_) window_->RequestRedraw();
    });
}

void ChatCubeWindow::ToggleGroupForm() {
    groupForm_->SetVisible(!groupForm_->IsVisible());
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::CreateGroup() {
    const std::string title = Trim(groupTitle_->GetText());
    if (title.empty()) {
        SetStatus("Give the group a name.");
        return;
    }
    std::vector<std::string> members;
    std::stringstream stream(groupMembers_->GetText());
    std::string item;
    while (std::getline(stream, item, ',')) {
        std::string id;
        for (char ch : item) {
            if (ch != ' ' && ch != '\t') id += ch;
        }
        if (!id.empty()) members.push_back(id);
    }
    SetStatus("Creating the group\xE2\x80\xA6");
    UltraIM_CreateGroup(providerId_, title, members, [this](const UltraIMResult& r, const UltraIMConversation& c) {
        if (!r) {
            SetStatus("Could not create the group: " + r.message);
            return;
        }
        groupTitle_->SetText("");
        groupMembers_->SetText("");
        groupForm_->SetVisible(false);
        conversations_->Upsert(c);
        SetStatus("Created " + c.title);
        OpenConversation(c.conversationId);
    });
}

void ChatCubeWindow::OpenConversation(const std::string& conversationId) {
    activeConversation_ = conversationId;
    typingContact_.clear();
    const UltraIMConversation* c = conversations_->Find(conversationId);
    messages_->SetGroup(c && c->isGroup);
    messages_->Clear();
    emptyHint_->SetVisible(false);
    messageList_->SetVisible(true);
    fillingList_ = true;
    const int row = conversations_->FindRow(conversationId);
    if (row >= 0) conversationList_->GetSelection()->Select(row);
    fillingList_ = false;
    UpdateHeader();

    UltraIM_FetchMessages(providerId_, conversationId, "", 200,
                          [this, conversationId](const UltraIMResult& r, const std::vector<UltraIMMessage>& list) {
        if (conversationId != activeConversation_) return;      // the user moved on
        if (!r) SetStatus("Could not load the messages: " + r.message);
        messages_->SetMessages(list);
        ScrollToEnd();
    });
    UltraIM_MarkRead(providerId_, conversationId);
    input_->SetFocus(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::UpdateHeader() {
    const UltraIMConversation* c = conversations_->Find(activeConversation_);
    if (!c) {
        title_->SetText("");
        subtitle_->SetText("");
        return;
    }
    title_->SetText(c->title);
    if (!typingContact_.empty() && NowMs() < typingUntilMs_) {
        subtitle_->SetText("typing\xE2\x80\xA6");
    } else if (c->isGroup) {
        subtitle_->SetText(std::to_string(c->memberIds.size()) + " members");
    } else {
        subtitle_->SetText(PresenceText(c->presence));
    }
    // Calls need UltraRTC media; the provider says when they are possible.
    // One call at a time.
    voiceCall_->SetDisabled(!callId_.empty() || !UltraIM_CanCall(providerId_, activeConversation_, false));
    videoCall_->SetDisabled(!callId_.empty() || !UltraIM_CanCall(providerId_, activeConversation_, true));
}

void ChatCubeWindow::ScrollToEnd() {
    if (messages_->GetRowCount() > 0) messageList_->EnsureRowVisible(messages_->GetRowCount() - 1);
}

void ChatCubeWindow::SendCurrentMessage() {
    if (activeConversation_.empty()) return;
    const std::string text = Trim(input_->GetText());
    if (text.empty()) return;

    // Optimistic: the bubble is shown now, its ticks follow the events.
    UltraIMProviderInfo info;
    UltraIM_GetProviderInfo(providerId_, info);
    UltraIMMessage message;
    message.providerId = providerId_;
    message.conversationId = activeConversation_;
    message.clientMessageId = UltraIM_NewClientMessageId();
    message.messageId = message.clientMessageId;
    message.senderId = info.localUserId;
    message.text = text;
    message.timestampMs = NowMs();
    message.isOwn = true;
    message.deliveryStatus = UltraIMDeliveryStatus::Sending;
    messages_->Upsert(message);
    ScrollToEnd();
    input_->SetText("");
    lastTypingSentMs_ = 0;
    UltraIM_SetTyping(providerId_, activeConversation_, false);

    UltraIMSendOptions options;
    options.clientMessageId = message.clientMessageId;
    const UltraIMResult r = UltraIM_SendText(providerId_, activeConversation_, text, options);
    if (!r) {
        messages_->UpdateStatus(message.clientMessageId, UltraIMDeliveryStatus::Failed);
        SetStatus("Not sent: " + r.message);
    }
}

// =================================================================== calls

std::string ChatCubeWindow::ContactName(const std::string& contactId) const {
    const UltraIMConversation* c = conversations_->Find(contactId);
    if (c && !c->title.empty()) return c->title;
    return GroupUserId(contactId);
}

void ChatCubeWindow::StartCall(bool video) {
    if (!callId_.empty() || activeConversation_.empty()) return;
    std::string callId;
    const UltraIMResult r = UltraIM_StartCall(providerId_, activeConversation_, video, callId);
    if (!r) {
        SetStatus("Could not call: " + r.message);
        return;
    }
    // The Ringing event that follows fills the bar; set it up now so a fast
    // answer finds the call.
    callId_ = callId;
    callContact_ = activeConversation_;
    callIncoming_ = false;
    callMuted_ = false;
    callVideo_ = video;
    cameraOn_ = video;
    callState_ = UltraIMCallState::Ringing;
    if (video) ConnectVideo();
    UpdateCallBar();
    UpdateHeader();
}

void ChatCubeWindow::ConnectVideo() {
    // The tiles take pictures from any thread; the window outlives the call's
    // sinks (they are cleared when the call ends and in the destructor).
    UltraCanvasVideoTile* remote = remoteVideo_.get();
    UltraCanvasVideoTile* self = selfVideo_.get();
    remote->ClearFrame();
    self->ClearFrame();
    remote->SetCaption("Connecting\xE2\x80\xA6");
    self->SetCaption(cameraOn_ ? "Starting camera\xE2\x80\xA6" : "Camera off");
    UltraIM_SetCallVideoSinks(providerId_, callId_,
        [remote](const uint8_t* rgba, int width, int height) { remote->PushFrame(rgba, width, height); },
        [self](const uint8_t* rgba, int width, int height) { self->PushFrame(rgba, width, height); });
}

void ChatCubeWindow::ToggleCamera() {
    if (callId_.empty() || !callVideo_) return;
    if (!UltraIM_SetCallCamera(providerId_, callId_, !cameraOn_)) return;
    cameraOn_ = !cameraOn_;
    if (!cameraOn_) {
        selfVideo_->ClearFrame();
        selfVideo_->SetCaption("Camera off");
    } else {
        selfVideo_->SetCaption("Starting camera\xE2\x80\xA6");
    }
    UpdateCallBar();
}

void ChatCubeWindow::AcceptCall() {
    if (callId_.empty() || !callIncoming_ || callState_ != UltraIMCallState::Ringing) return;
    const UltraIMResult r = UltraIM_AcceptCall(providerId_, callId_, callVideo_);
    if (!r) SetStatus("Could not answer: " + r.message);
}

void ChatCubeWindow::EndCall() {
    if (callId_.empty()) return;
    UltraIM_EndCall(providerId_, callId_);
}

void ChatCubeWindow::ToggleMute() {
    if (callId_.empty()) return;
    if (UltraIM_SetCallMuted(providerId_, callId_, !callMuted_)) {
        callMuted_ = !callMuted_;
        UpdateCallBar();
    }
}

void ChatCubeWindow::UpdateCallBar() {
    if (!callBar_) return;
    const bool inCall = !callId_.empty();
    callBar_->SetVisible(inCall);
    // The pictures once the call is answered (the caller sees its own while ringing).
    const bool showVideo = inCall && callVideo_ && (callState_ != UltraIMCallState::Ringing || !callIncoming_);
    videoPanel_->SetVisible(showVideo);
    if (inCall) {
        const std::string name = ContactName(callContact_);
        std::string text;
        switch (callState_) {
            case UltraIMCallState::Ringing:
                text = callIncoming_ ? std::string(callVideo_ ? "Incoming video call from " : "Incoming call from ") + name
                                     : "Calling " + name + "\xE2\x80\xA6";
                break;
            case UltraIMCallState::Connecting:
                text = "Connecting to " + name + "\xE2\x80\xA6";
                break;
            case UltraIMCallState::Active: {
                const int64_t seconds = std::max<int64_t>(0, (NowMs() - callActiveSinceMs_) / 1000);
                char clock[16];
                std::snprintf(clock, sizeof(clock), "%02d:%02d", static_cast<int>(seconds / 60),
                              static_cast<int>(seconds % 60));
                text = "In call with " + name + "   " + clock;
                if (callMuted_) text += "   (muted)";
                break;
            }
            case UltraIMCallState::Ended:
            case UltraIMCallState::Failed:
                break;
        }
        callText_->SetText(text);
        const bool ringingHere = callIncoming_ && callState_ == UltraIMCallState::Ringing;
        callAccept_->SetVisible(ringingHere);
        callMute_->SetVisible(!ringingHere);
        callCamera_->SetVisible(!ringingHere && callVideo_);
        callCamera_->SetText(cameraOn_ ? "Camera off" : "Camera on");
        callMute_->SetText(callMuted_ ? "Unmute" : "Mute");
        callEnd_->SetText(ringingHere ? "Decline" : (callState_ == UltraIMCallState::Ringing ? "Cancel" : "Hang up"));
    }
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::OnCallEvent(const UltraIMEvent& e) {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (e.type == UltraIMEventType::CallReceive) {
        if (!callId_.empty() && callId_ != e.callId) return;     // the provider answers "busy" itself
        callId_ = e.callId;
        callContact_ = e.contactId;
        callIncoming_ = true;
        callMuted_ = false;
        callVideo_ = e.isVideo;
        cameraOn_ = e.isVideo;
        callState_ = UltraIMCallState::Ringing;
        if (callVideo_) ConnectVideo();
        SetStatus(std::string(callVideo_ ? "Incoming video call from " : "Incoming call from ") + ContactName(e.contactId));
        if (activeConversation_.empty() && conversations_->Find(e.contactId)) OpenConversation(e.contactId);
        UpdateCallBar();
        UpdateHeader();
        return;
    }
    if (e.callId != callId_) return;
    if (e.type == UltraIMEventType::CallMediaChange) {
        // The other side's camera: off shows a caption, not a frozen picture.
        if (!e.isVideo) {
            remoteVideo_->ClearFrame();
            remoteVideo_->SetCaption(ContactName(callContact_) + " turned the camera off");
        } else {
            remoteVideo_->SetCaption("");
        }
        return;
    }
    const bool becameActive = e.callState == UltraIMCallState::Active && callState_ != UltraIMCallState::Active;
    callState_ = e.callState;
    if (e.callState == UltraIMCallState::Active) {
        // Active comes again with a notice (no camera) - the clock runs on.
        if (becameActive) callActiveSinceMs_ = NowMs();
        if (!e.errorText.empty()) SetStatus(e.errorText);           // e.g. no microphone
        if (e.errorText.rfind("No camera", 0) == 0) selfVideo_->SetCaption("No camera");
        if (app && !callTimer_) callTimer_ = app->StartTimer(1000, true, [this](TimerId) { UpdateCallBar(); });
    }
    if (e.callState == UltraIMCallState::Ended && e.errorText == "crossed") {
        // We called each other at once: the CallReceive for the other side's
        // call, answered by the provider, follows at once.
        callId_.clear();
        return;
    }
    if (e.callState == UltraIMCallState::Ended || e.callState == UltraIMCallState::Failed) {
        const std::string name = ContactName(callContact_);
        std::string why;
        if (e.errorText == "declined")       why = name + " declined the call";
        else if (e.errorText == "busy")      why = name + " is in another call";
        else if (e.errorText == "no answer") why = name + " did not answer";
        else if (e.errorText == "missed")    why = "Missed call from " + name;
        else if (e.errorText == "offline")   why = name + " is offline";
        else if (!e.errorText.empty())       why = "Call ended: " + e.errorText;
        else                                 why = "Call ended";
        if (!why.empty()) SetStatus(why);
        if (callVideo_) {
            UltraIM_SetCallVideoSinks(providerId_, callId_, nullptr, nullptr);
            remoteVideo_->ClearFrame();
            selfVideo_->ClearFrame();
        }
        callVideo_ = false;
        callId_.clear();
        callContact_.clear();
        if (app && callTimer_) app->StopTimer(callTimer_);
        callTimer_ = 0;
    }
    UpdateCallBar();
    UpdateHeader();
}

// =================================================================== UltraIM

void ChatCubeWindow::OnEvent(const UltraIMEvent& e) {
    if (e.providerId != providerId_) return;
    switch (e.type) {
        case UltraIMEventType::MessageReceive:
            if (e.conversationId == activeConversation_) {
                messages_->Upsert(e.message);
                ScrollToEnd();
                UltraIM_MarkRead(providerId_, activeConversation_);
                if (e.contactId == typingContact_) typingContact_.clear();
                UpdateHeader();
            }
            break;
        case UltraIMEventType::MessageDelivered:
            if (e.conversationId == activeConversation_)
                messages_->UpdateStatus(e.message.clientMessageId, e.deliveryStatus);
            if (e.deliveryStatus == UltraIMDeliveryStatus::Failed) SetStatus("A message could not be sent.");
            break;
        case UltraIMEventType::ConversationUpdate: {
            fillingList_ = true;
            UltraIMConversation c = e.conversation;
            if (c.conversationId == activeConversation_) c.unreadCount = 0;   // open: read as it arrives
            conversations_->Upsert(c);
            const int row = conversations_->FindRow(activeConversation_);
            if (row >= 0) conversationList_->GetSelection()->Select(row);
            fillingList_ = false;
            if (c.conversationId == activeConversation_) UpdateHeader();
            break;
        }
        case UltraIMEventType::ConversationRemove:
            conversations_->Remove(e.conversationId);
            if (e.conversationId == activeConversation_) {
                activeConversation_.clear();
                messages_->Clear();
                messageList_->SetVisible(false);
                emptyHint_->SetVisible(true);
                UpdateHeader();
            }
            break;
        case UltraIMEventType::PresenceChange:
            conversations_->SetPresence(e.contactId, e.presence);
            if (e.conversationId == activeConversation_) UpdateHeader();
            break;
        case UltraIMEventType::TypingChange:
            if (e.conversationId != activeConversation_) break;
            typingContact_ = e.isTyping ? e.contactId : std::string();
            typingUntilMs_ = NowMs() + kTypingShownMs;
            UpdateHeader();
            if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                app->StartTimer(static_cast<unsigned>(kTypingShownMs + 100), false,
                                [this](TimerId) { UpdateHeader(); });
            }
            break;
        case UltraIMEventType::ConnectionStateChange:
            switch (e.connectionState) {
                case UltraIMConnectionState::Connected:    SetStatus("Connected"); break;
                case UltraIMConnectionState::Connecting:   SetStatus("Connecting\xE2\x80\xA6"); break;
                case UltraIMConnectionState::Reconnecting: SetStatus("Connection lost - reconnecting\xE2\x80\xA6"); break;
                case UltraIMConnectionState::Error:
                    SetStatus("Connection error: " + e.errorText);
                    SetSignInStatus(e.errorText);
                    break;
                case UltraIMConnectionState::Disconnected: SetStatus("Signed out"); break;
            }
            UpdateHeader();         // whether a call can be placed follows the connection
            break;
        case UltraIMEventType::CallReceive:
        case UltraIMEventType::CallStateChange:
        case UltraIMEventType::CallMediaChange:
            OnCallEvent(e);
            break;
        case UltraIMEventType::NearbyChange:
            RefreshNearby();
            break;
        case UltraIMEventType::AuthRequire:
            ShowSignInPage();
            SetSignInStatus("Please sign in again.");
            break;
        default:
            break;
    }
}

} // namespace ChatCube
