// Apps/UltraMail/ui/UltraMailApp.cpp
// Version: 0.2.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailApp.h"

#include "UltraMailAttachmentCache.h"
#include "UltraMailDiscovery.h"
#include "UltraMailCredentialVault.h"
#include "UltraMailComposer.h"
#include "UltraMailSender.h"
#include "UltraMailContactCollector.h"
#include "UltraMailSyncService.h"

#include <UltraCloud/UltraCloudMemory.h>

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasModalDialog.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetMime.h>

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr int   kWindowWidth   = 1180;
constexpr int   kWindowHeight  = 760;
constexpr float kViewPadding   = 12.0f;
constexpr float kActionsWidth  = 150.0f;
constexpr float kActionHeight  = 28.0f;
constexpr float kActionGap     = 6.0f;
constexpr int   kActionIcon    = 16;

std::string IconPath(const std::string& name) {
    return UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/icons/" + name);
}
} // namespace

std::string UltraMailApp::LocalPart(const std::string& email) {
    auto at = email.find('@');
    return at == std::string::npos ? email : email.substr(0, at);
}

std::string UltraMailApp::SlugFromEmail(const std::string& email) {
    std::string slug;
    for (char c : email) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else
            slug.push_back('-');
    }
    return slug;
}

bool UltraMailApp::Initialize(const std::string& dataDir) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    const std::string dbPath = dataDir + "/mail.db";

    UltraDbResult opened = store_.Open("ultramail", dbPath);
    if (!opened) return false;

    dataDir_  = dataDir;
    cacheDir_ = dataDir + "/cache";
    mailDir_  = dataDir + "/mail";

    // The address book + outbox are global (account-independent) stores.
    contacts_.Open("ultramail-contacts", dataDir + "/contacts.db");
    outbox_.Open("ultramail-outbox", dataDir + "/outbox.db");

    // Cloud storage accounts (UltraCloud) for "Attach cloud link".
    UltraCloud::RegisterBuiltInProviders();
    cloudAccounts_.Open("ultramail-cloud", dataDir + "/cloud.db");
    cloudSecrets_ = std::make_unique<UltraCloud::FileSecretStore>(dataDir + "/cloud-vault");
    cloud_ = std::make_unique<UltraCloud::CloudService>(cloudAccounts_, *cloudSecrets_);

    // Bring up the UltraNet plug-in registry so the SMTP / IMAP DSOs load if
    // they are on the plug-in path (ULTRAMAIL_PLUGIN_DIR overrides the default).
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();
    if (const char* pd = std::getenv("ULTRAMAIL_PLUGIN_DIR"); pd && *pd)
        UltraNet_SetPluginDirectory(pd);
    UltraNet_RefreshPlugins();

    store_.ListAccounts(accounts_);
    store_.GetAccountStatus(status_);
    return true;
}

