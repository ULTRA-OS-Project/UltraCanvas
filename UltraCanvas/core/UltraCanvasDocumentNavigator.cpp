// core/UltraCanvasDocumentNavigator.cpp
// See include/UltraCanvasDocumentNavigator.h.
//
// Version: 1.0.0 - headings, tables, pictures, charts, comments, bookmarks, notes, tracked changes; search
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#include "UltraCanvasDocumentNavigator.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasRichTextEditDialogs.h"    // ShowCommentsDialog, DescribeRichComment
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasUtilsUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>

namespace UltraCanvas {

namespace {

constexpr float kSearchHeight = 28.0f;
constexpr float kToolbarHeight = 32.0f;
constexpr size_t kLabelLimit = 90;          // bytes of an entry's words on its row
constexpr size_t kFoundLimit = 200;         // places listed under "Found in the text"
constexpr int kSnippetBefore = 16;          // bytes of a find's row before it: the find stays in sight
constexpr int kSnippetAfter = 48;           // ...and after it

std::string Icon(const char* file) { return NormalizePath(GetResourcesDir() + "media/icons/" + file); }

// `text` cut to `limit` bytes at a whole character, "..." added when cut.
std::string Shorten(const std::string& text, size_t limit = kLabelLimit) {
    if (text.size() <= limit) return text;
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "...";
}

const char* KindCode(UltraCanvasDocumentNavigator::EntryKind kind) {
    using K = UltraCanvasDocumentNavigator::EntryKind;
    switch (kind) {
        case K::Category: return "cat";
        case K::Heading: return "h";
        case K::Table: return "t";
        case K::Picture: return "p";
        case K::Chart: return "c";
        case K::Comment: return "m";
        case K::Bookmark: return "b";
        case K::Note: return "n";
        case K::Change: return "x";
        case K::Found: return "f";
    }
    return "?";
}

// The words round a find, on one line: "...the budget for 2027 was..."
std::string Snippet(const std::string& text, int start, int end) {
    int from = std::max(0, start - kSnippetBefore);
    int to = std::min(static_cast<int>(text.size()), end + kSnippetAfter);
    from = UCRichDocumentEditor::SnapToCharStart(text, from);
    if (to < static_cast<int>(text.size())) to = UCRichDocumentEditor::SnapToCharStart(text, to);
    RichTextRun run;
    run.text = text.substr(static_cast<size_t>(from), static_cast<size_t>(to - from));
    const std::string placeholder = RichTextRun::kObjectReplacement;
    for (size_t at = run.text.find(placeholder); at != std::string::npos; at = run.text.find(placeholder, at))
        run.text.replace(at, placeholder.size(), " ");
    std::string line = RichOutlineText({run});
    if (from > 0) line = "..." + line;
    if (to < static_cast<int>(text.size())) line += "...";
    return line;
}

} // namespace

// ===== CONSTRUCTION =====

UltraCanvasDocumentNavigator::UltraCanvasDocumentNavigator(const std::string& identifier, float x, float y,
                                                           float width, float height)
    : UltraCanvasContainer(identifier, x, y, width, height) {
    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    search_ = CreateTextInput(identifier + ".search", 0, 0, width, kSearchHeight);
    search_->SetInputType(TextInputType::Search);
    search_->SetPlaceholder(Tr("Search the document"));
    search_->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(kSearchHeight));
    search_->SetMargin(6, 6, 2, 6);
    search_->layoutItem.SetFlexShrink(0);
    search_->onTextChanged = [this](const std::string& text) { SetFilterText(text); };
    AddChild(search_);

