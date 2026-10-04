// Tests/EmailCleaner/test_accounts.cpp
// EmailCleaner's own accounts: ids that cannot collide with UltraMail's, the
// "Add account" validation, the account record a request becomes, the account
// source in the analysis database, and the mailbox fetch — driven against a
// fake IMAP plug-in, so no server and no network.
// Version: 0.2.0 - a server name that cannot be one is refused
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerAccounts.h"
#include "EmailCleanerIngest.h"
#include "EmailCleanerMailBackend.h"
#include "UltraCanvasPathUtf8.h"

#include <UltraDatabase/UltraDatabase.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace EmailCleaner;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace {

int NextId() {
    static int counter = 0;
    return ++counter;
}

// A scratch directory per test, removed when the test ends.
struct TempDir {
    std::string path;
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = PathToUtf8(std::filesystem::temp_directory_path() /
                          ("ec_accounts_" + std::to_string(stamp) + "_" +
                           std::to_string(NextId())));
        std::filesystem::create_directories(PathFromUtf8(path));
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(PathFromUtf8(path), ec);
    }
};

// A mailbox with an inbox and a junk folder, each holding canned messages.
class FakeMailbox : public IMailboxProtocolPlugin {
public:
    std::vector<UltraNetMailFolder> folders;
    std::map<std::string, std::map<uint32_t, std::string>> messages;   // folder -> uid -> raw
    bool failListFolders = false;
    std::vector<std::string> fetchedFolders;
    std::string lastUsername, lastPassword, lastToken;
    UltraNetAuthType lastAuthType = UltraNetAuthType::None;
    int listFolderCalls = 0;
    int moveCalls = 0;

