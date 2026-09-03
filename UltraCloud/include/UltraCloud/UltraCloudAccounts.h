// UltraCloud/include/UltraCloud/UltraCloudAccounts.h
// The cloud account store: the list of configured accounts and which one is
// the default, persisted on UltraDatabase (SQLite). Shared by every app that
// links UltraCloud, so a cloud account is set up once.
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudTypes.h"

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

private:
    std::string connection_;
};

} // namespace UltraCloud
