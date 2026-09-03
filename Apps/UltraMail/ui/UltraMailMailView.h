// Apps/UltraMail/ui/UltraMailMailView.h
// The main window's mail area: a horizontal split pane with the message list
// of the selected account's inbox on the left (an "Inbox" group box holding a
// columns list: state · from · subject · date) and the message details on the
// right (a "Message" group box holding the MessagePreview). Driven by LocalStore.
// Version: 0.2.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro ordering; see MessagePreview.h).
#include "UltraCanvasContainer.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasColumnsTreeView.h"

#include "UltraMailMessagePreview.h"
#include "UltraMailLocalStore.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraMail {

class MailView {
public:
    void SetStore(LocalStore* store) { store_ = store; }
    void SetMailDir(std::string dir) { preview_.SetMailDir(std::move(dir)); }
    void SetAccounts(std::vector<Account> accounts) { preview_.SetAccounts(std::move(accounts)); }

    // Build the split pane. Call once; add the result to a parent.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Show an account's inbox (most recent first) and preview its newest message.
    void ShowAccount(const std::string& accountId);
    // Re-query the current account (after a sync or a flag change).
    void Reload();

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return root_; }

    // Forwarded to the preview.
    std::function<void(const Attachment&)> onOpenAttachment;
    std::function<void(const SourceMessage&, const std::string& selfName,
                       const std::string& selfAddr)> onReply;

private:
    void RebuildList();
    void SelectRow(int row);

    LocalStore* store_ = nullptr;
    std::string curAccount_;
    std::vector<MessageEnvelope> messages_;   // list rows, in list order

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvas::UltraCanvasGroupBox>  inboxBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasGroupBox>  messageBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasColumnsTreeView> list_;
    MessagePreview preview_;
};

} // namespace UltraMail
