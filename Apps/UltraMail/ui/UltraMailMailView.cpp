// Apps/UltraMail/ui/UltraMailMailView.cpp
// Version: 0.17.0 - Always trust / Block this sender / Block everything from the
//                   domain in both menus; RescanSender (off the UI thread)
// Version: 0.16.0 - the reading pane's sender menu: copy the address, the
//                   sender's mail, the address book, spam (SenderMenuItems)
// Version: 0.15.0 - RecheckShownMessage: the message on screen scanned again when
//                   the scam warnings change
// Version: 0.14.2 - ShowPendingPreviewNow: the selected message in the window's
//                  first frame at start
// Version: 0.14.1 - the folder tree, the list's rebuild and the reading pane in
//                  the timing trace, step by step (UltraMailTrace.h)
// Version: 0.14.0 - OpenMessage
// Version: 0.13.0 - a row whose badge has no icon asks for it when painted
//                   (lazy sender icons); IconCached shows it when it arrives
// Version: 0.12.0 - folders by the server's own separator ("INBOX.Drafts" is
//                   Drafts under Inbox); a folder gone from the server falls
//                   back to the inbox
// Version: 0.11.0 - a click on a column header sorts the list by it (again: the
//                   other way round); rows go into the model in one go; the
//                   reading pane renders once per selection, after the list is
//                   painted, and not again when a sync adds mail above it
// Version: 0.10.0 - forwards onComposeTo
// Version: 0.9.0 - SetLinkTooltips
// Version: 0.8.0 - forwards the reading pane's links and hovered link
// Version: 0.7.0 - SetFolderTreeWidth: the folder tree fitted to its rows
//                  (+10 px) or a fixed width
// Version: 0.6.0 - SetBodyOptions; trusted picture hosts reach the preview
// Version: 0.5.0 - a sender-badge column left of Subject, painted by the list
//                  delegate; the folder's stored scan verdicts are read once
//                  per list, and a row re-badges when the pane scans its body.
// Version: 0.4.0 - folder sidebar (per-account roots), UltraCanvasListView
//                  message list with a read/unread colour delegate, and a
//                  reading-pane toggle (side-by-side, or Gmail open-in-place).
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailMailView.h"
#include "UltraMailHeaderText.h"

#include "UltraMailTheme.h"
#include "UltraMailSenderBrands.h"
#include "UltraMailFolderNames.h"
#include "UltraMailSyncEngine.h"   // CachedBodyPath
#include "UltraMailTrace.h"
#include "UltraCanvasApplication.h"   // PostToUIThread
#include "UltraCanvasClipboard.h"     // SetClipboardText
#include "UltraCanvasConfig.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8
#include "UltraCanvasImage.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasUtilsUtf8.h"

#include <UltraNet/UltraNetMime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <optional>
#include <set>
#include <iterator>
#include <thread>
#include <fstream>
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

// The folder pane's range, whether fitted to its rows or set in pixels.
constexpr int kFolderMinWidth  = 100;
constexpr int kFolderMaxWidth  = 600;
constexpr int kFolderFitSlack  = 10;   // fitted: this much room after the longest row
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
    // Columns from this one on (subject, date) are drawn bold for unread mail.
    int   boldFromColumn = 2;
    // The subject column, which ends in a paperclip for mail with attachments.
    int   subjectColumn = 2;
    std::string clipIcon;
    // Asked for the icon a painted badge lacks: only rows on screen are
    // painted, so only their senders' icons are fetched.
    std::function<void(const std::string& key)> wantIcon;
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
            const SenderBadge& badge = (*badges)[row];
            if (badge.iconPath.empty() && !badge.iconKey.empty() && wantIcon)
                wantIcon(badge.iconKey);
            DrawSenderBadge(ctx, Rect2Dd(option.columnX + (option.columnWidth - side) / 2.0,
                                         option.rect.y + (option.rect.height - side) / 2.0,
                                         side, side),
                            badge);
            return;
        }

        ListIndex idx{row, column};
        std::string text = GetStringValue(model->GetData(idx, ListDataRole::DisplayRole));
        if (text.empty()) return;

        const int textX  = option.columnX + textPadding;
        int availW = option.columnWidth - textPadding * 2;
        if (availW <= 0) return;

        const bool haveState = states && row >= 0 && row < static_cast<int>(states->size());
        const bool unread = haveState && (*states)[row].unread;

        // Attachments: a paperclip at the right end of the subject cell; the
        // subject text stops short of it.
        if (column == subjectColumn && haveState && (*states)[row].attachments > 0) {
            const double side = std::min(14.0, option.rect.height - 6.0);
            if (side > 4.0 && availW > side + 8) {
                if (auto clip = UCImage::Get(clipIcon)) {
                    ctx->DrawMask(unread ? unreadColor : readColor, *clip,
                                  Rect2Dd(option.columnX + option.columnWidth - textPadding - side,
                                          option.rect.y + (option.rect.height - side) / 2.0,
                                          side, side),
                                  ImageFitMode::Contain);
                }
                availW -= static_cast<int>(side) + 6;
            }
        }

        ctx->SetFontSize(fontSize);
        // Unread mail stands out: its subject and date are bold (the sender
        // already carries the ● glyph and the darker colour).
        ctx->SetFontWeight(unread && column >= boldFromColumn ? FontWeight::Bold
                                                              : FontWeight::Normal);
        ctx->SetTextWrap(TextWrap::WrapNone);
        ctx->SetTextAlignment(option.columnAlignment);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(unread ? unreadColor : readColor);
        ctx->DrawTextInRect(text, Rect2Dd(textX, option.rect.y, availW, option.rect.height));
        ctx->SetFontWeight(FontWeight::Normal);   // leave the context as found
    }

    int GetRowHeight(const IListModel*, int) const override { return rowHeight; }
};

