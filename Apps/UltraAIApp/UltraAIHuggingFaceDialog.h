// Apps/UltraAIApp/UltraAIHuggingFaceDialog.h
// "Search Hugging Face": find a model on huggingface.co and add it to the
// local runtime. A search field (words, or a repository typed as
// "organisation/repository"), the repositories found - most downloaded
// first - and, for the one selected, the files in it that a local engine
// can run: each GGUF with its quantisation, its size and whether it fits
// this computer, and a whisper.cpp ggml .bin as a speech-to-text model.
// "Add" registers the selected file (LocalRuntime::AddHuggingFaceModel);
// "Add and install" also starts its download on the Local models page.
//
// The searches run off the UI thread (LocalRuntime::SearchHuggingFace,
// ListHuggingFaceFiles); an answer that arrives after a newer question, or
// after the dialog closed, is dropped.
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraAI Module
#pragma once

#include "UltraAILocalRuntime.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraAIApp {

class UltraAIHuggingFaceDialog : public UltraCanvas::UltraCanvasModalDialog {
public:
    // Called on the UI thread once a model is registered: its id, the name
    // it is listed under, and whether the user asked for it to be installed.
    using AddedCallback = std::function<void(const std::string& modelId, const std::string& name, bool install)>;

    explicit UltraAIHuggingFaceDialog(AddedCallback onAdded);
    ~UltraAIHuggingFaceDialog() override;

    // Build the dialog and start the first search (the most downloaded
    // GGUF repositories).
    void CreateHuggingFaceDialog();

private:
    void StartSearch();
    void ShowRepos(const std::vector<UltraAI::HuggingFaceRepo>& repos);
    void LoadFiles(const std::string& repo);
    void ShowFiles();
    void OnRepoSelected();
    void OnFileSelected();
    void OnAdd(bool install);
    void RefreshButtons();
    void SetStatus(const std::string& text);
    const UltraAI::HuggingFaceFile* SelectedFile() const;
    // The vision projector to add with the selected model: the repository's
    // only one, when the box is ticked.
    std::string ProjectorToAdd() const;

    AddedCallback onAdded_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> searchInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  ggufOnly_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    searchButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView>  repoList_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> repoModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     filesTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView>  fileList_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> fileModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  capabilityDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  projectorBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    addButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    installButton_;

    std::vector<UltraAI::HuggingFaceRepo> repos_;       // aligned with repoList_
    UltraAI::HuggingFaceRepo              repo_;        // the one whose files are shown
    std::vector<UltraAI::HuggingFaceFile> files_;       // its runnable files, projectors included
    std::vector<size_t>                   fileRows_;    // indices into files_, aligned with fileList_
    int  searchGeneration_ = 0;     // a newer search drops an older answer
    int  filesGeneration_  = 0;
    bool searching_ = false;
    bool loadingFiles_ = false;
    bool adding_ = false;

    static constexpr long kW = 660;
    static constexpr long kH = 610;
};

} // namespace UltraAIApp