    std::string GetName() const override { return "FakeMailbox"; }
    std::string GetVersion() const override { return "0.0.1"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"imap", "imaps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}

    UltraNetResult SendMail(const UltraNetMailMessage&, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult ListFolders(const std::string&, std::vector<UltraNetMailFolder>& out,
                               const UltraNetMailOptions& options) override {
        ++listFolderCalls;
        lastUsername = options.credentials.username;
        lastPassword = options.credentials.password;
        lastToken    = options.credentials.token;
        lastAuthType = options.credentials.type;
        if (failListFolders)
            return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                         "wrong password");
        out = folders;
        return UltraNetResult::Ok();
    }
    UltraNetResult GetMailboxStatus(const std::string&, const std::string&,
                                    UltraNetMailboxStatus&, const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::Unsupported, "no STATUS");
    }
    UltraNetResult FetchEnvelopes(const std::string&, const std::string& folder,
                                  uint32_t sinceUid, std::vector<UltraNetMailEnvelope>& out,
                                  const UltraNetMailOptions&) override {
        fetchedFolders.push_back(folder);
        auto it = messages.find(folder);
        if (it == messages.end()) return UltraNetResult::Ok();
        for (const auto& [uid, raw] : it->second) {
            if (uid <= sinceUid) continue;
            UltraNetMailEnvelope e;
            e.uid     = uid;
            e.subject = "Message " + std::to_string(uid);
            e.from    = "Shop <deals@shop.example>";
            out.push_back(e);
        }
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessage(const std::string&, const std::string& folder, uint32_t uid,
                                std::string& raw, const UltraNetMailOptions&) override {
        auto it = messages.find(folder);
        if (it == messages.end() || !it->second.count(uid))
            return UltraNetResult::Error(UltraNetResultCode::NotFound, "no body");
        raw = it->second.at(uid);
        return UltraNetResult::Ok();
    }
    UltraNetResult StoreFlags(const std::string&, const std::string&, uint32_t,
                              UltraNetMailFlags, bool, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult MoveMessage(const std::string&, const std::string&, uint32_t,
                               const std::string&, const UltraNetMailOptions&) override {
        ++moveCalls;
        return UltraNetResult::Ok();
    }
    UltraNetResult AppendMessage(const std::string&, const std::string&, const std::string&,
                                 UltraNetMailFlags, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
};

UltraNetMailFolder MailFolder(const std::string& name, const std::string& role = "") {
    UltraNetMailFolder f;
    f.name = name;
    f.role = role;
    f.delimiter = "/";
    f.selectable = true;
    return f;
}

std::string RawMessage(const std::string& subject) {
    return "From: Shop <deals@shop.example>\r\n"
           "To: erika@example.com\r\n"
           "Subject: " + subject + "\r\n"
           "Date: Tue, 01 Sep 2026 10:00:00 +0000\r\n"
           "Message-ID: <" + subject + "@shop.example>\r\n"
           "List-Unsubscribe: <https://shop.example/u>\r\n"
           "\r\n"
           "Half price, today only.\r\n";
}

NewAccountRequest Request(const std::string& email = "Erika@Example.com") {
    NewAccountRequest request;
    request.email    = email;
    request.password = "app-password";
    request.settings.found     = true;
    request.settings.imap.host = "imap.example.com";
    request.settings.imap.port = 993;
    request.settings.imap.security = UltraMail::MailSecurity::SslTls;
    return request;
}

} // namespace

// ---- Ids --------------------------------------------------------------------

TEST(Accounts_OwnIdsCannotCollideWithUltraMails) {
    REQUIRE_EQ(OwnAccountIdFor(" Erika@Example.com "), std::string("ec-erika-example-com"));
    REQUIRE(IsOwnAccountId("ec-erika-example-com"));
    // UltraMail's slug for the same address: no prefix, so a different row.
    REQUIRE(!IsOwnAccountId("erika-example-com"));
    REQUIRE(!IsOwnAccountId("ec-"));
}

// ---- Validation -------------------------------------------------------------

TEST(Accounts_ValidateAcceptsACompleteRequest) {
    REQUIRE(OwnAccounts::Validate(Request(), {}).empty());
}

TEST(Accounts_ValidateNamesWhatIsMissing) {
    NewAccountRequest bad = Request("not an address");
    REQUIRE(!OwnAccounts::Validate(bad, {}).empty());

    NewAccountRequest noPassword = Request();
    noPassword.password.clear();
    REQUIRE(OwnAccounts::Validate(noPassword, {}).find("password") != std::string::npos);

    NewAccountRequest noServer = Request();
    noServer.settings.imap.host.clear();
    REQUIRE(OwnAccounts::Validate(noServer, {}).find("server") != std::string::npos);
}

TEST(Accounts_ValidateCatchesAServerNameThatCannotBeOne) {
    // The address's @ typed where the server name has a dot.
    NewAccountRequest at = Request();
    at.settings.imap.host = "mail@example.com";
    const std::string error = OwnAccounts::Validate(at, {});
    REQUIRE(error.find("mail@example.com") != std::string::npos);
    REQUIRE(error.find("did you mean mail.example.com?") != std::string::npos);

    NewAccountRequest scheme = Request();
    scheme.settings.imap.host = "imaps://imap.example.com";
    REQUIRE(!OwnAccounts::Validate(scheme, {}).empty());

    NewAccountRequest withPort = Request();
    withPort.settings.imap.host = "imap.example.com:993";
    REQUIRE(OwnAccounts::Validate(withPort, {}).find("port") != std::string::npos);

    // Spaces around a good name are not a fault.
    NewAccountRequest spaced = Request();
    spaced.settings.imap.host = " imap.example.com ";
    REQUIRE(OwnAccounts::Validate(spaced, {}).empty());
    REQUIRE_EQ(OwnAccounts::MakeAccount(spaced).imap.host, std::string("imap.example.com"));
}

TEST(Accounts_ValidateRefusesAnAddressUltraMailAlreadyShares) {
    StoredAccount shared;
    shared.accountId = "erika-example-com";
    shared.email     = "erika@example.com";
    shared.source    = AccountSource::UltraMail;

    const std::string error = OwnAccounts::Validate(Request("ERIKA@example.com"), { shared });
    REQUIRE(error.find("UltraMail") != std::string::npos);

    shared.source = AccountSource::Own;
    REQUIRE(OwnAccounts::Validate(Request(), { shared }).find("already") != std::string::npos);
}

// ---- The account record ------------------------------------------------------

TEST(Accounts_MakeAccountFillsNamesAndServers) {
    const UltraMail::Account account = OwnAccounts::MakeAccount(Request());
    REQUIRE_EQ(account.accountId, std::string("ec-erika-example-com"));
    REQUIRE_EQ(account.email, std::string("erika@example.com"));
    REQUIRE_EQ(account.shortName, std::string("erika"));
    REQUIRE_EQ(account.displayName, std::string("erika"));
    REQUIRE_EQ(account.imap.host, std::string("imap.example.com"));
    REQUIRE_EQ(account.imap.username, std::string("erika@example.com"));
}

TEST(Accounts_PasswordSignInAtAnOAuthProviderLeavesTheMethodToTheServer) {
    // Gmail's table entry says OAuth2; EmailCleaner signs in with an app password.
    NewAccountRequest request = Request("someone@gmail.com");
    request.settings = UltraMail::AutoDiscovery::FromPresets("someone@gmail.com");
    REQUIRE(request.settings.found);

    const UltraMail::Account account = OwnAccounts::MakeAccount(request);
    REQUIRE(account.imap.auth != UltraNetMailAuth::OAuth2);
    REQUIRE(!account.imap.oauth);

    const UltraNetMailOptions options = SessionOptionsFor(account, "app-password");
    REQUIRE(options.auth != UltraNetMailAuth::OAuth2);
    REQUIRE_EQ(options.credentials.password, std::string("app-password"));
    REQUIRE(options.useTls);
}

// ---- The analysis database ---------------------------------------------------

TEST(Accounts_TheAnalysisStoreRemembersWhereAnAccountCameFrom) {
    AnalysisStore store;
    REQUIRE(store.Open("ec_accounts_store_" + std::to_string(NextId()), ":memory:"));

    StoredAccount shared;
    shared.accountId = "erika-example-com";
    shared.email     = "erika@example.com";
    REQUIRE(store.UpsertAccount(shared));

    REQUIRE(store.UpsertAccount(
        ToStoredAccount(OwnAccounts::MakeAccount(Request("max@example.org")),
                        AccountSource::Own)));

    std::vector<StoredAccount> all;
    REQUIRE(store.ListAccounts(all));
    REQUIRE_EQ(all.size(), static_cast<size_t>(2));
    for (const StoredAccount& account : all) {
        if (account.accountId == "erika-example-com")
            REQUIRE(account.source == AccountSource::UltraMail);
        else
            REQUIRE(account.source == AccountSource::Own);
    }
    REQUIRE(AccountSourceFromString("something else") == AccountSource::UltraMail);
}

// ---- The account list and the fetch -----------------------------------------

TEST(Accounts_SaveListAndRemove) {
    TempDir dir;
    OwnAccounts accounts;
    REQUIRE(accounts.Open(dir.path, "ec_own_" + std::to_string(NextId())));

    const UltraMail::Account account = OwnAccounts::MakeAccount(Request());
    REQUIRE(accounts.Save(account));

    UltraMail::Account found;
    REQUIRE(accounts.Find(account.accountId, found));
    REQUIRE_EQ(found.imap.host, std::string("imap.example.com"));

    // An UltraMail id is never saved or removed here.
    UltraMail::Account foreign = account;
    foreign.accountId = "erika-example-com";
    REQUIRE(!accounts.Save(foreign));
    REQUIRE(!accounts.Remove("erika-example-com"));

    REQUIRE(accounts.Remove(account.accountId));
    REQUIRE(!accounts.Find(account.accountId, found));
}

TEST(Accounts_FetchMailboxCachesInboxAndJunkForTheIngest) {
    TempDir dir;
    OwnAccounts accounts;
    REQUIRE(accounts.Open(dir.path, "ec_own_" + std::to_string(NextId())));
    const UltraMail::Account account = OwnAccounts::MakeAccount(Request());
    REQUIRE(accounts.Save(account));

    FakeMailbox mailbox;
    mailbox.folders = { MailFolder("INBOX", "inbox"), MailFolder("Sent", "sent"),
                        MailFolder("Spam", "junk") };
    mailbox.messages["INBOX"][1] = RawMessage("one");
    mailbox.messages["INBOX"][2] = RawMessage("two");
    mailbox.messages["Spam"][7]  = RawMessage("seven");
    mailbox.messages["Sent"][3]  = RawMessage("not-fetched");

    const UltraNetMailOptions options = SessionOptionsFor(account, "app-password");
    UltraMail::SyncOutcome outcome = FetchMailbox(accounts.Store(), mailbox,
                                                  accounts.MailCacheDir(), account, options);
    REQUIRE(outcome.ok);
    REQUIRE_EQ(outcome.stats.messages, 3);
    REQUIRE_EQ(outcome.stats.bodies, 3);
    REQUIRE_EQ(mailbox.lastUsername, std::string("erika@example.com"));
    REQUIRE_EQ(mailbox.lastPassword, std::string("app-password"));

    // What the ingest reads is exactly where the fetch wrote.
    AnalysisStore store;
    REQUIRE(store.Open("ec_accounts_ingest_" + std::to_string(NextId()), ":memory:"));
    Ingestor ingestor(store);
    IngestOptions ingest;
    ingest.ownerAddress = account.email;
    const IngestStats stats =
        ingestor.IngestMailCache(accounts.MailCacheDir(), account.accountId, ingest);
    REQUIRE_EQ(stats.analysed, 3);

    // A second fetch is incremental: nothing new, nothing re-downloaded.
    outcome = FetchMailbox(accounts.Store(), mailbox, accounts.MailCacheDir(), account, options);
    REQUIRE(outcome.ok);
    REQUIRE_EQ(outcome.stats.bodies, 0);

    // Removing the account takes its downloaded mail with it.
    REQUIRE(accounts.Remove(account.accountId));
    REQUIRE(!std::filesystem::exists(PathFromUtf8(accounts.MailCacheDir()) /
                                     PathFromUtf8(account.accountId)));
}

TEST(Accounts_FetchMailboxReportsASignInFailure) {
    TempDir dir;
    OwnAccounts accounts;
    REQUIRE(accounts.Open(dir.path, "ec_own_" + std::to_string(NextId())));
    const UltraMail::Account account = OwnAccounts::MakeAccount(Request());

    FakeMailbox mailbox;
    mailbox.failListFolders = true;
    const UltraMail::SyncOutcome outcome =
        FetchMailbox(accounts.Store(), mailbox, accounts.MailCacheDir(), account,
                     SessionOptionsFor(account, "wrong"));
    REQUIRE(!outcome.ok);
    REQUIRE(outcome.message.find("wrong password") != std::string::npos);
    REQUIRE(mailbox.fetchedFolders.empty());
}

// ---- UltraMail accounts that signed in through the browser (OAuth2) ---------

namespace {

UltraMail::OAuthApp GoogleTestApp() {
    UltraMail::OAuthApp app;
    app.clientId     = "client-123.apps.googleusercontent.com";
    app.clientSecret = "shh";
    return app;
}

} // namespace

TEST(Accounts_OAuthPreparerSignsInWithTheStoredTokenWhileItIsValid) {
    UltraMail::OAuthApps::Clear();
    UltraMail::OAuthApps::Set("google", GoogleTestApp());
    int refreshCalls = 0;
    UltraMail::OAuthHooks hooks;
    hooks.refresh = [&](const UltraNetOAuth2Config&, const std::string&, UltraNetOAuth2Token&) {
        ++refreshCalls;
        return UltraNetResult::Ok();
    };
    UltraMail::OAuthTokens tokens;
    tokens.accessToken = "a1"; tokens.refreshToken = "r1"; tokens.expiresAt = 2000;

    auto prepare = MakeOAuthSessionPreparer("google", "someone@gmail.com", tokens, hooks,
                                            [] { return int64_t(1000); });
    UltraNetMailOptions options;
    options.credentials.password = "stale";
    REQUIRE(prepare(options));
    REQUIRE(options.credentials.type == UltraNetAuthType::OAuth2);
    REQUIRE_EQ(options.credentials.token, std::string("a1"));
    REQUIRE_EQ(options.credentials.username, std::string("someone@gmail.com"));
    REQUIRE(options.credentials.password.empty());   // no password rides along
    REQUIRE(options.auth == UltraNetMailAuth::OAuth2);
    REQUIRE_EQ(refreshCalls, 0);
    UltraMail::OAuthApps::Clear();
}

TEST(Accounts_OAuthPreparerRenewsAnExpiredTokenOnceForEveryCopy) {
    UltraMail::OAuthApps::Clear();
    UltraMail::OAuthApps::Set("google", GoogleTestApp());
    int refreshCalls = 0;
    UltraMail::OAuthHooks hooks;
    hooks.refresh = [&](const UltraNetOAuth2Config&, const std::string& refreshToken,
                        UltraNetOAuth2Token& out) {
        ++refreshCalls;
        REQUIRE_EQ(refreshToken, std::string("r1"));
        out.accessToken = "a2"; out.expiresInSeconds = 3600;
        return UltraNetResult::Ok();
    };
    UltraMail::OAuthTokens tokens;
    tokens.accessToken = "a1"; tokens.refreshToken = "r1"; tokens.expiresAt = 1000;

    auto prepare = MakeOAuthSessionPreparer("google", "someone@gmail.com", tokens, hooks,
                                            [] { return int64_t(1500); });
    auto copy = prepare;   // MailBackend keeps its own copy of the access record

    UltraNetMailOptions first, second;
    REQUIRE(prepare(first));
    REQUIRE(copy(second));
    REQUIRE_EQ(first.credentials.token, std::string("a2"));
    REQUIRE_EQ(second.credentials.token, std::string("a2"));
    REQUIRE_EQ(refreshCalls, 1);
    UltraMail::OAuthApps::Clear();
}

TEST(Accounts_OAuthPreparerSaysWhenTheSignInCannotBeRenewed) {
    UltraMail::OAuthApps::Clear();
    UltraMail::OAuthApps::Set("google", GoogleTestApp());
    UltraMail::OAuthTokens dead;
    dead.accessToken = "old"; dead.expiresAt = 1;   // expired, no refresh token

    auto prepare = MakeOAuthSessionPreparer("google", "someone@gmail.com", dead, {},
                                            [] { return int64_t(5); });
    UltraNetMailOptions options;
    const UltraNetResult r = prepare(options);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationRequired);
    REQUIRE(r.message.find("UltraMail") != std::string::npos);
    UltraMail::OAuthApps::Clear();
}

TEST(Accounts_MailBackendSignsInThroughThePreparerBeforeEveryCall) {
    FakeMailbox mailbox;
    mailbox.folders = { MailFolder("INBOX", "inbox"), MailFolder("Trash", "trash") };

    MailAccountAccess access;
    access.accountId = "someone-gmail-com";
    access.serverUrl = "imaps://imap.gmail.com:993/";
    access.ownerAddress = "someone@gmail.com";
    int prepared = 0;
    access.prepareSession = [&prepared](UltraNetMailOptions& options) {
        ++prepared;
        options.credentials.type  = UltraNetAuthType::OAuth2;
        options.credentials.token = "token-" + std::to_string(prepared);
        return UltraNetResult::Ok();
    };

    MailBackend backend(mailbox);
    backend.SetAccount(access);
    std::string error;
    REQUIRE(backend.MoveToTrash("someone-gmail-com", "INBOX", 7, error));
    REQUIRE_EQ(prepared, 1);
    REQUIRE(mailbox.lastAuthType == UltraNetAuthType::OAuth2);
    REQUIRE_EQ(mailbox.lastToken, std::string("token-1"));
    REQUIRE_EQ(mailbox.moveCalls, 1);

    // The next call asks again - an hour later the token may have expired.
    REQUIRE(backend.MoveToTrash("someone-gmail-com", "INBOX", 8, error));
    REQUIRE_EQ(prepared, 2);
}

TEST(Accounts_MailBackendSendsNothingWhenTheSignInFails) {
    FakeMailbox mailbox;
    mailbox.folders = { MailFolder("INBOX", "inbox"), MailFolder("Trash", "trash") };

    MailAccountAccess access;
    access.accountId = "someone-gmail-com";
    access.serverUrl = "imaps://imap.gmail.com:993/";
    access.ownerAddress = "someone@gmail.com";
    access.prepareSession = [](UltraNetMailOptions&) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationRequired,
                                     "the Google sign-in has expired");
    };

    MailBackend backend(mailbox);
    backend.SetAccount(access);
    std::string error;
    REQUIRE(!backend.MoveToTrash("someone-gmail-com", "INBOX", 7, error));
    REQUIRE(error.find("could not sign in") != std::string::npos);
    REQUIRE(error.find("expired") != std::string::npos);
    REQUIRE_EQ(mailbox.listFolderCalls, 0);
    REQUIRE_EQ(mailbox.moveCalls, 0);

    REQUIRE(!backend.SendUnsubscribeMail("someone-gmail-com", "unsub@list.example",
                                         "unsubscribe", error));
    REQUIRE(error.find("could not sign in") != std::string::npos);
}

