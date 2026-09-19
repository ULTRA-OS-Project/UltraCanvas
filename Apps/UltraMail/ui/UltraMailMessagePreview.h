// Apps/UltraMail/ui/UltraMailMessagePreview.h
// The message detail pane: headers (subject, from, to, date), a Reply button,
// the body (HTML rendered natively through HTMLReader / CSSLayout, plain text
// in a read-only text area) and the attachment strip. Fed one envelope at a
// time from the mail view's list; the cached .eml body is decoded on show.
// Version: 0.4.0 - the sender badge replaces the initial avatar, and a warning
//                  strip above the body says why a message looks like a scam.
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first: they pull in X11 (which defines Bool/Status),
// and the engine headers below undef those macros — so the UI headers must be
// fully processed before the engine headers are seen.
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include "UltraMailAttachmentStrip.h"
#include "UltraMailComposer.h"   // SourceMessage
#include "UltraMailSenderBadge.h"
#include "UltraMailTypes.h"

#include <functional>
#include <memory>
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
    void SetIconCache(const SenderIconCache* cache) { badges_.SetIconCache(cache); }
    // Whether the folder being read is the account's junk mailbox.
    void SetJunkFolder(bool junk) { junkFolder_ = junk; }

    // Build the pane (a flex column). Call once; add the result to a parent.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Show one message: headers from the envelope, body + attachments from the
    // cached .eml under mailDir_/<account>/<folder>/<uid>.eml.
    void Show(const MessageEnvelope& env);
    // Back to the empty "Select a message" state.
    void Clear();

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return root_; }

    // Delegated to the app (writes to cache + opens in UltraCanvasMediaViewer).
    std::function<void(const Attachment&)> onOpenAttachment;
    // Raised by the attachment strip's "Save As…" entry. Without it that menu
    // item does nothing at all.
    std::function<void(const Attachment&)> onSaveAttachment;
    // Delegated to the app: build a reply for the shown message.
    std::function<void(const SourceMessage&, const std::string& selfName,
                       const std::string& selfAddr)> onReply;

    // Raised when a body was scanned for the first time (the verdict has been
    // stored already): the message list refreshes that row's badge.
    std::function<void(const MessageEnvelope&, const MessageSecurity&)> onSecurityScanned;

private:
    // Render a body into bodyHost_: HTML through the HTMLReader element
    // builder (CSSLayout engine), plain text into a read-only text area.
    void RenderBody(const std::string& body, bool isHtml);

    // The stored verdict for a message, scanning (and storing) the cached body
    // the first time it is read. `raw` is the .eml text, empty when it has not
    // been downloaded yet.
    MessageSecurity SecurityFor(const MessageEnvelope& env, const std::string& raw);

    // Fill (and show) the warning strip above the body, or hide it when the
    // message raised nothing.
    void ShowSecurityWarning(const SenderStatus& status, const MessageSecurity& security);

    std::string          mailDir_;
    std::vector<Account> accounts_;
    std::string          curAccount_;
    bool                 hasMessage_ = false;
    bool                 junkFolder_ = false;
    LocalStore*          store_ = nullptr;
    SenderBadgeResolver  badges_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     subject_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     from_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     to_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     date_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> header_;       // avatar · from/to · date · Reply
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> rule_;         // divider above the body
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> avatarHost_;   // sender badge
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> warning_;     // scam / spam strip
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     warningTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     warningText_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> bodyHost_;
    AttachmentStrip attachmentStrip_;
    SourceMessage   current_;   // the shown message, for Reply
};

} // namespace UltraMail