// `text` on one line: every run of line breaks, tabs and other whitespace
// becomes a single space, and the ends are trimmed.
std::string SingleLine(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (unsigned char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) { out += ' '; pendingSpace = false; }
        out += static_cast<char>(c);
    }
    return out;
}

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
    // A fitted tree follows the rows on show.
    folderTree_->onNodeExpanded  = [this](TreeNode*) { if (folderTreeFitToText_) ApplyFolderTreeWidth(); };
    folderTree_->onNodeCollapsed = [this](TreeNode*) { if (folderTreeFitToText_) ApplyFolderTreeWidth(); };
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
    d->clipIcon    = NormalizePath(GetResourcesDir() + "media/icons/paperclip.svg");
    d->wantIcon    = [this](const std::string& key) { if (iconRequester_) iconRequester_(key); };
    delegate_ = d;
    list_->SetDelegate(delegate_);

    list_->onSelectionChanged = [this](const std::vector<int>& rows) {
        // A selection set by code is shown (or not) by the code that set it.
        if (programmaticSelection_ || rows.empty()) return;
        SelectRow(rows.front());
    };
    list_->onItemClicked = [this](int row) { SelectRow(row); };
    list_->onContextMenu = [this](int row, const UCEvent& event) { ShowRowMenu(row, event); };
    // A column header orders the list by that column; the same header again
    // turns the order round. Dates start newest first, the rest from A.
    list_->onHeaderClicked = [this](int column) {
        static const MessageSortKey kByColumn[] = {
            MessageSortKey::Sender, MessageSortKey::Kind, MessageSortKey::Subject,
            MessageSortKey::Date };
        if (column < 0 || column >= 4) return;
        sort_ = sort_.Clicked(kByColumn[column]);
        ShowSortIndicator();
        ApplySort();
        if (onSortChanged) onSortChanged(sort_);
    };
    ShowSortIndicator();

    // Search above the list, inside the same card.
    auto column = CreateContainer("messageListColumn", 0, 0, 0, 0);
    column->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kInnerGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    search_ = CreateTextInput("messageSearch", 0, 0, 0, Theme::kControlHeight);
    search_->SetPlaceholder("Search sender or subject");
    search_->SetShowClearButton(true);
    Theme::StyleInput(search_);
    search_->SetText(searchText_);   // a rebuilt pane keeps the search
    search_->onTextChanged = [this](const std::string& text) {
        if (text == searchText_) return;
        searchText_ = text;
        FullRebuild(/*markTopRead=*/false);
    };
    column->AddChild(search_);
    search_->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    column->AddChild(list_);
    list_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    FillWith(listBox_, column);
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
    preview_.onLinksShown = [this](const std::vector<MessageLink>& links) {
        if (onLinksShown) onLinksShown(links);
    };
    preview_.onLinkHovered = [this](const std::string& href) {
        if (onLinkHovered) onLinkHovered(href);
    };
    preview_.onComposeTo = [this](const std::string& n, const std::string& a,
                                  const std::string& href) {
        if (onComposeTo) onComposeTo(n, a, href);
    };
    // A body read for the first time is also scanned for the first time: the
    // row's badge stops being "unscanned" the moment the pane knows better.
    preview_.remoteImagesAllowed = [this](const std::string& addr) {
        return remoteImagesAllowed && remoteImagesAllowed(addr);
    };
    preview_.remoteImageHostTrusted = [this](const std::string& url) {
        return remoteImageHostTrusted && remoteImageHostTrusted(url);
    };
    preview_.onAlwaysAllowRemoteImages = [this](const std::string& addr) {
        if (onAlwaysAllowRemoteImages) onAlwaysAllowRemoteImages(addr);
    };
    // Right-click on the sender in the reading pane: the sender's own menu.
    preview_.senderMenuItems = [this](const MessageEnvelope& m) { return SenderMenuItems(m); };
    preview_.onSecurityScanned = [this](const MessageEnvelope& m, const MessageSecurity& s) {
        RefreshRowBadge(m, s);
    };
    preview_.onBodyMissing = [this](const MessageEnvelope& m) {
        if (onBodyMissing) onBodyMissing(m);
    };
    FillWith(messageBox_, preview_.Build());
}

