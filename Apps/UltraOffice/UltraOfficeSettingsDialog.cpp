// Apps/UltraOffice/UltraOfficeSettingsDialog.cpp
// See the header. The window, the page skeleton and the controls come from
// the framework's UltraCanvasSettingsPageKit; the controls are the
// framework's elements (a text field, lists, checkboxes, buttons).
//
// Version: 0.1.1 - the format list on Saving wide enough for its names
// Version: 0.1.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeSettingsDialog.h"

#include "UltraOfficeSettings.h"
#include "UltraOfficeToolbar.h"   // PercentText
#include "UltraOfficeWindow.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSettingsPageKit.h"
#include "UltraCanvasTextInput.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraOffice {

using namespace UltraCanvas;
using namespace UltraCanvas::SettingsPageKit;

namespace {

constexpr int kControlWidth = 260;
constexpr int kCaptionWidth = 170;
constexpr int kWideControlWidth = 320;   // the format list: "Office Open XML (.docx, .xlsx, .pptx)"

const std::vector<int> kRecentCounts = {0, 4, 8, 12, 20, 30};
const std::vector<int> kMinutes = {1, 2, 5, 10, 15, 30, 60};

// "10.5", whatever the locale, as the toolbar's size list writes it.
std::string SizeText(float size) {
    char text[32];
    const auto result = std::to_chars(text, text + sizeof text, size);
    return std::string(text, result.ptr);
}

// The one open window: the kit's shell with this window's controls, each
// set from the settings by Refresh.
struct DialogState : public UltraCanvasSettingsWindow {
    UltraOfficeWindow* owner = nullptr;
    bool syncing = false;   // Refresh is setting the controls, not a person

    std::shared_ptr<UltraCanvasTextInput> author;
    std::shared_ptr<UltraCanvasDropdown> language;
    std::vector<std::string> languages;   // by the list's index; "system" first
    std::shared_ptr<UltraCanvasDropdown> theme;
    std::shared_ptr<UltraCanvasDropdown> recentCount;
    std::shared_ptr<UltraCanvasCheckbox> reopen;

    std::shared_ptr<UltraCanvasDropdown> format;
    std::shared_ptr<UltraCanvasCheckbox> autosave;
    std::shared_ptr<UltraCanvasCheckbox> recovery;
    std::shared_ptr<UltraCanvasDropdown> minutes;

    std::shared_ptr<UltraCanvasDropdown> font;
    std::shared_ptr<UltraCanvasDropdown> size;
    std::shared_ptr<UltraCanvasDropdown> page;
    std::shared_ptr<UltraCanvasCheckbox> navigator;
    std::shared_ptr<UltraCanvasDropdown> fit;
    std::shared_ptr<UltraCanvasDropdown> share;

    std::shared_ptr<UltraCanvasCheckbox> gridlines;

    std::shared_ptr<UltraCanvasDropdown> slideSize;
    std::shared_ptr<UltraCanvasCheckbox> secondScreen;
    std::shared_ptr<UltraCanvasCheckbox> presenter;

