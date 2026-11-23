// Apps/ChatCube/ui/ChatCubeWindow.cpp
// The ChatCube window: sign-in page, conversations, messages
// Version: 0.3.11 - the feature icons (ApplyFeatureIcons): the + menu, smileys & stickers with
//                   the list of sets, a location from OpenStreetMap, each placed by
//                   Settings > Display > Feature icons
// Version: 0.3.10 - the screenshot button beside the paperclip: an area of the screen into the
//                   composer (UltraCanvasScreenAreaPicker); hidden where the screen is not captured
// Version: 0.3.9 - the composer's text one row high, growing with its text to four rows, then scrolling
// Version: 0.3.8 - the compact layout (a phone, a narrow window): the list and the conversation take
//                  turns, a back arrow and Android's back key return to the list; network buttons only
//                  for the networks this build reaches; the microphone and camera asked for on Android
// Version: 0.3.7 - who is typing: per conversation, in the list's row and the header, named in a group
// Version: 0.3.6 - a network's module loaded on demand, from its button; names of networks not loaded
// Version: 0.3.5 - a Mastodon account (the server, then its sign-in page in the browser); the network
//                  buttons in two rows; @user@server and https://server/@user go to Mastodon
// Version: 0.3.4 - sending in the background, a file's progress in its bubble, a video's preview picture
//                  and length at once, songs played in their bubble, videos played in the chat, filter
//                  chips over the chat (pictures, videos, audio, files, links)
// Version: 0.3.3 - Reply and Edit in a message's menu (Edit also by a double click), a bar over the
//                  field for either; the search field over the chat; voice messages recorded with the
//                  microphone and played in their bubble; video messages recorded over the field
// Version: 0.3.2 - files dropped on the window: on a chat in the list (it opens) or on the open chat,
//                  into the strip; the paperclip's dialog shows every file, not only pictures
// Version: 0.3.1 - a group's whole member count (MemberCount) in its menu and under its name: a Telegram
//                  supergroup lists only its most active members
// Version: 0.3.0 - the chat list's menu (a right-click): read / unread, pinned, calls, the ID, a group's
//                  members, adding, renaming and leaving; a chat deleted; "1 member" in the singular
// Version: 0.2.9 - the relay from the settings (Networks > Relay) instead of a field on the sign-in
//                  page; a Connecting page while ChatCube Direct signs in, with Try again on a failure
// Version: 0.2.8 - a message's right-click menu: its file opened or saved, the message deleted for oneself or
//                  for everyone, as its messenger allows
// Version: 0.2.7 - recovery by the link e-mailed to the ID's address: the window waits for it and signs in
// Version: 0.2.6 - Tr("Recover my ChatCube ID"): the codes sent to the e-mail and phone bring an ID back
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
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeWindow.h"
#include "ChatCubeControls.h"
#include "ChatCubeGroupWindow.h"
#include "ChatCubeProfileWindow.h"
#include "ChatCubeTheme.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasAudioPlayer.h"
#include "UltraCanvasAudioRecorder.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasVideoFileInfo.h"
#include "UltraCanvasVideoPlayerElement.h"
#include "UltraCanvasVideoRecorderElement.h"
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
#include "UltraCanvasUtils.h"
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
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(__ANDROID__)
#include "UltraCanvasAndroidPermissions.h"   // OS/Android, on the Android include path (CMakeLists.txt)
#endif

using namespace UltraCanvas;

namespace ChatCube {

namespace {

constexpr int   kSidebarWidth = 320;
// Narrower than this (logical pixels), the window is compact: the list and
// the open conversation take turns instead of standing side by side. A
// phone held upright is 360 to 430; the side-by-side window needs the
// sidebar and a conversation of about as much again.
constexpr int   kCompactWidth = 640;
constexpr int   kInputHeight = 28;     // the composer's text before its first fit: one row
constexpr int   kInputMaxRows = 4;     // the composer grows to this many rows, then scrolls
constexpr int   kTileSide = 64;         // a staged picture's tile in the composer
constexpr int   kFileTileWidth = 180;   // a staged file's tile: its name and size
constexpr int   kTileClose = 20;        // the x on a tile
constexpr size_t  kMenuMembers = 40;     // members listed in a group's menu before "and N more"
constexpr int64_t kTypingShownMs = 5000;
constexpr int64_t kTypingResendMs = 3000;
constexpr unsigned kScreenshotSettleMs = 350;   // the hidden window has left the screen by then

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// The composer's text: one row high while the message is one line, growing
// with every row the words wrap or break into up to kInputMaxRows, and
// scrolling past that with the text area's own scrollbar. It fits itself
// when its text changes and when the layout gives it a new width (a wider
// window wraps fewer rows).
class ComposerTextArea : public UltraCanvasTextArea {
public:
    using UltraCanvasTextArea::UltraCanvasTextArea;

    void FitToText() {
        if (GetWidth() <= 0.0f) return;
        auto ctx = GetRenderContext();
        if (!ctx) return;
        const TextAreaStyle& st = GetStyle();
        ctx->PushState();
        ctx->SetFontStyle(st.fontStyle);
        ctx->SetFontWeight(FontWeight::Normal);
        const float row = std::max(1.0f, std::floor(static_cast<float>(ctx->GetTextLineHeight("M")) * st.lineHeight));
        ctx->PopState();
        const float content = MeasureContentHeight();
        const float text = std::clamp(content, row, row * static_cast<float>(kInputMaxRows));
        // textPadding above and below, and a pixel of slack so the last row
        // clears the padding (GetTextLineHeight rounds down).
        const float height = std::ceil(text + st.textPadding * 2.0f + 2.0f);
        fittedWidth_ = GetWidth();
        if (std::abs(height - GetHeight()) < 0.5f) return;
        SetElementSize(size.width, CSSLayout::Dimension::Px(height));
    }

    void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override {
        UltraCanvasTextArea::Arrange(finalRect, ctx);
        if (std::abs(GetWidth() - fittedWidth_) >= 0.5f) FitToText();
    }

private:
    float fittedWidth_ = -1.0f;
};

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
        case UltraIMPresence::Online:       return Tr("online");
        case UltraIMPresence::Away:         return Tr("away");
        case UltraIMPresence::DoNotDisturb: return Tr("do not disturb");
        case UltraIMPresence::Offline:      return Tr("offline - messages wait on the relay");
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
      profileWindow_(std::make_unique<ChatCubeProfileWindow>(providerId_)),
      groupWindow_(std::make_unique<ChatCubeGroupWindow>()) {
    SetDateTimeStyle(settings_.clock24, settings_.dateStyle);
    profileWindow_->onSaved = [this](const std::string& text) { SetStatus(text); };
    // The editor's actions that ask first are this window's.
    groupWindow_->onStatus = [this](const std::string& text) { SetStatus(text); };
    groupWindow_->nameOf = [this](const UltraIMConversation& g, const std::string& member) { return MemberName(g, member); };
    groupWindow_->onMessage = [this](const std::string& provider, const std::string& member) {
        MessageMember(provider, member);
        if (window_) window_->RaiseAndFocus();
    };
    groupWindow_->onRemove = [this](const std::string& id, const std::string& member) { RemoveMember(id, member); };
    groupWindow_->onBlock = [this](const std::string& id, const std::string& member) { BlockMember(id, member); };
    groupWindow_->onAddMembers = [this](const std::string& id) { AskToAddMembers(id); };
    groupWindow_->onSetPublic = [this](const std::string& id, bool isPublic) { SetGroupPublic(id, isPublic); };
    groupWindow_->onLeave = [this](const std::string& id) { AskToLeave(id); };
    groupWindow_->onChanged = [this](const UltraIMConversation& c) {
        UpsertConversation(c);
        if (c.conversationId == activeConversation_) UpdateHeader();
    };
}

ChatCubeWindow::~ChatCubeWindow() {
    // Nothing comes back to the window from here on; what is still queued
    // to send goes, then the sending thread ends.
    *alive_ = false;
    sendQueue_.reset();
    previewQueue_.reset();
    if (themeHandler_) UltraCanvasTheme::RemoveChangeHandler(themeHandler_);
    // The tiles go with the window: no picture may be pushed into them after.
    if (!callId_.empty()) UltraIM_SetCallVideoSinks(providerId_, callId_, nullptr, nullptr);
    if (subscription_) UltraIM_Unsubscribe(subscription_);
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        if (callTimer_) app->StopTimer(callTimer_);
        if (recordTimer_) app->StopTimer(recordTimer_);
        if (searchTimer_) app->StopTimer(searchTimer_);
        if (publicTimer_) app->StopTimer(publicTimer_);
        if (screenshotTimer_) app->StopTimer(screenshotTimer_);
    }
    // A recording under way goes with the window, nothing sent.
    if (voiceRecorder_) {
        voiceRecorder_->Stop();
        voiceRecorder_->Close();
    }
    if (voicePlayer_) voicePlayer_->Stop();
    if (videoRecorder_) videoRecorder_->CloseCamera();
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
    OnLook(window_, [](UltraCanvasWindow& w) { w.SetBackgroundColor(L().ground); });
    window_->AddChild(BuildSignInPage());
    window_->AddChild(BuildConnectingPage());
    window_->AddChild(BuildChatView());
    ApplySettings();
    // A phone, or a window made narrow: the compact layout (UpdateLayoutMode).
    window_->onWindowResize = [this](int width, int) { UpdateLayoutMode(width); };
    UpdateLayoutMode(window_->GetConfig().width);
#if defined(__ANDROID__)
    // Android's back key asks the window to close: from a conversation in
    // the compact layout it goes back to the list instead, as every phone app.
    window_->onWindowClosing = [this]() {
        if (compact_ && compactShowsChat_ && chatView_ && chatView_->IsVisible()) {
            ShowConversationList();
            return false;
        }
        return true;
    };
#endif
    // A change of the look while ChatCube runs: the framework's elements
    // follow the theme by themselves, ApplyLook brings ChatCube's own.
    themeHandler_ = UltraCanvasTheme::AddChangeHandler([this](const UltraCanvasTheme&) { ApplyLook(); });
    window_->onWindowClosed = [this]() { if (onClosed) onClosed(); };
    // Files dropped anywhere on the window: the chat list and the open chat
    // take them (DropFiles), whatever element is under the pointer.
    window_->InstallEventFilter("chatcube-drop-files",
                                [this](const UCEvent& event) { return DropFiles(event); },
                                {UCEventType::Drop});

    subscription_ = UltraIM_Subscribe([this](const UltraIMEvent& e) { OnEvent(e); });

    UltraIMProviderInfo info;
    if (!UltraIM_GetProviderInfo(providerId_, info)) {
        ShowSignInPage();
        SetSignInStatus(Tr("The ChatCube Direct module was not found (Plugins/UltraIM). Reinstall ChatCube."));
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
                                    Tr("Chat directly with the people you know - end-to-end encrypted"),
                                    11.0f, &Look::inkMuted));
    signInPage_->AddSpacer(10);

    // The form, on a card.
    auto card = std::make_shared<UltraCanvasContainer>("cc-signin-card");
    card->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    card->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    card->size.width = CSSLayout::Dimension::Px(kFormWidth + 48);
    OnLook(card, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().panel);
        c.SetBorders(1.0f, L().line, 14.0f);
    });
    card->SetPadding(22, 24, 18, 24);

    name_ = MakeInput("cc-signin-name", Tr("Your name, as your contacts will see it"), static_cast<int>(kFormWidth));
    name_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(name_);
    // To recover the account with: kept in the key vault and on the relay,
    // for the owner alone.
    email_ = MakeInput("cc-signin-email", Tr("E-mail address, to recover your account"), static_cast<int>(kFormWidth));
    email_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(email_);
    phone_ = MakeInput("cc-signin-phone", Tr("Phone number with + and country code, for recovery"),
                       static_cast<int>(kFormWidth));
    phone_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(phone_);
    // Asked by the provider at a start: the password, then the code.
    unlockPassword_ = MakeInput("cc-signin-password", Tr("Your ChatCube password"), static_cast<int>(kFormWidth));
    unlockPassword_->SetInputType(TextInputType::Password);
    unlockPassword_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(unlockPassword_);
    unlockCode_ = MakeInput("cc-signin-code", Tr("The 6-digit code UltraAuthenticator shows"), static_cast<int>(kFormWidth));
    unlockCode_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(unlockCode_);
    localNetwork_ = std::make_shared<UltraCanvasCheckbox>("cc-signin-lan", 0, 0, static_cast<long>(kFormWidth), 22,
                                                          Tr("Also find people on this network"));
    localNetwork_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    OnLook(localNetwork_, [](UltraCanvasCheckbox& box) {
        CheckboxVisualStyle lanStyle = CheckboxVisualStyle::FromTheme(UltraCanvasTheme::Current(), box.GetVisualStyle());
        lanStyle.base.fontSize = kTextFont;                 // the size of the page's other text
        lanStyle.base.textColor = L().ink;
        lanStyle.boxColor = L().panel;
        lanStyle.boxBorderColor = L().lineStrong;
        lanStyle.boxHoverColor = L().hover;
        lanStyle.checkmarkColor = L().accent;
        lanStyle.checkmarkHoverColor = L().accent;
        box.SetVisualStyle(lanStyle);
    });
    card->AddChild(localNetwork_);

    // How signing in on this computer is protected - a new ID's choice;
    // Edit profile changes it later.
    protectionRow_ = std::make_shared<UltraCanvasContainer>("cc-signin-protection-row");
    protectionRow_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    protectionRow_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    protectionRow_->AddChild(MakeLabel("cc-signin-protection-caption", Tr("Signing in on this computer"), 9.0f, &Look::inkMuted));
    protection_ = std::make_shared<UltraCanvasDropdown>("cc-signin-protection", 0, 0, kFormWidth, kControlHeight);
    protection_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    // The theme's colours (ChatCube's), in the page's size.
    OnLook(protection_, [](UltraCanvasDropdown& drop) {
        DropdownStyle dropStyle = DropdownStyle::FromTheme(UltraCanvasTheme::Current(), drop.GetStyle());
        dropStyle.fontSize = kTextFont;
        dropStyle.cornerRadius = kControlRadius;
        dropStyle.itemHeight = 28.0f;
        dropStyle.maxItemWidth = kFormWidth + 60;
        drop.SetStyle(dropStyle);
    });
    // In UltraIMSignInProtection's order.
    protection_->AddItem(Tr("No password - this computer signs in by itself"));
    protection_->AddItem(Tr("A password"));
    protection_->AddItem(Tr("A code from UltraAuthenticator"));
    protection_->AddItem(Tr("A password and a code from UltraAuthenticator"));
    protection_->SetSelectedIndex(0, false);
    protection_->SetAccessibleName(Tr("Signing in on this computer"));
    protection_->onSelectionChanged = [this](int, const DropdownItem&) { UpdateSignInForm(); };
    protectionRow_->AddChild(protection_);
    card->AddChild(protectionRow_);
    newPasswordRow_ = MakeRow("cc-signin-new-password", 8);
    const int halfForm = static_cast<int>((kFormWidth - 8) / 2);
    newPassword_ = MakeInput("cc-signin-new", Tr("Password"), halfForm);
    newPassword_->SetInputType(TextInputType::Password);
    newPasswordRow_->AddChild(newPassword_);
    repeatPassword_ = MakeInput("cc-signin-repeat", Tr("The same again"), halfForm);
    repeatPassword_->SetInputType(TextInputType::Password);
    repeatPassword_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    newPasswordRow_->AddChild(repeatPassword_);
    card->AddChild(newPasswordRow_);

    // Recovering an ID made on another computer: the e-mail address it has;
    // the relay e-mails a link to it.
    recoverAddress_ = MakeInput("cc-recover-address", Tr("Your ID's e-mail address"), static_cast<int>(kFormWidth));
    recoverAddress_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(recoverAddress_);

    signIn_ = MakeButton("cc-signin-go", Tr("Create my ChatCube ID"), static_cast<int>(kFormWidth), true);
    signIn_->SetOnClick([this]() { SignIn(); });
    card->AddChild(signIn_);
    recoverToggle_ = MakeButton("cc-recover-toggle", Tr("Recover my ChatCube ID"), static_cast<int>(kFormWidth));
    StyleButton(recoverToggle_, false, true);
    recoverToggle_->SetOnClick([this]() { ToggleRecoveryForm(); });
    card->AddChild(recoverToggle_);

    signInStatus_ = MakeLabel("cc-signin-status", "", kTextFont, &Look::inkMuted);
    signInStatus_->SetWrap(TextWrap::WrapWord);
    signInStatus_->SetAlignment(TextAlignment::Center);
    signInStatus_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    card->AddChild(signInStatus_);
    signInPage_->AddChild(card);

    auto note = MakeLabel(
        "cc-signin-note",
        Tr("Your ChatCube ID is a key made on this computer. Messages are end-to-end encrypted; "
           "the relay only connects you and keeps messages for people who are offline. "
           "Which relay is set in Settings > Networks > Relay."),
        9.0f, &Look::inkFaint);
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
    connectingTitle_ = MakeLabel("cc-connecting-title", Tr("Connecting\xE2\x80\xA6"), 13.0f);
    connectingPage_->AddChild(connectingTitle_);
    connectingRelay_ = MakeLabel("cc-connecting-relay", "", 10.0f, &Look::inkMuted);
    connectingPage_->AddChild(connectingRelay_);
    connectingStatus_ = MakeLabel("cc-connecting-status", "", kTextFont, &Look::inkMuted);
    connectingStatus_->SetWrap(TextWrap::WrapWord);
    connectingStatus_->SetAlignment(TextAlignment::Center);
    connectingStatus_->size.width = CSSLayout::Dimension::Px(kFormWidth + 48);
    connectingPage_->AddChild(connectingStatus_);

    // On a failure: again, or the Settings window to change the relay.
    connectingButtons_ = MakeRow("cc-connecting-buttons", 8);
    connectingButtons_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    auto retry = MakeButton("cc-connecting-retry", Tr("Try again"), 150, true);
    retry->SetOnClick([this]() { SignIn(); });
    connectingButtons_->AddChild(retry);
    auto settings = MakeButton("cc-connecting-settings", Tr("Settings"), 150);
    settings->SetOnClick([this]() { OpenSettings(); });
    connectingButtons_->AddChild(settings);
    connectingButtons_->SetVisible(false);
    connectingPage_->AddChild(connectingButtons_);

    connectingPage_->SetVisible(false);
    return connectingPage_;
}