std::shared_ptr<UltraCanvasWindow> UltraMailApp::CreateMainWindow() {
    WindowConfig config;
    config.title  = "UltraMail";
    config.width  = kWindowWidth;
    config.height = kWindowHeight;
    window_ = CreateWindow(config);

    const float w = static_cast<float>(config.width);
    const float h = static_cast<float>(config.height);

    // Start page — the only thing on screen until the first account exists:
    // logo, app title and the "Add email account" button.
    auto start = startPage_.Build();
    startPage_.onAddAccount = [this]() { HandleAddAccount(); };
    window_->AddChild(start);

    // Account view — actions column + account bar on top, inbox | message below.
    window_->AddChild(BuildAccountView(w, h));

    // Both views are sized to the client area so their layouts follow the window.
    ResizeViews(w, h);
    window_->onWindowResize = [this](int cw, int ch) {
        ResizeViews(static_cast<float>(cw), static_cast<float>(ch));
    };

    Refresh();

    // Register accounts for background sync (the live loop starts only when the
    // IMAP plug-in is present).
    StartBackgroundSync();

    // Demo path: seed mail, auto-collect senders, and open the contact manager.
    if (const char* dcol = std::getenv("ULTRAMAIL_DEMO_COLLECT"); dcol && *dcol == '1') {
        SeedDemoMail();
        OpenContacts();
    }
    // Demo path: seed contacts and open the contact manager.
    if (const char* dc = std::getenv("ULTRAMAIL_DEMO_CONTACTS"); dc && *dc == '1') {
        SeedDemoContacts();
        OpenContacts();
    }
    // Demo path: run the add-account flow for a given address (exercises
    // discovery + the credential vault + the result dialog).
    if (const char* addEmail = std::getenv("ULTRAMAIL_DEMO_ADD"); addEmail && *addEmail) {
        AccountDraft d;
        d.email = addEmail;
        d.password = "demo-password";
        HandleWizardSubmit(d);
    }
    // Demo path: seed messages + bodies (the main window shows them).
    if (const char* dm = std::getenv("ULTRAMAIL_DEMO_MAIL"); dm && *dm == '1') {
        SeedDemoMail();
        Refresh();
    }
    // Demo path: send a draft (exercises the outbox queue + result dialog).
    if (const char* ds = std::getenv("ULTRAMAIL_DEMO_SEND"); ds && *ds == '1') {
        Draft d = Composer::NewMessage("Erika Example", "erika@gmail.com");
        d.to = {"bob@example.com"};
        d.subject = "Hello from UltraMail";
        d.body = "This message was queued through the persistent outbox.";
        HandleSendDraft(d);
    }
    // Demo path: an in-memory cloud account with files, then a compose window
    // (exercises Attach file / Attach cloud link without a server).
    if (const char* dcl = std::getenv("ULTRAMAIL_DEMO_CLOUD"); dcl && *dcl) {
        SeedDemoCloud();
        OpenComposer(Composer::NewMessage("Erika Example", "erika@example.com"));
        if (*dcl == '2') composeView_.OpenCloudLinkPicker();   // =2: picker open too
    }
    // Demo path: open a reply-prefilled compose window.
    if (const char* dcomp = std::getenv("ULTRAMAIL_DEMO_COMPOSE"); dcomp && *dcomp == '1') {
        SourceMessage src;
        src.messageId = "<orig@example.com>";
        src.fromName = "Anna Schmidt"; src.fromAddr = "anna@example.com";
        src.to = {"erika@example.com"};
        src.subject = "Meeting notes";
        src.body = "Hi Erika,\n\nHere are the notes from our meeting.\n\nBest,\nAnna";
        src.date = "Tue, 14 Jan 2026 14:02:00 +0000";
        OpenComposer(Composer::Reply(src, "Erika Example", "erika@example.com", false));
    }

    return window_;
}

