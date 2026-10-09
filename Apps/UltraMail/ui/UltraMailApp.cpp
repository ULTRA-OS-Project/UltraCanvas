// Apps/UltraMail/ui/UltraMailApp.cpp
// Version: 0.9.19 - a password typed on the server settings page drops stored
//                   OAuth tokens, so the account switches to it
// Version: 0.9.18 - Settings > Display > Links: a link's address in the status line or
//                   as a tooltip (ApplyLinkDisplay)
// Version: 0.9.17 - the status line lists a message's links (summary, every link in
//                   its tooltip) and shows where the link under the pointer goes
// Version: 0.9.16 - Edit and Delete in the Outbox window wait for a running
//                   send instead of refusing; a Drafts copy that cannot be
//                   deleted now is deleted by a later pass
// Version: 0.9.15 - the Outbox window: the waiting messages with Send now,
//                   Edit and Delete (toolbar "Outbox (N)"); a sent message
//                   is filed in the Sent folder; outbox work runs in one queue
// Version: 0.9.14 - Send queues the message, closes the compose window and sends
//                   in the background, keeping a copy in Drafts until it has
//                   gone out; a message not sent is reported with Retry and
//                   tried again by itself (OutboxRetryClock)
// Version: 0.9.13 - a compose window per message: each has its own view, so a
//                   second one no longer takes over the first one's buttons
// Version: 0.9.12 - a signature per account (Account Settings > Signature): put
//                   into new mail, replies and forwards
// Version: 0.9.11 - and right after the computer wakes from sleep (WakeDetector)
// Version: 0.9.10 - new mail is fetched right after start, not five minutes later
// Version: 0.9.9 - a Settings window (gear at the right end of the toolbar, as in
//                  UltraFiler): layout, HTML / plain text, text size, remote-image
//                  policy with trusted websites, sender icons
// Version: 0.9.8 - replies and forwards of HTML mail keep the formatting
// Version: 0.9.7 - the vault auto-unlocks with a local device key (Thunderbird-
//                  style, no master-password prompt); old vaults migrate once
// Last Modified: 2026-10-04
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailApp.h"
#include "UltraMailHeaderText.h"

#include "UltraMailAlerts.h"
#include "UltraMailSettingsDialog.h"
#include "UltraMailTheme.h"

#include "UltraMailAttachmentCache.h"
#include "UltraMailDiscovery.h"
#include "UltraMailCredentialVault.h"
#include "UltraMailComposer.h"
#include "UltraMailRichComposer.h"
#include "UltraMailSignatureDialog.h"
#include "UltraMailSender.h"
#include "UltraMailContactCollector.h"
#include "UltraMailSyncService.h"
#include "UltraMailUnsubscribe.h"
#include "UltraMailOAuth.h"
#include "UltraMailLoginCheck.h"
#include "UltraMailWaitDialog.h"
#include "UltraMailOAuthCodeDialog.h"

#include <UltraCloud/UltraCloudMemory.h>

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDesktopShell.h"
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

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <atomic>
#include <fstream>
#include <string>
#include <thread>
#include "UltraCanvasPathUtf8.h"
#include "UltraMailSenderBrands.h"   // RegistrableDomain
#include <map>

