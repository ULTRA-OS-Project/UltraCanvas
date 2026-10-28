// Apps/ChatCube/ui/ChatCubeWindow.cpp
// The ChatCube window: sign-in page, conversations, messages
// Version: 0.2.1 - Telegram signs in by a QR code too, scanned from the phone's Telegram app
// Version: 0.2.0 - the look from ChatCubeTheme: a card on the sign-in page, avatars, icon buttons,
//                  a rounded composer, soft selection, a light and a dark theme (--dark)
// Version: 0.1.3 - the Telegram form says where the code went and asks for it another way
// Version: 0.1.2 - the status line shows every network's state, one entry each, beside the last action
// Version: 0.1.1 - the Telegram application's keys from the relay; TDLib's connection on the status line
// Version: 0.1.0
// Last Modified: 2026-10-28
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeWindow.h"
#include "ChatCubeTheme.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListSelection.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasVideoTile.h"
#include "UltraCanvasWindow.h"
#include "UltraIM/UltraIMCore.h"
#include "Plugins/QRCode/UltraCanvasQRCode.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

constexpr float kTextFont = 10.0f;
constexpr int   kControlHeight = 32;
constexpr int   kSidebarWidth = 320;
constexpr int   kInputHeight = 48;
constexpr float kFormWidth = 340.0f;
constexpr float kControlRadius = 8.0f;
constexpr int   kIconButton = 34;       // a round icon button's side
constexpr int64_t kTypingShownMs = 5000;
constexpr int64_t kTypingResendMs = 3000;

// The window's colours, from the theme set before it was built (main.cpp:
// ChatCube::ThemeFor, light or --dark). Read once: the look does not change
// while the window is up.
const Look& L() {
    static const Look look = CurrentLook();
    return look;
}

Color Darker(const Color& c, int by) {
    auto d = [by](uint8_t v) { return static_cast<uint8_t>(std::max(0, v - by)); };
    return Color(d(c.r), d(c.g), d(c.b), c.a);
}

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
                                            float fontSize = kTextFont, const Color& color = L().ink) {
    auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
    l->SetText(text);
    l->SetFontSize(fontSize);
    l->SetTextColor(color);
    l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    l->size.width = CSSLayout::Dimension::Auto();
    l->size.height = CSSLayout::Dimension::Auto();
    return l;
}

// A button in the window's look: rounded, a hairline border at rest, the
// accent fill when primary, no fill at rest when ghost (the icon buttons).
void StyleButton(const std::shared_ptr<UltraCanvasButton>& b, bool primary, bool ghost = false) {
    ButtonStyle st = b->GetStyle();
    st.cornerRadius = kControlRadius;
    st.fontSize = kTextFont;
    st.borderWidth = ghost || primary ? 0.0f : 1.0f;
    st.borderColor = L().line;
    if (primary) {
        st.normalColor = L().accent;
        st.hoverColor = L().accentHover;
        st.pressedColor = Darker(L().accentHover, 16);
        st.normalTextColor = st.hoverTextColor = st.pressedTextColor = L().onAccent;
        st.disabledColor = L().pressed;
        st.disabledTextColor = L().inkFaint;
    } else {
        st.normalColor = ghost ? Colors::Transparent : L().panel;
        st.hoverColor = L().hover;
        st.pressedColor = L().pressed;
        st.normalTextColor = st.hoverTextColor = st.pressedTextColor = L().ink;
        st.disabledColor = ghost ? Colors::Transparent : L().well;
        st.disabledTextColor = L().inkFaint;
    }
    b->SetStyle(st);
}

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text, int width,
                                              bool primary = false) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, kControlHeight, text);
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    StyleButton(b, primary);
    return b;
}

// A button with an icon from media/icons/chatcube, drawn in the text colour;
// `text` beside it when given, else the icon alone on a round button.
std::shared_ptr<UltraCanvasButton> MakeIconButton(const std::string& id, const std::string& icon,
                                                  const std::string& text = std::string(), int width = kIconButton,
                                                  bool primary = false, bool ghost = true) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, kIconButton, text);
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    StyleButton(b, primary, ghost && !primary);
    if (text.empty()) b->GetStyle().cornerRadius = kIconButton / 2.0f;
    b->SetIcon(IconPath(icon));
    b->SetIconSize(18, 18);
    b->SetUseIconAsMask(true);
    b->SetIconSpacing(6);
    return b;
}

std::shared_ptr<UltraCanvasTextInput> MakeInput(const std::string& id, const std::string& placeholder, int width,
                                                bool pill = false) {
    auto t = std::make_shared<UltraCanvasTextInput>(id, 0, 0, width, kControlHeight);
    t->SetPlaceholder(placeholder);
    t->SetFontSize(kTextFont);
    t->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    TextInputStyle st = t->GetStyle();
    st.backgroundColor = L().panel;
    st.borderColor = L().line;
    st.focusBorderColor = L().accent;
    st.textColor = L().ink;
    st.placeholderColor = L().inkFaint;
    st.caretColor = L().ink;
    st.selectionColor = Color(L().accent.r, L().accent.g, L().accent.b, 90);
    st.borderRadius = pill ? kControlHeight / 2 : static_cast<int>(kControlRadius);
    st.paddingLeft = st.paddingRight = pill ? 14 : 10;
    t->SetStyle(st);
    return t;
}