    // Promote, demote, up, down: what the selected heading's section can do.
    toolbar_ = std::make_shared<UltraCanvasToolbar>(identifier + ".tools", 0, 0, width, kToolbarHeight);
    ToolbarAppearance look = ToolbarAppearance::FromTheme(UltraCanvasTheme::Current(), ToolbarAppearance::Flat());
    look.iconSize = ToolbarIconSize::Small;
    toolbar_->SetAppearance(look);
    toolbar_->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(kToolbarHeight));
    toolbar_->layoutItem.SetFlexShrink(0);
    auto tool = [this, &identifier](const char* name, const char* icon, const std::string& tip,
                                    std::function<void()> action) {
        auto button = toolbar_->AddButton(identifier + "." + name, "", Icon(icon), std::move(action));
        button->SetTooltip(tip);
        button->SetAcceptsFocus(false);   // the keyboard stays where it was
        return button.get();
    };
    promote_ = tool("promote", "office/outdent.svg", Tr("Promote the heading and its subheadings"), [this]() { PromoteHeading(); });
    demote_ = tool("demote", "office/indent.svg", Tr("Demote the heading and its subheadings"), [this]() { DemoteHeading(); });
    up_ = tool("up", "menu/chevron-up.svg", Tr("Move the heading and its text up"), [this]() { MoveHeadingUp(); });
    down_ = tool("down", "menu/chevron-down.svg", Tr("Move the heading and its text down"), [this]() { MoveHeadingDown(); });
    AddChild(toolbar_);

    tree_ = std::make_shared<UltraCanvasTreeView>(identifier + ".tree", 0, 0, width,
                                                  std::max(40.0f, height - kSearchHeight - kToolbarHeight - 8));
    tree_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    tree_->SetRootNode(TreeNodeData("nav:root", ""));
    tree_->SetRootVisible(false);
    tree_->SetSelectionMode(TreeSelectionMode::Single);
    tree_->SetShowFirstChildOnExpand(false);
    tree_->SetAutoExpandSelectedNode(false);
    tree_->SetExpanderStyle(TreeExpanderStyle::Chevron);
    tree_->SetLineStyle(TreeLineStyle::NoLine);
    tree_->SetIndentSize(14);
    tree_->SetNodeDragEnabled(true);
    tree_->onNodeSelected = [this](TreeNode* node) {
        if (selecting_ || !node) return;
        EntryKind kind;
        int index = 0;
        if (ParseEntryId(node->data.nodeId, kind, index)) GoToEntry(kind, index);
        UpdateButtons();
    };
    tree_->onNodeDoubleClicked = [this](TreeNode* node) {
        EntryKind kind;
        int index = 0;
        if (node && ParseEntryId(node->data.nodeId, kind, index)) ActivateEntry(kind, index);
    };
    tree_->onNodeCollapsed = [this](TreeNode* node) { if (!selecting_ && node) collapsed_.insert(CollapseKey(node)); };
    tree_->onNodeExpanded = [this](TreeNode* node) { if (!selecting_ && node) collapsed_.erase(CollapseKey(node)); };
    // A heading is dropped on another heading (its section goes before that
    // one) or on the Headings row (to the end) - never into its own section.
    tree_->onNodeDragAccept = [this](TreeNode* dragged, TreeNode* target) {
        EntryKind draggedKind, targetKind;
        int draggedIndex = 0, targetIndex = 0;
        if (!dragged || !target || !ParseEntryId(dragged->data.nodeId, draggedKind, draggedIndex)
            || !ParseEntryId(target->data.nodeId, targetKind, targetIndex) || draggedKind != EntryKind::Heading)
            return false;
        if (draggedIndex < 0 || draggedIndex >= static_cast<int>(outline_.headings.size())) return false;
        if (targetKind == EntryKind::Category) return targetIndex == static_cast<int>(EntryKind::Heading);
        if (targetKind != EntryKind::Heading || targetIndex < 0
            || targetIndex >= static_cast<int>(outline_.headings.size()))
            return false;
        const RichOutlineHeading& moved = outline_.headings[static_cast<size_t>(draggedIndex)];
        const int block = outline_.headings[static_cast<size_t>(targetIndex)].blockIndex;
        return block < moved.blockIndex || block >= moved.sectionEnd;
    };
    tree_->onNodeDragDrop = [this](TreeNode* dragged, TreeNode* target) {
        EntryKind draggedKind, targetKind;
        int draggedIndex = 0, targetIndex = 0;
        if (!dragged || !target || !ParseEntryId(dragged->data.nodeId, draggedKind, draggedIndex)
            || !ParseEntryId(target->data.nodeId, targetKind, targetIndex))
            return;
        MoveHeadingBefore(draggedIndex, targetKind == EntryKind::Category ? -1 : targetIndex);
    };
    AddChild(tree_);

    RebuildTree();
    UpdateButtons();
}

UltraCanvasDocumentNavigator::~UltraCanvasDocumentNavigator() {
    DetachEdit();
    if (refreshTimer_ != 0)
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(refreshTimer_);
}

