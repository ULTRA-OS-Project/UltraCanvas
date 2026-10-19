// Apps/UltraSocial/ui/UltraSocialStartPage.h
// The first-run start page: shown in the main window while no social account
// is connected. UltraMail's start page, for UltraSocial: the app's logo, its
// name, one line on what it does and an "Add social account" button, centred,
// with the networks it can post to underneath. Once an account exists the app
// hides it and shows the compose view instead.
// Version: 0.1.0 - UltraMail's start page for UltraSocial
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasContainer.h"

#include <functional>
#include <memory>

namespace UltraSocial {

class StartPage {
public:
    // Build the page container (logo, title, tagline, button centred as a
    // column). Call once; add the result to the window.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Size the page to the window's client area so the column stays centred.
    // Call with the initial window size and again from onWindowResize.
    void Resize(float width, float height);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return page_; }

    // Fired when the "Add social account" button is clicked.
    std::function<void()> onAddAccount;

private:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
};

} // namespace UltraSocial
