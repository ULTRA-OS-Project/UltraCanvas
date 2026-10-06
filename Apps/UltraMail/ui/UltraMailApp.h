// Apps/UltraMail/ui/UltraMailApp.h
// The UltraMail application manager: owns the local store, the account list and
// the main window, and wires the start page, the account bar, the mail view
// (inbox table + message details) and the account-setup wizard together.
// Texter-style app-composition class.
// Version: 0.11.0 - SwitchToAccount (the account's stored mail at once, its
//                   inbox refreshed in the background); FetchMissingBody;
//                   ForgetDownloadedMail on a change of incoming server
// Version: 0.10.3 - ApplyLinkDisplay: a link's address in the status line or as a
//                   tooltip (Settings > Display > Links)
// Version: 0.10.2 - the links segment of the status line (ShowMessageLinks /
//                   ShowHoveredLink)
// Version: 0.10.1 - Edit / Delete wait for a running send (WhenOutboxIdle)
// Version: 0.10.0 - the Outbox window (OpenOutbox), outbox work in one queue
//                   (RunOutboxJob)
// Version: 0.9.0 - server settings per account (provider table, autoconfig
//                  lookup, manual page with a login check); stored on the account.
// Last Modified: 2026-10-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasBadge.h"
#include "UltraCanvasBusyIndicator.h"
#include "UltraCanvasMediaViewerWindow.h"
#include "UltraMailStartPage.h"
#include "UltraMailAccountBar.h"
#include "UltraMailMailView.h"
#include "UltraMailAccountWizard.h"
#include "UltraMailContactsView.h"
#include "UltraMailOutboxView.h"
#include "UltraMailComposeWindow.h"
#include "UltraMailSignature.h"
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
#include "UltraCanvasLabel.h"

#include <chrono>
#include <cstdint>
#include <ctime>
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
    // A click on an account's tile: its mail as stored, at once - the list,
    // then the reading pane after the list has been painted - and its inbox
    // refreshed from the server in the background (throttled like opening a
    // folder). None of the work Refresh() does for every account (counting
    // every account's mail, re-reading the address book) runs here: nothing
    // it reads changed by looking at another account.
    // By value: the id a tile's click hands over lives in that tile.
    void SwitchToAccount(std::string accountId);
    // The unread total, published for the desktop's mail badge on every Refresh.
    void PublishUnreadNotice();
    // Re-count the account bar (unread, waiting for reply) from the store and
    // redraw it, without rebuilding the mail list - after a message is read.
    void RefreshAccountCounts();
    // Hand the Settings' waiting-for-reply rules to the store; true when they
    // changed (the counts and the list's reply marks then need a refresh).
    bool ApplyNeedsAnswerRules();