std::shared_ptr<UltraCanvasContainer> MailView::BuildBackBar() {
    auto bar = CreateContainer("mailBackBar", 0, 0, 0, static_cast<int>(Theme::kControlHeight));
    bar->layout.SetFlexRow()
              .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto back = CreateButton("mailBack", 0, 0, 150, Theme::kControlHeight, "\xE2\x86\x90 Back to list");
    Theme::FitToLabel(back, 150);
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

void MailView::SetBodyOptions(bool showHtml, float textSizePx) {
    if (preview_.showHtml == showHtml && preview_.bodyFontSizePx == textSizePx) return;
    preview_.showHtml = showHtml;
    preview_.bodyFontSizePx = textSizePx;
    preview_.ReRender();
}

void MailView::SetLinkTooltips(bool tooltips) {
    if (preview_.linkTooltips == tooltips) return;
    preview_.linkTooltips = tooltips;
    preview_.ReRender();
}

void MailView::SetFolderTreeWidth(bool fitToText, int fixedPx) {
    folderTreeFitToText_  = fitToText;
    folderTreeFixedWidth_ = fixedPx;
    ApplyFolderTreeWidth();
}

void MailView::ApplyFolderTreeWidth(bool allowRetry) {
    if (!outerSplit_ || !folderBox_ || !folderTree_) return;
    int width = folderTreeFixedWidth_;
    if (folderTreeFitToText_) {
        const int rows = folderTree_->GetRequiredWidth();
        if (rows <= 0) {
            // Not in a window yet, so nothing to measure the text with.
            auto* app = UltraCanvasApplicationBase::GetCurrent();
            if (allowRetry && app && !folderTreeRetryPosted_) {
                folderTreeRetryPosted_ = true;
                app->PostToUIThread([this]() {
                    folderTreeRetryPosted_ = false;
                    ApplyFolderTreeWidth(/*allowRetry=*/false);
                });
            }
            return;
        }
        // The card around the tree: its border and padding on both sides.
        const int card = static_cast<int>(std::ceil(folderBox_->GetTotalBorderHorizontal() +
                                                    folderBox_->GetTotalPaddingHorizontal()));
        width = rows + kFolderFitSlack + card;
    }
    width = std::clamp(width, kFolderMinWidth, kFolderMaxWidth);
    if (outerSplit_->GetPaneFixedSize(0) != width) outerSplit_->SetPaneFixedSize(0, width);
}

void MailView::SetAccounts(std::vector<Account> accounts) {
    accounts_ = accounts;
    preview_.SetAccounts(std::move(accounts));
}

void MailView::SetContacts(ContactIndex contacts) {
    contacts_ = contacts;
    badges_.SetContacts(contacts);
    preview_.SetContacts(std::move(contacts));
}

std::vector<MenuItemData> MailView::ShowEmailsItems(const std::string& senderAddr) {
    std::vector<MenuItemData> show;
    auto option = [&](const std::string& label, MessageFilter f) {
        const bool active = f.kind == filter_.kind &&
            (f.kind != MessageFilterKind::SameSender || f.sender == filter_.sender);
        show.push_back(MenuItemData::Radio(label, /*group=*/1, active,
            [this, f]() { SetFilter(f); }));
    };
    option("All messages", {});
    if (!senderAddr.empty())
        option("Same sender (" + senderAddr + ")", {MessageFilterKind::SameSender, senderAddr});
    else if (filter_.kind == MessageFilterKind::SameSender)
        option("Same sender (" + filter_.sender + ")", filter_);
    option("Unread", {MessageFilterKind::Unread, ""});
    option("Needs an answer", {MessageFilterKind::NeedsAnswer, ""});
    option("Spam", {MessageFilterKind::Spam, ""});
    option("Social media", {MessageFilterKind::SocialMedia, ""});
    option("Payments & invoices", {MessageFilterKind::Payments, ""});
    return show;
}

void MailView::ShowRowMenu(int row, const UCEvent& event) {
    if (!list_) return;
    UltraCanvasWindowBase* window = list_->GetWindow();
    if (!window) return;
    // The empty area below the rows (or an empty, filtered list): only the
    // view choices, so a filter that left nothing to click can be cleared.
    if (row < 0 || row >= static_cast<int>(messages_.size())) {
        rowMenu_ = std::make_shared<UltraCanvasMenu>("mailRow.ctx", 0, 0, 200, 0);
        rowMenu_->SetMenuType(MenuType::PopupMenu);
        rowMenu_->AddItem(MenuItemData::Submenu("Show emails", ShowEmailsItems("")));
        PopupElementSettings settings;
        rowMenu_->OpenMenu(event.pointerWindow, *window, settings);
        return;
    }
    const MessageEnvelope m = messages_[static_cast<std::size_t>(row)];
    const bool unread  = (m.flags & Flag_Seen) == 0;
    const bool waiting = row < static_cast<int>(rowStates_.size()) &&
                         rowStates_[static_cast<std::size_t>(row)].waiting;

    rowMenu_ = std::make_shared<UltraCanvasMenu>("mailRow.ctx", 0, 0, 200, 0);
    rowMenu_->SetMenuType(MenuType::PopupMenu);
    // Whose message this is, as the menu's title: the sender's address.
    if (!m.fromAddr.empty()) {
        rowMenu_->AddItem(MenuItemData::Header(m.fromAddr));
        rowMenu_->AddItem(MenuItemData::Separator());
    }

    // Show emails ▸ - narrow the list; the active choice carries the check.
    rowMenu_->AddItem(MenuItemData::Submenu("Show emails", ShowEmailsItems(m.fromAddr)));
    rowMenu_->AddItem(MenuItemData::Separator());

    // Read state and the needs-an-answer list.
    if (unread) {
        rowMenu_->AddItem(MenuItemData::Action("Mark as read", [this, m]() {
            if (onMarkRead) onMarkRead(m);
        }));
    } else {
        rowMenu_->AddItem(MenuItemData::Action("Mark as unread", [this, m]() {
            if (onMarkUnread) onMarkUnread(m);
        }));
    }
    rowMenu_->AddItem(MenuItemData::Action(
        waiting ? "Doesn't need an answer" : "Needs an answer", [this, m, waiting]() {
            if (onSetNeedsAnswer) onSetNeedsAnswer(m, !waiting);
        }));
    rowMenu_->AddItem(MenuItemData::Separator());

    // Spam: out of the junk mailbox, or back to the inbox from it.
    if (curFolderIsJunk_) {
        rowMenu_->AddItem(MenuItemData::Action("Not spam", [this, m]() {
            if (onNotJunk) onNotJunk(m);
        }));
    } else {
        rowMenu_->AddItem(MenuItemData::Action("Mark as spam", [this, m]() {
            if (onJunk) onJunk(m);
        }));
    }
    rowMenu_->AddItem(MenuItemData::Action("Unsubscribe…", [this, m]() {
        if (onUnsubscribe) onUnsubscribe(m);
    }));

    // Move to: every folder of the account that holds mail, except this one.
    std::vector<MenuItemData> moveItems;
    if (store_) {
        std::vector<Folder> folders;
        store_->ListFolders(m.accountId, folders);
        for (const auto& f : folders) {
            if (!f.selectable || f.name == m.folder) continue;
            // "Projects / 2026", not the wire name "INBOX.Projects.2026".
            const std::string label = FolderDisplayPath(f.name, FolderDelimiter(f, folders));
            const std::string target = f.name;
            moveItems.push_back(MenuItemData::Action(label, [this, m, target]() {
                if (onMoveTo) onMoveTo(m, target);
            }));
        }
    }
    if (!moveItems.empty())
        rowMenu_->AddItem(MenuItemData::Submenu("Move to folder", moveItems));

    // The sender and the address book.
    if (!m.fromAddr.empty()) {
        rowMenu_->AddItem(MenuItemData::Separator());
        for (auto& item : AddressBookItems(m)) rowMenu_->AddItem(item);
        const auto listItems = SenderListItems(m);
        if (!listItems.empty()) rowMenu_->AddItem(MenuItemData::Separator());
        for (auto& item : listItems) rowMenu_->AddItem(item);
    }
    PopupElementSettings settings;
    rowMenu_->OpenMenu(event.pointerWindow, *window, settings);
}

std::vector<MenuItemData> MailView::AddressBookItems(const MessageEnvelope& m) {
    std::vector<MenuItemData> items;
    if (m.fromAddr.empty()) return items;
    std::vector<MenuItemData> places;
    for (ContactSection s : { ContactSection::Family, ContactSection::Friends,
                              ContactSection::Work, ContactSection::Leisure,
                              ContactSection::Services, ContactSection::Other }) {
        ContactPlace place; place.section = s;
        places.push_back(MenuItemData::Action(place.Title(), [this, m, place]() {
            if (onAddToContactGroup) onAddToContactGroup(m, place);
        }));
    }
    const std::vector<GroupCount> groups = contactGroups ? contactGroups()
                                                         : std::vector<GroupCount>{};
    if (!groups.empty()) places.push_back(MenuItemData::Separator());
    for (const auto& g : groups) {
        ContactPlace place; place.isGroup = true; place.group = g.name;
        places.push_back(MenuItemData::Action(place.Title(), [this, m, place]() {
            if (onAddToContactGroup) onAddToContactGroup(m, place);
        }));
    }
    items.push_back(MenuItemData::Submenu("Add to contact group", places));
    if (contacts_.Contains(m.fromAddr)) {
        items.push_back(MenuItemData::Action("Edit contact", [this, m]() {
            if (onEditContact) onEditContact(m);
        }));
    } else {
        items.push_back(MenuItemData::Action("Add to contacts", [this, m]() {
            if (onAddContact) onAddContact(m);
        }));
    }
    return items;
}

std::vector<MenuItemData> MailView::SenderListItems(const MessageEnvelope& m) {
    std::vector<MenuItemData> items;
    const std::string address = SenderLists::Normalize(m.fromAddr);
    if (address.find('@') == std::string::npos || !onSenderListChange) return items;
    const SenderLists lists = senderLists ? senderLists() : SenderLists{};
    auto change = [this](std::string entry, bool blockList, bool add) {
        return [this, entry, blockList, add]() {
            if (onSenderListChange) onSenderListChange(entry, blockList, add);
        };
    };
    // Trust: fewer warnings for this address - only what catches a lie.
    items.push_back(lists.Trusts(address)
        ? MenuItemData::Action("Stop trusting this sender", change(address, false, false))
        : MenuItemData::Action("Always trust this sender", change(address, false, true)));
    // Block: the address, or the whole domain unless it is a mailbox provider
    // (blocking gmail.com would block everyone who writes from it).
    const std::string blockedBy = lists.BlockedBy(address);
    if (blockedBy == address) {
        items.push_back(MenuItemData::Action("Unblock this sender", change(address, true, false)));
    } else if (!blockedBy.empty()) {
        items.push_back(MenuItemData::Action("Unblock everything from " + blockedBy.substr(1),
                                             change(blockedBy, true, false)));
    } else {
        items.push_back(MenuItemData::Action("Block this sender", change(address, true, true)));
        const std::string domain = RegistrableDomain(DomainOfAddress(address));
        if (!domain.empty() && !IsPersonalMailboxDomain(domain))
            items.push_back(MenuItemData::Action("Block everything from " + domain,
                                                 change("@" + domain, true, true)));
    }
    return items;
}

void MailView::RescanSender(const std::string& entry) {
    if (!store_ || mailDir_.empty() || entry.empty()) return;
    SenderLists match;
    match.blocked.insert(entry);
    std::vector<MessageEnvelope> mail;
    for (const auto& m : messages_)
        if (!match.BlockedBy(m.fromAddr).empty() && mail.size() < 300) mail.push_back(m);
    if (mail.empty()) return;
    const uint64_t token = ++rescanToken_;
    // Read and scan off the UI thread; the verdicts are stored and shown on it.
    std::thread([this, mail, token, dir = mailDir_]() {
        auto reports = std::make_shared<std::vector<std::pair<MessageEnvelope, ThreatReport>>>();
        for (const auto& m : mail) {
            std::ifstream in(PathFromUtf8(CachedBodyPath(dir, m.accountId, m.folder, m.uid)),
                             std::ios::binary);
            if (!in) continue;
            const std::string raw((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
            if (!raw.empty()) reports->emplace_back(m, ScanRawMessage(raw));
        }
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, reports, token]() {
            if (token != rescanToken_ || !store_) return;   // a newer rescan took over
            for (const auto& [m, report] : *reports) {
                MessageSecurity sec;
                store_->GetSecurity(m.accountId, m.folder, m.uid, sec);
                sec.level  = report.level;
                sec.score  = report.score;
                sec.bulk   = report.bulk;
                sec.reason = report.Summary();
                sec.verifiedDomain = report.verifiedDomain;
                sec.verifiedBy     = report.verifiedBy;
                sec.findings       = report.Codes();
                sec.scannedAt = static_cast<int64_t>(std::time(nullptr));
                store_->SetSecurity(m.accountId, m.folder, m.uid, sec);
                RefreshRowBadge(m, sec);
            }
            RecheckShownMessage();
        });
    }).detach();
}

std::vector<MenuItemData> MailView::SenderMenuItems(const MessageEnvelope& m) {
    std::vector<MenuItemData> items;
    if (m.fromAddr.empty()) return items;
    const std::string address = m.fromAddr;
    items.push_back(MenuItemData::Action("Copy address", [address]() {
        SetClipboardText(address);
    }));
    const bool showingSender = filter_.kind == MessageFilterKind::SameSender &&
                               filter_.sender == m.fromAddr;
    items.push_back(showingSender
        ? MenuItemData::Action("Show all messages", [this]() { SetFilter({}); })
        : MenuItemData::Action("Show only mail from this sender", [this, address]() {
              SetFilter({MessageFilterKind::SameSender, address});
          }));
    items.push_back(MenuItemData::Separator());
    for (auto& item : AddressBookItems(m)) items.push_back(item);
    items.push_back(MenuItemData::Separator());
    for (auto& item : SenderListItems(m)) items.push_back(item);
    // Spam: out of the junk mailbox, or back to the inbox from it.
    if (curFolderIsJunk_) {
        items.push_back(MenuItemData::Action("Not spam", [this, m]() {
            if (onNotJunk) onNotJunk(m);
        }));
    } else {
        items.push_back(MenuItemData::Action("Mark as spam", [this, m]() {
            if (onJunk) onJunk(m);
        }));
    }
    return items;
}

void MailView::SetIconCache(const SenderIconCache* cache) {
    icons_ = cache;
    badges_.SetIconCache(cache);
    preview_.SetIconCache(cache);
}

void MailView::SetIconRequester(std::function<void(const std::string& key)> request) {
    iconRequester_ = request;
    preview_.SetIconRequester(std::move(request));
}

void MailView::IconCached(const std::string& key) {
    if (!icons_ || key.empty()) return;
    bool changed = false;
    for (SenderBadge& badge : rowBadges_) {
        if (badge.iconKey != key) continue;
        badge.iconPath = icons_->IconForKey(key);
        badge.iconKey.clear();
        changed = true;
    }
    if (changed && list_) list_->RequestRedraw();
    preview_.IconCached(key);
}

void MailView::RefreshBadges() {
    for (std::size_t row = 0; row < messages_.size() && row < rowBadges_.size(); ++row)
        rowBadges_[row] = BadgeFor(messages_[row]);
    if (list_) list_->RequestRedraw();
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
        if (row < rowStates_.size())
            rowStates_[row].attachments = security.attachments > 0 ? security.attachments : 0;
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

SenderClass MailView::ClassFor(const MessageEnvelope& m) const {
    MessageSecurity sec;
    if (const auto it = security_.find(m.uid); it != security_.end()) sec = it->second;
    return badges_.Classify(m, sec, curFolderIsJunk_).cls;
}

void MailView::RebuildFolderTree() {
    if (!folderTree_) return;
    Trace::Stage trace("Folder tree", 0);
    folderNodeId_.clear();
    folderDelims_.clear();

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
        {
            Trace::Stage step("Folders of " + accId + " from the store", 0);
            if (store_) store_->ListFolders(accId, folders);
        }
        for (const auto& f : folders)
            folderDelims_[accId + "\n" + f.name] = FolderDelimiter(f, folders);
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
            // Its levels by the separator the server uses: "INBOX.Drafts" on a
            // Courier-style server is Drafts under the inbox, not a folder
            // called "INBOX.Drafts" (the leading INBOX level is the base node,
            // whose id the path "INBOX" already has).
            const std::string delim = FolderDelimiter(folder, folders);
            std::string parent = inboxNode;         // Inbox is the parent
            std::string path;
            for (const std::string& level : FolderLevels(folder.name, delim)) {
                if (!path.empty()) path += delim;
                path += level;
                // Elide a stored, non-selectable container: create no node and
                // leave `parent` unchanged so its children attach to it.
                auto sel = selectable.find(path);
                const bool isContainer = (sel != selectable.end() && !sel->second);
                if (!isContainer) {
                    const std::string nodeId = kFolderNodePrefix + accId + "::" + path;
                    if (created.insert(nodeId).second) {
                        TreeNodeData data(nodeId, FolderDisplayName(path, delim));
                        data.textColor = Theme::kTextPrimary;
                        folderTree_->AddNode(parent, data);
                        folderNodeId_[nodeId] = {accId, path};
                    }
                    parent = nodeId;
                }
            }
        }
    }

    Trace::Stage step("Expand the tree, select the folder, fit its width", 0);
    folderTree_->ExpandAll();
    SelectFolderNode(curAccount_, curFolder_);
    if (folderTreeFitToText_) ApplyFolderTreeWidth();
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
    if (!sameAccount) {
        curFolder_ = "INBOX";
        filter_ = MessageFilter{};
        // Another account's UID: the same number here is another message.
        selectedUid_ = -1;
        selectedFolder_.clear();
        traceNextPreview_ = true;
    }
    RebuildFolderTree();
    // The folder on screen was deleted or renamed on the server (the folder
    // sync dropped it): its account's inbox instead of an empty list.
    if (curFolder_ != "INBOX" && !folderDelims_.count(curAccount_ + "\n" + curFolder_)) {
        curFolder_ = "INBOX";
        filter_ = MessageFilter{};
        SelectFolderNode(curAccount_, curFolder_);
    }
    RebuildList();
}

