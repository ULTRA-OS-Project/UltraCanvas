// Apps/UltraPaint/UltraPaintWindow.h
// UltraPaint main window: menu bar, main toolbar, the tool palette on the
// left, the paint surface in the middle, the colour / tool options / layers
// panels on the right and a status bar at the bottom. Owns the document,
// the tools and every command (file, edit, image, layer, select, adjust,
// filter, view).
//
// The editor is multi-window: the class also holds the registry of open
// windows, so File > New Window and "Open new window" on a dropped image add
// to it and the application exits with the last of them.
// Version: 1.8.5 - DropPasswords forgets its passwords when the desktop session locks
//                  (UltraCanvasSessionLock); a drop's first file then asks again
// Version: 1.8.4 - DropPasswords: every password given in a drop (up to eight, newest
//                  first) is tried on each protected file after it, not only the last
// Version: 1.8.3 - DropPassword: the password given for one file of a drop is tried first
//                  on the next protected file of it, and forgotten when the drop is done;
//                  PasteAsNewImage says what it pasted
// Version: 1.8.2 - LoadDocumentAsking: a protected file asks for its password wherever a
//                  file is read - each of a multi-file drop in turn (ApplyImports), a
//                  pasted one; AskForFilePassword's notOpened
// Version: 1.8.1 - AskForFilePassword: one password question for a protected *.ucd and a
//                  protected PDF; ImportFile takes the PDF's password on to the rasterizer
// Version: 1.8.0 - password-protected *.ucd: CmdPassword (File > Password Protection...),
//                  OpenProtectedFile asks for a file's password, SaveLossQuestion
// Version: 1.7.0 - the palette rows under the picker (UltraCanvasColorPaletteManager):
//                  palettes added from a gallery, an entry set with a right click;
//                  RememberPalettes keeps them between starts
// Version: 1.6.2 - RememberColourPicker: the picker opens as it was left
// Version: 1.6.1 - ReturnKeyboardToText: a Text option's list closed without a choice gives
//                  the keyboard back to the text being typed
// Version: 1.6.0 - the Text tool types on the canvas: an UltraCanvasRichTextEdit over the
//                  canvas at its zoom edits a text layer; Layer > Edit Text / Rasterize Layer
// Version: 1.5.0 - the right panel's sections are group boxes (optionsPanel, layersPanel)
// Version: 1.4.0 - BringForward: restores and raises the window (a file handed over by a
//                  second start opens in a window of its own and comes to the front)
// Version: 1.3.0 - toolSlots: the palette's tools in groups, one slot each, with a fold-out bar
// Version: 1.2.0 - the mouse pointer magnification glass (a loupe over the canvas while the
//                  rectangle selection is active) and Edit > Settings... with its page
// Version: 1.1.3 - StartLayerRename / FinishLayerRename: a layer is renamed in the list
// Version: 1.1.2 - ScheduleLayerPanelRebuild: the layer list is rebuilt after the event that changed it
// Version: 1.1.1 - ToolIconPath / MenuIconPath: the palette's 28 px and the menus' 16 px icons
// Version: 1.1.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasColorPaletteManager.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasPaintSurface.h"
#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasFileSaveSettings.h"   // FileSaveImageSize
#include "UltraCanvasSessionLock.h"        // a drop's passwords go when the session locks

#include "UltraPaintTools.h"
#include "UltraPaintFilters.h"
#include "UltraPaintDialogs.h"
#include "UltraPaintSettings.h"
#include "UltraPaintSettingsDialog.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraPaintWindow {
public:
    UltraPaintWindow();
    ~UltraPaintWindow();

    // Creates the window; opens `paths[0]` when given, else a blank canvas.
    // Further paths are not this window's — OpenWindow() gives each of them
    // one of its own.
    bool Initialize(const std::vector<std::string>& paths);
    void Show();

    // Creates, registers and shows a further editor window on `paths` (empty:
    // a blank canvas). A window holds one image, so a second and further path
    // — a selection dropped on the application's icon, or a command line of
    // them — each get a window too; the returned one is the first. The windows
    // own themselves — the registry drops one when it closes, and the
    // application exits with the last of them — so the pointer may be ignored.
    static std::shared_ptr<UltraPaintWindow> OpenWindow(const std::vector<std::string>& paths);
    static int WindowCount();
    // Restores the window when minimised and raises it with the keyboard
    // focus: what a file a second start handed over opened in comes forward.
    void BringForward();

