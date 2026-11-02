// Apps/UltraMail/engine/UltraMailTypes.h
// Core data types for the UltraMail engine: accounts, folders, message
// envelopes, message flags, and the per-account status rollup that drives the
// account bar (unread today · unread before · waiting for reply).
// Version: 0.7.0 - AccountSecurity: how an account's new messages start out signed and encrypted
// Version: 0.6.1 - HasServers: a JMAP account needs no outgoing server
// Version: 0.6.0 - the protocol of the incoming server (IMAP or JMAP)
// Version: 0.5.0 - a signature per account (plain text or HTML)
// Version: 0.4.0 - the authentication method per server
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraNet/UltraNetPlugins.h>   // UltraNetMailAuth, UltraNetMailOptions

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

// The protocol an incoming mail server speaks. IMAP is what every account
// created before the choice existed uses; JMAP (RFC 8620/8621, JSON over
// HTTPS - Fastmail, Stalwart, Cyrus, Apache James) reaches the same mailbox
// through the UltraNet JMAP plug-in. Sending is SMTP either way.
enum class MailProtocol {
    Imap = 0,
    Jmap
};

// "imap" | "jmap" - the form stored in the database. Unknown text reads as
// Imap.
std::string  ToString(MailProtocol protocol);
MailProtocol MailProtocolFromString(const std::string& s);

// Connection security for a mail server.
enum class MailSecurity {
    Plain = 0,    // plaintext (discouraged); not "None" — X11 defines that macro
    StartTls,     // upgrade on the plaintext port
    SslTls        // implicit TLS from connect
};

// The port a server of that protocol usually listens on: 993 (IMAP, implicit
// TLS), 143 (IMAP, STARTTLS or plain), 443 (JMAP: HTTPS, whatever the
// security choice says).
int DefaultPort(MailProtocol protocol, MailSecurity security);

// "none" | "starttls" | "ssl" — the form stored in the database.
std::string  ToString(MailSecurity security);
MailSecurity MailSecurityFromString(const std::string& s);

// The security that actually matches a well-known port, reconciling a stored
// setting that contradicts it. RFC 8314: 465 = implicit TLS (SMTPS), 587/25 =
// STARTTLS submission/relay; 993 = implicit IMAPS, 143 = STARTTLS IMAP. Implicit
// TLS on a STARTTLS port makes libcurl send a TLS ClientHello before the SMTP/
// IMAP greeting — postfix logs "improper command pipelining" and fail2ban bans
// for it. The port is the authoritative signal of what the server speaks, so a
// choice that contradicts a well-known port follows the port; a non-standard
// port (and an explicit Plain choice) is left untouched.
MailSecurity EffectiveSecurity(int port, MailSecurity chosen);

// The authentication method of a server, as the server settings page offers
// it: "auto" | "password" | "encrypted" | "oauth2" | "kerberos" | "ntlm" |
// "none" — the form stored in the database. Unknown text reads as Any.
std::string      ToString(UltraNetMailAuth auth);
UltraNetMailAuth MailAuthFromString(const std::string& s);

struct MailServerSettings {
    // What the server speaks. Read for the incoming server only (an outgoing
    // server is SMTP); Imap on every account from before the choice existed.
    MailProtocol protocol = MailProtocol::Imap;
    std::string  host;
    int          port = 0;
    // For a JMAP server this is always SslTls: JMAP runs over HTTPS, so the
    // settings page fixes it there, and ApplyConnection reads it as such.
    MailSecurity security = MailSecurity::SslTls;
    std::string  username;     // resolved (full address or local-part)
    bool         oauth = false; // provider expects OAuth2/XOAUTH2
    // How the session signs in. Any (shown as "Automatic") lets the server's
    // offer decide — what every account created before the setting existed
    // keeps; the provider table sets OAuth2 for Gmail, Outlook and Yahoo.
    UltraNetMailAuth auth = UltraNetMailAuth::Any;

    bool Valid() const { return !host.empty() && port > 0; }
};