TEST(Accounts_MailBackendCanBeUsedFromAWorkerWhileAccountsChange) {
    // EmailCleaner moves mail on a worker thread while the UI thread may add
    // or re-register an account. Each call works on its own copy of the record.
    FakeMailbox mailbox;
    mailbox.folders = { MailFolder("INBOX", "inbox"), MailFolder("Trash", "trash") };

    MailAccountAccess access;
    access.accountId = "erika";
    access.serverUrl = "imaps://mail.example:993/";
    access.ownerAddress = "erika@example.com";

    MailBackend backend(mailbox);
    backend.SetAccount(access);

    int moved = 0;
    std::thread worker([&]() {
        for (int uid = 1; uid <= 200; ++uid) {
            std::string error;
            if (backend.MoveToTrash("erika", "INBOX", uid, error)) ++moved;
        }
    });
    for (int i = 0; i < 200; ++i) {
        MailAccountAccess other = access;
        other.accountId = "account-" + std::to_string(i % 5);
        backend.SetAccount(other);
        backend.SetAccount(access);   // the same account, registered again
    }
    worker.join();

    REQUIRE_EQ(moved, 200);
    REQUIRE_EQ(mailbox.moveCalls, 200);
    REQUIRE_EQ(backend.ResolvedTrash("erika").empty() ||
               backend.ResolvedTrash("erika") == "Trash", true);
}

