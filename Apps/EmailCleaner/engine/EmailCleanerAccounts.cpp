// Apps/EmailCleaner/engine/EmailCleanerAccounts.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerAccounts.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <mutex>

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace EmailCleaner {

namespace {

constexpr const char* kOwnPrefix = "ec-";

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trimmed(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

} // namespace

std::string OwnAccountIdFor(const std::string& email) {
    std::string id = kOwnPrefix;
    for (char c : Lower(Trimmed(email))) {
        id.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '-');
    }
    return id;
}

bool IsOwnAccountId(const std::string& accountId) {
    return accountId.size() > 3 && accountId.compare(0, 3, kOwnPrefix) == 0;
}

// ---- OwnAccounts -----------------------------------------------------------

UltraDbResult OwnAccounts::Open(const std::string& dataDir,
                                const std::string& connectionName) {
    std::error_code ec;
    std::filesystem::create_directories(PathFromUtf8(dataDir), ec);

    mailCacheDir_ = PathToUtf8(PathFromUtf8(dataDir) / "mail");
    vaultDir_     = PathToUtf8(PathFromUtf8(dataDir) / "vault");
    return store_.Open(connectionName, PathToUtf8(PathFromUtf8(dataDir) / "accounts.db"));
}

UltraDbResult OwnAccounts::List(std::vector<UltraMail::Account>& out) const {
    out.clear();
    if (!store_.IsOpen())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "the account list is not open");
    return store_.ListAccounts(out);
}

bool OwnAccounts::Find(const std::string& accountId, UltraMail::Account& out) const {
    std::vector<UltraMail::Account> all;
    if (!List(all)) return false;
    for (const UltraMail::Account& account : all) {
        if (account.accountId == accountId) {
            out = account;
            return true;
        }
    }
    return false;
}

UltraDbResult OwnAccounts::Save(const UltraMail::Account& account) {
    if (!IsOwnAccountId(account.accountId))
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "not an EmailCleaner account id: " + account.accountId);
    return store_.UpsertAccount(account);
}

UltraDbResult OwnAccounts::Remove(const std::string& accountId) {
    // Only ever an own account: an UltraMail id here would mean deleting a
    // directory under EmailCleaner's cache that UltraMail never wrote, which is
    // harmless — but asking for it is a caller bug worth refusing loudly.
    if (!IsOwnAccountId(accountId))
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "not an EmailCleaner account id: " + accountId);
    UltraDbResult r = store_.RemoveAccount(accountId);
    if (!r) return r;

    std::error_code ec;
    std::filesystem::remove_all(PathFromUtf8(mailCacheDir_) / PathFromUtf8(accountId), ec);
    return r;
}

std::string OwnAccounts::Validate(const NewAccountRequest& request,
                                  const std::vector<StoredAccount>& known) {
    const std::string email = Lower(Trimmed(request.email));
    if (!UltraMail::LooksLikeEmailAddress(email))
        return "That does not look like an email address.";
    if (request.password.empty())
        return "Enter the account's password (for Gmail, Outlook and Yahoo: an "
               "app password).";
    if (!request.settings.imap.Valid())
        return "The incoming (IMAP) server and port are needed.";

    for (const StoredAccount& account : known) {
        if (Lower(account.email) != email) continue;
        return account.source == AccountSource::UltraMail
            ? "UltraMail already shares " + email + " with EmailCleaner — "
              "choose it in the account list instead of adding it twice."
            : email + " is already one of EmailCleaner's accounts.";
    }
    return "";
}

UltraMail::Account OwnAccounts::MakeAccount(const NewAccountRequest& request) {
    UltraMail::Account account;
    account.email     = Lower(Trimmed(request.email));
    account.accountId = OwnAccountIdFor(account.email);
    account.shortName = UltraMail::EmailLocalPart(account.email);
    const std::string name = Trimmed(request.displayName);
    account.displayName = name.empty() ? account.shortName : name;

    UltraMail::AutoDiscovery::ApplyTo(account, request.settings);
    if (account.imap.username.empty()) account.imap.username = account.email;
    if (account.smtp.username.empty()) account.smtp.username = account.email;

    // EmailCleaner signs in with a password only. The provider table marks
    // Gmail / Outlook / Yahoo as OAuth2, but those accept an app password
    // over the ordinary mechanisms — so let the server's offer decide, exactly
    // as UltraMail does for an account that predates the method setting.
    for (UltraMail::MailServerSettings* server : { &account.imap, &account.smtp }) {
        if (server->auth == UltraNetMailAuth::OAuth2) server->auth = UltraNetMailAuth::Any;
        server->oauth = false;
    }
    return account;
}

