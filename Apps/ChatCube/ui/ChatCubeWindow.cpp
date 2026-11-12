// Apps/ChatCube/ui/ChatCubeWindow.cpp
// The ChatCube window: sign-in page, conversations, messages
// Version: 0.3.0 - the chat list's menu (a right-click): read / unread, pinned, calls, the ID, a group's
//                  members, adding, renaming and leaving; a chat deleted; "1 member" in the singular
// Version: 0.2.9 - the relay from the settings (Networks > Relay) instead of a field on the sign-in
//                  page; a Connecting page while ChatCube Direct signs in, with Try again on a failure
// Version: 0.2.8 - a message's right-click menu: its file opened or saved, the message deleted for oneself or
//                  for everyone, as its messenger allows
// Version: 0.2.7 - recovery by the link e-mailed to the ID's address: the window waits for it and signs in
// Version: 0.2.6 - "Recover my ChatCube ID": the codes sent to the e-mail and phone bring an ID back
// Version: 0.2.5 - an e-mail address and a phone number for a new ID, a password and/or an
//                  UltraAuthenticator code to sign in; the Edit profile window
// Version: 0.2.4 - the Settings window (the gear beside the copy button): the send key, typing either
//                  way, the nearby list, 12-hour times and other date orders, signing out of a network
// Version: 0.2.3 - a picture or a file waits in a strip above the field, with a x to drop it, and
//                  goes with Enter or the send button; Ctrl+V pastes a picture or a file into it
// Version: 0.2.2 - pictures: avatars from the person's photo, photos and videos in the bubbles and a
//                  viewer for them, a paperclip that sends a picture, a video or a file
// Version: 0.2.1 - Telegram signs in by a QR code too, scanned from the phone's Telegram app
// Version: 0.2.0 - the look from ChatCubeTheme: a card on the sign-in page, avatars, icon buttons,
//                  a rounded composer, soft selection, a light and a dark theme (--dark)
// Version: 0.1.3 - the Telegram form says where the code went and asks for it another way
// Version: 0.1.2 - the status line shows every network's state, one entry each, beside the last action
// Version: 0.1.1 - the Telegram application's keys from the relay; TDLib's connection on the status line
// Version: 0.1.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeWindow.h"
#include "ChatCubeControls.h"
#include "ChatCubeProfileWindow.h"
#include "ChatCubeTheme.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDataFolder.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListSelection.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasVideoTile.h"
#include "UltraCanvasWindow.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "Plugins/QRCode/UltraCanvasQRCode.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <vector>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

constexpr int   kSidebarWidth = 320;
constexpr int   kInputHeight = 48;
constexpr int   kTileSide = 64;         // a staged picture's tile in the composer
constexpr int   kFileTileWidth = 180;   // a staged file's tile: its name and size
constexpr int   kTileClose = 20;        // the x on a tile
constexpr int64_t kTypingShownMs = 5000;
constexpr int64_t kTypingResendMs = 3000;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// A network's button in the sidebar (Matrix, Telegram): always the network's
// name, which is what fits the third of the sidebar it has - where the
// account stands ("Matrix: signed in as @alice:matrix.org") is its tooltip.
void SetNetworkButton(const std::shared_ptr<UltraCanvasButton>& button, const std::string& name,
                      const std::string& tooltip) {
    button->SetText(name);
    button->SetTooltip(tooltip);
}

// What a file is sent as, by its extension: a photo, a video, or a file
// (a GIF goes as a file: Telegram makes it an animation, where a photo
// would lose its movement).
UltraIMMessageType AttachmentKind(const std::string& path) {
    std::string ext = PathToUtf8(PathFromUtf8(path).extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const char* const images[] = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".heic", ".heif",
                                         ".tif", ".tiff", ".jxl", ".avif"};
    static const char* const videos[] = {".mp4", ".m4v", ".mov", ".mkv", ".webm", ".avi", ".mpg", ".mpeg", ".3gp"};
    for (const char* e : images) {
        if (ext == e) return UltraIMMessageType::Image;
    }
    for (const char* e : videos) {
        if (ext == e) return UltraIMMessageType::Video;
    }
    return UltraIMMessageType::File;
}

// Whether a file shows as a picture: a photo, or a drawing (SVG), which
// goes as a file - Telegram and Matrix take no SVG as a photo - but is
// shown as its picture, in the strip as in the bubble.
bool IsPictureFile(const std::string& path) {
    if (AttachmentKind(path) == UltraIMMessageType::Image) return true;
    std::string ext = PathToUtf8(PathFromUtf8(path).extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".svg" || ext == ".svgz" || ext == ".gif";
}

// Whether what the clipboard calls text is text: UTF-8 without a NUL. A
// program that serves one picture under every target (xclip) hands the
// picture's bytes out as the text too, and they are not to be typed.
bool LooksLikeText(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0) return false;
        size_t more = 0;
        if (c < 0x80) more = 0;
        else if ((c & 0xE0) == 0xC0) more = 1;
        else if ((c & 0xF0) == 0xE0) more = 2;
        else if ((c & 0xF8) == 0xF0) more = 3;
        else return false;
        if (i + more >= s.size() + (more ? 0 : 1)) return false;
        for (size_t k = 1; k <= more; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += more + 1;
    }
    return true;
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

// The relay as the Connecting page names it: its host, without the scheme
// and the path ("relay-chatcube.ultraos.eu").
std::string RelayHost(const std::string& url) {
    std::string host = Trim(url);
    const size_t scheme = host.find("://");
    if (scheme != std::string::npos) host.erase(0, scheme + 3);
    const size_t path = host.find('/');
    if (path != std::string::npos) host.erase(path);
    return host;
}

} // namespace

ChatCubeWindow::ChatCubeWindow(std::string version, std::string providerId, Settings settings)
    : version_(std::move(version)), providerId_(std::move(providerId)), settings_(settings),
      dataFolder_(PathToUtf8(UltraCanvasDataFolder("ChatCube"))),
      profileWindow_(std::make_unique<ChatCubeProfileWindow>(providerId_)) {
    SetDateTimeStyle(settings_.clock24, settings_.dateStyle);
    profileWindow_->onSaved = [this](const std::string& text) { SetStatus(text); };
}

ChatCubeWindow::~ChatCubeWindow() {
    // The tiles go with the window: no picture may be pushed into them after.
    if (!callId_.empty()) UltraIM_SetCallVideoSinks(providerId_, callId_, nullptr, nullptr);
    if (subscription_) UltraIM_Unsubscribe(subscription_);
    if (callTimer_)
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(callTimer_);
    StopRecoveryPolling();
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
    window_->SetBackgroundColor(L().ground);
    window_->AddChild(BuildSignInPage());
    window_->AddChild(BuildConnectingPage());
    window_->AddChild(BuildChatView());
    ApplySettings();
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
    UpdateSignInForm();
    // A saved identity signs in without asking, on the Connecting page; a
    // computer without one is asked to make one.
    if (hasIdentity_) SignIn();
    else ShowSignInPage();
    return true;
}

void ChatCubeWindow::Show() {
    if (window_) window_->Show();
}

void ChatCubeWindow::Minimize() {
    if (window_) window_->Minimize();
}

