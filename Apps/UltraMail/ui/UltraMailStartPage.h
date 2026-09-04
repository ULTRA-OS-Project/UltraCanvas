// Apps/UltraMail/ui/UltraMailStartPage.h
// The first-run start page: shown in the main window while no email account is
// configured. It holds exactly three things — the UltraMail logo, the app title
// and an "Add email account" button — and nothing else. Once an account exists
// the app hides it and shows the account view (info-tile bar + Toolbox) instead.
// Version: 0.2.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasContainer.h"

#include <functional>
#include <memory>

namespace UltraMail {

class StartPage {
public:
    // Build the page container (logo, title, button centred as a column).
    // Call once; add the result to the window.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Size the page to the window's client area so the column stays centred.
    // Call with the initial window size and again from onWindowResize.
    void Resize(float width, float height);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return page_; }

    // Fired when the "Add email account" button is clicked.
    std::function<void()> onAddAccount;

private:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
};

} // namespace UltraMail
