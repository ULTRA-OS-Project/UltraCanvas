// Apps/UltraMail/ui/UltraMailMailView.cpp
// Version: 0.5.0 - a sender-badge column left of Subject, painted by the list
//                  delegate; the folder's stored scan verdicts are read once
//                  per list, and a row re-badges when the pane scans its body.
// Version: 0.4.0 - folder sidebar (per-account roots), UltraCanvasListView
//                  message list with a read/unread colour delegate, and a
//                  reading-pane toggle (side-by-side, or Gmail open-in-place).
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailMailView.h"

#include "UltraMailTheme.h"

#include <UltraNet/UltraNetMime.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr int kFromWidth    = 160;
constexpr int kBadgeWidth   = 26;   // the sender badge column, left of Subject
constexpr int kSubjectMin   = 140;
constexpr int kDateWidth    = 88;
constexpr int kRowHeight    = 22;
constexpr int kHeaderHeight = 22;
constexpr int kSplitterGap  = 8;   // the page shows through between the cards

constexpr int kFolderMinWidth  = 180;
constexpr int kListMinWidth    = 300;
constexpr int kPreviewMinWidth = 466;

const char* kFolderNodePrefix  = "f::";
const char* kAccountNodePrefix  = "acct::";
const char* kTreeRootId         = "mailboxesRoot";

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

// The name a mailbox shows under an account: "INBOX" reads as "Inbox", any
// other folder as the leaf of its IMAP path (the parents are separate rows).
std::string FriendlyLeaf(const std::string& segment, const std::string& fullPath) {
    if (fullPath == "INBOX") return "Inbox";
    // Mailbox names arrive in IMAP modified UTF-7; decode for display only (the
    // raw name stays the wire/DB key — see folderNodeId_ / curFolder_).
    return UltraNet_ImapUtf7Decode(segment);
}

// A stable ordering for a folder list: the inbox first, then the special-use
// roles in a familiar order, then everything else by name.
int RoleRank(FolderRole role) {
    switch (role) {
        case FolderRole::Inbox:   return 0;
        case FolderRole::Sent:    return 1;
        case FolderRole::Drafts:  return 2;
        case FolderRole::Archive: return 3;
        case FolderRole::Junk:    return 4;
        case FolderRole::Trash:   return 5;
        case FolderRole::Normal:  return 6;
    }
    return 6;
}

// Fill a container (group box or split pane) with one child that takes the
// whole content area.
void FillWith(const std::shared_ptr<UltraCanvasContainer>& host,
              const std::shared_ptr<UltraCanvasUIElement>& child) {
    host->layout.SetFlexColumn()
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    host->AddChild(child);
    child->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

// The invisible-gap splitter used between the cards: they read as separate
// surfaces on the page rather than as halves of one box.
SplitPaneStyle GapSplitterStyle() {
    SplitPaneStyle s;
    s.splitterThickness      = kSplitterGap;
    s.showSplitterBackground = false;
    s.splitterColor          = Theme::kPageBackground;
    s.splitterHoverColor     = Theme::kCardBorder;
    s.splitterActiveColor    = Theme::kAccent;
    return s;
}

// ---------------------------------------------------------------------------
// The message list, an UltraCanvasListView subclass that keeps its middle
// (Subject) column filling the free width — the base view lays columns out at
// their fixed widths, so without this the subject would truncate early and
// leave the date floating in empty space.
// ---------------------------------------------------------------------------
class MessageListView : public UltraCanvasListView {
public:
    explicit MessageListView(const std::string& id) : UltraCanvasListView(id) {}

    std::shared_ptr<UltraCanvasMultiColumnListModel> model;

    void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override {
        UltraCanvasListView::Arrange(finalRect, ctx);
        FitColumns();
    }
    void SetBounds(const Rect2Df& bounds) override {
        UltraCanvasListView::SetBounds(bounds);
        FitColumns();
    }

private:
    void FitColumns() {
        if (!model || model->GetColumnCount() < 4) return;
        if (ColumnsUserAdjusted()) return;   // once dragged, keep the user's widths
        const int w = static_cast<int>(GetWidth());
        if (w <= 0) return;
        // Subject fills the width left by From/badge/Date (their effective
        // widths), leaving room for the vertical scrollbar. Uses the per-view
        // override so it composes with interactive resize instead of rewriting
        // the model.
        int subj = w - GetColumnWidth(0) - GetColumnWidth(1) - GetColumnWidth(3) - 20;
        if (subj < kSubjectMin) subj = kSubjectMin;
        if (GetColumnWidth(2) == subj) return;   // no change: no churn
        SetColumnWidth(2, subj);
    }
};

// ---------------------------------------------------------------------------
// The row delegate: draws each cell's text in the read/unread colour (the
// glyphs — ● unread, ↩ waiting for a reply — are already in the From cell's
// text), and the sender badge in the badge column. Selection/hover backgrounds
// are painted by the view from its style. It reads MailView::rowStates_ and
// rowBadges_ through pointers to those (stable) vectors.
// ---------------------------------------------------------------------------
class MessageColorDelegate : public IItemDelegate {
public:
    const std::vector<MailRowState>* states = nullptr;
    const std::vector<SenderBadge>*  badges = nullptr;
    int   badgeColumn = -1;
    float badgeSide   = 18.0f;
    Color unreadColor;
    Color readColor;
    float fontSize   = 9.0f;
    int   rowHeight  = 22;
    int   textPadding = 6;

    void RenderItem(IRenderContext* ctx, const IListModel* model,
                    int row, int column,
                    const ListItemStyleOption& option) override {
        if (!ctx || !model) return;

        // The badge column carries no text: it is the sender badge, centred in
        // its cell (see UltraMailSenderBadge.h for what the colours mean).
        if (column == badgeColumn) {
            if (!badges || row < 0 || row >= static_cast<int>(badges->size())) return;
            const double side = badgeSide < option.rect.height ? badgeSide
                                                               : option.rect.height - 2;
            if (side <= 0) return;
            DrawSenderBadge(ctx, Rect2Dd(option.columnX + (option.columnWidth - side) / 2.0,
                                         option.rect.y + (option.rect.height - side) / 2.0,
                                         side, side),
                            (*badges)[row]);
            return;
        }

        ListIndex idx{row, column};
        std::string text = GetStringValue(model->GetData(idx, ListDataRole::DisplayRole));
        if (text.empty()) return;

        const int textX  = option.columnX + textPadding;
        const int availW = option.columnWidth - textPadding * 2;
        if (availW <= 0) return;

        const bool unread = states && row >= 0 && row < static_cast<int>(states->size()) &&
                            (*states)[row].unread;

        ctx->SetFontSize(fontSize);
        ctx->SetTextWrap(TextWrap::WrapNone);
        ctx->SetTextAlignment(option.columnAlignment);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(unread ? unreadColor : readColor);
        ctx->DrawTextInRect(text, Rect2Dd(textX, option.rect.y, availW, option.rect.height));
    }

    int GetRowHeight(const IListModel*, int) const override { return rowHeight; }
};

} // namespace

