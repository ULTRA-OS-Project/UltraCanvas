// Apps/UltraAIApp/UltraAILocalModelsPage.cpp
// The settings window's "Local models" page (UltraAILocalModelsPage.h).
// Version: 0.2.0 - the engine line and Acceleration follow the selected model, per engine;
//                  catalogue sources, search and filters; Hugging Face search
// Version: 0.1.1 - speech-to-text models: labelled, added as a whisper .bin
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraAI Module

#include "UltraAILocalModelsPage.h"

#include "UltraAICatalogueSourcesDialog.h"
#include "UltraAIHuggingFaceDialog.h"
#include "UltraAILocalModelText.h"
#include "UltraAISettingsWidgets.h"

#include "UltraCanvasApplication.h"      // UltraCanvasApplicationBase::GetCurrent
#include "UltraCanvasFileLoader.h"       // OpenFileDialog
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListSelection.h"
#include "UltraCanvasUtils.h"            // OpenURL
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <functional>
#include <thread>
#include <utility>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;
using namespace SettingsWidgets;
using namespace LocalModelText;

namespace {

constexpr int kRowHeight     = 22;
constexpr int kTableHeight   = 7 * kRowHeight + 26 + 2;   // seven rows and the header
constexpr int kLabelWidth    = 150;
constexpr int kDropdownWidth = 200;

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// What Auto picks for an engine here: the computer's first accelerator
// the engine has a build for that is not an optional one.
std::string AutoAccelerator(const EngineInfo& engine, const HardwareProfile& hw) {
    std::vector<std::string> order = hw.accelerators;
    if (std::find(order.begin(), order.end(), "cpu") == order.end()) order.push_back("cpu");
    for (const std::string& accel : order) {
        for (const EngineBundleInfo& b : engine.bundles) {
            if (b.platform == hw.platform && b.accelerator == accel && !b.optional) return accel;
        }
    }
    return {};
}

// Where a model is listed from, for its tooltip.
std::string SourceLabel(const LocalModelInfo& m, const std::vector<CatalogueSourceStatus>& sources) {
    switch (m.source) {
        case LocalModelSource::Catalogue:   return Tr("Recommended");
        case LocalModelSource::File:        return Tr("Your file");
        case LocalModelSource::HuggingFace: return "Hugging Face";
        case LocalModelSource::Gallery:
            for (const auto& s : sources) {
                if (s.source.id == m.gallery) return s.source.name;
            }
            return m.gallery;
    }
    return {};
}

// Post `fn` to the UI thread when there is an event loop, else run it.
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

std::shared_ptr<UltraCanvasDropdown> MakeDropdown(const std::string& id, int width) {
    auto dd = std::make_shared<UltraCanvasDropdown>(id, 0, 0, width, 26);
    dd->size.width  = CSSLayout::Dimension::Px(width);
    dd->size.height = CSSLayout::Dimension::Px(26);
    dd->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return dd;
}

} // namespace

UltraAILocalModelsPage::UltraAILocalModelsPage(std::shared_ptr<std::atomic<bool>> alive)
    : alive_(std::move(alive)) {}

UltraAILocalModelsPage::~UltraAILocalModelsPage() = default;

std::shared_ptr<UltraCanvasContainer> UltraAILocalModelsPage::Build(UltraCanvasWindowBase* parent) {
    parent_ = parent;
    PageParts parts = MakePage("uai-set-page-local-models", Tr("Local models"),
            Tr("Models that run on this computer. Nothing to install by hand: the "
            "engine and a model are downloaded when a service first needs them, "
            "or from here."));
    // This page holds more rows than the others: a little less air between
    // them keeps it inside the window.
    parts.body->layout.SetFlexGap(6);

    BuildHardwareLine(*parts.body);
    BuildEngineRow(*parts.body);
    BuildFilterRow(*parts.body);
    BuildModelTable(*parts.body);
    BuildActionRow(*parts.body);
    BuildProgressRow(*parts.body);
    BuildAddRow(*parts.body);
    BuildSettingsRows(*parts.body);

    AddNote(parts, "uai-lm-note1",
            Tr("GPU: the model fits the graphics card's memory and runs there. CPU: "
            "it fits system memory and runs on the processor, slower. Too large: "
            "it cannot be loaded on this computer. A model is downloaded from its "
            "publisher under the licence its tooltip names."));
    AddNote(parts, "uai-lm-note2",
            Tr("Ask: a service that needs a model it does not have asks first. "
            "Automatic: it downloads and continues. Never: models are only "
            "installed from this page."));
    AddNote(parts, "uai-lm-note3",
            Tr("A running engine is shared by every UltraCanvas application that "
            "wants the same model and stopped when nothing has used it for the "
            "idle time."));

    Refresh();
    return parts.page;
}

// ===== BUILDING =====

void UltraAILocalModelsPage::BuildHardwareLine(UltraCanvasContainer& body) {
    hardwareLabel_ = MakeText("uai-lm-hardware", "", kTextWidth, kNoteFontSize, kNoteTextColor);
    body.AddChild(hardwareLabel_);
}

