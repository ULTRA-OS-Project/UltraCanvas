// Apps/UltraAIApp/UltraAITranslatorDialog.cpp
// Version: 0.1.0

#include "UltraAITranslatorDialog.h"
#include "UltraAISettingsDialog.h"

#include "UltraAI.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"

#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;

namespace {

// Split a multi-line input into one entry per non-empty line (the same rule
// the service dialogs use for batched inputs).
std::vector<std::string> SplitLines(const std::string& in) {
    std::vector<std::string> out;
    std::string current;
    for (char c : in) {
        if (c == '\n') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

} // namespace

UltraAITranslatorDialog::UltraAITranslatorDialog() = default;

UltraAITranslatorDialog::~UltraAITranslatorDialog() {
    uiAlive_->store(false);
}

void UltraAITranslatorDialog::CreateTranslatorDialog() {
    using UltraCanvas::CSSLayout::Dimension;
    using UltraCanvas::CSSLayout::AlignItems;

    DialogConfig cfg;
    cfg.title      = "UltraAI — Translation";
    cfg.width      = kW;
    cfg.height     = kH;
    cfg.dialogType = DialogType::Custom;        // drop the icon/message chrome
    cfg.buttons    = DialogButtons::NoButtons;
    cfg.position   = DialogPosition::CenterParent;
    cfg.resizable  = true;
    CreateDialog(cfg);

    // Root flex column.
    layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(AlignItems::Stretch);
    SetPadding(16);

    // ===== Endpoint row: picker (grows) + Settings =====
    auto topRow = std::make_shared<UltraCanvasContainer>("tr-toprow");
    topRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Center);
    topRow->size.height = Dimension::Px(30);
    topRow->layoutItem.SetFlexShrink(0);

    endpointPicker_ = std::make_shared<UltraCanvasDropdown>("tr-endpoint", 0, 0, 0, 28);
    endpointPicker_->size.height = Dimension::Px(28);
    endpointPicker_->layoutItem.SetFlexGrow(1);
    topRow->AddChild(endpointPicker_);

    auto settingsBtn = std::make_shared<UltraCanvasButton>("tr-settings", 0, 0, 130, 28);
    settingsBtn->SetText("⚙  Settings");
    settingsBtn->size.width = Dimension::Px(130);
    settingsBtn->layoutItem.SetFlexShrink(0);
    settingsBtn->onClick = [this]() { OnOpenSettings(); };
    topRow->AddChild(settingsBtn);
    AddChild(topRow);

    // ===== Instruction line =====
    auto hint = std::make_shared<UltraCanvasLabel>(
        "tr-hint", 0, 0, 0, 18,
        "Pick an endpoint, enter one text per line, set the target language "
        "and press Run.");
    hint->size.height = Dimension::Px(18);
    hint->layoutItem.SetFlexShrink(0);
    AddChild(hint);

    // ===== Source text (grows) =====
    auto textLbl = std::make_shared<UltraCanvasLabel>(
        "tr-text-lbl", 0, 0, 0, 18, "Texts (one per line)");
    textLbl->size.height = Dimension::Px(18);
    textLbl->layoutItem.SetFlexShrink(0);
    AddChild(textLbl);

    input_ = std::make_shared<UltraCanvasTextArea>("tr-text", 0, 0, 0, 0);
    input_->SetEditingMode(TextAreaEditingMode::PlainText);
    input_->SetWordWrap(true);
    input_->SetPlaceholder("ich bin der schnelle fuchs");
    input_->layoutItem.SetFlexGrow(1);
    AddChild(input_);

    // ===== Options row: Target / Source / Formality =====
    auto opts = std::make_shared<UltraCanvasContainer>("tr-opts");
    opts->layout.SetFlexRow().SetFlexGap(16).SetFlexAlignItems(AlignItems::Stretch);
    opts->size.height = Dimension::Px(50);
    opts->layoutItem.SetFlexShrink(0);

    // A labelled column: a fixed-width flex column with the label on top; the
    // caller adds the field below it.
    auto makeColumn = [&](const std::string& id, const std::string& label,
                          long width) {
        auto col = std::make_shared<UltraCanvasContainer>(id + "-col");
        col->layout.SetFlexColumn().SetFlexGap(2)
            .SetFlexAlignItems(AlignItems::Stretch);
        col->size.width = Dimension::Px(width);
        col->layoutItem.SetFlexShrink(0);
        auto lbl = std::make_shared<UltraCanvasLabel>(id + "-lbl", 0, 0, 0, 18, label);
        lbl->size.height = Dimension::Px(18);
        lbl->layoutItem.SetFlexShrink(0);
        col->AddChild(lbl);
        return col;
    };

    auto tgtCol = makeColumn("tr-tgt", "Target (BCP-47)", 140);
    targetInput_ = std::make_shared<UltraCanvasTextInput>("tr-tgt", 0, 0, 0, 28);
    targetInput_->SetText("fr");
    targetInput_->size.height = Dimension::Px(28);
    targetInput_->layoutItem.SetFlexShrink(0);
    tgtCol->AddChild(targetInput_);
    opts->AddChild(tgtCol);

    auto srcCol = makeColumn("tr-src", "Source (empty = detect)", 220);
    sourceInput_ = std::make_shared<UltraCanvasTextInput>("tr-src", 0, 0, 0, 28);
    sourceInput_->SetPlaceholder("auto-detect");
    sourceInput_->size.height = Dimension::Px(28);
    sourceInput_->layoutItem.SetFlexShrink(0);
    srcCol->AddChild(sourceInput_);
    opts->AddChild(srcCol);

    auto fmtCol = makeColumn("tr-fmt", "Formality", 150);
    formalityPicker_ = std::make_shared<UltraCanvasDropdown>("tr-formality", 0, 0, 0, 28);
    formalityPicker_->AddItem("Default");
    formalityPicker_->AddItem("Formal");
    formalityPicker_->AddItem("Informal");
    formalityPicker_->SetSelectedIndex(0);
    formalityPicker_->size.height = Dimension::Px(28);
    formalityPicker_->layoutItem.SetFlexShrink(0);
    fmtCol->AddChild(formalityPicker_);
    opts->AddChild(fmtCol);
    AddChild(opts);

    // ===== Result (grows) =====
    auto resultLbl = std::make_shared<UltraCanvasLabel>(
        "tr-result-lbl", 0, 0, 0, 18, "Result");
    resultLbl->size.height = Dimension::Px(18);
    resultLbl->layoutItem.SetFlexShrink(0);
    AddChild(resultLbl);

    result_ = std::make_shared<UltraCanvasTextArea>("tr-result", 0, 0, 0, 0);
    result_->SetReadOnly(true);
    result_->SetCaretVisible(false);   // read-only result: no blinking caret
    result_->SetWordWrap(true);
    result_->layoutItem.SetFlexGrow(1);
    AddChild(result_);

    // ===== Pinned action row =====
    auto actionRow = std::make_shared<UltraCanvasContainer>("tr-actions");
    actionRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Center);
    actionRow->size.height = Dimension::Px(34);
    actionRow->layoutItem.SetFlexShrink(0);

