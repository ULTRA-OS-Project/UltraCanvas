// Apps/UltraOffice/UltraOfficeHost.h
// What a tab of UltraOffice holds: one document of one kind, behind one small
// interface the window talks to. Each kind implements it once over the
// framework element that edits it - UltraCanvasRichTextEdit for documents,
// UltraCanvasSpreadsheet for spreadsheets, UltraCanvasMediaViewer (slides and
// the slide show) for presentations - so the window never asks "and which
// kind is this?" (Docs/Research/UltraOfficeDesignProposal.md, §4.2).
//
// Version: 0.2.0 - AttachSettings (what a kind remembers: the document's navigator), HandleShortcut (F5)
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasUIElement.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas { class UltraCanvasAppSettings; }

namespace UltraOffice {

using OfficeKind = UltraCanvas::UltraCanvasFileLoader::OfficeDocumentKind;

// Where new files go by default: Office Open XML (.docx, .xlsx, .pptx) or
// OpenDocument (.odt, .ods, .odp). Both are read and written equally well;
// the File menu changes it.
enum class SaveFormat { OfficeOpenXml, OpenDocument };

// One command tab of a document kind ("Home", "Insert" ...) and the toolbar
// it shows.
struct CommandTab {
    std::string name;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> toolbar;
};

class OfficeHost {
public:
    virtual ~OfficeHost() = default;

    virtual OfficeKind GetKind() const = 0;
    // The element the tab shows under its toolbar; it grows to fill the tab.
    virtual std::shared_ptr<UltraCanvas::UltraCanvasUIElement> GetContent() = 0;
    // The command tabs and their toolbars, built once.
    virtual const std::vector<CommandTab>& GetCommandTabs() = 0;

    // An empty document of this kind.
    virtual void NewBlank() = 0;
    // Reads `path` (UTF-8) with `password` when it is protected. False with
    // a reason a person can read in `error`.
    virtual bool Load(const std::string& path, const std::string& password, std::string& error) = 0;
    // Writes the document as the type `path` names, with `password` when
    // not empty, and leaves whether it counts as saved alone: the window's
    // recovery copies are written this way.
    virtual bool Write(const std::string& path, const std::string& password, std::string& error) = 0;
    // Write, and the document counts as saved.
    bool Save(const std::string& path, const std::string& password, std::string& error) {
        if (!Write(path, password, error)) return false;
        SetModified(false);
        return true;
    }
    // The Save dialog's types for this kind (through UltraCanvasFileLoader).
    virtual void AddSaveTypes(UltraCanvas::FileDialogOptions& opts) const = 0;
    // The extension a new file of this kind is saved with.
    virtual std::string DefaultExtension(SaveFormat format) const = 0;
    // False for a kind whose changes cannot be written back (a presentation,
    // until the slide editor exists): Save then always asks for a name.
    virtual bool CanSaveInPlace() const { return true; }

    // PDF and printing; a kind without them says why.
    virtual bool ExportPdf(const std::string& path, std::string& error) { (void)path; error = "not available"; return false; }
    virtual bool Print(const std::string& documentName, UltraCanvas::UltraCanvasWindowBase* parent, std::string& error) {
        (void)documentName; (void)parent; error = "not available"; return false;
    }
    virtual bool CanExportPdf() const { return false; }

    virtual bool IsModified() const = 0;
    virtual void SetModified(bool modified) = 0;
    virtual void Undo() = 0;
    virtual void Redo() = 0;
    // The status line under the document: where the caret is, what the
    // selection adds up to.
    virtual std::string GetStatusText() const = 0;
    // Gives the keyboard to the document.
    virtual void Focus() = 0;

    // The window's settings, for what a kind remembers between runs (the
    // document's navigator, shown or not): given once, when its tab is added.
    virtual void AttachSettings(UltraCanvas::UltraCanvasAppSettings* settings) { (void)settings; }
    // A key the window's own shortcuts leave alone, offered to the tab that
    // is open: true when the kind used it (F5 shows the document's navigator).
    virtual bool HandleShortcut(const UltraCanvas::UCEvent& event) { (void)event; return false; }

    // The host tells the window that the modified state or the status changed.
    std::function<void()> onChanged;
};

// Where the toolbar icons live: media/icons/<name>, through the resources
// folder.
std::string IconPath(const std::string& relative);

std::unique_ptr<OfficeHost> CreateDocumentHost(UltraCanvas::UltraCanvasWindowBase* window);
std::unique_ptr<OfficeHost> CreateSpreadsheetHost(UltraCanvas::UltraCanvasWindowBase* window);
std::unique_ptr<OfficeHost> CreatePresentationHost(UltraCanvas::UltraCanvasWindowBase* window);

} // namespace UltraOffice
