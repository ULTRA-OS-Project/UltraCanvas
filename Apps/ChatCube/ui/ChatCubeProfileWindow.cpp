// Apps/ChatCube/ui/ChatCubeProfileWindow.cpp
// The Edit profile window - see the header.
// Version: 0.1.2 - the e-mail address confirmed by the link e-mailed to it (Check, Send again); recovery
//                  by an e-mailed link, on once the address is confirmed
// Version: 0.1.1 - confirming the e-mail and phone with the relay's codes; account recovery on or off
// Version: 0.1.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeProfileWindow.h"
#include "ChatCubeControls.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"
#include "UltraIM/UltraIMCore.h"
#include "Plugins/QRCode/UltraCanvasQRCode.h"

#include <algorithm>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

constexpr int kWindowWidth = 520;
constexpr int kWindowHeight = 760;
constexpr int kFieldWidth = 440;
constexpr int kHalfField = (kFieldWidth - 10) / 2;
constexpr int kQrSide = 180;

// The protections in the list's order, which is UltraIMSignInProtection's.
struct ProtectionText {
    const char* name;
    const char* about;
};
const ProtectionText kProtections[] = {
    {"No password - this computer signs in by itself",
     "Whoever can use this computer account can open ChatCube. The key to your ID is still encrypted on "
     "disk, with a key kept beside it."},
    {"A password",
     "The key to your ChatCube ID is encrypted with your password, so without it nobody can read the "
     "key - not even from a copy of the files. ChatCube asks for it at every start. Keep it safe: "
     "nobody can recover it for you."},
    {"A code from UltraAuthenticator",
     "ChatCube asks for the 6-digit code that UltraAuthenticator (or any authenticator app) shows, at "
     "every start - each code works once. It keeps someone at this computer out of ChatCube; the key "
     "itself is encrypted as without a password."},
    {"A password and a code from UltraAuthenticator (strongest)",
     "Both: the key to your ID is encrypted with your password, and the authenticator's code is asked "
     "for after it."},
};

// A caption over its field.
std::shared_ptr<UltraCanvasContainer> Field(const std::string& id, const std::string& caption,
                                            const std::shared_ptr<UltraCanvasUIElement>& field) {
    auto column = std::make_shared<UltraCanvasContainer>(id);
    column->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    column->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    column->AddChild(MakeLabel(id + "-caption", caption, 9.0f, L().inkMuted));
    column->AddChild(field);
    return column;
}

std::shared_ptr<UltraCanvasLabel> MakeNote(const std::string& id, const std::string& text, int width) {
    auto note = MakeLabel(id, text, 9.0f, L().inkMuted);
    note->SetWrap(TextWrap::WrapWord);
    note->size.width = CSSLayout::Dimension::Px(static_cast<float>(width));
    return note;
}

std::shared_ptr<UltraCanvasTextInput> MakePasswordInput(const std::string& id, const std::string& placeholder,
                                                        int width) {
    auto input = MakeInput(id, placeholder, width);
    input->SetInputType(TextInputType::Password);
    return input;
}

// The Base32 key out of an otpauth:// link, in groups of four, for an
// authenticator that has no camera: typed by hand.
std::string KeyOfLink(const std::string& link) {
    const size_t at = link.find("secret=");
    if (at == std::string::npos) return std::string();
    const size_t end = link.find('&', at);
    const std::string key = link.substr(at + 7, end == std::string::npos ? std::string::npos : end - at - 7);
    std::string grouped;
    for (size_t i = 0; i < key.size(); ++i) {
        if (i && i % 4 == 0) grouped += ' ';
        grouped += key[i];
    }
    return grouped;
}

} // namespace

ChatCubeProfileWindow::ChatCubeProfileWindow(std::string providerId) : providerId_(std::move(providerId)) {}

ChatCubeProfileWindow::~ChatCubeProfileWindow() {
    Close();
}

void ChatCubeProfileWindow::Close() {
    if (qr_) qr_->SetContent(std::string());     // the link holds the authenticator's secret
    if (window_) {
        if (!closed_) window_->Close();
        window_.reset();
    }
    closed_ = true;
    authenticatorShown_ = false;
}