void UltraAILocalModelsPage::BuildFilterRow(UltraCanvasContainer& body) {
    auto row = MakeRow("uai-lm-filter-row", 28);
    searchInput_ = std::make_shared<UltraCanvasTextInput>("uai-lm-search", 0, 0, 200, 26);
    searchInput_->SetPlaceholder(Tr("Search the models"));
    searchInput_->SetFontSize(kTextFontSize);
    searchInput_->size.height = CSSLayout::Dimension::Px(26);
    searchInput_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    searchInput_->onTextChanged = [this](const std::string& text) {
        filterText_ = Lower(text);
        ApplyFilter();
    };
    row->AddChild(searchInput_);

    serviceDropdown_ = MakeDropdown("uai-lm-service", 130);
    serviceDropdown_->AddItem(Tr("All services"), "");
    serviceDropdown_->AddItem(CapabilityLabel("textllm"), "textllm");
    serviceDropdown_->AddItem(CapabilityLabel("embeddings"), "embeddings");
    serviceDropdown_->AddItem(CapabilityLabel("speechtotext"), "speechtotext");
    serviceDropdown_->SetSelectedIndex(0, false);
    serviceDropdown_->onSelectionChanged = [this](int, const DropdownItem& item) {
        filterService_ = item.value;
        ApplyFilter();
    };
    row->AddChild(serviceDropdown_);

    sourceDropdown_ = MakeDropdown("uai-lm-source", 150);
    sourceDropdown_->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (index < 0 || index >= static_cast<int>(sourceFilterIds_.size())) return;
        filterSource_ = sourceFilterIds_[static_cast<size_t>(index)];
        ApplyFilter();
    };
    row->AddChild(sourceDropdown_);
    body.AddChild(row);
}

void UltraAILocalModelsPage::BuildEngineRow(UltraCanvasContainer& body) {
    engineLabel_ = MakeText("uai-lm-engine", "", kTextWidth);
    body.AddChild(engineLabel_);

    auto row = MakeRow("uai-lm-accel-row", 28);
    auto lbl = MakeLabel("uai-lm-accel-lbl", Tr("Acceleration"));
    lbl->size.width = CSSLayout::Dimension::Px(kLabelWidth);
    row->AddChild(lbl);
    accelDropdown_ = MakeDropdown("uai-lm-accel", kDropdownWidth);
    accelDropdown_->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (index < 0 || index >= static_cast<int>(accelIds_.size()) || engineId_.empty()) return;
        // The choice is the shown engine's own: another engine's GPU build
        // may not suit this computer.
        LocalRuntime& rt = LocalRuntime::Instance();
        LocalRuntimeSettings s = rt.GetSettings();
        s.engineAccelerators[engineId_] = accelIds_[static_cast<size_t>(index)];
        rt.SetSettings(s);
        RefreshEngine();
    };
    row->AddChild(accelDropdown_);
    engineButton_ = MakeButton("uai-lm-engine-btn", Tr("Install engine"), 114, [this]() { OnInstallEngine(); });
    row->AddChild(engineButton_);
    body.AddChild(row);
}

void UltraAILocalModelsPage::BuildModelTable(UltraCanvasContainer& body) {
    tableModel_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    tableModel_->AddColumn(ListColumnDef(Tr("Model"), 168, TextAlignment::Left, Tr("The model, as the catalogue names it")));
    tableModel_->AddColumn(ListColumnDef(Tr("Service"), 80, TextAlignment::Left, Tr("What it serves")));
    tableModel_->AddColumn(ListColumnDef(Tr("Size"), 62, TextAlignment::Right, Tr("On disk, or to download")));
    tableModel_->AddColumn(ListColumnDef(Tr("Fits"), 58, TextAlignment::Left, Tr("Whether it fits this computer, and where it runs")));
    tableModel_->AddColumn(ListColumnDef(Tr("State"), 98, TextAlignment::Left, Tr("Installed, running, or not yet installed")));

    table_ = std::make_shared<UltraCanvasListView>("uai-lm-table", 0, 0, kTextWidth, kTableHeight);
    table_->SetModel(tableModel_);
    ListViewStyle style;
    style.showHeader     = true;
    style.showGridLines  = false;
    style.alternateRowColors = true;
    style.headerFontSize = kTextFontSize;
    style.headerHeight   = 26;
    style.rowHeight      = kRowHeight;
    table_->SetStyle(style);
    auto delegate = std::make_shared<UltraCanvasDefaultListDelegate>();
    delegate->SetFontSize(kTextFontSize);
    table_->SetDelegate(delegate);
    table_->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    table_->size.width  = CSSLayout::Dimension::Px(kTextWidth);
    table_->size.height = CSSLayout::Dimension::Px(kTableHeight);
    table_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    table_->onSelectionChanged = [this](const std::vector<int>&) { OnSelectionChanged(); };
    body.AddChild(table_);
}

void UltraAILocalModelsPage::BuildActionRow(UltraCanvasContainer& body) {
    auto row = MakeRow("uai-lm-actions", 30);
    installButton_ = MakeButton("uai-lm-install", Tr("Install"), 90, [this]() { OnInstall(); });
    removeButton_  = MakeButton("uai-lm-remove", Tr("Remove"), 90, [this]() { OnRemove(); });
    stopButton_    = MakeButton("uai-lm-stop", Tr("Stop"), 70, [this]() { OnStop(); });
    forgetButton_  = MakeButton("uai-lm-forget", Tr("Forget"), 80, [this]() { OnForget(); });
    row->AddChild(installButton_);
    row->AddChild(removeButton_);
    row->AddChild(stopButton_);
    row->AddChild(forgetButton_);
    body.AddChild(row);
    // What the last action did, on a line of its own: it names models and
    // sources and needs the page's width.
    statusLabel_ = MakeText("uai-lm-status", "", kTextWidth, kNoteFontSize, kNoteTextColor);
    statusLabel_->SetVisible(false);        // no line while there is nothing to say
    body.AddChild(statusLabel_);
}

