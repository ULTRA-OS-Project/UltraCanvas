// Apps/UltraMail/ui/UltraMailStartPage.cpp
// Version: 0.3.0 - themed title colour and primary button
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailStartPage.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraMailTheme.h"

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr float kLogoSize     = 96.0f;
constexpr float kTitleSize    = 22.0f;   // font size of the app title
constexpr float kGap          = 16.0f;   // vertical gap between logo, title, button
constexpr float kButtonWidth  = 200.0f;
constexpr float kButtonHeight = 34.0f;
constexpr float kButtonFont   = 11.0f;
constexpr int   kButtonIcon   = 16;
constexpr float kButtonRadius = 6.0f;
} // namespace

std::shared_ptr<UltraCanvasContainer> StartPage::Build() {
    // A flex column that centres its three children both ways. The page itself
    // is sized to the window by Resize(), so the centring follows the window.
    page_ = CreateContainer("startPage", 0, 0, 0, 0);
    page_->layout.SetFlexColumn()
                 .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Center)
                 .SetFlexGap(kGap);

    // Logo — the app icon, rendered from its vector source.
    auto logo = CreateImageElement("startLogo", kLogoSize, kLogoSize);
    logo->LoadFromFile(NormalizePath(GetResourcesDir() + "media/appicon/UltraMail.svg"));
    logo->SetFitMode(ImageFitMode::Contain);
    page_->AddChild(logo);

    // App title (fit-content, so the column centres it on its real width).
    auto title = CreateLabel("startTitle", "UltraMail");
    title->SetFontSize(kTitleSize);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Theme::kTextPrimary);
    title->SetAlignment(TextAlignment::Center);
    page_->AddChild(title);

    // The single call to action: a primary button with an envelope icon.
    auto add = CreateButton("startAddAccount", 0, 0, kButtonWidth, kButtonHeight,
                            "Add email account");
    Theme::StylePrimary(add);
    add->SetFontSize(kButtonFont);
    add->SetCornerRadius(kButtonRadius);
    add->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/envelope.svg"));
    add->SetIconPosition(ButtonIconPosition::Left);
    add->SetIconSize(kButtonIcon, kButtonIcon);
    add->SetUseIconAsMask(true);   // tint the glyph with the button's text colour
    add->onClick = [this]() { if (onAddAccount) onAddAccount(); };
    page_->AddChild(add);

    return page_;
}

void StartPage::Resize(float width, float height) {
    if (!page_) return;
    page_->SetElementSize(Size2Df(width, height));
}

} // namespace UltraMail