std::string UltraCanvasDocumentNavigator::GetAccessibleName() const { return Tr("Document navigator"); }

// ===== THE DOCUMENT =====

void UltraCanvasDocumentNavigator::DetachEdit() {
    if (auto edit = edit_.lock(); edit && observerId_ != 0) edit->RemoveDocumentObserver(observerId_);
    observerId_ = 0;
    edit_.reset();
}

void UltraCanvasDocumentNavigator::SetEdit(const std::shared_ptr<UltraCanvasRichTextEdit>& edit) {
    DetachEdit();
    edit_ = edit;
    // The edit keeps the listener; this navigator takes it away again when it
    // goes (DetachEdit), so the raw pointer never outlives it.
    if (edit) observerId_ = edit->AddDocumentObserver([this](bool documentChanged) { OnDocumentEvent(documentChanged); });
    collapsed_.clear();
    Refresh();
}

void UltraCanvasDocumentNavigator::OnDocumentEvent(bool documentChanged) {
    if (documentChanged) ScheduleRefresh();
    else FollowCaret();
}

void UltraCanvasDocumentNavigator::ScheduleRefresh() {
    auto* app = UltraCanvasApplication::GetInstance();
    std::weak_ptr<UltraCanvasUIElement> weak = weak_from_this();
    if (refreshDelay_ == 0 || !app || weak.expired()) {
        Refresh();
        return;
    }
    // Every edit puts the reading off again: the lists follow once the
    // typing pauses.
    refreshPending_ = true;
    if (refreshTimer_ != 0) app->StopTimer(refreshTimer_);
    refreshTimer_ = app->StartTimer(static_cast<unsigned>(refreshDelay_), false, [weak](TimerId) {
        auto self = std::static_pointer_cast<UltraCanvasDocumentNavigator>(weak.lock());
        if (!self) return;
        self->refreshTimer_ = 0;
        self->Refresh();
    });
}

void UltraCanvasDocumentNavigator::Refresh() {
    if (refreshTimer_ != 0) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(refreshTimer_);
        refreshTimer_ = 0;
    }
    refreshPending_ = false;
    auto edit = edit_.lock();
    RichDocumentOutline outline;
    std::vector<RichDocRange> found;
    std::vector<std::string> foundText;
    if (edit && edit->GetDocument()) {
        outline = BuildRichDocumentOutline(*edit->GetDocument());
        // The editing core searches the body; while a header or a note is
        // being edited it holds that instead, so the finds wait.
        if (!filter_.empty() && !edit->IsEditingHeaderOrFooter()) {
            const UCRichDocumentEditor& editor = edit->GetEditor();
            RichFindOptions options;
            options.caseSensitive = false;
            for (const RichDocRange& range : editor.FindAll(filter_, options)) {
                if (found.size() >= kFoundLimit) break;
                found.push_back(range);
                foundText.push_back(Snippet(editor.TextAt(range.start), range.start.byteOffset, range.end.byteOffset));
            }
        }
    }
    const bool same = built_ && outline == outline_ && foundText == foundText_ && found.size() == found_.size();
    outline_ = std::move(outline);
    found_ = std::move(found);
    foundText_ = std::move(foundText);
    if (!same) RebuildTree();
    FollowCaret();
    UpdateButtons();
}

// ===== SEARCH =====

bool UltraCanvasDocumentNavigator::Matches(const std::string& text) const {
    return filter_.empty() || utf8_find(text, filter_, 0, false) >= 0;
}

void UltraCanvasDocumentNavigator::SetFilterText(const std::string& text) {
    if (search_ && search_->GetText() != text) search_->SetText(text);
    if (filter_ == text && built_) return;
    filter_ = text;
    built_ = false;   // the rows change whatever the document does
    Refresh();
}

// ===== ENTRIES =====

std::string UltraCanvasDocumentNavigator::EntryId(EntryKind kind, int index) {
    return std::string("nav:") + KindCode(kind) + ":" + std::to_string(index);
}

