// Apps/UltraClipboard/ui/UltraClipboardWindow.cpp
// UltraClipboard's main window. See UltraClipboardWindow.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraClipboardWindow.h"
#include "ClipboardDialogs.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasClipboardHistoryView.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

#ifndef ULTRACLIPBOARD_VERSION
#error "ULTRACLIPBOARD_VERSION must come from Docs/UltraClipboard/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace UltraClipboard {
namespace {

constexpr int kTickMs = 250;
constexpr int kMessageTicks = 12;    // 3 s
constexpr int kGenerationTicks = 4;  // the history changed elsewhere? once a second

const Color kChrome(250, 251, 252);
const Color kLine(229, 231, 235);
const Color kMuted(107, 114, 128);
const Color kLink(37, 99, 235);

// The filter segments, in order.
enum Filter { All, Texts, Images, Files, Links, Pinned };

ClipboardHistoryQuery QueryFor(int filter, const std::string& text) {
    ClipboardHistoryQuery query;
    query.text = text;
    switch (filter) {
        case Texts:
            query.kinds = {ClipboardEntryKind::Text, ClipboardEntryKind::Code, ClipboardEntryKind::RichText,
                           ClipboardEntryKind::Colour};
            break;
        case Images: query.kinds = {ClipboardEntryKind::Image}; break;
        case Files: query.kinds = {ClipboardEntryKind::Files}; break;
        case Links: query.kinds = {ClipboardEntryKind::Link}; break;
        case Pinned: query.pinnedOnly = true; break;
        default: break;
    }
    return query;
}

std::shared_ptr<UltraCanvasButton> IconButton(const std::string& id, const std::string& icon,
                                              const std::string& tooltip) {
    auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 34, 34, "");
    ButtonStyle style = button->GetStyle();
    style.normalColor = Colors::Transparent;
    style.hoverColor = Color(229, 231, 235);
    style.pressedColor = Color(209, 213, 219);
    style.borderColor = Colors::Transparent;
    style.cornerRadius = 6;
    style.useIconAsMask = true;
    // A mask icon takes the text colour.
    style.normalTextColor = Color(75, 85, 99);
    style.hoverTextColor = Color(17, 24, 39);
    style.pressedTextColor = Color(17, 24, 39);
    button->SetStyle(style);
    button->SetIcon(GetResourcesDir() + "media/icons/clipboard/" + icon);
    button->SetIconSize(18, 18);
    button->SetTooltip(tooltip);
    button->layoutItem.SetFlexShrink(0);
    return button;
}

std::string Plural(size_t count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

std::string KeptFor(int days) {
    if (days % 365 == 0) return Plural(static_cast<size_t>(days / 365), "year", "years");
    return Plural(static_cast<size_t>(days), "day", "days");
}

// Where an image waits for UltraPaint: a folder only this user can enter -
// the session's runtime folder on Linux, the per-user temporary folder on
// Windows and macOS, and in a /tmp shared with other users a folder of a
// random name this process made itself.
std::filesystem::path ImageHandOffFolder() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path folder;
    if (const std::string runtime = GetEnvUtf8("XDG_RUNTIME_DIR"); !runtime.empty()) {
        folder = PathFromUtf8(runtime) / "UltraClipboard";
        fs::create_directories(folder, ec);
    } else {
#if defined(_WIN32) || defined(__APPLE__)
        folder = fs::temp_directory_path(ec) / "UltraClipboard";
        fs::create_directories(folder, ec);
#else
        std::random_device random;
        char name[32];
        std::snprintf(name, sizeof(name), "UltraClipboard-%08x%08x", random(), random());
        folder = fs::temp_directory_path(ec) / name;
        if (!fs::create_directory(folder, ec)) return {};
#endif
    }
    fs::permissions(folder, fs::perms::owner_all, fs::perm_options::replace, ec);
    return fs::is_directory(folder, ec) ? folder : fs::path();
}

} // namespace

UltraClipboardWindow::UltraClipboardWindow(UltraCanvasApplication& app) : app_(app) {}

UltraClipboardWindow::~UltraClipboardWindow() {
    if (timer_) app_.StopTimer(timer_);
    if (recorder_) recorder_->Detach();
    // A removal not undone is final now.
    if (history_ && history_->IsOpen()) history_->Prune();
}