std::string MailView::DelimiterOf(const std::string& accountId, const std::string& folder) const {
    const auto it = folderDelims_.find(accountId + "\n" + folder);
    return it != folderDelims_.end() ? it->second : std::string("/");
}

void MailView::ShowFolder(const std::string& accountId, const std::string& folder) {
    if (accountId != curAccount_ || folder != curFolder_) filter_ = MessageFilter{};
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

void MailView::RecheckShownMessage() {
    for (const auto& m : messages_) {
        if (m.uid != selectedUid_ || m.folder != selectedFolder_) continue;
        if (preview_.Shows(m.accountId, m.folder, m.uid)) preview_.Show(m);
        return;
    }
}

bool MailView::OpenMessage(const std::string& accountId, const std::string& folder, int64_t uid) {
    if (accountId != curAccount_) ShowAccount(accountId);
    if (folder != curFolder_) ShowFolder(accountId, folder);
    if (!list_) return false;
    for (std::size_t i = 0; i < messages_.size(); ++i) {
        if (messages_[i].uid != uid || messages_[i].folder != folder) continue;
        const int row = static_cast<int>(i);
        programmaticSelection_ = true;
        if (auto sel = list_->GetSelection()) sel->Select(row);
        programmaticSelection_ = false;
        list_->EnsureRowVisible(row);
        SelectRowImpl(row, /*markRead=*/true);
        return true;
    }
    return false;
}

void MailView::BuildMessageRow(const MessageEnvelope& m, const std::set<int64_t>& waitingUids,
                               MultiColumnListItem& outItem, MailRowState& outState,
                               SenderBadge& outBadge) const {
    const bool isUnread  = (m.flags & Flag_Seen) == 0;
    const bool isWaiting = waitingUids.count(m.uid) > 0;

    // Decode defensively: messages synced before header decoding are still
    // stored raw. Decoding already-decoded text is a no-op.
    // One line each: a list row has room for one, and word wrapping being off
    // does not stop an explicit line break - a subject such as LinkedIn's
    // "... storage.\n\nWe're partnering ..." (the break is in the encoded
    // header itself) drew over two rows.
    std::string sender  = SingleLine(DisplayHeader(
        m.fromName.empty() ? m.fromAddr : m.fromName));
    std::string subject = m.subject.empty()
        ? std::string("(no subject)") : SingleLine(DisplayHeader(m.subject));

    // State glyphs in front of the sender: ● unread, ↩ waiting for a reply.
    std::string state = std::string(isUnread ? "\xE2\x97\x8F " : "")
                      + (isWaiting ? "\xE2\x86\xA9 " : "");
    outBadge = BadgeFor(m);
    outItem = MultiColumnListItem({ state + sender, "", subject, FormatListDate(m.date) });
    int attachments = 0;
    if (auto it = security_.find(m.uid); it != security_.end() && it->second.attachments > 0)
        attachments = it->second.attachments;
    outItem.tooltip = sender + " <" + m.fromAddr + ">"
                 + (isUnread ? " — unread" : "") + (isWaiting ? " — waiting for reply" : "")
                 + (attachments == 1 ? " — 1 attachment"
                    : attachments > 1 ? " — " + std::to_string(attachments) + " attachments" : "")
                 + "\n" + FormatShortDate(m.date);
    // The badge cell explains itself rather than repeating the row tooltip:
    // what the sender is, and — when the content scan found something — why.
    outItem.SetCellTooltip(1, outBadge.tooltip);
    outState = { isUnread, isWaiting, attachments };
}

int MailView::SortedInsertPos(const MessageEnvelope& m) const {
    return static_cast<int>(SortedPosition(messages_, m, sort_, SortText()));
}

MessageSortText MailView::SortText() const {
    MessageSortText text;
    // As the row shows them: decoded (mail stored before header decoding is
    // still raw).
    text.sender = [](const MessageEnvelope& m) {
        return DisplayHeader(m.fromName.empty() ? m.fromAddr : m.fromName);
    };
    text.subject = [](const MessageEnvelope& m) { return DisplayHeader(m.subject); };
    // Contacts first, then business contacts, new senders, advertising,
    // spam and scams (SenderClass's order).
    // The classification alone: the whole badge (its icon, its tooltip) is
    // not needed to sort by it.
    text.kindRank = [this](const MessageEnvelope& m) {
        return static_cast<int>(ClassFor(m));
    };
    return text;
}

void MailView::ShowSortIndicator() {
    if (!list_) return;
    int column = 3;
    switch (sort_.key) {
        case MessageSortKey::Sender:  column = 0; break;
        case MessageSortKey::Kind:    column = 1; break;
        case MessageSortKey::Subject: column = 2; break;
        case MessageSortKey::Date:    column = 3; break;
    }
    list_->SetSortIndicator(column, sort_.ascending);
}

void MailView::SetSort(const MessageSort& sort) {
    if (sort == sort_) return;
    sort_ = sort;
    ShowSortIndicator();
    ApplySort();
}

void MailView::ApplySort() {
    if (!list_ || !model_ || messages_.empty()) return;
    if (rowStates_.size() != messages_.size() || rowBadges_.size() != messages_.size() ||
        model_->GetItemCount() != static_cast<int>(messages_.size())) {
        FullRebuild(/*markTopRead=*/false);   // out of step: build it in order
        return;
    }
    // The same rows in the new order: the messages, their states and badges
    // and the model's items move together; nothing is read from the store.
    const std::vector<std::size_t> order = SortOrder(messages_, sort_, SortText());
    std::vector<MessageEnvelope>     messages;
    std::vector<MailRowState>        states;
    std::vector<SenderBadge>         badges;
    std::vector<MultiColumnListItem> items;
    messages.reserve(order.size());
    states.reserve(order.size());
    badges.reserve(order.size());
    items.reserve(order.size());
    for (std::size_t i : order) {
        messages.push_back(std::move(messages_[i]));
        states.push_back(rowStates_[i]);
        badges.push_back(std::move(rowBadges_[i]));
        items.push_back(model_->GetItem(static_cast<int>(i)));
    }
    messages_  = std::move(messages);
    rowStates_ = std::move(states);
    rowBadges_ = std::move(badges);
    programmaticSelection_ = true;
    list_->ResetSelection();
    model_->SetItems(std::move(items));
    // The message being read stays selected, wherever it moved to.
    for (std::size_t i = 0; i < messages_.size(); ++i) {
        if (messages_[i].uid != selectedUid_ || messages_[i].folder != selectedFolder_) continue;
        if (auto sel = list_->GetSelection()) sel->Select(static_cast<int>(i));
        list_->EnsureRowVisible(static_cast<int>(i));
        break;
    }
    programmaticSelection_ = false;
}

void MailView::PreviewAfterPaint(int row, bool markRead) {
    if (row < 0 || row >= static_cast<int>(messages_.size())) return;
    const MessageEnvelope m = messages_[static_cast<std::size_t>(row)];
    // Known at once, so a rebuild before the pane renders keeps this message.
    selectedUid_    = m.uid;
    selectedFolder_ = m.folder;
    const uint64_t token = ++previewToken_;
    auto show = [this, m, token, markRead]() {
        if (token != previewToken_) return;   // another row was chosen meanwhile
        for (std::size_t i = 0; i < messages_.size(); ++i) {
            if (messages_[i].uid != m.uid || messages_[i].folder != m.folder ||
                messages_[i].accountId != m.accountId)
                continue;
            if (readingPane_) SelectRowImpl(static_cast<int>(i), markRead);
            else if (!preview_.Shows(m.accountId, m.folder, m.uid)) preview_.Show(messages_[i]);
            return;
        }
    };
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) { show(); return; }
    pendingPreview_ = show;
    // Twice: a task posted from an event handler (a click on an account or a
    // folder) runs before the frame is painted; the one it posts runs after.
    app->PostToUIThread([app, show]() { app->PostToUIThread(show); });
}