std::shared_ptr<UltraCanvasContainer> MailView::Build() {
    root_ = CreateContainer("mailView", 0, 0, 0, 0);
    root_->layout.SetFlexColumn()
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    outerSplit_ = std::make_shared<UltraCanvasSplitPane>("mailOuterSplit", 0, 0, 0, 0,
                                                         SplitOrientation::Horizontal);
    auto folderPane  = outerSplit_->AddPane(0.6);
    auto contentPane = outerSplit_->AddPane(3.0);
    outerSplit_->SetPaneMinSize(0, kFolderMinWidth);
    outerSplit_->SetPaneMinSize(1, kListMinWidth + kPreviewMinWidth);
    outerSplit_->SetSplitPaneStyle(GapSplitterStyle());

    // Left: the folder tree (one email root per account, mailboxes beneath).
    folderBox_ = CreateGroupBox("folderBox", 0, 0, 0, 0, "Folders");
    folderBox_->SetFrameStyle(GroupBoxFrameStyle::Header);
    folderBox_->SetVisualStyle(Theme::CardGroupBox(6.0f));
    folderTree_ = std::make_shared<UltraCanvasTreeView>("folderTree", 0, 0, 0, 0);
    folderTree_->SetRootVisible(false);
    folderTree_->SetShowExpandButtons(true);
    folderTree_->SetShowRootLines(false);
    folderTree_->SetRowHeight(22);
    folderTree_->SetIndentSize(14);
    folderTree_->SetFontSize(Theme::kSizeBody);
    folderTree_->SetSelectionMode(TreeSelectionMode::Single);
    folderTree_->SetBackgroundColor(Theme::kCardBackground);
    folderTree_->SetSelectionColor(Theme::kRowSelected);
    folderTree_->SetHoverColor(Theme::kRowHover);
    folderTree_->SetTextColor(Theme::kTextPrimary);
    folderTree_->onNodeSelected = [this](TreeNode* node) {
        if (suppressTreeCallback_ || !node) return;
        auto it = folderNodeId_.find(node->data.nodeId);
        if (it == folderNodeId_.end()) return;
        const std::string acct   = it->second.first;
        const std::string folder = it->second.second;
        if (acct != curAccount_ && onSelectAccount) onSelectAccount(acct);
        ShowFolder(acct, folder);
    };
    FillWith(folderBox_, folderTree_);
    FillWith(folderPane, folderBox_);

    // Right: the content host (list | preview, or list with open-in-place).
    contentHost_ = CreateContainer("mailContent", 0, 0, 0, 0);
    contentHost_->layout.SetFlexColumn()
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    FillWith(contentPane, contentHost_);
    ApplyContentLayout();

    root_->AddChild(outerSplit_);
    outerSplit_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return root_;
}

