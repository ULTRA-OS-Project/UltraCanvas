// Apps/UltraMail/ui/UltraMailApp.cpp
// Version: 0.9.7 - the vault auto-unlocks with a local device key (Thunderbird-
//                  style, no master-password prompt); old vaults migrate once
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailApp.h"

#include "UltraMailAlerts.h"
#include "UltraMailTheme.h"

#include "UltraMailAttachmentCache.h"
#include "UltraMailDiscovery.h"
#include "UltraMailCredentialVault.h"
#include "UltraMailComposer.h"
#include "UltraMailSender.h"
#include "UltraMailContactCollector.h"
#include "UltraMailSyncService.h"
#include "UltraMailOAuth.h"
#include "UltraMailLoginCheck.h"
#include "UltraMailWaitDialog.h"
#include "UltraMailOAuthCodeDialog.h"

#include <UltraCloud/UltraCloudMemory.h>

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasUtils.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetMime.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <atomic>
#include <fstream>
#include <string>
#include <thread>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr int   kWindowWidth   = 1180;
constexpr int   kWindowHeight  = 760;
constexpr int   kActionIcon    = 12;

std::string IconPath(const std::string& name) {
    return UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/icons/" + name);
}

// A readable folder name for the status line: "Inbox" for INBOX, otherwise the
// leaf of the IMAP path decoded from modified UTF-7 (the same "&...-" encoding
// the folder tree decodes for display) into UTF-8.
std::string FriendlyFolderName(const std::string& folder) {
    if (folder == "INBOX") return "Inbox";
    std::size_t slash = folder.find_last_of('/');
    const std::string leaf = slash == std::string::npos ? folder : folder.substr(slash + 1);
    return UltraNet_ImapUtf7Decode(leaf);
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
    // App-wide view preferences (e.g. the reading pane). A missing file keeps
    // the defaults; it is written the first time the user changes a setting.
    prefsPath_ = dataDir + "/preferences.ini";
    prefs_.Load(prefsPath_);
    // The sender-icon cache (the badge left of every subject line) lives under
    // the cache directory; it is safe to point at it before the folder exists.
    ConfigureSenderIcons();
    // The credential vault stays locked until the user supplies the master
    // password; nothing reads or writes a secret before then.
    vault_ = CredentialVault(dataDir + "/vault");
    // The OAuth client UltraMail signs in to Google with (see README, "Google
    // sign-in"): ULTRAMAIL_GOOGLE_CLIENT_ID in the environment, else oauth.ini.
    OAuthApps::LoadFile(dataDir + "/oauth.ini");

    // The address book + outbox are global (account-independent) stores. A
    // failure here is not fatal — the rest of the client still works — but it
    // must not be silent: remember it so the feature that needs the store can
    // say what went wrong instead of doing nothing.
    if (UltraDbResult c = contacts_.Open("ultramail-contacts", dataDir + "/contacts.db"); !c)
        contactsError_ = DetailLine(c);
    if (UltraDbResult o = outbox_.Open("ultramail-outbox", dataDir + "/outbox.db"); !o)
        outboxError_ = DetailLine(o);

    // Cloud storage accounts (UltraCloud) for "Attach cloud link".
    UltraCloud::RegisterBuiltInProviders();
    cloudAccounts_.Open("ultramail-cloud", dataDir + "/cloud.db");
    cloudSecrets_ = std::make_unique<UltraCloud::VaultSecretStore>();   // in vault_
    cloud_ = std::make_unique<UltraCloud::CloudService>(cloudAccounts_, *cloudSecrets_);

    // Bring up the UltraNet plug-in registry so the SMTP / IMAP DSOs load. The
    // registry's default directory is relative to the working directory, which
    // only matches the build tree when the app is started from there; resolve
    // it against the executable instead (ULTRAMAIL_PLUGIN_DIR overrides).
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();
    pluginDir_ = ResolvePluginDirectory();
    UltraNet_SetPluginDirectory(pluginDir_);
    UltraNet_RefreshPlugins();

    // Unlock the credential vault with the local device key so the user is not
    // prompted (Thunderbird-style; see CredentialVault). A brand-new vault is
    // created here; an old master-password vault stays locked until the first
    // action prompts once (then the key is persisted).
    if (vault_.TryAutoUnlock()) MigrateCloudSecrets();

    store_.ListAccounts(accounts_);
    store_.GetAccountStatus(status_);
    for (const auto& a : accounts_) feed_.SetAccount(a.accountId, a.email, a.displayName);
    return true;
}

std::shared_ptr<UltraCanvasWindow> UltraMailApp::CreateMainWindow() {
    WindowConfig config;
    config.title  = "UltraMail";
    config.width  = kWindowWidth;
    config.height = kWindowHeight;
    config.backgroundColor = Theme::kPageBackground;
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

    // Migration: an existing vault made with a master password (before device
    // keys) stays locked after Initialize's silent attempt. Prompt once now so
    // mail syncs; EnsureVaultUnlocked persists the entered password as the
    // device key, so this is the only time it is asked.
    if (!accounts_.empty() && !vault_.IsUnlocked()) {
        EnsureVaultUnlocked([this]() { RunSyncs(/*force=*/false); Refresh(); });
    }

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
    accountView_->SetPadding(Theme::kPagePadding);
    accountView_->layout.SetFlexColumn()
                        .SetFlexGap(Theme::kGap)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // ----- Toolbar: one compact row of actions, the primary one first -----
    auto toolbar = CreateContainer("umToolbar", 0, 0, 0, Theme::kToolbarHeight);
    toolbar->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto makeAction = [&](const std::string& id, const std::string& text, float width,
                          const std::string& icon, bool primary,
                          std::function<void()> onClick) {
        auto button = CreateButton(id, 0, 0, width, Theme::kControlHeight, text);
        if (primary) Theme::StylePrimary(button); else Theme::StyleSecondary(button);
        if (!icon.empty()) {
            button->SetIcon(IconPath(icon));
            button->SetIconPosition(ButtonIconPosition::Left);
            button->SetIconSize(kActionIcon, kActionIcon);
            button->SetIconSpacing(6);
            button->SetUseIconAsMask(true);
        }
        button->onClick = std::move(onClick);
        toolbar->AddChild(button);
        return button;
    };
    makeAction("umNewEmail", "New email", 96, "envelope.svg", true, [this]() {
        std::string name, addr;
        for (const auto& a : accounts_)
            if (a.accountId == selectedAccount_) { name = a.displayName; addr = a.email; }
        if (addr.empty() && !accounts_.empty()) {
            name = accounts_.front().displayName; addr = accounts_.front().email;
        }
        OpenComposer(Composer::NewMessage(name, addr));
    });
    reloadButton_ = makeAction("umReload", "Reload", 80, "reload.svg", false,
                               [this]() { HandleReload(); });
    makeAction("umContacts", "Contacts", 76, "", false, [this]() { OpenContacts(); });
    toolbar->AddStretchSpacer(1);
    makeAction("umSettings", "Account Settings", 0, "", false, [this]() {
        if (!selectedAccount_.empty()) HandleAccountSettings(selectedAccount_);
    });
    makeAction("umAddAccount", "Add account", 0, "", false,
               [this]() { HandleAddAccount(); });
    // "Delete account" moved to the account settings dialog's bottom row
    // (ServerSettingsDialog, red button) — see HandleAccountSettings.
    accountView_->AddChild(toolbar);
    // Freeze the chrome rows: grow 0, shrink 0. Without shrink 0 (the flex
    // default is 1) a tall inbox list pushes the column past the window and the
    // engine crushes the toolbar and account bar toward zero height.
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // ----- Account bar: one summary card, or a tile per account -----
    auto bar = accountBar_.Build();
    accountBar_.onSelectAccount = [this](const std::string& accountId) {
        selectedAccount_ = accountId;
        Refresh();
    };
    accountView_->AddChild(bar);
    bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                   .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // These rows are single-row chrome; they must never show a scrollbar (which
    // otherwise paints over them when space is tight). The mail area below keeps
    // its scrollbar — the inbox list must scroll.
    auto noScroll = [](const std::shared_ptr<UltraCanvasContainer>& c) {
        auto s = c->GetContainerStyle();
        s.autoShowScrollbars = false;
        c->SetContainerStyle(s);
    };
    noScroll(toolbar);
    noScroll(bar);

    // ----- Mail area: inbox table | message details -----
    mailView_.SetStore(&store_);
    mailView_.SetMailDir(mailDir_);
    mailView_.onOpenAttachment = [this](const Attachment& a) { OpenAttachment(a); };
    mailView_.onSaveAttachment = [this](const Attachment& a) { SaveAttachment(a); };
    mailView_.onReply = [this](const SourceMessage& src, const std::string& selfName,
                               const std::string& selfAddr) {
        OpenComposer(Composer::Reply(src, selfName, selfAddr, /*replyAll=*/false));
    };
    mailView_.onForward = [this](const SourceMessage& src, const std::string& selfName,
                                 const std::string& selfAddr) {
        OpenComposer(Composer::Forward(src, selfName, selfAddr));
    };
    mailView_.onDelete     = [this](const MessageEnvelope& e) { HandleDeleteMessage(e); };
    mailView_.onJunk       = [this](const MessageEnvelope& e) { HandleJunkMessage(e); };
    mailView_.onMarkUnread = [this](const MessageEnvelope& e) { HandleMarkUnread(e); };
    mailView_.onMarkRead   = [this](const MessageEnvelope& e) { HandleMarkRead(e); };
    mailView_.onViewSource = [this](const std::string& subject, const std::string& raw) {
        OpenSourceViewer(subject, raw);
    };
    // The folder tree switched to a folder under a different account: adopt that
    // account (and highlight its tile) without re-showing its inbox, so the
    // tree's chosen folder stays open.
    mailView_.onSelectAccount = [this](const std::string& accountId) {
        selectedAccount_ = accountId;
        store_.ListAccounts(accounts_);
        store_.GetAccountStatus(status_);
        accountBar_.Rebuild(accounts_, status_, selectedAccount_);
    };
    // A folder was opened: its cached mail is already on screen (RebuildList ran
    // before this fires), so refresh it from the server — new mail plus a flag
    // reconcile — but throttled, so switching folders back and forth does not
    // re-hit the server on every click.
    mailView_.onOpenFolder = [this](const std::string& accountId, const std::string& folder) {
        // Can't fetch right now (no plug-in / vault locked): leave it unstamped so
        // opening it again retries once the prerequisites are met.
        if (!ImapPlugin() || !vault_.IsUnlocked()) return;
        const std::string key = accountId + "\n" + folder;
        if (folderSyncInFlight_.count(key)) return;   // already fetching this folder
        const int64_t now = NowMonotonicSec();
        auto last = folderSyncedAt_.find(key);
        if (last != folderSyncedAt_.end() && now - last->second < kFolderResyncSec)
            return;                                   // opened again too soon; throttle
        folderSyncInFlight_.insert(key);
        folderSyncedAt_[key] = now;
        SyncFolder(accountId, folder);
    };
    auto mail = mailView_.Build();
    accountView_->AddChild(mail);
    mail->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // Apply the remembered reading-pane choice (default on; a rebuild only when off).
    mailView_.SetReadingPane(prefs_.showReadingPane);

    // ----- Status line: what the app is currently doing -----
    statusLabel_ = Theme::MakeLine("umStatus", "Ready", Theme::kToolbarHeight * 0.75f,
                                   Theme::kSizeSecondary, Theme::kTextSecondary);
    accountView_->AddChild(statusLabel_);
    statusLabel_->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    return accountView_;
}