std::shared_ptr<UltraCanvasContainer> ChatCubeWindow::BuildSidebar() {
    auto sidebar = std::make_shared<UltraCanvasContainer>("cc-sidebar");
    sidebar_ = sidebar;
    sidebar->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    sidebar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    sidebar->size.width = CSSLayout::Dimension::Px(kSidebarWidth);
    sidebar->SetPadding(10, 8, 8, 8);
    OnLook(sidebar, [](UltraCanvasContainer& c) {
        c.SetBorderRight(1, L().line);
        c.SetBackgroundColor(L().sidebar);
    });

    // Who I am: the logo, my ID in groups of four, and a copy button.
    auto idRow = MakeRow("cc-myid-row", 8);
    idRow->AddChild(MakeLogo("cc-myid-logo", 30));
    auto idColumn = std::make_shared<UltraCanvasContainer>("cc-myid-column");
    idColumn->layout.SetFlexColumn().SetFlexGap(1).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    idColumn->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    idColumn->AddChild(MakeLabel("cc-myid-caption", Tr("My ChatCube ID"), 8.5f, &Look::inkFaint));
    myId_ = MakeLabel("cc-myid", "", 9.5f, &Look::ink);
    myId_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    idColumn->AddChild(myId_);
    idRow->AddChild(idColumn);
    auto copy = MakeIconButton("cc-myid-copy", "copy");
    copy->SetTooltip(Tr("Copy my ChatCube ID"));
    copy->SetOnClick([this]() { CopyMyId(); });
    idRow->AddChild(copy);
    // The gear opens the Settings window, as UltraFiler's does.
    auto gear = MakeIconButton("cc-settings", "settings");
    gear->SetTooltip(Tr("Settings"));
    gear->SetOnClick([this]() { OpenSettings(); });
    idRow->AddChild(gear);
    auto profile = MakeIconButton("cc-myid-profile", "profile");
    profile->SetTooltip(Tr("Edit profile: name, e-mail, phone and how you sign in"));
    profile->SetOnClick([this]() { EditProfile(); });
    idRow->AddChild(profile);
    sidebar->AddChild(idRow);

    // Adding someone: a pill, and a round plus.
    auto addRow = MakeRow("cc-add-row", 6);
    contactId_ = MakeInput("cc-add-id", Tr("Add: a ChatCube ID, @name:server, @telegram or @user@server"),
                           kSidebarWidth - 16 - kIconButton - 6, true);
    contactId_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    contactId_->onEnterPressed = [this](const std::string&) { AddContact(); return true; };
    addRow->AddChild(contactId_);
    auto add = MakeIconButton("cc-add", "plus", "", kIconButton, true);
    add->SetTooltip(Tr("Add the contact"));
    add->SetOnClick([this]() { AddContact(); });
    addRow->AddChild(add);
    sidebar->AddChild(addRow);

    // The doors, two rows of two: a new group and a Matrix account, a
    // Telegram and a Mastodon account (each name whole at half the sidebar).
    auto doors = MakeRow("cc-doors", 6);
    auto moreDoors = MakeRow("cc-doors-2", 6);
    const int doorWidth = (kSidebarWidth - 16 - 6) / 2;
    auto newGroup = MakeIconButton("cc-new-group", "group", Tr("Group"), doorWidth, false, false);
    newGroup->SetTooltip(Tr("New group"));
    newGroup->SetOnClick([this]() { ToggleGroupForm(); });
    newGroup->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    doors->AddChild(newGroup);
    matrixToggle_ = MakeIconButton("cc-matrix", "matrix", "Matrix", doorWidth, false, false);
    matrixToggle_->SetTooltip(Tr("Matrix account"));
    matrixToggle_->SetOnClick([this]() { ToggleMatrixForm(); });
    matrixToggle_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    doors->AddChild(matrixToggle_);
    telegramToggle_ = MakeIconButton("cc-telegram", "telegram", "Telegram", doorWidth, false, false);
    telegramToggle_->SetTooltip(Tr("Telegram account"));
    telegramToggle_->SetOnClick([this]() { ToggleTelegramForm(); });
    telegramToggle_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    moreDoors->AddChild(telegramToggle_);
    mastodonToggle_ = MakeIconButton("cc-mastodon", "mastodon", "Mastodon", doorWidth, false, false);
    mastodonToggle_->SetTooltip(Tr("Mastodon account"));
    mastodonToggle_->SetOnClick([this]() { ToggleMastodonForm(); });
    mastodonToggle_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    moreDoors->AddChild(mastodonToggle_);
    // A third row: the networks signed in to with a server and an account.
    auto lastDoors = MakeRow("cc-doors-3", 6);
    for (AccountForm* f : {&mattermost_, &zulip_}) {
        f->toggle = MakeIconButton("cc-" + f->providerId, f->providerId, f->name, doorWidth, false, false);
        f->toggle->SetTooltip(TrF("{0} account", f->name));
        f->toggle->SetOnClick([this, f]() { ToggleAccountForm(*f); });
        f->toggle->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        lastDoors->AddChild(f->toggle);
    }
    // A fourth row: Signal.
    auto signalDoors = MakeRow("cc-doors-4", 6);
    signalToggle_ = MakeIconButton("cc-signal", "signal", "Signal", doorWidth, false, false);
    signalToggle_->SetTooltip(TrF("{0} account", std::string("Signal")));
    signalToggle_->SetOnClick([this]() { ToggleSignalForm(); });
    signalDoors->AddChild(signalToggle_);
    xmpp_.toggle = MakeIconButton("cc-xmpp", "xmpp", xmpp_.name, doorWidth, false, false);
    xmpp_.toggle->SetTooltip(TrF("{0} account", xmpp_.name));
    xmpp_.toggle->SetOnClick([this]() { ToggleAccountForm(xmpp_); });
    xmpp_.toggle->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    signalToggle_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    signalDoors->AddChild(xmpp_.toggle);
    sidebar->AddChild(doors);
    sidebar->AddChild(moreDoors);
    sidebar->AddChild(lastDoors);
    sidebar->AddChild(signalDoors);
    // A fifth row: IRC.
    auto ircDoors = MakeRow("cc-doors-5", 6);
    irc_.toggle = MakeIconButton("cc-irc", "irc", irc_.name, doorWidth, false, false);
    irc_.toggle->SetTooltip(TrF("{0} account", irc_.name));
    irc_.toggle->SetOnClick([this]() { ToggleAccountForm(irc_); });
    ircDoors->AddChild(irc_.toggle);
    sidebar->AddChild(ircDoors);
    // Only the networks this build reaches: on Android, Telegram (TDLib) and
    // Signal (signal-cli) are not there; the other button of a row then takes
    // the row.
    const std::pair<std::string, std::shared_ptr<UltraCanvasButton>> networkDoors[] = {
        {matrixProviderId_, matrixToggle_}, {telegramProviderId_, telegramToggle_},
        {mastodonProviderId_, mastodonToggle_}, {mattermost_.providerId, mattermost_.toggle},
        {zulip_.providerId, zulip_.toggle}, {signalProviderId_, signalToggle_},
        {xmpp_.providerId, xmpp_.toggle}, {irc_.providerId, irc_.toggle}};
    for (const auto& door : networkDoors) {
        if (door.second && !NetworkAvailable(door.first)) door.second->SetVisible(false);
    }
    for (const auto& row : {moreDoors, lastDoors, signalDoors, ircDoors}) {
        bool anyShown = false;
        for (const auto& child : row->GetChildren()) anyShown = anyShown || child->IsVisible();
        if (!anyShown) row->SetVisible(false);
    }

    // A Matrix account next to ChatCube Direct.
    matrixForm_ = std::make_shared<UltraCanvasContainer>("cc-matrix-form");
    matrixForm_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    matrixForm_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    matrixServer_ = MakeInput("cc-matrix-server", Tr("Homeserver, e.g. matrix.org"), kSidebarWidth - 16);
    matrixForm_->AddChild(matrixServer_);
    matrixUser_ = MakeInput("cc-matrix-user", Tr("User name, or @name:server"), kSidebarWidth - 16);
    matrixForm_->AddChild(matrixUser_);
    matrixPassword_ = MakeInput("cc-matrix-password", Tr("Password"), kSidebarWidth - 16);
    matrixPassword_->SetInputType(TextInputType::Password);
    matrixPassword_->onEnterPressed = [this](const std::string&) { SignInToMatrix(false); return true; };
    matrixForm_->AddChild(matrixPassword_);
    matrixSignIn_ = MakeButton("cc-matrix-go", Tr("Sign in to Matrix"), kSidebarWidth - 16, true);
    matrixSignIn_->SetOnClick([this]() { SignInToMatrix(false); });
    matrixForm_->AddChild(matrixSignIn_);
    matrixForm_->SetVisible(false);
    sidebar->AddChild(matrixForm_);
    // Signed in: the keys to the encrypted rooms, to keep or to move.
    matrixKeysForm_ = MakeRow("cc-matrix-keys", 6);
    auto exportKeys = MakeButton("cc-matrix-export", Tr("Export keys"), (kSidebarWidth - 22) / 2);
    exportKeys->SetOnClick([this]() { ExportMatrixKeys(); });
    matrixKeysForm_->AddChild(exportKeys);
    auto importKeys = MakeButton("cc-matrix-import", Tr("Import keys"), (kSidebarWidth - 22) / 2);
    importKeys->SetOnClick([this]() { ImportMatrixKeys(); });
    matrixKeysForm_->AddChild(importKeys);
    matrixKeysForm_->SetVisible(false);
    sidebar->AddChild(matrixKeysForm_);

    // A Mastodon account: the server; the sign-in itself is its own page,
    // opened in the browser.
    mastodonForm_ = std::make_shared<UltraCanvasContainer>("cc-mastodon-form");
    mastodonForm_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    mastodonForm_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    mastodonServer_ = MakeInput("cc-mastodon-server", Tr("Server, e.g. mastodon.social"), kSidebarWidth - 16);
    mastodonServer_->onEnterPressed = [this](const std::string&) { SignInToMastodon(false); return true; };
    mastodonForm_->AddChild(mastodonServer_);
    mastodonSignIn_ = MakeButton("cc-mastodon-go", Tr("Sign in in the browser"), kSidebarWidth - 16, true);
    mastodonSignIn_->SetOnClick([this]() { SignInToMastodon(false); });
    mastodonForm_->AddChild(mastodonSignIn_);
    mastodonForm_->SetVisible(false);
    sidebar->AddChild(mastodonForm_);

    // Signal: this computer becomes a linked device of the phone's account -
    // a QR code the phone's Signal scans, through signal-cli.
    signalForm_ = std::make_shared<UltraCanvasContainer>("cc-signal-form");
    signalForm_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    signalForm_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    signalDaemon_ = MakeInput("cc-signal-daemon", Tr("signal-cli daemon (empty: ChatCube starts one)"), kSidebarWidth - 16);
    signalForm_->AddChild(signalDaemon_);
    signalLink_ = MakeButton("cc-signal-go", Tr("Link with my phone"), kSidebarWidth - 16, true);
    signalLink_->SetOnClick([this]() { SignInToSignal(false); });
    signalForm_->AddChild(signalLink_);
    signalHint_ = MakeLabel("cc-signal-hint", "", 9.0f, &Look::inkMuted);
    signalHint_->SetWrap(TextWrap::WrapWord);
    signalHint_->size.width = CSSLayout::Dimension::Px(static_cast<float>(kSidebarWidth - 16));
    signalHint_->SetVisible(false);
    signalForm_->AddChild(signalHint_);
    constexpr int signalQrSide = 200;
    signalQr_ = std::make_shared<UltraCanvasQRCode>("cc-signal-qr", 0, 0, signalQrSide, signalQrSide);
    signalQr_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Center);
    signalQr_->size.width = CSSLayout::Dimension::Px(static_cast<float>(signalQrSide));
    signalQr_->size.height = CSSLayout::Dimension::Px(static_cast<float>(signalQrSide));
    signalQr_->SetForegroundColor(Colors::Black);
    signalQr_->SetBackgroundColor(Colors::White);
    signalQr_->SetQuietZoneModules(2);
    signalQr_->SetVisible(false);
    signalForm_->AddChild(signalQr_);
    signalForm_->SetVisible(false);
    sidebar->AddChild(signalForm_);

    BuildAccountForm(mattermost_, *sidebar, Tr("Server, e.g. chat.example.org"), Tr("User name or e-mail address"));
    BuildAccountForm(zulip_, *sidebar, Tr("Organization, e.g. chat.zulip.org"), Tr("E-mail address"));
    BuildAccountForm(xmpp_, *sidebar, Tr("Server (empty: found from your address)"),
                     Tr("Your address, e.g. erin@example.org"));
    BuildAccountForm(irc_, *sidebar, Tr("Server, e.g. irc.libera.chat"), Tr("Nickname"));
    irc_.password->SetPlaceholder(Tr("Password (only for a registered nickname)"));

    // A Telegram account: the phone number, then the code, then the
    // two-step password if there is one - one field at a time; or a QR code
    // the phone's Telegram app scans, in place of the number and the code.
    telegramForm_ = std::make_shared<UltraCanvasContainer>("cc-telegram-form");
    telegramForm_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    telegramForm_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto next = [this](const std::string&) { SignInToTelegram(false); return true; };
    telegramPhone_ = MakeInput("cc-telegram-phone", Tr("Phone number, e.g. +44 20 7946 0000"), kSidebarWidth - 16);
    telegramPhone_->onEnterPressed = next;
    telegramForm_->AddChild(telegramPhone_);
    telegramApiId_ = MakeInput("cc-telegram-api-id", Tr("API ID (my.telegram.org; empty if built in)"), kSidebarWidth - 16);
    telegramForm_->AddChild(telegramApiId_);
    telegramApiHash_ = MakeInput("cc-telegram-api-hash", Tr("API hash"), kSidebarWidth - 16);
    telegramForm_->AddChild(telegramApiHash_);
    // Where the code went, in the form itself: the status line moves on,
    // and a code that went to another Telegram app is easy to miss.
    telegramHint_ = MakeLabel("cc-telegram-hint", "", 9.0f, &Look::inkMuted);
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
    telegramCode_ = MakeInput("cc-telegram-code", Tr("The code Telegram sent"), kSidebarWidth - 16);
    telegramCode_->onEnterPressed = next;
    telegramForm_->AddChild(telegramCode_);
    // Telegram's other way of sending the code (SMS, a call), when it offers one.
    telegramResend_ = MakeButton("cc-telegram-resend", Tr("Send it another way"), kSidebarWidth - 16);
    telegramResend_->SetOnClick([this]() {
        telegramResend_->SetDisabled(true);
        SetProviderStatus(telegramProviderId_, Tr("asking Telegram for the code another way\xE2\x80\xA6"));
        UltraIM_ResendAuthCode(telegramProviderId_, [this](const UltraIMResult& r) {
            telegramResend_->SetDisabled(false);
            if (!r) SetProviderStatus(telegramProviderId_, r.message);
            UpdateTelegramForm();
        });
    });
    telegramForm_->AddChild(telegramResend_);
    telegramPassword_ = MakeInput("cc-telegram-password", Tr("Two-step verification password"), kSidebarWidth - 16);
    telegramPassword_->SetInputType(TextInputType::Password);
    telegramPassword_->onEnterPressed = next;
    telegramForm_->AddChild(telegramPassword_);
    telegramName_ = MakeInput("cc-telegram-name", Tr("Your name (a new account)"), kSidebarWidth - 16);
    telegramName_->onEnterPressed = next;
    telegramForm_->AddChild(telegramName_);
    telegramNext_ = MakeButton("cc-telegram-go", Tr("Send me the code"), kSidebarWidth - 16, true);
    telegramNext_->SetOnClick([this]() { SignInToTelegram(false); });
    telegramForm_->AddChild(telegramNext_);
    // The QR code instead - of the number, or of a code that never came.
    telegramQrButton_ = MakeButton("cc-telegram-qr-go", Tr("Sign in with a QR code instead"), kSidebarWidth - 16);
    telegramQrButton_->SetOnClick([this]() { SignInToTelegramByQrCode(); });
    telegramForm_->AddChild(telegramQrButton_);
    telegramForm_->SetVisible(false);
    sidebar->AddChild(telegramForm_);

    groupForm_ = std::make_shared<UltraCanvasContainer>("cc-group-form");
    groupForm_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    groupForm_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    groupTitle_ = MakeInput("cc-group-title", Tr("Group name"), kSidebarWidth - 16);
    groupForm_->AddChild(groupTitle_);
    groupMembers_ = MakeInput("cc-group-members", Tr("Members' ChatCube IDs, separated by commas"), kSidebarWidth - 16);
    groupMembers_->onEnterPressed = [this](const std::string&) { CreateGroup(); return true; };
    groupForm_->AddChild(groupMembers_);
    auto create = MakeButton("cc-group-create", Tr("Create group"), kSidebarWidth - 16, true);
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

    // Public groups anyone may join, not joined yet (filled by RefreshPublicGroups).
    publicBox_ = std::make_shared<UltraCanvasContainer>("cc-public-groups");
    publicBox_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    publicBox_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    publicBox_->SetVisible(false);
    sidebar->AddChild(publicBox_);

    conversations_ = std::make_shared<ConversationListModel>();
    conversationList_ = std::make_shared<UltraCanvasListView>("cc-conversations", 0, 0, kSidebarWidth - 16, 300);
    conversationList_->SetModel(conversations_);
    conversationList_->SetDelegate(std::make_shared<ConversationDelegate>());
    conversationList_->SetRowHeight(64);
    OnLook(conversationList_, [](UltraCanvasListView& list) {
        // The theme's scrollbars; the rows paint their own rounded selection and hover.
        ListViewStyle listStyle = ListViewStyle::FromTheme(UltraCanvasTheme::Current(), list.GetStyle());
        listStyle.backgroundColor = L().sidebar;
        listStyle.selectionBackgroundColor = Colors::Transparent;
        listStyle.hoverBackgroundColor = Colors::Transparent;
        list.SetStyle(listStyle);
        list.SetBackgroundColor(L().sidebar);
        list.SetBorders(0, Colors::Transparent);
    });
    conversationList_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    conversationList_->onSelectionChanged = [this](const std::vector<int>& rows) {
        if (fillingList_ || rows.empty()) return;
        const auto& list = conversations_->Conversations();
        if (rows.front() >= 0 && rows.front() < static_cast<int>(list.size()))
            OpenConversation(list[static_cast<size_t>(rows.front())].conversationId);
    };
    // Compact, back on the list: a tap on the conversation still selected
    // changes no selection, and opens it again from here.
    conversationList_->onItemClicked = [this](int row) {
        if (!compact_ || compactShowsChat_) return;
        const auto& list = conversations_->Conversations();
        if (row >= 0 && row < static_cast<int>(list.size()) &&
            list[static_cast<size_t>(row)].conversationId == activeConversation_)
            OpenConversation(activeConversation_);
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
    pane_ = pane;
    pane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    pane->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    OnLook(pane, [](UltraCanvasContainer& c) { c.SetBackgroundColor(L().ground); });

    // ----- header: the avatar, the name and its state, the call buttons -----
    auto header = MakeRow("cc-header", 10);
    header->SetPadding(9, 14, 9, 14);
    OnLook(header, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().panel);
        c.SetBorderBottom(1, L().line);
    });
    // The compact layout's way back to the list (hidden side by side).
    back_ = MakeIconButton("cc-back", "back", "", kIconButton, false, true);
    back_->SetTooltip(Tr("Back to the chats"));
    back_->SetOnClick([this]() { ShowConversationList(); });
    back_->SetVisible(false);
    header->AddChild(back_);
    // Its colours are the conversation's (UpdateHeader).
    headerAvatar_ = MakeLabel("cc-header-avatar", "", 12.0f);
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
    subtitle_ = MakeLabel("cc-subtitle", "", 9.0f, &Look::inkMuted);
    titles->AddChild(subtitle_);
    header->AddChild(titles);
    voiceCall_ = MakeIconButton("cc-call", "call", "", kIconButton + 2);
    voiceCall_->SetTooltip(Tr("Call"));
    voiceCall_->SetDisabled(true);
    voiceCall_->SetOnClick([this]() { StartCall(false); });
    header->AddChild(voiceCall_);
    videoCall_ = MakeIconButton("cc-video", "video", "", kIconButton + 2);
    videoCall_->SetTooltip(Tr("Video call"));
    videoCall_->SetDisabled(true);
    videoCall_->SetOnClick([this]() { StartCall(true); });
    header->AddChild(videoCall_);
    pane->AddChild(header);

    // ----- search: a field over the chat, the list showing what matches -----
    searchRow_ = MakeRow("cc-search-row", 8);
    searchRow_->SetPadding(6, 14, 6, 14);
    OnLook(searchRow_, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().panel);
        c.SetBorderBottom(1, L().line);
    });
    searchInput_ = MakeInput("cc-search", Tr("Search in this chat"), 260, true);
    searchInput_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    searchInput_->onTextChanged = [this](const std::string&) {
        if (!settingSearch_) ScheduleSearch();
    };
    searchInput_->onEnterPressed = [this](const std::string&) {
        RunSearch();
        return true;
    };
    searchRow_->AddChild(searchInput_);
    searchInfo_ = MakeLabel("cc-search-info", "", 9.0f, &Look::inkMuted);
    searchInfo_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    searchRow_->AddChild(searchInfo_);
    auto searchClear = MakeIconButton("cc-search-clear", "close", "", kIconButton, false, true);
    searchClear->SetTooltip(Tr("Show the whole chat again"));
    searchClear->SetOnClick([this]() { ClearSearch(true); });
    searchRow_->AddChild(searchClear);
    searchRow_->SetVisible(false);
    pane->AddChild(searchRow_);
    // Under it, the kinds: the list shows only pictures, videos, sound,
    // files or messages with a link (with the words, when some are typed).
    auto filterRow = MakeRow("cc-filter-row", 6);
    filterRow->SetPadding(0, 14, 6, 14);
    OnLook(filterRow, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().panel);
        c.SetBorderBottom(1, L().line);
    });
    const std::pair<UltraIMSearchKind, std::string> kinds[] = {
        {UltraIMSearchKind::AnyKind, Tr("All")},      {UltraIMSearchKind::Images, Tr("Pictures")},
        {UltraIMSearchKind::Videos, Tr("Videos")},    {UltraIMSearchKind::Audio, Tr("Audio")},
        {UltraIMSearchKind::Files, Tr("Files")},      {UltraIMSearchKind::Links, Tr("Links")}};
    for (const auto& [kind, words] : kinds) {
        auto chip = MakeButton("cc-filter-" + std::to_string(static_cast<int>(kind)), words, 82,
                               kind == UltraIMSearchKind::AnyKind);
        chip->size.height = CSSLayout::Dimension::Px(26);
        chip->SetOnClick([this, k = kind]() { SetSearchKind(k); });
        filterRow->AddChild(chip);
        filterChips_.push_back({kind, chip});
    }
    // Shown with the search row (OpenConversation / CloseConversationView).
    filterRow->SetVisible(false);
    filterRow_ = filterRow;
    pane->AddChild(filterRow);

    // ----- messages -----
    auto empty = std::make_shared<UltraCanvasContainer>("cc-empty-box");
    empty->layout.SetFlexColumn().SetFlexGap(10)
        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    empty->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto emptyLogo = MakeLogo("cc-empty-logo", 64);
    emptyLogo->SetOpacity(0.35f);
    empty->AddChild(emptyLogo);
    empty->AddChild(MakeLabel("cc-empty-title", Tr("Your conversations"), 13.0f, &Look::inkMuted));
    auto hint = MakeLabel("cc-empty",
                          Tr("Pick a conversation on the left, or add a contact by the ChatCube ID they give you."),
                          10.0f, &Look::inkFaint);
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
    OnLook(messageList_, [](UltraCanvasListView& list) {
        ListViewStyle messageStyle = ListViewStyle::FromTheme(UltraCanvasTheme::Current(), list.GetStyle());
        messageStyle.backgroundColor = L().ground;
        messageStyle.selectionBackgroundColor = Colors::Transparent;   // bubbles show their own state
        messageStyle.hoverBackgroundColor = Colors::Transparent;
        list.SetStyle(messageStyle);
        list.SetBackgroundColor(L().ground);
        list.SetBorders(0, Colors::Transparent);
    });
    messageList_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    messageList_->SetVisible(false);
    // A click on a picture, or on a file's bubble, opens it.
    messageList_->onCellClicked = [this](int row, int, const Point2Di& posInCell) {
        Rect2Di media;
        if (!bubbles_->MediaRect(messages_.get(), row, media) || !media.Contains(posInCell)) return;
        const auto& rows = messages_->Rows();
        // Sound plays in its bubble, a video in the chat; anything else opens.
        if (row < 0 || row >= static_cast<int>(rows.size())) return;
        const UltraIMMessage& clicked = rows[static_cast<size_t>(row)].message;
        if (IsAudioMessage(clicked)) ToggleVoicePlayback(row);
        else if (clicked.type == UltraIMMessageType::Video) PlayVideoInChat(row);
        else OpenAttachment(row);
    };
    // A double click on one of our own messages changes it.
    messageList_->onCellDoubleClicked = [this](int row, int, const Point2Di&) {
        const auto& rows = messages_->Rows();
        if (row < 0 || row >= static_cast<int>(rows.size()) || rows[static_cast<size_t>(row)].kind != MessageRow::Kind::Message)
            return;
        const UltraIMMessage& m = rows[static_cast<size_t>(row)].message;
        if (CanEdit(m)) StartEdit(m);
    };
    messageList_->onContextMenu = [this](int row, const UCEvent& event) {
        ShowMessageMenu(row, event.pointerWindow.x, event.pointerWindow.y);
    };
    pane->AddChild(messageList_);

    // ----- a video played in the chat: a player over the field, and a x -----
    videoPlayerPanel_ = std::make_shared<UltraCanvasContainer>("cc-video-player-panel");
    videoPlayerPanel_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    videoPlayerPanel_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    videoPlayerPanel_->SetPadding(8, 14, 10, 14);
    OnLook(videoPlayerPanel_, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().panel);
        c.SetBorderTop(1, L().line);
    });
    auto playerBar = MakeRow("cc-video-player-bar", 8);
    videoPlayerTitle_ = MakeLabel("cc-video-player-title", "", 10.5f);
    videoPlayerTitle_->SetFontWeight(FontWeight::Bold);
    videoPlayerTitle_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    playerBar->AddChild(videoPlayerTitle_);
    auto playerClose = MakeIconButton("cc-video-player-close", "close", "", kIconButton, false, true);
    playerClose->SetTooltip(Tr("Close the video"));
    playerClose->SetOnClick([this]() { CloseVideoPlayer(); });
    playerBar->AddChild(playerClose);
    videoPlayerPanel_->AddChild(playerBar);
    videoPlayer_ = CreateVideoPlayer("cc-video-player", 0, 0, 560, 315);
    videoPlayer_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    videoPlayer_->size.height = CSSLayout::Dimension::Px(315);
    videoPlayerPanel_->AddChild(videoPlayer_);
    videoPlayerPanel_->SetVisible(false);
    pane->AddChild(videoPlayerPanel_);

    // ----- a video message: the camera over the field while it is recorded -----
    videoNotePanel_ = MakeRow("cc-video-note", 12);
    videoNotePanel_->SetPadding(10, 14, 10, 14);
    videoNotePanel_->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    OnLook(videoNotePanel_, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().panel);
        c.SetBorderTop(1, L().line);
    });
    videoRecorder_ = CreateVideoRecorder("cc-video-recorder", 0, 0, 360, 270);
    videoRecorder_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    videoRecorder_->size.width = CSSLayout::Dimension::Px(360);
    videoRecorder_->size.height = CSSLayout::Dimension::Px(270);
    // Its callbacks come from the camera's threads.
    videoRecorder_->onRecordStarted = [this]() {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent())
            app->PostToUIThread([this]() {
                videoNoteStartedMs_ = NowMs();
                videoNoteReady_ = false;
                videoNoteSend_->SetDisabled(true);
                videoNoteText_->SetText(Tr("Recording - press the red button again to stop."));
            });
    };
    videoRecorder_->onSaved = [this](const std::string& path) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent())
            app->PostToUIThread([this, path]() {
                videoNotePath_ = path;
                videoNoteSeconds_ = videoNoteStartedMs_ > 0 ? (NowMs() - videoNoteStartedMs_) / 1000.0 : 0.0;
                std::error_code ec;
                videoNoteReady_ = std::filesystem::is_regular_file(PathFromUtf8(path), ec);
                videoNoteSend_->SetDisabled(!videoNoteReady_);
                videoNoteText_->SetText(videoNoteReady_ ? TrF("Recorded {0} - send it, or record again.",
                                                              FormatDuration(videoNoteSeconds_))
                                                        : Tr("The recording could not be saved."));
                if (window_) window_->RequestRedraw();
            });
    };
    videoRecorder_->onError = [this](const std::string& message) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent())
            app->PostToUIThread([this, message]() { videoNoteText_->SetText(TrF("Camera: {0}", message)); });
    };
    videoNotePanel_->AddChild(videoRecorder_);
    auto videoNoteSide = std::make_shared<UltraCanvasContainer>("cc-video-note-side");
    videoNoteSide->layout.SetFlexColumn().SetFlexGap(10);
    videoNoteSide->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    videoNoteSide->AddChild(MakeLabel("cc-video-note-title", Tr("Video message"), 12.0f));
    videoNoteText_ = MakeLabel("cc-video-note-text", "", 10.0f, &Look::inkMuted);
    videoNoteText_->SetWrap(TextWrap::WrapWord);
    videoNoteSide->AddChild(videoNoteText_);
    auto videoNoteButtons = MakeRow("cc-video-note-buttons", 8);
    videoNoteSend_ = MakeIconButton("cc-video-note-send", "send", Tr("Send"), 96, true);
    videoNoteSend_->SetOnClick([this]() { SendVideoMessage(); });
    videoNoteButtons->AddChild(videoNoteSend_);
    auto videoNoteCancel = MakeIconButton("cc-video-note-cancel", "close", Tr("Cancel"), 104);
    videoNoteCancel->SetOnClick([this]() { CloseVideoRecorder(false); });
    videoNoteButtons->AddChild(videoNoteCancel);
    videoNoteSide->AddChild(videoNoteButtons);
    videoNotePanel_->AddChild(videoNoteSide);
    videoNotePanel_->SetVisible(false);
    pane->AddChild(videoNotePanel_);

    // ----- the composer: a rounded field with the send button inside -----
    auto inputRow = std::make_shared<UltraCanvasContainer>("cc-input-row");
    inputRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::End);
    inputRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    inputRow->SetPadding(10, 14, 10, 14);
    OnLook(inputRow, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().panel);
        c.SetBorderTop(1, L().line);
    });
    // The rounded field: the strip of what goes with the next message
    // (hidden while empty) over the row with the paperclip, the text and
    // the send button.
    auto composer = std::make_shared<UltraCanvasContainer>("cc-composer");
    composer->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    composer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    OnLook(composer, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().well);
        c.SetBorders(1.0f, L().line, 22.0f);
    });
    composer->SetPadding(5, 6, 5, 6);
    // Replying to a message, or changing one of ours: which, and a x.
    composerBar_ = MakeRow("cc-composer-bar", 8);
    composerBar_->SetPadding(4, 8, 0, 10);
    auto barWords = std::make_shared<UltraCanvasContainer>("cc-composer-bar-words");
    barWords->layout.SetFlexColumn().SetFlexGap(1);
    barWords->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    composerBarTitle_ = MakeLabel("cc-composer-bar-title", "", 9.5f, &Look::accent);
    composerBarTitle_->SetFontWeight(FontWeight::Bold);
    barWords->AddChild(composerBarTitle_);
    composerBarText_ = MakeLabel("cc-composer-bar-text", "", 9.5f, &Look::inkMuted);
    barWords->AddChild(composerBarText_);
    composerBar_->AddChild(barWords);
    auto barClose = MakeIconButton("cc-composer-bar-close", "close", "", kTileClose + 8, false, true);
    barClose->SetTooltip(Tr("Cancel (Escape)"));
    barClose->SetOnClick([this]() { ClearComposerMode(); });
    composerBar_->AddChild(barClose);
    composerBar_->SetVisible(false);
    composer->AddChild(composerBar_);
    attachmentStrip_ = std::make_shared<UltraCanvasContainer>("cc-attachments");
    attachmentStrip_->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(6)
        .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    attachmentStrip_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    attachmentStrip_->SetPadding(4, 8, 0, 8);
    attachmentStrip_->SetVisible(false);
    composer->AddChild(attachmentStrip_);
    // A voice message being recorded: in place of the field.
    recordBar_ = MakeRow("cc-record-bar", 8);
    recordBar_->SetPadding(2, 4, 2, 8);
    recordBar_->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto recordCancel = MakeIconButton("cc-record-cancel", "close", "", kIconButton, false, true);
    recordCancel->SetTooltip(Tr("Throw the recording away"));
    recordCancel->SetOnClick([this]() { FinishVoiceRecording(false); });
    recordBar_->AddChild(recordCancel);
    recordText_ = MakeLabel("cc-record-text", "", 11.0f, &Look::danger);
    recordText_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    recordBar_->AddChild(recordText_);
    auto recordSend = MakeIconButton("cc-record-send", "send", "", kIconButton, true);
    recordSend->SetTooltip(Tr("Send the voice message"));
    recordSend->SetOnClick([this]() { FinishVoiceRecording(true); });
    recordBar_->AddChild(recordSend);
    recordBar_->SetVisible(false);
    composer->AddChild(recordBar_);
    auto composerRow = std::make_shared<UltraCanvasContainer>("cc-composer-row");
    composerRow_ = composerRow;
    composerRow->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::End);
    composerRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // The + first: the features put behind it (Settings > Display >
    // Feature icons), then those in the field.
    plus_ = MakeIconButton("cc-plus", "plus", "", kIconButton);
    plus_->SetTooltip(Tr("More: pictures, screenshots, places and more"));
    plus_->SetOnClick([this]() { OpenFeatureMenu(); });
    composerRow->AddChild(plus_);
    attach_ = MakeIconButton("cc-attach", "attach", "", kIconButton);
    attach_->SetTooltip(Tr("Send a picture, a video or a file"));
    attach_->SetDisabled(true);
    attach_->SetOnClick([this]() { AttachFile(); });
    composerRow->AddChild(attach_);
    screenshot_ = MakeIconButton("cc-screenshot", "screenshot", "", kIconButton);
    screenshot_->SetTooltip(Tr("Insert a screenshot: choose a window or drag an area"));
    screenshot_->SetDisabled(true);
    screenshot_->SetOnClick([this]() { TakeScreenshot(); });
    composerRow->AddChild(screenshot_);
    // Smileys & stickers: the face opens the set chosen last, the narrow
    // arrow beside it lists the sets
    emoji_ = MakeIconButton("cc-emoji", "smile", "", kIconButton);
    emoji_->SetTooltip(Tr("Smileys & stickers"));
    emoji_->SetOnClick([this]() { OpenEmojiGrid(); });
    composerRow->AddChild(emoji_);
    emojiSets_ = MakeIconButton("cc-emoji-sets", "chevron-down", "", 16);
    emojiSets_->SetIconSize(12, 12);
    emojiSets_->GetStyle().cornerRadius = 6.0f;
    emojiSets_->SetTooltip(Tr("Choose the smileys or stickers"));
    emojiSets_->SetOnClick([this]() { OpenEmojiSets(); });
    composerRow->AddChild(emojiSets_);
    emojiPicker_.onEmoji = [this](const std::string& emoji) { InsertIntoMessage(emoji); };
    emojiPicker_.onSticker = [this](const std::string& path) { StageSticker(path); };
    emojiPicker_.onSetChosen = [this](const std::string& key) {
        settings_.emojiSet = key;
        settings_.Save();
    };
    location_ = MakeIconButton("cc-location", "location", "", kIconButton);
    location_->SetTooltip(Tr("Insert a location (OpenStreetMap)"));
    location_->SetOnClick([this]() { OpenLocation(); });
    composerRow->AddChild(location_);
    auto composerText = std::make_shared<ComposerTextArea>("cc-input", 0, 0, 300, kInputHeight);
    input_ = composerText;
    input_->SetEditingMode(TextAreaEditingMode::PlainText);
    input_->SetWordWrap(true);
    input_->SetShowLineNumbers(false);
    input_->SetFontSize(11.0f);
    // Centred on the buttons while it is one row; taller, it sets the row's
    // height and the buttons stay at the bottom, by the last line.
    input_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Center);
    OnLook(input_, [](UltraCanvasTextArea& area) {
        TextAreaStyle st = area.GetStyle();
        st.backgroundColor = L().well;
        st.borderColor = Colors::Transparent;
        st.fontColor = L().ink;
        st.placeholderColor = L().inkFaint;
        st.cursorColor = L().ink;
        st.selectionColor = Color(L().accent.r, L().accent.g, L().accent.b, 90);
        st.currentLineHighlightColor = Colors::Transparent;
        st.scrollbarCornerRadius = 4.0f;
        area.SetStyle(st);
        area.SetBackgroundColor(L().well);
        area.SetBorders(0, Colors::Transparent);
    });
    // Enter sends; Shift+Enter is left to the text area, which breaks the
    // line - or, as the Settings window may say, Ctrl+Enter sends and Enter
    // is the text area's. Ctrl+V (Shift+Insert) with a picture or a file on
    // the clipboard puts it in the strip; with text, the area pastes it as usual.
    input_->onBeforeKeyDown = [this](const UCEvent& e) {
        // Escape gives up a reply or an edit.
        if (e.virtualKey == UCKeys::Escape && (!replyTo_.messageId.empty() || !editing_.clientMessageId.empty())) {
            ClearComposerMode();
            return true;
        }
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
    std::weak_ptr<ComposerTextArea> fitText = composerText;
    input_->SetOnTextChanged([this, fitText](const std::string& text) {
        if (auto area = fitText.lock()) area->FitToText();
        if (activeConversation_.empty() || text.empty() || !settings_.sendTyping) return;
        const int64_t now = NowMs();
        if (now - lastTypingSentMs_ < kTypingResendMs) return;
        lastTypingSentMs_ = now;
        UltraIM_SetTyping(activeProvider_, activeConversation_, true);
    });
    composerRow->AddChild(input_);
    recordVideo_ = MakeIconButton("cc-record-video", "video-message", "", kIconButton);
    recordVideo_->SetTooltip(Tr("Record a video message"));
    recordVideo_->SetDisabled(true);
    recordVideo_->SetOnClick([this]() { OpenVideoRecorder(); });
    composerRow->AddChild(recordVideo_);
    recordVoice_ = MakeIconButton("cc-record-voice", "mic", "", kIconButton);
    recordVoice_->SetTooltip(Tr("Record a voice message"));
    recordVoice_->SetDisabled(true);
    recordVoice_->SetOnClick([this]() { StartVoiceRecording(); });
    composerRow->AddChild(recordVoice_);
    send_ = MakeIconButton("cc-send", "send", "", kIconButton, true);
    send_->SetOnClick([this]() { SendCurrentMessage(); });
    composerRow->AddChild(send_);
    composer->AddChild(composerRow);
    inputRow->AddChild(composer);
    ApplyFeatureIcons();
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
    OnLook(callBar_, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().callBar);
        c.SetBorderBottom(1, L().line);
    });
    callText_ = MakeLabel("cc-call-text", "", 11.0f);
    callText_->SetFontWeight(FontWeight::Bold);
    callText_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    callBar_->AddChild(callText_);
    callAccept_ = MakeIconButton("cc-call-accept", "accept", Tr("Accept"), 96, true);
    callAccept_->SetOnClick([this]() { AcceptCall(); });
    callBar_->AddChild(callAccept_);
    callCamera_ = MakeIconButton("cc-call-camera", "camera-off", Tr("Camera off"), 116, false, false);
    callCamera_->SetOnClick([this]() { ToggleCamera(); });
    callBar_->AddChild(callCamera_);
    callMute_ = MakeIconButton("cc-call-mute", "mic-off", Tr("Mute"), 86, false, false);
    callMute_->SetOnClick([this]() { ToggleMute(); });
    callBar_->AddChild(callMute_);
    callEnd_ = MakeIconButton("cc-call-end", "hangup", Tr("Hang up"), 104, true);
    OnLook(callEnd_, [](UltraCanvasButton& b) {
        ButtonStyle st = b.GetStyle();
        st.normalColor = L().danger;
        st.hoverColor = Darker(L().danger, 24);
        st.pressedColor = Darker(L().danger, 40);
        st.normalTextColor = st.hoverTextColor = st.pressedTextColor = InkOn(L().danger);
        b.SetStyle(st);
    });
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

    status_ = MakeLabel("cc-status", "", 9.0f, &Look::inkMuted);
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
    connectingTitle_->SetText(failed ? Tr("Could not connect") : Tr("Connecting\xE2\x80\xA6"));
    connectingRelay_->SetText(host.empty() ? Tr("the local network") : host);
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

