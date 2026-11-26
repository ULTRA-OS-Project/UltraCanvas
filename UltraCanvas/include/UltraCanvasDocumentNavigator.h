// include/UltraCanvasDocumentNavigator.h
// UltraCanvasDocumentNavigator — a word processor's navigator, for any
// application that edits with UltraCanvasRichTextEdit: a side panel listing
// what the document holds, read from its model (RichDocumentOutline):
//
//   Headings           a tree by level; the heading the caret is under is
//                      the selected row, and follows the caret
//   Tables, Pictures, Charts
//   Comments           with their authors; a double-click opens the list of
//                      comments (ShowCommentsDialog) unless onCommentActivated
//                      is set
//   Bookmarks, Footnotes and endnotes, Tracked changes
//   Found in the text  what the search box finds in the document
//
// A click on an entry goes there (a picture or a chart is selected); a
// double-click also gives the keyboard to the document, and on a note opens
// it for editing. The search box filters every list by its text and lists
// the places the document says it.
//
// A heading's section - the heading, its text and its subheadings - is moved
// by dragging its row onto another heading (it goes before that one) or onto
// the Headings row (to the end), and by the buttons above the list: promote,
// demote, move up and move down (past the sibling before or after it). Each is
// one undo step of the document (UltraCanvasRichTextEdit::MoveHeadingSection,
// ChangeHeadingSectionLevel).
//
// The lists follow the document: after an edit they are read again once the
// typing pauses (SetRefreshDelay; 400 ms), and the current heading follows
// the caret at once. The navigator holds the edit weakly and listens through
// AddDocumentObserver, so the host's own onDocumentChanged stays its own.
//
// doc-check: std::shared_ptr<UltraCanvas::UltraCanvasRichTextEdit> edit;
// doc-check: std::shared_ptr<UltraCanvas::UltraCanvasContainer> sidePanel;
// doc-check: example
//     auto navigator = std::make_shared<UltraCanvas::UltraCanvasDocumentNavigator>("navigator", 0, 0, 260, 600);
//     navigator->SetEdit(edit);                     // reads and follows its document
//     navigator->SetFilterText("budget");           // what the search box does
//     sidePanel->AddChild(navigator);
//
// Version: 1.0.0 - headings (followed by the caret, moved by dragging or the buttons, promoted and demoted),
//                  tables, pictures, charts, comments, bookmarks, notes, tracked changes; search with finds
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasRichDocumentOutline.h"
#include "UltraCanvasTimer.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasRichTextEdit;
class UltraCanvasTextInput;
class UltraCanvasToolbar;
class UltraCanvasTreeView;
class UltraCanvasButton;
class TreeNode;

class UltraCanvasDocumentNavigator : public UltraCanvasContainer {
public:
    UltraCanvasDocumentNavigator(const std::string& identifier, float x, float y, float width, float height);
    explicit UltraCanvasDocumentNavigator(const std::string& identifier)
        : UltraCanvasDocumentNavigator(identifier, 0, 0, 260, 400) {}
    ~UltraCanvasDocumentNavigator() override;

    // ===== THE DOCUMENT =====
    // The edit whose document is listed; held weakly. Null lists nothing.
    void SetEdit(const std::shared_ptr<UltraCanvasRichTextEdit>& edit);
    std::shared_ptr<UltraCanvasRichTextEdit> GetEdit() const { return edit_.lock(); }

    // How long after an edit the lists are read again (milliseconds); 0 = at
    // once, after every edit.
    void SetRefreshDelay(int milliseconds) { refreshDelay_ = milliseconds < 0 ? 0 : milliseconds; }
    int GetRefreshDelay() const { return refreshDelay_; }
    // Reads the document now (a pending refresh included).
    void Refresh();
    bool IsRefreshPending() const { return refreshPending_; }
    const RichDocumentOutline& GetOutline() const { return outline_; }

