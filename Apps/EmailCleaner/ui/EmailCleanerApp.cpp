// Apps/EmailCleaner/ui/EmailCleanerApp.cpp
// Version: 0.3.0 - own accounts beside UltraMail's
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerApp.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasUtils.h"      // GetExecutableDir
#include "UltraCanvasLabel.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasModalDialog.h"

#include "UltraMailCredentialVault.h"
#include "UltraMailDiscovery.h"
#include "UltraMailLocalStore.h"
#include "UltraMailLoginCheck.h"
#include "UltraMailOAuth.h"

#include <UltraDatabase/UltraDatabase.h>
#include <UltraNet/UltraNetPlugins.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

// EMAILCLEANER_VERSION comes from the build alone: CMake reads the first line of
// Docs/EmailCleaner/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it as a
// compile definition. No fallback, so a build that lost it fails instead of
// showing a wrong number in the window title.
#ifndef EMAILCLEANER_VERSION
#error "EMAILCLEANER_VERSION is not defined: build through CMake, which reads it from Docs/EmailCleaner/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {
constexpr float kWindowW = 1200.0f;
constexpr float kWindowH = 800.0f;
constexpr float kBarH    = 62.0f;

// EmailCleaner's own credential vault: UltraVault's device-key vault with
// EmailCleaner's file name and key prefix, in <data dir>/vault. UltraMail's
// is only ever read; this one is the only vault EmailCleaner writes.
const UltraVault::DeviceKeyVaultProfile kOwnVaultProfile{
    /*vaultFileName=*/"emailcleaner.vault",
    /*keyPrefix=*/    "mail.emailcleaner."};

// Where UltraNet's protocol plug-ins (the IMAP DSO among them) are: the
// EMAILCLEANER_PLUGIN_DIR override, else Plugins/UltraNet next to the
// executable or up to two levels above it (the build tree, bin/), else the
// registry's own working-directory default. A directory counts only when it
// holds a DSO, so an empty one does not shadow the real one.
std::string ResolvePluginDirectory() {
    namespace fs = std::filesystem;
    if (const char* dir = std::getenv("EMAILCLEANER_PLUGIN_DIR"); dir && *dir) return dir;

    auto holdsPlugin = [](const fs::path& dir) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec) || ec) return false;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) return false;
            const std::string ext = PathToUtf8(entry.path().extension());
            if (ext == ".so" || ext == ".dll" || ext == ".dylib") return true;
        }
        return false;
    };

    const fs::path relative = PathFromUtf8("Plugins") / "UltraNet";
    std::vector<fs::path> candidates;
    if (const std::string exeDir = GetExecutableDir(); !exeDir.empty()) {
        fs::path base = PathFromUtf8(exeDir);
        for (int up = 0; up < 3; ++up) {
            candidates.push_back(base / relative);
            base = base.parent_path();
        }
    }
    candidates.push_back(relative);

    for (const auto& candidate : candidates)
        if (holdsPlugin(candidate)) return PathToUtf8(candidate.lexically_normal());
    // Nothing found: name a concrete place in the diagnostic.
    return PathToUtf8(candidates.front().lexically_normal());
}

// Run `task` on the UI thread (from a worker). Dropped when the app is gone.
void OnUiThread(std::function<void()> task) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent())
        app->PostToUIThread(std::move(task));
}
} // namespace

bool EmailCleanerApp::Initialize(const std::string& dataDir,
                                 const std::string& mailDataDir) {
    std::error_code ec;
    std::filesystem::create_directories(PathFromUtf8(dataDir), ec);

    const UltraDbResult opened = store_.Open("emailcleaner", dataDir + "/analysis.db");
    if (!opened) return false;

    dataDir_      = dataDir;
    mailDataDir_  = mailDataDir;
    // UltraMail caches raw bodies under <its data dir>/mail/<account>/<folder>.
    mailCacheDir_ = mailDataDir + "/mail";
    rulesPath_    = dataDir + "/rules.txt";

    // EmailCleaner's own accounts. A failure here costs only the accounts
    // added in EmailCleaner; UltraMail's still load.
    ownAccounts_.Open(dataDir);

    // Bring up UltraNet's plug-in registry so the IMAP DSO loads. Without it
    // no account — UltraMail's or EmailCleaner's own — can reach its server.
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();
    pluginDir_ = ResolvePluginDirectory();
    UltraNet_SetPluginDirectory(pluginDir_);
    UltraNet_RefreshPlugins();

    // The OAuth client UltraMail signs in as. An UltraMail account that signed
    // in through its provider's browser login holds a refresh token issued to
    // that client, so renewing it has to go through the same one: the
    // environment and the build's baked-in client are shared already, and
    // this adds the oauth.ini in UltraMail's data folder, as UltraMail does.
    UltraMail::OAuthApps::LoadFile(mailDataDir + "/oauth.ini");

    // Attachments opened in the viewer are copies; prune them now, before any
    // viewer has one open (a week unopened, then down to 256 MB).
    PruneAttachmentCache(dataDir_ + "/attachments");

    LoadRules();
    ImportAccounts();
    store_.ListAccounts(accounts_);
    WireMailBackend();
    return true;
}

