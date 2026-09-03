// Apps/UltraMail/ui/UltraMailApp.h
// The UltraMail application manager: owns the local store, the account list and
// the main window, and wires the Toolbox, the account info-tile bar and the
// account-setup wizard together. Texter-style app-composition class.
// Version: 0.4.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailToolbox.h"
#include "UltraMailInfoTileBar.h"
#include "UltraMailAccountWizard.h"
#include "UltraMailAttachmentStrip.h"
#include "UltraMailContactsView.h"
#include "UltraMailReadingView.h"
#include "UltraMailComposeWindow.h"
#include "UltraMailPassphraseDialog.h"

#include "UltraMailLocalStore.h"
#include "UltraMailMimeCodec.h"
#include "UltraMailContactStore.h"
#include "UltraMailOutbox.h"
#include "UltraMailSyncScheduler.h"
#include "UltraMailCredentialVault.h"

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"

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

    // Create the main window with the info-tile bar and the Toolbox grid.
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> CreateMainWindow();

    // Reload accounts + status and rebuild the info-tile bar and Toolbox.
    void Refresh();

private:
    void HandleAddAccount();
    void HandleWizardSubmit(const AccountDraft& draft);
    static std::string SlugFromEmail(const std::string& email);
    static std::string LocalPart(const std::string& email);

    // Show a message's attachments in the strip and wire open/save.
    void ShowAttachments(const ParsedMessage& message);
    // Materialise an attachment to the cache and open it in a MediaViewer window.
    void OpenAttachment(const Attachment& attachment);
    // Save an attachment to a location the user picks, through the framework's
    // file dialog (UltraCanvasFileLoader::SaveFileDialog).
    void SaveAttachment(const Attachment& attachment);
    // Where the Save-As dialog starts: Downloads, else home, else ".".
    static std::string DefaultSaveDirectory();
    // Demo path (ULTRAMAIL_DEMO=1): build a message with an attachment so the
    // strip and viewer can be exercised without a live sync.
    void ShowDemoAttachments();

    // Open the contact manager in its own window.
    void OpenContacts();
    // Seed a few contacts across sections (demo only).
    void SeedDemoContacts();

    // Open the three-pane reading view in its own window.
    void OpenReadingView();
    // Seed a few messages + cached .eml bodies (demo only).
    void SeedDemoMail();

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
    // Sync any accounts the scheduler reports as due (called from the timer).
    void RunDueSyncs();

    // Session-lifetime: the master password is entered once, and the derived
    // key lives only while the app runs.
    CredentialVault vault_{""};

    LocalStore store_;
    ContactStore contacts_;
    OutboxStore outbox_;
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
    Toolbox         toolbox_;
    InfoTileBar     infoBar_;
    AttachmentStrip attachmentStrip_;
    ContactsView    contactsView_;
    ReadingView     readingView_;
    ComposeView     composeView_;
    SyncScheduler   scheduler_;
    ParsedMessage   currentMessage_;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasWindow>> viewerWindows_;
};

} // namespace UltraMail
