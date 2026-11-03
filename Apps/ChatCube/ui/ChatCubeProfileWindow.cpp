// Apps/ChatCube/ui/ChatCubeProfileWindow.cpp
// The Edit profile window - see the header.
// Version: 0.1.0
// Last Modified: 2026-11-01
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
    email_ = MakeInput("cc-profile-email", "you@example.org", kFieldWidth);
    profile->AddChild(Field("cc-profile-email-field", "E-mail address", email_));
    phone_ = MakeInput("cc-profile-phone", "+49 151 2345 6789", kFieldWidth);
    phone_->onEnterPressed = [this](const std::string&) { SaveProfile(); return true; };
    profile->AddChild(Field("cc-profile-phone-field", "Phone number, with + and the country code", phone_));
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
        if (p.email.empty() || p.phoneNumber.empty())
            SetProfileStatus("Add your e-mail address and phone number: they are how you get your account back.");
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
        SetProfileStatus("Saved.");
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
