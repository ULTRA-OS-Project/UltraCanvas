// Apps/UltraAIApp/UltraAICatalogueSourcesDialog.cpp
// "Sources" (UltraAICatalogueSourcesDialog.h).
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraAI Module

#include "UltraAICatalogueSourcesDialog.h"

#include "UltraAISettingsWidgets.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListSelection.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;
using namespace SettingsWidgets;

namespace {

constexpr int kInnerWidth = 568;        // the dialog's width less its padding

std::string Trimmed(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// What a list's address is called in the table, and the id its models
// carry: "owner/repository" and "owner-repository" for LocalAI's
// "github:owner/repository/path", the host for an https:// address.
void NameAddress(const std::string& address, std::string& name, std::string& id) {
    std::string rest = address;
    for (const char* scheme : {"https://", "github://", "github:"}) {
        const std::string s = scheme;
        if (rest.compare(0, s.size(), s) == 0) { rest = rest.substr(s.size()); break; }
    }
    const bool github = address.compare(0, 7, "github:") == 0;
    const size_t first = rest.find('/');
    const size_t second = first == std::string::npos ? std::string::npos : rest.find('/', first + 1);
    name = github ? rest.substr(0, second) : rest.substr(0, first);
    for (unsigned char c : name) {
        if (std::isalnum(c)) id += static_cast<char>(std::tolower(c));
        else if (!id.empty() && id.back() != '-') id += '-';
    }
    while (!id.empty() && id.back() == '-') id.pop_back();
    if (id.size() > 32) id.resize(32);
}

} // namespace

UltraAICatalogueSourcesDialog::UltraAICatalogueSourcesDialog(std::function<void()> onChanged)
    : onChanged_(std::move(onChanged)) {}

UltraAICatalogueSourcesDialog::~UltraAICatalogueSourcesDialog() {
    alive_->store(false);
}

void UltraAICatalogueSourcesDialog::CreateSourcesDialog() {
    using CSSLayout::AlignItems;
    using CSSLayout::Dimension;

    DialogConfig cfg;
    cfg.title      = Tr("Catalogue sources");
    cfg.width      = kW;
    cfg.height     = kH;
    cfg.dialogType = DialogType::Custom;
    cfg.buttons    = DialogButtons::NoButtons;
    cfg.position   = DialogPosition::CenterParent;
    cfg.resizable  = true;
    CreateDialog(cfg);

    layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(AlignItems::Stretch);
    SetPadding(16);

    AddChild(MakeText("uai-src-intro",
                      Tr("The lists the Local models page offers models from, beside the recommended "
                         "ones. A source that is turned off is kept, and its models are not offered. "
                         "Refresh catalogue on the page downloads the lists."),
                      kInnerWidth, kNoteFontSize, kNoteTextColor));

    model_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    model_->AddColumn(ListColumnDef(Tr("Source"), 250, TextAlignment::Left, Tr("The list's name")));
    model_->AddColumn(ListColumnDef(Tr("Models"), 80, TextAlignment::Right, Tr("How many of its models the local engines can run")));
    model_->AddColumn(ListColumnDef(Tr("Downloaded"), 110, TextAlignment::Left, Tr("When its list was last downloaded")));
    model_->AddColumn(ListColumnDef(Tr("State"), 100, TextAlignment::Left, Tr("Whether its models are offered")));
    list_ = std::make_shared<UltraCanvasListView>("uai-src-list", 0, 0, kInnerWidth, 150);
    list_->SetModel(model_);
    ListViewStyle style;
    style.showHeader         = true;
    style.showGridLines      = false;
    style.alternateRowColors = true;
    style.headerFontSize     = kTextFontSize;
    style.headerHeight       = 26;
    style.rowHeight          = 22;
    list_->SetStyle(style);
    auto delegate = std::make_shared<UltraCanvasDefaultListDelegate>();
    delegate->SetFontSize(kTextFontSize);
    list_->SetDelegate(delegate);
    list_->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    list_->size.height = Dimension::Px(150);
    list_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    list_->onSelectionChanged = [this](const std::vector<int>&) { RefreshButtons(); };
    AddChild(list_);

    addressLabel_ = MakeText("uai-src-address", "", kInnerWidth, kNoteFontSize, kNoteTextColor);
    AddChild(addressLabel_);
    statusLabel_ = MakeText("uai-src-status", "", kInnerWidth, kNoteFontSize, kNoteTextColor);
    AddChild(statusLabel_);

    auto footer = std::make_shared<UltraCanvasContainer>("uai-src-footer");
    footer->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Center);
    footer->size.height = Dimension::Px(30);
    footer->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    toggleButton_ = MakeButton("uai-src-toggle", Tr("Turn off"), 84, [this]() { OnToggle(); });
    removeButton_ = MakeButton("uai-src-remove", Tr("Remove"), 84, [this]() { OnRemove(); });
    footer->AddChild(toggleButton_);
    footer->AddChild(removeButton_);
    footer->AddChild(MakeButton("uai-src-add", Tr("Add source"), 100, [this]() { OnAdd(); }));
    auto spacer = std::make_shared<UltraCanvasContainer>("uai-src-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    footer->AddChild(spacer);
    footer->AddChild(MakeButton("uai-src-close", Tr("Close"), 84, [this]() { CloseDialog(DialogResult::Close); }));
    AddChild(footer);

    Reload();
}

int UltraAICatalogueSourcesDialog::SelectedRow() const {
    if (!list_ || !list_->GetSelection()) return -1;
    const int row = list_->GetSelection()->GetCurrentRow();
    return row >= 0 && row < static_cast<int>(sources_.size()) ? row : -1;
}

void UltraAICatalogueSourcesDialog::SetStatus(const std::string& text) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->RequestRedraw();
}