void MailView::BuildListBox() {
    listBox_ = CreateGroupBox("listBox", 0, 0, 0, 0, "Inbox");
    listBox_->SetFrameStyle(GroupBoxFrameStyle::Header);
    listBox_->SetVisualStyle(Theme::CardGroupBox(6.0f));

    model_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    model_->SetColumns({
        ListColumnDef("From",    kFromWidth,  TextAlignment::Left),
        // The sender badge, immediately left of the subject: who the message is
        // from, before a word of it is read. Titled with a bullet rather than a
        // word so the 26px column keeps its header readable.
        ListColumnDef("\xE2\x97\x8F", kBadgeWidth, TextAlignment::Center,
                      "Sender: green = contact, blue = business contact, black = new, "
                      "dark blue = advertisement, orange = spam, red = likely scam"),
        ListColumnDef("Subject", kSubjectMin, TextAlignment::Left),
        ListColumnDef("Date",    kDateWidth,  TextAlignment::Left),
    });

    auto lv = std::make_shared<MessageListView>("messageList");
    lv->model = model_;
    list_ = lv;
    list_->SetModel(model_);
    list_->SetSelection(std::make_shared<UltraCanvasSingleSelection>());

    ListViewStyle st;
    st.backgroundColor          = Theme::kCardBackground;
    st.showHeader               = true;
    st.headerHeight             = kHeaderHeight;
    st.headerBackgroundColor    = Theme::kSidebar;
    st.headerTextColor          = Theme::kTextSecondary;
    st.headerFontSize           = Theme::kSizeSecondary;
    st.rowHeight                = kRowHeight;
    st.showGridLines            = false;
    st.selectionBackgroundColor = Theme::kRowSelected;
    st.hoverBackgroundColor     = Theme::kRowHover;
    list_->SetStyle(st);

    auto d = std::make_shared<MessageColorDelegate>();
    d->states      = &rowStates_;
    d->badges      = &rowBadges_;
    d->badgeColumn = 1;
    d->badgeSide   = Theme::kBadgeSize;
    d->unreadColor = Theme::kTextPrimary;
    d->readColor   = Theme::kTextSecondary;
    d->fontSize    = Theme::kSizeBody;
    d->rowHeight   = kRowHeight;
    delegate_ = d;
    list_->SetDelegate(delegate_);

    list_->onSelectionChanged = [this](const std::vector<int>& rows) {
        if (!rows.empty()) SelectRow(rows.front());
    };
    list_->onItemClicked = [this](int row) { SelectRow(row); };

    FillWith(listBox_, list_);
}

void MailView::BuildMessageBox() {
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
    preview_.onForward = [this](const SourceMessage& src, const std::string& n,
                                const std::string& a) {
        if (onForward) onForward(src, n, a);
    };
    preview_.onDelete = [this](const MessageEnvelope& e) {
        if (onDelete) onDelete(e);
    };
    preview_.onJunk = [this](const MessageEnvelope& e) {
        if (onJunk) onJunk(e);
    };
    preview_.onMarkUnread = [this](const MessageEnvelope& e) {
        if (onMarkUnread) onMarkUnread(e);
    };
    preview_.onViewSource = [this](const std::string& subject, const std::string& raw) {
        if (onViewSource) onViewSource(subject, raw);
    };
    // A body read for the first time is also scanned for the first time: the
    // row's badge stops being "unscanned" the moment the pane knows better.
    preview_.onSecurityScanned = [this](const MessageEnvelope& m, const MessageSecurity& s) {
        RefreshRowBadge(m, s);
    };
    FillWith(messageBox_, preview_.Build());
}

std::shared_ptr<UltraCanvasContainer> MailView::BuildBackBar() {
    auto bar = CreateContainer("mailBackBar", 0, 0, 0, static_cast<int>(Theme::kControlHeight));
    bar->layout.SetFlexRow()
              .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto back = CreateButton("mailBack", 0, 0, 150, Theme::kControlHeight, "\xE2\x86\x90 Back to list");
    Theme::StyleSecondary(back);
    back->onClick = [this]() { ShowListInPlace(); };
    bar->AddChild(back);
    return bar;
}

