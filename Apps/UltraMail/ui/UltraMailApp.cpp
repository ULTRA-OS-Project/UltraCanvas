// Apps/UltraMail/ui/UltraMailApp.cpp
// Version: 0.5.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailApp.h"

#include "UltraMailAlerts.h"

#include "UltraMailAttachmentCache.h"
#include "UltraMailDiscovery.h"
#include "UltraMailCredentialVault.h"
#include "UltraMailComposer.h"
#include "UltraMailSender.h"
#include "UltraMailContactCollector.h"
#include "UltraMailSyncService.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasFileLoader.h"
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

UltraMailApp::~UltraMailApp() {
    vault_.Lock();   // wipe the derived key and decrypted secrets
}

bool UltraMailApp::Initialize(const std::string& dataDir, std::string* outError) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    if (ec && outError) *outError = ec.message();
    const std::string dbPath = dataDir + "/mail.db";

    UltraDbResult opened = store_.Open("ultramail", dbPath);
    if (!opened) {
        if (outError) *outError = DetailLine(opened);
        return false;
    }

    dataDir_  = dataDir;
    cacheDir_ = dataDir + "/cache";
    mailDir_  = dataDir + "/mail";
    // The credential vault stays locked until the user supplies the master
    // password; nothing reads or writes a secret before then.
    vault_ = CredentialVault(dataDir + "/vault");

    // The address book + outbox are global (account-independent) stores. A
    // failure here is not fatal — the rest of the client still works — but it
    // must not be silent: remember it so the feature that needs the store can
    // say what went wrong instead of doing nothing.
    if (UltraDbResult c = contacts_.Open("ultramail-contacts", dataDir + "/contacts.db"); !c)
        contactsError_ = DetailLine(c);
    if (UltraDbResult o = outbox_.Open("ultramail-outbox", dataDir + "/outbox.db"); !o)
        outboxError_ = DetailLine(o);

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
    mailView_.onSaveAttachment = [this](const Attachment& a) { SaveAttachment(a); };
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

    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;

    // ---- Validation before anything is queued -----------------------------
    if (draft.to.empty() && draft.cc.empty() && draft.bcc.empty()) {
        AlertWarning(parent, "This message has no recipient.",
                     "Add at least one address in To, Cc or Bcc before sending.");
        return;
    }
    if (!outbox_.IsOpen()) {
        AlertError(parent, "The message could not be queued for sending.",
                   outboxError_.empty()
                       ? "UltraMail's outbox database is not available."
                       : outboxError_);
        return;
    }

    DiscoveryResult disc = AutoDiscovery::FromPresets(draft.fromAddr);
    const std::string smtpUrl = disc.found ? AutoDiscovery::SmtpServerUrl(disc.smtp) : "";

    // Always queue to the persistent outbox first (survives restarts / offline).
    int64_t id = 0;
    if (UltraDbResult q = outbox_.Enqueue(SlugFromEmail(draft.fromAddr), smtpUrl, draft, id); !q) {
        AlertError(parent, "The message could not be queued for sending, so it "
                           "has not been saved.",
                   DetailLine(q));
        return;
    }

    // Remember the people we write to.
    for (const auto& addr : draft.to) ContactCollector::Collect(contacts_, "", addr);
    for (const auto& addr : draft.cc) ContactCollector::Collect(contacts_, "", addr);

    // Attempt an immediate flush if the SMTP plug-in is loaded.
    auto plugin = UltraNet_GetPlugin(disc.smtp.security == MailSecurity::SslTls ? "smtps" : "smtp");
    IMailProtocolPlugin* smtp = plugin ? dynamic_cast<IMailProtocolPlugin*>(plugin.get()) : nullptr;

    if (smtp && !smtpUrl.empty()) {
        // Sending needs the account password, so unlock first. The message is
        // already safely queued: if the user cancels, it waits in the outbox.
        EnsureVaultUnlocked([this, draft, parent, join]() {
            FlushAndReport(draft, parent, join(draft.to));
        });
        return;
    }

    // No SMTP plug-in / no server: queued, not failed — a warning, not an error.
    int n = 0; outbox_.PendingCount(n);
    AlertWarning(parent,
        "The message is queued in the outbox (" + std::to_string(n) + " pending) "
        "but was not sent yet.",
        smtp ? "No outgoing (SMTP) server is known for " + draft.fromAddr + "."
             : "The SMTP plug-in is not loaded, so UltraMail cannot reach a mail "
               "server yet. It will be sent once the plug-in is on the plug-in "
               "path and you are online"
               + (smtpUrl.empty() ? "." : (" (" + smtpUrl + ").")));
}

