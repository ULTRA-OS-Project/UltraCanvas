// Apps/UltraAIApp/UltraAITopicsPage.cpp
// The settings window's "Model topics" page (UltraAITopicsPage.h).
// Version: 0.1.0
// Last Modified: 2026-11-06
// Author: UltraAI Module

#include "UltraAITopicsPage.h"

#include "UltraAISettingsWidgets.h"

#include "UltraCanvasApplication.h"      // UltraCanvasApplicationBase::GetCurrent
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListSelection.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <thread>
#include <utility>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;
using namespace SettingsWidgets;

namespace {

constexpr int kRowHeight     = 22;
constexpr int kTopicRows     = 6;
constexpr int kNewRows       = 3;
constexpr int kLabelWidth    = 110;
constexpr int kDropdownWidth = 250;

int TableHeight(int rows) { return rows * kRowHeight + 26 + 2; }

std::string Bytes(uint64_t bytes) {
    if (!bytes) return "—";
    char buffer[32];
    if (bytes >= 1024ull * 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));   // locale-ok: shown to the user
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024));   // locale-ok: shown to the user
    }
    return buffer;
}

std::string CapabilityLabel(const std::string& capability) {
    if (capability == "textllm")      return "Chat";
    if (capability == "embeddings")   return "Embeddings";
    if (capability == "speechtotext") return "Speech to text";
    if (capability == "texttospeech") return "Speech";
    if (capability == "imagegen")     return "Images";
    return capability;
}

void OnUiThread(std::function<void()> fn) {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (app) app->PostToUIThread(std::move(fn));
    else fn();
}

std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id, int height) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.width  = CSSLayout::Dimension::Px(kTextWidth);
    row->size.height = CSSLayout::Dimension::Px(height);
    return row;
}

std::shared_ptr<UltraCanvasLabel> RowLabel(const std::string& id, const std::string& text) {
    auto lbl = MakeLabel(id, text);
    lbl->size.width = CSSLayout::Dimension::Px(kLabelWidth);
    lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return lbl;
}

std::shared_ptr<UltraCanvasDropdown> MakeDropdown(const std::string& id, int width) {
    auto dd = std::make_shared<UltraCanvasDropdown>(id, 0, 0, width, 26);
    dd->size.width  = CSSLayout::Dimension::Px(width);
    dd->size.height = CSSLayout::Dimension::Px(26);
    dd->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return dd;
}

std::shared_ptr<UltraCanvasListView> MakeTable(const std::string& id, int rows,
                                               std::shared_ptr<UltraCanvasMultiColumnListModel>& model) {
    model = std::make_shared<UltraCanvasMultiColumnListModel>();
    auto table = std::make_shared<UltraCanvasListView>(id, 0, 0, kTextWidth, TableHeight(rows));
    table->SetModel(model);
    ListViewStyle style;
    style.showHeader         = true;
    style.showGridLines      = false;
    style.alternateRowColors = true;
    style.headerFontSize     = kTextFontSize;
    style.headerHeight       = 26;
    style.rowHeight          = kRowHeight;
    table->SetStyle(style);
    auto delegate = std::make_shared<UltraCanvasDefaultListDelegate>();
    delegate->SetFontSize(kTextFontSize);
    table->SetDelegate(delegate);
    table->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    table->size.width  = CSSLayout::Dimension::Px(kTextWidth);
    table->size.height = CSSLayout::Dimension::Px(TableHeight(rows));
    table->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return table;
}

int SelectedRow(const std::shared_ptr<UltraCanvasListView>& table) {
    if (!table || !table->GetSelection()) return -1;
    return table->GetSelection()->GetCurrentRow();
}

} // namespace

UltraAITopicsPage::UltraAITopicsPage(std::shared_ptr<std::atomic<bool>> alive)
    : alive_(std::move(alive)) {}

UltraAITopicsPage::~UltraAITopicsPage() = default;