void EmailCleanerApp::WireMailBackend() {
    // Acting on mail needs the same IMAP plug-in UltraMail syncs with. It is a
    // DSO loaded at start-up, so its absence is a normal state, not an error:
    // the analysis and the blocklist work without it.
    imapPlugin_ = UltraNet_GetPlugin("imaps");
    auto* mailbox = imapPlugin_ ? dynamic_cast<IMailboxProtocolPlugin*>(imapPlugin_.get())
                                : nullptr;
    if (!mailbox) {
        imapPlugin_.reset();
        backendUnavailable_ = "The IMAP plug-in (ultranet_imap) was not found in " +
                              pluginDir_ + ", so EmailCleaner cannot reach the mail "
                              "server. Set EMAILCLEANER_PLUGIN_DIR to the folder that "
                              "holds it.";
        return;
    }

    mailBackend_ = std::make_unique<MailBackend>(*mailbox);

    // The two vaults are opened one after the other, never together:
    // UltraVault holds one store per process.
    std::string ultraMailProblem;
    const int usable = RegisterUltraMailAccounts(ultraMailProblem) + RegisterOwnAccounts();

    if (usable > 0) {
        backendUnavailable_.clear();
    } else if (!ultraMailProblem.empty()) {
        backendUnavailable_ = ultraMailProblem;
    } else {
        backendUnavailable_ = "No account has a server and a saved password — add "
                              "one under Accounts…, or set it up in UltraMail.";
    }
}

int EmailCleanerApp::RegisterUltraMailAccounts(std::string& problem) {
    bool any = false;
    for (const StoredAccount& account : accounts_)
        if (account.source == AccountSource::UltraMail) any = true;
    if (!any) return 0;

    // One entry per account: where its server is, and how it signs in - the
    // password UltraMail holds, or, for an account that signed in through its
    // provider's browser login, its OAuth2 token set. An account with neither
    // is simply not registered, and the backend then refuses its messages by
    // name rather than failing obscurely. The vault is UltraMail's, unlocked
    // with the device key UltraMail keeps beside it, and only ever read.
    UltraMail::CredentialVault vault(mailDataDir_ + "/vault");
    if (!vault.TryAutoUnlock()) {
        problem = vault.Exists()
            ? "UltraMail's credential vault is locked with a master password — "
              "open UltraMail once so it stores its device key, then restart."
            : "UltraMail has no credential vault yet — set the account up in "
              "UltraMail first, or add it under Accounts….";
        return 0;
    }
    int usable = 0;
    for (const StoredAccount& account : accounts_) {
        if (account.source != AccountSource::UltraMail || account.email.empty()) continue;

        // The servers UltraMail stored for the account (discovered or typed
        // in by hand), else its provider table entry.
        UltraMail::Account record;
        if (auto it = ultraMailAccounts_.find(account.accountId); it != ultraMailAccounts_.end())
            record = it->second;
        else
            record.email = account.email;
        const UltraMail::DiscoveryResult discovered = UltraMail::AutoDiscovery::ForAccount(record);
        if (!discovered.imap.Valid()) continue;

        MailAccountAccess access;
        access.accountId    = account.accountId;
        access.serverUrl    = UltraMail::AutoDiscovery::ImapServerUrl(discovered.imap);
        access.ownerAddress = account.email;
        const std::string username =
            discovered.imap.username.empty() ? account.email : discovered.imap.username;
        access.options.credentials.username = username;
        UltraMail::ApplyConnection(discovered.imap, access.options);

        UltraMail::OAuthTokens tokens;
        if (vault.RetrieveOAuthTokens(account.accountId, tokens)) {
            // Browser sign-in: a fresh bearer token before every server call,
            // renewed through the provider when it has expired.
            const std::string provider = UltraMail::OAuthProviderFor(discovered);
            if (provider.empty()) continue;   // cannot renew without knowing whom to ask
            access.prepareSession =
                MakeOAuthSessionPreparer(provider, username, tokens);
        } else {
            std::string password;
            if (!vault.Retrieve(account.accountId, password) || password.empty()) continue;
            access.options.credentials.password = password;
        }
        mailBackend_->SetAccount(access);
        ++usable;
    }
    vault.Lock();   // the secrets are in the backend now; drop the key
    return usable;
}

