// include/UltraCanvasClipboardHistoryView.h
// A clipboard history in an UltraCanvasListView: the list model (entries,
// optionally under section headers) and the row painter (thumbnail, title,
// meta line, Copy / Edit / Delete). UltraDesktop's quick panel and the
// UltraClipboard application use the same two, in a dark compact style and a
// light regular one (Docs/Research/UltraClipboard/UltraClipboard-RowAnatomy.svg).
//
//   auto model = std::make_shared<ClipboardHistoryListModel>();
//   auto rows  = std::make_shared<ClipboardHistoryRowDelegate>(ClipboardRowStyle::Light());
//   list->SetModel(model);
//   list->SetDelegate(rows);
//   model->SetEntries(history.List(), true);
//   list->onCellClicked = [rows = rows.get()](int row, int, const Point2Di& at) {
//       switch (rows->ActionAt(row, at)) { case ClipboardRowAction::Copy: ...; }
//   };
//
// The actions are painted into the row, as UltraMail's contact list paints its
// bin, and hit-tested through onCellClicked; the keyboard reaches them too
// (Enter copies, F2 edits, Delete deletes - the window handles those keys).
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListModel.h"

#include <chrono>
#include <string>
#include <vector>

namespace UltraCanvas {

struct ClipboardHistoryRow {
    bool header = false;
    std::string headerText;   // "PINNED", "TODAY", ...
    int headerCount = 0;
    ClipboardHistoryEntry entry;
};

class ClipboardHistoryListModel : public IListModel {
public:
    // sections: Pinned / Today / Yesterday / Earlier headers (the
    // application); compact: Pinned / Recent (the desktop's panel).
    enum class Sections { Flat, ByDay, PinnedAndRecent };
    // A section above the others: the first `count` entries under `title`
    // ("FOR ULTRAPAINT"), whatever their own section would be. The desktop's
    // quick panel puts what the window being pasted into takes there.
    struct LeadSection {
        std::string title;
        size_t count = 0;
    };
    void SetEntries(std::vector<ClipboardHistoryEntry> entries, Sections sections, const LeadSection& lead);
    void SetEntries(std::vector<ClipboardHistoryEntry> entries, Sections sections) {
        SetEntries(std::move(entries), sections, LeadSection{});
    }

    int GetRowCount() const override { return static_cast<int>(rows.size()); }
    int GetColumnCount() const override { return 1; }
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override;
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }

    const ClipboardHistoryRow* GetRow(int row) const;
    const ClipboardHistoryEntry* GetEntry(int row) const;   // nullptr for a header
    int FindRow(int64_t entryId) const;                     // -1 when not listed
    // The first entry row from `row` going by `step` (+1 / -1), skipping
    // headers; -1 when there is none.
    int EntryRowFrom(int row, int step) const;
    size_t GetEntryCount() const { return entryCount; }

private:
    std::vector<ClipboardHistoryRow> rows;
    size_t entryCount = 0;
};

enum class ClipboardRowAction { NoAction, Copy, Edit, Delete };

struct ClipboardRowStyle {
    bool compact = false;             // 48 px rows, 32 px thumbnails, actions only on the active row
    int rowHeight = 64;
    int headerHeight = 28;
    int thumbnailSize = 48;
    int padding = 16;
    int actionSize = 32;              // hit target
    int actionGlyph = 18;
    float titleSize = 10.5f;
    float metaSize = 9.0f;
    float headerSize = 8.0f;
    Color titleColor = Color(17, 24, 39);
    Color metaColor = Color(107, 114, 128);
    Color selectedTitleColor = Colors::White;
    Color selectedMetaColor = Color(220, 235, 250);
    Color headerColor = Color(107, 114, 128);
    Color headerBackground = Color(250, 251, 252);
    Color divider = Color(238, 240, 243);
    Color actionColor = Color(156, 163, 175);
    Color actionActiveColor = Color(55, 65, 81);
    Color actionHoverBackground = Color(204, 228, 247);
    Color deleteHoverColor = Color(220, 38, 38);
    Color deleteHoverBackground = Color(253, 226, 226);
    Color pinColor = Colors::Selection;
    Color copiedColor = Color(22, 163, 74);
    Color copiedBackground = Color(220, 252, 231);
    Color tileBackground = Color(243, 244, 246);
    Color tileBorder = Color(229, 231, 235);
    Color selectionBackground = Colors::Selection;
    Color hoverBackground = Colors::SelectionHover;
    Color listBackground = Colors::White;

