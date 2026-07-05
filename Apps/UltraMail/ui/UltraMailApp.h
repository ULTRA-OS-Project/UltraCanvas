// Apps/UltraMail/ui/UltraMailApp.h
// The UltraMail application manager: owns the local store, the account list and
// the main window, and wires the Toolbox, the account info-tile bar and the
// account-setup wizard together. Texter-style app-composition class.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailToolbox.h"
#include "UltraMailInfoTileBar.h"
#include "UltraMailAccountWizard.h"
#include "UltraMailAttachmentStrip.h"
#include "UltraMailContactsView.h"
#include "UltraMailReadingView.h"
#include "UltraMailComposeWindow.h"

#include "UltraMailLocalStore.h"
#include "UltraMailMimeCodec.h"
#include "UltraMailContactStore.h"
#include "UltraMailOutbox.h"

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraMail {

class UltraMailApp {
public:
    // Open the local store under `dataDir` (created if absent) and load the
    // account list. Returns false if the store cannot be opened.
    bool Initialize(const std::string& dataDir);

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
    // Save an attachment (Phase 2: writes to the cache dir; a Save-As dialog
    // follows once the file-dialog wiring lands).
    void SaveAttachment(const Attachment& attachment);
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

    LocalStore store_;
    ContactStore contacts_;
    OutboxStore outbox_;
    std::vector<Account> accounts_;
    std::vector<AccountStatus> status_;
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
    ParsedMessage   currentMessage_;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasWindow>> viewerWindows_;
};

} // namespace UltraMail