int EmailCleanerApp::RegisterOwnAccounts() {
    std::vector<UltraMail::Account> own;
    if (!ownAccounts_.IsOpen() || !ownAccounts_.List(own) || own.empty()) return 0;

    UltraVault::DeviceKeyVault vault(ownAccounts_.VaultDir(), kOwnVaultProfile);
    if (!vault.TryAutoUnlock()) return 0;
    int usable = 0;
    for (const UltraMail::Account& account : own) {
        std::string password;
        if (!vault.Retrieve(account.accountId, password) || password.empty()) continue;
        RegisterOwnAccount(account, password);
        ++usable;
    }
    vault.Lock();
    return usable;
}

void EmailCleanerApp::RegisterOwnAccount(const UltraMail::Account& account,
                                         const std::string& password) {
    if (!mailBackend_) return;
    MailAccountAccess access;
    access.accountId    = account.accountId;
    access.serverUrl    = UltraMail::AutoDiscovery::ImapServerUrl(account.imap);
    access.ownerAddress = account.email;
    access.options      = SessionOptionsFor(account, password);
    mailBackend_->SetAccount(access);
}

std::string EmailCleanerApp::OwnPassword(const std::string& accountId) const {
    UltraVault::DeviceKeyVault vault(ownAccounts_.VaultDir(), kOwnVaultProfile);
    std::string password;
    if (vault.TryAutoUnlock()) {
        vault.Retrieve(accountId, password);
        vault.Lock();
    }
    return password;
}

std::string EmailCleanerApp::CacheDirFor(const std::string& accountId) const {
    for (const StoredAccount& account : accounts_) {
        if (account.accountId == accountId && account.source == AccountSource::Own)
            return ownAccounts_.MailCacheDir();
    }
    return mailCacheDir_;
}

void EmailCleanerApp::LoadRules() {
    RuleSet rules = RuleSet::BuiltIn();

    // A user file adds to the built-ins rather than replacing them, so editing
    // it can only ever sharpen the detection. On the first run, write the
    // built-in table out so there is something to edit.
    std::error_code ec;
    if (std::filesystem::exists(PathFromUtf8(rulesPath_), ec)) {
        RuleSet user;
        if (user.LoadFile(rulesPath_)) rules.Merge(user);
    } else {
        RuleSet().SaveFile(rulesPath_);
    }
    ingestor_.SetClassifier(Classifier(std::move(rules)));
}

