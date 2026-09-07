// Apps/UltraAIApp/UltraAISettingsDialog.cpp
// Version: 0.1.0

#include "UltraAISettingsDialog.h"

#include "UltraCanvasContainer.h"
#include "UltraAI.h"
#ifdef ULTRAAI_HAS_ULTRAVAULT
#include <UltraVault/UltraVault.h>
#endif

#include <algorithm>
#include <set>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;

namespace {
constexpr const char* kNewEndpointLabel = "➕  New endpoint";
} // namespace

UltraAISettingsDialog::UltraAISettingsDialog() = default;

std::vector<std::string> UltraAISettingsDialog::AllKnownProviders() const {
    std::set<std::string> uniq;
    auto merge = [&](const std::vector<std::string>& v) {
        for (const auto& s : v) uniq.insert(s);
    };
    merge(ListTextLLMProviders());
    merge(ListEmbeddingsProviders());
    merge(ListSpeechToTextProviders());
    merge(ListTextToSpeechProviders());
    merge(ListTranslatorProviders());
    merge(ListImageGenProviders());
    merge(ListVisionAnalyzerProviders());
    merge(ListVideoGenProviders());
    merge(ListMusicGenProviders());
    merge(ListCodeAssistProviders());
    return {uniq.begin(), uniq.end()};
}

// Small helpers for building flex children. Keeping them local avoids leaking
// layout boilerplate into the header.
namespace {

using UltraCanvas::CSSLayout::Dimension;
using UltraCanvas::CSSLayout::AlignItems;

std::shared_ptr<UltraCanvasLabel> FlexLabel(const std::string& id,
                                            const std::string& text, float h) {
    auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, h, text);
    l->size.height = Dimension::Px(h);
    l->layoutItem.SetFlexShrink(0);
    return l;
}

// A "label above input" field group.
std::shared_ptr<UltraCanvasContainer> FieldColumn(
    const std::string& id, const std::string& label, const std::string& placeholder,
    std::shared_ptr<UltraCanvasTextInput>& out) {
    auto col = std::make_shared<UltraCanvasContainer>(id + "-grp");
    col->layout.SetFlexColumn().SetFlexGap(4);
    col->size.height = Dimension::Px(52);
    col->layoutItem.SetFlexShrink(0);
    col->AddChild(FlexLabel(id + "-lbl", label, 18));
    out = std::make_shared<UltraCanvasTextInput>(id, 0, 0, 0, 28);
    out->SetPlaceholder(placeholder);
    out->size.height = Dimension::Px(28);
    col->AddChild(out);
    return col;
}

} // namespace