void ChatCubeProfileWindow::Show(UltraCanvasWindowBase* host) {
    if (window_ && !closed_) {
        window_->RaiseAndFocus();
        return;
    }
    Close();
    Build(host);
    Load();
}

void ChatCubeProfileWindow::Show(UltraCanvasWindowBase* host, UltraIMSignInProtection preset,
                                 const std::string& currentPassword) {
    Show(host);
    if (!window_) return;
    protection_->SetSelectedIndex(static_cast<int>(preset), false);
    if (!currentPassword.empty()) currentPassword_->SetText(currentPassword);
    UpdateProtectionForm();
}

void ChatCubeProfileWindow::Build(UltraCanvasWindowBase* host) {
    WindowConfig wc;
    wc.title = "Edit profile - ChatCube";
    wc.width = kWindowWidth;
    wc.height = kWindowHeight;
    wc.resizable = true;
    wc.type = WindowType::Standard;
    wc.backgroundColor = L().ground;
    if (host) {
        wc.parentWindow = host;
        int hx = 0, hy = 0;
        host->GetWindowPosition(hx, hy);
        wc.x = hx + std::max(0, (static_cast<int>(host->GetWidth()) - kWindowWidth) / 2);
        wc.y = hy + 20;
    }
    window_ = CreateWindow(wc);
    if (!window_ || !window_->IsCreated()) {
        window_.reset();
        return;
    }
    closed_ = false;
    window_->SetBackgroundColor(L().ground);
    window_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window_->onWindowClosed = [this]() {
        closed_ = true;
        if (qr_) qr_->SetContent(std::string());
    };

    // Everything in one column that scrolls when the window is short.
    auto page = std::make_shared<UltraCanvasContainer>("cc-profile-page");
    ContainerStyle pageStyle = page->GetContainerStyle();
    pageStyle.autoShowScrollbars = true;
    pageStyle.autoShowHorizontalScrollbar = false;
    page->SetContainerStyle(pageStyle);
    page->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    page->SetPadding(18, 20, 18, 20);
    window_->AddChild(page);

    auto card = [](const std::string& id) {
        auto c = std::make_shared<UltraCanvasContainer>(id);
        c->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Start);
        c->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        c->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth + 40));
        c->SetBackgroundColor(L().panel);
        c->SetBorders(1.0f, L().line, 14.0f);
        c->SetPadding(16, 20, 16, 20);
        return c;
    };

    // ---- the profile ---------------------------------------------------------------
    auto profile = card("cc-profile-card");
    auto heading = MakeLabel("cc-profile-heading", "Profile", 13.0f);
    heading->SetFontWeight(FontWeight::Bold);
    profile->AddChild(heading);
    name_ = MakeInput("cc-profile-name", "Your name, as your contacts see it", kFieldWidth);
    profile->AddChild(Field("cc-profile-name-field", "Name", name_));
    // Under each: whether the relay has it confirmed - or, for the address,
    // Check (after opening the link e-mailed to it), for the phone the code to type.
    auto verifyRow = [this](int index, const std::string& id) {
        VerifyRow& v = verify_[index];
        const auto channel = static_cast<UltraIMContactChannel>(index);
        v.row = MakeRow(id, 6);
        v.state = MakeLabel(id + "-state", "", 9.0f, L().inkMuted);
        v.state->SetWrap(TextWrap::WrapWord);
        v.state->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        v.row->AddChild(v.state);
        v.code = MakeInput(id + "-code", "123 456", 90);
        v.code->onEnterPressed = [this, channel](const std::string&) { Confirm(channel); return true; };
        v.row->AddChild(v.code);
        v.confirm = MakeButton(id + "-confirm", "Confirm", 84, true);
        v.confirm->SetOnClick([this, channel]() { Confirm(channel); });
        v.row->AddChild(v.confirm);
        v.again = MakeButton(id + "-again", "Send again", 96);
        v.again->SetOnClick([this, channel]() { SendAgain(channel); });
        v.row->AddChild(v.again);
        v.row->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth));
        v.row->SetVisible(false);
        return v.row;
    };
    email_ = MakeInput("cc-profile-email", "you@example.org", kFieldWidth);
    profile->AddChild(Field("cc-profile-email-field", "E-mail address", email_));
    profile->AddChild(verifyRow(static_cast<int>(UltraIMContactChannel::Email), "cc-profile-email-verify"));
    phone_ = MakeInput("cc-profile-phone", "+49 151 2345 6789", kFieldWidth);
    phone_->onEnterPressed = [this](const std::string&) { SaveProfile(); return true; };
    profile->AddChild(Field("cc-profile-phone-field", "Phone number, with + and the country code", phone_));
    profile->AddChild(verifyRow(static_cast<int>(UltraIMContactChannel::Phone), "cc-profile-phone-verify"));
    profile->AddChild(MakeNote("cc-profile-note",
                               "Your contacts see your name. The e-mail address and the phone number are kept "
                               "for you alone - in the key vault on this computer and on your relay - to "
                               "recover your account with; nobody else is shown them.",
                               kFieldWidth));
    auto profileRow = MakeRow("cc-profile-actions", 10);
    saveProfile_ = MakeButton("cc-profile-save", "Save profile", 130, true);
    saveProfile_->SetOnClick([this]() { SaveProfile(); });
    profileRow->AddChild(saveProfile_);
    profileStatus_ = MakeLabel("cc-profile-status", "", kTextFont, L().inkMuted);
    profileStatus_->SetWrap(TextWrap::WrapWord);
    profileStatus_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    profileRow->AddChild(profileStatus_);
    profile->AddChild(profileRow);
    page->AddChild(profile);

    // ---- getting this ID back ------------------------------------------------------------
    auto recovery = card("cc-recovery-card");
    auto recoveryHeading = MakeLabel("cc-recovery-heading", "Getting this ID back", 13.0f);
    recoveryHeading->SetFontWeight(FontWeight::Bold);
    recovery->AddChild(recoveryHeading);
    recoveryText_ = MakeNote("cc-recovery-text", "", kFieldWidth);
    recovery->AddChild(recoveryText_);
    auto recoveryRow = MakeRow("cc-recovery-actions", 10);
    recoveryButton_ = MakeButton("cc-recovery-toggle", "Turn recovery on", 170);
    recoveryButton_->SetOnClick([this]() { ToggleRecovery(); });
    recoveryRow->AddChild(recoveryButton_);
    recoveryStatus_ = MakeLabel("cc-recovery-status", "", kTextFont, L().inkMuted);
    recoveryStatus_->SetWrap(TextWrap::WrapWord);
    recoveryStatus_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    recoveryRow->AddChild(recoveryStatus_);
    recovery->AddChild(recoveryRow);
    page->AddChild(recovery);

    // ---- signing in --------------------------------------------------------------------
    auto security = card("cc-protection-card");
    auto securityHeading = MakeLabel("cc-protection-heading", "Signing in on this computer", 13.0f);
    securityHeading->SetFontWeight(FontWeight::Bold);
    security->AddChild(securityHeading);
    protection_ = std::make_shared<UltraCanvasDropdown>("cc-protection", 0, 0, kFieldWidth, kControlHeight);
    protection_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    DropdownStyle dropStyle = protection_->GetStyle();
    dropStyle.fontSize = kTextFont;
    dropStyle.cornerRadius = kControlRadius;
    dropStyle.itemHeight = 28.0f;
    dropStyle.maxItemWidth = static_cast<float>(kFieldWidth + 60);
    protection_->SetStyle(dropStyle);
    for (const ProtectionText& p : kProtections) protection_->AddItem(p.name);
    protection_->SetAccessibleName("How signing in is protected");
    protection_->onSelectionChanged = [this](int, const DropdownItem&) { UpdateProtectionForm(); };
    security->AddChild(protection_);
    protectionText_ = MakeNote("cc-protection-about", "", kFieldWidth);
    security->AddChild(protectionText_);

    currentPassword_ = MakePasswordInput("cc-protection-current", "Your password now", kFieldWidth);
    currentPasswordRow_ = Field("cc-protection-current-field", "Current password", currentPassword_);
    security->AddChild(currentPasswordRow_);
    currentCode_ = MakeInput("cc-protection-current-code", "123 456", 140);
    currentCodeRow_ = Field("cc-protection-current-code-field", "The code UltraAuthenticator shows now", currentCode_);
    security->AddChild(currentCodeRow_);

    newPasswordRow_ = MakeRow("cc-protection-new", 10);
    newPassword_ = MakePasswordInput("cc-protection-new-password", "8 characters or more", kHalfField);
    newPasswordRow_->AddChild(Field("cc-protection-new-field", "New password", newPassword_));
    repeatPassword_ = MakePasswordInput("cc-protection-repeat", "The same again", kHalfField);
    repeatPassword_->onEnterPressed = [this](const std::string&) { SaveProtection(); return true; };
    newPasswordRow_->AddChild(Field("cc-protection-repeat-field", "Repeat it", repeatPassword_));
    security->AddChild(newPasswordRow_);

    newAuthenticator_ = MakeButton("cc-protection-new-authenticator", "Use a new authenticator", 200);
    newAuthenticator_->SetOnClick([this]() { StartAuthenticator(); });
    security->AddChild(newAuthenticator_);

    // The authenticator to add: its QR code, and the key for one that types it.
    authenticatorBox_ = MakeRow("cc-authenticator", 14);
    authenticatorBox_->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    qr_ = std::make_shared<UltraCanvasQRCode>("cc-authenticator-qr", 0, 0, kQrSide, kQrSide);
    qr_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    qr_->size.width = CSSLayout::Dimension::Px(static_cast<float>(kQrSide));
    qr_->size.height = CSSLayout::Dimension::Px(static_cast<float>(kQrSide));
    qr_->SetForegroundColor(Colors::Black);
    qr_->SetBackgroundColor(Colors::White);
    qr_->SetQuietZoneModules(2);
    authenticatorBox_->AddChild(qr_);
    auto steps = std::make_shared<UltraCanvasContainer>("cc-authenticator-steps");
    steps->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    steps->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    const int stepsWidth = kFieldWidth - kQrSide - 14;
    steps->AddChild(MakeNote("cc-authenticator-how",
                             "1. In UltraAuthenticator choose Add account and scan this code - from the screen, "
                             "or with your phone's authenticator app. Without a camera, type the key below.",
                             stepsWidth));
    authenticatorKey_ = MakeLabel("cc-authenticator-key", "", 9.5f, L().ink);
    authenticatorKey_->SetWrap(TextWrap::WrapWord);
    authenticatorKey_->size.width = CSSLayout::Dimension::Px(static_cast<float>(stepsWidth));
    steps->AddChild(authenticatorKey_);
    steps->AddChild(MakeNote("cc-authenticator-then", "2. Type the 6-digit code it shows for ChatCube:", stepsWidth));
    newCode_ = MakeInput("cc-authenticator-code", "123 456", 140);
    newCode_->onEnterPressed = [this](const std::string&) { SaveProtection(); return true; };
    steps->AddChild(newCode_);
    authenticatorBox_->AddChild(steps);
    security->AddChild(authenticatorBox_);

    auto securityRow = MakeRow("cc-protection-actions", 10);
    saveProtection_ = MakeButton("cc-protection-save", "Save sign-in protection", 200, true);
    saveProtection_->SetOnClick([this]() { SaveProtection(); });
    securityRow->AddChild(saveProtection_);
    protectionStatus_ = MakeLabel("cc-protection-status", "", kTextFont, L().inkMuted);
    protectionStatus_->SetWrap(TextWrap::WrapWord);
    protectionStatus_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    securityRow->AddChild(protectionStatus_);
    security->AddChild(securityRow);
    page->AddChild(security);

    // Escape closes, as in the framework's other secondary windows.
    window_->eventCallback = [this](const UCEvent& event) {
        if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
            Close();
            return true;
        }
        return false;
    };
    window_->Show();
}

