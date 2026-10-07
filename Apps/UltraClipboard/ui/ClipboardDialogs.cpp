// Apps/UltraClipboard/ui/ClipboardDialogs.cpp
// The edit and settings dialogs. See ClipboardDialogs.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ClipboardDialogs.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasClipboardHistoryView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextArea.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace UltraClipboard {
namespace {

constexpr float kDialogWidth = 620;
constexpr float kContentWidth = 572;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

std::shared_ptr<UltraCanvasButton> ToolButton(const std::string& id, const std::string& text, float width) {
    auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, text);
    button->layoutItem.SetFlexShrink(0);
    return button;
}

struct EditState {
    std::string originalText;
    std::string html;          // the entry's HTML, while it still matches the text
    bool keepHtml = false;
    std::shared_ptr<UltraCanvasMenu> caseMenu;
};

} // namespace

void ShowEditDialog(UltraCanvasWindowBase* parent, UltraCanvasClipboardHistory& history,
                    const ClipboardHistoryEntry& entry, std::function<void(int64_t, bool)> onSaved) {
    std::vector<ClipboardFormat> formats;
    if (!history.ReadFormats(entry.id, formats)) {
        UltraCanvasAlert::Error("This entry can no longer be read from the clipboard history.", "Edit", nullptr,
                                parent);
        return;
    }
    ClipboardSnapshot original;
    original.formats = formats;
    auto state = std::make_shared<EditState>();
    state->originalText = original.GetText();
    if (const ClipboardFormat* html = original.Find(ClipboardMime::Html)) {
        state->html.assign(html->data.begin(), html->data.end());
        state->keepHtml = !state->html.empty();
    }

    DialogConfig config;
    config.title = "Edit clipboard entry";
    config.message = DescribeClipboardEntry(entry, NowMs(), false);
    config.dialogType = DialogType::Information;
    config.showIcon = false;
    config.buttons = DialogButtons::NoButtons;
    config.width = static_cast<int>(kDialogWidth);
    config.height = 520;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    dialog->SetIconVisible(false);
    UltraCanvasModalDialog* dialogWindow = dialog.get();

    auto area = std::make_shared<UltraCanvasTextArea>("cbe.text", 0, 0, kContentWidth, 280);
    area->SetText(state->originalText);
    area->SetWordWrap(entry.kind != ClipboardEntryKind::Code);
    area->SetBorders(1, Color(209, 213, 219), 6);
    if (entry.kind == ClipboardEntryKind::Code) {
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            area->SetFontFamily(app->GetDefaultMonospacedFontStyle().fontFamily);
        }
    }

    // The tools.
    auto tools = std::make_shared<UltraCanvasContainer>("cbe.tools", 0, 0, kContentWidth, 32);
    tools->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    UltraCanvasTextArea* text = area.get();
    if (entry.kind == ClipboardEntryKind::RichText) {
        auto plain = ToolButton("cbe.plain", "Plain text", 92);
        plain->SetTooltip("Keep the text without its formatting");
        plain->SetOnClick([state, plainButton = plain.get()]() {
            state->keepHtml = false;
            plainButton->SetDisabled(true);
        });
        tools->AddChild(plain);
    }
    auto trim = ToolButton("cbe.trim", "Trim", 64);
    trim->SetTooltip("Remove spaces at the ends of lines and empty lines at the ends");
    trim->SetOnClick([text]() { text->SetText(EditClipboardText(text->GetText(), ClipboardTextEdit::Trim)); });
    tools->AddChild(trim);
    auto caseButton = ToolButton("cbe.case", "Aa  Case \xE2\x96\xBE", 108);
    caseButton->SetOnClick([state, text, dialogWindow, button = caseButton.get()]() {
        state->caseMenu = std::make_shared<UltraCanvasMenu>("cbe.caseMenu", 0, 0, 180, 0);
        state->caseMenu->SetMenuType(MenuType::PopupMenu);
        const std::pair<const char*, ClipboardTextEdit> choices[] = {
            {"UPPER CASE", ClipboardTextEdit::Upper}, {"lower case", ClipboardTextEdit::Lower},
            {"Title Case", ClipboardTextEdit::Title}, {"Sentence case", ClipboardTextEdit::Sentence}};
        for (const auto& [label, edit] : choices) {
            state->caseMenu->AddItem(MenuItemData::Action(label, [text, edit = edit]() {
                text->SetText(EditClipboardText(text->GetText(), edit));
            }));
        }
        const Rect2Df bounds = button->GetBoundsInWindow();
        PopupElementSettings settings;
        state->caseMenu->OpenMenu(Point2Di(static_cast<int>(bounds.x), static_cast<int>(bounds.y + bounds.height)),
                                  *dialogWindow, settings);
    });
    tools->AddChild(caseButton);
    auto join = ToolButton("cbe.join", "Join lines", 86);
    join->SetTooltip("Make the text one line");
    join->SetOnClick([text]() { text->SetText(EditClipboardText(text->GetText(), ClipboardTextEdit::JoinLines)); });
    tools->AddChild(join);

    auto keep = UltraCanvasCheckbox::CreateCheckbox("cbe.keep", 0, 0, kContentWidth, 26,
                                                    "Keep the original entry as well", true);
    dialog->AddDialogElement(tools);
    dialog->AddDialogElement(area);
    dialog->AddDialogElement(keep);
    dialog->AddCustomButton("Cancel", DialogResult::Cancel, DialogButtonRole::Cancel);
    dialog->AddCustomButton("Save", DialogResult::Yes, DialogButtonRole::Normal);
    dialog->AddCustomButton("Save and copy", DialogResult::OK, DialogButtonRole::Default);

    UltraCanvasClipboardHistory* store = &history;
    const int64_t id = entry.id;
    UltraCanvasCheckbox* keepBox = keep.get();
    UltraCanvasDialogManager::ShowDialog(dialog, [store, id, state, text, keepBox, onSaved, parent](DialogResult result) {
        if (result != DialogResult::OK && result != DialogResult::Yes) return;
        ClipboardSnapshot edited;
        const std::string newText = text->GetText();
        if (newText.empty()) return;
        edited.SetText(newText);
        // The formatting goes back only with the text it formats.
        if (state->keepHtml && newText == state->originalText) {
            edited.formats.push_back({ClipboardMime::Html, std::vector<uint8_t>(state->html.begin(), state->html.end())});
        }
        const int64_t newId = store->Replace(id, edited, keepBox->IsChecked());
        if (newId == 0) {
            UltraCanvasAlert::Error("The edited entry could not be saved: " + store->GetLastError(), "Edit", nullptr,
                                    parent);
            return;
        }
        if (onSaved) onSaved(newId, result == DialogResult::OK);
    }, parent);
}