    OfficeSettings& Settings() { return owner->Settings(); }
};

std::shared_ptr<DialogState> g_dialog;

// A change made in the window: saved, and applied where it applies at once.
void Changed(DialogState* d) {
    if (d->syncing || !d->owner) return;
    d->Settings().Save();
    d->owner->ApplySettings();
}

std::shared_ptr<UltraCanvasDropdown> MakeList(const std::string& id, const std::vector<std::string>& items,
                                              std::function<void(int)> onPick) {
    auto list = std::make_shared<UltraCanvasDropdown>(id, 0, 0, kControlWidth, 26);
    for (const std::string& item : items) list->AddItem(item);
    list->size.width = CSSLayout::Dimension::Px(kControlWidth);
    list->size.height = CSSLayout::Dimension::Px(26);
    list->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    list->onSelectionChanged = [onPick](int index, const DropdownItem&) { if (index >= 0 && onPick) onPick(index); };
    return list;
}

template <typename T>
int IndexOf(const std::vector<T>& items, const T& value) {
    const auto it = std::find(items.begin(), items.end(), value);
    return it == items.end() ? -1 : static_cast<int>(it - items.begin());
}

// The controls as the settings are.
void Refresh(DialogState* d) {
    OfficeSettings& s = d->Settings();
    d->syncing = true;
    if (d->author) {
        d->author->SetText(s.GetAuthorName());
        d->author->SetPlaceholder(OfficeSettings::LoginName());
    }
    if (d->language) {
        const int at = IndexOf(d->languages, s.GetLanguage());
        d->language->SetSelectedIndex(at >= 0 ? at : 0, false);
    }
    if (d->theme) d->theme->SetSelectedIndex(static_cast<int>(s.GetTheme()), false);
    if (d->recentCount) {
        const int at = IndexOf(kRecentCounts, s.GetRecentCount());
        if (at >= 0) d->recentCount->SetSelectedIndex(at, false);
        else { d->recentCount->SetSelectedIndex(-1, false); d->recentCount->SetPlaceholderText(std::to_string(s.GetRecentCount())); }
    }
    if (d->reopen) d->reopen->SetChecked(s.GetReopenFiles());
    if (d->format) d->format->SetSelectedIndex(s.GetSaveFormat() == SaveFormat::OpenDocument ? 1 : 0, false);
    if (d->autosave) d->autosave->SetChecked(s.GetAutosave());
    if (d->recovery) d->recovery->SetChecked(s.GetRecoveryCopies());
    if (d->minutes) {
        const int at = IndexOf(kMinutes, s.GetAutosaveMinutes());
        if (at >= 0) d->minutes->SetSelectedIndex(at, false);
        else { d->minutes->SetSelectedIndex(-1, false); d->minutes->SetPlaceholderText(std::to_string(s.GetAutosaveMinutes())); }
    }
    if (d->font) {
        const int at = IndexOf(OfficeSettings::FontChoices(), s.GetDocumentFont());
        if (at >= 0) d->font->SetSelectedIndex(at, false);
        else { d->font->SetSelectedIndex(-1, false); d->font->SetPlaceholderText(s.GetDocumentFont()); }
    }
    if (d->size) {
        const int at = IndexOf(OfficeSettings::FontSizeChoices(), s.GetDocumentFontSize());
        if (at >= 0) d->size->SetSelectedIndex(at, false);
        else { d->size->SetSelectedIndex(-1, false); d->size->SetPlaceholderText(SizeText(s.GetDocumentFontSize())); }
    }
    if (d->page) d->page->SetSelectedIndex(s.GetPageSize() == PageSizeChoice::Letter ? 1 : 0, false);
    if (d->navigator) d->navigator->SetChecked(s.GetShowNavigator());
    if (d->fit) {
        const DocumentFit fit = s.GetDocumentFit();
        d->fit->SetSelectedIndex(fit == DocumentFit::WholePage ? 1 : fit == DocumentFit::WidthShare ? 2
                                 : fit == DocumentFit::Actual ? 3 : 0, false);
    }
    if (d->share) {
        std::vector<int> shares;
        for (float choice : OfficeSettings::WidthShareChoices()) shares.push_back(static_cast<int>(std::lround(choice * 100.0f)));
        const int percent = static_cast<int>(std::lround(s.GetWidthShare() * 100.0f));
        const int at = IndexOf(shares, percent);
        if (at >= 0) d->share->SetSelectedIndex(at, false);
        else { d->share->SetSelectedIndex(-1, false); d->share->SetPlaceholderText(PercentText(percent)); }
    }
    if (d->gridlines) d->gridlines->SetChecked(s.GetShowGridlines());
    if (d->slideSize) d->slideSize->SetSelectedIndex(s.GetSlideSize() == SlideSizeChoice::Standard ? 1 : 0, false);
    if (d->secondScreen) d->secondScreen->SetChecked(s.GetShowOnSecondScreen());
    if (d->presenter) d->presenter->SetChecked(s.GetPresenterView());
    d->syncing = false;
}

std::shared_ptr<UltraCanvasCheckbox> Box(DialogState* d, const std::string& id, const std::string& text,
                                         std::function<void(OfficeSettings&, bool)> set) {
    return MakeCheckbox(id, text, kTextWidth, false, [d, set](bool on) {
        if (d->syncing) return;
        set(d->Settings(), on);
        Changed(d);
    });
}

// A page's Restore defaults: its settings back to theirs, the controls with
// them.
void AddReset(DialogState* d, const std::string& page, std::function<void(OfficeSettings&)> reset) {
    d->resets[page] = PageReset{Tr("Restore defaults"), 170, [d, reset]() {
        reset(d->Settings());
        Refresh(d);
        Changed(d);
    }};
}

// ===== GENERAL =====

std::shared_ptr<UltraCanvasContainer> BuildGeneral(DialogState* d) {
    PageParts parts = MakePage("office-set-general", Tr("General"),
                               Tr("Who you are, and how UltraOffice looks and starts:"));
    d->author = std::make_shared<UltraCanvasTextInput>("office-set-author", 0, 0, kControlWidth, 26);
    d->author->size.width = CSSLayout::Dimension::Px(kControlWidth);
    d->author->size.height = CSSLayout::Dimension::Px(26);
    d->author->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    d->author->onTextChanged = [d](const std::string& text) {
        if (d->syncing) return;
        d->Settings().SetAuthorName(text);
        d->Settings().Save();
    };
    // The open documents sign with it once it is typed.
    d->author->onFocusLost = [d]() { Changed(d); };
    d->author->onEnterPressed = [d](const std::string&) { Changed(d); return true; };
    parts.body->AddChild(MakeLabelledRow("office-set-author-row", Tr("Your name:"), d->author, kCaptionWidth));

    // The languages there are catalogues for; "system" is the one every
    // UltraCanvas application shares (UOS-Settings sets it).
    d->languages = {"system"};
    std::vector<std::string> names = {Tr("System language")};
    for (const std::string& tag : UltraLocalisation::AvailableLanguages()) {
        d->languages.push_back(tag);
        names.push_back(UltraLocalisation::NativeName(tag) + " (" + tag + ")");
    }
    d->language = MakeList("office-set-language", names, [d](int index) {
        if (d->syncing || index >= static_cast<int>(d->languages.size())) return;
        d->Settings().SetLanguage(d->languages[static_cast<size_t>(index)]);
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-language-row", Tr("Language:"), d->language, kCaptionWidth));

    d->theme = MakeList("office-set-theme", {Tr("As the desktop"), Tr("Light"), Tr("Dark")}, [d](int index) {
        if (d->syncing) return;
        d->Settings().SetTheme(static_cast<ThemeChoice>(std::clamp(index, 0, 2)));
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-theme-row", Tr("Look:"), d->theme, kCaptionWidth));

    std::vector<std::string> counts;
    for (int count : kRecentCounts) counts.push_back(count == 0 ? Tr("None") : std::to_string(count));
    d->recentCount = MakeList("office-set-recent", counts, [d](int index) {
        if (d->syncing || index >= static_cast<int>(kRecentCounts.size())) return;
        d->Settings().SetRecentCount(kRecentCounts[static_cast<size_t>(index)]);
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-recent-row", Tr("Recent files kept:"), d->recentCount, kCaptionWidth));
    auto clearRow = MakeButtonRow("office-set-clear-row");
    clearRow->AddChild(MakeButton("office-set-clear-recent", Tr("Clear recent files"), 180, [d]() {
        if (d->owner) d->owner->ClearRecentFiles();
    }));
    parts.body->AddChild(clearRow);

    d->reopen = Box(d, "office-set-reopen", Tr("Reopen the files open at the last close"),
                    [](OfficeSettings& s, bool on) { s.SetReopenFiles(on); });
    parts.body->AddChild(d->reopen);

    AddNote(parts, "office-set-general-note1",
            Tr("Your name signs comments and tracked changes; if empty, your login name is used."));
    AddNote(parts, "office-set-general-note2",
            Tr("The language takes effect at the next start; everything else at once."));
    AddReset(d, kSettingsGeneral, [](OfficeSettings& s) {
        s.SetAuthorName("");
        s.SetLanguage("system");
        s.SetTheme(ThemeChoice::System);
        s.SetRecentCount(8);
        s.SetReopenFiles(false);
    });
    return parts.page;
}

// ===== SAVING =====

std::shared_ptr<UltraCanvasContainer> BuildSaving(DialogState* d) {
    PageParts parts = MakePage("office-set-saving", Tr("Saving"), Tr("How files are saved and kept:"));
    d->format = MakeList("office-set-format", {Tr("Office Open XML (.docx, .xlsx, .pptx)"), Tr("OpenDocument (.odt, .ods, .odp)")},
                         [d](int index) {
                             if (d->syncing) return;
                             d->Settings().SetSaveFormat(index == 1 ? SaveFormat::OpenDocument : SaveFormat::OfficeOpenXml);
                             Changed(d);
                         });
    // The two formats' names with their extensions: a wider list, so neither is cut.
    d->format->size.width = CSSLayout::Dimension::Px(kWideControlWidth);
    auto formatRow = MakeLabelledRow("office-set-format-row", Tr("New files are saved as:"), d->format, kCaptionWidth);
    formatRow->size.width = CSSLayout::Dimension::Px(kCaptionWidth + 10 + kWideControlWidth);
    parts.body->AddChild(formatRow);

    d->autosave = Box(d, "office-set-autosave", Tr("Autosave documents into their own files"),
                      [](OfficeSettings& s, bool on) { s.SetAutosave(on); });
    parts.body->AddChild(d->autosave);
    d->recovery = Box(d, "office-set-recovery", Tr("Keep recovery copies of unsaved changes"),
                      [](OfficeSettings& s, bool on) { s.SetRecoveryCopies(on); });
    parts.body->AddChild(d->recovery);
    std::vector<std::string> minutes;
    for (int m : kMinutes) minutes.push_back(TrF("{0} min", m));
    d->minutes = MakeList("office-set-minutes", minutes, [d](int index) {
        if (d->syncing || index >= static_cast<int>(kMinutes.size())) return;
        d->Settings().SetAutosaveMinutes(kMinutes[static_cast<size_t>(index)]);
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-minutes-row", Tr("How often:"), d->minutes, kCaptionWidth));

    AddNote(parts, "office-set-saving-note1",
            Tr("Every change applies at once. Autosave writes only documents that already have a file; recovery "
               "copies are offered back on the Start tab after a crash."));
    AddReset(d, kSettingsSaving, [](OfficeSettings& s) {
        s.SetSaveFormat(SaveFormat::OfficeOpenXml);
        s.SetAutosave(false);
        s.SetRecoveryCopies(true);
        s.SetAutosaveMinutes(1);
    });
    return parts.page;
}

// ===== DOCUMENTS =====

std::shared_ptr<UltraCanvasContainer> BuildDocuments(DialogState* d) {
    PageParts parts = MakePage("office-set-documents", Tr("Documents"),
                               Tr("New documents, and how documents are shown:"));
    std::vector<std::string> fonts = OfficeSettings::FontChoices();
    fonts.front() = Tr("Default font");
    d->font = MakeList("office-set-font", fonts, [d](int index) {
        const auto choices = OfficeSettings::FontChoices();
        if (d->syncing || index >= static_cast<int>(choices.size())) return;
        d->Settings().SetDocumentFont(choices[static_cast<size_t>(index)]);
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-font-row", Tr("Font:"), d->font, kCaptionWidth));
    std::vector<std::string> sizes;
    for (float s : OfficeSettings::FontSizeChoices()) sizes.push_back(SizeText(s));
    d->size = MakeList("office-set-size", sizes, [d](int index) {
        const auto choices = OfficeSettings::FontSizeChoices();
        if (d->syncing || index >= static_cast<int>(choices.size())) return;
        d->Settings().SetDocumentFontSize(choices[static_cast<size_t>(index)]);
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-size-row", Tr("Font size (points):"), d->size, kCaptionWidth));
    d->page = MakeList("office-set-page", {Tr("A4 (21 x 29.7 cm)"), Tr("Letter (8.5 x 11 in)")}, [d](int index) {
        if (d->syncing) return;
        d->Settings().SetPageSize(index == 1 ? PageSizeChoice::Letter : PageSizeChoice::A4);
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-page-row", Tr("Page:"), d->page, kCaptionWidth));

    AddBodyCaption(parts, "office-set-documents-view", Tr("Showing documents:"));
    d->navigator = Box(d, "office-set-navigator", Tr("Show the navigator beside the document (F5)"),
                       [](OfficeSettings& s, bool on) { s.SetShowNavigator(on); });
    parts.body->AddChild(d->navigator);
    d->fit = MakeList("office-set-fit", {Tr("Page width"), Tr("Whole page"), Tr("Text width"), Tr("100 %")}, [d](int index) {
        if (d->syncing) return;
        static const DocumentFit fits[] = {DocumentFit::PageWidth, DocumentFit::WholePage, DocumentFit::WidthShare,
                                           DocumentFit::Actual};
        d->Settings().SetDocumentFit(fits[std::clamp(index, 0, 3)]);
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-fit-row", Tr("Zoom:"), d->fit, kCaptionWidth));
    std::vector<std::string> shares;
    for (float share : OfficeSettings::WidthShareChoices()) shares.push_back(PercentText(static_cast<int>(std::lround(share * 100.0f))));
    d->share = MakeList("office-set-share", shares, [d](int index) {
        const auto choices = OfficeSettings::WidthShareChoices();
        if (d->syncing || index >= static_cast<int>(choices.size())) return;
        d->Settings().SetWidthShare(choices[static_cast<size_t>(index)]);
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-share-row", Tr("Text width fills:"), d->share, kCaptionWidth));

    AddNote(parts, "office-set-documents-note1",
            Tr("Font, size and page apply to new documents."));
    AddNote(parts, "office-set-documents-note2",
            Tr("The navigator and the zoom apply to open documents at once. Text width makes the page that share "
               "of the window's width."));
    AddReset(d, kSettingsDocuments, [](OfficeSettings& s) {
        s.SetDocumentFont("");
        s.SetDocumentFontSize(12.0f);
        s.SetPageSize(PageSizeChoice::A4);
        s.SetShowNavigator(false);
        s.SetDocumentFit(DocumentFit::PageWidth);
        s.SetWidthShare(0.6f);
    });
    return parts.page;
}

// ===== SPREADSHEETS =====

std::shared_ptr<UltraCanvasContainer> BuildSpreadsheets(DialogState* d) {
    PageParts parts = MakePage("office-set-spreadsheets", Tr("Spreadsheets"), Tr("How spreadsheets are shown:"));
    d->gridlines = Box(d, "office-set-gridlines", Tr("Show the gridlines between the cells"),
                       [](OfficeSettings& s, bool on) { s.SetShowGridlines(on); });
    parts.body->AddChild(d->gridlines);
    AddNote(parts, "office-set-spreadsheets-note",
            Tr("Applies at once to every open spreadsheet. Whether gridlines are printed is set on the Page tab."));
    AddReset(d, kSettingsSpreadsheets, [](OfficeSettings& s) { s.SetShowGridlines(true); });
    return parts.page;
}

// ===== PRESENTATIONS =====

std::shared_ptr<UltraCanvasContainer> BuildPresentations(DialogState* d) {
    PageParts parts = MakePage("office-set-presentations", Tr("Presentations"),
                               Tr("New presentations, and where the show runs:"));
    d->slideSize = MakeList("office-set-slide-size", {Tr("Widescreen (16:9)"), Tr("Standard (4:3)")}, [d](int index) {
        if (d->syncing) return;
        d->Settings().SetSlideSize(index == 1 ? SlideSizeChoice::Standard : SlideSizeChoice::Widescreen);
        Changed(d);
    });
    parts.body->AddChild(MakeLabelledRow("office-set-slide-size-row", Tr("Slide size:"), d->slideSize, kCaptionWidth));
    d->secondScreen = Box(d, "office-set-second-screen", Tr("Start the show on the second monitor when there is one"),
                          [](OfficeSettings& s, bool on) { s.SetShowOnSecondScreen(on); });
    parts.body->AddChild(d->secondScreen);
    d->presenter = Box(d, "office-set-presenter", Tr("Show the presenter view on the other monitor"),
                       [](OfficeSettings& s, bool on) { s.SetPresenterView(on); });
    parts.body->AddChild(d->presenter);
    AddNote(parts, "office-set-presentations-note",
            Tr("The slide size applies to new presentations, the monitors to the next show."));
    AddReset(d, kSettingsPresentations, [](OfficeSettings& s) {
        s.SetSlideSize(SlideSizeChoice::Widescreen);
        s.SetShowOnSecondScreen(true);
        s.SetPresenterView(true);
    });
    return parts.page;
}

void Build(DialogState* d, UltraOfficeWindow& owner) {
    d->owner = &owner;
    SettingsWindowOptions options;
    options.width = 760;
    options.height = 600;
    options.treeWidth = 170;
    options.closeLabel = Tr("Close");
    if (!d->Create(&owner, Tr("UltraOffice - Settings"), options)) return;
    d->restoreButton->SetText(Tr("Restore defaults"));
    d->AddSection(kSettingsGeneral, Tr("General"));
    d->AddPage(kSettingsGeneral, BuildGeneral(d));
    d->AddSection(kSettingsSaving, Tr("Saving"));
    d->AddPage(kSettingsSaving, BuildSaving(d));
    d->AddSection(kSettingsDocuments, Tr("Documents"));
    d->AddPage(kSettingsDocuments, BuildDocuments(d));
    d->AddSection(kSettingsSpreadsheets, Tr("Spreadsheets"));
    d->AddPage(kSettingsSpreadsheets, BuildSpreadsheets(d));
    d->AddSection(kSettingsPresentations, Tr("Presentations"));
    d->AddPage(kSettingsPresentations, BuildPresentations(d));
    Refresh(d);
    // What was typed last into the name applies once the window goes.
    d->onClosed = [d]() { Changed(d); };
}

} // namespace

void ShowSettingsDialog(UltraOfficeWindow& window, const std::string& page) {
    const std::string start = page.empty() ? std::string(kSettingsGeneral) : page;
    if (g_dialog && g_dialog->IsOpen() && g_dialog->owner == &window) {
        Refresh(g_dialog.get());
        g_dialog->SelectPage(start);
        g_dialog->window->Show();
        g_dialog->window->RaiseAndFocus();
        return;
    }
    CloseSettingsDialog();
    auto d = std::make_shared<DialogState>();
    g_dialog = d;
    Build(d.get(), window);
    if (!d->window) { g_dialog.reset(); return; }
    d->Finish(start);
    d->SelectPage(start);
}

SettingsPageKit::UltraCanvasSettingsWindow* GetSettingsDialog() {
    return g_dialog && g_dialog->IsOpen() ? g_dialog.get() : nullptr;
}

void CloseSettingsDialog() {
    if (!g_dialog) return;
    std::shared_ptr<DialogState> d = std::move(g_dialog);
    if (d->IsOpen()) {
        d->onClosed = nullptr;   // the owner may be going
        d->window->onWindowClosed = nullptr;
        d->window->Close();
        d->closed = true;
    }
}

} // namespace UltraOffice
