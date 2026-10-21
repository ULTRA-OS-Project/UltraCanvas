// Apps/ChatCube/ui/ChatCubeViews.h
// The two lists of the ChatCube window: the conversations on the left, the
// messages of the open conversation on the right (chat bubbles, date
// separators, delivery ticks). Models adapt UltraIM data to
// UltraCanvasListView; delegates paint one row's content - the list view
// paints selection, hover and scrolling.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListModel.h"
#include "UltraIM/UltraIMTypes.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ChatCube {

// "14:05" / "21.10.2026" in local time.
std::string FormatTime(int64_t timestampMs);
std::string FormatDate(int64_t timestampMs);
// A user id in groups of four, so two people can read it to each other.
std::string GroupUserId(const std::string& userId);

// ============================================================================
// MESSAGES
// ============================================================================

struct MessageRow {
    enum class Kind { Message, DateSeparator };
    Kind           kind = Kind::Message;
    UltraIMMessage message;     // Message rows
    std::string    dateLabel;   // DateSeparator rows
};

class MessageListModel : public UltraCanvas::IListModel {
public:
    void SetGroup(bool isGroup) { isGroup_ = isGroup; }
    bool IsGroup() const { return isGroup_; }

    void Clear();
    // History, oldest first; date separators are inserted between days.
    void SetMessages(const std::vector<UltraIMMessage>& messages);
    // Appends a new message, or - when its client id is already shown (our
    // own optimistic row, or a copy that came by a second path) - updates
    // that row instead. Returns the row.
    int Upsert(const UltraIMMessage& message);
    // Ticks of our own message, matched by client id; false when not shown.
    bool UpdateStatus(const std::string& clientId, UltraIMDeliveryStatus status);
    // A message deleted (redacted) for everyone: by its id or its client id.
    bool Remove(const std::string& messageId);

    const std::vector<MessageRow>& Rows() const { return rows_; }

    int GetRowCount() const override { return static_cast<int>(rows_.size()); }
    int GetColumnCount() const override { return 1; }
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }

private:
    int FindRow(const std::string& clientId) const;

    std::vector<MessageRow> rows_;
    bool isGroup_ = false;
};

// Paints a message row as a bubble: own messages right, others left with the
// sender's name in groups; the time and, for own messages, the ticks
//   ...  sending   ✓ sent   ✓✓ delivered   ✓✓ (green) read   ! failed
// GetRowHeight has no render context, so a bubble's height is measured when
// it is painted and cached per message and width; when a measurement differs
// from the estimate the list is told to lay out again (onHeightsChanged).
class MessageBubbleDelegate : public UltraCanvas::IItemDelegate {
public:
    std::function<void()> onHeightsChanged;

    void RenderItem(UltraCanvas::IRenderContext* ctx, const UltraCanvas::IListModel* model,
                    int row, int column, const UltraCanvas::ListItemStyleOption& option) override;
    int GetRowHeight(const UltraCanvas::IListModel* model, int row) const override;

    void SetViewWidth(int width);

private:
    int BubbleMaxWidth() const;
    int EstimateHeight(const MessageRow& row, bool showSender) const;

    int viewWidth_ = 600;
    std::map<std::string, int> measured_;   // client id -> height at viewWidth_
};

// ============================================================================
// CONVERSATIONS
// ============================================================================

class ConversationListModel : public UltraCanvas::IListModel {
public:
    void SetConversations(std::vector<UltraIMConversation> conversations);
    // Inserts or replaces one, keeping newest first. Returns its row.
    int Upsert(const UltraIMConversation& conversation);
    bool Remove(const std::string& conversationId);
    void SetPresence(const std::string& contactId, UltraIMPresence presence);
    int  FindRow(const std::string& conversationId) const;
    const UltraIMConversation* Find(const std::string& conversationId) const;
    const std::vector<UltraIMConversation>& Conversations() const { return list_; }

    int GetRowCount() const override { return static_cast<int>(list_.size()); }
    int GetColumnCount() const override { return 1; }
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }

private:
    void Sort();
    std::vector<UltraIMConversation> list_;
};

// One row: a presence dot, the title, the last message, its time and the
// unread count.
class ConversationDelegate : public UltraCanvas::IItemDelegate {
public:
    void RenderItem(UltraCanvas::IRenderContext* ctx, const UltraCanvas::IListModel* model,
                    int row, int column, const UltraCanvas::ListItemStyleOption& option) override;
    int GetRowHeight(const UltraCanvas::IListModel*, int) const override { return 56; }
};

} // namespace ChatCube