std::shared_ptr<UltraCanvasContainer> UltraAITopicsPage::Build(UltraCanvasWindowBase* parent) {
    parent_ = parent;
    PageParts parts = MakePage("uai-set-page-topics", "Model topics",
            "Which model each kind of work uses on this computer, and what happens "
            "when the model server offers new ones.");

    BuildServerRow(*parts.body);
    BuildTopicTable(*parts.body);
    BuildTopicControls(*parts.body);
    BuildNewModels(*parts.body);

    statusLabel_ = MakeText("uai-tp-status", "", kTextWidth, kNoteFontSize, kNoteTextColor);
    parts.body->AddChild(statusLabel_);

    AddNote(parts, "uai-tp-note1",
            "Server's choice: the topic uses the model the model server recommends "
            "for it, and moves to a new one when the server changes its "
            "recommendation. Pick a model to keep that one whatever the server says.");
    AddNote(parts, "uai-tp-note2",
            "A model that does not fit this computer's memory is never used; the "
            "next model the topic offers takes its place. Downloads follow the "
            "policy on the Local models page: under Never, nothing is downloaded "
            "from here either.");
    AddNote(parts, "uai-tp-note3",
            "Model server: the address an administrator gave you "
            "(https://<server>/gallery/index.json). Empty uses the gallery built "
            "into UltraAI.");

    Refresh();
    return parts.page;
}

// ===== BUILDING =====

void UltraAITopicsPage::BuildServerRow(UltraCanvasContainer& body) {
    auto row = MakeRow("uai-tp-server-row", 30);
    row->AddChild(RowLabel("uai-tp-server-lbl", "Model server"));
    serverInput_ = std::make_shared<UltraCanvasTextInput>("uai-tp-server", 0, 0, 250, 26);
    serverInput_->SetPlaceholder("Built-in gallery");
    serverInput_->size.width  = CSSLayout::Dimension::Px(250);
    serverInput_->size.height = CSSLayout::Dimension::Px(26);
    serverInput_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    serverInput_->onEnterPressed = [this](const std::string&) { OnServerChanged(); return true; };
    serverInput_->onFocusLost = [this]() { OnServerChanged(); };
    row->AddChild(serverInput_);
    refreshButton_ = MakeButton("uai-tp-refresh", "Refresh", 100, [this]() { OnRefreshFromServer(); });
    row->AddChild(refreshButton_);
    body.AddChild(row);

    serverLabel_ = MakeText("uai-tp-server-info", "", kTextWidth, kNoteFontSize, kNoteTextColor);
    body.AddChild(serverLabel_);
}

void UltraAITopicsPage::BuildTopicTable(UltraCanvasContainer& body) {
    topicTable_ = MakeTable("uai-tp-topics", kTopicRows, topicModel_);
    topicModel_->AddColumn(ListColumnDef("Topic", 140, TextAlignment::Left, "What the model is used for"));
    topicModel_->AddColumn(ListColumnDef("Service", 76, TextAlignment::Left, "The kind of model the topic needs"));
    topicModel_->AddColumn(ListColumnDef("Model used", 170, TextAlignment::Left, "The model this computer uses for the topic"));
    topicModel_->AddColumn(ListColumnDef("State", 90, TextAlignment::Left, "Installed, or downloaded when first used"));
    topicTable_->onSelectionChanged = [this](const std::vector<int>&) { RefreshTopicControls(); };
    body.AddChild(topicTable_);
}

void UltraAITopicsPage::BuildTopicControls(UltraCanvasContainer& body) {
    topicDescription_ = MakeText("uai-tp-desc", "", kTextWidth, kNoteFontSize, kNoteTextColor);
    body.AddChild(topicDescription_);

    auto row = MakeRow("uai-tp-model-row", 30);
    row->AddChild(RowLabel("uai-tp-model-lbl", "Model"));
    modelDropdown_ = MakeDropdown("uai-tp-model", kDropdownWidth);
    modelDropdown_->onSelectionChanged = [this](int index, const DropdownItem&) { OnTopicModelChosen(index); };
    row->AddChild(modelDropdown_);
    installButton_ = MakeButton("uai-tp-install", "Install now", 100, [this]() { OnInstallTopicModel(); });
    row->AddChild(installButton_);
    body.AddChild(row);

    auto row2 = MakeRow("uai-tp-auto-row", 26);
    auto spacer = RowLabel("uai-tp-auto-sp", "");
    row2->AddChild(spacer);
    autoInstallBox_ = MakeCheckbox("uai-tp-auto", "Download the server's choice when it changes", 340, true,
                                   [this](bool checked) { OnAutoInstallChanged(checked); });
    row2->AddChild(autoInstallBox_);
    body.AddChild(row2);
}

