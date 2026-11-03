// Apps/UltraAIApp/UltraAILocalModelsPage.cpp
// The settings window's "Local models" page (UltraAILocalModelsPage.h).
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraAI Module

#include "UltraAILocalModelsPage.h"

#include "UltraAISettingsWidgets.h"

#include "UltraCanvasApplication.h"      // UltraCanvasApplicationBase::GetCurrent
#include "UltraCanvasFileLoader.h"       // OpenFileDialog
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListSelection.h"
#include "UltraCanvasModalDialog.h"      // ShowInputDialog
#include "UltraCanvasUtils.h"            // OpenURL

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
constexpr int kTableHeight   = 7 * kRowHeight + 26 + 2;   // seven rows and the header
constexpr int kLabelWidth    = 150;
constexpr int kDropdownWidth = 200;

std::string Bytes(uint64_t bytes) {
    if (!bytes) return "—";
    char buffer[32];
    if (bytes >= 1024ull * 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));   // locale-ok: shown to the user
    } else if (bytes >= 1024ull * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024));   // locale-ok: shown to the user
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f KB", static_cast<double>(bytes) / 1024.0);   // locale-ok: shown to the user
    }
    return buffer;
}

std::string AcceleratorLabel(const std::string& id) {
    if (id == "cpu")    return "CPU only";
    if (id == "vulkan") return "Vulkan (any GPU)";
    if (id == "metal")  return "Metal (Apple GPU)";
    if (id == "cuda12") return "CUDA 12 (NVIDIA)";
    if (id == "rocm")   return "ROCm (AMD)";
    return id;
}

std::string CapabilityLabel(const std::string& capability) {
    if (capability == "textllm")    return "Chat";
    if (capability == "embeddings") return "Embeddings";
    return capability;
}

std::string FitLabel(ModelFit fit) {
    switch (fit) {
        case ModelFit::Recommended: return "GPU";
        case ModelFit::RunsOnCpu:   return "CPU";
        case ModelFit::TooLarge:    return "Too large";
        case ModelFit::Unknown:     return "—";
    }
    return "—";
}

std::string FitTooltip(ModelFit fit) {
    switch (fit) {
        case ModelFit::Recommended: return "Fits this computer's GPU memory: runs on the GPU";
        case ModelFit::RunsOnCpu:   return "Fits system memory: runs on the CPU, or partly on the GPU";
        case ModelFit::TooLarge:    return "Does not fit this computer's memory";
        case ModelFit::Unknown:     return "No memory requirement on record";
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
    PageParts parts = MakePage("uai-set-page-local-models", "Local models",
            "Models that run on this computer. Nothing to install by hand: the "
            "engine and a model are downloaded when a service first needs them, "
            "or from here.");

    BuildHardwareLine(*parts.body);
    BuildEngineRow(*parts.body);
    BuildModelTable(*parts.body);
    BuildActionRow(*parts.body);
    BuildProgressRow(*parts.body);
    BuildAddRow(*parts.body);
    BuildSettingsRows(*parts.body);

    AddNote(parts, "uai-lm-note1",
            "GPU: the model fits the graphics card's memory and runs there. CPU: "
            "it fits system memory and runs on the processor, slower. Too large: "
            "it cannot be loaded on this computer. A model is downloaded from its "
            "publisher under the licence the table names.");
    AddNote(parts, "uai-lm-note2",
            "Ask: a service that needs a model it does not have asks first. "
            "Automatic: it downloads and continues. Never: models are only "
            "installed from this page.");
    AddNote(parts, "uai-lm-note3",
            "A running engine is shared by every UltraCanvas application that "
            "wants the same model and stopped when nothing has used it for the "
            "idle time.");

    Refresh();
    return parts.page;
}

// ===== BUILDING =====

void UltraAILocalModelsPage::BuildHardwareLine(UltraCanvasContainer& body) {
    hardwareLabel_ = MakeText("uai-lm-hardware", "", kTextWidth, kNoteFontSize, kNoteTextColor);
    body.AddChild(hardwareLabel_);
}

void UltraAILocalModelsPage::BuildEngineRow(UltraCanvasContainer& body) {
    engineLabel_ = MakeText("uai-lm-engine", "", kTextWidth);
    body.AddChild(engineLabel_);

    auto row = MakeRow("uai-lm-accel-row", 28);
    auto lbl = MakeLabel("uai-lm-accel-lbl", "Acceleration");
    lbl->size.width = CSSLayout::Dimension::Px(kLabelWidth);
    row->AddChild(lbl);
    accelDropdown_ = MakeDropdown("uai-lm-accel", kDropdownWidth);
    accelDropdown_->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (index < 0 || index >= static_cast<int>(accelIds_.size())) return;
        LocalRuntime& rt = LocalRuntime::Instance();
        LocalRuntimeSettings s = rt.GetSettings();
        s.preferredAccelerator = accelIds_[static_cast<size_t>(index)];
        rt.SetSettings(s);
        RefreshEngine();
    };
    row->AddChild(accelDropdown_);
    engineButton_ = MakeButton("uai-lm-engine-btn", "Install engine", 114, [this]() { OnInstallEngine(); });
    row->AddChild(engineButton_);
    body.AddChild(row);
}

