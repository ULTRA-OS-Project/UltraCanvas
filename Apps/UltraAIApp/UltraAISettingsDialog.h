// Apps/UltraAIApp/UltraAISettingsDialog.h
// The dashboard's Settings dialog. Configures the reusable endpoints that the
// per-mode dialogs draw from: an endpoint picker across the top ("➕ New
// endpoint" plus every saved endpoint), an editor form below (name, provider,
// base URL, default model, API key), and a checkbox per capability marking
// which modes the endpoint may serve. Save persists to endpoints.json and,
// for cloud providers, stores the key in UltraVault.
// Version: 0.1.0
// Author: UltraAI Module
#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"

#include "UltraAIEndpoints.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraAIApp {

class UltraAISettingsDialog : public UltraCanvas::UltraCanvasModalDialog {
public:
    UltraAISettingsDialog();

    // Build the dialog shell and its widgets, then load the endpoint identified
    // by selectEndpointId into the editor. When empty (the default), loads the
    // first endpoint or an empty editor when none are configured.
    void CreateSettingsDialog(const std::string& selectEndpointId = "");

private:
    void RebuildEndpointPicker(const std::string& selectId);
    void LoadIntoEditor(const Endpoint* e);   // nullptr => "New endpoint"
    void OnPickerChanged();
    void OnDelete();
    void OnSave();
    void SetStatus(const std::string& text);

    // Union of every capability's registered providers, de-duplicated.
    std::vector<std::string> AllKnownProviders() const;

    // Widgets.
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  endpointPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> nameInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  providerDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> baseUrlInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> modelInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> keyInput_;
    std::map<AICapability, std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>> modeChecks_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;

    // Endpoint id backing each picker row; index 0 ("New endpoint") is empty.
    std::vector<std::string> pickerIds_;
    // The endpoint currently loaded in the editor; empty => new/unsaved.
    std::string editingId_;

    static constexpr long kW      = 780;
    static constexpr long kH      = 590;
    static constexpr long kMargin = 16;
};

} // namespace UltraAIApp
