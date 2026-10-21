// ChatCube/core/ChatMessageModel.cpp
// Implementation of the chat message ListView model
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework

#include "ChatMessageModel.h"
#include <algorithm>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ChatMessageModel::ChatMessageModel() = default;

// ============================================================================
// IListModel — Row / Column counts
// ============================================================================

int ChatMessageModel::GetRowCount() const {
    return static_cast<int>(messages_.size());
}

int ChatMessageModel::GetColumnCount() const {
    // Always single-column: the ChatBubbleDelegate renders the full row
    return 1;
}

// ============================================================================
// IListModel — GetData
// Maps each role to the corresponding ChatMessage field
// ============================================================================

ListDataValue ChatMessageModel::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= static_cast<int>(messages_.size())) {
        return std::monostate{};
    }

    const ChatMessage& msg = messages_[index.row];

    // ── Standard roles ─────────────────────────────────────────────────────
    if (role == ListDataRole::DisplayRole) {
        // Primary text. For media rows this may be empty — delegate handles it.
        return msg.Text;
    }

    if (role == ListDataRole::DecorationRole) {
        // Sender avatar image path (loaded by delegate via ctx->DrawImage)
        return msg.SenderAvatarPath;
    }

    if (role == ListDataRole::ToolTipRole) {
        // Full timestamp as tooltip (e.g. "Tuesday, 13 April 2026 09:14")
        return msg.TimestampDisplay;
    }

    // ── Custom ChatCube roles ───────────────────────────────────────────────
    if (role == ChatMsgRole_SenderName)       return msg.SenderName;
    if (role == ChatMsgRole_SenderId)         return msg.SenderId;
    if (role == ChatMsgRole_MessageId)        return msg.MessageId;

    if (role == ChatMsgRole_MessageType)
        return MessageTypeToInt(msg.Type);

    if (role == ChatMsgRole_TimestampDisplay) return msg.TimestampDisplay;

    if (role == ChatMsgRole_DeliveryStatus)
        return DeliveryStatusToInt(msg.DeliveryStatus);

    if (role == ChatMsgRole_IsOwn)
        return msg.IsOwn ? 1 : 0;

    // Attachment fields
    if (role == ChatMsgRole_AttachPath)       return msg.Attachment.LocalPath;
    if (role == ChatMsgRole_AttachFileName)   return msg.Attachment.FileName;
    if (role == ChatMsgRole_AttachSize)       return msg.Attachment.GetDisplaySize();
    if (role == ChatMsgRole_AttachWidth)      return msg.Attachment.ImageWidth;
    if (role == ChatMsgRole_AttachHeight)     return msg.Attachment.ImageHeight;

    // Date separator / system event
    if (role == ChatMsgRole_DateLabel)        return msg.DateLabel;
    if (role == ChatMsgRole_SystemEventText)  return msg.SystemEventText;

    // Reply context
    if (role == ChatMsgRole_ReplySnippet)     return msg.ReplyContext.OriginalTextSnippet;
    if (role == ChatMsgRole_ReplySenderName)  return msg.ReplyContext.OriginalSenderName;

    // Forwarded
    if (role == ChatMsgRole_ForwardedFrom)    return msg.ForwardedFrom;

    return std::monostate{};
}

// ============================================================================
// IListModel — SetData
// Only delivery status updates are supported via SetData.
// All other fields are updated through dedicated methods below.
// ============================================================================

bool ChatMessageModel::SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) {
    if (index.row < 0 || index.row >= static_cast<int>(messages_.size())) {
        return false;
    }

    if (role == ChatMsgRole_DeliveryStatus) {
        const int* intVal = std::get_if<int>(&value);
        if (!intVal) return false;
        messages_[index.row].DeliveryStatus = static_cast<ChatDeliveryStatus>(*intVal);
        NotifyRowChanged(index.row);
        return true;
    }

    // Attachment download completion: update local path
    if (role == ChatMsgRole_AttachPath) {
        const std::string* strVal = std::get_if<std::string>(&value);
        if (!strVal) return false;
        messages_[index.row].Attachment.LocalPath  = *strVal;
        messages_[index.row].Attachment.IsDownloaded = true;
        NotifyRowChanged(index.row);
        return true;
    }

    return false;
}

// ============================================================================
// MESSAGE MANAGEMENT
// ============================================================================

void ChatMessageModel::AppendMessage(const ChatMessage& message) {
    messages_.push_back(message);
    const int newRow = static_cast<int>(messages_.size()) - 1;
    NotifyRowInserted(newRow);
}

void ChatMessageModel::PrependHistory(const std::vector<ChatMessage>& history) {
    if (history.empty()) return;

    // Insert at front — reverse to preserve chronological order
    messages_.insert(messages_.begin(), history.begin(), history.end());

    // Notify view to full-reload (scroll anchor preserved by the view on rebuild)
    NotifyDataChanged();

    if (onHistoryLoaded) {
        onHistoryLoaded(static_cast<int>(history.size()));
    }
}

void ChatMessageModel::SetMessages(const std::vector<ChatMessage>& messages) {
    messages_ = messages;
    NotifyDataChanged();
}

void ChatMessageModel::Clear() {
    messages_.clear();
    NotifyDataChanged();
}

bool ChatMessageModel::UpdateDeliveryStatus(const std::string& messageId, ChatDeliveryStatus status) {
    const int row = FindRowByMessageId(messageId);
    if (row < 0) return false;

    messages_[row].DeliveryStatus = status;
    NotifyRowChanged(row);
    return true;
}

// ============================================================================
// ACCESSORS
// ============================================================================

const ChatMessage* ChatMessageModel::GetMessage(int row) const {
    if (row < 0 || row >= static_cast<int>(messages_.size())) return nullptr;
    return &messages_[row];
}

int ChatMessageModel::GetMessageCount() const {
    return static_cast<int>(messages_.size());
}

int ChatMessageModel::FindRowByMessageId(const std::string& messageId) const {
    for (int i = 0; i < static_cast<int>(messages_.size()); ++i) {
        if (messages_[i].MessageId == messageId) return i;
    }
    return -1;
}

// ============================================================================
// PRIVATE HELPERS — enum to int conversions for ListDataValue (int variant)
// ============================================================================

int ChatMessageModel::MessageTypeToInt(ChatMessageType type) {
    return static_cast<int>(type);
}

int ChatMessageModel::DeliveryStatusToInt(ChatDeliveryStatus status) {
    return static_cast<int>(status);
}

} // namespace ChatCube
} // namespace UltraCanvas