void UltraAILocalModelsPage::BuildProgressRow(UltraCanvasContainer& body) {
    progressRow_ = MakeRow("uai-lm-progress-row", 26);
    progressLabel_ = MakeLabel("uai-lm-progress-lbl", "", kNoteFontSize, kNoteTextColor);
    progressLabel_->size.width = CSSLayout::Dimension::Px(250);
    progressRow_->AddChild(progressLabel_);
    // The framework's progress bar is the gauge in LinearBar mode; at this
    // height it is just the bar, the words are the label's.
    progressBar_ = CreateGaugeDiagramElement("uai-lm-progress", 0, 0, 140, 8);
    progressBar_->SetMode(GaugeMode::LinearBar);
    progressBar_->SetOrientation(GaugeOrientation::Horizontal);
    progressBar_->SetMinValue(0.0);
    progressBar_->SetMaxValue(100.0);
    progressBar_->SetValue(0.0);
    progressBar_->SetTitle("");
    progressBar_->SetUnit("");
    progressBar_->SetGaugeColor(Color(37, 99, 235, 255));
    progressBar_->size.width  = CSSLayout::Dimension::Px(140);
    progressBar_->size.height = CSSLayout::Dimension::Px(8);
    progressBar_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    progressRow_->AddChild(progressBar_);
    cancelButton_ = MakeButton("uai-lm-cancel", Tr("Cancel"), 80, [this]() {
        if (!activeJobId_.empty()) LocalRuntime::Instance().Cancel(activeJobId_);
    });
    progressRow_->AddChild(cancelButton_);
    progressRow_->SetVisible(false);
    body.AddChild(progressRow_);
}

void UltraAILocalModelsPage::BuildAddRow(UltraCanvasContainer& body) {
    auto row = MakeRow("uai-lm-add-row", 30);
    row->AddChild(MakeButton("uai-lm-add-file", Tr("Add model file"), 108, [this]() { OnAddFile(); }));
    row->AddChild(MakeButton("uai-lm-add-hf", Tr("Search Hugging Face"), 142, [this]() { OnAddHuggingFace(); }));
    row->AddChild(MakeButton("uai-lm-refresh", Tr("Refresh catalogue"), 126, [this]() { OnRefreshCatalogue(); }));
    row->AddChild(MakeButton("uai-lm-sources", Tr("Sources"), 72, [this]() { OnSources(); }));
    body.AddChild(row);
    // The catalogue sources and what each holds.
    sourcesLabel_ = MakeText("uai-lm-sources-line", "", kTextWidth, kNoteFontSize, kNoteTextColor);
    body.AddChild(sourcesLabel_);
}

void UltraAILocalModelsPage::BuildSettingsRows(UltraCanvasContainer& body) {
    {
        auto row = MakeRow("uai-lm-policy-row", 28);
        auto lbl = MakeLabel("uai-lm-policy-lbl", Tr("Downloads"));
        lbl->size.width = CSSLayout::Dimension::Px(kLabelWidth);
        row->AddChild(lbl);
        policyDropdown_ = MakeDropdown("uai-lm-policy", kDropdownWidth);
        policyDropdown_->AddItem(Tr("Ask before downloading"), "ask");
        policyDropdown_->AddItem(Tr("Download automatically"), "automatic");
        policyDropdown_->AddItem(Tr("Never - install from here only"), "never");
        policyDropdown_->onSelectionChanged = [this](int, const DropdownItem& item) {
            DownloadPolicy policy;
            if (!ParseDownloadPolicy(item.value, policy)) return;
            LocalRuntime& rt = LocalRuntime::Instance();
            LocalRuntimeSettings s = rt.GetSettings();
            s.policy = policy;
            rt.SetSettings(s);
        };
        row->AddChild(policyDropdown_);
        body.AddChild(row);
    }
    {
        auto row = MakeRow("uai-lm-idle-row", 28);
        auto lbl = MakeLabel("uai-lm-idle-lbl", Tr("Stop idle engines after"));
        lbl->size.width = CSSLayout::Dimension::Px(kLabelWidth);
        row->AddChild(lbl);
        idleDropdown_ = MakeDropdown("uai-lm-idle", kDropdownWidth);
        idleDropdown_->AddItem(Tr("5 minutes"), "5");
        idleDropdown_->AddItem(Tr("15 minutes"), "15");
        idleDropdown_->AddItem(Tr("1 hour"), "60");
        idleDropdown_->AddItem(Tr("4 hours"), "240");
        idleDropdown_->AddItem(Tr("Never"), "525600");
        idleDropdown_->onSelectionChanged = [this](int, const DropdownItem& item) {
            const int minutes = std::atoi(item.value.c_str());
            if (minutes <= 0) return;
            LocalRuntime& rt = LocalRuntime::Instance();
            LocalRuntimeSettings s = rt.GetSettings();
            s.idleMinutes = minutes;
            rt.SetSettings(s);
        };
        row->AddChild(idleDropdown_);
        body.AddChild(row);
    }
    {
        auto row = MakeRow("uai-lm-disk-row", 28);
        diskLabel_ = MakeLabel("uai-lm-disk", "", kNoteFontSize, kNoteTextColor);
        diskLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(diskLabel_);
        row->AddChild(MakeButton("uai-lm-open-folder", Tr("Open folder"), 110, [this]() { OnOpenFolder(); }));
        body.AddChild(row);
    }
}