bool UltraClipboardWindow::Create(const std::string& search, int64_t editId) {
    history_ = std::make_unique<UltraCanvasClipboardHistory>();
    std::string openError;
    if (!history_->Open()) {
        openError = history_->GetLastError();
        history_.reset();
    }

    WindowConfig config;
    config.title = std::string("UltraClipboard ") + ULTRACLIPBOARD_VERSION;
    config.width = 1000;
    config.height = 720;
    config.minWidth = 640;
    config.minHeight = 400;
    config.backgroundColor = Colors::White;
    window_ = CreateWindow(config);
    if (!window_) return false;

    UltraCanvasWindow* window = window_.get();
    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->SetBackgroundColor(Colors::White);
    window->AddChild(BuildToolbar());

    model_ = std::make_shared<ClipboardHistoryListModel>();
    rows_ = std::make_shared<ClipboardHistoryRowDelegate>(ClipboardRowStyle::Light());
    list_ = std::make_shared<UltraCanvasListView>("ucb.list");
    ListViewStyle listStyle;
    listStyle.backgroundColor = rows_->Style().listBackground;
    listStyle.selectionBackgroundColor = rows_->Style().selectionBackground;
    listStyle.hoverBackgroundColor = rows_->Style().hoverBackground;
    list_->SetStyle(listStyle);
    list_->SetModel(model_);
    list_->SetDelegate(rows_);
    list_->SetVariableRowHeights(true);
    list_->SetBorders(0);
    list_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    list_->onCellHovered = [this](int row, int, const Point2Di& at) {
        if (rows_->SetHover(row, at)) list_->RequestRedraw();
    };
    list_->onCellClicked = [this](int row, int, const Point2Di& at) {
        const ClipboardHistoryEntry* entry = model_->GetEntry(row);
        if (!entry) return;
        const int64_t id = entry->id;
        switch (rows_->ActionAt(row, at)) {
            case ClipboardRowAction::Copy: CopyEntry(id); break;
            case ClipboardRowAction::Edit: EditEntry(id); break;
            case ClipboardRowAction::Delete: DeleteEntry(id); break;
            default: break;
        }
    };
    list_->onItemDoubleClicked = [this](int row) {
        if (const ClipboardHistoryEntry* entry = model_->GetEntry(row)) CopyEntry(entry->id);
    };
    list_->onSelectionChanged = [this](const std::vector<int>& selected) {
        // A header is not something to choose: go to the entry under it.
        if (selected.size() == 1 && !model_->GetEntry(selected[0])) {
            const int next = model_->EntryRowFrom(selected[0], 1);
            if (next >= 0) SelectRow(next);
        }
    };
    list_->onContextMenu = [this](int row, const UCEvent& event) { ShowRowMenu(row, event); };
    // Over an action: its name and key; elsewhere the model's text of the entry.
    list_->tooltipProvider = [this](int row, int) -> std::string {
        return ClipboardHistoryRowDelegate::ActionTooltip(rows_->GetHoverAction(row));
    };
    window->AddChild(list_);
    window->AddChild(BuildStatusBar());

    window->InstallEventFilter("ultraclipboard-keys", [this](const UCEvent& event) { return OnKey(event); },
                               {UCEventType::KeyDown});
    window->onWindowClosing = [this]() {
        app_.RequestExit();
        return true;
    };

    if (!history_) {
        // Shown, not hidden: an empty list would say "nothing copied".
        status_->SetText("The clipboard history could not be opened: " + openError);
        status_->SetTextColor(Color(185, 28, 28));
        search_->SetDisabled(true);
        kinds_->SetDisabled(true);
        recording_->SetDisabled(true);
        return true;
    }

    // Records while no desktop does (the desktop's recorder outranks this one).
    recorder_ = std::make_unique<UltraCanvasClipboardRecorder>();
    recorder_->onRecorded = [this](int64_t, ClipboardRecordResult result) {
        if (result == ClipboardRecordResult::Recorded || result == ClipboardRecordResult::MovedToTop) Reload();
    };
    if (auto* clipboard = GetClipboard()) recorder_->Attach(history_.get(), clipboard, "ultraclipboard", 0);

    generation_ = history_->GetGeneration();
    search_->SetText(search);
    pendingEdit_ = editId;
    Reload();
    timer_ = app_.StartTimer(kTickMs, true, [this](TimerId) { Tick(); });
    return true;
}

