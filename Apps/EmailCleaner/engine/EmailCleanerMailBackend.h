// Apps/EmailCleaner/engine/EmailCleanerMailBackend.h
// The half of an action that leaves the machine: moving a message to Trash on
// the IMAP server, and sending an unsubscribe request.
//
// It implements IActionBackend over UltraNet's mailbox plug-in — the same
// interface UltraMail's SyncEngine drives — so the whole path can be tested
// against a fake mailbox with no server, and so a live run and a test run
// exercise the same code.
//
// Trash resolution matters and is done here rather than assumed: the folder
// carrying the Trash special-use role is asked for by name from the account's
// folder list, because "Trash" is not what every server calls it (Gmail uses
// "[Gmail]/Bin", others "Deleted Items"). If no Trash can be identified the
// move is refused rather than guessed at — a wrong guess would scatter mail
// into a folder the user never looks at.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerActions.h"

#include <UltraNet/UltraNetPlugins.h>

#include <map>
#include <string>

namespace EmailCleaner {

// What the backend needs to reach one account's mailbox.
struct MailAccountAccess {
    std::string accountId;
    std::string serverUrl;              // "imaps://host:993/"
    std::string ownerAddress;           // the account's own address, for the From
    UltraNetMailOptions options;        // credentials, TLS, timeouts
    std::string trashFolder;            // resolved lazily when empty
};

class MailBackend : public IActionBackend {
public:
    // `mailbox` is UltraNet's IMAP plug-in (or a fake, in tests).
    explicit MailBackend(IMailboxProtocolPlugin& mailbox) : mailbox_(mailbox) {}

    // Register how to reach an account. Without this, actions on that
    // account's mail fail with a clear message rather than silently.
    void SetAccount(const MailAccountAccess& access);

    // IActionBackend
    bool MoveToTrash(const std::string& accountId, const std::string& folder,
                     int64_t uid, std::string& outError) override;
    bool PostOneClick(const std::string& url, std::string& outError) override;
    bool SendUnsubscribeMail(const std::string& accountId, const std::string& address,
                             const std::string& subject, std::string& outError) override;

    // The folder this account's Trash resolved to (empty until a move ran, or
    // when none could be identified). Exposed for the status line and tests.
    std::string ResolvedTrash(const std::string& accountId) const;

    // Find the Trash folder for an account: the SPECIAL-USE role first, then
    // the well-known names. Empty when nothing matches. Exposed for testing.
    std::string ResolveTrashFolder(const MailAccountAccess& access, std::string& outError);

    // True when the name is one servers commonly use for Trash. Pure.
    static bool LooksLikeTrash(const std::string& folderName);

private:
    MailAccountAccess* Find(const std::string& accountId);

    IMailboxProtocolPlugin&                mailbox_;
    std::map<std::string, MailAccountAccess> accounts_;
};

} // namespace EmailCleaner