void UltraMailApp::FlushAndReport(const Draft& draft,
                                  UltraCanvas::UltraCanvasWindowBase* parent,
                                  const std::string& recipients) {
    DiscoveryResult disc = AutoDiscovery::FromPresets(draft.fromAddr);
    auto plugin = UltraNet_GetPlugin(disc.smtp.security == MailSecurity::SslTls ? "smtps" : "smtp");
    auto* smtp = plugin ? dynamic_cast<IMailProtocolPlugin*>(plugin.get()) : nullptr;
    if (!smtp) {
        AlertError(parent, "The message could not be sent.",
                   "The SMTP plug-in is no longer loaded.");
        return;
    }
    Outbox ob(outbox_);
    auto stats = ob.Flush(*smtp, [this](const std::string& acc) {
        std::string p; vault_.Retrieve(acc, p); return p;
    });
    if (stats.sent > 0) {
        AlertSuccess(parent, "Message sent to " + recipients + ".");
        return;
    }
    // Failed: say why. The reason came back from SMTP in stats.lastFailure
    // and is also persisted as the outbox row's last_error.
    const std::string why    = FriendlyMessage(stats.lastFailure);
    const std::string detail = DetailLine(stats.lastFailure);
    const std::string summary =
        "The message could not be sent, so it is waiting in the outbox.\n" + why;
    if (IsRetryable(stats.lastFailure)) {
        const std::string from = draft.fromAddr;
        AlertErrorRetry(parent, summary, detail,
                        [this, from]() { RetryOutbox(from); });
    } else {
        AlertError(parent, summary, detail);
    }
}

void UltraMailApp::RetryOutbox(const std::string& fromAddr) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;

    DiscoveryResult disc = AutoDiscovery::FromPresets(fromAddr);
    auto plugin = UltraNet_GetPlugin(disc.smtp.security == MailSecurity::SslTls ? "smtps" : "smtp");
    auto* smtp = plugin ? dynamic_cast<IMailProtocolPlugin*>(plugin.get()) : nullptr;
    if (!smtp) {
        AlertError(parent, "The message could not be sent.",
                   "The SMTP plug-in is no longer loaded.");
        return;
    }

    if (!vault_.IsUnlocked()) {
        EnsureVaultUnlocked([this, fromAddr]() { RetryOutbox(fromAddr); });
        return;
    }

    Outbox ob(outbox_);
    auto stats = ob.Flush(*smtp, [this](const std::string& acc) {
        std::string p; vault_.Retrieve(acc, p); return p;
    });
    if (stats.failed == 0) {
        AlertSuccess(parent, "The outbox was sent (" + std::to_string(stats.sent)
                             + " message" + (stats.sent == 1 ? "" : "s") + ").");
        return;
    }
    const std::string why = FriendlyMessage(stats.lastFailure);
    if (IsRetryable(stats.lastFailure)) {
        AlertErrorRetry(parent, "Still could not send.\n" + why,
                        DetailLine(stats.lastFailure),
                        [this, fromAddr]() { RetryOutbox(fromAddr); });
    } else {
        AlertError(parent, "Still could not send.\n" + why,
                   DetailLine(stats.lastFailure));
    }
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

    // A background timer must never raise a modal password prompt over whatever
    // the user is doing. If the vault is still locked, skip this round and say
    // so once — the next send or account change prompts in the foreground.
    if (!vault_.IsUnlocked()) {
        if (!syncErrorReported_) {
            syncErrorReported_ = true;
            AlertWarning(window_ ? window_.get() : nullptr,
                         "New mail is not being fetched yet.",
                         "Your mail account passwords are locked. Enter your "
                         "master password — sending a message or adding an "
                         "account will ask for it — and syncing resumes.");
        }
        return;
    }

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
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
        std::string pw; vault_.Retrieve(acc.accountId, pw);
        opts.credentials.password = pw;
        opts.useTls = true; opts.implicitTls = true;

        auto svc = std::make_shared<SyncService>(store_, *imap, mailDir_);
        const std::string aid = acc.accountId;
        if (++syncsInFlight_ == 1 && reloadButton_) reloadButton_->SetText("Reloading…");
        // onDone keeps `svc` alive until the worker thread finishes; it marshals
        // the follow-up work back to the UI thread.
        // The outcome carries the reason a sync failed (bad password, untrusted
        // certificate, unreachable host). Marshal it to the UI thread and say
        // so — once per run of failures, so a broken server does not raise an
        // alert on every timer tick. The in-flight count unwinds either way, so
        // the Reload button is restored even when the sync failed.
        svc->SyncInBackground(aid, acc.serverUrl, opts,
                              [this, svc, aid, email](SyncOutcome outcome) {
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) return;
            app->PostToUIThread([this, aid, email, outcome]() {
                if (--syncsInFlight_ <= 0) {
                    syncsInFlight_ = 0;
                    if (reloadButton_) reloadButton_->SetText("Reload email");
                }
                if (!outcome) {
                    if (!syncErrorReported_) {
                        syncErrorReported_ = true;
                        AlertError(window_ ? window_.get() : nullptr,
                                   "New mail could not be fetched for "
                                   + (email.empty() ? aid : email) + ".",
                                   outcome.message);
                    }
                    return;
                }
                syncErrorReported_ = false;   // recovered: arm the next report
                CollectContacts(aid, "INBOX");
                Refresh();
            });
        });
        scheduler_.MarkSynced(acc.accountId, now);
    }
}

