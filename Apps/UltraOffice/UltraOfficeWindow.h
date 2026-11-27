// Apps/UltraOffice/UltraOfficeWindow.h
// The UltraOffice window: one tab strip for every open document, whatever its
// kind, each tab marked with its kind's icon; a Start tab first. Each
// document's tab holds one toolbar at the top - File, Undo, Redo, Save, and
// the kind's view group (fits, zoom, panes) last, at its right end - the
// document itself and a status line. The tools are, by default, in tool
// panels at the left of the document (UltraOfficePanels.h): a column with a
// splitter whose width is kept, panels the user reorders, opens and closes,
// kept per kind in the settings, context panels for the selection, and an
// icon rail in a window narrower than 1100 px or when chosen. The Settings
// window's other choice is the command tabs (Home, Insert ...) in the top
// row with the chosen tab's toolbar under it; a narrow window puts its
// tools behind their chevron first. F6 goes from the tab strip to the
// toolbar, the panels and the document. Every minute (the Settings window
// sets how often) a copy of each document with unsaved changes goes to the
// recovery folder; a run that ends without closing leaves them there, and
// the next start offers them back. A template opens as a new, untitled
// document (UltraOfficeTemplates.h). The Settings window
// (UltraOfficeSettingsDialog.h, File > Settings, Ctrl+,) sets what
// UltraOfficeSettings.h names.
//
// Version: 0.6.0 - the tool panels at the left (the default), the toolbar a setting; one top toolbar with the
//                  view group last; panel order, open panels and the column's width kept per kind; the icon
//                  rail; F6 between the tab strip, the toolbar, the panels and the document
// Version: 0.5.0 - the view group at the toolbar's right end; the Settings window; Ctrl++, Ctrl+-,
//                  Ctrl+0 zoom; autosave to the file, the recovery copies and their interval, the recent
//                  files' count and the files reopened at the next start from the settings
// Version: 0.4.0 - new from template (the Start tab's gallery, File > New from template) and
//                  Save as template
// Version: 0.3.0 - recovered documents offered on the Start tab, not in a dialog
// Version: 0.2.0 - autosave and crash recovery
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraOfficeHost.h"
#include "UltraOfficePanels.h"
#include "UltraOfficeSettings.h"

#include "UltraCanvasAppSettings.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasStartPage.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasWindow.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraOffice {

class UltraOfficeWindow : public UltraCanvas::UltraCanvasWindow {
public:
    UltraOfficeWindow();
    ~UltraOfficeWindow() override;

    // Builds the window's content once Create() has made the window, and
    // opens `paths` (UTF-8).
    void Initialize(const std::vector<std::string>& paths);
    // Opens files in tabs of their kind; a file already open comes forward.
    void OpenPaths(const std::vector<std::string>& paths);

    // New, empty documents.
    void NewDocument(OfficeKind kind);
    // A new, untitled document with the content of the template at `path`
    // (UTF-8): the template itself is never written to.
    void NewFromTemplate(const std::string& path);

    // The open documents, in tab order: their file (empty: never saved -
    // Save asks for a name), tab title, kind and unsaved state. For tests.
    struct DocumentSummary {
        std::string path;
        std::string name;
        OfficeKind kind = OfficeKind::Unknown;
        bool modified = false;
    };
    std::vector<DocumentSummary> GetDocuments() const;
    // The user's templates folder (<settings folder>/UltraOffice/templates).
    std::filesystem::path OwnTemplatesFolder() const;

    // ===== SETTINGS =====
    // What the Settings window sets, saved in <settings folder>/UltraOffice/
    // settings.json.
    OfficeSettings& Settings() { return prefs_; }
    // The Settings window (File > Settings..., Ctrl+, , the Start tab).
    void ShowSettings();
    // After a change in the Settings window: the theme, the autosave timer,
    // the Start tab's recent files and each open tab follow at once.
    void ApplySettings();
    void ClearRecentFiles();

