// Apps/ChatCube/ui/ChatCubeViews.cpp
// The conversation list and the message list of the ChatCube window
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeViews.h"

#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <ctime>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

// ---- bubble geometry ---------------------------------------------------------
constexpr int   kRowPadV = 4;
constexpr int   kRowPadH = 12;
constexpr int   kBubblePadH = 10;
constexpr int   kBubblePadV = 7;
constexpr int   kBubbleMaxWidth = 460;
constexpr int   kBubbleMinWidth = 90;
constexpr float kBubbleRadius = 10.0f;
constexpr float kBodyFont = 11.0f;
constexpr float kMetaFont = 8.5f;
constexpr float kSenderFont = 9.5f;
constexpr int   kMetaHeight = 14;
constexpr int   kSenderHeight = 15;
constexpr int   kSeparatorHeight = 30;
constexpr int   kLineHeightEstimate = 17;

// The ChatCube palette (the RISC OS original: green own bubbles, blue-grey others).
const Color kOwnBubble       = Color(214, 238, 186, 255);
const Color kOwnBorder       = Color(184, 214, 152, 255);
const Color kOtherBubble     = Color(226, 234, 246, 255);
const Color kOtherBorder     = Color(196, 208, 228, 255);
const Color kBodyText        = Color(28, 28, 32, 255);
const Color kMetaText        = Color(110, 118, 128, 255);
const Color kSenderText      = Color(60, 84, 160, 255);
const Color kTickRead        = Color(46, 150, 70, 255);
const Color kTickFailed      = Color(200, 50, 50, 255);
const Color kSeparatorText   = Color(110, 110, 118, 255);
const Color kSeparatorPill   = Color(234, 234, 236, 255);

// ---- conversation rows -------------------------------------------------------
constexpr int   kListPad = 10;
constexpr float kTitleFont = 10.5f;
constexpr float kSnippetFont = 9.0f;
const Color kTitleText       = Color(32, 32, 36, 255);
const Color kSnippetText     = Color(110, 110, 118, 255);
const Color kSelectedText    = Color(255, 255, 255, 255);
const Color kUnreadFill      = Color(46, 150, 70, 255);
const Color kPresenceOnline  = Color(60, 190, 60, 255);
const Color kPresenceAway    = Color(230, 180, 40, 255);
const Color kPresenceBusy    = Color(210, 60, 60, 255);
const Color kPresenceOffline = Color(170, 170, 176, 255);

std::tm LocalTime(int64_t timestampMs) {
    const std::time_t t = static_cast<std::time_t>(timestampMs / 1000);
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

const char* TickText(UltraIMDeliveryStatus status) {
    switch (status) {
        case UltraIMDeliveryStatus::Sending:   return "\xE2\x80\xA6";          // …
        case UltraIMDeliveryStatus::Sent:      return "\xE2\x9C\x93";          // ✓
        case UltraIMDeliveryStatus::Delivered: return "\xE2\x9C\x93\xE2\x9C\x93";
        case UltraIMDeliveryStatus::Read:      return "\xE2\x9C\x93\xE2\x9C\x93";
        case UltraIMDeliveryStatus::Failed:    return "!";
    }
    return "";
}

std::string MessageText(const UltraIMMessage& m) {
    switch (m.type) {
        case UltraIMMessageType::Text:        return m.text;
        case UltraIMMessageType::Image:       return "[Image] " + m.text;
        case UltraIMMessageType::File:        return "[File] " + m.attachment.fileName;
        case UltraIMMessageType::Sticker:     return "[Sticker]";
        case UltraIMMessageType::Audio:       return "[Voice message]";
        case UltraIMMessageType::Video:       return "[Video] " + m.text;
        case UltraIMMessageType::SystemEvent: return m.text;
    }
    return m.text;
}

} // namespace

std::string FormatTime(int64_t timestampMs) {
    if (timestampMs <= 0) return std::string();
    const std::tm tm = LocalTime(timestampMs);
    char buffer[8];
    std::strftime(buffer, sizeof(buffer), "%H:%M", &tm);
    return buffer;
}

std::string FormatDate(int64_t timestampMs) {
    if (timestampMs <= 0) return std::string();
    const std::tm tm = LocalTime(timestampMs);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%d.%m.%Y", &tm);
    return buffer;
}