void UltraMailApp::OpenContacts() {
    if (!contacts_.IsOpen()) {
        AlertError(window_ ? window_.get() : nullptr,
                   "The address book could not be opened.",
                   contactsError_.empty()
                       ? "UltraMail's contacts database is not available."
                       : contactsError_);
        return;
    }
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
    if (path.empty()) {
        AlertError(window_ ? window_.get() : nullptr,
                   "The attachment could not be opened.",
                   "\"" + (attachment.filename.empty() ? std::string("(unnamed)")
                                                       : attachment.filename)
                   + "\" could not be written to the attachment cache in "
                   + cacheDir_ + ". Check that the folder exists and is writable.");
        return;
    }

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

void UltraMailApp::SaveAttachment(const Attachment& attachment) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    const std::string suggested =
        AttachmentCache::SanitizeFilename(attachment.filename, attachment.mediaType);
    const std::string name = attachment.filename.empty() ? std::string("The attachment")
                                                         : "\"" + attachment.filename + "\"";

    // Let the user choose where it goes, through the framework's file dialog.
    UltraCanvas::FileDialogOptions opts;
    opts.SetTitle("Save attachment as…")
        .SetInitialDirectory(DefaultSaveDirectory())
        .SetDefaultFileName(suggested)
        .SetParentWindow(parent);
    // Offer the attachment's own type first, then an unrestricted choice.
    if (const std::string ext = std::filesystem::path(suggested).extension().string();
        ext.size() > 1)
        opts.AddFilter(attachment.mediaType.empty() ? ("*" + ext) : attachment.mediaType,
                       ext.substr(1));
    opts.AddFilter("All files", "*");

    const std::string cacheDir = cacheDir_;
    UltraCanvas::UltraCanvasFileLoader::SaveFileDialog(
        opts,
        [attachment, cacheDir, parent, name](UltraCanvas::DialogResult result,
                                             const std::string& destPath) {
            if (result != UltraCanvas::DialogResult::OK || destPath.empty())
                return;   // the user cancelled — nothing to report
            AttachmentCache cache(cacheDir);
            if (cache.SaveAs(attachment, destPath)) {
                UltraCanvas::UltraCanvasFileLoader::NotifyRecentFile(destPath);
                AlertSuccess(parent, name + " was saved.", destPath);
            } else {
                AlertError(parent, name + " could not be saved.",
                           "It could not be written to " + destPath
                           + ". Check that the folder exists and is writable.");
            }
        });
}