void MailView::ShowPendingPreviewNow() {
    if (!pendingPreview_) return;
    // The posted copy finds its token taken when it runs, and does nothing.
    auto show = std::move(pendingPreview_);
    pendingPreview_ = nullptr;
    show();
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
    std::string sender = SingleLine(DisplayHeader(
        messages_[row].fromName.empty() ? messages_[row].fromAddr : messages_[row].fromName));
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
    std::string title = FolderDisplayName(curFolder_, DelimiterOf(curAccount_, curFolder_));
    if (filter_.Active()) title += " \xC2\xB7 " + Describe(filter_);   // "Inbox · Unread"
    if (!searchText_.empty()) title += " \xC2\xB7 \xE2\x80\x9C" + searchText_ + "\xE2\x80\x9D";
    if (!messages_.empty()) {
        title += " — " + std::to_string(messages_.size()) + " message"
               + (messages_.size() == 1 ? "" : "s");
        if (shownUnread_ > 0) title += ", " + std::to_string(shownUnread_) + " unread";
    }
    else if (filter_.Active() || !searchText_.empty()) title += " \xE2\x80\x94 no messages";
    listBox_->SetTitle(title);
}

MessageFacts MailView::FactsFor(const MessageEnvelope& m,
                                const std::set<int64_t>& waitingUids) const {
    MessageFacts facts;
    facts.unread = (m.flags & Flag_Seen) == 0;
    facts.needsAnswer = waitingUids.count(m.uid) > 0;
    const SenderClass cls = ClassFor(m);
    facts.spam = cls == SenderClass::Spam || cls == SenderClass::Scam;
    if (const SenderBrand* brand = BrandForAddress(m.fromAddr)) facts.brand = brand->category;
    return facts;
}