void UltraAILocalModelsPage::BuildModelTable(UltraCanvasContainer& body) {
    tableModel_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    tableModel_->AddColumn(ListColumnDef("Model", 168, TextAlignment::Left, "The model, as the catalogue names it"));
    tableModel_->AddColumn(ListColumnDef("Service", 80, TextAlignment::Left, "What it serves"));
    tableModel_->AddColumn(ListColumnDef("Size", 62, TextAlignment::Right, "On disk, or to download"));
    tableModel_->AddColumn(ListColumnDef("Fits", 58, TextAlignment::Left, "Whether it fits this computer, and where it runs"));
    tableModel_->AddColumn(ListColumnDef("State", 98, TextAlignment::Left, "Installed, running, or not yet installed"));

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
    table_->onSelectionChanged = [this](const std::vector<int>&) { RefreshButtons(); };
    body.AddChild(table_);
}

void UltraAILocalModelsPage::BuildActionRow(UltraCanvasContainer& body) {
    auto row = MakeRow("uai-lm-actions", 30);
    installButton_ = MakeButton("uai-lm-install", "Install", 90, [this]() { OnInstall(); });
    removeButton_  = MakeButton("uai-lm-remove", "Remove", 90, [this]() { OnRemove(); });
    stopButton_    = MakeButton("uai-lm-stop", "Stop", 70, [this]() { OnStop(); });
    forgetButton_  = MakeButton("uai-lm-forget", "Forget", 80, [this]() { OnForget(); });
    row->AddChild(installButton_);
    row->AddChild(removeButton_);
    row->AddChild(stopButton_);
    row->AddChild(forgetButton_);
    statusLabel_ = MakeLabel("uai-lm-status", "", kNoteFontSize, kNoteTextColor);
    statusLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(statusLabel_);
    body.AddChild(row);
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
    cancelButton_ = MakeButton("uai-lm-cancel", "Cancel", 80, [this]() {
        if (!activeJobId_.empty()) LocalRuntime::Instance().Cancel(activeJobId_);
    });
    progressRow_->AddChild(cancelButton_);
    progressRow_->SetVisible(false);
    body.AddChild(progressRow_);
}

void UltraAILocalModelsPage::BuildAddRow(UltraCanvasContainer& body) {
    auto row = MakeRow("uai-lm-add-row", 30);
    row->AddChild(MakeButton("uai-lm-add-file", "Add model file...", 130, [this]() { OnAddFile(); }));
    row->AddChild(MakeButton("uai-lm-add-hf", "Add from Hugging Face...", 170, [this]() { OnAddHuggingFace(); }));
    row->AddChild(MakeButton("uai-lm-refresh", "Refresh catalogue", 130, [this]() { OnRefreshCatalogue(); }));
    body.AddChild(row);
}

void UltraAILocalModelsPage::BuildSettingsRows(UltraCanvasContainer& body) {
    {
        auto row = MakeRow("uai-lm-policy-row", 28);
        auto lbl = MakeLabel("uai-lm-policy-lbl", "Downloads");
        lbl->size.width = CSSLayout::Dimension::Px(kLabelWidth);
        row->AddChild(lbl);
        policyDropdown_ = MakeDropdown("uai-lm-policy", kDropdownWidth);
        policyDropdown_->AddItem("Ask before downloading", "ask");
        policyDropdown_->AddItem("Download automatically", "automatic");
        policyDropdown_->AddItem("Never - install from here only", "never");
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
        auto lbl = MakeLabel("uai-lm-idle-lbl", "Stop idle engines after");
        lbl->size.width = CSSLayout::Dimension::Px(kLabelWidth);
        row->AddChild(lbl);
        idleDropdown_ = MakeDropdown("uai-lm-idle", kDropdownWidth);
        idleDropdown_->AddItem("5 minutes", "5");
        idleDropdown_->AddItem("15 minutes", "15");
        idleDropdown_->AddItem("1 hour", "60");
        idleDropdown_->AddItem("4 hours", "240");
        idleDropdown_->AddItem("Never", "525600");
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
        row->AddChild(MakeButton("uai-lm-open-folder", "Open folder", 110, [this]() { OnOpenFolder(); }));
        body.AddChild(row);
    }
}