void UltraClipboardWindow::Show() {
    if (!window_) return;
    window_->Show();
    search_->SetFocus(true);
    if (pendingEdit_ != 0) {
        // Once the window is on the screen, so the dialog has a parent to sit on.
        app_.StartTimer(100, false, [this](TimerId) {
            const int64_t id = pendingEdit_;
            pendingEdit_ = 0;
            SelectRow(model_->FindRow(id));
            EditEntry(id);
        });
    }
}

std::shared_ptr<UltraCanvasContainer> UltraClipboardWindow::BuildToolbar() {
    auto toolbar = std::make_shared<UltraCanvasContainer>("ucb.toolbar");
    toolbar->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    toolbar->SetPadding(12, 16, 10, 16);
    toolbar->SetBackgroundColor(kChrome);
    toolbar->SetBorderBottom(1, kLine);

    // The search, the recording switch, settings and the rest.
    auto top = std::make_shared<UltraCanvasContainer>("ucb.top");
    top->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    top->layoutItem.SetFlexShrink(0);
    search_ = CreateTextInput("ucb.search", 0, 0, 240, 34);
    search_->SetPlaceholder("Search clipboard history   (Ctrl+F)");
    search_->SetShowPlaceholderAlways(true);
    TextInputStyle searchStyle = search_->GetStyle();
    searchStyle.borderRadius = 6;
    searchStyle.paddingLeft = 10;
    search_->SetStyle(searchStyle);
    search_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    search_->onTextChanged = [this](const std::string&) { Reload(); };
    top->AddChild(search_);

    const bool paused = history_ ? history_->GetPolicy().recordingPaused : false;
    recording_ = UltraCanvasSwitch::Create("ucb.recording", 0, 0, "Recording", !paused);
    recording_->SetTooltip("Record what is copied. Off: copies are not kept until it is on again.");
    recording_->layoutItem.SetFlexShrink(0);
    recording_->onStateChanged = [this](CheckedState, CheckedState now) {
        if (!updatingRecording_) SetRecording(now == CheckedState::Checked);
    };
    top->AddChild(recording_);

    auto settings = IconButton("ucb.settings", "settings.svg", "Settings");
    settings->SetOnClick([this]() { OpenSettings(); });
    top->AddChild(settings);
    moreButton_ = std::make_shared<UltraCanvasButton>("ucb.more", 0, 0, 34, 34, "\xE2\x8B\xAF");
    ButtonStyle moreStyle = moreButton_->GetStyle();
    moreStyle.normalColor = Colors::Transparent;
    moreStyle.hoverColor = Color(229, 231, 235);
    moreStyle.pressedColor = Color(209, 213, 219);
    moreStyle.borderColor = Colors::Transparent;
    moreStyle.cornerRadius = 6;
    moreStyle.fontSize = 14;
    moreButton_->SetStyle(moreStyle);
    moreButton_->SetTooltip("More");
    moreButton_->layoutItem.SetFlexShrink(0);
    moreButton_->SetOnClick([this]() { ShowMoreMenu(); });
    top->AddChild(moreButton_);
    toolbar->AddChild(top);

    // What to list.
    kinds_ = CreateSegmentedControl("ucb.kinds", 0, 0, 420, 30);
    for (const char* name : {"All", "Text", "Images", "Files", "Links", "Pinned"}) kinds_->AddSegment(name);
    kinds_->SetWidthMode(SegmentWidthMode::FitContent);
    SegmentedControlStyle kindStyle = SegmentedControlStyle::Flat();
    kindStyle.normalTextColor = Color(55, 65, 81);
    kindStyle.hoverColor = Color(229, 231, 235);
    kindStyle.paddingHorizontal = 14;
    kindStyle.cornerRadius = 15;
    kindStyle.fontSize = 10;
    kinds_->SetStyle(kindStyle);
    kinds_->SetSelectedIndex(All);
    kinds_->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
    kinds_->onSegmentSelected = [this](int) { Reload(); };
    toolbar->AddChild(kinds_);
    return toolbar;
}

