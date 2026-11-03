// ChatCube/include/ChatMessageModel.h
// ListView model for the chat message area
// Provides ChatMessage data to the ListView via the IListModel interface
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasListModel.h"
#include "ChatCubeTypes.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <algorithm>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// CUSTOM DATA ROLES
// Extended from ListDataRole::UserRole (= 256) following the pattern
// established in UltraCanvasDropdown.h
// ============================================================================

// Roles for ChatMessageModel (UserRole + 0..N)
// Note: DisplayRole (0) = message text, DecorationRole (1) = sender avatar path
constexpr auto ChatMsgRole_SenderName       = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 0);
constexpr auto ChatMsgRole_SenderId         = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 1);
constexpr auto ChatMsgRole_MessageType      = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 2);  // int (ChatMessageType)
constexpr auto ChatMsgRole_TimestampDisplay = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 3);  // string "HH:MM"
constexpr auto ChatMsgRole_DeliveryStatus   = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 4);  // int (ChatDeliveryStatus)
constexpr auto ChatMsgRole_IsOwn            = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 5);  // int 0/1
constexpr auto ChatMsgRole_AttachPath       = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 6);  // string local path
constexpr auto ChatMsgRole_AttachFileName   = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 7);  // string display name
constexpr auto ChatMsgRole_AttachSize       = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 8);  // string "7.23 MB"
constexpr auto ChatMsgRole_AttachWidth      = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 9);  // int pixels
constexpr auto ChatMsgRole_AttachHeight     = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 10); // int pixels
constexpr auto ChatMsgRole_DateLabel        = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 11); // string "DD.MM.YYYY"
constexpr auto ChatMsgRole_SystemEventText  = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 12); // string
constexpr auto ChatMsgRole_ReplySnippet     = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 13); // string quoted text
constexpr auto ChatMsgRole_ReplySenderName  = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 14); // string
constexpr auto ChatMsgRole_ForwardedFrom    = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 15); // string
constexpr auto ChatMsgRole_MessageId        = static_cast<ListDataRole>(static_cast<int>(ListDataRole::UserRole) + 16); // string (for action callbacks)

// ============================================================================
// ChatMessageModel
// Wraps a vector<ChatMessage> and exposes it to ListView via IListModel
// ============================================================================

class ChatMessageModel : public IListModel {
public:
    ChatMessageModel();
    ~ChatMessageModel() override = default;

    // ===== IListModel interface =====

    int GetRowCount() const override;
    int GetColumnCount() const override;

    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override;

    // SetData: supports updating delivery status on existing messages
    bool SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) override;

    // ===== Message management =====

    // Append a new message to the bottom of the conversation
    // Triggers onRowInserted so the ListView auto-scrolls
    void AppendMessage(const ChatMessage& message);

    // Insert messages in bulk (e.g. loading history — inserts at top)
    // Does NOT trigger individual row callbacks; fires onDataChanged once
    void PrependHistory(const std::vector<ChatMessage>& history);

    // Replace all messages (switching conversations)
    void SetMessages(const std::vector<ChatMessage>& messages);

    // Clear all messages (switching conversations or logout)
    void Clear();

    // Update delivery status on an existing message (matched by MessageId)
    // Fires onRowChanged for the matched row
    bool UpdateDeliveryStatus(const std::string& messageId, ChatDeliveryStatus status);

    // Retrieve a full ChatMessage struct by row index (used by ChatBubbleDelegate)
    const ChatMessage* GetMessage(int row) const;

    // Row count helper
    int GetMessageCount() const;

    // ===== Sorting helpers =====

    // Returns the row index for a given MessageId, or -1 if not found
    int FindRowByMessageId(const std::string& messageId) const;

    // ===== Callbacks (additional to IListModel base callbacks) =====

    // Fired when PrependHistory completes (view should re-anchor scroll)
    std::function<void(int insertedCount)> onHistoryLoaded;

private:
    std::vector<ChatMessage> messages_;

    // Internal helper: converts ChatMessageType to int for ListDataValue
    static int MessageTypeToInt(ChatMessageType type);
    static int DeliveryStatusToInt(ChatDeliveryStatus status);
};

} // namespace ChatCube
} // namespace UltraCanvas
