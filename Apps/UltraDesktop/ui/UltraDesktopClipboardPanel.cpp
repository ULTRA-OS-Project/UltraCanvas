// Apps/UltraDesktop/ui/UltraDesktopClipboardPanel.cpp
// The clipboard quick panel. See UltraDesktopClipboardPanel.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopClipboardPanel.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasClipboardHistoryView.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <algorithm>

using namespace UltraCanvas;

namespace UltraDesktop {
namespace {

constexpr int kPanelWidth = 386;
constexpr int kPanelHeight = 484;
constexpr size_t kRecentShown = 10;   // unpinned entries when nothing is searched
constexpr size_t kFoundShown = 40;
constexpr size_t kTargetShown = 3;    // under "For <program>"

// The right bar's palette (UltraDesktopWindow.cpp): the panel belongs to it.
const Color kPanel(31, 31, 31);
const Color kInset(20, 20, 20);
const Color kBar(74, 74, 74);
const Color kRaised(46, 46, 46);
const Color kText(242, 242, 242);
const Color kMuted(140, 140, 140);

} // namespace

UltraDesktopClipboardPanel::UltraDesktopClipboardPanel(UltraCanvasClipboardHistory* history, Actions actions)
    : history_(history), actions_(std::move(actions)) {}

UltraDesktopClipboardPanel::~UltraDesktopClipboardPanel() {
    if (undoTimer_) UltraCanvasApplication::GetInstance()->StopTimer(undoTimer_);
    if (window_) {
        window_->onWindowClosed = nullptr;
        window_->onWindowBlur = nullptr;
        if (open_) window_->Close();
    }
}

bool UltraDesktopClipboardPanel::IsOpen() const {
    return open_ && window_;
}

void UltraDesktopClipboardPanel::Open(int rightX, int topY, Target target) {
    target_ = std::move(target);
    if (IsOpen()) {
        Reload();
        window_->RaiseAndFocus();
        return;
    }
    WindowConfig config;
    config.title = "Clipboard";
    config.type = WindowType::Borderless;
    config.width = kPanelWidth;
    config.height = kPanelHeight;
    config.resizable = false;
    config.minimizable = false;
    config.maximizable = false;
    config.minWidth = kPanelWidth;
    config.minHeight = kPanelHeight;
    config.backgroundColor = kPanel;
    int screenW = 0, screenH = 0;
    UltraCanvasDesktopShell::GetScreenSize(screenW, screenH);
    window_ = CreateWindow(config);
    if (!window_) return;
    const int physW = window_->LogicalToPhysical(kPanelWidth);
    const int physH = window_->LogicalToPhysical(kPanelHeight);
    int x = rightX - physW;
    int y = topY;
    if (rightX < 0 && topY < 0 && screenW > 0) {
        x = (screenW - physW) / 2;
        y = (screenH - physH) / 3;
    }
    if (screenW > 0) x = std::clamp(x, 0, std::max(0, screenW - physW));
    if (screenH > 0) y = std::clamp(y, 0, std::max(0, screenH - physH));
    window_->SetWindowPosition(x, y);

    Build();
    open_ = true;
    closing_ = false;
    window_->onWindowClosed = [this]() {
        open_ = false;
        window_.reset();
    };
    // Gone when something else is chosen, like a menu.
    window_->onWindowBlur = [this]() { CloseSoon(); };
    window_->InstallEventFilter("clipboard-panel-keys",
                                [this](const UCEvent& event) { return OnKey(event); },
                                {UCEventType::KeyDown});
    Reload();
    window_->Show();
    window_->RaiseAndFocus();
    search_->SetFocus(true);
}

void UltraDesktopClipboardPanel::Close() {
    if (!window_) return;
    open_ = false;
    window_->onWindowBlur = nullptr;
    window_->Close();
}

// Closing from inside the window's own event (its focus leaving) waits for
// that event to finish.
void UltraDesktopClipboardPanel::CloseSoon() {
    if (closing_ || !IsOpen()) return;
    closing_ = true;
    UltraCanvasApplication::GetInstance()->StartTimer(1, false, [this](TimerId) {
        closing_ = false;
        Close();
    });
}

void UltraDesktopClipboardPanel::Build() {
    UltraCanvasWindow* window = window_.get();
    window->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->SetPadding(14, 12, 12, 12);
    window->SetBackgroundColor(kPanel);

    // Title, and the shortcut that opens this.
    auto header = std::make_shared<UltraCanvasContainer>("cbp.header");
    header->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    auto title = CreateLabel("cbp.title", 0, 0, 200, 22, "Clipboard");
    title->SetFontSize(11.5f);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(kText);
    title->layoutItem.SetFlexGrow(1);
    header->AddChild(title);
    auto shortcut = CreateLabel("cbp.shortcut", 0, 0, 70, 22, "Super+V");
    shortcut->SetFontSize(8.5f);
    shortcut->SetTextColor(Color(200, 200, 200));
    shortcut->SetAlignment(TextAlignment::Right);
    header->AddChild(shortcut);
    window->AddChild(header);

    search_ = CreateTextInput("cbp.search", 0, 0, 0, 32);
    search_->SetPlaceholder("Type to search");
    search_->SetShowPlaceholderAlways(true);
    TextInputStyle searchStyle = search_->GetStyle();
    searchStyle.backgroundColor = kInset;
    searchStyle.borderColor = kBar;
    searchStyle.focusBorderColor = Colors::Selection;
    searchStyle.textColor = kText;
    searchStyle.placeholderColor = kMuted;
    searchStyle.caretColor = kText;
    searchStyle.borderRadius = 6;
    searchStyle.paddingLeft = 10;
    search_->SetStyle(searchStyle);
    search_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    search_->onTextChanged = [this](const std::string&) { Reload(); };
    window->AddChild(search_);

    model_ = std::make_shared<ClipboardHistoryListModel>();
    rows_ = std::make_shared<ClipboardHistoryRowDelegate>(ClipboardRowStyle::Dark());
    list_ = std::make_shared<UltraCanvasListView>("cbp.list");
    ListViewStyle listStyle;
    listStyle.backgroundColor = rows_->Style().listBackground;
    listStyle.selectionBackgroundColor = rows_->Style().selectionBackground;
    listStyle.hoverBackgroundColor = rows_->Style().hoverBackground;
    list_->SetStyle(listStyle);
    list_->SetModel(model_);
    list_->SetDelegate(rows_);
    list_->SetVariableRowHeights(true);
    list_->SetBackgroundColor(kPanel);
    list_->SetBorders(0);
    list_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    list_->onCellHovered = [this](int row, int, const Point2Di& at) {
        if (rows_->SetHover(row, at)) list_->RequestRedraw();
    };
    list_->onCellClicked = [this](int row, int, const Point2Di& at) {
        const ClipboardHistoryEntry* entry = model_->GetEntry(row);
        if (!entry) return;
        switch (rows_->ActionAt(row, at)) {
            case ClipboardRowAction::Copy: CopyEntry(entry->id); break;
            case ClipboardRowAction::Edit: EditEntry(entry->id); break;
            case ClipboardRowAction::Delete: DeleteEntry(entry->id); break;
            default: break;
        }
    };
    list_->onItemActivated = [this](int row) {
        if (const ClipboardHistoryEntry* entry = model_->GetEntry(row)) CopyEntry(entry->id);
    };
    list_->onItemDoubleClicked = list_->onItemActivated;
    list_->onSelectionChanged = [this](const std::vector<int>& selected) {
        // A header is not something to choose: go to the entry under it.
        if (selected.size() == 1 && !model_->GetEntry(selected[0])) {
            const int next = model_->EntryRowFrom(selected[0], 1);
            if (next >= 0) SelectRow(next);
        }
    };
    // Over an action: its name and key; elsewhere the model's text of the entry.
    list_->tooltipProvider = [this](int row, int) -> std::string {
        return ClipboardHistoryRowDelegate::ActionTooltip(rows_->GetHoverAction(row));
    };
    window->AddChild(list_);

    // The recording switch, the way into the application, and - for a few
    // seconds after a removal - Undo.
    auto footer = std::make_shared<UltraCanvasContainer>("cbp.footer");
    footer->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    footer->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    footer->SetPadding(8, 0, 0, 0);
    footer->SetBorderTop(1, Color(58, 58, 58));
    const bool paused = history_ ? history_->GetPolicy().recordingPaused : false;
    recording_ = UltraCanvasSwitch::Create("cbp.recording", 0, 0, "Recording", !paused);
    recording_->layoutItem.SetFlexShrink(0);
    SwitchVisualStyle switchStyle = recording_->GetVisualStyle();
    switchStyle.base.textColor = kText;
    switchStyle.base.textHoverColor = kText;
    recording_->SetVisualStyle(switchStyle);
    recording_->onStateChanged = [this](CheckedState, CheckedState now) {
        if (!history_) return;
        ClipboardHistoryPolicy policy = history_->GetPolicy();
        policy.recordingPaused = now != CheckedState::Checked;
        history_->SetPolicy(policy);
    };
    footer->AddChild(recording_);
    status_ = CreateLabel("cbp.status", 0, 0, 40, 24, "");
    status_->SetFontSize(8.5f);
    status_->SetTextColor(kMuted);
    status_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    footer->AddChild(status_);
    undo_ = std::make_shared<UltraCanvasButton>("cbp.undo", 0, 0, 56, 28, "Undo");
    ButtonStyle undoStyle = undo_->GetStyle();
    undoStyle.normalColor = kRaised;
    undoStyle.hoverColor = kBar;
    undoStyle.pressedColor = kBar;
    undoStyle.normalTextColor = Color(147, 197, 253);
    undoStyle.hoverTextColor = Color(191, 219, 254);
    undoStyle.pressedTextColor = Color(191, 219, 254);
    undoStyle.borderColor = kBar;
    undoStyle.cornerRadius = 6;
    undo_->SetStyle(undoStyle);
    undo_->SetOnClick([this]() { Undo(); });
    undo_->SetVisible(false);
    undo_->layoutItem.SetFlexShrink(0);
    footer->AddChild(undo_);
    auto openApp = std::make_shared<UltraCanvasButton>("cbp.open", 0, 0, 196, 30, "Open UltraClipboard");
    openApp->layoutItem.SetFlexShrink(0);
    ButtonStyle openStyle = openApp->GetStyle();
    openStyle.normalColor = kRaised;
    openStyle.hoverColor = Color(58, 58, 58);
    openStyle.pressedColor = kBar;
    openStyle.normalTextColor = kText;
    openStyle.hoverTextColor = kText;
    openStyle.pressedTextColor = kText;
    openStyle.borderColor = kBar;
    openStyle.cornerRadius = 6;
    openStyle.useIconAsMask = true;
    openApp->SetStyle(openStyle);
    openApp->SetIcon(GetResourcesDir() + "media/icons/clipboard/external.svg");
    openApp->SetIconSize(14, 14);
    openApp->SetIconPosition(ButtonIconPosition::Right);
    openApp->SetOnClick([this]() {
        if (actions_.openApplication) actions_.openApplication(search_ ? search_->GetText() : "");
        Close();
    });
    footer->AddChild(openApp);
    window->AddChild(footer);
}

void UltraDesktopClipboardPanel::Refresh() {
    if (IsOpen()) Reload();
}

void UltraDesktopClipboardPanel::Reload() {
    if (!history_ || !model_) return;
    const int64_t selected = SelectedEntry();
    const std::string text = search_ ? search_->GetText() : "";
    std::vector<ClipboardHistoryEntry> shown;
    ClipboardHistoryListModel::LeadSection lead;
    if (text.empty()) {
        // What the program being pasted into takes: the most wanted kind
        // first, the newest of each kind first within it.
        std::vector<int64_t> leading;
        if (!target_.kinds.empty()) {
            ClipboardHistoryQuery wanted;
            wanted.kinds = target_.kinds;
            wanted.newestFirst = true;
            std::vector<ClipboardHistoryEntry> matches = history_->List(wanted);
            auto rank = [this](ClipboardEntryKind kind) {
                return std::find(target_.kinds.begin(), target_.kinds.end(), kind) - target_.kinds.begin();
            };
            std::stable_sort(matches.begin(), matches.end(),
                             [&rank](const ClipboardHistoryEntry& a, const ClipboardHistoryEntry& b) {
                                 return rank(a.kind) < rank(b.kind);
                             });
            if (matches.size() > kTargetShown) matches.resize(kTargetShown);
            for (auto& entry : matches) {
                leading.push_back(entry.id);
                shown.push_back(std::move(entry));
            }
            if (!leading.empty()) {
                lead.title = EditClipboardText("For " + target_.name, ClipboardTextEdit::Upper);
                lead.count = leading.size();
            }
        }
        size_t recent = 0;
        for (auto& entry : history_->List()) {
            if (std::find(leading.begin(), leading.end(), entry.id) != leading.end()) continue;
            if (!entry.pinned && recent++ >= kRecentShown) continue;
            shown.push_back(std::move(entry));
        }
    } else {
        ClipboardHistoryQuery query;
        query.text = text;
        query.limit = kFoundShown;
        shown = history_->List(query);
    }
    model_->SetEntries(std::move(shown), ClipboardHistoryListModel::Sections::PinnedAndRecent, lead);
    if (recording_ && history_->IsOpen()) {
        const bool recordingNow = !history_->GetPolicy().recordingPaused;
        if (recording_->IsChecked() != recordingNow) recording_->SetChecked(recordingNow);
    }
    if (model_->GetEntryCount() == 0) {
        if (status_ && removedId_ == 0) status_->SetText(text.empty() ? "Nothing copied yet" : "Nothing found");
        list_->ResetSelection();
        return;
    }
    if (status_ && removedId_ == 0) status_->SetText("");
    // Keep the chosen entry chosen; otherwise the first one for the program
    // being pasted into, or else the newest one (not a pinned one: what was
    // just copied is what is wanted most).
    int row = selected ? model_->FindRow(selected) : -1;
    if (row < 0 && lead.count > 0) row = model_->EntryRowFrom(0, 1);
    if (row < 0) {
        for (int r = 0; r < model_->GetRowCount(); ++r) {
            const ClipboardHistoryEntry* entry = model_->GetEntry(r);
            if (entry && !entry->pinned) { row = r; break; }
        }
    }
    if (row < 0) row = model_->EntryRowFrom(0, 1);
    SelectRow(row);
}

int64_t UltraDesktopClipboardPanel::SelectedEntry() const {
    if (!list_ || !model_ || !list_->GetSelection()) return 0;
    const ClipboardHistoryEntry* entry = model_->GetEntry(list_->GetSelection()->GetCurrentRow());
    return entry ? entry->id : 0;
}

void UltraDesktopClipboardPanel::SelectRow(int row) {
    if (!list_ || row < 0) return;
    list_->GetSelection()->Select(row);
    list_->EnsureRowVisible(row);
    list_->RequestRedraw();
}

void UltraDesktopClipboardPanel::Move(int step) {
    if (!list_ || !model_) return;
    const int current = list_->GetSelection()->GetCurrentRow();
    int next = model_->EntryRowFrom(current < 0 ? 0 : current + step, step);
    if (next < 0) next = current;
    SelectRow(next);
}

bool UltraDesktopClipboardPanel::OnKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown) return false;
    switch (event.virtualKey) {
        case UCKeys::Escape:
            CloseSoon();
            return true;
        case UCKeys::Up:
            Move(-1);
            return true;
        case UCKeys::Down:
            Move(1);
            return true;
        case UCKeys::Return:
            if (const int64_t id = SelectedEntry()) CopyEntry(id);
            return true;
        case UCKeys::F2:
            if (const int64_t id = SelectedEntry()) EditEntry(id);
            return true;
        case UCKeys::Delete:
            if (event.ctrl || (search_ && search_->GetText().empty())) {
                if (const int64_t id = SelectedEntry()) DeleteEntry(id);
                return true;
            }
            return false;
        default:
            if (event.ctrl && (event.virtualKey == UCKeys::E || event.character == 'e')) {
                if (const int64_t id = SelectedEntry()) EditEntry(id);
                return true;
            }
            return false;
    }
}