// ---- Moved messages leave the analysis, and stay out -------------------------

namespace {

// Write one raw message into a cache folder, as the SyncEngine would.
void WriteCached(const std::string& cacheDir, const std::string& accountId,
                 const std::string& folderDir, int64_t uid, const std::string& raw) {
    const auto dir = PathFromUtf8(cacheDir) / PathFromUtf8(accountId) / PathFromUtf8(folderDir);
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / (std::to_string(uid) + ".eml"), std::ios::binary);
    out << raw;
}

int CountMessages(const AnalysisStore& store) {
    std::vector<AnalyzedMessage> rows;
    store.ListMessages(MessageFilter{}, rows);
    return static_cast<int>(rows.size());
}

} // namespace

TEST(Accounts_MovedMessagesLeaveTheAnalysisAndAScanDoesNotBringThemBack) {
    TempDir dir;
    const std::string cache = PathToUtf8(PathFromUtf8(dir.path) / "mail");
    WriteCached(cache, "erika", "INBOX", 1, RawMessage("one"));
    WriteCached(cache, "erika", "INBOX", 2, RawMessage("two"));

    AnalysisStore store;
    REQUIRE(store.Open("ec_moved_" + std::to_string(NextId()), ":memory:"));
    Ingestor ingestor(store);
    IngestOptions scan;
    REQUIRE_EQ(ingestor.IngestMailCache(cache, "erika", scan).analysed, 2);
    REQUIRE_EQ(CountMessages(store), 2);

    // Move the sender's mail to Trash.
    ActionRequest request;
    request.target.senderAddr = "deals@shop.example";
    request.deleteMail = true;
    const ActionPlan plan = ActionPlanner(store).Plan(request);
    REQUIRE_EQ(plan.MessageCount(), 2);

    FakeMailbox mailbox;
    mailbox.folders = { MailFolder("INBOX", "inbox"), MailFolder("Trash", "trash") };
    MailBackend backend(mailbox);
    MailAccountAccess access;
    access.accountId = "erika";
    access.serverUrl = "imaps://mail.example:993/";
    backend.SetAccount(access);

    const ActionOutcome outcome = ActionExecutor(store, &backend).Execute(plan);
    REQUIRE(outcome.ok);
    REQUIRE_EQ(outcome.moved, 2);
    REQUIRE_EQ(outcome.movedMessages.size(), static_cast<std::size_t>(2));

    // Gone from the analysis at once - the map no longer counts them ...
    REQUIRE_EQ(CountMessages(store), 0);
    std::string movedId;
    REQUIRE(store.FindMovedAway("erika", "INBOX", 1, movedId));
    REQUIRE_EQ(movedId, std::string("<one@shop.example>"));

    // ... although the bodies are still in the cache, which is not ours to
    // prune. Neither a scan nor a full re-analyse brings them back.
    IngestStats again = ingestor.IngestMailCache(cache, "erika", scan);
    REQUIRE_EQ(again.analysed, 0);
    REQUIRE_EQ(again.movedAway, 2);
    IngestOptions reanalyse;
    reanalyse.skipExisting = false;
    again = ingestor.IngestMailCache(cache, "erika", reanalyse);
    REQUIRE_EQ(again.analysed, 0);
    REQUIRE_EQ(again.movedAway, 2);
    REQUIRE_EQ(CountMessages(store), 0);
}