void UltraAISettingsDialog::CreateSettingsDialog(const std::string& selectEndpointId) {
    DialogConfig cfg;
    cfg.title      = "UltraAI — Settings";
    cfg.width      = kW;
    cfg.height     = kH;
    cfg.dialogType = DialogType::Custom;        // drop the icon/message chrome
    cfg.buttons    = DialogButtons::NoButtons;  // we lay out our own footer
    cfg.position   = DialogPosition::CenterParent;
    cfg.resizable  = false;
    // Grow the dialog to fit its content (capped at the screen) so the editor
    // form never needs a scrollbar. The form is laid out for a fixed 780px
    // width, so pin the width and let only the height fit the content.
    cfg.autoResizeToContent = true;
    cfg.minWidth   = kW;
    cfg.maxWidth   = kW;
    CreateDialog(cfg);

    // The dialog itself is the root flex column.
    layout.SetFlexColumn().SetFlexGap(10);
    SetPadding(16);

    // ===== Header =====
    AddChild(FlexLabel("set-title",
        "Endpoints — one model can serve several modes", 24));
    AddChild(FlexLabel("set-hint",
        "Configure an endpoint once, then tick the modes it can serve.", 18));

    // ===== Endpoint picker row: dropdown (grows) + Delete =====
    auto pickRow = std::make_shared<UltraCanvasContainer>("set-pickrow");
    pickRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Center);
    pickRow->size.height = Dimension::Px(30);
    pickRow->layoutItem.SetFlexShrink(0);

    endpointPicker_ = std::make_shared<UltraCanvasDropdown>("set-picker", 0, 0, 0, 28);
    endpointPicker_->size.height = Dimension::Px(28);
    endpointPicker_->layoutItem.SetFlexGrow(1);
    endpointPicker_->onSelectionChanged =
        [this](int, const DropdownItem&) { OnPickerChanged(); };
    pickRow->AddChild(endpointPicker_);

    auto deleteBtn = std::make_shared<UltraCanvasButton>("set-delete", 0, 0, 110, 28);
    deleteBtn->SetText("Delete");
    deleteBtn->size.width = Dimension::Px(110);
    deleteBtn->layoutItem.SetFlexShrink(0);
    deleteBtn->onClick = [this]() { OnDelete(); };
    pickRow->AddChild(deleteBtn);
    AddChild(pickRow);

    // ===== Scrollable editor form (grows to fill; footer stays pinned) =====
    auto form = std::make_shared<UltraCanvasContainer>("set-form");
    form->layout.SetFlexColumn().SetFlexGap(8);
    form->layoutItem.SetFlexGrow(1);

    form->AddChild(FieldColumn("set-name", "Name", "e.g. My OpenAI account",
                               nameInput_));

    // Provider row.
    auto provCol = std::make_shared<UltraCanvasContainer>("set-prov-grp");
    provCol->layout.SetFlexColumn().SetFlexGap(4);
    provCol->size.height = Dimension::Px(52);
    provCol->layoutItem.SetFlexShrink(0);
    provCol->AddChild(FlexLabel("set-prov-lbl", "Provider", 18));
    providerDropdown_ = std::make_shared<UltraCanvasDropdown>("set-provider", 0, 0, 0, 28);
    providerDropdown_->size.height = Dimension::Px(28);
    for (const auto& p : AllKnownProviders()) providerDropdown_->AddItem(p);
    if (providerDropdown_->GetItemCount() > 0)
        providerDropdown_->SetSelectedIndex(0, false);
    provCol->AddChild(providerDropdown_);
    form->AddChild(provCol);

    form->AddChild(FlexLabel("set-localhint",
        "Local server? Pick \"openai\", set Base URL http://127.0.0.1:8080, "
        "leave the key empty.", 16));

    form->AddChild(FieldColumn("set-baseurl", "Base URL (optional, for self-hosted)",
        "https://... — leave empty for the provider default", baseUrlInput_));
    form->AddChild(FieldColumn("set-model", "Default model (id, GGUF path or checkpoint)",
        "provider default", modelInput_));
    form->AddChild(FieldColumn("set-key",
        "API key — cloud providers only; leave empty for local/self-hosted",
        "leave empty to keep the stored key", keyInput_));

    // Mode checkboxes in a fixed two-column grid so the second column lines up
    // regardless of how wide each checkbox's label is (a flex row sized each
    // cell to its content, leaving the right column ragged).
    using namespace CSSLayout;
    form->AddChild(FlexLabel("set-modes-lbl", "Modes this endpoint can serve", 18));
    auto modesCol = std::make_shared<UltraCanvasContainer>("set-modes");
    const auto& caps = AllCapabilities();
    constexpr int kCols   = 2;
    constexpr int kRowH   = 24;
    constexpr int kRowGap = 4;
    const int rows = static_cast<int>((caps.size() + kCols - 1) / kCols);

    std::vector<GridTrackSize> colTracks(kCols, {GridTrackSizeKind::Fr, Dimension::Fr(1)});
    std::vector<GridTrackSize> rowTracks(
        static_cast<size_t>(std::max(0, rows)),
        {GridTrackSizeKind::Fixed, Dimension::Px(kRowH)});
    modesCol->layout.SetGrid()
                    .SetGridColumns(colTracks)
                    .SetGridRows(rowTracks)
                    .SetGridGap(kRowGap, 8);   // rowGap, columnGap
    modesCol->size.height =
        Dimension::Px(static_cast<float>(rows * kRowH + std::max(0, rows - 1) * kRowGap));
    modesCol->layoutItem.SetFlexShrink(0);

    for (size_t i = 0; i < caps.size(); ++i) {
        auto cb = std::make_shared<UltraCanvasCheckbox>(
            std::string("set-mode-") + caps[i].id, 0, 0, 0, 24, caps[i].label);
        cb->size.height = Dimension::Px(24);
        modeChecks_[caps[i].cap] = cb;
        modesCol->AddChild(cb);
        cb->layoutItem.SetGridRowColSimplified(
            static_cast<int>(i / kCols), static_cast<int>(i % kCols));
    }
    form->AddChild(modesCol);

    AddChild(form);

    // ===== Pinned footer: Save + status + Close =====
    auto footer = std::make_shared<UltraCanvasContainer>("set-footer");
    footer->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Center);
    footer->size.height = Dimension::Px(34);
    footer->layoutItem.SetFlexShrink(0);

    auto saveBtn = std::make_shared<UltraCanvasButton>("set-save", 0, 0, 150, 30);
    saveBtn->SetText("Save endpoint");
    saveBtn->size.width = Dimension::Px(150);
    saveBtn->layoutItem.SetFlexShrink(0);
    saveBtn->onClick = [this]() { OnSave(); };
    footer->AddChild(saveBtn);

    statusLabel_ = std::make_shared<UltraCanvasLabel>("set-status", 0, 0, 0, 20, "");
    statusLabel_->layoutItem.SetFlexGrow(1);
    footer->AddChild(statusLabel_);

    auto closeBtn = std::make_shared<UltraCanvasButton>("set-close", 0, 0, 100, 30);
    closeBtn->SetText("Close");
    closeBtn->size.width = Dimension::Px(100);
    closeBtn->layoutItem.SetFlexShrink(0);
    closeBtn->onClick = [this]() { CloseDialog(DialogResult::Close); };
    footer->AddChild(closeBtn);
    AddChild(footer);

    // Populate the picker and load the requested endpoint (or the first one /
    // an empty editor when selectEndpointId is empty or unknown).
    RebuildEndpointPicker(selectEndpointId);
}

