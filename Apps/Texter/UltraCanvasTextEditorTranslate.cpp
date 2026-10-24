// Apps/Texter/UltraCanvasTextEditorTranslate.cpp
// Edit > Translate Selection...: the selected text, translated through
// UltraAI - a model on this computer first (the local runtime's), a cloud
// service only when the user set one up - and put back in place of the
// selection. The first time nothing is installed, the module's "Get a local
// model" dialog offers the download and the translation runs once it is
// done. A build without UltraAI says so instead.
// Version: 1.0.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#ifdef TEXTER_HAS_ULTRAAI
#include "UltraAILocalRuntime.h"
#include "UltraAILocalModelDialog.h"
#include "UltraAITranslator.h"
#include <thread>
#endif

#include "UltraCanvasTextEditor.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasModalDialog.h"

#include <memory>
#include <string>

namespace UltraCanvas {

#ifndef TEXTER_HAS_ULTRAAI

void UltraCanvasTextEditor::OnEditTranslateSelection() {
    UltraCanvasDialogManager::ShowInformation(
        "This build of UltraTexter has no UltraAI module to translate with.",
        "Translate Selection", nullptr, GetWindow());
}

void UltraCanvasTextEditor::TranslateSelection(const std::string&, const std::string&) {}

#else

void UltraCanvasTextEditor::OnEditTranslateSelection() {
    std::string selected;
    if (UltraCanvasRichTextEdit* rich = GetActiveRichEdit()) {
        selected = rich->GetSelectedText();
    } else if (auto doc = GetActiveDocument(); doc && doc->textArea) {
        selected = doc->textArea->GetSelectedText();
    }
    if (selected.empty()) {
        UltraCanvasDialogManager::ShowInformation(
            "Select the text to translate first.", "Translate Selection", nullptr, GetWindow());
        return;
    }
    UltraCanvasDialogManager::ShowInputDialog(
        "Translate into (a language code such as en, de, fr or pt-BR):",
        "Translate Selection", lastTranslateLanguage, InputType::Text,
        [this, selected](DialogResult result, const std::string& language) {
            if (result != DialogResult::OK || language.empty()) return;
            lastTranslateLanguage = language;
            TranslateSelection(selected, language);
        },
        GetWindow());
}

void UltraCanvasTextEditor::TranslateSelection(const std::string& text, const std::string& language) {
    if (translating) return;
    translating = true;
    if (statusLabel) statusLabel->SetText("Translating into " + language + "...");

    // The provider call runs off the UI thread; the result comes back
    // through the event loop. `this` outlives the thread: the editor is the
    // application's window and lives until the application ends.
    std::thread worker([this, text, language]() {
        UltraAI::TranslatorConfig cfg;          // empty providerId: local first
        UltraAI::Error createError;
        std::unique_ptr<UltraAI::ITranslator> translator = UltraAI::CreateTranslator(cfg, &createError);
        UltraAI::TranslateResponse resp;
        if (!translator) {
            resp.error = createError;
            if (resp.error.IsOk()) {
                resp.error.code    = UltraAI::ErrorCode::ProviderError;
                resp.error.message = "no translation provider is available";
            }
        } else {
            UltraAI::TranslateRequest req;
            req.texts = {text};
            req.targetLanguage = language;
            req.preserveFormatting = true;
            resp = translator->Translate(req);
        }
        auto deliver = [this, text, language, resp]() {
            translating = false;
            if (UltraAI::UltraAILocalModelDialog::NeedsInstall(resp.error)) {
                if (statusLabel) statusLabel->SetText("Translation needs a model on this computer.");
                UltraAI::UltraAILocalModelDialog::Show(GetWindow(), "Translation", "textllm", "",
                    [this, text, language](const std::string&) { TranslateSelection(text, language); });
                return;
            }
            if (!resp.error.IsOk() || resp.results.empty()) {
                if (statusLabel) statusLabel->SetText("Translation failed.");
                UltraCanvasDialogManager::ShowError(
                    resp.error.message.empty() ? "The translation returned nothing." : resp.error.message,
                    "Translate Selection", nullptr, GetWindow());
                return;
            }
            // Put the translation where the selection was. The user may have
            // moved on meanwhile; InsertText replaces whatever is selected now
            // and otherwise inserts at the caret, which is still the right
            // place for a translation they asked for.
            const std::string& translated = resp.results.front().text;
            if (UltraCanvasRichTextEdit* rich = GetActiveRichEdit()) {
                rich->InsertText(translated);
            } else if (auto doc = GetActiveDocument(); doc && doc->textArea) {
                doc->textArea->InsertText(translated);
            }
            if (statusLabel) statusLabel->SetText("Translated into " + language + ".");
        };
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (app) app->PostToUIThread(std::move(deliver));
        else deliver();
    });
    worker.detach();
}

#endif

} // namespace UltraCanvas
