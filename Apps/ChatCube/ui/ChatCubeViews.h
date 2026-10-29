// Apps/ChatCube/ui/ChatCubeViews.h
// The two lists of the ChatCube window: the conversations on the left, the
// messages of the open conversation on the right (chat bubbles, date
// separators, delivery ticks). Models adapt UltraIM data to
// UltraCanvasListView; delegates paint one row's content - the list view
// paints selection, hover and scrolling.
// Version: 0.2.1 - pictures: avatars from the person's photo, photos and video previews in the bubbles,
//                  a sender's avatar beside their bubble in a group, files as their name and size
// Version: 0.2.0 - avatars, a rounded selection, the theme's colours
// Version: 0.1.0
// Last Modified: 2026-10-29
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
// "0:12" / "1:05:09" for a video's or a voice message's length.
std::string FormatDuration(double seconds);
// "840 bytes" / "12 KB" / "1.2 MB" for a file's size.
std::string FormatSize(int64_t bytes);
// A photo, a video or a sticker: shown as a picture in its bubble.
bool HasPicture(const UltraIMMessage& message);
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
    // Whether a message is shown, by its client id.
    bool Contains(const std::string& clientId) const { return FindRow(clientId) >= 0; }

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
// sender's name and avatar in groups; a photo as its picture, a video as its
// preview picture under a play mark with its length, a file as its name and
// size (a picture still on its way is a box that says so); the caption under
// the picture; the time and, for own messages, the ticks
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

    // Where a row's attachment was last painted - the picture of a photo,
    // a video or a sticker, the whole bubble of a file or a voice message -
    // relative to the row's top left (the space of a click's posInCell).
    // False for a row with nothing to open.
    bool MediaRect(const UltraCanvas::IListModel* model, int row, UltraCanvas::Rect2Di& outRect) const;

private:
    int BubbleMaxWidth() const;
    int EstimateHeight(const MessageRow& row, bool showSender) const;
    // The size a message's picture is shown at: the attachment's dimensions
    // (or the file's, or a 4:3 box while they are unknown) scaled into the bubble.
    UltraCanvas::Size2Di PictureSize(const UltraIMMessage& message, bool showSender) const;

    int viewWidth_ = 600;
    std::map<std::string, int> measured_;               // client id -> height at viewWidth_
    std::map<std::string, UltraCanvas::Rect2Di> media_; // client id -> what MediaRect answers
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

// One row: the avatar (initials on the person's colour, a presence dot at
// its corner; a group mark for a group), the title, the last message, its
// time and the unread count, on a rounded tint when selected or hovered.
class ConversationDelegate : public UltraCanvas::IItemDelegate {
public:
    void RenderItem(UltraCanvas::IRenderContext* ctx, const UltraCanvas::IListModel* model,
                    int row, int column, const UltraCanvas::ListItemStyleOption& option) override;
    int GetRowHeight(const UltraCanvas::IListModel*, int) const override;
};

} // namespace ChatCube