void UltraAISettingsDialog::RebuildEndpointPicker(const std::string& selectId) {
    if (!endpointPicker_) return;
    endpointPicker_->ClearItems();
    pickerIds_.clear();

    endpointPicker_->AddItem(kNewEndpointLabel);
    pickerIds_.push_back("");   // index 0 => new

    int selectIndex = 0;
    const auto& all = EndpointStore::Instance().All();
    for (const auto& e : all) {
        const std::string label =
            (e.name.empty() ? e.id : e.name) + "   (" + e.providerId + ")";
        endpointPicker_->AddItem(label);
        pickerIds_.push_back(e.id);
        if (e.id == selectId) selectIndex = static_cast<int>(pickerIds_.size()) - 1;
    }

    endpointPicker_->SetSelectedIndex(selectIndex, /*runNotifications=*/false);
    const std::string& id = pickerIds_[selectIndex];
    LoadIntoEditor(id.empty() ? nullptr : EndpointStore::Instance().FindById(id));
}

void UltraAISettingsDialog::OnPickerChanged() {
    const int idx = endpointPicker_ ? endpointPicker_->GetSelectedIndex() : 0;
    if (idx < 0 || idx >= static_cast<int>(pickerIds_.size())) return;
    const std::string& id = pickerIds_[idx];
    LoadIntoEditor(id.empty() ? nullptr : EndpointStore::Instance().FindById(id));
}

void UltraAISettingsDialog::LoadIntoEditor(const Endpoint* e) {
    editingId_ = e ? e->id : "";

    if (nameInput_)    nameInput_->SetText(e ? e->name : "");
    if (baseUrlInput_) baseUrlInput_->SetText(e ? e->baseUrl : "");
    if (modelInput_)   modelInput_->SetText(e ? e->defaultModel : "");
    if (keyInput_)     keyInput_->SetText("");   // never surface stored keys

    if (providerDropdown_ && e && !e->providerId.empty()) {
        for (int i = 0; i < providerDropdown_->GetItemCount(); ++i) {
            if (const auto* item = providerDropdown_->GetItem(i);
                item && item->text == e->providerId) {
                providerDropdown_->SetSelectedIndex(i, false);
                break;
            }
        }
    } else if (providerDropdown_ && providerDropdown_->GetItemCount() > 0) {
        providerDropdown_->SetSelectedIndex(0, false);
    }

    for (auto& [cap, cb] : modeChecks_) {
        cb->SetChecked(e ? e->Supports(cap) : false);
    }

    SetStatus(e ? "" : "Editing a new endpoint — fill in the form and Save.");
}

void UltraAISettingsDialog::OnDelete() {
    if (editingId_.empty()) { SetStatus("Nothing to delete."); return; }
    EndpointStore::Instance().Remove(editingId_);
    EndpointStore::Instance().Save();
    editingId_.clear();
    RebuildEndpointPicker("");
    SetStatus("Endpoint deleted.");
}

void UltraAISettingsDialog::OnSave() {
    const std::string name = nameInput_ ? nameInput_->GetText() : "";
    std::string provider;
    if (providerDropdown_) {
        if (const auto* item = providerDropdown_->GetSelectedItem()) provider = item->text;
    }

    if (provider.empty()) { SetStatus("Pick a provider first."); return; }

    Endpoint e;
    auto& store = EndpointStore::Instance();
    if (!editingId_.empty()) {
        if (const auto* existing = store.FindById(editingId_)) e = *existing;
    }
    if (e.id.empty()) e.id = store.MakeId(name.empty() ? provider : name);

    e.name         = name.empty() ? e.id : name;
    e.providerId   = provider;
    e.baseUrl      = baseUrlInput_ ? baseUrlInput_->GetText() : "";
    e.defaultModel = modelInput_ ? modelInput_->GetText() : "";
    e.modes.clear();
    for (auto& [cap, cb] : modeChecks_) {
        if (cb->IsChecked()) e.modes.insert(cap);
    }

    // Store a freshly-typed API key in the vault, then blank the field.
    if (keyInput_ && !keyInput_->GetText().empty()) {
#ifdef ULTRAAI_HAS_ULTRAVAULT
        if (!UltraVault::IsAvailable()) UltraVault::Initialize();
        auto stored = UltraVault::Put(
            e.VaultRef(), UltraVault::SecretValue::FromString(keyInput_->GetText()));
        if (!stored.IsOk()) {
            SetStatus("Could not store the key: " + stored.message);
            return;
        }
#endif
        keyInput_->SetText("");
    }

    store.Upsert(e);
    if (!store.Save()) {
        SetStatus("Saved in memory, but writing " + EndpointStore::ConfigPath() +
                  " failed.");
        return;
    }
    editingId_ = e.id;
    RebuildEndpointPicker(e.id);
    SetStatus("Saved \"" + e.name + "\".");
}

void UltraAISettingsDialog::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

} // namespace UltraAIApp