void MailView::ApplyContentLayout() {
    if (!contentHost_) return;
    contentHost_->ClearChildren();
    innerSplit_.reset();
    backBar_.reset();

    BuildListBox();
    BuildMessageBox();

    if (readingPane_) {
        innerSplit_ = std::make_shared<UltraCanvasSplitPane>("mailInnerSplit", 0, 0, 0, 0,
                                                            SplitOrientation::Horizontal);
        auto lp = innerSplit_->AddPane(1.15);
        auto pp = innerSplit_->AddPane(1.0);
        innerSplit_->SetPaneMinSize(0, kListMinWidth);
        innerSplit_->SetPaneMinSize(1, kPreviewMinWidth);
        innerSplit_->SetSplitPaneStyle(GapSplitterStyle());
        FillWith(lp, listBox_);
        FillWith(pp, messageBox_);
        contentHost_->AddChild(innerSplit_);
        innerSplit_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    } else {
        // Gmail: a back bar, then the list and the message stacked, one shown at
        // a time. The list is up by default; opening a message swaps them.
        backBar_ = BuildBackBar();
        contentHost_->AddChild(backBar_);
        backBar_->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        backBar_->SetVisible(false);

        contentHost_->AddChild(listBox_);
        listBox_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        contentHost_->AddChild(messageBox_);
        messageBox_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        messageBox_->SetVisible(false);
    }
}

void MailView::OpenMessageInPlace() {
    if (readingPane_) return;
    if (listBox_)    listBox_->SetVisible(false);
    if (backBar_)    backBar_->SetVisible(true);
    if (messageBox_) messageBox_->SetVisible(true);
}

void MailView::ShowListInPlace() {
    if (readingPane_) return;
    if (backBar_)    backBar_->SetVisible(false);
    if (messageBox_) messageBox_->SetVisible(false);
    if (listBox_)    listBox_->SetVisible(true);
}

void MailView::SetReadingPane(bool on) {
    if (on == readingPane_) return;
    readingPane_ = on;
    ApplyContentLayout();
    RebuildList();
}

void MailView::SetAccounts(std::vector<Account> accounts) {
    accounts_ = accounts;
    preview_.SetAccounts(std::move(accounts));
}

void MailView::SetContacts(ContactIndex contacts) {
    badges_.SetContacts(contacts);
    preview_.SetContacts(std::move(contacts));
}

void MailView::SetIconCache(const SenderIconCache* cache) {
    badges_.SetIconCache(cache);
    preview_.SetIconCache(cache);
}

bool MailView::CurrentFolderIsJunk() const {
    if (!store_ || curAccount_.empty()) return false;
    std::vector<Folder> folders;
    store_->ListFolders(curAccount_, folders);
    for (const auto& f : folders)
        if (f.name == curFolder_) return f.role == FolderRole::Junk;
    return false;
}

void MailView::RefreshRowBadge(const MessageEnvelope& message,
                               const MessageSecurity& security) {
    if (!model_ || message.accountId != curAccount_ || message.folder != curFolder_) return;
    security_[message.uid] = security;
    for (std::size_t row = 0; row < messages_.size(); ++row) {
        if (messages_[row].uid != message.uid) continue;
        if (row >= rowBadges_.size()) break;
        rowBadges_[row] = BadgeFor(messages_[row]);
        // Writing the cell tooltip also notifies the view, which redraws the row.
        model_->SetData(ListIndex{static_cast<int>(row), 1}, ListDataRole::ToolTipRole,
                        rowBadges_[row].tooltip);
        break;
    }
}

SenderBadge MailView::BadgeFor(const MessageEnvelope& m) const {
    MessageSecurity sec;
    const auto it = security_.find(m.uid);
    if (it != security_.end()) sec = it->second;
    return badges_.Resolve(m, sec, curFolderIsJunk_);
}