std::shared_ptr<UltraCanvasContainer> UltraMailApp::BuildAccountView(float width, float height) {
    accountView_ = CreateContainer("accountView", 0, 0, width, height);
    accountView_->SetPadding(kViewPadding);
    accountView_->layout.SetFlexColumn()
                        .SetFlexGap(kViewPadding)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // ----- Top row: actions column on the left, account bar on the right -----
    // Auto height: as tall as the actions column or the account bar's content
    // (the summary strip stretches to the column; tiles set their own height).
    auto top = CreateContainer("umTopRow", 0, 0, 0, 0);
    top->layout.SetFlexRow()
               .SetFlexGap(16)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto actions = CreateContainer("umActions", 0, 0, kActionsWidth, 0);
    actions->layout.SetFlexColumn()
                   .SetFlexGap(kActionGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    auto makeAction = [&](const std::string& id, const std::string& text,
                          const std::string& icon, std::function<void()> onClick) {
        auto button = CreateButton(id, 0, 0, kActionsWidth, kActionHeight, text);
        if (!icon.empty()) {
            button->SetIcon(IconPath(icon));
            button->SetIconPosition(ButtonIconPosition::Left);
            button->SetIconSize(kActionIcon, kActionIcon);
            button->SetUseIconAsMask(true);
        }
        button->SetTextAlign(TextAlignment::Left);
        button->onClick = std::move(onClick);
        actions->AddChild(button);
        return button;
    };
    makeAction("umNewEmail", "New email", "envelope.svg", [this]() {
        std::string name, addr;
        for (const auto& a : accounts_)
            if (a.accountId == selectedAccount_) { name = a.displayName; addr = a.email; }
        if (addr.empty() && !accounts_.empty()) {
            name = accounts_.front().displayName; addr = accounts_.front().email;
        }
        OpenComposer(Composer::NewMessage(name, addr));
    });
    reloadButton_ = makeAction("umReload", "Reload email", "reload.svg",
                               [this]() { HandleReload(); });
    makeAction("umContacts", "Contacts", "", [this]() { OpenContacts(); });
    makeAction("umAddAccount", "Add account", "", [this]() { HandleAddAccount(); });
    top->AddChild(actions);

    auto bar = accountBar_.Build();
    accountBar_.onSelectAccount = [this](const std::string& accountId) {
        selectedAccount_ = accountId;
        Refresh();
    };
    top->AddChild(bar);
    bar->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    accountView_->AddChild(top);
    top->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // ----- Mail area: inbox table | message details -----
    mailView_.SetStore(&store_);
    mailView_.SetMailDir(mailDir_);
    mailView_.onOpenAttachment = [this](const Attachment& a) { OpenAttachment(a); };
    mailView_.onReply = [this](const SourceMessage& src, const std::string& selfName,
                               const std::string& selfAddr) {
        OpenComposer(Composer::Reply(src, selfName, selfAddr, /*replyAll=*/false));
    };
    auto mail = mailView_.Build();
    accountView_->AddChild(mail);
    mail->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    return accountView_;
}

void UltraMailApp::ResizeViews(float width, float height) {
    startPage_.Resize(width, height);
    if (accountView_) accountView_->SetElementSize(Size2Df(width, height));
}

void UltraMailApp::HandleReload() {
    RunSyncs(/*force=*/true);
    Refresh();
}

void UltraMailApp::OpenComposer(const Draft& draft) {
    WindowConfig cfg;
    cfg.title  = draft.subject.empty() ? "New message" : draft.subject;
    cfg.width  = 680;
    cfg.height = 580;
    auto win = CreateWindow(cfg);

    composeView_.SetDraft(draft);
    composeView_.SetParentWindow(win.get());
    composeView_.SetCloud(cloud_.get());
    composeView_.onSend   = [this](const Draft& d) { HandleSendDraft(d); };
    composeView_.onCancel = []() { /* window stays; close via title bar */ };
    win->AddChild(composeView_.Build());
    win->Show();
    viewerWindows_.push_back(win);
}

void UltraMailApp::HandleSendDraft(const Draft& draft) {
    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (std::size_t i = 0; i < v.size(); ++i) { if (i) s += ", "; s += v[i]; }
        return s;
    };

    DiscoveryResult disc = AutoDiscovery::FromPresets(draft.fromAddr);
    const std::string smtpUrl = disc.found ? AutoDiscovery::SmtpServerUrl(disc.smtp) : "";

    // Always queue to the persistent outbox first (survives restarts / offline).
    int64_t id = 0;
    outbox_.Enqueue(SlugFromEmail(draft.fromAddr), smtpUrl, draft, id);

    // Remember the people we write to.
    for (const auto& addr : draft.to) ContactCollector::Collect(contacts_, "", addr);
    for (const auto& addr : draft.cc) ContactCollector::Collect(contacts_, "", addr);

    // Attempt an immediate flush if the SMTP plug-in is loaded.
    auto plugin = UltraNet_GetPlugin(disc.smtp.security == MailSecurity::SslTls ? "smtps" : "smtp");
    IMailProtocolPlugin* smtp = plugin ? dynamic_cast<IMailProtocolPlugin*>(plugin.get()) : nullptr;

    std::string msg;
    if (smtp && !smtpUrl.empty()) {
        CredentialVault vault(dataDir_ + "/vault");
        Outbox ob(outbox_);
        auto stats = ob.Flush(*smtp, [&vault](const std::string& acc) {
            std::string p; vault.Retrieve(acc, p); return p;
        });
        if (stats.sent > 0)
            msg = "Message sent to " + join(draft.to) + ".";
        else
            msg = "Send failed — the message is queued in the outbox and will be retried.";
    } else {
        int n = 0; outbox_.PendingCount(n);
        msg = "Message queued in the outbox (" + std::to_string(n) + " pending).\n"
              "It will be sent once the SMTP plug-in is on the path and you are online"
              + (smtpUrl.empty() ? "." : (" (" + smtpUrl + ")."));
    }
    UltraCanvas::UltraCanvasDialogManager::ShowInformation(
        msg, "UltraMail", nullptr, window_ ? window_.get() : nullptr);
}

