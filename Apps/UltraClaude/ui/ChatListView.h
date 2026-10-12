// Apps/UltraClaude/ui/ChatListView.h
// What the chat list's UltraCanvasListView shows: one row per chat, its
// title on the left and, on the right, a badge with the lines its folder
// holds that the repository's default branch does not (RepoStatus):
//
//   | Fix the build in project A      [ 344 ] |   accent: work not yet in main
//   | Explain parser.cpp              [  0  ] |   grey: everything is in main
//   | Notes                                   |   no badge: not a git folder
//
// While a count is being measured the badge reads "…", and "?" when git could
// not answer; the row's tooltip says which and why. The list view paints the
// selection and hover; the delegate paints the row's content only.
//
// Version: 0.1.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListModel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraClaude {

struct ChatBadge {
    enum class Kind {
        NoBadge,     // no badge (not a repository, or not measured yet)
        Measuring,   // "…"
        Lines,       // lines (0 = everything is in the default branch)
        Unknown      // "?" - git could not answer
    };
    Kind kind = Kind::NoBadge;
    int64_t lines = 0;
    std::string tooltip;   // what the badge means, for the row's tooltip
};

struct ChatListRow {
    std::string title;
    std::string folder;
    ChatBadge badge;
};

class ChatListModel : public UltraCanvas::IListModel {
public:
    void SetRows(std::vector<ChatListRow> rows);
    const std::vector<ChatListRow>& Rows() const { return rows_; }

    int GetRowCount() const override { return static_cast<int>(rows_.size()); }
    int GetColumnCount() const override { return 1; }
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }

private:
    std::vector<ChatListRow> rows_;
};

class ChatListDelegate : public UltraCanvas::IItemDelegate {
public:
    void RenderItem(UltraCanvas::IRenderContext* ctx, const UltraCanvas::IListModel* model,
                    int row, int column, const UltraCanvas::ListItemStyleOption& option) override;
    int GetRowHeight(const UltraCanvas::IListModel*, int) const override { return 30; }
};

// The badge's text: "344", "12k", "…", "?"; empty for none.
std::string BadgeText(const ChatBadge& badge);

} // namespace UltraClaude