// ULTRAMAIL_VERSION comes from the build alone: CMake reads the first line of
// Docs/UltraMail/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it as a
// compile definition. No fallback, so a build that lost it fails instead of
// showing a wrong number in the window title.
#ifndef ULTRAMAIL_VERSION
#error "ULTRAMAIL_VERSION is not defined: build through CMake, which reads it from Docs/UltraMail/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr int   kWindowWidth   = 1180;
constexpr int   kWindowHeight  = 760;
constexpr int   kActionIcon    = 12;
// The first fetch after start waits this long, so the main window is painted
// (with the cached mail) before the network work begins.
constexpr unsigned int kStartupSyncDelayMs = 400;
// After a wake from sleep, the check waits this long: Wi-Fi usually needs a few
// seconds to reconnect, and a check before that would only report "offline".
constexpr unsigned int kWakeSyncDelayMs = 5000;

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
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(dataDir), ec);
    if (ec && outError) *outError = ec.message();
    const std::string dbPath = dataDir + "/mail.db";

    UltraDbResult opened = store_.Open("ultramail", dbPath);
    if (opened) opened = workerStore_.Open("ultramail-worker", dbPath);
    if (!opened) {
        if (outError) *outError = DetailLine(opened);
        return false;
    }

    dataDir_  = dataDir;
    cacheDir_ = dataDir + "/cache";
    // Attachments opened in the viewer are copies of what the message already
    // holds, so their folder is pruned at every start - before any viewer has
    // one open: what was not opened for a week goes, then the oldest until
    // the rest fits in 256 MB. Earlier versions wrote them straight into the
    // cache folder, where nothing else keeps files (the sender icons have a
    // folder of their own); those loose files are cleared once.
    attachmentDir_ = cacheDir_ + "/attachments";
    AttachmentCache(cacheDir_).Prune(/*maxAgeSeconds=*/-1, /*maxBytes=*/0);
    AttachmentCache(attachmentDir_).Prune(/*maxAgeSeconds=*/7 * 24 * 3600,
                                          /*maxBytes=*/256ull * 1024 * 1024);
    mailDir_  = dataDir + "/mail";
    // App-wide view preferences (e.g. the reading pane). A missing file keeps
    // the defaults; it is written the first time the user changes a setting.
    prefsPath_ = dataDir + "/preferences.ini";
    prefs_.Load(prefsPath_);
    ApplyNeedsAnswerRules();
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
    config.title  = "UltraMail " ULTRAMAIL_VERSION;
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
    startPage_.onSettings   = [this]() { OpenSettings(); };
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
    // Messages left in the outbox by an earlier run go out by themselves,
    // shortly after start (once the window is up and the network had a moment).
    StartOutboxRetryTimer();
    // (and Drafts copies of deleted messages still to be removed)
    if (OutboxPending() > 0 || OutboxWithdrawn() > 0)
        outboxRetry_.RetryAt(NowMonotonicSec() + 20);
    RefreshOutbox();

    // Migration: an existing vault made with a master password (before device
    // keys) stays locked after Initialize's silent attempt. Prompt once now so
    // mail syncs; EnsureVaultUnlocked persists the entered password as the
    // device key, so this is the only time it is asked.
    if (!accounts_.empty() && !vault_.IsUnlocked()) {
        EnsureVaultUnlocked([this]() { RunSyncs(/*force=*/false); Refresh(); });
    } else if (!accounts_.empty() && ImapPlugin()) {
        // Fetch new mail right after start. The periodic timer's first tick is
        // five minutes out, so without this the inbox showed only what was
        // cached until Update was pressed - and the status line never said it
        // was checking. Every account is due (never synced in this run). A
        // background sync, not a user one: a network that is not up yet right
        // after boot takes the offline grace period instead of an alert. Run
        // from the loop, shortly after the window is shown, so it paints first.
        if (auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent()) {
            app->StartTimer(kStartupSyncDelayMs, /*periodic=*/false,
                            [this](UltraCanvas::TimerId) { RunSyncs(/*force=*/false); });
        }
    }

    // Demo path: seed mail and auto-collect its senders; the main window shows
    // the mail. "contacts" also opens the contact manager on the collected
    // senders — on request only, since it lands on top of the main window.
    if (const char* dcol = std::getenv("ULTRAMAIL_DEMO_COLLECT"); dcol && *dcol) {
        SeedDemoMail();
        Refresh();
        if (std::string(dcol) == "contacts") OpenContacts();
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
        ComposeView* view = OpenComposer(Composer::NewMessage("Erika Example", "erika@example.com"));
        if (*dcl == '2' && view) view->OpenCloudLinkPicker();   // =2: picker open too
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
    // Demo path: the account signature. =1 opens the signature editor on a
    // designed HTML signature; =2 a new message from the demo account (seeded
    // by ULTRAMAIL_DEMO_MAIL=1) signed with it.
    if (const char* dsig = std::getenv("ULTRAMAIL_DEMO_SIGNATURE"); dsig && *dsig) {
        Signature sig;
        sig.kind = SignatureKind::Html;
        sig.text = "Erika Example\nSales, ACME Ltd.";
        sig.html = "<p><b><span style=\"color:#1E3A8A\">Erika Example</span></b><br>"
                   "<i>Sales</i>, ACME Ltd.<br>"
                   "<a href=\"https://acme.example\">acme.example</a></p>";
        if (*dsig == '1') {
            SignatureDialog::Show(window_.get(), "erika@example.com", sig,
                                  [](const Signature&) {});
        } else {
            SaveSignature("erika", sig);
            OpenComposer(WithSignature(Composer::NewMessage("Erika Example", "erika@example.com"),
                                       DraftPurpose::NewMessage));
        }
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
        Theme::FitToLabel(button, width);
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
        OpenComposer(WithSignature(Composer::NewMessage(name, addr), DraftPurpose::NewMessage));
    });
    // Update: download new mail for the account on screen now, rather than
    // waiting for the background check.
    reloadButton_ = makeAction("umReload", "Update", 80, "mail-download.svg", false,
                               [this]() { HandleReload(); });
    reloadButton_->SetTooltip("Download new mail for this account now");
    makeAction("umContacts", "Contacts", 76, "", false, [this]() { OpenContacts(); });
    // Shown while messages wait to be sent (RefreshOutbox keeps the count).
    outboxButton_ = makeAction("umOutbox", "Outbox", 76, "", false, [this]() { OpenOutbox(); });
    outboxButton_->SetTooltip("Messages waiting to be sent");
    outboxButton_->SetVisible(false);
    toolbar->AddStretchSpacer(1);
    makeAction("umSettings", "Account Settings", 0, "", false, [this]() {
        if (!selectedAccount_.empty()) HandleAccountSettings(selectedAccount_);
    });
    makeAction("umAddAccount", "Add account", 0, "", false,
               [this]() { HandleAddAccount(); });
    // The gear at the far right opens the settings window (UltraFiler's gear).
    toolbar->AddChild(SettingsDialog::MakeGearButton("umAppSettings", Theme::kControlHeight,
                                                     [this]() { OpenSettings(); }));
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
        // Its last failure, if any, before Refresh() - whose folder fetch
        // replaces it with "Opening …" while it runs.
        ShowAccountStatus();
        UpdateConnectionIndicator();
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
    // An HTML message is answered and forwarded with its formatting; a
    // plain-text one with "> "-quoted text.
    mailView_.onReply = [this](const SourceMessage& src, const std::string& selfName,
                               const std::string& selfAddr) {
        Draft draft = Composer::Reply(src, selfName, selfAddr, /*replyAll=*/false);
        MakeRichReply(draft, src);
        OpenComposer(WithSignature(std::move(draft), DraftPurpose::ReplyOrForward));
    };
    mailView_.onForward = [this](const SourceMessage& src, const std::string& selfName,
                                 const std::string& selfAddr) {
        Draft draft = Composer::Forward(src, selfName, selfAddr);
        MakeRichForward(draft, src);
        OpenComposer(WithSignature(std::move(draft), DraftPurpose::ReplyOrForward));
    };
    mailView_.onDelete     = [this](const MessageEnvelope& e) { HandleDeleteMessage(e); };
    mailView_.onJunk       = [this](const MessageEnvelope& e) { HandleJunkMessage(e); };
    mailView_.onMarkUnread = [this](const MessageEnvelope& e) { HandleMarkUnread(e); };
    mailView_.onMarkRead   = [this](const MessageEnvelope& e) { HandleMarkRead(e); };
    auto lowerAddr = [](std::string a) {
        for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return a;
    };
    // Settings > Privacy > Images: whether a message's pictures on the web load
    // by themselves. Junk and suspicious mail never ask here (the preview
    // decides that first).
    mailView_.remoteImagesAllowed = [this, lowerAddr](const std::string& addr) {
        switch (prefs_.remoteImages) {
            case RemoteImagePolicy::LoadAlways: return true;
            case RemoteImagePolicy::LoadNever:  return false;
            case RemoteImagePolicy::LoadTrusted: break;
        }
        const std::string a = lowerAddr(addr);
        if (prefs_.remoteImageSenders.count(a)) return true;
        if (const auto at = a.rfind('@'); at != std::string::npos &&
            prefs_.IsTrustedDomain(a.substr(at + 1)))
            return true;
        Contact contact;
        bool found = false;
        return contacts_.FindByEmail(a, contact, found) && found;
    };
    // ... and, for any other message, the pictures hosted on a trusted website.
    mailView_.remoteImageHostTrusted = [this](const std::string& url) {
        return prefs_.remoteImages == RemoteImagePolicy::LoadTrusted && prefs_.IsTrustedDomain(url);
    };
    mailView_.onAlwaysAllowRemoteImages = [this, lowerAddr](const std::string& addr) {
        if (prefs_.remoteImageSenders.insert(lowerAddr(addr)).second) {
            prefs_.Save(prefsPath_);
            SettingsDialog::SyncWithPreferences();
        }
    };
    mailView_.onAddToContactGroup = [this](const MessageEnvelope& e, const ContactPlace& p) {
        AddSenderToContactGroup(e, p);
    };
    mailView_.contactGroups = [this]() {
        std::vector<GroupCount> groups;
        if (contacts_.IsOpen()) contacts_.ListGroups(groups);
        return groups;
    };
    mailView_.onNotJunk    = [this](const MessageEnvelope& e) { HandleNotJunk(e); };
    mailView_.onUnsubscribe = [this](const MessageEnvelope& e) { HandleUnsubscribe(e); };
    mailView_.onMoveTo     = [this](const MessageEnvelope& e, const std::string& folder) {
        HandleMoveMessage(e, folder);
    };
    mailView_.onSetNeedsAnswer = [this](const MessageEnvelope& e, bool needs) {
        HandleSetNeedsAnswer(e, needs);
    };
    // The list's right-click menu: add the sender to the address book, or edit
    // the contact that already holds its address. Either way the badges and the
    // "known sender" state follow as soon as it is saved.
    mailView_.onAddContact  = [this](const MessageEnvelope& e) { EditSenderContact(e, /*isNew=*/true); };
    mailView_.onEditContact = [this](const MessageEnvelope& e) { EditSenderContact(e, /*isNew=*/false); };
    mailView_.onViewSource = [this](const std::string& subject, const std::string& raw) {
        OpenSourceViewer(subject, raw);
    };
    mailView_.onLinksShown = [this](const std::vector<MessageLink>& links) { ShowMessageLinks(links); };
    mailView_.onLinkHovered = [this](const std::string& href) { ShowHoveredLink(href); };
    // The folder tree switched to a folder under a different account: adopt that
    // account (and highlight its tile) without re-showing its inbox, so the
    // tree's chosen folder stays open.
    mailView_.onSelectAccount = [this](const std::string& accountId) {
        selectedAccount_ = accountId;
        ShowAccountStatus();
        UpdateConnectionIndicator();
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
        SyncFolder(accountId, folder, /*userInitiated=*/false);
    };
    auto mail = mailView_.Build();
    accountView_->AddChild(mail);
    mail->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // Apply the remembered reading-pane choice (default on; a rebuild only when off).
    mailView_.SetReadingPane(prefs_.showReadingPane);
    mailView_.SetBodyOptions(prefs_.showHtml, static_cast<float>(prefs_.messageTextSize));
    mailView_.SetFolderTreeWidth(prefs_.folderTreeWidthMode == FolderTreeWidthMode::FitToText,
                                 prefs_.folderTreeWidth);

    // ----- Status line: what the app is currently doing -----
    // A turning ring left of the text while anything runs in the background
    // (sync, send, mailbox action); blank when the app is idle.
    const float statusHeight = Theme::kToolbarHeight * 0.75f;
    auto statusRow = CreateContainer("umStatusRow", 0, 0, 0, statusHeight);
    statusRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kGap * 0.5f)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    if (auto style = statusRow->GetContainerStyle(); true) {
        style.autoShowScrollbars = false;
        statusRow->SetContainerStyle(style);
    }
    accountView_->AddChild(statusRow);
    statusRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    busyIndicator_ = CreateBusyIndicator("umBusy", 0, 0, statusHeight * 0.6f);
    BusyIndicatorStyle busyStyle = busyIndicator_->GetStyle();
    busyStyle.arcColor = Theme::kAccent;
    busyIndicator_->SetStyle(busyStyle);
    busyIndicator_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    statusRow->AddChild(busyIndicator_);

    statusLabel_ = Theme::MakeLine("umStatus", "Ready", statusHeight,
                                   Theme::kSizeSecondary, Theme::kTextSecondary);
    statusRow->AddChild(statusLabel_);
    statusLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

    // The links of the message being read, so the reader can check where they
    // go before clicking: a summary, every link in its tooltip, and the target
    // of the link under the pointer while it is on one.
    linksLabel_ = Theme::MakeLine("umLinks", "", statusHeight,
                                  Theme::kSizeSecondary, Theme::kTextSecondary);
    linksLabel_->layoutItem.SetFlexGrow(0).SetFlexShrink(1);
    statusRow->AddChild(linksLabel_);
    ApplyLinkDisplay();

    // The connection pill, right-aligned: the selected account's last contact
    // with its mail server. Hovering it tells the server, when it was last
    // reached and why it was not.
    connectionBadge_ = CreateBadge("umConnection", 0, 0, "Not checked",
                                   BadgeVariant::Neutral);
    BadgeStyle badgeStyle = connectionBadge_->GetStyle();
    badgeStyle.height   = statusHeight * 0.7f;
    badgeStyle.fontSize = Theme::kSizeSmall;
    badgeStyle.paddingH = Theme::kGap;
    connectionBadge_->SetStyle(badgeStyle);
    connectionBadge_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    statusRow->AddChild(connectionBadge_);
    statusRow->SetPadding(0, Theme::kGap, 0, 0);   // the pill clear of the edge
    UpdateBusyIndicator();
    UpdateConnectionIndicator();

    return accountView_;
}

void UltraMailApp::NoteConnection(const std::string& accountId, ConnectionState state,
                                  const std::string& reason) {
    ConnectionInfo& c = connection_[accountId];
    const std::time_t now = std::time(nullptr);
    switch (state) {
        case ConnectionState::Checking:
            c.lastTry = now;
            break;
        case ConnectionState::Connected:
            c.lastOk = now;
            c.failures = 0;
            c.reason.clear();
            break;
        case ConnectionState::Unreachable:
        case ConnectionState::Failed:
            ++c.failures;
            c.reason = reason;
            break;
        case ConnectionState::Unknown:
            break;
    }
    c.state = state;
    if (accountId == selectedAccount_) UpdateConnectionIndicator();
}

namespace {
std::string ClockTime(std::time_t t) {
    if (t == 0) return "not yet";
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M", &local);
    return buf;
}
} // namespace