void MailView::RebuildFolderTree() {
    if (!folderTree_) return;
    folderNodeId_.clear();

    TreeNodeData rootData(kTreeRootId, "Mailboxes");
    folderTree_->SetRootNode(rootData);

    for (const auto& account : accounts_) {
        const std::string accId  = account.accountId;
        const std::string accNode = kAccountNodePrefix + accId;
        TreeNodeData accData(accNode, account.email.empty() ? account.displayName
                                                            : account.email);
        accData.textColor = Theme::kTextPrimary;
        folderTree_->AddNode(kTreeRootId, accData);
        // Clicking the account row opens its inbox.
        folderNodeId_[accNode] = {accId, "INBOX"};

        std::vector<Folder> folders;
        if (store_) store_->ListFolders(accId, folders);
        std::stable_sort(folders.begin(), folders.end(),
                         [](const Folder& a, const Folder& b) {
                             int ra = RoleRank(a.role), rb = RoleRank(b.role);
                             if (ra != rb) return ra < rb;
                             return a.name < b.name;
                         });

        // Inbox is the parent of every other mailbox (Thunderbird-style): create
        // its node under the account and use it as the base parent below. It is
        // created even if the folder list has no INBOX row yet.
        const std::string inboxNode = kFolderNodePrefix + accId + "::INBOX";
        TreeNodeData inboxData(inboxNode, "Inbox");
        inboxData.textColor = Theme::kTextPrimary;
        folderTree_->AddNode(accNode, inboxData);
        folderNodeId_[inboxNode] = {accId, "INBOX"};

        // Which stored paths are non-selectable containers (e.g. "[Gmail]"),
        // to elide from the tree.
        std::map<std::string, bool> selectable;
        for (const auto& f : folders) selectable[f.name] = f.selectable;

        // Track which path nodes exist so a hierarchy shares parents instead of
        // adding a row per segment per folder. INBOX is already the base node.
        std::set<std::string> created{ inboxNode };
        for (const auto& folder : folders) {
            if (folder.name == "INBOX") continue;   // the base node above
            std::string parent = inboxNode;         // Inbox is the parent
            std::string path;
            std::size_t start = 0;
            while (start <= folder.name.size()) {
                std::size_t slash = folder.name.find('/', start);
                std::string segment = folder.name.substr(
                    start, slash == std::string::npos ? std::string::npos : slash - start);
                if (!path.empty()) path += "/";
                path += segment;
                // Elide a stored, non-selectable container: create no node and
                // leave `parent` unchanged so its children attach to it.
                auto sel = selectable.find(path);
                const bool isContainer = (sel != selectable.end() && !sel->second);
                if (!isContainer) {
                    const std::string nodeId = kFolderNodePrefix + accId + "::" + path;
                    if (created.insert(nodeId).second) {
                        TreeNodeData data(nodeId, FriendlyLeaf(segment, path));
                        data.textColor = Theme::kTextPrimary;
                        folderTree_->AddNode(parent, data);
                        folderNodeId_[nodeId] = {accId, path};
                    }
                    parent = nodeId;
                }
                if (slash == std::string::npos) break;
                start = slash + 1;
            }
        }
    }

    folderTree_->ExpandAll();
    SelectFolderNode(curAccount_, curFolder_);
}

void MailView::SelectFolderNode(const std::string& accountId, const std::string& folder) {
    if (!folderTree_) return;
    const std::string nodeId = kFolderNodePrefix + accountId + "::" + folder;
    TreeNode* node = folderTree_->FindNode(nodeId);
    if (!node) node = folderTree_->FindNode(kAccountNodePrefix + accountId);
    if (!node) return;
    suppressTreeCallback_ = true;
    folderTree_->SelectNode(node);
    suppressTreeCallback_ = false;
}

void MailView::ShowAccount(const std::string& accountId) {
    const bool sameAccount = (accountId == curAccount_);
    curAccount_ = accountId;
    if (!sameAccount) curFolder_ = "INBOX";
    RebuildFolderTree();
    RebuildList();
}

void MailView::ShowFolder(const std::string& accountId, const std::string& folder) {
    curAccount_ = accountId;
    curFolder_  = folder;
    SelectFolderNode(accountId, folder);
    // A user folder switch: in the reading pane the auto-shown top message is
    // being read, so mark it read (Gmail/Thunderbird style).
    RebuildList(/*markTopRead=*/true);
    if (onOpenFolder) onOpenFolder(accountId, folder);
}

void MailView::Reload() {
    RebuildList();
}

void MailView::BuildMessageRow(const MessageEnvelope& m, const std::set<int64_t>& waitingUids,
                               MultiColumnListItem& outItem, MailRowState& outState,
                               SenderBadge& outBadge) const {
    const bool isUnread  = (m.flags & Flag_Seen) == 0;
    const bool isWaiting = waitingUids.count(m.uid) > 0;

    // Decode defensively: messages synced before header decoding are still
    // stored raw. Decoding already-decoded text is a no-op.
    std::string sender  = UltraNet_MimeDecodeHeader(
        m.fromName.empty() ? m.fromAddr : m.fromName);
    std::string subject = m.subject.empty()
        ? std::string("(no subject)") : UltraNet_MimeDecodeHeader(m.subject);

    // State glyphs in front of the sender: ● unread, ↩ waiting for a reply.
    std::string state = std::string(isUnread ? "\xE2\x97\x8F " : "")
                      + (isWaiting ? "\xE2\x86\xA9 " : "");
    outBadge = BadgeFor(m);
    outItem = MultiColumnListItem({ state + sender, "", subject, FormatListDate(m.date) });
    outItem.tooltip = sender + " <" + m.fromAddr + ">"
                 + (isUnread ? " — unread" : "") + (isWaiting ? " — waiting for reply" : "")
                 + "\n" + FormatShortDate(m.date);
    // The badge cell explains itself rather than repeating the row tooltip:
    // what the sender is, and — when the content scan found something — why.
    outItem.SetCellTooltip(1, outBadge.tooltip);
    outState = { isUnread, isWaiting };
}