void UltraMailApp::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text.empty() ? "Ready" : text);
}

void UltraMailApp::ResizeViews(float width, float height) {
    startPage_.Resize(width, height);
    if (accountView_) accountView_->SetElementSize(Size2Df(width, height));
}

void UltraMailApp::HandleReload() {
    if (accounts_.empty()) return;
    // Say what is missing before asking for a master password that would
    // then unlock nothing useful.
    if (!ImapPlugin()) { ReportMissingImapPlugin(); return; }
    // Reload acts on the account whose tile is selected (the one whose inbox is
    // shown), not every account: it must not fetch — or pop a settings dialog
    // for — an account the user is not looking at. The periodic background sync
    // still covers the rest. Fall back to the first account if nothing is
    // selected (should not happen once accounts exist).
    std::string target = selectedAccount_;
    if (target.empty()) target = accounts_.front().accountId;
    // Reload is a foreground action, so unlike the timer it may ask for the
    // master password; the sync runs once the vault is open (not on Cancel).
    EnsureVaultUnlocked([this, target]() {
        SyncAccount(target);
        // Reload also refreshes the folder in view when it is not the inbox (the
        // account sync covers the inbox); other folders are lazily fetched.
        const std::string folder = mailView_.CurrentFolder();
        if (!folder.empty() && folder != "INBOX") SyncFolder(target, folder);
        Refresh();
    });
}

std::string UltraMailApp::ResolvePluginDirectory() {
    namespace fs = std::filesystem;
    if (const char* pd = std::getenv("ULTRAMAIL_PLUGIN_DIR"); pd && *pd) return pd;

    // A directory counts when it holds at least one plug-in DSO, so an empty
    // Plugins/UltraNet next to the executable does not shadow the real one.
    auto holdsPlugin = [](const fs::path& dir) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec) || ec) return false;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) return false;
            const std::string ext = entry.path().extension().string();
            if (ext == ".so" || ext == ".dll" || ext == ".dylib") return true;
        }
        return false;
    };

    const fs::path relative = fs::path("Plugins") / "UltraNet";
    std::vector<fs::path> candidates;
    const std::string exeDir = UltraCanvas::GetExecutableDir();
    if (!exeDir.empty()) {
        // The build tree puts the executable at <build>/ and the DSOs at
        // <build>/Plugins/UltraNet; an installed or copied app may sit one
        // or two levels deeper (bin/, Apps/UltraMail/).
        fs::path base = exeDir;
        for (int up = 0; up < 3; ++up) {
            candidates.push_back(base / relative);
            base = base.parent_path();
        }
    }
    candidates.push_back(relative);   // the registry's own default (cwd)

    for (const auto& c : candidates)
        if (holdsPlugin(c)) return c.lexically_normal().string();
    // Nothing found: keep the first executable-relative path so the diagnostic
    // names a concrete place to put the DSOs.
    return candidates.front().lexically_normal().string();
}

IMailboxProtocolPlugin* UltraMailApp::ImapPlugin() const {
    auto plugin = UltraNet_GetPlugin("imaps");
    // The registry keeps the plug-in alive; the raw interface pointer stays
    // valid until UltraNet shuts down.
    return plugin ? dynamic_cast<IMailboxProtocolPlugin*>(plugin.get()) : nullptr;
}

void UltraMailApp::ReportMissingImapPlugin() {
    AlertError(window_ ? window_.get() : nullptr,
               "Mail cannot be fetched: the IMAP plug-in was not found.",
               "UltraMail looked for ultranet_imap in " + pluginDir_
               + ". Build the UltraNet IMAP plug-in (ULTRACANVAS_PLUGIN_IMAP) "
                 "and keep it there, or point ULTRAMAIL_PLUGIN_DIR at the folder "
                 "that holds it, then restart UltraMail.");
}

void UltraMailApp::OpenComposer(const Draft& draft) {
    WindowConfig cfg;
    cfg.title  = draft.subject.empty() ? "New message" : draft.subject;
    cfg.width  = 640;
    cfg.height = 520;
    cfg.backgroundColor = Theme::kCardBackground;
    auto win = CreateWindow(cfg);

    composeView_.SetDraft(draft);
    composeView_.SetParentWindow(win.get());
    composeView_.SetCloud(cloud_.get());
    composeView_.onSend   = [this](const Draft& d) { HandleSendDraft(d); };
    UltraCanvasWindow* raw = win.get();
    composeView_.onCancel = [raw]() { raw->Close(); };
    auto view = composeView_.Build();
    win->AddChild(view);
    composeView_.Resize(static_cast<float>(cfg.width), static_cast<float>(cfg.height));
    win->onWindowResize = [this](int cw, int ch) {
        composeView_.Resize(static_cast<float>(cw), static_cast<float>(ch));
    };
    win->Show();
    viewerWindows_.push_back(win);
}

std::string UltraMailApp::FolderWithRole(const std::string& accountId, FolderRole role) const {
    std::vector<Folder> folders;
    store_.ListFolders(accountId, folders);
    for (const auto& f : folders) if (f.role == role) return f.name;
    return {};
}