void ChatCubeProfileWindow::Load() {
    if (!window_) return;
    current_ = UltraIM_GetSignInProtection(providerId_);
    protection_->SetSelectedIndex(static_cast<int>(current_), false);
    UpdateProtectionForm();
    UltraIM_FetchAccountProfile(providerId_, [this](const UltraIMResult& r, const UltraIMAccountProfile& p) {
        if (!window_) return;
        if (!r) {
            SetProfileStatus(r.message, true);
            return;
        }
        name_->SetText(p.displayName);
        email_->SetText(p.email);
        phone_->SetText(p.phoneNumber);
        profile_ = p;
        UpdateVerification();
        UpdateRecovery();
        if (p.email.empty() || p.phoneNumber.empty())
            SetProfileStatus("Add your e-mail address and phone number: they are how you get your account back.");
    });
}

void ChatCubeProfileWindow::Refresh() {
    UltraIM_FetchAccountProfile(providerId_, [this](const UltraIMResult& r, const UltraIMAccountProfile& p) {
        if (!window_ || !r) return;
        profile_ = p;
        UpdateVerification();
        UpdateRecovery();
    });
}

void ChatCubeProfileWindow::UpdateVerification() {
    for (int index = 0; index < 2; ++index) {
        VerifyRow& v = verify_[index];
        const bool email = index == static_cast<int>(UltraIMContactChannel::Email);
        const std::string& value = email ? profile_.email : profile_.phoneNumber;
        const bool confirmed = email ? profile_.emailVerified : profile_.phoneVerified;
        // Nothing to say without a value, or without a relay that confirms it.
        const bool confirms = email ? profile_.recoveryAvailable : profile_.phoneCodesAvailable;
        v.row->SetVisible(!value.empty() && confirms);
        if (confirmed) {
            v.state->SetText("Confirmed");
            v.state->SetTextColor(L().accent);
        } else {
            v.state->SetText(email ? "Not confirmed yet - open the link we e-mailed you, then:"
                                   : "Not confirmed yet - type the code we sent by SMS:");
            v.state->SetTextColor(L().inkMuted);
        }
        v.code->SetVisible(!confirmed && !email);
        v.confirm->SetText(email ? "Check" : "Confirm");
        v.confirm->SetVisible(!confirmed);
        v.again->SetVisible(!confirmed);
    }
    if (window_) window_->RequestRedraw();
}

