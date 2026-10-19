// Apps/UltraSocial/ui/UltraSocialStartPage.cpp
// Version: 0.1.0 - UltraMail's start page for UltraSocial
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialStartPage.h"

#include "UltraSocialTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"

using namespace UltraCanvas;

namespace UltraSocial {

namespace {
// UltraMail's start-page metrics, so the two first screens match.
constexpr float kLogoSize     = 96.0f;
constexpr float kGap          = 16.0f;   // vertical gap between logo, title, button
constexpr float kButtonWidth  = 200.0f;
constexpr float kButtonHeight = 34.0f;
constexpr float kButtonFont   = 11.0f;
constexpr int   kButtonIcon   = 16;
constexpr float kButtonRadius = 6.0f;

// The networks the add-account wizard offers, in its order.
constexpr SocialNetwork kNetworks[] = {
    SocialNetwork::Mastodon, SocialNetwork::Bluesky, SocialNetwork::Telegram,
    SocialNetwork::Reddit,   SocialNetwork::X,       SocialNetwork::LinkedIn,
    SocialNetwork::Facebook,
};
} // namespace

std::shared_ptr<UltraCanvasContainer> StartPage::Build() {
    // A flex column that centres its children both ways. The page itself is
    // sized to the window by Resize(), so the centring follows the window.
    page_ = CreateContainer("startPage", 0, 0, 0, 0);
    page_->layout.SetFlexColumn()
                 .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Center)
                 .SetFlexGap(kGap);

    // Logo - the app icon itself (media/appicon/UltraSocial.png), the same
    // file main.cpp gives the window, so the start page shows what the
    // taskbar and the filer show.
    auto logo = CreateImageElement("startLogo", kLogoSize, kLogoSize);
    logo->LoadFromFile(Theme::AppIconPath());
    logo->SetFitMode(ImageFitMode::Contain);
    page_->AddChild(logo);

    // App title and one line on what it is for (fit-content, so the column
    // centres them on their real width).
    auto titles = CreateContainer("startTitles", 0, 0, 0, 0);
    titles->layout.SetFlexColumn()
                  .SetFlexGap(6)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto title = Theme::MakeText("startTitle", "UltraSocial", Theme::kSizeAppTitle,
                                 Theme::kTextPrimary, FontWeight::Bold);
    title->SetAlignment(TextAlignment::Center);
    titles->AddChild(title);
    auto tagline = Theme::MakeText("startTagline", "Write once - post to all your networks.",
                                   Theme::kSizeHeading, Theme::kTextSecondary);
    tagline->SetAlignment(TextAlignment::Center);
    titles->AddChild(tagline);
    page_->AddChild(titles);

    // The single call to action: a primary button with a person-plus glyph.
    auto add = CreateButton("startAddAccount", 0, 0, kButtonWidth, kButtonHeight,
                            "Add social account");
    Theme::FitToLabel(add, kButtonWidth);
    Theme::StylePrimary(add);
    add->SetFontSize(kButtonFont);
    add->SetCornerRadius(kButtonRadius);
    Theme::SetButtonIcon(add, "user-plus.svg", kButtonIcon);
    add->onClick = [this]() { if (onAddAccount) onAddAccount(); };
    page_->AddChild(add);

    // What can be connected: one avatar per network, the same round tiles the
    // compose view shows beside each account, each named in its tooltip.
    auto networks = CreateContainer("startNetworks", 0, 0, 0, 0);
    networks->layout.SetFlexColumn()
                    .SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto row = CreateContainer("startNetworkRow", 0, 0, 0, 0);
    row->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    int index = 0;
    for (SocialNetwork network : kNetworks) {
        row->AddChild(Theme::MakeNetworkAvatar("startNet" + std::to_string(index++),
                                               network, 22.0f));
    }
    networks->AddChild(row);
    auto caption = Theme::MakeText("startNetworksCaption",
                                   "Mastodon, Bluesky, Telegram, Reddit, X, LinkedIn "
                                   "and Facebook Pages",
                                   Theme::kSizeSecondary, Theme::kTextMuted);
    caption->SetAlignment(TextAlignment::Center);
    networks->AddChild(caption);
    page_->AddChild(networks);

    return page_;
}

void StartPage::Resize(float width, float height) {
    if (!page_) return;
    page_->SetElementSize(Size2Df(width, height));
}

} // namespace UltraSocial