// ===== REFRESHING =====

void UltraAILocalModelsPage::Refresh() {
    if (!alive_ || !alive_->load()) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    const HardwareProfile& hw = rt.Hardware();
    std::string line = TrF("This computer: {0} memory", Bytes(hw.memoryBytes));
    if (hw.physicalCores) line += ", " + TrN("{n} core", "{n} cores", hw.physicalCores);
    for (const auto& gpu : hw.gpus) {
        line += ", " + (gpu.model.empty() ? gpu.vendor : gpu.model);
        if (gpu.videoMemoryBytes) line += " (" + Bytes(gpu.videoMemoryBytes) + ")";
    }
    if (hw.gpus.empty()) line += Tr(", no GPU found");
    if (!hw.accelerators.empty()) line += " - " + TrF("{0} by default", AcceleratorLabel(hw.accelerators.front()));
    if (hardwareLabel_) hardwareLabel_->SetText(line);

    const LocalRuntimeSettings s = rt.GetSettings();
    if (policyDropdown_) {
        for (int i = 0; i < policyDropdown_->GetItemCount(); ++i) {
            const DropdownItem* item = policyDropdown_->GetItem(i);
            if (item && item->value == ToString(s.policy)) {
                policyDropdown_->SetSelectedIndex(i, false);
                break;
            }
        }
    }
    if (idleDropdown_) {
        int best = 1;
        for (int i = 0; i < idleDropdown_->GetItemCount(); ++i) {
            const DropdownItem* item = idleDropdown_->GetItem(i);
            if (item && std::atoi(item->value.c_str()) == s.idleMinutes) best = i;
        }
        idleDropdown_->SetSelectedIndex(best, false);
    }

    RefreshFilters();
    RefreshTable();
    RefreshEngine();
    RefreshDisk();
    RefreshSourcesLine();
}

void UltraAILocalModelsPage::RefreshFilters() {
    if (!sourceDropdown_) return;
    // All, the recommended ones, each catalogue source that is turned on,
    // the user's own, what is installed.
    sourceDropdown_->ClearItems();
    sourceFilterIds_.clear();
    auto add = [&](const std::string& label, const std::string& id) {
        sourceDropdown_->AddItem(label, id);
        sourceFilterIds_.push_back(id);
    };
    add(Tr("All sources"), "");
    add(Tr("Recommended"), "@recommended");
    for (const CatalogueSourceStatus& s : LocalRuntime::Instance().ListCatalogueSources()) {
        if (s.source.enabled) add(s.source.name, s.source.id);
    }
    add(Tr("Your models"), "@yours");
    add(Tr("Installed"), "@installed");
    int selected = 0;
    for (size_t i = 0; i < sourceFilterIds_.size(); ++i) {
        if (sourceFilterIds_[i] == filterSource_) selected = static_cast<int>(i);
    }
    // A source that is gone takes its filter with it.
    if (selected == 0) filterSource_.clear();
    sourceDropdown_->SetSelectedIndex(selected, false);
}

std::string UltraAILocalModelsPage::ShownEngineId() const {
    if (const LocalModelInfo* m = SelectedModel()) return m->engineId;
    // Nothing selected: the engine shown last, else the first listed model's.
    if (!engineId_.empty()) return engineId_;
    if (!rows_.empty()) return rows_.front().engineId;
    return "llama-server";
}