void ChatCubeProfileWindow::Confirm(UltraIMContactChannel channel) {
    if (channel == UltraIMContactChannel::Email) {
        // The link was opened in a browser: whether the relay saw it.
        UltraIM_FetchAccountProfile(providerId_, [this](const UltraIMResult& r, const UltraIMAccountProfile& p) {
            if (!window_) return;
            if (!r) return SetProfileStatus(r.message, true);
            profile_ = p;
            UpdateVerification();
            UpdateRecovery();
            SetProfileStatus(p.emailVerified ? "Your e-mail address is confirmed."
                                             : "Not yet - open the link in the e-mail we sent, or Send again.",
                             !p.emailVerified);
        });
        return;
    }
    VerifyRow& v = verify_[static_cast<int>(channel)];
    const std::string code = Trim(v.code->GetText());
    if (code.empty()) return SetProfileStatus("Type the code first.", true);
    v.confirm->SetDisabled(true);
    UltraIM_VerifyContact(providerId_, channel, code, [this, channel](const UltraIMResult& r) {
        if (!window_) return;
        VerifyRow& row = verify_[static_cast<int>(channel)];
        row.confirm->SetDisabled(false);
        if (!r) return SetProfileStatus(r.message, true);
        row.code->SetText(std::string());
        SetProfileStatus("Your phone number is confirmed.");
        Refresh();
    });
}