private:
    // ----- window registry -----
    static std::vector<std::shared_ptr<UltraPaintWindow>>& OpenWindows();
    // Drops `window` from the registry on the next turn of the event loop:
    // the call arrives from inside that window's own close, so the object
    // cannot be destroyed yet.
    static void RetireWindow(UltraPaintWindow* closed);

    // ----- construction -----
    void BuildMenuBar();
    void BuildToolbar();
    void BuildToolPalette();
    void BuildRightPanel();
    // The colour picker's design and open / folded channels, into the
    // settings file: the next editor opens it the way it was left.
    void RememberColourPicker();
    // The palette rows under the picker into the settings file.
    void RememberPalettes();
    void BuildStatusBar();
    void InstallShortcuts();
    std::string IconPath(const std::string& file) const;
    // A menu entry's icon: the 16 px set (media/icons/ultrapaint/16).
    std::string MenuIconPath(const std::string& file) const;
    // A tool's icon for the palette: the 28 px set (media/icons/ultrapaint/28).
    std::string ToolIconPath(const std::string& file) const;
    std::string TexterIconPath(const std::string& file) const;

    // ----- document -----
    void SetDocument(std::shared_ptr<UCRasterDocument> doc, const std::string& title);
    void NewImage(const UltraPaintNewImageResult& r);
    void OpenFile(const std::string& path);
    // The straight "replace what is open with this file" path; OpenFile()
    // sends vector artwork through the import dialog first.
    bool LoadIntoWindow(const std::string& path);

    // ----- import: dropped files and vector artwork -----
    // Files dropped on the canvas: asks once what to do with them, then does
    // it to all of them.
    void HandleDroppedFiles(const std::vector<std::string>& files);
    // Asks what to do with `path` — open it here, open a new window for it,
    // or merge it into the open image — and carries the answer out.
    // `fromDrop` decides which of those the dialog offers; `alsoFiles` are the
    // rest of a multi-file drop, which follow the same answer.
    // `preloaded`: the file already read (a password-protected *.ucd, opened
    // once its password was given), so it is not read again. `password`: the
    // one given for it - a PDF is rasterized with it, and the rest of a drop
    // try it first (DropPasswords).
    void ImportFile(const std::string& path, bool fromDrop,
                    const std::vector<std::string>& alsoFiles = {},
                    std::shared_ptr<UCRasterDocument> preloaded = nullptr,
                    const std::string& password = std::string());
    // What a password does when it is tried on a file: opens it, is not its
    // password, or is right but the file would not open (`error` says why).
    enum class PasswordTry { Opened, Wrong, Failed };
    // A password-protected file - a *.ucd to open, a PDF to import: asks for
    // its password and tries it with `attempt`, asking again (`note` says
    // why) while it is not right. Cancel opens nothing; a failure is shown.
    // `notOpened` runs when the question ends without opening the file
    // (Cancel, or once the failure's message is closed).
    void AskForFilePassword(const std::string& path,
                            const std::function<PasswordTry(const std::string& password,
                                                            std::string& error)>& attempt,
                            const std::function<void()>& notOpened = nullptr,
                            const std::string& note = std::string());
    // A 3D model takes the framework's model-view dialog instead: it has no
    // view either, and one has to be chosen before there are any pixels.
    void ImportModel(const std::string& path, bool fromDrop,
                     const std::vector<std::string>& alsoFiles = {});
    // Carries out the dialog's answer. `preloaded` is the document already
    // read for the dialog (a dropped bitmap is decoded to measure it), so the
    // file is not read a second time; null means read it now.
    void ApplyImport(const std::string& path, const UltraPaintImportResult& result,
                     std::shared_ptr<UCRasterDocument> preloaded = nullptr);
    // The passwords given for the files of a drop, tried - the one that
    // last opened a file first - on each protected file after them: files
    // dropped together often share a password, or a few, so each is typed
    // once, not once per file. Held in UltraCryptSecureBuffers (zeroed when
    // freed), at most kMax distinct ones, and only while the drop is being
    // read: ApplyImports lets go of them when the drop is done, and so does
    // the desktop session locking - a drop left half-answered while the user
    // steps away asks again for what comes after.
    struct DropPasswords {
        static constexpr size_t kMax = 8;
        std::vector<std::shared_ptr<UltraCryptSecureBuffer>> values;   // newest first
        DropPasswords() = default;
        ~DropPasswords();
        DropPasswords(const DropPasswords&) = delete;
        DropPasswords& operator=(const DropPasswords&) = delete;
        // To the front - a password already held moves there; the oldest
        // goes past kMax. While any is held, a session lock forgets them.
        void Remember(const std::string& password);
        std::vector<std::string> All() const;   // newest first
        std::string Newest() const;
        bool Empty() const { return values.empty(); }
        void Forget();
    private:
        SessionLockListenerId lockListener = 0;
    };
    // The rest of a multi-file drop: each read and given the same answer, one
    // after the other - a password-protected one tries the passwords
    // `remembered` holds first, else asks for its password, and the next waits for the answer; one
    // cancelled or unreadable is left out and the next goes on.
    void ApplyImports(std::vector<std::string> files, const UltraPaintImportResult& result,
                      std::shared_ptr<DropPasswords> remembered = nullptr);
    // Runs `task` once the editor has drawn again (a task posted from a
    // posted task runs after the loop's render), so what it asks is not
    // asked over a dialog that has just closed or a canvas that has not
    // caught up - the desktop's own dialogs hold the loop while they ask.
    void AfterNextDraw(std::function<void()> task);
    // What the answer does with a document that has been read.
    void ApplyLoaded(const std::string& path, const UltraPaintImportResult& result,
                     const std::shared_ptr<UCRasterDocument>& incoming);
    // Reads a file into a document, rasterizing vector artwork at `result`'s
    // size and page. Null with the reason already shown when it cannot be read.
    std::shared_ptr<UCRasterDocument> LoadDocument(const std::string& path,
                                                   const UltraPaintImportResult& result);
    // LoadDocument for a file that may be password-protected (a *.ucd, a
    // PDF): asks for its password first, again while it is wrong. `loaded`
    // gets the document; `notLoaded` runs instead when there is none
    // (cancelled, or unreadable - the reason already shown). Either runs at
    // once for a file that needs no password, and on a later turn of the
    // loop - once the question has closed - for one that did.
    // `remembered`, when given: its passwords are tried before asking, and
    // the one that is then given joins them.
    void LoadDocumentAsking(const std::string& path, const UltraPaintImportResult& result,
                            const std::function<void(std::shared_ptr<UCRasterDocument>)>& loaded,
                            const std::function<void()>& notLoaded = nullptr,
                            std::shared_ptr<DropPasswords> remembered = nullptr);
    // Merges a document's flattened pixels into the open image as a new layer.
    void MergeDocument(const std::shared_ptr<UCRasterDocument>& incoming,
                       const std::string& path, bool scaleToFit);
    // `size`: the pixel size chosen in the Save dialog's Settings for this
    // save (Save As); the picture's own size otherwise.
    bool SaveToPath(const std::string& path, const FileSaveImageSize& size = FileSaveImageSize());
    // Opens a dialog as this window's own: created, marked transient for the
    // editor and centred on it. A dialog that skips this is a top-level window
    // as far as the desktop is concerned, and gets placed like one — behind
    // the editor, in a screen corner, or (on a window manager that maximises
    // plain windows) over the whole image. The adjustment dialogs did skip it,
    // which is what made the Adjust menu look as though it did nothing.
    void ShowDialog(const std::shared_ptr<UltraCanvasWindow>& dialog);
    void ConfirmDiscard(const std::string& question, const std::function<void()>& proceed);
    // What saving the open image to `path` would lose - its layers (a flat
    // format, when `layers` is asked), its password (anything but a *.ucd) -
    // as the question to confirm first; empty when nothing is lost.
    std::string SaveLossQuestion(const std::string& path, bool layers) const;
    void UpdateTitle();
    void UpdateStatus();
    void UpdateUndoButtons();

    // ----- commands -----
    static std::vector<std::string> OpenableExtensions();
    void CmdNew();
    void CmdNewWindow();
    void CmdOpen();
    void CmdImport();
    void CmdSave();
    void CmdSaveAs();
    void CmdExport();
    void CmdPassword();
    void CmdUndo();
    void CmdRedo();
    void CmdCut();
    void CmdCopy(bool merged);
    void CmdPaste();
    void CmdPasteAsNew();
    // The newest image in the clipboard history, whatever the clipboard holds
    // now (text copied after the picture, for one).
    void CmdPasteLastImage();
    void CmdDelete();
    void CmdFill(bool foreground);
    void CmdScaleImage();
    void CmdCanvasSize();
    void CmdCropToSelection();
    void CmdFlatten();
    void CmdLayerAdd();
    void CmdLayerDuplicate();
    void CmdLayerDelete();
    void CmdLayerMergeDown();
    void CmdLayerMove(int delta);
    void CmdLayerProperties();
    void CmdSelectAll();
    void CmdSelectNone();
    void CmdSelectInvert();
    void CmdSelectModify(int kind);   // 0 feather, 1 grow, 2 shrink
    void CmdSelectFromAlpha();
    void CmdCurves();
    void CmdColourToAlpha();
    void CmdFilter(const PaintFilter& filter);
    void CmdSettings();
    void CmdAbout();
    void CmdQuit();

    // ----- tools -----
    void SelectTool(PaintToolId id);
    void RebuildToolOptions();
    void RebuildLayerPanel();
    // Rebuilds the layer list once the current event has been handled: the
    // request usually comes from a widget in that list (a row's button or
    // visibility box), which the rebuild would destroy under its own handler.
    void ScheduleLayerPanelRebuild();
    // Renaming a layer in the list: a double-click on its name puts a text
    // field in the name's place; Enter or a click elsewhere keeps what was
    // typed, Escape the old name.
    void StartLayerRename(int index);
    void FinishLayerRename(bool keep, const std::string& name);
    PaintTool* ActiveTool() const;

    // ----- the Text tool: typing on the canvas -----
    // A text is typed into an UltraCanvasRichTextEdit lying over the canvas
    // at the canvas's zoom, in the text's box; it lays the words out as the
    // text layer will hold them (UltraCanvasRasterText.h), so what is typed is
    // what the layer gets. The layer being edited is left out of the canvas
    // meanwhile (UCRasterDocument::SetInPlaceEditLayer), and the edit goes
    // into the document - a new text layer, or the edited one drawn again -
    // as one undo step when it is committed.
    struct TextEditSession {
        std::shared_ptr<UCRasterLayer> layer;      // the text layer being edited; null for a new text
        RasterTextContent content;                 // its box and default font (the edit's words are the editor's)
    };
    // Starts typing at (x, y): into the text layer whose box is there, or a
    // new text there. `boxWidth` > 0 when a box was dragged out.
    void BeginTextEdit(double x, double y, double boxWidth);
    // The editor opened on layer `layerIndex` (a text layer), or on a new
    // text at (x, y) when it is -1.
    void OpenTextSession(int layerIndex, double x, double y, double boxWidth);
    // Puts what was typed into the document and closes the editor. A new text
    // left empty adds nothing; an edited text emptied is removed.
    void CommitTextEdit();
    bool IsEditingText() const { return textSession != nullptr; }
    // Keeps the editor on the box as the view zooms and pans, and as tall as
    // what is typed.
    void PlaceTextEditor();
    // A Text tool option changed: applied to the editor's selection (or what
    // is typed next), else to the whole active text layer.
    void ApplyTextOption(const std::string& option);
    // A Text option's control let go of the keyboard without a change (a
    // list closed with Escape): after the event that closed it, the keys go
    // back to the text being typed - if the control still has them; Tab or a
    // click elsewhere put them where they were meant to go.
    void ReturnKeyboardToText(UltraCanvasUIElement& control);
    // The topmost visible text layer whose box holds (x, y), or -1.
    int TextLayerAt(double x, double y) const;
    // Before painting on a text layer: asks whether to rasterize it.
    void AskRasterize(const std::string& action);
    // For a command that changes the active layer's pixels (a filter, Fill,
    // Delete): finishes a text being typed and, when the active layer is a
    // text layer, asks to rasterize it and returns true - the command stops.
    bool StopsAtTextLayer(const std::string& action);
    // Draws the box of the text being typed (and, with the Text tool, every
    // text layer's) over the canvas.
    void DrawTextBoxes(IRenderContext* ctx, const PaintViewTransform& view);
    void CmdEditText();
    void CmdRasterizeLayer();

    // ----- the mouse pointer magnification glass -----
    // A loupe over a corner of the canvas (a UltraCanvasPaintSurface in loupe
    // mode inside a framed container) that shows the pixels around the
    // pointer at the view's zoom times the configured factor, with a
    // crosshair on the pointer's pixel. Shown while the active tool asks for
    // it (PaintTool::WantsMagnifier) and Settings > Display has it on.
    void BuildMagnifier();
    // Applies settings.magnifier*: size, zoom, and whether it shows at all.
    void ApplySettings();
    // Shows or hides it for the active tool and the settings.
    void UpdateMagnifierVisibility();
    // Follows the pointer: the view, and the corner when the pointer comes
    // near. `view` is the pointer in the surface's own coordinates.
    void UpdateMagnifier(double imageX, double imageY, const Point2Di& view);
    // Moves the glass to `corner` (0 top-left, 1 top-right, 2 bottom-left,
    // 3 bottom-right) of the canvas.
    void PlaceMagnifier(int corner);
    Rect2Df MagnifierRectForCorner(int corner) const;

    // ----- filters with preview -----
