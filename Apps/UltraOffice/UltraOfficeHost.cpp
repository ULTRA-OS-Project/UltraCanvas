// Apps/UltraOffice/UltraOfficeHost.cpp
// Helpers every kind of tab shares.
//
// Version: 0.4.0 - AddFontList, FontListSettings, ApplyFontChoice
// Version: 0.3.0 - AddActionGroup; an icon-only tool has a narrower padding
// Version: 0.2.0 - the view group's bar, the percent boxes
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeSettings.h"
#include "UltraOfficeToolbar.h"

#include "UltraCanvasAppSettings.h"
#include "UltraCanvasRichTextEdit.h"

#include "UltraCanvasConfig.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <charconv>

namespace UltraOffice {

using namespace UltraCanvas;

std::string IconPath(const std::string& relative) {
    return NormalizePath(GetResourcesDir() + "media/icons/" + relative);
}

std::shared_ptr<UltraCanvasToolbar> MakeToolbar(const std::string& id) {
    auto toolbar = std::make_shared<UltraCanvasToolbar>(id, 0, 0, 0, kToolbarHeight);
    // Flat, in the theme's colours; a window too narrow for every tool puts
    // the rest behind a chevron menu.
    toolbar->SetAppearance(ToolbarAppearance::FromTheme(UltraCanvasTheme::Current(), ToolbarAppearance::Flat()));
    toolbar->SetOverflowMode(ToolbarOverflowMode::Menu);
    toolbar->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(kToolbarHeight));
    return toolbar;
}

std::shared_ptr<UltraCanvasButton> AddTool(UltraCanvasToolbar& toolbar, const std::string& id, const std::string& text,
                                           const std::string& icon, const std::string& tip, std::function<void()> action) {
    auto button = toolbar.AddButton(id, text, icon.empty() ? std::string() : IconPath(icon),
                                    [action]() { if (action) action(); });
    button->SetTooltip(tip);
    button->SetAcceptsFocus(false);   // pressing a tool must not take the keyboard from the document
    if (text.empty()) button->SetPadding(4, 5, 4, 5);   // an icon alone: 34 px, so a bar holds more
    return button;
}

std::shared_ptr<UltraCanvasButton> AddToggleTool(UltraCanvasToolbar& toolbar, const std::string& id, const std::string& text,
                                                 const std::string& icon, const std::string& tip, std::function<void()> action) {
    auto button = toolbar.AddToggleButton(id, text, icon.empty() ? std::string() : IconPath(icon),
                                          [action](bool) { if (action) action(); });
    button->SetTooltip(tip);
    button->SetAcceptsFocus(false);
    if (text.empty()) button->SetPadding(4, 5, 4, 5);
    return button;
}

std::shared_ptr<UltraCanvasToolGroupButton> AddActionGroup(UltraCanvasToolbar& toolbar, const std::string& id,
                                                           const std::vector<GroupTool>& tools) {
    std::vector<ToolGroupItem> items;
    for (const GroupTool& tool : tools) items.push_back({tool.id, IconPath(tool.icon), tool.tip});
    auto group = toolbar.AddToolGroup(id, items);
    group->SetAcceptsFocus(false);
    group->SetPadding(4, 5, 4, 5);
    UltraCanvasToolGroupButton* raw = group.get();
    group->onItemSelected = [raw, tools](const std::string& picked) {
        // An action, not a mode: the slot is not left pressed.
        raw->SetPressed(false);
        for (const GroupTool& tool : tools)
            if (tool.id == picked && tool.action) { tool.action(); break; }
    };
    return group;
}

std::shared_ptr<UltraCanvasToolbar> MakeViewToolbar(const std::string& id) {
    auto toolbar = std::make_shared<UltraCanvasToolbar>(id, 0, 0, 0, kToolbarHeight);
    toolbar->SetAppearance(ToolbarAppearance::FromTheme(UltraCanvasTheme::Current(), ToolbarAppearance::Flat()));
    toolbar->SetOverflowMode(ToolbarOverflowMode::OverflowNone);
    toolbar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kToolbarHeight));
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return toolbar;
}

std::string PercentText(int percent) { return TrF("{0} %", percent); }

void ShowPercent(UltraCanvasDropdown& box, const std::vector<int>& choices, int percent) {
    const auto it = std::find(choices.begin(), choices.end(), percent);
    if (it != choices.end()) {
        box.SetSelectedIndex(static_cast<int>(it - choices.begin()), false);
    } else {
        box.SetSelectedIndex(-1, false);
        box.SetPlaceholderText(PercentText(percent));
    }
}

