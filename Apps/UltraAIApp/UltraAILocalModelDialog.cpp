// Apps/UltraAIApp/UltraAILocalModelDialog.cpp
// The "Get a local model" dialog (UltraAILocalModelDialog.h).
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraAI Module

#include "UltraAILocalModelDialog.h"

#include "UltraCanvasApplication.h"
#include <cstdio>

#include <algorithm>
#include <utility>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;

namespace {

constexpr long kWidth   = 540;
constexpr long kHeight  = 352;
constexpr long kMargin  = 20;
// The modal keeps a margin of its own inside the window, and the bottom
// ~80 px are its button bar: the content stays clear of both.
constexpr long kInner   = kWidth - 2 * kMargin - 24;
constexpr long kButtonY = kHeight - 112;
constexpr float kFont   = 10.0f;

// The one open dialog, kept alive until it closes; the modal keeps the
// window, this keeps the object the callbacks point at.
std::shared_ptr<UltraAILocalModelDialog> g_open;

std::string Bytes(uint64_t bytes) {
    if (!bytes) return "size unknown";
    char buffer[32];
    if (bytes >= 1024ull * 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));   // locale-ok: shown to the user
    } else if (bytes >= 1024ull * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024));   // locale-ok: shown to the user
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f KB", static_cast<double>(bytes) / 1024.0);   // locale-ok: shown to the user
    }
    return buffer;
}

std::string FitWords(ModelFit fit) {
    switch (fit) {
        case ModelFit::Recommended: return "runs on your GPU";
        case ModelFit::RunsOnCpu:   return "runs on the CPU";
        case ModelFit::TooLarge:    return "too large for this computer";
        case ModelFit::Unknown:     return "";
    }
    return {};
}

std::shared_ptr<UltraCanvasLabel> Text(const std::string& id, long x, long y, long w, long h,
                                       const std::string& text, bool wrap) {
    auto l = std::make_shared<UltraCanvasLabel>(id, x, y, w, h);
    l->SetText(text);
    l->SetFontSize(kFont);
    l->SetTextColor(Color(40, 40, 44, 255));
    if (wrap) l->SetWrap(TextWrap::WrapWord);
    return l;
}

void OnUiThread(std::function<void()> fn) {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (app) app->PostToUIThread(std::move(fn));
    else fn();
}

} // namespace

void UltraAILocalModelDialog::Show(UltraCanvasWindowBase* parent, const std::string& serviceName,
                                   const std::string& capability, const std::string& preferredModelId,
                                   std::function<void(const std::string&)> onReady) {
    if (g_open) return;   // one at a time
    auto dlg = std::make_shared<UltraAILocalModelDialog>(serviceName, capability, preferredModelId,
                                                         std::move(onReady));
    dlg->CreateLocalModelDialog();
    g_open = dlg;
    dlg->ShowModal(parent);
}

UltraAILocalModelDialog::UltraAILocalModelDialog(std::string serviceName, std::string capability,
                                                 std::string preferredModelId,
                                                 std::function<void(const std::string&)> onReady)
    : serviceName_(std::move(serviceName)), capability_(std::move(capability)),
      preferredModelId_(std::move(preferredModelId)), onReady_(std::move(onReady)) {}

UltraAILocalModelDialog::~UltraAILocalModelDialog() {
    uiAlive_->store(false);
}