void EmailCleanerApp::ImportAccounts() {
    // Read UltraMail's account list. Opening its store read-only would be
    // nicer, but Stage 1 UltraDatabase has no read-only mode for SQLite, and
    // this only ever reads.
    std::error_code ec;
    const std::string mailDb = mailDataDir_ + "/mail.db";
    int imported = 0;

    if (std::filesystem::exists(PathFromUtf8(mailDb), ec)) {
        UltraMail::LocalStore mailStore;
        if (mailStore.Open("emailcleaner-mailaccounts", mailDb)) {
            std::vector<UltraMail::Account> mailAccounts;
            if (mailStore.ListAccounts(mailAccounts)) {
                for (const UltraMail::Account& account : mailAccounts) {
                    store_.UpsertAccount(ToStoredAccount(account, AccountSource::UltraMail));
                    ultraMailAccounts_[account.accountId] = account;
                    std::vector<UltraMail::Folder> folders;
                    if (mailStore.ListFolders(account.accountId, folders)) {
                        for (const UltraMail::Folder& folder : folders) {
                            if (folder.role == UltraMail::FolderRole::Trash)
                                ultraMailTrashDirs_[account.accountId].insert(
                                    CacheDirectoryName(account.accountId, folder.name));
                        }
                    }
                    ++imported;
                }
            }
        }
        // Let go of UltraMail's database: the account list is mirrored now,
        // and holding the file open would keep a second writer on it.
        UltraDb_CloseConnection("emailcleaner-mailaccounts");
    }

    // EmailCleaner's own accounts, which need no UltraMail at all.
    std::vector<UltraMail::Account> own;
    if (ownAccounts_.IsOpen() && ownAccounts_.List(own)) {
        for (const UltraMail::Account& account : own)
            store_.UpsertAccount(ToStoredAccount(account, AccountSource::Own));
    }

    if (imported > 0) return;

    // No account list to mirror — a mailbox copied over for analysis, with
    // EMAILCLEANER_MAIL_DIR pointing at it. The cache layout still names the
    // accounts: one directory per account under <mail dir>/mail. Take them
    // from there so the corpus can be loaded without UltraMail present.
    const std::filesystem::path cacheRoot = PathFromUtf8(mailCacheDir_);
    if (!std::filesystem::is_directory(cacheRoot, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(cacheRoot, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        StoredAccount stored;
        stored.accountId   = PathToUtf8(entry.path().filename());
        stored.displayName = stored.accountId;
        stored.shortName   = stored.accountId;
        // The owner address is unknown here, which only means "addressed to
        // me" cannot be scored — every other signal still applies.
        store_.UpsertAccount(stored);
    }
}

std::shared_ptr<UltraCanvasWindow> EmailCleanerApp::CreateMainWindow() {
    WindowConfig config;
    config.title  = "EmailCleaner " EMAILCLEANER_VERSION;
    config.width  = static_cast<int>(kWindowW);
    config.height = static_cast<int>(kWindowH);
    window_ = CreateWindow(config);

    // ---- Account bar -------------------------------------------------------
    auto bar = accountBar_.Build(0, 0, kWindowW, kBarH);
    accountBar_.onFilterChanged = [this]() { Refresh(); };
    accountBar_.onScan          = [this]() { ScanMailCache(); };
    accountBar_.onReanalyse     = [this]() { Reanalyse(); };
    accountBar_.onEditRules     = [this]() { EditRules(); };
    accountBar_.onManageAccounts = [this]() { ManageAccounts(); };
    window_->AddChild(bar);

    // ---- Views -------------------------------------------------------------
    const float tabsY = kBarH;
    const float tabsH = kWindowH - tabsY;
    tabs_ = CreateTabbedContainer("ecTabs", 0, tabsY, kWindowW, tabsH);

    // The tab strip eats some height; give the pages what is left.
    const float pageW = kWindowW - 8;
    const float pageH = tabsH - 44;

    mapView_.SetAnalytics(&analytics_);
    mapView_.onBlockSelected = [this](const std::string& sender, const std::string& domain) {
        selectedSender_ = sender;
        selectedDomain_ = domain;
        // The map keeps showing everything; only the scoped views follow the
        // selection, which is what makes the map a navigation surface.
        timetableView_.Refresh(CurrentFilter(), CurrentTitle());
        detailView_.Refresh(CurrentFilter(), CurrentTitle());
        actionsPanel_.SetTarget(CurrentTarget(), accountBar_.Filter().accountId);
    };
    tabs_->AddTab("Sender map", mapView_.Build(0, 0, pageW, pageH));

    timetableView_.SetAnalytics(&analytics_);
    tabs_->AddTab("Timetable", timetableView_.Build(0, 0, pageW, pageH));

    // The Messages page is the detail view with the actions panel above it:
    // what the selection contains, and what can be done about it, on one page.
    detailView_.SetStore(&store_);
    detailView_.onOpenAttachments =
        [this](const AnalyzedMessage& m) { ShowAttachments(m); };
    auto messagesPage = CreateContainer("ecMessagesPage", 0, 0, pageW, pageH);

    actionsPanel_.SetStore(&store_);
    actionsPanel_.SetBackend(mailBackend_.get());
    if (!backendUnavailable_.empty())
        actionsPanel_.SetBackendUnavailableReason(backendUnavailable_);
    actionsPanel_.onApplied = [this](const ActionOutcome& outcome) {
        // Acting changes what the corpus looks like, so every view is stale.
        Refresh();
        // Only a plan that did something has an outcome worth reporting; the
        // rest raise onStatus with their own sentence, and an empty outcome
        // must not overwrite it with "Nothing to do."
        if (outcome.blocked || outcome.unsubscribed || outcome.moved || outcome.failed)
            accountBar_.SetStatus(outcome.Describe());
    };
    actionsPanel_.onStatus = [this](const std::string& text) {
        accountBar_.SetStatus(text);
    };
    messagesPage->AddChild(actionsPanel_.Build(0, 0, pageW, ActionsPanel::kHeight));
    messagesPage->AddChild(detailView_.Build(0, ActionsPanel::kHeight, pageW,
                                             pageH - ActionsPanel::kHeight));
    tabs_->AddTab("Messages", messagesPage);

    window_->AddChild(tabs_);

    Refresh();
    return window_;
}

MessageFilter EmailCleanerApp::CurrentFilter() const {
    MessageFilter filter = accountBar_.Filter();
    if (!selectedSender_.empty())      filter.senderAddr   = selectedSender_;
    else if (!selectedDomain_.empty()) filter.senderDomain = selectedDomain_;
    return filter;
}

ActionTarget EmailCleanerApp::CurrentTarget() const {
    ActionTarget target;
    target.senderAddr = selectedSender_;
    // A sender target carries its domain too, so a plan can say which domain
    // it belongs to; a domain-only selection is what makes IsDomain() true.
    target.domain     = selectedDomain_;
    return target;
}

std::string EmailCleanerApp::CurrentTitle() const {
    if (!selectedSender_.empty()) return selectedSender_;
    if (!selectedDomain_.empty()) return "Domain " + selectedDomain_;
    return "All senders";
}

void EmailCleanerApp::Refresh() {
    store_.ListAccounts(accounts_);
    accountBar_.SetAccounts(accounts_);

    StoreOverview overview;
    if (store_.GetOverview(accountBar_.Filter(), overview))
        accountBar_.SetStatus(Analytics::DescribeOverview(overview));

    // The map always shows the whole filtered corpus — narrowing it to the
    // selected block would leave a single square with nothing to compare.
    mapView_.Refresh(accountBar_.Filter());
    timetableView_.Refresh(CurrentFilter(), CurrentTitle());
    detailView_.Refresh(CurrentFilter(), CurrentTitle());
    actionsPanel_.SetTarget(CurrentTarget(), accountBar_.Filter().accountId);
}

void EmailCleanerApp::ScanMailCache() {
    if (accounts_.empty()) {
        accountBar_.SetStatus("No mail accounts yet — add one under Accounts…, or set "
                              "one up in UltraMail, then press \"Load mail\" here.");
        return;
    }
    if (fetching_) {
        accountBar_.SetStatus("Still downloading — the map updates when it is done.");
        return;
    }
    // UltraMail's accounts are read from what UltraMail last synced. Own
    // accounts have no one else to sync them, so "Load mail" downloads what
    // is new on their server first.
    std::vector<std::string> toFetch;
    const std::string wanted = accountBar_.Filter().accountId;
    for (const StoredAccount& account : accounts_) {
        if (!wanted.empty() && account.accountId != wanted) continue;
        if (account.source == AccountSource::Own) toFetch.push_back(account.accountId);
    }
    FetchThenAnalyse(toFetch);
}

void EmailCleanerApp::FetchThenAnalyse(const std::vector<std::string>& accountIds) {
    if (accountIds.empty()) {
        AnalyseCaches(/*skipExisting=*/true, "");
        return;
    }
    // One download at a time: the account list's database is shared with it.
    if (fetching_) {
        accountBar_.SetStatus("Another download is still running — press \"Load mail\" "
                              "again when it is done.");
        return;
    }
    auto* mailbox = imapPlugin_ ? dynamic_cast<IMailboxProtocolPlugin*>(imapPlugin_.get())
                                : nullptr;
    if (!mailbox) {
        AnalyseCaches(true, "EmailCleaner's own accounts were not downloaded: " +
                            backendUnavailable_);
        return;
    }

    // Everything the worker needs is gathered here, on the UI thread: the
    // account records and their passwords (the vault is not thread-safe).
    struct Job { UltraMail::Account account; UltraNetMailOptions options; };
    std::vector<Job> jobs;
    std::vector<std::string> missing;
    for (const std::string& id : accountIds) {
        Job job;
        if (!ownAccounts_.Find(id, job.account)) continue;
        const std::string password = OwnPassword(id);
        if (password.empty()) {
            missing.push_back(job.account.email);
            continue;
        }
        job.options = SessionOptionsFor(job.account, password);
        jobs.push_back(std::move(job));
    }
    std::string report;
    if (!missing.empty()) {
        report = "No saved password for " + missing.front() +
                 (missing.size() > 1 ? " and " + std::to_string(missing.size() - 1) + " more" : "") +
                 " — remove it under Accounts… and add it again.";
    }
    if (jobs.empty()) {
        AnalyseCaches(true, report);
        return;
    }

    fetching_ = true;
    accountBar_.SetStatus(jobs.size() == 1
        ? "Downloading new mail for " + jobs.front().account.email + "…"
        : "Downloading new mail for " + std::to_string(jobs.size()) + " accounts…");

    // The plug-in is kept alive by the copy the worker holds.
    std::shared_ptr<IUltraNetPlugin> plugin = imapPlugin_;
    const std::string cacheDir = ownAccounts_.MailCacheDir();
    std::thread([this, plugin, mailbox, cacheDir, jobs, report]() {
        int bodies = 0;
        std::string failures = report;
        for (const Job& job : jobs) {
            const UltraMail::SyncOutcome outcome = FetchMailbox(
                ownAccounts_.Store(), *mailbox, cacheDir, job.account, job.options);
            if (outcome) {
                bodies += outcome.stats.bodies;
            } else {
                if (!failures.empty()) failures += " ";
                failures += job.account.email + " could not be downloaded: " +
                            outcome.message + ".";
            }
        }
        OnUiThread([this, bodies, failures]() {
            fetching_ = false;
            std::string fetched = "Downloaded " + std::to_string(bodies) + " new messages.";
            if (!failures.empty()) fetched += " " + failures;
            AnalyseCaches(/*skipExisting=*/true, fetched);
        });
    }).detach();
}

std::set<std::string> EmailCleanerApp::TrashDirsFor(const std::string& accountId) {
    if (!IsOwnAccountId(accountId)) {
        auto it = ultraMailTrashDirs_.find(accountId);
        return it == ultraMailTrashDirs_.end() ? std::set<std::string>{} : it->second;
    }
    std::set<std::string> dirs;
    std::vector<UltraMail::Folder> folders;
    if (ownAccounts_.IsOpen() && ownAccounts_.Store().ListFolders(accountId, folders)) {
        for (const UltraMail::Folder& folder : folders)
            if (folder.role == UltraMail::FolderRole::Trash)
                dirs.insert(CacheDirectoryName(accountId, folder.name));
    }
    return dirs;
}

void EmailCleanerApp::AnalyseCaches(bool skipExisting, const std::string& fetchReport) {
    IngestOptions options;
    options.skipExisting = skipExisting;
    // Mail in Trash has been dealt with - including what the actions moved
    // there - so it is not analysed: by the server's folder role where known,
    // and by name for the rest.
    options.skipTrash = true;

    IngestStats total;
    const std::string wanted = accountBar_.Filter().accountId;
    for (const StoredAccount& account : accounts_) {
        if (!wanted.empty() && account.accountId != wanted) continue;
        options.ownerAddress = account.email;
        options.skipFolders  = TrashDirsFor(account.accountId);
        total.Add(ingestor_.IngestMailCache(CacheDirFor(account.accountId),
                                            account.accountId, options));
    }

    const std::string prefix = fetchReport.empty() ? "" : fetchReport + " ";
    if (!skipExisting) {
        Refresh();
        accountBar_.SetStatus(prefix + "Re-analysed " + std::to_string(total.analysed) +
                              " messages with " +
                              std::to_string(ingestor_.GetClassifier().Rules().Size()) +
                              " rules (" + std::to_string(total.unwanted) + " unwanted).");
        return;
    }
    if (total.filesSeen == 0) {
        accountBar_.SetStatus(prefix + "No downloaded messages yet — sync the account "
                              "in UltraMail, or press \"Load mail\" for an account "
                              "added under Accounts….");
        return;
    }

    Refresh();
    accountBar_.SetStatus(prefix + "Analysed " + std::to_string(total.analysed) +
                          " new messages (" + std::to_string(total.skipped) +
                          " already known, " + std::to_string(total.unwanted) +
                          " unwanted).");
}

// ---- Own accounts ----------------------------------------------------------

std::vector<AccountsDialog::Row> EmailCleanerApp::AccountRows() const {
    std::vector<AccountsDialog::Row> rows;
    for (const StoredAccount& account : accounts_) {
        AccountsDialog::Row row;
        row.account = account;
        if (account.source == AccountSource::Own) {
            UltraMail::Account record;
            row.detail = ownAccounts_.Find(account.accountId, record)
                ? "EmailCleaner · " + record.imap.host
                : "EmailCleaner";
        } else {
            row.detail = "from UltraMail";
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void EmailCleanerApp::ManageAccounts() {
    accountsDialog_.onAdd = [this](const NewAccountRequest& request,
                                   std::function<void(const std::string&)> done) {
        AddOwnAccount(request, std::move(done));
    };
    accountsDialog_.onRemove = [this](const StoredAccount& account) {
        RemoveOwnAccount(account);
    };
    accountsDialog_.onDiscover = [](const std::string& email,
                                    std::function<void(const UltraMail::DiscoveryResult&)> done) {
        // Autoconfig is an HTTP round trip or three: off the UI thread.
        std::thread([email, done]() {
            UltraMail::AutoDiscovery discovery;
            UltraMail::DiscoveryResult result = discovery.Discover(email);
            if (!result.found) result = UltraMail::AutoDiscovery::GuessForDomain(email);
            OnUiThread([done, result]() { done(result); });
        }).detach();
    };
    accountsDialog_.Show(AccountRows());
}

void EmailCleanerApp::AddOwnAccount(const NewAccountRequest& request,
                                    std::function<void(const std::string&)> done) {
    const std::string invalid = OwnAccounts::Validate(request, accounts_);
    if (!invalid.empty()) { done(invalid); return; }
    if (!ownAccounts_.IsOpen()) {
        done("EmailCleaner's account list could not be opened in " + dataDir_ + ".");
        return;
    }
    auto* mailbox = imapPlugin_ ? dynamic_cast<IMailboxProtocolPlugin*>(imapPlugin_.get())
                                : nullptr;
    if (!mailbox) {
        done(backendUnavailable_.empty()
                 ? "The IMAP plug-in is not loaded, so the sign-in cannot be checked."
                 : backendUnavailable_);
        return;
    }

    const UltraMail::Account account = OwnAccounts::MakeAccount(request);
    const std::string password = request.password;

    // One folder listing proves the host, the port, TLS and the password —
    // the same session a download opens. Off the UI thread: it is the network.
    std::shared_ptr<IUltraNetPlugin> plugin = imapPlugin_;
    std::thread([this, plugin, mailbox, account, password, done]() {
        UltraNetCredentials credentials;
        credentials.username = account.imap.username;
        credentials.password = password;
        const UltraNetResult signIn = UltraMail::LoginCheck::Imap(*mailbox, account.imap,
                                                                  credentials);
        OnUiThread([this, signIn, account, password, done]() {
            if (!signIn) {
                // The app-password hint only when the server turned the
                // password down — not for a host that could not be reached.
                const bool refused = signIn.code == UltraNetResultCode::AuthenticationFailed ||
                                     signIn.code == UltraNetResultCode::AuthenticationRequired;
                done("The sign-in failed: " + signIn.message +
                     (refused && !account.providerName.empty()
                          ? " — for " + account.providerName + ", use an app password."
                          : "."));
                return;
            }

            // The password first: an account saved without one would fail
            // every download for no visible reason.
            UltraVault::DeviceKeyVault vault(ownAccounts_.VaultDir(), kOwnVaultProfile);
            const bool stored = vault.TryAutoUnlock() && vault.Store(account.accountId, password);
            vault.Lock();
            if (!stored) {
                done("The sign-in worked, but the password could not be stored in " +
                     ownAccounts_.VaultDir() + ", so the account was not added.");
                return;
            }
            if (UltraDbResult saved = ownAccounts_.Save(account); !saved) {
                done("The account could not be saved: " + saved.message);
                return;
            }
            store_.UpsertAccount(ToStoredAccount(account, AccountSource::Own));
            RegisterOwnAccount(account, password);
            if (mailBackend_ && !backendUnavailable_.empty()) {
                backendUnavailable_.clear();
                actionsPanel_.SetBackendUnavailableReason("");
            }

            Refresh();
            accountsDialog_.SetRows(AccountRows());
            done("");
            FetchThenAnalyse({ account.accountId });
        });
    }).detach();
}

void EmailCleanerApp::RemoveOwnAccount(const StoredAccount& account) {
    if (account.source != AccountSource::Own) return;
    if (fetching_) {
        UltraCanvasDialogManager::ShowWarning(
            "A download is running. Remove the account once it has finished.",
            "Not now", nullptr);
        return;
    }
    UltraCanvasDialogManager::ShowConfirmation(
        "Remove " + account.email + " from EmailCleaner?\n\nIts saved password, the "
        "mail EmailCleaner downloaded for it and its analysis are deleted. Nothing "
        "on the mail server changes.",
        "Remove account",
        [this, account](bool confirmed) {
            if (!confirmed) return;
            ownAccounts_.Remove(account.accountId);
            UltraVault::DeviceKeyVault vault(ownAccounts_.VaultDir(), kOwnVaultProfile);
            if (vault.TryAutoUnlock()) {
                vault.Remove(account.accountId);
                vault.Lock();
            }
            store_.RemoveAccount(account.accountId);
            Refresh();
            accountsDialog_.SetRows(AccountRows());
            accountBar_.SetStatus("Removed " + account.email + ".");
        });
}

void EmailCleanerApp::EditRules() {
    // The dialog edits the user's file only. The built-ins are the floor it is
    // layered on and stay out of reach: a rule set the user can break is one
    // they can also silently disarm.
    rulesDialog_.SetSuggestedPhrase(TopTermForSelection());
    rulesDialog_.onSaved = [this]() { Reanalyse(); };
    rulesDialog_.Show(rulesPath_, RuleSet::BuiltIn().Size());
}

std::string EmailCleanerApp::TopTermForSelection() const {
    // The strongest term behind the selected block, so adding a rule from the
    // map starts from what actually fired rather than an empty box.
    std::vector<KeywordHit> hits;
    if (!store_.GetTopKeywords(CurrentFilter(), 1, hits) || hits.empty()) return "";
    return hits.front().term;
}

void EmailCleanerApp::ShowAttachments(const AnalyzedMessage& message) {
    std::vector<AttachmentRecord> attachments;
    store_.GetAttachments(message.accountId, message.folder, message.uid, attachments);

    DialogConfig config;
    config.title      = "Attachments";
    config.width      = 640;
    config.height     = 400;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();
    dialog->layout.SetFlexColumn()
                  .SetFlexGap(10)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(14);

    auto heading = CreateLabel("ecAttHeading", 0, 0, 600, 40,
        message.subject.empty() ? "(no subject)" : message.subject);
    heading->SetWrap(TextWrap::WrapWord);
    dialog->AddChild(heading);

    auto list = CreateScrollableContainer("ecAttList", 0, 0, 600, 260);
    float y = 0.0f;
    int index = 0;
    for (const AttachmentRecord& record : attachments) {
        const std::string id = "ecAtt" + std::to_string(index++);
        auto row = CreateContainer(id, 0, y, 580, 40);

        row->AddChild(CreateLabel(id + ".name", 0, 0, 460, 19,
            record.filename.empty() ? "(unnamed)" : record.filename));

        std::string sub = record.mediaType + " · " + FormatBytes(record.sizeBytes);
        if (record.isInline) sub += " · inline";
        auto subLabel = CreateLabel(id + ".sub", 0, 19, 460, 18, sub);
        subLabel->SetTextColor(Color(96, 96, 96, 255));
        row->AddChild(subLabel);

        // Risky attachments get no button at all. An app whose job is to deal
        // with unwanted mail must not be the thing that opens the executable
        // in it — and a disabled button still invites a second try.
        const bool risky = record.risky ||
            Classifier::IsRiskyAttachment(record.filename, record.mediaType);
        if (risky) {
            auto blocked = CreateLabel(id + ".blocked", 470, 10, 110, 20, "⚠ not opened");
            blocked->SetTextColor(Color(176, 96, 0, 255));
            row->AddChild(blocked);
        } else {
            auto open = CreateButton(id + ".open", 470, 8, 96, 24, "Open");
            const AnalyzedMessage owner = message;
            const AttachmentRecord copy = record;
            open->onClick = [this, owner, copy]() { OpenAttachment(owner, copy); };
            row->AddChild(open);
        }

        list->AddChild(row);
        y += 44.0f;
    }
    if (attachments.empty()) {
        list->AddChild(CreateLabel("ecAttNone", 0, 0, 560, 20,
            "The index has no attachments for this message. Re-analyse to refresh it."));
    }
    dialog->AddChild(list);
    list->layoutItem.SetFlexGrow(1);

    auto note = CreateLabel("ecAttNote", 0, 0, 600, 36,
        "Executable, script and macro-bearing attachments are never opened or "
        "copied — they stay in the mail cache, untouched.");
    note->SetWrap(TextWrap::WrapWord);
    note->SetTextColor(Color(96, 96, 96, 255));
    dialog->AddChild(note);

    auto buttonRow = CreateContainer("ecAttButtons", 0, 0, 0, 34);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto closeBtn = std::make_shared<UltraCanvasButton>("ecAttClose", 0, 0, 90, 28);
    closeBtn->SetText("Close");
    closeBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(closeBtn);
    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, nullptr);
}

void EmailCleanerApp::OpenAttachment(const AnalyzedMessage& message,
                                     const AttachmentRecord& record) {
    // The bytes are not in the analysis database — only their description is.
    // They come back out of the .eml UltraMail cached, which is also where the
    // final refusal happens, against the part the message really carries.
    std::vector<uint8_t> bytes;
    const AttachmentFetch status = FetchAttachment(CacheDirFor(message.accountId),
                                                   message.accountId,
                                                   message.folder, message.uid,
                                                   record, bytes);
    if (status != AttachmentFetch::Ok) {
        UltraCanvasDialogManager::ShowWarning(
            DescribeFetch(status, record.filename),
            status == AttachmentFetch::RefusedRisky ? "Not opened" : "Could not open",
            nullptr);
        return;
    }

    const std::string path = WriteToCache(dataDir_ + "/attachments",
                                          record.filename, record.mediaType, bytes);
    if (path.empty()) {
        UltraCanvasDialogManager::ShowWarning(
            "Could not write the attachment where the viewer can reach it.",
            "Could not open", nullptr);
        return;
    }

    WindowConfig cfg;
    cfg.title  = record.filename.empty() ? "Attachment" : record.filename;
    cfg.width  = 900;
    cfg.height = 680;
    auto win = CreateWindow(cfg);

    auto viewer = CreateMediaViewer("ecAttViewer", 0, 0,
                                    static_cast<float>(cfg.width),
                                    static_cast<float>(cfg.height));
    win->AddChild(viewer);
    viewer->OpenFile(path);
    win->Show();

    viewerWindows_.push_back(win);   // keep the window alive
}

void EmailCleanerApp::Reanalyse() {
    // Re-read the rules first: this is the button you press after editing them.
    LoadRules();
    AnalyseCaches(/*skipExisting=*/false, "");
}

} // namespace EmailCleaner