    static ClipboardRowStyle Light();
    static ClipboardRowStyle Dark();   // the desktop's bar colours, compact
};

class ClipboardHistoryRowDelegate : public IItemDelegate {
public:
    explicit ClipboardHistoryRowDelegate(ClipboardRowStyle rowStyle = ClipboardRowStyle::Light());

    void RenderItem(IRenderContext* ctx, const IListModel* model, int row, int column,
                    const ListItemStyleOption& option) override;
    int GetRowHeight(const IListModel* model, int row) const override;

    // Which action a point in a row is on (posInCell from the list's cell
    // callbacks); NoAction for the rest of the row and for headers.
    // (Not "None": X11 defines that as a macro.)
    ClipboardRowAction ActionAt(int row, const Point2Di& posInCell) const;
    // From onCellHovered: the action under the pointer gets its highlight.
    // Returns true when that changed (the list wants a redraw).
    bool SetHover(int row, const Point2Di& posInCell);
    // The action under the pointer on `row`, NoAction elsewhere: for the tooltip.
    ClipboardRowAction GetHoverAction(int row) const { return row == hoverRow ? hoverAction : ClipboardRowAction::NoAction; }
    // The Copy glyph shows a check for a moment after a copy.
    void ShowCopied(int64_t entryId);
    bool IsShowingCopied() const;
    // "Copy  Enter", "Edit  F2", "Delete  Del".
    static std::string ActionTooltip(ClipboardRowAction action);

    const ClipboardRowStyle& Style() const { return style; }
    // Folder of the action glyphs; defaults to <resources>/media/icons/clipboard/.
    std::string iconsDir;

private:
    ClipboardRowStyle style;
    int rowWidth = 0;          // as last painted
    int hoverRow = -1;
    ClipboardRowAction hoverAction = ClipboardRowAction::NoAction;
    int64_t copiedId = 0;
    std::chrono::steady_clock::time_point copiedAt{};

    bool ActionsVisible(const ListItemStyleOption& option) const;
    double ActionsLeft(int width) const;
    void PaintThumbnail(IRenderContext* ctx, const ClipboardHistoryEntry& entry, const Rect2Dd& tile) const;
    void PaintActions(IRenderContext* ctx, const ClipboardHistoryEntry& entry, const ListItemStyleOption& option,
                      int row) const;
    void PaintText(IRenderContext* ctx, const std::string& text, const Rect2Dd& rect, float size,
                   FontWeight weight, const Color& color, bool monospace) const;
};

// The meta line under a title: "Text · 2 lines · 29 characters · UltraMail · 5 min ago".
std::string DescribeClipboardEntry(const ClipboardHistoryEntry& entry, int64_t nowMs, bool compact);
// "just now", "5 min ago", "2 h ago", "today 09:41", "yesterday 16:20", "3 Oct", "3 Oct 2025".
std::string FormatClipboardAge(int64_t whenMs, int64_t nowMs);
// "512 bytes", "412 KB", "2.4 MB".
std::string FormatClipboardSize(uint64_t bytes);
// A colour literal (#RGB, #RRGGBB, #RRGGBBAA, rgb()/rgba()) as a Color; false when not one.
bool ParseClipboardColour(const std::string& text, Color& colour);

} // namespace UltraCanvas