void UltraDesktopClipboardPanel::CopyEntry(int64_t id) {
    if (actions_.copy) actions_.copy(id);
    CloseSoon();
}

void UltraDesktopClipboardPanel::EditEntry(int64_t id) {
    if (actions_.edit) actions_.edit(id);
    CloseSoon();
}

void UltraDesktopClipboardPanel::DeleteEntry(int64_t id) {
    if (!history_ || !history_->Remove(id)) return;
    removedId_ = id;
    if (status_) status_->SetText("Deleted");
    if (undo_) undo_->SetVisible(true);
    auto* app = UltraCanvasApplication::GetInstance();
    if (undoTimer_) app->StopTimer(undoTimer_);
    undoTimer_ = app->StartTimer(UltraCanvasClipboardHistory::kUndoSeconds * 1000, false, [this](TimerId) {
        undoTimer_ = 0;
        removedId_ = 0;
        if (undo_) undo_->SetVisible(false);
        if (status_) status_->SetText("");
    });
    Reload();
}

void UltraDesktopClipboardPanel::Undo() {
    if (!history_ || removedId_ == 0) return;
    history_->Restore(removedId_);
    const int64_t restored = removedId_;
    removedId_ = 0;
    if (undoTimer_) UltraCanvasApplication::GetInstance()->StopTimer(undoTimer_);
    undoTimer_ = 0;
    if (undo_) undo_->SetVisible(false);
    if (status_) status_->SetText("");
    Reload();
    SelectRow(model_->FindRow(restored));
}

} // namespace UltraDesktop