void UltraAILocalModelsPage::RefreshEngine() {
    LocalRuntime& rt = LocalRuntime::Instance();
    const HardwareProfile& hw = rt.Hardware();
    engineId_ = ShownEngineId();
    const LocalModelInfo* selected = SelectedModel();

    std::vector<EngineInfo> engines = rt.ListEngines();
    const EngineInfo* engine = nullptr;
    for (const auto& e : engines) {
        if (e.id == engineId_) engine = &e;
    }
    // The builds this engine has for this computer, one per accelerator.
    std::vector<const EngineBundleInfo*> builds;
    if (engine) {
        for (const auto& b : engine->bundles) {
            if (b.platform != hw.platform) continue;
            if (std::any_of(builds.begin(), builds.end(),
                            [&](const EngineBundleInfo* o) { return o->accelerator == b.accelerator; })) continue;
            builds.push_back(&b);
        }
    }
    EngineBundleInfo chosen;
    const bool haveChoice = rt.ChooseBundle(engineId_, chosen);

    if (accelDropdown_) {
        accelDropdown_->ClearItems();
        accelIds_.clear();
        if (builds.size() <= 1) {
            // One build, or none: nothing to choose. It is what is installed
            // with the engine's first model.
            const std::string only = builds.empty() ? std::string() : builds.front()->accelerator;
            accelDropdown_->AddItem(builds.empty() ? Tr("No build for this computer") : AcceleratorLabel(only), only);
            accelIds_.push_back(only);
            accelDropdown_->SetSelectedIndex(0, false);
            accelDropdown_->SetDisabled(true);
        } else {
            // Auto, then every accelerator the gallery has a build for here.
            std::string autoLabel = Tr("Automatic");
            const std::string autoAccel = AutoAccelerator(*engine, hw);
            if (!autoAccel.empty()) autoLabel += " (" + AcceleratorLabel(autoAccel) + ")";
            accelDropdown_->AddItem(autoLabel, "");
            accelIds_.push_back("");
            for (const EngineBundleInfo* b : builds) {
                std::string label = AcceleratorLabel(b->accelerator);
                if (b->optional) label += ", " + Bytes(b->sizeBytes);
                accelDropdown_->AddItem(label, b->accelerator);
                accelIds_.push_back(b->accelerator);
            }
            const std::string preferred = rt.PreferredAccelerator(engineId_);
            int index = 0;
            for (size_t i = 0; i < accelIds_.size(); ++i) {
                if (accelIds_[i] == preferred) index = static_cast<int>(i);
            }
            accelDropdown_->SetSelectedIndex(index, false);
            accelDropdown_->SetDisabled(false);
        }
        accelDropdown_->RequestRedraw();
    }

    // The engine line and its button.
    const std::string subject = selected ? TrF("Engine for {0}", selected->displayName) : Tr("Engine");
    std::string text;
    bool installed = false;
    if (!engine) {
        text = TrF("{0}: {1} is not in the engine gallery.", subject, engineId_);
    } else if (!haveChoice) {
        text = TrF("{0}: {1} {2} - no build for {3}.", subject, engine->id, engine->version, hw.platform);
    } else {
        installed = chosen.installed;
        text = installed ? TrF("{0}: {1} {2} - {3}, installed", subject, engine->id, engine->version,
                               AcceleratorLabel(chosen.accelerator))
                         : TrF("{0}: {1} {2} - {3}, not installed ({4}, downloaded with its first model)",
                               subject, engine->id, engine->version, AcceleratorLabel(chosen.accelerator),
                               Bytes(chosen.sizeBytes));
        // Other installed bundles are worth a word: they take disk.
        for (const auto& b : engine->bundles) {
            if (b.installed && b.accelerator != chosen.accelerator) {
                text += "; " + TrF("{0} also installed", AcceleratorLabel(b.accelerator));
            }
        }
    }
    if (engineLabel_) engineLabel_->SetText(text);
    if (engineButton_) {
        engineButton_->SetText(installed ? Tr("Remove engine") : Tr("Install engine"));
        engineButton_->SetDisabled(!haveChoice || !activeJobId_.empty() || busy_);
        engineButton_->RequestRedraw();
    }
}

void UltraAILocalModelsPage::RefreshTable() {
    if (!table_ || !tableModel_) return;
    models_ = LocalRuntime::Instance().ListModels();
    // Installed first, then the recommended ones, the user's own, and the
    // models of the catalogue sources; each in its order.
    auto rank = [](const LocalModelInfo& m) {
        if (m.installed) return 0;
        switch (m.source) {
            case LocalModelSource::Catalogue:   return 1;
            case LocalModelSource::File:
            case LocalModelSource::HuggingFace: return 2;
            case LocalModelSource::Gallery:     return 3;
        }
        return 3;
    };
    std::stable_sort(models_.begin(), models_.end(), [&](const LocalModelInfo& a, const LocalModelInfo& b) {
        return rank(a) < rank(b);
    });
    ApplyFilter();
}

void UltraAILocalModelsPage::ApplyFilter() {
    if (!table_ || !tableModel_) return;
    std::string selectedId;
    if (const LocalModelInfo* current = SelectedModel()) selectedId = current->id;

    // Every word of the search has to be somewhere in the model's name,
    // its id, its tags or its licence.
    std::vector<std::string> words;
    for (size_t start = 0; start < filterText_.size();) {
        const size_t end = filterText_.find(' ', start);
        const std::string word = filterText_.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!word.empty()) words.push_back(word);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    auto shown = [&](const LocalModelInfo& m) {
        if (!filterService_.empty() && m.capability != filterService_) return false;
        if (filterSource_ == "@recommended") {
            if (m.source != LocalModelSource::Catalogue) return false;
        } else if (filterSource_ == "@yours") {
            if (m.source != LocalModelSource::File && m.source != LocalModelSource::HuggingFace) return false;
        } else if (filterSource_ == "@installed") {
            if (!m.installed) return false;
        } else if (!filterSource_.empty()) {
            if (m.source != LocalModelSource::Gallery || m.gallery != filterSource_) return false;
        }
        if (words.empty()) return true;
        std::string haystack = m.displayName + " " + m.id + " " + m.license;
        for (const std::string& tag : m.tags) haystack += " " + tag;
        haystack = Lower(haystack);
        for (const std::string& word : words) {
            if (haystack.find(word) == std::string::npos) return false;
        }
        return true;
    };

    const std::vector<CatalogueSourceStatus> sources = LocalRuntime::Instance().ListCatalogueSources();
    rows_.clear();
    std::vector<MultiColumnListItem> items;
    for (const auto& m : models_) {
        if (!shown(m)) continue;
        std::string state = m.running ? Tr("Running") : m.installed ? Tr("Installed") : Tr("Not installed");
        if (m.fit == ModelFit::TooLarge && !m.installed) state = Tr("Too large");
        std::string name = m.displayName;
        if (m.source == LocalModelSource::File) name = TrF("{0} (your file)", name);
        if (m.source == LocalModelSource::HuggingFace) name = TrF("{0} (Hugging Face)", name);
        MultiColumnListItem item({name, CapabilityLabel(m.capability), Bytes(m.sizeBytes),
                                  FitLabel(m.fit), state});
        // Where it is from, its licence and what it is, on the name.
        std::string about = SourceLabel(m, sources);
        if (!m.license.empty()) about += " - " + TrF("Licence: {0}", m.license);
        std::string description = m.description;
        if (description.size() > 420) {
            size_t cutAt = 420;
            while (cutAt > 0 && (static_cast<unsigned char>(description[cutAt]) & 0xC0) == 0x80) --cutAt;
            description = description.substr(0, cutAt) + "…";
        }
        item.tooltip = description.empty() ? m.displayName : description;
        // The whole name too: the column is narrower than a gallery's names.
        item.SetCellTooltip(0, m.displayName + "\n" + about + (description.empty() ? "" : "\n" + description));
        item.SetCellTooltip(2, m.sizeBytes ? Bytes(m.sizeBytes)
                                           : Tr("Its catalogue names no size; it is asked of the host when the row is selected"));
        item.SetCellTooltip(3, FitTooltip(m.fit));
        if (m.installed) item.SetCellTooltip(4, m.installedPath);
        items.push_back(std::move(item));
        rows_.push_back(m);
    }
    tableModel_->SetItems(std::move(items));

    // Keep the row the user had.
    bool kept = false;
    if (!selectedId.empty() && table_->GetSelection()) {
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].id == selectedId) {
                table_->GetSelection()->Select(static_cast<int>(i));
                kept = true;
                break;
            }
        }
    }
    if (!kept && table_->GetSelection()) table_->GetSelection()->Clear();
    RefreshButtons();
    RefreshEngine();
}

