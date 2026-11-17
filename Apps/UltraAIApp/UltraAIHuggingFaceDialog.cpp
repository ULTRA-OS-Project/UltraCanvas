// Apps/UltraAIApp/UltraAIHuggingFaceDialog.cpp
// "Search Hugging Face" (UltraAIHuggingFaceDialog.h).
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraAI Module

#include "UltraAIHuggingFaceDialog.h"

#include "UltraAILocalModelText.h"
#include "UltraAISettingsWidgets.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListSelection.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <thread>
#include <utility>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;
using namespace SettingsWidgets;
using namespace LocalModelText;

namespace {

constexpr int kInnerWidth = 628;        // the dialog's width less its padding

void OnUiThread(std::function<void()> fn) {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (app) app->PostToUIThread(std::move(fn));
    else fn();
}

std::string Trimmed(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// What the search field holds when it names a repository rather than
// words: "organisation/repository", the same with ":file" after it (how
// this page asked for a file before it could search), or the repository's
// address on huggingface.co. `file` takes the file part, when there is one.
bool AsRepository(std::string text, std::string& repo, std::string& file) {
    file.clear();
    for (const char* prefix : {"https://huggingface.co/", "http://huggingface.co/", "huggingface.co/", "hf.co/"}) {
        const std::string p = prefix;
        if (text.compare(0, p.size(), p) == 0) {
            text = text.substr(p.size());
            // ".../tree/main" and the like after the repository are not part of it.
            const size_t first = text.find('/');
            const size_t second = first == std::string::npos ? std::string::npos : text.find('/', first + 1);
            if (second != std::string::npos) text.resize(second);
            break;
        }
    }
    const size_t colon = text.find(':');
    if (colon != std::string::npos) {
        file = text.substr(colon + 1);
        text.resize(colon);
    }
    if (text.find_first_of(" \t") != std::string::npos) return false;
    const size_t slash = text.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= text.size()) return false;
    if (text.find('/', slash + 1) != std::string::npos) return false;
    repo = text;
    return true;
}

std::shared_ptr<UltraCanvasContainer> Row(const std::string& id, int height) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->size.height = CSSLayout::Dimension::Px(height);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return row;
}

std::shared_ptr<UltraCanvasListView> Table(const std::string& id,
                                           const std::shared_ptr<UltraCanvasMultiColumnListModel>& model,
                                           int height) {
    auto table = std::make_shared<UltraCanvasListView>(id, 0, 0, kInnerWidth, height);
    table->SetModel(model);
    ListViewStyle style;
    style.showHeader         = true;
    style.showGridLines      = false;
    style.alternateRowColors = true;
    style.headerFontSize     = kTextFontSize;
    style.headerHeight       = 26;
    style.rowHeight          = 22;
    table->SetStyle(style);
    auto delegate = std::make_shared<UltraCanvasDefaultListDelegate>();
    delegate->SetFontSize(kTextFontSize);
    table->SetDelegate(delegate);
    table->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    table->size.height = CSSLayout::Dimension::Px(height);
    table->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    return table;
}

} // namespace

UltraAIHuggingFaceDialog::UltraAIHuggingFaceDialog(AddedCallback onAdded)
    : onAdded_(std::move(onAdded)) {}

UltraAIHuggingFaceDialog::~UltraAIHuggingFaceDialog() {
    alive_->store(false);
}