    // ===== SEARCH =====
    // What the search box holds: every list shows only the entries whose
    // words contain it (any case, any script; a heading also when one under
    // it does), and "Found in the text" lists where the document says it.
    void SetFilterText(const std::string& text);
    const std::string& GetFilterText() const { return filter_; }
    const std::vector<RichDocRange>& GetFoundRanges() const { return found_; }

    // ===== ENTRIES =====
    enum class EntryKind { Category, Heading, Table, Picture, Chart, Comment, Bookmark, Note, Change, Found };
    // The tree's row id of an entry (index into the outline's list of that
    // kind, or into GetFoundRanges), and back. A category's index is the
    // EntryKind it lists.
    static std::string EntryId(EntryKind kind, int index);
    static bool ParseEntryId(const std::string& id, EntryKind& kind, int& index);
    // What a click on the entry does: goes there. False when it is gone.
    bool GoToEntry(EntryKind kind, int index);
    // ...and a double-click: goes there and gives the document the keyboard;
    // a note opens for editing, a comment opens the comments.
    bool ActivateEntry(EntryKind kind, int index);
    // Whether the entry is a row of the tree now (the search may hide it).
    bool IsEntryShown(EntryKind kind, int index) const;

    // ===== HEADINGS =====
    // The heading the caret is under (index into GetOutline().headings), the
    // row shown selected; -1 above the first heading.
    int GetCurrentHeading() const { return currentHeading_; }
    // What the buttons do, to the selected heading (or the caret's): one
    // undo step each; false when it cannot (no heading, level 1 promoted, no
    // sibling to move past).
    bool PromoteHeading();
    bool DemoteHeading();
    bool MoveHeadingUp();
    bool MoveHeadingDown();
    // What dropping heading `dragged` on heading `target` does: its section
    // goes before the target's; target -1 = to the end of the document.
    bool MoveHeadingBefore(int dragged, int target);

    // ===== PARTS =====
    UltraCanvasTreeView* GetTree() const { return tree_.get(); }
    UltraCanvasTextInput* GetSearchBox() const { return search_.get(); }
    UltraCanvasToolbar* GetToolbar() const { return toolbar_.get(); }

    // A comment's entry was double-clicked; unset, the list of comments
    // (ShowCommentsDialog) opens.
    std::function<void(int commentIndex)> onCommentActivated;
    // An entry was gone to (after the caret moved).
    std::function<void(EntryKind kind, int index)> onEntryActivated;

    AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Group; }
    std::string GetAccessibleName() const override;

private:
    std::weak_ptr<UltraCanvasRichTextEdit> edit_;
    int observerId_ = 0;
    std::shared_ptr<UltraCanvasTextInput> search_;
    std::shared_ptr<UltraCanvasToolbar> toolbar_;
    std::shared_ptr<UltraCanvasTreeView> tree_;
    UltraCanvasButton* promote_ = nullptr;
    UltraCanvasButton* demote_ = nullptr;
    UltraCanvasButton* up_ = nullptr;
    UltraCanvasButton* down_ = nullptr;

    RichDocumentOutline outline_;
    std::string filter_;
    std::vector<RichDocRange> found_;
    std::vector<std::string> foundText_;
    int currentHeading_ = -1;
    int refreshDelay_ = 400;
    bool refreshPending_ = false;
    TimerId refreshTimer_ = 0;
    bool selecting_ = false;              // the navigator selects a row itself: no going there
    bool built_ = false;
    // Rows the user closed, by id (a heading's by its level and words, which
    // survive the edits that renumber the headings).
    std::set<std::string> collapsed_;

    void DetachEdit();
    void OnDocumentEvent(bool documentChanged);
    void ScheduleRefresh();
    void RebuildTree();
    void FollowCaret();
    void UpdateButtons();
    int SelectedHeading() const;
    std::string CollapseKey(const TreeNode* node) const;
    bool Matches(const std::string& text) const;
};

} // namespace UltraCanvas
