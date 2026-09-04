// Apps/UltraMail/ui/UltraMailApp.h
// The UltraMail application manager: owns the local store, the account list and
// the main window, and wires the start page, the account bar, the mail view
// (inbox table + message details) and the account-setup wizard together.
// Texter-style app-composition class.
// Version: 0.5.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailStartPage.h"
#include "UltraMailAccountBar.h"
#include "UltraMailMailView.h"
#include "UltraMailAccountWizard.h"
#include "UltraMailContactsView.h"
#include "UltraMailComposeWindow.h"
#include "UltraMailPassphraseDialog.h"

#include "UltraMailLocalStore.h"
#include "UltraMailMimeCodec.h"
#include "UltraMailContactStore.h"
#include "UltraMailOutbox.h"
#include "UltraMailSyncScheduler.h"
#include "UltraMailCredentialVault.h"

#include <UltraCloud/UltraCloud.h>

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"

#include <functional>
#include <memory>
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
    void HandleWizardSubmit(const AccountDraft& draft);
    // "Reload email": sync every account now (when the IMAP plug-in is present)
    // and re-read the store.
    void HandleReload();
    static std::string SlugFromEmail(const std::string& email);
    static std::string LocalPart(const std::string& email);

    // Materialise an attachment to the cache and open it in a MediaViewer window.
    void OpenAttachment(const Attachment& attachment);
    // Save an attachment to a location the user picks, through the framework's
    // file dialog (UltraCanvasFileLoader::SaveFileDialog).
    void SaveAttachment(const Attachment& attachment);
    // Where the Save-As dialog starts: Downloads, else home, else ".".
    static std::string DefaultSaveDirectory();

    // Open the contact manager in its own window.
    void OpenContacts();
    // Seed a few contacts across sections (demo only).
    void SeedDemoContacts();

    // Seed a few messages + cached .eml bodies (demo only).
    void SeedDemoMail();
    // Add an in-memory demo cloud account with a few files (demo only).
    void SeedDemoCloud();

    // Open a compose window for the given draft (new / reply / forward).
    void OpenComposer(const Draft& draft);
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
    // Register accounts with the scheduler and start a periodic background sync
    // (only when the IMAP plug-in is available).
    void StartBackgroundSync();
    // Sync the accounts the scheduler reports as due (called from the timer),
    // or every account when `force` is set (the Reload button).
    void RunSyncs(bool force);

    // Session-lifetime: the master password is entered once, and the derived
    // key lives only while the app runs.
    CredentialVault vault_{""};

    LocalStore store_;
    ContactStore contacts_;
    OutboxStore outbox_;
    // Cloud storage (UltraCloud): accounts + secrets behind the composer's
    // "Attach cloud link". Per-app store for now (see the module README).
    UltraCloud::AccountStore cloudAccounts_;
    std::unique_ptr<UltraCloud::FileSecretStore> cloudSecrets_;
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

    std::string dataDir_;
    std::string cacheDir_;
    std::string mailDir_;

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
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasWindow>> viewerWindows_;
};

} // namespace UltraMail