std::shared_ptr<UltraCanvasContainer> UltraClipboardWindow::BuildStatusBar() {
    auto bar = std::make_shared<UltraCanvasContainer>("ucb.status");
    bar->layout.SetFlexRow().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bar->SetPadding(5, 16, 5, 16);
    bar->SetBackgroundColor(kChrome);
    bar->SetBorderTop(1, kLine);

    status_ = CreateLabel("ucb.stats", 0, 0, 200, 24, "");
    status_->SetFontSize(9);
    status_->SetTextColor(kMuted);
    status_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bar->AddChild(status_);
    message_ = CreateLabel("ucb.message", 0, 0, 220, 24, "");
    message_->SetFontSize(9);
    message_->SetTextColor(Color(17, 24, 39));
    message_->SetAlignment(TextAlignment::Right);
    message_->layoutItem.SetFlexShrink(1);
    bar->AddChild(message_);
    undo_ = std::make_shared<UltraCanvasButton>("ucb.undo", 0, 0, 60, 24, "Undo");
    ButtonStyle undoStyle = undo_->GetStyle();
    undoStyle.normalColor = Colors::Transparent;
    undoStyle.hoverColor = Color(219, 234, 254);
    undoStyle.pressedColor = Color(191, 219, 254);
    undoStyle.normalTextColor = kLink;
    undoStyle.hoverTextColor = kLink;
    undoStyle.pressedTextColor = kLink;
    undoStyle.borderColor = Colors::Transparent;
    undoStyle.cornerRadius = 5;
    undoStyle.fontSize = 9;
    undo_->SetStyle(undoStyle);
    undo_->SetTooltip("Undo  Ctrl+Z");
    undo_->SetVisible(false);
    undo_->layoutItem.SetFlexShrink(0);
    undo_->SetOnClick([this]() { Undo(); });
    bar->AddChild(undo_);
    hint_ = CreateLabel("ucb.hint", 0, 0, 200, 24, "Super+V opens the quick panel");
    hint_->SetFontSize(9);
    hint_->SetTextColor(kMuted);
    hint_->SetAlignment(TextAlignment::Right);
    hint_->layoutItem.SetFlexShrink(0);
    bar->AddChild(hint_);
    return bar;
}

void UltraClipboardWindow::Reload() {
    if (!history_ || !model_) return;
    const ClipboardHistoryEntry* before = SelectedEntry();
    const int64_t selected = before ? before->id : 0;
    const std::string text = search_->GetText();
    model_->SetEntries(history_->List(QueryFor(kinds_->GetSelectedIndex(), text)),
                       ClipboardHistoryListModel::Sections::ByDay);
    updatingRecording_ = true;
    const bool recordingNow = !history_->GetPolicy().recordingPaused;
    if (recording_->IsChecked() != recordingNow) recording_->SetChecked(recordingNow);
    updatingRecording_ = false;
    UpdateStatus();
    if (model_->GetEntryCount() == 0) {
        list_->ResetSelection();
        list_->RequestRedraw();
        return;
    }
    int row = selected ? model_->FindRow(selected) : -1;
    if (row < 0) row = model_->EntryRowFrom(0, 1);
    SelectRow(row);
}

// "126 entries · 18.2 MB · kept for 30 days · pinned entries never expire",
// or what the list is empty of.
void UltraClipboardWindow::UpdateStatus() {
    if (!history_ || !status_) return;
    const ClipboardHistoryStats stats = history_->GetStats();
    const ClipboardHistoryPolicy policy = history_->GetPolicy();
    std::string text;
    if (model_->GetEntryCount() == 0) {
        if (!search_->GetText().empty()) text = "Nothing found for \xE2\x80\x9C" + search_->GetText() + "\xE2\x80\x9D \xC2\xB7 ";
        else if (kinds_->GetSelectedIndex() != All && stats.entries > 0) text = "None of these \xC2\xB7 ";
        else if (stats.entries == 0) text = "Nothing copied yet: what you copy appears here \xC2\xB7 ";
    } else if (!search_->GetText().empty() || kinds_->GetSelectedIndex() != All) {
        text = Plural(model_->GetEntryCount(), "match", "matches") + " \xC2\xB7 ";
    }
    if (policy.recordingPaused) text += "Recording is paused \xC2\xB7 ";
    text += Plural(stats.entries, "entry", "entries") + " \xC2\xB7 " + FormatClipboardSize(stats.bytes) +
            " \xC2\xB7 kept for " + KeptFor(policy.maxAgeDays) + " \xC2\xB7 pinned entries never expire";
    status_->SetText(text);
}

