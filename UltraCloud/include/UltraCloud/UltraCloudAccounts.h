// UltraCloud/include/UltraCloud/UltraCloudAccounts.h
// The cloud account store: the list of configured accounts and which one is
// the default, persisted on UltraDatabase (SQLite). Shared by every app that
// links UltraCloud, so a cloud account is set up once. It also keeps what
// CloudService needs to know about uploads across runs: the ones whose
// leftovers are to be removed if they do not finish, and the accounts whose
// server would not rename an upload into place (UltraCloudUploads.h).
// Version: 0.2.0 - pending uploads; accounts that upload straight to the name
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudTypes.h"
#include "UltraCloudUploads.h"

#include <string>
#include <vector>

namespace UltraCloud {

// A stable id for an account: "<provider>-<user>-<host>" slug.
std::string MakeAccountId(const std::string& providerId, const std::string& username,
                          const std::string& serverUrl);

class AccountStore {
public:
    // Register an UltraDatabase connection and bring the schema up to date.
    // `databasePath` is a file (created if absent) or ":memory:".
    Result Open(const std::string& connectionName, const std::string& databasePath);
    bool IsOpen() const { return !connection_.empty(); }

    // Insert or update. The first account stored becomes the default.
    Result Upsert(const Account& account);
    Result Remove(const std::string& accountId);
    Result Get(const std::string& accountId, Account& out) const;
    Result List(std::vector<Account>& out) const;   // default first, then by name

    Result SetDefault(const std::string& accountId);
    Result GetDefault(Account& out) const;          // NotFound when there is none

    // ---- Uploads (UltraCloudUploads.h) ----------------------------------------
    // One record per account and path: adding one for a path already there
    // replaces it. Removing an account removes its records too.
    Result AddPendingUpload(const PendingUpload& upload);
    Result RemovePendingUpload(const std::string& accountId, const std::string& remotePath);
    // The account's records started within kPendingUploadMaxAgeSeconds of
    // `now`, oldest first; older ones are deleted.
    Result ListPendingUploads(const std::string& accountId, int64_t now,
                              std::vector<PendingUpload>& out);
    // The account's server refused to rename an upload into place, at the
    // address `serverUrl`: its uploads go straight to their names.
    Result SetUploadsDirect(const std::string& accountId, const std::string& serverUrl,
                            int64_t since);
    // Whether that was said for this account at this address, within
    // kPendingUploadMaxAgeSeconds of `now`. A record that is older, or for
    // another address, is deleted and answers false.
    bool UploadsDirect(const std::string& accountId, const std::string& serverUrl,
                       int64_t now);

private:
    std::string connection_;
};

} // namespace UltraCloud