void UltraMailApp::RunMailboxAction(
    const std::string& accountId,
    std::function<SyncOutcome(SyncEngine&, const std::string&, const UltraNetMailOptions&)> op,
    const std::string& actionName) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    IMailboxProtocolPlugin* imap = ImapPlugin();
    if (!imap) { ReportMissingImapPlugin(); return; }

    // The IMAP action needs the account password / OAuth token, so unlock first
    // (silently with the device key, or a single prompt for an old vault).
    EnsureVaultUnlocked([this, accountId, op, actionName, parent, imap]() {
        const Account* account = nullptr;
        for (const auto& a : accounts_) if (a.accountId == accountId) account = &a;
        if (!account) return;

        const DiscoveryResult settings = SettingsFor(*account);
        const std::string serverUrl =
            settings.found ? AutoDiscovery::ImapServerUrl(settings.imap) : "";
        if (serverUrl.empty() || !settings.found) {
            AlertWarning(parent, actionName + " could not be completed.",
                         "No incoming (IMAP) server is known for this account.");
            return;
        }
        if (vault_.MethodFor(accountId) == SignInMethod::None) {
            AlertWarning(parent, actionName + " could not be completed.",
                         "No password or sign-in is stored for this account in the "
                         "credential vault.");
            return;
        }

        UltraNetMailOptions opts;
        opts.useTls      = settings.imap.security != MailSecurity::Plain;
        opts.implicitTls = settings.imap.security == MailSecurity::SslTls;
        const std::string email    = account->email;
        const std::string username = settings.imap.username.empty() ? email
                                                                    : settings.imap.username;
        const std::string provider = OAuthProviderFor(settings);
        opts.credentials.username  = username;

        // The server op + local-store update run off the UI thread (a credential
        // refresh can make an HTTPS request); the result is marshalled back.
        std::thread([this, accountId, serverUrl, opts, op, actionName,
                     username, provider, imap]() mutable {
            UltraNetResult cred = ResolveCredentials(accountId, username, provider,
                                                     opts.credentials);
            SyncOutcome outcome;
            // Default Success: only a credential failure carries an auth code the
            // UI thread can turn into a re-sign-in offer; an IMAP-op failure keeps
            // Success here and falls through to the plain error alert.
            UltraNetResultCode credCode = UltraNetResultCode::Success;
            if (!cred) {
                outcome = SyncOutcome::Fail(cred.message);
                credCode = cred.code;
            } else {
                SyncEngine engine(store_, *imap, mailDir_);
                outcome = op(engine, serverUrl, opts);
            }
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) return;
            app->PostToUIThread([this, accountId, provider, outcome, actionName, credCode, op]() {
                if (!outcome) {
                    // A dead OAuth sign-in offers Retry → re-sign-in → re-run the
                    // action. Anything else is the usual dead-end error.
                    if (!MaybeOfferReauth(accountId, credCode, provider,
                            [this, accountId, op, actionName]() {
                                RunMailboxAction(accountId, op, actionName);
                            }))
                        AlertError(window_ ? window_.get() : nullptr,
                                   actionName + " could not be completed.", outcome.message);
                    return;
                }
                Refresh();
            });
        }).detach();
    });
}

void UltraMailApp::RunMailboxActionQuiet(
    const std::string& accountId,
    std::function<SyncOutcome(SyncEngine&, const std::string&, const UltraNetMailOptions&)> op) {
    IMailboxProtocolPlugin* imap = ImapPlugin();
    if (!imap) return;                       // quiet: no plug-in, nothing to push

    const Account* account = nullptr;
    for (const auto& a : accounts_) if (a.accountId == accountId) account = &a;
    if (!account) return;

    const DiscoveryResult settings = SettingsFor(*account);
    const std::string serverUrl =
        settings.found ? AutoDiscovery::ImapServerUrl(settings.imap) : "";
    if (serverUrl.empty() || !settings.found) return;
    if (vault_.MethodFor(accountId) == SignInMethod::None) return;

    UltraNetMailOptions opts;
    opts.useTls      = settings.imap.security != MailSecurity::Plain;
    opts.implicitTls = settings.imap.security == MailSecurity::SslTls;
    const std::string email    = account->email;
    const std::string username = settings.imap.username.empty() ? email
                                                                : settings.imap.username;
    const std::string provider = OAuthProviderFor(settings);
    opts.credentials.username  = username;

    // Best-effort, off the UI thread: no result is marshalled back — the local
    // store and the list row already reflect the change, and the next folder
    // reconcile fixes any drift if this push fails.
    std::thread([this, accountId, serverUrl, opts, op, username, provider, imap]() mutable {
        UltraNetResult cred = ResolveCredentials(accountId, username, provider,
                                                 opts.credentials);
        if (!cred) return;
        SyncEngine engine(store_, *imap, mailDir_);
        op(engine, serverUrl, opts);
    }).detach();
}

void UltraMailApp::HandleMarkUnread(const MessageEnvelope& env) {
    RunMailboxAction(env.accountId,
        [env](SyncEngine& engine, const std::string& url, const UltraNetMailOptions& opts) {
            return engine.SetFlag(env.accountId, env.folder, env.uid, Flag_Seen, false, url, opts);
        },
        "Mark as unread");
}

void UltraMailApp::HandleMarkRead(const MessageEnvelope& env) {
    if (env.flags & Flag_Seen) return;                  // already read
    // Optimistic: reflect it locally now so the row updates instantly and a
    // rebuild keeps it read even if the server push below is slow or offline.
    store_.SetFlags(env.accountId, env.folder, env.uid, Flag_Seen, true);
    mailView_.MarkRead(env.accountId, env.folder, env.uid);
    // Only reach for the server when the vault is already open — a passive click
    // must never pop the master-password dialog.
    if (!ImapPlugin() || !vault_.IsUnlocked()) return;
    RunMailboxActionQuiet(env.accountId,
        [env](SyncEngine& engine, const std::string& url, const UltraNetMailOptions& opts) {
            return engine.SetFlag(env.accountId, env.folder, env.uid, Flag_Seen, true, url, opts);
        });
}

void UltraMailApp::HandleDeleteMessage(const MessageEnvelope& env) {
    const std::string trash = FolderWithRole(env.accountId, FolderRole::Trash);
    RunMailboxAction(env.accountId,
        [this, env, trash](SyncEngine& engine, const std::string& url,
                           const UltraNetMailOptions& opts) -> SyncOutcome {
            if (!trash.empty() && trash != env.folder)
                return engine.MoveMessage(env.accountId, env.folder, env.uid, trash, url, opts);
            // No Trash mailbox (or already in it): flag \Deleted on the server and
            // drop the local row so it leaves the list.
            SyncOutcome o = engine.SetFlag(env.accountId, env.folder, env.uid,
                                           Flag_Deleted, true, url, opts);
            if (o) store_.RemoveMessage(env.accountId, env.folder, env.uid);
            return o;
        },
        "Delete");
}

void UltraMailApp::HandleJunkMessage(const MessageEnvelope& env) {
    const std::string junk = FolderWithRole(env.accountId, FolderRole::Junk);
    if (junk.empty()) {
        AlertWarning(window_ ? window_.get() : nullptr,
                     "This account has no Junk (Spam) folder.",
                     "UltraMail could not find a mailbox marked as Junk on this "
                     "account, so the message was not moved.");
        return;
    }
    RunMailboxAction(env.accountId,
        [env, junk](SyncEngine& engine, const std::string& url, const UltraNetMailOptions& opts) {
            return engine.MoveMessage(env.accountId, env.folder, env.uid, junk, url, opts);
        },
        "Mark as junk");
}