void UltraAIHuggingFaceDialog::CreateHuggingFaceDialog() {
    using CSSLayout::AlignItems;
    using CSSLayout::Dimension;

    DialogConfig cfg;
    cfg.title      = Tr("Search Hugging Face");
    cfg.width      = kW;
    cfg.height     = kH;
    cfg.dialogType = DialogType::Custom;        // its own content, no icon or message
    cfg.buttons    = DialogButtons::NoButtons;
    cfg.position   = DialogPosition::CenterParent;
    cfg.resizable  = true;
    CreateDialog(cfg);

    layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(AlignItems::Stretch);
    SetPadding(16);

    AddChild(MakeText("uai-hf-intro",
                      Tr("Find a model on huggingface.co by its name, or type a repository as "
                         "organisation/repository. A model in GGUF format runs on llama-server; a "
                         "whisper.cpp model (ggml-….bin) is a speech-to-text model."),
                      kInnerWidth, kNoteFontSize, kNoteTextColor));

    // ===== The search =====
    auto searchRow = Row("uai-hf-search-row", 28);
    searchInput_ = std::make_shared<UltraCanvasTextInput>("uai-hf-search", 0, 0, 300, 26);
    searchInput_->SetPlaceholder(Tr("Search Hugging Face"));
    searchInput_->SetFontSize(kTextFontSize);
    searchInput_->size.height = Dimension::Px(26);
    searchInput_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    searchInput_->onEnterPressed = [this](const std::string&) {
        StartSearch();
        return true;
    };
    searchRow->AddChild(searchInput_);
    ggufOnly_ = MakeCheckbox("uai-hf-gguf", Tr("GGUF models only"), 140, true, [this](bool) { StartSearch(); });
    searchRow->AddChild(ggufOnly_);
    searchButton_ = MakeButton("uai-hf-go", Tr("Search"), 84, [this]() { StartSearch(); });
    searchRow->AddChild(searchButton_);
    AddChild(searchRow);

    // ===== The repositories found =====
    repoModel_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    repoModel_->AddColumn(ListColumnDef(Tr("Repository"), 356, TextAlignment::Left, Tr("The repository, as organisation/name")));
    repoModel_->AddColumn(ListColumnDef(Tr("Downloads"), 84, TextAlignment::Right, Tr("Downloads in the last month")));
    repoModel_->AddColumn(ListColumnDef(Tr("Likes"), 60, TextAlignment::Right, Tr("How many people marked it")));
    repoModel_->AddColumn(ListColumnDef(Tr("Updated"), 100, TextAlignment::Left, Tr("When it last changed")));
    repoList_ = Table("uai-hf-repos", repoModel_, 180);
    repoList_->onSelectionChanged = [this](const std::vector<int>&) { OnRepoSelected(); };
    AddChild(repoList_);

    // ===== The files of the one selected =====
    filesTitle_ = MakeText("uai-hf-files-title", Tr("Select a repository to see its model files."), kInnerWidth);
    AddChild(filesTitle_);
    fileModel_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    fileModel_->AddColumn(ListColumnDef(Tr("File"), 330, TextAlignment::Left, Tr("The model file inside the repository")));
    fileModel_->AddColumn(ListColumnDef(Tr("Quantisation"), 100, TextAlignment::Left, Tr("How far the weights are reduced: Q4 is smaller and faster, Q8 closer to the original")));
    fileModel_->AddColumn(ListColumnDef(Tr("Size"), 80, TextAlignment::Right, Tr("To download")));
    fileModel_->AddColumn(ListColumnDef(Tr("Fits"), 90, TextAlignment::Left, Tr("Whether it fits this computer, and where it runs")));
    fileList_ = Table("uai-hf-files", fileModel_, 140);
    fileList_->onSelectionChanged = [this](const std::vector<int>&) { OnFileSelected(); };
    AddChild(fileList_);

    // ===== What it is added as =====
    auto optionsRow = Row("uai-hf-options", 28);
    optionsRow->AddChild(MakeLabel("uai-hf-as-lbl", Tr("Add as")));
    capabilityDropdown_ = std::make_shared<UltraCanvasDropdown>("uai-hf-as", 0, 0, 160, 26);
    capabilityDropdown_->size.width  = Dimension::Px(160);
    capabilityDropdown_->size.height = Dimension::Px(26);
    capabilityDropdown_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    capabilityDropdown_->AddItem(CapabilityLabel("textllm"), "textllm");
    capabilityDropdown_->AddItem(CapabilityLabel("embeddings"), "embeddings");
    capabilityDropdown_->AddItem(CapabilityLabel("speechtotext"), "speechtotext");
    capabilityDropdown_->SetSelectedIndex(0, false);
    capabilityDropdown_->onSelectionChanged = [this](int, const DropdownItem&) { RefreshButtons(); };
    optionsRow->AddChild(capabilityDropdown_);
    projectorBox_ = MakeCheckbox("uai-hf-projector", Tr("With its vision projector"), 260, true, nullptr);
    projectorBox_->SetVisible(false);
    optionsRow->AddChild(projectorBox_);
    AddChild(optionsRow);

    statusLabel_ = MakeText("uai-hf-status", "", kInnerWidth, kNoteFontSize, kNoteTextColor);
    AddChild(statusLabel_);

    // ===== Footer =====
    auto footer = Row("uai-hf-footer", 30);
    addButton_     = MakeButton("uai-hf-add", Tr("Add"), 84, [this]() { OnAdd(false); });
    installButton_ = MakeButton("uai-hf-install", Tr("Add and install"), 130, [this]() { OnAdd(true); });
    footer->AddChild(addButton_);
    footer->AddChild(installButton_);
    auto spacer = std::make_shared<UltraCanvasContainer>("uai-hf-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    footer->AddChild(spacer);
    footer->AddChild(MakeButton("uai-hf-close", Tr("Close"), 84, [this]() { CloseDialog(DialogResult::Close); }));
    AddChild(footer);

    RefreshButtons();
    StartSearch();      // the most downloaded GGUF repositories, to begin with
}

void UltraAIHuggingFaceDialog::SetStatus(const std::string& text) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->RequestRedraw();
}

