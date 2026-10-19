// Apps/UltraMail/ui/UltraMailMessagePreview.h
// The message detail pane: headers (subject, from, to, date), a Reply button,
// the body (HTML rendered natively through HTMLReader / CSSLayout, plain text
// in a read-only text area) and the attachment strip. Fed one envelope at a
// time from the mail view's list; the cached .eml body is decoded on show.
// Version: 0.13.0 - senderMenuItems: a right-click on the sender's name or badge
//                   opens the sender's menu
// Version: 0.12.0 - the message's text can be selected and copied: the HTML body
//                   (one selection across all of it), the header (subject,
//                   from, to, date), and a right-click menu with Copy and
//                   Select All over both bodies; a one-time code (a sign-in
//                   code, in any language) gets a copy button where it stands,
//                   or in a bar above the body
// Version: 0.11.0 - the sender checks as bordered labels in the header:
//                   [DMARC] [DKIM] [SPF] (and [S/MIME] / [OpenPGP]), details as
//                   tooltips
// Version: 0.10.0 - the sender badge asks for its icon when it has none
//                   (SetIconRequester) and shows it on arrival (IconCached)
// Version: 0.9.0 - onBodyMissing / BodyArrived: a message shown before its body
//                  was downloaded fetches it now and shows it when it arrives
// Version: 0.8.0 - onComposeTo: a clicked mail address (mailto:) is written to in
//                UltraMail, from the shown message's account
// Version: 0.7.0 - linkTooltips: a link's address as a tooltip (Settings > Display >
//                Links), or only through onLinkHovered
// Version: 0.6.0 - onLinksShown / onLinkHovered (the links of the shown body, and
//                the one under the pointer, for the status line)
// Version: 0.5.0 - Settings: HTML or plain-text view, body text size, and
//                  pictures hosted on trusted websites load by themselves.
// Version: 0.4.0 - the sender badge replaces the initial avatar, and a warning
//                  strip above the body says why a message looks like a scam.
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first: they pull in X11 (which defines Bool/Status),
// and the engine headers below undef those macros — so the UI headers must be
// fully processed before the engine headers are seen.
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasTextSelection.h"

#include "UltraMailAttachmentStrip.h"
#include "UltraMailInlineImages.h"
#include "UltraMailComposer.h"   // SourceMessage
#include "UltraMailSenderBadge.h"
#include "UltraMailTypes.h"
#include "UltraMailThreatScan.h"   // MessageLink
#include "UltraMailOneTimeCode.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraMail {

// "Jan 14, 2026 14:02" for an epoch second (UTC); empty for 0.
std::string FormatShortDate(int64_t epoch);

class MessagePreview {
public:
    void SetMailDir(std::string dir) { mailDir_ = std::move(dir); }
    void SetAccounts(std::vector<Account> accounts) { accounts_ = std::move(accounts); }

    // The store the pane reads a message's scan verdict from — and writes it
    // back to when it scans a body for the first time.
    void SetStore(LocalStore* store) { store_ = store; }
    // The address book and the icon cache behind the sender badge.
    void SetContacts(ContactIndex contacts) { badges_.SetContacts(std::move(contacts)); }
    void SetIconCache(const SenderIconCache* cache) { icons_ = cache; badges_.SetIconCache(cache); }
    // Asked for the icon the shown sender's badge lacks; IconCached shows it
    // once it has arrived.
    void SetIconRequester(std::function<void(const std::string& key)> request) {
        requestIcon_ = std::move(request);
    }
    void IconCached(const std::string& key);
    // Whether the folder being read is the account's junk mailbox.
    void SetJunkFolder(bool junk) { junkFolder_ = junk; }

    // Build the pane (a flex column). Call once; add the result to a parent.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Show one message: headers from the envelope, body + attachments from the
    // cached .eml under mailDir_/<account>/<folder>/<uid>.eml.
    void Show(const MessageEnvelope& env);
    // Back to the empty "Select a message" state.
    void Clear();

