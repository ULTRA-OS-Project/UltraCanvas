// Tests/LocalisationEditorTest.cpp
// The localisation editor (UltraCanvasLocalisationEditor.h) on a locale tree
// of its own under the temp folder: an application "Demo" that takes the
// base set Video, a draft German standard set, a second base set Audio
// whose "Trim" differs from Video's.
//
// What the test pins:
// - The workspace lists the applications, the base sets and the languages
//   from the folders, and opens the application's sets in lookup order.
// - The rows: the application's own keys and the inherited ones, each with
//   the set that answers and its status (standard, base, override,
//   untranslated, plural, draft); the whole shared sets on request; the
//   comparison language's text.
// - Editing: a text written into the application's set is an override, an
//   empty text clears it, Undo takes it back, a change is dirty.
// - Merging: taking a base set shows the collision between two bases;
//   promoting a row moves it into the base in every language and lists it
//   as inherited; demoting is the reverse; merging two bases names the
//   collisions and renames the application's bases.
// - Saving: into the settings folder, only what differs from the shipped
//   file; into the source tree, the whole file in the extraction's format
//   with the translator's name; the default target is the writable tree.
// - Export: the merged set as JSON and as PO.
// - The element: its grid shows the rows with a column per set, the edit
//   row writes the selected row, the filters and the search narrow the rows,
//   a drafter fills the untranslated rows and marks them.
//
// Headless: no window.
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#include "UltraCanvasLocalisationEditor.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

void Put(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string Slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const LocalisationRow* Find(const std::vector<LocalisationRow>& rows, const std::string& key) {
    for (const auto& r : rows) if (r.key == key) return &r;
    return nullptr;
}

std::string Cell(TreeNode* node, const std::string& column) {
    if (!node) return "<no node>";
    const TreeCellData* c = node->data.GetCell(column);
    return c ? c->text : std::string();
}

struct Tree {
    std::filesystem::path root, shipped, settings;
    Tree() {
        root = std::filesystem::temp_directory_path() / ("uc-localisation-editor-test-" + std::to_string(getpid()));
        std::filesystem::remove_all(root);
        shipped = root / "shipped";
        settings = root / "settings";
        std::filesystem::create_directories(settings);
        Put(shipped / "en" / "Standard.json",
            "{\n  \"@language\": \"en\",\n  \"@name\": \"English\",\n  \"Delete\": \"Delete\",\n  \"Open\": \"Open\",\n"
            "  \"Save\": \"Save\",\n  \"{n} file\": {\n    \"one\": \"{n} file\",\n    \"other\": \"{n} files\"\n  }\n}\n");
        Put(shipped / "en" / "base" / "Video.json",
            "{\n  \"@language\": \"en\",\n  \"Fade out\": \"Fade out\",\n  \"Trim\": \"Trim\"\n}\n");
        Put(shipped / "en" / "base" / "Audio.json",
            "{\n  \"@language\": \"en\",\n  \"Fade out\": \"Fade out\",\n  \"Mute\": \"Mute\",\n  \"Trim\": \"Trim\"\n}\n");
        Put(shipped / "en" / "Demo.json",
            "{\n  \"@language\": \"en\",\n  \"@bases\": [\n    \"Video\"\n  ],\n  \"@inherited\": [\n    \"Delete\",\n    \"Fade out\",\n"
            "    \"Save\",\n    \"Trim\",\n    \"{n} file\"\n  ],\n  \"Fade out the audio\": \"Fade out the audio\",\n  \"Ripple delete\": \"Ripple delete\"\n}\n");
        Put(shipped / "de" / "Standard.json",
            "{\n  \"@language\": \"de\",\n  \"@name\": \"Deutsch\",\n  \"@translated-by\": \"machine draft 2026-11-02, reviewed by -\",\n"
            "  \"Delete\": \"Löschen\",\n  \"Open\": \"Öffnen\",\n  \"Save\": \"Speichern\",\n"
            "  \"{n} file\": {\n    \"one\": \"{n} Datei\",\n    \"other\": \"{n} Dateien\"\n  }\n}\n");
        Put(shipped / "de" / "base" / "Video.json",
            "{\n  \"@language\": \"de\",\n  \"Fade out\": \"Ausblenden\",\n  \"Trim\": \"Trimmen\"\n}\n");
        Put(shipped / "de" / "base" / "Audio.json",
            "{\n  \"@language\": \"de\",\n  \"Fade out\": \"Ausblenden\",\n  \"Mute\": \"Stumm\",\n  \"Trim\": \"Beschneiden\"\n}\n");
        Put(shipped / "de" / "Demo.json",
            "{\n  \"@language\": \"de\",\n  \"@bases\": [\n    \"Video\"\n  ],\n  \"Delete\": \"Aus dem Album entfernen\"\n}\n");
    }
    ~Tree() { std::error_code ec; std::filesystem::remove_all(root, ec); }
    std::string Shipped() const { return PathToUtf8(shipped); }
    std::string Settings() const { return PathToUtf8(settings); }
};

}  // namespace

