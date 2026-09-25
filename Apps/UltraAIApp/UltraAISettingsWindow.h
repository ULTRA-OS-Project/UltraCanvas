// Apps/UltraAIApp/UltraAISettingsWindow.h
// UltraAI settings window, built like UltraFiler's: a tree of settings pages
// on the left (sections with sub pages, e.g. Services > Default providers)
// and the selected page on the right, each page with a title, a caption,
// its controls and a notes block at the foot. Changes apply to the running
// application immediately (UltraAIAppSettings::Apply) and are saved right
// away; there is nothing to confirm.
//
// Pages: Services > Default providers (which provider each service's
// "(default route)" uses — "Automatic — local first" unless one is picked),
// Services > Local & cloud (whether the default route may fall back to a
// paid cloud service when nothing local is available; off by default), and
// Accounts > Endpoints (opens the endpoint editor: providers, base URLs,
// models and API keys).
// Version: 0.1.0
// Last Modified: 2026-09-24
// Author: UltraAI Module
#pragma once

namespace UltraCanvas {
class UltraCanvasWindowBase;
}

namespace UltraAIApp {

class UltraAISettingsWindow {
public:
    // Which page the window opens on. Default opens on the start page, which
    // says what the sections hold.
    enum class Page { Default, DefaultProviders, LocalAndCloud, Endpoints };

    // Opens the settings window (or raises it, on `page`, when already open).
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     Page page = Page::Default);

    // Releases the retained widget tree. Call during app shutdown so it is
    // torn down while the application is still alive.
    static void Shutdown();
};

} // namespace UltraAIApp