void UltraAIHuggingFaceDialog::RefreshButtons() {
    const bool can = SelectedFile() != nullptr && !adding_ && !loadingFiles_;
    for (const auto& b : {addButton_, installButton_}) {
        if (!b) continue;
        b->SetDisabled(!can);
        b->RequestRedraw();
    }
    if (searchButton_) {
        searchButton_->SetDisabled(searching_);
        searchButton_->RequestRedraw();
    }
    // A projector goes with a chat model of a repository that has one.
    if (projectorBox_) {
        const UltraAI::HuggingFaceFile* projector = nullptr;
        for (const auto& f : files_) {
            if (f.projector) { projector = &f; break; }
        }
        std::string capability;
        if (capabilityDropdown_) {
            if (const DropdownItem* item = capabilityDropdown_->GetSelectedItem()) capability = item->value;
        }
        const bool offer = projector && capability == "textllm";
        if (offer) projectorBox_->SetText(TrF("With its vision projector ({0})", Bytes(projector->sizeBytes)));
        projectorBox_->SetVisible(offer);
        projectorBox_->RequestRedraw();
    }
}

void UltraAIHuggingFaceDialog::StartSearch() {
    if (!searchInput_) return;
    const std::string text = Trimmed(searchInput_->GetText());
    const bool ggufOnly = !ggufOnly_ || ggufOnly_->IsChecked();
    const int generation = ++searchGeneration_;
    searching_ = true;
    RefreshButtons();

    std::string repo, wantedFile;
    const bool named = AsRepository(text, repo, wantedFile);
    SetStatus(named ? TrF("Reading the files of {0}…", repo)
                    : text.empty() ? Tr("Asking Hugging Face for the most downloaded models…")
                                   : TrF("Searching Hugging Face for {0}…", text));

    std::thread worker([this, alive = alive_, generation, text, ggufOnly, named, repo]() {
        LocalRuntime& rt = LocalRuntime::Instance();
        std::vector<HuggingFaceRepo> repos;
        std::vector<HuggingFaceFile> files;
        HuggingFaceRepo facts;
        bool direct = false;
        Error error;
        if (named) {
            // A repository by name: its files at once. One that does not
            // exist is searched for as words instead.
            error = rt.ListHuggingFaceFiles(repo, files, &facts);
            if (error.IsOk()) {
                direct = true;
                repos.push_back(facts);
            } else if (error.code == ErrorCode::ModelNotFound) {
                error = rt.SearchHuggingFace(text, ggufOnly, 50, repos);
            }
        } else {
            error = rt.SearchHuggingFace(text, ggufOnly, 50, repos);
        }
        OnUiThread([this, alive, generation, error, repos, files, facts, direct, text]() {
            if (!alive->load() || generation != searchGeneration_) return;
            searching_ = false;
            if (!error.IsOk()) {
                SetStatus(TrF("Hugging Face could not be asked: {0}", error.message));
                RefreshButtons();
                return;
            }
            ShowRepos(repos);
            if (direct) {
                // The files came with the answer; no second question.
                ++filesGeneration_;
                loadingFiles_ = false;
                repo_  = facts;
                files_ = files;
                if (repoList_ && repoList_->GetSelection()) {
                    // Selecting the row would ask for the files again.
                    auto keep = repoList_->onSelectionChanged;
                    repoList_->onSelectionChanged = nullptr;
                    repoList_->GetSelection()->Select(0);
                    repoList_->onSelectionChanged = keep;
                }
                ShowFiles();
            } else {
                SetStatus(repos.empty() ? (text.empty() ? Tr("Hugging Face listed nothing.")
                                                        : TrF("Nothing found for {0}.", text))
                                        : TrN("{n} repository, most downloaded first. Select one to see its files.",
                                              "{n} repositories, most downloaded first. Select one to see its files.",
                                              static_cast<int>(repos.size())));
            }
            RefreshButtons();
        });
    });
    worker.detach();
}