    // Whether the pane shows this message now.
    bool Shows(const std::string& accountId, const std::string& folder, int64_t uid) const {
        return hasMessage_ && curEnv_.uid == uid && curEnv_.folder == folder &&
               curEnv_.accountId == accountId;
    }
    // Whether the message shown had no downloaded body when it was shown.
    bool BodyMissing() const { return hasMessage_ && bodyMissing_; }
    // The body area's note while the message is not downloaded: "Downloading
    // the message…", or why it could not be. No-op unless the body is missing.
    void ShowBodyNote(const std::string& note);
    // A body the shown message did not have has been downloaded: show it.
    // No-op unless the pane shows that message and still lacks its body.
    void BodyArrived(const MessageEnvelope& env);
    // Raised by Show for a message whose body is not downloaded: the app
    // downloads it now instead of waiting for the next sync.
    std::function<void(const MessageEnvelope&)> onBodyMissing;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return root_; }

    // Delegated to the app (writes to cache + opens in UltraCanvasMediaViewer).
    std::function<void(const Attachment&)> onOpenAttachment;
    // Raised by the attachment strip's "Save As…" entry. Without it that menu
    // item does nothing at all.
    std::function<void(const Attachment&)> onSaveAttachment;
    // Delegated to the app: build a reply for the shown message.
    std::function<void(const SourceMessage&, const std::string& selfName,
                       const std::string& selfAddr)> onReply;
    // Delegated to the app: build a forward of the shown message.
    std::function<void(const SourceMessage&, const std::string& selfName,
                       const std::string& selfAddr)> onForward;
    // Delegated to the app: move the shown message to Trash (server + local).
    std::function<void(const MessageEnvelope&)> onDelete;
    // Delegated to the app: move the shown message to the Junk mailbox.
    std::function<void(const MessageEnvelope&)> onJunk;
    // Delegated to the app: clear \Seen on the shown message (server + local).
    std::function<void(const MessageEnvelope&)> onMarkUnread;
    // Delegated to the app: open the raw .eml source in a read-only window.
    std::function<void(const std::string& subject, const std::string& raw)> onViewSource;
    // Every link of the body just shown (where each really goes), so the window
    // can list them for the reader to check; empty for a body without links.
    std::function<void(const std::vector<MessageLink>&)> onLinksShown;
    // The link under the pointer in the body (its target), "" when it leaves.
    std::function<void(const std::string& href)> onLinkHovered;
    // A clicked mail address (a mailto: link, or an address written in plain
    // text): the app opens a new message to it, from this account's identity.
    // Unset, the system's mail handler gets the mailto: address.
    std::function<void(const std::string& selfName, const std::string& selfAddr,
                       const std::string& mailtoHref)> onComposeTo;

    // Remote images (http/https) are not loaded until the reader asks: a bar
    // above the body offers "Show images" for this message and "Always from
    // <sender>". `remoteImagesAllowed` answers whether a sender is on that
    // list; `onAlwaysAllowRemoteImages` adds one. Images embedded in the
    // message (cid:, data:) are always shown.
    std::function<bool(const std::string& address)> remoteImagesAllowed;
    std::function<void(const std::string& address)> onAlwaysAllowRemoteImages;
    // Whether a remote picture's own address (its host) is on the trusted
    // websites: such pictures load even when the sender is not trusted - never
    // in a suspicious message.
    std::function<bool(const std::string& url)> remoteImageHostTrusted;

    // Settings > Reading: HTML mail formatted (false: shown as plain text, no
    // pictures fetched) and the body text size in CSS px. ReRender() shows
    // the message on screen again with them.
    bool  showHtml = true;
    float bodyFontSizePx = 12.f;
    // Settings > Display > Links: show a link's address as a tooltip over the
    // link (false: the status line shows it, through onLinkHovered).
    bool  linkTooltips = false;
    void  ReRender();

    // The one-time codes of the message shown (UltraMailOneTimeCode.h): each
    // has a copy button - on the code itself when it stands in a box of its
    // own in the HTML body, otherwise the first one in the code bar above
    // the body.
    const std::vector<OneTimeCode>& OneTimeCodes() const { return codes_; }

    // Raised when a body was scanned for the first time (the verdict has been
    // stored already): the message list refreshes that row's badge.
    std::function<void(const MessageEnvelope&, const MessageSecurity&)> onSecurityScanned;
    // A right-click on the sender's name or badge: the items the sender's
    // menu offers above Copy and Select All - the address book, spam, the
    // sender's mail - for the message shown. Asked for as the menu opens.
    std::function<std::vector<UltraCanvas::MenuItemData>(const MessageEnvelope&)> senderMenuItems;