std::string GroupUserId(const std::string& userId) {
    std::string out;
    for (size_t i = 0; i < userId.size(); ++i) {
        if (i > 0 && i % 4 == 0) out += ' ';
        out += userId[i];
    }
    return out;
}

// ============================================================================
// MESSAGE LIST MODEL
// ============================================================================

void MessageListModel::Clear() {
    rows_.clear();
    NotifyDataChanged();
}

void MessageListModel::SetMessages(const std::vector<UltraIMMessage>& messages) {
    rows_.clear();
    std::string lastDate;
    for (const auto& m : messages) {
        const std::string date = FormatDate(m.timestampMs);
        if (!date.empty() && date != lastDate) {
            MessageRow separator;
            separator.kind = MessageRow::Kind::DateSeparator;
            separator.dateLabel = date;
            rows_.push_back(separator);
            lastDate = date;
        }
        MessageRow row;
        row.message = m;
        rows_.push_back(row);
    }
    NotifyDataChanged();
}

int MessageListModel::FindRow(const std::string& clientId) const {
    if (clientId.empty()) return -1;
    for (int i = static_cast<int>(rows_.size()) - 1; i >= 0; --i) {
        const MessageRow& r = rows_[static_cast<size_t>(i)];
        if (r.kind == MessageRow::Kind::Message && r.message.clientMessageId == clientId) return i;
    }
    return -1;
}

int MessageListModel::Upsert(const UltraIMMessage& message) {
    const int existing = FindRow(message.clientMessageId);
    if (existing >= 0) {
        UltraIMMessage& shown = rows_[static_cast<size_t>(existing)].message;
        const UltraIMDeliveryStatus status = std::max(shown.deliveryStatus, message.deliveryStatus);
        shown = message;
        shown.deliveryStatus = status;
        NotifyRowChanged(existing);
        return existing;
    }
    // Older than the newest shown (history a provider brings late - a Matrix
    // invitation's messages arrive once it is accepted): into its place.
    std::vector<UltraIMMessage> shownMessages;
    for (const auto& r : rows_) {
        if (r.kind == MessageRow::Kind::Message) shownMessages.push_back(r.message);
    }
    if (!shownMessages.empty() && message.timestampMs < shownMessages.back().timestampMs) {
        auto at = std::upper_bound(shownMessages.begin(), shownMessages.end(), message.timestampMs,
                                   [](int64_t ts, const UltraIMMessage& m) { return ts < m.timestampMs; });
        shownMessages.insert(at, message);
        SetMessages(shownMessages);
        return FindRow(message.clientMessageId);
    }
    // A new day starts with its date.
    std::string lastDate;
    for (auto it = rows_.rbegin(); it != rows_.rend(); ++it) {
        if (it->kind == MessageRow::Kind::Message) {
            lastDate = FormatDate(it->message.timestampMs);
            break;
        }
    }
    const std::string date = FormatDate(message.timestampMs);
    if (!date.empty() && date != lastDate) {
        MessageRow separator;
        separator.kind = MessageRow::Kind::DateSeparator;
        separator.dateLabel = date;
        rows_.push_back(separator);
        NotifyRowInserted(static_cast<int>(rows_.size()) - 1);
    }
    MessageRow row;
    row.message = message;
    rows_.push_back(row);
    const int index = static_cast<int>(rows_.size()) - 1;
    NotifyRowInserted(index);
    return index;
}

bool MessageListModel::Remove(const std::string& messageId) {
    if (messageId.empty()) return false;
    for (int i = static_cast<int>(rows_.size()) - 1; i >= 0; --i) {
        const MessageRow& r = rows_[static_cast<size_t>(i)];
        if (r.kind != MessageRow::Kind::Message) continue;
        if (r.message.messageId != messageId && r.message.clientMessageId != messageId) continue;
        rows_.erase(rows_.begin() + i);
        NotifyRowRemoved(i);
        return true;
    }
    return false;
}

bool MessageListModel::UpdateStatus(const std::string& clientId, UltraIMDeliveryStatus status) {
    const int row = FindRow(clientId);
    if (row < 0) return false;
    UltraIMMessage& m = rows_[static_cast<size_t>(row)].message;
    // Ticks only move forward; Failed replaces only Sending.
    if (status == UltraIMDeliveryStatus::Failed ? m.deliveryStatus != UltraIMDeliveryStatus::Sending
                                                : (m.deliveryStatus != UltraIMDeliveryStatus::Failed &&
                                                   status <= m.deliveryStatus))
        return true;
    m.deliveryStatus = status;
    NotifyRowChanged(row);
    return true;
}