void UltraMailApp::UpdateConnectionIndicator() {
    if (!connectionBadge_) return;
    const Account* account = nullptr;
    for (const auto& a : accounts_) if (a.accountId == selectedAccount_) account = &a;
    if (!account) {
        connectionBadge_->SetVisible(false);
        return;
    }
    connectionBadge_->SetVisible(true);
    auto it = connection_.find(account->accountId);
    const ConnectionInfo info = it != connection_.end() ? it->second : ConnectionInfo{};

    // The pill: one word and a colour.
    std::string label, stateText;
    BadgeVariant variant = BadgeVariant::Neutral;
    switch (info.state) {
        case ConnectionState::Unknown:
            label = "Not checked"; variant = BadgeVariant::Neutral;
            stateText = "Not contacted yet in this session";
            break;
        case ConnectionState::Checking:
            label = "Checking…"; variant = BadgeVariant::Primary;
            stateText = "Contacting the server";
            break;
        case ConnectionState::Connected:
            label = "Connected"; variant = BadgeVariant::Successful;
            stateText = "The server answered and the mailbox was read";
            break;
        case ConnectionState::Unreachable:
            label = "Offline"; variant = BadgeVariant::Warning;
            stateText = "The server could not be reached";
            break;
        case ConnectionState::Failed:
            label = "Failed"; variant = BadgeVariant::Danger;
            stateText = "The server answered but refused the request";
            break;
    }
    connectionBadge_->SetText(label);
    connectionBadge_->SetVariant(variant);

    // The tooltip: where, when, and why not.
    const DiscoveryResult settings = SettingsFor(*account);
    std::string server = settings.found ? AutoDiscovery::ImapServerUrl(settings.imap) : "";
    if (server.empty()) server = "no server known";
    TooltipContent tip;
    tip.AddTitle("Mail server connection")
       .AddRow("Account", account->email)
       .AddRow("Server", server)
       .AddRow("State", stateText)
       .AddRow("Last contact", ClockTime(info.lastOk))
       .AddRow("Last attempt", ClockTime(info.lastTry));
    if (!info.reason.empty()) tip.AddRow("Reason", info.reason);
    if (info.failures > 1) tip.AddRow("Failed attempts", std::to_string(info.failures));
    tip.AddSeparator();
    switch (info.state) {
        case ConnectionState::Unreachable:
            tip.AddText(offline_.InGrace(account->accountId, NowMonotonicSec())
                        ? "Probably the network is not up yet. UltraMail tries again "
                          "every minute and alerts after ten minutes offline."
                        : "Check the network connection, or the server name and "
                          "port on the account's settings page.");
            break;
        case ConnectionState::Failed:
            tip.AddText("Update tries again; the account's settings page lets you "
                        "correct the sign-in.");
            break;
        case ConnectionState::Unknown:
            tip.AddText("Update contacts the server now.");
            break;
        default:
            tip.AddText("Mail is checked every five minutes; Update checks now.");
            break;
    }
    connectionBadge_->SetTooltipContent(tip);
}

void UltraMailApp::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text.empty() ? "Ready" : text);
    UpdateBusyIndicator();
}

void UltraMailApp::ShowMessageLinks(const std::vector<MessageLink>& links) {
    if (!linksLabel_) return;
    // Sites by registrable domain, most links first.
    std::map<std::string, int> perSite;
    for (const auto& link : links) {
        if (link.host.empty()) continue;
        ++perSite[RegistrableDomain(link.host)];
    }
    std::vector<std::pair<std::string, int>> sites(perSite.begin(), perSite.end());
    std::stable_sort(sites.begin(), sites.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    if (links.empty()) {
        linksSummary_ = "No links";
    } else {
        const std::size_t n = links.size();
        linksSummary_ = std::to_string(n) + (n == 1 ? " link" : " links") + " \xE2\x86\x92 ";
        if (sites.empty()) linksSummary_ += "no web site";
        for (std::size_t i = 0; i < sites.size() && i < 3; ++i) {
            if (i) linksSummary_ += ", ";
            linksSummary_ += sites[i].first;
        }
        if (sites.size() > 3) linksSummary_ += " +" + std::to_string(sites.size() - 3);
    }
    linksLabel_->SetText(linksSummary_);
    // Every link: what it shows, and where it really goes.
    std::string list;
    const std::size_t shown = std::min<std::size_t>(links.size(), 40);
    for (std::size_t i = 0; i < shown; ++i) {
        std::string text = links[i].text;
        for (char& c : text) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (text.size() > 50) text = text.substr(0, 47) + "...";
        std::string href = links[i].href;
        if (href.size() > 90) href = href.substr(0, 87) + "...";
        if (!list.empty()) list += "\n";
        list += (text.empty() ? std::string("(no text)") : "\"" + text + "\"") + "  \xE2\x86\x92  " + href;
    }
    if (links.size() > shown) list += "\n... and " + std::to_string(links.size() - shown) + " more";
    linksLabel_->SetTooltip(list);
}

void UltraMailApp::ApplyLinkDisplay() {
    const bool tooltip = prefs_.linkDisplay == LinkDisplay::Tooltip;
    mailView_.SetLinkTooltips(tooltip);
    if (!linksLabel_) return;
    linksLabel_->SetText(linksSummary_);
    linksLabel_->SetVisible(!tooltip);
    linksLabel_->RequestRedraw();
}

void UltraMailApp::ShowHoveredLink(const std::string& href) {
    if (!linksLabel_ || prefs_.linkDisplay == LinkDisplay::Tooltip) return;
    linksLabel_->SetText(href.empty() ? linksSummary_ : "\xE2\x86\x92 " + href);
}

void UltraMailApp::UpdateBusyIndicator() {
    if (!busyIndicator_) return;
    busyIndicator_->SetRunning(syncsInFlight_ > 0 || outboxFlushInFlight_ ||
                               mailboxActionsInFlight_ > 0);
}

void UltraMailApp::RefreshAccountCounts() {
    store_.GetAccountStatus(status_);
    accountBar_.Rebuild(accounts_, status_, selectedAccount_);
    PublishUnreadNotice();
}

bool UltraMailApp::ApplyNeedsAnswerRules() {
    NeedsAnswerRules rules;
    rules.maxAgeDays    = prefs_.needsAnswerMaxAgeDays;
    rules.onlyWrittenTo = prefs_.needsAnswerOnlyWrittenTo;
    const NeedsAnswerRules& current = store_.GetNeedsAnswerRules();
    if (current.maxAgeDays == rules.maxAgeDays && current.onlyWrittenTo == rules.onlyWrittenTo)
        return false;
    store_.SetNeedsAnswerRules(rules);
    return true;
}

void UltraMailApp::ShowAccountStatus() {
    // While something is still running its own progress text stays.
    if (syncsInFlight_ > 0 || outboxFlushInFlight_) return;
    auto it = accountError_.find(selectedAccount_);
    SetStatus(it != accountError_.end() ? it->second : std::string("Up to date"));
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
        if (!folder.empty() && folder != "INBOX")
            SyncFolder(target, folder, /*userInitiated=*/true);
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
            const std::string ext = PathToUtf8(entry.path().extension());
            if (ext == ".so" || ext == ".dll" || ext == ".dylib") return true;
        }
        return false;
    };

    const fs::path relative = PathFromUtf8("Plugins") / "UltraNet";
    std::vector<fs::path> candidates;
    const std::string exeDir = UltraCanvas::GetExecutableDir();
    if (!exeDir.empty()) {
        // The build tree puts the executable at <build>/ and the DSOs at
        // <build>/Plugins/UltraNet; an installed or copied app may sit one
        // or two levels deeper (bin/, Apps/UltraMail/).
        fs::path base = UltraCanvas::PathFromUtf8(exeDir);
        for (int up = 0; up < 3; ++up) {
            candidates.push_back(base / relative);
            base = base.parent_path();
        }
    }
    candidates.push_back(relative);   // the registry's own default (cwd)

    for (const auto& c : candidates)
        if (holdsPlugin(c)) return PathToUtf8(c.lexically_normal());
    // Nothing found: keep the first executable-relative path so the diagnostic
    // names a concrete place to put the DSOs.
    return PathToUtf8(candidates.front().lexically_normal());
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

Draft UltraMailApp::WithSignature(Draft draft, DraftPurpose purpose) const {
    const std::string from = ToLowerCase(draft.fromAddr);
    for (const Account& account : accounts_) {
        if (ToLowerCase(account.email) != from) continue;
        ApplySignature(draft, account.signature, purpose);
        break;
    }
    return draft;
}

void UltraMailApp::SaveSignature(const std::string& accountId, const Signature& signature) {
    if (UltraDbResult up = store_.SetAccountSignature(accountId, signature); !up) {
        AlertError(window_ ? window_.get() : nullptr,
                   "The signature could not be saved.", DetailLine(up));
        return;
    }
    for (Account& account : accounts_)
        if (account.accountId == accountId) account.signature = signature;
}

ComposeView* UltraMailApp::OpenComposer(const Draft& draft, int64_t replacesOutboxId) {
    WindowConfig cfg;
    cfg.title  = draft.subject.empty() ? "New message" : draft.subject;
    cfg.width  = 640;
    cfg.height = 560;   // two toolbar rows above the body
    cfg.backgroundColor = Theme::kCardBackground;
    auto win = CreateWindow(cfg);

    // Its own view: the buttons, the toolbar and Send act on this window's
    // message, whatever other compose windows are open.
    auto view = std::make_shared<ComposeView>();
    view->SetDraft(draft);
    view->SetParentWindow(win.get());
    view->SetCloud(cloud_.get());
    UltraCanvasWindow* raw = win.get();
    // Sent, or safely queued in the outbox: the window has done its job. It
    // stays open when the message was not queued (no recipient, no outbox),
    // so nothing typed is lost. The send's own result (sent / waiting in the
    // outbox / failed) is reported over the main window.
    view->onSend   = [this, raw, replacesOutboxId](const Draft& d) {
        if (!HandleSendDraft(d, replacesOutboxId)) return;
        // The old version is being deleted now: closing must not let it go.
        for (auto& s : composers_) if (s.window.get() == raw) s.editsOutboxId = 0;
        raw->Close();
    };
    view->onCancel = [raw]() { raw->Close(); };
    win->AddChild(view->Build());
    view->Resize(static_cast<float>(cfg.width), static_cast<float>(cfg.height));
    // Raw: the window owns these callbacks, and the view lives as long as the
    // window's entry in composers_.
    ComposeView* rawView = view.get();
    win->onWindowResize = [rawView](int cw, int ch) {
        rawView->Resize(static_cast<float>(cw), static_cast<float>(ch));
    };
    win->onWindowClosed = [this, raw]() { RetireComposer(raw); };
    win->Show();
    composers_.push_back({win, view, replacesOutboxId});
    return rawView;
}