bool UltraCanvasDocumentNavigator::ParseEntryId(const std::string& id, EntryKind& kind, int& index) {
    if (id.rfind("nav:", 0) != 0) return false;
    const size_t colon = id.find(':', 4);
    if (colon == std::string::npos) return false;
    const std::string code = id.substr(4, colon - 4);
    static const EntryKind kinds[] = {EntryKind::Category, EntryKind::Heading, EntryKind::Table, EntryKind::Picture,
                                      EntryKind::Chart, EntryKind::Comment, EntryKind::Bookmark, EntryKind::Note,
                                      EntryKind::Change, EntryKind::Found};
    for (EntryKind candidate : kinds) {
        if (code != KindCode(candidate)) continue;
        int value = 0;
        bool digits = colon + 1 < id.size();
        for (size_t i = colon + 1; i < id.size(); ++i) {
            if (id[i] < '0' || id[i] > '9') { digits = false; break; }
            value = value * 10 + (id[i] - '0');
        }
        if (!digits) return false;
        kind = candidate;
        index = value;
        return true;
    }
    return false;
}

bool UltraCanvasDocumentNavigator::IsEntryShown(EntryKind kind, int index) const {
    return tree_ && tree_->FindNode(EntryId(kind, index)) != nullptr;
}

bool UltraCanvasDocumentNavigator::GoToEntry(EntryKind kind, int index) {
    auto edit = edit_.lock();
    if (!edit || index < 0) return false;
    auto in = [index](const auto& list) { return index < static_cast<int>(list.size()); };
    // Going somewhere moves the caret, which would select its heading's row
    // in place of the row clicked: not while the click is being answered.
    selecting_ = true;
    bool went = false;
    switch (kind) {
        case EntryKind::Heading:
            went = in(outline_.headings)
                && edit->GoToPosition(RichDocPosition(outline_.headings[static_cast<size_t>(index)].blockIndex, 0));
            break;
        case EntryKind::Table:
            went = in(outline_.tables)
                && edit->GoToPosition(RichDocPosition(outline_.tables[static_cast<size_t>(index)].blockIndex, 0, 0, 0));
            break;
        case EntryKind::Picture:
        case EntryKind::Chart: {
            const bool picture = kind == EntryKind::Picture;
            if (picture ? !in(outline_.pictures) : !in(outline_.charts)) break;
            const RichDocPosition at = picture ? outline_.pictures[static_cast<size_t>(index)].position
                                               : outline_.charts[static_cast<size_t>(index)].position;
            went = edit->GoToPosition(at);
            if (went) edit->SelectImage(at);   // selected, as a click on it does
            break;
        }
        case EntryKind::Comment:
            went = in(outline_.comments) && edit->GoToComment(outline_.comments[static_cast<size_t>(index)].index);
            break;
        case EntryKind::Bookmark:
            went = in(outline_.bookmarks) && edit->GoToBookmark(outline_.bookmarks[static_cast<size_t>(index)].name);
            break;
        case EntryKind::Note:
            went = in(outline_.notes) && edit->GoToPosition(outline_.notes[static_cast<size_t>(index)].position);
            break;
        case EntryKind::Change:
            went = in(outline_.changes) && edit->GoToRange(outline_.changes[static_cast<size_t>(index)].range);
            break;
        case EntryKind::Found:
            went = in(found_) && edit->GoToRange(found_[static_cast<size_t>(index)]);
            break;
        case EntryKind::Category:
            break;
    }
    selecting_ = false;
    if (!went) return false;
    // The heading the place is under, without taking the row clicked away.
    const int block = edit->GetEditor().GetCaret().blockIndex;
    currentHeading_ = outline_.HeadingAt(block);
    if (kind == EntryKind::Heading) currentHeading_ = index;
    edit->RequestRedraw();
    if (onEntryActivated) onEntryActivated(kind, index);
    return true;
}

bool UltraCanvasDocumentNavigator::ActivateEntry(EntryKind kind, int index) {
    auto edit = edit_.lock();
    if (!edit) return false;
    if (kind == EntryKind::Category) return false;
    if (!GoToEntry(kind, index)) return false;
    if (kind == EntryKind::Comment) {
        const int comment = outline_.comments[static_cast<size_t>(index)].index;
        if (onCommentActivated) onCommentActivated(comment);
        else ShowCommentsDialog(*edit, [this](bool) { Refresh(); });
        return true;
    }
    if (kind == EntryKind::Note) {
        edit->EditNote(outline_.notes[static_cast<size_t>(index)].noteIndex);
    }
    edit->SetFocus(true);
    return true;
}

// ===== THE TREE =====