// ---- Free functions --------------------------------------------------------

StoredAccount ToStoredAccount(const UltraMail::Account& account, AccountSource source) {
    StoredAccount stored;
    stored.accountId   = account.accountId;
    stored.displayName = account.displayName;
    stored.email       = account.email;
    stored.shortName   = account.shortName;
    stored.source      = source;
    return stored;
}

UltraNetMailOptions SessionOptionsFor(const UltraMail::Account& account,
                                      const std::string& password) {
    UltraNetMailOptions options;
    UltraMail::ApplyConnection(account.imap, options);
    options.credentials.username =
        account.imap.username.empty() ? account.email : account.imap.username;
    options.credentials.password = password;
    return options;
}

UltraMail::SyncOutcome FetchMailbox(UltraMail::LocalStore& store,
                                    IMailboxProtocolPlugin& mailbox,
                                    const std::string& mailCacheDir,
                                    const UltraMail::Account& account,
                                    const UltraNetMailOptions& options) {
    const std::string serverUrl = UltraMail::AutoDiscovery::ImapServerUrl(account.imap);
    if (serverUrl.empty())
        return UltraMail::SyncOutcome::Fail("the account has no incoming server");

    UltraMail::SyncEngine engine(store, mailbox, mailCacheDir);
    UltraMail::SyncOutcome folders = engine.SyncFolders(account.accountId, serverUrl, options);
    if (!folders) return folders;

    // The inbox always; the junk folder too when the server names one, since
    // what the provider already filtered is half of what a cleaner is for.
    std::vector<std::string> wanted = { "INBOX" };
    std::vector<UltraMail::Folder> known;
    if (store.ListFolders(account.accountId, known)) {
        for (const UltraMail::Folder& folder : known) {
            if (folder.role == UltraMail::FolderRole::Junk && folder.selectable &&
                folder.name != "INBOX") {
                wanted.push_back(folder.name);
                break;
            }
        }
    }

    UltraMail::SyncOutcome out;
    out.stats.folders = folders.stats.folders;
    for (const std::string& folder : wanted) {
        UltraMail::SyncOutcome fetched = engine.SyncMessages(
            account.accountId, folder, serverUrl, options, /*fetchBodies=*/true);
        // The inbox failing is the account failing; a junk folder that will
        // not open is not worth losing the inbox over.
        if (!fetched && folder == "INBOX") return fetched;
        out.stats.messages += fetched.stats.messages;
        out.stats.bodies   += fetched.stats.bodies;
    }
    return out;
}

std::function<UltraNetResult(UltraNetMailOptions&)> MakeOAuthSessionPreparer(
    const std::string& providerId, const std::string& username,
    const UltraMail::OAuthTokens& tokens, UltraMail::OAuthHooks hooks,
    std::function<int64_t()> now) {
    // Shared by every copy of the returned function (MailBackend copies the
    // access record), so a refresh is done once and seen by all of them.
    struct State {
        std::mutex              mutex;
        UltraMail::MailOAuth    oauth;
        UltraMail::OAuthTokens  tokens;
        explicit State(UltraMail::OAuthHooks h) : oauth(std::move(h)) {}
    };
    auto state = std::make_shared<State>(std::move(hooks));
    state->tokens = tokens;

    return [state, providerId, username, now](UltraNetMailOptions& options) {
        std::lock_guard<std::mutex> lock(state->mutex);
        bool refreshed = false;
        const UltraNetResult r = state->oauth.EnsureFresh(
            providerId, state->tokens, refreshed, now ? now() : 0);
        if (!r) {
            return UltraNetResult::Error(r.code,
                r.message + " (sign in to the account again in UltraMail)");
        }
        options.credentials          = UltraNetCredentials{};
        options.credentials.type     = UltraNetAuthType::OAuth2;
        options.credentials.username = username;
        options.credentials.token    = state->tokens.accessToken;
        options.auth                 = UltraNetMailAuth::OAuth2;
        return UltraNetResult::Ok();
    };
}

} // namespace EmailCleaner