void UltraClipboardWindow::Tick() {
    if (recorder_) recorder_->Tick();
    if (history_ && ++generationTicks_ >= kGenerationTicks) {
        generationTicks_ = 0;
        const uint64_t generation = history_->GetGeneration();
        if (generation != generation_) {
            generation_ = generation;
            Reload();
        }
    }
    // The Copy glyph's check, while it lasts.
    const bool copied = rows_ && rows_->IsShowingCopied();
    if (copied || copiedShowing_) list_->RequestRedraw();
    copiedShowing_ = copied;
    if (messageTicks_ > 0 && --messageTicks_ == 0) message_->SetText("");
    if (undoTicks_ > 0 && --undoTicks_ == 0) {
        removedId_ = 0;
        undo_->SetVisible(false);
        message_->SetText("");
    }
}

bool UltraClipboardWindow::OnKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown || !history_) return false;
    // An open menu takes its own keys (a dialog is a window of its own).
    if (event.targetElement && event.targetElement->IsPopupElement()) return false;
    const bool searching = search_ && !search_->GetText().empty();
    auto chosen = [this]() -> int64_t {
        const ClipboardHistoryEntry* entry = SelectedEntry();
        return entry ? entry->id : 0;
    };
    auto letter = [&event](char c) {
        return event.ctrl && (event.character == c || event.character == c - 'a' + 'A' ||
                              event.virtualKey == static_cast<UCKeys>(UCKeys::A + (c - 'a')));
    };
    switch (event.virtualKey) {
        case UCKeys::Escape:
            if (!searching) return false;
            search_->SetText("");
            return true;
        case UCKeys::Up: Move(-1); return true;
        case UCKeys::Down: Move(1); return true;
        case UCKeys::Return:
            if (const int64_t id = chosen()) CopyEntry(id);
            return true;
        case UCKeys::F2:
            if (const int64_t id = chosen()) EditEntry(id);
            return true;
        case UCKeys::Delete:
            // In a search field with text, Delete deletes a character.
            if (!event.ctrl && searching && search_->IsFocused()) return false;
            if (const int64_t id = chosen()) DeleteEntry(id);
            return true;
        default: break;
    }
    if (letter('f')) {
        search_->SetFocus(true);
        search_->SelectAll();
        return true;
    }
    if (letter('e')) {
        if (const int64_t id = chosen()) EditEntry(id);
        return true;
    }
    if (letter('p')) {
        if (const int64_t id = chosen()) TogglePin(id);
        return true;
    }
    if (letter('z') && removedId_ != 0) {
        Undo();
        return true;
    }
    return false;
}

const ClipboardHistoryEntry* UltraClipboardWindow::SelectedEntry() const {
    if (!list_ || !model_ || !list_->GetSelection()) return nullptr;
    return model_->GetEntry(list_->GetSelection()->GetCurrentRow());
}

void UltraClipboardWindow::SelectRow(int row) {
    if (!list_ || row < 0) return;
    list_->GetSelection()->Select(row);
    list_->EnsureRowVisible(row);
    list_->RequestRedraw();
}

void UltraClipboardWindow::Move(int step) {
    if (!list_ || !model_) return;
    const int current = list_->GetSelection()->GetCurrentRow();
    int next = model_->EntryRowFrom(current < 0 ? 0 : current + step, step);
    if (next < 0) next = current;
    SelectRow(next);
}