void UltraAITopicsPage::BuildNewModels(UltraCanvasContainer& body) {
    newLabel_ = MakeText("uai-tp-new-lbl", "", kTextWidth);
    body.AddChild(newLabel_);

    newTable_ = MakeTable("uai-tp-new", kNewRows, newModel_);
    newModel_->AddColumn(ListColumnDef("New model", 200, TextAlignment::Left, "Published by the model server since you last looked"));
    newModel_->AddColumn(ListColumnDef("Service", 90, TextAlignment::Left, "What it serves"));
    newModel_->AddColumn(ListColumnDef("Size", 70, TextAlignment::Right, "To download"));
    newModel_->AddColumn(ListColumnDef("Fits", 100, TextAlignment::Left, "Whether it fits this computer"));
    newTable_->onSelectionChanged = [this](const std::vector<int>&) {
        const int row = SelectedRow(newTable_);
        if (installNewButton_) installNewButton_->SetDisabled(row < 0 || row >= static_cast<int>(newModels_.size()));
    };
    body.AddChild(newTable_);

    auto row = MakeRow("uai-tp-new-actions", 30);
    installNewButton_ = MakeButton("uai-tp-new-install", "Install", 90, [this]() { OnInstallNewModel(); });
    row->AddChild(installNewButton_);
    row->AddChild(MakeButton("uai-tp-new-seen", "Mark all as seen", 130, [this]() { OnMarkAllSeen(); }));
    body.AddChild(row);

    auto row2 = MakeRow("uai-tp-policy-row", 30);
    row2->AddChild(RowLabel("uai-tp-policy-lbl", "New models"));
    policyDropdown_ = MakeDropdown("uai-tp-policy", kDropdownWidth);
    policyDropdown_->AddItem("Tell me about them", "notify");
    policyDropdown_->AddItem("Download a topic's new choice", "automatic");
    policyDropdown_->AddItem("Do not tell me", "ignore");
    policyDropdown_->onSelectionChanged = [this](int, const DropdownItem& item) { OnNewModelPolicy(item.value); };
    row2->AddChild(policyDropdown_);
    body.AddChild(row2);
}

// ===== REFRESHING =====

void UltraAITopicsPage::Refresh() {
    if (!alive_ || !alive_->load()) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    const LocalRuntimeSettings s = rt.GetSettings();
    filling_ = true;
    if (serverInput_ && !serverInput_->IsFocused()) serverInput_->SetText(s.galleryUrl);
    if (serverLabel_) {
        const std::string published = rt.GalleryPublished();
        serverLabel_->SetText(std::to_string(rt.ListTopics().size()) + " topics" +
                              (published.empty() ? std::string() : ", catalogue of " + published) +
                              (s.policy == DownloadPolicy::Never ? "; downloads are off (Local models page)" : ""));
    }
    const TopicPreferences prefs = rt.GetTopicPreferences();
    if (policyDropdown_) {
        for (int i = 0; i < policyDropdown_->GetItemCount(); ++i) {
            const DropdownItem* item = policyDropdown_->GetItem(i);
            if (item && item->value == ToString(prefs.newModels)) policyDropdown_->SetSelectedIndex(i, false);
        }
    }
    filling_ = false;
    RefreshTopicTable();
    RefreshNewModels();
}

std::string UltraAITopicsPage::ModelName(const std::string& modelId) const {
    if (modelId.empty()) return "—";
    LocalModelInfo m;
    return LocalRuntime::Instance().FindModel(modelId, m) ? m.displayName : modelId;
}

