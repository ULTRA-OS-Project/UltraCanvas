// Apps/EmailCleaner/engine/EmailCleanerAccounts.h
// EmailCleaner's own mail accounts, beside the ones it shares with UltraMail.
//
// Up to 0.4 every account came from UltraMail: EmailCleaner mirrored its
// account list and read the bodies its sync engine had cached. That still
// works and stays the default — but it meant a mailbox could only be cleaned
// after it had been set up in a mail client. An account added *here* is
// EmailCleaner's alone:
//
//   <EmailCleaner data dir>/accounts.db     the account list, in UltraMail's
//                                           LocalStore schema (so the same
//                                           SyncEngine drives it)
//   <EmailCleaner data dir>/vault/          its passwords (OwnCredentialVault)
//   <EmailCleaner data dir>/mail/<account>/<folder>/<uid>.eml
//                                           the bodies it fetched itself
//
// Nothing here writes to UltraMail's files, and UltraMail never sees these
// accounts. The two lists meet only in the analysis database, where each row
// says which one it came from (AccountSource).
//
// An own account's id is "ec-" + the address slug, so it can never collide
// with UltraMail's id for the same address — and adding an address UltraMail
// already shares is refused, because it would load the same mailbox twice.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerStore.h"

#include "UltraMailDiscovery.h"
#include "UltraMailLocalStore.h"
#include "UltraMailSyncEngine.h"

#include <UltraNet/UltraNetPlugins.h>

#include <string>
#include <vector>

namespace EmailCleaner {

// The id an account added in EmailCleaner gets for `email`: "ec-" followed by
// the address with every non-alphanumeric character turned into '-'.
std::string OwnAccountIdFor(const std::string& email);

// True for an id OwnAccountIdFor() produced.
bool IsOwnAccountId(const std::string& accountId);

// What the "Add account" form hands over.
struct NewAccountRequest {
    std::string email;
    std::string displayName;               // empty -> the address's local part
    std::string password;                  // an app password for OAuth providers
    UltraMail::DiscoveryResult settings;   // incoming (and outgoing) servers
};

// The account list an own account is saved to, and the mailbox it syncs into.
class OwnAccounts {
public:
    // Open (creating when absent) the account list under `dataDir`. The
    // connection name is a parameter so a test can use its own.
    UltraDbResult Open(const std::string& dataDir,
                       const std::string& connectionName = "emailcleaner-own-accounts");

    bool IsOpen() const { return store_.IsOpen(); }

    // Where the fetched bodies live: <dataDir>/mail.
    const std::string& MailCacheDir() const { return mailCacheDir_; }
    // Where the passwords live: <dataDir>/vault.
    const std::string& VaultDir() const { return vaultDir_; }

    UltraDbResult List(std::vector<UltraMail::Account>& out) const;
    bool Find(const std::string& accountId, UltraMail::Account& out) const;

    // Save a new or edited account. The password is not part of it — the
    // caller stores that in OwnCredentialVault under the same id.
    UltraDbResult Save(const UltraMail::Account& account);

    // Drop the account and everything fetched for it: its folders and message
    // index in the account list, and its cached bodies on disk. The vault
    // entry and the analysis rows are the caller's (they live elsewhere).
    UltraDbResult Remove(const std::string& accountId);

    // The account list's LocalStore, for the SyncEngine.
    UltraMail::LocalStore& Store() { return store_; }

    // ---- Pure helpers ------------------------------------------------------

    // Why `request` cannot become an account, or "" when it can. `known` is
    // every account EmailCleaner already has, from either source.
    static std::string Validate(const NewAccountRequest& request,
                                const std::vector<StoredAccount>& known);

    // The account record for a validated request. A password account at a
    // provider whose table entry says OAuth2 (Gmail, Outlook, Yahoo) signs in
    // with that password — an app password — so the method is left to the
    // server rather than insisting on a token EmailCleaner does not have.
    static UltraMail::Account MakeAccount(const NewAccountRequest& request);

private:
    UltraMail::LocalStore store_;
    std::string           mailCacheDir_;
    std::string           vaultDir_;
};

// The analysis database's view of a mail account.
StoredAccount ToStoredAccount(const UltraMail::Account& account, AccountSource source);

// The session options an own account signs in with: TLS mode and method from
// its incoming server, the server's username (else the address) and password.
UltraNetMailOptions SessionOptionsFor(const UltraMail::Account& account,
                                      const std::string& password);

// Download what EmailCleaner analyses for one account into its own cache: the
// folder list, then the inbox and — when the server has one — the junk folder,
// envelopes and bodies, incrementally (only UIDs above the highest stored).
// Blocking on the network; run it off the UI thread.
UltraMail::SyncOutcome FetchMailbox(UltraMail::LocalStore& store,
                                    IMailboxProtocolPlugin& mailbox,
                                    const std::string& mailCacheDir,
                                    const UltraMail::Account& account,
                                    const UltraNetMailOptions& options);

} // namespace EmailCleaner