    // ===== THE TOOLS =====
    // The tool panels of the tab open; null on the Start tab or in toolbar
    // mode. For tests.
    ToolPanels* GetToolPanels();
    // F6: the focus to the next of the tab strip, the toolbar, the panels
    // (panel mode) and the document; Shift+F6 the other way.
    void CycleFocus(bool backwards);

private:
    bool HandleShortcut(const UltraCanvas::UCEvent& event);
    struct OpenDocument {
        int id = 0;
        std::string path;                // empty: never saved
        std::string name;                // the tab's title
        std::string password;            // the one it was opened with, to save it protected again
        std::unique_ptr<OfficeHost> host;
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> pane;
        std::shared_ptr<UltraCanvas::UltraCanvasToolbar> commandRow;
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> toolbarSlot;   // the command tab's bar, the view group
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> commandSlot;   // where the command tab's bar goes
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> status;
        std::vector<UltraCanvas::UltraCanvasButton*> commandButtons;
        UltraCanvas::UltraCanvasButton* fileButton = nullptr;            // raw: the command row owns it
        std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> panelSplit;  // panel mode: the panels | the document
        std::unique_ptr<ToolPanels> panels;                              // panel mode
        ToolLayout layout = ToolLayout::Panels;                          // the one built
        bool railShown = false;                                          // the panels as the icon rail now
        int commandTab = -1;
        std::filesystem::path recoveryCopy;   // its copy in the recovery folder; empty: none
        bool changedSinceCopy = true;         // the copy is behind the document
    };

    UltraCanvas::UltraCanvasAppSettings settings_;
    OfficeSettings prefs_{settings_};
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> tabs_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> startPane_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
    std::vector<std::unique_ptr<OpenDocument>> documents_;
    int nextId_ = 1;
    int untitled_[3] = {0, 0, 0};
    bool closingConfirmed_ = false;

    void BuildStartPage();
    OpenDocument* AddDocument(std::unique_ptr<OfficeHost> host, const std::string& path, const std::string& name,
                              const std::string& password);
    void BuildPane(OpenDocument& doc);
    // The tab's tools as the settings say: the panels at the left, or the
    // command tabs and their toolbar; built again when the setting changes.
    void LayoutTools(OpenDocument& doc);
    void BuildCommandRow(OpenDocument& doc, bool withTabs);
    void BuildPanels(OpenDocument& doc);
    // The panel column's splitter as wide as the column, or the rail.
    void FitPanelSplit(OpenDocument& doc);
    // A kind's panels kept in the settings, and given to its other tabs.
    void SavePanels(OpenDocument& doc);
    void ShowCommandTab(OpenDocument& doc, int index);
    void OpenPath(const std::string& path);
    void ShowOpenDialog();
    void ShowFileMenu(UltraCanvas::UltraCanvasUIElement* anchor);

    // ===== TEMPLATES =====
    // OwnTemplatesFolder (public): where "Save as template" puts the user's
    // own, which the gallery offers beside the shipped ones.
    void ShowTemplateOpenDialog();
    void SaveAsTemplate(OpenDocument& doc);

    // Saves `doc`: in place when it has a path it can be written to, else (or
    // with `askForName`) through the Save dialog. `done(true)` once written.
    void Save(OpenDocument& doc, bool askForName, std::function<void(bool)> done = nullptr);
    bool WriteTo(OpenDocument& doc, const std::string& path);
    void ExportPdf(OpenDocument& doc);
    void Print(OpenDocument& doc);
    // Closes the tab, asking first about unsaved changes.
    void RequestCloseDocument(OpenDocument& doc);
    void CloseDocument(OpenDocument& doc);

    OpenDocument* Active();
    OpenDocument* FindById(int id);
    int TabIndexOf(const OpenDocument& doc) const;
    void Refresh(OpenDocument& doc);
    void UpdateTitle();
    void Remember(const std::string& path);
    // The files open now, for "reopen at the next start".
    void RememberOpenFiles();
    void ApplyTheme();
    SaveFormat GetSaveFormat() const;
    void SetSaveFormat(SaveFormat format);
    void ShowError(const std::string& title, const std::string& message);

    // ===== RECOVERY =====
    // <settings folder>/UltraOffice/recovery holds, for each document with
    // unsaved changes, a copy ("<id>.<ext>", written with its password) and
    // a note of what it is ("<id>.json": its kind, file and name). Saving,
    // closing or quitting removes them.
    UltraCanvas::TimerId autosaveTimer_ = 0;
    std::string runTag_;   // this run's copies are "<runTag>-<id>.<ext>", a crashed run's stay apart
    std::filesystem::path RecoveryFolder() const;
    void StartAutosave();
    void RestartAutosave();
    void Autosave();
    void WriteRecovery(OpenDocument& doc);
    void RemoveRecovery(OpenDocument& doc);
    void RemoveAllRecovery();
    // Offers back the copies a run that ended without closing left behind:
    // a section on the Start tab, which shows their count, so nothing waits
    // on an answer.
    std::vector<std::filesystem::path> recoveryNotes_;
    void OfferRecovery();
    void RebuildStartPageLater();
    void ShowRecoveryBadge();
    void RestoreAllRecovered();
    void DiscardAllRecovered();
    void RestoreRecovered(const std::vector<std::filesystem::path>& notes, size_t next);
};

} // namespace UltraOffice