void UltraMailApp::OpenSourceViewer(const std::string& subject, const std::string& raw) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    if (raw.empty()) {
        AlertWarning(parent, "There is no source to show.",
                     "This message's body has not been downloaded yet.");
        return;
    }
    WindowConfig cfg;
    cfg.title  = subject.empty() ? "Message source" : ("Source: " + subject);
    cfg.width  = 720;
    cfg.height = 640;
    cfg.backgroundColor = Theme::kCardBackground;
    auto win = CreateWindow(cfg);

    auto root = CreateContainer("mailSourceRoot", 0, 0,
                                static_cast<float>(cfg.width), static_cast<float>(cfg.height));
    root->SetPadding(Theme::kPagePadding);
    root->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto text = std::make_shared<UltraCanvasTextArea>("mailSource", 0, 0, 0, 0);
    text->SetReadOnly(true);
    text->SetEditingMode(TextAreaEditingMode::PlainText);
    text->SetWordWrap(false);
    Theme::StyleTextArea(text, /*bordered=*/true);
    text->SetText(raw);
    root->AddChild(text);
    text->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    win->AddChild(root);
    UltraCanvasContainer* rootRaw = root.get();
    win->onWindowResize = [rootRaw](int cw, int ch) {
        rootRaw->SetElementSize(Size2Df(static_cast<float>(cw), static_cast<float>(ch)));
    };
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

    DiscoveryResult disc = SettingsForEmail(draft.fromAddr);
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
    DiscoveryResult disc = SettingsForEmail(draft.fromAddr);
    auto plugin = UltraNet_GetPlugin(disc.smtp.security == MailSecurity::SslTls ? "smtps" : "smtp");
    auto* smtp = plugin ? dynamic_cast<IMailProtocolPlugin*>(plugin.get()) : nullptr;
    if (!smtp) {
        AlertError(parent, "The message could not be sent.",
                   "The SMTP plug-in is no longer loaded.");
        return;
    }
    Outbox ob(outbox_);
    auto stats = ob.Flush(*smtp, [this](const std::string& acc, UltraNetMailOptions& o) {
        return PrepareSmtp(acc, o);
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

    DiscoveryResult disc = SettingsForEmail(fromAddr);
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
    auto stats = ob.Flush(*smtp, [this](const std::string& acc, UltraNetMailOptions& o) {
        return PrepareSmtp(acc, o);
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
    // CollectSender, not Collect: a sender in the known-sender registry is
    // filed under Services as a business contact (with the service's name as
    // the organization), everything else lands in Other as before.
    for (const auto& m : msgs)
        ContactCollector::CollectSender(contacts_, m.fromName, m.fromAddr);
}

void UltraMailApp::StartBackgroundSync() {
    // Register every account's sync cadence with the scheduler. SetAccount
    // keeps the last-sync time of an account that is already registered, so
    // this is safe to repeat after an account was added.
    for (const auto& a : accounts_) {
        DiscoveryResult d = SettingsFor(a);
        std::string url = d.found ? AutoDiscovery::ImapServerUrl(d.imap) : "";
        scheduler_.SetAccount(a.accountId, url, /*intervalSec=*/300);
    }
    // Only run the live loop when the IMAP plug-in is present (otherwise a timer
    // would just fire against nothing), and only one of it.
    if (syncTimerStarted_ || !ImapPlugin()) return;
    if (auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent()) {
        app->StartTimer(300000, /*periodic=*/true,
                        [this](UltraCanvas::TimerId) { RunSyncs(/*force=*/false); });
        syncTimerStarted_ = true;
    }
}

void UltraMailApp::RunSyncs(bool force) {
    // Which accounts: the due ones, or all of them for a forced reload.
    std::vector<ScheduledAccount> targets;
    if (force) {
        for (const auto& a : accounts_) {
            DiscoveryResult d = SettingsFor(a);
            ScheduledAccount sa;
            sa.accountId = a.accountId;
            sa.serverUrl = d.found ? AutoDiscovery::ImapServerUrl(d.imap) : "";
            targets.push_back(sa);
        }
    } else {
        targets = scheduler_.DueAccounts(static_cast<int64_t>(std::time(nullptr)));
    }
    SyncAccounts(targets, /*userInitiated=*/force);
}

void UltraMailApp::SyncAccount(const std::string& accountId) {
    for (const auto& a : accounts_) {
        if (a.accountId != accountId) continue;
        DiscoveryResult d = SettingsFor(a);
        ScheduledAccount sa;
        sa.accountId = a.accountId;
        sa.serverUrl = d.found ? AutoDiscovery::ImapServerUrl(d.imap) : "";
        SyncAccounts({sa}, /*userInitiated=*/true);
        return;
    }
}

void UltraMailApp::SyncFolder(const std::string& accountId, const std::string& folder) {
    IMailboxProtocolPlugin* imap = ImapPlugin();
    if (!imap || !vault_.IsUnlocked()) return;

    const Account* account = nullptr;
    for (const auto& a : accounts_) if (a.accountId == accountId) account = &a;
    if (!account) return;

    const DiscoveryResult settings = SettingsFor(*account);
    const std::string serverUrl =
        settings.found ? AutoDiscovery::ImapServerUrl(settings.imap) : "";
    if (serverUrl.empty() || !settings.found) return;
    if (vault_.MethodFor(accountId) == SignInMethod::None) return;

    UltraNetMailOptions opts;
    opts.useTls      = settings.imap.security != MailSecurity::Plain;
    opts.implicitTls = settings.imap.security == MailSecurity::SslTls;
    const std::string email    = account->email;
    const std::string who      = email.empty() ? accountId : email;
    const std::string username = settings.imap.username.empty() ? email : settings.imap.username;
    const std::string provider = OAuthProviderFor(settings);
    opts.credentials.username  = username;

    auto svc = std::make_shared<SyncService>(store_, *imap, mailDir_);
    if (++syncsInFlight_ == 1 && reloadButton_) reloadButton_->SetText("Reloading…");
    SetStatus("Opening " + FriendlyFolderName(folder) + "…");
    auto progressBuf = std::make_shared<std::vector<MessageEnvelope>>();
    svc->SyncFolderInBackground(accountId, folder, serverUrl, opts,
        [this, accountId, username, provider](UltraNetMailOptions& o) {
            return ResolveCredentials(accountId, username, provider, o.credentials);
        },
        [this, svc, accountId, folder, who](SyncOutcome outcome) {
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) return;
            app->PostToUIThread([this, accountId, folder, who, outcome]() {
                // Clear the in-flight guard first — even on failure — so the next
                // open can retry once the throttle window passes. (Harmless no-op
                // for callers that never set it, e.g. HandleReload.)
                folderSyncInFlight_.erase(accountId + "\n" + folder);
                const bool last = (--syncsInFlight_ <= 0);
                if (last) {
                    syncsInFlight_ = 0;
                    statusReceived_ = 0;
                    if (reloadButton_) reloadButton_->SetText("Reload");
                }
                if (!outcome) {
                    SetStatus("Could not reach the server");
                    if (!syncErrorReported_) {
                        syncErrorReported_ = true;
                        AlertError(window_ ? window_.get() : nullptr,
                                   "That folder could not be fetched for " + who + ".",
                                   outcome.message);
                    }
                    return;
                }
                syncErrorReported_ = false;
                if (last) SetStatus("Up to date");
                Refresh();   // re-query the store; the open folder now shows its mail
            });
        },
        [this, accountId, progressBuf](const MessageEnvelope& m) {
            feed_.Publish(m);   // worker thread; the publisher filters and rate-limits
            progressBuf->push_back(m);
            if (progressBuf->size() < 20) return;
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) { progressBuf->clear(); return; }
            auto batch = std::make_shared<std::vector<MessageEnvelope>>();
            batch->swap(*progressBuf);
            app->PostToUIThread([this, accountId, batch]() {
                statusReceived_ += static_cast<int>(batch->size());
                SetStatus("Receiving messages… (" + std::to_string(statusReceived_) + ")");
                if (accountId == selectedAccount_) mailView_.AppendMessages(accountId, *batch);
            });
        });
}

void UltraMailApp::SyncAccounts(const std::vector<ScheduledAccount>& targets,
                                bool userInitiated) {
    if (targets.empty()) return;
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;

    // Without the plug-in nothing can be fetched. The user asked (Reload, a
    // new account), so say so every time; the timer never runs without it.
    IMailboxProtocolPlugin* imap = ImapPlugin();
    if (!imap) {
        if (userInitiated) ReportMissingImapPlugin();
        return;
    }

    // A background timer must never raise a modal password prompt over whatever
    // the user is doing. If the vault is still locked, skip this round and say
    // so once — the next send or account change prompts in the foreground.
    if (!vault_.IsUnlocked()) {
        if (userInitiated || !syncErrorReported_) {
            syncErrorReported_ = true;
            AlertWarning(parent, "New mail is not being fetched yet.",
                         "Your mail account passwords are locked. Enter your "
                         "master password — sending a message or adding an "
                         "account will ask for it — and syncing resumes.");
        }
        return;
    }

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    for (const auto& acc : targets) {
        const Account* account = nullptr;
        for (const auto& a : accounts_) if (a.accountId == acc.accountId) account = &a;
        const std::string email = account ? account->email : "";
        const std::string who = email.empty() ? acc.accountId : email;
        const DiscoveryResult settings = account ? SettingsFor(*account) : DiscoveryResult{};

        // No server is known for the account (neither stored nor in the
        // provider table). On a user action open the settings page so it can
        // be fixed right here; the timer just skips it.
        if (acc.serverUrl.empty() || !settings.found) {
            if (userInitiated && account) EditServerSettings(acc.accountId);
            continue;
        }

        // The password — or the Google sign-in — lives in the vault. Without
        // either the login would be rejected anyway, so report the real reason
        // instead of a bad-password error from the server.
        if (vault_.MethodFor(acc.accountId) == SignInMethod::None) {
            if (userInitiated)
                AlertWarning(parent, "New mail cannot be fetched for " + who + ".",
                             "No password or sign-in is stored for this account "
                             "in the credential vault. Add the account again; "
                             "the existing entry is updated.");
            continue;
        }

        UltraNetMailOptions opts;
        opts.useTls      = settings.imap.security != MailSecurity::Plain;
        opts.implicitTls = settings.imap.security == MailSecurity::SslTls;
        const std::string username =
            settings.imap.username.empty() ? email : settings.imap.username;
        const std::string provider = OAuthProviderFor(settings);
        opts.credentials.username = username;

        auto svc = std::make_shared<SyncService>(store_, *imap, mailDir_);
        const std::string aid = acc.accountId;
        if (++syncsInFlight_ == 1 && reloadButton_) reloadButton_->SetText("Reloading…");
        SetStatus("Checking " + who + "…");
        // onDone keeps `svc` alive until the worker thread finishes; it marshals
        // the follow-up work back to the UI thread.
        // The outcome carries the reason a sync failed (bad password, untrusted
        // certificate, unreachable host). Marshal it to the UI thread and say
        // so — once per run of failures, so a broken server does not raise an
        // alert on every timer tick; always when the user asked for the sync.
        // The in-flight count unwinds either way, so the Reload button is
        // restored even when the sync failed.
        // The credentials are resolved on the worker: an expired OAuth2 token
        // is refreshed through the provider first, which must not block the UI.
        // onProgress streams each new header off the worker thread; we batch a
        // few before marshalling to the UI so a large mailbox's list fills in as
        // it downloads instead of appearing frozen until the whole sync ends.
        auto progressBuf = std::make_shared<std::vector<MessageEnvelope>>();
        // Stash the credential-resolve code so the UI thread can tell an expired
        // OAuth sign-in (offer re-sign-in) from an ordinary sync failure.
        auto credCode = std::make_shared<UltraNetResultCode>(UltraNetResultCode::Success);
        svc->SyncInBackground(aid, acc.serverUrl, opts,
                              [this, aid, username, provider, credCode](UltraNetMailOptions& o) {
                                  UltraNetResult r = ResolveCredentials(aid, username, provider,
                                                                        o.credentials);
                                  if (!r) *credCode = r.code;
                                  return r;
                              },
                              [this, svc, aid, who, provider, userInitiated, credCode](SyncOutcome outcome) {
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) return;
            app->PostToUIThread([this, aid, who, provider, userInitiated, outcome, credCode]() {
                const bool last = (--syncsInFlight_ <= 0);
                if (last) {
                    syncsInFlight_ = 0;
                    statusReceived_ = 0;
                    if (reloadButton_) reloadButton_->SetText("Reload");
                }
                if (!outcome) {
                    SetStatus("Could not reach the server");
                    // A dead OAuth sign-in offers Retry → re-sign-in (which
                    // re-syncs the account), but only when the user asked — a
                    // background timer must never pop a dialog.
                    if (userInitiated &&
                        MaybeOfferReauth(aid, *credCode, provider, nullptr)) {
                        syncErrorReported_ = true;
                        return;
                    }
                    if (userInitiated || !syncErrorReported_) {
                        syncErrorReported_ = true;
                        AlertError(window_ ? window_.get() : nullptr,
                                   "New mail could not be fetched for " + who + ".",
                                   outcome.message);
                    }
                    return;
                }
                syncErrorReported_ = false;   // recovered: arm the next report
                if (last) SetStatus("Up to date");
                CollectContacts(aid, "INBOX");
                Refresh();   // authoritative, correctly date-sorted final list
            });
        },
                              // onProgress — runs on the worker thread. Only this
                              // one worker touches progressBuf, so no lock is
                              // needed; each flush hands a fresh batch to the UI.
                              [this, aid, progressBuf](const MessageEnvelope& m) {
            // On the worker thread, so this is where a known service's icon is
            // fetched: once per brand, never for an address that is not in the
            // registry, and not at all when the user turned downloads off.
            senderIcons_.EnsureIconForAddress(m.fromAddr);
            feed_.Publish(m);   // the desktop feed learns of new mail as it arrives
            progressBuf->push_back(m);
            std::fprintf(stderr, "[UMSTREAM] onProgress uid=%lld buf=%zu aid=%s\n",
                         (long long)m.uid, progressBuf->size(), aid.c_str());
            if (progressBuf->size() < 20) return;   // bound UI churn on big syncs
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) { progressBuf->clear(); return; }
            auto batch = std::make_shared<std::vector<MessageEnvelope>>();
            batch->swap(*progressBuf);
            std::fprintf(stderr, "[UMSTREAM] flush batch=%zu aid=%s selected=%s\n",
                         batch->size(), aid.c_str(), selectedAccount_.c_str());
            app->PostToUIThread([this, aid, batch]() {
                statusReceived_ += static_cast<int>(batch->size());
                SetStatus("Receiving messages… (" + std::to_string(statusReceived_) + ")");
                if (aid == selectedAccount_) mailView_.AppendMessages(aid, *batch);
            });
            // The trailing partial batch (< 20) is left for the final Refresh(),
            // which re-queries the store and shows every message anyway.
        });
        scheduler_.MarkSynced(acc.accountId, now);
    }
}