// ===================================================================== views

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildSignInPage() {
    signInPage_ = std::make_shared<UltraCanvasContainer>("cc-signin");
    signInPage_->layout.SetFlexColumn()
        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
        .SetFlexAlignItems(CSSLayout::AlignItems::Center)
        .SetFlexGap(10);
    signInPage_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    signInPage_->AddChild(MakeLogo("cc-signin-logo", 72));
    auto title = MakeLabel("cc-signin-title", "ChatCube", 26.0f);
    title->SetFontWeight(FontWeight::Bold);
    signInPage_->AddChild(title);
    signInPage_->AddChild(MakeLabel("cc-signin-tagline",
                                    "Chat directly with the people you know - end-to-end encrypted",
                                    11.0f, L().inkMuted));
    signInPage_->AddSpacer(10);

    // The form, on a card.
    auto card = std::make_shared<UltraCanvasContainer>("cc-signin-card");
    card->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    card->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    card->size.width = CSSLayout::Dimension::Px(kFormWidth + 48);
    card->SetBackgroundColor(L().panel);
    card->SetBorders(1.0f, L().line, 14.0f);
    card->SetPadding(22, 24, 18, 24);

    name_ = MakeInput("cc-signin-name", "Your name, as your contacts will see it", static_cast<int>(kFormWidth));
    name_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(name_);
    // To recover the account with: kept in the key vault and on the relay,
    // for the owner alone.
    email_ = MakeInput("cc-signin-email", "E-mail address, to recover your account", static_cast<int>(kFormWidth));
    email_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(email_);
    phone_ = MakeInput("cc-signin-phone", "Phone number with + and country code, for recovery",
                       static_cast<int>(kFormWidth));
    phone_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(phone_);
    // Asked by the provider at a start: the password, then the code.
    unlockPassword_ = MakeInput("cc-signin-password", "Your ChatCube password", static_cast<int>(kFormWidth));
    unlockPassword_->SetInputType(TextInputType::Password);
    unlockPassword_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(unlockPassword_);
    unlockCode_ = MakeInput("cc-signin-code", "The 6-digit code UltraAuthenticator shows", static_cast<int>(kFormWidth));
    unlockCode_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(unlockCode_);
    localNetwork_ = std::make_shared<UltraCanvasCheckbox>("cc-signin-lan", 0, 0, static_cast<long>(kFormWidth), 22,
                                                          "Also find people on this network");
    localNetwork_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    CheckboxVisualStyle lanStyle = localNetwork_->GetVisualStyle();
    lanStyle.base.fontSize = kTextFont;                 // the size of the page's other text
    lanStyle.base.textColor = L().ink;
    lanStyle.boxColor = L().panel;
    lanStyle.boxBorderColor = L().lineStrong;
    lanStyle.boxHoverColor = L().hover;
    lanStyle.checkmarkColor = L().accent;
    lanStyle.checkmarkHoverColor = L().accent;
    localNetwork_->SetVisualStyle(lanStyle);
    card->AddChild(localNetwork_);

    // How signing in on this computer is protected - a new ID's choice;
    // Edit profile changes it later.
    protectionRow_ = std::make_shared<UltraCanvasContainer>("cc-signin-protection-row");
    protectionRow_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    protectionRow_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    protectionRow_->AddChild(MakeLabel("cc-signin-protection-caption", "Signing in on this computer", 9.0f, L().inkMuted));
    protection_ = std::make_shared<UltraCanvasDropdown>("cc-signin-protection", 0, 0, kFormWidth, kControlHeight);
    protection_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    DropdownStyle dropStyle = protection_->GetStyle();
    dropStyle.fontSize = kTextFont;
    dropStyle.cornerRadius = kControlRadius;
    dropStyle.itemHeight = 28.0f;
    dropStyle.maxItemWidth = kFormWidth + 60;
    protection_->SetStyle(dropStyle);
    // In UltraIMSignInProtection's order.
    protection_->AddItem("No password - this computer signs in by itself");
    protection_->AddItem("A password");
    protection_->AddItem("A code from UltraAuthenticator");
    protection_->AddItem("A password and a code from UltraAuthenticator");
    protection_->SetSelectedIndex(0, false);
    protection_->SetAccessibleName("Signing in on this computer");
    protection_->onSelectionChanged = [this](int, const DropdownItem&) { UpdateSignInForm(); };
    protectionRow_->AddChild(protection_);
    card->AddChild(protectionRow_);
    newPasswordRow_ = MakeRow("cc-signin-new-password", 8);
    const int halfForm = static_cast<int>((kFormWidth - 8) / 2);
    newPassword_ = MakeInput("cc-signin-new", "Password", halfForm);
    newPassword_->SetInputType(TextInputType::Password);
    newPasswordRow_->AddChild(newPassword_);
    repeatPassword_ = MakeInput("cc-signin-repeat", "The same again", halfForm);
    repeatPassword_->SetInputType(TextInputType::Password);
    repeatPassword_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    newPasswordRow_->AddChild(repeatPassword_);
    card->AddChild(newPasswordRow_);

    // Recovering an ID made on another computer: the e-mail address it has;
    // the relay e-mails a link to it.
    recoverAddress_ = MakeInput("cc-recover-address", "Your ID's e-mail address", static_cast<int>(kFormWidth));
    recoverAddress_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(recoverAddress_);

    signIn_ = MakeButton("cc-signin-go", "Create my ChatCube ID", static_cast<int>(kFormWidth), true);
    signIn_->SetOnClick([this]() { SignIn(); });
    card->AddChild(signIn_);
    recoverToggle_ = MakeButton("cc-recover-toggle", "Recover my ChatCube ID", static_cast<int>(kFormWidth));
    StyleButton(recoverToggle_, false, true);
    recoverToggle_->SetOnClick([this]() { ToggleRecoveryForm(); });
    card->AddChild(recoverToggle_);

    signInStatus_ = MakeLabel("cc-signin-status", "", kTextFont, L().inkMuted);
    signInStatus_->SetWrap(TextWrap::WrapWord);
    signInStatus_->SetAlignment(TextAlignment::Center);
    signInStatus_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    card->AddChild(signInStatus_);
    signInPage_->AddChild(card);

    auto note = MakeLabel(
        "cc-signin-note",
        "Your ChatCube ID is a key made on this computer. Messages are end-to-end encrypted; "
        "the relay only connects you and keeps messages for people who are offline. "
        "Which relay is set in Settings > Networks > Relay.",
        9.0f, L().inkFaint);
    note->SetWrap(TextWrap::WrapWord);
    note->SetAlignment(TextAlignment::Center);
    note->size.width = CSSLayout::Dimension::Px(kFormWidth + 48);
    signInPage_->AddChild(note);
    return signInPage_;
}

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildConnectingPage() {
    connectingPage_ = std::make_shared<UltraCanvasContainer>("cc-connecting");
    connectingPage_->layout.SetFlexColumn()
        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
        .SetFlexAlignItems(CSSLayout::AlignItems::Center)
        .SetFlexGap(10);
    connectingPage_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    connectingPage_->AddChild(MakeLogo("cc-connecting-logo", 72));
    auto title = MakeLabel("cc-connecting-name", "ChatCube", 26.0f);
    title->SetFontWeight(FontWeight::Bold);
    connectingPage_->AddChild(title);
    connectingPage_->AddSpacer(6);
    connectingTitle_ = MakeLabel("cc-connecting-title", "Connecting\xE2\x80\xA6", 13.0f);
    connectingPage_->AddChild(connectingTitle_);
    connectingRelay_ = MakeLabel("cc-connecting-relay", "", 10.0f, L().inkMuted);
    connectingPage_->AddChild(connectingRelay_);
    connectingStatus_ = MakeLabel("cc-connecting-status", "", kTextFont, L().inkMuted);
    connectingStatus_->SetWrap(TextWrap::WrapWord);
    connectingStatus_->SetAlignment(TextAlignment::Center);
    connectingStatus_->size.width = CSSLayout::Dimension::Px(kFormWidth + 48);
    connectingPage_->AddChild(connectingStatus_);

    // On a failure: again, or the Settings window to change the relay.
    connectingButtons_ = MakeRow("cc-connecting-buttons", 8);
    connectingButtons_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    auto retry = MakeButton("cc-connecting-retry", "Try again", 150, true);
    retry->SetOnClick([this]() { SignIn(); });
    connectingButtons_->AddChild(retry);
    auto settings = MakeButton("cc-connecting-settings", "Settings", 150);
    settings->SetOnClick([this]() { OpenSettings(); });
    connectingButtons_->AddChild(settings);
    connectingButtons_->SetVisible(false);
    connectingPage_->AddChild(connectingButtons_);

    connectingPage_->SetVisible(false);
    return connectingPage_;
}

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildSidebar() {
    auto sidebar = std::make_shared<UltraCanvasContainer>("cc-sidebar");
    sidebar->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    sidebar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    sidebar->size.width = CSSLayout::Dimension::Px(kSidebarWidth);
    sidebar->SetPadding(10, 8, 8, 8);
    sidebar->SetBorderRight(1, L().line);
    sidebar->SetBackgroundColor(L().sidebar);

    // Who I am: the logo, my ID in groups of four, and a copy button.
    auto idRow = MakeRow("cc-myid-row", 8);
    idRow->AddChild(MakeLogo("cc-myid-logo", 30));
    auto idColumn = std::make_shared<UltraCanvasContainer>("cc-myid-column");
    idColumn->layout.SetFlexColumn().SetFlexGap(1).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    idColumn->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    idColumn->AddChild(MakeLabel("cc-myid-caption", "My ChatCube ID", 8.5f, L().inkFaint));
    myId_ = MakeLabel("cc-myid", "", 9.5f, L().ink);
    myId_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    idColumn->AddChild(myId_);
    idRow->AddChild(idColumn);
    auto copy = MakeIconButton("cc-myid-copy", "copy");
    copy->SetTooltip("Copy my ChatCube ID");
    copy->SetOnClick([this]() { CopyMyId(); });
    idRow->AddChild(copy);
    // The gear opens the Settings window, as UltraFiler's does.
    auto gear = MakeIconButton("cc-settings", "settings");
    gear->SetTooltip("Settings");
    gear->SetOnClick([this]() { OpenSettings(); });
    idRow->AddChild(gear);
    auto profile = MakeIconButton("cc-myid-profile", "profile");
    profile->SetTooltip("Edit profile: name, e-mail, phone and how you sign in");
    profile->SetOnClick([this]() { EditProfile(); });
    idRow->AddChild(profile);
    sidebar->AddChild(idRow);

    // Adding someone: a pill, and a round plus.
    auto addRow = MakeRow("cc-add-row", 6);
    contactId_ = MakeInput("cc-add-id", "Add: a ChatCube ID, @name:server or @telegram",
                           kSidebarWidth - 16 - kIconButton - 6, true);
    contactId_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    contactId_->onEnterPressed = [this](const std::string&) { AddContact(); return true; };
    addRow->AddChild(contactId_);
    auto add = MakeIconButton("cc-add", "plus", "", kIconButton, true);
    add->SetTooltip("Add the contact");
    add->SetOnClick([this]() { AddContact(); });
    addRow->AddChild(add);
    sidebar->AddChild(addRow);

    // The three doors: a new group, a Matrix account, a Telegram account.
    auto doors = MakeRow("cc-doors", 6);
    const int doorWidth = (kSidebarWidth - 16 - 12) / 3;
    auto newGroup = MakeIconButton("cc-new-group", "group", "Group", doorWidth, false, false);
    newGroup->SetTooltip("New group");
    newGroup->SetOnClick([this]() { ToggleGroupForm(); });
    newGroup->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    doors->AddChild(newGroup);
    matrixToggle_ = MakeIconButton("cc-matrix", "matrix", "Matrix", doorWidth, false, false);
    matrixToggle_->SetTooltip("Matrix account");
    matrixToggle_->SetOnClick([this]() { ToggleMatrixForm(); });
    matrixToggle_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    doors->AddChild(matrixToggle_);
    telegramToggle_ = MakeIconButton("cc-telegram", "telegram", "Telegram", doorWidth, false, false);
    telegramToggle_->SetTooltip("Telegram account");
    telegramToggle_->SetOnClick([this]() { ToggleTelegramForm(); });
    telegramToggle_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    doors->AddChild(telegramToggle_);
    sidebar->AddChild(doors);

    // A Matrix account next to ChatCube Direct.
    matrixForm_ = std::make_shared<UltraCanvasContainer>("cc-matrix-form");
    matrixForm_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    matrixForm_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    matrixServer_ = MakeInput("cc-matrix-server", "Homeserver, e.g. matrix.org", kSidebarWidth - 16);
    matrixForm_->AddChild(matrixServer_);
    matrixUser_ = MakeInput("cc-matrix-user", "User name, or @name:server", kSidebarWidth - 16);
    matrixForm_->AddChild(matrixUser_);
    matrixPassword_ = MakeInput("cc-matrix-password", "Password", kSidebarWidth - 16);
    matrixPassword_->SetInputType(TextInputType::Password);
    matrixPassword_->onEnterPressed = [this](const std::string&) { SignInToMatrix(false); return true; };
    matrixForm_->AddChild(matrixPassword_);
    matrixSignIn_ = MakeButton("cc-matrix-go", "Sign in to Matrix", kSidebarWidth - 16, true);
    matrixSignIn_->SetOnClick([this]() { SignInToMatrix(false); });
    matrixForm_->AddChild(matrixSignIn_);
    matrixForm_->SetVisible(false);
    sidebar->AddChild(matrixForm_);
    // Signed in: the keys to the encrypted rooms, to keep or to move.
    matrixKeysForm_ = MakeRow("cc-matrix-keys", 6);
    auto exportKeys = MakeButton("cc-matrix-export", "Export keys", (kSidebarWidth - 22) / 2);
    exportKeys->SetOnClick([this]() { ExportMatrixKeys(); });
    matrixKeysForm_->AddChild(exportKeys);
    auto importKeys = MakeButton("cc-matrix-import", "Import keys", (kSidebarWidth - 22) / 2);
    importKeys->SetOnClick([this]() { ImportMatrixKeys(); });
    matrixKeysForm_->AddChild(importKeys);
    matrixKeysForm_->SetVisible(false);
    sidebar->AddChild(matrixKeysForm_);

    // A Telegram account: the phone number, then the code, then the
    // two-step password if there is one - one field at a time; or a QR code
    // the phone's Telegram app scans, in place of the number and the code.
    telegramForm_ = std::make_shared<UltraCanvasContainer>("cc-telegram-form");
    telegramForm_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    telegramForm_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto next = [this](const std::string&) { SignInToTelegram(false); return true; };
    telegramPhone_ = MakeInput("cc-telegram-phone", "Phone number, e.g. +44 20 7946 0000", kSidebarWidth - 16);
    telegramPhone_->onEnterPressed = next;
    telegramForm_->AddChild(telegramPhone_);
    telegramApiId_ = MakeInput("cc-telegram-api-id", "API ID (my.telegram.org; empty if built in)", kSidebarWidth - 16);
    telegramForm_->AddChild(telegramApiId_);
    telegramApiHash_ = MakeInput("cc-telegram-api-hash", "API hash", kSidebarWidth - 16);
    telegramForm_->AddChild(telegramApiHash_);
    // Where the code went, in the form itself: the status line moves on,
    // and a code that went to another Telegram app is easy to miss.
    telegramHint_ = MakeLabel("cc-telegram-hint", "", 9.0f, L().inkMuted);
    telegramHint_->SetWrap(TextWrap::WrapWord);
    telegramHint_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    telegramForm_->AddChild(telegramHint_);
    // The link Telegram gives, as a QR code for the phone's Telegram app to
    // scan - black on white whatever the theme, which every scanner reads.
    const int qrSide = kSidebarWidth - 16;
    telegramQr_ = std::make_shared<UltraCanvasQRCode>("cc-telegram-qr", 0, 0, qrSide, qrSide);
    telegramQr_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Center);
    telegramQr_->size.width = CSSLayout::Dimension::Px(static_cast<float>(qrSide));
    telegramQr_->size.height = CSSLayout::Dimension::Px(static_cast<float>(qrSide));
    telegramQr_->SetForegroundColor(Colors::Black);
    telegramQr_->SetBackgroundColor(Colors::White);
    telegramQr_->SetQuietZoneModules(2);
    telegramQr_->SetVisible(false);
    telegramForm_->AddChild(telegramQr_);
    telegramCode_ = MakeInput("cc-telegram-code", "The code Telegram sent", kSidebarWidth - 16);
    telegramCode_->onEnterPressed = next;
    telegramForm_->AddChild(telegramCode_);
    // Telegram's other way of sending the code (SMS, a call), when it offers one.
    telegramResend_ = MakeButton("cc-telegram-resend", "Send it another way", kSidebarWidth - 16);
    telegramResend_->SetOnClick([this]() {
        telegramResend_->SetDisabled(true);
        SetProviderStatus(telegramProviderId_, "asking Telegram for the code another way\xE2\x80\xA6");
        UltraIM_ResendAuthCode(telegramProviderId_, [this](const UltraIMResult& r) {
            telegramResend_->SetDisabled(false);
            if (!r) SetProviderStatus(telegramProviderId_, r.message);
            UpdateTelegramForm();
        });
    });
    telegramForm_->AddChild(telegramResend_);
    telegramPassword_ = MakeInput("cc-telegram-password", "Two-step verification password", kSidebarWidth - 16);
    telegramPassword_->SetInputType(TextInputType::Password);
    telegramPassword_->onEnterPressed = next;
    telegramForm_->AddChild(telegramPassword_);
    telegramName_ = MakeInput("cc-telegram-name", "Your name (a new account)", kSidebarWidth - 16);
    telegramName_->onEnterPressed = next;
    telegramForm_->AddChild(telegramName_);
    telegramNext_ = MakeButton("cc-telegram-go", "Send me the code", kSidebarWidth - 16, true);
    telegramNext_->SetOnClick([this]() { SignInToTelegram(false); });
    telegramForm_->AddChild(telegramNext_);
    // The QR code instead - of the number, or of a code that never came.
    telegramQrButton_ = MakeButton("cc-telegram-qr-go", "Sign in with a QR code instead", kSidebarWidth - 16);
    telegramQrButton_->SetOnClick([this]() { SignInToTelegramByQrCode(); });
    telegramForm_->AddChild(telegramQrButton_);
    telegramForm_->SetVisible(false);
    sidebar->AddChild(telegramForm_);

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
    conversationList_->SetRowHeight(64);
    ListViewStyle listStyle = conversationList_->GetStyle();
    listStyle.backgroundColor = L().sidebar;
    // The rows paint their own rounded selection and hover.
    listStyle.selectionBackgroundColor = Colors::Transparent;
    listStyle.hoverBackgroundColor = Colors::Transparent;
    conversationList_->SetStyle(listStyle);
    conversationList_->SetBackgroundColor(L().sidebar);
    conversationList_->SetBorders(0, Colors::Transparent);
    conversationList_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    conversationList_->onSelectionChanged = [this](const std::vector<int>& rows) {
        if (fillingList_ || rows.empty()) return;
        const auto& list = conversations_->Conversations();
        if (rows.front() >= 0 && rows.front() < static_cast<int>(list.size()))
            OpenConversation(list[static_cast<size_t>(rows.front())].conversationId);
    };
    // A right-click opens the conversation's menu, not the conversation:
    // selecting a row here opens it, so the press leaves the selection alone.
    conversationList_->SetSelectsOnContextMenu(false);
    conversationList_->onContextMenu = [this](int row, const UCEvent& event) {
        ShowConversationMenu(row, event.pointerWindow.x, event.pointerWindow.y);
    };
    marks_.Load();
    ApplyMarks();
    sidebar->AddChild(conversationList_);
    return sidebar;
}

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildConversationPane() {
    auto pane = std::make_shared<UltraCanvasContainer>("cc-pane");
    pane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    pane->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    pane->SetBackgroundColor(L().ground);

    // ----- header: the avatar, the name and its state, the call buttons -----
    auto header = MakeRow("cc-header", 10);
    header->SetPadding(9, 14, 9, 14);
    header->SetBackgroundColor(L().panel);
    header->SetBorderBottom(1, L().line);
    headerAvatar_ = MakeLabel("cc-header-avatar", "", 12.0f, Color(255, 255, 255, 255));
    headerAvatar_->SetFontWeight(FontWeight::Bold);
    headerAvatar_->SetAlignment(TextAlignment::Center);
    headerAvatar_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    headerAvatar_->size.width = CSSLayout::Dimension::Px(36);
    headerAvatar_->size.height = CSSLayout::Dimension::Px(36);
    headerAvatar_->SetBorderRadius(18.0f);
    headerAvatar_->SetVisible(false);
    header->AddChild(headerAvatar_);
    // The person's picture in its place, when the provider fetched one.
    headerPhoto_ = std::make_shared<UltraCanvasImageElement>("cc-header-photo", 0, 0, 36, 36);
    headerPhoto_->SetFitMode(ImageFitMode::Cover);
    headerPhoto_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    headerPhoto_->size.width = CSSLayout::Dimension::Px(36);
    headerPhoto_->size.height = CSSLayout::Dimension::Px(36);
    headerPhoto_->SetBorderRadius(18.0f);
    headerPhoto_->SetVisible(false);
    header->AddChild(headerPhoto_);
    auto titles = std::make_shared<UltraCanvasContainer>("cc-header-titles");
    titles->layout.SetFlexColumn().SetFlexGap(1);
    titles->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    title_ = MakeLabel("cc-title", "", 12.5f);
    title_->SetFontWeight(FontWeight::Bold);
    titles->AddChild(title_);
    subtitle_ = MakeLabel("cc-subtitle", "", 9.0f, L().inkMuted);
    titles->AddChild(subtitle_);
    header->AddChild(titles);
    voiceCall_ = MakeIconButton("cc-call", "call", "", kIconButton + 2);
    voiceCall_->SetTooltip("Call");
    voiceCall_->SetDisabled(true);
    voiceCall_->SetOnClick([this]() { StartCall(false); });
    header->AddChild(voiceCall_);
    videoCall_ = MakeIconButton("cc-video", "video", "", kIconButton + 2);
    videoCall_->SetTooltip("Video call");
    videoCall_->SetDisabled(true);
    videoCall_->SetOnClick([this]() { StartCall(true); });
    header->AddChild(videoCall_);
    pane->AddChild(header);

    // ----- messages -----
    auto empty = std::make_shared<UltraCanvasContainer>("cc-empty-box");
    empty->layout.SetFlexColumn().SetFlexGap(10)
        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    empty->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto emptyLogo = MakeLogo("cc-empty-logo", 64);
    emptyLogo->SetOpacity(0.35f);
    empty->AddChild(emptyLogo);
    empty->AddChild(MakeLabel("cc-empty-title", "Your conversations", 13.0f, L().inkMuted));
    auto hint = MakeLabel("cc-empty",
                          "Pick a conversation on the left, or add a contact by the ChatCube ID they give you.",
                          10.0f, L().inkFaint);
    hint->SetAlignment(TextAlignment::Center);
    hint->SetWrap(TextWrap::WrapWord);
    hint->size.width = CSSLayout::Dimension::Px(380);
    empty->AddChild(hint);
    emptyHint_ = empty;
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
    messageStyle.backgroundColor = L().ground;
    messageStyle.selectionBackgroundColor = Colors::Transparent;   // bubbles show their own state
    messageStyle.hoverBackgroundColor = Colors::Transparent;
    messageList_->SetStyle(messageStyle);
    messageList_->SetBackgroundColor(L().ground);
    messageList_->SetBorders(0, Colors::Transparent);
    messageList_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    messageList_->SetVisible(false);
    // A click on a picture, or on a file's bubble, opens it.
    messageList_->onCellClicked = [this](int row, int, const Point2Di& posInCell) {
        Rect2Di media;
        if (bubbles_->MediaRect(messages_.get(), row, media) && media.Contains(posInCell)) OpenAttachment(row);
    };
    messageList_->onContextMenu = [this](int row, const UCEvent& event) {
        ShowMessageMenu(row, event.pointerWindow.x, event.pointerWindow.y);
    };
    pane->AddChild(messageList_);

    // ----- the composer: a rounded field with the send button inside -----
    auto inputRow = std::make_shared<UltraCanvasContainer>("cc-input-row");
    inputRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::End);
    inputRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    inputRow->SetPadding(10, 14, 10, 14);
    inputRow->SetBackgroundColor(L().panel);
    inputRow->SetBorderTop(1, L().line);
    // The rounded field: the strip of what goes with the next message
    // (hidden while empty) over the row with the paperclip, the text and
    // the send button.
    auto composer = std::make_shared<UltraCanvasContainer>("cc-composer");
    composer->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    composer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    composer->SetBackgroundColor(L().well);
    composer->SetBorders(1.0f, L().line, 22.0f);
    composer->SetPadding(5, 6, 5, 6);
    attachmentStrip_ = std::make_shared<UltraCanvasContainer>("cc-attachments");
    attachmentStrip_->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(6)
        .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    attachmentStrip_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    attachmentStrip_->SetPadding(4, 8, 0, 8);
    attachmentStrip_->SetVisible(false);
    composer->AddChild(attachmentStrip_);
    auto composerRow = std::make_shared<UltraCanvasContainer>("cc-composer-row");
    composerRow->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::End);
    composerRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    attach_ = MakeIconButton("cc-attach", "attach", "", kIconButton);
    attach_->SetTooltip("Send a picture, a video or a file");
    attach_->SetDisabled(true);
    attach_->SetOnClick([this]() { AttachFile(); });
    composerRow->AddChild(attach_);
    input_ = std::make_shared<UltraCanvasTextArea>("cc-input", 0, 0, 300, kInputHeight);
    input_->SetEditingMode(TextAreaEditingMode::PlainText);
    input_->SetWordWrap(true);
    input_->SetShowLineNumbers(false);
    input_->SetFontSize(11.0f);
    input_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    {
        TextAreaStyle st = input_->GetStyle();
        st.backgroundColor = L().well;
        st.borderColor = Colors::Transparent;
        st.fontColor = L().ink;
        st.placeholderColor = L().inkFaint;
        st.cursorColor = L().ink;
        st.selectionColor = Color(L().accent.r, L().accent.g, L().accent.b, 90);
        st.currentLineHighlightColor = Colors::Transparent;
        st.scrollbarCornerRadius = 4.0f;
        input_->SetStyle(st);
    }
    input_->SetBackgroundColor(L().well);
    input_->SetBorders(0, Colors::Transparent);
    // Enter sends; Shift+Enter is left to the text area, which breaks the
    // line - or, as the Settings window may say, Ctrl+Enter sends and Enter
    // is the text area's. Ctrl+V (Shift+Insert) with a picture or a file on
    // the clipboard puts it in the strip; with text, the area pastes it as usual.
    input_->onBeforeKeyDown = [this](const UCEvent& e) {
        const bool enter = e.virtualKey == UCKeys::Return || e.virtualKey == UCKeys::NumPadEnter;
        const bool sends = settings_.ctrlEnterSends ? (e.ctrl && !e.shift && !e.alt) : !e.shift;
        if (enter && sends) {
            SendCurrentMessage();
            return true;
        }
        const bool paste = (e.virtualKey == UCKeys::V && e.ctrl && !e.shift && !e.alt) ||
                           (e.virtualKey == UCKeys::Insert && e.shift && !e.ctrl && !e.alt);
        return paste && PasteAttachment();
    };
    input_->SetOnTextChanged([this](const std::string& text) {
        if (activeConversation_.empty() || text.empty() || !settings_.sendTyping) return;
        const int64_t now = NowMs();
        if (now - lastTypingSentMs_ < kTypingResendMs) return;
        lastTypingSentMs_ = now;
        UltraIM_SetTyping(activeProvider_, activeConversation_, true);
    });
    composerRow->AddChild(input_);
    send_ = MakeIconButton("cc-send", "send", "", kIconButton, true);
    send_->SetOnClick([this]() { SendCurrentMessage(); });
    composerRow->AddChild(send_);
    composer->AddChild(composerRow);
    inputRow->AddChild(composer);
    pane->AddChild(inputRow);
    return pane;
}

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildChatView() {
    chatView_ = std::make_shared<UltraCanvasContainer>("cc-chat");
    chatView_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    chatView_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // ----- the call bar (hidden without a call) -----
    callBar_ = MakeRow("cc-call-bar");
    callBar_->SetPadding(8, 14, 8, 14);
    callBar_->SetBackgroundColor(L().callBar);
    callBar_->SetBorderBottom(1, L().line);
    callText_ = MakeLabel("cc-call-text", "", 11.0f);
    callText_->SetFontWeight(FontWeight::Bold);
    callText_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    callBar_->AddChild(callText_);
    callAccept_ = MakeIconButton("cc-call-accept", "accept", "Accept", 96, true);
    callAccept_->SetOnClick([this]() { AcceptCall(); });
    callBar_->AddChild(callAccept_);
    callCamera_ = MakeIconButton("cc-call-camera", "camera-off", "Camera off", 116, false, false);
    callCamera_->SetOnClick([this]() { ToggleCamera(); });
    callBar_->AddChild(callCamera_);
    callMute_ = MakeIconButton("cc-call-mute", "mic-off", "Mute", 86, false, false);
    callMute_->SetOnClick([this]() { ToggleMute(); });
    callBar_->AddChild(callMute_);
    callEnd_ = MakeIconButton("cc-call-end", "hangup", "Hang up", 104, true);
    {
        ButtonStyle st = callEnd_->GetStyle();
        st.normalColor = L().danger;
        st.hoverColor = Darker(L().danger, 24);
        st.pressedColor = Darker(L().danger, 40);
        st.normalTextColor = st.hoverTextColor = st.pressedTextColor = Color(255, 255, 255, 255);
        callEnd_->SetStyle(st);
    }
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

    status_ = MakeLabel("cc-status", "", 9.0f, L().inkMuted);
    status_->SetMargin(3, 14, 5, 14);
    chatView_->AddChild(status_);
    return chatView_;
}

