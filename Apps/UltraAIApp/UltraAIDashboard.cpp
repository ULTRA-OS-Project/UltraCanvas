// Apps/UltraAIApp/UltraAIDashboard.cpp
// Version: 0.1.0

#include "UltraAIDashboard.h"
#include "UltraAIDialogs.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace UltraAIApp {

using namespace UltraCanvas;

UltraAIDashboard::UltraAIDashboard(UltraCanvasApplication& app) : app_(app) {}

bool UltraAIDashboard::Create() {
    WindowConfig cfg;
    cfg.title  = "UltraAI";
    cfg.width  = kWindowWidth;
    cfg.height = kWindowHeight;
    cfg.x      = 120;
    cfg.y      = 120;
    cfg.resizable = false;
    cfg.type   = WindowType::Standard;

    window_ = std::make_shared<UltraCanvasWindow>(cfg);
    if (!window_) return false;

    window_->onWindowClosing = [this]() {
        OnWindowClosing();
        return true;
    };

    // Header label
    auto title = std::make_shared<UltraCanvasLabel>(
        "dash-title", 0, 20, 18, kWindowWidth - 40, 22,
        "UltraAI — choose a service");
    window_->AddElement(title);

    auto sub = std::make_shared<UltraCanvasLabel>(
        "dash-sub", 0, 20, 38, kWindowWidth - 40, 18,
        "All services use the in-process mock adapter (no network).");
    window_->AddElement(sub);

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
        const char* label;
        std::function<void()> open;
    };

    std::vector<Entry> entries = {
        { "💬  Chat (LLM)",       [this]() { OpenChatDialog(); } },
        { "🧮  Embeddings",        [this]() { OpenEmbeddingsDialog(); } },
        { "🎙  Speech → Text",     [this]() { OpenSpeechToTextDialog(); } },
        { "🔊  Text → Speech",     [this]() { OpenTextToSpeechDialog(); } },
        { "🌐  Translation",       [this]() { OpenTranslatorDialog(); } },
        { "🖼  Image Generation",  [this]() { OpenImageGenDialog(); } },
        { "👁  Vision Analysis",   [this]() { OpenVisionDialog(); } },
        { "🎬  Video Generation",  [this]() { OpenVideoGenDialog(); } },
        { "🎵  Music Generation",  [this]() { OpenMusicGenDialog(); } },
        { "📝  Code Assist",       [this]() { OpenCodeAssistDialog(); } },
    };

    const int cols = 5;
    for (size_t i = 0; i < entries.size(); ++i) {
        int col = static_cast<int>(i % cols);
        int row = static_cast<int>(i / cols);

        long x = kGap + col * (kButtonWidth + kGap);
        long y = kHeaderHeight + row * (kButtonHeight + kGap);

        auto btn = std::make_shared<UltraCanvasButton>(
            "svc-" + std::to_string(i), 0,
            x, y, kButtonWidth, kButtonHeight);
        btn->SetText(entries[i].label);
        btn->onClick = entries[i].open;
        window_->AddElement(btn);
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

void UltraAIDashboard::OpenChatDialog() {
    ShowServiceDialog<ChatDialog>(window_.get());
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
    ShowServiceDialog<TranslatorDialog>(window_.get());
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