void MailView::AddMessageRow(const MessageEnvelope& m,
                             const std::set<int64_t>& waitingUids) {
    MultiColumnListItem item; MailRowState st; SenderBadge badge;
    BuildMessageRow(m, waitingUids, item, st, badge);
    if (st.unread) ++shownUnread_;
    model_->AddItem(item);
    rowStates_.push_back(st);
    rowBadges_.push_back(badge);
}

int MailView::SortedInsertPos(int64_t date) const {
    // The list is date-DESC (matching the store's ORDER BY date DESC), so a
    // message belongs before the first row older than it.
    for (std::size_t i = 0; i < messages_.size(); ++i)
        if (messages_[i].date < date) return static_cast<int>(i);
    return static_cast<int>(messages_.size());
}

void MailView::InsertMessageRowAt(int row, const MessageEnvelope& m,
                                  const std::set<int64_t>& waitingUids) {
    if (row < 0) row = 0;
    if (row > static_cast<int>(messages_.size())) row = static_cast<int>(messages_.size());
    MultiColumnListItem item; MailRowState st; SenderBadge badge;
    BuildMessageRow(m, waitingUids, item, st, badge);
    if (st.unread) ++shownUnread_;
    messages_.insert(messages_.begin() + row, m);
    rowStates_.insert(rowStates_.begin() + row, st);
    rowBadges_.insert(rowBadges_.begin() + row, badge);
    if (model_) model_->InsertItem(row, item);
}

void MailView::RemoveRowByUid(int64_t uid) {
    for (std::size_t row = 0; row < messages_.size(); ++row) {
        if (messages_[row].uid != uid) continue;
        if (row < rowStates_.size() && rowStates_[row].unread && shownUnread_ > 0) --shownUnread_;
        if (model_) model_->RemoveItem(static_cast<int>(row));
        messages_.erase(messages_.begin() + row);
        if (row < rowStates_.size()) rowStates_.erase(rowStates_.begin() + row);
        if (row < rowBadges_.size()) rowBadges_.erase(rowBadges_.begin() + row);
        security_.erase(uid);
        return;
    }
}

void MailView::RefreshRowText(int row) {
    if (row < 0 || row >= static_cast<int>(messages_.size()) ||
        row >= static_cast<int>(rowStates_.size()))
        return;
    std::string sender = UltraNet_MimeDecodeHeader(
        messages_[row].fromName.empty() ? messages_[row].fromAddr : messages_[row].fromName);
    std::string state = std::string(rowStates_[row].unread ? "\xE2\x97\x8F " : "")
                      + (rowStates_[row].waiting ? "\xE2\x86\xA9 " : "");
    if (model_)
        model_->SetData(ListIndex{row, 0}, ListDataRole::DisplayRole, state + sender);
}

void MailView::UpdateRowFlags(int row, uint32_t newFlags) {
    if (row < 0 || row >= static_cast<int>(messages_.size()) ||
        row >= static_cast<int>(rowStates_.size()))
        return;
    const bool wasUnread = rowStates_[row].unread;
    const bool nowUnread = (newFlags & Flag_Seen) == 0;
    messages_[row].flags = newFlags;
    if (wasUnread == nowUnread) return;   // only the read glyph/colour depends on flags
    rowStates_[row].unread = nowUnread;
    shownUnread_ += nowUnread ? 1 : -1;
    if (shownUnread_ < 0) shownUnread_ = 0;
    RefreshRowText(row);                  // delegate re-colours from rowStates_ on redraw
}

void MailView::MarkRead(const std::string& accountId, const std::string& folder,
                        int64_t uid) {
    if (accountId != curAccount_ || folder != curFolder_) return;
    for (std::size_t row = 0; row < messages_.size(); ++row) {
        if (messages_[row].uid != uid) continue;
        MarkRowRead(static_cast<int>(row));
        break;
    }
}

void MailView::MarkRowRead(int row) {
    if (row < 0 || row >= static_cast<int>(messages_.size())) return;
    UpdateRowFlags(row, messages_[row].flags | Flag_Seen);   // drop the ●, dim the row
    UpdateListTitle();
}

void MailView::UpdateListTitle() {
    if (!listBox_) return;
    std::string title = FriendlyLeaf(curFolder_, curFolder_);
    if (!messages_.empty()) {
        title += " — " + std::to_string(messages_.size()) + " message"
               + (messages_.size() == 1 ? "" : "s");
        if (shownUnread_ > 0) title += ", " + std::to_string(shownUnread_) + " unread";
    }
    listBox_->SetTitle(title);
}

void MailView::RebuildList(bool markTopRead) {
    if (!list_ || !model_) return;
    // Refreshing the folder already on screen (after a sync) reconciles the rows
    // in place — no flicker, selection and scroll kept. A folder/account switch,
    // or a list that was cleared, still does a full clear+build.
    const bool sameView = store_ && !curAccount_.empty()
                       && curAccount_ == loadedAccount_ && curFolder_ == loadedFolder_
                       && !messages_.empty();
    if (sameView) { DiffListFromStore(markTopRead); return; }
    FullRebuild(markTopRead);
}

