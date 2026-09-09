// Apps/EmailCleaner/ui/EmailCleanerApp.cpp
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerApp.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasLabel.h"

#include "UltraMailCredentialVault.h"
#include "UltraMailDiscovery.h"
#include "UltraMailLocalStore.h"

#include <UltraDatabase/UltraDatabase.h>
#include <UltraNet/UltraNetPlugins.h>

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {
constexpr float kWindowW = 1200.0f;
constexpr float kWindowH = 800.0f;
constexpr float kBarH    = 62.0f;
} // namespace

bool EmailCleanerApp::Initialize(const std::string& dataDir,
                                 const std::string& mailDataDir) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);

    const UltraDbResult opened = store_.Open("emailcleaner", dataDir + "/analysis.db");
    if (!opened) return false;

    dataDir_      = dataDir;
    mailDataDir_  = mailDataDir;
    // UltraMail caches raw bodies under <its data dir>/mail/<account>/<folder>.
    mailCacheDir_ = mailDataDir + "/mail";
    rulesPath_    = dataDir + "/rules.txt";

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
        backendUnavailable_ = "The IMAP plug-in is not loaded, so EmailCleaner "
                              "cannot reach the mail server.";
        return;
    }

    mailBackend_ = std::make_unique<MailBackend>(*mailbox);

    // One entry per account: where its server is, and the password UltraMail
    // already holds. An account with neither is simply not registered, and the
    // backend then refuses its messages by name rather than failing obscurely.
    UltraMail::CredentialVault vault(mailDataDir_ + "/vault");
    int usable = 0;
    for (const StoredAccount& account : accounts_) {
        if (account.email.empty()) continue;
        const UltraMail::DiscoveryResult discovered =
            UltraMail::AutoDiscovery::FromPresets(account.email);
        if (!discovered.found || !discovered.imap.Valid()) continue;

        std::string password;
        if (!vault.Retrieve(account.accountId, password) || password.empty()) continue;

        MailAccountAccess access;
        access.accountId    = account.accountId;
        access.serverUrl    = UltraMail::AutoDiscovery::ImapServerUrl(discovered.imap);
        access.ownerAddress = account.email;
        access.options.credentials.username =
            discovered.imap.username.empty() ? account.email : discovered.imap.username;
        access.options.credentials.password = password;
        access.options.useTls      = discovered.imap.security != UltraMail::MailSecurity::None;
        access.options.implicitTls = discovered.imap.security == UltraMail::MailSecurity::SslTls;
        mailBackend_->SetAccount(access);
        ++usable;
    }

    if (usable == 0) {
        backendUnavailable_ = "No account has a server and a saved password — "
                              "set the account up in UltraMail first.";
    } else {
        backendUnavailable_.clear();
    }
}

