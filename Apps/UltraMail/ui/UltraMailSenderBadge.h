// Apps/UltraMail/ui/UltraMailSenderBadge.h
// The sender badge: the small square left of every subject line that says who
// a message is from before it is opened — the service's own icon when the
// address belongs to one the icon cache knows, otherwise the sender's initial
// — inside a frame whose colour is the verdict:
//
//   filled green    in the address book, a private section (Family/Friends/…)
//   filled blue     in the address book, Work or Services
//   black outline   a sender never seen before
//   dark blue       bulk mail: a newsletter or an advertisement
//   orange          likely spam
//   red             likely scam: the content scan found links that lie
//
// Two shapes of the same value are offered, because the two places that show
// it are different kinds of surface:
//   * DrawSenderBadge — for the message list, painted by the list's item
//     delegate (the framework's model-view-delegate path; a list cell is not
//     a place a child element can live);
//   * MakeSenderBadgeElement — for the reading pane, built out of catalogue
//     elements (container + image element + label) like every other widget.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro ordering; see MessagePreview.h).
#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"

#include "UltraMailLocalStore.h"        // MessageSecurity
#include "UltraMailSenderIconCache.h"
#include "UltraMailSenderTrust.h"
#include "UltraMailTypes.h"

#include <memory>
#include <string>

namespace UltraMail {

// How one badge is drawn.
struct SenderBadge {
    SenderClass cls      = SenderClass::New;
    std::string iconPath;                 // cached brand icon, or "" for the monogram
    std::string initial  = "?";           // monogram fallback
    UltraCanvas::Color brandColor = UltraCanvas::Color(91, 100, 112);
    std::string tooltip;                  // the whole explanation, ready to show
};

// The badge's frame/fill/text colours for one class.
struct BadgeColors {
    UltraCanvas::Color fill;
    UltraCanvas::Color border;
    UltraCanvas::Color text;
    bool outlined = false;   // stranger: a coloured frame around a pale tile
};
BadgeColors BadgeColorsFor(SenderClass cls);

// Turns an envelope plus its stored scan verdict into a badge. Holds the
// address-book index and (optionally) the icon cache; both may be swapped at
// any time — the next list rebuild picks the new ones up.
class SenderBadgeResolver {
public:
    void SetContacts(ContactIndex contacts) { contacts_ = std::move(contacts); }
    const ContactIndex& Contacts() const { return contacts_; }
    void SetIconCache(const SenderIconCache* cache) { icons_ = cache; }

    // `security` may be an unscanned default — the badge then rests on the
    // address book and the brand alone.
    SenderBadge Resolve(const MessageEnvelope& message,
                        const MessageSecurity& security,
                        bool junkFolder) const;

    // The classification on its own (the reading pane shows it in words).
    SenderStatus Classify(const MessageEnvelope& message,
                          const MessageSecurity& security,
                          bool junkFolder) const;

private:
    ContactIndex            contacts_;
    const SenderIconCache*  icons_ = nullptr;
};

// Paint one badge inside `rect` (used by the message list's item delegate).
void DrawSenderBadge(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Rect2Dd& rect,
                     const SenderBadge& badge);

// The same badge as a real element, for the reading pane's header row.
std::shared_ptr<UltraCanvas::UltraCanvasContainer>
MakeSenderBadgeElement(const std::string& id, const SenderBadge& badge, float side);

} // namespace UltraMail