void UltraAILocalModelDialog::CreateLocalModelDialog() {
    DialogConfig cfg;
    cfg.title     = "Get a local model";
    cfg.width     = static_cast<int>(kWidth);
    cfg.height    = static_cast<int>(kHeight);
    cfg.message   = "";
    cfg.buttons   = DialogButtons::NoButtons;   // the buttons below are the dialog's own
    cfg.position  = DialogPosition::CenterParent;
    cfg.resizable = false;
    cfg.showIcon  = false;
    autoSizeHeight = false;
    CreateDialog(cfg);

    LocalRuntime& rt = LocalRuntime::Instance();
    for (const LocalModelInfo& m : rt.ListModels(capability_)) {
        if (m.fit == ModelFit::TooLarge) continue;
        if (m.installed) continue;             // an installed one would have served
        choices_.push_back(m);
    }
    // The recommendation first, then the rest in catalogue order.
    const LocalModelInfo recommended = rt.Recommend(capability_);
    std::stable_sort(choices_.begin(), choices_.end(), [&](const LocalModelInfo& a, const LocalModelInfo& b) {
        return (a.id == recommended.id) > (b.id == recommended.id);
    });

    long y = kMargin;
    auto title = Text("lm-title", kMargin, y, kInner, 22,
                      serviceName_ + " needs " + (capability_ == "embeddings" ? "an embedding" : "a language") +
                          " model on this computer.", false);
    title->SetFontWeight(FontWeight::Bold);
    title->SetFontSize(12.0f);
    AddDialogElement(title);
    y += 28;

    AddDialogElement(Text("lm-intro", kMargin, y, kInner, 34,
        "Nothing is installed yet. UltraAI can download one now; it runs on this "
        "computer and nothing you type leaves it.", true));
    y += 42;

    AddDialogElement(Text("lm-pick-lbl", kMargin, y + 4, 60, 20, "Model", false));
    picker_ = std::make_shared<UltraCanvasDropdown>("lm-picker", kMargin + 64, y, kInner - 64, 28);
    int selected = 0;
    for (size_t i = 0; i < choices_.size(); ++i) {
        const LocalModelInfo& m = choices_[i];
        std::string label = m.displayName + "  -  " + Bytes(m.sizeBytes);
        const std::string fit = FitWords(m.fit);
        if (!fit.empty()) label += ", " + fit;
        if (m.id == recommended.id) label += "  (recommended)";
        picker_->AddItem(label, m.id);
        if (m.id == preferredModelId_) selected = static_cast<int>(i);
    }
    if (choices_.empty()) picker_->AddItem("No model of the catalogue fits this computer", "");
    picker_->SetSelectedIndex(selected, false);
    picker_->onSelectionChanged = [this](int, const DropdownItem&) { OnChoiceChanged(); };
    AddDialogElement(picker_);
    y += 36;

    details_ = Text("lm-details", kMargin, y, kInner, 44, "", true);
    details_->SetTextColor(Color(96, 96, 104, 255));
    details_->SetFontSize(9.0f);
    AddDialogElement(details_);
    y += 50;

    automaticBox_ = std::make_shared<UltraCanvasCheckbox>("lm-automatic", kMargin, y, kInner, 22,
        "Download future models without asking");
    automaticBox_->SetFontSize(kFont);
    automaticBox_->SetChecked(false);
    AddDialogElement(automaticBox_);
    y += 30;

    progressLabel_ = Text("lm-progress-lbl", kMargin, y, kInner - 160, 20, "", false);
    progressLabel_->SetFontSize(9.0f);
    progressLabel_->SetTextColor(Color(96, 96, 104, 255));
    progressLabel_->SetVisible(false);
    AddDialogElement(progressLabel_);
    progressBar_ = CreateGaugeDiagramElement("lm-progress", kMargin + kInner - 150, y + 6, 150, 8);
    progressBar_->SetMode(GaugeMode::LinearBar);
    progressBar_->SetOrientation(GaugeOrientation::Horizontal);
    progressBar_->SetMinValue(0.0);
    progressBar_->SetMaxValue(100.0);
    progressBar_->SetValue(0.0);
    progressBar_->SetTitle("");
    progressBar_->SetUnit("");
    progressBar_->SetGaugeColor(Color(37, 99, 235, 255));
    progressBar_->SetVisible(false);
    AddDialogElement(progressBar_);
    y += 30;

    // Buttons, right-aligned: [Not now] [Download and continue].
    const long buttonY = kButtonY;
    downloadButton_ = std::make_shared<UltraCanvasButton>("lm-download", kMargin + kInner - 190, buttonY, 190, 30);
    downloadButton_->SetText("Download and continue");
    downloadButton_->SetFontSize(kFont);
    downloadButton_->onClick = [this]() { OnDownload(); };
    if (choices_.empty()) downloadButton_->SetDisabled(true);
    AddDialogElement(downloadButton_);
    notNowButton_ = std::make_shared<UltraCanvasButton>("lm-notnow", kMargin + kInner - 190 - 100, buttonY, 90, 30);
    notNowButton_->SetText("Not now");
    notNowButton_->SetFontSize(kFont);
    notNowButton_->onClick = [this]() { OnNotNow(); };
    AddDialogElement(notNowButton_);

    OnChoiceChanged();
}

