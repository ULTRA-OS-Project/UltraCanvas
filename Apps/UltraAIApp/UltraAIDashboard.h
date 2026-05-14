// Apps/UltraAIApp/UltraAIDashboard.h
// Top-level window for the UltraAI app: a grid of buttons, one per
// capability. Clicking a button opens the matching modal service
// dialog backed by the in-process mock adapter for that capability.
// Version: 0.1.0
// Last Modified: 2026-05-08
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

    // Window geometry: 5 columns x 2 rows of 170x130 buttons,
    // with 20px gaps and a 50px header. See .cpp for exact math.
    static constexpr long kWindowWidth  = 990;
    static constexpr long kWindowHeight = 460;
    static constexpr long kButtonWidth  = 170;
    static constexpr long kButtonHeight = 130;
    static constexpr long kGap          = 20;
    static constexpr long kHeaderHeight = 60;
};

} // namespace UltraAIApp
