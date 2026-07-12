// Apps/UltraAIApp/UltraAIServiceDialog.cpp
// Version: 0.1.1
// Last Modified: 2026-07-12

#include "UltraAIServiceDialog.h"
#include "UltraCanvasButton.h"

namespace UltraAIApp {

using namespace UltraCanvas;

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
    auto runBtn = std::make_shared<UltraCanvasButton>(
        "svc-run", kMargin, actionRowY, 120, 30);
    runBtn->SetText("Run");
    runBtn->onClick = [this]() { RunCapability(); };
    AddDialogElement(runBtn);

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

} // namespace UltraAIApp