// ===== REFRESHING =====

void UltraAILocalModelsPage::Refresh() {
    if (!alive_ || !alive_->load()) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    const HardwareProfile& hw = rt.Hardware();
    std::string line = "This computer: " + Bytes(hw.memoryBytes) + " memory";
    if (hw.physicalCores) line += ", " + std::to_string(hw.physicalCores) + " cores";
    for (const auto& gpu : hw.gpus) {
        line += ", " + (gpu.model.empty() ? gpu.vendor : gpu.model);
        if (gpu.videoMemoryBytes) line += " (" + Bytes(gpu.videoMemoryBytes) + ")";
    }
    if (hw.gpus.empty()) line += ", no GPU found";
    if (!hw.accelerators.empty()) line += " - " + AcceleratorLabel(hw.accelerators.front()) + " by default";
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

    RefreshEngine();
    RefreshTable();
    RefreshDisk();
}

void UltraAILocalModelsPage::RefreshEngine() {
    LocalRuntime& rt = LocalRuntime::Instance();
    const HardwareProfile& hw = rt.Hardware();
    const LocalRuntimeSettings s = rt.GetSettings();

    // The accelerator choice: Auto, then every accelerator the gallery has a
    // bundle for on this platform.
    std::vector<EngineInfo> engines = rt.ListEngines();
    const EngineInfo* llama = nullptr;
    for (const auto& e : engines) {
        if (e.id == "llama-server") llama = &e;
    }
    if (accelDropdown_) {
        accelDropdown_->ClearItems();
        accelIds_.clear();
        std::string autoLabel = "Automatic";
        EngineBundleInfo chosen;
        if (rt.ChooseBundle("llama-server", chosen)) {
            autoLabel += " (" + AcceleratorLabel(chosen.accelerator) + ")";
        }
        accelDropdown_->AddItem(autoLabel, "");
        accelIds_.push_back("");
        if (llama) {
            for (const auto& b : llama->bundles) {
                if (b.platform != hw.platform) continue;
                if (std::find(accelIds_.begin(), accelIds_.end(), b.accelerator) != accelIds_.end()) continue;
                std::string label = AcceleratorLabel(b.accelerator);
                if (b.optional) label += ", " + Bytes(b.sizeBytes);
                accelDropdown_->AddItem(label, b.accelerator);
                accelIds_.push_back(b.accelerator);
            }
        }
        int selected = 0;
        for (size_t i = 0; i < accelIds_.size(); ++i) {
            if (accelIds_[i] == s.preferredAccelerator) selected = static_cast<int>(i);
        }
        accelDropdown_->SetSelectedIndex(selected, false);
    }

    // The engine line and its button.
    std::string text;
    bool installed = false;
    EngineBundleInfo chosen;
    const bool haveChoice = rt.ChooseBundle("llama-server", chosen);
    if (!llama) {
        text = "Engine: the gallery has no llama-server for this build.";
    } else if (!haveChoice) {
        text = "Engine: llama-server " + llama->version + " - no build for " + hw.platform +
               " in the gallery.";
    } else {
        installed = chosen.installed;
        text = "Engine: llama-server " + llama->version + " - " + AcceleratorLabel(chosen.accelerator) +
               (installed ? ", installed"
                          : ", not installed (" + Bytes(chosen.sizeBytes) +
                                ", downloaded with the first model)");
        // Other installed bundles are worth a word: they take disk.
        for (const auto& b : llama->bundles) {
            if (b.installed && b.accelerator != chosen.accelerator) {
                text += "; " + AcceleratorLabel(b.accelerator) + " also installed";
            }
        }
    }
    if (engineLabel_) engineLabel_->SetText(text);
    if (engineButton_) {
        engineButton_->SetText(installed ? "Remove engine" : "Install engine");
        engineButton_->SetDisabled(!haveChoice || !activeJobId_.empty() || busy_);
        engineButton_->RequestRedraw();
    }
}