void UltraMailApp::ConfigureSenderIcons() {
    senderIcons_.SetRoot(cacheDir_ + "/sender-icons");
    senderIcons_.SetNetworkEnabled(prefs_.fetchSenderIcons);
    // One HTTPS GET, TLS verified (UltraNet's default), with a short timeout:
    // an icon is never worth holding a sync open for. Only the URLs in the
    // known-sender registry are ever passed here.
    senderIcons_.SetFetcher([](const std::string& url, std::vector<uint8_t>& out) {
        UltraNetHttpOptions options = UltraNetHttpOptions::Default();
        options.timeoutMs        = 10000;
        options.connectTimeoutMs = 5000;
        options.followRedirects  = true;
        options.maxReceiveSize   = 512 * 1024;   // an icon, not a page
        UltraNetResponse response;
        if (!UltraNet_HttpGet(url, response, options)) return false;
        if (!response.IsSuccess() || response.body.empty()) return false;
        out = response.body;
        return true;
    });
    mailView_.SetIconCache(&senderIcons_);
}

void UltraMailApp::RefreshContactIndex() {
    ContactIndex index;
    if (contacts_.IsOpen()) BuildContactIndex(contacts_, index);
    mailView_.SetContacts(std::move(index));
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
    cfg.width  = 620;
    cfg.height = 460;
    cfg.backgroundColor = Theme::kPageBackground;
    auto win = CreateWindow(cfg);

    contactsView_.SetStore(&contacts_);
    win->AddChild(contactsView_.Build());
    contactsView_.Resize(static_cast<float>(cfg.width), static_cast<float>(cfg.height));
    win->onWindowResize = [this](int cw, int ch) {
        contactsView_.Resize(static_cast<float>(cw), static_cast<float>(ch));
    };
    win->Show();
    viewerWindows_.push_back(win);
}