void UltraAIHuggingFaceDialog::ShowRepos(const std::vector<HuggingFaceRepo>& repos) {
    repos_ = repos;
    std::vector<MultiColumnListItem> items;
    for (const HuggingFaceRepo& r : repos_) {
        MultiColumnListItem item({r.id, Count(r.downloads), Count(r.likes), r.lastModified});
        std::string about = r.id;
        if (!r.pipelineTag.empty()) about += "\n" + r.pipelineTag;
        if (!r.license.empty()) about += "\n" + TrF("Licence: {0}", r.license);
        item.tooltip = about;
        items.push_back(std::move(item));
    }
    if (repoModel_) repoModel_->SetItems(std::move(items));
    if (repoList_ && repoList_->GetSelection()) repoList_->GetSelection()->Clear();
    // A new list: the files shown belong to the old one.
    ++filesGeneration_;
    loadingFiles_ = false;
    repo_ = HuggingFaceRepo{};
    files_.clear();
    fileRows_.clear();
    if (fileModel_) fileModel_->SetItems({});
    if (filesTitle_) filesTitle_->SetText(Tr("Select a repository to see its model files."));
}

void UltraAIHuggingFaceDialog::OnRepoSelected() {
    if (!repoList_ || !repoList_->GetSelection()) return;
    const int row = repoList_->GetSelection()->GetCurrentRow();
    if (row < 0 || row >= static_cast<int>(repos_.size())) return;
    const std::string id = repos_[static_cast<size_t>(row)].id;
    if (id == repo_.id && !files_.empty()) return;
    LoadFiles(id);
}

void UltraAIHuggingFaceDialog::LoadFiles(const std::string& repo) {
    const int generation = ++filesGeneration_;
    loadingFiles_ = true;
    files_.clear();
    fileRows_.clear();
    if (fileModel_) fileModel_->SetItems({});
    if (filesTitle_) filesTitle_->SetText(TrF("Files of {0}", repo));
    SetStatus(TrF("Reading the files of {0}…", repo));
    RefreshButtons();
    std::thread worker([this, alive = alive_, generation, repo]() {
        std::vector<HuggingFaceFile> files;
        HuggingFaceRepo facts;
        const Error error = LocalRuntime::Instance().ListHuggingFaceFiles(repo, files, &facts);
        OnUiThread([this, alive, generation, error, files, facts]() {
            if (!alive->load() || generation != filesGeneration_) return;
            loadingFiles_ = false;
            if (!error.IsOk()) {
                SetStatus(TrF("The files could not be read: {0}", error.message));
                RefreshButtons();
                return;
            }
            repo_  = facts;
            files_ = files;
            ShowFiles();
            RefreshButtons();
        });
    });
    worker.detach();
}

