// Apps/UltraPassword/ui/PasswordApp.h
// The main window and everything it does.
//
// Two views share the window, as in UltraMail:
//  - the start page (StartPage.h) while no vault is open: "Create password
//    vault" on first run, the master-password field once a vault exists;
//  - the vault view once it is unlocked: a toolbar, then a split pane with
//    the group tree on the left and the password cards of the selected group
//    (or the search results) on the right, and a status line.
//
// The tree shows the vault's name at the root, its groups (which nest), and
// under each group its passwords. Selecting a group lists its passwords as
// cards; selecting a password shows its card alone.
//
// Every change is saved at once (VaultFile::Save — a fresh nonce, an atomic
// rename). There is no "unsaved" state to lose.
//
// Locking: the Lock button, five minutes without input to this window, and
// minimising all wipe the decrypted vault and the derived key from memory and
// return to the start page in unlock mode. A password copied to the
// clipboard is cleared from it 30 seconds later, if it is still there.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartPage.h"
#include "core/PasswordVault.h"
#include "core/VaultFile.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>

namespace UltraPassword {

class PasswordApp {
public:
    PasswordApp(UltraCanvas::UltraCanvasApplication& app, const std::string& vaultPath);
    ~PasswordApp();

    PasswordApp(const PasswordApp&) = delete;
    PasswordApp& operator=(const PasswordApp&) = delete;

    bool Create();
    void Show();

private:
    // ----- views -----
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildVaultView();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolbar();
    void ResizeViews(float width, float height);
    void ShowStartPage(StartPage::Mode mode, const std::string& detail = "");
    void ShowVaultView();

    // ----- vault lifecycle -----
    void BeginCreateVault();
    void CreateVault(const std::string& name, const std::string& password, KdfProfile profile);
    void Unlock(const std::string& password);
    void OpenOtherVault();
    void Lock(const std::string& reason);
    bool SaveVault();

    // ----- tree and cards -----
    void RebuildTree();
    void AddGroupNodes(const std::string& parentGroupId, const std::string& parentNodeId);
    void OnTreeSelection(UltraCanvas::TreeNode* node);
    void RebuildCards();
    void SetSearch(const std::string& text);

    // ----- actions -----
    void AddEntry();
    void EditEntry(const std::string& entryId);
    void DeleteEntry(const std::string& entryId);
    void AddGroup();
    void RenameGroup();
    void DeleteGroup();
    void Export();
    void ExportCopy();
    void ExportWithPassword();
    void ExportCsv();
    void Import();
    void ImportVaultFile(const std::string& path);
    void ImportCsvFile(const std::string& path);
    void ChangeMasterPassword();
    void CopyToClipboard(const std::string& text, const std::string& what);
    void OpenWebsite(const std::string& url);

    // ----- housekeeping -----
    void Tick();
    void NoteActivity();
    void ClearClipboardIfOurs();
    void SetStatus(const std::string& text, bool isError = false);
    void Alert(const std::string& message, bool isError = true);
    // Runs `work` after the window has painted once, so a "Unlocking…" or
    // "Creating…" state is visible during the Argon2id derivation.
    void RunAfterPaint(std::function<void()> work);

    UltraCanvas::UltraCanvasApplication& app_;
    std::string   vaultPath_;
    VaultFile     file_;
    PasswordVault vault_;
    UnlockThrottle throttle_;
    bool          unlocked_ = false;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow>    window_;
    StartPage                                          startPage_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> vaultView_;
    std::shared_ptr<UltraCanvas::UltraCanvasTreeView>  tree_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> cardsPane_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     paneTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     paneSummary_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> searchInput_;

    std::string           selectedGroup_ = kRootGroupId;
    std::string           selectedEntry_;
    std::string           searchText_;
    std::set<std::string> expandedNodes_;
    bool                  rebuildingTree_ = false;

    UltraCanvas::TimerId tickTimer_ = 0;
    bool                 tickRunning_ = false;
    int64_t              lastActivity_ = 0;
    int64_t              clipboardClearAt_ = 0;
    std::string          clipboardText_;   // what we put there, to clear only that
    bool                 lockPending_ = false;   // minimised: lock on the next tick
    bool                 showingWait_ = false;   // the lock page shows a back-off countdown

    static constexpr int     kWindowWidth       = 1140;
    static constexpr int     kWindowHeight      = 700;
    static constexpr int     kTreeWidth         = 270;
    static constexpr int64_t kAutoLockSeconds   = 5 * 60;
    static constexpr int64_t kClipboardSeconds  = 30;
};

} // namespace UltraPassword
