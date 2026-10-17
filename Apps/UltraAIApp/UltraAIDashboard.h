// Apps/UltraAIApp/UltraAIDashboard.h
// Top-level window for the UltraAI app: a grid of buttons, one per
// capability. Clicking a button opens the matching modal service
// dialog backed by the in-process mock adapter for that capability.
// Version: 0.2.0
// Last Modified: 2026-10-16
// Author: UltraAI Module
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"

#include <memory>

namespace UltraAIApp {

class UltraAIDashboard {
public:
    explicit UltraAIDashboard(UltraCanvas::UltraCanvasApplication& app);

    // Build the window and its grid of icon-buttons.
    bool Create();
    void Show();

    // Cleanly request the application to exit.
    void OnWindowClosing();

private:
    void CreateButtons();

    // Open the settings window (default providers, cloud fallback, endpoints).
    void OpenSettingsDialog();

    // Open a modal dialog for the chosen capability.
    void OpenChatDialog();
    void OpenEmbeddingsDialog();
    void OpenSpeechToTextDialog();
    void OpenTextToSpeechDialog();
    void OpenImageGenDialog();
    void OpenVisionDialog();
    void OpenTranslatorDialog();
    void OpenVideoGenDialog();
    void OpenMusicGenDialog();
    void OpenCodeAssistDialog();

    UltraCanvas::UltraCanvasApplication&        app_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;

    // Window geometry: 5 columns x 2 rows of 176x184 tiles (a 128 px icon
    // over the service name), with 20px gaps and a 60px header.
    static constexpr long kButtonWidth  = 176;
    static constexpr long kButtonHeight = 184;
    static constexpr long kGap          = 20;
    static constexpr long kHeaderHeight = 60;
    static constexpr int  kIconSize     = 128;
    static constexpr long kWindowWidth  = kGap + 5 * (kButtonWidth + kGap);
    static constexpr long kWindowHeight = kHeaderHeight + 2 * (kButtonHeight + kGap);
};

} // namespace UltraAIApp