    runButton_ = std::make_shared<UltraCanvasButton>("tr-run", 0, 0, 120, 30);
    runButton_->SetText("Run");
    runButton_->size.width = Dimension::Px(120);
    runButton_->layoutItem.SetFlexShrink(0);
    runButton_->onClick = [this]() { OnRun(); };
    actionRow->AddChild(runButton_);

    statusLabel_ = std::make_shared<UltraCanvasLabel>("tr-status", 0, 0, 0, 20, "");
    statusLabel_->layoutItem.SetFlexGrow(1);
    actionRow->AddChild(statusLabel_);

    auto closeBtn = std::make_shared<UltraCanvasButton>("tr-close", 0, 0, 90, 30);
    closeBtn->SetText("Close");
    closeBtn->size.width = Dimension::Px(90);
    closeBtn->layoutItem.SetFlexShrink(0);
    closeBtn->onClick = [this]() { CloseDialog(DialogResult::Close); };
    actionRow->AddChild(closeBtn);
    AddChild(actionRow);

    RebuildEndpointPicker();
}

void UltraAITranslatorDialog::RebuildEndpointPicker(const std::string& selectId) {
    translationEndpoints_ =
        EndpointStore::Instance().ForCapability(AICapability::Translation);
    if (!endpointPicker_) return;

    endpointPicker_->ClearItems();
    if (translationEndpoints_.empty()) {
        endpointPicker_->AddItem("(no translation endpoint — open Settings)");
        endpointPicker_->SetSelectedIndex(0, false);
        SetStatus("No translation endpoint configured. Click Settings to add one.");
        return;
    }
    // Keep the caller's endpoint selected (by its stable id) across a rebuild;
    // fall back to the first when it's empty or no longer in the list.
    int selectIndex = 0;
    for (size_t i = 0; i < translationEndpoints_.size(); ++i) {
        const auto& e = translationEndpoints_[i];
        endpointPicker_->AddItem((e.name.empty() ? e.id : e.name) +
                                 "   (" + e.providerId + ")");
        if (!selectId.empty() && e.id == selectId) selectIndex = static_cast<int>(i);
    }
    endpointPicker_->SetSelectedIndex(selectIndex, false);
    SetStatus("");
}

