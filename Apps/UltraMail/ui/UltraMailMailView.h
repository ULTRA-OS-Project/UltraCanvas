// Apps/UltraMail/ui/UltraMailMailView.h
// The main window's mail area: a horizontal split pane with the message list
// of the selected account's inbox on the left (an "Inbox" group box holding a
// columns list: state · from · subject · date) and the message details on the
// right (a "Message" group box holding the MessagePreview). Driven by LocalStore.
// Version: 0.3.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro ordering; see MessagePreview.h).
#include "UltraCanvasContainer.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasColumnsTreeView.h"

#include "UltraMailMessagePreview.h"
#include "UltraMailLocalStore.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <set>
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

    // Append freshly-synced messages to the list as their headers arrive, so a
    // large mailbox fills in instead of looking hung. No-op unless `accountId` is
    // the account currently shown. Appends in arrival order (newest UID first)
    // without disturbing the user's selection or the preview; the final Reload()
    // after the sync re-queries the store and puts rows in exact date order.
    void AppendMessages(const std::string& accountId,
                        const std::vector<MessageEnvelope>& batch);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return root_; }

    // Forwarded to the preview.
    std::function<void(const Attachment&)> onOpenAttachment;
    std::function<void(const Attachment&)> onSaveAttachment;
    std::function<void(const SourceMessage&, const std::string& selfName,
                       const std::string& selfAddr)> onReply;

private:
    void RebuildList();
    // Build one row for messages_[index] and add it under the list root. Keeps
    // the msg_<index> node id in step with messages_[index] (RowIndexOf relies on
    // it) and bumps shownUnread_ when the row is unread.
    void AddMessageRow(std::size_t index, const MessageEnvelope& m,
                       const std::set<int64_t>& waitingUids);
    void UpdateInboxTitle();
    void SelectRow(int row);

    LocalStore* store_ = nullptr;
    std::string curAccount_;
    std::vector<MessageEnvelope> messages_;   // list rows, in list order
    int shownUnread_ = 0;                      // unread count of the rows shown

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvas::UltraCanvasGroupBox>  inboxBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasGroupBox>  messageBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasColumnsTreeView> list_;
    MessagePreview preview_;
};

} // namespace UltraMail