// ===== SETTINGS =====
namespace {

struct Choice {
    std::string label;
    uint64_t value;
};

// "~/.config/ultraos/clipboard.key": a path under the home folder, shorter.
std::string HomeRelative(const std::string& path) {
    const std::string home = GetEnvUtf8("HOME");
    if (home.size() > 1 && path.rfind(home + "/", 0) == 0) return "~" + path.substr(home.size());
    return path;
}

std::shared_ptr<UltraCanvasDropdown> ChoiceRow(const std::shared_ptr<UltraCanvasModalDialog>& dialog,
                                               const std::string& id, const std::string& caption,
                                               const std::vector<Choice>& choices, uint64_t current) {
    auto row = std::make_shared<UltraCanvasContainer>(id + ".row", 0, 0, kContentWidth, 30);
    row->layout.SetFlexRow().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto label = CreateLabel(id + ".label", 0, 0, 220, 24, caption);
    label->layoutItem.SetFlexShrink(0);
    row->AddChild(label);
    auto dropdown = CreateDropdown(id, 0, 0, 200, 26);
    size_t nearest = 0;
    uint64_t bestDistance = UINT64_MAX;
    for (size_t i = 0; i < choices.size(); ++i) {
        dropdown->AddItem(choices[i].label);
        const uint64_t distance = choices[i].value > current ? choices[i].value - current : current - choices[i].value;
        if (distance < bestDistance) {
            bestDistance = distance;
            nearest = i;
        }
    }
    dropdown->SetSelectedIndex(static_cast<int>(nearest), false);
    row->AddChild(dropdown);
    dialog->AddDialogElement(row);
    return dropdown;
}

} // namespace