std::shared_ptr<UltraCanvasDropdown> AddPercentBox(UltraCanvasToolbar& toolbar, const std::string& id,
                                                   const std::vector<int>& choices, int minimum, int maximum,
                                                   const std::string& tip, const std::string& askTitle,
                                                   UltraCanvasWindowBase* window, std::function<void(int)> onPick) {
    std::vector<std::string> items;
    for (int percent : choices) items.push_back(PercentText(percent));
    items.push_back(Tr("Other..."));
    auto box = toolbar.AddDropdownButton(id, "", items, nullptr);
    box->SetTooltip(tip);
    box->SetAcceptsFocus(false);
    box->size.width = CSSLayout::Dimension::Px(74);
    UltraCanvasDropdown* raw = box.get();
    box->onSelectionChanged = [raw, choices, minimum, maximum, askTitle, window, onPick](int index, const DropdownItem&) {
        if (index >= 0 && index < static_cast<int>(choices.size())) {
            if (onPick) onPick(choices[static_cast<size_t>(index)]);
            return;
        }
        if (index != static_cast<int>(choices.size())) return;
        // "Other...": a whole number typed in, kept within the range.
        raw->SetSelectedIndex(-1, false);
        UltraCanvasDialogManager::ShowInputDialog(
            TrF("A percentage from {0} to {1}:", minimum, maximum), askTitle, "", InputType::Number,
            [minimum, maximum, onPick](DialogResult result, const std::string& typed) {
                if (result != DialogResult::OK) return;
                int percent = 0;
                const char* begin = typed.data();
                while (*begin == ' ') ++begin;
                const auto parsed = std::from_chars(begin, typed.data() + typed.size(), percent);
                if (parsed.ec != std::errc() || percent <= 0) return;
                if (onPick) onPick(std::clamp(percent, minimum, maximum));
            }, window);
    };
    return box;
}

// ===== FONT LISTS =====

void FontListSettings(UltraCanvasFontPicker& list, std::function<UltraCanvasAppSettings*()> settings) {
    list.SetRecentLimit(OfficeSettings::kMaxRecentFonts);
    if (settings) {
        if (UltraCanvasAppSettings* store = settings()) list.SetRecentFonts(OfficeSettings(*store).GetRecentFonts());
    }
    // The recent fonts are the window's, shared by every tab and dialog:
    // read as the list opens, kept as one is chosen.
    auto before = list.onListOpening;
    list.onListOpening = [raw = &list, settings, before]() {
        if (settings) {
            if (UltraCanvasAppSettings* store = settings()) raw->SetRecentFonts(OfficeSettings(*store).GetRecentFonts());
        }
        if (before) before();
    };
    list.onRecentFontsChanged = [settings](const std::vector<std::string>& families) {
        UltraCanvasAppSettings* store = settings ? settings() : nullptr;
        if (!store) return;
        OfficeSettings prefs(*store);
        prefs.SetRecentFonts(families);
        prefs.Save();
    };
}

std::shared_ptr<UltraCanvasFontPicker> AddFontList(UltraCanvasToolbar& toolbar, const std::string& id,
                                                   std::function<UltraCanvasAppSettings*()> settings,
                                                   std::function<std::vector<std::string>()> documentFonts) {
    auto list = std::make_shared<UltraCanvasFontPicker>(id, 0, 0, 130, 26);
    list->SetTooltip(Tr("Font"));
    list->size.width = CSSLayout::Dimension::Px(130);   // the bar's slot; the list opens wider
    list->SetListWidth(300);
    list->SetAcceptsFocus(false);   // the keyboard stays with the document; the open list takes what is typed
    UltraCanvasFontPicker* raw = list.get();
    list->onListOpening = [raw, documentFonts]() {
        if (documentFonts) raw->SetDocumentFonts(documentFonts());
    };
    FontListSettings(*list, std::move(settings));
    toolbar.AddWidget(id, list);
    return list;
}

FontChoiceRun FontChoiceForRun(const FontPickerChoice& choice) {
    FontChoiceRun run;
    run.family = choice.family;
    if (!choice.fromStyles) return run;
    run.setsStyle = true;
    run.italic = choice.slant != FontSlant::Normal;
    run.bold = choice.weight >= 600;
    // Bold and italic reach 400 and 700 at the normal width; another weight
    // or width is kept by the face's own family name, where it has one.
    const bool boldItalicReach = ((choice.weight >= 370 && choice.weight <= 430) ||
                                  (choice.weight >= 670 && choice.weight <= 730)) && choice.stretch == 100;
    if (!boldItalicReach && !choice.faceFamily.empty()) {
        run.family = choice.faceFamily;
        run.bold = choice.faceFamilyBold;   // "Ubuntu Light" in bold is Ubuntu Medium
    }
    return run;
}

void ApplyFontChoice(UltraCanvasRichTextEdit& edit, const FontPickerChoice& choice) {
    const FontChoiceRun run = FontChoiceForRun(choice);
    RichCharFormatDelta delta;
    delta.setFontFamily = true;
    delta.fontFamily = run.family;
    if (run.setsStyle) {
        delta.setBold = true;
        delta.bold = run.bold;
        delta.setItalic = true;
        delta.italic = run.italic;
    }
    edit.ApplyCharFormat(delta);
}

} // namespace UltraOffice