int main() {
    std::printf("LocalisationEditorTest\n");

    // ===== the workspace: what there is =====
    {
        Tree t;
        LocalisationWorkspace w;
        w.SetFolders(t.Shipped(), t.Settings());
        Check(w.Applications() == std::vector<std::string>{"Demo"}, "the applications: the English sets that are not Standard");
        Check(w.BaseSetNames() == std::vector<std::string>{"Audio", "Video"}, "the base set names");
        Check(w.Languages() == std::vector<std::string>{"en", "de"}, "the languages with a standard set, en first");

        Check(w.Open("Demo", "de"), "Demo opens in German");
        Check(w.Bases() == std::vector<std::string>{"Video"}, "its bases from its catalogue");
        const auto sets = w.Sets();
        Check(sets.size() == 3 && sets[0].isApplication && sets[1].isBase && sets[1].name == "Video" && sets[2].isStandard,
              "the sets in lookup order: the application, its base, Standard");

        // ===== the rows =====
        const auto rows = w.Rows();
        Check(rows.size() == 7, "seven rows: two of its own, five inherited (" + std::to_string(rows.size()) + ")");
        const LocalisationRow* save = Find(rows, "Save");
        Check(save && save->status == LocalisationRowStatus::Standard && save->shown == "Speichern" && save->shownFrom == "Standard",
              "Save comes from the standard set");
        Check(save && save->draft, "... which is a machine draft");
        Check(save && save->compare == "Save", "... compared with English by default");
        const LocalisationRow* del = Find(rows, "Delete");
        Check(del && del->status == LocalisationRowStatus::Override && del->shown == "Aus dem Album entfernen",
              "Delete is the application's override of the standard word");
        const LocalisationRow* trim = Find(rows, "Trim");
        Check(trim && trim->status == LocalisationRowStatus::Base && trim->shownFrom == "Video" && trim->shown == "Trimmen",
              "Trim comes from the Video base set");
        const LocalisationRow* fade = Find(rows, "Fade out the audio");
        Check(fade && fade->status == LocalisationRowStatus::Untranslated && fade->shown == "Fade out the audio" && fade->inApplication,
              "the application's own untranslated key shows its English");
        const LocalisationRow* files = Find(rows, "{n} file");
        Check(files && files->plural && files->categories == std::vector<std::string>{"one", "other"}
              && files->forms.count("Standard") && files->forms.at("Standard").at("other") == "{n} Dateien",
              "a plural row carries the language's forms");
        Check(!Find(rows, "Open"), "a standard entry the application does not use is not a row");
        const auto whole = w.Rows(true);
        const LocalisationRow* open = Find(whole, "Open");
        Check(whole.size() == 8 && open && open->unusedSharedEntry, "with the whole sets, the unused standard entry is a row");

        // ===== editing =====
        Check(!w.Dirty(), "nothing dirty after opening");
        Check(w.SetText("Demo", "Fade out the audio", "Ton ausblenden"), "a text written into the application's set");
        const LocalisationRow* fade2 = Find(w.Rows(), "Fade out the audio");
        Check(fade2 && fade2->status == LocalisationRowStatus::Application && fade2->shown == "Ton ausblenden" && fade2->changed,
              "... the row is the application's own now, and changed");
        Check(w.Dirty() && w.DirtyFiles() == std::vector<std::string>{t.Shipped() + "/de/Demo.json"},
              "... the German application file is dirty, nothing else");
        Check(w.CanUndo() && w.Undo(), "Undo");
        const LocalisationRow* fade3 = Find(w.Rows(), "Fade out the audio");
        Check(fade3 && fade3->status == LocalisationRowStatus::Untranslated && !w.Dirty(), "... takes the text back, nothing dirty");
        Check(w.Clear("Demo", "Delete"), "the override of Delete cleared");
        const LocalisationRow* del2 = Find(w.Rows(), "Delete");
        Check(del2 && del2->status == LocalisationRowStatus::Standard && del2->shown == "Löschen", "... the row is inherited again");
        Check(w.SetText("Demo", "Save", "Speichern"), "an override with the inherited text");
        const LocalisationRow* save2 = Find(w.Rows(), "Save");
        Check(save2 && save2->status == LocalisationRowStatus::Duplicate, "... is marked a duplicate to drop");
        w.Undo();
        w.Undo();
        Check(!w.Dirty(), "two undos: back to the opened state");

        // ===== merging =====
        Check(w.TakeBaseSet("Audio"), "the application takes Audio");
        Check(w.Bases() == std::vector<std::string>{"Video", "Audio"}, "... after Video");
        const LocalisationRow* trim2 = Find(w.Rows(), "Trim");
        Check(trim2 && trim2->status == LocalisationRowStatus::Collision && trim2->shownFrom == "Video" && trim2->collidesWith == "Audio",
              "... Trim is a collision: Video's text shown, Audio differs");
        const LocalisationRow* fadeOut = Find(w.Rows(), "Fade out");
        Check(fadeOut && fadeOut->status == LocalisationRowStatus::Base, "... Fade out, the same in both, is no collision");
        Check(w.DropBaseSet("Audio") && w.Bases() == std::vector<std::string>{"Video"}, "Audio dropped again");
        Check(!w.TakeBaseSet("Nowhere"), "a base set that does not exist cannot be taken");

        Check(w.PromoteToBase("Ripple delete", "Video"), "Ripple delete promoted into Video");
        Check(w.EnglishText("Ripple delete") == "Ripple delete", "... its English is still found (now in Video)");
        Check(w.DemoteToApplication("Trim", "Video"), "Trim demoted into the application");
        const LocalisationRow* trim3 = Find(w.Rows(), "Trim");
        Check(trim3 && trim3->status == LocalisationRowStatus::Application && trim3->shown == "Trimmen",
              "... the German text is the application's own now");
        Check(!w.Has("Video", "Trim"), "... and Video has it no more");
        w.Undo();
        w.Undo();

        const std::vector<std::string> collisions = w.MergeBaseSets("Video", "Audio", "Media");
        Check(collisions == std::vector<std::string>{"Trim"}, "Video and Audio merged into Media: Trim collides");
        Check(w.Bases() == std::vector<std::string>{"Media"}, "... the application's base is Media now");
        const LocalisationRow* trim4 = Find(w.Rows(), "Trim");
        Check(trim4 && trim4->shownFrom == "Media" && trim4->shown == "Trimmen", "... Video's text won");
        Check(std::find(w.BaseSetNames().begin(), w.BaseSetNames().end(), "Media") != w.BaseSetNames().end(), "... Media is a base set name");
        w.Undo();
        Check(w.Bases() == std::vector<std::string>{"Video"} && !w.Dirty(), "... undone");

        // ===== saving into the settings folder =====
        w.SetTranslatorName("Ada");
        w.SetText("Demo", "Fade out the audio", "Ton ausblenden");
        std::string report;
        Check(w.Save(LocalisationSaveTarget::SettingsFolder, report), "saved into the settings folder: " + report);
        const std::string overridePath = t.Settings() + "/locale/de/Demo.json";
        LocalisationCatalogue over;
        std::string error;
        Check(UltraLocalisation::ReadCatalogue(overridePath, over, error) && over.entries.size() == 1
              && over.entries.count("Fade out the audio") && over.translatedBy == "Ada",
              "... the override holds the one entry that differs, with the translator");
        Check(!w.Dirty(), "... nothing dirty after the save");
        LocalisationWorkspace again;
        again.SetFolders(t.Shipped(), t.Settings());
        again.Open("Demo", "de");
        const LocalisationRow* fade4 = Find(again.Rows(), "Fade out the audio");
        Check(fade4 && fade4->status == LocalisationRowStatus::Application && fade4->shown == "Ton ausblenden",
              "... a fresh workspace reads the override over the shipped file");
        Check(again.Clear("Demo", "Fade out the audio") && again.Save(LocalisationSaveTarget::SettingsFolder, report)
              && !std::filesystem::exists(PathFromUtf8(overridePath)),
              "... clearing the entry and saving removes the override file");

        // ===== saving into the source tree =====
        Check(w.DefaultSaveTarget() == LocalisationSaveTarget::SourceTree, "a writable shipped folder is the default target");
        LocalisationWorkspace none;
        none.SetFolders("", t.Settings());
        Check(none.DefaultSaveTarget() == LocalisationSaveTarget::SettingsFolder, "no shipped folder: the settings folder");
        w.SetText("Demo", "Ripple delete", "Ripple-Löschen");
        w.SetText("Standard", "Open", "Öffnen!");
        Check(w.Save(LocalisationSaveTarget::SourceTree, report), "saved into the source tree: " + report);
        const std::string demoDe = Slurp(t.shipped / "de" / "Demo.json");
        Check(demoDe.find("\"@translated-by\": \"Ada\"") != std::string::npos
              && demoDe.find("\"@bases\": [\n    \"Video\"\n  ]") != std::string::npos
              && demoDe.find("\"Ripple delete\": \"Ripple-Löschen\"") != std::string::npos,
              "... the application's file in the extraction's format, with the translator");
        const std::string standardDe = Slurp(t.shipped / "de" / "Standard.json");
        Check(standardDe.find("reviewed by Ada") != std::string::npos && standardDe.find("\"Open\": \"Öffnen!\"") != std::string::npos
              && standardDe.find("  \"{n} file\": {\n    \"one\": \"{n} Datei\",\n    \"other\": \"{n} Dateien\"\n  }") != std::string::npos,
              "... the standard set's draft mark now names the reviewer, plural forms one per line");
        const std::string demoEn = Slurp(t.shipped / "en" / "Demo.json");
        Check(demoEn.find("Ripple delete") != std::string::npos, "... the English file was not touched by a German edit");

        // ===== export =====
        const std::string po = PathToUtf8(t.root / "Demo.de.po");
        Check(w.ExportPo(po, error), "PO export: " + error);
        const std::string poText = Slurp(PathFromUtf8(po));
        Check(poText.find("msgid \"Save\"\nmsgstr \"Speichern\"") != std::string::npos
              && poText.find("msgid \"{n} file\"\nmsgid_plural \"{n} files\"\nmsgstr[0] \"{n} Datei\"\nmsgstr[1] \"{n} Dateien\"") != std::string::npos,
              "... entries and plural forms in gettext's form");
        const std::string json = PathToUtf8(t.root / "Demo.de.json");
        LocalisationCatalogue merged;
        Check(w.ExportJson(json, error) && UltraLocalisation::ReadCatalogue(json, merged, error)
              && merged.entries.size() + merged.plurals.size() == 7 && merged.entries.at("Trim") == "Trimmen",
              "JSON export: the merged set, every row with the set's text");
    }

    // ===== the element =====
    {
        Tree t;
        auto editor = std::make_shared<UltraCanvasLocalisationEditor>("editor", 0, 0, 1000, 600);
        editor->SetFolders(t.Shipped(), t.Settings());
        Check(editor->Open("", "de"), "opens the first application when none is named");
        Check(editor->GetWorkspace().Application() == "Demo" && editor->GetRows().size() == 7, "... Demo in German, seven rows");
        auto grid = editor->GetGrid();
        TreeNode* save = grid ? grid->FindNode("r:Save") : nullptr;
        Check(save && Cell(save, "set:Standard") == "Speichern" && Cell(save, "shown") == "Speichern"
              && Cell(save, "status").rfind("standard", 0) == 0 && Cell(save, "compare") == "Save",
              "the grid row of Save: the Standard column, Shown, Status, Compare");
        TreeNode* del = grid ? grid->FindNode("r:Delete") : nullptr;
        Check(del && Cell(del, "set:Demo") == "Aus dem Album entfernen" && Cell(del, "set:Standard") == "Löschen"
              && Cell(del, "status").rfind("override", 0) == 0, "the grid row of Delete: both columns, an override");
        TreeNode* files = grid ? grid->FindNode("r:{n} file#other") : nullptr;
        Check(files && Cell(files, "set:Standard") == "{n} Dateien", "a plural's forms are child rows");
        Check(grid && grid->FindNode("g:0") && grid->FindNode("g:1") && grid->FindNode("g:2"), "the three groups");

        Check(editor->SelectRow("Fade out the audio") && editor->GetSelectedKey() == "Fade out the audio", "a row selected by key");
        Check(editor->SetSelectedTranslation("Ton ausblenden"), "the edit row writes the application's text");
        const LocalisationRow* fade = Find(editor->GetRows(), "Fade out the audio");
        Check(fade && fade->status == LocalisationRowStatus::Application, "... the row shows it");
        Check(editor->GetStatusText().find("Fade out the audio") != std::string::npos, "... and the status line says so");
        editor->SetEditSet("Standard");
        Check(editor->GetEditSet() == "Demo", "the standard column is not editable until shared sets are");
        editor->SetEditSharedSets(true);
        editor->SetEditSet("Standard");
        Check(editor->GetEditSet() == "Standard", "... then it is");
        editor->SetEditSet("Demo");
        Check(editor->Undo(), "Undo from the element");
        Check(Find(editor->GetRows(), "Fade out the audio")->status == LocalisationRowStatus::Untranslated, "... took the text back");

        editor->SetStatusFilter("untranslated");
        Check(editor->GetRows().size() == 2, "the untranslated filter: the application's two own keys");
        editor->SetStatusFilter("");
        editor->SetSearch("trim");
        Check(editor->GetRows().size() == 1 && editor->GetRows().front().key == "Trim", "the search narrows to Trim");
        editor->SetSearch("");
        editor->SetShowWholeSets(true);
        Check(Find(editor->GetRows(), "Open") != nullptr && grid->FindNode("g:3"), "the whole sets add the unused entries' group");
        editor->SetShowWholeSets(false);
        editor->SetColumnVisible("Standard", false);
        Check(!editor->IsColumnVisible("Standard") && Cell(grid->FindNode("r:Save"), "set:Standard").empty(), "a hidden column has no cells");
        editor->SetColumnVisible("Standard", true);

        Check(editor->TakeBaseSet("Audio") && editor->GetWorkspace().Bases().size() == 2, "the Sets picker takes a base");
        Check(Find(editor->GetRows(), "Trim")->status == LocalisationRowStatus::Collision, "... and the collision shows");
        editor->DropBaseSet("Audio");

        editor->SetStatusFilter("draft");
        Check(editor->GetRows().size() == 2, "the draft filter: the two rows the draft standard set answers");
        editor->SetStatusFilter("");
        Check(!editor->HasDrafter() && editor->Draft(true) == 0, "no drafter: nothing drafted");
        editor->SetDrafter([](const std::string& language, const std::vector<std::string>& texts, std::vector<std::string>& out, std::string&) {
            for (const auto& text : texts) out.push_back("[" + language + "] " + text);
            return true;
        });
        Check(editor->Draft(true) == 2, "the drafter fills the two untranslated rows");
        const LocalisationRow* ripple = Find(editor->GetRows(), "Ripple delete");
        Check(ripple && ripple->shown == "[de] Ripple delete" && ripple->status == LocalisationRowStatus::Application, "... with its text");
        editor->SetStatusFilter("draft");
        Check(editor->GetRows().size() == 4, "... both marked as drafts, with the standard set's two");
        editor->SetStatusFilter("");
        editor->SelectRow("Ripple delete");
        editor->SetSelectedTranslation("Ripple-Löschen");
        editor->SetStatusFilter("draft");
        Check(editor->GetRows().size() == 3, "a person's edit clears the draft mark");
        editor->SetStatusFilter("");

        bool saved = false;
        editor->onSaved = [&saved](const std::string&) { saved = true; };
        Check(editor->Save(LocalisationSaveTarget::SettingsFolder) && saved
              && std::filesystem::exists(PathFromUtf8(t.Settings() + "/locale/de/Demo.json")), "Save writes the override and reports");
        Check(editor->ExportTo(PathToUtf8(t.root / "out.po")) && std::filesystem::exists(t.root / "out.po"), "Export by the path's extension");
        editor->SetCompareLanguage("de");
        Check(editor->GetWorkspace().CompareLanguage() == "de", "the comparison language follows the picker");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