void UltraMailApp::RetireComposer(UltraCanvasWindow* window) {
    // Closed without sending the correction: the waiting message it was made
    // from is sent as it is after all.
    for (auto& s : composers_) {
        if (s.window.get() != window || s.editsOutboxId == 0) continue;
        outbox_.SetHeld(s.editsOutboxId, false);
        s.editsOutboxId = 0;
        RefreshOutbox();
    }
    // Deferred: dropping the last reference to a window from inside its own
    // close callback would destroy it while it is still running.
    auto drop = [this, window]() {
        composers_.erase(std::remove_if(composers_.begin(), composers_.end(),
                                        [window](const ComposeSession& s) {
                                            return s.window.get() == window;
                                        }),
                         composers_.end());
    };
    if (auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(drop);
    else drop();
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
    const std::string& actionName, std::function<void()> onSuccess) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    IMailboxProtocolPlugin* imap = ImapPlugin();
    if (!imap) { ReportMissingImapPlugin(); return; }

    // The IMAP action needs the account password / OAuth token, so unlock first
    // (silently with the device key, or a single prompt for an old vault).
    EnsureVaultUnlocked([this, accountId, op, actionName, parent, imap, onSuccess]() {
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
        ApplyConnection(settings.imap, opts);
        const std::string email    = account->email;
        const std::string username = settings.imap.username.empty() ? email
                                                                    : settings.imap.username;
        const std::string provider = OAuthProviderFor(settings);
        opts.credentials.username  = username;

        // The server op + local-store update run off the UI thread (a credential
        // refresh can make an HTTPS request); the result is marshalled back.
        ++mailboxActionsInFlight_;
        UpdateBusyIndicator();
        std::thread([this, accountId, serverUrl, opts, op, actionName,
                     username, provider, imap, onSuccess]() mutable {
            UltraNetResult cred = ResolveCredentials(accountId, username, provider,
                                                     opts.credentials);
            SyncOutcome outcome;
            // Default Success: only a credential failure carries an auth code the
            // UI thread can turn into a re-sign-in offer; an IMAP-op failure keeps
            // Success here and falls through to the plain error alert.
            UltraNetResultCode credCode = UltraNetResultCode::Success;
            if (!cred) {
                outcome = SyncOutcome::Fail(cred);
                credCode = cred.code;
            } else {
                SyncEngine engine(workerStore_, *imap, mailDir_);
                outcome = op(engine, serverUrl, opts);
            }
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) return;
            app->PostToUIThread([this, accountId, provider, outcome, actionName, credCode, op,
                                 onSuccess]() {
                --mailboxActionsInFlight_;
                UpdateBusyIndicator();
                if (!outcome) {
                    // A dead OAuth sign-in offers Retry → re-sign-in → re-run the
                    // action. Anything else is the usual dead-end error.
                    if (!MaybeOfferReauth(accountId, credCode, provider,
                            [this, accountId, op, actionName, onSuccess]() {
                                RunMailboxAction(accountId, op, actionName, onSuccess);
                            }))
                        AlertError(window_ ? window_.get() : nullptr,
                                   actionName + " could not be completed.",
                                   WithDiagnostics(outcome.message, outcome.diagnostics));
                    return;
                }
                Refresh();
                if (onSuccess) onSuccess();
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
    ApplyConnection(settings.imap, opts);
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
        SyncEngine engine(workerStore_, *imap, mailDir_);
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
    RefreshAccountCounts();   // the account's unread number goes down with it
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
            // drop the local row and its cached body so it leaves the list.
            SyncOutcome o = engine.SetFlag(env.accountId, env.folder, env.uid,
                                           Flag_Deleted, true, url, opts);
            if (o) engine.ForgetMessage(env.accountId, env.folder, env.uid);
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

void UltraMailApp::HandleMoveMessage(const MessageEnvelope& env, const std::string& folder) {
    if (folder.empty() || folder == env.folder) return;
    RunMailboxAction(env.accountId,
        [env, folder](SyncEngine& engine, const std::string& url, const UltraNetMailOptions& opts) {
            return engine.MoveMessage(env.accountId, env.folder, env.uid, folder, url, opts);
        },
        "Move to " + FriendlyFolderName(folder));
}

void UltraMailApp::HandleNotJunk(const MessageEnvelope& env) {
    std::string inbox = FolderWithRole(env.accountId, FolderRole::Inbox);
    if (inbox.empty()) inbox = "INBOX";
    RunMailboxAction(env.accountId,
        [env, inbox](SyncEngine& engine, const std::string& url, const UltraNetMailOptions& opts) {
            return engine.MoveMessage(env.accountId, env.folder, env.uid, inbox, url, opts);
        },
        "Not spam");
}

void UltraMailApp::HandleSetNeedsAnswer(const MessageEnvelope& env, bool needsAnswer) {
    // Local only: IMAP has no standard flag for it (see LocalStore::SetNeedsAnswer).
    if (UltraDbResult r = store_.SetNeedsAnswer(env.accountId, env.folder, env.uid, needsAnswer);
        !r) {
        AlertError(window_ ? window_.get() : nullptr,
                   "The message could not be marked.", r.message);
        return;
    }
    Refresh();
}

void UltraMailApp::HandleUnsubscribe(const MessageEnvelope& env) {
    // The List-Unsubscribe header is in the message itself: use the cached
    // body, or download it first.
    const std::string path = CachedBodyPath(mailDir_, env.accountId, env.folder, env.uid);
    std::error_code ec;
    if (std::filesystem::exists(PathFromUtf8(path), ec)) {
        auto loaded = UltraCanvas::UltraCanvasFileLoader::LoadFile(path);
        if (loaded.success) {
            UnsubscribeWith(env, std::string(loaded.bytes.begin(), loaded.bytes.end()));
            return;
        }
    }
    RunMailboxAction(env.accountId,
        [env](SyncEngine& engine, const std::string& url, const UltraNetMailOptions& opts) {
            if (engine.FetchBody(env.accountId, env.folder, env.uid, url, opts).empty())
                return SyncOutcome::Fail(std::string("the message could not be downloaded"));
            return SyncOutcome{};
        },
        "Unsubscribe",
        [this, env, path]() {
            auto loaded = UltraCanvas::UltraCanvasFileLoader::LoadFile(path);
            UnsubscribeWith(env, loaded.success
                ? std::string(loaded.bytes.begin(), loaded.bytes.end()) : std::string());
        });
}

void UltraMailApp::UnsubscribeWith(const MessageEnvelope& env, const std::string& raw) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    const UnsubscribeInfo info = ReadUnsubscribe(raw);
    const std::string sender = env.fromName.empty() ? env.fromAddr
                                                    : DisplayHeader(env.fromName);
    if (!info.Any()) {
        AlertWarning(parent, "This message offers no way to unsubscribe.",
                     "It has no List-Unsubscribe header, so UltraMail cannot leave the "
                     "list for you. Look for an unsubscribe link in the message, or use "
                     "Mark as spam.");
        return;
    }

    // Unsubscribing tells the sender the address is read. For a list you
    // signed up to that is the point; for spam it invites more.
    const bool inJunk = FolderWithRole(env.accountId, FolderRole::Junk) == env.folder;
    std::string how;
    if (!info.oneClickUrl.empty())
        how = "UltraMail will ask the sender's server to remove you from the list "
              "(no browser needed).";
    else if (!info.webUrl.empty())
        how = "The unsubscribe page will open in your browser.";
    else
        how = "A message to " + info.mailtoAddress + " will be prepared for you to send.";
    std::string question = "Unsubscribe from mail sent by " + sender + "?\n\n" + how;
    if (inJunk)
        question += "\n\nThis message is in your spam folder. Unsubscribing from real "
                    "spam confirms to the sender that your address is read; leaving it "
                    "in spam is safer.";

    UltraCanvasAlert::Confirm(question, "Unsubscribe",
        [this, env, info, sender](bool yes) {
            if (!yes) return;
            if (!info.oneClickUrl.empty()) {
                // RFC 8058 one-click: a POST with this exact body, off the UI thread.
                ++mailboxActionsInFlight_;
                SetStatus("Unsubscribing from " + sender + "…");
                std::thread([this, info, sender]() {
                    UltraNetHttpOptions options;
                    options.headers.Set("Content-Type", "application/x-www-form-urlencoded");
                    const std::string form = "List-Unsubscribe=One-Click";
                    UltraNetResponse response;
                    UltraNetResult r = UltraNet_HttpPost(
                        info.oneClickUrl, std::vector<uint8_t>(form.begin(), form.end()),
                        response, options);
                    const bool ok = r && response.statusCode >= 200 && response.statusCode < 300;
                    const std::string why = !r ? r.message
                        : "The server answered " + std::to_string(response.statusCode) + " "
                          + response.statusMessage + ".";
                    auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
                    if (!app) return;
                    app->PostToUIThread([this, info, sender, ok, why]() {
                        --mailboxActionsInFlight_;
                        UpdateBusyIndicator();
                        ShowAccountStatus();
                        UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
                        if (ok) {
                            AlertSuccess(parent, "Unsubscribed from " + sender + ".",
                                         "The list may take a few days to stop sending.");
                        } else if (!info.webUrl.empty()) {
                            AlertError(parent, "The one-click unsubscribe did not work.",
                                       why + "\nThe unsubscribe page opens in your browser instead.",
                                       [url = info.webUrl]() { UltraCanvas::OpenURL(url); });
                        } else {
                            AlertError(parent, "The one-click unsubscribe did not work.", why);
                        }
                    });
                }).detach();
            } else if (!info.webUrl.empty()) {
                UltraCanvas::OpenURL(info.webUrl);
            } else {
                // A message to the list's unsubscribe address, from this account.
                std::string name, addr;
                for (const auto& a : accounts_)
                    if (a.accountId == env.accountId) { name = a.displayName; addr = a.email; }
                Draft d = Composer::NewMessage(name, addr);
                d.to = { info.mailtoAddress };
                d.subject = info.mailtoSubject.empty() ? std::string("unsubscribe")
                                                       : info.mailtoSubject;
                d.body = info.mailtoBody;
                OpenComposer(d);
            }
        },
        parent);
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
    // Coloured as HTML: tags, attributes and values stand out in an HTML
    // mail's source, and the headers above it stay plain text.
    text->SetHighlightSyntax(true);
    text->SetProgrammingLanguage("HTML");
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

bool UltraMailApp::HandleSendDraft(const Draft& draft, int64_t replacesOutboxId) {
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
        return false;
    }
    if (!outbox_.IsOpen()) {
        AlertError(parent, "The message could not be queued for sending.",
                   outboxError_.empty()
                       ? "UltraMail's outbox database is not available."
                       : outboxError_);
        return false;
    }

    DiscoveryResult disc = SettingsForEmail(draft.fromAddr);
    const std::string smtpUrl = disc.found ? AutoDiscovery::SmtpServerUrl(disc.smtp) : "";

    // Always queue to the persistent outbox first (survives restarts / offline).
    int64_t id = 0;
    // Filed under the account that owns the From address (its sign-in and
    // servers send it and keep its Drafts copy); the slug for an address no
    // account has.
    std::string accountId = SlugFromEmail(draft.fromAddr);
    for (const auto& a : accounts_)
        if (ToLowerCase(a.email) == ToLowerCase(draft.fromAddr)) { accountId = a.accountId; break; }
    if (UltraDbResult q = outbox_.Enqueue(accountId, smtpUrl, draft, id); !q) {
        AlertError(parent, "The message could not be queued for sending, so it "
                           "has not been saved.",
                   DetailLine(q));
        return false;
    }

    // A corrected message replaces the one it was made from: that one goes
    // (with its Drafts copy) before the pass below can pick it up - it is
    // held until then.
    if (replacesOutboxId != 0) DeleteFromOutbox(replacesOutboxId, /*quiet=*/true);
    RefreshOutbox();

    // Remember the people we write to.
    for (const auto& addr : draft.to) ContactCollector::Collect(contacts_, "", addr);
    for (const auto& addr : draft.cc) ContactCollector::Collect(contacts_, "", addr);

    // Sending, and the copy in Drafts, need the account's sign-in: open the
    // vault first. The message is already safe in the outbox; a cancelled
    // unlock leaves it there.
    const std::string from = draft.fromAddr;
    const std::string recipients =
        join(!draft.to.empty() ? draft.to : !draft.cc.empty() ? draft.cc : draft.bcc);
    EnsureVaultUnlocked([this, from, recipients]() { SendQueued(from, recipients); });
    return true;
}

void UltraMailApp::SendQueued(const std::string& fromAddr, const std::string& recipients) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    DiscoveryResult disc = SettingsForEmail(fromAddr);
    auto plugin = UltraNet_GetPlugin(disc.smtp.security == MailSecurity::SslTls ? "smtps" : "smtp");
    auto* smtp = plugin ? dynamic_cast<IMailProtocolPlugin*>(plugin.get()) : nullptr;

    UltraNetResult cannotSend = UltraNetResult::Ok();
    if (!smtp)
        cannotSend = UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme,
            "The SMTP plug-in is not loaded, so UltraMail cannot reach a mail server. "
            "Put it on the plug-in path (" + pluginDir_ + ") and choose Retry.");
    else if (!disc.found || AutoDiscovery::SmtpServerUrl(disc.smtp).empty())
        cannotSend = UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "No outgoing (SMTP) server is known for " + fromAddr
            + ". Enter it in Account Settings and choose Retry.");
    if (!cannotSend) {
        // Nothing can be sent now: keep the copy in Drafts all the same.
        FlushOutboxInBackground(nullptr,
            [this, fromAddr, recipients, cannotSend](const Outbox::FlushStats& stats) {
                NoteOutboxPass();
                ReportNotSent(fromAddr, recipients, cannotSend, stats);
            });
        return;
    }
    FlushOutboxInBackground(plugin,
        [this, parent, fromAddr, recipients](const Outbox::FlushStats& stats) {
            NoteOutboxPass();
            if (stats.failed == 0) {
                AlertSuccess(parent, recipients.empty()
                    ? "The outbox was sent (" + std::to_string(stats.sent) + " message"
                          + (stats.sent == 1 ? "" : "s") + ")."
                    : "Message sent to " + recipients + ".");
                return;
            }
            ReportNotSent(fromAddr, recipients, stats.lastFailure, stats);
        });
}

void UltraMailApp::ReportNotSent(const std::string& fromAddr, const std::string& recipients,
                                 const UltraNetResult& why, const Outbox::FlushStats& stats) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    // Where the waiting messages are now: each has a copy in Drafts, or only
    // the outbox holds them.
    std::vector<OutboxItem> pending;
    outbox_.ListPending(pending);
    // A message the user is deleting (queued behind this pass) is not one
    // to warn about; with nothing else left, there is nothing to say.
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                 [this](const OutboxItem& item) {
                                     return outboxDeleting_.count(item.id) != 0;
                                 }),
                  pending.end());
    if (pending.empty()) return;
    bool allInDrafts = !pending.empty();
    for (const auto& item : pending) if (!item.HasDraftCopy()) allInDrafts = false;

    // A reason as a sentence: capital first letter, a full stop at the end.
    auto sentence = [](std::string text) {
        if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        if (!text.empty() && text.back() != '.' && text.back() != '!' && text.back() != '?')
            text += '.';
        return text;
    };
    std::string where = allInDrafts
        ? "It is kept in your Drafts folder and in UltraMail's outbox until it has been sent."
        : "It is kept in UltraMail's outbox until it has been sent.";
    if (!allInDrafts && stats.draftFailures > 0)
        where += " It could not be saved to the Drafts folder: "
               + sentence(FriendlyMessage(stats.lastDraftFailure));
    const std::string detail = WithDiagnostics(
        sentence(FriendlyMessage(why)) + "\n\n" + where
            + "\n\nUltraMail tries again by itself - in a minute, then less often, and "
              "as soon as the connection is back. Choose Retry to send it now.",
        why.diagnostics);
    AlertWarningRetry(parent,
        "The message" + (recipients.empty() ? std::string() : " to " + recipients)
            + " could not be sent yet.",
        detail,
        [this, fromAddr, recipients]() { RetryOutbox(fromAddr, recipients); });
}

