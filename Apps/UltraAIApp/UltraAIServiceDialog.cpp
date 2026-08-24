// Apps/UltraAIApp/UltraAIServiceDialog.cpp
// Version: 0.1.1
// Last Modified: 2026-07-12

#include "UltraAIServiceDialog.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"

#include <exception>
#include <thread>
#include <utility>

namespace UltraAIApp {

using namespace UltraCanvas;

UltraAIServiceDialog::~UltraAIServiceDialog() {
    // A run may still be in flight; cancel its delivery rather than let it
    // call into a destroyed dialog.
    uiAlive_->store(false);
}

void UltraAIServiceDialog::RunOffThread(std::function<RunOutcome()> work) {
    if (running_ || !work) return;
    running_ = true;
    SetStatus("Running...");
    SetResult("");
    if (runButton_) runButton_->SetText("Running...");

    std::thread worker([this, alive = uiAlive_, work = std::move(work)]() {
        RunOutcome outcome;
        try {
            outcome = work();
        } catch (const std::exception& e) {
            outcome.status = "Failed";
            outcome.result = std::string("Uncaught exception: ") + e.what();
        } catch (...) {
            outcome.status = "Failed";
            outcome.result = "Uncaught exception";
        }

        auto deliver = [this, alive, outcome = std::move(outcome)]() {
            if (!alive->load()) return;      // the dialog is gone
            SetResult(outcome.result);
            SetStatus(outcome.status);
            if (runButton_) runButton_->SetText("Run");
            running_ = false;
        };

        // Widgets are not thread-safe, so the delivery goes through the
        // event loop. Without an application (headless use) there is no loop
        // to marshal through, and nothing racing us either.
        auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
        if (app) {
            app->PostToUIThread(std::move(deliver));
        } else {
            deliver();
        }
    });
    worker.detach();
}

UltraAIServiceDialog::UltraAIServiceDialog(std::string serviceName,
                                           std::string description)
    : serviceName_(std::move(serviceName)),
      description_(std::move(description)) {}

void UltraAIServiceDialog::CreateServiceDialog() {
    DialogConfig cfg;
    cfg.title    = "UltraAI — " + serviceName_;
    cfg.width    = kDialogWidth;
    cfg.height   = kDialogHeight;
    cfg.message  = "";
    cfg.buttons  = DialogButtons::NoButtons;     // we add our own footer
    cfg.position = DialogPosition::CenterParent;
    cfg.resizable = false;
    // The form is absolutely laid out against kDialogWidth/kDialogHeight;
    // the modal's fit-height-to-message pass would collapse the empty-message
    // dialog to its minimum, clipping the form (same opt-out as the input
    // and file dialogs).
    autoSizeHeight = false;
    CreateDialog(cfg);

    // ===== Header: bold service name + description =====
    auto title = MakeLabel("svc-title",
                           kMargin, kMargin,
                           kDialogWidth - 2 * kMargin, 24,
                           serviceName_);
    AddDialogElement(title);

    auto desc = MakeLabel("svc-desc",
                          kMargin, kMargin + 28,
                          kDialogWidth - 2 * kMargin, 36,
                          description_);
    AddDialogElement(desc);

    // ===== Subclass-supplied form =====
    long formBottom = BuildForm(kFormTop);

    // ===== Action row: Run + Status =====
    long actionRowY = formBottom + 8;
    runButton_ = std::make_shared<UltraCanvasButton>(
        "svc-run", kMargin, actionRowY, 120, 30);
    runButton_->SetText("Run");
    runButton_->onClick = [this]() { RunCapability(); };
    AddDialogElement(runButton_);

    statusLabel_ = MakeLabel("svc-status",
                             kMargin + 140, actionRowY + 6,
                             kDialogWidth - kMargin * 2 - 140, 20,
                             "");
    AddDialogElement(statusLabel_);

    // ===== Result area =====
    long resultY = actionRowY + 44;
    auto resultLbl = MakeLabel("svc-result-lbl",
                               kMargin, resultY,
                               kDialogWidth - 2 * kMargin, 20,
                               "Result");
    AddDialogElement(resultLbl);

    long resultH = kDialogHeight - resultY - 80;
    resultArea_ = std::make_shared<UltraCanvasTextInput>(
        "svc-result",
        kMargin, resultY + 22,
        kDialogWidth - 2 * kMargin, resultH);
    resultArea_->SetInputType(TextInputType::Multiline);
    resultArea_->SetText("");
    AddDialogElement(resultArea_);

    // ===== Footer: Close button =====
    auto closeBtn = std::make_shared<UltraCanvasButton>(
        "svc-close",
        kDialogWidth - kMargin - 100, kDialogHeight - 56, 100, 30);
    closeBtn->SetText("Close");
    closeBtn->onClick = [this]() { CloseDialog(DialogResult::Close); };
    AddDialogElement(closeBtn);
}

void UltraAIServiceDialog::SetResult(const std::string& text) {
    if (resultArea_) resultArea_->SetText(text);
}

void UltraAIServiceDialog::AppendResult(const std::string& text) {
    if (!resultArea_) return;
    resultArea_->SetText(resultArea_->GetText() + text);
}

void UltraAIServiceDialog::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

std::shared_ptr<UltraCanvasLabel> UltraAIServiceDialog::MakeLabel(
    const std::string& id, long x, long y, long w, long h,
    const std::string& text) {
    return std::make_shared<UltraCanvasLabel>(id, x, y, w, h, text);
}

std::shared_ptr<UltraCanvasTextInput> UltraAIServiceDialog::MakeInput(
    const std::string& id, long x, long y, long w, long h,
    const std::string& placeholder, bool multiline) {
    auto in = std::make_shared<UltraCanvasTextInput>(id, x, y, w, h);
    in->SetPlaceholder(placeholder);
    if (multiline) in->SetInputType(TextInputType::Multiline);
    return in;
}

void UltraAIServiceDialog::AddProviderAndModelRow(
    long& y, const std::string& idPrefix,
    const std::vector<std::string>& providers,
    const std::string& modelLabel, const std::string& modelPlaceholder,
    std::shared_ptr<UltraCanvasTextInput>& outModel) {
    constexpr long kPickerWidth = 240;
    const long formWidth = kDialogWidth - 2 * kMargin;

    AddDialogElement(MakeLabel(idPrefix + "-prov-lbl", kMargin, y,
                               kPickerWidth, 18, "Provider"));
    AddDialogElement(MakeLabel(idPrefix + "-model-lbl",
                               kMargin + kPickerWidth + 20, y,
                               formWidth - kPickerWidth - 20, 18, modelLabel));
    y += 18 + 2;

    providerDropdown_ = CreateDropdown(idPrefix + "-provider", kMargin, y,
                                       kPickerWidth, 28);
    providerDropdown_->AddItem(kDefaultRouteLabel);
    for (const auto& id : providers) {
        providerDropdown_->AddItem(id);
    }
    providerDropdown_->SetSelectedIndex(0);
    AddDialogElement(providerDropdown_);

    outModel = MakeInput(idPrefix + "-model", kMargin + kPickerWidth + 20, y,
                         formWidth - kPickerWidth - 20, 28, modelPlaceholder);
    AddDialogElement(outModel);
    y += 28 + 6;
}

void UltraAIServiceDialog::AddLabelledInput(
    long& y, const std::string& id, const std::string& label,
    const std::string& placeholder,
    std::shared_ptr<UltraCanvasTextInput>& outInput) {
    const long formWidth = kDialogWidth - 2 * kMargin;
    AddDialogElement(MakeLabel(id + "-lbl", kMargin, y, formWidth, 18, label));
    y += 18 + 2;
    outInput = MakeInput(id, kMargin, y, formWidth, 28, placeholder);
    AddDialogElement(outInput);
    y += 28 + 6;
}

void UltraAIServiceDialog::AddProviderPicker(
    long& y, const std::vector<std::string>& providers) {
    AddDialogElement(MakeLabel("svc-prov-lbl", kMargin, y, 240, 18,
                               "Provider"));
    y += 18 + 2;
    providerDropdown_ = CreateDropdown("svc-provider", kMargin, y, 240, 28);
    providerDropdown_->AddItem(kDefaultRouteLabel);
    for (const auto& id : providers) {
        providerDropdown_->AddItem(id);
    }
    providerDropdown_->SetSelectedIndex(0);
    AddDialogElement(providerDropdown_);
    y += 28 + 6;
}

std::string UltraAIServiceDialog::SelectedProviderId() const {
    if (!providerDropdown_) return {};
    if (const auto* item = providerDropdown_->GetSelectedItem()) {
        if (item->text != kDefaultRouteLabel) return item->text;
    }
    return {};
}

} // namespace UltraAIApp
