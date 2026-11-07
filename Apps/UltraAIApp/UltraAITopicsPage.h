// Apps/UltraAIApp/UltraAITopicsPage.h
// The settings window's "Model topics" page: which local model each kind of
// work uses, and what happens to the models the model server publishes.
//
// - Model server: the address of the gallery (an UltraAI model server's
//   /gallery/index.json, or "" for the built-in one) and Refresh, which
//   fetches the gallery, the catalogue and the topics, then queues the
//   downloads the topic settings call for.
// - A table of the topics (chat, code, translation, summaries, pictures,
//   search, and whatever the server adds) with the model each uses on this
//   computer and whether it is installed. For the selected topic: the model
//   ("Server's choice" follows the server's default and moves with it, or a
//   model the user picks), "Download the server's choice when it changes",
//   and Install now.
// - New models: the ones the server published since the user last looked,
//   with Install and Mark all as seen, and the policy for them (tell me /
//   download automatically / ignore).
//
// Everything is LocalRuntime's (UltraAILocalRuntime.h: ListTopics,
// ModelForTopic, TopicPreferences, NewModels, ApplyTopicUpdates), applied
// and saved at once like the other pages. The refresh runs on a thread of
// its own; install progress comes from the runtime's worker; both are
// marshalled onto the UI thread.
// Version: 0.1.0
// Last Modified: 2026-11-06
// Author: UltraAI Module
#pragma once

#include "UltraAILocalRuntime.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTextInput.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasWindowBase;
}

namespace UltraAIApp {

class UltraAITopicsPage {
public:
    // `alive` is the settings window's flag: a callback that arrives after
    // the window closed does nothing.
    explicit UltraAITopicsPage(std::shared_ptr<std::atomic<bool>> alive);
    ~UltraAITopicsPage();

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(
        UltraCanvas::UltraCanvasWindowBase* parent);

    // Re-read the runtime (local files only): the topics, the selected
    // topic's controls, the new models. Called when the page is shown.
    void Refresh();

    // The page's "Restore defaults": every topic follows the server with
    // downloads on, new models are reported, the built-in gallery.
    void RestoreDefaults();

private:
    void BuildServerRow(UltraCanvas::UltraCanvasContainer& body);
    void BuildTopicTable(UltraCanvas::UltraCanvasContainer& body);
    void BuildTopicControls(UltraCanvas::UltraCanvasContainer& body);
    void BuildNewModels(UltraCanvas::UltraCanvasContainer& body);

    void RefreshTopicTable();
    void RefreshTopicControls();
    void RefreshNewModels();

    const UltraAI::ModelTopic* SelectedTopic() const;
    std::string ModelName(const std::string& modelId) const;

    void OnServerChanged();
    void OnRefreshFromServer();
    void OnTopicModelChosen(int index);
    void OnAutoInstallChanged(bool checked);
    void OnInstallTopicModel();
    void OnInstallNewModel();
    void OnMarkAllSeen();
    void OnNewModelPolicy(const std::string& value);
    void Install(const std::vector<std::string>& modelIds);
    void SetStatus(const std::string& text);
    UltraAI::InstallProgressCallback ProgressSink();

    std::shared_ptr<std::atomic<bool>> alive_;
    UltraCanvas::UltraCanvasWindowBase* parent_ = nullptr;

    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> serverInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    refreshButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     serverLabel_;

    std::shared_ptr<UltraCanvas::UltraCanvasListView>  topicTable_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> topicModel_;
    std::vector<UltraAI::ModelTopic>                   topics_;        // aligned with the table

    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     topicDescription_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  modelDropdown_;
    std::vector<std::string>                           modelIds_;      // aligned with the dropdown; [0] = "" (server's choice)
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  autoInstallBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    installButton_;

    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     newLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView>  newTable_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> newModel_;
    std::vector<UltraAI::LocalModelInfo>               newModels_;     // aligned with the table
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    installNewButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  policyDropdown_;

    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;

    bool filling_ = false;      // controls being filled: their callbacks do nothing
    std::shared_ptr<std::atomic<bool>> refreshing_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace UltraAIApp