// Earlier releases kept the cloud account secrets in obfuscated files under
// <data dir>/cloud-vault (UltraCloud's old FileSecretStore). They belong in
// the mail vault; carry them across once it is open and drop the files.
void UltraMailApp::MigrateCloudSecrets() {
    if (!cloudSecrets_ || !vault_.IsUnlocked() || dataDir_.empty()) return;
    std::vector<UltraCloud::Account> known;
    cloudAccounts_.List(known);
    UltraCloud::MigrateLegacyFileSecrets(dataDir_ + "/cloud-vault", known, *cloudSecrets_);
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
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;

    AttachmentCache cache(cacheDir_);
    const std::string path = cache.Write(attachment);
    if (path.empty()) {
        AlertError(parent,
                   "The attachment could not be opened.",
                   "\"" + (attachment.filename.empty() ? std::string("(unnamed)")
                                                       : attachment.filename)
                   + "\" could not be written to the attachment cache in "
                   + cacheDir_ + ". Check that the folder exists and is writable.");
        return;
    }

    // Hand the file to the operating system's default application — the same
    // behaviour as double-clicking it in a file manager. When nothing is
    // associated with the type, offer to save it instead.
    if (UltraCanvas::FileAssociations::HasDefaultApplication(path)) {
        std::string err;
        if (!UltraCanvas::FileAssociations::OpenWithDefaultApplication({path}, err)) {
            AlertError(parent, "The attachment could not be opened.",
                       err.empty() ? ("The system could not open " + path + ".") : err);
        }
        return;
    }

    const std::string name = attachment.filename.empty() ? std::string("this attachment")
                                                         : "\"" + attachment.filename + "\"";
    UltraCanvasAlert::Confirm(
        "There is no application associated with " + name + ".\n\n"
        "Would you like to save it instead?",
        "Open attachment",
        [this, attachment](bool save) { if (save) SaveAttachment(attachment); },
        parent);
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
    for (const auto& a : accounts_) feed_.SetAccount(a.accountId, a.email, a.displayName);

    // Keep the selection on an existing account (default: the first one).
    bool selectedExists = false;
    for (const auto& a : accounts_) if (a.accountId == selectedAccount_) selectedExists = true;
    if (!selectedExists) selectedAccount_ = accounts_.empty() ? "" : accounts_.front().accountId;

    // The badge's colours come from the address book, which the auto-collector
    // and the contact manager both write to — so re-read it here, where every
    // path that can have changed it ends up.
    RefreshContactIndex();

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

    // Default path: unlock silently with the local device key (no prompt). This
    // also creates a fresh vault on first run. Only an old master-password vault
    // (device key not yet written) falls through to the dialog below.
    if (errorText.empty() && vault_.TryAutoUnlock()) {
        MigrateCloudSecrets();
        if (onUnlocked) onUnlocked();
        return;
    }

    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    // Reaching the dialog means an existing vault must be opened with the master
    // password it was made with; ask the user to recall it (not choose one).
    const bool firstRun = false;

    PassphraseDialog::Show(parent, firstRun, errorText,
        [this, onUnlocked, parent](const std::string& passphrase) {
            switch (vault_.Unlock(passphrase)) {
                case VaultStatus::Ok:
                    // Persist the working passphrase as the device key so this
                    // is the last time the user is asked (Thunderbird-style).
                    vault_.PersistDeviceKey(passphrase);
                    MigrateCloudSecrets();
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

void UltraMailApp::HandleDeleteAccount(const std::string& accountId) {
    const std::string email = EmailForAccount(accountId);
    if (email.empty()) return;   // already gone / unknown id

    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    UltraCanvasAlert::Confirm(
        "Remove " + email + "?\n\nThis deletes the mail downloaded to this "
        "computer for the account. Mail on the server is not affected.",
        "Remove account",
        [this, accountId, email, parent](bool confirmed) {
            if (!confirmed) return;

            // Stop future background syncs for the account first. An in-flight
            // sync worker (syncsInFlight_) still holds valid store_/mailDir_
            // references; at worst it re-inserts a few rows after this, which
            // the next removal (or a restart) clears — acceptable here.
            scheduler_.Remove(accountId);

            // Forget the stored password / OAuth tokens. Best-effort: these are
            // no-ops when the vault is locked, and the leftover encrypted
            // entries are harmless (overwritten if the address is re-added).
            vault_.Remove(accountId);
            vault_.RemoveOAuthTokens(accountId);

            // Delete the messages, folders and account row in one transaction.
            if (UltraDbResult rm = store_.RemoveAccount(accountId); !rm) {
                AlertError(parent, "The account could not be removed.", DetailLine(rm));
                return;
            }

            // Delete the downloaded mail bodies (mailDir_/<accountId>/…).
            std::error_code ec;
            std::filesystem::remove_all(std::filesystem::path(mailDir_) / accountId, ec);

            // Drop the selection; Refresh() re-selects the first account left.
            if (selectedAccount_ == accountId) selectedAccount_.clear();
            Refresh();

            AlertSuccess(parent, email + " was removed.");
        },
        parent);
}

void UltraMailApp::HandleWizardSubmit(const AccountDraft& draft) {
    // 1. The provider table answers instantly for the well-known providers.
    DiscoveryResult disc = AutoDiscovery::FromPresets(draft.email);
    if (disc.found) { CompleteAccountSetup(draft, disc); return; }

    // 2. Adding an address again keeps the servers it already has (found by
    //    autoconfig or entered by hand the first time).
    for (const auto& a : accounts_) {
        if (a.email == draft.email && a.HasServers()) {
            CompleteAccountSetup(draft, SettingsFor(a));
            return;
        }
    }

    // 3. Ask the network, then the user.
    LookupServerSettings(draft);
}

void UltraMailApp::LookupServerSettings(const AccountDraft& draft) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    const std::string domain = EmailDomain(draft.email);

    // The lookup is a few HTTPS requests; Cancel just drops the answer.
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    std::weak_ptr<UltraCanvasModalDialog> waiting = WaitDialog::Show(parent,
        "Looking up server settings",
        "UltraMail is asking " + domain + " and the Thunderbird provider database "
        "for the mail servers of " + draft.email + "…",
        [cancelled]() { cancelled->store(true); });

    std::thread([this, draft, parent, cancelled, waiting]() {
        AutoDiscovery discovery;
        DiscoveryResult found = discovery.Discover(draft.email);

        auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, draft, parent, cancelled, waiting, found]() {
            if (cancelled->load()) return;
            WaitDialog::Close(waiting);
            if (found.found) { CompleteAccountSetup(draft, found); return; }

            // Nothing published for the domain: the manual page, seeded with
            // the conventional host names so most of it is already right. The
            // typed password checks the sign-in before the page closes; with
            // no password there is nothing to check.
            const std::string password = draft.password;
            ServerSettingsDialog::Verifier verify;
            if (!password.empty()) {
                verify = LoginVerifier([password](const ServerSettingsDialog::Result&,
                                                  const std::string& username,
                                                  UltraNetCredentials& c) {
                    c.type = UltraNetAuthType::Basic;
                    c.username = username;
                    c.password = password;
                    return UltraNetResult::Ok();
                });
            }
            ServerSettingsDialog::Show(parent, draft.email,
                "No mail settings are published for " + EmailDomain(draft.email)
                + ". Enter the servers from your provider's help page; the "
                  "sign-in is checked and the account added once they are saved.",
                AutoDiscovery::GuessForDomain(draft.email),
                [this, draft](const ServerSettingsDialog::Result& r) {
                    CompleteAccountSetup(draft, r.settings);
                },
                verify);
        });
    }).detach();
}

void UltraMailApp::EditServerSettings(const std::string& accountId) {
    const Account* found = nullptr;
    for (const auto& a : accounts_) if (a.accountId == accountId) found = &a;
    if (!found) return;
    const Account account = *found;   // the list may change under the dialog
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;

    DiscoveryResult current = SettingsFor(account);
    if (!current.found) current = AutoDiscovery::GuessForDomain(account.email);

    // The check signs in with what the vault holds for the account, so the
    // vault is opened first; the sign-in method (password or OAuth2) is
    // resolved on the worker like a sync would.
    EnsureVaultUnlocked([this, account, current, parent]() {
        const std::string accountId = account.accountId;
        ServerSettingsDialog::Verifier verify;
        if (vault_.MethodFor(accountId) != SignInMethod::None) {
            verify = LoginVerifier([this, accountId, current](
                    const ServerSettingsDialog::Result&,
                    const std::string& username, UltraNetCredentials& c) {
                return ResolveCredentials(accountId, username, OAuthProviderFor(current), c);
            });
        }
        ServerSettingsDialog::Show(parent, account.email,
            "New mail cannot be fetched for " + account.email + " because its mail "
            "servers are not known. Enter them from your provider's help page; the "
            "sign-in is checked before they are saved.",
            current,
            [this, account](const ServerSettingsDialog::Result& r) {
                Account updated = account;
                AutoDiscovery::ApplyTo(updated, r.settings);
                if (UltraDbResult up = store_.UpsertAccount(updated); !up) {
                    AlertError(window_ ? window_.get() : nullptr,
                               "The server settings could not be saved.", DetailLine(up));
                    return;
                }
                Refresh();
                StartBackgroundSync();
                SyncAccount(account.accountId);
            },
            verify);
    });
}

void UltraMailApp::HandleAccountSettings(const std::string& accountId) {
    const Account* found = nullptr;
    for (const auto& a : accounts_) if (a.accountId == accountId) found = &a;
    if (!found) return;
    const Account account = *found;   // the list may change under the dialog
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;

    DiscoveryResult current = SettingsFor(account);
    if (!current.found) current = AutoDiscovery::GuessForDomain(account.email);

    // Editing signs in with either a newly typed password or the account's
    // stored credentials, so the vault is opened first (the later Store /
    // OAuth sign-in then needs no second prompt).
    EnsureVaultUnlocked([this, account, current, parent]() {
        const std::string accountId = account.accountId;
        // Capabilities come from the provider, not the current sign-in method,
        // so a Gmail account created with a password can still switch to Google
        // sign-in: Gmail offers both (app password OR OAuth), Outlook is OAuth
        // only, a generic IMAP account is password only.
        const std::string provider = OAuthProviderFor(current);   // "" for generic

        ServerSettingsDialog::AccountFields fields;
        fields.edit            = true;
        fields.displayName     = account.displayName;
        fields.acceptsPassword = ProviderAcceptsPassword(current);
        fields.canOAuth        = !provider.empty();
        fields.providerName    = provider.empty() ? std::string()
                                                   : OAuthProviderDisplayName(provider);
        fields.showReadingPane  = prefs_.showReadingPane;
        fields.fetchSenderIcons = prefs_.fetchSenderIcons;
        // The red "Delete account" button in the settings dialog's bottom row.
        // It closes the page, then HandleDeleteAccount runs the confirm-and-remove.
        fields.onDelete = [this, accountId]() { HandleDeleteAccount(accountId); };

        // The login check uses the typed new password when present, else the
        // account's stored credentials. It only runs on the password path; the
        // OAuth button re-signs in through the browser instead.
        ServerSettingsDialog::Verifier verify;
        if (fields.acceptsPassword) {
            verify = LoginVerifier([this, accountId, provider](
                    const ServerSettingsDialog::Result& r,
                    const std::string& username, UltraNetCredentials& c) {
                if (!r.newPassword.empty()) {
                    c.type = UltraNetAuthType::Basic;
                    c.username = username;
                    c.password = r.newPassword;
                    return UltraNetResult::Ok();
                }
                return ResolveCredentials(accountId, username, provider, c);
            });
        }

        ServerSettingsDialog::Show(parent, account.email,
            "Edit the settings for " + account.email + ". The sign-in is checked "
            "before the changes are saved.",
            current,
            [this, account, provider](const ServerSettingsDialog::Result& r) {
                // The reading-pane checkbox is an app-wide view option; apply and
                // remember it regardless of the server/credential outcome below.
                if (r.showReadingPane != prefs_.showReadingPane) {
                    prefs_.showReadingPane = r.showReadingPane;
                    prefs_.Save(prefsPath_);
                    mailView_.SetReadingPane(prefs_.showReadingPane);
                }
                if (r.fetchSenderIcons != prefs_.fetchSenderIcons) {
                    prefs_.fetchSenderIcons = r.fetchSenderIcons;
                    prefs_.Save(prefsPath_);
                    senderIcons_.SetNetworkEnabled(prefs_.fetchSenderIcons);
                }
                Account updated = account;
                AutoDiscovery::ApplyTo(updated, r.settings);
                updated.displayName =
                    r.displayName.empty() ? LocalPart(account.email) : r.displayName;
                if (UltraDbResult up = store_.UpsertAccount(updated); !up) {
                    AlertError(window_ ? window_.get() : nullptr,
                               "The account settings could not be saved.", DetailLine(up));
                    return;
                }
                // A typed password replaces the stored one (the vault is open).
                if (!r.newPassword.empty()) vault_.Store(account.accountId, r.newPassword);
                Refresh();
                StartBackgroundSync();
                // The "Sign in with <provider>" button flags a re-auth: run the
                // browser sign-in (switching a password account to OAuth too).
                // Without a configured OAuth client, say what is missing rather
                // than let the sign-in fail obscurely.
                if (r.reauth) {
                    if (!OAuthApps::Has(provider)) { ReportMissingOAuthClient(provider); return; }
                    StartOAuthSignIn(account.accountId, account.email, provider);
                    return;
                }
                SyncAccount(account.accountId);
            },
            verify, fields);
    });
}

void UltraMailApp::ReportMissingOAuthClient(const std::string& providerId) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    const std::string name = OAuthProviderDisplayName(providerId);
    std::string upper = providerId;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    AlertWarning(parent, "UltraMail cannot sign in to " + name + " yet.",
                 "No OAuth client is configured for it. Put the client id of an "
                 "OAuth client registered with " + name + " into " + dataDir_
                 + "/oauth.ini under [" + providerId + "] (client_id = …), or set "
                 "ULTRAMAIL_" + upper + "_CLIENT_ID — see Docs/UltraMail/AccountSetup.md.");
}