void UltraAILocalModelsPage::RefreshTable() {
    if (!table_ || !tableModel_) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    std::string selectedId;
    if (const LocalModelInfo* current = SelectedModel()) selectedId = current->id;

    rows_ = rt.ListModels();
    // Installed first, then the catalogue's, then the user's; each in its order.
    std::stable_sort(rows_.begin(), rows_.end(), [](const LocalModelInfo& a, const LocalModelInfo& b) {
        if (a.installed != b.installed) return a.installed;
        return false;
    });

    std::vector<MultiColumnListItem> items;
    for (const auto& m : rows_) {
        std::string state = m.running ? "Running" : m.installed ? "Installed" : "Not installed";
        if (m.fit == ModelFit::TooLarge && !m.installed) state = "Too large";
        std::string name = m.displayName;
        if (m.source == LocalModelSource::File) name += " (your file)";
        if (m.source == LocalModelSource::HuggingFace) name += " (Hugging Face)";
        MultiColumnListItem item({name, CapabilityLabel(m.capability), Bytes(m.sizeBytes),
                                  FitLabel(m.fit), state});
        item.tooltip = m.description.empty() ? m.displayName : m.description;
        if (!m.license.empty()) item.SetCellTooltip(0, "Licence: " + m.license +
                                                       (m.description.empty() ? "" : "\n" + m.description));
        item.SetCellTooltip(3, FitTooltip(m.fit));
        if (m.installed) item.SetCellTooltip(4, m.installedPath);
        items.push_back(std::move(item));
    }
    tableModel_->SetItems(std::move(items));

    // Keep the row the user had.
    if (!selectedId.empty()) {
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].id == selectedId && table_->GetSelection()) {
                table_->GetSelection()->Select(static_cast<int>(i));
                break;
            }
        }
    }
    RefreshButtons();
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
    set(forgetButton_,  m && m->source != LocalModelSource::Catalogue && !jobRunning);
}

void UltraAILocalModelsPage::RefreshDisk() {
    if (!diskLabel_) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    const LocalRuntimeSettings s = rt.GetSettings();
    const uint64_t used = rt.DiskUsedBytes();
    std::string text = used ? "Disk: " + Bytes(used) + " used" : std::string("Disk: nothing used yet");
    if (s.diskCapBytes) text += " of a " + Bytes(s.diskCapBytes) + " limit";
    const std::string published = rt.GalleryPublished();
    if (!published.empty()) text += "; catalogue of " + published;
    diskLabel_->SetText(text);
}

void UltraAILocalModelsPage::ShowProgress(const InstallProgress& p) {
    if (!progressRow_) return;
    std::string text = p.what;
    switch (p.state) {
        case InstallProgress::State::Queued:      text += ": queued"; break;
        case InstallProgress::State::Downloading: {
            text += ": downloading";
            if (p.bytesTotal) {
                const int percent = static_cast<int>(p.bytesDone * 100 / p.bytesTotal);
                text += " " + std::to_string(percent) + "% of " + Bytes(p.bytesTotal);
                if (progressBar_) progressBar_->SetValue(percent);
            } else {
                text += " " + Bytes(p.bytesDone);
            }
            break;
        }
        case InstallProgress::State::Verifying:   text += ": verifying"; if (progressBar_) progressBar_->SetValue(100); break;
        case InstallProgress::State::Unpacking:   text += ": unpacking"; break;
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
        statusLabel_->RequestRedraw();
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
                    SetStatus(p.what + " installed.");
                    Refresh();
                    break;
                case InstallProgress::State::Failed:
                    activeJobId_.clear();
                    HideProgress();
                    SetStatus("Failed: " + p.error);
                    Refresh();
                    break;
                case InstallProgress::State::Cancelled:
                    activeJobId_.clear();
                    HideProgress();
                    SetStatus(p.what + ": cancelled. A partial download is kept and resumed next time.");
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
    if (!rt.ChooseBundle("llama-server", chosen) || !activeJobId_.empty()) return;
    if (chosen.installed) {
        const std::string accelerator = chosen.accelerator;
        RunThenRefresh([accelerator]() {
            const Error error = LocalRuntime::Instance().RemoveEngine("llama-server", accelerator);
            return error.IsOk() ? std::string("Engine removed.") : error.message;
        });
        return;
    }
    SetStatus("");
    activeJobId_ = rt.InstallEngine("llama-server", chosen.accelerator, ProgressSink());
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
        return error.IsOk() ? name + " removed." : error.message;
    });
}