void UltraMailApp::RetryOutbox(const std::string& fromAddr, const std::string& recipients) {
    EnsureVaultUnlocked([this, fromAddr, recipients]() { SendQueued(fromAddr, recipients); });
}

int UltraMailApp::OutboxPending() const {
    int n = 0;
    if (outbox_.IsOpen()) outbox_.PendingCount(n);
    return n;
}

int UltraMailApp::OutboxWithdrawn() const {
    int n = 0;
    if (outbox_.IsOpen()) outbox_.WithdrawnCount(n);
    return n;
}

void UltraMailApp::NoteOutboxPass() {
    // A message held for correcting is not one the pass failed to send; a
    // deleted message's Drafts copy still on the server is work left over.
    if (OutboxPending() - outbox_.HeldCount() <= 0 && OutboxWithdrawn() == 0)
        outboxRetry_.Succeeded();
    else
        outboxRetry_.Failed(NowMonotonicSec());
}

void UltraMailApp::StartOutboxRetryTimer() {
    if (outboxRetryTimerStarted_) return;
    auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
    if (!app) return;
    outboxRetryTimerStarted_ = true;
    app->StartTimer(30000, /*periodic=*/true, [this](UltraCanvas::TimerId) {
        if (outboxRetry_.Due(NowMonotonicSec())) AutoRetryOutbox();
    });
}

void UltraMailApp::AutoRetryOutbox() {
    // A pass the user started is running, or the vault is locked: an
    // automatic pass never prompts for a password. It stays due and runs on
    // a later tick.
    if (outboxFlushInFlight_ || !vault_.IsUnlocked()) return;
    std::vector<OutboxItem> pending;
    outbox_.ListPending(pending);
    if (pending.empty()) {
        if (OutboxWithdrawn() == 0) { outboxRetry_.Succeeded(); return; }
        // Only Drafts copies of deleted messages to remove: no send.
        FlushOutboxInBackground(nullptr, [this](const Outbox::FlushStats&) { NoteOutboxPass(); });
        return;
    }

    const std::string from = pending.front().draft.fromAddr;
    DiscoveryResult disc = SettingsForEmail(from);
    auto plugin = UltraNet_GetPlugin(disc.smtp.security == MailSecurity::SslTls ? "smtps" : "smtp");
    const bool canSend = plugin && dynamic_cast<IMailProtocolPlugin*>(plugin.get())
                      && disc.found && !AutoDiscovery::SmtpServerUrl(disc.smtp).empty();
    // Silent: the warning was shown when the message first failed. A pass
    // that cannot send still saves the Drafts copies.
    FlushOutboxInBackground(canSend ? plugin : nullptr,
        [this](const Outbox::FlushStats& stats) {
            NoteOutboxPass();
            if (stats.sent > 0)
                SetStatus(stats.sent == 1
                    ? std::string("A waiting message was sent.")
                    : std::to_string(stats.sent) + " waiting messages were sent.");
        });
}

ServerCopies UltraMailApp::MakeServerCopies() {
    ServerCopies copies;
    copies.imap = ImapPlugin();
    if (!copies.imap) return copies;
    // Copied here, on the UI thread: the worker must not read accounts_.
    struct Target {
        DiscoveryResult settings;
        std::string     email;
        std::string     draftsFolder;
        std::string     sentFolder;
    };
    auto targets = std::make_shared<std::map<std::string, Target>>();
    for (const auto& a : accounts_) {
        Target t;
        t.settings = SettingsFor(a);
        t.email = a.email;
        // The folders the server marks as Drafts and Sent (known once the
        // folders were synced), else the usual names.
        t.draftsFolder = FolderWithRole(a.accountId, FolderRole::Drafts);
        if (t.draftsFolder.empty()) t.draftsFolder = "Drafts";
        // No Sent copy where the server files sent mail itself (a second one).
        if (!ServerFilesSentMail(t.settings.imap.host)) {
            t.sentFolder = FolderWithRole(a.accountId, FolderRole::Sent);
            if (t.sentFolder.empty()) t.sentFolder = "Sent";
        }
        (*targets)[a.accountId] = std::move(t);
    }
    copies.prepare = [this, targets](const std::string& accountId, ServerFolders& out) {
        auto it = targets->find(accountId);
        if (it == targets->end())
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "the account of this message no longer exists");
        const Target& t = it->second;
        out.serverUrl = t.settings.found ? AutoDiscovery::ImapServerUrl(t.settings.imap) : "";
        if (out.serverUrl.empty())
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "no incoming (IMAP) server is known for " + t.email);
        out.draftsFolder = t.draftsFolder;
        out.sentFolder = t.sentFolder;
        ApplyConnection(t.settings.imap, out.options);
        const std::string username =
            t.settings.imap.username.empty() ? t.email : t.settings.imap.username;
        out.options.credentials.username = username;
        return ResolveCredentials(accountId, username, OAuthProviderFor(t.settings),
                                  out.options.credentials);
    };
    return copies;
}