void EmailCleanerApp::LoadRules() {
    RuleSet rules = RuleSet::BuiltIn();

    // A user file adds to the built-ins rather than replacing them, so editing
    // it can only ever sharpen the detection. On the first run, write the
    // built-in table out so there is something to edit.
    std::error_code ec;
    if (std::filesystem::exists(rulesPath_, ec)) {
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

    if (std::filesystem::exists(mailDb, ec)) {
        UltraMail::LocalStore mailStore;
        if (mailStore.Open("emailcleaner-mailaccounts", mailDb)) {
            std::vector<UltraMail::Account> mailAccounts;
            if (mailStore.ListAccounts(mailAccounts)) {
                for (const UltraMail::Account& account : mailAccounts) {
                    StoredAccount stored;
                    stored.accountId   = account.accountId;
                    stored.displayName = account.displayName;
                    stored.email       = account.email;
                    stored.shortName   = account.shortName;
                    store_.UpsertAccount(stored);
                    ++imported;
                }
            }
        }
        // Let go of UltraMail's database: the account list is mirrored now,
        // and holding the file open would keep a second writer on it.
        UltraDb_CloseConnection("emailcleaner-mailaccounts");
    }

    if (imported > 0) return;

    // No account list to mirror — a mailbox copied over for analysis, with
    // EMAILCLEANER_MAIL_DIR pointing at it. The cache layout still names the
    // accounts: one directory per account under <mail dir>/mail. Take them
    // from there so the corpus can be loaded without UltraMail present.
    if (!std::filesystem::is_directory(mailCacheDir_, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(mailCacheDir_, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        StoredAccount stored;
        stored.accountId   = entry.path().filename().string();
        stored.displayName = stored.accountId;
        stored.shortName   = stored.accountId;
        // The owner address is unknown here, which only means "addressed to
        // me" cannot be scored — every other signal still applies.
        store_.UpsertAccount(stored);
    }
}

std::shared_ptr<UltraCanvasWindow> EmailCleanerApp::CreateMainWindow() {
    WindowConfig config;
    config.title  = "EmailCleaner";
    config.width  = static_cast<int>(kWindowW);
    config.height = static_cast<int>(kWindowH);
    window_ = CreateWindow(config);

    // ---- Account bar -------------------------------------------------------
    auto bar = accountBar_.Build(0, 0, kWindowW, kBarH);
    accountBar_.onFilterChanged = [this]() { Refresh(); };
    accountBar_.onScan          = [this]() { ScanMailCache(); };
    accountBar_.onReanalyse     = [this]() { Reanalyse(); };
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
    auto messagesPage = CreateContainer("ecMessagesPage", 0, 0, pageW, pageH);

    actionsPanel_.SetStore(&store_);
    actionsPanel_.SetBackend(mailBackend_.get());
    if (!backendUnavailable_.empty())
        actionsPanel_.SetBackendUnavailableReason(backendUnavailable_);
    actionsPanel_.onApplied = [this](const ActionOutcome& outcome) {
        // Acting changes what the corpus looks like, so every view is stale.
        Refresh();
        if (!outcome.Describe().empty()) accountBar_.SetStatus(outcome.Describe());
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
    IngestOptions options;
    options.skipExisting = true;

    IngestStats total;
    const std::string wanted = accountBar_.Filter().accountId;
    for (const StoredAccount& account : accounts_) {
        if (!wanted.empty() && account.accountId != wanted) continue;
        options.ownerAddress = account.email;
        total.Add(ingestor_.IngestMailCache(mailCacheDir_, account.accountId, options));
    }

    if (accounts_.empty()) {
        accountBar_.SetStatus("No mail accounts found — set one up in UltraMail first, "
                              "then press \"Load mail\" here.");
        return;
    }
    if (total.filesSeen == 0) {
        accountBar_.SetStatus("No cached messages under " + mailCacheDir_ +
                              " — sync the account in UltraMail first.");
        return;
    }

    Refresh();
    accountBar_.SetStatus("Analysed " + std::to_string(total.analysed) + " new messages (" +
                          std::to_string(total.skipped) + " already known, " +
                          std::to_string(total.unwanted) + " unwanted). " +
                          "Load mail again after the next sync.");
}

void EmailCleanerApp::Reanalyse() {
    // Re-read the rules first: this is the button you press after editing them.
    LoadRules();

    IngestOptions options;
    options.skipExisting = false;

    IngestStats total;
    const std::string wanted = accountBar_.Filter().accountId;
    for (const StoredAccount& account : accounts_) {
        if (!wanted.empty() && account.accountId != wanted) continue;
        options.ownerAddress = account.email;
        total.Add(ingestor_.IngestMailCache(mailCacheDir_, account.accountId, options));
    }

    Refresh();
    accountBar_.SetStatus("Re-analysed " + std::to_string(total.analysed) +
                          " messages with " +
                          std::to_string(ingestor_.GetClassifier().Rules().Size()) +
                          " rules (" + std::to_string(total.unwanted) + " unwanted).");
}

} // namespace EmailCleaner
