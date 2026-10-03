// Apps/UltraPassword/ui/StartPage.h
// The page the main window shows while no vault is open. Built the way
// UltraMail's first-run start page is (UltraMailStartPage.h): the app logo,
// the app name and the call to action, centred in a column that follows the
// window.
//
// Two modes:
//  - Create: there is no vault yet. One primary "Create password vault"
//    button opens the setup wizard (CreateVaultWizard.h); a quiet second
//    button opens a vault file that already exists elsewhere (an export
//    copied from another computer).
//  - Unlock: a vault exists. The same column holds the master-password field
//    and an Unlock button instead, so the first thing on screen after a lock
//    is the same page as after a launch. Nothing about the vault's contents
//    is shown here — not even how many passwords it holds.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraPassword {

class StartPage {
public:
    enum class Mode { Create, Unlock };

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();
    void Resize(float width, float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return page_; }

    // Unlock mode shows `detail` under the title ("Locked after 5 minutes
    // without input.", the vault's file name).
    void SetMode(Mode mode, const std::string& detail = "");
    Mode GetMode() const { return mode_; }

    // A red line under the password field; empty hides it.
    void SetError(const std::string& text);
    // Greys the Unlock button and shows `text` on it while a wait or the key
    // derivation runs; empty restores it.
    void SetBusy(const std::string& text);
    // Puts the caret in the password field.
    void FocusPassword();

    // Fired when "Create password vault" is clicked.
    std::function<void()> onCreate;
    // Fired when "Open existing vault…" is clicked (both modes).
    std::function<void()> onOpenOther;
    // Fired with the typed master password. The page clears its field after.
    std::function<void(const std::string& password)> onUnlock;

private:
    void SubmitPassword();

    Mode mode_ = Mode::Create;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     subtitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    createButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> unlockRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> password_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    unlockButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     error_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    openOther_;
};

} // namespace UltraPassword