void UltraMailApp::SeedDemoMail() {
    namespace fs = std::filesystem;

    Account a; a.accountId = "erika"; a.email = "erika@example.com";
    a.shortName = "erika"; a.displayName = "Erika Example";
    store_.UpsertAccount(a);
    Folder inbox; inbox.accountId = "erika"; inbox.name = "INBOX"; inbox.role = FolderRole::Inbox;
    store_.UpsertFolder(inbox);
    Folder sent; sent.accountId = "erika"; sent.name = "Sent"; sent.role = FolderRole::Sent;
    store_.UpsertFolder(sent);

    auto seed = [&](int64_t uid, const std::string& fromName, const std::string& fromAddr,
                    const std::string& subject, const std::string& body, uint32_t flags,
                    bool withAttachment, bool isHtml = false) {
        UltraNetMimeBuildInput in;
        in.from = fromName + " <" + fromAddr + ">";
        in.to = {"erika@example.com"};
        in.subject = subject;
        in.body = body;
        in.bodyMediaType = isHtml ? "text/html" : "text/plain";
        in.date = "Wed, 14 Jan 2026 1" + std::to_string(uid) + ":00:00 +0000";
        in.messageId = "<demo" + std::to_string(uid) + "@example.com>";
        if (withAttachment) {
            UltraNetMimeBuildAttachment att;
            att.filename = "meeting-notes.txt"; att.mediaType = "text/plain";
            std::string t = "Meeting notes\n\n- ship UltraMail\n- review the reading view\n";
            att.data.assign(t.begin(), t.end());
            in.attachments.push_back(att);
        }
        const std::string raw = UltraNet_MimeBuild(in);

        fs::path p = fs::path(mailDir_) / "erika" / "INBOX" / (std::to_string(uid) + ".eml");
        std::error_code ec; fs::create_directories(p.parent_path(), ec);
        std::ofstream(p, std::ios::binary).write(raw.data(),
                                                 static_cast<std::streamsize>(raw.size()));

        MessageEnvelope m;
        m.accountId = "erika"; m.folder = "INBOX"; m.uid = uid;
        m.fromName = fromName; m.fromAddr = fromAddr; m.subject = subject;
        m.to = {"erika@example.com"}; m.messageId = in.messageId; m.flags = flags;
        m.date = uid >= 5 ? static_cast<int64_t>(std::time(nullptr)) - (10 - uid) * 600
                          : 1736852400 + uid * 3600;   // uid ≥ 5: today; else ~Jan 2026
        store_.UpsertMessage(m);
    };

    seed(6, "Carol Boss", "carol@acme.com", "Budget review this afternoon",
         "Hi Erika,\n\ncan we go through the Q3 numbers at 15:00?\n\nCarol",
         Flag_None, /*withAttachment=*/false);
    seed(5, "ULTRA Store", "orders@ultra.store", "Your invoice is ready",
         "Your invoice for order #4711 is attached to your account page.", Flag_None, false);

    seed(4, "UltraCanvas News", "news@ultracanvas.dev", "UltraMail now renders HTML",
         "<html><body style=\"font-family:sans-serif;color:#222\">"
         "<h2 style=\"color:#1a4d8f\">HTML mail, rendered natively</h2>"
         "<p>UltraMail now draws HTML message bodies with the "
         "<b>UltraCanvas CSSLayout engine</b> &mdash; no browser, no web view.</p>"
         "<ul><li>Block &amp; inline layout from HTMLReader</li>"
         "<li><i>Bold</i>, <i>italic</i> and <a href=\"https://ultracanvas.dev\">links</a></li>"
         "<li>Lists, headings and colors</li></ul>"
         "<p>Welcome to the reading view.</p></body></html>",
         Flag_None, /*withAttachment=*/false, /*isHtml=*/true);
    seed(3, "Anna Schmidt", "anna@example.com", "Re: Meeting notes",
         "Hi Erika,\n\nHere are the notes from our meeting — see the attachment.\n\nBest,\nAnna",
         Flag_None, /*withAttachment=*/true);
    seed(2, "ULTRA Store", "orders@ultra.store", "Your order shipped",
         "Good news! Your order has shipped and is on its way.", Flag_Seen, false);
    seed(1, "Max Weber", "max@example.com", "Lunch on Friday?",
         "Are you free for lunch on Friday around noon?", Flag_None, false);

    // Auto-collect the senders into the address book.
    CollectContacts("erika", "INBOX");
}