void ShowSettingsDialog(UltraCanvasWindowBase* parent, UltraCanvasClipboardHistory& history,
                        std::function<void()> onChanged) {
    const ClipboardHistoryPolicy policy = history.GetPolicy();
    const ClipboardHistoryStats stats = history.GetStats();

    DialogConfig config;
    config.title = "Clipboard history settings";
    config.message = "What the clipboard history keeps, and for how long. Pinned entries are kept whatever "
                      "the limits say.";
    config.dialogType = DialogType::Information;
    config.showIcon = false;
    config.buttons = DialogButtons::NoButtons;
    config.width = static_cast<int>(kDialogWidth);
    config.height = 610;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    dialog->SetIconVisible(false);

    constexpr uint64_t MB = 1024ull * 1024;
    const std::vector<Choice> entryChoices = {{"100 entries", 100}, {"200 entries", 200}, {"500 entries", 500},
                                              {"1,000 entries", 1000}, {"2,000 entries", 2000}};
    const std::vector<Choice> ageChoices = {{"1 day", 1}, {"7 days", 7}, {"30 days", 30}, {"90 days", 90},
                                            {"1 year", 365}};
    const std::vector<Choice> totalChoices = {{"128 MB", 128 * MB}, {"256 MB", 256 * MB}, {"512 MB", 512 * MB},
                                              {"1 GB", 1024 * MB}, {"2 GB", 2048 * MB}};
    const std::vector<Choice> entrySizeChoices = {{"16 MB", 16 * MB}, {"64 MB", 64 * MB}, {"256 MB", 256 * MB}};
    auto entries = ChoiceRow(dialog, "cbs.entries", "Keep at most", entryChoices,
                             static_cast<uint64_t>(policy.maxEntries));
    auto age = ChoiceRow(dialog, "cbs.age", "For", ageChoices, static_cast<uint64_t>(policy.maxAgeDays));
    auto total = ChoiceRow(dialog, "cbs.total", "Up to", totalChoices, policy.maxTotalBytes);
    auto largest = ChoiceRow(dialog, "cbs.largest", "Largest copy kept", entrySizeChoices, policy.maxEntryBytes);

    auto thumbnails = UltraCanvasCheckbox::CreateCheckbox("cbs.thumbnails", 0, 0, kContentWidth, 26,
                                                          "Show thumbnails of images", policy.imageThumbnails);
    dialog->AddDialogElement(thumbnails);

    dialog->AddDialogElement(CreateLabel("cbs.excludedLabel", 0, 0, kContentWidth, 24,
                                         "Never record copies from these programs (one per line):"));
    auto excluded = std::make_shared<UltraCanvasTextArea>("cbs.excluded", 0, 0, kContentWidth, 104);
    excluded->SetBorders(1, Color(209, 213, 219), 6);
    std::string excludedText;
    for (const std::string& name : policy.excludedApplications) excludedText += name + "\n";
    excluded->SetText(excludedText);
    dialog->AddDialogElement(excluded);

    std::string security;
    if (stats.encrypted) {
        security = "Titles, texts and copies are encrypted (XChaCha20-Poly1305) with a key kept at " +
                   HomeRelative(UltraCanvasClipboardHistory::DefaultKeyPath()) + ", apart from the history in " +
                   HomeRelative(history.GetDirectory()) + ". That protects a copy of the history folder and a backup of it. "
                   "A program running as you can read the clipboard itself. Copies that password managers "
                   "mark as secret are never recorded.";
    } else {
        security = "This build cannot encrypt (it has no libsodium): the history is stored as it was copied. "
                   "Copies that password managers mark as secret are never recorded.";
    }
    auto securityLabel = CreateLabel("cbs.security", 0, 0, kContentWidth, 96, security);
    securityLabel->SetWrap(TextWrap::WrapWord);
    securityLabel->SetFontSize(8.5f);
    securityLabel->SetTextColor(Color(90, 96, 105));
    dialog->AddDialogElement(securityLabel);

    dialog->AddCustomButton("Cancel", DialogResult::Cancel, DialogButtonRole::Cancel);
    dialog->AddCustomButton("Save", DialogResult::OK, DialogButtonRole::Default);

    UltraCanvasClipboardHistory* store = &history;
    UltraCanvasDropdown* entriesBox = entries.get();
    UltraCanvasDropdown* ageBox = age.get();
    UltraCanvasDropdown* totalBox = total.get();
    UltraCanvasDropdown* largestBox = largest.get();
    UltraCanvasCheckbox* thumbnailsBox = thumbnails.get();
    UltraCanvasTextArea* excludedBox = excluded.get();
    UltraCanvasDialogManager::ShowDialog(dialog, [=](DialogResult result) {
        if (result != DialogResult::OK) return;
        auto pick = [](UltraCanvasDropdown* box, const std::vector<Choice>& choices) {
            const int index = std::clamp(box->GetSelectedIndex(), 0, static_cast<int>(choices.size()) - 1);
            return choices[static_cast<size_t>(index)].value;
        };
        ClipboardHistoryPolicy updated = store->GetPolicy();
        updated.maxEntries = static_cast<int>(pick(entriesBox, entryChoices));
        updated.maxAgeDays = static_cast<int>(pick(ageBox, ageChoices));
        updated.maxTotalBytes = pick(totalBox, totalChoices);
        updated.maxEntryBytes = pick(largestBox, entrySizeChoices);
        updated.imageThumbnails = thumbnailsBox->IsChecked();
        updated.excludedApplications.clear();
        std::string line;
        for (char c : excludedBox->GetText() + "\n") {
            if (c != '\n' && c != '\r') {
                line += c;
                continue;
            }
            const size_t begin = line.find_first_not_of(" \t");
            const size_t end = line.find_last_not_of(" \t");
            if (begin != std::string::npos) updated.excludedApplications.push_back(line.substr(begin, end - begin + 1));
            line.clear();
        }
        store->SetPolicy(updated);
        if (onChanged) onChanged();
    }, parent);
}

} // namespace UltraClipboard