void UltraAILocalModelsPage::OnSelectionChanged() {
    RefreshButtons();
    RefreshEngine();
    ProbeSelected();
}

void UltraAILocalModelsPage::ProbeSelected() {
    const LocalModelInfo* m = SelectedModel();
    if (!m || probing_ || m->installed || m->source == LocalModelSource::File) return;
    const bool unsized = std::any_of(m->files.begin(), m->files.end(),
                                     [](const LocalModelFile& f) { return f.sizeBytes == 0; });
    if (!unsized || !probed_.insert(m->id).second) return;
    // A LocalAI gallery lists no sizes: the host is asked, once per model,
    // so that the size and the fit are known before anything is downloaded.
    probing_ = true;
    const std::string id = m->id;
    std::thread worker([this, alive = alive_, id]() {
        LocalModelInfo model;
        const Error error = LocalRuntime::Instance().ProbeModel(id, model);
        OnUiThread([this, alive, error, sized = model.sizeBytes > 0]() {
            if (!alive->load()) return;
            probing_ = false;
            if (sized) RefreshTable();
            else if (!error.IsOk()) SetStatus(TrF("The size could not be asked for: {0}", error.message));
            // The selection may have moved on while the host answered.
            ProbeSelected();
        });
    });
    worker.detach();
}

void UltraAILocalModelsPage::SelectModel(const std::string& modelId) {
    auto find = [&]() {
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].id == modelId) return static_cast<int>(i);
        }
        return -1;
    };
    int row = find();
    if (row < 0) {
        // Hidden by the search or a filter: show everything.
        filterText_.clear();
        filterService_.clear();
        filterSource_.clear();
        if (searchInput_) searchInput_->SetText("");
        if (serviceDropdown_) serviceDropdown_->SetSelectedIndex(0, false);
        if (sourceDropdown_) sourceDropdown_->SetSelectedIndex(0, false);
        ApplyFilter();
        row = find();
    }
    if (row < 0 || !table_ || !table_->GetSelection()) return;
    table_->GetSelection()->Select(row);
    table_->ScrollToRow(row);
}

void UltraAILocalModelsPage::RefreshButtons() {
    const LocalModelInfo* m = SelectedModel();
    const bool jobRunning = !activeJobId_.empty();
    auto set = [&](const std::shared_ptr<UltraCanvasButton>& b, bool enabled) {
        if (!b) return;
        b->SetDisabled(!enabled || busy_);
        b->RequestRedraw();
    };
    set(installButton_, m && !m->installed && m->fit != ModelFit::TooLarge && !jobRunning);
    set(removeButton_,  m && m->installed && m->source != LocalModelSource::File && !jobRunning);
    set(stopButton_,    m && m->running);
    // Forget is for what the user added; a catalogue's model is only removed.
    set(forgetButton_,  m && (m->source == LocalModelSource::File || m->source == LocalModelSource::HuggingFace) &&
                        !jobRunning);
}

void UltraAILocalModelsPage::RefreshDisk() {
    if (!diskLabel_) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    const LocalRuntimeSettings s = rt.GetSettings();
    const uint64_t used = rt.DiskUsedBytes();
    std::string text = used ? (s.diskCapBytes ? TrF("Disk: {0} used of a {1} limit", Bytes(used), Bytes(s.diskCapBytes))
                                             : TrF("Disk: {0} used", Bytes(used)))
                            : Tr("Disk: nothing used yet");
    diskLabel_->SetText(text);
}