std::string UltraCanvasDocumentNavigator::CollapseKey(const TreeNode* node) const {
    EntryKind kind;
    int index = 0;
    if (!node || !ParseEntryId(node->data.nodeId, kind, index)) return "";
    if (kind == EntryKind::Heading && index < static_cast<int>(outline_.headings.size())) {
        const RichOutlineHeading& heading = outline_.headings[static_cast<size_t>(index)];
        return "h:" + std::to_string(heading.level) + ":" + heading.text;
    }
    return node->data.nodeId;
}

void UltraCanvasDocumentNavigator::RebuildTree() {
    if (!tree_) return;
    built_ = true;
    selecting_ = true;
    TreeRebuildScope scope(*tree_);
    tree_->SetRootNode(TreeNodeData("nav:root", ""));
    tree_->SetRootVisible(false);
    std::vector<TreeNode*> added;

    auto category = [&](EntryKind kind, const std::string& name, size_t count, const char* icon) -> std::string {
        const std::string id = EntryId(EntryKind::Category, static_cast<int>(kind));
        TreeNodeData data(id, name + "  " + std::to_string(count));
        data.leftIcon = TreeNodeIcon(Icon(icon), 16, 16);
        data.leftIcon.drawnInTextColor = true;
        data.showFirstChildOnExpand = false;
        if (TreeNode* node = tree_->AddNode("nav:root", data)) added.push_back(node);
        return id;
    };
    auto entry = [&](const std::string& parent, EntryKind kind, int index, const std::string& text,
                     const std::string& tooltip, bool dimmed = false) {
        TreeNodeData data(EntryId(kind, index), Shorten(text));
        data.tooltip = tooltip.empty() ? text : tooltip;
        data.dimmed = dimmed;
        data.showFirstChildOnExpand = false;
        if (TreeNode* node = tree_->AddNode(parent, data)) added.push_back(node);
    };

    // ===== HEADINGS: a heading on show when it matches or one under it does =====
    const size_t headingCount = outline_.headings.size();
    std::vector<bool> keep(headingCount, false);
    for (size_t i = headingCount; i-- > 0;) {
        const RichOutlineHeading& heading = outline_.headings[i];
        if (Matches(heading.text)) keep[i] = true;
        if (keep[i] && heading.parent >= 0) keep[static_cast<size_t>(heading.parent)] = true;
    }
    size_t shownHeadings = 0;
    for (bool k : keep) shownHeadings += k ? 1 : 0;
    // The Headings row is always there (a drop target), but a search that
    // finds no heading leaves it out.
    const std::string headings = shownHeadings > 0 || filter_.empty()
        ? category(EntryKind::Heading, Tr("Headings"), shownHeadings, "list-tree.svg") : std::string();
    for (size_t i = 0; i < headingCount; ++i) {
        if (!keep[i]) continue;
        const RichOutlineHeading& heading = outline_.headings[i];
        const std::string parent = heading.parent >= 0 ? EntryId(EntryKind::Heading, heading.parent) : headings;
        entry(parent, EntryKind::Heading, static_cast<int>(i), heading.text.empty() ? Tr("(no text)") : heading.text,
              heading.text);
    }

    // ===== THE OTHER LISTS: those with an entry the search keeps =====
    struct Row {
        int index;
        std::string text;
        std::string tooltip;
        bool dimmed = false;
    };
    auto list = [&](EntryKind kind, const std::string& name, const char* icon, const std::vector<Row>& rows) {
        std::vector<const Row*> shown;
        for (const Row& row : rows) {
            if (Matches(row.text) || (kind == EntryKind::Found)) shown.push_back(&row);
        }
        if (shown.empty()) return;
        const std::string id = category(kind, name, shown.size(), icon);
        for (const Row* row : shown) entry(id, kind, row->index, row->text, row->tooltip, row->dimmed);
    };

    std::vector<Row> rows;
    for (size_t i = 0; i < outline_.tables.size(); ++i) {
        const RichOutlineTable& table = outline_.tables[i];
        std::string text = table.caption;
        if (text.empty()) {
            text = TrF("Table {0}", static_cast<int>(i + 1));
            if (!table.firstCellText.empty()) text += ": " + table.firstCellText;
        }
        rows.push_back({static_cast<int>(i), text, TrF("{0} rows, {1} columns", table.rows, table.columns)});
    }
    list(EntryKind::Table, Tr("Tables"), "table.svg", rows);

    rows.clear();
    for (size_t i = 0; i < outline_.pictures.size(); ++i) {
        const RichOutlinePicture& picture = outline_.pictures[i];
        std::string text = !picture.caption.empty() ? picture.caption
                         : !picture.altText.empty() ? picture.altText
                                                    : TrF("Picture {0}", static_cast<int>(i + 1));
        rows.push_back({static_cast<int>(i), text, picture.name});
    }
    list(EntryKind::Picture, Tr("Pictures"), "image.svg", rows);

    rows.clear();
    for (size_t i = 0; i < outline_.charts.size(); ++i) {
        const RichOutlineChart& chart = outline_.charts[i];
        std::string text = !chart.caption.empty() ? chart.caption
                         : !chart.title.empty()   ? chart.title
                                                  : TrF("Chart {0}", static_cast<int>(i + 1));
        rows.push_back({static_cast<int>(i), text, ""});
    }
    list(EntryKind::Chart, Tr("Charts"), "chart.svg", rows);

    rows.clear();
    const auto& comments = outline_.comments;
    for (size_t i = 0; i < comments.size(); ++i) {
        RichComment comment;
        comment.author = comments[i].author;
        comment.text = comments[i].text;
        comment.resolved = comments[i].resolved;
        rows.push_back({static_cast<int>(i), DescribeRichComment(comment), comments[i].commentedText, comments[i].resolved});
    }
    list(EntryKind::Comment, Tr("Comments"), "quote.svg", rows);

    rows.clear();
    for (size_t i = 0; i < outline_.bookmarks.size(); ++i) {
        const RichOutlineBookmark& bookmark = outline_.bookmarks[i];
        if (bookmark.contentsMark) continue;    // the contents' own marks on the headings
        rows.push_back({static_cast<int>(i), bookmark.name, bookmark.text});
    }
    list(EntryKind::Bookmark, Tr("Bookmarks"), "menu/book.svg", rows);

    rows.clear();
    for (size_t i = 0; i < outline_.notes.size(); ++i) {
        const RichOutlineNote& note = outline_.notes[i];
        rows.push_back({static_cast<int>(i), note.mark + "  " + note.text,
                        note.kind == RichNote::Kind::Endnote ? Tr("Endnote") : Tr("Footnote")});
    }
    list(EntryKind::Note, Tr("Footnotes and endnotes"), "superscript.svg", rows);

    rows.clear();
    for (size_t i = 0; i < outline_.changes.size(); ++i) {
        const RichOutlineChange& change = outline_.changes[i];
        const std::string what = change.change == RichTextRun::Change::Deleted ? Tr("Deleted") : Tr("Inserted");
        std::string tip = change.author;
        if (!change.date.empty()) tip += (tip.empty() ? "" : ", ") + change.date.substr(0, 10);
        rows.push_back({static_cast<int>(i), what + ": " + change.text, tip});
    }
    list(EntryKind::Change, Tr("Tracked changes"), "edit.svg", rows);

    rows.clear();
    for (size_t i = 0; i < foundText_.size(); ++i) rows.push_back({static_cast<int>(i), foundText_[i], ""});
    list(EntryKind::Found, Tr("Found in the text"), "search.svg", rows);

    // Open, except what the user closed.
    for (TreeNode* node : added) {
        if (!node->HasChildren()) continue;
        if (collapsed_.count(CollapseKey(node))) node->Collapse();
        else node->Expand();
    }
    selecting_ = false;
}