private:
    // A link of the body was clicked: web addresses open in the browser, mail
    // addresses through onComposeTo.
    void ActivateLink(const std::string& href);
    // Render a body into bodyHost_: HTML through the HTMLReader element
    // builder (CSSLayout engine), plain text into a read-only text area.
    void RenderBody(const std::string& body, bool isHtml);

    // The right-click menu over the message's text: Copy (offered when
    // something is selected) and Select All.
    // The sender's menu: its address as the title, senderMenuItems, then
    // Copy and Select All when the right-click was on the header's text.
    void ShowSenderMenu(const UltraCanvas::UCEvent& event, bool withText, bool canCopy,
                        std::function<void()> copy, std::function<void()> selectAll);
    // Whether a window point is over the sender's name or badge.
    bool PointerOnSender(const UltraCanvas::Point2Di& windowPoint) const;
    void ShowTextMenu(const UltraCanvas::UCEvent& event, bool canCopy,
                      std::function<void()> copy, std::function<void()> selectAll);
    // Gives a selection (the header's, the HTML body's) its menu, and makes
    // it the only one highlighted once it selects something.
    void WireTextSelection(const std::shared_ptr<UltraCanvas::UltraCanvasTextSelection>& selection);

    // Finds the body's one-time codes - `blocks` its text a paragraph (HTML:
    // a label, given in `labels`) or a line (plain text) at a time - and gives
    // them their copy buttons.
    void ShowOneTimeCodes(const std::vector<std::string>& blocks,
                          const std::vector<UltraCanvas::UltraCanvasLabel*>& labels);
    // Puts a code on the clipboard; the button shows a check mark for a moment.
    void CopyCode(const std::string& code, UltraCanvas::UltraCanvasButton* button);

    // The image loader behind RenderBody: embedded images from the message,
    // remote ones from remoteCache_ (noted in blockedRemote_ when absent).
    std::vector<uint8_t> LoadBodyImage(const std::string& src);
    // Show / hide the remote-images bar for the message on screen.
    void UpdateRemoteBar();
    // Download blockedRemote_ off the UI thread, then render the body again.
    void FetchRemoteImages();
    // Download `urls` only (pictures on trusted websites): the rest stay
    // blocked, and the bar keeps offering them.
    void FetchSomeRemoteImages(const std::vector<std::string>& urls);
    // Fetch the blocked pictures whose host is trusted, if any.
    void FetchTrustedHostImages();

    // The stored verdict for a message, scanning (and storing) the cached body
    // the first time it is read. `raw` is the .eml text, empty when it has not
    // been downloaded yet.
    MessageSecurity SecurityFor(const MessageEnvelope& env, const std::string& raw);

    // Fill (and show) the warning strip above the body, or hide it when the
    // message raised nothing.
    void ShowSecurityWarning(const SenderStatus& status, const MessageSecurity& security,
                             const std::string& raw);

    std::string          mailDir_;
    std::vector<Account> accounts_;
    std::string          curAccount_;
    bool                 hasMessage_ = false;
    bool                 bodyMissing_ = false;   // shown without a downloaded body
    bool                 junkFolder_ = false;
    LocalStore*          store_ = nullptr;
    SenderBadgeResolver  badges_;
    const SenderIconCache* icons_ = nullptr;
    std::function<void(const std::string& key)> requestIcon_;
    SenderBadge          shownBadge_;   // the badge beside the shown message

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> actions_;      // Reply · Forward · Junk · Delete · More
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    junkBtn_;      // hidden while reading the Junk mailbox
    std::shared_ptr<UltraCanvas::UltraCanvasMenu>      moreMenu_;     // "Mark as Unread" / "View source"
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     subject_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     from_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     to_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     date_;
    // [DMARC] [DKIM] [SPF]: the sender checks the receiving server made, a
    // small bordered label each (green passed, red failed, grey no verdict),
    // with the details as its tooltip; [S/MIME] / [OpenPGP] for a signed
    // message.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> authRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> header_;       // avatar · from/to · date · Reply
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> rule_;         // divider above the body
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> avatarHost_;   // sender badge
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> warning_;     // scam / spam strip
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     warningTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     warningText_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> bodyHost_;
    // The selectable text: the header's labels share one selection, the HTML
    // body's labels another (made with the body, null for plain text, which
    // its text area selects).
    std::shared_ptr<UltraCanvas::UltraCanvasTextSelection> headerSelection_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextSelection> bodySelection_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu>          textMenu_;   // Copy / Select All
    // One-time codes: the bar above the body, for a code without a button of
    // its own where it stands.
    std::vector<OneTimeCode>                           codes_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> codeBar_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     codeText_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    codeCopy_;
    AttachmentStrip attachmentStrip_;
    SourceMessage   current_;   // the shown message, for Reply / Forward
    MessageEnvelope curEnv_;    // the shown message's identity, for Delete / Junk / Mark-Unread
    std::string     curRaw_;    // the shown message's raw .eml, for View source

    // Images of the shown HTML body.
    std::string          curHtml_;             // the body, for a re-render
    std::string          lastBody_;            // what RenderBody showed last,
    bool                 lastIsHtml_ = false;  // for ReRender
    InlineImages         inlineImages_;        // parts of the message itself
    std::set<std::string> blockedRemote_;      // remote images not (yet) loaded
    bool                 remoteAllowed_ = false;   // load them without asking
    bool                 remoteDangerous_ = false; // scam / spam: no "Always"
    bool                 fetchingRemote_ = false;
    uint64_t             showToken_ = 0;       // bumps per Show/Clear: stale fetches drop
    // Downloaded remote images by URL, shared across messages (bounded).
    std::map<std::string, std::vector<uint8_t>> remoteCache_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> remoteBar_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     remoteText_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    remoteShow_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    remoteAlways_;
};

} // namespace UltraMail
