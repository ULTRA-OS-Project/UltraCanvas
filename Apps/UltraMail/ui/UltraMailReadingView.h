// Apps/UltraMail/ui/UltraMailReadingView.h
// The three-pane mail reading view: folder tree (accounts + folders) | message
// list (for the selected folder) | preview (headers + body + attachments). The
// list is driven by LocalStore; the preview decodes the cached .eml body via
// the MIME codec and shows attachments through the AttachmentStrip.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first: they pull in X11 (which defines Bool/Status),
// and the engine headers below undef those macros — so the UI headers must be
// fully processed before the engine headers are seen.
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"

#include "UltraMailAttachmentStrip.h"
#include "UltraMailLocalStore.h"
#include "UltraMailComposer.h"   // SourceMessage

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraMail {

// A selectable message row (single-click selects, double-click raises onOpen).
class MessageRow : public UltraCanvas::UltraCanvasContainer {
public:
    MessageRow(const std::string& id, float x, float y, float w, float h,
               const MessageEnvelope& env,
               std::function<void()> onSelect,
               std::function<void()> onOpen);
    bool OnEvent(const UltraCanvas::UCEvent& event) override;
private:
    std::function<void()> onSelect_;
    std::function<void()> onOpen_;
};

class ReadingView {
public:
    void SetStore(LocalStore* store) { store_ = store; }
    void SetMailDir(std::string dir) { mailDir_ = std::move(dir); }
    void SetAccounts(std::vector<Account> accounts) { accounts_ = std::move(accounts); }

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    void SelectFolder(const std::string& accountId, const std::string& folder);
    void SelectMessage(const MessageEnvelope& env);

    // Delegated to the app (writes to cache + opens in UltraCanvasMediaViewer).
    std::function<void(const Attachment&)> onOpenAttachment;

    // Delegated to the app: build a reply for the selected message.
    std::function<void(const SourceMessage&, const std::string& selfName,
                       const std::string& selfAddr)> onReply;

private:
    void RebuildFolders();
    void RebuildList();

    LocalStore*        store_ = nullptr;
    std::string        mailDir_;
    std::vector<Account> accounts_;
    std::string        curAccount_;
    std::string        curFolder_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> folderPane_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> listPane_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> previewPane_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     hdrFrom_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     hdrSubject_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     hdrDate_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  body_;
    AttachmentStrip    attachmentStrip_;
    SourceMessage      current_;   // the selected message, for Reply
};

} // namespace UltraMail
