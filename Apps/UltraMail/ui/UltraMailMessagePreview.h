// Apps/UltraMail/ui/UltraMailMessagePreview.h
// The message detail pane: headers (subject, from, to, date), a Reply button,
// the body (HTML rendered natively through HTMLReader / CSSLayout, plain text
// in a read-only text area) and the attachment strip. Fed one envelope at a
// time from the mail view's list; the cached .eml body is decoded on show.
// Version: 0.2.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first: they pull in X11 (which defines Bool/Status),
// and the engine headers below undef those macros — so the UI headers must be
// fully processed before the engine headers are seen.
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include "UltraMailAttachmentStrip.h"
#include "UltraMailComposer.h"   // SourceMessage
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

private:
    // Render a body into bodyHost_: HTML through the HTMLReader element
    // builder (CSSLayout engine), plain text into a read-only text area.
    void RenderBody(const std::string& body, bool isHtml);

    std::string          mailDir_;
    std::vector<Account> accounts_;
    std::string          curAccount_;
    bool                 hasMessage_ = false;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     subject_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     from_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     to_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     date_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> bodyHost_;
    AttachmentStrip attachmentStrip_;
    SourceMessage   current_;   // the shown message, for Reply
};

} // namespace UltraMail