void MailView::ApplyFilter(std::vector<MessageEnvelope>& messages,
                           const std::set<int64_t>& waitingUids) const {
    const bool searching = !searchText_.empty();
    if (!filter_.Active() && !searching) return;
    std::vector<MessageEnvelope> kept;
    kept.reserve(messages.size());
    for (auto& m : messages) {
        if (filter_.Active() && !FilterMatches(filter_, m, FactsFor(m, waitingUids))) continue;
        if (searching && !SearchMatches(m)) continue;
        kept.push_back(std::move(m));
    }
    messages = std::move(kept);
}

bool MailView::SearchMatches(const MessageEnvelope& m) const {
    // Decoded like the row shows them, so what can be read can be found.
    const std::string name = DisplayHeader(m.fromName);
    const std::string subject = DisplayHeader(m.subject);
    std::size_t pos = 0;
    while (pos < searchText_.size()) {
        while (pos < searchText_.size() && searchText_[pos] == ' ') ++pos;
        std::size_t end = searchText_.find(' ', pos);
        if (end == std::string::npos) end = searchText_.size();
        const std::string word = searchText_.substr(pos, end - pos);
        pos = end;
        if (word.empty()) continue;
        // Unicode-aware, case-insensitive (Ü finds ü).
        if (utf8_find(name, word, 0, false) < 0 && utf8_find(m.fromAddr, word, 0, false) < 0 &&
            utf8_find(subject, word, 0, false) < 0)
            return false;
    }
    return true;
}

