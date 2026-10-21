// UltraAI/ui/include/UltraAILocalModelDialog.h
// "Get a local model": the dialog an application shows the first time a
// service needs a model that is not on this computer. One dialog for every
// UltraCanvas application, so Texter's translation and the dashboard's chat
// ask the same way. It names the service, offers the models of the local
// runtime's catalogue that fit this computer (the runtime's recommendation
// preselected), shows the size and the licence, downloads the chosen one
// with its engine on "Download and continue" - progress in the dialog -
// and hands the model id back when it is installed, so the caller can go
// on with what it was doing. "Not now" leaves everything as it was. A
// checkbox turns the runtime's download policy to Automatic, after which
// no service asks again.
//
// The cue to open it is the `local` provider's ModelNotFound error with
// providerCode "needs-install" (UltraAILocal.h) - NeedsInstall(error) tells
// - which the text-LLM translator passes through from the chat model, so a
// translation that needs a model is the same cue. Everything in it is the
// local runtime's (UltraAILocalRuntime.h); the dialog adds no logic of its
// own. Part of UltraAI_UI, the module's one library that draws; built
// in-tree with the UltraCanvas core.
//
// For example, after a translation:
//   UltraAI::TranslateResponse resp = translator->Translate(req);
//   if (UltraAI::UltraAILocalModelDialog::NeedsInstall(resp.error)) {
//       UltraAI::UltraAILocalModelDialog::Show(window, "Translation", "textllm", "",
//           [](const std::string& modelId) { /* run the translation again */ });
//   }
// Version: 0.2.0 - in the module's UI library, for every application
// Version: 0.1.0 - in the dashboard
// Last Modified: 2026-10-21
// Author: UltraAI Module
#pragma once

#include "UltraAILocalRuntime.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraAI {

class UltraAILocalModelDialog : public UltraCanvas::UltraCanvasModalDialog {
public:
    // Open the dialog for `capability` ("textllm", "embeddings"); the
    // service name is what the user sees ("Chat"). `preferredModelId` is
    // preselected when it is offered (the endpoint's model); empty takes
    // the runtime's recommendation. `onReady(modelId)` runs on the UI
    // thread once the model is installed; nothing runs on "Not now".
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     const std::string& serviceName,
                     const std::string& capability,
                     const std::string& preferredModelId,
                     std::function<void(const std::string& modelId)> onReady);

    // Whether an error is the cue for this dialog: the `local` provider's
    // "needs-install", as a chat, an embedding or a translation returns it.
    static bool NeedsInstall(const Error& error);

    UltraAILocalModelDialog(std::string serviceName, std::string capability,
                            std::string preferredModelId,
                            std::function<void(const std::string&)> onReady);
    ~UltraAILocalModelDialog() override;

    void CreateLocalModelDialog();
    void PerformClose() override;

private:
    void OnDownload();
    void OnNotNow();
    void OnChoiceChanged();
    void ShowProgress(const InstallProgress& p);

    std::string serviceName_;
    std::string capability_;
    std::string preferredModelId_;
    std::function<void(const std::string&)> onReady_;

    std::vector<LocalModelInfo> choices_;   // aligned with the dropdown
    std::string activeJobId_;

    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> picker_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>    details_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> automaticBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>    progressLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasGaugeDiagramElement> progressBar_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>   downloadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>   notNowButton_;

    std::shared_ptr<std::atomic<bool>> uiAlive_ = std::make_shared<std::atomic<bool>>(true);
};

} // namespace UltraAI
