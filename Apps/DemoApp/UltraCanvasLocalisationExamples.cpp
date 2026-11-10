// Apps/DemoApp/UltraCanvasLocalisationExamples.cpp
// The UltraLocalisation module: the four calls a program makes at the site
// (Tr, TrC, TrN, TrF), live in the language picked on the page, with which
// of the three layers answered each one; the catalogue files of the three
// layers for that language, shown as they ship; and the scripts around
// them. The language picked here is the demo's own language (SetLanguage,
// as UOS-Settings' Language page would set it) until it is picked again:
// the demo's pages are built in English, so what changes is what Tr gives
// from then on.
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

// The application whose files the page shows: one with a set of its own
// and a base set, so the three layers are all there in English.
constexpr const char* kExampleApplication = "UltraTexter";
constexpr const char* kExampleBase = "Text";

std::string ShippedLocaleFolder() {
    std::string folder = UltraLocalisation::ShippedFolder();
    if (folder.empty()) folder = NormalizePath(GetResourcesDir() + "media/locale");
    while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\')) folder.pop_back();
    return folder;
}

// Which layer answered, as the caption under a result says it. English
// loads no set: the text at the site is the answer.
std::string OriginText(LocalisationOrigin origin, const std::string& language) {
    switch (origin) {
        case LocalisationOrigin::Application: return "from the application's own set";
        case LocalisationOrigin::Library:     return "from a library's set";
        case LocalisationOrigin::Base:        return "from a base set";
        case LocalisationOrigin::Standard:    return "from the standard set";
        case LocalisationOrigin::Source:
            return language == "en" ? "the English written at the site: English needs no catalogue"
                                    : "the English written at the site: no set of " + language + " has it";
    }
    return "";
}

