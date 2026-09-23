// Apps/UltraMail/ui/UltraMailApp.h
// The UltraMail application manager: owns the local store, the account list and
// the main window, and wires the start page, the account bar, the mail view
// (inbox table + message details) and the account-setup wizard together.
// Texter-style app-composition class.
// Version: 0.9.0 - server settings per account (provider table, autoconfig
//                  lookup, manual page with a login check); stored on the account.
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailStartPage.h"
#include "UltraMailAccountBar.h"
#include "UltraMailMailView.h"
#include "UltraMailAccountWizard.h"
#include "UltraMailContactsView.h"
#include "UltraMailComposeWindow.h"
#include "UltraMailPassphraseDialog.h"
#include "UltraMailServerSettingsDialog.h"

#include "UltraMailPreferences.h"

#include "UltraMailLocalStore.h"
#include "UltraMailSyncEngine.h"
#include "UltraMailMimeCodec.h"
#include "UltraMailContactStore.h"
#include "UltraMailSenderIconCache.h"
#include "UltraMailSenderTrust.h"
#include "UltraMailOutbox.h"
#include "UltraMailSyncScheduler.h"
#include "UltraMailFeedPublisher.h"
#include "UltraMailCredentialVault.h"
#include "UltraMailOAuth.h"

#include <UltraCloud/UltraCloud.h>

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraMail {

class UltraMailApp {
public:
    // Open the local store under `dataDir` (created if absent) and load the
    // account list. Returns false if the store cannot be opened; `outError`,
    // when given, receives the database diagnostic so main() can show it
    // instead of exiting silently.
    bool Initialize(const std::string& dataDir, std::string* outError = nullptr);

    // Wipes the vault's derived key from memory when the app goes away.
    ~UltraMailApp();

    // Create the main window: the start page (no account yet) or the account
    // view (actions · account bar · inbox table | message details).
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> CreateMainWindow();

    // Reload accounts + status, rebuild the account bar and the mail view, and
    // switch between the start page and the account view.
    void Refresh();

private:
    // Build the account view (everything shown once an account exists).
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildAccountView(float width, float height);
    // Size the start page and the account view to the window's client area.
    void ResizeViews(float width, float height);

    void HandleAddAccount();
    // Confirm, then remove an account entirely: its background-sync entry, its
    // vault credentials, its store rows (messages + folders + account) and its
    // downloaded mail under mailDir_/<accountId>. Mail on the server is not
    // touched. Falls back to the start page when the last account is removed.
    void HandleDeleteAccount(const std::string& accountId);
    // The wizard's identity step is done: find the servers (provider table,
    // stored settings of an account with the same address, then the autoconfig
    // lookup on a worker thread, then the manual page) and complete the setup.
    void HandleWizardSubmit(const AccountDraft& draft);
    // The autoconfig lookup with a cancellable wait dialog; falls through to
    // the manual settings page when nothing was found.
    void LookupServerSettings(const AccountDraft& draft);
    // Store the account with its servers, seed the inbox, report the settings,
    // and store the password / run the browser sign-in.
    void CompleteAccountSetup(const AccountDraft& draft, const DiscoveryResult& settings);
    // The manual settings page for an existing account whose servers are not
    // known (or to correct them); checks the sign-in with the account's stored
    // credentials, saves, then syncs the account.
    void EditServerSettings(const std::string& accountId);
    // The full account settings page (toolbar "Settings"): edit the display
    // name, the IMAP/SMTP servers and the password — or re-run the browser
    // sign-in for an OAuth account — checking the sign-in before saving.
    void HandleAccountSettings(const std::string& accountId);
    // The settings page's login check: resolves the credentials through
    // `credentials` (on the worker) and lists the incoming server once with
    // the IMAP plug-in; the outcome is delivered on the UI thread. A missing
    // plug-in is a failed check (the page then offers "Save anyway"). The
    // candidate carries a typed new password when the user changed it.
    ServerSettingsDialog::Verifier LoginVerifier(
        std::function<UltraNetResult(const ServerSettingsDialog::Result& candidate,
                                     const std::string& username, UltraNetCredentials&)> credentials);
    // The servers an account uses: stored on the account, else the provider
    // table (AutoDiscovery::ForAccount).
    static DiscoveryResult SettingsFor(const Account& account);
    // Same, by address — for the composer's From address; the provider table
    // when no account carries it.
    DiscoveryResult SettingsForEmail(const std::string& email) const;
    // Fill an SMTP session's options for an account: username, TLS mode of
    // its outgoing server, and the credentials (see ResolveCredentials).
    UltraNetResult PrepareSmtp(const std::string& accountId, UltraNetMailOptions& options);
    // Browser sign-in for an OAuth2 provider ("google"): opens the consent page,
    // waits (with a cancellable dialog) for the redirect on a worker thread,
    // stores the tokens in the vault — which must be open — and runs the first
    // sync. Failures are reported with the provider's reason. `onReauthed`, when
    // given, runs after a successful sign-in (used to retry the action that hit
    // an expired sign-in).
    void StartOAuthSignIn(const std::string& accountId, const std::string& email,
                          const std::string& providerId,
                          std::function<void()> onReauthed = nullptr);
    // Warn that "Sign in with <provider>" cannot run because no OAuth client id
    // is configured, naming oauth.ini / the env var that would supply it.
    void ReportMissingOAuthClient(const std::string& providerId);
    // The address of an account, or "" when unknown.
    std::string EmailForAccount(const std::string& accountId) const;
    // Resolve the IMAP/SMTP credentials of an account from the vault: its
    // password, or a fresh OAuth2 bearer token (refreshing through the provider
    // when expired — one HTTPS request, so call it off the UI thread where the
    // caller can). The vault must be open. Takes the username and the OAuth
    // provider rather than looking them up, so a worker thread never reads the
    // UI-owned account list.
    UltraNetResult ResolveCredentials(const std::string& accountId, const std::string& username,
                                      const std::string& providerId, UltraNetCredentials& out);
    // "Reload email": sync the selected account now (when the IMAP plug-in is
    // present) and re-read the store. Only the selected account, so Reload never
    // fetches — or opens a settings dialog for — an account not in view.
    void HandleReload();
    static std::string SlugFromEmail(const std::string& email);
    static std::string LocalPart(const std::string& email);

    // Materialise an attachment to the cache and hand it to the OS default
    // application; when no application is associated, offer to save it instead.
    void OpenAttachment(const Attachment& attachment);
    // Save an attachment to a location the user picks, through the framework's
    // file dialog (UltraCanvasFileLoader::SaveFileDialog).
    void SaveAttachment(const Attachment& attachment);
    // Where the Save-As dialog starts: Downloads, else home, else ".".
    static std::string DefaultSaveDirectory();

    // Point the sender-icon cache at its folder under the cache directory, give
    // it the HTTPS fetcher it downloads a known service's icon with, and apply
    // the user's "download icons of known senders" preference. Called once at
    // startup and again whenever that preference changes.
    void ConfigureSenderIcons();
    // Re-read the address book into the index the sender badge classifies with,
    // and hand it to the mail view. Cheap enough to run on every Refresh(), and
    // that is what keeps a newly added contact's mail turning green.
    void RefreshContactIndex();

    // Open the contact manager in its own window.
    void OpenContacts();
    // Seed a few contacts across sections (demo only).
    void SeedDemoContacts();

    // Seed a few messages + cached .eml bodies (demo only).
    void SeedDemoMail();
    // Add an in-memory demo cloud account with a few files (demo only).
    void SeedDemoCloud();
    void MigrateCloudSecrets();

    // Open a compose window for the given draft (new / reply / forward).
    void OpenComposer(const Draft& draft);

    // Message actions from the reading pane, mirrored to the IMAP server on a
    // background worker and then refreshed. Delete moves to Trash (fallback:
    // \Deleted flag + local removal); Junk moves to the Junk mailbox; Mark-Unread
    // clears \Seen. All non-blocking; failures surface an alert.
    void HandleDeleteMessage(const MessageEnvelope& env);
    void HandleJunkMessage(const MessageEnvelope& env);
    void HandleMarkUnread(const MessageEnvelope& env);
    // Opening a message marks it read: updates the local store and the list row
    // immediately (optimistic), then pushes \Seen to the server in the
    // background when the vault is already open (a passive click never prompts
    // for the master password, and staying offline is fine — the next folder
    // reconcile agrees the server later).
    void HandleMarkRead(const MessageEnvelope& env);
    // Run one IMAP mailbox op on a worker (credentials resolved off the UI
    // thread), then Refresh() on success or alert `actionName` on failure.
    void RunMailboxAction(const std::string& accountId,
                          std::function<SyncOutcome(SyncEngine&, const std::string& serverUrl,
                                                    const UltraNetMailOptions&)> op,
                          const std::string& actionName);
    // Like RunMailboxAction, but for a passive, best-effort op: it does not
    // Refresh() on success (so the list selection is not bounced to the top) and
    // it stays silent on failure. Used by mark-read-on-open.
    void RunMailboxActionQuiet(const std::string& accountId,
                               std::function<SyncOutcome(SyncEngine&, const std::string& serverUrl,
                                                         const UltraNetMailOptions&)> op);
    // When a mailbox/sync op failed because an OAuth account's stored sign-in is
    // dead (the refresh token was expired or revoked — Google's invalid_grant),
    // show a "sign in again" prompt whose Retry re-runs the browser consent and
    // then `onReauthed`, and return true. Returns false for any other failure so
    // the caller shows its normal error. `provider` is "" for password accounts.
    bool MaybeOfferReauth(const std::string& accountId, UltraNetResultCode code,
                          const std::string& provider,
                          std::function<void()> onReauthed);
    // The name of the account's folder with the given special-use role, or "".
    std::string FolderWithRole(const std::string& accountId, FolderRole role) const;
    // Open the raw .eml source of a message in a read-only window.
    void OpenSourceViewer(const std::string& subject, const std::string& raw);
    // Attempt to send a draft via the SMTP plug-in; report the outcome.
    void HandleSendDraft(const Draft& draft);
    // Re-flush the outbox after a failed send (the Retry button's action).
    void RetryOutbox(const std::string& fromAddr);
    // Flush the outbox with the vault open and report the outcome. Split out
    // of HandleSendDraft because unlocking is answered through a dialog, so the
    // send continues in a callback rather than in line.
    void FlushAndReport(const Draft& draft,
                        UltraCanvas::UltraCanvasWindowBase* parent,
                        const std::string& recipients);

    // Run `onUnlocked` with the credential vault open, prompting for the master
    // password first when it is still locked (and re-prompting on a wrong one).
    // `onUnlocked` does not run if the user cancels or the vault cannot open.
    void EnsureVaultUnlocked(std::function<void()> onUnlocked,
                             const std::string& errorText = {});

    // Auto-collect senders of a folder's messages into the address book.
    void CollectContacts(const std::string& accountId, const std::string& folder);
    // Register every account with the scheduler and start the periodic
    // background sync timer (only when the IMAP plug-in is available). Safe to
    // call again after an account was added: accounts already registered keep
    // their last-sync time and the timer is started once.
    void StartBackgroundSync();
    // Sync the accounts the scheduler reports as due (called from the timer),
    // or every account when `force` is set (the Reload button).
    void RunSyncs(bool force);
    // Sync one account now — the first sync right after it was added.
    void SyncAccount(const std::string& accountId);
    // Fetch one folder's messages now (envelopes + bodies), on a worker. Backs
    // the lazy load when a non-inbox folder is first opened and the Reload of a
    // folder other than the inbox. No-op without the IMAP plug-in / an unlocked
    // vault / known servers.
    void SyncFolder(const std::string& accountId, const std::string& folder);
    // Run the given accounts through the SyncService on worker threads and
    // report the outcome on the UI thread. `userInitiated` syncs (Reload, a new
    // account) always say why nothing was fetched; timer syncs say so once.
    void SyncAccounts(const std::vector<ScheduledAccount>& targets, bool userInitiated);
    // The IMAP plug-in as the mailbox interface, or null when it is not loaded.
    IMailboxProtocolPlugin* ImapPlugin() const;
    // Explain that no mail can be fetched because the IMAP plug-in was not
    // found, naming the directory that was searched.
    void ReportMissingImapPlugin();
    // Where the UltraNet plug-in DSOs are: ULTRAMAIL_PLUGIN_DIR, else the first
    // Plugins/UltraNet directory next to (or up to two levels above) the
    // executable, else the working directory's — so the app finds its plug-ins
    // wherever it is started from, not only from the build directory.
    static std::string ResolvePluginDirectory();

    // Session-lifetime: the master password is entered once, and the derived
    // key lives only while the app runs.
    CredentialVault vault_{""};
    // OAuth2 sign-in + token refresh for providers that need it (Gmail).
    MailOAuth       oauth_;

    LocalStore store_;
    ContactStore contacts_;
    // Icons of the known services in the sender registry, under
    // <cacheDir>/sender-icons. Read by the badge on the UI thread, filled by
    // the sync worker; the class is internally locked for exactly that.
    SenderIconCache senderIcons_;
    OutboxStore outbox_;
    // Cloud storage (UltraCloud): accounts + secrets behind the composer's
    // "Attach cloud link". The secrets live in the mail vault (vault_) under
    // "cloud.<accountId>.*"; MigrateCloudSecrets() carries the obfuscated
    // cloud-vault/ files of earlier releases into it once it is unlocked.
    UltraCloud::AccountStore cloudAccounts_;
    std::unique_ptr<UltraCloud::VaultSecretStore> cloudSecrets_;
    std::unique_ptr<UltraCloud::CloudService> cloud_;
    std::vector<Account> accounts_;
    std::vector<AccountStatus> status_;
    // Non-empty when a store the app needs could not be opened; the matching
    // entry point alerts instead of returning silently.
    std::string contactsError_;
    std::string outboxError_;
    // Set once a background sync has alerted, so a broken server does not raise
    // an alert on every timer tick.
    bool syncErrorReported_ = false;

    // App-wide view preferences (reading pane on/off), remembered between runs
    // in preferences.ini under the data directory.
    Preferences prefs_;
    std::string prefsPath_;
    // Per-folder resync bookkeeping, keyed by (accountId + "\n" + folder).
    // Opening a folder always refreshes it from the server, but throttled: a
    // repeat open within kFolderResyncSec of the last one is skipped, and a
    // folder already being fetched is not fetched again.
    std::map<std::string, int64_t> folderSyncedAt_;      // key -> monotonic seconds
    std::set<std::string>          folderSyncInFlight_;  // key currently fetching
    // Do not re-hit the server for a folder opened again within this window.
    static constexpr int64_t kFolderResyncSec = 15;
    // Seconds on a steady clock (wall-clock jumps must not affect the throttle).
    static int64_t NowMonotonicSec() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::string dataDir_;
    std::string cacheDir_;
    std::string mailDir_;
    // The plug-in directory the registry was pointed at (for diagnostics).
    std::string pluginDir_;
    // True once the periodic sync timer runs, so StartBackgroundSync() can be
    // called again (after an account is added) without starting a second one.
    bool        syncTimerStarted_ = false;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    // The account view root; hidden while the start page is up (no account
    // configured) and shown once the first account exists.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> accountView_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    reloadButton_;
    std::string     selectedAccount_;   // the account the mail view shows
    int             syncsInFlight_ = 0;
    StartPage       startPage_;
    AccountBar      accountBar_;
    MailView        mailView_;
    ContactsView    contactsView_;
    ComposeView     composeView_;
    SyncScheduler   scheduler_;
    // New mail to the desktop feed (UltraMessage mail.message); fed from the
    // sync workers' progress callbacks.
    FeedPublisher   feed_;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasWindow>> viewerWindows_;
};

} // namespace UltraMail