void UltraMailApp::FlushOutboxInBackground(std::shared_ptr<IUltraNetPlugin> plugin,
                                           std::function<void(const Outbox::FlushStats&)> onDone) {
    auto* smtp = plugin ? dynamic_cast<IMailProtocolPlugin*>(plugin.get()) : nullptr;
    // Copied here, on the UI thread: the worker must not read accounts_.
    auto accounts = std::make_shared<std::map<std::string, SmtpAccount>>(SmtpAccounts());
    auto stats = std::make_shared<Outbox::FlushStats>();
    RunOutboxJob(
        // `plugin` rides along: it keeps the SMTP plug-in loaded while it sends.
        [this, plugin, smtp, accounts, stats](Outbox& ob, const ServerCopies* copies) {
            if (smtp) {
                *stats = ob.Flush(*smtp,
                    [this, accounts](const std::string& acc, UltraNetMailOptions& o) {
                        return PrepareSmtp(*accounts, acc, o);
                    },
                    copies);
            } else if (copies) {
                *stats = ob.SaveDraftCopies(*copies);
            }
        },
        smtp ? "Sending…" : "Saving to Drafts…",
        [stats, onDone]() { if (onDone) onDone(*stats); });
}

void UltraMailApp::RunOutboxJob(OutboxJob job, const std::string& status,
                                std::function<void()> onDone) {
    if (outboxFlushInFlight_) {
        // One job at a time: two passes would pick up the same queued message
        // and send it twice. This one runs when the current one ends.
        pendingOutboxJobs_.push_back({std::move(job), status, std::move(onDone)});
        return;
    }
    outboxFlushInFlight_ = true;
    SetStatus(status);
    RefreshOutbox();   // the Outbox window says it is busy
    // Copied here, on the UI thread: the worker must not read accounts_.
    auto copies = std::make_shared<ServerCopies>(MakeServerCopies());
    std::thread([this, job = std::move(job), copies, onDone = std::move(onDone)]() {
        Outbox ob(outbox_);
        job(ob, copies->imap ? copies.get() : nullptr);
        auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, onDone]() {
            outboxFlushInFlight_ = false;
            UpdateBusyIndicator();
            ShowAccountStatus();
            if (onDone) onDone();
            RefreshOutbox();
            if (!pendingOutboxJobs_.empty()) {
                PendingOutboxJob next = std::move(pendingOutboxJobs_.front());
                pendingOutboxJobs_.erase(pendingOutboxJobs_.begin());
                RunOutboxJob(std::move(next.job), next.status, std::move(next.onDone));
                return;
            }
            // The queue is empty: what waited for it (Edit) runs now.
            std::vector<std::function<void()>> waiting;
            waiting.swap(whenOutboxIdle_);
            if (outboxWindow_) outboxView_.SetNote("");
            for (auto& action : waiting) action();
        });
    }).detach();
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

        fs::path p = PathFromUtf8(mailDir_) / "erika" / "INBOX" / (std::to_string(uid) + ".eml");
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
        // The five-minute timer cannot tell a wake from sleep: after one it may
        // be minutes before it fires. This short one can (see WakeDetector).
        wake_.Tick(static_cast<int64_t>(std::time(nullptr)));   // start its clock
        app->StartTimer(static_cast<unsigned int>(wake_.TickSec() * 1000), /*periodic=*/true,
                        [this](UltraCanvas::TimerId) {
                            if (wake_.Tick(static_cast<int64_t>(std::time(nullptr))))
                                OnWokeFromSleep();
                        });
    }
}

void UltraMailApp::OnWokeFromSleep() {
    if (accounts_.empty() || wakeCheckPending_) return;
    // Whatever was unreachable before the sleep is a fresh question now: a
    // first failure after the wake is held back like one right after boot.
    offline_.Clear();
    // Waiting messages too: the connection is probably back after the wake.
    if (OutboxPending() > 0)
        outboxRetry_.RetryAt(NowMonotonicSec() + kWakeSyncDelayMs / 1000 + 5);
    auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
    if (!app) return;
    wakeCheckPending_ = true;
    app->StartTimer(kWakeSyncDelayMs, /*periodic=*/false, [this](UltraCanvas::TimerId) {
        wakeCheckPending_ = false;
        SyncAllInBackground();
    });
}