#ifdef HAS_LIBVIPS
    void BeginPreview();
    void ShowPreview(const std::function<PixelFX::PFXImage(const PixelFX::PFXImage&)>& op);
    void EndPreview(bool commit, const std::string& label,
                    const std::function<PixelFX::PFXImage(const PixelFX::PFXImage&)>& op);
#endif

    // ----- members -----
    std::shared_ptr<UltraCanvasWindow>         window;
    std::shared_ptr<UltraCanvasMenu>           menuBar;
    std::shared_ptr<UltraCanvasToolbar>        toolbar;
    std::shared_ptr<UltraCanvasContainer>      body;          // palette | surface | right panel
    std::shared_ptr<UltraCanvasToolbar>        palette;
    std::shared_ptr<UltraCanvasPaintSurface>   surface;
    std::shared_ptr<UltraCanvasContainer>      rightPanel;
    std::shared_ptr<UltraCanvasColorPicker>    colorPicker;
    std::shared_ptr<UltraCanvasColorPaletteManager> palettes;   // the palette rows under the picker
    // Two of the right panel's cards (BuildRightPanel; the Colour card holds
    // the picker and swatches): the active tool's options, captioned with the
    // tool's name, and Layers.
    std::shared_ptr<UltraCanvasGroupBox>       optionsPanel;
    std::shared_ptr<UltraCanvasGroupBox>       layersPanel;
    std::shared_ptr<UltraCanvasContainer>      layerRows;
    std::shared_ptr<UltraCanvasSlider>         layerOpacity;
    std::shared_ptr<UltraCanvasDropdown>       layerBlend;
    std::shared_ptr<UltraCanvasContainer>      statusBar;
    std::shared_ptr<UltraCanvasLabel>          statusPos, statusZoom, statusSize, statusHint;
    std::shared_ptr<UltraCanvasButton>         undoButton, redoButton;
    // The palette's slots, one per tool group (UltraPaintWindow.cpp:
    // PaletteGroups), top to bottom.
    std::vector<std::shared_ptr<UltraCanvasToolGroupButton>> toolSlots;
    std::shared_ptr<UltraCanvasContainer>      textEditorHost;   // over the canvas while a text is typed
    std::shared_ptr<UltraCanvasRichTextEdit>   textEditor;       // in it, on the text's box
    std::unique_ptr<TextEditSession>           textSession;
    std::shared_ptr<UltraCanvasContainer>      magnifierFrame;   // the glass's border, over the canvas
    std::shared_ptr<UltraCanvasPaintSurface>   magnifier;        // the loupe inside it
    int magnifierCorner = 1;                                      // where it sits now
    std::weak_ptr<UltraPaintSettingsDialog>    settingsDialog;   // raised instead of reopened

    UltraPaintSettings settings;

    std::shared_ptr<UCRasterDocument> document;
    UCRasterDocument::ListenerId documentListener = 0;   // the panels' and title's
    std::vector<std::unique_ptr<PaintTool>> tools;
    PaintToolOptions toolOptions;
    PaintToolContext toolContext;
    PaintToolId activeTool = PaintToolId::Brush;
    RasterPixel foreground = RasterPixel(0, 0, 0, 255);
    RasterPixel background = RasterPixel(255, 255, 255, 255);

    // in-app clipboard (the system clipboard gets a PNG too when it can)
    std::shared_ptr<UCRasterLayer> clipboardLayer;
    Point2Di clipboardOrigin;
    // the PNG the last copy put on the system clipboard: while the clipboard
    // still holds exactly these bytes, a paste is this window's own copy
    std::vector<uint8_t> clipboardPng;

    // What a paste brings in: a picture, or an image file copied in a file
    // manager. fromHere marks this window's own copy, which goes back where
    // it was copied from.
    struct PasteSource {
        std::shared_ptr<UCRasterLayer> image;
        bool fromHere = false;
        std::string file;
    };
    PasteSource ReadPasteSource();
    // `image` as a new layer: where it was copied from when it came from
    // here and still fits, centred otherwise.
    void PasteAsLayer(std::shared_ptr<UCRasterLayer> image, const std::string& layerName, bool fromHere,
                      const std::string& what);
    // `image` as a new image; `what` names it in the status line ("the last
    // copied image").
    void PasteAsNewImage(std::shared_ptr<UCRasterLayer> image, const std::string& what = std::string());
    // The clipboard history's newest image (UltraCanvasClipboardHistory, the
    // one UltraDesktop keeps) and when it was copied ("5 min ago"); null when
    // there is no history or no image in it.
    std::shared_ptr<UCRasterLayer> LastCopiedImage(std::string& when);
    // Nothing to paste: offers the history's newest image to `use`.
    void OfferLastCopiedImage(const std::function<void(std::shared_ptr<UCRasterLayer>)>& use);

    // filter preview state
    std::shared_ptr<UCRasterLayer> previewOriginal;
    int previewLayerIndex = -1;
    bool previewActive = false;
    std::weak_ptr<UltraCanvasWindow> activeDialog;

    UltraPaintNewImageResult lastNewImage;
    // A command-line vector drawing: its size dialog waits until the window
    // is on screen rather than opening behind it.
    std::string pendingImport;
    double pointerX = 0, pointerY = 0;
    bool pointerInside = false;
    bool layerPanelRebuildPending = false;
    int renamingLayer = -1;   // the layer whose name the list is editing, or -1
    // Expires with the window, so a rebuild posted to the event loop does
    // not run on a window that closed in between.
    std::shared_ptr<bool> lifeToken = std::make_shared<bool>(true);
};

} // namespace UltraCanvas
