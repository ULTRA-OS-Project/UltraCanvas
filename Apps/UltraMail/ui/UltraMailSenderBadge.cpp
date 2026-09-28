// Apps/UltraMail/ui/UltraMailSenderBadge.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSenderBadge.h"

#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"

#include "UltraMailTheme.h"

#include <UltraNet/UltraNetMime.h>

#include <cctype>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

// The badge's own geometry, as fractions of its side, so it reads the same at
// 18px in a list row and at 28px in the reading pane.
constexpr float kRadiusFraction = 0.28f;
constexpr float kBorderWidth    = 1.6f;

Color FromRgb(uint32_t rgb) {
    return Color(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                 static_cast<uint8_t>((rgb >> 8) & 0xFF),
                 static_cast<uint8_t>(rgb & 0xFF));
}

// The first letter of the sender, for the monogram fallback.
std::string InitialOf(const std::string& name, const std::string& addr) {
    for (char c : (name.empty() ? addr : name))
        if (std::isalnum(static_cast<unsigned char>(c)))
            return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return "?";
}

} // namespace

BadgeColors BadgeColorsFor(SenderClass cls) {
    BadgeColors c;
    switch (cls) {
        case SenderClass::Friend:
            c.fill = Theme::kTrustFriendSoft;   c.border = Theme::kTrustFriend;
            c.text = Theme::kTrustFriend;       c.outlined = false;  break;
        case SenderClass::Business:
            c.fill = Theme::kTrustBusinessSoft; c.border = Theme::kTrustBusiness;
            c.text = Theme::kTrustBusiness;     c.outlined = false;  break;
        case SenderClass::New:
            c.fill = Theme::kCardBackground;    c.border = Theme::kTrustNew;
            c.text = Theme::kTrustNew;          c.outlined = true;   break;
        case SenderClass::Advertisement:
            c.fill = Theme::kCardBackground;    c.border = Theme::kTrustAdvert;
            c.text = Theme::kTrustAdvert;       c.outlined = true;   break;
        case SenderClass::Spam:
            c.fill = Theme::kTrustSpamSoft;     c.border = Theme::kTrustSpam;
            c.text = Theme::kTrustSpam;         c.outlined = true;   break;
        case SenderClass::Scam:
            c.fill = Theme::kTrustScamSoft;     c.border = Theme::kTrustScam;
            c.text = Theme::kTrustScam;         c.outlined = true;   break;
    }
    return c;
}

// ---------------------------------------------------------------------------
// Resolving
// ---------------------------------------------------------------------------
SenderStatus SenderBadgeResolver::Classify(const MessageEnvelope& message,
                                           const MessageSecurity& security,
                                           bool junkFolder) const {
    SenderIdentity who;
    who.address     = message.fromAddr;
    // Headers synced before decoding was added are still stored raw, and the
    // brand-impersonation check reads this text — decode defensively (decoding
    // already-decoded text is a no-op).
    who.displayName = UltraNet_MimeDecodeHeader(message.fromName);
    who.subject     = UltraNet_MimeDecodeHeader(message.subject);
    who.junkFolder  = junkFolder;
    who.level       = security.level;
    who.bulk        = security.bulk;
    return ClassifySender(who, contacts_);
}

SenderBadge SenderBadgeResolver::Resolve(const MessageEnvelope& message,
                                         const MessageSecurity& security,
                                         bool junkFolder) const {
    const SenderStatus status = Classify(message, security, junkFolder);

    SenderBadge badge;
    badge.cls     = status.cls;
    badge.initial = InitialOf(UltraNet_MimeDecodeHeader(message.fromName), message.fromAddr);
    if (!status.brandId.empty()) {
        badge.brandColor = FromRgb(status.brandAccentRgb);
        if (icons_) badge.iconPath = icons_->IconForBrand(status.brandId);
    }

    // The tooltip is the whole story: what the sender is, then — when the scan
    // had something to say — why.
    std::string tip = DisplayName(status.cls);
    if (!status.brandName.empty())
        tip += " \xC2\xB7 " + status.brandName + " (" +
               DisplayName(status.brandCategory) + ")";   // " · Kickstarter (…)"
    if (!status.reason.empty())    tip += "\n" + status.reason;
    if (!security.reason.empty())  tip += "\n" + security.reason;
    badge.tooltip = tip;
    return badge;
}

// ---------------------------------------------------------------------------
// Painting (message-list cells)
// ---------------------------------------------------------------------------
void DrawSenderBadge(IRenderContext* ctx, const Rect2Dd& rect, const SenderBadge& badge) {
    if (!ctx || rect.width <= 0 || rect.height <= 0) return;
    const BadgeColors colors = BadgeColorsFor(badge.cls);
    const double side   = rect.width < rect.height ? rect.width : rect.height;
    const double radius = side * kRadiusFraction;

    // A brand icon is the badge on its own: no frame and no fill, drawn at the
    // full cell so the logo reads at list size.
    if (!badge.iconPath.empty()) {
        ctx->DrawImage(badge.iconPath, rect, ImageFitMode::Contain);
        return;
    }

    ctx->DrawFilledRectangle(rect, colors.fill, kBorderWidth, colors.border,
                             static_cast<float>(radius));

    // No icon: the sender's initial, in the brand's colour when the address
    // belongs to a known service and in the verdict's colour otherwise.
    ctx->SetFontSize(static_cast<float>(side * 0.5));
    ctx->SetTextWrap(TextWrap::WrapNone);
    ctx->SetTextAlignment(TextAlignment::Center);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->SetTextPaint(colors.text);
    ctx->DrawTextInRect(badge.initial, rect);
}

// ---------------------------------------------------------------------------
// The same badge as elements (reading pane)
// ---------------------------------------------------------------------------
std::shared_ptr<UltraCanvasContainer>
MakeSenderBadgeElement(const std::string& id, const SenderBadge& badge, float side) {
    const BadgeColors colors = BadgeColorsFor(badge.cls);

    auto box = CreateContainer(id, 0, 0, side, side);
    box->layout.SetFlexRow()
               .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    // Chrome, not content: a badge never grows a scrollbar of its own.
    if (auto style = box->GetContainerStyle(); true) {
        style.autoShowScrollbars = false;
        box->SetContainerStyle(style);
    }

    // A brand icon stands alone, frameless and at the full badge size.
    if (!badge.iconPath.empty()) {
        auto icon = CreateImageElement(id + ".icon", 0, 0, side, side);
        icon->SetFitMode(ImageFitMode::Contain);
        if (icon->LoadFromFile(badge.iconPath)) {
            icon->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            box->AddChild(icon);
            return box;
        }
        // A cached file the image loader cannot decode falls through to the
        // framed monogram rather than leaving an empty square.
    }

    box->SetBackgroundColor(colors.fill);
    box->SetBorders(kBorderWidth, colors.border, side * kRadiusFraction);

    auto letter = Theme::MakeText(id + ".letter", badge.initial, side * 0.45f,
                                  colors.text, FontWeight::Bold);
    letter->SetAlignment(TextAlignment::Center);
    box->AddChild(letter);
    return box;
}

} // namespace UltraMail