void UltraClipboardWindow::CopyEntry(int64_t id) {
    auto* clipboard = GetClipboard();
    if (!history_ || !clipboard) return;
    std::vector<ClipboardFormat> formats;
    if (!history_->ReadFormats(id, formats) || !RestoreToClipboard(*clipboard, formats)) {
        UltraCanvasAlert::Error("This entry can no longer be read from the clipboard history.", "Copy", nullptr,
                                window_.get());
        return;
    }
    history_->MarkUsed(id);
    if (recorder_) recorder_->Tick();   // what is on the clipboard now is this entry, not a new copy
    rows_->ShowCopied(id);
    Message("Copied to the clipboard");
    Reload();
    SelectRow(model_->FindRow(id));
}

void UltraClipboardWindow::EditEntry(int64_t id) {
    if (!history_) return;
    const std::optional<ClipboardHistoryEntry> entry = history_->Get(id);
    if (!entry) return;
    if (entry->kind == ClipboardEntryKind::Image) {
        OpenInUltraPaint(id);
        return;
    }
    if (entry->kind == ClipboardEntryKind::Files) {
        Message("Files are edited where they are: open them in UltraFiler");
        return;
    }
    ShowEditDialog(window_.get(), *history_, *entry, [this](int64_t newId, bool copyIt) {
        Reload();
        SelectRow(model_->FindRow(newId));
        if (copyIt) CopyEntry(newId);
        else Message("Saved");
    });
}

void UltraClipboardWindow::DeleteEntry(int64_t id) {
    if (!history_ || !history_->Remove(id)) return;
    // Removing is soft until the undo time has passed; a new removal ends
    // the last one's chance.
    removedId_ = id;
    undoTicks_ = UltraCanvasClipboardHistory::kUndoSeconds * 1000 / kTickMs;
    messageTicks_ = 0;
    message_->SetText("Deleted");
    undo_->SetVisible(true);
    // The row below takes the removed one's place.
    const int row = list_->GetSelection()->GetCurrentRow();
    Reload();
    int next = model_->EntryRowFrom(std::max(0, row), 1);
    if (next < 0) next = model_->EntryRowFrom(std::max(0, row), -1);
    SelectRow(next);
}

void UltraClipboardWindow::Undo() {
    if (!history_ || removedId_ == 0) return;
    const int64_t restored = removedId_;
    history_->Restore(restored);
    removedId_ = 0;
    undoTicks_ = 0;
    undo_->SetVisible(false);
    message_->SetText("");
    Reload();
    SelectRow(model_->FindRow(restored));
}

void UltraClipboardWindow::TogglePin(int64_t id) {
    if (!history_) return;
    const std::optional<ClipboardHistoryEntry> entry = history_->Get(id);
    if (!entry || !history_->SetPinned(id, !entry->pinned)) return;
    Message(entry->pinned ? "Unpinned" : "Pinned: kept whatever the limits say");
    Reload();
    SelectRow(model_->FindRow(id));
}

// UltraPaint edits the image; a copy made there comes back as a new entry.
void UltraClipboardWindow::OpenInUltraPaint(int64_t id) {
    namespace fs = std::filesystem;
    std::vector<ClipboardFormat> formats;
    ClipboardSnapshot snapshot;
    if (!history_ || !history_->ReadFormats(id, formats)) return;
    snapshot.formats = std::move(formats);
    const ClipboardFormat* image = snapshot.FindImage();
    if (!image) return;
    std::string extension;
    const std::vector<uint8_t> bytes = ClipboardImageFile(*image, extension);
    const fs::path folder = ImageHandOffFolder();
    if (folder.empty()) {
        UltraCanvasAlert::Error("There is no folder to hand the image over in.", "Edit", nullptr, window_.get());
        return;
    }
    const fs::path file = folder / PathFromUtf8("clipboard-" + std::to_string(id) + "." + extension);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        UltraCanvasAlert::Error("The image could not be written to " + PathToUtf8(folder) + ".", "Edit", nullptr,
                                window_.get());
        return;
    }
    std::string error;
    if (!UltraCanvasDesktopShell::LaunchProgram("UltraPaint", {PathToUtf8(file)}, &error)) {
        UltraCanvasAlert::Error("UltraPaint could not be started: " + error, "Edit", nullptr, window_.get());
        return;
    }
    Message("Opened in UltraPaint: copy the result to add it here");
}