TEST(Accounts_AReusedUidIsAnalysedAgain) {
    // After a UIDVALIDITY reset the server may hand the moved message's UID
    // to a different message; that one belongs in the analysis.
    TempDir dir;
    const std::string cache = PathToUtf8(PathFromUtf8(dir.path) / "mail");
    WriteCached(cache, "erika", "INBOX", 1, RawMessage("one"));

    AnalysisStore store;
    REQUIRE(store.Open("ec_reused_" + std::to_string(NextId()), ":memory:"));
    AnalyzedMessage moved;
    moved.accountId = "erika"; moved.folder = "INBOX"; moved.uid = 1;
    moved.messageId = "<something-else@shop.example>";
    REQUIRE(store.RecordMovedAway({ moved }));

    Ingestor ingestor(store);
    const IngestStats stats = ingestor.IngestMailCache(cache, "erika", IngestOptions{});
    REQUIRE_EQ(stats.analysed, 1);
    REQUIRE_EQ(stats.movedAway, 0);
    std::string id;
    REQUIRE(!store.FindMovedAway("erika", "INBOX", 1, id));   // the stale record is gone
}

TEST(Accounts_TrashIsLeftOutOfTheAnalysis) {
    TempDir dir;
    const std::string cache = PathToUtf8(PathFromUtf8(dir.path) / "mail");
    WriteCached(cache, "erika", "INBOX", 1, RawMessage("one"));
    WriteCached(cache, "erika", "[Gmail]_Bin", 5, RawMessage("binned"));
    WriteCached(cache, "erika", "Rubbish", 6, RawMessage("rubbish"));

    AnalysisStore store;
    REQUIRE(store.Open("ec_trash_" + std::to_string(NextId()), ":memory:"));
    Ingestor ingestor(store);

    // An earlier scan, before Trash was left out, stored all three.
    REQUIRE_EQ(ingestor.IngestMailCache(cache, "erika", IngestOptions{}).analysed, 3);

    // By name: "[Gmail]_Bin" is "[Gmail]/Bin" as the cache writes it. By role:
    // "Rubbish" says nothing, but the server calls it the Trash.
    IngestOptions options;
    options.skipTrash = true;
    options.skipFolders = { CacheDirectoryName("erika", "Rubbish") };
    const IngestStats stats = ingestor.IngestMailCache(cache, "erika", options);
    REQUIRE_EQ(stats.foldersLeftOut, 2);

    std::vector<AnalyzedMessage> rows;
    REQUIRE(store.ListMessages(MessageFilter{}, rows));
    REQUIRE_EQ(rows.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(rows[0].folder, std::string("INBOX"));

    REQUIRE(LooksLikeTrashDirectory("[Gmail]_Bin"));
    REQUIRE(LooksLikeTrashDirectory("INBOX.Trash"));
    REQUIRE(!LooksLikeTrashDirectory("INBOX"));
    REQUIRE_EQ(CacheDirectoryName("erika", "[Gmail]/Bin"), std::string("[Gmail]_Bin"));
}