ListDataValue MessageListModel::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= GetRowCount()) return ListDataValue();
    const MessageRow& r = rows_[static_cast<size_t>(index.row)];
    if (role == ListDataRole::DisplayRole)
        return r.kind == MessageRow::Kind::DateSeparator ? r.dateLabel : MessageText(r.message);
    if (role == ListDataRole::ToolTipRole && r.kind == MessageRow::Kind::Message)
        return FormatDate(r.message.timestampMs) + " " + FormatTime(r.message.timestampMs);
    return ListDataValue();
}

// ============================================================================
// MESSAGE BUBBLES
// ============================================================================

void MessageBubbleDelegate::SetViewWidth(int width) {
    if (width == viewWidth_) return;
    viewWidth_ = width;
    measured_.clear();
}

int MessageBubbleDelegate::BubbleMaxWidth() const {
    return std::max(kBubbleMinWidth, std::min(kBubbleMaxWidth, viewWidth_ * 7 / 10));
}

int MessageBubbleDelegate::EstimateHeight(const MessageRow& row, bool showSender) const {
    const std::string text = MessageText(row.message);
    const int charsPerLine = std::max(1, (BubbleMaxWidth() - 2 * kBubblePadH) / 7);
    int lines = 1;
    int column = 0;
    for (char c : text) {
        if (c == '\n' || ++column > charsPerLine) {
            ++lines;
            column = 0;
        }
    }
    return 2 * kRowPadV + 2 * kBubblePadV + lines * kLineHeightEstimate + kMetaHeight +
           (showSender ? kSenderHeight : 0);
}

int MessageBubbleDelegate::GetRowHeight(const IListModel* model, int row) const {
    const auto* messages = dynamic_cast<const MessageListModel*>(model);
    if (!messages || row < 0 || row >= messages->GetRowCount()) return 24;
    const MessageRow& r = messages->Rows()[static_cast<size_t>(row)];
    if (r.kind == MessageRow::Kind::DateSeparator) return kSeparatorHeight;
    auto it = measured_.find(r.message.clientMessageId);
    if (it != measured_.end()) return it->second;
    return EstimateHeight(r, messages->IsGroup() && !r.message.isOwn);
}