void ChatCubeProfileWindow::SendAgain(UltraIMContactChannel channel) {
    UltraIM_ResendContactCode(providerId_, channel, [this, channel](const UltraIMResult& r) {
        if (!window_) return;
        if (!r) return SetProfileStatus(r.message, true);
        SetProfileStatus(channel == UltraIMContactChannel::Email
                             ? "A new link is on its way to your e-mail - the one before no longer works."
                             : "A new code is on its way by SMS.");
    });
}

void ChatCubeProfileWindow::UpdateRecovery() {
    const bool confirmed = profile_.emailVerified;
    if (!profile_.recoveryAvailable) {
        recoveryText_->SetText("Your relay does not offer recovery - it needs to be able to send e-mail. "
                               "Without it, a ChatCube ID lost with its computer cannot come back: keep "
                               "ChatCube's data folder in your backups.");
        recoveryButton_->SetVisible(false);
    } else if (profile_.recoveryEnabled) {
        recoveryText_->SetText("Recovery is on. On another computer, \"Recover my ChatCube ID\" on the sign-in "
                               "page has a link e-mailed to your address; opening it and confirming brings this "
                               "ID back there. You get an e-mail whenever that happens. Your relay keeps a copy "
                               "of your key for it.");
        recoveryButton_->SetText("Turn recovery off");
        recoveryButton_->SetVisible(true);
        recoveryButton_->SetDisabled(false);
    } else {
        recoveryText_->SetText("If this computer is lost, your ID can come back on another one: your relay keeps "
                               "a copy of your key and gives it back to the computer that asked, once you confirm "
                               "through the link it e-mails to your confirmed address - so keep that mailbox safe. "
                               "Whoever runs the relay can read that copy, so turn this on only with a relay you "
                               "trust.");
        recoveryButton_->SetText("Turn recovery on");
        recoveryButton_->SetVisible(true);
        recoveryButton_->SetDisabled(!confirmed);
        if (!confirmed) recoveryStatus_->SetText("Confirm your e-mail address first.");
        else recoveryStatus_->SetText(std::string());
    }
    if (window_) window_->RequestRedraw();
}