// The connection part of a session's options for `server`: TLS mode from its
// security setting (always implicit TLS for a JMAP server - HTTPS) and its
// authentication method. Credentials are left alone.
void ApplyConnection(const MailServerSettings& server, UltraNetMailOptions& options);

// Which signature an account puts under its messages.
enum class SignatureKind {
    Off = 0,    // no signature (not "None": X11 defines that macro)
    Text,       // Signature::text, below a "-- " line
    Html        // Signature::html, formatted (the message is then sent as HTML)
};

// "none" | "text" | "html" — the form stored in the database. Unknown text
// reads as Off.
std::string   ToString(SignatureKind kind);
SignatureKind SignatureKindFromString(const std::string& s);

// An account's signature. Both versions are kept whichever one is chosen, so
// switching between plain text and HTML in the editor loses neither.
struct Signature {
    SignatureKind kind = SignatureKind::Off;
    std::string   text;           // plain text, UTF-8, lines separated by '\n'
    std::string   html;           // an HTML fragment; pictures inline as data: URIs
    bool          onReplies = true;   // also under replies and forwards, not only new mail

    // True when the chosen kind has something to show.
    bool IsActive() const;
    bool operator==(const Signature& other) const {
        return kind == other.kind && text == other.text && html == other.html
            && onReplies == other.onReplies;
    }
};

// How the account's new messages start out signed and encrypted
// (UltraMailSecureMime.h) - the compose window's Security row is set from it,
// and stays the writer's to change per message.
struct AccountSecurity {
    std::string method;          // "S/MIME", "OpenPGP", "" (off)
    bool        sign = true;     // with a method: signed
    bool        encrypt = false; // with a method: encrypted (every recipient then needs a key)
    // OpenPGP: the fingerprint of the secret key to sign with; "" takes the
    // one whose user id is the account's address.
    std::string openPgpKey;

    bool operator==(const AccountSecurity& other) const {
        return method == other.method && sign == other.sign && encrypt == other.encrypt
            && openPgpKey == other.openPgpKey;
    }
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
    // `imap` is the incoming server whatever it speaks: its `protocol` says
    // IMAP or JMAP (the member keeps its name from before the choice existed).
    MailServerSettings imap;
    MailServerSettings smtp;
    std::string        providerName;   // "Gmail", an autoconfig display name, or ""

    // Put under the messages the account writes. LocalStore::UpsertAccount
    // leaves it alone (a re-added address keeps its signature); it is saved
    // with LocalStore::SetAccountSignature.
    Signature          signature;

    // How its new messages start out signed and encrypted. Like the
    // signature, UpsertAccount leaves it alone; LocalStore::SetAccountSecurity
    // saves it.
    AccountSecurity    security;

    // Whether the account can sync and send: an incoming server, and an
    // outgoing one unless the incoming one is JMAP, which sends by itself
    // (an SMTP server given next to it is used instead).
    bool HasServers() const {
        return imap.Valid() && (smtp.Valid() || imap.protocol == MailProtocol::Jmap);
    }
    // Whether mail is sent through the JMAP server: a JMAP incoming server
    // and no outgoing (SMTP) one.
    bool SendsThroughJmap() const {
        return imap.protocol == MailProtocol::Jmap && !smtp.Valid();
    }
};

// A mailbox folder within an account.
struct Folder {
    std::string accountId;
    std::string name;                 // full IMAP path, e.g. "INBOX"
    FolderRole  role = FolderRole::Normal;
    int64_t     uidValidity = 0;
    int64_t     uidNext = 0;
    // False for \Noselect container folders (e.g. Gmail's "[Gmail]"): they hold
    // no mail and are elided from the folder tree (their children are promoted).
    bool        selectable = true;
    // The hierarchy separator the server listed for it ("/", or "." on
    // Courier-style servers whose folders are "INBOX.Drafts"); "" until the
    // folder list has been fetched since it was stored (UltraMailFolderNames
    // then works it out from the names).
    std::string delimiter;
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