// The ChatCube logo, `side` pixels square.
std::shared_ptr<UltraCanvasImageElement> MakeLogo(const std::string& id, int side) {
    auto logo = std::make_shared<UltraCanvasImageElement>(id, 0, 0, side, side);
    logo->LoadFromFile(NormalizePath(GetResourcesDir() + "media/appicon/ChatCube.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    logo->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    logo->size.width = CSSLayout::Dimension::Px(static_cast<float>(side));
    logo->size.height = CSSLayout::Dimension::Px(static_cast<float>(side));
    return logo;
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
    window_->SetBackgroundColor(L().ground);
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
        .SetFlexGap(10);
    signInPage_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    signInPage_->AddChild(MakeLogo("cc-signin-logo", 72));
    auto title = MakeLabel("cc-signin-title", "ChatCube", 26.0f);
    title->SetFontWeight(FontWeight::Bold);
    signInPage_->AddChild(title);
    signInPage_->AddChild(MakeLabel("cc-signin-tagline",
                                    "Chat directly with the people you know - no account, no password",
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
    relay_ = MakeInput("cc-signin-relay", "Relay server (optional), e.g. https://relay.example.org",
                       static_cast<int>(kFormWidth));
    relay_->onEnterPressed = [this](const std::string&) { SignIn(); return true; };
    card->AddChild(relay_);
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

    signIn_ = MakeButton("cc-signin-go", "Create my ChatCube ID", static_cast<int>(kFormWidth), true);
    signIn_->SetOnClick([this]() { SignIn(); });
    card->AddChild(signIn_);

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
        "Without one, ChatCube finds the people on your network itself.",
        9.0f, L().inkFaint);
    note->SetWrap(TextWrap::WrapWord);
    note->SetAlignment(TextAlignment::Center);
    note->size.width = CSSLayout::Dimension::Px(kFormWidth + 48);
    signInPage_->AddChild(note);
    return signInPage_;
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
    pane->AddChild(messageList_);

    // ----- the composer: a rounded field with the send button inside -----
    auto inputRow = std::make_shared<UltraCanvasContainer>("cc-input-row");
    inputRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::End);
    inputRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    inputRow->SetPadding(10, 14, 10, 14);
    inputRow->SetBackgroundColor(L().panel);
    inputRow->SetBorderTop(1, L().line);
    auto composer = std::make_shared<UltraCanvasContainer>("cc-composer");
    composer->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::End);
    composer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    composer->SetBackgroundColor(L().well);
    composer->SetBorders(1.0f, L().line, 22.0f);
    composer->SetPadding(5, 6, 5, 14);
    input_ = std::make_shared<UltraCanvasTextArea>("cc-input", 0, 0, 300, kInputHeight);
    input_->SetEditingMode(TextAreaEditingMode::PlainText);
    input_->SetWordWrap(true);
    input_->SetShowLineNumbers(false);
    input_->SetFontSize(11.0f);
    input_->SetPlaceholder("Message\xE2\x80\xA6  (Enter sends, Shift+Enter for a new line)");
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
        UltraIM_SetTyping(activeProvider_, activeConversation_, true);
    });
    composer->AddChild(input_);
    send_ = MakeIconButton("cc-send", "send", "", kIconButton, true);
    send_->SetTooltip("Send (Enter)");
    send_->SetOnClick([this]() { SendCurrentMessage(); });
    composer->AddChild(send_);
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
    if (signInPage_) signInPage_->SetVisible(true);
    if (window_) window_->RequestRedraw();
}

void ChatCubeWindow::ShowChatView() {
    if (signInPage_) signInPage_->SetVisible(false);
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
            myId_->SetText(GroupUserId(info.localUserId));
        ShowChatView();
        SetProviderStatus(providerId_, "connected");
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
        return;
    }
    title_->SetText(c->title);
    headerAvatar_->SetText(c->isGroup ? "#" : Initials(c->title));
    headerAvatar_->SetBackgroundColor(c->isGroup ? L().well : AvatarColor(c->conversationId, L()));
    headerAvatar_->SetTextColor(c->isGroup ? L().inkMuted : Color(255, 255, 255, 255));
    headerAvatar_->SetVisible(true);
    if (!typingContact_.empty() && NowMs() < typingUntilMs_) {
        subtitle_->SetText("typing\xE2\x80\xA6");
    } else if (c->isGroup) {
        subtitle_->SetText(std::to_string(c->memberIds.size()) + " members" +
                           (c->isEncrypted ? " \xC2\xB7 end-to-end encrypted" : ""));
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

void ChatCubeWindow::SendCurrentMessage() {
    if (activeConversation_.empty()) return;
    const std::string text = Trim(input_->GetText());
    if (text.empty()) return;

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
        case UltraIMEventType::MessageUpdate:
            if (e.conversationId == activeConversation_) messages_->Upsert(e.message);
            break;
        case UltraIMEventType::MessageDelete:
            if (e.conversationId == activeConversation_) messages_->Remove(e.message.messageId);
            break;
        case UltraIMEventType::ConnectionStateChange:
            if (telegram) {
                if (e.connectionState == UltraIMConnectionState::Connected) {
                    SetProviderStatus(telegramProviderId_, "signed in");
                    telegramToggle_->SetText("Telegram: signed in");
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
                    telegramToggle_->SetText("Telegram account");
                }
                break;
            }
            if (matrix) {
                // Matrix speaks for itself on the status line, Direct below.
                if (e.connectionState == UltraIMConnectionState::Connected) {
                    UltraIMProviderInfo info;
                    UltraIM_GetProviderInfo(matrixProviderId_, info);
                    SetProviderStatus(matrixProviderId_, "signed in as " + info.localUserId);
                    matrixToggle_->SetText("Matrix: " + info.localUserId);
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
                telegramToggle_->SetText("Telegram account");
                UpdateTelegramForm();
                break;
            }
            if (matrix) {
                SetProviderStatus(matrixProviderId_, "the session ended - sign in again");
                matrixForm_->SetVisible(true);
                matrixToggle_->SetText("Matrix account");
                break;
            }
            ShowSignInPage();
            SetSignInStatus("Please sign in again.");
            break;
        default:
            break;
    }
}

} // namespace ChatCube
