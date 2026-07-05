// Apps/UltraMail/engine/UltraMailTypes.h
// Core data types for the UltraMail engine: accounts, folders, message
// envelopes, message flags, and the per-account status rollup that drives the
// account info-tile bar (short name · unread · needs-answer).
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraMail {

// IMAP-style message flags, stored as a bitfield. \Answered is the anchor for
// the "needs answer" feature — set when the user sends a reply.
enum MessageFlag : uint32_t {
    Flag_None     = 0,
    Flag_Seen     = 1u << 0,   // \Seen
    Flag_Answered = 1u << 1,   // \Answered
    Flag_Flagged  = 1u << 2,   // \Flagged (starred)
    Flag_Deleted  = 1u << 3,   // \Deleted
    Flag_Draft    = 1u << 4    // \Draft
};

// Special-use role of a folder (SPECIAL-USE / name-based detection).
enum class FolderRole {
    Normal = 0,
    Inbox,
    Sent,
    Drafts,
    Junk,
    Trash,
    Archive
};

std::string ToString(FolderRole role);
FolderRole  FolderRoleFromString(const std::string& s);

// A configured account as the local store knows it (no secrets here —
// credentials live in the OS keychain via the credential vault).
struct Account {
    std::string accountId;    // stable slug, e.g. "erika-example-com"
    std::string displayName;  // "Erika Example"
    std::string email;        // "erika@example.com"
    std::string shortName;    // nickname for the info tile, e.g. "erika"
};

// A mailbox folder within an account.
struct Folder {
    std::string accountId;
    std::string name;                 // full IMAP path, e.g. "INBOX"
    FolderRole  role = FolderRole::Normal;
    int64_t     uidValidity = 0;
    int64_t     uidNext = 0;
};

// The header-level view of a message kept in the index (bodies stay in .eml
// files on disk; this is what the message list and rollups run on).
struct MessageEnvelope {
    std::string accountId;
    std::string folder;
    int64_t     uid = 0;

    std::string messageId;            // RFC 5322 Message-ID
    std::string inReplyTo;            // In-Reply-To (for reply matching)
    std::string subject;
    std::string fromName;
    std::string fromAddr;
    std::vector<std::string> to;      // direct recipients (To:)
    int64_t     date = 0;             // epoch seconds
    uint32_t    flags = Flag_None;

    // True if the message is bulk / list / auto-submitted (excluded from
    // "needs answer"). Set by the caller from header inspection.
    bool automated = false;
};

// Per-account rollup for the info-tile bar.
struct AccountStatus {
    std::string accountId;
    std::string shortName;
    int unread = 0;       // unseen, non-deleted messages in the inbox
    int needsAnswer = 0;  // messages addressed to the user awaiting a reply
};

} // namespace UltraMail