void UltraAITopicsPage::RefreshTopicTable() {
    if (!topicTable_ || !topicModel_) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    std::string selectedId;
    if (const ModelTopic* t = SelectedTopic()) selectedId = t->id;
    topics_ = rt.ListTopics();
    const TopicPreferences prefs = rt.GetTopicPreferences();

    std::vector<MultiColumnListItem> items;
    for (const ModelTopic& t : topics_) {
        const std::string used = rt.ModelForTopic(t.id);
        LocalModelInfo m;
        const bool known = !used.empty() && rt.FindModel(used, m);
        std::string state = !known ? "No model" : m.running ? "Running" : m.installed ? "Installed" : "Not installed";
        auto it = prefs.choices.find(t.id);
        const bool follows = it == prefs.choices.end() || it->second.modelId.empty();
        std::string name = known ? m.displayName : used;
        if (follows) name += " (server)";
        MultiColumnListItem item({t.name, CapabilityLabel(t.capability), name, state});
        item.tooltip = t.description.empty() ? t.name : t.description;
        items.push_back(std::move(item));
    }
    topicModel_->SetItems(std::move(items));
    int select = topics_.empty() ? -1 : 0;
    for (size_t i = 0; i < topics_.size(); ++i) {
        if (topics_[i].id == selectedId) select = static_cast<int>(i);
    }
    if (select >= 0 && topicTable_->GetSelection()) topicTable_->GetSelection()->Select(select);
    RefreshTopicControls();
}

const ModelTopic* UltraAITopicsPage::SelectedTopic() const {
    const int row = SelectedRow(topicTable_);
    if (row < 0 || row >= static_cast<int>(topics_.size())) return nullptr;
    return &topics_[static_cast<size_t>(row)];
}

void UltraAITopicsPage::RefreshTopicControls() {
    LocalRuntime& rt = LocalRuntime::Instance();
    const ModelTopic* topic = SelectedTopic();
    filling_ = true;
    if (modelDropdown_) {
        modelDropdown_->ClearItems();
        modelIds_.clear();
    }
    if (!topic) {
        if (topicDescription_) topicDescription_->SetText("");
        if (modelDropdown_) modelDropdown_->SetDisabled(true);
        if (installButton_) installButton_->SetDisabled(true);
        if (autoInstallBox_) autoInstallBox_->SetDisabled(true);
        filling_ = false;
        return;
    }
    const TopicPreferences prefs = rt.GetTopicPreferences();
    TopicChoice choice;
    if (auto it = prefs.choices.find(topic->id); it != prefs.choices.end()) choice = it->second;

    if (topicDescription_) {
        topicDescription_->SetText(topic->name + (topic->description.empty() ? "" : ": " + topic->description));
    }
    if (modelDropdown_) {
        modelDropdown_->SetDisabled(false);
        const std::string serverDefault = topic->ServerDefault();
        modelDropdown_->AddItem("Server's choice (" + ModelName(serverDefault) + ")", "");
        modelIds_.push_back("");
        // The models the server offers for the topic, then every other one
        // of its capability on this computer.
        const std::vector<LocalModelInfo> models = rt.ListModels(topic->capability);
        auto add = [&](const LocalModelInfo& m, bool offered) {
            if (std::find(modelIds_.begin(), modelIds_.end(), m.id) != modelIds_.end()) return;
            std::string label = m.displayName;
            if (m.fit == ModelFit::TooLarge) label += " - too large";
            else if (m.installed) label += " - installed";
            else label += " - " + Bytes(m.sizeBytes);
            if (offered) label += " *";
            modelDropdown_->AddItem(label, m.id);
            modelIds_.push_back(m.id);
        };
        for (const TopicModelRef& ref : topic->models) {
            for (const auto& m : models) {
                if (m.id == ref.modelId) add(m, true);
            }
        }
        for (const auto& m : models) add(m, false);
        if (!choice.modelId.empty() &&
            std::find(modelIds_.begin(), modelIds_.end(), choice.modelId) == modelIds_.end()) {
            modelDropdown_->AddItem(choice.modelId + " - not available", choice.modelId);
            modelIds_.push_back(choice.modelId);
        }
        int selected = 0;
        for (size_t i = 0; i < modelIds_.size(); ++i) {
            if (modelIds_[i] == choice.modelId) selected = static_cast<int>(i);
        }
        modelDropdown_->SetSelectedIndex(selected, false);
    }
    if (autoInstallBox_) {
        autoInstallBox_->SetChecked(choice.autoInstall);
        autoInstallBox_->SetDisabled(!choice.modelId.empty());
    }
    if (installButton_) {
        LocalModelInfo m;
        const std::string used = rt.ModelForTopic(topic->id);
        const bool canInstall = !used.empty() && rt.FindModel(used, m) && !m.installed &&
                                m.fit != ModelFit::TooLarge;
        installButton_->SetDisabled(!canInstall);
    }
    filling_ = false;
}

