// Plugins/UltraIM/direct/DirectStore.h
// ChatCube Direct - the device's own record: contacts and their keys,
// conversations, message history and the outbox (SQLite via UltraDatabase)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraIM/UltraIMTypes.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace UltraIMDirect {

struct StoredContact {
    std::string     userId;
    std::string     displayName;
    std::string     publicKey;      // Ed25519, base64
    UltraIMPresence presence = UltraIMPresence::Unknown;
};

// A message not yet confirmed by any path (data channel ack or relay queue).
struct OutboxEntry {
    std::string clientId;
    std::string conversation;
    std::string recipient;          // one row per recipient (a group has several)
    std::string body;               // plaintext body JSON, sealed when sent
    int64_t     timestampMs = 0;
    int         attempts = 0;
};

class Store {
public:
    ~Store();

    // Opens (and migrates) the database file, UTF-8 path; ":memory:" works.
    bool Open(const std::string& path, const std::string& providerId, std::string& error);
    void Close();

    // ---- settings -----------------------------------------------------------
    std::string GetSetting(const std::string& key);
    void        SetSetting(const std::string& key, const std::string& value);

    // ---- contacts -----------------------------------------------------------
    void UpsertContact(const StoredContact& contact);
    bool GetContact(const std::string& userId, StoredContact& out);
    std::vector<StoredContact> ListContacts();

    // ---- conversations ------------------------------------------------------
    void UpsertConversation(const UltraIMConversation& conversation);
    bool GetConversation(const std::string& id, UltraIMConversation& out);
    std::vector<UltraIMConversation> ListConversations();
    bool RemoveConversation(const std::string& id);
    void ClearUnread(const std::string& conversationId);

    // ---- messages -----------------------------------------------------------
    // False when the conversation already holds this client id (a message
    // that arrived by two paths, or a resend).
    bool AddMessage(const UltraIMMessage& message);
    bool UpdateStatus(const std::string& conversationId, const std::string& clientId,
                      UltraIMDeliveryStatus status);
    bool FindMessage(const std::string& conversationId, const std::string& clientId, UltraIMMessage& out);
    // Oldest first; beforeClientId "" = the newest page.
    std::vector<UltraIMMessage> Messages(const std::string& conversationId,
                                         const std::string& beforeClientId, int limit);
    // Own messages in the conversation that the peer has not read yet, and
    // incoming ones not reported read.
    std::vector<std::string> UnreadIncoming(const std::string& conversationId);

    // ---- outbox -------------------------------------------------------------
    void AddOutbox(const OutboxEntry& entry);
    void RemoveOutbox(const std::string& clientId, const std::string& recipient);
    std::vector<OutboxEntry> Outbox(const std::string& recipient = std::string());

private:
    std::string connection_;
    std::string providerId_ = "direct";
    std::mutex  mutex_;
};

} // namespace UltraIMDirect