// The page's state the controls write and the labels show.
struct LocalisationDemoState {
    long long count = 3;
    std::string argument = "Report.docx";
    std::vector<std::string> languages;      // the tags in the picker's order
    std::vector<std::string> filePaths;      // the files in the file picker's order, relative to the locale folder
    int fileIndex = 0;
};

}  // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLocalisationExamples() {
    auto page = DemoKit::CreatePage("LocalisationPage", Tr("Localisation"),
        Tr("The text of every application in the person's language: Tr(\"Save\") gives the "
        "translation of the English written at the site from three layers of catalogue files - "
        "the application's own set, the base sets of its kind and the standard set - and the "
        "English itself when none has it. Pick a language: every example below is answered "
        "again, with the layer that answered it."));

    auto state = std::make_shared<LocalisationDemoState>();
    auto* app = UltraCanvasApplication::GetInstance();
    const std::string mono = app ? app->GetDefaultMonospacedFontStyle().fontFamily : std::string("monospace");

    // A line of code: the call as it is written.
    auto code = [&](const std::shared_ptr<UltraCanvasContainer>& parent, const std::string& id, const std::string& text) {
        auto label = DemoKit::AddText(parent, id, text);
        label->SetFont(mono, 12.0f);
        label->SetTextColor(Color(80, 60, 140, 255));
        return label;
    };
    // What the call gives, large, and under it which layer answered.
    auto result = [&](const std::shared_ptr<UltraCanvasContainer>& parent, const std::string& id) {
        auto label = DemoKit::AddText(parent, id, "");
        label->SetFontSize(15.0f);
        label->SetTextColor(DemoKit::TitleText());
        return label;
    };

    // ===== THE LANGUAGE =====
    auto languageCard = DemoKit::AddCard(page, "LocalisationLanguageCard", Tr("The language"),
        Tr("Every language a catalogue ships for (media/locale/<lang>/), named as it names itself. "
        "The pick calls UltraLocalisation::SetLanguage, which loads the sets for the demo and "
        "stays until it is picked again - what UOS-Settings' Language page does for every application."));
    auto languageRow = DemoKit::AddRow(languageCard, "LocalisationLanguageRow", 10.0f);
    DemoKit::AddText(languageRow, "LocalisationLanguageCaption", Tr("Language"));
    auto languagePicker = std::make_shared<UltraCanvasDropdown>("LocalisationLanguagePicker", 0, 0, 260, 26);
    DemoKit::AddFixed(languageRow, languagePicker);
    auto loadedText = DemoKit::AddCaption(languageCard, "LocalisationLoaded", "");

    // ===== THE CALLS =====
    auto callsRow = DemoKit::AddRow(page, "LocalisationCallsRow");

    auto trCard = DemoKit::AddCard(callsRow, "LocalisationTrCard", Tr("Tr - a text"),
        Tr("The English written at the site is the key; the standard set has the words every application uses."));
    code(trCard, "LocalisationTrCode", "Tr(\"Save\")");
    auto trResult = result(trCard, "LocalisationTrResult");
    auto trOrigin = DemoKit::AddCaption(trCard, "LocalisationTrOrigin", "");
    code(trCard, "LocalisationTrCode2", "Tr(\"Fade out the audio\")");
    auto trResult2 = result(trCard, "LocalisationTrResult2");
    auto trOrigin2 = DemoKit::AddCaption(trCard, "LocalisationTrOrigin2", "");

    auto trcCard = DemoKit::AddCard(callsRow, "LocalisationTrCCard", Tr("TrC - a text in a context"),
        Tr("The same English can need two translations: a short month name is not the word, a button's "
        "\"Open\" is not the adjective. The context goes in front of the key: \"month-short|Apr\"."));
    code(trcCard, "LocalisationTrCCode", "TrC(\"month-short\", \"Apr\")");
    auto trcResult = result(trcCard, "LocalisationTrCResult");
    auto trcOrigin = DemoKit::AddCaption(trcCard, "LocalisationTrCOrigin", "");

    auto trnCard = DemoKit::AddCard(callsRow, "LocalisationTrNCard", Tr("TrN - a count"),
        Tr("The singular and the plural are written at the site; the language's CLDR rule picks the "
        "form - one, few, many, other - from the catalogue's forms, {n} is the count. Try 1, 2, 5, 21 "
        "in Russian, Polish or Arabic."));
    code(trnCard, "LocalisationTrNCode", "TrN(\"{n} file\", \"{n} files\", n)");
    auto trnRow = DemoKit::AddRow(trnCard, "LocalisationTrNRow", 10.0f);
    DemoKit::AddText(trnRow, "LocalisationTrNCaption", Tr("n ="));
    auto countSpinner = CreateIntSpinner("LocalisationTrNCount", 0, 0, 110, 26, 0, 1000, static_cast<int>(state->count), 1);
    DemoKit::AddFixed(trnRow, countSpinner);
    auto trnResult = result(trnCard, "LocalisationTrNResult");
    auto trnOrigin = DemoKit::AddCaption(trnCard, "LocalisationTrNOrigin", "");

    auto trfCard = DemoKit::AddCard(callsRow, "LocalisationTrFCard", Tr("TrF - a text with values"),
        Tr("{0}, {1}, ... are filled after the translation, so a language can put them in its own order "
        "or leave one out. The argument is any text or number."));
    code(trfCard, "LocalisationTrFCode", "TrF(\"\\\"{0}\\\" is write-protected.\", name)");
    auto trfRow = DemoKit::AddRow(trfCard, "LocalisationTrFRow", 10.0f);
    DemoKit::AddText(trfRow, "LocalisationTrFCaption", Tr("name ="));
    auto argumentField = std::make_shared<UltraCanvasTextInput>("LocalisationTrFArgument", 0, 0, 180, 26);
    argumentField->SetText(state->argument);
    DemoKit::AddFixed(trfRow, argumentField);
    auto trfResult = result(trfCard, "LocalisationTrFResult");
    auto trfOrigin = DemoKit::AddCaption(trfCard, "LocalisationTrFOrigin", "");

    // ===== THE FILES =====
    auto filesCard = DemoKit::AddCard(page, "LocalisationFilesCard", Tr("The files"),
        std::string("The three layers of ") + kExampleApplication + " in the language picked above, looked up in "
        "this order: its own set, the base set it takes (\"@bases\" in its English file), the standard "
        "set; a file that is not there is skipped. A file of the same name under the UltraCanvas "
        "settings folder's locale/ is read before the shipped one, which is what the localisation "
        "editor and a translator's override write.");
    auto fileRow = DemoKit::AddRow(filesCard, "LocalisationFileRow", 10.0f);
    DemoKit::AddText(fileRow, "LocalisationFileCaption", Tr("File"));
    auto filePicker = std::make_shared<UltraCanvasDropdown>("LocalisationFilePicker", 0, 0, 360, 26);
    DemoKit::AddFixed(fileRow, filePicker);
    auto fileInfo = DemoKit::AddCaption(filesCard, "LocalisationFileInfo", "");
    auto fileText = std::make_shared<UltraCanvasTextArea>("LocalisationFileText");
    fileText->SetReadOnly(true);
    fileText->SetWordWrap(false);
    fileText->SetHeight(320);
    DemoKit::AddStretched(filesCard, fileText);

    DemoKit::AddNotes(page, "LocalisationNotes", Tr("Around the module"), {
        Tr("scripts/extract_ui_strings.py rewrites the English catalogues from the Tr sites in the sources; "
        "scripts/wrap_ui_strings.py wraps a file's interface strings in Tr for review; "
        "scripts/check_locale_catalogues.py is the CI gate over every translation."),
        Tr("scripts/translate_catalogue.py drafts a language's sets through UltraAI (ultralocalisation-draft); "
        "a draft is marked \"@translated-by\" until a person reviews it."),
        Tr("The localisation editor (Widgets > Localisation Editor, UOS-Settings > Language > Translations, "
        "Help > Translate ... in an application) edits the sets of an application in one grid."),
        Tr("Docs/Modules/UltraLocalisation/README.md is the module's documentation, "
        "Docs/UltraCanvas/UltraCanvasLocalisationProposal.md its design."),
    });

    // ----- the files of the picked language -----
    auto fillFiles = [state, filePicker](const std::string& language) {
        state->filePaths.clear();
        filePicker->ClearItems();
        auto add = [&](const std::string& relative, const std::string& layer) {
            state->filePaths.push_back(relative);
            filePicker->AddItem(relative + "   -   " + layer, relative);
        };
        add(language + "/" + kExampleApplication + ".json", "the application's own set");
        add(language + "/base/" + std::string(kExampleBase) + ".json", "the base set");
        add(language + "/Standard.json", "the standard set");
        if (language != "en") {
            add(std::string("en/") + kExampleApplication + ".json", "the English set: the keys, @bases, @inherited");
        }
        if (state->fileIndex >= static_cast<int>(state->filePaths.size())) state->fileIndex = 0;
        filePicker->SetSelectedIndex(state->fileIndex);
    };
    auto showFile = [state, fileInfo, fileText]() {
        if (state->fileIndex < 0 || state->fileIndex >= static_cast<int>(state->filePaths.size())) return;
        const std::string relative = state->filePaths[static_cast<size_t>(state->fileIndex)];
        const std::string path = ShippedLocaleFolder() + "/" + relative;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(PathFromUtf8(path), ec)) {
            fileInfo->SetText("media/locale/" + relative + " is not there: the lookup goes on to the next layer.");
            fileText->SetText("");
            return;
        }
        LocalisationCatalogue catalogue;
        std::string error;
        std::string info = "media/locale/" + relative;
        if (UltraLocalisation::ReadCatalogue(path, catalogue, error)) {
            info += "   -   " + std::to_string(catalogue.entries.size()) + " entries, "
                  + std::to_string(catalogue.plurals.size()) + " plurals";
            if (!catalogue.bases.empty()) {
                info += ", @bases:";
                for (const std::string& base : catalogue.bases) info += " " + base;
            }
            if (!catalogue.inherited.empty()) info += ", " + std::to_string(catalogue.inherited.size()) + " inherited";
            if (!catalogue.translatedBy.empty()) info += ", @translated-by: " + catalogue.translatedBy;
        } else if (!error.empty()) {
            info += "   -   " + error;
        }
        fileInfo->SetText(info);
        fileText->SetText(LoadFile(path));
    };

    // ----- the calls, answered again -----
    auto refresh = [state, trResult, trOrigin, trResult2, trOrigin2, trcResult, trcOrigin,
                    trnResult, trnOrigin, trfResult, trfOrigin, loadedText]() {
        const std::string language = UltraLocalisation::CurrentLanguage();
        auto origin = [&language](std::string_view key) { return OriginText(UltraLocalisation::OriginOf(key), language); };

        trResult->SetText(Tr("Save"));
        trOrigin->SetText(origin("Save"));
        trResult2->SetText(Tr("Fade out the audio"));
        trOrigin2->SetText(origin("Fade out the audio"));

        trcResult->SetText(TrC("month-short", "Apr"));
        trcOrigin->SetText(origin("month-short|Apr"));

        trnResult->SetText(TrN("{n} file", "{n} files", state->count));
        trnOrigin->SetText(origin("{n} file") + "   -   CLDR category for " + std::to_string(state->count)
                           + " in " + language + ": " + UltraLocalisation::PluralCategory(language, state->count));

        trfResult->SetText(TrF("\"{0}\" is write-protected.", state->argument));
        trfOrigin->SetText(origin("\"{0}\" is write-protected."));

        std::string loaded = "Loaded for " + UltraLocalisation::CurrentApplication() + " in " + language + ": "
            + std::to_string(UltraLocalisation::EntryCount(LocalisationOrigin::Application)) + " of its own, "
            + std::to_string(UltraLocalisation::EntryCount(LocalisationOrigin::Base)) + " from its base sets, "
            + std::to_string(UltraLocalisation::EntryCount(LocalisationOrigin::Standard)) + " standard entries.";
        loadedText->SetText(loaded);
    };

    // ----- the pickers -----
    state->languages = UltraLocalisation::AvailableLanguages();
    const std::string current = UltraLocalisation::CurrentLanguage();
    int currentIndex = 0;
    for (size_t i = 0; i < state->languages.size(); i++) {
        const std::string& tag = state->languages[i];
        languagePicker->AddItem(UltraLocalisation::NativeName(tag) + "  (" + tag + ")", tag);
        if (tag == current) currentIndex = static_cast<int>(i);
    }
    languagePicker->SetSelectedIndex(currentIndex);
    languagePicker->onSelectionChanged = [state, fillFiles, showFile, refresh](int index, const DropdownItem&) {
        if (index < 0 || index >= static_cast<int>(state->languages.size())) return;
        const std::string& tag = state->languages[static_cast<size_t>(index)];
        UltraLocalisation::SetLanguage(tag);
        refresh();
        fillFiles(tag);
        showFile();
    };
    filePicker->onSelectionChanged = [state, showFile](int index, const DropdownItem&) {
        if (index < 0) return;
        state->fileIndex = index;
        showFile();
    };
    countSpinner->onValueChanged = [state, refresh](double value) {
        state->count = static_cast<long long>(value);
        refresh();
    };
    argumentField->onTextChanged = [state, refresh](const std::string& text) {
        state->argument = text;
        refresh();
    };

    refresh();
    fillFiles(current);
    showFile();
    return page;
}

} // namespace UltraCanvas
