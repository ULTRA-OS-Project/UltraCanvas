// Apps/UltraMail/engine/UltraMailTypes.h
// Core data types for the UltraMail engine: accounts, folders, message
// envelopes, message flags, and the per-account status rollup that drives the
// account bar (unread today · unread before · waiting for reply).
// Version: 0.3.0 - server settings (IMAP/SMTP host, port, security, username)
//                  stored on the account
// Last Modified: 2026-09-10
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

// Connection security for a mail server.
enum class MailSecurity {
    Plain = 0,    // plaintext (discouraged); not "None" — X11 defines that macro
    StartTls,     // upgrade on the plaintext port
    SslTls        // implicit TLS from connect
};

// "none" | "starttls" | "ssl" — the form stored in the database.
std::string  ToString(MailSecurity security);
MailSecurity MailSecurityFromString(const std::string& s);

struct MailServerSettings {
    std::string  host;
    int          port = 0;
    MailSecurity security = MailSecurity::SslTls;
    std::string  username;     // resolved (full address or local-part)
    bool         oauth = false; // provider expects OAuth2/XOAUTH2

    bool Valid() const { return !host.empty() && port > 0; }
};

// A configured account as the local store knows it (no secrets here —
// credentials live in the OS keychain via the credential vault).
struct Account {
    std::string accountId;    // stable slug, e.g. "erika-example-com"
    std::string displayName;  // "Erika Example"
    std::string email;        // "erika@example.com"
    std::string shortName;    // nickname for the info tile, e.g. "erika"

    // The servers the account syncs and sends with, stored once discovered
    // (provider table, autoconfig) or entered by hand. Empty on accounts
    // created before they were stored — those fall back to the provider table.
    MailServerSettings imap;
    MailServerSettings smtp;
    std::string        providerName;   // "Gmail", an autoconfig display name, or ""

    bool HasServers() const { return imap.Valid() && smtp.Valid(); }
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

// Per-account rollup for the account bar (single-account summary / tiles).
struct AccountStatus {
    std::string accountId;
    std::string shortName;
    std::string email;
    int unread = 0;       // unseen, non-deleted messages in the inbox (all)
    int unreadToday = 0;  // ... of those, dated today (local midnight onwards)
    int unreadOlder = 0;  // ... of those, dated before today
    int needsAnswer = 0;  // messages addressed to the user awaiting a reply
};

} // namespace UltraMail
