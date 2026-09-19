// Apps/UltraMail/ui/UltraMailMailView.h
// The main window's mail area, Thunderbird/Gmail style: an outer horizontal
// split with a folder tree on the left (one email root per account, its
// mailboxes beneath) and, on the right, the content area — either the message
// list beside the message preview (reading pane on) or the list alone with the
// clicked message opening in its place (reading pane off). Driven by LocalStore.
// Version: 0.5.0 - sender-badge column between From and Subject (address book,
//                  known-sender registry and the stored content-scan verdict).
// Version: 0.4.0 - folder sidebar, UltraCanvasListView message list, reading-
//                  pane toggle, per-folder view with lazy sync hook.
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro ordering; see MessagePreview.h).
#include "UltraCanvasContainer.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasButton.h"

#include "UltraMailMessagePreview.h"
#include "UltraMailSenderBadge.h"
#include "UltraMailLocalStore.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace UltraMail {

// One list row's read/answer state, read by the message-list row delegate to
// pick the text colour (the ● / ↩ glyphs themselves live in the row text).
struct MailRowState {
    bool unread  = false;
    bool waiting = false;
};

class MailView {
public:
    void SetStore(LocalStore* store) { store_ = store; preview_.SetStore(store); }
    void SetMailDir(std::string dir) { preview_.SetMailDir(std::move(dir)); }
    // Keep the account list (drives the folder tree) and forward it to the
    // preview (which resolves the "self" address for replies).
    void SetAccounts(std::vector<Account> accounts);

    // The address book behind the sender badge: who is a contact, and in which
    // section. Re-set it after the address book changes; the next Reload()
    // draws the new colours.
    void SetContacts(ContactIndex contacts);
    // The sender-icon cache the badge reads brand icons from (not owned).
    void SetIconCache(const SenderIconCache* cache);

    // Build the mail area. Call once; add the result to a parent.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Show an account (its inbox, or — when re-shown for the same account after
    // a sync — the folder currently open). Rebuilds the folder tree.
    void ShowAccount(const std::string& accountId);
    // Show a specific folder of an account (a folder-tree click). Rebuilds the
    // list and raises onOpenFolder so the app can lazily fetch it.
    void ShowFolder(const std::string& accountId, const std::string& folder);
    // Re-query the current account/folder (after a sync or a flag change).
    void Reload();

    // Append freshly-synced messages to the list as their headers arrive, so a
    // large mailbox fills in instead of looking hung. No-op unless the batch is
    // for the account and folder currently shown (each envelope carries its
    // folder). The final Reload() after the sync re-queries the store and puts
    // rows in exact date order.
    void AppendMessages(const std::string& accountId,
                        const std::vector<MessageEnvelope>& batch);

    // Turn the message preview (reading) pane on or off. On: list | preview
    // side by side. Off (Gmail): the list fills the area and a clicked message
    // opens in its place, with a "Back to list" button. Rebuilds the content.
    void SetReadingPane(bool on);
    bool ReadingPane() const { return readingPane_; }

    // The folder currently shown (for Reload, which fetches it too).
    const std::string& CurrentFolder() const { return curFolder_; }

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return root_; }

    // Forwarded to the preview.
    std::function<void(const Attachment&)> onOpenAttachment;
    std::function<void(const Attachment&)> onSaveAttachment;
    std::function<void(const SourceMessage&, const std::string& selfName,
                       const std::string& selfAddr)> onReply;

    // The folder tree selected a folder under a different account: the app
    // updates the selected account (and the account bar) without re-showing the
    // inbox, so the tree's chosen folder stays open.
    std::function<void(const std::string& accountId)> onSelectAccount;
    // A folder was opened from the tree: the app may lazily sync it if it has
    // never been fetched (only the inbox is synced up front).
    std::function<void(const std::string& accountId, const std::string& folder)> onOpenFolder;

private:
    // Layout ----------------------------------------------------------------
    void ApplyContentLayout();          // (re)build contentHost_ per readingPane_
    void BuildListBox();                // listBox_ + list_ + model_ + delegate_
    void BuildMessageBox();             // messageBox_ + preview_
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildBackBar();
    void OpenMessageInPlace();          // Gmail mode: show the message, hide the list
    void ShowListInPlace();             // Gmail mode: back to the list

    // Folder tree -----------------------------------------------------------
    void RebuildFolderTree();
    void SelectFolderNode(const std::string& accountId, const std::string& folder);

    // Message list ----------------------------------------------------------
    void RebuildList();
    void AddMessageRow(const MessageEnvelope& m, const std::set<int64_t>& waitingUids);
    // The badge for one message, from the address book, the brand registry and
    // the stored content-scan verdict.
    SenderBadge BadgeFor(const MessageEnvelope& m) const;
    // True when the folder on screen is the account's junk/spam mailbox (a
    // message sitting in it is spam by the server's own verdict).
    bool CurrentFolderIsJunk() const;
    // Re-draw one row's badge after the reading pane scanned that message's
    // body for the first time.
    void RefreshRowBadge(const MessageEnvelope& message, const MessageSecurity& security);
    void UpdateListTitle();
    void SelectRow(int row);

    LocalStore*                  store_ = nullptr;
    std::vector<Account>         accounts_;
    std::string                  curAccount_;
    std::string                  curFolder_ = "INBOX";
    std::vector<MessageEnvelope> messages_;    // list rows, in list order
    std::vector<MailRowState>    rowStates_;    // parallel to messages_ / list rows
    std::vector<SenderBadge>     rowBadges_;    // parallel to messages_ / list rows
    // The stored scan verdicts of the folder on screen, by UID — one query per
    // list rather than one per row.
    std::map<int64_t, MessageSecurity> security_;
    SenderBadgeResolver          badges_;
    bool                         curFolderIsJunk_ = false;
    int                          shownUnread_ = 0;
    bool                         readingPane_ = true;
    bool                         suppressTreeCallback_ = false;

    // Folder-tree node id -> (accountId, folderName).
    std::map<std::string, std::pair<std::string, std::string>> folderNodeId_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane>  outerSplit_;
    std::shared_ptr<UltraCanvas::UltraCanvasGroupBox>   folderBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasTreeView>   folderTree_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer>  contentHost_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane>  innerSplit_;   // reading-pane mode
    std::shared_ptr<UltraCanvas::UltraCanvasGroupBox>   listBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasGroupBox>   messageBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer>  backBar_;      // Gmail mode
    std::shared_ptr<UltraCanvas::UltraCanvasListView>   list_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> model_;
    std::shared_ptr<UltraCanvas::IItemDelegate>         delegate_;
    MessagePreview preview_;
};

} // namespace UltraMail