void MailView::FullRebuild(bool markTopRead) {
    if (!list_ || !model_) return;
    // Remember the shown message so a refresh keeps it selected instead of
    // snapping to the newest row (captured before the vectors are cleared).
    const int64_t     keepUid    = selectedUid_;
    const std::string keepFolder = selectedFolder_;
    messages_.clear();
    rowStates_.clear();
    rowBadges_.clear();
    security_.clear();
    shownUnread_ = 0;
    model_->Clear();
    list_->ResetSelection();
    preview_.Clear();

    if (!store_ || curAccount_.empty()) {
        selectedUid_ = -1;
        loadedAccount_.clear();
        loadedFolder_.clear();
        UpdateListTitle();
        if (!readingPane_) ShowListInPlace();
        return;
    }

    // The badge's two inputs that come from the store: whether this folder is
    // the junk mailbox, and the content-scan verdicts of the messages in it.
    curFolderIsJunk_ = CurrentFolderIsJunk();
    preview_.SetJunkFolder(curFolderIsJunk_);
    store_->ListSecurity(curAccount_, curFolder_, security_);

    store_->ListMessages(curAccount_, curFolder_, 0, messages_);
    std::vector<MessageEnvelope> waiting;
    store_->ListNeedsAnswer(curAccount_, waiting);
    std::set<int64_t> waitingUids;
    for (const auto& w : waiting) if (w.folder == curFolder_) waitingUids.insert(w.uid);

    for (const auto& m : messages_) AddMessageRow(m, waitingUids);

    UpdateListTitle();

    // Default: the list is up; preview the newest message only when the reading
    // pane is on (Gmail mode waits for a click before hiding the list).
    if (!readingPane_) ShowListInPlace();
    if (!messages_.empty()) {
        // Keep the previously-shown message selected across a refresh of the same
        // folder; fall back to the newest row on a folder/account switch (where
        // the kept uid belonged to a different folder) or if it was expunged.
        int restore = 0;
        if (keepUid >= 0 && keepFolder == curFolder_)
            for (std::size_t i = 0; i < messages_.size(); ++i)
                if (messages_[i].uid == keepUid) { restore = static_cast<int>(i); break; }

        // Selecting a row fires onSelectionChanged synchronously → SelectRow();
        // suppress its mark-read so a background rebuild never marks unseen mail
        // read. The reading-pane preview below then opts in explicitly when this
        // rebuild was a user folder switch (markTopRead, always restore==0).
        suppressAutoRead_ = true;
        if (auto sel = list_->GetSelection()) sel->Select(restore);
        list_->EnsureRowVisible(restore);
        suppressAutoRead_ = false;
        if (readingPane_) SelectRowImpl(restore, /*markRead=*/markTopRead);
        else              preview_.Show(messages_[static_cast<std::size_t>(restore)]);
    }

    loadedAccount_ = curAccount_;   // what the rows now represent (for the diff path)
    loadedFolder_  = curFolder_;
}

