// Apps/UltraAIApp/UltraAIDashboard.cpp
// Version: 0.2.0
// Last Modified: 2026-10-16

#include "UltraAIDashboard.h"
#include "UltraAIDialogs.h"
#include "UltraAIChatDialog.h"
#include "UltraAILocalModelDialog.h"
#include "UltraAITranslatorDialog.h"
#include "UltraAISettingsWindow.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

// ULTRAAI_APP_VERSION comes from the build alone: CMake reads the first line of
// Docs/UltraAI/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it as a
// compile definition. No fallback, so a build that lost it fails instead of
// showing a wrong number in the window title.
#ifndef ULTRAAI_APP_VERSION
#error "ULTRAAI_APP_VERSION is not defined: build through CMake, which reads it from Docs/UltraAI/CHANGELOG.md"
#endif

namespace UltraAIApp {

using namespace UltraCanvas;

namespace {

// The dashboard's icon set: 128 x 128 PNGs under media/icons/ultraai/ (the
// SVG sources and their generator sit beside them).
std::string IconPath(const std::string& name) {
    return NormalizePath(GetResourcesDir() + "media/icons/ultraai/" + name + ".png");
}

} // namespace

UltraAIDashboard::UltraAIDashboard(UltraCanvasApplication& app) : app_(app) {}

bool UltraAIDashboard::Create() {
    WindowConfig cfg;
    cfg.title  = "UltraAI " ULTRAAI_APP_VERSION;
    cfg.width  = kWindowWidth;
    cfg.height = kWindowHeight;
    cfg.x      = 120;
    cfg.y      = 120;
    cfg.resizable = false;
    cfg.type   = WindowType::Standard;

    window_ = CreateWindow(cfg);
    if (!window_) return false;

    window_->onWindowClosing = [this]() {
        OnWindowClosing();
        return true;
    };

    // Header label
    auto title = std::make_shared<UltraCanvasLabel>(
        "dash-title", 20, 18, kWindowWidth - 40, 22,
        "UltraAI — choose a service");
    window_->AddChild(title);

    auto sub = std::make_shared<UltraCanvasLabel>(
        "dash-sub", 20, 38, kWindowWidth - 240, 18,
        "Services run locally by default — choose providers, cloud "
        "fallback and endpoints in Settings.");
    window_->AddChild(sub);

    // Settings button (top-right of the header): opens the settings window.
    auto settingsBtn = std::make_shared<UltraCanvasButton>(
        "dash-settings", kWindowWidth - 20 - 140, 14, 140, 34);
    settingsBtn->SetText("Settings");
    settingsBtn->SetIcon(IconPath("settings"));
    settingsBtn->SetIconSize(24, 24);
    settingsBtn->SetIconSpacing(8);
    settingsBtn->onClick = [this]() { OpenSettingsDialog(); };
    window_->AddChild(settingsBtn);

    CreateButtons();
    return true;
}

void UltraAIDashboard::Show() {
    if (window_) window_->Show();
}

void UltraAIDashboard::OnWindowClosing() {
    app_.RequestExit();
}

void UltraAIDashboard::CreateButtons() {
    struct Entry {
        const char* icon;      // file stem under media/icons/ultraai/
        const char* label;
        const char* tooltip;
        std::function<void()> open;
    };

    std::vector<Entry> entries = {
        { "chat",             "Chat (LLM)",       "Talk to a language model",
          [this]() { OpenChatDialog(); } },
        { "embeddings",       "Embeddings",       "Turn text into vectors for search and similarity",
          [this]() { OpenEmbeddingsDialog(); } },
        { "speech-to-text",   "Speech → Text",    "Transcribe recorded speech",
          [this]() { OpenSpeechToTextDialog(); } },
        { "text-to-speech",   "Text → Speech",    "Read text aloud",
          [this]() { OpenTextToSpeechDialog(); } },
        { "translation",      "Translation",      "Translate text between languages",
          [this]() { OpenTranslatorDialog(); } },
        { "image-generation", "Image Generation", "Create an image from a description",
          [this]() { OpenImageGenDialog(); } },
        { "vision",           "Vision Analysis",  "Describe and analyse an image",
          [this]() { OpenVisionDialog(); } },
        { "video-generation", "Video Generation", "Create a video clip from a description",
          [this]() { OpenVideoGenDialog(); } },
        { "music-generation", "Music Generation", "Compose music from a description",
          [this]() { OpenMusicGenDialog(); } },
        { "code-assist",      "Code Assist",      "Write, explain and fix code",
          [this]() { OpenCodeAssistDialog(); } },
    };

    const int cols = 5;
    for (size_t i = 0; i < entries.size(); ++i) {
        int col = static_cast<int>(i % cols);
        int row = static_cast<int>(i / cols);

        long x = kGap + col * (kButtonWidth + kGap);
        long y = kHeaderHeight + row * (kButtonHeight + kGap);

        // A tile: the 128 px icon on top, the service name below it.
        auto btn = std::make_shared<UltraCanvasButton>(
            "svc-" + std::to_string(i),
            x, y, kButtonWidth, kButtonHeight);
        btn->SetText(entries[i].label);
        btn->SetIcon(IconPath(entries[i].icon));
        btn->SetIconSize(kIconSize, kIconSize);
        btn->SetIconPosition(ButtonIconPosition::Top);
        btn->SetIconSpacing(8);
        btn->SetPadding(12, 8, 8, 8);
        ButtonStyle style = btn->GetStyle();
        style.cornerRadius = 14;
        style.fontSize = 13;
        btn->SetStyle(style);
        btn->SetTooltip(entries[i].tooltip);
        btn->onClick = entries[i].open;
        window_->AddChild(btn);
    }
}

// ===== Dialog openers =====

namespace {

template <typename DialogT, typename... Args>
void ShowServiceDialog(UltraCanvas::UltraCanvasWindow* parent, Args&&... args) {
    auto dlg = std::make_shared<DialogT>(std::forward<Args>(args)...);
    dlg->CreateServiceDialog();
    dlg->ShowModal(parent);
}

} // namespace

void UltraAIDashboard::OpenSettingsDialog(UltraAISettingsWindow::Page page) {
    UltraAISettingsWindow::Show(window_.get(), page);
}

void UltraAIDashboard::OpenLocalModelDialog() {
    UltraAILocalModelDialog::Show(window_.get(), "Chat", "textllm", "", nullptr);
}

void UltraAIDashboard::OpenChatDialog() {
    auto dlg = std::make_shared<UltraAIChatDialog>();
    dlg->CreateChatDialog();
    dlg->ShowModal(window_.get());
}
void UltraAIDashboard::OpenEmbeddingsDialog() {
    ShowServiceDialog<EmbeddingsDialog>(window_.get());
}
void UltraAIDashboard::OpenSpeechToTextDialog() {
    ShowServiceDialog<SpeechToTextDialog>(window_.get());
}
void UltraAIDashboard::OpenTextToSpeechDialog() {
    ShowServiceDialog<TextToSpeechDialog>(window_.get());
}
void UltraAIDashboard::OpenImageGenDialog() {
    ShowServiceDialog<ImageGenDialog>(window_.get());
}
void UltraAIDashboard::OpenVisionDialog() {
    ShowServiceDialog<VisionDialog>(window_.get());
}
void UltraAIDashboard::OpenTranslatorDialog() {
    auto dlg = std::make_shared<UltraAITranslatorDialog>();
    dlg->CreateTranslatorDialog();
    dlg->ShowModal(window_.get());
}
void UltraAIDashboard::OpenVideoGenDialog() {
    ShowServiceDialog<VideoGenDialog>(window_.get());
}
void UltraAIDashboard::OpenMusicGenDialog() {
    ShowServiceDialog<MusicGenDialog>(window_.get());
}
void UltraAIDashboard::OpenCodeAssistDialog() {
    ShowServiceDialog<CodeAssistDialog>(window_.get());
}

} // namespace UltraAIApp
