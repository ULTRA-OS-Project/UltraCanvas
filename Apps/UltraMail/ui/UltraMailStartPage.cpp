// Apps/UltraMail/ui/UltraMailStartPage.cpp
// Version: 0.2.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailStartPage.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr float kLogoSize     = 128.0f;
constexpr float kTitleSize    = 32.0f;   // font size of the app title
constexpr float kGap          = 24.0f;   // vertical gap between logo, title, button
constexpr float kButtonWidth  = 260.0f;
constexpr float kButtonHeight = 48.0f;
constexpr float kButtonFont   = 16.0f;
constexpr int   kButtonIcon   = 22;
constexpr float kButtonRadius = 8.0f;
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
    title->SetAlignment(TextAlignment::Center);
    page_->AddChild(title);

    // The single call to action: a primary button with an envelope icon.
    auto add = CreateButton("startAddAccount", 0, 0, kButtonWidth, kButtonHeight,
                            "Add email account");
    add->SetStyle(ButtonStyles::PrimaryStyle());
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