void UltraAITranslatorDialog::OnOpenSettings() {
    // Open Settings on the endpoint currently selected in the picker so it can
    // be edited directly, rather than the default "New endpoint".
    std::string selectId;
    if (endpointPicker_) {
        const int idx = endpointPicker_->GetSelectedIndex();
        if (idx >= 0 && idx < static_cast<int>(translationEndpoints_.size())) {
            selectId = translationEndpoints_[static_cast<size_t>(idx)].id;
        }
    }

    auto dlg = std::make_shared<UltraAISettingsDialog>();
    dlg->CreateSettingsDialog(selectId);
    dlg->ShowModal(this);
    // Reflect any endpoint changes, keeping the endpoint the user had selected.
    RebuildEndpointPicker(selectId);
}

void UltraAITranslatorDialog::OnRun() {
    if (running_) return;

    int idx = endpointPicker_ ? endpointPicker_->GetSelectedIndex() : -1;
    if (idx < 0 || idx >= static_cast<int>(translationEndpoints_.size())) {
        SetStatus("No translation endpoint configured. Click Settings to add one.");
        return;
    }
    const Endpoint endpoint = translationEndpoints_[static_cast<size_t>(idx)];

    TranslateRequest req;
    req.texts          = SplitLines(input_ ? input_->GetText() : "");
    req.targetLanguage = targetInput_ ? targetInput_->GetText() : "";
    req.sourceLanguage = sourceInput_ ? sourceInput_->GetText() : "";
    switch (formalityPicker_ ? formalityPicker_->GetSelectedIndex() : 0) {
        case 1:  req.formality = TranslationFormality::Formal;   break;
        case 2:  req.formality = TranslationFormality::Informal; break;
        default: req.formality = TranslationFormality::Default;  break;
    }
    if (req.texts.empty()) {
        SetStatus("Provide at least one input line."); return;
    }
    if (req.targetLanguage.empty()) {
        SetStatus("Provide a target language."); return;
    }

    // ToTextLLMConfig builds a ProviderConfig (== TranslatorConfig) from the
    // endpoint: provider id, base URL, default model and the vault key ref.
    TranslatorConfig cfg = ToTextLLMConfig(endpoint);

    running_ = true;
    if (runButton_) runButton_->SetText("Running...");
    if (result_) result_->SetText("");
    // Name the endpoint the user picked (e.g. "ai.ultraos.eu"), not its
    // provider id — the server being contacted is the endpoint, not "openai".
    const std::string epLabel = endpoint.name.empty() ? endpoint.id : endpoint.name;
    SetStatus("Contacting " + epLabel + "...");

    // A real provider takes seconds per batch; keep the window responsive.
    std::thread worker([this, alive = uiAlive_, cfg, req]() {
        std::string body;
        std::string status = "Done";

        Error createError;
        auto tr = CreateTranslator(cfg, &createError);
        if (!tr) {
            status = "Failed to create Translator";
            body   = createError.message.empty()
                ? "Could not create the translator." : createError.message;
        } else {
            auto resp = tr->Translate(req);
            std::ostringstream os;
            for (const auto& r : resp.results) {
                const std::string source = r.detectedSourceLanguage.empty()
                    ? (req.sourceLanguage.empty() ? "?" : req.sourceLanguage)
                    : r.detectedSourceLanguage;
                os << source << " → " << req.targetLanguage << "\n"
                   << r.text << "\n\n";
            }
            if (!resp.error.IsOk()) {
                const std::string msg = resp.error.message.empty()
                    ? "Translation failed." : resp.error.message;
                os << msg << "\n";
                status = "Error: " + msg;
            }
            body = os.str();
        }

        auto deliver = [this, alive, body, status]() {
            if (!alive->load()) return;      // the dialog is gone
            OnRunFinished(body, status);
        };
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (app) app->PostToUIThread(std::move(deliver));
        else deliver();
    });
    worker.detach();
}

void UltraAITranslatorDialog::OnRunFinished(const std::string& result,
                                            const std::string& status) {
    if (result_) result_->SetText(result);
    SetStatus(status);
    running_ = false;
    if (runButton_) runButton_->SetText("Run");
}

void UltraAITranslatorDialog::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

} // namespace UltraAIApp