void UltraAILocalModelDialog::OnChoiceChanged() {
    if (!details_ || !picker_) return;
    const int index = picker_->GetSelectedIndex();
    if (index < 0 || index >= static_cast<int>(choices_.size())) {
        details_->SetText(choices_.empty()
            ? "Add a GGUF of your own, or a Hugging Face file, in Settings > Services > Local models."
            : "");
        return;
    }
    const LocalModelInfo& m = choices_[static_cast<size_t>(index)];
    EngineBundleInfo bundle;
    std::string engine;
    if (LocalRuntime::Instance().ChooseBundle(m.engineId, bundle) && !bundle.installed) {
        engine = " Also downloads the " + m.engineId + " engine (" + Bytes(bundle.sizeBytes) + ").";
    }
    details_->SetText((m.license.empty() ? "" : "Licence: " + m.license + ". ") +
                      (m.description.empty() ? "" : m.description + " ") + engine);
    details_->RequestRedraw();
}

void UltraAILocalModelDialog::ShowProgress(const InstallProgress& p) {
    std::string text = p.what;
    switch (p.state) {
        case InstallProgress::State::Queued:      text += ": waiting"; break;
        case InstallProgress::State::Downloading:
            text += ": downloading";
            if (p.bytesTotal) {
                const int percent = static_cast<int>(p.bytesDone * 100 / p.bytesTotal);
                text += " " + std::to_string(percent) + "%";
                if (progressBar_) progressBar_->SetValue(percent);
            }
            break;
        case InstallProgress::State::Verifying: text += ": verifying"; if (progressBar_) progressBar_->SetValue(100); break;
        case InstallProgress::State::Unpacking: text += ": unpacking"; break;
        default: break;
    }
    if (progressLabel_) {
        progressLabel_->SetText(text);
        progressLabel_->SetVisible(true);
        progressLabel_->RequestRedraw();
    }
    if (progressBar_) {
        progressBar_->SetVisible(true);
        progressBar_->RequestRedraw();
    }
}

void UltraAILocalModelDialog::OnDownload() {
    if (!picker_ || !activeJobId_.empty()) return;
    const int index = picker_->GetSelectedIndex();
    if (index < 0 || index >= static_cast<int>(choices_.size())) return;
    const std::string modelId = choices_[static_cast<size_t>(index)].id;

    LocalRuntime& rt = LocalRuntime::Instance();
    if (automaticBox_ && automaticBox_->IsChecked()) {
        LocalRuntimeSettings s = rt.GetSettings();
        s.policy = DownloadPolicy::Automatic;
        rt.SetSettings(s);
    }
    if (downloadButton_) { downloadButton_->SetDisabled(true); downloadButton_->SetText("Downloading..."); }
    if (notNowButton_) notNowButton_->SetText("Cancel");
    if (picker_) picker_->SetDisabled(true);

    activeJobId_ = rt.InstallModel(modelId, [this, alive = uiAlive_, modelId](const InstallProgress& p) {
        OnUiThread([this, alive, modelId, p]() {
            if (!alive->load()) return;
            switch (p.state) {
                case InstallProgress::State::Done: {
                    activeJobId_.clear();
                    auto ready = onReady_;
                    PerformClose();
                    if (ready) ready(modelId);
                    break;
                }
                case InstallProgress::State::Failed:
                    activeJobId_.clear();
                    if (progressLabel_) {
                        progressLabel_->SetText("Failed: " + p.error);
                        progressLabel_->SetVisible(true);
                        progressLabel_->RequestRedraw();
                    }
                    if (progressBar_) progressBar_->SetVisible(false);
                    if (downloadButton_) { downloadButton_->SetDisabled(false); downloadButton_->SetText("Try again"); }
                    if (notNowButton_) notNowButton_->SetText("Not now");
                    if (picker_) picker_->SetDisabled(false);
                    break;
                case InstallProgress::State::Cancelled:
                    activeJobId_.clear();
                    PerformClose();
                    break;
                default:
                    ShowProgress(p);
                    break;
            }
        });
    });
}

void UltraAILocalModelDialog::OnNotNow() {
    if (!activeJobId_.empty()) {
        LocalRuntime::Instance().Cancel(activeJobId_);   // the Cancelled event closes the dialog
        if (progressLabel_) progressLabel_->SetText("Cancelling...");
        return;
    }
    PerformClose();
}

void UltraAILocalModelDialog::PerformClose() {
    uiAlive_->store(false);
    UltraCanvasModalDialog::PerformClose();
    // Let go of the object once this call has returned to the event loop.
    OnUiThread([]() { g_open.reset(); });
}

} // namespace UltraAIApp