void UltraAILocalModelsPage::RefreshSourcesLine() {
    if (!sourcesLabel_) return;
    // The recommended list and each catalogue source: what it holds, since when.
    LocalRuntime& rt = LocalRuntime::Instance();
    int recommended = 0;
    for (const auto& m : models_) {
        if (m.source == LocalModelSource::Catalogue) ++recommended;
    }
    std::string text = TrN("{n} recommended model", "{n} recommended models", recommended);
    for (const CatalogueSourceStatus& s : rt.ListCatalogueSources()) {
        if (!s.source.enabled) continue;
        text += "; ";
        text += s.fetched.empty()
            ? TrF("{0}: not downloaded yet", s.source.name)
            : TrF("{0}: {1} models of {2}", s.source.name, s.models, s.fetched);
    }
    sourcesLabel_->SetText(text);
}

void UltraAILocalModelsPage::ShowProgress(const InstallProgress& p) {
    if (!progressRow_) return;
    std::string text = p.what;
    switch (p.state) {
        case InstallProgress::State::Queued:      text = TrF("{0}: queued", p.what); break;
        case InstallProgress::State::Downloading: {
            if (p.bytesTotal) {
                const int percent = static_cast<int>(p.bytesDone * 100 / p.bytesTotal);
                text = TrF("{0}: downloading {1}% of {2}", p.what, percent, Bytes(p.bytesTotal));
                if (progressBar_) progressBar_->SetValue(percent);
            } else {
                text = TrF("{0}: downloading {1}", p.what, Bytes(p.bytesDone));
            }
            break;
        }
        case InstallProgress::State::Verifying:   text = TrF("{0}: verifying", p.what); if (progressBar_) progressBar_->SetValue(100); break;
        case InstallProgress::State::Unpacking:   text = TrF("{0}: unpacking", p.what); break;
        default: break;
    }
    if (progressLabel_) progressLabel_->SetText(text);
    progressRow_->SetVisible(true);
    progressRow_->RequestRedraw();
}

void UltraAILocalModelsPage::HideProgress() {
    if (progressRow_) {
        progressRow_->SetVisible(false);
        progressRow_->RequestRedraw();
    }
    if (progressBar_) progressBar_->SetValue(0);
}

void UltraAILocalModelsPage::SetStatus(const std::string& text) {
    if (statusLabel_) {
        statusLabel_->SetText(text);
        statusLabel_->SetVisible(!text.empty());
        statusLabel_->RequestRedraw();
    }
    // One line of words at a time: what the last action did, or else what
    // the catalogue holds. The page has no room for both.
    if (sourcesLabel_) {
        sourcesLabel_->SetVisible(text.empty());
        sourcesLabel_->RequestRedraw();
    }
}

// ===== ACTIONS =====

const LocalModelInfo* UltraAILocalModelsPage::SelectedModel() const {
    if (!table_ || !table_->GetSelection()) return nullptr;
    const int row = table_->GetSelection()->GetCurrentRow();
    if (row < 0 || row >= static_cast<int>(rows_.size())) return nullptr;
    return &rows_[static_cast<size_t>(row)];
}

InstallProgressCallback UltraAILocalModelsPage::ProgressSink() {
    // The runtime's worker reports here; the page is touched on the UI
    // thread only, and not after the window closed.
    return [this, alive = alive_](const InstallProgress& p) {
        OnUiThread([this, alive, p]() {
            if (!alive->load()) return;
            switch (p.state) {
                case InstallProgress::State::Done:
                    activeJobId_.clear();
                    HideProgress();
                    SetStatus(TrF("{0} installed.", p.what));
                    Refresh();
                    break;
                case InstallProgress::State::Failed:
                    activeJobId_.clear();
                    HideProgress();
                    SetStatus(TrF("Failed: {0}", p.error));
                    Refresh();
                    break;
                case InstallProgress::State::Cancelled:
                    activeJobId_.clear();
                    HideProgress();
                    SetStatus(TrF("{0}: cancelled. A partial download is kept and resumed next time.", p.what));
                    Refresh();
                    break;
                default:
                    activeJobId_ = p.jobId;
                    ShowProgress(p);
                    RefreshButtons();
                    break;
            }
        });
    };
}

void UltraAILocalModelsPage::OnInstall() {
    const LocalModelInfo* m = SelectedModel();
    if (!m || !activeJobId_.empty()) return;
    SetStatus("");
    activeJobId_ = LocalRuntime::Instance().InstallModel(m->id, ProgressSink());
    RefreshButtons();
}

void UltraAILocalModelsPage::OnInstallEngine() {
    LocalRuntime& rt = LocalRuntime::Instance();
    EngineBundleInfo chosen;
    const std::string engineId = engineId_;
    if (engineId.empty() || !rt.ChooseBundle(engineId, chosen) || !activeJobId_.empty()) return;
    if (chosen.installed) {
        const std::string accelerator = chosen.accelerator;
        RunThenRefresh([engineId, accelerator]() {
            const Error error = LocalRuntime::Instance().RemoveEngine(engineId, accelerator);
            return error.IsOk() ? TrF("{0} removed.", engineId) : error.message;
        });
        return;
    }
    SetStatus("");
    activeJobId_ = rt.InstallEngine(engineId, chosen.accelerator, ProgressSink());
    RefreshButtons();
    RefreshEngine();
}

void UltraAILocalModelsPage::RunThenRefresh(std::function<std::string()> work) {
    if (busy_) return;
    busy_ = true;
    RefreshButtons();
    RefreshEngine();
    std::thread worker([this, alive = alive_, work = std::move(work)]() {
        const std::string status = work();
        OnUiThread([this, alive, status]() {
            if (!alive->load()) return;
            busy_ = false;
            SetStatus(status);
            Refresh();
        });
    });
    worker.detach();
}

