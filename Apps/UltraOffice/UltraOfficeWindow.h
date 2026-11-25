// Apps/UltraOffice/UltraOfficeWindow.h
// The UltraOffice window: one tab strip for every open document, whatever its
// kind, each tab marked with its kind's icon; a Start tab first. Each
// document's tab holds its command row (File, Undo, Redo, Save and the
// kind's command tabs), the toolbar of the command tab chosen, the document
// itself and a status line - so the toolbar follows the tab that is open.
//
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraOfficeHost.h"

#include "UltraCanvasAppSettings.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasStartPage.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasWindow.h"

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
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> toolbarSlot;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> status;
        std::vector<UltraCanvas::UltraCanvasButton*> commandButtons;
        int commandTab = -1;
    };

    UltraCanvas::UltraCanvasAppSettings settings_;
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
    void ShowCommandTab(OpenDocument& doc, int index);
    void OpenPath(const std::string& path);
    void ShowOpenDialog();
    void ShowFileMenu(UltraCanvas::UltraCanvasUIElement* anchor);

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
    SaveFormat GetSaveFormat() const;
    void SetSaveFormat(SaveFormat format);
    void ShowError(const std::string& title, const std::string& message);
};

} // namespace UltraOffice