void ChatCubeProfileWindow::ToggleRecovery() {
    const bool on = !profile_.recoveryEnabled;
    recoveryButton_->SetDisabled(true);
    recoveryStatus_->SetText(on ? "Turning recovery on\xE2\x80\xA6" : "Turning recovery off\xE2\x80\xA6");
    UltraIM_SetRecoveryEnabled(providerId_, on, [this, on](const UltraIMResult& r) {
        if (!window_) return;
        recoveryButton_->SetDisabled(false);
        if (!r) {
            recoveryStatus_->SetText(r.message);
            recoveryStatus_->SetTextColor(L().danger);
            return;
        }
        recoveryStatus_->SetTextColor(L().inkMuted);
        recoveryStatus_->SetText(on ? "Recovery is on." : "Recovery is off - the relay deleted its copy of your key.");
        if (onSaved) onSaved(on ? "Account recovery is on." : "Account recovery is off.");
        Refresh();
    });
}

UltraIMSignInProtection ChatCubeProfileWindow::Chosen() const {
    const int index = protection_ ? protection_->GetSelectedIndex() : 0;
    return static_cast<UltraIMSignInProtection>(std::clamp(index, 0, 3));
}

void ChatCubeProfileWindow::SetProfileStatus(const std::string& text, bool error) {
    if (!profileStatus_) return;
    profileStatus_->SetText(text);
    profileStatus_->SetTextColor(error ? L().danger : L().inkMuted);
}

void ChatCubeProfileWindow::SetProtectionStatus(const std::string& text, bool error) {
    if (!protectionStatus_) return;
    protectionStatus_->SetText(text);
    protectionStatus_->SetTextColor(error ? L().danger : L().inkMuted);
}

void ChatCubeProfileWindow::UpdateProtectionForm() {
    if (!window_) return;
    const UltraIMSignInProtection chosen = Chosen();
    protectionText_->SetText(kProtections[static_cast<int>(chosen)].about);
    const bool nowPassword = UltraIM_ProtectionHasPassword(current_);
    const bool nowCode = UltraIM_ProtectionHasCode(current_);
    const bool wantPassword = UltraIM_ProtectionHasPassword(chosen);
    const bool wantCode = UltraIM_ProtectionHasCode(chosen);
    // What protects it now proves any change of it.
    currentPasswordRow_->SetVisible(nowPassword);
    currentCodeRow_->SetVisible(nowCode);
    newPasswordRow_->SetVisible(wantPassword);
    newPassword_->SetPlaceholder(nowPassword ? "Leave empty to keep it" : "8 characters or more");
    // A code wanted and none there: the authenticator to add, at once.
    if (wantCode && !nowCode && !authenticatorShown_) StartAuthenticator();
    authenticatorBox_->SetVisible(wantCode && authenticatorShown_);
    newAuthenticator_->SetVisible(wantCode && nowCode && !authenticatorShown_);
    window_->RequestRedraw();
}