void MessageBubbleDelegate::RenderItem(IRenderContext* ctx, const IListModel* model,
                                       int row, int column, const ListItemStyleOption& option) {
    (void)column;
    const auto* messages = dynamic_cast<const MessageListModel*>(model);
    if (!ctx || !messages || row < 0 || row >= messages->GetRowCount()) return;
    const MessageRow& r = messages->Rows()[static_cast<size_t>(row)];
    const Rect2Di& cell = option.rect;
    if (cell.width > 0 && cell.width != viewWidth_) SetViewWidth(cell.width);

    if (r.kind == MessageRow::Kind::DateSeparator) {
        ctx->SetFontSize(kMetaFont + 0.5f);
        ctx->SetFontWeight(FontWeight::Normal);
        const int textWidth = ctx->GetTextLineWidth(r.dateLabel);
        const Rect2Dd pill(cell.x + (cell.width - textWidth) / 2.0 - 10, cell.y + 6, textWidth + 20,
                           kSeparatorHeight - 12);
        ctx->DrawFilledRectangle(pill, kSeparatorPill, 0.0f, Colors::Transparent, 8.0f);
        ctx->SetTextWrap(TextWrap::WrapNone);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(kSeparatorText);
        ctx->DrawTextInRect(r.dateLabel, pill);
        return;
    }

    const UltraIMMessage& m = r.message;
    const bool showSender = messages->IsGroup() && !m.isOwn;
    const std::string text = MessageText(m);
    const int maxText = BubbleMaxWidth() - 2 * kBubblePadH;

    // Measure the text as it will be drawn.
    ctx->SetFontSize(kBodyFont);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextWrap(TextWrap::WrapWordChar);
    const Size2Di textSize = ctx->GetTextDimensions(text, Size2Di(maxText, 0));
    const std::string meta = FormatTime(m.timestampMs) + (m.isOwn ? std::string("  ") + TickText(m.deliveryStatus) : "");
    ctx->SetFontSize(kMetaFont);
    const int metaWidth = ctx->GetTextLineWidth(meta);
    int senderWidth = 0;
    if (showSender) {
        ctx->SetFontSize(kSenderFont);
        ctx->SetFontWeight(FontWeight::Bold);
        senderWidth = ctx->GetTextLineWidth(m.senderName);
    }
    const int contentWidth = std::min(maxText, std::max({textSize.width, metaWidth, senderWidth}));
    const int bubbleWidth = std::max(kBubbleMinWidth, contentWidth + 2 * kBubblePadH);
    const int bubbleHeight = 2 * kBubblePadV + textSize.height + kMetaHeight + (showSender ? kSenderHeight : 0);
    const int rowHeight = bubbleHeight + 2 * kRowPadV;

    // A height that differs from what the list laid out: lay out again.
    auto it = measured_.find(m.clientMessageId);
    const int laidOut = it != measured_.end() ? it->second : EstimateHeight(r, showSender);
    measured_[m.clientMessageId] = rowHeight;
    if (laidOut != rowHeight && onHeightsChanged) onHeightsChanged();

    const double left = m.isOwn ? cell.x + cell.width - kRowPadH - bubbleWidth : cell.x + kRowPadH;
    const Rect2Dd bubble(left, cell.y + kRowPadV, bubbleWidth, bubbleHeight);
    ctx->DrawFilledRectangle(bubble, m.isOwn ? kOwnBubble : kOtherBubble, 1.0f,
                             m.isOwn ? kOwnBorder : kOtherBorder, kBubbleRadius);

    double y = bubble.y + kBubblePadV;
    const double x = bubble.x + kBubblePadH;
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Top);
    if (showSender) {
        ctx->SetFontSize(kSenderFont);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextWrap(TextWrap::WrapNone);
        ctx->SetTextPaint(kSenderText);
        ctx->DrawTextInRect(m.senderName, Rect2Dd(x, y, contentWidth, kSenderHeight));
        y += kSenderHeight;
    }
    ctx->SetFontSize(kBodyFont);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextWrap(TextWrap::WrapWordChar);
    ctx->SetTextPaint(kBodyText);
    ctx->DrawTextInRect(text, Rect2Dd(x, y, maxText, textSize.height));
    y += textSize.height;

    ctx->SetFontSize(kMetaFont);
    ctx->SetTextWrap(TextWrap::WrapNone);
    ctx->SetTextAlignment(TextAlignment::Right);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    Color metaColor = kMetaText;
    if (m.isOwn && m.deliveryStatus == UltraIMDeliveryStatus::Read) metaColor = kTickRead;
    if (m.isOwn && m.deliveryStatus == UltraIMDeliveryStatus::Failed) metaColor = kTickFailed;
    ctx->SetTextPaint(metaColor);
    ctx->DrawTextInRect(meta, Rect2Dd(bubble.x, y, bubble.width - kBubblePadH, kMetaHeight));
}

// ============================================================================
// CONVERSATION LIST
// ============================================================================

void ConversationListModel::Sort() {
    std::stable_sort(list_.begin(), list_.end(), [](const UltraIMConversation& a, const UltraIMConversation& b) {
        if (a.lastMessageTimeMs != b.lastMessageTimeMs) return a.lastMessageTimeMs > b.lastMessageTimeMs;
        return a.title < b.title;
    });
}

void ConversationListModel::SetConversations(std::vector<UltraIMConversation> conversations) {
    list_ = std::move(conversations);
    Sort();
    NotifyDataChanged();
}

int ConversationListModel::Upsert(const UltraIMConversation& conversation) {
    const int row = FindRow(conversation.conversationId);
    if (row >= 0) {
        const UltraIMPresence presence = list_[static_cast<size_t>(row)].presence;
        list_[static_cast<size_t>(row)] = conversation;
        if (conversation.presence == UltraIMPresence::Unknown) list_[static_cast<size_t>(row)].presence = presence;
    } else {
        list_.push_back(conversation);
    }
    Sort();
    NotifyDataChanged();
    return FindRow(conversation.conversationId);
}

bool ConversationListModel::Remove(const std::string& conversationId) {
    const int row = FindRow(conversationId);
    if (row < 0) return false;
    list_.erase(list_.begin() + row);
    NotifyDataChanged();
    return true;
}

void ConversationListModel::SetPresence(const std::string& contactId, UltraIMPresence presence) {
    for (size_t i = 0; i < list_.size(); ++i) {
        if (list_[i].directContactId == contactId) {
            list_[i].presence = presence;
            NotifyRowChanged(static_cast<int>(i));
        }
    }
}