void UltraAITopicsPage::RefreshNewModels() {
    if (!newModel_) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    newModels_ = rt.NewModels();
    std::vector<MultiColumnListItem> items;
    for (const auto& m : newModels_) {
        std::string fit = m.fit == ModelFit::Recommended ? "GPU"
                        : m.fit == ModelFit::RunsOnCpu   ? "CPU"
                        : m.fit == ModelFit::TooLarge    ? "Too large" : "—";
        MultiColumnListItem item({m.displayName, CapabilityLabel(m.capability), Bytes(m.sizeBytes), fit});
        item.tooltip = m.description.empty() ? m.displayName : m.description;
        items.push_back(std::move(item));
    }
    newModel_->SetItems(std::move(items));
    if (newLabel_) {
        const TopicPreferences prefs = rt.GetTopicPreferences();
        newLabel_->SetText(prefs.newModels == NewModelPolicy::Ignore
                               ? std::string("New models from the server are not reported.")
                               : newModels_.empty() ? std::string("No new models since you last looked.")
                                                    : std::to_string(newModels_.size()) + " new model" +
                                                          (newModels_.size() == 1 ? "" : "s") + " from the server:");
    }
    if (installNewButton_) installNewButton_->SetDisabled(true);
}

// ===== ACTIONS =====

void UltraAITopicsPage::OnServerChanged() {
    if (filling_ || !serverInput_) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    LocalRuntimeSettings s = rt.GetSettings();
    std::string url = serverInput_->GetText();
    const size_t first = url.find_first_not_of(" \t");
    url = first == std::string::npos ? std::string() : url.substr(first, url.find_last_not_of(" \t") - first + 1);
    // A server's base address is enough: the gallery is under /gallery/.
    if (!url.empty() && url.find(".json") == std::string::npos) {
        while (!url.empty() && url.back() == '/') url.pop_back();
        url += "/gallery/index.json";
    }
    if (url == s.galleryUrl) return;
    if (!url.empty() && url.rfind("https://", 0) != 0) {
        SetStatus("The model server's address starts with https://.");
        return;
    }
    s.galleryUrl = url;
    rt.SetSettings(s);
    serverInput_->SetText(url);
    SetStatus(url.empty() ? "The built-in gallery is used. Refresh to load it." : "Saved. Refresh to load the server's models.");
}

void UltraAITopicsPage::OnRefreshFromServer() {
    OnServerChanged();
    if (refreshing_->exchange(true)) return;
    if (refreshButton_) refreshButton_->SetDisabled(true);
    SetStatus("Fetching the models and topics from the server…");
    auto sink = ProgressSink();
    std::thread([this, alive = alive_, refreshing = refreshing_, sink]() {
        LocalRuntime& rt = LocalRuntime::Instance();
        const Error error = rt.RefreshGallery();
        std::string text;
        if (!error.IsOk()) {
            text = "The server could not be read: " + error.message;
        } else {
            const std::vector<std::string> jobs = rt.ApplyTopicUpdates(sink);
            text = "Up to date.";
            if (!jobs.empty()) text += " Downloading " + std::to_string(jobs.size()) + " model" +
                                       (jobs.size() == 1 ? "" : "s") + " for your topics.";
        }
        OnUiThread([this, alive, refreshing, text]() {
            refreshing->store(false);
            if (!alive->load()) return;
            if (refreshButton_) refreshButton_->SetDisabled(false);
            SetStatus(text);
            Refresh();
        });
    }).detach();
}