void UltraClipboardWindow::ShowRowMenu(int row, const UCEvent& event) {
    const ClipboardHistoryEntry* entry = model_ ? model_->GetEntry(row) : nullptr;
    if (!entry || !window_) return;
    const int64_t id = entry->id;
    menu_ = std::make_shared<UltraCanvasMenu>("ucb.rowMenu", 0, 0, 220, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::ActionWithShortcut("Copy", "Enter", [this, id]() { CopyEntry(id); }));
    if (entry->kind == ClipboardEntryKind::Image) {
        menu_->AddItem(MenuItemData::ActionWithShortcut("Edit in UltraPaint", "F2", [this, id]() { EditEntry(id); }));
    } else if (entry->kind != ClipboardEntryKind::Files) {
        menu_->AddItem(MenuItemData::ActionWithShortcut("Edit\xE2\x80\xA6", "F2", [this, id]() { EditEntry(id); }));
    }
    menu_->AddItem(MenuItemData::ActionWithShortcut(entry->pinned ? "Unpin" : "Pin", "Ctrl+P",
                                                    [this, id]() { TogglePin(id); }));
    menu_->AddItem(MenuItemData::Separator());
    menu_->AddItem(MenuItemData::ActionWithShortcut("Delete", "Del", [this, id]() { DeleteEntry(id); }));
    PopupElementSettings settings;
    menu_->OpenMenu(event.pointerWindow, *window_, settings);
}

void UltraClipboardWindow::ShowMoreMenu() {
    if (!window_ || !history_) return;
    menu_ = std::make_shared<UltraCanvasMenu>("ucb.moreMenu", 0, 0, 260, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::ActionWithShortcut("Find", "Ctrl+F", [this]() {
        search_->SetFocus(true);
        search_->SelectAll();
    }));
    menu_->AddItem(MenuItemData::Action("Settings\xE2\x80\xA6", [this]() { OpenSettings(); }));
    menu_->AddItem(MenuItemData::Separator());
    menu_->AddItem(MenuItemData::Action("Clear history\xE2\x80\xA6", [this]() { ConfirmClear(); }));
    const Rect2Df bounds = moreButton_->GetBoundsInWindow();
    PopupElementSettings settings;
    menu_->OpenMenu(Point2Di(static_cast<int>(bounds.x + bounds.width) - 260,
                             static_cast<int>(bounds.y + bounds.height + 2)),
                    *window_, settings);
}

void UltraClipboardWindow::OpenSettings() {
    if (!history_) return;
    ShowSettingsDialog(window_.get(), *history_, [this]() {
        history_->Prune();
        Message("Settings saved");
        Reload();
    });
}

void UltraClipboardWindow::ConfirmClear() {
    if (!history_) return;
    const ClipboardHistoryStats stats = history_->GetStats();
    const size_t unpinned = stats.entries - std::min(stats.entries, stats.pinned);
    std::string question = "Remove the " + Plural(unpinned, "entry", "entries") +
                           " that are not pinned from the clipboard history?";
    if (stats.pinned > 0) question += " The " + Plural(stats.pinned, "pinned entry stays", "pinned entries stay") + ".";
    UltraCanvasAlert::Confirm(question, "Clear clipboard history", [this](bool yes) {
        if (!yes || !history_) return;
        history_->Clear(false);
        removedId_ = 0;
        undoTicks_ = 0;
        undo_->SetVisible(false);
        Message("History cleared");
        Reload();
    }, window_.get());
}

void UltraClipboardWindow::SetRecording(bool on) {
    if (!history_) return;
    ClipboardHistoryPolicy policy = history_->GetPolicy();
    policy.recordingPaused = !on;
    history_->SetPolicy(policy);
    generation_ = history_->GetGeneration();
    Message(on ? "Recording copies" : "Recording paused: copies are not kept");
    UpdateStatus();
}

void UltraClipboardWindow::Message(const std::string& text) {
    if (!message_) return;
    if (undoTicks_ > 0) {   // the removal's Undo goes first
        removedId_ = 0;
        undoTicks_ = 0;
        undo_->SetVisible(false);
    }
    message_->SetText(text);
    messageTicks_ = kMessageTicks;
}

} // namespace UltraClipboard
