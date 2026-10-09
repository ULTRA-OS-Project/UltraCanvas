// Apps/UltraClaude/ui/ChatListView.cpp
// See ChatListView.h.
// Version: 0.1.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatListView.h"
#include "RepoStatus.h"

#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <utility>

using namespace UltraCanvas;

namespace UltraClaude {

namespace {
    constexpr float kTitleFontSize = 10.5f;
    constexpr float kBadgeFontSize = 9.0f;
    constexpr int kPadding = 8;
    constexpr int kBadgeGap = 6;          // between the title and the badge
    constexpr int kBadgeHeight = 18;
    constexpr int kBadgeMinWidth = 26;
    constexpr int kBadgeTextPadding = 7;

    const Color kTitleColor = Color(40, 40, 44, 255);
    const Color kSelectedTitleColor = Color(255, 255, 255, 255);
    // Work not yet in the default branch: the app's accent.
    const Color kPendingFill = Color(201, 100, 66, 255);
    const Color kPendingText = Color(255, 255, 255, 255);
    // Everything merged, still measuring, or unknown: a quiet grey.
    const Color kQuietFill = Color(228, 226, 220, 255);
    const Color kQuietText = Color(100, 100, 108, 255);
} // namespace

std::string BadgeText(const ChatBadge& badge) {
    switch (badge.kind) {
        case ChatBadge::Kind::Lines:     return FormatLineCount(badge.lines);
        case ChatBadge::Kind::Measuring: return "\xE2\x80\xA6";   // …
        case ChatBadge::Kind::Unknown:   return "?";
        case ChatBadge::Kind::NoBadge:      break;
    }
    return std::string();
}

void ChatListModel::SetRows(std::vector<ChatListRow> rows) {
    rows_ = std::move(rows);
    NotifyDataChanged();
}

ListDataValue ChatListModel::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= GetRowCount()) return ListDataValue();
    const ChatListRow& r = rows_[static_cast<size_t>(index.row)];
    switch (role) {
        case ListDataRole::DisplayRole:
            return r.title;
        case ListDataRole::ToolTipRole: {
            std::string tip = r.title + "\n" + r.folder;
            if (!r.badge.tooltip.empty()) tip += "\n" + r.badge.tooltip;
            return tip;
        }
        default:
            return ListDataValue();
    }
}

void ChatListDelegate::RenderItem(IRenderContext* ctx, const IListModel* model,
                                  int row, int column, const ListItemStyleOption& option) {
    (void)column;
    const auto* chats = dynamic_cast<const ChatListModel*>(model);
    if (!ctx || !chats || row < 0 || row >= chats->GetRowCount()) return;
    const ChatListRow& r = chats->Rows()[static_cast<size_t>(row)];

    const int cellX = option.columnX;
    const int cellW = option.columnWidth;
    const int cellY = option.rect.y;
    const int cellH = option.rect.height;
    int titleRight = cellX + cellW - kPadding;

    // ----- the badge, right-aligned -----
    const std::string badgeText = BadgeText(r.badge);
    if (!badgeText.empty()) {
        ctx->SetFontSize(kBadgeFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        const int textWidth = ctx->GetTextLineWidth(badgeText);
        const int width = std::max(kBadgeMinWidth, textWidth + 2 * kBadgeTextPadding);
        const Rect2Dd pill(cellX + cellW - kPadding - width, cellY + (cellH - kBadgeHeight) / 2.0,
                           width, kBadgeHeight);
        const bool pending = r.badge.kind == ChatBadge::Kind::Lines && r.badge.lines > 0;
        ctx->DrawFilledRectangle(pill, pending ? kPendingFill : kQuietFill, 0.0f,
                                 Colors::Transparent, kBadgeHeight / 2.0f);
        ctx->SetTextWrap(TextWrap::WrapNone);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(pending ? kPendingText : kQuietText);
        ctx->DrawTextInRect(badgeText, pill);
        ctx->SetFontWeight(FontWeight::Normal);
        titleRight = static_cast<int>(pill.x) - kBadgeGap;
    }

    // ----- the title, shortened to what is left -----
    const int titleWidth = titleRight - (cellX + kPadding);
    if (titleWidth <= 0 || r.title.empty()) return;
    ctx->SetFontSize(kTitleFontSize);
    ctx->SetTextWrap(TextWrap::WrapNone);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->SetTextPaint(option.isSelected ? kSelectedTitleColor : kTitleColor);
    ctx->DrawTextInRect(r.title, Rect2Dd(cellX + kPadding, cellY, titleWidth, cellH));
}

} // namespace UltraClaude