void MailView::SetFilter(MessageFilter filter) {
    filter_ = std::move(filter);
    FullRebuild(/*markTopRead=*/false);
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
    Trace::Stage trace("Message list: rebuild " + curFolder_ + " of " + curAccount_, 0);
    std::optional<Trace::Stage> step;
    // Remember the shown message so a refresh keeps it selected instead of
    // snapping to the newest row (captured before the vectors are cleared).
    const int64_t     keepUid    = selectedUid_;
    const std::string keepFolder = selectedFolder_;
    messages_.clear();
    rowStates_.clear();
    rowBadges_.clear();
    security_.clear();
    shownUnread_ = 0;
    programmaticSelection_ = true;
    list_->ResetSelection();
    programmaticSelection_ = false;

    if (!store_ || curAccount_.empty()) {
        model_->Clear();
        preview_.Clear();
        selectedUid_ = -1;
        loadedAccount_.clear();
        loadedFolder_.clear();
        UpdateListTitle();
        if (!readingPane_) ShowListInPlace();
        return;
    }

    // The badge's two inputs that come from the store: whether this folder is
    // the junk mailbox, and the content-scan verdicts of the messages in it.
    step.emplace("Junk folder check and scan verdicts from the store", 0);
    curFolderIsJunk_ = CurrentFolderIsJunk();
    preview_.SetJunkFolder(curFolderIsJunk_);
    store_->ListSecurity(curAccount_, curFolder_, security_);

    step.emplace("Messages from the store", 0);
    store_->ListMessages(curAccount_, curFolder_, 0, messages_);
    step.emplace("Waiting-for-reply set from the store", 0);
    std::vector<MessageEnvelope> waiting;
    store_->ListNeedsAnswer(curAccount_, waiting);
    std::set<int64_t> waitingUids;
    for (const auto& w : waiting) if (w.folder == curFolder_) waitingUids.insert(w.uid);
    step.emplace("Filter and sort " + std::to_string(messages_.size()) + " message(s)", 0);
    ApplyFilter(messages_, waitingUids);
    SortMessages(messages_, sort_, SortText());

    // Every row into the model at once: added one by one, each row made the
    // view work out its geometry and scrollbar again - most of the time a
    // large mailbox took to appear.
    step.emplace("Rows: text, badges, tooltips", 0);
    std::vector<MultiColumnListItem> items;
    items.reserve(messages_.size());
    rowStates_.reserve(messages_.size());
    rowBadges_.reserve(messages_.size());
    for (const auto& m : messages_) {
        MultiColumnListItem item; MailRowState st; SenderBadge badge;
        BuildMessageRow(m, waitingUids, item, st, badge);
        if (st.unread) ++shownUnread_;
        items.push_back(std::move(item));
        rowStates_.push_back(st);
        rowBadges_.push_back(std::move(badge));
    }
    step.emplace("Into the list", 0);
    model_->SetItems(std::move(items));

    UpdateListTitle();
    step.emplace("Selection and scroll position", 0);

    // Default: the list is up; preview the newest message only when the reading
    // pane is on (Gmail mode waits for a click before hiding the list).
    if (!readingPane_) ShowListInPlace();
    if (messages_.empty()) {
        preview_.Clear();
        selectedUid_ = -1;
    } else {
        // Keep the previously-shown message selected across a refresh of the same
        // folder; fall back to the top row on a folder/account switch (where
        // the kept uid belonged to a different folder) or if it was expunged.
        int restore = 0;
        if (keepUid >= 0 && keepFolder == curFolder_)
            for (std::size_t i = 0; i < messages_.size(); ++i)
                if (messages_[i].uid == keepUid) { restore = static_cast<int>(i); break; }

        // Selected by code: the selection callback does not show it (nor mark
        // it read); the pane is filled below.
        programmaticSelection_ = true;
        suppressAutoRead_ = true;
        if (auto sel = list_->GetSelection()) sel->Select(restore);
        list_->EnsureRowVisible(restore);
        suppressAutoRead_ = false;
        programmaticSelection_ = false;
        // The pane keeps a message it already shows (a search or a filter that
        // left it in the list); anything else is rendered after the list has
        // been painted. A reading-pane folder switch marks the top message
        // read, as reading it (markTopRead); a background rebuild never does.
        const MessageEnvelope& m = messages_[static_cast<std::size_t>(restore)];
        if (!preview_.Shows(m.accountId, m.folder, m.uid)) preview_.Clear();
        PreviewAfterPaint(restore, markTopRead);
    }

    loadedAccount_ = curAccount_;   // what the rows now represent (for the diff path)
    loadedFolder_  = curFolder_;
    step.reset();
    Trace::Line(std::to_string(messages_.size()) + " message(s) in the list, " +
                std::to_string(shownUnread_) + " unread");
}

