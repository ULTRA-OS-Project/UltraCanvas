// Apps/UltraMail/ui/UltraMailMailView.cpp
// Version: 0.4.0 - decode RFC 2047 headers for display; the list/preview
//                  splitter stays put across message selection (only a manual
//                  drag moves it).
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailMailView.h"

#include "UltraMailTheme.h"

#include <UltraNet/UltraNetMime.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

// Column ids (keys into TreeNodeData::cells; "from" is the tree column and
// reads the node text, which carries the state glyphs as a prefix).
const char* kColSubject = "subject";
const char* kColDate    = "date";
const char* kRootId     = "inboxRoot";
const char* kRowPrefix  = "msg_";

constexpr int kFromWidth    = 160;
constexpr int kSubjectMin   = 140;
constexpr int kDateWidth    = 88;
constexpr int kRowHeight    = 22;
constexpr int kHeaderHeight = 22;
constexpr int kSplitterGap  = 8;   // the page shows through between the cards

constexpr int kListMinWidth    = 320;
constexpr int kPreviewMinWidth = 340;

const Color& kUnreadText = Theme::kTextPrimary;
const Color& kReadText   = Theme::kTextSecondary;

// Fill a container (group box or split pane) with one child that takes the
// whole content area.
void FillWith(const std::shared_ptr<UltraCanvasContainer>& host,
              const std::shared_ptr<UltraCanvasUIElement>& child) {
    host->layout.SetFlexColumn()
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    host->AddChild(child);
    child->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

// The list's date column, the way mail clients shorten it: the time for
// today, "Sep 09" for this year, "Jan 14, 2025" for anything older.
std::string FormatListDate(int64_t epoch) {
    if (epoch <= 0) return "";
    std::time_t t = static_cast<std::time_t>(epoch);
    std::time_t now = std::time(nullptr);
    std::tm tm{}, tmNow{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
    gmtime_s(&tmNow, &now);
#else
    gmtime_r(&t, &tm);
    gmtime_r(&now, &tmNow);
#endif
    char buf[32];
    if (tm.tm_year == tmNow.tm_year && tm.tm_yday == tmNow.tm_yday)
        std::strftime(buf, sizeof buf, "%H:%M", &tm);
    else if (tm.tm_year == tmNow.tm_year)
        std::strftime(buf, sizeof buf, "%b %d", &tm);
    else
        std::strftime(buf, sizeof buf, "%b %d, %Y", &tm);
    return buf;
}

int RowIndexOf(const TreeNode* node) {
    if (!node) return -1;
    const std::string& id = node->data.nodeId;
    if (id.rfind(kRowPrefix, 0) != 0) return -1;
    return std::atoi(id.c_str() + std::strlen(kRowPrefix));
}

} // namespace

std::shared_ptr<UltraCanvasContainer> MailView::Build() {
    root_ = CreateContainer("mailView", 0, 0, 0, 0);
    root_->layout.SetFlexColumn()
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    split_ = std::make_shared<UltraCanvasSplitPane>("mailSplit", 0, 0, 0, 0,
                                                    SplitOrientation::Horizontal);
    auto listPane    = split_->AddPane(1.15);
    auto previewPane = split_->AddPane(1.0);
    split_->SetPaneMinSize(0, kListMinWidth);
    split_->SetPaneMinSize(1, kPreviewMinWidth);
    // The splitter is an invisible gap: the two cards read as separate
    // surfaces on the page rather than as halves of one box.
    SplitPaneStyle splitStyle;
    splitStyle.splitterThickness     = kSplitterGap;
    splitStyle.showSplitterBackground = false;
    splitStyle.splitterColor         = Theme::kPageBackground;
    splitStyle.splitterHoverColor    = Theme::kCardBorder;
    splitStyle.splitterActiveColor   = Theme::kAccent;
    split_->SetSplitPaneStyle(splitStyle);

    // Left: the inbox list (state · from · subject · date).
    inboxBox_ = CreateGroupBox("inboxBox", 0, 0, 0, 0, "Inbox");
    inboxBox_->SetFrameStyle(GroupBoxFrameStyle::Header);
    inboxBox_->SetVisualStyle(Theme::CardGroupBox(6.0f));
    list_ = std::make_shared<UltraCanvasColumnsTreeView>("inboxList", 0, 0, 0, 0);
    list_->SetDisplayMode(TreeDisplayMode::Columns);
    list_->SetSelectionMode(TreeSelectionMode::Single);
    list_->SetShowColumnHeader(true);
    list_->SetRowHeight(kRowHeight);
    list_->SetRootVisible(false);
    list_->SetShowExpandButtons(false);
    list_->SetIndentSize(6);
    list_->SetFontSize(Theme::kSizeBody);
    list_->SetTextColor(Theme::kTextPrimary);
    list_->SetSelectionColor(Theme::kRowSelected);
    list_->SetHoverColor(Theme::kRowHover);
    list_->SetLineColor(Colors::Transparent);
    list_->SetBorders(1.0f, Theme::kDivider, Theme::kControlRadius);
    list_->SetColumns({
        { "from",      "From",    kFromWidth,  0, 1.0f, TextAlignment::Left,
          kUnreadText, Colors::Transparent, 0, /*isTreeColumn=*/true },
        { kColSubject, "Subject", 0, kSubjectMin, 1.0f, TextAlignment::Left,
          kUnreadText, Colors::Transparent, 0, false },
        { kColDate,    "Date",    kDateWidth,  0, 1.0f, TextAlignment::Left,
          kUnreadText, Colors::Transparent, 0, false },
    });
    TreeColumnStyle columnStyle;
    columnStyle.headerHeight      = kHeaderHeight;
    columnStyle.headerBackground  = Theme::kSidebar;
    columnStyle.headerTextColor   = Theme::kTextSecondary;
    columnStyle.headerBorderColor = Theme::kDivider;
    columnStyle.columnGap         = 10;
    list_->SetColumnStyle(columnStyle);
    list_->onNodeSelected = [this](TreeNode* node) { SelectRow(RowIndexOf(node)); };
    FillWith(inboxBox_, list_);
    FillWith(listPane, inboxBox_);

    // Right: the message details.
    messageBox_ = CreateGroupBox("messageBox", 0, 0, 0, 0, "Message");
    messageBox_->SetFrameStyle(GroupBoxFrameStyle::Header);
    messageBox_->SetVisualStyle(Theme::CardGroupBox(Theme::kPagePadding));
    preview_.onSaveAttachment = [this](const Attachment& a) {
        if (onSaveAttachment) onSaveAttachment(a);
    };
    preview_.onOpenAttachment = [this](const Attachment& a) {
        if (onOpenAttachment) onOpenAttachment(a);
    };
    preview_.onReply = [this](const SourceMessage& src, const std::string& n,
                              const std::string& a) {
        if (onReply) onReply(src, n, a);
    };
    FillWith(messageBox_, preview_.Build());
    FillWith(previewPane, messageBox_);

    root_->AddChild(split_);
    split_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return root_;
}

void MailView::ShowAccount(const std::string& accountId) {
    curAccount_ = accountId;
    RebuildList();
}

void MailView::Reload() {
    RebuildList();
}

void MailView::AddMessageRow(std::size_t index, const MessageEnvelope& m,
                             const std::set<int64_t>& waitingUids) {
    const bool isUnread  = (m.flags & Flag_Seen) == 0;
    const bool isWaiting = waitingUids.count(m.uid) > 0;
    if (isUnread) ++shownUnread_;
    const Color& text = isUnread ? kUnreadText : kReadText;

    // Decode defensively: messages synced before header decoding are still
    // stored raw. Decoding already-decoded text is a no-op.
    std::string sender  = UltraNet_MimeDecodeHeader(
        m.fromName.empty() ? m.fromAddr : m.fromName);
    std::string subject = m.subject.empty()
        ? std::string("(no subject)") : UltraNet_MimeDecodeHeader(m.subject);

    // State glyphs in front of the sender: ● unread, ↩ waiting for a reply.
    std::string state = std::string(isUnread ? "● " : "") + (isWaiting ? "↩ " : "");
    TreeNodeData node(kRowPrefix + std::to_string(index), state + sender);
    node.textColor = text;
    node.tooltip   = sender + " <" + m.fromAddr + ">"
                   + (isUnread ? " — unread" : "") + (isWaiting ? " — waiting for reply" : "");
    node.SetCell(kColSubject, subject, text);
    node.SetCell(kColDate, FormatListDate(m.date), text);
    node.tooltip += "\n" + FormatShortDate(m.date);
    list_->AddNode(kRootId, node);
}

void MailView::UpdateInboxTitle() {
    if (!inboxBox_) return;
    std::string title = "Inbox";
    if (!messages_.empty()) {
        title += " — " + std::to_string(messages_.size()) + " message"
               + (messages_.size() == 1 ? "" : "s");
        if (shownUnread_ > 0) title += ", " + std::to_string(shownUnread_) + " unread";
    }
    inboxBox_->SetTitle(title);
}

void MailView::RebuildList() {
    if (!list_) return;
    messages_.clear();
    shownUnread_ = 0;
    preview_.Clear();

    // A hidden root whose children are the rows.
    TreeNodeData rootData(kRootId, "Inbox");
    list_->SetRootNode(rootData);

    if (!store_ || curAccount_.empty()) {
        if (inboxBox_) inboxBox_->SetTitle("Inbox");
        list_->ExpandAll();
        return;
    }

    store_->ListMessages(curAccount_, "INBOX", 0, messages_);
    std::vector<MessageEnvelope> waiting;
    store_->ListNeedsAnswer(curAccount_, waiting);
    std::set<int64_t> waitingUids;
    for (const auto& w : waiting) if (w.folder == "INBOX") waitingUids.insert(w.uid);

    for (std::size_t i = 0; i < messages_.size(); ++i)
        AddMessageRow(i, messages_[i], waitingUids);
    list_->ExpandAll();

    UpdateInboxTitle();

    // Preview the newest message by default.
    if (!messages_.empty()) {
        if (TreeNode* first = list_->FindNode(std::string(kRowPrefix) + "0"))
            list_->SelectNode(first);
        SelectRow(0);
    }
}

void MailView::AppendMessages(const std::string& accountId,
                              const std::vector<MessageEnvelope>& batch) {
    // Only the currently-shown account's rows belong in this list. The root is
    // already expanded (RebuildList ran when the account was shown), so appended
    // children just need a redraw — no ExpandAll, which would be O(n) per batch.
    std::fprintf(stderr, "[UMSTREAM] AppendMessages account=%s cur=%s batch=%zu list=%p -> %s\n",
                 accountId.c_str(), curAccount_.c_str(), batch.size(), (void*)list_.get(),
                 (!list_ || batch.empty() || accountId != curAccount_) ? "SKIP" : "APPEND");
    if (!list_ || batch.empty() || accountId != curAccount_) return;

    // Needs-answer glyphs (↩) are filled in by the final RebuildList; freshly
    // arrived mail is essentially never already awaiting a reply, so stream with
    // an empty set to keep the per-row cost off the store.
    static const std::set<int64_t> kNoWaiting;
    for (const auto& m : batch) {
        messages_.push_back(m);
        AddMessageRow(messages_.size() - 1, m, kNoWaiting);
    }
    UpdateInboxTitle();
    // Same finalizer RebuildList uses to make freshly added rows appear: it
    // recomputes scroll geometry and requests the repaint. (A bare RequestRedraw
    // can leave the new rows unrendered if the view hasn't refreshed its layout.)
    list_->ExpandAll();
}

void MailView::SelectRow(int row) {
    if (row < 0 || row >= static_cast<int>(messages_.size())) return;
    // Pin the list pane to its current width before the preview rebuilds its
    // body: that content change triggers a relayout which would otherwise let
    // the weight-based divider drift. A fixed pane keeps its width through
    // relayout and window resize, and a manual splitter drag updates the fixed
    // size — so the divider only moves when the user drags it. (Width is 0
    // before the first layout; skip until then.)
    if (split_ && split_->PaneCount() >= 1) {
        if (auto pane = split_->GetPane(0)) {
            int listW = static_cast<int>(pane->GetWidth());
            if (listW > 0) split_->SetPaneFixedSize(0, listW);
        }
    }
    preview_.Show(messages_[static_cast<std::size_t>(row)]);
}

} // namespace UltraMail