ServerSettingsDialog::Verifier UltraMailApp::LoginVerifier(
        std::function<UltraNetResult(const ServerSettingsDialog::Result& candidate,
                                     const std::string& username, UltraNetCredentials&)> credentials) {
    return [this, credentials](const ServerSettingsDialog::Result& candidate,
                               std::function<void(UltraNetResult)> onResult) {
        IMailboxProtocolPlugin* imap = ImapPlugin();
        if (!imap) {
            onResult(UltraNetResult::Error(UltraNetResultCode::PluginNotFound,
                "the IMAP plug-in is not loaded (" + pluginDir_ + "), so the sign-in "
                "could not be checked"));
            return;
        }
        // The check talks to the server (and may refresh an OAuth2 token
        // first), so it runs on a worker; the answer comes back on the UI.
        const DiscoveryResult settings = candidate.settings;
        std::thread([imap, candidate, settings, credentials, onResult]() {
            UltraNetCredentials c;
            UltraNetResult r = credentials
                ? credentials(candidate, settings.imap.username, c) : UltraNetResult::Ok();
            if (r) r = LoginCheck::Imap(*imap, settings.imap, c);
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) return;
            app->PostToUIThread([onResult, r]() { onResult(r); });
        }).detach();
    };
}

void UltraMailApp::CompleteAccountSetup(const AccountDraft& draft, const DiscoveryResult& disc) {
    Account a;
    a.accountId   = SlugFromEmail(draft.email);
    a.email       = draft.email;
    a.displayName = draft.displayName.empty() ? LocalPart(draft.email) : draft.displayName;
    a.shortName   = LocalPart(draft.email);
    // The servers are stored with the account, so later syncs and sends do
    // not depend on the provider table (or on the network) again.
    AutoDiscovery::ApplyTo(a, disc);

    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;

    if (UltraDbResult up = store_.UpsertAccount(a); !up) {
        AlertError(parent, "The account could not be saved.", DetailLine(up));
        return;
    }

    // Store the password out of the config, in the credential vault — which
    // needs the master password first. If the store fails the account still
    // works, but every later sync would be rejected for no visible reason, so
    // warn now while the user can act on it.
    const std::string accountId = a.accountId;
    const std::string email     = draft.email;
    const std::string password  = draft.password;
    // Gmail and Outlook sign in through the browser when no password was
    // typed (an app password typed at Gmail still works the classic way).
    const std::string provider  = OAuthProviderFor(disc);
    const bool useOAuth = !provider.empty() && password.empty();
    // Runs after the settings alert is dismissed, so the master-password
    // prompt is not stacked underneath it.
    // Once the password (or the sign-in) is in the vault the account is
    // complete: put it on the background schedule and fetch its inbox right
    // away, so the new tile fills instead of waiting for the next timer tick
    // or a manual Reload.
    auto storePassword = [this, accountId, email, password, provider, useOAuth, parent]() {
        if (useOAuth) {
            if (!OAuthApps::Has(provider)) {
                std::string upper = provider;
                for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                AlertWarning(parent, "The account was added, but UltraMail cannot "
                                     "sign in to " + OAuthProviderDisplayName(provider)
                                     + " yet.",
                             "No OAuth client is configured for it. Put the client "
                             "id of an OAuth client registered with "
                             + OAuthProviderDisplayName(provider) + " into " + dataDir_
                             + "/oauth.ini under [" + provider + "] (client_id = …), or "
                             "set ULTRAMAIL_" + upper + "_CLIENT_ID — see "
                             "Docs/UltraMail/AccountSetup.md. Alternatively add the "
                             "account again with an app password.");
                return;
            }
            // The tokens go into the vault, so open it before the browser
            // round-trip rather than after — a cancelled unlock then costs
            // nothing.
            EnsureVaultUnlocked([this, accountId, email, provider]() {
                StartOAuthSignIn(accountId, email, provider);
            });
            return;
        }
        if (password.empty()) {
            AlertWarning(parent, "The account was added without a password, so "
                                 "its mail cannot be fetched.",
                         "Add it again (Add account) with its password; the "
                         "existing entry is updated.");
            return;
        }
        EnsureVaultUnlocked([this, accountId, password, parent]() {
            if (!vault_.Store(accountId, password)) {
                AlertWarning(parent, "The account was added, but its password "
                                     "could not be saved to the credential vault.",
                             "Mail for it cannot be fetched until the password is "
                             "stored. Check that " + vault_.VaultPath()
                             + " is writable, then add the account again with "
                               "its password; the existing entry is updated.");
                return;
            }
            StartBackgroundSync();
            SyncAccount(accountId);
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

    // Report the servers the account will use and where they came from.
    std::string summary;
    if (disc.source == "manual")
        summary = "Account ready — server settings saved.";
    else if (disc.source == "autoconfig")
        summary = "Account ready — settings found for " + EmailDomain(draft.email)
                + (disc.displayName.empty() ? "" : " (" + disc.displayName + ")") + ".";
    else
        summary = "Account ready — settings detected"
                + (disc.displayName.empty() ? "" : " (" + disc.displayName + ")") + ".";
    std::string detail = "Incoming (IMAP): " + AutoDiscovery::ImapServerUrl(disc.imap)
                       + "\nOutgoing (SMTP): " + AutoDiscovery::SmtpServerUrl(disc.smtp);
    // Providers that expect OAuth2 (Gmail, Outlook), Yahoo and iCloud
    // reject the normal account password over IMAP. Gmail and Outlook sign
    // in through the browser; a typed password there, and Yahoo / iCloud
    // always, need an app password from the provider's security settings;
    // say so here, where the user can still act on it.
    if (useOAuth)
        detail += "\nSign-in: " + OAuthProviderDisplayName(provider)
                + " account, in your browser (next step).";
    else if (!provider.empty() && !ProviderAcceptsPassword(disc))
        detail += "\nSign-in: password — but " + disc.displayName + " no longer "
                  "accepts passwords in mail programs. Add the account again "
                  "with the password empty to sign in with "
                + OAuthProviderDisplayName(provider) + " in your browser.";
    else if (ProviderNeedsAppPassword(disc))
        detail += "\nSign-in: password. " + disc.displayName
                + " needs an app password for mail programs (generated in "
                  "your account's security settings); the normal sign-in "
                  "password is rejected.";
    AlertSuccess(parent, summary, detail, storePassword);
}

std::string UltraMailApp::EmailForAccount(const std::string& accountId) const {
    for (const auto& a : accounts_) if (a.accountId == accountId) return a.email;
    return "";
}

UltraNetResult UltraMailApp::ResolveCredentials(const std::string& accountId,
                                                const std::string& username,
                                                const std::string& providerId,
                                                UltraNetCredentials& out) {
    return oauth_.CredentialsFor(vault_, accountId, username, providerId, out);
}

DiscoveryResult UltraMailApp::SettingsFor(const Account& account) {
    return AutoDiscovery::ForAccount(account);
}

DiscoveryResult UltraMailApp::SettingsForEmail(const std::string& email) const {
    for (const auto& a : accounts_) if (a.email == email) return SettingsFor(a);
    return AutoDiscovery::FromPresets(email);
}

UltraNetResult UltraMailApp::PrepareSmtp(const std::string& accountId, UltraNetMailOptions& o) {
    const Account* account = nullptr;
    for (const auto& a : accounts_) if (a.accountId == accountId) account = &a;
    if (!account)
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "the account of this message no longer exists");
    const DiscoveryResult settings = SettingsFor(*account);
    if (!settings.found)
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "no outgoing (SMTP) server is known for " + account->email);
    o.useTls      = settings.smtp.security != MailSecurity::Plain;
    o.implicitTls = settings.smtp.security == MailSecurity::SslTls;
    const std::string username =
        settings.smtp.username.empty() ? account->email : settings.smtp.username;
    return ResolveCredentials(accountId, username, OAuthProviderFor(settings), o.credentials);
}

bool UltraMailApp::MaybeOfferReauth(const std::string& accountId, UltraNetResultCode code,
                                    const std::string& provider,
                                    std::function<void()> onReauthed) {
    // Only an OAuth account can be re-signed-in, and only a sign-in rejection is
    // worth re-signing-in for: AuthenticationFailed (the provider refused the
    // refresh — invalid_grant) or AuthenticationRequired (the stored sign-in has
    // expired with no usable refresh token). Every other failure — no client id,
    // a bad server address, a network drop — the caller reports as usual.
    if (provider.empty()) return false;
    if (code != UltraNetResultCode::AuthenticationFailed &&
        code != UltraNetResultCode::AuthenticationRequired)
        return false;

    const std::string email = EmailForAccount(accountId);
    const std::string name  = OAuthProviderDisplayName(provider);
    AlertErrorRetry(window_ ? window_.get() : nullptr,
        "Your " + name + " sign-in for " + (email.empty() ? accountId : email)
            + " has expired.",
        "UltraMail could not refresh the sign-in — the token was expired or "
        "revoked. Choose Retry to sign in to " + name + " again.",
        [this, accountId, email, provider, onReauthed]() {
            StartOAuthSignIn(accountId, email, provider, onReauthed);
        });
    return true;
}

void UltraMailApp::StartOAuthSignIn(const std::string& accountId, const std::string& email,
                                    const std::string& providerId,
                                    std::function<void()> onReauthed) {
    // Out-of-band providers (Yahoo) can't redirect to a loopback listener, so
    // they take a separate flow: open the browser, then prompt for the code the
    // provider shows rather than waiting on a socket.
    if (OAuthApps::Get(providerId).redirectUri == "oob") {
        StartOAuthOobSignIn(accountId, email, providerId, std::move(onReauthed));
        return;
    }

    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    const std::string providerName = OAuthProviderDisplayName(providerId);

    // The wait dialog's Cancel only detaches the flow: UltraNet keeps listening
    // for the redirect until its timeout, and whatever arrives then is dropped.
    // (The listener sits on an ephemeral port, so a retry is never blocked.)
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    std::weak_ptr<UltraCanvasModalDialog> waiting =
        OAuthWaitDialog::Show(parent, providerName, email,
                              [cancelled]() { cancelled->store(true); });

    std::thread([this, accountId, email, providerId, providerName, parent, cancelled, waiting,
                 onReauthed]() {
        OAuthTokens tokens;
        UltraNetResult r = oauth_.SignIn(providerId, email,
            [](const std::string& url) { UltraCanvas::OpenURL(url); }, tokens);

        auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, accountId, email, providerName, parent, cancelled,
                             waiting, r, tokens, onReauthed]() {
            if (cancelled->load()) return;
            OAuthWaitDialog::Close(waiting);
            if (!r) {
                AlertError(parent, "Signing in to " + providerName + " for " + email
                                   + " did not succeed, so its mail cannot be fetched.",
                           FriendlyMessage(r) + " Add the account again to retry.");
                return;
            }
            if (!vault_.StoreOAuthTokens(accountId, tokens)) {
                AlertWarning(parent, "Signed in to " + providerName + ", but the sign-in "
                                     "could not be saved to the credential vault.",
                             "Check that " + vault_.VaultPath() + " is writable, then "
                             "add the account again.");
                return;
            }
            StartBackgroundSync();
            SyncAccount(accountId);
            // Re-sign-in recovery: retry whatever action hit the dead sign-in now
            // that a fresh token is stored.
            if (onReauthed) onReauthed();
        });
    }).detach();
}