void MailView::DiffListFromStore(bool /*markTopRead*/) {
    // The store already holds the authoritative post-sync state (SyncMessages
    // upserted new mail, ReconcileFlags removed deleted UIDs and corrected flags).
    // Reconcile the visible rows to it in place, keeping selection and scroll.
    Trace::Stage trace("Message list: update " + curFolder_ + " of " + curAccount_ + " in place", 0);
    std::optional<Trace::Stage> step;
    step.emplace("Messages, scan verdicts and waiting-for-reply set from the store", 0);
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
    step.emplace("Filter and sort " + std::to_string(fresh.size()) + " message(s)", 0);
    ApplyFilter(fresh, waitingUids);
    SortMessages(fresh, sort_, SortText());
    step.emplace("Reconcile the rows", 0);

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
    if (maxN == 0 || (removed + added) * 2 > maxN) { step.reset(); FullRebuild(false); return; }

    // The rows that stay must already be in the fresh order (the walk below
    // inserts around them). A row whose place changed - its badge's kind,
    // when the list is sorted by kind - would be inserted a second time.
    {
        std::vector<int64_t> kept, freshKept;
        for (const auto& m : messages_) if (freshUids.count(m.uid)) kept.push_back(m.uid);
        for (const auto& m : fresh)     if (curUids.count(m.uid))   freshKept.push_back(m.uid);
        if (kept != freshKept) { step.reset(); FullRebuild(false); return; }
    }

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
            // The ↩ needs-an-answer glyph can change without a flag change
            // (the user's own "needs an answer" choice).
            const bool waiting = waitingUids.count(f.uid) > 0;
            if (pos < rowStates_.size() && rowStates_[pos].waiting != waiting) {
                rowStates_[pos].waiting = waiting;
                RefreshRowText(static_cast<int>(pos));
            }
            // The paperclip, once the sync has counted a body already listed.
            int attachments = 0;
            if (auto it = security_.find(f.uid); it != security_.end() && it->second.attachments > 0)
                attachments = it->second.attachments;
            if (pos < rowStates_.size() && rowStates_[pos].attachments != attachments) {
                rowStates_[pos].attachments = attachments;
                RefreshRowText(static_cast<int>(pos));   // notifies the view: the row redraws
            }
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
            // The same message, at its new row: the pane already shows it, so
            // it is not rendered again (its scroll position stays).
            programmaticSelection_ = true;
            suppressAutoRead_ = true;
            if (auto sel = list_->GetSelection()) sel->Select(selRow);
            list_->EnsureRowVisible(selRow);
            suppressAutoRead_ = false;
            programmaticSelection_ = false;
            // Its body may have come with this sync.
            preview_.BodyArrived(messages_[static_cast<std::size_t>(selRow)]);
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
    // screen belong in this list (each envelope carries its folder). Each one
    // goes in at its place in the list's order, so the list stays ordered as
    // mail streams in (rather than appending and re-sorting at the end).
    static const std::set<int64_t> kNoWaiting;
    bool added = false;
    for (const auto& m : batch) {
        if (m.folder != curFolder_) continue;
        bool dup = false;
        for (const auto& e : messages_) if (e.uid == m.uid) { dup = true; break; }
        if (dup) continue;
        InsertMessageRowAt(SortedInsertPos(m), m, kNoWaiting);
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
    // After an account switch always in the timing trace, step by step; a
    // click on a message only when it was slow.
    Trace::Stage trace("Reading pane: message " + std::to_string(m.uid) + " of " + m.folder,
                       traceNextPreview_ ? 0 : 50);
    traceNextPreview_ = false;
    // Remember what is shown so a rebuild after a sync can restore this selection
    // (by uid within the same folder) rather than snapping back to the newest row.
    selectedUid_    = m.uid;
    selectedFolder_ = m.folder;
    ++previewToken_;   // a preview still waiting for the paint is out of date
    // A click both changes the selection and reports the click: the message
    // is rendered once. Again only when its body was missing (a retry).
    if (!preview_.Shows(m.accountId, m.folder, m.uid) || preview_.BodyMissing())
        preview_.Show(m);
    if (!readingPane_) OpenMessageInPlace();
    // Opening an unread message reads it: hand the app the envelope so it can
    // set \Seen locally and on the server. MarkRowRead (driven back through
    // MarkRead) then updates this very row, so re-selecting it won't re-fire.
    if (markRead && onMarkRead && (m.flags & Flag_Seen) == 0) onMarkRead(m);
}

} // namespace UltraMail