void UltraCanvasDocumentNavigator::FollowCaret() {
    auto edit = edit_.lock();
    if (!edit || !tree_ || selecting_ || edit->IsEditingHeaderOrFooter()) return;
    currentHeading_ = outline_.HeadingAt(edit->GetEditor().GetCaret().blockIndex);
    TreeNode* node = currentHeading_ >= 0 ? tree_->FindNode(EntryId(EntryKind::Heading, currentHeading_)) : nullptr;
    // Under a search the caret's heading may be hidden: its nearest shown
    // parent stands for it.
    for (int h = currentHeading_; !node && h >= 0; h = outline_.headings[static_cast<size_t>(h)].parent)
        node = tree_->FindNode(EntryId(EntryKind::Heading, h));
    selecting_ = true;
    if (node) {
        if (!node->selected) tree_->SelectNode(node);
        tree_->ScrollTo(node);
    } else if (TreeNode* selected = tree_->GetFirstSelectedNode()) {
        EntryKind kind;
        int index = 0;
        if (ParseEntryId(selected->data.nodeId, kind, index) && kind == EntryKind::Heading) tree_->ClearSelection();
    }
    selecting_ = false;
    UpdateButtons();
}

// ===== HEADINGS =====

int UltraCanvasDocumentNavigator::SelectedHeading() const {
    if (TreeNode* selected = tree_ ? tree_->GetFirstSelectedNode() : nullptr) {
        EntryKind kind;
        int index = 0;
        if (ParseEntryId(selected->data.nodeId, kind, index) && kind == EntryKind::Heading
            && index < static_cast<int>(outline_.headings.size()))
            return index;
    }
    return currentHeading_ >= 0 && currentHeading_ < static_cast<int>(outline_.headings.size()) ? currentHeading_ : -1;
}