void UltraAITopicsPage::OnTopicModelChosen(int index) {
    if (filling_) return;
    const ModelTopic* topic = SelectedTopic();
    if (!topic || index < 0 || index >= static_cast<int>(modelIds_.size())) return;
    LocalRuntime::Instance().SetTopicModel(topic->id, modelIds_[static_cast<size_t>(index)]);
    SetStatus("");
    RefreshTopicTable();
}

void UltraAITopicsPage::OnAutoInstallChanged(bool checked) {
    if (filling_) return;
    const ModelTopic* topic = SelectedTopic();
    if (!topic) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    TopicPreferences prefs = rt.GetTopicPreferences();
    prefs.choices[topic->id].autoInstall = checked;
    rt.SetTopicPreferences(prefs);
    RefreshTopicTable();
}

void UltraAITopicsPage::OnInstallTopicModel() {
    const ModelTopic* topic = SelectedTopic();
    if (!topic) return;
    const std::string id = LocalRuntime::Instance().ModelForTopic(topic->id);
    if (!id.empty()) Install({id});
}

void UltraAITopicsPage::OnInstallNewModel() {
    const int row = SelectedRow(newTable_);
    if (row < 0 || row >= static_cast<int>(newModels_.size())) return;
    const std::string id = newModels_[static_cast<size_t>(row)].id;
    LocalRuntime::Instance().MarkModelsSeen({id});
    Install({id});
}

void UltraAITopicsPage::OnMarkAllSeen() {
    LocalRuntime::Instance().MarkModelsSeen({});
    RefreshNewModels();
}

void UltraAITopicsPage::OnNewModelPolicy(const std::string& value) {
    if (filling_) return;
    NewModelPolicy policy;
    if (!ParseNewModelPolicy(value, policy)) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    TopicPreferences prefs = rt.GetTopicPreferences();
    prefs.newModels = policy;
    rt.SetTopicPreferences(prefs);
    RefreshNewModels();
}

void UltraAITopicsPage::RestoreDefaults() {
    LocalRuntime& rt = LocalRuntime::Instance();
    TopicPreferences prefs = rt.GetTopicPreferences();
    prefs.choices.clear();
    prefs.newModels = NewModelPolicy::Notify;
    rt.SetTopicPreferences(prefs);       // the seen list stays: nothing old turns new
    LocalRuntimeSettings s = rt.GetSettings();
    s.galleryUrl.clear();
    rt.SetSettings(s);
    SetStatus("Every topic follows the server again.");
    Refresh();
}

void UltraAITopicsPage::Install(const std::vector<std::string>& modelIds) {
    // Never means "installed from the settings only": this page is one, so
    // the user's click is the go-ahead, as on the Local models page.
    LocalRuntime& rt = LocalRuntime::Instance();
    for (const std::string& id : modelIds) rt.InstallModel(id, ProgressSink());
    SetStatus("Download queued; the Local models page shows its progress.");
}

InstallProgressCallback UltraAITopicsPage::ProgressSink() {
    return [this, alive = alive_](const InstallProgress& p) {
        if (p.state != InstallProgress::State::Done && p.state != InstallProgress::State::Failed &&
            p.state != InstallProgress::State::Cancelled) {
            return;
        }
        OnUiThread([this, alive, p]() {
            if (!alive->load()) return;
            if (p.state == InstallProgress::State::Done) SetStatus(p.what + ": installed.");
            else if (p.state == InstallProgress::State::Failed) SetStatus(p.what + ": " + p.error);
            else SetStatus(p.what + ": cancelled.");
            Refresh();
        });
    };
}

void UltraAITopicsPage::SetStatus(const std::string& text) {
    if (statusLabel_) {
        statusLabel_->SetText(text);
        statusLabel_->RequestRedraw();
    }
}

} // namespace UltraAIApp