int ConversationListModel::FindRow(const std::string& conversationId) const {
    for (size_t i = 0; i < list_.size(); ++i) {
        if (list_[i].conversationId == conversationId) return static_cast<int>(i);
    }
    return -1;
}

const UltraIMConversation* ConversationListModel::Find(const std::string& conversationId) const {
    const int row = FindRow(conversationId);
    return row < 0 ? nullptr : &list_[static_cast<size_t>(row)];
}

ListDataValue ConversationListModel::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= GetRowCount()) return ListDataValue();
    const UltraIMConversation& c = list_[static_cast<size_t>(index.row)];
    if (role == ListDataRole::DisplayRole) return c.title;
    if (role == ListDataRole::ToolTipRole)
        return c.isGroup ? c.title + "\n" + std::to_string(c.memberIds.size()) + " members"
                         : c.title + "\n" + GroupUserId(c.directContactId);
    return ListDataValue();
}

void ConversationDelegate::RenderItem(IRenderContext* ctx, const IListModel* model,
                                      int row, int column, const ListItemStyleOption& option) {
    (void)column;
    const auto* list = dynamic_cast<const ConversationListModel*>(model);
    if (!ctx || !list || row < 0 || row >= list->GetRowCount()) return;
    const UltraIMConversation& c = list->Conversations()[static_cast<size_t>(row)];
    const Rect2Di& cell = option.rect;
    const bool selected = option.isSelected;

    // Presence dot (1:1) or a group mark.
    const double dotX = cell.x + kListPad + 5;
    const double dotY = cell.y + 18;
    if (!c.isGroup) {
        Color dot = kPresenceOffline;
        if (c.presence == UltraIMPresence::Online) dot = kPresenceOnline;
        if (c.presence == UltraIMPresence::Away) dot = kPresenceAway;
        if (c.presence == UltraIMPresence::DoNotDisturb) dot = kPresenceBusy;
        ctx->DrawFilledCircle(Point2Dd(dotX, dotY), 5.0f, dot);
    }

    const double textX = cell.x + kListPad + 18;
    const double rightEdge = cell.x + cell.width - kListPad;

    // Time, top right; unread count under it.
    ctx->SetFontSize(kSnippetFont);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextWrap(TextWrap::WrapNone);
    const std::string time = FormatTime(c.lastMessageTimeMs);
    const int timeWidth = time.empty() ? 0 : ctx->GetTextLineWidth(time);
    if (!time.empty()) {
        ctx->SetTextAlignment(TextAlignment::Right);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(selected ? kSelectedText : kSnippetText);
        ctx->DrawTextInRect(time, Rect2Dd(rightEdge - timeWidth - 2, cell.y + 8, timeWidth + 2, 18));
    }
    int badgeWidth = 0;
    if (c.unreadCount > 0) {
        const std::string count = c.unreadCount > 99 ? "99+" : std::to_string(c.unreadCount);
        ctx->SetFontWeight(FontWeight::Bold);
        badgeWidth = std::max(20, ctx->GetTextLineWidth(count) + 10);
        const Rect2Dd pill(rightEdge - badgeWidth, cell.y + 30, badgeWidth, 18);
        ctx->DrawFilledRectangle(pill, kUnreadFill, 0.0f, Colors::Transparent, 9.0f);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(kSelectedText);
        ctx->DrawTextInRect(count, pill);
    }

    ctx->SetFontSize(kTitleFont);
    ctx->SetFontWeight(c.unreadCount > 0 ? FontWeight::Bold : FontWeight::Normal);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->SetTextPaint(selected ? kSelectedText : kTitleText);
    const std::string title = c.isGroup ? "# " + c.title : c.title;
    ctx->DrawTextInRect(title, Rect2Dd(textX, cell.y + 8, rightEdge - textX - timeWidth - 8, 18));

    ctx->SetFontSize(kSnippetFont);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(selected ? kSelectedText : kSnippetText);
    const std::string snippet = c.lastMessageSnippet.empty() ? std::string("No messages yet") : c.lastMessageSnippet;
    ctx->DrawTextInRect(snippet, Rect2Dd(textX, cell.y + 30, rightEdge - textX - badgeWidth - 8, 18));
}

} // namespace ChatCube
