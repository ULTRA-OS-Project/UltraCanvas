// Apps/UltraMail/ui/UltraMailComposeWindow.h
// The compose surface: To / Cc / Subject fields, an editable body, the
// attachment strip, and the buttons Send / Cancel / Attach file / Attach cloud
// link. Built from a Draft (blank, reply-prefilled or forward-prefilled) and
// hands an updated Draft back through onSend. "Attach file" reads a local file
// into the draft's attachments; "Attach cloud link" uploads through (or picks
// from) an UltraCloud account and puts the share link into the body.
// The body is plain text or formatted, switched with Plain text | Formatted
// at the end of the formatting toolbar (UltraMailFormatBar, the signature
// editor's tools). A draft with a formatted body - the reply or forward of an
// HTML message, or any draft signed with an HTML signature - opens formatted.
// Plain text shows only the switch; switching back to it drops the formatting
// and pictures, after asking.
// Every compose window has its own view (UltraMailApp keeps one per window),
// held by a shared_ptr: the answers that can come after its window closed -
// the file and cloud pickers, the plain-text question, a Link… or Picture…
// dialog - hold it weakly and find nothing to change.
// Version: 0.7.0 - one view per window: owned by a shared_ptr, late dialog
//                  answers hold it weakly
// Version: 0.6.0 - the full formatting toolbar, and the Plain text | Formatted
//                  switch
// Version: 0.5.0
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers before engine headers (X11 macro ordering).
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextArea.h"

#include "UltraMailAttachmentStrip.h"
#include "UltraMailFormatBar.h"
#include "UltraMailComposer.h"

#include <UltraCloud/UltraCloudService.h>

#include <functional>
#include <memory>
#include <string>

namespace UltraMail {

// Create it with std::make_shared: Build() hands weak references to itself to
// the dialogs it opens.
class ComposeView : public std::enable_shared_from_this<ComposeView> {
public:
    void SetDraft(Draft draft) { draft_ = std::move(draft); }
    // The window the composer lives in (parent of its file and cloud dialogs).
    void SetParentWindow(UltraCanvas::UltraCanvasWindowBase* window) { parent_ = window; }
    // The cloud service behind "Attach cloud link" (null = button disabled).
    void SetCloud(UltraCloud::CloudService* cloud) { cloud_ = cloud; }

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();
    // Size the view to the window's client area (call on window resize).
    void Resize(float width, float height);

    // Add a local file to the draft's attachments (shown in the strip).
    bool AttachFile(const std::string& path);
    // Put a share link into the body: "<name>: <url>" on its own line.
    void InsertLink(const std::string& name, const std::string& url);
    // Open the cloud link picker (what "Attach cloud link…" does).
    void OpenCloudLinkPicker() { ChooseCloudLink(); }

    // Raised when Send is clicked, with the edited draft.
    std::function<void(const Draft&)> onSend;
    // Raised when Cancel is clicked.
    std::function<void()> onCancel;

private:
    Draft CollectDraft() const;
    void ChooseFileToAttach();
    void ChooseCloudLink();
    // Rebuild the chips and show the attachment row only while there are any.
    void RefreshAttachments();
    // Switches the body between the plain-text area and the rich editor,
    // carrying the text across. Formatted -> plain drops the formatting, so it
    // asks first when there is something to lose (unless `ask` is false).
    void SetFormatted(bool formatted, bool ask = true);
    // Shows the editor, the tools and the switch position for formatted_.
    void ShowMode();
    // Formatted -> plain text, without asking.
    void SwitchToPlain();

    Draft draft_;
    UltraCanvas::UltraCanvasWindowBase* parent_ = nullptr;
    UltraCloud::CloudService* cloud_ = nullptr;
    AttachmentStrip attachments_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> to_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> cc_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> bcc_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> bccRow_;   // shown by the Bcc toggle
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    bccToggle_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> subject_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  body_;       // plain drafts
    std::shared_ptr<UltraCanvas::UltraCanvasRichTextEdit> rich_;    // formatted drafts
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> mode_;  // Plain text | Formatted
    FormatBar formatBar_;
    bool formatted_ = false;        // which of body_ / rich_ holds the message
    bool switchingMode_ = false;    // SetSelectedIndex from code, not a click
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> attachWrap_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
};

} // namespace UltraMail
