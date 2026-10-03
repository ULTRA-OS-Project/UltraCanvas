// Apps/UltraPassword/ui/StartPage.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartPage.h"

#include "Theme.h"

#include "UltraCanvasConfig.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasWindow.h"

using namespace UltraCanvas;

namespace UltraPassword {

namespace {
constexpr float kLogoSize      = 96.0f;
constexpr float kTitleSize     = 22.0f;
constexpr float kGap           = 16.0f;
constexpr float kButtonWidth   = 220.0f;
constexpr float kButtonHeight  = 34.0f;
constexpr float kButtonFont    = 11.0f;
constexpr float kButtonRadius  = 6.0f;
constexpr int   kButtonIcon    = 16;
constexpr float kPasswordWidth = 260.0f;
constexpr float kPageTextWidth = 420.0f;
} // namespace

std::shared_ptr<UltraCanvasContainer> StartPage::Build() {
    page_ = CreateContainer("upwStartPage", 0, 0, 0, 0);
    page_->SetBackgroundColor(Theme::kPageBackground);
    page_->layout.SetFlexColumn()
                 .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Center)
                 .SetFlexGap(kGap);

    // The app icon itself, as on UltraMail's start page.
    auto logo = CreateImageElement("upwStartLogo", kLogoSize, kLogoSize);
    logo->LoadFromFile(NormalizePath(GetResourcesDir() + "media/appicon/UltraPassword.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    page_->AddChild(logo);

    auto title = CreateLabel("upwStartTitle", "UltraPassword");
    title->SetFontSize(kTitleSize);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Theme::kTextPrimary);
    title->SetAlignment(TextAlignment::Center);
    page_->AddChild(title);

    subtitle_ = CreateLabel("upwStartSubtitle", 0, 0, kPageTextWidth, 34, "");
    subtitle_->SetFontSize(Theme::kSizeBody);
    subtitle_->SetTextColor(Theme::kTextSecondary);
    subtitle_->SetAlignment(TextAlignment::Center);
    subtitle_->SetWrap(TextWrap::WrapWord);
    page_->AddChild(subtitle_);

    // ----- Create mode: the single call to action -----
    createButton_ = CreateButton("upwStartCreate", 0, 0, kButtonWidth, kButtonHeight,
                                 "Create password vault");
    Theme::FitToLabel(createButton_, kButtonWidth);
    Theme::StylePrimary(createButton_);
    createButton_->SetFontSize(kButtonFont);
    createButton_->SetCornerRadius(kButtonRadius);
    createButton_->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/key.svg"));
    createButton_->SetIconPosition(ButtonIconPosition::Left);
    createButton_->SetIconSize(kButtonIcon, kButtonIcon);
    createButton_->SetUseIconAsMask(true);
    createButton_->onClick = [this]() { if (onCreate) onCreate(); };
    page_->AddChild(createButton_);

    // ----- Unlock mode: password + Unlock in one row -----
    unlockRow_ = CreateContainer("upwStartUnlockRow", 0, 0, 0, kButtonHeight);
    unlockRow_->layout.SetFlexRow()
                      .SetFlexGap(Theme::kInnerGap)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    password_ = CreatePasswordInput("upwStartPassword", 0, 0, kPasswordWidth, kButtonHeight);
    password_->SetPlaceholder("Master password");
    Theme::StyleInput(password_);
    password_->onEnterPressed = [this](const std::string&) { SubmitPassword(); return true; };
    unlockRow_->AddChild(password_);
    unlockButton_ = CreateButton("upwStartUnlock", 0, 0, 90, kButtonHeight, "Unlock");
    Theme::FitToLabel(unlockButton_, 90);
    Theme::StylePrimary(unlockButton_);
    unlockButton_->SetFontSize(kButtonFont);
    unlockButton_->SetCornerRadius(kButtonRadius);
    unlockButton_->onClick = [this]() { SubmitPassword(); };
    unlockRow_->AddChild(unlockButton_);
    page_->AddChild(unlockRow_);

    error_ = CreateLabel("upwStartError", 0, 0, kPageTextWidth, 30, "");
    error_->SetFontSize(Theme::kSizeBody);
    error_->SetTextColor(Theme::kDanger);
    error_->SetAlignment(TextAlignment::Center);
    error_->SetWrap(TextWrap::WrapWord);
    page_->AddChild(error_);

    // ----- both modes: a vault file from elsewhere -----
    openOther_ = CreateButton("upwStartOpenOther", 0, 0, 160, Theme::kControlHeight,
                              "Open existing vault…");
    Theme::FitToLabel(openOther_, 160);
    Theme::StyleSecondary(openOther_);
    openOther_->onClick = [this]() { if (onOpenOther) onOpenOther(); };
    page_->AddChild(openOther_);

    SetMode(Mode::Create);
    return page_;
}

void StartPage::Resize(float width, float height) {
    if (page_) page_->SetElementSize(Size2Df(width, height));
}

void StartPage::SetMode(Mode mode, const std::string& detail) {
    mode_ = mode;
    if (!page_) return;
    const bool create = mode == Mode::Create;
    createButton_->SetVisible(create);
    unlockRow_->SetVisible(!create);
    if (create) {
        subtitle_->SetText(detail.empty()
            ? "Keep every password in one vault, encrypted with a master password "
              "only you know."
            : detail);
    } else {
        subtitle_->SetText(detail.empty() ? "Enter your master password to unlock the vault."
                                          : detail);
    }
    password_->SetText("");
    SetError("");
    SetBusy("");
}

void StartPage::SetError(const std::string& text) {
    if (!error_) return;
    error_->SetText(text);
    error_->SetVisible(!text.empty());
}

void StartPage::SetBusy(const std::string& text) {
    if (!unlockButton_) return;
    unlockButton_->SetText(text.empty() ? "Unlock" : text);
    unlockButton_->SetDisabled(!text.empty());
    password_->SetDisabled(!text.empty());
}

void StartPage::FocusPassword() {
    if (!password_ || mode_ != Mode::Unlock) return;
    if (auto* window = password_->GetWindow()) window->SetFocusedElement(password_.get());
    else password_->SetFocus(true);
}

void StartPage::SubmitPassword() {
    if (!password_ || unlockButton_->IsDisabled()) return;
    std::string typed = password_->GetText();
    password_->SetText("");
    if (typed.empty()) {
        SetError("Enter the master password.");
        return;
    }
    if (onUnlock) onUnlock(typed);
    WipeString(typed);
}

} // namespace UltraPassword