void ChatCubeWindow::UpdateLayoutMode(int windowWidth) {
    if (windowWidth <= 0) return;
    const bool compact = windowWidth < kCompactWidth;
    if (compact == compact_ && sidebar_) {
        return;
    }
    compact_ = compact;
    // Made compact with a conversation open: it stays in sight.
    compactShowsChat_ = compact && !activeConversation_.empty();
    ApplyCompactPanes();
}

void ChatCubeWindow::ApplyCompactPanes() {
    if (!sidebar_ || !pane_) return;
    if (!compact_) {
        // Side by side: the sidebar at its width, the conversation beside it.
        sidebar_->size.width = CSSLayout::Dimension::Px(kSidebarWidth);
        sidebar_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        sidebar_->SetVisible(true);
        pane_->SetVisible(true);
        if (back_) back_->SetVisible(false);
    } else {
        // One at a time, the one shown across the whole window.
        const bool chat = compactShowsChat_ && !activeConversation_.empty();
        sidebar_->size.width = CSSLayout::Dimension::Auto();
        sidebar_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        sidebar_->SetVisible(!chat);
        pane_->SetVisible(chat);
        if (back_) back_->SetVisible(true);
    }
    sidebar_->InvalidateLayout();
    pane_->InvalidateLayout();
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ShowConversationList() {
    if (!compact_ || !compactShowsChat_) return;
    // What the conversation had under way stays with it, as when another is
    // opened; it is still the active one, shown again by a tap on its row.
    FinishVoiceRecording(false);
    CloseVideoRecorder(false);
    CloseVideoPlayer();
    compactShowsChat_ = false;
    ApplyCompactPanes();
}

bool ChatCubeWindow::HaveMediaPermission(bool camera, std::function<void()> retry) {
#if defined(__ANDROID__)
    // Each permission the system's own way: the prompt the first time,
    // remembered after (the manifest declares both, android/AndroidManifest.xml).
    const char* needed = nullptr;
    if (!AndroidPermissions::IsGranted(AndroidPermissions::RecordAudio)) needed = AndroidPermissions::RecordAudio;
    else if (camera && !AndroidPermissions::IsGranted(AndroidPermissions::Camera)) needed = AndroidPermissions::Camera;
    if (!needed) return true;
    const bool asksCamera = needed == AndroidPermissions::Camera;
    AndroidPermissions::Request(needed, [this, asksCamera, retry](bool granted) {
        if (granted) {
            if (retry) retry();    // asks for the next one, if there is one
        } else {
            SetStatus(asksCamera ? Tr("ChatCube may not use the camera: allow it in the system's settings for ChatCube.")
                                 : Tr("ChatCube may not use the microphone: allow it in the system's settings for ChatCube."));
        }
    });
    return false;
#else
    (void)camera;
    (void)retry;
    return true;
#endif
}

bool ChatCubeWindow::NetworkAvailable(const std::string& providerId) const {
    if (providerId.empty()) return false;
    UltraIMProviderInfo info;
    if (UltraIM_GetProviderInfo(providerId, info)) return true;     // registered (or loaded) already
    const std::vector<std::string> modules = UltraIM_ListProviderModules();
    return std::find(modules.begin(), modules.end(), providerId) != modules.end();
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

std::vector<std::pair<std::string, const char*>> ChatCubeWindow::Networks() const {
    return {{providerId_, "ChatCube"}, {matrixProviderId_, "Matrix"}, {telegramProviderId_, "Telegram"},
            {mastodonProviderId_, "Mastodon"}, {mattermost_.providerId, "Mattermost"}, {zulip_.providerId, "Zulip"},
            {signalProviderId_, "Signal"}, {xmpp_.providerId, "XMPP"}, {irc_.providerId, "IRC"}};
}

std::string ChatCubeWindow::NetworkName(const std::string& providerId) const {
    for (const auto& network : Networks()) {
        if (network.first == providerId) return network.second;
    }
    UltraIMProviderInfo info;
    if (UltraIM_GetProviderInfo(providerId, info) && !info.displayName.empty()) return info.displayName;
    return providerId;
}

bool ChatCubeWindow::LoadNetwork(const std::string& providerId, UltraIMProviderInfo& info) {
    // A network's module is loaded the first time it is asked for (UltraIM
    // loads at start only those in use): its button opens its form.
    const bool wasLoaded = UltraIM_GetProviderInfo(providerId, info);
    if (!wasLoaded && !UltraIM_LoadProvider(providerId)) return false;
    if (!UltraIM_GetProviderInfo(providerId, info)) return false;
    // Telegram's keys come from the relay, asked for at start when Telegram
    // was loaded then: now, when it is loaded only now.
    if (!wasLoaded && providerId == telegramProviderId_ && telegramRelayApiId_.empty())
        FetchTelegramCredentials([this]() { UpdateTelegramForm(); });
    return true;
}

void ChatCubeWindow::RenderStatus() {
    if (!status_) return;
    std::string line = actionStatus_;
    // The networks in a fixed order, each under the name the sidebar uses.
    for (const auto& network : Networks()) {
        auto it = providerStatus_.find(network.first);
        if (it == providerStatus_.end()) continue;
        if (!line.empty()) line += " \xC2\xB7 ";       // a middle dot
        line += TrF("{0}: {1}", network.second, it->second);
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
    recoverToggle_->SetText(!recovering ? Tr("Recover my ChatCube ID")
                            : recoveryId_.empty() ? Tr("Make a new ChatCube ID instead")
                                                  : Tr("Start over - send a new link"));
    if (recovering) signIn_->SetText(recoveryId_.empty() ? Tr("E-mail me a link") : Tr("Check now"));
    else if (creating) signIn_->SetText(Tr("Create my ChatCube ID"));
    else if (unlockStep_ == UltraIMAuthStep::Password || unlockStep_ == UltraIMAuthStep::Code) signIn_->SetText(Tr("Unlock"));
    else signIn_->SetText(Tr("Sign in"));
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ToggleRecoveryForm() {
    // While a link is on its way, back to the address (a new link); from
    // there, or from making an ID, between the two.
    StopRecoveryPolling();
    if (recovering_ && !recoveryId_.empty()) {
        recoveryId_.clear();
        SetSignInStatus(Tr("Ask for a new link - the one before stops working when it expires."));
        UpdateSignInForm();
        return;
    }
    recovering_ = !recovering_;
    recoveryId_.clear();
    SetSignInStatus(recovering_ ? Tr("Your ID comes back from your relay, if recovery was turned on in Edit profile: "
                                     "it e-mails a link to your ID's address.")
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
    if (!recoveryId_.empty()) return PollRecovery();        // Tr("Check now")
    // The link, e-mailed to the address - if an ID with recovery on has it.
    const std::string address = Trim(recoverAddress_->GetText());
    if (server.empty()) return SetSignInStatus(Tr("Set the relay your ID used in Settings > Networks > Relay."));
    if (address.find('@') == std::string::npos) return SetSignInStatus(Tr("Type your ID's e-mail address."));
    signIn_->SetDisabled(true);
    SetSignInStatus(Tr("Asking the relay\xE2\x80\xA6"));
    UltraIM_StartRecovery(providerId_, server, address,
                          [this](const UltraIMResult& r, const UltraIMRecoveryChallenge& challenge) {
        signIn_->SetDisabled(false);
        if (!r) return SetSignInStatus(r.message);
        recoveryId_ = challenge.recoveryId;
        SetSignInStatus(Tr("If that address belongs to an ID with recovery on, a link is on its way to it. Open it "
                           "and confirm - ChatCube signs in here by itself. (Not there? Look in the spam folder.)"));
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
        SetSignInStatus(Tr("Your ChatCube ID is back on this computer."));
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
            SetSignInStatus(Tr("Enter the name your contacts will see."));
            return;
        }
        if (credentials.email.empty() || credentials.phoneNumber.empty()) {
            SetSignInStatus(Tr("Enter your e-mail address and phone number: they are how you get your account back."));
            return;
        }
        if (UltraIM_ProtectionHasPassword(chosen)) {
            password = newPassword_->GetText();
            if (password.size() < 8) {
                SetSignInStatus(Tr("Choose a password of 8 characters or more."));
                return;
            }
            if (password != repeatPassword_->GetText()) {
                SetSignInStatus(Tr("The two passwords are not the same."));
                return;
            }
        }
    } else if (unlockStep_ == UltraIMAuthStep::Password) {
        credentials.password = unlockPassword_->GetText();
        if (credentials.password.empty()) {
            SetSignInStatus(Tr("Type your ChatCube password."));
            return;
        }
    } else if (unlockStep_ == UltraIMAuthStep::Code) {
        credentials.otpCode = Trim(unlockCode_->GetText());
        if (credentials.otpCode.empty()) {
            SetSignInStatus(Tr("Type the code UltraAuthenticator shows for ChatCube."));
            return;
        }
    }
    // No relay: the local network only (the provider says so when there is
    // no relay remembered either).
    credentials.localNetwork = localNetwork_->IsChecked();
    signIn_->SetDisabled(true);
    // Signing in has a page of its own: the form is not shown while it runs.
    SetSignInStatus(unlockStep_ == UltraIMAuthStep::Password ? Tr("Opening the key vault\xE2\x80\xA6")
                    : !hasIdentity_ ? Tr("Making your ChatCube ID\xE2\x80\xA6")
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
        SetProviderStatus(providerId_, Tr("connected"));
        if (created) FinishCreatedProtection(chosen, password);
        RefreshConversations();
        RefreshNearby();
        // The public groups now, and again every few minutes: new ones appear.
        RefreshPublicGroups();
        if (auto* app = UltraCanvasApplicationBase::GetCurrent(); app && !publicTimer_)
            publicTimer_ = app->StartTimer(5 * 60 * 1000, true, [this](TimerId) { RefreshPublicGroups(); });
        UltraIMProviderInfo matrixInfo;
        if (UltraIM_GetProviderInfo(matrixProviderId_, matrixInfo) && matrixInfo.hasStoredSession &&
            matrixInfo.connectionState == UltraIMConnectionState::Disconnected)
            SignInToMatrix(true);
        UltraIMProviderInfo mastodonInfo;
        if (UltraIM_GetProviderInfo(mastodonProviderId_, mastodonInfo) && mastodonInfo.hasStoredSession &&
            mastodonInfo.connectionState == UltraIMConnectionState::Disconnected)
            SignInToMastodon(true);
        UltraIMProviderInfo signalInfo;
        if (UltraIM_GetProviderInfo(signalProviderId_, signalInfo) && signalInfo.hasStoredSession &&
            signalInfo.connectionState == UltraIMConnectionState::Disconnected)
            SignInToSignal(true);
        for (AccountForm* f : {&mattermost_, &zulip_, &xmpp_, &irc_}) {
            UltraIMProviderInfo stored;
            if (UltraIM_GetProviderInfo(f->providerId, stored) && stored.hasStoredSession &&
                stored.connectionState == UltraIMConnectionState::Disconnected)
                SignInToAccount(*f, true);
        }
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
        SetStatus(Tr("Add ChatCube to UltraAuthenticator to finish: scan the code in Edit profile."));
        profileWindow_->Show(window_.get(), chosen, password);
    };
    if (!UltraIM_ProtectionHasPassword(chosen)) {
        authenticator();
        return;
    }
    UltraIMProtectionChange change;
    change.protection = UltraIMSignInProtection::Password;
    change.newPassword = password;
    SetStatus(Tr("Encrypting your ID with your password\xE2\x80\xA6"));
    UltraIM_SetSignInProtection(providerId_, change, [this, authenticator](const UltraIMResult& r) {
        if (!r) {
            SetStatus(TrF("The password could not be set: {0} - set it in Edit profile.", r.message));
            return;
        }
        SetStatus(Tr("Your ID is protected by your password: ChatCube asks for it at every start."));
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
    // (The open one too, from the list a compact window shows in its place.)
    if (!active || (compact_ && !compactShowsChat_))
        list.push_back(MenuItemData::Action(Tr("Open"), [this, id]() { OpenConversation(id); }));
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
    if (c.isGroup) who.push_back(MenuItemData::Action(Tr("Group settings\xE2\x80\xA6"), [this, id]() { OpenGroupEditor(id); }));
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
        const long long total = MemberCount(c);
        long long listed = 0;
        for (const std::string& member : c.memberIds) {
            if (listed == static_cast<long long>(kMenuMembers)) break;
            ++listed;
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
            // Blocking is ChatCube's, and the owner's.
            if (!self && provider == providerId_ && !c.ownerId.empty() && c.ownerId == me) {
                actions.push_back(MenuItemData::Action(Tr("Block"), [this, id, member]() { BlockMember(id, member); }));
            }
            std::string name = self ? Tr("You") : MemberName(c, member);
            if (!c.ownerId.empty() && member == c.ownerId) name = TrF("{0} (owner)", name);
            members.push_back(MenuItemData::Submenu(name, actions));
        }
        // A big group: the first ones, and a line saying how many more - also
        // those the network did not list (a supergroup's members past its first page).
        if (total > listed && listed > 0) {
            MenuItemData more = MenuItemData::Action(TrN("and {n} more", "and {n} more", total - listed), []() {});
            more.enabled = false;
            members.push_back(more);
        }
        if (!members.empty()) who.push_back(MenuItemData::Submenu(TrF("Members ({0})", total), members));
        if (manage) {
            who.push_back(MenuItemData::Action(Tr("Add members"), [this, id]() { AskToAddMembers(id); }));
            who.push_back(MenuItemData::Action(Tr("Rename group"), [this, id]() { AskToRenameGroup(id); }));
        }
        // A ChatCube group's owner: listed for everyone, or for its members alone.
        if (provider == providerId_ && !c.ownerId.empty() && c.ownerId == me) {
            const bool makePublic = !c.isPublic;
            who.push_back(MenuItemData::Action(makePublic ? Tr("Make public") : Tr("Make private"),
                                               [this, id, makePublic]() { SetGroupPublic(id, makePublic); }));
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

// ---- Who is typing ------------------------------------------------------------
// Per conversation, each person typing and until when it is shown: a network
// says "typing" again every few seconds while it lasts, and not every one
// says when it stops - so it goes kTypingShownMs after the last word of it,
// or when that person's message arrives.

void ChatCubeWindow::SetTyping(const std::string& conversationId, const std::string& contactId, bool typing) {
    if (conversationId.empty() || contactId.empty()) return;
    auto it = typing_.find(conversationId);
    if (typing) {
        typing_[conversationId][contactId] = NowMs() + kTypingShownMs;
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            app->StartTimer(static_cast<unsigned>(kTypingShownMs + 100), false,
                            [this](TimerId) { ExpireTyping(); });
        }
    } else {
        if (it == typing_.end() || !it->second.erase(contactId)) return;
        if (it->second.empty()) typing_.erase(it);
    }
    ShowTyping(conversationId);
}

void ChatCubeWindow::ExpireTyping() {
    const int64_t now = NowMs();
    std::vector<std::string> changed;
    for (auto it = typing_.begin(); it != typing_.end();) {
        const size_t before = it->second.size();
        for (auto who = it->second.begin(); who != it->second.end();) {
            if (who->second <= now) who = it->second.erase(who);
            else ++who;
        }
        if (it->second.size() != before) changed.push_back(it->first);
        if (it->second.empty()) it = typing_.erase(it);
        else ++it;
    }
    for (const std::string& id : changed) ShowTyping(id);
}

void ChatCubeWindow::ShowTyping(const std::string& conversationId) {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    conversations_->SetTyping(conversationId, c ? TypingText(*c) : std::string());
    if (conversationId == activeConversation_) UpdateHeader();
}

std::string ChatCubeWindow::TypingText(const UltraIMConversation& c) const {
    auto it = typing_.find(c.conversationId);
    if (it == typing_.end() || it->second.empty()) return std::string();
    // A chat with one person: just that they are typing - their name is above it.
    if (!c.isGroup) return Tr("typing\xE2\x80\xA6");
    std::vector<std::string> names;
    for (const auto& who : it->second) names.push_back(MemberName(c, who.first));
    std::sort(names.begin(), names.end());
    if (names.size() == 1) return TrF("{0} is typing\xE2\x80\xA6", names[0]);
    if (names.size() == 2) return TrF("{0} and {1} are typing\xE2\x80\xA6", names[0], names[1]);
    return TrF("{0} and others are typing\xE2\x80\xA6", names[0]);
}

std::string ChatCubeWindow::MemberName(const UltraIMConversation& group, const std::string& memberId) const {
    const auto named = group.memberNames.find(memberId);
    if (named != group.memberNames.end() && !named->second.empty()) return named->second;
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

void ChatCubeWindow::OpenGroupEditor(const std::string& conversationId) {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (!c || !c->isGroup || !window_) return;
    UltraIMConversation group = *c;
    if (group.providerId.empty()) group.providerId = providerId_;
    UltraIMProviderInfo info;
    UltraIM_GetProviderInfo(group.providerId, info);
    groupWindow_->Show(window_.get(), group, info.localUserId, group.providerId == providerId_);
}

void ChatCubeWindow::BlockMember(const std::string& conversationId, const std::string& memberId) {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (!c) return;
    const std::string provider = c->providerId.empty() ? providerId_ : c->providerId;
    const std::string name = MemberName(*c, memberId);
    const std::string question = TrF("Block {0}? They are taken out of {1} and cannot join it again until you "
                                     "unblock them.", name, c->title);
    UltraCanvasDialogManager::ShowConfirmation(question, Tr("Block"),
                                               [this, provider, conversationId, memberId, name](bool yes) {
        if (!yes) return;
        UltraIM_BlockGroupMember(provider, conversationId, memberId, [this, name](const UltraIMResult& r) {
            SetStatus(r ? TrF("{0} is blocked.", name) : TrF("Could not block {0}: {1}", name, r.message));
            groupWindow_->RefreshBlocked();
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
    FinishVoiceRecording(false);
    CloseVideoRecorder(false);
    StopVoicePlayback();
    CloseVideoPlayer();
    ClearComposerMode();
    ClearSearch(false);
    searchRow_->SetVisible(false);
    filterRow_->SetVisible(false);
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
        SetStatus(Tr("Your ChatCube ID is on the clipboard - send it to the people who should reach you."));
}

void ChatCubeWindow::AddContact() {
    const std::string id = Trim(contactId_->GetText());
    if (id.empty()) return;
    AddContactById(id);
}

void ChatCubeWindow::AddContactById(const std::string& id) {
    // More than one signed-in network takes this address (@user: Telegram and
    // Mattermost; an e-mail: Zulip and Mattermost): which one is asked.
    std::vector<std::pair<std::string, std::string>> connected;     // provider, name
    for (const std::string& provider : ProvidersOfAddress(id)) {
        UltraIMProviderInfo info;
        if (UltraIM_GetProviderInfo(provider, info) && info.connectionState == UltraIMConnectionState::Connected)
            connected.push_back({provider, info.displayName.empty() ? provider : info.displayName});
    }
    if (connected.size() > 1 && window_) {
        networkMenu_ = std::make_shared<UltraCanvasMenu>("cc-network-menu", 0, 0, 220, 0);
        networkMenu_->SetMenuType(MenuType::PopupMenu);
        networkMenu_->AddItem(MenuItemData::Header(TrF("Add {0} - on which network?", id)));
        for (const auto& [provider, name] : connected) {
            const std::string chosen = provider;
            networkMenu_->AddItem(MenuItemData::Action(name, [this, chosen, id]() { AddContactOn(chosen, id); }));
        }
        PopupElementSettings settings;
        networkMenu_->OpenMenu(Point2Di(16, 88), *window_, settings);
        return;
    }
    // Several networks could take it and none is signed in: say which.
    const std::vector<std::string> candidates = ProvidersOfAddress(id);
    if (connected.empty() && candidates.size() > 1) {
        std::string names;
        for (const std::string& provider : candidates) {
            names += (names.empty() ? "" : ", ") + NetworkName(provider);
        }
        SetStatus(TrF("Sign in to one of these first: {0}", names));
        return;
    }
    AddContactOn(ProviderOfAddress(id), id);
}

void ChatCubeWindow::AddContactOn(const std::string& provider, const std::string& address) {
    // An xmpp: link is the address after it.
    const std::string id = address.rfind("xmpp:", 0) == 0  ? address.substr(5)
                           : address.rfind("irc:", 0) == 0 ? address.substr(4)
                                                           : address;
    SetStatus(Tr("Adding contact\xE2\x80\xA6"));
    UltraIM_AddContact(provider, id, [this](const UltraIMResult& r, const UltraIMConversation& c) {
        if (!r) {
            SetStatus(TrF("Could not add the contact: {0}", r.message));
            return;
        }
        contactId_->SetText("");
        conversations_->Upsert(c);
        SetStatus(TrF("Added {0}", c.title));
        OpenConversation(c.conversationId);
        RefreshNearby();
    });
}

void ChatCubeWindow::ToggleMatrixForm() {
    UltraIMProviderInfo info;
    if (!LoadNetwork(matrixProviderId_, info)) {
        SetStatus(Tr("The Matrix module was not found (Plugins/UltraIM)."));
        return;
    }
    // Signed in, the form is the keys' buttons; signed out, the sign-in.
    if (info.connectionState == UltraIMConnectionState::Connected) {
        matrixForm_->SetVisible(false);
        const bool canExport = UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::E2EEncryption);
        if (!canExport) SetStatus(Tr("Matrix: this ChatCube was built without end-to-end encryption."));
        matrixKeysForm_->SetVisible(canExport && !matrixKeysForm_->IsVisible());
    } else {
        matrixKeysForm_->SetVisible(false);
        matrixForm_->SetVisible(!matrixForm_->IsVisible());
    }
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ToggleTelegramForm() {
    UltraIMProviderInfo info;
    if (!LoadNetwork(telegramProviderId_, info)) {
        SetStatus(Tr("The Telegram module was not found (Plugins/UltraIM)."));
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
                           : scan ? Tr("Scan this with the Telegram app on your phone: Settings, Devices, "
                                       "Link Desktop Device. No code is needed; the two-step password "
                                       "is asked for here afterwards, if your account has one.")
                                  : std::string());
    telegramQr_->SetVisible(scan);
    if (scan) telegramQr_->SetContent(info.authHint);
    telegramResend_->SetVisible(step == UltraIMAuthStep::Code && !info.authResendHint.empty());
    if (!info.authResendHint.empty()) telegramResend_->SetText(info.authResendHint);
    telegramPassword_->SetVisible(step == UltraIMAuthStep::Password);
    telegramName_->SetVisible(step == UltraIMAuthStep::Registration);
    if (step == UltraIMAuthStep::Code) telegramCode_->SetPlaceholder(Tr("The code"));
    if (step == UltraIMAuthStep::Password && !info.authHint.empty()) telegramPassword_->SetPlaceholder(info.authHint);
    telegramNext_->SetText(start ? Tr("Send me the code") : scan ? Tr("Use my phone number instead") : Tr("Sign in to Telegram"));
    telegramQrButton_->SetVisible(start || step == UltraIMAuthStep::Code);
    telegramQrButton_->SetText(start ? Tr("Sign in with a QR code instead") : Tr("Scan a QR code instead"));
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
                    SetStatus(Tr("Telegram: enter your phone number with its country code, or sign in with a QR code."));
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
                      credentials.loginType == UltraIMLoginType::QRCode ? Tr("asking Telegram for a QR code\xE2\x80\xA6")
                                                                         : Tr("signing in\xE2\x80\xA6"));
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
                                                       ? Tr("scan the QR code with your phone's Telegram app")
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
        SetStatus(Tr("Matrix: exporting the room keys\xE2\x80\xA6"));
        UltraIM_ExportEncryptionKeys(matrixProviderId_, passphrase, [this](const UltraIMResult& r, const std::string& data) {
            if (!r) {
                SetStatus(TrF("Matrix: {0}", r.message));
                return;
            }
            FileDialogOptions opts;
            opts.SetTitle(Tr("Save the Matrix room keys"))
                .SetDefaultFileName("matrix-room-keys.txt")
                .AddFilter(Tr("Matrix room keys"), std::vector<std::string>{"txt"})
                .AddFilter(Tr("All files"), std::vector<std::string>{"*"})
                .SetParentWindow(window_.get());
            UltraCanvasFileLoader::SaveFileDialog(opts, [this, data](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                // The provider wrote the format; the file is the user's.
                std::FILE* f = OpenFileUtf8(path, "wb");
                const bool written = f && std::fwrite(data.data(), 1, data.size(), f) == data.size();
                if (f) std::fclose(f);
                SetStatus(written ? TrF("Matrix: room keys saved to {0}", path) : TrF("Matrix: could not write {0}", path));
            });
        });
    });
}

void ChatCubeWindow::ImportMatrixKeys() {
    FileDialogOptions opts;
    opts.SetTitle(Tr("Import Matrix room keys"))
        .AddFilter(Tr("Matrix room keys"), std::vector<std::string>{"txt"})
        .AddFilter(Tr("All files"), std::vector<std::string>{"*"})
        .SetParentWindow(window_.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        const FileBytesResult file = UltraCanvasFileLoader::LoadFile(path);
        if (!file.success) {
            SetStatus(TrF("Matrix: {0}", file.error));
            return;
        }
        const std::string data(file.bytes.begin(), file.bytes.end());
        UltraCanvasFileLoader::RequestDocumentPassword(path, window_.get(),
                                                       [this, data](bool proceed, const std::string& passphrase) {
            if (!proceed) return;
            SetStatus(Tr("Matrix: importing the room keys\xE2\x80\xA6"));
            UltraIM_ImportEncryptionKeys(matrixProviderId_, data, passphrase, [this](const UltraIMResult& r, int imported) {
                SetStatus(r ? TrN("Matrix: {n} room key imported", "Matrix: {n} room keys imported", imported)
                          : TrF("Matrix: {0}", r.message));
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
            SetStatus(Tr("Matrix: enter your user name and password."));
            return;
        }
        matrixSignIn_->SetDisabled(true);
    }
    SetProviderStatus(matrixProviderId_, Tr("signing in\xE2\x80\xA6"));
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

void ChatCubeWindow::ToggleMastodonForm() {
    UltraIMProviderInfo info;
    if (!LoadNetwork(mastodonProviderId_, info)) {
        SetStatus(Tr("The Mastodon module was not found (Plugins/UltraIM)."));
        return;
    }
    if (info.connectionState == UltraIMConnectionState::Connected) {
        mastodonForm_->SetVisible(false);
        SetStatus(TrF("Mastodon: signed in as {0}", info.localUserId));
    } else {
        mastodonForm_->SetVisible(!mastodonForm_->IsVisible());
    }
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::SignInToMastodon(bool stored) {
    UltraIMCredentials credentials;
    credentials.loginType = UltraIMLoginType::SsoOidc;
    if (!stored) {
        credentials.server = Trim(mastodonServer_->GetText());
        if (credentials.server.empty()) {
            SetStatus(Tr("Mastodon: enter your server, e.g. mastodon.social."));
            return;
        }
        mastodonSignIn_->SetDisabled(true);
    }
    SetProviderStatus(mastodonProviderId_, Tr("signing in\xE2\x80\xA6"));
    UltraIM_SignIn(mastodonProviderId_, credentials, [this, stored](const UltraIMResult& r) {
        if (!stored) mastodonSignIn_->SetDisabled(false);
        if (!r) {
            SetProviderStatus(mastodonProviderId_, r.message);
            return;
        }
        mastodonForm_->SetVisible(false);
        RefreshConversations();
    });
}

void ChatCubeWindow::ToggleSignalForm() {
    UltraIMProviderInfo info;
    if (!LoadNetwork(signalProviderId_, info)) {
        SetStatus(TrF("The {0} module was not found (Plugins/UltraIM).", std::string("Signal")));
        return;
    }
    if (info.connectionState == UltraIMConnectionState::Connected) {
        signalForm_->SetVisible(false);
        SetStatus(TrF("{0}: signed in as {1}", std::string("Signal"), info.localUserId));
    } else {
        signalForm_->SetVisible(!signalForm_->IsVisible());
    }
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::SignInToSignal(bool stored) {
    UltraIMCredentials credentials;
    credentials.loginType = UltraIMLoginType::QRCode;
    if (!stored) {
        credentials.server = Trim(signalDaemon_->GetText());
        signalLink_->SetDisabled(true);
    }
    SetProviderStatus(signalProviderId_, Tr("starting signal-cli\xE2\x80\xA6"));
    UltraIM_SignIn(signalProviderId_, credentials, [this, stored](const UltraIMResult& r) {
        if (!stored) signalLink_->SetDisabled(false);
        // The QR code to scan: shown by the AuthStepChange that comes with it.
        if (r.code == UltraIMResultCode::NeedsInput) return;
        if (!r) {
            SetProviderStatus(signalProviderId_, r.message);
            signalHint_->SetText(r.message);
            signalHint_->SetVisible(true);
            return;
        }
        signalForm_->SetVisible(false);
        RefreshConversations();
    });
}

bool ChatCubeWindow::OnSignalEvent(const UltraIMEvent& e) {
    if (e.providerId != signalProviderId_) return false;
    switch (e.type) {
        case UltraIMEventType::ConnectionStateChange:
            if (settingsWindow_) settingsWindow_->RefreshAccounts();
            if (e.connectionState == UltraIMConnectionState::Connected) {
                UltraIMProviderInfo info;
                UltraIM_GetProviderInfo(signalProviderId_, info);
                SetProviderStatus(signalProviderId_, TrF("signed in as {0}", info.localUserId));
                SetNetworkButton(signalToggle_, "Signal", TrF("{0}: signed in as {1}", std::string("Signal"), info.localUserId));
                signalQr_->SetVisible(false);
                signalQr_->SetContent(std::string());
                signalHint_->SetVisible(false);
                signalForm_->SetVisible(false);
                RefreshConversations();
            } else if (e.connectionState == UltraIMConnectionState::Error) {
                SetProviderStatus(signalProviderId_, e.errorText);
                signalLink_->SetDisabled(false);
            } else if (e.connectionState == UltraIMConnectionState::Reconnecting) {
                SetProviderStatus(signalProviderId_, Tr("connection lost - reconnecting\xE2\x80\xA6"));
            } else if (e.connectionState == UltraIMConnectionState::Disconnected) {
                SetProviderStatus(signalProviderId_, Tr("signed out"));
                SetNetworkButton(signalToggle_, "Signal", TrF("{0} account", std::string("Signal")));
            }
            return true;
        case UltraIMEventType::AuthStepChange:
            if (e.authStep == UltraIMAuthStep::OtherDevice && !e.errorText.empty()) {
                // The link for the phone to scan (sgnl://linkdevice?...).
                signalQr_->SetContent(e.errorText);
                signalQr_->SetVisible(true);
                signalHint_->SetText(Tr("On your phone, open Signal: Settings > Linked devices > Link new device, "
                                        "and scan this code."));
                signalHint_->SetVisible(true);
                signalForm_->SetVisible(true);
                SetProviderStatus(signalProviderId_, Tr("scan the code with Signal on your phone"));
                if (window_) window_->RequestRedraw();
            }
            return true;
        case UltraIMEventType::AuthRequire:
            SetProviderStatus(signalProviderId_, Tr("this computer was unlinked - link it again"));
            signalForm_->SetVisible(true);
            SetNetworkButton(signalToggle_, "Signal", TrF("{0} account", std::string("Signal")));
            return true;
        default:
            return false;
    }
}

void ChatCubeWindow::BuildAccountForm(AccountForm& f, UltraCanvasContainer& sidebar, const std::string& serverHint,
                                      const std::string& userHint) {
    const std::string id = "cc-" + f.providerId;
    f.form = std::make_shared<UltraCanvasContainer>(id + "-form");
    f.form->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    f.form->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AccountForm* form = &f;
    const auto go = [this, form](const std::string&) { SignInToAccount(*form, false); return true; };
    f.server = MakeInput(id + "-server", serverHint, kSidebarWidth - 16);
    f.form->AddChild(f.server);
    f.user = MakeInput(id + "-user", userHint, kSidebarWidth - 16);
    f.form->AddChild(f.user);
    f.password = MakeInput(id + "-password", Tr("Password"), kSidebarWidth - 16);
    f.password->SetInputType(TextInputType::Password);
    f.password->onEnterPressed = go;
    f.form->AddChild(f.password);
    // The account's second step, when it has one (an authenticator's code).
    f.code = MakeInput(id + "-code", Tr("The code from your authenticator app"), kSidebarWidth - 16);
    f.code->onEnterPressed = go;
    f.code->SetVisible(false);
    f.form->AddChild(f.code);
    f.signIn = MakeButton(id + "-go", TrF("Sign in to {0}", f.name), kSidebarWidth - 16, true);
    f.signIn->SetOnClick([this, form]() { SignInToAccount(*form, false); });
    f.form->AddChild(f.signIn);
    f.form->SetVisible(false);
    sidebar.AddChild(f.form);
}

ChatCubeWindow::AccountForm* ChatCubeWindow::AccountFormOf(const std::string& providerId) {
    if (providerId == mattermost_.providerId) return &mattermost_;
    if (providerId == zulip_.providerId) return &zulip_;
    if (providerId == xmpp_.providerId) return &xmpp_;
    if (providerId == irc_.providerId) return &irc_;
    return nullptr;
}

void ChatCubeWindow::ToggleAccountForm(AccountForm& f) {
    UltraIMProviderInfo info;
    if (!LoadNetwork(f.providerId, info)) {
        SetStatus(TrF("The {0} module was not found (Plugins/UltraIM).", f.name));
        return;
    }
    if (info.connectionState == UltraIMConnectionState::Connected) {
        f.form->SetVisible(false);
        AccountForm* form = &f;
        UltraIM_FetchAccountProfile(f.providerId, [this, form, id = info.localUserId](const UltraIMResult& r,
                                                                                    const UltraIMAccountProfile& p) {
            SetStatus(TrF("{0}: signed in as {1}", form->name, r && !p.displayName.empty() ? p.displayName : id));
        });
    } else {
        f.form->SetVisible(!f.form->IsVisible());
    }
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::SignInToAccount(AccountForm& f, bool stored) {
    UltraIMCredentials credentials;
    credentials.loginType = UltraIMLoginType::UsernamePassword;
    if (!stored) {
        UltraIMProviderInfo info;
        UltraIM_GetProviderInfo(f.providerId, info);
        if (info.authStep == UltraIMAuthStep::Code && f.code->IsVisible()) {
            // The second step: the code alone (the provider kept the rest).
            credentials.otpCode = Trim(f.code->GetText());
            if (credentials.otpCode.empty()) {
                SetStatus(TrF("{0}: enter the code from your authenticator app.", f.name));
                return;
            }
        } else {
            credentials.server = Trim(f.server->GetText());
            credentials.username = Trim(f.user->GetText());
            credentials.password = f.password->GetText();
            if ((credentials.server.empty() && !f.serverOptional) || credentials.username.empty() ||
                (credentials.password.empty() && !f.passwordOptional)) {
                SetStatus(f.serverOptional    ? TrF("{0}: enter your address and your password.", f.name)
                          : f.passwordOptional ? TrF("{0}: enter the server and your nickname.", f.name)
                                               : TrF("{0}: enter the server, your account and your password.", f.name));
                return;
            }
        }
        f.signIn->SetDisabled(true);
    }
    SetProviderStatus(f.providerId, Tr("signing in\xE2\x80\xA6"));
    AccountForm* form = &f;
    UltraIM_SignIn(f.providerId, credentials, [this, form, stored](const UltraIMResult& r) {
        if (!stored) form->signIn->SetDisabled(false);
        if (r.code == UltraIMResultCode::NeedsInput) {
            // A code is asked for (AuthStepChange shows the field).
            SetProviderStatus(form->providerId, Tr("enter the code from your authenticator app"));
            return;
        }
        if (!r) {
            SetProviderStatus(form->providerId, r.message);
            return;
        }
        // A note from the network (IRC: the nickname was taken, another given).
        if (!r.message.empty()) SetStatus(TrF("{0}: {1}", form->name, r.message));
        form->password->SetText("");        // not kept: the session token is
        form->code->SetText("");
        form->code->SetVisible(false);
        form->form->SetVisible(false);
        RefreshConversations();
    });
}

bool ChatCubeWindow::OnAccountEvent(const UltraIMEvent& e) {
    AccountForm* f = AccountFormOf(e.providerId);
    if (!f) return false;
    switch (e.type) {
        case UltraIMEventType::ConnectionStateChange: {
            if (settingsWindow_) settingsWindow_->RefreshAccounts();
            if (e.connectionState == UltraIMConnectionState::Connected) {
                UltraIMProviderInfo info;
                UltraIM_GetProviderInfo(f->providerId, info);
                SetProviderStatus(f->providerId, TrF("signed in as {0}", info.localUserId));
                SetNetworkButton(f->toggle, f->name, TrF("{0}: signed in as {1}", f->name, info.localUserId));
                // The account's name rather than its id, which means nothing to a person.
                UltraIM_FetchAccountProfile(f->providerId, [this, f](const UltraIMResult& r,
                                                                     const UltraIMAccountProfile& profile) {
                    if (!r || profile.displayName.empty()) return;
                    SetProviderStatus(f->providerId, TrF("signed in as {0}", profile.displayName));
                    SetNetworkButton(f->toggle, f->name, TrF("{0}: signed in as {1}", f->name, profile.displayName));
                });
                RefreshConversations();
                // The next sign-in starts at the server again, not at a code.
                f->server->SetVisible(true);
                f->user->SetVisible(true);
                f->password->SetVisible(true);
                f->code->SetVisible(false);
            } else if (e.connectionState == UltraIMConnectionState::Error) {
                SetProviderStatus(f->providerId, e.errorText);
                f->signIn->SetDisabled(false);
            } else if (e.connectionState == UltraIMConnectionState::Reconnecting) {
                SetProviderStatus(f->providerId, Tr("connection lost - reconnecting\xE2\x80\xA6"));
            } else if (e.connectionState == UltraIMConnectionState::Connecting) {
                SetProviderStatus(f->providerId, Tr("signing in\xE2\x80\xA6"));
            } else if (e.connectionState == UltraIMConnectionState::Disconnected) {
                SetProviderStatus(f->providerId, Tr("signed out"));
                SetNetworkButton(f->toggle, f->name, TrF("{0} account", f->name));
            }
            return true;
        }
        case UltraIMEventType::AuthStepChange: {
            const bool code = e.authStep == UltraIMAuthStep::Code;
            f->code->SetVisible(code);
            f->server->SetVisible(!code);
            f->user->SetVisible(!code);
            f->password->SetVisible(!code);
            if (code) {
                f->form->SetVisible(true);
                if (!e.errorText.empty()) f->code->SetPlaceholder(e.errorText);
            }
            if (window_) window_->RequestRedraw();
            return true;
        }
        case UltraIMEventType::AuthRequire:
            SetProviderStatus(f->providerId, Tr("the session ended - sign in again"));
            f->form->SetVisible(true);
            SetNetworkButton(f->toggle, f->name, TrF("{0} account", f->name));
            return true;
        default:
            return false;
    }
}

void ChatCubeWindow::RefreshNearby() {
    UltraIM_FetchNearbyContacts(providerId_, [this](const UltraIMResult&, const std::vector<UltraIMContact>& list) {
        nearbyBox_->ClearChildren();
        if (!list.empty()) nearbyBox_->AddChild(MakeLabel("cc-nearby-title", Tr("On this network"), 9.0f, &Look::inkMuted));
        for (const UltraIMContact& person : list) {
            auto row = MakeRow("cc-nearby-row", 6);
            auto name = MakeLabel("cc-nearby-name", person.displayName, kTextFont);
            name->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(name);
            auto add = MakeButton("cc-nearby-add", Tr("Add"), 52, true);
            const std::string id = person.contactId;
            add->SetOnClick([this, id]() { AddContactById(id); });
            row->AddChild(add);
            nearbyBox_->AddChild(row);
        }
        nearbyBox_->SetVisible(!list.empty() && settings_.showNearby);
        if (window_) window_->RequestRedraw();
    });
}

void ChatCubeWindow::RefreshPublicGroups() {
    UltraIM_FetchPublicGroups(providerId_, std::string(),
                              [this](const UltraIMResult& r, const std::vector<UltraIMPublicGroup>& list) {
        publicBox_->ClearChildren();
        std::vector<UltraIMPublicGroup> open;
        if (r) {
            for (const UltraIMPublicGroup& g : list) {
                if (!g.joined) open.push_back(g);
            }
        }
        if (!open.empty()) publicBox_->AddChild(MakeLabel("cc-public-title", Tr("Public groups"), 9.0f, &Look::inkMuted));
        // The biggest few; the rest on asking.
        constexpr size_t kShown = 4;
        const size_t shown = showAllPublic_ ? open.size() : std::min(open.size(), kShown);
        for (size_t i = 0; i < shown; ++i) {
            const UltraIMPublicGroup& g = open[i];
            auto row = MakeRow("cc-public-row", 6);
            auto name = MakeLabel("cc-public-name", g.title, kTextFont);
            // What it is about, when hovered.
            name->SetTooltip(g.description.empty() ? TrN("{n} member", "{n} members", g.memberCount) : g.description);
            name->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(name);
            auto count = MakeLabel("cc-public-count", std::to_string(g.memberCount), 9.0f, &Look::inkMuted);
            count->SetTooltip(TrN("{n} member", "{n} members", g.memberCount));
            row->AddChild(count);
            auto join = MakeButton("cc-public-join", Tr("Join"), 52, true);
            const std::string id = g.conversationId;
            const std::string title = g.title;
            join->SetOnClick([this, id, title]() { JoinPublicGroup(id, title); });
            row->AddChild(join);
            publicBox_->AddChild(row);
        }
        if (open.size() > shown) {
            auto more = MakeButton("cc-public-more", TrF("Show all public groups ({0})", open.size()),
                                   kSidebarWidth - 16);
            more->SetOnClick([this]() {
                showAllPublic_ = true;
                RefreshPublicGroups();
            });
            publicBox_->AddChild(more);
        }
        publicBox_->SetVisible(!open.empty());
        if (window_) window_->RequestRedraw();
    });
}

void ChatCubeWindow::JoinPublicGroup(const std::string& conversationId, const std::string& title) {
    SetStatus(TrF("Joining {0}\xE2\x80\xA6", title));
    UltraIM_JoinGroup(providerId_, conversationId, [this, title](const UltraIMResult& r, const UltraIMConversation& c) {
        if (!r) {
            SetStatus(TrF("Could not join {0}: {1}", title, r.message));
            RefreshPublicGroups();
            return;
        }
        UpsertConversation(c);
        SetStatus(TrF("You joined {0}.", title));
        RefreshPublicGroups();
        OpenConversation(c.conversationId);
    });
}

void ChatCubeWindow::SetGroupPublic(const std::string& conversationId, bool isPublic) {
    UltraIM_SetGroupPublic(providerId_, conversationId, isPublic,
                           [this, isPublic](const UltraIMResult& r, const UltraIMConversation& c) {
        if (!r) {
            SetStatus(TrF("Could not change the group: {0}", r.message));
            return;
        }
        UpsertConversation(c);
        SetStatus(isPublic ? TrF("{0} is public: everyone on ChatCube finds it under Public groups and can join.", c.title)
                           : TrF("{0} is private again: only those in it see it.", c.title));
        RefreshPublicGroups();
    });
}

void ChatCubeWindow::ToggleGroupForm() {
    groupForm_->SetVisible(!groupForm_->IsVisible());
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::CreateGroup() {
    const std::string title = Trim(groupTitle_->GetText());
    if (title.empty()) {
        SetStatus(Tr("Give the group a name."));
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
    SetStatus(Tr("Creating the group\xE2\x80\xA6"));
    // The members' addresses say where: a Matrix room, a Telegram group, or ChatCube.
    UltraIM_CreateGroup(members.empty() ? providerId_ : ProviderOfAddress(members.front()), title, members,
                        [this](const UltraIMResult& r, const UltraIMConversation& c) {
        if (!r) {
            SetStatus(TrF("Could not create the group: {0}", r.message));
            return;
        }
        groupTitle_->SetText("");
        groupMembers_->SetText("");
        groupForm_->SetVisible(false);
        conversations_->Upsert(c);
        SetStatus(TrF("Created {0}", c.title));
        OpenConversation(c.conversationId);
    });
}

std::vector<std::string> ChatCubeWindow::ProvidersOfAddress(const std::string& address) const {
    if (address.empty()) return {providerId_};
    // Mastodon: @user@server, or a profile's page (https://server/@user).
    if (address[0] == '@' && address.find('@', 1) != std::string::npos) return {mastodonProviderId_};
    if (address.rfind("https://", 0) == 0 && address.find("/@") != std::string::npos) return {mastodonProviderId_};
    if (address[0] == '@') {
        if (address.find(':') != std::string::npos) return {matrixProviderId_};
        return {telegramProviderId_, mattermost_.providerId};
    }
    if (address[0] == '+') return {telegramProviderId_, signalProviderId_};
    if (address.rfind("t.me/", 0) == 0 || address.rfind("https://t.me/", 0) == 0) return {telegramProviderId_};
    // A Signal username: a name, a dot and two or more digits ("erin.42").
    {
        const size_t dot = address.rfind('.');
        if (dot != std::string::npos && dot > 0 && address.size() - dot >= 3 && address.find('@') == std::string::npos &&
            std::all_of(address.begin() + static_cast<std::ptrdiff_t>(dot) + 1, address.end(),
                        [](char c) { return c >= '0' && c <= '9'; }))
            return {signalProviderId_};
    }
    if (address.rfind("xmpp:", 0) == 0) return {xmpp_.providerId};
    // IRC: a channel (#name), or irc:nick.
    if (address[0] == '#' || address.rfind("irc:", 0) == 0) return {irc_.providerId};
    // An address name@domain: a Zulip or Mattermost account's e-mail, or an
    // XMPP address (a room's too).
    const size_t at = address.find('@');
    if (at != std::string::npos && at > 0 && address.find('.', at) != std::string::npos &&
        address.find(' ') == std::string::npos)
        return {zulip_.providerId, mattermost_.providerId, xmpp_.providerId};
    return {providerId_};
}

std::string ChatCubeWindow::ProviderOfAddress(const std::string& address) const {
    // The first signed-in network that takes the address, else the likeliest.
    const std::vector<std::string> candidates = ProvidersOfAddress(address);
    for (const std::string& id : candidates) {
        UltraIMProviderInfo info;
        if (UltraIM_GetProviderInfo(id, info) && info.connectionState == UltraIMConnectionState::Connected) return id;
    }
    return candidates.front();
}

std::string ChatCubeWindow::ProviderOf(const std::string& conversationId) const {
    const UltraIMConversation* c = conversations_->Find(conversationId);
    return c && !c->providerId.empty() ? c->providerId : providerId_;
}

void ChatCubeWindow::OpenConversation(const std::string& conversationId) {
    // What belonged to the chat before stays with it: a reply or an edit
    // under way, a recording, a voice message playing, a search.
    if (conversationId != activeConversation_) {
        FinishVoiceRecording(false);
        CloseVideoRecorder(false);
        StopVoicePlayback();
        CloseVideoPlayer();
        ClearComposerMode();
        ClearSearch(false);
    }
    activeConversation_ = conversationId;
    activeProvider_ = ProviderOf(conversationId);
    // Compact: the conversation takes the window.
    if (compact_ && !compactShowsChat_) {
        compactShowsChat_ = true;
        ApplyCompactPanes();
    }
    const UltraIMConversation* c = conversations_->Find(conversationId);
    if (c && conversations_->IsMarkedUnread(*c)) SetMarkedUnread(conversationId, false);   // read now
    c = conversations_->Find(conversationId);
    messages_->SetGroup(c && c->isGroup);
    messages_->Clear();
    emptyHint_->SetVisible(false);
    messageList_->SetVisible(true);
    searchRow_->SetVisible(true);
    filterRow_->SetVisible(true);
    fillingList_ = true;
    const int row = conversations_->FindRow(conversationId);
    if (row >= 0) conversationList_->GetSelection()->Select(row);
    fillingList_ = false;
    UpdateHeader();

    LoadMessages();
    UltraIM_MarkRead(activeProvider_, conversationId);
    input_->SetFocus(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::LoadMessages() {
    const std::string conversationId = activeConversation_;
    if (conversationId.empty()) return;
    const int serial = ++searchSerial_;
    UltraIM_FetchMessages(activeProvider_, conversationId, "", 200,
                          [this, conversationId, serial](const UltraIMResult& r, const std::vector<UltraIMMessage>& list) {
        // The user moved on, or searched meanwhile.
        if (conversationId != activeConversation_ || serial != searchSerial_) return;
        if (!r) SetStatus(TrF("Could not load the messages: {0}", r.message));
        messages_->SetMessages(list);
        ScrollToEnd();
        for (const UltraIMMessage& m : list) EnsureVideoPreview(m);
    });
}

void ChatCubeWindow::UpdateHeader() {
    const UltraIMConversation* c = conversations_->Find(activeConversation_);
    if (!c) {
        title_->SetText("");
        subtitle_->SetText("");
        headerAvatar_->SetVisible(false);
        headerPhoto_->SetVisible(false);
        ApplyFeatureIcons();
        return;
    }
    title_->SetText(c->title);
    // What a group is about, when its name is hovered.
    title_->SetTooltip(c->isGroup ? c->description : std::string());
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
    const Color avatar = c->isGroup ? L().well : AvatarColor(c->conversationId, L());
    headerAvatar_->SetBackgroundColor(avatar);
    headerAvatar_->SetTextColor(c->isGroup ? L().inkMuted : InkOn(avatar));
    headerAvatar_->SetVisible(!photo);
    headerPhoto_->SetVisible(photo);
    // The paperclip, for a network that takes pictures or files.
    const bool attachments = AttachmentsPossible();
    attach_->SetTooltip(attachments ? Tr("Send a picture, a video or a file")
                                    : Tr("This network does not take pictures or files yet"));
    screenshot_->SetTooltip(attachments ? Tr("Insert a screenshot: choose a window or drag an area")
                                        : Tr("This network does not take pictures or files yet"));
    ApplyFeatureIcons();
    if (const std::string typing = TypingText(*c); !typing.empty()) {
        subtitle_->SetText(typing);
    } else if (c->isGroup) {
        subtitle_->SetText(TrN("{n} member", "{n} members", MemberCount(*c)) +
                           (c->isPublic ? " \xC2\xB7 " + Tr("public group") : std::string()) +
                           (c->isEncrypted ? " \xC2\xB7 " + Tr("end-to-end encrypted") : std::string()));
    } else if (c->providerId == providerId_) {
        subtitle_->SetText(PresenceText(c->presence));
    } else {
        // Not ChatCube Direct: no relay to wait on.
        // A network with no presence (Signal) leaves only the encryption: no dot before it.
        const std::string presence = c->presence == UltraIMPresence::Offline ? Tr("offline") : PresenceText(c->presence);
        std::string line = presence;
        if (c->isEncrypted) line += (line.empty() ? std::string() : std::string(" \xC2\xB7 ")) + Tr("end-to-end encrypted");
        subtitle_->SetText(line);
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
    if (!editing_.clientMessageId.empty()) {
        SaveEdit(text);
        return;
    }
    // What is sent shows in the whole chat, not among a search's matches.
    if ((!searchText_.empty() || shownKind_ != UltraIMSearchKind::AnyKind) && (!text.empty() || !staged_.empty()))
        ClearSearch(true);
    const std::string replyTo = replyTo_.messageId;
    if (staged_.empty()) {
        if (!text.empty()) {
            SendText(text, replyTo);
            ClearComposerMode();
        }
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
    bool replied = replyTo.empty();         // the first message answers
    for (const StagedAttachment& item : items) {
        const bool takesCaption = !captionSent && AttachmentKind(item.path) != UltraIMMessageType::File;
        SendAttachment(item.path, takesCaption ? text : std::string(), replied ? std::string() : replyTo);
        replied = true;
        if (takesCaption) captionSent = true;
    }
    if (!captionSent) SendText(text);
    ClearComposerMode();
    input_->SetText("");
    lastTypingSentMs_ = 0;
    UltraIM_SetTyping(activeProvider_, activeConversation_, false);
}

void ChatCubeWindow::SendText(const std::string& text, const std::string& replyTo) {
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
    message.replyTo.messageId = replyTo;    // the list fills in the quote from the message shown
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
    options.replyToMessageId = replyTo;
    // In the background, behind what was sent before it.
    const std::string provider = activeProvider_;
    const std::string conversation = activeConversation_;
    sendQueue_->Post([this, provider, conversation, text, options]() {
        const UltraIMResult r = UltraIM_SendText(provider, conversation, text, options);
        if (r) return;
        OnUI([this, id = options.clientMessageId, why = r.message]() {
            messages_->UpdateStatus(id, UltraIMDeliveryStatus::Failed);
            SetStatus(TrF("Not sent: {0}", why));
        });
    });
}

void ChatCubeWindow::AttachFile() {
    if (activeConversation_.empty()) return;
    FileDialogOptions opts;
    opts.SetTitle(Tr("Send a picture, a video or a file")).SetParentWindow(window_.get());
    // Any file goes: every file shows (All files is the first type, the one
    // the dialog opens with); the pictures and videos this build reads are
    // a choice after it.
    opts.AddFilter(Tr("All files"), std::vector<std::string>{"*"});
    UltraCanvasFileLoader::AddLoadTypes(opts, Tr("Pictures and videos"), {MediaFormatCategory::Bitmap, MediaFormatCategory::Video});
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        StageAttachment(path, false);
    });
}

bool ChatCubeWindow::DropFiles(const UCEvent& event) {
    if (event.droppedFiles.empty()) return false;
    if (!chatView_ || !chatView_->IsVisible()) return true;     // signed out: nothing takes them
    // On a chat in the list: that chat, opened.
    const Point2Df at(static_cast<float>(event.pointerWindow.x), static_cast<float>(event.pointerWindow.y));
    if (conversationList_ && conversationList_->IsVisible() && conversationList_->ContainsInWindow(at)) {
        // The row whose rectangle, as drawn now (scrolled), holds the point.
        const Point2Di local(static_cast<int>(at.x - conversationList_->GetXInWindow()),
                             static_cast<int>(at.y - conversationList_->GetYInWindow()));
        const auto& list = conversations_->Conversations();
        for (size_t row = 0; row < list.size(); ++row) {
            if (!conversationList_->GetCellRect(static_cast<int>(row), 0).Contains(local)) continue;
            if (list[row].conversationId != activeConversation_) OpenConversation(list[row].conversationId);
            break;
        }
    }
    if (activeConversation_.empty()) {
        SetStatus(Tr("Drop the files on a chat, or open one first."));
        return true;
    }
    int staged = 0, folders = 0;
    std::error_code ec;
    for (const std::string& file : event.droppedFiles) {
        if (std::filesystem::is_regular_file(PathFromUtf8(file), ec)) {
            StageAttachment(file, false);
            ++staged;
        } else if (std::filesystem::is_directory(PathFromUtf8(file), ec)) {
            ++folders;
        }
    }
    if (folders > 0 && staged == 0) SetStatus(Tr("A folder cannot be sent - drop the files in it."));
    if (staged > 0) input_->SetFocus(true);
    if (window_) window_->RequestRedraw();
    return true;
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
        SetStatus(Tr("This network does not take pictures yet."));
        return true;
    }

    // Into a file of ChatCube's own, in its cache: the provider reads the
    // file, and the bubble shows it from there. PNG and JPEG as they came;
    // anything else (a bitmap, say) decoded and written as PNG.
    const std::string base = NewCachedPicturePath("Pasted");
    std::string path;
    if (base.empty()) {
        // no cache folder: reported below as unreadable
    } else if (format == "image/png" || format == "image/jpeg") {
        path = base + (format == "image/png" ? ".png" : ".jpg");
        std::FILE* out = OpenFileUtf8(path, "wb");
        const bool written = out && std::fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
        if (out) std::fclose(out);
        if (!written) path.clear();
    } else {
        UCImagePtr image = UCImage::LoadFromMemory(bytes);
        if (image && image->IsValid()) {
            path = base + ".png";
            UCImageSave::ImageExportOptions options;
            options.format = UCImageSaveFormat::PNG;
            if (!image->Save(path, options).empty()) path.clear();
        }
    }
    if (path.empty()) {
        SetStatus(Tr("The picture on the clipboard could not be read."));
        return true;
    }
    StageAttachment(path, true);
    return true;
}

std::string ChatCubeWindow::NewCachedPicturePath(const std::string& prefix) const {
    std::error_code ec;
    const std::filesystem::path folder = UltraCanvasDataFolder("ChatCube") / "cache" / "pasted";
    std::filesystem::create_directories(folder, ec);
    if (!std::filesystem::is_directory(folder, ec)) return {};
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    std::string name = prefix + "-" + stamp;
    for (int n = 1; std::filesystem::exists(folder / PathFromUtf8(name + ".png"), ec) ||
                    std::filesystem::exists(folder / PathFromUtf8(name + ".jpg"), ec); ++n)
        name = prefix + "-" + stamp + "-" + std::to_string(n);
    return PathToUtf8(folder / PathFromUtf8(name));
}

// ===== THE FEATURE ICONS =====

bool ChatCubeWindow::FeatureUsable(Feature feature) const {
    if (activeConversation_.empty()) return false;
    switch (feature) {
        case Feature::Emoji:
        case Feature::Location:
            return true;        // text; a sticker or a map picture only where pictures go
        case Feature::Screenshot:
            return UltraCanvasDesktopShell::CanCaptureScreen() && AttachmentsPossible();
        case Feature::Attach:
        case Feature::VoiceMessage:
        case Feature::VideoMessage:
            break;
    }
    return AttachmentsPossible();
}

void ChatCubeWindow::ApplyFeatureIcons() {
    if (!plus_) return;
    const auto button = [this](Feature f) -> std::shared_ptr<UltraCanvasButton> {
        switch (f) {
            case Feature::Attach: return attach_;
            case Feature::Emoji: return emoji_;
            case Feature::Screenshot: return screenshot_;
            case Feature::Location: return location_;
            case Feature::VoiceMessage: return recordVoice_;
            case Feature::VideoMessage: return recordVideo_;
        }
        return nullptr;
    };
    bool anyBehindPlus = false;
    for (Feature f : kAllFeatures) {
        auto b = button(f);
        if (!b) continue;
        FeaturePlace place = settings_.features.Get(f);
        // A screenshot where this system takes none: not offered anywhere
        if (f == Feature::Screenshot && !UltraCanvasDesktopShell::CanCaptureScreen()) place = FeaturePlace::Off;
        b->SetVisible(place == FeaturePlace::Field);
        b->SetDisabled(!FeatureUsable(f));
        if (f == Feature::Emoji && emojiSets_) {
            emojiSets_->SetVisible(place == FeaturePlace::Field);
            emojiSets_->SetDisabled(!FeatureUsable(f));
        }
        if (place == FeaturePlace::Menu) anyBehindPlus = true;
    }
    plus_->SetVisible(anyBehindPlus);
    plus_->SetDisabled(activeConversation_.empty());
    if (composerRow_) composerRow_->InvalidateLayout();
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::RunFeature(Feature feature) {
    if (!FeatureUsable(feature)) return;
    switch (feature) {
        case Feature::Attach: AttachFile(); break;
        case Feature::Emoji: OpenEmojiGrid(); break;
        case Feature::Screenshot: TakeScreenshot(); break;
        case Feature::Location: OpenLocation(); break;
        case Feature::VoiceMessage: StartVoiceRecording(); break;
        case Feature::VideoMessage: OpenVideoRecorder(); break;
    }
}

Rect2Di ChatCubeWindow::WindowRectOf(UltraCanvasUIElement& element) const {
    const Point2Df at = element.GetPositionInWindow();
    return Rect2Di(static_cast<int>(at.x), static_cast<int>(at.y), static_cast<int>(element.GetWidth()),
                   static_cast<int>(element.GetHeight()));
}

void ChatCubeWindow::OpenFeatureMenu() {
    if (!window_ || !plus_) return;
    featureMenu_ = std::make_shared<UltraCanvasMenu>("cc-feature-menu", 0, 0, 260, 0);
    featureMenu_->SetMenuType(MenuType::PopupMenu);
    int items = 0;
    for (Feature f : kAllFeatures) {
        if (settings_.features.Get(f) != FeaturePlace::Menu) continue;
        if (f == Feature::Screenshot && !UltraCanvasDesktopShell::CanCaptureScreen()) continue;
        // After the menu has gone: a feature may open a window or a popup
        MenuItemData item = MenuItemData::Action(FeatureName(f), IconPath(FeatureIcon(f)), [this, f]() {
            if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                app->PostToUIThread([this, f, alive = std::weak_ptr<bool>(alive_)]() {
                    if (auto a = alive.lock(); a && *a) RunFeature(f);
                });
            }
        });
        item.enabled = FeatureUsable(f);
        featureMenu_->AddItem(item);
        ++items;
    }
    if (items == 0) return;
    featureMenu_->OpenMenuAnchored(WindowRectOf(*plus_), *window_, PopupElementSettings());
}

void ChatCubeWindow::OpenEmojiGrid() {
    if (!window_ || activeConversation_.empty()) return;
    // From its own button when that is shown, else from the + it is behind
    UltraCanvasUIElement* anchor = emoji_ && emoji_->IsVisible() ? static_cast<UltraCanvasUIElement*>(emoji_.get())
                                                                 : static_cast<UltraCanvasUIElement*>(plus_.get());
    if (!anchor) return;
    emojiPicker_.OpenGrid(*window_, WindowRectOf(*anchor), settings_.emojiSet);
}

void ChatCubeWindow::OpenEmojiSets() {
    if (!window_ || !emojiSets_ || activeConversation_.empty()) return;
    emojiPicker_.OpenSetList(*window_, WindowRectOf(*emoji_));
}

void ChatCubeWindow::InsertIntoMessage(const std::string& text) {
    if (!input_ || text.empty()) return;
    input_->SetFocus(true);
    input_->InsertText(text);
}

void ChatCubeWindow::StageSticker(const std::string& svgPath) {
    if (!AttachmentsPossible()) {
        SetStatus(Tr("This network does not take pictures yet."));
        return;
    }
    // As a PNG, which every network shows; the drawing itself if that
    // cannot be made
    const std::string base = NewCachedPicturePath("Sticker");
    if (!base.empty()) {
        UCImagePtr drawing = UCImage::Get(svgPath);
        if (drawing && drawing->IsValid()) {
            const std::string path = base + ".png";
            UCImageSave::ImageExportOptions options;
            options.format = UCImageSaveFormat::PNG;
            options.targetWidth = 256;
            options.targetHeight = 256;
            if (drawing->Save(path, options).empty()) {
                StageAttachment(path, true);
                return;
            }
        }
    }
    std::error_code ec;
    const std::string copy = base.empty() ? std::string() : base + ".svg";
    if (!copy.empty() && std::filesystem::copy_file(PathFromUtf8(svgPath), PathFromUtf8(copy), ec)) {
        StageAttachment(copy, true);
    } else {
        SetStatus(Tr("The sticker could not be prepared."));
    }
}

void ChatCubeWindow::OpenLocation() {
    if (!window_ || activeConversation_.empty()) return;
    if (!locationWindow_) {
        const std::string cache = PathToUtf8(UltraCanvasDataFolder("ChatCube") / "cache" / "pasted");
        locationWindow_ = std::make_unique<ChatCubeLocationWindow>(
            version_, cache, [this](const MapPlace& place, const std::string& picture) { InsertLocation(place, picture); });
    }
    locationWindow_->Show(window_.get());
}

void ChatCubeWindow::InsertLocation(const MapPlace& place, const std::string& picturePath) {
    if (activeConversation_.empty() || !input_) return;
    bool picture = !picturePath.empty() && AttachmentsPossible();
    if (picture) StageAttachment(picturePath, true);
    std::string text = LocationText(place, picture);
    // On a line of its own after what is written already
    const std::string written = input_->GetText();
    if (!written.empty() && written.back() != '\n') text = "\n" + text;
    input_->MoveCursorToEnd();
    InsertIntoMessage(text);
    if (window_) window_->RaiseAndFocus();
}

// ===== A SCREENSHOT INTO THE COMPOSER =====

void ChatCubeWindow::TakeScreenshot() {
    if (activeConversation_.empty() || screenshotRunning_ || !window_) return;
    if (!AttachmentsPossible()) {
        SetStatus(Tr("This network does not take pictures yet."));
        return;
    }
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) return;
    // Out of the picture: hidden, and given a moment to leave the screen
    // (the window system takes it off after the call returns)
    screenshotRunning_ = true;
    window_->Hide();
    screenshotTimer_ = app->StartTimer(kScreenshotSettleMs, false, [this](TimerId) {
        screenshotTimer_ = 0;
        PickScreenArea();
    });
}

void ChatCubeWindow::PickScreenArea() {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    std::string error;
    if (!app || !UltraCanvasDesktopShell::CaptureScreenImage(screenShot_, &error)) {
        EndScreenshot();
        SetStatus(TrF("The screen could not be captured: {0}", error));
        return;
    }
    // A window clicked or an area dragged; where the platform lists no
    // windows (Wayland, macOS), an area, Enter taking the whole screen
    std::vector<ScreenAreaWindow> windows = ScreenArea::ListPickableWindows();
    const ScreenAreaMode mode = windows.empty() ? ScreenAreaMode::Rectangle : ScreenAreaMode::Window;
    screenPicker_ = std::make_unique<UltraCanvasScreenAreaPicker>();
    screenPicker_->SetTitle("ChatCube");
    // The picker is let go of from the UI thread's queue, not from inside
    // its own callback
    std::weak_ptr<bool> alive = alive_;
    screenPicker_->onSelected = [this, app, alive](const Rect2Di& area) {
        app->PostToUIThread([this, area, alive]() {
            if (auto a = alive.lock(); !a || !*a) return;
            screenPicker_.reset();
            StageScreenshot(area);
            EndScreenshot();
        });
    };
    screenPicker_->onCancelled = [this, app, alive]() {
        app->PostToUIThread([this, alive]() {
            if (auto a = alive.lock(); !a || !*a) return;
            screenPicker_.reset();
            EndScreenshot();
        });
    };
    if (!screenPicker_->Open(screenShot_, mode, windows, &error)) {
        screenPicker_.reset();
        EndScreenshot();
        SetStatus(error);
    }
}

void ChatCubeWindow::StageScreenshot(const Rect2Di& area) {
    ClipboardDib::RgbaImage image;
    std::vector<uint8_t> png;
    const std::string base = NewCachedPicturePath("Screenshot");
    if (base.empty() || !ScreenArea::CropToRgba(screenShot_, area, image) || !ClipboardDib::EncodePng(image, png)) {
        SetStatus(Tr("The screenshot could not be kept."));
        return;
    }
    const std::string path = base + ".png";
    std::FILE* out = OpenFileUtf8(path, "wb");
    const bool written = out && std::fwrite(png.data(), 1, png.size(), out) == png.size();
    const bool closed = out && std::fclose(out) == 0;
    if (!written || !closed) {
        std::error_code ec;
        std::filesystem::remove(PathFromUtf8(path), ec);
        SetStatus(Tr("The screenshot could not be kept."));
        return;
    }
    StageAttachment(path, true);
}

void ChatCubeWindow::EndScreenshot() {
    screenShot_ = DesktopScreenImage();     // the whole screen's pixels, let go of
    screenshotRunning_ = false;
    if (!window_) return;
    window_->Show();
    window_->RaiseAndFocus();
    if (input_) input_->SetFocus(true);
}

void ChatCubeWindow::StageAttachment(const std::string& path, bool ownFile) {
    if (activeConversation_.empty() || path.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(PathFromUtf8(path), ec)) {
        SetStatus(TrF("There is no file {0}", path));
        return;
    }
    if (!AttachmentsPossible()) {
        SetStatus(Tr("This network does not take pictures or files yet."));
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
    OnLook(tile, [](UltraCanvasContainer& c) {
        c.SetBackgroundColor(L().panel);
        c.SetBorders(1.0f, L().line, 10.0f);
    });
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
        SetInk(name, &Look::ink);
        name->SetWrap(TextWrap::WrapNone);
        name->SetTooltip(fileName);
        tile->AddChild(name);
        const int64_t bytes = static_cast<int64_t>(std::filesystem::file_size(PathFromUtf8(path), ec));
        auto size = std::make_shared<UltraCanvasLabel>(id + "-size", 10, 34, textWidth, 16);
        size->SetText(bytes > 0 ? FormatSize(bytes) : std::string());
        size->SetFontSize(kTextFont - 1.0f);
        SetInk(size, &Look::inkMuted);
        tile->AddChild(size);
    }
    // The x, over the tile's top right corner (a non-zero offset places it
    // there, outside the flow).
    auto remove = std::make_shared<UltraCanvasButton>(id + "-remove", width - kTileClose - 3, 3, kTileClose, kTileClose, "");
    // White on a dark disc over the picture, red under the pointer.
    OnLook(remove, [](UltraCanvasButton& b) {
        ButtonStyle st = b.GetStyle();
        st.cornerRadius = kTileClose / 2.0f;
        st.borderWidth = 0.0f;
        st.normalColor = Color(0, 0, 0, 150);
        st.hoverColor = L().danger;
        st.pressedColor = Darker(L().danger, 24);
        st.normalTextColor = st.hoverTextColor = st.pressedTextColor = Color(255, 255, 255, 255);
        b.SetStyle(st);
    });
    remove->SetIcon(IconPath("close"));
    remove->SetIconSize(12, 12);
    remove->SetUseIconAsMask(true);
    remove->SetTooltip(Tr("Remove"));
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
    const std::string key = settings_.ctrlEnterSends ? Tr("Ctrl+Enter") : Tr("Enter");
    send_->SetTooltip(staged_.empty()       ? TrF("Send ({0})", key)
                      : staged_.size() == 1 ? TrF("Send the file ({0})", key)
                                            : TrF("Send the files ({0})", key));
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::SendAttachment(const std::string& path, const std::string& caption, const std::string& replyTo,
                                    double seconds, bool voice) {
    if (activeConversation_.empty() || path.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(PathFromUtf8(path), ec)) {
        SetStatus(TrF("There is no file {0}", path));
        return;
    }
    const UltraIMMessageType kind = voice ? UltraIMMessageType::Audio : AttachmentKind(path);

    // Optimistic, like a text: the bubble shows the file at once; what takes
    // time - a picture's size, a video's preview picture and length - is
    // found in the background and the bubble follows.
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
    message.attachment.durationSeconds = seconds;
    message.replyTo.messageId = replyTo;
    message.timestampMs = NowMs();
    message.isOwn = true;
    message.deliveryStatus = UltraIMDeliveryStatus::Sending;
    messages_->Upsert(message);
    // A file of some size shows its bar from the start.
    if (message.attachment.fileSizeBytes > 256 * 1024)
        messages_->SetTransfer(message.clientMessageId, {0, message.attachment.fileSizeBytes, true});
    ScrollToEnd();

    UltraIMSendOptions options;
    options.clientMessageId = message.clientMessageId;
    options.replyToMessageId = replyTo;
    options.durationSeconds = seconds;
    const std::string provider = activeProvider_;
    const std::string conversation = activeConversation_;
    const bool voiceNotes = UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::VoiceNotes);
    sendQueue_->Post([this, message, provider, conversation, path, caption, kind, voice, voiceNotes, options]() mutable {
        // What the bubble can show before the file is gone: a picture's size,
        // a video's preview picture, size and length.
        UltraIMAttachment found = message.attachment;
        bool learned = false;
        if (IsPictureFile(path)) {
            // GetFresh: not the shared cache, from this thread.
            UCImagePtr image = UCImage::GetFresh(path);
            if (image && image->IsValid()) {
                found.width = image->GetWidth();
                found.height = image->GetHeight();
                learned = true;
            }
        } else if (kind == UltraIMMessageType::Video) {
            VideoFileInfo facts;
            if (ReadVideoFileInfo(path, facts)) {
                if (facts.duration > 0) found.durationSeconds = facts.duration;
                found.width = facts.displayWidth > 0 ? facts.displayWidth : facts.width;
                found.height = facts.displayHeight > 0 ? facts.displayHeight : facts.height;
                learned = true;
            }
            int w = 0, h = 0;
            // The provider asks for the same preview; it is made once.
            const std::string preview = UltraIM_MakePreview(path, "video/mp4", 320, w, h);
            if (!preview.empty()) {
                found.thumbnailPath = preview;
                if (found.width <= 0 || found.height <= 0) {
                    found.width = w;
                    found.height = h;
                }
                learned = true;
            }
            if (found.durationSeconds > 0) options.durationSeconds = found.durationSeconds;
        }
        if (learned) {
            OnUI([this, id = message.clientMessageId, found]() {
                const UltraIMMessage* shown = messages_->FindMessage(id);
                if (!shown) return;
                UltraIMMessage updated = *shown;
                updated.attachment.width = found.width;
                updated.attachment.height = found.height;
                updated.attachment.thumbnailPath = found.thumbnailPath;
                if (found.durationSeconds > 0) updated.attachment.durationSeconds = found.durationSeconds;
                messages_->Upsert(updated);
            });
        }
        UltraIMResult r;
        if (voice && voiceNotes) r = UltraIM_SendVoiceNote(provider, conversation, path, options);
        else if (kind == UltraIMMessageType::Image) r = UltraIM_SendImage(provider, conversation, path, caption, options);
        else if (kind == UltraIMMessageType::Video) r = UltraIM_SendVideo(provider, conversation, path, caption, options);
        else r = UltraIM_SendFile(provider, conversation, path, options);   // a recording, where no voice messages
        if (r) return;
        OnUI([this, id = message.clientMessageId, why = r.message]() {
            messages_->ClearTransfer(id);
            messages_->UpdateStatus(id, UltraIMDeliveryStatus::Failed);
            SetStatus(TrF("Not sent: {0}", why));
        });
    });
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
                                 ? (m.type == UltraIMMessageType::Video ? Tr("the video") : Tr("the file"))
                                 : m.attachment.fileName;
    SetStatus(TrF("Fetching {0}\xE2\x80\xA6", name));
    UltraIM_DownloadAttachment(activeProvider_, m.conversationId, m.messageId,
                               [this, name](const UltraIMResult& r, const UltraIMAttachment& a) {
        if (!r) {
            SetStatus(TrF("Could not fetch {0}: {1}", name, r.message));
            return;
        }
        SetStatus("");
        ShowAttachment(a.localPath);
    });
}

// ============================================================================
// REPLY AND EDIT
// ============================================================================

namespace {

// A message's words on one line, at most `max` bytes (never in a letter).
std::string OneLine(std::string text, size_t max) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    if (text.size() <= max) return text;
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "\xE2\x80\xA6";
}

// What stands for a message in the bar over the field: its words, else
// what it is.
std::string MessageGist(const UltraIMMessage& m) {
    if (!m.text.empty()) return m.text;
    switch (m.type) {
        case UltraIMMessageType::Image:   return Tr("Photo");
        case UltraIMMessageType::Video:   return Tr("Video");
        case UltraIMMessageType::Audio:   return Tr("Voice message");
        case UltraIMMessageType::Sticker: return Tr("Sticker");
        default: break;
    }
    return m.attachment.fileName.empty() ? Tr("File") : m.attachment.fileName;
}

bool Settled(const UltraIMMessage& m) {
    return m.deliveryStatus != UltraIMDeliveryStatus::Sending && m.deliveryStatus != UltraIMDeliveryStatus::Failed;
}

} // namespace

bool ChatCubeWindow::CanReply(const UltraIMMessage& message) const {
    if (message.type == UltraIMMessageType::SystemEvent || message.messageId.empty() || !Settled(message)) return false;
    UltraIMProviderInfo info;
    return UltraIM_GetProviderInfo(activeProvider_, info) &&
           UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::ReplyToMessage);
}

bool ChatCubeWindow::CanEdit(const UltraIMMessage& message) const {
    // Our own texts, once they have gone.
    if (!message.isOwn || message.type != UltraIMMessageType::Text || !Settled(message)) return false;
    UltraIMProviderInfo info;
    return UltraIM_GetProviderInfo(activeProvider_, info) &&
           UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::EditMessage);
}

void ChatCubeWindow::StartReply(const UltraIMMessage& message) {
    if (!editing_.clientMessageId.empty()) {
        editing_ = UltraIMMessage();
        input_->SetText("");
    }
    replyTo_ = message;
    UpdateComposerBar();
    input_->SetFocus(true);
}

void ChatCubeWindow::StartEdit(const UltraIMMessage& message) {
    if (voiceRecorder_) return;
    replyTo_ = UltraIMMessage();
    editing_ = message;
    input_->SetText(message.text);
    input_->MoveCursorToEnd();          // typing goes on where the words end
    UpdateComposerBar();
    input_->SetFocus(true);
}

void ChatCubeWindow::SaveEdit(const std::string& text) {
    UltraIMMessage message = editing_;
    if (text == message.text) {
        ClearComposerMode();
        return;
    }
    if (text.empty()) {
        SetStatus(Tr("A message cannot be empty - delete it instead."));
        return;
    }
    const UltraIMResult r = UltraIM_EditMessage(activeProvider_, message.conversationId, message.messageId, text);
    if (!r) {
        SetStatus(TrF("Could not change the message: {0}", r.message));
        return;
    }
    // At once here; the provider's MessageUpdate follows.
    message.text = text;
    message.isEdited = true;
    if (message.conversationId == activeConversation_) messages_->Upsert(message);
    ClearComposerMode();
}

void ChatCubeWindow::ClearComposerMode() {
    const bool wasEditing = !editing_.clientMessageId.empty();
    replyTo_ = UltraIMMessage();
    editing_ = UltraIMMessage();
    if (wasEditing && input_) input_->SetText("");
    UpdateComposerBar();
}

void ChatCubeWindow::UpdateComposerBar() {
    if (!composerBar_) return;
    const bool editing = !editing_.clientMessageId.empty();
    const bool replying = !replyTo_.messageId.empty();
    if (editing) {
        composerBarTitle_->SetText(Tr("Edit message"));
        composerBarText_->SetText(OneLine(editing_.text, 90));
    } else if (replying) {
        const std::string who = replyTo_.isOwn ? Tr("yourself")
                                : !replyTo_.senderName.empty() ? replyTo_.senderName
                                : (conversations_->Find(activeConversation_) ? conversations_->Find(activeConversation_)->title
                                                                             : std::string());
        composerBarTitle_->SetText(TrF("Reply to {0}", who));
        composerBarText_->SetText(OneLine(MessageGist(replyTo_), 90));
    }
    composerBar_->SetVisible(editing || replying);
    const std::string key = settings_.ctrlEnterSends ? Tr("Ctrl+Enter") : Tr("Enter");
    if (editing) send_->SetTooltip(TrF("Save the change ({0})", key));
    else UpdateAttachmentStrip();
    if (window_) window_->RequestRedraw();
}

// ============================================================================
// SEARCH
// ============================================================================

void ChatCubeWindow::ScheduleSearch() {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) return;
    if (searchTimer_) app->StopTimer(searchTimer_);
    // A moment after the last key, not at every key.
    searchTimer_ = app->StartTimer(350, false, [this](TimerId) {
        searchTimer_ = 0;
        RunSearch();
    });
}

void ChatCubeWindow::RunSearch() {
    if (searchTimer_) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(searchTimer_);
        searchTimer_ = 0;
    }
    if (activeConversation_.empty()) return;
    const std::string text = Trim(searchInput_->GetText());
    if (text == searchText_ && searchKind_ == shownKind_) return;
    searchText_ = text;
    shownKind_ = searchKind_;
    if (text.empty() && searchKind_ == UltraIMSearchKind::AnyKind) {
        searchInfo_->SetText("");
        LoadMessages();
        return;
    }
    searchInfo_->SetText(Tr("Searching\xE2\x80\xA6"));
    const int serial = ++searchSerial_;
    const std::string conversationId = activeConversation_;
    const std::string provider = activeProvider_;
    UltraIMMessageSearch search;
    search.conversationId = conversationId;
    search.text = text;
    search.kind = searchKind_;
    search.limit = 200;
    // The matches, oldest first like the chat.
    auto show = [this, serial, conversationId](std::vector<UltraIMMessage> found, bool more) {
        if (serial != searchSerial_ || conversationId != activeConversation_) return;
        std::stable_sort(found.begin(), found.end(), [](const UltraIMMessage& a, const UltraIMMessage& b) {
            return a.timestampMs < b.timestampMs;
        });
        messages_->SetMessages(found);
        ScrollToEnd();
        for (const UltraIMMessage& m : found) EnsureVideoPreview(m);
        const long long n = static_cast<long long>(found.size());
        searchInfo_->SetText(n == 0 ? Tr("Nothing found")
                             : more ? TrN("the newest {n} found", "the newest {n} found", n)
                                    : TrN("{n} found", "{n} found", n));
        if (window_) window_->RequestRedraw();
    };
    UltraIMProviderInfo info;
    if (UltraIM_GetProviderInfo(provider, info) &&
        UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::SearchMessages)) {
        UltraIM_SearchMessages(provider, search, [this, show, serial](const UltraIMResult& r, const UltraIMSearchPage& page) {
            if (!r && serial == searchSerial_) SetStatus(TrF("Could not search: {0}", r.message));
            show(page.messages, !page.nextCursor.empty());
        });
        return;
    }
    // A network that cannot search: its history as far as it gives it, filtered here.
    UltraIM_FetchMessages(provider, conversationId, "", 1000,
                          [show, search](const UltraIMResult&, const std::vector<UltraIMMessage>& list) {
        std::vector<UltraIMMessage> found;
        for (const UltraIMMessage& m : list) {
            if (UltraIM_MessageMatchesSearch(m, search)) found.push_back(m);
        }
        show(found, false);
    });
}

void ChatCubeWindow::ClearSearch(bool reload) {
    if (searchTimer_) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(searchTimer_);
        searchTimer_ = 0;
    }
    const bool searched = !searchText_.empty() || shownKind_ != UltraIMSearchKind::AnyKind;
    searchText_.clear();
    searchKind_ = shownKind_ = UltraIMSearchKind::AnyKind;
    for (const auto& [kind, chip] : filterChips_) StyleButton(chip, kind == UltraIMSearchKind::AnyKind);
    if (searchInput_) {
        settingSearch_ = true;
        searchInput_->SetText("");
        settingSearch_ = false;
    }
    if (searchInfo_) searchInfo_->SetText("");
    if (reload && searched) LoadMessages();
}

// ============================================================================
// RECORDINGS
// ============================================================================

namespace {

// The format a voice message is written in: the smallest this build can
// write (FileLoader's inventory says which), else WAV.
std::pair<AudioFormat, std::string> VoiceFormat() {
    const auto formats = UltraCanvasSupportedFormats::GetByCategory(MediaFormatCategory::Audio);
    for (const char* wanted : {"opus", "ogg", "m4a", "mp3"}) {
        for (const auto& f : formats) {
            if (!f.canSave) continue;
            const bool named = f.extension == wanted ||
                               std::find(f.aliases.begin(), f.aliases.end(), wanted) != f.aliases.end();
            const AudioFormat format = AudioFormatFromExtension(wanted);
            if (named && format != AudioFormat::Unknown) return {format, wanted};
        }
    }
    return {AudioFormat::WAV, "wav"};
}

} // namespace

std::string ChatCubeWindow::RecordingPath(const std::string& prefix, const std::string& extension) const {
    std::error_code ec;
    const std::filesystem::path folder = UltraCanvasDataFolder("ChatCube") / "cache" / "recorded";
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
    std::string name = prefix + "-" + stamp;
    for (int n = 1; std::filesystem::exists(folder / PathFromUtf8(name + "." + extension), ec); ++n)
        name = prefix + "-" + stamp + "-" + std::to_string(n);
    return PathToUtf8(folder / PathFromUtf8(name + "." + extension));
}

void ChatCubeWindow::StartVoiceRecording() {
    if (voiceRecorder_ || activeConversation_.empty()) return;
    if (!AttachmentsPossible()) {
        SetStatus(Tr("This network does not take pictures or files yet."));
        return;
    }
    if (!HaveMediaPermission(false, [this]() { StartVoiceRecording(); })) return;
    if (!editing_.clientMessageId.empty()) ClearComposerMode();
    StopVoicePlayback();
    CloseVideoRecorder(false);
    AudioCaptureConfig config;
    config.sampleRate = 48000;
    config.channels = 1;
    config.maxDurationMs = 15 * 60 * 1000;
    auto recorder = std::make_shared<UltraCanvasAudioRecorder>(config);
    // From the audio thread: onto the UI's.
    recorder->onMaxDurationReached = [this]() {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent())
            app->PostToUIThread([this]() { FinishVoiceRecording(true); });
    };
    recorder->onError = [this](const std::string& message) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent())
            app->PostToUIThread([this, message]() { SetStatus(TrF("Microphone: {0}", message)); });
    };
    if (!recorder->Open() || !recorder->Start()) {
        const std::string why = recorder->GetLastError();
        recorder->Close();
        SetStatus(why.empty() ? Tr("The microphone could not be opened.")
                              : TrF("The microphone could not be opened: {0}", why));
        return;
    }
    voiceRecorder_ = recorder;
    composerRow_->SetVisible(false);
    recordBar_->SetVisible(true);
    UpdateRecordBar();
    if (auto* app = UltraCanvasApplicationBase::GetCurrent())
        recordTimer_ = app->StartTimer(250, true, [this](TimerId) { UpdateRecordBar(); });
}

void ChatCubeWindow::UpdateRecordBar() {
    if (!voiceRecorder_) return;
    recordText_->SetText("\xE2\x97\x8F  " + TrF("Recording {0}", FormatDuration(voiceRecorder_->GetElapsed())));
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::FinishVoiceRecording(bool send) {
    if (!voiceRecorder_) return;
    std::shared_ptr<UltraCanvasAudioRecorder> recorder = voiceRecorder_;
    voiceRecorder_.reset();
    if (recordTimer_) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(recordTimer_);
        recordTimer_ = 0;
    }
    const double seconds = recorder->GetElapsed();
    recorder->Stop();
    recordBar_->SetVisible(false);
    composerRow_->SetVisible(true);
    if (send && seconds < 0.5) {
        SetStatus(Tr("Too short to send - record a little longer."));
    } else if (send) {
        auto [format, extension] = VoiceFormat();
        std::string path = RecordingPath("Voice", extension);
        bool saved = recorder->SaveToFile(path, format);
        if (!saved && format != AudioFormat::WAV) {
            path = RecordingPath("Voice", "wav");
            saved = recorder->SaveToFile(path, AudioFormat::WAV);
        }
        if (!saved) {
            SetStatus(TrF("The recording could not be saved: {0}", recorder->GetLastError()));
        } else {
            if (!searchText_.empty() || shownKind_ != UltraIMSearchKind::AnyKind) ClearSearch(true);
            SendAttachment(path, std::string(), replyTo_.messageId, seconds, true);
            ClearComposerMode();
        }
    }
    recorder->Discard();
    recorder->Close();
    if (input_) input_->SetFocus(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::OpenVideoRecorder() {
    if (activeConversation_.empty() || videoNotePanel_->IsVisible()) return;
    if (!AttachmentsPossible()) {
        SetStatus(Tr("This network does not take pictures or files yet."));
        return;
    }
    if (!HaveMediaPermission(true, [this]() { OpenVideoRecorder(); })) return;
    FinishVoiceRecording(false);
    StopVoicePlayback();
    // The camera opens as a preview; the red button records into ChatCube's
    // cache, and again stops.
    VideoCaptureConfig config = videoRecorder_->GetConfig();
    config.width = 640;
    config.height = 480;
    config.captureAudio = true;
    config.maxDurationMs = 5 * 60 * 1000;
    config.outputPath.clear();
    videoRecorder_->SetConfig(config);
    videoNotePath_.clear();
    videoNoteReady_ = false;
    videoNoteStartedMs_ = 0;
    videoNoteSeconds_ = 0;
    videoNoteSend_->SetDisabled(true);
    videoNoteText_->SetText(Tr("Press the red button to record, and again to stop."));
    videoNotePanel_->SetVisible(true);
    if (!videoRecorder_->OpenCamera(true)) {
        auto engine = videoRecorder_->GetRecorder();
        const std::string why = engine ? engine->GetLastError() : std::string();
        videoNoteText_->SetText(why.empty() ? Tr("The camera could not be opened.")
                                            : TrF("The camera could not be opened: {0}", why));
    }
    videoRecorder_->SetOutputPath(RecordingPath("Video", "mp4"));
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::CloseVideoRecorder(bool keepFile) {
    if (!videoNotePanel_ || !videoNotePanel_->IsVisible()) return;
    auto engine = videoRecorder_->GetRecorder();
    if (engine && (engine->GetState() == VideoRecordingState::Recording ||
                   engine->GetState() == VideoRecordingState::Paused))
        videoRecorder_->StopRecording();
    videoRecorder_->CloseCamera();
    videoNotePanel_->SetVisible(false);
    // A recording not sent is not kept.
    std::error_code ec;
    if (!keepFile) {
        const std::string path = engine ? engine->GetConfig().outputPath : std::string();
        if (!path.empty()) std::filesystem::remove(PathFromUtf8(path), ec);
    }
    videoNoteReady_ = false;
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::SendVideoMessage() {
    if (!videoNoteReady_ || videoNotePath_.empty() || activeConversation_.empty()) return;
    const std::string path = videoNotePath_;
    const double seconds = videoNoteSeconds_;
    CloseVideoRecorder(true);
    if (!searchText_.empty() || shownKind_ != UltraIMSearchKind::AnyKind) ClearSearch(true);
    SendAttachment(path, Trim(input_->GetText()), replyTo_.messageId, seconds, false);
    input_->SetText("");
    ClearComposerMode();
}

void ChatCubeWindow::ToggleVoicePlayback(int row) {
    const auto& rows = messages_->Rows();
    if (row < 0 || row >= static_cast<int>(rows.size())) return;
    const UltraIMMessage m = rows[static_cast<size_t>(row)].message;
    if (playingVoice_ == m.clientMessageId) {
        StopVoicePlayback();
        return;
    }
    StopVoicePlayback();
    if (m.attachment.isDownloaded && !m.attachment.localPath.empty()) {
        PlayVoice(m.clientMessageId, m.attachment.localPath);
        return;
    }
    if (!Settled(m)) return;
    SetStatus(Tr("Fetching the voice message\xE2\x80\xA6"));
    const std::string id = m.clientMessageId;
    UltraIM_DownloadAttachment(activeProvider_, m.conversationId, m.messageId,
                               [this, id](const UltraIMResult& r, const UltraIMAttachment& a) {
        if (!r) {
            SetStatus(TrF("Could not fetch the voice message: {0}", r.message));
            return;
        }
        SetStatus("");
        PlayVoice(id, a.localPath);
    });
}

void ChatCubeWindow::PlayVoice(const std::string& clientId, const std::string& path) {
    auto player = std::make_shared<UltraCanvasAudioPlayer>();
    if (path.empty() || !player->LoadFromFile(path) || !player->Play()) {
        // Not a sound this build plays itself: the media viewer may.
        ShowAttachment(path);
        return;
    }
    player->onEnded = [this, clientId]() {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent())
            app->PostToUIThread([this, clientId]() {
                if (playingVoice_ == clientId) StopVoicePlayback();
            });
    };
    voicePlayer_ = player;
    playingVoice_ = clientId;
    bubbles_->SetPlaying(clientId);
    if (messageList_) messageList_->RequestRedraw();
}

void ChatCubeWindow::StopVoicePlayback() {
    if (voicePlayer_) {
        voicePlayer_->onEnded = nullptr;
        voicePlayer_->Stop();
        voicePlayer_.reset();
    }
    playingVoice_.clear();
    if (bubbles_) bubbles_->SetPlaying("");
    if (messageList_) messageList_->RequestRedraw();
}

void ChatCubeWindow::SetSearchKind(UltraIMSearchKind kind) {
    searchKind_ = kind;
    for (const auto& [k, chip] : filterChips_) StyleButton(chip, k == kind);
    RunSearch();
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::PlayVideoInChat(int row) {
    const auto& rows = messages_->Rows();
    if (row < 0 || row >= static_cast<int>(rows.size())) return;
    const UltraIMMessage m = rows[static_cast<size_t>(row)].message;
    const std::string title = m.attachment.fileName.empty() ? Tr("Video") : m.attachment.fileName;
    if (m.attachment.isDownloaded && !m.attachment.localPath.empty()) {
        ShowVideoPlayer(m.attachment.localPath, title);
        return;
    }
    if (!Settled(m)) return;
    SetStatus(TrF("Fetching {0}\xE2\x80\xA6", title));
    const std::string conversation = activeConversation_;
    UltraIM_DownloadAttachment(activeProvider_, m.conversationId, m.messageId,
                               [this, title, conversation](const UltraIMResult& r, const UltraIMAttachment& a) {
        if (!r) {
            SetStatus(TrF("Could not fetch {0}: {1}", title, r.message));
            return;
        }
        SetStatus("");
        if (conversation == activeConversation_) ShowVideoPlayer(a.localPath, title);
    });
}

void ChatCubeWindow::ShowVideoPlayer(const std::string& path, const std::string& title) {
    if (path.empty()) return;
    StopVoicePlayback();
    CloseVideoRecorder(false);
    if (!videoPlayer_->LoadFromFile(path)) {
        // Not a video this build plays itself: the media viewer may.
        ShowAttachment(path);
        return;
    }
    videoPlayerTitle_->SetText(title);
    videoPlayerPanel_->SetVisible(true);
    videoPlayer_->Play();
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::CloseVideoPlayer() {
    if (!videoPlayerPanel_ || !videoPlayerPanel_->IsVisible()) return;
    videoPlayer_->Unload();          // lets go of the file
    videoPlayerPanel_->SetVisible(false);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::EnsureVideoPreview(const UltraIMMessage& m) {
    if (m.type != UltraIMMessageType::Video || !m.attachment.thumbnailPath.empty()) return;
    // Made before (the chat loaded again, a search's results): at once.
    const auto apply = [this](const std::string& id, const UltraIMAttachment& found) {
        const UltraIMMessage* shown = messages_->FindMessage(id);
        if (!shown) return;
        UltraIMMessage updated = *shown;
        if (!found.thumbnailPath.empty()) updated.attachment.thumbnailPath = found.thumbnailPath;
        if (found.width > 0 && found.height > 0) {
            updated.attachment.width = found.width;
            updated.attachment.height = found.height;
        }
        if (found.durationSeconds > 0 && updated.attachment.durationSeconds <= 0)
            updated.attachment.durationSeconds = found.durationSeconds;
        messages_->Upsert(updated);
    };
    auto made = previewsMade_.find(m.clientMessageId);
    if (made != previewsMade_.end()) {
        apply(m.clientMessageId, made->second);
        return;
    }
    if (!m.attachment.isDownloaded || m.attachment.localPath.empty() || !previewAsked_.insert(m.clientMessageId).second)
        return;
    const std::string id = m.clientMessageId;
    const std::string path = m.attachment.localPath;
    previewQueue_->Post([this, id, path, apply]() {
        UltraIMAttachment found;
        VideoFileInfo facts;
        if (ReadVideoFileInfo(path, facts)) {
            found.durationSeconds = facts.duration;
            found.width = facts.displayWidth > 0 ? facts.displayWidth : facts.width;
            found.height = facts.displayHeight > 0 ? facts.displayHeight : facts.height;
        }
        int w = 0, h = 0;
        found.thumbnailPath = UltraIM_MakePreview(path, "video/mp4", 320, w, h);
        if (found.width <= 0 || found.height <= 0) {
            found.width = w;
            found.height = h;
        }
        if (found.thumbnailPath.empty() && found.width <= 0) return;
        OnUI([this, id, found, apply]() {
            previewsMade_[id] = found;
            apply(id, found);
        });
    });
}

void ChatCubeWindow::OnUI(std::function<void()> f) {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app || !f) return;
    app->PostToUIThread([alive = alive_, f = std::move(f)]() {
        if (*alive) f();
    });
}

ChatCubeWindow::SendQueue::SendQueue() : thread_([this]() { Run(); }) {}

ChatCubeWindow::SendQueue::~SendQueue() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void ChatCubeWindow::SendQueue::Post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void ChatCubeWindow::SendQueue::Run() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this]() { return stop_ || !jobs_.empty(); });
            if (jobs_.empty()) return;          // stopping, and everything went
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
    }
}

void ChatCubeWindow::ShowAttachment(const std::string& path) {
    if (path.empty()) return;
    MediaViewerWindowOptions options;
    options.browseFolder = false;       // the provider's cache is no album
    if (!viewer_.Show(path, window_.get(), options)) SetStatus(TrF("Could not open {0}", path));
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
    if (CanReply(message)) {
        messageMenu_->AddItem(MenuItemData::Action(Tr("Reply"), [this, message]() { StartReply(message); }));
        ++entries;
    }
    if (CanEdit(message)) {
        messageMenu_->AddItem(MenuItemData::Action(Tr("Edit"), [this, message]() { StartEdit(message); }));
        ++entries;
    }
    if (!message.text.empty()) {
        messageMenu_->AddItem(MenuItemData::Action(Tr("Copy text"), [this, message]() {
            if (SetClipboardText(message.text)) SetStatus(Tr("The text is on the clipboard."));
        }));
        ++entries;
    }
    if (hasFile && (sent || message.attachment.isDownloaded)) {
        if (entries) messageMenu_->AddItem(MenuItemData::Separator());
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
    if (!HaveMediaPermission(video, [this, video]() { StartCall(video); })) return;
    std::string callId;
    const UltraIMResult r = UltraIM_StartCall(activeProvider_, activeConversation_, video, callId);
    if (!r) {
        SetStatus(TrF("Could not call: {0}", r.message));
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
    remote->SetCaption(Tr("Connecting\xE2\x80\xA6"));
    self->SetCaption(cameraOn_ ? Tr("Starting camera\xE2\x80\xA6") : Tr("Camera off"));
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
        selfVideo_->SetCaption(Tr("Camera off"));
    } else {
        selfVideo_->SetCaption(Tr("Starting camera\xE2\x80\xA6"));
    }
    UpdateCallBar();
}

void ChatCubeWindow::AcceptCall() {
    if (callId_.empty() || !callIncoming_ || callState_ != UltraIMCallState::Ringing) return;
    if (!HaveMediaPermission(callVideo_, [this]() { AcceptCall(); })) return;
    const UltraIMResult r = UltraIM_AcceptCall(providerId_, callId_, callVideo_);
    if (!r) SetStatus(TrF("Could not answer: {0}", r.message));
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
                text = !callIncoming_ ? TrF("Calling {0}\xE2\x80\xA6", name)
                       : callVideo_     ? TrF("Incoming video call from {0}", name)
                                        : TrF("Incoming call from {0}", name);
                break;
            case UltraIMCallState::Connecting:
                text = TrF("Connecting to {0}\xE2\x80\xA6", name);
                break;
            case UltraIMCallState::Active: {
                const int64_t seconds = std::max<int64_t>(0, (NowMs() - callActiveSinceMs_) / 1000);
                char clock[16];
                std::snprintf(clock, sizeof(clock), "%02d:%02d", static_cast<int>(seconds / 60),
                              static_cast<int>(seconds % 60));
                text = TrF("In call with {0}   {1}", name, clock);
                if (callMuted_) text += "   " + Tr("(muted)");
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
        callCamera_->SetText(cameraOn_ ? Tr("Camera off") : Tr("Camera on"));
        callMute_->SetText(callMuted_ ? Tr("Unmute") : Tr("Mute"));
        callEnd_->SetText(ringingHere ? Tr("Decline") : (callState_ == UltraIMCallState::Ringing ? Tr("Cancel") : Tr("Hang up")));
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
        SetStatus(callVideo_ ? TrF("Incoming video call from {0}", ContactName(e.contactId))
                             : TrF("Incoming call from {0}", ContactName(e.contactId)));
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
            remoteVideo_->SetCaption(TrF("{0} turned the camera off", ContactName(callContact_)));
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
        if (e.errorText.rfind(Tr("No camera"), 0) == 0) selfVideo_->SetCaption(Tr("No camera"));
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
        if (e.errorText == "declined")       why = TrF("{0} declined the call", name);
        else if (e.errorText == "busy")      why = TrF("{0} is in another call", name);
        else if (e.errorText == "no answer") why = TrF("{0} did not answer", name);
        else if (e.errorText == "missed")    why = TrF("Missed call from {0}", name);
        else if (e.errorText == "offline")   why = TrF("{0} is offline", name);
        else if (!e.errorText.empty())       why = TrF("Call ended: {0}", e.errorText);
        else                                 why = Tr("Call ended");
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

void ChatCubeWindow::ApplyLook() {
    RestyleForLook();
    UpdateHeader();
    if (conversationList_) conversationList_->RequestRedraw();
    if (messageList_) messageList_->RequestRedraw();
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ApplySettings() {
    // The look: built again when the Settings window changed it.
    if (SetLookChoice(settings_.look))
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->ApplyThemePolicy();
    // The send key, in the field's hint and on the send button.
    if (input_) {
        input_->SetPlaceholder(settings_.ctrlEnterSends
                                   ? Tr("Message\xE2\x80\xA6  (Ctrl+Enter sends, Enter for a new line)")
                                   : Tr("Message\xE2\x80\xA6  (Enter sends, Shift+Enter for a new line)"));
    }
    if (send_) UpdateAttachmentStrip();     // the button's tooltip names the key
    ApplyFeatureIcons();                    // Display > Feature icons
    // Typing: nothing more goes out, and what was shown goes.
    if (!settings_.sendTyping && !activeConversation_.empty() && lastTypingSentMs_ != 0) {
        lastTypingSentMs_ = 0;
        UltraIM_SetTyping(activeProvider_, activeConversation_, false);
    }
    if (!settings_.showTyping && !typing_.empty()) {
        const auto shown = typing_;
        typing_.clear();
        for (const auto& conversation : shown) conversations_->SetTyping(conversation.first, std::string());
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
        {providerId_, "ChatCube Direct"}, {matrixProviderId_, "Matrix"}, {telegramProviderId_, "Telegram"},
        {mastodonProviderId_, "Mastodon"}, {mattermost_.providerId, "Mattermost"}, {zulip_.providerId, "Zulip"},
        {signalProviderId_, "Signal"}, {xmpp_.providerId, "XMPP"}, {irc_.providerId, "IRC"}};
    for (const auto& [id, name] : networks) {
        ChatCubeSettingsWindow::Account account;
        account.providerId = id;
        account.name = name;
        UltraIMProviderInfo info;
        if (!UltraIM_GetProviderInfo(id, info)) {
            account.state = Tr("Not in this ChatCube: its module was not found (Plugins/UltraIM).");
            accounts.push_back(account);
            continue;
        }
        const bool direct = id == providerId_;
        switch (info.connectionState) {
            case UltraIMConnectionState::Connected:
                account.state = direct ? Tr("Connected.") : TrF("Signed in as {0}.", info.localUserId);
                account.canSignOut = !direct;
                break;
            case UltraIMConnectionState::Connecting:
                account.state = Tr("Signing in\xE2\x80\xA6");
                account.canSignOut = !direct;
                break;
            case UltraIMConnectionState::Reconnecting:
                account.state = Tr("The connection was lost - reconnecting\xE2\x80\xA6");
                account.canSignOut = !direct && !info.localUserId.empty();
                break;
            case UltraIMConnectionState::Error:
                account.state = Tr("Not connected: the last attempt failed.");
                account.canSignOut = !direct && info.hasStoredSession;
                break;
            case UltraIMConnectionState::Disconnected:
                account.state = direct ? Tr("Not connected.") : Tr("Not signed in.");
                break;
        }
        if (!info.version.empty()) account.state += "  " + TrF("Module {0}.", info.version);
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
        SetNetworkButton(matrixToggle_, "Matrix", Tr("Matrix account"));
        matrixKeysForm_->SetVisible(false);
        SetProviderStatus(matrixProviderId_, Tr("signed out"));
    } else if (providerId == telegramProviderId_) {
        SetNetworkButton(telegramToggle_, "Telegram", Tr("Telegram account"));
        telegramForm_->SetVisible(false);
        SetProviderStatus(telegramProviderId_, Tr("signed out"));
    } else if (providerId == mastodonProviderId_) {
        SetNetworkButton(mastodonToggle_, "Mastodon", Tr("Mastodon account"));
        mastodonForm_->SetVisible(false);
        SetProviderStatus(mastodonProviderId_, Tr("signed out"));
    } else if (providerId == signalProviderId_) {
        SetNetworkButton(signalToggle_, "Signal", TrF("{0} account", std::string("Signal")));
        signalForm_->SetVisible(false);
        SetProviderStatus(signalProviderId_, Tr("signed out"));
    } else if (AccountForm* f = AccountFormOf(providerId)) {
        SetNetworkButton(f->toggle, f->name, TrF("{0} account", f->name));
        f->form->SetVisible(false);
        SetProviderStatus(f->providerId, Tr("signed out"));
    }
    if (conversationList_) conversationList_->RequestRedraw();
    if (window_) window_->RequestRedraw();
}

// =================================================================== UltraIM

void ChatCubeWindow::OnEvent(const UltraIMEvent& e) {
    if (e.providerId != providerId_ && e.providerId != matrixProviderId_ && e.providerId != telegramProviderId_ &&
        e.providerId != mastodonProviderId_ && e.providerId != signalProviderId_ && !AccountFormOf(e.providerId))
        return;
    if (OnAccountEvent(e) || OnSignalEvent(e)) return;
    const bool matrix = e.providerId == matrixProviderId_;
    const bool mastodon = e.providerId == mastodonProviderId_;
    const bool telegram = e.providerId == telegramProviderId_;
    switch (e.type) {
        case UltraIMEventType::MessageReceive:
            // Their message is here: they are no longer typing it.
            SetTyping(e.conversationId, e.message.senderId.empty() ? e.contactId : e.message.senderId, false);
            if (e.conversationId == activeConversation_) {
                // While a search shows its matches, only what matches joins them.
                UltraIMMessageSearch shown;
                shown.text = searchText_;
                shown.kind = shownKind_;
                if ((searchText_.empty() && shownKind_ == UltraIMSearchKind::AnyKind) ||
                    UltraIM_MessageMatchesSearch(e.message, shown))
                    messages_->Upsert(e.message);
                EnsureVideoPreview(e.message);
                ScrollToEnd();
                // Read only when it is in sight: not while a compact window
                // shows the list (opening it again marks it read).
                if (!compact_ || compactShowsChat_) UltraIM_MarkRead(e.providerId, activeConversation_);
                UpdateHeader();
            }
            break;
        case UltraIMEventType::MessageDelivered:
            if (e.conversationId == activeConversation_) {
                messages_->UpdateStatus(e.message.clientMessageId, e.deliveryStatus);
                // A bar that never moved (a network that reports no progress)
                // goes once the message has gone; one that moves stays until 100 %.
                const MessageListModel::Transfer* transfer = messages_->TransferOf(e.message.clientMessageId);
                if (transfer && transfer->done == 0 && e.deliveryStatus != UltraIMDeliveryStatus::Sending)
                    messages_->ClearTransfer(e.message.clientMessageId);
            }
            if (e.deliveryStatus == UltraIMDeliveryStatus::Failed) SetStatus(Tr("A message could not be sent."));
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
            if (c.isGroup) {
                UltraIMConversation shown = c;
                if (shown.providerId.empty()) shown.providerId = e.providerId;
                groupWindow_->Update(shown);
            }
            break;
        }
        case UltraIMEventType::ConversationRemove: {
            conversations_->Remove(e.conversationId);
            groupWindow_->Removed(e.conversationId);
            // A public group left is one to join again.
            if (e.providerId == providerId_ && e.conversationId.rfind("group:", 0) == 0) RefreshPublicGroups();
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
            if (!settings_.showTyping) break;
            SetTyping(e.conversationId, e.contactId, e.isTyping);
            break;
        case UltraIMEventType::MessageUpdate:
            // An edit, or an attachment's picture that has arrived - of a
            // message that is shown (one further back than the history
            // fetched is not appended to the end).
            if (e.conversationId == activeConversation_ && messages_->Contains(e.message.clientMessageId)) {
                // The provider's copy has no picture we made for it: keep ours.
                UltraIMMessage update = e.message;
                const UltraIMMessage* shown = messages_->FindMessage(update.clientMessageId);
                if (shown && update.attachment.thumbnailPath.empty() && !shown->attachment.thumbnailPath.empty()) {
                    update.attachment.thumbnailPath = shown->attachment.thumbnailPath;
                    if (update.attachment.width <= 0) update.attachment.width = shown->attachment.width;
                    if (update.attachment.height <= 0) update.attachment.height = shown->attachment.height;
                    if (update.attachment.durationSeconds <= 0)
                        update.attachment.durationSeconds = shown->attachment.durationSeconds;
                }
                messages_->Upsert(update);
                EnsureVideoPreview(update);
            }
            break;
        case UltraIMEventType::TransferProgress:
            // A file on its way: its bubble's bar, gone once it is all there.
            if (e.conversationId != activeConversation_) break;
            if (e.totalBytes <= 0 || e.transferredBytes >= e.totalBytes)
                messages_->ClearTransfer(e.message.clientMessageId);
            else
                messages_->SetTransfer(e.message.clientMessageId, {e.transferredBytes, e.totalBytes, e.isUpload});
            break;
        case UltraIMEventType::MessageDelete:
            if (e.conversationId == activeConversation_) messages_->Remove(e.message.messageId);
            break;
        case UltraIMEventType::ConnectionStateChange:
            if (settingsWindow_) settingsWindow_->RefreshAccounts();
            if (telegram) {
                if (e.connectionState == UltraIMConnectionState::Connected) {
                    SetProviderStatus(telegramProviderId_, Tr("signed in"));
                    SetNetworkButton(telegramToggle_, "Telegram", Tr("Telegram: signed in"));
                    RefreshConversations();
                } else if (e.connectionState == UltraIMConnectionState::Error) {
                    SetProviderStatus(telegramProviderId_, e.errorText);
                    telegramNext_->SetDisabled(false);
                    telegramQrButton_->SetDisabled(false);
                } else if (e.connectionState == UltraIMConnectionState::Reconnecting) {
                    SetProviderStatus(telegramProviderId_, e.errorText.empty() ? Tr("reconnecting\xE2\x80\xA6") : e.errorText);
                } else if (e.connectionState == UltraIMConnectionState::Connecting) {
                    // What TDLib is doing while the sign-in waits on it.
                    SetProviderStatus(telegramProviderId_, (e.errorText.empty() ? Tr("signing in") : e.errorText) + "\xE2\x80\xA6");
                } else if (e.connectionState == UltraIMConnectionState::Disconnected) {
                    SetProviderStatus(telegramProviderId_, Tr("signed out"));
                    SetNetworkButton(telegramToggle_, "Telegram", Tr("Telegram account"));
                }
                break;
            }
            if (mastodon) {
                if (e.connectionState == UltraIMConnectionState::Connected) {
                    UltraIMProviderInfo info;
                    UltraIM_GetProviderInfo(mastodonProviderId_, info);
                    SetProviderStatus(mastodonProviderId_, TrF("signed in as {0}", info.localUserId));
                    SetNetworkButton(mastodonToggle_, "Mastodon", TrF("Mastodon: signed in as {0}", info.localUserId));
                    RefreshConversations();
                } else if (e.connectionState == UltraIMConnectionState::Error) {
                    SetProviderStatus(mastodonProviderId_, e.errorText);
                    mastodonSignIn_->SetDisabled(false);
                } else if (e.connectionState == UltraIMConnectionState::Connecting) {
                    SetProviderStatus(mastodonProviderId_, Tr("signing in\xE2\x80\xA6"));
                } else if (e.connectionState == UltraIMConnectionState::Disconnected) {
                    SetProviderStatus(mastodonProviderId_, Tr("signed out"));
                    SetNetworkButton(mastodonToggle_, "Mastodon", Tr("Mastodon account"));
                }
                break;
            }
            if (matrix) {
                // Matrix speaks for itself on the status line, Direct below.
                if (e.connectionState == UltraIMConnectionState::Connected) {
                    UltraIMProviderInfo info;
                    UltraIM_GetProviderInfo(matrixProviderId_, info);
                    SetProviderStatus(matrixProviderId_, TrF("signed in as {0}", info.localUserId));
                    SetNetworkButton(matrixToggle_, "Matrix", TrF("Matrix: signed in as {0}", info.localUserId));
                    RefreshConversations();
                } else if (e.connectionState == UltraIMConnectionState::Error) {
                    SetProviderStatus(matrixProviderId_, e.errorText);
                } else if (e.connectionState == UltraIMConnectionState::Reconnecting) {
                    SetProviderStatus(matrixProviderId_, Tr("connection lost - reconnecting\xE2\x80\xA6"));
                } else if (e.connectionState == UltraIMConnectionState::Connecting) {
                    SetProviderStatus(matrixProviderId_, Tr("signing in\xE2\x80\xA6"));
                } else if (e.connectionState == UltraIMConnectionState::Disconnected) {
                    SetProviderStatus(matrixProviderId_, Tr("signed out"));
                }
                break;
            }
            switch (e.connectionState) {
                case UltraIMConnectionState::Connected:    SetProviderStatus(providerId_, Tr("connected")); break;
                case UltraIMConnectionState::Connecting:   SetProviderStatus(providerId_, Tr("connecting\xE2\x80\xA6")); break;
                case UltraIMConnectionState::Reconnecting: SetProviderStatus(providerId_, Tr("connection lost - reconnecting\xE2\x80\xA6")); break;
                case UltraIMConnectionState::Error:
                    SetProviderStatus(providerId_, TrF("error: {0}", e.errorText));
                    SetSignInStatus(e.errorText);
                    break;
                case UltraIMConnectionState::Disconnected: SetProviderStatus(providerId_, Tr("signed out")); break;
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
            if (mastodon && e.authStep == UltraIMAuthStep::Browser && !e.errorText.empty()) {
                // The server's own page: the password is typed there, not here.
                OpenURL(e.errorText);
                SetProviderStatus(mastodonProviderId_, Tr("continue in the browser\xE2\x80\xA6"));
            }
            break;
        case UltraIMEventType::AuthRequire:
            if (telegram) {
                SetProviderStatus(telegramProviderId_, Tr("the session ended - sign in again"));
                telegramForm_->SetVisible(true);
                SetNetworkButton(telegramToggle_, "Telegram", Tr("Telegram account"));
                UpdateTelegramForm();
                break;
            }
            if (mastodon) {
                SetProviderStatus(mastodonProviderId_, Tr("the session ended - sign in again"));
                mastodonForm_->SetVisible(true);
                SetNetworkButton(mastodonToggle_, "Mastodon", Tr("Mastodon account"));
                break;
            }
            if (matrix) {
                SetProviderStatus(matrixProviderId_, Tr("the session ended - sign in again"));
                matrixForm_->SetVisible(true);
                SetNetworkButton(matrixToggle_, "Matrix", Tr("Matrix account"));
                break;
            }
            SetSignInStatus(Tr("The connection ended - sign in again."));
            if (hasIdentity_) ShowConnectingPage(true);
            else ShowSignInPage();
            break;
        default:
            break;
    }
}

} // namespace ChatCube