void UltraCanvasDocumentNavigator::UpdateButtons() {
    const int h = SelectedHeading();
    auto edit = edit_.lock();
    const bool editable = edit && !edit->IsReadOnly() && h >= 0;
    const RichOutlineHeading* heading = editable ? &outline_.headings[static_cast<size_t>(h)] : nullptr;
    const std::vector<RichDocBlock>* blocks = edit && edit->GetDocument() ? &edit->GetDocument()->blocks : nullptr;
    // Promoting a section needs room above every heading in it, demoting below.
    int highest = 6, lowest = 1;
    if (heading) {
        for (size_t i = static_cast<size_t>(h); i < outline_.headings.size()
                                                && outline_.headings[i].blockIndex < heading->sectionEnd; ++i) {
            highest = std::min(highest, outline_.headings[i].level);
            lowest = std::max(lowest, outline_.headings[i].level);
        }
    }
    if (promote_) promote_->SetDisabled(!heading || highest <= 1);
    if (demote_) demote_->SetDisabled(!heading || lowest >= 6);
    if (up_) up_->SetDisabled(!heading || !blocks || UCRichDocumentEditor::SiblingHeading(*blocks, heading->blockIndex, false) < 0);
    if (down_) down_->SetDisabled(!heading || !blocks || UCRichDocumentEditor::SiblingHeading(*blocks, heading->blockIndex, true) < 0);
}

bool UltraCanvasDocumentNavigator::PromoteHeading() {
    auto edit = edit_.lock();
    const int h = SelectedHeading();
    if (!edit || h < 0) return false;
    const bool done = edit->ChangeHeadingSectionLevel(outline_.headings[static_cast<size_t>(h)].blockIndex, -1);
    if (done) Refresh();
    return done;
}

bool UltraCanvasDocumentNavigator::DemoteHeading() {
    auto edit = edit_.lock();
    const int h = SelectedHeading();
    if (!edit || h < 0) return false;
    const bool done = edit->ChangeHeadingSectionLevel(outline_.headings[static_cast<size_t>(h)].blockIndex, +1);
    if (done) Refresh();
    return done;
}

bool UltraCanvasDocumentNavigator::MoveHeadingUp() {
    auto edit = edit_.lock();
    const int h = SelectedHeading();
    if (!edit || h < 0) return false;
    const bool done = edit->MoveHeadingSectionUp(outline_.headings[static_cast<size_t>(h)].blockIndex);
    if (done) Refresh();
    return done;
}

bool UltraCanvasDocumentNavigator::MoveHeadingDown() {
    auto edit = edit_.lock();
    const int h = SelectedHeading();
    if (!edit || h < 0) return false;
    const bool done = edit->MoveHeadingSectionDown(outline_.headings[static_cast<size_t>(h)].blockIndex);
    if (done) Refresh();
    return done;
}

bool UltraCanvasDocumentNavigator::MoveHeadingBefore(int dragged, int target) {
    auto edit = edit_.lock();
    const int count = static_cast<int>(outline_.headings.size());
    if (!edit || dragged < 0 || dragged >= count || target >= count) return false;
    const int before = target < 0 ? static_cast<int>(edit->GetDocument()->blocks.size())
                                  : outline_.headings[static_cast<size_t>(target)].blockIndex;
    const bool done = edit->MoveHeadingSection(outline_.headings[static_cast<size_t>(dragged)].blockIndex, before);
    if (done) Refresh();
    return done;
}

} // namespace UltraCanvas
