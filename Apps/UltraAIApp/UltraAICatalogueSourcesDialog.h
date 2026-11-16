// Apps/UltraAIApp/UltraAICatalogueSourcesDialog.h
// "Sources": where the Local models page's list of models is downloaded
// from (LocalRuntimeSettings::catalogueSources). Each source with how many
// models it offers and when its list was downloaded; turn one off or on,
// remove one, add one by the address of its list - a LocalAI model gallery
// (its index.yaml) or an UltraAI catalogue.json. Every change is saved at
// once; "Refresh catalogue" on the page downloads the lists.
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraAI Module
#pragma once

#include "UltraAILocalRuntime.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasModalDialog.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraAIApp {

class UltraAICatalogueSourcesDialog : public UltraCanvas::UltraCanvasModalDialog {
public:
    // `onChanged` runs on the UI thread after every change to the sources.
    explicit UltraAICatalogueSourcesDialog(std::function<void()> onChanged);
    ~UltraAICatalogueSourcesDialog() override;

    void CreateSourcesDialog();

private:
    void Reload();
    void RefreshButtons();
    void OnToggle();
    void OnRemove();
    void OnAdd();
    void AddSource(const std::string& address);
    void SetStatus(const std::string& text);
    int SelectedRow() const;

    std::function<void()> onChanged_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    std::shared_ptr<UltraCanvas::UltraCanvasListView> list_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> model_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>  addressLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>  statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> toggleButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> removeButton_;
    std::vector<UltraAI::CatalogueSourceStatus> sources_;     // aligned with list_

    static constexpr long kW = 600;
    static constexpr long kH = 400;
};

} // namespace UltraAIApp