void UltraMailApp::StartOAuthOobSignIn(const std::string& accountId, const std::string& email,
                                       const std::string& providerId,
                                       std::function<void()> onReauthed) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    const std::string providerName = OAuthProviderDisplayName(providerId);

    // Build the consent URL (cheap: PKCE + string work, no network) and open it.
    std::string consentUrl, codeVerifier;
    UltraNetResult began = oauth_.BeginOob(providerId, email, consentUrl, codeVerifier);
    if (!began) {
        AlertError(parent, "Signing in to " + providerName + " for " + email
                           + " could not be started.",
                   FriendlyMessage(began) + " Add the account again to retry.");
        return;
    }
    UltraCanvas::OpenURL(consentUrl);

    // Prompt for the code the provider showed; the exchange runs on submit.
    OAuthCodeDialog::Show(parent, providerName, email,
        [this, accountId, email, providerId, providerName, parent, codeVerifier,
         onReauthed](const std::string& code) {
            std::thread([this, accountId, email, providerId, providerName, parent, code,
                         codeVerifier, onReauthed]() {
                OAuthTokens tokens;
                UltraNetResult r = oauth_.CompleteOob(providerId, code, codeVerifier, tokens);

                auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
                if (!app) return;
                app->PostToUIThread([this, accountId, email, providerName, parent, r, tokens,
                                     onReauthed]() {
                    if (!r) {
                        AlertError(parent, "Signing in to " + providerName + " for " + email
                                           + " did not succeed, so its mail cannot be fetched.",
                                   FriendlyMessage(r) + " Add the account again to retry.");
                        return;
                    }
                    if (!vault_.StoreOAuthTokens(accountId, tokens)) {
                        AlertWarning(parent, "Signed in to " + providerName + ", but the sign-in "
                                             "could not be saved to the credential vault.",
                                     "Check that " + vault_.VaultPath() + " is writable, then "
                                     "add the account again.");
                        return;
                    }
                    StartBackgroundSync();
                    SyncAccount(accountId);
                    if (onReauthed) onReauthed();
                });
            }).detach();
        });
}

} // namespace UltraMail