void UltraAILocalModelsPage::OnRemove() {
    const LocalModelInfo* m = SelectedModel();
    if (!m) return;
    const std::string id = m->id, name = m->displayName;
    RunThenRefresh([id, name]() {
        const Error error = LocalRuntime::Instance().RemoveModel(id);
        return error.IsOk() ? TrF("{0} removed.", name) : error.message;
    });
}

void UltraAILocalModelsPage::OnStop() {
    const LocalModelInfo* m = SelectedModel();
    if (!m) return;
    const std::string id = m->id, name = m->displayName;
    RunThenRefresh([id, name]() {
        LocalRuntime::Instance().StopModel(id);
        return TrF("{0} stopped.", name);
    });
}

void UltraAILocalModelsPage::OnForget() {
    const LocalModelInfo* m = SelectedModel();
    if (!m) return;
    const std::string id = m->id, name = m->displayName;
    RunThenRefresh([id, name]() {
        const Error error = LocalRuntime::Instance().ForgetModel(id);
        return error.IsOk() ? TrF("{0} forgotten.", name) : error.message;
    });
}

void UltraAILocalModelsPage::OnAddFile() {
    FileDialogOptions opts;
    opts.SetTitle(Tr("Add a model file"))
        .SetParentWindow(parent_)
        .AddFilter(Tr("GGUF models"), std::vector<std::string>{"gguf"})
        .AddFilter(Tr("Whisper models"), std::vector<std::string>{"bin"})
        .AddFilter(Tr("All files"), "*");
    UltraCanvasFileLoader::OpenFileDialog(
        opts, [this, alive = alive_](DialogResult result, const std::string& path) {
            if (!alive->load()) return;
            if (result != DialogResult::OK || path.empty()) return;
            // A whisper.cpp model (a ggml .bin) and an embedding model are
            // told apart by their names; everything else is a chat model. The
            // user corrects it with Forget and a second add under the other
            // service when the guess is wrong.
            std::string lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool bin = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".bin") == 0;
            const std::string capability = bin || lower.find("whisper") != std::string::npos ? "speechtotext"
                                         : lower.find("embed") != std::string::npos           ? "embeddings"
                                                                                              : "textllm";
            std::string id;
            const Error error = LocalRuntime::Instance().AddModelFile(path, capability, "", id);
            SetStatus(error.IsOk() ? TrF("Added {0} as a {1} model.", path, CapabilityLabel(capability))
                                   : error.message);
            Refresh();
        });
}

void UltraAILocalModelsPage::OnAddHuggingFace() {
    // The dialog searches, lists a repository's files and registers the one
    // picked; this page then shows it and, when asked, installs it.
    auto dialog = std::make_shared<UltraAIHuggingFaceDialog>(
        [this, alive = alive_](const std::string& modelId, const std::string& name, bool install) {
            if (!alive->load()) return;
            Refresh();
            SelectModel(modelId);
            if (install && activeJobId_.empty()) {
                OnInstall();
            } else {
                SetStatus(TrF("Added {0}; select it and Install.", name));
            }
        });
    dialog->CreateHuggingFaceDialog();
    dialog->ShowModal(parent_);
}

void UltraAILocalModelsPage::OnRefreshCatalogue() {
    SetStatus(Tr("Downloading the catalogues…"));
    RunThenRefresh([]() {
        LocalRuntime& rt = LocalRuntime::Instance();
        std::string text;
        // The engine gallery of a model server, when the settings name one.
        const Error gallery = rt.RefreshGallery();
        if (!gallery.IsOk()) text = TrF("Engine gallery: {0}", gallery.message);
        std::vector<CatalogueSourceStatus> sources;
        rt.RefreshCatalogueSources(&sources);
        if (sources.empty()) {
            if (!text.empty()) text += " ";
            text += Tr("No catalogue source is turned on; Sources adds one.");
        }
        for (const CatalogueSourceStatus& s : sources) {
            if (!text.empty()) text += " ";
            if (!s.error.IsOk()) {
                text += TrF("{0}: {1}.", s.source.name, s.error.message);
            } else if (s.skipped > 0) {
                text += TrF("{0}: {1} models ({2} more need an engine UltraAI does not have).",
                            s.source.name, s.models, s.skipped);
            } else {
                text += TrF("{0}: {1} models.", s.source.name, s.models);
            }
        }
        return text;
    });
}

void UltraAILocalModelsPage::OnSources() {
    auto dialog = std::make_shared<UltraAICatalogueSourcesDialog>([this, alive = alive_]() {
        if (alive->load()) Refresh();
    });
    dialog->CreateSourcesDialog();
    dialog->ShowModal(parent_);
}

void UltraAILocalModelsPage::OnOpenFolder() {
    OpenURL("file://" + LocalRuntime::Instance().RootFolder());
}

void UltraAILocalModelsPage::RestoreDefaults() {
    LocalRuntime& rt = LocalRuntime::Instance();
    LocalRuntimeSettings s = rt.GetSettings();
    s.policy = DownloadPolicy::Ask;
    s.preferredAccelerator.clear();
    s.engineAccelerators.clear();
    s.catalogueSources = DefaultCatalogueSources();
    s.idleMinutes = 15;
    rt.SetSettings(s);
    Refresh();
}

} // namespace UltraAIApp