void UltraAILocalModelsPage::OnStop() {
    const LocalModelInfo* m = SelectedModel();
    if (!m) return;
    const std::string id = m->id, name = m->displayName;
    RunThenRefresh([id, name]() {
        LocalRuntime::Instance().StopModel(id);
        return name + " stopped.";
    });
}

void UltraAILocalModelsPage::OnForget() {
    const LocalModelInfo* m = SelectedModel();
    if (!m) return;
    const std::string id = m->id, name = m->displayName;
    RunThenRefresh([id, name]() {
        const Error error = LocalRuntime::Instance().ForgetModel(id);
        return error.IsOk() ? name + " forgotten." : error.message;
    });
}

void UltraAILocalModelsPage::OnAddFile() {
    FileDialogOptions opts;
    opts.SetTitle("Add a model file")
        .SetParentWindow(parent_)
        .AddFilter("GGUF models", std::vector<std::string>{"gguf"})
        .AddFilter("All files", "*");
    UltraCanvasFileLoader::OpenFileDialog(
        opts, [this, alive = alive_](DialogResult result, const std::string& path) {
            if (!alive->load()) return;
            if (result != DialogResult::OK || path.empty()) return;
            // An embedding model is told apart by its name; everything else
            // is a chat model. The user corrects it with Forget and a second
            // add under the other service when the guess is wrong.
            std::string lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const std::string capability = lower.find("embed") != std::string::npos ? "embeddings" : "textllm";
            std::string id;
            const Error error = LocalRuntime::Instance().AddModelFile(path, capability, "", id);
            SetStatus(error.IsOk() ? "Added " + path + " as a " + CapabilityLabel(capability) + " model."
                                   : error.message);
            Refresh();
        });
}

void UltraAILocalModelsPage::OnAddHuggingFace() {
    UltraCanvasDialogManager::ShowInputDialog(
        "The repository and the GGUF file in it, as \"organisation/repository:file.gguf\". "
        "Add \"embeddings\" after a space for an embedding model.",
        "Add a model from Hugging Face", "", InputType::Text,
        [this, alive = alive_](DialogResult result, const std::string& text) {
            if (!alive->load()) return;
            if (result != DialogResult::OK || text.empty()) return;
            std::string spec = text, capability = "textllm";
            const size_t space = spec.find(' ');
            if (space != std::string::npos) {
                if (spec.substr(space + 1).find("embed") != std::string::npos) capability = "embeddings";
                spec = spec.substr(0, space);
            }
            const size_t colon = spec.find(':');
            if (colon == std::string::npos) {
                SetStatus("Write it as organisation/repository:file.gguf");
                return;
            }
            const std::string repo = spec.substr(0, colon), file = spec.substr(colon + 1);
            busy_ = true;
            RefreshButtons();
            SetStatus("Asking Hugging Face about " + file + "...");
            std::thread worker([this, alive, repo, file, capability]() {
                std::string id;
                const Error error = LocalRuntime::Instance().AddHuggingFaceModel(repo, file, capability, "", id);
                const std::string status = error.IsOk()
                    ? "Added " + file + " from " + repo + "; select it and Install."
                    : error.message;
                OnUiThread([this, alive, status]() {
                    if (!alive->load()) return;
                    busy_ = false;
                    SetStatus(status);
                    Refresh();
                });
            });
            worker.detach();
        },
        parent_);
}

void UltraAILocalModelsPage::OnRefreshCatalogue() {
    RunThenRefresh([]() {
        const Error error = LocalRuntime::Instance().RefreshGallery();
        return error.IsOk() ? std::string("Catalogue refreshed.")
                            : "Could not refresh the catalogue: " + error.message;
    });
}

void UltraAILocalModelsPage::OnOpenFolder() {
    OpenURL("file://" + LocalRuntime::Instance().RootFolder());
}

void UltraAILocalModelsPage::RestoreDefaults() {
    LocalRuntime& rt = LocalRuntime::Instance();
    LocalRuntimeSettings s = rt.GetSettings();
    s.policy = DownloadPolicy::Ask;
    s.preferredAccelerator.clear();
    s.idleMinutes = 15;
    rt.SetSettings(s);
    Refresh();
}

} // namespace UltraAIApp