std::string UltraMailApp::DefaultSaveDirectory() {
    // The user's Downloads folder when it exists, else their home, else the
    // working directory — the same order a browser's save dialog uses.
    if (const char* home = std::getenv("HOME"); home && *home) {
        std::error_code ec;
        const std::filesystem::path downloads = std::filesystem::path(home) / "Downloads";
        if (std::filesystem::is_directory(downloads, ec)) return downloads.string();
        return home;
    }
    return ".";
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

void UltraMailApp::EnsureVaultUnlocked(std::function<void()> onUnlocked,
                                       const std::string& errorText) {
    if (vault_.IsUnlocked()) { if (onUnlocked) onUnlocked(); return; }

    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    // No vault file yet means this is the first run: ask the user to choose a
    // master password (with confirmation) rather than to recall one.
    const bool firstRun = !vault_.Exists();

    PassphraseDialog::Show(parent, firstRun, errorText,
        [this, onUnlocked, parent](const std::string& passphrase) {
            switch (vault_.Unlock(passphrase)) {
                case VaultStatus::Ok:
                    if (onUnlocked) onUnlocked();
                    return;
                case VaultStatus::WrongPassphrase:
                    // Ask again, with the reason in the dialog itself. The
                    // vault reports a wrong password and a tampered file
                    // identically, so the text covers both without guessing.
                    EnsureVaultUnlocked(onUnlocked,
                        "That master password did not open the vault. If it is "
                        "correct, the vault file may have been altered.");
                    return;
                case VaultStatus::Unavailable:
                    AlertError(parent,
                        "Your mail passwords cannot be unlocked on this build.",
                        "UltraMail encrypts them with UltraCrypt, which needs "
                        "libsodium. This build was made without it, so the "
                        "credential vault cannot be opened.");
                    return;
                case VaultStatus::IoError:
                    AlertError(parent, "The credential vault could not be opened.",
                               "UltraMail could not read or write "
                               + vault_.VaultPath()
                               + ". Check that the folder exists and is writable.");
                    return;
                case VaultStatus::Locked:
                    AlertError(parent, "The credential vault could not be opened.",
                               "The vault reported that it is not open.");
                    return;
            }
        });
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

    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;

    if (UltraDbResult up = store_.UpsertAccount(a); !up) {
        AlertError(parent, "The account could not be saved.", DetailLine(up));
        return;
    }

    // Auto-discover the server settings from the address (offline provider
    // presets; the wizard's network autoconfig + login verify run in the
    // engine's AutoDiscovery::Discover).
    DiscoveryResult disc = AutoDiscovery::FromPresets(draft.email);

    // Store the password out of the config, in the credential vault — which
    // needs the master password first. If the store fails the account still
    // works, but every later sync would be rejected for no visible reason, so
    // warn now while the user can act on it.
    const std::string accountId = a.accountId;
    const std::string password  = draft.password;
    // Runs after the discovery alert is dismissed, so the master-password
    // prompt is not stacked underneath it.
    auto storePassword = [this, accountId, password, parent]() {
        if (password.empty()) return;
        EnsureVaultUnlocked([this, accountId, password, parent]() {
            if (!vault_.Store(accountId, password)) {
                AlertWarning(parent, "The account was added, but its password "
                                     "could not be saved to the credential vault.",
                             "UltraMail will ask for it again. Check that "
                             + vault_.VaultPath() + " is writable.");
            }
        });
    };

    // Seed the inbox so the tile + rollups have somewhere to hang; real folders
    // arrive from the first IMAP sync (SyncEngine).
    Folder inbox;
    inbox.accountId = a.accountId;
    inbox.name      = "INBOX";
    inbox.role      = FolderRole::Inbox;
    store_.UpsertFolder(inbox);

    Refresh();

    // Report what discovery found. Success is a success alert; a failed
    // auto-detect leaves the account unable to send or receive, so it is a
    // warning that names the consequence rather than an informational note.
    if (disc.found) {
        std::string detail = "Incoming (IMAP): " + AutoDiscovery::ImapServerUrl(disc.imap)
                           + "\nOutgoing (SMTP): " + AutoDiscovery::SmtpServerUrl(disc.smtp);
        if (disc.imap.oauth) detail += "\nSign-in: OAuth2 (browser)";
        AlertSuccess(parent, "Account ready — settings detected ("
                             + disc.displayName + ").", detail, storePassword);
    } else {
        AlertWarning(parent,
            "The account was added, but its server settings could not be "
            "detected from the address.",
            "UltraMail cannot send or receive for " + draft.email
            + " until the incoming and outgoing servers are set up.",
            storePassword);
    }
}

} // namespace UltraMail