private:
    // Build the account view (everything shown once an account exists).
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildAccountView(float width, float height);
    // Size the start page and the account view to the window's client area.
    void ResizeViews(float width, float height);

    void HandleAddAccount();
    // The account's incoming server now reaches another mailbox (another host
    // or user name, IncomingMailboxChanged): drop the mail downloaded from the
    // old one - store rows, folders, cached bodies - so the next sync fetches
    // the new mailbox whole. Its UIDs mean nothing on the new server, and an
    // incremental fetch would skip every new message below the old highest UID.
    void ForgetDownloadedMail(const std::string& accountId);
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
    // The Settings window (the toolbar's gear): app-wide options.
    void OpenSettings();
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
    // Each account's address and servers, copied on the UI thread for a
    // worker that sends mail: accounts_ belongs to the UI thread (Refresh()
    // replaces it), so a worker reads this snapshot instead.
    struct SmtpAccount {
        std::string     email;
        DiscoveryResult settings;
    };
    std::map<std::string, SmtpAccount> SmtpAccounts() const;
    // Fill an SMTP session's options for an account from the snapshot:
    // username, TLS mode and sign-in method of its outgoing server, and the
    // credentials (see ResolveCredentials). Runs on the send worker.
    UltraNetResult PrepareSmtp(const std::map<std::string, SmtpAccount>& accounts,
                               const std::string& accountId, UltraNetMailOptions& options);
    // A null `plugin` (no SMTP plug-in) only saves the Drafts copies.
    // Flush the outbox on a worker and call `onDone` on the UI thread. SMTP to
    // a slow or failing server - connect and operation timeouts per queued
    // message, an OAuth2 token refresh - used to run on the UI thread and hold
    // the whole window. One flush at a time: two would pick up the same queued
    // message and send it twice; a flush asked for meanwhile runs next.
    void FlushOutboxInBackground(std::shared_ptr<IUltraNetPlugin> plugin,
                                 std::function<void(const Outbox::FlushStats&)> onDone);
    // Work on the outbox - a send pass, deleting a message - runs here: on a
    // worker, one job at a time (a job asked for meanwhile runs next, in
    // order), with the server copies snapshotted on the UI thread as it
    // starts. `onDone` runs on the UI thread afterwards.
    using OutboxJob = std::function<void(Outbox&, const ServerCopies*)>;
    void RunOutboxJob(OutboxJob job, const std::string& status, std::function<void()> onDone);
    // Browser sign-in for an OAuth2 provider ("google"): opens the consent page,
    // waits (with a cancellable dialog) for the redirect on a worker thread,
    // stores the tokens in the vault — which must be open — and runs the first
    // sync. Failures are reported with the provider's reason. `onReauthed`, when
    // given, runs after a successful sign-in (used to retry the action that hit
    // an expired sign-in).
    void StartOAuthSignIn(const std::string& accountId, const std::string& email,
                          const std::string& providerId,
                          std::function<void()> onReauthed = nullptr);
    // The out-of-band variant (Yahoo): opens the browser, prompts for the code
    // the provider shows, then exchanges it. Used by StartOAuthSignIn when the
    // provider's redirect is "oob".
    void StartOAuthOobSignIn(const std::string& accountId, const std::string& email,
                             const std::string& providerId,
                             std::function<void()> onReauthed);
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
    // Set the bottom status-line text (UI thread). Empty resets to "Ready".
    void SetStatus(const std::string& text);
    // The status line's links segment: how many links the shown message has
    // and where they go (each one, text and target, in its tooltip), and while
    // the pointer is on a link, that link's real target.
    void ShowMessageLinks(const std::vector<MessageLink>& links);
    void ShowHoveredLink(const std::string& href);
    // Settings > Display > Links: the status line's links segment (status
    // bar), or a tooltip over the link under the pointer and no segment.
    void ApplyLinkDisplay();
    // Runs the status-line ring while a sync, send or mailbox action is in flight.
    void UpdateBusyIndicator();
    // The connection pill at the right end of the status line: the selected
    // account's last contact with its mail server, with the details (server,
    // last contact, reason) in its tooltip.
    enum class ConnectionState { Unknown, Checking, Connected, Unreachable, Failed };
    void NoteConnection(const std::string& accountId, ConnectionState state,
                        const std::string& reason = "");
    void UpdateConnectionIndicator();
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

    // Open a compose window for the given draft (new / reply / forward). Every
    // window has its own ComposeView, so several can be open at once; the
    // returned view lives until its window closes.
    // `replacesOutboxId`: the window corrects that waiting message (the
    // outbox window's Edit), held meanwhile; once the new version is queued
    // the old one is deleted, and closing the window unsent lets it go again.
    ComposeView* OpenComposer(const Draft& draft, int64_t replacesOutboxId = 0);
    // Forgets a compose window once it has closed (on the next UI turn, never
    // inside the window's own close callback).
    void RetireComposer(UltraCanvas::UltraCanvasWindow* window);
    // `draft` with the signature of the account it is sent from (its fromAddr)
    // put in - as account settings define it.
    Draft WithSignature(Draft draft, DraftPurpose purpose) const;
    // Saves an account's signature (from the signature editor) and keeps the
    // in-memory account list in step.
    void SaveSignature(const std::string& accountId, const Signature& signature);

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
                          const std::string& actionName,
                          std::function<void()> onSuccess = nullptr);
    // Message-list menu actions.
    void HandleMoveMessage(const MessageEnvelope& env, const std::string& folder);
    void HandleNotJunk(const MessageEnvelope& env);
    void HandleSetNeedsAnswer(const MessageEnvelope& env, bool needsAnswer);
    // Leave the mailing list a message came from, the way its List-Unsubscribe
    // header asks (one-click POST, web page or a message to send); the body is
    // downloaded first when it is not cached yet.
    void HandleUnsubscribe(const MessageEnvelope& env);
    void UnsubscribeWith(const MessageEnvelope& env, const std::string& raw);
    // Like RunMailboxAction, but for a passive, best-effort op: it does not
    // Refresh() on success (so the list selection is not bounced to the top) and
    // it stays silent on failure. Used by mark-read-on-open.
    // `onDone`, when given, runs on the UI thread with the outcome (a failed
    // sign-in included); it does not run when the op could not be started.
    void RunMailboxActionQuiet(const std::string& accountId,
                               std::function<SyncOutcome(SyncEngine&, const std::string& serverUrl,
                                                         const UltraNetMailOptions&)> op,
                               std::function<void(const SyncOutcome&)> onDone = nullptr);
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
    // How a folder of the account reads ("Drafts" for "INBOX.Drafts"), by the
    // separator its server lists (UltraMailFolderNames).
    std::string FolderLabel(const std::string& accountId, const std::string& folder) const;
    // Open the raw .eml source of a message in a read-only window.
    void OpenSourceViewer(const std::string& subject, const std::string& raw);
    // Queue a draft in the outbox (UltraMail's local store, which survives a
    // crash or a restart), then send it in the background - saving a copy to
    // the account's Drafts folder first, kept there until it has gone out -
    // and report the outcome. True once the message is safely in the outbox,
    // so its compose window can close; false when it was not queued (no
    // recipient, no outbox) and the draft would be lost with the window.
    // `replacesOutboxId`: the waiting message this one corrects, deleted
    // from the outbox (and from Drafts) once this one is queued.
    bool HandleSendDraft(const Draft& draft, int64_t replacesOutboxId = 0);
    // The Outbox window (toolbar "Outbox (N)"): the waiting messages, with
    // Send now, Edit and Delete. One at a time.
    void OpenOutbox();
    // The toolbar button's count (hidden when nothing waits) and the open
    // Outbox window's list, after anything that changes the outbox.
    void RefreshOutbox();
    // Delete asks first; the message and its Drafts copy go on a worker.
    void ConfirmDeleteFromOutbox(int64_t id);
    // `quiet`: no word on the outcome (Edit's replacement). A Drafts copy the
    // server cannot be reached for now is deleted by a later pass.
    void DeleteFromOutbox(int64_t id, bool quiet);
    // Runs `action` once no outbox job runs or waits (now, when idle).
    void WhenOutboxIdle(std::function<void()> action);
    // A waiting message in a compose window, to correct and send again.
    void EditFromOutbox(int64_t id);
    // Send what waits in the outbox (the Retry button's action), opening the
    // vault first. `recipients` names the message in the report ("" = the
    // outbox as a whole).
    void RetryOutbox(const std::string& fromAddr, const std::string& recipients = "");
    // "Add to contacts" / "Edit contact" from the message list's menu: the
    // contact editor for the message's sender, prefilled from the message
    // when new, loaded from the address book by address when not.
    void EditSenderContact(const MessageEnvelope& m, bool isNew);
    // File a message's sender in a section or group (adding it first when new).
    void AddSenderToContactGroup(const MessageEnvelope& m, const ContactPlace& place);
    // With the vault open: send the outbox in the background and report. When
    // nothing can be sent now (no SMTP plug-in, no outgoing server), the Drafts
    // copies are still saved and the reason is reported. Split out of
    // HandleSendDraft because unlocking is answered through a dialog.
    void SendQueued(const std::string& fromAddr, const std::string& recipients);
    // A message was not sent: a warning with Retry, saying why and where the
    // message is kept (the Drafts folder and the outbox, or the outbox only).
    void ReportNotSent(const std::string& fromAddr, const std::string& recipients,
                       const UltraNetResult& why, const Outbox::FlushStats& stats);
    // Automatic retry of the outbox: a light timer runs a silent pass when
    // OutboxRetryClock says one is due (nothing else sending, the vault open).
    void StartOutboxRetryTimer();
    void AutoRetryOutbox();
    // After a pass: the next automatic one is scheduled, or the retries end
    // when nothing waits any more.
    void NoteOutboxPass();
    int OutboxPending() const;
    // Deleted messages whose Drafts copies are still to be removed.
    int OutboxWithdrawn() const;
    // The IMAP side of the server copies for a send worker: the IMAP plug-in
    // (null without it) and, from a snapshot of the accounts taken here on the
    // UI thread, each account's server, Drafts and Sent folders and sign-in.
    ServerCopies MakeServerCopies();

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
    // Every account's check interval from the preferences (Settings > Mail >
    // New mail): the next tick of the sync timer follows it.
    void ApplyCheckMailInterval();
    // Sync the accounts the scheduler reports as due (called from the timer),
    // or every account when `force` is set (the Reload button).
    void RunSyncs(bool force);
    // A background sync could not reach the server: sync the accounts whose
    // grace period is running again after kOfflineRetrySec (see OfflineGrace).
    void ScheduleOfflineRetry();
    void RetryUnreachableAccounts();
    // The computer woke from sleep (WakeDetector): forget the offline grace
    // from before the sleep and check every account shortly after, once the
    // network has had a moment to come back.
    void OnWokeFromSleep();
    // Check every account now, as a background sync (no alerts for a network
    // that is not up yet - the offline grace applies).
    void SyncAllInBackground();
    // Sync one account now — the first sync right after it was added.
    void SyncAccount(const std::string& accountId);
    // Fetch one folder's messages now (envelopes + bodies), on a worker. Backs
    // the lazy load when a non-inbox folder is first opened and the Reload of a
    // folder other than the inbox. No-op without the IMAP plug-in / an unlocked
    // vault / known servers. `userInitiated` is Reload; opening a folder is a
    // passive refresh, so a server it cannot reach gets the same grace period
    // as a background sync instead of an alert.
    void SyncFolder(const std::string& accountId, const std::string& folder,
                    bool userInitiated);
    // The throttle in front of SyncFolder for a passive refresh (a folder
    // opened, an account switched to): skipped while that folder is being
    // fetched or was fetched less than kFolderResyncSec ago.
    void RefreshFolderSoon(const std::string& accountId, const std::string& folder);
    // The reading pane shows a message whose body is not downloaded (its
    // download failed, or the sync has not got to it): fetch it now, on a
    // worker, and show it when it arrives. Quiet - no prompt, no alert.
    void FetchMissingBody(const MessageEnvelope& env);
    std::set<std::string> bodyFetchInFlight_;   // account \n folder \n uid
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
    // The same mail.db on a connection of its own, for the worker threads
    // (sync, folder fetch, mailbox actions). A connection runs one statement
    // at a time, so with one shared connection the UI thread queued behind
    // every row a sync wrote: switching accounts mid-sync took 10-20 seconds.
    // Under WAL (LocalStore::Open) the UI's reads never wait for these writes.
    LocalStore workerStore_;
    ContactStore contacts_;
    // Sender icons (the registry's services, and other senders' websites),
    // under <cacheDir>/sender-icons. Read by the badge on the UI thread,
    // filled by the cache's own loader threads when the list asks for a row
    // it paints; the class is internally locked for exactly that.
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
    // Accounts whose failing sync has been alerted, so a broken server does not
    // raise an alert on every timer tick - per account: one shared flag used to
    // silence every other account's failures (and this account's, after
    // another's) until some sync succeeded.
    std::set<std::string> syncErrorReported_;
    // Holds back the alert for a background sync that could not reach the
    // server until the account has stayed unreachable for the grace period:
    // right after the computer starts the network is often not up yet, and
    // that first failure is a false alarm. Keyed on NowMonotonicSec().
    OfflineGrace offline_;
    bool         offlineRetryPending_ = false;
    // How soon an unreachable account is tried again while its grace period
    // runs, so mail arrives soon after the network does.
    static constexpr int64_t kOfflineRetrySec = 60;
    // The locked-vault warning, once per run of locked rounds.
    bool vaultLockReported_ = false;
    // The last sync failure per account ("Could not fetch mail for …: reason"),
    // shown on the status line whenever that account is the selected one;
    // cleared by its next successful sync.
    std::map<std::string, std::string> accountError_;
    // The status line for the selected account: its last failure if it has
    // one, else "Up to date".
    void ShowAccountStatus();
    // Outbox work (RunOutboxJob): true while a job runs on its worker.
    bool outboxFlushInFlight_ = false;
    OutboxRetryClock outboxRetry_;          // when the outbox tries again by itself
    bool outboxRetryTimerStarted_ = false;
    struct PendingOutboxJob {
        OutboxJob             job;
        std::string           status;
        std::function<void()> onDone;
    };
    std::vector<PendingOutboxJob> pendingOutboxJobs_;
    // Messages a queued DeleteFromOutbox will delete: a pass that fails to
    // send one of them does not warn about it.
    std::set<int64_t> outboxDeleting_;
    // Run once the queue is empty (WhenOutboxIdle).
    std::vector<std::function<void()>> whenOutboxIdle_;

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
    std::string attachmentDir_;   // <cache>/attachments: copies for the viewer, pruned
    std::string mailDir_;
    // The plug-in directory the registry was pointed at (for diagnostics).
    std::string pluginDir_;
    // True once the periodic sync timer runs, so StartBackgroundSync() can be
    // called again (after an account is added) without starting a second one.
    bool        syncTimerStarted_ = false;
    // Notices a wake from sleep from a short periodic timer (started with the
    // sync timer), so mail is checked right after the computer wakes.
    WakeDetector wake_;
    bool         wakeCheckPending_ = false;   // a post-wake sync is scheduled

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    // The account view root; hidden while the start page is up (no account
    // configured) and shown once the first account exists.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> accountView_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    reloadButton_;
    // A one-line status at the bottom of the account view saying what the app is
    // doing ("Checking <account>…", "Receiving messages… (N)", "Up to date").
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     linksLabel_;
    std::string                                        linksSummary_;
    std::shared_ptr<UltraCanvas::UltraCanvasBusyIndicator> busyIndicator_;
    std::shared_ptr<UltraCanvas::UltraCanvasBadge>     connectionBadge_;
    // What the last contact with each account's mail server came to, for the
    // connection pill. Wall-clock times, since they are shown to the user.
    struct ConnectionInfo {
        ConnectionState state = ConnectionState::Unknown;
        std::string     reason;         // the last failure's message
        std::time_t     lastOk = 0;     // last successful contact, 0 = none this run
        std::time_t     lastTry = 0;    // last attempt, 0 = none this run
        int             failures = 0;   // in a row, since the last success
        // How many messages the server's inbox held at the last contact (its
        // STATUS), -1 when not known: set beside what the list shows, it
        // tells mail that never reached this server from mail not fetched.
        int             serverInbox = -1;
    };
    std::map<std::string, ConnectionInfo> connection_;
    int                                                mailboxActionsInFlight_ = 0;
    // Cumulative messages streamed in during the current run of syncs (for the
    // "Receiving messages… (N)" status); reset when the last sync ends.
    int                                                statusReceived_ = 0;
    std::string     selectedAccount_;   // the account the mail view shows
    int             syncsInFlight_ = 0;
    // The accounts whose sync (SyncAccounts) is running: a check that falls
    // due while the last one has not finished - a short interval, a slow
    // server, a first download - is skipped, not run a second time beside it.
    std::set<std::string> accountSyncsInFlight_;
    StartPage       startPage_;
    AccountBar      accountBar_;
    MailView        mailView_;
    ContactsView    contactsView_;
    // One per open compose window - its window and its own view. The view is
    // a shared_ptr because the dialogs it opens hold it weakly.
    struct ComposeSession {
        std::shared_ptr<UltraCanvas::UltraCanvasWindow> window;
        std::shared_ptr<ComposeView>                    view;
        int64_t editsOutboxId = 0;   // held while this window corrects it
    };
    std::vector<ComposeSession> composers_;
    SyncScheduler   scheduler_;
    // New mail to the desktop feed (UltraMessage mail.message); fed from the
    // sync workers' progress callbacks.
    FeedPublisher   feed_;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasWindow>> viewerWindows_;
    // The Contacts window while it is open (one at a time).
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> contactsWindow_;
    // The Outbox window while it is open (one at a time), and the toolbar
    // button that opens it - shown while messages wait.
    OutboxView      outboxView_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> outboxWindow_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> outboxButton_;
    // Attachments open in the framework's media viewer (images, PDF, office
    // sheets, text, audio, video, fonts, …); one window, reused per attachment.
    std::unique_ptr<UltraCanvas::UltraCanvasMediaViewerWindow> attachmentViewer_;
};

} // namespace UltraMail