void UltraMailApp::SyncAllInBackground() {
    std::vector<ScheduledAccount> targets;
    for (const auto& a : accounts_) {
        DiscoveryResult d = SettingsFor(a);
        ScheduledAccount sa;
        sa.accountId = a.accountId;
        sa.serverUrl = d.found ? AutoDiscovery::ImapServerUrl(d.imap) : "";
        targets.push_back(sa);
    }
    SyncAccounts(targets, /*userInitiated=*/false);
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

void UltraMailApp::ScheduleOfflineRetry() {
    // One pending retry at a time; it fires only for the accounts whose grace
    // period is still running, and re-arms itself from their next failure —
    // never from a success, and never past the grace period, after which the
    // regular cadence takes over.
    if (offlineRetryPending_) return;
    auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
    if (!app) return;
    offlineRetryPending_ = true;
    app->StartTimer(static_cast<unsigned int>(kOfflineRetrySec * 1000), /*periodic=*/false,
                    [this](UltraCanvas::TimerId) {
                        offlineRetryPending_ = false;
                        RetryUnreachableAccounts();
                    });
}

void UltraMailApp::RetryUnreachableAccounts() {
    const std::vector<std::string> ids = offline_.AccountsInGrace(NowMonotonicSec());
    if (ids.empty()) return;
    std::vector<ScheduledAccount> targets;
    for (const auto& a : accounts_) {
        if (std::find(ids.begin(), ids.end(), a.accountId) == ids.end()) continue;
        DiscoveryResult d = SettingsFor(a);
        ScheduledAccount sa;
        sa.accountId = a.accountId;
        sa.serverUrl = d.found ? AutoDiscovery::ImapServerUrl(d.imap) : "";
        targets.push_back(sa);
    }
    SyncAccounts(targets, /*userInitiated=*/false);
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

void UltraMailApp::SyncFolder(const std::string& accountId, const std::string& folder,
                              bool userInitiated) {
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
    ApplyConnection(settings.imap, opts);
    const std::string email    = account->email;
    const std::string who      = email.empty() ? accountId : email;
    const std::string username = settings.imap.username.empty() ? email : settings.imap.username;
    const std::string provider = OAuthProviderFor(settings);
    opts.credentials.username  = username;

    auto svc = std::make_shared<SyncService>(workerStore_, *imap, mailDir_);
    if (++syncsInFlight_ == 1 && reloadButton_) reloadButton_->SetText("Updating…");
    SetStatus("Opening " + FriendlyFolderName(folder) + "…");
    NoteConnection(accountId, ConnectionState::Checking);
    auto progressBuf = std::make_shared<std::vector<MessageEnvelope>>();
    svc->SyncFolderInBackground(accountId, folder, serverUrl, opts,
        [this, accountId, username, provider](UltraNetMailOptions& o) {
            return ResolveCredentials(accountId, username, provider, o.credentials);
        },
        [this, svc, accountId, folder, who, userInitiated](SyncOutcome outcome) {
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) return;
            app->PostToUIThread([this, accountId, folder, who, userInitiated, outcome]() {
                // Clear the in-flight guard first — even on failure — so the next
                // open can retry once the throttle window passes. (Harmless no-op
                // for callers that never set it, e.g. HandleReload.)
                folderSyncInFlight_.erase(accountId + "\n" + folder);
                const bool last = (--syncsInFlight_ <= 0);
                if (last) {
                    syncsInFlight_ = 0;
                    statusReceived_ = 0;
                    if (reloadButton_) reloadButton_->SetText("Update");
                    UpdateBusyIndicator();
                }
                if (!outcome) {
                    accountError_[accountId] = "Could not fetch mail for " + who + ": " + outcome.message;
                    NoteConnection(accountId, outcome.NetworkUnreachable()
                                                  ? ConnectionState::Unreachable
                                                  : ConnectionState::Failed,
                                   outcome.message);
                    if (last || accountId == selectedAccount_) ShowAccountStatus();
                    // Opening a folder is a passive refresh: a server it cannot
                    // reach right after boot is the network not being up yet,
                    // so it takes the same grace period as a background sync
                    // (status line, a retry in a minute, the alert after ten
                    // minutes offline). Reload reports as before.
                    if (!userInitiated && outcome.NetworkUnreachable()) {
                        if (!offline_.Unreachable(accountId, NowMonotonicSec())) {
                            ScheduleOfflineRetry();
                            return;
                        }
                    } else {
                        offline_.Reached(accountId);
                    }
                    // Once per run of failures, on Reload too: its account sync
                    // reports the same server in its own alert.
                    if (syncErrorReported_.insert(accountId).second) {
                        AlertError(window_ ? window_.get() : nullptr,
                                   "That folder could not be fetched for " + who + ".",
                                   WithDiagnostics(outcome.message, outcome.diagnostics));
                    }
                    return;
                }
                syncErrorReported_.erase(accountId);
                offline_.Reached(accountId);
                accountError_.erase(accountId);
                NoteConnection(accountId, ConnectionState::Connected);
                if (last) ShowAccountStatus();
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
        if (userInitiated || !vaultLockReported_) {
            vaultLockReported_ = true;
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
        ApplyConnection(settings.imap, opts);
        const std::string username =
            settings.imap.username.empty() ? email : settings.imap.username;
        const std::string provider = OAuthProviderFor(settings);
        opts.credentials.username = username;

        auto svc = std::make_shared<SyncService>(workerStore_, *imap, mailDir_);
        const std::string aid = acc.accountId;
        if (++syncsInFlight_ == 1 && reloadButton_) reloadButton_->SetText("Updating…");
        SetStatus("Checking " + who + "…");
        NoteConnection(aid, ConnectionState::Checking);
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
                    if (reloadButton_) reloadButton_->SetText("Update");
                    UpdateBusyIndicator();
                }
                if (!outcome) {
                    accountError_[aid] = "Could not fetch mail for " + who + ": " + outcome.message;
                    NoteConnection(aid, outcome.NetworkUnreachable()
                                            ? ConnectionState::Unreachable
                                            : ConnectionState::Failed,
                                   outcome.message);
                    if (last || aid == selectedAccount_) ShowAccountStatus();
                    // A dead OAuth sign-in offers Retry → re-sign-in (which
                    // re-syncs the account), but only when the user asked — a
                    // background timer must never pop a dialog.
                    if (userInitiated &&
                        MaybeOfferReauth(aid, *credCode, provider, nullptr)) {
                        syncErrorReported_.insert(aid);
                        return;
                    }
                    // The server could not be reached at all. On a background
                    // sync that is most often the network not being up yet —
                    // right after the computer starts, or between two Wi-Fi
                    // networks — so the status line carries it, the account is
                    // retried sooner than the regular cadence, and the alert
                    // waits until the account has stayed unreachable for the
                    // whole grace period. The user's own Reload always reports.
                    if (!userInitiated && outcome.NetworkUnreachable()) {
                        const int64_t now = NowMonotonicSec();
                        const bool persisted = offline_.Unreachable(aid, now);
                        if (!persisted) { ScheduleOfflineRetry(); return; }
                    } else {
                        offline_.Reached(aid);   // the server answered: not a network gap
                    }
                    const bool firstReport = syncErrorReported_.insert(aid).second;
                    if (userInitiated || firstReport) {
                        AlertError(window_ ? window_.get() : nullptr,
                                   "New mail could not be fetched for " + who + ".",
                                   WithDiagnostics(outcome.message, outcome.diagnostics));
                    }
                    return;
                }
                syncErrorReported_.erase(aid);   // recovered: arm the next report
                offline_.Reached(aid);
                // The server answered, so the connection is up: messages that
                // failed to go out are worth another try now.
                if (outboxRetry_.Failures() > 0) outboxRetry_.RetryAt(NowMonotonicSec());
                accountError_.erase(aid);
                NoteConnection(aid, ConnectionState::Connected);
                vaultLockReported_ = false;
                if (last) ShowAccountStatus();
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
            if (progressBuf->size() < 20) return;   // bound UI churn on big syncs
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) { progressBuf->clear(); return; }
            auto batch = std::make_shared<std::vector<MessageEnvelope>>();
            batch->swap(*progressBuf);
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

void UltraMailApp::EditSenderContact(const MessageEnvelope& m, bool isNew) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    if (!contacts_.IsOpen()) {
        AlertError(parent, "The address book could not be opened.",
                   contactsError_.empty() ? "UltraMail's contacts database is not available."
                                          : contactsError_);
        return;
    }
    Contact contact;
    bool found = false;
    if (!isNew) {
        if (UltraDbResult r = contacts_.FindByEmail(m.fromAddr, contact, found); !r) {
            AlertError(parent, "The contact could not be loaded.", DetailLine(r));
            return;
        }
    }
    if (!found) {
        // New - or the index said "known" but the store no longer has it.
        contact = Contact{};
        contact.displayName = DisplayHeader(m.fromName);
        ContactEmail e; e.address = m.fromAddr; e.primary = true;
        contact.emails.push_back(e);
    }
    contactsView_.SetStore(&contacts_);
    contactsView_.EditContact(contact, /*isNew=*/!found, parent, [this](const Contact&) {
        RefreshContactIndex();
        Refresh();   // badges: the sender is (still) in the address book
    });
}

void UltraMailApp::AddSenderToContactGroup(const MessageEnvelope& m,
                                           const ContactPlace& place) {
    UltraCanvas::UltraCanvasWindowBase* parent = window_ ? window_.get() : nullptr;
    if (!contacts_.IsOpen() || m.fromAddr.empty()) return;
    Contact contact;
    bool found = false;
    UltraDbResult r = contacts_.FindByEmail(m.fromAddr, contact, found);
    if (r && found) {
        r = place.isGroup ? contacts_.MoveToGroup(contact.id, place.group)
                          : contacts_.MoveToSection(contact.id, place.section);
    } else if (r) {
        // New: the sender's name and address, filed where asked.
        contact = Contact{};
        contact.displayName = DisplayHeader(m.fromName);
        if (contact.displayName.empty()) contact.displayName = m.fromAddr;
        if (place.isGroup) contact.group = place.group;
        else               contact.section = place.section;
        ContactEmail e; e.address = m.fromAddr; e.primary = true;
        contact.emails.push_back(e);
        r = contacts_.Save(contact);
    }
    if (!r) {
        AlertError(parent, m.fromAddr + " could not be added to " + place.Title() + ".",
                   DetailLine(r));
        return;
    }
    SetStatus(m.fromAddr + (found ? " moved to " : " added to ") + place.Title());
    contactsView_.Refresh();   // no-op unless the Contacts window is open
    Refresh();                 // re-reads the address book: the sender's badge
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
    // One Contacts window: asking again brings the open one forward instead of
    // building a second panel over the same ContactsView.
    if (contactsWindow_) {
        contactsWindow_->RaiseAndFocus();
        return;
    }
    WindowConfig cfg;
    cfg.title  = "Contacts";
    cfg.width  = 720;
    cfg.height = 520;
    cfg.backgroundColor = Theme::kPageBackground;
    auto win = CreateWindow(cfg);

    contactsView_.SetStore(&contacts_);
    contactsView_.onChanged = [this]() { RefreshContactIndex(); Refresh(); };
    win->AddChild(contactsView_.Build());
    contactsView_.Resize(static_cast<float>(cfg.width), static_cast<float>(cfg.height));
    win->onWindowResize = [this](int cw, int ch) {
        contactsView_.Resize(static_cast<float>(cw), static_cast<float>(ch));
    };
    // Closed (by any means): drop the panel and the window, after the close
    // has finished with them, so the next Contacts opens a fresh one.
    win->onWindowClosed = [this]() {
        contactsView_.Release();
        auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
        auto drop = [this]() { contactsWindow_.reset(); };
        if (app) app->PostToUIThread(drop); else drop();
    };
    contactsWindow_ = win;
    win->Show();
}

void UltraMailApp::OpenOutbox() {
    if (!outbox_.IsOpen()) {
        AlertError(window_ ? window_.get() : nullptr, "The outbox could not be opened.",
                   outboxError_.empty() ? "UltraMail's outbox database is not available."
                                        : outboxError_);
        return;
    }
    if (outboxWindow_) {
        outboxWindow_->RaiseAndFocus();
        return;
    }
    WindowConfig cfg;
    cfg.title  = "Outbox";
    cfg.width  = 760;
    cfg.height = 380;
    cfg.backgroundColor = Theme::kPageBackground;
    auto win = CreateWindow(cfg);
    UltraCanvasWindow* raw = win.get();

    outboxView_.onSendNow = [this]() {
        std::vector<OutboxItem> pending;
        outbox_.ListPending(pending);
        for (const auto& item : pending) {
            if (outbox_.IsHeld(item.id)) continue;
            RetryOutbox(item.draft.fromAddr);
            return;
        }
    };
    outboxView_.onEdit   = [this](int64_t id) { EditFromOutbox(id); };
    outboxView_.onDelete = [this](int64_t id) { ConfirmDeleteFromOutbox(id); };
    outboxView_.onClose  = [raw]() { raw->Close(); };
    win->AddChild(outboxView_.Build());
    outboxView_.Resize(static_cast<float>(cfg.width), static_cast<float>(cfg.height));
    win->onWindowResize = [this](int cw, int ch) {
        outboxView_.Resize(static_cast<float>(cw), static_cast<float>(ch));
    };
    // Closed (by any means): drop the panel and the window after the close
    // has finished with them, so the next Outbox opens a fresh one.
    win->onWindowClosed = [this]() {
        outboxView_.Release();
        auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
        auto drop = [this]() { outboxWindow_.reset(); };
        if (app) app->PostToUIThread(drop); else drop();
    };
    outboxWindow_ = win;
    RefreshOutbox();
    win->Show();
}

void UltraMailApp::RefreshOutbox() {
    std::vector<OutboxItem> pending;
    if (outbox_.IsOpen()) outbox_.ListPending(pending);
    if (outboxButton_) {
        outboxButton_->SetText("Outbox (" + std::to_string(pending.size()) + ")");
        outboxButton_->SetVisible(!pending.empty());
    }
    if (outboxWindow_) {
        std::set<int64_t> held;
        for (const auto& item : pending) if (outbox_.IsHeld(item.id)) held.insert(item.id);
        outboxView_.SetItems(pending, outboxFlushInFlight_, held);
    }
}

void UltraMailApp::ConfirmDeleteFromOutbox(int64_t id) {
    std::vector<OutboxItem> pending;
    outbox_.ListPending(pending);
    const OutboxItem* item = nullptr;
    for (const auto& p : pending) if (p.id == id) item = &p;
    if (!item) { RefreshOutbox(); return; }   // sent meanwhile
    std::string to;
    for (const auto& list : {item->draft.to, item->draft.cc, item->draft.bcc})
        for (const auto& a : list) to += (to.empty() ? "" : ", ") + a;
    const std::string subject = item->draft.subject.empty() ? "(no subject)" : item->draft.subject;
    const std::string where = item->HasDraftCopy()
        ? " Its copy in " + item->draftsFolder + " is deleted too." : std::string();
    UltraCanvasDialogManager::ShowConfirmation(
        "Delete \"" + subject + "\" to " + to + "? It will not be sent." + where,
        "Delete message",
        [this, id](bool confirmed) {
            if (!confirmed) return;
            // The Drafts copy is deleted on the server: that needs the sign-in.
            // Queued behind a send that is running now: it may be sending
            // this very message, and says so if it did.
            EnsureVaultUnlocked([this, id]() { DeleteFromOutbox(id, /*quiet=*/false); });
        },
        outboxWindow_ ? outboxWindow_.get()
                      : (window_ ? static_cast<UltraCanvas::UltraCanvasWindowBase*>(window_.get())
                                 : nullptr));
}

void UltraMailApp::DeleteFromOutbox(int64_t id, bool quiet) {
    auto result = std::make_shared<UltraDbResult>(UltraDbResult::Ok());
    auto outcome = std::make_shared<Outbox::DeleteOutcome>(Outbox::DeleteOutcome::Deleted);
    outboxDeleting_.insert(id);
    RunOutboxJob(
        [id, result, outcome](Outbox& ob, const ServerCopies* copies) {
            *result = ob.DeleteMessage(id, copies, outcome.get());
        },
        "Deleting from the outbox…",
        [this, id, quiet, result, outcome]() {
            outbox_.SetHeld(id, false);
            outboxDeleting_.erase(id);
            UltraCanvas::UltraCanvasWindowBase* parent =
                outboxWindow_ ? outboxWindow_.get() : (window_ ? window_.get() : nullptr);
            if (!*result) {
                AlertError(parent, "The message could not be deleted from the outbox.",
                           DetailLine(*result));
                return;
            }
            // Nothing left to do: the automatic retries end. A Drafts copy
            // still on the server: a pass comes back for it.
            if (OutboxPending() - outbox_.HeldCount() <= 0 && OutboxWithdrawn() == 0)
                outboxRetry_.Succeeded();
            else if (!outboxRetry_.Scheduled())
                outboxRetry_.Failed(NowMonotonicSec());
            if (quiet) return;
            if (*outcome == Outbox::DeleteOutcome::AlreadyGone)
                AlertSuccess(parent, "This message had been sent before it could be deleted.");
            else if (*outcome == Outbox::DeleteOutcome::CopyLeftForLater)
                AlertSuccess(parent, "The message was deleted from the outbox and will not be sent.",
                             "Its copy in the Drafts folder could not be deleted yet - the "
                             "server could not be reached. UltraMail deletes it the next time "
                             "it does.");
        });
}

void UltraMailApp::WhenOutboxIdle(std::function<void()> action) {
    if (!outboxFlushInFlight_) { action(); return; }
    whenOutboxIdle_.push_back(std::move(action));
}

void UltraMailApp::EditFromOutbox(int64_t id) {
    // Already open for correcting: that window, not a second one.
    for (const auto& s : composers_) {
        if (s.editsOutboxId != id) continue;
        s.window->RaiseAndFocus();
        return;
    }
    UltraCanvas::UltraCanvasWindowBase* parent =
        outboxWindow_ ? outboxWindow_.get() : (window_ ? window_.get() : nullptr);
    if (outboxFlushInFlight_) {
        // A send is running, maybe of this very message: the window opens
        // once it has finished (or says the message has gone out).
        if (outboxWindow_)
            outboxView_.SetNote("The message opens for correcting once the current attempt "
                                "to send has finished.");
        WhenOutboxIdle([this, id]() { EditFromOutbox(id); });
        return;
    }
    std::vector<OutboxItem> pending;
    outbox_.ListPending(pending);
    for (const auto& item : pending) {
        if (item.id != id) continue;
        // Held from now on: no pass sends the old version while it is corrected.
        outbox_.SetHeld(id, true);
        Draft draft = item.draft;
        draft.messageId.clear();   // the corrected message is a new one
        MakeRichEdit(draft);       // an HTML message opens formatted
        OpenComposer(draft, id);
        RefreshOutbox();
        return;
    }
    AlertSuccess(parent, "This message has been sent meanwhile.");
    RefreshOutbox();
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
        // Contacts filed in the user's own groups count too.
        std::vector<GroupCount> groups;
        if (contacts_.ListGroups(groups))
            for (auto& g : groups) total += g.count;
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

    AttachmentCache cache(attachmentDir_);
    const std::string path = cache.Write(attachment);
    if (path.empty()) {
        AlertError(parent,
                   "The attachment could not be opened.",
                   "\"" + (attachment.filename.empty() ? std::string("(unnamed)")
                                                       : attachment.filename)
                   + "\" could not be written to the attachment cache in "
                   + attachmentDir_ + ". Check that the folder exists and is writable.");
        return;
    }

    // Show it in UltraCanvas's own media viewer when it knows the kind: no
    // other application needed, and it looks the same on every platform.
    if (UltraCanvas::UltraCanvasMediaViewer::IsSupportedMedia(path)) {
        if (!attachmentViewer_)
            attachmentViewer_ = std::make_unique<UltraCanvas::UltraCanvasMediaViewerWindow>();
        UltraCanvas::MediaViewerWindowOptions options;
        options.title = attachment.filename.empty() ? std::string("Attachment")
                                                    : attachment.filename;
        // The cache folder holds every attachment ever opened: show this one only.
        options.browseFolder = false;
        if (attachmentViewer_->Show(path, parent, options)) return;
    }

    // Anything else goes to the operating system's default application — the
    // same behaviour as double-clicking it in a file manager. When nothing is
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
    if (const std::string ext = PathToUtf8(PathFromUtf8(suggested).extension());
        ext.size() > 1)
        opts.AddFilter(attachment.mediaType.empty() ? ("*" + ext) : attachment.mediaType,
                       ext.substr(1));
    opts.AddFilter("All files", "*");

    const std::string cacheDir = attachmentDir_;
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
        const std::filesystem::path downloads = PathFromUtf8(home) / "Downloads";
        if (std::filesystem::is_directory(downloads, ec)) return PathToUtf8(downloads);
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
    UpdateConnectionIndicator();   // follows the selection settled above
    mailView_.SetAccounts(accounts_);
    mailView_.ShowAccount(selectedAccount_);
    PublishUnreadNotice();

    // No account yet → only the start page; otherwise only the account view.
    const bool firstRun = accounts_.empty();
    if (auto page = startPage_.Container()) page->SetVisible(firstRun);
    if (accountView_) accountView_->SetVisible(!firstRun);
}

// The desktop's mail icon shows the unread total as a badge. It reads the
// notice UltraCanvasDesktopShell keeps (one small file per application), so
// the desktop never links UltraMail and UltraMail never knows the desktop.
void UltraMailApp::PublishUnreadNotice() {
    int unread = 0;
    int today = 0;
    for (const auto& st : status_) {
        unread += st.unread;
        today += st.unreadToday;
    }
    std::string text = std::to_string(unread) + " unread";
    if (today > 0) text += ", " + std::to_string(today) + " today";
    UltraCanvasDesktopShell::PublishNotice("UltraMail", unread, text);
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
            // sync worker (syncsInFlight_) still holds valid workerStore_/mailDir_
            // references; at worst it re-inserts a few rows after this, which
            // the next removal (or a restart) clears — acceptable here.
            scheduler_.Remove(accountId);
            offline_.Reached(accountId);   // and its held-back network failure
            connection_.erase(accountId);

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
            std::filesystem::remove_all(PathFromUtf8(mailDir_) / PathFromUtf8(accountId), ec);

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

void UltraMailApp::OpenSettings() {
    SettingsDialog::Show(window_ ? window_.get() : nullptr, &prefs_, [this]() {
        // Every change is saved and applied at once.
        prefs_.Save(prefsPath_);
        mailView_.SetReadingPane(prefs_.showReadingPane);
        mailView_.SetBodyOptions(prefs_.showHtml, static_cast<float>(prefs_.messageTextSize));
        mailView_.SetFolderTreeWidth(prefs_.folderTreeWidthMode == FolderTreeWidthMode::FitToText,
                                     prefs_.folderTreeWidth);
        senderIcons_.SetNetworkEnabled(prefs_.fetchSenderIcons);
        ApplyLinkDisplay();
        // New waiting-for-reply rules: the account bar's count and the list's
        // reply marks are worked out again.
        if (ApplyNeedsAnswerRules()) Refresh();
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
        // The red "Delete account" button in the settings dialog's bottom row.
        // It closes the page, then HandleDeleteAccount runs the confirm-and-remove.
        fields.onDelete = [this, accountId]() { HandleDeleteAccount(accountId); };
        // The Signature row: the editor saves on its own Save.
        fields.signature       = account.signature;
        fields.onSaveSignature = [this, accountId](const Signature& signature) {
            SaveSignature(accountId, signature);
        };

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
                Account updated = account;
                AutoDiscovery::ApplyTo(updated, r.settings);
                updated.displayName =
                    r.displayName.empty() ? LocalPart(account.email) : r.displayName;
                if (UltraDbResult up = store_.UpsertAccount(updated); !up) {
                    AlertError(window_ ? window_.get() : nullptr,
                               "The account settings could not be saved.", DetailLine(up));
                    return;
                }
                // A typed password replaces the stored one (the vault is open)
                // and switches the account to it: stored OAuth tokens would
                // otherwise go on being used ahead of the password.
                if (!r.newPassword.empty()) {
                    vault_.Store(account.accountId, r.newPassword);
                    if (!r.reauth) vault_.RemoveOAuthTokens(account.accountId);
                }
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

std::map<std::string, UltraMailApp::SmtpAccount> UltraMailApp::SmtpAccounts() const {
    std::map<std::string, SmtpAccount> out;
    for (const auto& a : accounts_) out[a.accountId] = SmtpAccount{a.email, SettingsFor(a)};
    return out;
}

UltraNetResult UltraMailApp::PrepareSmtp(const std::map<std::string, SmtpAccount>& accounts,
                                         const std::string& accountId, UltraNetMailOptions& o) {
    auto it = accounts.find(accountId);
    if (it == accounts.end())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "the account of this message no longer exists");
    const SmtpAccount& account = it->second;
    const DiscoveryResult& settings = account.settings;
    if (!settings.found)
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "no outgoing (SMTP) server is known for " + account.email);
    ApplyConnection(settings.smtp, o);
    // The account's server as it is now: a message queued before the server
    // was known (or before it was corrected) still goes out on Retry.
    o.serverUrl = AutoDiscovery::SmtpServerUrl(settings.smtp);
    const std::string username =
        settings.smtp.username.empty() ? account.email : settings.smtp.username;
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
    // provider shows (or the https redirect address it lands on) rather than
    // waiting on a socket.
    if (OAuthUsesPastedCode(OAuthApps::Get(providerId).redirectUri)) {
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