void ChatCubeProfileWindow::StartAuthenticator() {
    std::string link;
    const UltraIMResult r = UltraIM_StartAuthenticatorSetup(providerId_, link);
    if (!r) {
        SetProtectionStatus(r.message, true);
        return;
    }
    qr_->SetContent(link);
    authenticatorKey_->SetText("Key: " + KeyOfLink(link));
    newCode_->SetText(std::string());
    authenticatorShown_ = true;
    authenticatorBox_->SetVisible(UltraIM_ProtectionHasCode(Chosen()));
    newAuthenticator_->SetVisible(false);
    if (window_) window_->RequestRedraw();
}

void ChatCubeProfileWindow::SaveProfile() {
    UltraIMAccountProfile profile;
    profile.displayName = Trim(name_->GetText());
    profile.email = Trim(email_->GetText());
    profile.phoneNumber = Trim(phone_->GetText());
    if (profile.displayName.empty()) return SetProfileStatus("Type the name your contacts see.", true);
    if (profile.email.empty()) return SetProfileStatus("Type your e-mail address - it is how you recover the account.", true);
    if (profile.phoneNumber.empty())
        return SetProfileStatus("Type your phone number - it is how you recover the account.", true);
    saveProfile_->SetDisabled(true);
    SetProfileStatus("Saving\xE2\x80\xA6");
    UltraIM_UpdateAccountProfile(providerId_, profile, [this, profile](const UltraIMResult& r) {
        if (!window_) return;
        saveProfile_->SetDisabled(false);
        if (!r) {
            SetProfileStatus(r.message, true);
            return;
        }
        SetProfileStatus("Saved. A changed e-mail address or phone number gets a code to confirm it.");
        Refresh();
        if (onProfileChanged) onProfileChanged(profile);
        if (onSaved) onSaved("Your profile is saved.");
    });
}

void ChatCubeProfileWindow::SaveProtection() {
    UltraIMProtectionChange change;
    change.protection = Chosen();
    const bool wantPassword = UltraIM_ProtectionHasPassword(change.protection);
    const bool wantCode = UltraIM_ProtectionHasCode(change.protection);
    if (UltraIM_ProtectionHasPassword(current_)) change.currentPassword = currentPassword_->GetText();
    if (UltraIM_ProtectionHasCode(current_)) change.currentCode = Trim(currentCode_->GetText());
    if (wantPassword) {
        change.newPassword = newPassword_->GetText();
        if (change.newPassword != repeatPassword_->GetText())
            return SetProtectionStatus("The two new passwords are not the same.", true);
    }
    if (wantCode && authenticatorShown_) {
        change.newCode = Trim(newCode_->GetText());
        if (change.newCode.empty())
            return SetProtectionStatus("Type the code the authenticator shows, to show it was added.", true);
    }
    if (change.protection == current_ && change.newPassword.empty() && !authenticatorShown_)
        return SetProtectionStatus("Nothing to change.");
    saveProtection_->SetDisabled(true);
    SetProtectionStatus("Saving\xE2\x80\xA6 (the password is stretched: this takes a moment)");
    UltraIM_SetSignInProtection(providerId_, change, [this](const UltraIMResult& r) {
        if (!window_) return;
        saveProtection_->SetDisabled(false);
        if (!r) {
            SetProtectionStatus(r.message, true);
            return;
        }
        current_ = UltraIM_GetSignInProtection(providerId_);
        authenticatorShown_ = false;
        qr_->SetContent(std::string());
        currentPassword_->SetText(std::string());
        currentCode_->SetText(std::string());
        newPassword_->SetText(std::string());
        repeatPassword_->SetText(std::string());
        newCode_->SetText(std::string());
        protection_->SetSelectedIndex(static_cast<int>(current_), false);
        UpdateProtectionForm();
        const std::string text = std::string("Signing in now: ") + kProtections[static_cast<int>(current_)].name + ".";
        SetProtectionStatus("Saved. " + text);
        if (onSaved) onSaved(text);
    });
}

} // namespace ChatCube