void UltraAICatalogueSourcesDialog::Reload() {
    std::string selectedId;
    const int before = SelectedRow();
    if (before >= 0) selectedId = sources_[static_cast<size_t>(before)].source.id;

    sources_ = LocalRuntime::Instance().ListCatalogueSources();
    std::vector<MultiColumnListItem> items;
    for (const CatalogueSourceStatus& s : sources_) {
        MultiColumnListItem item({s.source.name,
                                  s.fetched.empty() ? std::string("—") : std::to_string(s.models),
                                  s.fetched.empty() ? Tr("Not yet") : s.fetched,
                                  s.source.enabled ? Tr("On") : Tr("Off")});
        item.tooltip = s.source.url;
        if (s.skipped > 0) {
            item.SetCellTooltip(1, TrF("{0} more of its models need an engine UltraAI does not have", s.skipped));
        }
        items.push_back(std::move(item));
    }
    if (model_) model_->SetItems(std::move(items));
    if (list_ && list_->GetSelection()) {
        int select = sources_.empty() ? -1 : 0;
        for (size_t i = 0; i < sources_.size(); ++i) {
            if (sources_[i].source.id == selectedId) select = static_cast<int>(i);
        }
        if (select >= 0) list_->GetSelection()->Select(select);
        else list_->GetSelection()->Clear();
    }
    RefreshButtons();
}

void UltraAICatalogueSourcesDialog::RefreshButtons() {
    const int row = SelectedRow();
    if (toggleButton_) {
        toggleButton_->SetText(row >= 0 && !sources_[static_cast<size_t>(row)].source.enabled ? Tr("Turn on") : Tr("Turn off"));
        toggleButton_->SetDisabled(row < 0);
        toggleButton_->RequestRedraw();
    }
    if (removeButton_) {
        removeButton_->SetDisabled(row < 0);
        removeButton_->RequestRedraw();
    }
    if (addressLabel_) {
        addressLabel_->SetText(row >= 0 ? TrF("Its list: {0}", sources_[static_cast<size_t>(row)].source.url) : std::string());
        addressLabel_->RequestRedraw();
    }
}

void UltraAICatalogueSourcesDialog::OnToggle() {
    const int row = SelectedRow();
    if (row < 0) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    LocalRuntimeSettings s = rt.GetSettings();
    const std::string id = sources_[static_cast<size_t>(row)].source.id;
    bool nowOn = false;
    std::string name;
    for (CatalogueSource& source : s.catalogueSources) {
        if (source.id != id) continue;
        source.enabled = !source.enabled;
        nowOn = source.enabled;
        name  = source.name;
    }
    rt.SetSettings(s);
    SetStatus(nowOn ? TrF("{0} is on: its models are offered again.", name)
                    : TrF("{0} is off: its models are no longer offered. Installed ones stay.", name));
    Reload();
    if (onChanged_) onChanged_();
}

void UltraAICatalogueSourcesDialog::OnRemove() {
    const int row = SelectedRow();
    if (row < 0) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    LocalRuntimeSettings s = rt.GetSettings();
    const std::string id = sources_[static_cast<size_t>(row)].source.id;
    const std::string name = sources_[static_cast<size_t>(row)].source.name;
    s.catalogueSources.erase(std::remove_if(s.catalogueSources.begin(), s.catalogueSources.end(),
                                            [&](const CatalogueSource& source) { return source.id == id; }),
                             s.catalogueSources.end());
    rt.SetSettings(s);
    SetStatus(TrF("{0} removed. Installed models of it stay; Restore defaults on the page brings the first sources back.", name));
    Reload();
    if (onChanged_) onChanged_();
}

void UltraAICatalogueSourcesDialog::OnAdd() {
    UltraCanvasDialogManager::ShowInputDialog(
        Tr("The address of a list of models: a LocalAI model gallery (its index.yaml) or an "
           "UltraAI catalogue.json. Write it as https://… or, as LocalAI does, as "
           "github:owner/repository/path@branch. Its models are offered after Refresh catalogue; "
           "only add a list whose publisher you trust."),
        Tr("Add a catalogue source"), "", InputType::Text,
        [this, alive = alive_](DialogResult result, const std::string& text) {
            if (!alive->load() || result != DialogResult::OK) return;
            AddSource(Trimmed(text));
        },
        this);
}

void UltraAICatalogueSourcesDialog::AddSource(const std::string& address) {
    if (address.empty()) return;
    LocalRuntime& rt = LocalRuntime::Instance();
    LocalRuntimeSettings s = rt.GetSettings();
    for (const CatalogueSource& existing : s.catalogueSources) {
        if (existing.url == address) {
            SetStatus(TrF("{0} already has that address.", existing.name));
            return;
        }
    }
    CatalogueSource source;
    source.url = address;
    NameAddress(address, source.name, source.id);
    // An id of its own: the ids of its models start with it.
    const std::string base = source.id;
    for (int n = 2; std::any_of(s.catalogueSources.begin(), s.catalogueSources.end(),
                                [&](const CatalogueSource& o) { return o.id == source.id; }); ++n) {
        source.id = base + "-" + std::to_string(n);
    }
    s.catalogueSources.push_back(source);
    rt.SetSettings(s);
    // The runtime keeps a source only when it could download its list.
    const std::vector<CatalogueSource> kept = rt.GetSettings().catalogueSources;
    if (std::none_of(kept.begin(), kept.end(), [&](const CatalogueSource& o) { return o.url == address; })) {
        SetStatus(Tr("That is not an address a list can be downloaded from: write https://… or "
                     "github:owner/repository/path@branch."));
        return;
    }
    SetStatus(TrF("{0} added. Refresh catalogue on the page downloads its list.", source.name));
    Reload();
    if (onChanged_) onChanged_();
}

} // namespace UltraAIApp
