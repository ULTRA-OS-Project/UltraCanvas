// include/UltraCanvasLocalisationEditor.h
// The localisation editor: every set an application's texts come from - its
// own set, the base sets it takes, the standard set - in one grid, one row per
// key and a column per set, with where each text comes from visible in the
// row; the sets merged in the editor (take a base set, promote a row to a
// base, demote it to the application, merge two bases); a save into the
// source tree where it is writable and into the settings folder otherwise;
// a compare column; a drafting button. Phase L4 of
// Docs/UltraCanvas/UltraCanvasLocalisationProposal.md, §5.
//
// Two layers:
//
//   LocalisationWorkspace - the files, with no UI: opens an application and a
//   language from a shipped locale tree (media/locale/) and the settings
//   folder's override tree, lists the rows with their status, edits the
//   working copies, merges the sets, undoes, saves, exports. A test drives it
//   on a folder of its own; the element and a headless tool share it.
//
//   UltraCanvasLocalisationEditor - the element over it: a toolbar (the
//   application, the language, the comparison language, the search, the
//   status filter, the Sets picker, the switches, the buttons), the columns
//   tree view, the edit row under it, the status line. Hosted by UOS-Settings
//   (Language > Translations), by DemoApp, and by ShowLocalisationEditorDialog
//   for an application's own "Translate this application..." item.
//
//   auto editor = std::make_shared<UltraCanvasLocalisationEditor>("editor");
//   editor->SetFolders(GetResourcesDir() + "media/locale", PathToUtf8(UltraCanvasSettingsFolder()));
//   editor->Open("UltraVideo", "de");
//   editor->SetDrafter(...);                // UltraAI, where the host links it
//
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasTextInput.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasWindowBase;

    // ===== THE WORKSPACE =====

    // One set the rows come from, in lookup order: the application, its base
    // sets most specific first, Standard.
    struct LocalisationSetRef {
        std::string name;        // "UltraVideo", "Video", "Standard"
        bool isBase = false;     // a file under base/
        bool isStandard = false;
        bool isApplication = false;
    };

    // Where a row's shown text comes from and what the editor marks it with.
    enum class LocalisationRowStatus {
        Application,    // the application's own word, no set below has it
        Override,       // the application translates a key a set below also has
        Duplicate,      // ... with the same text the set below has: an override to drop
        Base,           // inherited from a base set
        Collision,      // two base sets translate it differently; the first in order is shown
        Standard,       // inherited from the standard set
        Untranslated    // no set has it in this language: the English is shown
    };

    // One row of the grid: a key, its English, and the text of every set.
    struct LocalisationRow {
        std::string key;             // "button|Open", "{n} file"
        std::string context;         // "button" of "button|Open", else empty
        std::string source;          // the English text (the key without its context)
        bool plural = false;
        std::vector<std::string> categories;              // the language's CLDR categories, for a plural row
        std::map<std::string, std::string> texts;         // set name -> text (a plural: its "other" form)
        std::map<std::string, std::map<std::string, std::string>> forms;   // set name -> category -> form
        std::string shown;           // what the application shows, in the language
        std::string shownFrom;       // the set that answers; empty when untranslated
        LocalisationRowStatus status = LocalisationRowStatus::Untranslated;
        std::string collidesWith;    // Collision: the base set whose text differs
        bool draft = false;          // the answering file is a machine draft
        bool changed = false;        // edited in this session, not saved
        bool inApplication = false;  // the application's extraction lists the key
        bool unusedSharedEntry = false;   // shown only with "the whole set": the application does not use it
        std::string compare;         // the text in the comparison language
    };

    enum class LocalisationSaveTarget {
        SourceTree,      // media/locale/<lang>/<Set>.json in the shipped tree: a clean diff for a pull request
        SettingsFolder   // <settings>/locale/<lang>/<Set>.json: only the entries that differ from the shipped file
    };

    class LocalisationWorkspace {
    public:
        // ----- what there is -----
        // `shippedLocaleDir` is media/locale (GetResourcesDir() + "media/locale"
        // in an application, a checkout's media/locale for the source tree);
        // `settingsFolder` the UltraCanvas settings folder, whose locale/ holds
        // a person's own overrides. Either may be empty.
        void SetFolders(const std::string& shippedLocaleDir, const std::string& settingsFolder);
        const std::string& ShippedFolder() const { return shipped; }
        const std::string& SettingsFolder() const { return settings; }
        // Every application with an English set (<shipped>/en/<App>.json),
        // sorted; the base set names (<shipped>/en/base/*.json); the
        // languages that have a Standard set in either tree, "en" first.
        std::vector<std::string> Applications() const;
        std::vector<std::string> BaseSetNames() const;
        std::vector<std::string> Languages() const;

        // ----- open -----
        // The application's sets in `language`. An application with no
        // English set yet opens empty: its rows come as the person adds them.
        // Unsaved edits are dropped; a host asks first (Dirty()).
        bool Open(const std::string& application, const std::string& language);
        const std::string& Application() const { return application; }
        const std::string& Language() const { return language; }
        // The sets in lookup order: the application, its bases, Standard.
        std::vector<LocalisationSetRef> Sets() const;
        const std::vector<std::string>& Bases() const { return bases; }
        // The comparison language ("en" by default); its texts fill
        // LocalisationRow::compare.
        void SetCompareLanguage(const std::string& language);
        const std::string& CompareLanguage() const { return compareLanguage; }
        // The person's name for "@translated-by" (Language.conf's translator=).
        void SetTranslatorName(const std::string& name) { translator = name; }

        // ----- the rows -----
        // The application's keys (its own entries and the inherited ones its
        // extraction lists), grouped by status; with `wholeSets`, the unused
        // entries of the bases and of Standard too.
        std::vector<LocalisationRow> Rows(bool wholeSets = false) const;
        // One set's text for a key in the open language (a plural row: the
        // category's form, "other" when empty); empty when it has none.
        std::string Text(const std::string& set, const std::string& key, const std::string& category = "") const;
        // Whether the set's file in the open language has the key at all.
        bool Has(const std::string& set, const std::string& key) const;
        // The English of a key from the first set that has it: a plural's
        // form for `category` ("other" when empty), else the entry, else the
        // key's own text.
        std::string EnglishText(const std::string& key, const std::string& category = "") const;

        // ----- editing -----
        // Writes the set's text for a key in the open language; an empty text
        // removes the entry (an override cleared, the row inherited again). A
        // plural: `category` names the form; the set's key is then a plural
        // in that file. False for a set the application does not take.
        bool SetText(const std::string& set, const std::string& key, const std::string& text,
                     const std::string& category = "");
        bool Clear(const std::string& set, const std::string& key);
        // A key the application's English set does not have yet: listed from
        // now on, with `english` as its source text.
        bool AddKey(const std::string& key, const std::string& english);

        // ----- merging -----
        // The application takes the base set (its "@bases", in every language
        // it has a file in, the English first); the rows it had that the base
        // already translates show as Duplicate.
        bool TakeBaseSet(const std::string& base);
        bool DropBaseSet(const std::string& base);
        // The bases in a new lookup order (the same names).
        bool ReorderBases(const std::vector<std::string>& order);
        // A row of the application's set moved into a base set it takes, in
        // every language the application has it in; the application's row
        // becomes inherited (listed in its "@inherited").
        bool PromoteToBase(const std::string& key, const std::string& base);
        // The reverse: the base's entry becomes the application's own, in
        // every language, and leaves the base.
        bool DemoteToApplication(const std::string& key, const std::string& base);
        // The entries of `a` and `b` into `into` (one of them, or a new base),
        // in every language either has a file in; on a collision `a`'s text
        // is taken and the key is returned for a decision. The application's
        // "@bases" names `into` where it named `a` or `b`.
        std::vector<std::string> MergeBaseSets(const std::string& a, const std::string& b, const std::string& into);

        // ----- undo -----
        bool CanUndo() const { return !undo.empty(); }
        bool Undo();

        // ----- saving -----
        bool Dirty() const;
        std::vector<std::string> DirtyFiles() const;   // the paths a Save would write
        // Where a save goes by default: the source tree when the shipped
        // folder is writable, the settings folder otherwise.
        LocalisationSaveTarget DefaultSaveTarget() const;
        // Writes every changed file. SourceTree: the whole catalogue, keys in
        // the extraction's order, so the diff is clean. SettingsFolder: the
        // entries that differ from the shipped file (an override file that
        // would be empty is removed). `report` says what was written, or what
        // could not be.
        bool Save(LocalisationSaveTarget target, std::string& report);
        // The application's merged set in the open language - every key it
        // shows, with the set's text - as a catalogue file, or as gettext PO.
        bool ExportJson(const std::string& path, std::string& error) const;
        bool ExportPo(const std::string& path, std::string& error) const;

        // The file a set's catalogue lives in (shipped tree) and its override.
        std::string ShippedPath(const std::string& lang, const std::string& set, bool base) const;
        std::string OverridePath(const std::string& lang, const std::string& set, bool base) const;
        // The CLDR categories the open language needs, in CLDR order.
        std::vector<std::string> Categories() const;

    private:
        struct File {
            std::string lang, set;
            bool base = false;
            bool exists = false;              // a shipped or override file was read
            LocalisationCatalogue shipped;    // the shipped file as read
            LocalisationCatalogue persisted;  // shipped with the override over it, as read or as last saved
            LocalisationCatalogue working;    // persisted, then the edits
            bool Dirty() const;               // working differs from persisted
        };
        struct Snapshot {
            std::map<std::string, File> files;
            std::vector<std::string> bases;
        };

        static std::string Key(const std::string& lang, const std::string& set, bool base);
        File& FileFor(const std::string& lang, const std::string& set, bool base);
        const File* FindFile(const std::string& lang, const std::string& set, bool base) const;
        bool IsBase(const std::string& set) const;
        bool TakesSet(const std::string& set) const;
        std::vector<std::string> LanguagesOnDisk() const;
        void PushUndo();
        static bool WriteFile(const std::string& path, const std::string& text, std::string& error);
        static LocalisationCatalogue Diff(const LocalisationCatalogue& shipped, const LocalisationCatalogue& working);
        static bool IsDraft(const LocalisationCatalogue& catalogue);
        LocalisationCatalogue MergedApplicationSet() const;
        void StampTranslator(LocalisationCatalogue& catalogue) const;

        std::string shipped, settings;
        std::string application, language, compareLanguage = "en", translator;
        std::vector<std::string> bases;
        std::map<std::string, File> files;
        std::vector<Snapshot> undo;
    };

    // ===== THE ELEMENT =====

    // Drafts `texts` (English) into `language`; false with `error` set when
    // it cannot. The editor's Draft button is enabled by SetDrafter; the host
    // wires UltraAI's translator (see Apps/UltraLocalisationDraft/main.cpp).
    using LocalisationDrafter = std::function<bool(const std::string& language,
                                                   const std::vector<std::string>& texts,
                                                   std::vector<std::string>& out, std::string& error)>;

    class UltraCanvasLocalisationEditor : public UltraCanvasContainer {
    public:
        UltraCanvasLocalisationEditor(const std::string& id, float x, float y, float w, float h);
        explicit UltraCanvasLocalisationEditor(const std::string& id)
                : UltraCanvasLocalisationEditor(id, -1, -1, -1, -1) {}

        // ----- what is edited -----
        void SetFolders(const std::string& shippedLocaleDir, const std::string& settingsFolder);
        // Fills the application and language lists from the folders and
        // opens the pair; an empty application opens the first listed.
        bool Open(const std::string& application, const std::string& language);
        LocalisationWorkspace& GetWorkspace() { return workspace; }
        const LocalisationWorkspace& GetWorkspace() const { return workspace; }
        void SetTranslatorName(const std::string& name);
        void SetDrafter(LocalisationDrafter drafter);
        bool HasDrafter() const { return static_cast<bool>(drafter); }
        // Which language the compare column shows ("en" by default).
        void SetCompareLanguage(const std::string& language);

        // ----- the view -----
        void SetSearch(const std::string& text);
        // "" every row, or "untranslated", "draft", "override", "collision", "changed".
        void SetStatusFilter(const std::string& filter);
        void SetShowWholeSets(bool on);
        bool GetShowWholeSets() const { return wholeSets; }
        // The base and Standard columns editable (a change there reaches every
        // application that takes the set).
        void SetEditSharedSets(bool on);
        bool GetEditSharedSets() const { return editShared; }
        // A set's column hidden or shown (the Sets picker's tick).
        void SetColumnVisible(const std::string& set, bool visible);
        bool IsColumnVisible(const std::string& set) const;
        void Rebuild();

        // ----- editing, open to the keyboard and to tests -----
        bool SelectRow(const std::string& key);
        const std::string& GetSelectedKey() const { return selectedKey; }
        // The set the edit row writes to; the application unless shared sets
        // are editable and another was chosen.
        void SetEditSet(const std::string& set);
        const std::string& GetEditSet() const { return editSet; }
        // The selected row's text in the edit set (a plural: the category's).
        bool SetSelectedTranslation(const std::string& text, const std::string& category = "");
        bool ClearSelectedOverride();
        bool Undo();
        bool TakeBaseSet(const std::string& base);
        bool DropBaseSet(const std::string& base);
        bool PromoteSelected(const std::string& base);
        bool DemoteSelected(const std::string& base);
        // The Draft button: the selected rows, else every untranslated row of
        // the application's keys. The rows drafted are selected for review.
        int Draft(bool everyUntranslated);
        // Save and Export, as the buttons do them.
        bool Save();
        bool Save(LocalisationSaveTarget target);
        bool ExportTo(const std::string& path);

        // The Close button of a dialog host (hidden in a page).
        void ShowCloseButton(bool show);

        // ----- the parts -----
        std::shared_ptr<UltraCanvasColumnsTreeView> GetGrid() const { return grid; }
        const std::vector<LocalisationRow>& GetRows() const { return rows; }   // as last rebuilt, filtered
        const std::string& GetStatusText() const { return statusText; }

        // ----- callbacks -----
        std::function<void(const std::string& report)> onSaved;
        std::function<void(const std::string& status)> onStatus;
        std::function<void()> onClose;    // the Close button of a dialog host

    private:
        void BuildUi();
        void FillPickers();
        void RebuildColumns();
        void RebuildRows();
        void RefreshEditRow();
        void SetStatus(const std::string& text);
        void OpenRowMenu(TreeNode* node, const UCEvent& event);
        void OpenSetsMenu();
        bool RowPasses(const LocalisationRow& row) const;
        std::string NodeIdOf(const std::string& key, const std::string& category = "") const;
        std::string StatusLabel(const LocalisationRow& row) const;

        LocalisationWorkspace workspace;
        LocalisationDrafter drafter;
        std::vector<LocalisationRow> rows;          // the rows shown
        std::map<std::string, size_t> rowIndex;     // key -> index into rows
        std::set<std::string> hiddenColumns;
        std::set<std::string> draftedKeys;          // drafted this session, for the mark
        std::string selectedKey, selectedCategory, editSet, search, statusFilter, statusText;
        bool wholeSets = false;
        bool editShared = false;
        bool rebuilding = false;
        std::vector<std::string> applicationNames, languageTags, compareTags, setNames;

        std::shared_ptr<UltraCanvasDropdown> applicationList, languageList, compareList, statusList, editSetList;
        std::shared_ptr<UltraCanvasTextInput> searchField, editField;
        std::shared_ptr<UltraCanvasCheckbox> wholeBox, sharedBox;
        std::shared_ptr<UltraCanvasButton> setsButton, undoButton, draftButton, saveButton, exportButton, clearButton, closeButton;
        std::shared_ptr<UltraCanvasColumnsTreeView> grid;
        std::shared_ptr<UltraCanvasLabel> keyLabel, statusLabel;
        std::shared_ptr<UltraCanvasMenu> openMenu;
    };

    // The editor in a dialog window on the application's own set and the
    // language it runs in - the "Help > Translate this application..." item.
    // The dialog's Close button closes it; a save goes where
    // LocalisationWorkspace::DefaultSaveTarget says.
    std::shared_ptr<UltraCanvasWindowBase> ShowLocalisationEditorDialog(
            UltraCanvasWindowBase* parent, const std::string& application, const std::string& language,
            const std::string& title = "", int width = 1100, int height = 680);

} // namespace UltraCanvas