void UltraMailApp::SeedDemoCloud() {
    if (!cloud_) return;
    UltraCloud::Account a;
    a.providerId  = "memory";
    a.username    = "erika";
    a.displayName = "Erika's demo cloud";
    a.isDefault   = true;
    if (!cloud_->AddAccount(a, {}, /*verify=*/false)) return;
    UltraCloud::MemoryProvider::Seed(a.accountId, "/Documents", -1);
    UltraCloud::MemoryProvider::Seed(a.accountId, "/Documents/Q3 report.pdf", 482'113, "Sep 02, 2026");
    UltraCloud::MemoryProvider::Seed(a.accountId, "/Documents/Meeting notes.md", 3'201, "Sep 03, 2026");
    UltraCloud::MemoryProvider::Seed(a.accountId, "/Photos", -1);
    UltraCloud::MemoryProvider::Seed(a.accountId, "/Photos/team.jpg", 1'904'774, "Aug 28, 2026");
    UltraCloud::MemoryProvider::Seed(a.accountId, "/Shared from ULTRA OS", -1);
    UltraCloud::MemoryProvider::Seed(a.accountId, "/invoice-4711.pdf", 88'320, "Aug 30, 2026");
}

void UltraMailApp::CollectContacts(const std::string& accountId, const std::string& folder) {
    if (!contacts_.IsOpen()) return;
    std::vector<MessageEnvelope> msgs;
    store_.ListMessages(accountId, folder, 0, msgs);
    for (const auto& m : msgs)
        ContactCollector::Collect(contacts_, m.fromName, m.fromAddr, ContactSection::Other);
}

void UltraMailApp::StartBackgroundSync() {
    // Register every account's sync cadence with the scheduler.
    for (const auto& a : accounts_) {
        DiscoveryResult d = AutoDiscovery::FromPresets(a.email);
        std::string url = d.found ? AutoDiscovery::ImapServerUrl(d.imap) : "";
        scheduler_.SetAccount(a.accountId, url, /*intervalSec=*/300);
    }
    // Only run the live loop when the IMAP plug-in is present (otherwise a timer
    // would just fire against nothing).
    auto imap = UltraNet_GetPlugin("imaps");
    if (!imap) return;
    if (auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent())
        app->StartTimer(300000, /*periodic=*/true,
                        [this](UltraCanvas::TimerId) { RunSyncs(/*force=*/false); });
}

void UltraMailApp::RunSyncs(bool force) {
    auto imapPlugin = UltraNet_GetPlugin("imaps");
    auto* imap = imapPlugin ? dynamic_cast<IMailboxProtocolPlugin*>(imapPlugin.get()) : nullptr;
    if (!imap) return;

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    CredentialVault vault(dataDir_ + "/vault");

    // Which accounts: the due ones, or all of them for a forced reload.
    std::vector<ScheduledAccount> targets;
    if (force) {
        for (const auto& a : accounts_) {
            DiscoveryResult d = AutoDiscovery::FromPresets(a.email);
            ScheduledAccount sa;
            sa.accountId = a.accountId;
            sa.serverUrl = d.found ? AutoDiscovery::ImapServerUrl(d.imap) : "";
            targets.push_back(sa);
        }
    } else {
        targets = scheduler_.DueAccounts(now);
    }

    for (const auto& acc : targets) {
        if (acc.serverUrl.empty()) continue;
        std::string email;
        for (const auto& a : accounts_) if (a.accountId == acc.accountId) email = a.email;

        UltraNetMailOptions opts;
        opts.credentials.username = email;
        std::string pw; vault.Retrieve(acc.accountId, pw);
        opts.credentials.password = pw;
        opts.useTls = true; opts.implicitTls = true;

        auto svc = std::make_shared<SyncService>(store_, *imap, mailDir_);
        const std::string aid = acc.accountId;
        if (++syncsInFlight_ == 1 && reloadButton_) reloadButton_->SetText("Reloading…");
        // onDone keeps `svc` alive until the worker thread finishes; it marshals
        // the follow-up work back to the UI thread.
        svc->SyncInBackground(aid, acc.serverUrl, opts, [this, svc, aid](SyncOutcome) {
            if (auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent())
                app->PostToUIThread([this, aid]() {
                    if (--syncsInFlight_ <= 0) {
                        syncsInFlight_ = 0;
                        if (reloadButton_) reloadButton_->SetText("Reload email");
                    }
                    CollectContacts(aid, "INBOX");
                    Refresh();
                });
        });
        scheduler_.MarkSynced(acc.accountId, now);
    }
}

void UltraMailApp::OpenContacts() {
    if (!contacts_.IsOpen()) return;
    WindowConfig cfg;
    cfg.title  = "Contacts";
    cfg.width  = 640;
    cfg.height = 520;
    auto win = CreateWindow(cfg);

    contactsView_.SetStore(&contacts_);
    win->AddChild(contactsView_.Build());
    win->Show();
    viewerWindows_.push_back(win);
}

void UltraMailApp::SeedDemoContacts() {
    std::vector<SectionCount> counts;
    if (contacts_.GetSectionCounts(counts)) {
        int total = 0;
        for (auto& c : counts) total += c.count;
        if (total > 0) return;   // already seeded
    }
    auto add = [&](const std::string& name, ContactSection section,
                   const std::string& email, const std::string& phone,
                   const std::string& org) {
        Contact c; c.displayName = name; c.section = section; c.organization = org;
        if (!email.empty()) { ContactEmail e; e.address = email; e.primary = true; c.emails.push_back(e); }
        if (!phone.empty()) { ContactPhone p; p.number = phone; p.label = "mobile"; c.phones.push_back(p); }
        contacts_.Save(c);
    };
    add("Mum",          ContactSection::Family,   "mum@example.com",   "+49 170 1112222", "");
    add("Brother Tom",  ContactSection::Family,   "tom@example.com",   "+49 151 3334444", "");
    add("Anna Schmidt", ContactSection::Friends,  "anna@example.com",  "+49 160 5556666", "");
    add("Max Weber",    ContactSection::Friends,  "max@example.com",   "",                "");
    add("Carol Boss",   ContactSection::Work,     "carol@acme.com",    "+49 30 1234567",  "Acme GmbH");
    add("IT Helpdesk",  ContactSection::Work,     "help@acme.com",     "",                "Acme GmbH");
    add("Chess Club",   ContactSection::Leisure,  "info@chessclub.org","",                "");
    add("Plumber",      ContactSection::Services, "service@plumb.example", "+49 40 7654321", "Plumb & Co");
    add("Electricity",  ContactSection::Services, "billing@power.example", "",             "PowerCo");
}

void UltraMailApp::OpenAttachment(const Attachment& attachment) {
    AttachmentCache cache(cacheDir_);
    const std::string path = cache.Write(attachment);
    if (path.empty()) return;

    WindowConfig cfg;
    cfg.title  = attachment.filename.empty() ? "Attachment" : attachment.filename;
    cfg.width  = 900;
    cfg.height = 680;
    auto win = CreateWindow(cfg);

    auto viewer = CreateMediaViewer("attachmentViewer", 0, 0,
                                    static_cast<float>(cfg.width),
                                    static_cast<float>(cfg.height));
    win->AddChild(viewer);
    viewer->OpenFile(path);
    win->Show();

    viewerWindows_.push_back(win);   // keep the window alive
}

void UltraMailApp::Refresh() {
    store_.ListAccounts(accounts_);
    store_.GetAccountStatus(status_);

    // Keep the selection on an existing account (default: the first one).
    bool selectedExists = false;
    for (const auto& a : accounts_) if (a.accountId == selectedAccount_) selectedExists = true;
    if (!selectedExists) selectedAccount_ = accounts_.empty() ? "" : accounts_.front().accountId;

    accountBar_.Rebuild(accounts_, status_, selectedAccount_);
    mailView_.SetAccounts(accounts_);
    mailView_.ShowAccount(selectedAccount_);

    // No account yet → only the start page; otherwise only the account view.
    const bool firstRun = accounts_.empty();
    if (auto page = startPage_.Container()) page->SetVisible(firstRun);
    if (accountView_) accountView_->SetVisible(!firstRun);
}

void UltraMailApp::HandleAddAccount() {
    AccountWizard::Show(window_.get(),
        [this](const AccountDraft& draft) { HandleWizardSubmit(draft); });
}

void UltraMailApp::HandleWizardSubmit(const AccountDraft& draft) {
    Account a;
    a.accountId   = SlugFromEmail(draft.email);
    a.email       = draft.email;
    a.displayName = draft.displayName.empty() ? LocalPart(draft.email) : draft.displayName;
    a.shortName   = LocalPart(draft.email);

    if (!store_.UpsertAccount(a)) return;

    // Auto-discover the server settings from the address (offline provider
    // presets; the wizard's network autoconfig + login verify run in the
    // engine's AutoDiscovery::Discover).
    DiscoveryResult disc = AutoDiscovery::FromPresets(draft.email);

    // Store the password out of the config, in the credential vault.
    CredentialVault vault(dataDir_ + "/vault");
    if (!draft.password.empty()) vault.Store(a.accountId, draft.password);

    // Seed the inbox so the tile + rollups have somewhere to hang; real folders
    // arrive from the first IMAP sync (SyncEngine).
    Folder inbox;
    inbox.accountId = a.accountId;
    inbox.name      = "INBOX";
    inbox.role      = FolderRole::Inbox;
    store_.UpsertFolder(inbox);

    Refresh();

    // Report what discovery found.
    std::string msg;
    if (disc.found) {
        msg = "Account ready — settings detected (" + disc.displayName + ").\n"
              "Incoming (IMAP): " + AutoDiscovery::ImapServerUrl(disc.imap) + "\n"
              "Outgoing (SMTP): " + AutoDiscovery::SmtpServerUrl(disc.smtp);
        if (disc.imap.oauth) msg += "\nSign-in: OAuth2 (browser)";
    } else {
        msg = "Account added. Server settings could not be auto-detected from "
              "the address — network autoconfig or manual setup will follow.";
    }
    UltraCanvas::UltraCanvasDialogManager::ShowInformation(
        msg, "UltraMail", nullptr, window_ ? window_.get() : nullptr);
}

} // namespace UltraMail