void UltraAIHuggingFaceDialog::ShowFiles() {
    fileRows_.clear();
    std::vector<MultiColumnListItem> items;
    int best = -1, firstFitting = -1;
    for (size_t i = 0; i < files_.size(); ++i) {
        const HuggingFaceFile& f = files_[i];
        if (f.projector) continue;      // it goes with a model; the box below adds it
        std::string size = Bytes(f.sizeBytes);
        if (!f.parts.empty()) size = TrF("{0}, {1} parts", size, static_cast<int>(f.parts.size()) + 1);
        MultiColumnListItem item({f.name, f.quantization.empty() ? "—" : f.quantization, size, FitLabel(f.fit)});
        item.tooltip = f.name;
        item.SetCellTooltip(3, FitTooltip(f.fit));
        items.push_back(std::move(item));
        const int row = static_cast<int>(fileRows_.size());
        fileRows_.push_back(i);
        const bool fits = f.fit != ModelFit::TooLarge;
        if (fits && firstFitting < 0) firstFitting = row;
        // Q4_K_M is the usual balance of size and quality.
        if (fits && best < 0 && f.quantization == "Q4_K_M") best = row;
    }
    if (fileModel_) fileModel_->SetItems(std::move(items));

    std::string title = TrF("Files of {0}", repo_.id);
    if (!repo_.license.empty()) title += " - " + TrF("Licence: {0}", repo_.license);
    if (filesTitle_) filesTitle_->SetText(title);

    if (fileRows_.empty()) {
        SetStatus(Tr("This repository holds no file a local engine can run: no GGUF, and no whisper.cpp ggml model."));
    } else if (repo_.gated) {
        SetStatus(Tr("This repository is gated: its publisher asks you to accept its terms on "
                     "huggingface.co first, and the download is refused until then."));
    } else {
        SetStatus(TrN("{n} model file. Select the one to add.", "{n} model files. Select the one to add.",
                      static_cast<int>(fileRows_.size())));
    }
    const int select = best >= 0 ? best : firstFitting;
    if (select >= 0 && fileList_ && fileList_->GetSelection()) {
        fileList_->GetSelection()->Select(select);
        fileList_->ScrollToRow(select);
    }
    OnFileSelected();
}

const HuggingFaceFile* UltraAIHuggingFaceDialog::SelectedFile() const {
    if (!fileList_ || !fileList_->GetSelection()) return nullptr;
    const int row = fileList_->GetSelection()->GetCurrentRow();
    if (row < 0 || row >= static_cast<int>(fileRows_.size())) return nullptr;
    return &files_[fileRows_[static_cast<size_t>(row)]];
}

void UltraAIHuggingFaceDialog::OnFileSelected() {
    // What the file would serve, as the repository and its name suggest; the
    // user may know better.
    if (const HuggingFaceFile* f = SelectedFile()) {
        if (capabilityDropdown_) {
            for (int i = 0; i < capabilityDropdown_->GetItemCount(); ++i) {
                const DropdownItem* item = capabilityDropdown_->GetItem(i);
                if (item && item->value == f->capability) capabilityDropdown_->SetSelectedIndex(i, false);
            }
        }
    }
    RefreshButtons();
}

std::string UltraAIHuggingFaceDialog::ProjectorToAdd() const {
    if (!projectorBox_ || !projectorBox_->IsVisible() || !projectorBox_->IsChecked()) return {};
    for (const auto& f : files_) {
        if (f.projector) return f.name;
    }
    return {};
}

void UltraAIHuggingFaceDialog::OnAdd(bool install) {
    const HuggingFaceFile* file = SelectedFile();
    if (!file || adding_) return;
    HuggingFaceModelRequest request;
    request.repo       = repo_.id;
    request.file       = file->name;
    request.extraFiles = file->parts;
    request.license    = repo_.license;
    request.capability = file->capability;
    if (capabilityDropdown_) {
        if (const DropdownItem* item = capabilityDropdown_->GetSelectedItem()) request.capability = item->value;
    }
    if (request.capability == "textllm") request.projectorFile = ProjectorToAdd();
    adding_ = true;
    RefreshButtons();
    SetStatus(TrF("Adding {0}…", file->name));
    std::thread worker([this, alive = alive_, request, install]() {
        std::string id;
        const Error error = LocalRuntime::Instance().AddHuggingFaceModel(request, id);
        OnUiThread([this, alive, error, id, install, name = request.file]() {
            if (!alive->load()) return;
            adding_ = false;
            if (!error.IsOk()) {
                SetStatus(error.message);
                RefreshButtons();
                return;
            }
            // The page shows the model and installs it; the dialog is done.
            // Closing may delete it, so nothing of it is touched afterwards.
            const AddedCallback added = onAdded_;
            const std::string shown = name.substr(name.find_last_of('/') == std::string::npos ? 0 : name.find_last_of('/') + 1);
            CloseDialog(DialogResult::OK);
            if (added) added(id, shown, install);
        });
    });
    worker.detach();
}

} // namespace UltraAIApp