void MailView::DiffListFromStore(bool /*markTopRead*/) {
    // The store already holds the authoritative post-sync state (SyncMessages
    // upserted new mail, ReconcileFlags removed deleted UIDs and corrected flags).
    // Reconcile the visible rows to it in place, keeping selection and scroll.
    std::vector<MessageEnvelope> fresh;
    store_->ListMessages(curAccount_, curFolder_, 0, fresh);

    // Refresh the badge inputs (scan verdicts + the needs-answer ↩ set) as the
    // full rebuild does; existing rows' badges do not change during a sync.
    curFolderIsJunk_ = CurrentFolderIsJunk();
    preview_.SetJunkFolder(curFolderIsJunk_);
    security_.clear();
    store_->ListSecurity(curAccount_, curFolder_, security_);
    std::vector<MessageEnvelope> waiting;
    store_->ListNeedsAnswer(curAccount_, waiting);
    std::set<int64_t> waitingUids;
    for (const auto& w : waiting) if (w.folder == curFolder_) waitingUids.insert(w.uid);

    // Measure the turnover; a near-total change (e.g. a UIDVALIDITY renumber) is
    // cheaper and cleaner as a full rebuild — which is what the user asked for.
    std::unordered_set<int64_t> freshUids, curUids;
    freshUids.reserve(fresh.size());
    curUids.reserve(messages_.size());
    for (const auto& m : fresh)     freshUids.insert(m.uid);
    for (const auto& m : messages_) curUids.insert(m.uid);
    std::size_t removed = 0, added = 0;
    for (const auto& m : messages_) if (!freshUids.count(m.uid)) ++removed;
    for (const auto& m : fresh)     if (!curUids.count(m.uid))   ++added;
    const std::size_t maxN = std::max(messages_.size(), fresh.size());
    if (maxN == 0 || (removed + added) * 2 > maxN) { FullRebuild(false); return; }

    // 1) Drop rows the server no longer lists (snapshot uids first — we mutate).
    std::vector<int64_t> curOrder;
    curOrder.reserve(messages_.size());
    for (const auto& m : messages_) curOrder.push_back(m.uid);
    for (int64_t uid : curOrder) if (!freshUids.count(uid)) RemoveRowByUid(uid);

    // 2) Walk the fresh (sorted) list; the surviving rows are a subsequence of it,
    //    so insert any missing message at the current position and update flags
    //    on the ones that stayed. This reproduces the store's order exactly.
    std::size_t pos = 0;
    for (const auto& f : fresh) {
        if (pos < messages_.size() && messages_[pos].uid == f.uid) {
            if (messages_[pos].flags != f.flags) UpdateRowFlags(static_cast<int>(pos), f.flags);
            ++pos;
        } else {
            InsertMessageRowAt(static_cast<int>(pos), f, waitingUids);
            ++pos;
        }
    }

    UpdateListTitle();

    // Keep the selection stable. Inserts above it shift its row index but the
    // model does not renumber the selection, so re-assert it by uid (suppressing
    // mark-read — this is a background refresh). If it was deleted, clear the pane.
    if (selectedUid_ >= 0 && selectedFolder_ == curFolder_) {
        int selRow = -1;
        for (std::size_t i = 0; i < messages_.size(); ++i)
            if (messages_[i].uid == selectedUid_) { selRow = static_cast<int>(i); break; }
        if (selRow >= 0) {
            suppressAutoRead_ = true;
            if (auto sel = list_->GetSelection()) sel->Select(selRow);
            list_->EnsureRowVisible(selRow);
            suppressAutoRead_ = false;
        } else {
            preview_.Clear();
            selectedUid_ = -1;
        }
    }
}

void MailView::AppendMessages(const std::string& accountId,
                              const std::vector<MessageEnvelope>& batch) {
    if (!list_ || !model_ || batch.empty() || accountId != curAccount_) return;

    // Needs-answer glyphs (↩) are filled in by the final refresh; freshly arrived
    // mail is essentially never already awaiting a reply, so stream with an empty
    // set to keep the per-row cost off the store. Only rows for the folder on
    // screen belong in this list (each envelope carries its folder). New mail is
    // newest → insert at its sorted (date-DESC) position so the list stays ordered
    // as it streams in (rather than appending and re-sorting at the end).
    static const std::set<int64_t> kNoWaiting;
    bool added = false;
    for (const auto& m : batch) {
        if (m.folder != curFolder_) continue;
        bool dup = false;
        for (const auto& e : messages_) if (e.uid == m.uid) { dup = true; break; }
        if (dup) continue;
        InsertMessageRowAt(SortedInsertPos(m.date), m, kNoWaiting);
        added = true;
    }
    if (added) UpdateListTitle();   // model InsertItem already requested the redraw
}

void MailView::SelectRow(int row) {
    // A genuine click / arrow-key selection marks the message read; a
    // programmatic selection during RebuildList sets suppressAutoRead_ so only
    // the reading-pane auto-preview (via SelectRowImpl) decides for itself.
    SelectRowImpl(row, /*markRead=*/!suppressAutoRead_);
}

void MailView::SelectRowImpl(int row, bool markRead) {
    if (row < 0 || row >= static_cast<int>(messages_.size())) return;
    // Reading pane: pin the list pane to its current width before the preview
    // rebuilds its body, so the weight-based divider does not drift on the
    // relayout that follows. A manual splitter drag updates the fixed size — so
    // the divider only moves when the user drags it. (Width is 0 before the
    // first layout; skip until then.)
    if (readingPane_ && innerSplit_ && innerSplit_->PaneCount() >= 1) {
        if (auto pane = innerSplit_->GetPane(0)) {
            int listW = static_cast<int>(pane->GetWidth());
            if (listW > 0) innerSplit_->SetPaneFixedSize(0, listW);
        }
    }
    const MessageEnvelope& m = messages_[static_cast<std::size_t>(row)];
    // Remember what is shown so a rebuild after a sync can restore this selection
    // (by uid within the same folder) rather than snapping back to the newest row.
    selectedUid_    = m.uid;
    selectedFolder_ = m.folder;
    preview_.Show(m);
    if (!readingPane_) OpenMessageInPlace();
    // Opening an unread message reads it: hand the app the envelope so it can
    // set \Seen locally and on the server. MarkRowRead (driven back through
    // MarkRead) then updates this very row, so re-selecting it won't re-fire.
    if (markRead && onMarkRead && (m.flags & Flag_Seen) == 0) onMarkRead(m);
}

} // namespace UltraMail