void ChatCubeWindow::ShowSignInPage() {
    if (chatView_) chatView_->SetVisible(false);
    if (connectingPage_) connectingPage_->SetVisible(false);
    if (signInPage_) signInPage_->SetVisible(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ShowConnectingPage(bool failed) {
    const std::string host = RelayHost(settings_.relayServer);
    connectingTitle_->SetText(failed ? "Could not connect" : "Connecting\xE2\x80\xA6");
    connectingRelay_->SetText(host.empty() ? "the local network" : host);
    connectingButtons_->SetVisible(failed);
    if (chatView_) chatView_->SetVisible(false);
    if (signInPage_) signInPage_->SetVisible(false);
    connectingPage_->SetVisible(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ShowChatView() {
    if (signInPage_) signInPage_->SetVisible(false);
    if (connectingPage_) connectingPage_->SetVisible(false);
    if (chatView_) chatView_->SetVisible(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::SetStatus(const std::string& text) {
    actionStatus_ = text;
    RenderStatus();
}

void ChatCubeWindow::SetProviderStatus(const std::string& providerId, const std::string& text) {
    if (text.empty()) providerStatus_.erase(providerId);
    else providerStatus_[providerId] = text;
    RenderStatus();
}

void ChatCubeWindow::RenderStatus() {
    if (!status_) return;
    std::string line = actionStatus_;
    // The networks in a fixed order, each under the name the sidebar uses.
    const std::pair<std::string, const char*> networks[] = {
        {providerId_, "ChatCube"}, {matrixProviderId_, "Matrix"}, {telegramProviderId_, "Telegram"}};
    for (const auto& network : networks) {
        auto it = providerStatus_.find(network.first);
        if (it == providerStatus_.end()) continue;
        if (!line.empty()) line += " \xC2\xB7 ";       // a middle dot
        line += std::string(network.second) + ": " + it->second;
    }
    status_->SetText(line);
}

void ChatCubeWindow::SetSignInStatus(const std::string& text) {
    if (signInStatus_) signInStatus_->SetText(text);
    if (connectingStatus_) connectingStatus_->SetText(text);
}

// =================================================================== actions

void ChatCubeWindow::UpdateSignInForm() {
    const bool recovering = !hasIdentity_ && recovering_;
    const bool creating = !hasIdentity_ && !recovering_;
    const int chosen = protection_->GetSelectedIndex();
    const auto protection = static_cast<UltraIMSignInProtection>(std::clamp(chosen, 0, 3));
    name_->SetVisible(creating);
    email_->SetVisible(creating);
    phone_->SetVisible(creating);
    protectionRow_->SetVisible(creating);
    newPasswordRow_->SetVisible(creating && UltraIM_ProtectionHasPassword(protection));
    localNetwork_->SetVisible(!recovering);
    unlockPassword_->SetVisible(!hasIdentity_ ? false : unlockStep_ == UltraIMAuthStep::Password);
    unlockCode_->SetVisible(!hasIdentity_ ? false : unlockStep_ == UltraIMAuthStep::Code);
    recoverAddress_->SetVisible(recovering && recoveryId_.empty());
    recoverToggle_->SetVisible(!hasIdentity_);
    recoverToggle_->SetText(!recovering ? "Recover my ChatCube ID"
                            : recoveryId_.empty() ? "Make a new ChatCube ID instead"
                                                  : "Start over - send a new link");
    if (recovering) signIn_->SetText(recoveryId_.empty() ? "E-mail me a link" : "Check now");
    else if (creating) signIn_->SetText("Create my ChatCube ID");
    else if (unlockStep_ == UltraIMAuthStep::Password || unlockStep_ == UltraIMAuthStep::Code) signIn_->SetText("Unlock");
    else signIn_->SetText("Sign in");
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ToggleRecoveryForm() {
    // While a link is on its way, back to the address (a new link); from
    // there, or from making an ID, between the two.
    StopRecoveryPolling();
    if (recovering_ && !recoveryId_.empty()) {
        recoveryId_.clear();
        SetSignInStatus("Ask for a new link - the one before stops working when it expires.");
        UpdateSignInForm();
        return;
    }
    recovering_ = !recovering_;
    recoveryId_.clear();
    SetSignInStatus(recovering_ ? "Your ID comes back from your relay, if recovery was turned on in Edit profile: "
                                  "it e-mails a link to your ID's address."
                                : std::string());
    UpdateSignInForm();
}

void ChatCubeWindow::StopRecoveryPolling() {
    if (recoveryTimer_)
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(recoveryTimer_);
    recoveryTimer_ = 0;
}

void ChatCubeWindow::Recover() {
    const std::string server = Trim(settings_.relayServer);
    if (!recoveryId_.empty()) return PollRecovery();        // "Check now"
    // The link, e-mailed to the address - if an ID with recovery on has it.
    const std::string address = Trim(recoverAddress_->GetText());
    if (server.empty()) return SetSignInStatus("Set the relay your ID used in Settings > Networks > Relay.");
    if (address.find('@') == std::string::npos) return SetSignInStatus("Type your ID's e-mail address.");
    signIn_->SetDisabled(true);
    SetSignInStatus("Asking the relay\xE2\x80\xA6");
    UltraIM_StartRecovery(providerId_, server, address,
                          [this](const UltraIMResult& r, const UltraIMRecoveryChallenge& challenge) {
        signIn_->SetDisabled(false);
        if (!r) return SetSignInStatus(r.message);
        recoveryId_ = challenge.recoveryId;
        SetSignInStatus("If that address belongs to an ID with recovery on, a link is on its way to it. Open it "
                        "and confirm - ChatCube signs in here by itself. (Not there? Look in the spam folder.)");
        UpdateSignInForm();
        // Asked every few seconds until the link is approved or expires.
        if (auto* app = UltraCanvasApplicationBase::GetCurrent())
            recoveryTimer_ = app->StartTimer(3000, true, [this](TimerId) { PollRecovery(); });
    });
}

void ChatCubeWindow::PollRecovery() {
    if (recoveryId_.empty() || recoveryAsking_) return;
    recoveryAsking_ = true;
    UltraIMRecoveryAnswer answer;
    answer.server = Trim(settings_.relayServer);
    answer.recoveryId = recoveryId_;
    UltraIM_FinishRecovery(providerId_, answer, [this](const UltraIMResult& r) {
        recoveryAsking_ = false;
        if (r.code == UltraIMResultCode::NeedsInput) return;        // not opened yet
        if (!r) {
            if (r.code == UltraIMResultCode::AuthFailed) {
                // Expired: back to the address, for a new link.
                StopRecoveryPolling();
                recoveryId_.clear();
                UpdateSignInForm();
            }
            return SetSignInStatus(r.message);      // the relay away: asked again in a moment
        }
        StopRecoveryPolling();
        hasIdentity_ = true;
        recovering_ = false;
        recoveryId_.clear();
        UpdateSignInForm();
        SetSignInStatus("Your ChatCube ID is back on this computer.");
        SignIn();                       // with what FinishRecovery stored
    });
}

void ChatCubeWindow::SignIn() {
    if (!hasIdentity_ && recovering_) return Recover();
    UltraIMCredentials credentials;
    credentials.loginType = UltraIMLoginType::KeyPair;
    // The relay is a setting (Networks > Relay), ULTRA OS's own unless
    // changed; empty, the provider uses the one it remembers, if any.
    credentials.server = Trim(settings_.relayServer);
    credentials.displayName = Trim(name_->GetText());
    credentials.createAccount = !hasIdentity_;
    const auto chosen = static_cast<UltraIMSignInProtection>(std::clamp(protection_->GetSelectedIndex(), 0, 3));
    std::string password;
    if (!hasIdentity_) {
        credentials.email = Trim(email_->GetText());
        credentials.phoneNumber = Trim(phone_->GetText());
        if (credentials.displayName.empty()) {
            SetSignInStatus("Enter the name your contacts will see.");
            return;
        }
        if (credentials.email.empty() || credentials.phoneNumber.empty()) {
            SetSignInStatus("Enter your e-mail address and phone number: they are how you get your account back.");
            return;
        }
        if (UltraIM_ProtectionHasPassword(chosen)) {
            password = newPassword_->GetText();
            if (password.size() < 8) {
                SetSignInStatus("Choose a password of 8 characters or more.");
                return;
            }
            if (password != repeatPassword_->GetText()) {
                SetSignInStatus("The two passwords are not the same.");
                return;
            }
        }
    } else if (unlockStep_ == UltraIMAuthStep::Password) {
        credentials.password = unlockPassword_->GetText();
        if (credentials.password.empty()) {
            SetSignInStatus("Type your ChatCube password.");
            return;
        }
    } else if (unlockStep_ == UltraIMAuthStep::Code) {
        credentials.otpCode = Trim(unlockCode_->GetText());
        if (credentials.otpCode.empty()) {
            SetSignInStatus("Type the code UltraAuthenticator shows for ChatCube.");
            return;
        }
    }
    // No relay: the local network only (the provider says so when there is
    // no relay remembered either).
    credentials.localNetwork = localNetwork_->IsChecked();
    signIn_->SetDisabled(true);
    // Signing in has a page of its own: the form is not shown while it runs.
    SetSignInStatus(unlockStep_ == UltraIMAuthStep::Password ? "Opening the key vault\xE2\x80\xA6"
                    : !hasIdentity_ ? "Making your ChatCube ID\xE2\x80\xA6"
                               : std::string());
    ShowConnectingPage();
    const bool created = !hasIdentity_;
    UltraIM_SignIn(providerId_, credentials, [this, created, chosen, password](const UltraIMResult& r) {
        signIn_->SetDisabled(false);
        if (!r) {
            // A protected ID: the password, then the code, on this page.
            UltraIMProviderInfo asked;
            const bool unlock = hasIdentity_ && UltraIM_GetProviderInfo(providerId_, asked) &&
                                (asked.authStep == UltraIMAuthStep::Password ||
                                 asked.authStep == UltraIMAuthStep::Code);
            if (unlock) {
                // Typed again from the start: a wrong one, or the next step.
                unlockPassword_->SetText(std::string());
                unlockCode_->SetText(std::string());
                unlockStep_ = asked.authStep;
                UpdateSignInForm();
                (unlockStep_ == UltraIMAuthStep::Password ? unlockPassword_ : unlockCode_)->SetFocus(true);
            }
            SetSignInStatus(r.message);
            // A new ID, or a password or code to type: the form again. An
            // ID that could not connect: the Connecting page says why, with
            // Try again and the Settings to change the relay.
            if (unlock || !hasIdentity_) ShowSignInPage();
            else ShowConnectingPage(true);
            return;
        }
        unlockStep_ = UltraIMAuthStep::Idle;
        unlockPassword_->SetText(std::string());
        unlockCode_->SetText(std::string());
        newPassword_->SetText(std::string());
        repeatPassword_->SetText(std::string());
        SetSignInStatus(std::string());
        hasIdentity_ = true;
        UpdateSignInForm();
        UltraIMProviderInfo info;
        if (UltraIM_GetProviderInfo(providerId_, info))
            myId_->SetText(GroupUserId(info.localUserId));
        ShowChatView();
        SetProviderStatus(providerId_, "connected");
        if (created) FinishCreatedProtection(chosen, password);
        RefreshConversations();
        RefreshNearby();
        UltraIMProviderInfo matrixInfo;
        if (UltraIM_GetProviderInfo(matrixProviderId_, matrixInfo) && matrixInfo.hasStoredSession &&
            matrixInfo.connectionState == UltraIMConnectionState::Disconnected)
            SignInToMatrix(true);
        // The relay's Telegram keys first: a stored Telegram session resumes
        // with them, so a change of keys on the relay reaches it.
        FetchTelegramCredentials([this]() {
            UltraIMProviderInfo telegramInfo;
            if (UltraIM_GetProviderInfo(telegramProviderId_, telegramInfo) && telegramInfo.hasStoredSession &&
                telegramInfo.connectionState == UltraIMConnectionState::Disconnected)
                SignInToTelegram(true);
        });
    });
}

void ChatCubeWindow::FinishCreatedProtection(UltraIMSignInProtection chosen, const std::string& password) {
    if (chosen == UltraIMSignInProtection::DeviceKey) return;
    // The authenticator to add is shown in Edit profile, the password (when
    // chosen with it) already set and filled in there.
    auto authenticator = [this, chosen, password]() {
        if (!UltraIM_ProtectionHasCode(chosen)) return;
        SetStatus("Add ChatCube to UltraAuthenticator to finish: scan the code in Edit profile.");
        profileWindow_->Show(window_.get(), chosen, password);
    };
    if (!UltraIM_ProtectionHasPassword(chosen)) {
        authenticator();
        return;
    }
    UltraIMProtectionChange change;
    change.protection = UltraIMSignInProtection::Password;
    change.newPassword = password;
    SetStatus("Encrypting your ID with your password\xE2\x80\xA6");
    UltraIM_SetSignInProtection(providerId_, change, [this, authenticator](const UltraIMResult& r) {
        if (!r) {
            SetStatus("The password could not be set: " + r.message + " - set it in Edit profile.");
            return;
        }
        SetStatus("Your ID is protected by your password: ChatCube asks for it at every start.");
        authenticator();
    });
}

void ChatCubeWindow::EditProfile() {
    profileWindow_->Show(window_.get());
}

void ChatCubeWindow::FetchTelegramCredentials(std::function<void()> then) {
    UltraIMProviderInfo telegramInfo;
    if (!UltraIM_GetProviderInfo(telegramProviderId_, telegramInfo)) {
        if (then) then();
        return;
    }
    UltraIM_FetchSharedCredentials(providerId_, telegramProviderId_,
                                   [this, then](const UltraIMResult& r, const UltraIMCredentials& c) {
        if (r) {
            telegramRelayApiId_ = c.apiId;
            telegramRelayApiHash_ = c.apiHash;
        } else {
            // No relay, or one without the keys: the user types them.
            telegramRelayApiId_.clear();
            telegramRelayApiHash_.clear();
        }
        if (telegramForm_->IsVisible()) UpdateTelegramForm();
        if (then) then();
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

// ======================================================= the chat list's menu

bool ChatCubeWindow::ProviderCan(const std::string& providerId, UltraIMCapabilityFlags capability) const {
    UltraIMProviderInfo info;
    return UltraIM_GetProviderInfo(providerId, info) && UltraIM_HasCapability(info.capabilities, capability);
}

void ChatCubeWindow::ShowConversationMenu(int row, int windowX, int windowY) {
    if (!window_ || row < 0 || row >= conversations_->GetRowCount()) return;
    // A copy: the list may change while the menu is open, so the actions
    // look the conversation up again by its id.
    const UltraIMConversation c = conversations_->Conversations()[static_cast<size_t>(row)];
    const std::string id = c.conversationId;
    const std::string provider = c.providerId.empty() ? providerId_ : c.providerId;
    UltraIMProviderInfo info;
    UltraIM_GetProviderInfo(provider, info);
    const bool connected = info.connectionState == UltraIMConnectionState::Connected;
    const bool manage = connected && UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::ManageGroups);
    const bool leave = UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::LeaveConversation);
    const bool active = id == activeConversation_;

    // Sections, with a line between those that have something in them.
    std::vector<std::vector<MenuItemData>> sections(3);

    // ---- the conversation in the list
    std::vector<MenuItemData>& list = sections[0];
    if (!active) list.push_back(MenuItemData::Action(Tr("Open"), [this, id]() { OpenConversation(id); }));
    if (c.unreadCount > 0 || conversations_->IsMarkedUnread(c)) {
        list.push_back(MenuItemData::Action(Tr("Mark as read"), [this, id]() { MarkConversationRead(id); }));
    } else if (!active) {
        list.push_back(MenuItemData::Action(Tr("Mark as unread"), [this, id]() { SetMarkedUnread(id, true); }));
    }
    const bool pinned = conversations_->IsPinned(c);
    list.push_back(MenuItemData::Action(pinned ? Tr("Unpin") : Tr("Pin to top"),
                                        [this, id, pinned]() { SetPinned(id, !pinned); }));

    // ---- the person, or the group
    std::vector<MenuItemData>& who = sections[1];
    if (!c.isGroup) {
        const auto addCall = [&](bool video) {
            const UltraIMCapabilityFlags needed = video ? UltraIMCapabilityFlags::VideoCalls
                                                        : UltraIMCapabilityFlags::VoiceCalls;
            if (!UltraIM_HasCapability(info.capabilities, needed)) return;
            MenuItemData item = MenuItemData::Action(video ? Tr("Video call") : Tr("Voice call"),
                                                     [this, id, video]() { CallFromMenu(id, video); });
            item.enabled = callId_.empty() && UltraIM_CanCall(provider, id, video);
            who.push_back(item);
        };
        addCall(false);
        addCall(true);
        const std::string contact = c.directContactId;
        if (!contact.empty()) {
            who.push_back(MenuItemData::Action(provider == providerId_ ? Tr("Copy ChatCube ID") : Tr("Copy address"),
                                               [this, contact]() { CopyAddress(contact); }));
        }
        // Into one of the groups of the same network it is not in yet.
        if (manage && !contact.empty()) {
            std::vector<MenuItemData> groups;
            for (const UltraIMConversation& g : conversations_->Conversations()) {
                if (!g.isGroup || g.providerId != c.providerId) continue;
                if (std::find(g.memberIds.begin(), g.memberIds.end(), contact) != g.memberIds.end()) continue;
                const std::string groupId = g.conversationId;
                groups.push_back(MenuItemData::Action(g.title, [this, groupId, contact]() { AddToGroup(groupId, contact); }));
            }
            if (!groups.empty()) who.push_back(MenuItemData::Submenu(Tr("Add to group"), groups));
        }
    } else {
        // Every member with a menu of their own.
        const std::string me = info.localUserId;
        std::vector<MenuItemData> members;
        for (const std::string& member : c.memberIds) {
            const bool self = member == me;
            std::vector<MenuItemData> actions;
            if (!self) {
                actions.push_back(MenuItemData::Action(Tr("Send a message"),
                                                       [this, provider, member]() { MessageMember(provider, member); }));
            }
            actions.push_back(MenuItemData::Action(Tr("Copy ID"), [this, member]() { CopyAddress(member); }));
            // Someone else out: the owner's, where the network has one.
            if (manage && !self && (c.ownerId.empty() || c.ownerId == me)) {
                actions.push_back(MenuItemData::Action(Tr("Remove from group"),
                                                       [this, id, member]() { RemoveMember(id, member); }));
            }
            std::string name = self ? Tr("You") : MemberName(c, member);
            if (!c.ownerId.empty() && member == c.ownerId) name = TrF("{0} (owner)", name);
            members.push_back(MenuItemData::Submenu(name, actions));
        }
        if (!members.empty()) {
            who.push_back(MenuItemData::Submenu(TrF("Members ({0})", static_cast<long long>(members.size())), members));
        }
        if (manage) {
            who.push_back(MenuItemData::Action(Tr("Add members"), [this, id]() { AskToAddMembers(id); }));
            who.push_back(MenuItemData::Action(Tr("Rename group"), [this, id]() { AskToRenameGroup(id); }));
        }
    }

    // ---- out of it
    if (leave) {
        sections[2].push_back(MenuItemData::Action(c.isGroup ? Tr("Leave group") : Tr("Delete chat"),
                                                   [this, id]() { AskToLeave(id); }));
    }

    conversationMenu_ = std::make_shared<UltraCanvasMenu>("cc-conversation-menu", 0, 0, 240, 0);
    conversationMenu_->SetMenuType(MenuType::PopupMenu);
    conversationMenu_->AddItem(MenuItemData::Header(c.title));
    bool first = true;
    for (const auto& section : sections) {
        if (section.empty()) continue;
        if (!first) conversationMenu_->AddItem(MenuItemData::Separator());
        first = false;
        for (const MenuItemData& item : section) conversationMenu_->AddItem(item);
    }
    PopupElementSettings settings;
    conversationMenu_->OpenMenu(Point2Di(windowX, windowY), *window_, settings);
}

void ChatCubeWindow::ApplyMarks() {
    fillingList_ = true;
    conversations_->SetMarks(marks_.pinned, marks_.unread);
    const int row = conversations_->FindRow(activeConversation_);
    if (row >= 0) conversationList_->GetSelection()->Select(row);
    fillingList_ = false;
    if (conversationList_) conversationList_->RequestRedraw();
}

void ChatCubeWindow::UpsertConversation(const UltraIMConversation& conversation) {
    fillingList_ = true;
    conversations_->Upsert(conversation);
    const int row = conversations_->FindRow(activeConversation_);
    if (row >= 0) conversationList_->GetSelection()->Select(row);
    fillingList_ = false;
    if (conversation.conversationId == activeConversation_) UpdateHeader();
}

void ChatCubeWindow::SetPinned(const std::string& conversationId, bool pinned) {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (!c) return;
    const std::string key = ConversationListModel::MarkKey(*c);
    if (pinned) marks_.pinned.insert(key);
    else marks_.pinned.erase(key);
    marks_.Save();
    ApplyMarks();
}

void ChatCubeWindow::SetMarkedUnread(const std::string& conversationId, bool unread) {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (!c) return;
    const std::string key = ConversationListModel::MarkKey(*c);
    if (unread) marks_.unread.insert(key);
    else marks_.unread.erase(key);
    marks_.Save();
    ApplyMarks();
}

void ChatCubeWindow::MarkConversationRead(const std::string& conversationId) {
    const UltraIMConversation* found = conversations_->Find(conversationId);
    if (!found) return;
    UltraIMConversation c = *found;
    UltraIM_MarkRead(c.providerId.empty() ? providerId_ : c.providerId, conversationId);
    if (c.unreadCount > 0) {
        c.unreadCount = 0;
        UpsertConversation(c);
    }
    if (conversations_->IsMarkedUnread(c)) SetMarkedUnread(conversationId, false);
}

void ChatCubeWindow::CallFromMenu(const std::string& conversationId, bool video) {
    if (conversationId != activeConversation_) OpenConversation(conversationId);
    StartCall(video);
}

void ChatCubeWindow::CopyAddress(const std::string& address) {
    if (address.empty()) return;
    if (SetClipboardText(address)) SetStatus(TrF("{0} is on the clipboard.", address));
}

std::string ChatCubeWindow::MemberName(const UltraIMConversation& group, const std::string& memberId) const {
    for (const UltraIMConversation& c : conversations_->Conversations()) {
        if (!c.isGroup && c.providerId == group.providerId && c.directContactId == memberId && !c.title.empty())
            return c.title;
    }
    // Nobody we chat with: their ID, in groups of four for a ChatCube ID.
    return group.providerId == providerId_ ? GroupUserId(memberId) : memberId;
}

void ChatCubeWindow::MessageMember(const std::string& providerId, const std::string& memberId) {
    for (const UltraIMConversation& c : conversations_->Conversations()) {
        if (!c.isGroup && c.providerId == providerId && c.directContactId == memberId) {
            OpenConversation(c.conversationId);
            return;
        }
    }
    SetStatus(Tr("Opening the chat\xE2\x80\xA6"));
    UltraIM_AddContact(providerId, memberId, [this](const UltraIMResult& r, const UltraIMConversation& c) {
        if (!r) {
            SetStatus(TrF("Could not open the chat: {0}", r.message));
            return;
        }
        UpsertConversation(c);
        SetStatus(std::string());
        OpenConversation(c.conversationId);
    });
}

void ChatCubeWindow::RemoveMember(const std::string& conversationId, const std::string& memberId) {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (!c) return;
    const std::string provider = c->providerId.empty() ? providerId_ : c->providerId;
    const std::string name = MemberName(*c, memberId);
    const std::string question = TrF("Take {0} out of {1}?", name, c->title);
    UltraCanvasDialogManager::ShowConfirmation(question, Tr("Remove from group"),
                                               [this, provider, conversationId, memberId, name](bool yes) {
        if (!yes) return;
        UltraIM_RemoveGroupMember(provider, conversationId, memberId, [this, name](const UltraIMResult& r) {
            SetStatus(r ? TrF("{0} is no longer in the group.", name)
                        : TrF("Could not remove {0}: {1}", name, r.message));
        });
    }, window_.get());
}

void ChatCubeWindow::AskToRenameGroup(const std::string& conversationId) {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (!c) return;
    const std::string provider = c->providerId.empty() ? providerId_ : c->providerId;
    const std::string current = c->title;
    UltraCanvasDialogManager::ShowInputDialog(Tr("The group's new name:"), Tr("Rename group"), current, InputType::Text,
                                              [this, provider, conversationId, current](DialogResult result,
                                                                                         const std::string& text) {
        const std::string title = Trim(text);
        if (result != DialogResult::OK || title.empty() || title == current) return;
        UltraIM_RenameGroup(provider, conversationId, title,
                            [this, title](const UltraIMResult& r, const UltraIMConversation& c) {
            if (!r) {
                SetStatus(TrF("Could not rename the group: {0}", r.message));
                return;
            }
            if (!c.conversationId.empty()) UpsertConversation(c);
            SetStatus(TrF("The group is called {0} now.", title));
        });
    }, window_.get());
}

void ChatCubeWindow::AskToAddMembers(const std::string& conversationId) {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (!c) return;
    const std::string provider = c->providerId.empty() ? providerId_ : c->providerId;
    const std::string prompt = provider == providerId_
        ? Tr("The ChatCube IDs of the people to add, separated by commas:")
        : Tr("The addresses of the people to add (@name:server), separated by commas:");
    UltraCanvasDialogManager::ShowInputDialog(prompt, TrF("Add members to {0}", c->title), "", InputType::Text,
                                              [this, provider, conversationId](DialogResult result,
                                                                               const std::string& text) {
        if (result != DialogResult::OK) return;
        std::vector<std::string> ids;
        std::stringstream stream(text);
        std::string item;
        while (std::getline(stream, item, ',')) {
            std::string id;
            for (char ch : item) {
                if (ch != ' ' && ch != '\t') id += ch;
            }
            if (!id.empty()) ids.push_back(id);
        }
        if (ids.empty()) return;
        SetStatus(Tr("Adding to the group\xE2\x80\xA6"));
        UltraIM_AddGroupMembers(provider, conversationId, ids,
                                [this, count = ids.size()](const UltraIMResult& r, const UltraIMConversation& c) {
            if (!r) {
                SetStatus(TrF("Could not add to the group: {0}", r.message));
                return;
            }
            if (!c.conversationId.empty()) UpsertConversation(c);
            SetStatus(TrN("Added {n} person to the group.", "Added {n} people to the group.",
                          static_cast<long long>(count)));
        });
    }, window_.get());
}

void ChatCubeWindow::AddToGroup(const std::string& groupId, const std::string& memberId) {
    const UltraIMConversation* g = conversations_->Find(groupId);
    if (!g) return;
    const std::string provider = g->providerId.empty() ? providerId_ : g->providerId;
    const std::string name = MemberName(*g, memberId);
    const std::string title = g->title;
    UltraIM_AddGroupMembers(provider, groupId, {memberId},
                            [this, name, title](const UltraIMResult& r, const UltraIMConversation& c) {
        if (!r) {
            SetStatus(TrF("Could not add {0} to {1}: {2}", name, title, r.message));
            return;
        }
        if (!c.conversationId.empty()) UpsertConversation(c);
        SetStatus(TrF("Added {0} to {1}.", name, title));
    });
}

void ChatCubeWindow::AskToLeave(const std::string& conversationId) {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (!c) return;
    const std::string provider = c->providerId.empty() ? providerId_ : c->providerId;
    const bool group = c->isGroup;
    const std::string title = c->title;
    const std::string question = group
        ? TrF("Leave {0}? Its messages stop coming to you, and the others are told you left.", title)
        : TrF("Delete the chat with {0} and its messages on this computer? {0} keeps theirs.", title);
    UltraCanvasDialogManager::ShowConfirmation(question, group ? Tr("Leave group") : Tr("Delete chat"),
                                               [this, provider, conversationId, group, title](bool yes) {
        if (!yes) return;
        // The conversation leaves the list with the ConversationRemove that follows.
        UltraIM_LeaveConversation(provider, conversationId, [this, group, title](const UltraIMResult& r) {
            if (!r) {
                SetStatus(group ? TrF("Could not leave {0}: {1}", title, r.message)
                                : TrF("Could not delete the chat: {0}", r.message));
                return;
            }
            SetStatus(group ? TrF("You left {0}.", title) : TrF("Deleted the chat with {0}.", title));
        });
    }, window_.get());
}

void ChatCubeWindow::CloseConversationView() {
    activeConversation_.clear();
    messages_->Clear();
    messageList_->SetVisible(false);
    emptyHint_->SetVisible(true);
    UpdateHeader();
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
    UltraIM_AddContact(ProviderOfAddress(id), id, [this](const UltraIMResult& r, const UltraIMConversation& c) {
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

void ChatCubeWindow::ToggleMatrixForm() {
    UltraIMProviderInfo info;
    if (!UltraIM_GetProviderInfo(matrixProviderId_, info)) {
        SetStatus("The Matrix module was not found (Plugins/UltraIM).");
        return;
    }
    // Signed in, the form is the keys' buttons; signed out, the sign-in.
    if (info.connectionState == UltraIMConnectionState::Connected) {
        matrixForm_->SetVisible(false);
        const bool canExport = UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::E2EEncryption);
        if (!canExport) SetStatus("Matrix: this ChatCube was built without end-to-end encryption.");
        matrixKeysForm_->SetVisible(canExport && !matrixKeysForm_->IsVisible());
    } else {
        matrixKeysForm_->SetVisible(false);
        matrixForm_->SetVisible(!matrixForm_->IsVisible());
    }
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ToggleTelegramForm() {
    UltraIMProviderInfo info;
    if (!UltraIM_GetProviderInfo(telegramProviderId_, info)) {
        SetStatus("The Telegram module was not found (Plugins/UltraIM).");
        return;
    }
    if (info.connectionState == UltraIMConnectionState::Connected) return;    // the status line says so
    telegramForm_->SetVisible(!telegramForm_->IsVisible());
    UpdateTelegramForm();
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::UpdateTelegramForm() {
    UltraIMProviderInfo info;
    UltraIM_GetProviderInfo(telegramProviderId_, info);
    const UltraIMAuthStep step = info.authStep;
    const bool start = step == UltraIMAuthStep::Idle || step == UltraIMAuthStep::PhoneNumber;
    // The application's keys come from the relay when it has them; the
    // fields are for a relay without, or a ChatCube with no relay.
    const bool keysFromRelay = !telegramRelayApiId_.empty() && !telegramRelayApiHash_.empty();
    // Waiting for the phone to scan: the hint is the link, shown as the QR
    // code, and the words say what to do with it.
    const bool scan = step == UltraIMAuthStep::OtherDevice;
    telegramPhone_->SetVisible(start);
    telegramApiId_->SetVisible(start && !keysFromRelay);
    telegramApiHash_->SetVisible(start && !keysFromRelay);
    telegramCode_->SetVisible(step == UltraIMAuthStep::Code);
    telegramHint_->SetVisible((step == UltraIMAuthStep::Code && !info.authHint.empty()) || scan);
    telegramHint_->SetText(step == UltraIMAuthStep::Code ? info.authHint
                           : scan ? "Scan this with the Telegram app on your phone: Settings, Devices, "
                                    "Link Desktop Device. No code is needed; the two-step password "
                                    "is asked for here afterwards, if your account has one."
                                  : std::string());
    telegramQr_->SetVisible(scan);
    if (scan) telegramQr_->SetContent(info.authHint);
    telegramResend_->SetVisible(step == UltraIMAuthStep::Code && !info.authResendHint.empty());
    if (!info.authResendHint.empty()) telegramResend_->SetText(info.authResendHint);
    telegramPassword_->SetVisible(step == UltraIMAuthStep::Password);
    telegramName_->SetVisible(step == UltraIMAuthStep::Registration);
    if (step == UltraIMAuthStep::Code) telegramCode_->SetPlaceholder("The code");
    if (step == UltraIMAuthStep::Password && !info.authHint.empty()) telegramPassword_->SetPlaceholder(info.authHint);
    telegramNext_->SetText(start ? "Send me the code" : scan ? "Use my phone number instead" : "Sign in to Telegram");
    telegramQrButton_->SetVisible(start || step == UltraIMAuthStep::Code);
    telegramQrButton_->SetText(start ? "Sign in with a QR code instead" : "Scan a QR code instead");
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::SignInToTelegram(bool stored) {
    UltraIMCredentials credentials;
    credentials.loginType = UltraIMLoginType::PhoneOTP;
    if (!stored) {
        UltraIMProviderInfo info;
        UltraIM_GetProviderInfo(telegramProviderId_, info);
        switch (info.authStep) {
            case UltraIMAuthStep::Code:         credentials.otpCode = Trim(telegramCode_->GetText()); break;
            case UltraIMAuthStep::Password:     credentials.password = telegramPassword_->GetText(); break;
            case UltraIMAuthStep::Registration: credentials.displayName = Trim(telegramName_->GetText()); break;
            case UltraIMAuthStep::OtherDevice:
                // Back from the QR code to the number: Telegram takes none
                // while the code waits, so the sign-in starts over, with the
                // form back at the number.
                UltraIM_SignOut(telegramProviderId_);
                UpdateTelegramForm();
                return;
            default:
                credentials.phoneNumber = Trim(telegramPhone_->GetText());
                credentials.apiId = Trim(telegramApiId_->GetText());
                credentials.apiHash = Trim(telegramApiHash_->GetText());
                if (credentials.phoneNumber.empty()) {
                    SetStatus("Telegram: enter your phone number with its country code, or sign in with a QR code.");
                    return;
                }
                break;
        }
        telegramNext_->SetDisabled(true);
    }
    SendTelegramSignIn(std::move(credentials), stored);
}

void ChatCubeWindow::SignInToTelegramByQrCode() {
    UltraIMCredentials credentials;
    credentials.loginType = UltraIMLoginType::QRCode;
    credentials.apiId = Trim(telegramApiId_->GetText());
    credentials.apiHash = Trim(telegramApiHash_->GetText());
    telegramNext_->SetDisabled(true);
    telegramQrButton_->SetDisabled(true);
    SendTelegramSignIn(std::move(credentials), false);
}

void ChatCubeWindow::SendTelegramSignIn(UltraIMCredentials credentials, bool stored) {
    // The relay's keys, unless the user typed a pair (a relay without them,
    // or their own application).
    if (credentials.apiId.empty() || credentials.apiHash.empty()) {
        credentials.apiId = telegramRelayApiId_;
        credentials.apiHash = telegramRelayApiHash_;
    }
    SetProviderStatus(telegramProviderId_,
                      credentials.loginType == UltraIMLoginType::QRCode ? "asking Telegram for a QR code\xE2\x80\xA6"
                                                                         : "signing in\xE2\x80\xA6");
    UltraIM_SignIn(telegramProviderId_, credentials, [this, stored](const UltraIMResult& r) {
        telegramNext_->SetDisabled(false);
        telegramQrButton_->SetDisabled(false);
        if (r.code == UltraIMResultCode::NeedsInput) {
            telegramForm_->SetVisible(true);
            UpdateTelegramForm();
            // The hint, except the QR code's: that is the link, with the
            // sign-in token in it, and the form shows it as the code.
            UltraIMProviderInfo info;
            UltraIM_GetProviderInfo(telegramProviderId_, info);
            SetProviderStatus(telegramProviderId_, info.authStep == UltraIMAuthStep::OtherDevice
                                                       ? "scan the QR code with your phone's Telegram app"
                                                       : r.message);
            return;
        }
        if (!r) {
            SetProviderStatus(telegramProviderId_, r.message);
            if (stored) telegramForm_->SetVisible(true);
            UpdateTelegramForm();
            return;
        }
        // Nothing of the sign-in is kept here: TDLib keeps the session.
        telegramCode_->SetText("");
        telegramPassword_->SetText("");
        telegramForm_->SetVisible(false);
        RefreshConversations();
    });
}

void ChatCubeWindow::ExportMatrixKeys() {
    UltraCanvasFileLoader::RequestNewDocumentPassword(window_.get(), [this](bool proceed, const std::string& passphrase) {
        if (!proceed) return;
        SetStatus("Matrix: exporting the room keys\xE2\x80\xA6");
        UltraIM_ExportEncryptionKeys(matrixProviderId_, passphrase, [this](const UltraIMResult& r, const std::string& data) {
            if (!r) {
                SetStatus("Matrix: " + r.message);
                return;
            }
            FileDialogOptions opts;
            opts.SetTitle("Save the Matrix room keys")
                .SetDefaultFileName("matrix-room-keys.txt")
                .AddFilter("Matrix room keys", std::vector<std::string>{"txt"})
                .AddFilter("All files", std::vector<std::string>{"*"})
                .SetParentWindow(window_.get());
            UltraCanvasFileLoader::SaveFileDialog(opts, [this, data](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                // The provider wrote the format; the file is the user's.
                std::FILE* f = OpenFileUtf8(path, "wb");
                const bool written = f && std::fwrite(data.data(), 1, data.size(), f) == data.size();
                if (f) std::fclose(f);
                SetStatus(written ? "Matrix: room keys saved to " + path : "Matrix: could not write " + path);
            });
        });
    });
}

void ChatCubeWindow::ImportMatrixKeys() {
    FileDialogOptions opts;
    opts.SetTitle("Import Matrix room keys")
        .AddFilter("Matrix room keys", std::vector<std::string>{"txt"})
        .AddFilter("All files", std::vector<std::string>{"*"})
        .SetParentWindow(window_.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        const FileBytesResult file = UltraCanvasFileLoader::LoadFile(path);
        if (!file.success) {
            SetStatus("Matrix: " + file.error);
            return;
        }
        const std::string data(file.bytes.begin(), file.bytes.end());
        UltraCanvasFileLoader::RequestDocumentPassword(path, window_.get(),
                                                       [this, data](bool proceed, const std::string& passphrase) {
            if (!proceed) return;
            SetStatus("Matrix: importing the room keys\xE2\x80\xA6");
            UltraIM_ImportEncryptionKeys(matrixProviderId_, data, passphrase, [this](const UltraIMResult& r, int imported) {
                SetStatus(r ? "Matrix: " + std::to_string(imported) + " room keys imported" : "Matrix: " + r.message);
                if (r) RefreshConversations();
            });
        });
    });
}

void ChatCubeWindow::SignInToMatrix(bool stored) {
    UltraIMCredentials credentials;
    credentials.loginType = UltraIMLoginType::UsernamePassword;
    if (!stored) {
        credentials.server = Trim(matrixServer_->GetText());
        credentials.username = Trim(matrixUser_->GetText());
        credentials.password = matrixPassword_->GetText();
        if (credentials.username.empty() || credentials.password.empty()) {
            SetStatus("Matrix: enter your user name and password.");
            return;
        }
        matrixSignIn_->SetDisabled(true);
    }
    SetProviderStatus(matrixProviderId_, "signing in\xE2\x80\xA6");
    UltraIM_SignIn(matrixProviderId_, credentials, [this, stored](const UltraIMResult& r) {
        if (!stored) matrixSignIn_->SetDisabled(false);
        if (!r) {
            SetProviderStatus(matrixProviderId_, r.message);
            return;
        }
        matrixPassword_->SetText("");      // not kept: the session token is
        matrixForm_->SetVisible(false);
        RefreshConversations();
    });
}

void ChatCubeWindow::RefreshNearby() {
    UltraIM_FetchNearbyContacts(providerId_, [this](const UltraIMResult&, const std::vector<UltraIMContact>& list) {
        nearbyBox_->ClearChildren();
        if (!list.empty()) nearbyBox_->AddChild(MakeLabel("cc-nearby-title", "On this network", 9.0f, L().inkMuted));
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
        nearbyBox_->SetVisible(!list.empty() && settings_.showNearby);
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
    // The members' addresses say where: a Matrix room, a Telegram group, or ChatCube.
    UltraIM_CreateGroup(members.empty() ? providerId_ : ProviderOfAddress(members.front()), title, members,
                        [this](const UltraIMResult& r, const UltraIMConversation& c) {
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

std::string ChatCubeWindow::ProviderOfAddress(const std::string& address) const {
    if (address.empty()) return providerId_;
    if (address[0] == '@') return address.find(':') != std::string::npos ? matrixProviderId_ : telegramProviderId_;
    if (address[0] == '+' || address.rfind("t.me/", 0) == 0 || address.rfind("https://t.me/", 0) == 0)
        return telegramProviderId_;
    return providerId_;
}

std::string ChatCubeWindow::ProviderOf(const std::string& conversationId) const {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    return c && !c->providerId.empty() ? c->providerId : providerId_;
}

void ChatCubeWindow::OpenConversation(const std::string& conversationId) {
    activeConversation_ = conversationId;
    activeProvider_ = ProviderOf(conversationId);
    typingContact_.clear();
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (c && conversations_->IsMarkedUnread(*c)) SetMarkedUnread(conversationId, false);   // read now
    c = conversations_->Find(conversationId);
    messages_->SetGroup(c && c->isGroup);
    messages_->Clear();
    emptyHint_->SetVisible(false);
    messageList_->SetVisible(true);
    fillingList_ = true;
    const int row = conversations_->FindRow(conversationId);
    if (row >= 0) conversationList_->GetSelection()->Select(row);
    fillingList_ = false;
    UpdateHeader();

    UltraIM_FetchMessages(activeProvider_, conversationId, "", 200,
                          [this, conversationId](const UltraIMResult& r, const std::vector<UltraIMMessage>& list) {
        if (conversationId != activeConversation_) return;      // the user moved on
        if (!r) SetStatus("Could not load the messages: " + r.message);
        messages_->SetMessages(list);
        ScrollToEnd();
    });
    UltraIM_MarkRead(activeProvider_, conversationId);
    input_->SetFocus(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::UpdateHeader() {
    const UltraIMConversation* c = conversations_->Find(activeConversation_);
    if (!c) {
        title_->SetText("");
        subtitle_->SetText("");
        headerAvatar_->SetVisible(false);
        headerPhoto_->SetVisible(false);
        attach_->SetDisabled(true);
        return;
    }
    title_->SetText(c->title);
    // Their picture when there is one and it reads; else the initials.
    bool photo = false;
    if (!c->avatarPath.empty()) {
        if (c->avatarPath != headerPhotoPath_) {
            headerPhotoPath_ = c->avatarPath;
            headerPhoto_->LoadFromFile(c->avatarPath);
        }
        UCImagePtr image = UCImage::Get(c->avatarPath);
        photo = image && image->IsValid();
    }
    headerAvatar_->SetText(c->isGroup ? "#" : Initials(c->title));
    headerAvatar_->SetBackgroundColor(c->isGroup ? L().well : AvatarColor(c->conversationId, L()));
    headerAvatar_->SetTextColor(c->isGroup ? L().inkMuted : Color(255, 255, 255, 255));
    headerAvatar_->SetVisible(!photo);
    headerPhoto_->SetVisible(photo);
    // The paperclip, for a network that takes pictures or files.
    const bool attachments = AttachmentsPossible();
    attach_->SetDisabled(!attachments);
    attach_->SetTooltip(attachments ? "Send a picture, a video or a file"
                                    : "This network does not take pictures or files yet");
    if (!typingContact_.empty() && NowMs() < typingUntilMs_) {
        subtitle_->SetText("typing\xE2\x80\xA6");
    } else if (c->isGroup) {
        subtitle_->SetText(TrN("{n} member", "{n} members", static_cast<long long>(c->memberIds.size())) +
                           (c->isEncrypted ? " \xC2\xB7 " + Tr("end-to-end encrypted") : std::string()));
    } else if (c->providerId == providerId_) {
        subtitle_->SetText(PresenceText(c->presence));
    } else {
        // Not ChatCube Direct: no relay to wait on.
        subtitle_->SetText((c->presence == UltraIMPresence::Offline ? "offline" : PresenceText(c->presence)) +
                           (c->isEncrypted ? " \xC2\xB7 end-to-end encrypted" : ""));
    }
    // Calls need UltraRTC media; the provider says when they are possible.
    // One call at a time.
    voiceCall_->SetDisabled(!callId_.empty() || !UltraIM_CanCall(activeProvider_, activeConversation_, false));
    videoCall_->SetDisabled(!callId_.empty() || !UltraIM_CanCall(activeProvider_, activeConversation_, true));
}

void ChatCubeWindow::ScrollToEnd() {
    if (messages_->GetRowCount() > 0) messageList_->EnsureRowVisible(messages_->GetRowCount() - 1);
}

bool ChatCubeWindow::AttachmentsPossible() const {
    UltraIMProviderInfo info;
    return !activeConversation_.empty() && UltraIM_GetProviderInfo(activeProvider_, info) &&
           (UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::ImageMessages) ||
            UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::FileTransfer));
}

void ChatCubeWindow::SendCurrentMessage() {
    if (activeConversation_.empty()) return;
    const std::string text = Trim(input_->GetText());
    if (staged_.empty()) {
        if (!text.empty()) SendText(text);
        return;
    }
    // What waits in the strip, each as a message of its own, the text as the
    // caption of the first picture or video; a strip of files alone sends
    // the text after them.
    std::vector<StagedAttachment> items;
    items.swap(staged_);
    for (const StagedAttachment& item : items) {
        if (item.tile) attachmentStrip_->RemoveChild(item.tile);
    }
    UpdateAttachmentStrip();
    bool captionSent = text.empty();
    for (const StagedAttachment& item : items) {
        const bool takesCaption = !captionSent && AttachmentKind(item.path) != UltraIMMessageType::File;
        SendAttachment(item.path, takesCaption ? text : std::string());
        if (takesCaption) captionSent = true;
    }
    if (!captionSent) SendText(text);
    input_->SetText("");
    lastTypingSentMs_ = 0;
    UltraIM_SetTyping(activeProvider_, activeConversation_, false);
}

void ChatCubeWindow::SendText(const std::string& text) {
    if (activeConversation_.empty() || text.empty()) return;

    // Optimistic: the bubble is shown now, its ticks follow the events.
    UltraIMProviderInfo info;
    UltraIM_GetProviderInfo(activeProvider_, info);
    UltraIMMessage message;
    message.providerId = activeProvider_;
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
    UltraIM_SetTyping(activeProvider_, activeConversation_, false);

    UltraIMSendOptions options;
    options.clientMessageId = message.clientMessageId;
    const UltraIMResult r = UltraIM_SendText(activeProvider_, activeConversation_, text, options);
    if (!r) {
        messages_->UpdateStatus(message.clientMessageId, UltraIMDeliveryStatus::Failed);
        SetStatus("Not sent: " + r.message);
    }
}

void ChatCubeWindow::AttachFile() {
    if (activeConversation_.empty()) return;
    FileDialogOptions opts;
    opts.SetTitle("Send a picture, a video or a file").SetParentWindow(window_.get());
    // Every picture and video format this build reads, then anything.
    UltraCanvasFileLoader::AddLoadTypes(opts, "Pictures and videos", {MediaFormatCategory::Bitmap, MediaFormatCategory::Video});
    opts.AddFilter("All files", std::vector<std::string>{"*"});
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        StageAttachment(path, false);
    });
}

bool ChatCubeWindow::PasteAttachment() {
    if (activeConversation_.empty()) return false;
    auto* clipboard = GetClipboard();
    if (!clipboard) return false;
    // Files copied in a file manager first - they come with their path as
    // text, which is not what to paste - then text, which the field takes,
    // then a picture: a screenshot, a copy from a browser or a drawing program.
    std::vector<std::string> files;
    if (clipboard->GetFiles(files) && !files.empty()) {
        int staged = 0;
        std::error_code ec;
        for (const std::string& file : files) {
            if (!std::filesystem::is_regular_file(PathFromUtf8(file), ec)) continue;
            StageAttachment(file, false);
            ++staged;
        }
        if (staged) return true;
    }
    std::string text;
    if (clipboard->GetText(text) && !text.empty() && LooksLikeText(text)) return false;
    std::vector<uint8_t> bytes;
    std::string format;
    if (!clipboard->GetImage(bytes, format) || bytes.empty()) return false;
    if (!AttachmentsPossible()) {
        SetStatus("This network does not take pictures yet.");
        return true;
    }

    // Into a file of ChatCube's own, in its cache: the provider reads the
    // file, and the bubble shows it from there. PNG and JPEG as they came;
    // anything else (a bitmap, say) decoded and written as PNG.
    std::error_code ec;
    const std::filesystem::path folder = UltraCanvasDataFolder("ChatCube") / "cache" / "pasted";
    std::filesystem::create_directories(folder, ec);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    std::string name = std::string("Pasted-") + stamp;
    for (int n = 1; std::filesystem::exists(folder / PathFromUtf8(name + ".png"), ec) ||
                    std::filesystem::exists(folder / PathFromUtf8(name + ".jpg"), ec); ++n)
        name = std::string("Pasted-") + stamp + "-" + std::to_string(n);
    std::string path;
    if (format == "image/png" || format == "image/jpeg") {
        path = PathToUtf8(folder / PathFromUtf8(name + (format == "image/png" ? ".png" : ".jpg")));
        std::FILE* out = OpenFileUtf8(path, "wb");
        const bool written = out && std::fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
        if (out) std::fclose(out);
        if (!written) path.clear();
    } else {
        UCImagePtr image = UCImage::LoadFromMemory(bytes);
        if (image && image->IsValid()) {
            path = PathToUtf8(folder / PathFromUtf8(name + ".png"));
            UCImageSave::ImageExportOptions options;
            options.format = UCImageSaveFormat::PNG;
            if (!image->Save(path, options).empty()) path.clear();
        }
    }
    if (path.empty()) {
        SetStatus("The picture on the clipboard could not be read.");
        return true;
    }
    StageAttachment(path, true);
    return true;
}

void ChatCubeWindow::StageAttachment(const std::string& path, bool ownFile) {
    if (activeConversation_.empty() || path.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(PathFromUtf8(path), ec)) {
        SetStatus("There is no file " + path);
        return;
    }
    if (!AttachmentsPossible()) {
        SetStatus("This network does not take pictures or files yet.");
        if (ownFile) std::filesystem::remove(PathFromUtf8(path), ec);
        return;
    }
    for (const StagedAttachment& item : staged_) {
        if (item.path == path) return;      // already waiting
    }

    // The tile: the picture, or the file's name and size, with the x at its
    // top right corner.
    const std::string id = "cc-staged-" + std::to_string(++stagedCount_);
    const bool picture = IsPictureFile(path);
    const int width = picture ? kTileSide : kFileTileWidth;
    auto tile = std::make_shared<UltraCanvasContainer>(id);
    tile->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    tile->size.width = CSSLayout::Dimension::Px(static_cast<float>(width));
    tile->size.height = CSSLayout::Dimension::Px(static_cast<float>(kTileSide));
    tile->SetBackgroundColor(L().panel);
    tile->SetBorders(1.0f, L().line, 10.0f);
    const std::string fileName = PathToUtf8(PathFromUtf8(path).filename());
    if (picture) {
        auto image = std::make_shared<UltraCanvasImageElement>(id + "-picture", 0, 0, kTileSide, kTileSide);
        image->SetFitMode(ImageFitMode::Cover);
        image->SetBorderRadius(10.0f);
        image->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        image->size.width = CSSLayout::Dimension::Px(static_cast<float>(kTileSide));
        image->size.height = CSSLayout::Dimension::Px(static_cast<float>(kTileSide));
        image->SetTooltip(fileName);
        image->LoadFromFile(path, true);
        tile->AddChild(image);
    } else {
        // The name over the size, each at its place (a non-zero offset keeps
        // a child out of the flow, which is what leaves the x its corner).
        const int textWidth = width - kTileClose - 20;
        auto name = std::make_shared<UltraCanvasLabel>(id + "-name", 10, 12, textWidth, 18);
        name->SetText(fileName);
        name->SetFontSize(kTextFont);
        name->SetFontWeight(FontWeight::Bold);
        name->SetTextColor(L().ink);
        name->SetWrap(TextWrap::WrapNone);
        name->SetTooltip(fileName);
        tile->AddChild(name);
        const int64_t bytes = static_cast<int64_t>(std::filesystem::file_size(PathFromUtf8(path), ec));
        auto size = std::make_shared<UltraCanvasLabel>(id + "-size", 10, 34, textWidth, 16);
        size->SetText(bytes > 0 ? FormatSize(bytes) : std::string());
        size->SetFontSize(kTextFont - 1.0f);
        size->SetTextColor(L().inkMuted);
        tile->AddChild(size);
    }
    // The x, over the tile's top right corner (a non-zero offset places it
    // there, outside the flow).
    auto remove = std::make_shared<UltraCanvasButton>(id + "-remove", width - kTileClose - 3, 3, kTileClose, kTileClose, "");
    ButtonStyle st = remove->GetStyle();
    st.cornerRadius = kTileClose / 2.0f;
    st.borderWidth = 0.0f;
    st.normalColor = Color(0, 0, 0, 150);
    st.hoverColor = L().danger;
    st.pressedColor = Darker(L().danger, 24);
    st.normalTextColor = st.hoverTextColor = st.pressedTextColor = Color(255, 255, 255, 255);
    remove->SetStyle(st);
    remove->SetIcon(IconPath("close"));
    remove->SetIconSize(12, 12);
    remove->SetUseIconAsMask(true);
    remove->SetTooltip("Remove");
    remove->SetOnClick([this, tile = tile.get()]() { RemoveStagedAttachment(tile); });
    tile->AddChild(remove);

    staged_.push_back({path, ownFile, tile});
    attachmentStrip_->AddChild(tile);
    UpdateAttachmentStrip();
    input_->SetFocus(true);
}

void ChatCubeWindow::RemoveStagedAttachment(const UltraCanvasContainer* tile) {
    for (auto it = staged_.begin(); it != staged_.end(); ++it) {
        if (it->tile.get() != tile) continue;
        if (it->ownFile) {
            std::error_code ec;
            std::filesystem::remove(PathFromUtf8(it->path), ec);
        }
        attachmentStrip_->RemoveChild(it->tile);
        staged_.erase(it);
        break;
    }
    UpdateAttachmentStrip();
    // The click on the x took the focus: back to the field, so Enter sends.
    input_->SetFocus(true);
}

void ChatCubeWindow::UpdateAttachmentStrip() {
    attachmentStrip_->SetVisible(!staged_.empty());
    const std::string key = settings_.ctrlEnterSends ? "Ctrl+Enter" : "Enter";
    send_->SetTooltip(staged_.empty() ? "Send (" + key + ")"
                                      : (staged_.size() == 1 ? "Send the file (" : "Send the files (") + key + ")");
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::SendAttachment(const std::string& path, const std::string& caption) {
    if (activeConversation_.empty() || path.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(PathFromUtf8(path), ec)) {
        SetStatus("There is no file " + path);
        return;
    }
    const UltraIMMessageType kind = AttachmentKind(path);

    // Optimistic, like a text: the bubble shows the file at once.
    UltraIMProviderInfo info;
    UltraIM_GetProviderInfo(activeProvider_, info);
    UltraIMMessage message;
    message.providerId = activeProvider_;
    message.conversationId = activeConversation_;
    message.clientMessageId = UltraIM_NewClientMessageId();
    message.messageId = message.clientMessageId;
    message.senderId = info.localUserId;
    message.type = kind;
    message.text = kind == UltraIMMessageType::File ? std::string() : caption;
    message.attachment.localPath = path;
    message.attachment.isDownloaded = true;
    message.attachment.fileName = PathToUtf8(PathFromUtf8(path).filename());
    message.attachment.fileSizeBytes = static_cast<int64_t>(std::filesystem::file_size(PathFromUtf8(path), ec));
    if (IsPictureFile(path)) {
        // A photo's size, and a drawing's (an SVG goes as a file but shows
        // as its picture): the bubble lays the picture out from it.
        UCImagePtr image = UCImage::Get(path);
        if (image && image->IsValid()) {
            message.attachment.width = image->GetWidth();
            message.attachment.height = image->GetHeight();
        }
    }
    message.timestampMs = NowMs();
    message.isOwn = true;
    message.deliveryStatus = UltraIMDeliveryStatus::Sending;
    messages_->Upsert(message);
    ScrollToEnd();

    UltraIMSendOptions options;
    options.clientMessageId = message.clientMessageId;
    UltraIMResult r;
    if (kind == UltraIMMessageType::Image) r = UltraIM_SendImage(activeProvider_, activeConversation_, path, caption, options);
    else if (kind == UltraIMMessageType::Video) r = UltraIM_SendVideo(activeProvider_, activeConversation_, path, caption, options);
    else r = UltraIM_SendFile(activeProvider_, activeConversation_, path, options);
    if (!r) {
        messages_->UpdateStatus(message.clientMessageId, UltraIMDeliveryStatus::Failed);
        SetStatus("Not sent: " + r.message);
    }
}

void ChatCubeWindow::OpenAttachment(int row) {
    const auto& rows = messages_->Rows();
    if (row < 0 || row >= static_cast<int>(rows.size()) || rows[static_cast<size_t>(row)].kind != MessageRow::Kind::Message)
        return;
    const UltraIMMessage& m = rows[static_cast<size_t>(row)].message;
    if (m.type == UltraIMMessageType::Text || m.type == UltraIMMessageType::SystemEvent) return;
    if (m.attachment.isDownloaded && !m.attachment.localPath.empty()) {
        ShowAttachment(m.attachment.localPath);
        return;
    }
    // A picture comes by itself; its box says so meanwhile.
    if (m.type == UltraIMMessageType::Image || m.type == UltraIMMessageType::Sticker) return;
    if (m.deliveryStatus == UltraIMDeliveryStatus::Sending || m.deliveryStatus == UltraIMDeliveryStatus::Failed) return;
    const std::string name = m.attachment.fileName.empty()
                                 ? std::string(m.type == UltraIMMessageType::Video ? "the video" : "the file")
                                 : m.attachment.fileName;
    SetStatus("Fetching " + name + "\xE2\x80\xA6");
    UltraIM_DownloadAttachment(activeProvider_, m.conversationId, m.messageId,
                               [this, name](const UltraIMResult& r, const UltraIMAttachment& a) {
        if (!r) {
            SetStatus("Could not fetch " + name + ": " + r.message);
            return;
        }
        SetStatus("");
        ShowAttachment(a.localPath);
    });
}

void ChatCubeWindow::ShowAttachment(const std::string& path) {
    if (path.empty()) return;
    MediaViewerWindowOptions options;
    options.browseFolder = false;       // the provider's cache is no album
    if (!viewer_.Show(path, window_.get(), options)) SetStatus("Could not open " + path);
}

// A message's right-click menu. What it offers is what is true of this
// message now: a file to open or save when it has one, and the deletes its
// messenger allows - ChatCube's own, Matrix's and Telegram's differ.
void ChatCubeWindow::ShowMessageMenu(int row, int windowX, int windowY) {
    const auto& rows = messages_->Rows();
    if (!window_ || row < 0 || row >= static_cast<int>(rows.size()) ||
        rows[static_cast<size_t>(row)].kind != MessageRow::Kind::Message)
        return;
    const UltraIMMessage message = rows[static_cast<size_t>(row)].message;
    const bool hasFile = message.type != UltraIMMessageType::Text && message.type != UltraIMMessageType::SystemEvent;
    const bool sent = message.deliveryStatus != UltraIMDeliveryStatus::Sending &&
                      message.deliveryStatus != UltraIMDeliveryStatus::Failed;
    const UltraIMDeleteChoices choices =
        UltraIM_GetDeleteChoices(activeProvider_, message.conversationId, message.messageId);

    messageMenu_ = std::make_shared<UltraCanvasMenu>("cc-message-menu", 0, 0, 220, 0);
    messageMenu_->SetMenuType(MenuType::PopupMenu);
    int entries = 0;
    if (hasFile && (sent || message.attachment.isDownloaded)) {
        messageMenu_->AddItem(MenuItemData::Action(Tr("Open"), [this, row]() { OpenAttachment(row); }));
        messageMenu_->AddItem(MenuItemData::Action(Tr("Save as"), [this, message]() { SaveAttachment(message); }));
        entries += 2;
    }
    if (entries && (choices.forMe || choices.forEveryone)) messageMenu_->AddItem(MenuItemData::Separator());
    if (choices.forMe) {
        messageMenu_->AddItem(MenuItemData::Action(Tr("Delete for me"), [this, message]() {
            DeleteMessage(message, UltraIMDeleteScope::ForMe);
        }));
        ++entries;
    }
    if (choices.forEveryone) {
        messageMenu_->AddItem(MenuItemData::Action(Tr("Delete for everyone"), [this, message]() {
            DeleteMessage(message, UltraIMDeleteScope::ForEveryone);
        }));
        ++entries;
    }
    if (!entries) {
        messageMenu_.reset();
        return;
    }
    PopupElementSettings settings;
    messageMenu_->OpenMenu(Point2Di(windowX, windowY), *window_, settings);
}

void ChatCubeWindow::SaveAttachment(const UltraIMMessage& message) {
    const UltraIMAttachment& a = message.attachment;
    const std::string name = a.fileName.empty() ? Tr("the file") : a.fileName;
    if (a.isDownloaded && !a.localPath.empty()) {
        SaveAttachmentAs(a.localPath, a.fileName);
        return;
    }
    SetStatus(TrF("Fetching {0}\xE2\x80\xA6", name));
    UltraIM_DownloadAttachment(activeProvider_, message.conversationId, message.messageId,
                               [this, name](const UltraIMResult& r, const UltraIMAttachment& fetched) {
        if (!r) {
            SetStatus(TrF("Could not fetch {0}: {1}", name, r.message));
            return;
        }
        SetStatus("");
        SaveAttachmentAs(fetched.localPath, fetched.fileName);
    });
}

void ChatCubeWindow::SaveAttachmentAs(const std::string& path, const std::string& name) {
    if (path.empty()) return;
    const std::string suggested = name.empty() ? PathToUtf8(PathFromUtf8(path).filename()) : name;
    FileDialogOptions opts;
    opts.SetTitle(Tr("Save the file"))
        .SetDefaultFileName(suggested)
        .AddFilter(Tr("All files"), std::vector<std::string>{"*"})
        .SetParentWindow(window_.get());
    UltraCanvasFileLoader::SaveFileDialog(opts, [this, path](DialogResult result, const std::string& target) {
        if (result != DialogResult::OK || target.empty()) return;
        // The bytes as they were received: a copy, nothing converted.
        std::error_code ec;
        std::filesystem::copy_file(PathFromUtf8(path), PathFromUtf8(target),
                                   std::filesystem::copy_options::overwrite_existing, ec);
        SetStatus(ec ? TrF("Could not save to {0}", target) : TrF("Saved to {0}", target));
    });
}

void ChatCubeWindow::DeleteMessage(const UltraIMMessage& message, UltraIMDeleteScope scope) {
    const std::string provider = activeProvider_;
    const auto remove = [this, provider, message, scope]() {
        const UltraIMResult r = UltraIM_DeleteMessage(provider, message.conversationId, message.messageId, scope);
        // The row goes when the provider says the message is gone (MessageDelete).
        SetStatus(r ? std::string() : TrF("Could not delete the message: {0}", r.message));
    };
    if (scope == UltraIMDeleteScope::ForMe) {
        remove();
        return;
    }
    // For everyone cannot be taken back, so it asks first.
    UltraCanvasDialogManager::ShowConfirmation(
        Tr("This message will be removed for everyone in the conversation. This cannot be undone."),
        Tr("Delete for everyone"), [remove](bool confirmed) { if (confirmed) remove(); }, window_.get());
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
    const UltraIMResult r = UltraIM_StartCall(activeProvider_, activeConversation_, video, callId);
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

// ================================================================== settings

void ChatCubeWindow::OpenSettings() {
    if (!settingsWindow_) {
        ChatCubeSettingsWindow::Hooks hooks;
        hooks.applied = [this]() { ApplySettings(); };
        hooks.accounts = [this]() { return Accounts(); };
        hooks.myId = [this]() {
            UltraIMProviderInfo info;
            return UltraIM_GetProviderInfo(providerId_, info) ? info.localUserId : std::string();
        };
        hooks.copyMyId = [this]() { CopyMyId(); };
        hooks.signOut = [this](const std::string& providerId) { SignOutOf(providerId); };
        settingsWindow_ = std::make_unique<ChatCubeSettingsWindow>(settings_, version_, dataFolder_, std::move(hooks));
    }
    settingsWindow_->Show(window_.get());
}

void ChatCubeWindow::ApplySettings() {
    // The send key, in the field's hint and on the send button.
    if (input_) {
        input_->SetPlaceholder(settings_.ctrlEnterSends
                                   ? "Message\xE2\x80\xA6  (Ctrl+Enter sends, Enter for a new line)"
                                   : "Message\xE2\x80\xA6  (Enter sends, Shift+Enter for a new line)");
    }
    if (send_) UpdateAttachmentStrip();     // the button's tooltip names the key
    // Typing: nothing more goes out, and what was shown goes.
    if (!settings_.sendTyping && !activeConversation_.empty() && lastTypingSentMs_ != 0) {
        lastTypingSentMs_ = 0;
        UltraIM_SetTyping(activeProvider_, activeConversation_, false);
    }
    if (!settings_.showTyping && !typingContact_.empty()) {
        typingContact_.clear();
        UpdateHeader();
    }
    // The people on this network: listed again, or put away.
    if (nearbyBox_) {
        if (settings_.showNearby) {
            if (!nearbyBox_->IsVisible() && chatView_ && chatView_->IsVisible()) RefreshNearby();
        } else {
            nearbyBox_->SetVisible(false);
        }
    }
    // Times and dates: the lists written again, the date lines of the open
    // conversation with them.
    SetDateTimeStyle(settings_.clock24, settings_.dateStyle);
    if (messages_ && !activeConversation_.empty()) {
        std::vector<UltraIMMessage> shown;
        for (const MessageRow& row : messages_->Rows()) {
            if (row.kind == MessageRow::Kind::Message) shown.push_back(row.message);
        }
        messages_->SetMessages(shown);
        if (messageList_) {
            messageList_->InvalidateRowHeights();
            messageList_->RequestRedraw();
        }
    }
    if (conversationList_) conversationList_->RequestRedraw();
    if (window_) window_->RequestRedraw();
}

std::vector<ChatCubeSettingsWindow::Account> ChatCubeWindow::Accounts() const {
    std::vector<ChatCubeSettingsWindow::Account> accounts;
    const std::pair<std::string, const char*> networks[] = {
        {providerId_, "ChatCube Direct"}, {matrixProviderId_, "Matrix"}, {telegramProviderId_, "Telegram"}};
    for (const auto& [id, name] : networks) {
        ChatCubeSettingsWindow::Account account;
        account.providerId = id;
        account.name = name;
        UltraIMProviderInfo info;
        if (!UltraIM_GetProviderInfo(id, info)) {
            account.state = "Not in this ChatCube: its module was not found (Plugins/UltraIM).";
            accounts.push_back(account);
            continue;
        }
        const bool direct = id == providerId_;
        switch (info.connectionState) {
            case UltraIMConnectionState::Connected:
                account.state = direct ? "Connected." : "Signed in as " + info.localUserId + ".";
                account.canSignOut = !direct;
                break;
            case UltraIMConnectionState::Connecting:
                account.state = "Signing in\xE2\x80\xA6";
                account.canSignOut = !direct;
                break;
            case UltraIMConnectionState::Reconnecting:
                account.state = "The connection was lost - reconnecting\xE2\x80\xA6";
                account.canSignOut = !direct && !info.localUserId.empty();
                break;
            case UltraIMConnectionState::Error:
                account.state = "Not connected: the last attempt failed.";
                account.canSignOut = !direct && info.hasStoredSession;
                break;
            case UltraIMConnectionState::Disconnected:
                account.state = direct ? "Not connected." : "Not signed in.";
                break;
        }
        if (!info.version.empty()) account.state += "  Module " + info.version + ".";
        accounts.push_back(account);
    }
    return accounts;
}

void ChatCubeWindow::SignOutOf(const std::string& providerId) {
    if (providerId == providerId_) return;      // a ChatCube ID has nothing to sign out of
    UltraIM_SignOut(providerId);
    // Its conversations leave the list, the open one with them.
    std::vector<std::string> gone;
    for (const UltraIMConversation& c : conversations_->Conversations()) {
        if (c.providerId == providerId) gone.push_back(c.conversationId);
    }
    for (const std::string& id : gone) conversations_->Remove(id);
    if (activeProvider_ == providerId && !activeConversation_.empty()) CloseConversationView();
    if (providerId == matrixProviderId_) {
        SetNetworkButton(matrixToggle_, "Matrix", "Matrix account");
        matrixKeysForm_->SetVisible(false);
        SetProviderStatus(matrixProviderId_, "signed out");
    } else if (providerId == telegramProviderId_) {
        SetNetworkButton(telegramToggle_, "Telegram", "Telegram account");
        telegramForm_->SetVisible(false);
        SetProviderStatus(telegramProviderId_, "signed out");
    }
    if (conversationList_) conversationList_->RequestRedraw();
    if (window_) window_->RequestRedraw();
}

// =================================================================== UltraIM

void ChatCubeWindow::OnEvent(const UltraIMEvent& e) {
    if (e.providerId != providerId_ && e.providerId != matrixProviderId_ && e.providerId != telegramProviderId_) return;
    const bool matrix = e.providerId == matrixProviderId_;
    const bool telegram = e.providerId == telegramProviderId_;
    switch (e.type) {
        case UltraIMEventType::MessageReceive:
            if (e.conversationId == activeConversation_) {
                messages_->Upsert(e.message);
                ScrollToEnd();
                UltraIM_MarkRead(e.providerId, activeConversation_);
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
        case UltraIMEventType::ConversationRemove: {
            conversations_->Remove(e.conversationId);
            // Its marks go with it.
            const std::string key = e.providerId + "/" + e.conversationId;
            if (marks_.pinned.erase(key) + marks_.unread.erase(key) > 0) {
                marks_.Save();
                ApplyMarks();
            }
            if (e.conversationId == activeConversation_) CloseConversationView();
            break;
        }
        case UltraIMEventType::PresenceChange:
            conversations_->SetPresence(e.contactId, e.presence);
            if (e.conversationId == activeConversation_) UpdateHeader();
            break;
        case UltraIMEventType::TypingChange:
            if (e.conversationId != activeConversation_ || !settings_.showTyping) break;
            typingContact_ = e.isTyping ? e.contactId : std::string();
            typingUntilMs_ = NowMs() + kTypingShownMs;
            UpdateHeader();
            if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                app->StartTimer(static_cast<unsigned>(kTypingShownMs + 100), false,
                                [this](TimerId) { UpdateHeader(); });
            }
            break;
        case UltraIMEventType::MessageUpdate:
            // An edit, or an attachment's picture that has arrived - of a
            // message that is shown (one further back than the history
            // fetched is not appended to the end).
            if (e.conversationId == activeConversation_ && messages_->Contains(e.message.clientMessageId))
                messages_->Upsert(e.message);
            break;
        case UltraIMEventType::MessageDelete:
            if (e.conversationId == activeConversation_) messages_->Remove(e.message.messageId);
            break;
        case UltraIMEventType::ConnectionStateChange:
            if (settingsWindow_) settingsWindow_->RefreshAccounts();
            if (telegram) {
                if (e.connectionState == UltraIMConnectionState::Connected) {
                    SetProviderStatus(telegramProviderId_, "signed in");
                    SetNetworkButton(telegramToggle_, "Telegram", "Telegram: signed in");
                    RefreshConversations();
                } else if (e.connectionState == UltraIMConnectionState::Error) {
                    SetProviderStatus(telegramProviderId_, e.errorText);
                    telegramNext_->SetDisabled(false);
                    telegramQrButton_->SetDisabled(false);
                } else if (e.connectionState == UltraIMConnectionState::Reconnecting) {
                    SetProviderStatus(telegramProviderId_, e.errorText.empty() ? std::string("reconnecting\xE2\x80\xA6") : e.errorText);
                } else if (e.connectionState == UltraIMConnectionState::Connecting) {
                    // What TDLib is doing while the sign-in waits on it.
                    SetProviderStatus(telegramProviderId_, (e.errorText.empty() ? std::string("signing in") : e.errorText) + "\xE2\x80\xA6");
                } else if (e.connectionState == UltraIMConnectionState::Disconnected) {
                    SetProviderStatus(telegramProviderId_, "signed out");
                    SetNetworkButton(telegramToggle_, "Telegram", "Telegram account");
                }
                break;
            }
            if (matrix) {
                // Matrix speaks for itself on the status line, Direct below.
                if (e.connectionState == UltraIMConnectionState::Connected) {
                    UltraIMProviderInfo info;
                    UltraIM_GetProviderInfo(matrixProviderId_, info);
                    SetProviderStatus(matrixProviderId_, "signed in as " + info.localUserId);
                    SetNetworkButton(matrixToggle_, "Matrix", "Matrix: signed in as " + info.localUserId);
                    RefreshConversations();
                } else if (e.connectionState == UltraIMConnectionState::Error) {
                    SetProviderStatus(matrixProviderId_, e.errorText);
                } else if (e.connectionState == UltraIMConnectionState::Reconnecting) {
                    SetProviderStatus(matrixProviderId_, "connection lost - reconnecting\xE2\x80\xA6");
                } else if (e.connectionState == UltraIMConnectionState::Connecting) {
                    SetProviderStatus(matrixProviderId_, "signing in\xE2\x80\xA6");
                } else if (e.connectionState == UltraIMConnectionState::Disconnected) {
                    SetProviderStatus(matrixProviderId_, "signed out");
                }
                break;
            }
            switch (e.connectionState) {
                case UltraIMConnectionState::Connected:    SetProviderStatus(providerId_, "connected"); break;
                case UltraIMConnectionState::Connecting:   SetProviderStatus(providerId_, "connecting\xE2\x80\xA6"); break;
                case UltraIMConnectionState::Reconnecting: SetProviderStatus(providerId_, "connection lost - reconnecting\xE2\x80\xA6"); break;
                case UltraIMConnectionState::Error:
                    SetProviderStatus(providerId_, "error: " + e.errorText);
                    SetSignInStatus(e.errorText);
                    break;
                case UltraIMConnectionState::Disconnected: SetProviderStatus(providerId_, "signed out"); break;
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
        case UltraIMEventType::AuthStepChange:
            if (telegram) UpdateTelegramForm();
            break;
        case UltraIMEventType::AuthRequire:
            if (telegram) {
                SetProviderStatus(telegramProviderId_, "the session ended - sign in again");
                telegramForm_->SetVisible(true);
                SetNetworkButton(telegramToggle_, "Telegram", "Telegram account");
                UpdateTelegramForm();
                break;
            }
            if (matrix) {
                SetProviderStatus(matrixProviderId_, "the session ended - sign in again");
                matrixForm_->SetVisible(true);
                SetNetworkButton(matrixToggle_, "Matrix", "Matrix account");
                break;
            }
            SetSignInStatus("The connection ended - sign in again.");
            if (hasIdentity_) ShowConnectingPage(true);
            else ShowSignInPage();
            break;
        default:
            break;
    }
}

} // namespace ChatCube
