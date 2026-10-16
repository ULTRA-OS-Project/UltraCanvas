// Apps/UltraAIApp/UltraAITranslatorDialog.h
// A dedicated Translation window, built like the Chat dialog rather than on the
// generic service-dialog shell: no info-icon chrome, controls flush left, and a
// resizable flex layout so everything fits. The provider/model come from a
// configured endpoint picked at the top — an endpoint whose modes include
// Translation — with a Settings button to add or edit endpoints.
// Version: 0.1.0
// Author: UltraAI Module
#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"

#include "UltraAIEndpoints.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace UltraAIApp {

class UltraAITranslatorDialog : public UltraCanvas::UltraCanvasModalDialog {
public:
    UltraAITranslatorDialog();
    ~UltraAITranslatorDialog() override;

    void CreateTranslatorDialog();

private:
    // Rebuild the picker from the Translation endpoints. When `selectId` names
    // an endpoint still in the list its row stays selected; otherwise the first
    // endpoint is chosen (first open, or the selected one was just deleted).
    void RebuildEndpointPicker(const std::string& selectId = "");
    void OnOpenSettings();
    void OnRun();

    void SetStatus(const std::string& text);
    // Marshalled onto the UI thread when the off-thread translate finishes.
    void OnRunFinished(const std::string& result, const std::string& status);

    // Widgets.
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  endpointPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  input_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> targetInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> sourceInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  formalityPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  result_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    runButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;

    // Endpoints offering Translation, aligned with the picker's item indices.
    std::vector<Endpoint> translationEndpoints_;

    bool running_ = false;

    // Flipped false in the destructor so a late delivery becomes a no-op
    // instead of calling into a destroyed dialog (the RunOffThread pattern
    // from UltraAIServiceDialog / UltraAIChatDialog).
    std::shared_ptr<std::atomic<bool>> uiAlive_ =
        std::make_shared<std::atomic<bool>>(true);

    static constexpr long kW      = 760;
    static constexpr long kH      = 620;
};

} // namespace UltraAIApp
