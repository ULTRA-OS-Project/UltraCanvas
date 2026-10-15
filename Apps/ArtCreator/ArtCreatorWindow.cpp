// Apps/ArtCreator/ArtCreatorWindow.cpp
// ArtCreator main window: menus, toolbars, palette, canvas, panels, status
// bar, shortcuts, file open / save through the Vector plugin's converters,
// and every command.
// Version: 1.8.0 - icon mode: File > New Icon (16 to 256 px), an Icon panel with the
//                 icon size, a pixel grid, pointer snapping that puts lines of odd
//                 pixel widths on pixel centres, Arrange > Snap to Pixels, the page
//                 drawn at the icon's size with its soft pixels, marked on the canvas
//                 too; Export writes the icon at its size; Object > line widths in
//                 pixels (1 to 4 px: icon pixels in icon mode); the tool palette in
//                 groups with fold-out bars, as UltraPaint's
// Version: 1.7.0 - BringForward: restores and raises the window
// Version: 1.6.0 - Export offers FileLoader's types (AddVectorDocumentSaveTypes):
//                 every vector writer and every bitmap type this build writes -
//                 PNG, JPEG, WebP, AVIF, HEIC, TIFF, GIF, QOI, ICO ... - and
//                 saves through UltraCanvasFileLoader::SaveVectorDocument, a
//                 bitmap drawn at the size the Settings give, sharp at any
// Version: 1.5.0 - icons redrawn on pixel grids, in three sets: the palette's tools at
//                 28 px (media/icons/artcreator/28) in 36 px buttons, the toolbar and
//                 panel icons on a 24 px grid (toolbars draw them at 24 px now),
//                 and a 16 px set for every menu entry (media/icons/artcreator/16);
//                 flat palette and toolbar buttons, as UltraPaint's
// Version: 1.4.0 - flat one-tone icons throughout: the menu set's for the file
//                 and edit commands (was the black Texter set), and an icon on
//                 most menu items
// Version: 1.3.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "ArtCreatorWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardDib.h"   // ClipboardDib::EncodePng
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasTextUtils.h"      // Base64Encode
#include "UltraCanvasVectorRaster.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif

// ARTCREATOR_VERSION comes from the build alone: CMake reads the first
// line of Docs/ArtCreator/CHANGELOG.md (cmake/UltraCanvasVersion.cmake)
// and passes it as a compile definition. No fallback here, so a build
// that lost it fails instead of reporting a wrong number.
#ifndef ARTCREATOR_VERSION
#error "ARTCREATOR_VERSION is not defined: build ArtCreator through the top-level CMakeLists.txt"
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {

using namespace VectorStorage;
using namespace VectorEdit;

namespace {
    // A menu entry with an icon beside its check box.
    MenuItemData WithIcon(MenuItemData item, const std::string& iconPath) {
        item.iconPath = iconPath;
        return item;
    }

    constexpr float kMenuHeight = 28.0f;
    constexpr float kToolbarHeight = 38.0f;
    constexpr float kPaletteWidth = 48.0f;
    constexpr float kToolButton = 36.0f;   // a tool's button, square, 4 px around its 28 px icon
    constexpr float kToolIcon = 28.0f;
    constexpr float kRightWidth = 300.0f;
    constexpr float kRightInner = 262.0f;
    constexpr float kStatusHeight = 24.0f;

    std::string FileNameOf(const std::string& path) {
        if (path.empty()) return "Untitled";
        return PathToUtf8(PathFromUtf8(path).filename());
    }

    std::string ExtensionOf(const std::string& path) {
        std::string e = PathToUtf8(PathFromUtf8(path).extension());
        if (!e.empty() && e[0] == '.') e.erase(0, 1);
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return e;
    }

    // The MIME type a picture file is embedded under, from its extension.
    std::string PictureMimeOf(const std::string& path) {
        const std::string ext = ExtensionOf(path);
        if (ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif") return "image/jpeg";
        if (ext == "tif") return "image/tiff";
        return "image/" + ext;
    }

    // A file a file manager copied that holds a picture, by extension.
    bool IsPictureFile(const std::string& path) {
        const std::string ext = ExtensionOf(path);
        if (ext.empty()) return false;
        const auto bitmaps = UltraCanvasFileLoader::GetSupportedLoadExtensions(MediaFormatCategory::Bitmap);
        return std::find(bitmaps.begin(), bitmaps.end(), ext) != bitmaps.end();
    }

    // Every element of a subtree gets a fresh Id: pasted or imported objects
    // must not share one with what the drawing already holds, or selection
    // and undo rebind to the wrong object.
    void RenewIds(const ElementPtr& element) {
        if (!element) return;
        element->Id = GenerateId();
        if (auto* group = dynamic_cast<VectorGroup*>(element.get())) {
            for (const auto& child : group->Children) RenewIds(child);
        }
    }

    bool FocusIsTextEntry(UltraCanvasWindowBase* win) {
        if (!win) return false;
        UltraCanvasUIElement* f = win->GetFocusedElement();
        return f && dynamic_cast<UltraCanvasTextInput*>(f) != nullptr;
    }

    // ui-reuse-exempt: helper that only sizes framework elements; nothing is painted here.
    std::shared_ptr<UltraCanvasLabel> PanelTitle(const std::string& id, const std::string& text) {
        auto l = CreateLabel(id, 0, 0, 0, 22, text);
        l->SetFontSize(12);
        l->SetFontWeight(FontWeight::Bold);
        l->SetTextColor(Color(60, 60, 70, 255));
        l->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return l;
    }

    const char* ElementKindName(const VectorElement& e) {
        switch (e.Type) {
            case VectorElementType::Rectangle: case VectorElementType::RoundedRectangle: return "rectangle";
            case VectorElementType::Circle: return "circle";
            case VectorElementType::Ellipse: return "ellipse";
            case VectorElementType::Line: return "line";
            case VectorElementType::Polyline: return "polyline";
            case VectorElementType::Polygon: return "polygon";
            case VectorElementType::Path: return "shape";
            case VectorElementType::Text: return "text";
            case VectorElementType::Group: return "group";
            case VectorElementType::ClipView: return "clip view";
            case VectorElementType::Blend: return "blend";
            case VectorElementType::Mould: return "mould";
            case VectorElementType::Image: return "image";
            default: return "object";
        }
    }
}

// ===========================================================================
// LIFECYCLE
// ===========================================================================

ArtCreatorWindow::ArtCreatorWindow() : selection(std::make_shared<VectorSelection>()) {}
ArtCreatorWindow::~ArtCreatorWindow() = default;

std::string ArtCreatorWindow::IconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/artcreator/" + file);
}

std::string ArtCreatorWindow::ToolIconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/artcreator/28/" + file);
}

std::string ArtCreatorWindow::MenuIconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/artcreator/16/" + file);
}

// The framework's shared line set (New, Open, Save, Undo, Zoom, ...).
std::string ArtCreatorWindow::SharedIconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/menu/" + file);
}

bool ArtCreatorWindow::Initialize(const std::vector<std::string>& paths) {
    WindowConfig config;
    config.title = "ArtCreator " ARTCREATOR_VERSION;
    config.width = 1280;
    config.height = 840;
    config.minWidth = 900;
    config.minHeight = 600;
    config.resizable = true;
    config.type = WindowType::Standard;

    window = CreateWindow(config);
    if (!window || !window->IsCreated()) return false;
    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->SetBackgroundColor(Color(236, 236, 238, 255));

    tools = CreateArtTools();

    BuildMenuBar();
    BuildToolbar();

    body = std::make_shared<UltraCanvasContainer>("ac-body", 0, 0, 0, 0);
    body->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    window->AddChild(body);

    BuildToolPalette();

    canvas = CreateVectorCanvas("ac-canvas");
    canvas->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    canvas->SetSelection(selection);
    canvas->SetRulerUnit(72.0 / 25.4, "mm");
    VectorSnapOptions snap;
    snap.toGuides = true; snap.toGrid = false; snap.toObjects = true; snap.toPage = true;
    canvas->SetSnapOptions(snap);
    VectorGridSpec grid;
    grid.visible = false; grid.spacing = 72.0 / 25.4 * 10; grid.subdivisions = 2;   // 10 mm, 5 mm minor
    canvas->SetGrid(grid);
    body->AddChild(canvas);

    BuildRightPanel();
    BuildStatusBar();

    // ----- tool context -----
    toolContext.getDocument = [this]() { return document; };
    toolContext.activeLayer = [this]() { return ActiveLayer(); };
    toolContext.selection = selection.get();
    toolContext.history = &history;
    toolContext.canvas = canvas.get();
    toolContext.options = &toolOptions;
    toolContext.fillColor = [this]() { return fillColour; };
    toolContext.lineColor = [this]() { return lineColour; };
    toolContext.setStatus = [this](const std::string& s) { if (statusHint) statusHint->SetText(s); };
    toolContext.requestText = [this](double x, double y) {
        ArtCreatorTextResult init = lastText;
        init.text.clear();
        init.font = toolOptions.textFont; init.size = toolOptions.textSize; init.bold = toolOptions.textBold;
        auto dlg = std::make_shared<ArtCreatorTextDialog>(init);
        dlg->onAccept = [this, x, y](const ArtCreatorTextResult& r) {
            toolOptions.textFont = r.font; toolOptions.textSize = r.size; toolOptions.textBold = r.bold;
            lastText = r;
            PlaceText(x, y, r);
        };
        ShowDialog(dlg);
    };
    toolContext.selectTool = [this](ArtToolId id) { SelectTool(id); };
    toolContext.refreshOptions = [this]() { RebuildToolOptions(); };

    // ----- canvas -> tool -----
    // In icon mode the pointer goes to the icon's pixel grid first (IconEvent).
    canvas->onToolPress = [this](const VectorPointerEvent& e) { if (auto* t = ActiveTool()) t->OnPress(toolContext, IconEvent(e)); };
    canvas->onToolDrag = [this](const VectorPointerEvent& e) {
        pointerX = e.doc.x; pointerY = e.doc.y; pointerInside = true;
        if (auto* t = ActiveTool()) t->OnDrag(toolContext, IconEvent(e));
        UpdateStatus();
    };
    canvas->onToolRelease = [this](const VectorPointerEvent& e) { if (auto* t = ActiveTool()) t->OnRelease(toolContext, IconEvent(e)); };
    canvas->onToolHover = [this](const VectorPointerEvent& e) {
        pointerX = e.doc.x; pointerY = e.doc.y; pointerInside = true;
        if (auto* t = ActiveTool()) t->OnHover(toolContext, IconEvent(e));
        UpdateStatus();
    };
    canvas->onToolDoubleClick = [this](const VectorPointerEvent& e) { if (auto* t = ActiveTool()) t->OnDoubleClick(toolContext, IconEvent(e)); };
    canvas->onToolKey = [this](const UCEvent& ev) { auto* t = ActiveTool(); return t && t->OnKey(toolContext, ev); };
    canvas->onDrawOverlay = [this](IRenderContext* ctx, const VectorViewTransform& v) {
        DrawIconOverlay(ctx, v);
        if (auto* t = ActiveTool()) t->DrawOverlay(toolContext, ctx, v);
    };
    canvas->onViewChanged = [this]() { UpdateStatus(); };
    canvas->onFilesDropped = [this](const std::vector<std::string>& files) {
        if (files.empty()) return;
        auto go = [this, files]() { OpenFile(files.front()); for (size_t i = 1; i < files.size(); ++i) OpenWindow({files[i]}); };
        if (modified) ConfirmDiscard("Discard the unsaved changes and open " + FileNameOf(files.front()) + "?", go);
        else go();
    };

    // ----- history and selection -----
    history.onChanged = [this]() { OnDocumentChanged(); };
    selection->AddListener([this]() { OnSelectionChanged(); });

    InstallShortcuts();

    window->onWindowClosing = [this]() -> bool {
        if (!modified) return true;
        ConfirmDiscard("The drawing has unsaved changes. Close anyway?", [this]() {
            modified = false;
            window->Close();
        });
        return false;
    };

    // ----- first document -----
    if (!paths.empty() && OpenFile(paths.front())) {
        // opened
    } else {
        NewDrawing(lastPage);
    }
    SelectTool(ArtToolId::Selector);
    return true;
}

void ArtCreatorWindow::Show() {
    if (window) window->Show();
}

// ===========================================================================
// WINDOW REGISTRY
// ===========================================================================

std::vector<std::shared_ptr<ArtCreatorWindow>>& ArtCreatorWindow::OpenWindows() {
    static std::vector<std::shared_ptr<ArtCreatorWindow>> windows;
    return windows;
}

int ArtCreatorWindow::WindowCount() {
    return static_cast<int>(OpenWindows().size());
}

void ArtCreatorWindow::BringForward() {
    if (!window) return;
    if (window->IsMinimized()) window->Restore();
    window->RaiseAndFocus();
}

std::shared_ptr<ArtCreatorWindow> ArtCreatorWindow::OpenWindow(const std::vector<std::string>& paths) {
    auto editor = std::make_shared<ArtCreatorWindow>();
    if (!editor->Initialize(paths)) return nullptr;
    OpenWindows().push_back(editor);
    ArtCreatorWindow* raw = editor.get();
    editor->window->onWindowClosed = [raw]() { RetireWindow(raw); };
    editor->Show();
    for (size_t i = 1; i < paths.size(); ++i) OpenWindow({ paths[i] });
    return editor;
}

void ArtCreatorWindow::RetireWindow(ArtCreatorWindow* closed) {
    auto drop = [closed]() {
        auto& windows = OpenWindows();
        windows.erase(std::remove_if(windows.begin(), windows.end(),
                                     [closed](const std::shared_ptr<ArtCreatorWindow>& w) { return w.get() == closed; }),
                      windows.end());
    };
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(drop);
    else drop();
}

// ===========================================================================
// CONSTRUCTION
// ===========================================================================

void ArtCreatorWindow::BuildMenuBar() {
    using M = MenuItemData;
    menuBar = MenuBuilder("ac-menubar", 0, 0, 0, kMenuHeight)
        .SetType(MenuType::Menubar)
        .AddSubmenu("File", {
            M::ActionWithShortcut("New...", "Ctrl+N", MenuIconPath("new.svg"), [this]() { CmdNew(); }),
            M::ActionWithShortcut("New Window", "Ctrl+Alt+N", MenuIconPath("new-window.svg"), [this]() { CmdNewWindow(); }),
            M::Submenu("New Icon", MenuIconPath("new-icon.svg"), [this]() {
                std::vector<MenuItemData> sizes;
                for (int n : ArtIconSizes())
                    sizes.push_back(M::Action(std::to_string(n) + " x " + std::to_string(n) + " px", [this, n]() { CmdNewIcon(n); }));
                return sizes;
            }),
            M::ActionWithShortcut("Open...", "Ctrl+O", MenuIconPath("open.svg"), [this]() { CmdOpen(); }),
            M::Separator(),
            M::ActionWithShortcut("Save", "Ctrl+S", MenuIconPath("save.svg"), [this]() { CmdSave(); }),
            M::ActionWithShortcut("Save As...", "Ctrl+Shift+S", MenuIconPath("save-as.svg"), [this]() { CmdSaveAs(); }),
            M::ActionWithShortcut("Export...", "Ctrl+E", MenuIconPath("export.svg"), [this]() { CmdExport(); }),
            M::Separator(),
            M::Action("Document Setup...", MenuIconPath("document-setup.svg"), [this]() { CmdDocumentSetup(); }),
            M::Separator(),
            M::ActionWithShortcut("Quit", "Ctrl+Q", MenuIconPath("quit.svg"), [this]() { CmdQuit(); }),
        })
        .AddSubmenu("Edit", {
            M::ActionWithShortcut("Undo", "Ctrl+Z", MenuIconPath("undo.svg"), [this]() { CmdUndo(); }),
            M::ActionWithShortcut("Redo", "Ctrl+Y", MenuIconPath("redo.svg"), [this]() { CmdRedo(); }),
            M::Separator(),
            M::ActionWithShortcut("Cut", "Ctrl+X", MenuIconPath("cut.svg"), [this]() { CmdCut(); }),
            M::ActionWithShortcut("Copy", "Ctrl+C", MenuIconPath("copy.svg"), [this]() { CmdCopy(); }),
            M::ActionWithShortcut("Paste", "Ctrl+V", MenuIconPath("paste.svg"), [this]() { CmdPaste(); }),
            M::ActionWithShortcut("Duplicate", "Ctrl+D", MenuIconPath("duplicate.svg"), [this]() { CmdDuplicate(); }),
            M::ActionWithShortcut("Delete", "Del", MenuIconPath("delete.svg"), [this]() { CmdDelete(); }),
            M::Separator(),
            M::ActionWithShortcut("Select All", "Ctrl+A", MenuIconPath("select-all.svg"), [this]() { CmdSelectAll(); }),
            M::ActionWithShortcut("Select None", "Ctrl+Shift+A", MenuIconPath("select-none.svg"), [this]() { CmdSelectNone(); }),
        })
        .AddSubmenu("Arrange", {
            M::ActionWithShortcut("Bring to Front", "Ctrl+F", MenuIconPath("to-front.svg"), [this]() { CmdReorder(ZOrderMove::ToFront); }),
            M::ActionWithShortcut("Bring Forward", "Ctrl+Shift+F", MenuIconPath("forward.svg"), [this]() { CmdReorder(ZOrderMove::Forward); }),
            M::ActionWithShortcut("Send Backward", "Ctrl+Shift+B", MenuIconPath("backward.svg"), [this]() { CmdReorder(ZOrderMove::Backward); }),
            M::ActionWithShortcut("Send to Back", "Ctrl+B", MenuIconPath("to-back.svg"), [this]() { CmdReorder(ZOrderMove::ToBack); }),
            M::Separator(),
            M::ActionWithShortcut("Group", "Ctrl+G", MenuIconPath("group.svg"), [this]() { CmdGroup(); }),
            M::ActionWithShortcut("Ungroup", "Ctrl+U", MenuIconPath("ungroup.svg"), [this]() { CmdUngroup(); }),
            M::Separator(),
            M::Action("Mirror Horizontally", MenuIconPath("mirror-h.svg"), [this]() { CmdMirror(true); }),
            M::Action("Mirror Vertically", MenuIconPath("mirror-v.svg"), [this]() { CmdMirror(false); }),
            M::Separator(),
            M::Header("Align to selection"),
            M::Action("Left Edges", MenuIconPath("align-left.svg"), [this]() { CmdAlign(AlignMode::Left, false); }),
            M::Action("Horizontal Centres", MenuIconPath("align-hcentre.svg"), [this]() { CmdAlign(AlignMode::HorizontalCenter, false); }),
            M::Action("Right Edges", MenuIconPath("align-right.svg"), [this]() { CmdAlign(AlignMode::Right, false); }),
            M::Action("Top Edges", MenuIconPath("align-top.svg"), [this]() { CmdAlign(AlignMode::Top, false); }),
            M::Action("Vertical Centres", MenuIconPath("align-vcentre.svg"), [this]() { CmdAlign(AlignMode::VerticalCenter, false); }),
            M::Action("Bottom Edges", MenuIconPath("align-bottom.svg"), [this]() { CmdAlign(AlignMode::Bottom, false); }),
            M::Separator(),
            M::Header("Align to page"),
            M::Action("Centre on Page", MenuIconPath("align-page.svg"), [this]() { CmdAlign(AlignMode::HorizontalCenter, true); CmdAlign(AlignMode::VerticalCenter, true); }),
            M::Separator(),
            M::Header("Distribute"),
            M::Action("Horizontal Centres Evenly", MenuIconPath("distribute-h.svg"), [this]() { CmdDistribute(DistributeMode::HorizontalCenters); }),
            M::Action("Vertical Centres Evenly", MenuIconPath("distribute-v.svg"), [this]() { CmdDistribute(DistributeMode::VerticalCenters); }),
            M::Action("Equal Horizontal Gaps", MenuIconPath("space-h.svg"), [this]() { CmdDistribute(DistributeMode::HorizontalGaps); }),
            M::Action("Equal Vertical Gaps", MenuIconPath("space-v.svg"), [this]() { CmdDistribute(DistributeMode::VerticalGaps); }),
            M::Separator(),
            M::Header("Combine shapes"),
            M::Action("Add Shapes", MenuIconPath("combine-add.svg"), [this]() { CmdCombine(CombineOp::Add); }),
            M::Action("Subtract Shapes", MenuIconPath("combine-subtract.svg"), [this]() { CmdCombine(CombineOp::Subtract); }),
            M::Action("Intersect Shapes", MenuIconPath("combine-intersect.svg"), [this]() { CmdCombine(CombineOp::Intersect); }),
            M::Action("Slice Shapes", MenuIconPath("combine-slice.svg"), [this]() { CmdCombine(CombineOp::Slice); }),
            M::Separator(),
            M::ActionWithShortcut("Apply ClipView", "Ctrl+K", MenuIconPath("clipview.svg"), [this]() { CmdApplyClipView(); }),
            M::Action("Remove ClipView, Blend or Mould", MenuIconPath("release.svg"), [this]() { CmdRemoveContainer(); }),
            M::Separator(),
            M::ActionWithShortcut("Convert to Editable Shapes", "Ctrl+Shift+S", MenuIconPath("shape-editor.svg"), [this]() { CmdConvertToPath(); }),
            M::ActionWithShortcut("Snap to Pixels", "Ctrl+Shift+P", MenuIconPath("snap-pixels.svg"), [this]() { CmdSnapToPixels(); }),
        })
        .AddSubmenu("Object", {
            M::Action("No Fill", MenuIconPath("no-fill.svg"), [this]() { CmdRemoveFill(); }),
            M::Action("No Line", MenuIconPath("no-line.svg"), [this]() { CmdRemoveLine(); }),
            M::Separator(),
            M::Header("Line width in points"),
            M::Action("Hairline (0.25 pt)", MenuIconPath("line-hairline.svg"), [this]() { CmdLineWidth(0.25f); }),
            M::Action("0.5 pt", MenuIconPath("line-05.svg"), [this]() { CmdLineWidth(0.5f); }),
            M::Action("1 pt", MenuIconPath("line-1.svg"), [this]() { CmdLineWidth(1.0f); }),
            M::Action("2 pt", MenuIconPath("line-2.svg"), [this]() { CmdLineWidth(2.0f); }),
            M::Action("4 pt", MenuIconPath("line-4.svg"), [this]() { CmdLineWidth(4.0f); }),
            M::Action("8 pt", MenuIconPath("line-8.svg"), [this]() { CmdLineWidth(8.0f); }),
            M::Header("Line width in pixels"),
            M::Action("1 px", MenuIconPath("line-1.svg"), [this]() { CmdLineWidthPixels(1); }),
            M::Action("2 px", MenuIconPath("line-2.svg"), [this]() { CmdLineWidthPixels(2); }),
            M::Action("3 px", MenuIconPath("line-3.svg"), [this]() { CmdLineWidthPixels(3); }),
            M::Action("4 px", MenuIconPath("line-4.svg"), [this]() { CmdLineWidthPixels(4); }),
        })
        .AddSubmenu("Layer", {
            M::ActionWithShortcut("New Layer", "Ctrl+Shift+N", MenuIconPath("layer-add.svg"), [this]() { CmdLayerAdd(); }),
            M::Action("Delete Layer", MenuIconPath("layer-delete.svg"), [this]() { CmdLayerDelete(); }),
            M::Separator(),
            M::Action("Move Layer Up", MenuIconPath("layer-up.svg"), [this]() { CmdLayerMove(1); }),
            M::Action("Move Layer Down", MenuIconPath("layer-down.svg"), [this]() { CmdLayerMove(-1); }),
            M::Separator(),
            M::Action("Show Layer", MenuIconPath("layer-show.svg"), [this]() { CmdLayerToggle(true); }),
            M::Action("Hide Layer", MenuIconPath("layer-hide.svg"), [this]() { CmdLayerToggle(false); }),
        })
        .AddSubmenu("View", {
            M::ActionWithShortcut("Zoom In", "+", MenuIconPath("zoom-in.svg"), [this]() { canvas->ZoomIn(); }),
            M::ActionWithShortcut("Zoom Out", "-", MenuIconPath("zoom-out.svg"), [this]() { canvas->ZoomOut(); }),
            M::ActionWithShortcut("Fit Page", "Ctrl+0", MenuIconPath("zoom-page.svg"), [this]() { canvas->ZoomToPage(); }),
            M::Action("Fit Drawing", MenuIconPath("fit-drawing.svg"), [this]() { canvas->ZoomToDrawing(); }),
            M::Action("Fit Selection", MenuIconPath("fit-selection.svg"), [this]() { canvas->ZoomToSelection(); }),
            M::ActionWithShortcut("Actual Size", "Ctrl+1", MenuIconPath("zoom-100.svg"), [this]() { canvas->ZoomToActual(); }),
            M::Separator(),
            WithIcon(M::Checkbox("Rulers", true, [this](bool on) { canvas->SetShowRulers(on); }), MenuIconPath("rulers.svg")),
            WithIcon(M::Checkbox("Grid", false, [this](bool on) { VectorGridSpec g = canvas->GetGrid(); g.visible = on; canvas->SetGrid(g); }), MenuIconPath("grid.svg")),
            WithIcon(M::Checkbox("Guides", true, [this](bool on) { canvas->SetShowGuides(on); }), MenuIconPath("guides.svg")),
            M::Separator(),
            WithIcon(M::Checkbox("Snap to Grid", false, [this](bool on) { VectorSnapOptions s = canvas->GetSnapOptions(); s.toGrid = on; canvas->SetSnapOptions(s); }), MenuIconPath("snap-grid.svg")),
            WithIcon(M::Checkbox("Snap to Guides", true, [this](bool on) { VectorSnapOptions s = canvas->GetSnapOptions(); s.toGuides = on; canvas->SetSnapOptions(s); }), MenuIconPath("snap-guides.svg")),
            WithIcon(M::Checkbox("Snap to Objects", true, [this](bool on) { VectorSnapOptions s = canvas->GetSnapOptions(); s.toObjects = on; canvas->SetSnapOptions(s); }), MenuIconPath("snap-objects.svg")),
            WithIcon(M::Checkbox("Snap to Page", true, [this](bool on) { VectorSnapOptions s = canvas->GetSnapOptions(); s.toPage = on; canvas->SetSnapOptions(s); }), MenuIconPath("snap-page.svg")),
            M::Action("Clear Guides", MenuIconPath("clear-guides.svg"), [this]() { canvas->ClearGuides(); }),
            M::Separator(),
            M::Submenu("Icon Size", MenuIconPath("new-icon.svg"), [this]() {
                std::vector<MenuItemData> items;
                items.push_back(M::Action(iconSize > 0 ? "Off" : "Off (now)", [this]() { SetIconSize(0); }));
                for (int n : ArtIconSizes()) {
                    std::string label = std::to_string(n) + " x " + std::to_string(n) + " px";
                    if (n == iconSize) label += " (now)";
                    items.push_back(M::Action(label, [this, n]() { SetIconSize(n); }));
                }
                return items;
            }),
        })
        .AddSubmenu("Help", {
            M::Action("About ArtCreator", MenuIconPath("about.svg"), [this]() { CmdAbout(); }),
        })
        .Build();
    menuBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    window->AddChild(menuBar);
}

void ArtCreatorWindow::BuildToolbar() {
    toolbar = std::make_shared<UltraCanvasToolbar>("ac-toolbar", 0, 0, 0, kToolbarHeight);
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    ToolbarAppearance app = toolbar->GetAppearance();
    app.showIconLabels = false;
    app.style = ToolbarStyle::Flat;
    app.hoverBackgroundColor = Color(0, 0, 0, 18);
    toolbar->SetAppearance(app);
    toolbar->AddButton("ac-tb-new", "", SharedIconPath("new.svg"), [this]() { CmdNew(); })->SetTooltip("New drawing (Ctrl+N)");
    toolbar->AddButton("ac-tb-open", "", SharedIconPath("folder.svg"), [this]() { CmdOpen(); })->SetTooltip("Open (Ctrl+O)");
    toolbar->AddButton("ac-tb-save", "", SharedIconPath("save.svg"), [this]() { CmdSave(); })->SetTooltip("Save (Ctrl+S)");
    toolbar->AddSeparator("ac-tb-s1");
    undoButton = toolbar->AddButton("ac-tb-undo", "", SharedIconPath("undo.svg"), [this]() { CmdUndo(); });
    undoButton->SetTooltip("Undo (Ctrl+Z)");
    redoButton = toolbar->AddButton("ac-tb-redo", "", SharedIconPath("redo.svg"), [this]() { CmdRedo(); });
    redoButton->SetTooltip("Redo (Ctrl+Y)");
    toolbar->AddSeparator("ac-tb-s2");
    toolbar->AddButton("ac-tb-zoomout", "", SharedIconPath("zoom-out.svg"), [this]() { canvas->ZoomOut(); })->SetTooltip("Zoom out (-)");
    toolbar->AddButton("ac-tb-zoomin", "", SharedIconPath("zoom-in.svg"), [this]() { canvas->ZoomIn(); })->SetTooltip("Zoom in (+)");
    toolbar->AddButton("ac-tb-fit", "", IconPath("zoom-page.svg"), [this]() { canvas->ZoomToPage(); })->SetTooltip("Fit page (Ctrl+0)");
    toolbar->AddButton("ac-tb-100", "", IconPath("zoom-100.svg"), [this]() { canvas->ZoomToActual(); })->SetTooltip("Actual size (Ctrl+1)");
    toolbar->AddSeparator("ac-tb-s3");
    toolbar->AddButton("ac-tb-group", "", IconPath("group.svg"), [this]() { CmdGroup(); })->SetTooltip("Group (Ctrl+G)");
    toolbar->AddButton("ac-tb-ungroup", "", IconPath("ungroup.svg"), [this]() { CmdUngroup(); })->SetTooltip("Ungroup (Ctrl+U)");
    toolbar->AddButton("ac-tb-front", "", IconPath("to-front.svg"), [this]() { CmdReorder(ZOrderMove::ToFront); })->SetTooltip("Bring to front (Ctrl+F)");
    toolbar->AddButton("ac-tb-back", "", IconPath("to-back.svg"), [this]() { CmdReorder(ZOrderMove::ToBack); })->SetTooltip("Send to back (Ctrl+B)");
    toolbar->AddSeparator("ac-tb-s4");
    toolbar->AddButton("ac-tb-mirror-h", "", IconPath("mirror-h.svg"), [this]() { CmdMirror(true); })->SetTooltip("Mirror horizontally");
    toolbar->AddButton("ac-tb-mirror-v", "", IconPath("mirror-v.svg"), [this]() { CmdMirror(false); })->SetTooltip("Mirror vertically");
    for (auto& c : toolbar->Children()) {
        if (auto b = std::dynamic_pointer_cast<UltraCanvasButton>(c)) {
            ButtonStyle flat = b->GetStyle();
            flat.cornerRadius = 6.0f;
            flat.pressedColor = Color(0, 0, 0, 30);
            b->SetStyle(flat);
        }
    }
    toolbar->AddSeparator("ac-tb-s5");
    // Display quality: outlines only, fills and lines, or antialiased.
    auto qualityCaption = toolbar->AddLabel("ac-tb-quality-caption", "Quality");
    qualityCaption->SetFontSize(11);
    qualityCaption->SetElementSize(Size2Df(48, 24));
    qualitySlider = CreateHorizontalSlider("ac-tb-quality", 0, 0, 90, 24, 0, 2);
    qualitySlider->SetStep(1);
    qualitySlider->SetValue(static_cast<float>(VectorDisplayQuality::Normal));
    qualitySlider->SetValueDisplay(SliderValueDisplay::NoDisplay);
    qualitySlider->SetTooltip("Display quality: outlines / fills and lines / antialiased");
    qualitySlider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    qualitySlider->onValueChanging = [this](float v) { SetDisplayQuality(static_cast<int>(std::lround(v))); };
    qualitySlider->onValueChanged = [this](float v) { SetDisplayQuality(static_cast<int>(std::lround(v))); };
    toolbar->AddChild(qualitySlider);
    qualityLabel = toolbar->AddLabel("ac-tb-quality-value", DisplayQualityName(VectorDisplayQuality::Normal));
    qualityLabel->SetFontSize(11);
    qualityLabel->SetElementSize(Size2Df(84, 24));
    window->AddChild(toolbar);
}

const char* ArtCreatorWindow::DisplayQualityName(VectorDisplayQuality quality) {
    switch (quality) {
        case VectorDisplayQuality::Outline: return "Outlines";
        case VectorDisplayQuality::Simple:  return "Fills + lines";
        default:                            return "Antialiased";
    }
}

void ArtCreatorWindow::SetDisplayQuality(int level) {
    const auto quality = static_cast<VectorDisplayQuality>(std::clamp(level, 0, 2));
    if (qualityLabel) qualityLabel->SetText(DisplayQualityName(quality));
    if (canvas) canvas->SetDisplayQuality(quality);
}

namespace {
    // The palette's groups, top to bottom, as UltraPaint's: tools of a kind
    // share one slot, the group's main tool first. The slot shows the tool
    // last picked from its group, and resting the pointer on it folds out a
    // bar with the whole group. A group of one tool is a plain button. A
    // tool missing here still gets a slot of its own, after these, so a tool
    // added to CreateArtTools() never goes missing.
    struct PaletteGroup {
        const char* id;
        std::vector<ArtToolId> tools;
    };
    const std::vector<PaletteGroup>& PaletteGroups() {
        using T = ArtToolId;
        static const std::vector<PaletteGroup> groups = {
            { "select",  { T::Selector, T::ShapeEditor } },
            { "draw",    { T::Pen, T::Freehand, T::Line } },
            { "shapes",  { T::Rectangle, T::Ellipse, T::QuickShape } },
            { "text",    { T::Text } },
            { "fill",    { T::Fill, T::Transparency } },
            { "effects", { T::Shadow, T::Feather, T::Contour, T::Bevel } },   // drawn around a shape
            { "combine", { T::Blend, T::Mould } },                             // shapes into new ones
            { "view",    { T::Zoom, T::Push } },
        };
        return groups;
    }

    // A tool's id among the palette's group items.
    std::string ToolItemId(ArtToolId id) {
        return "tool-" + std::to_string(static_cast<int>(id));
    }
}

void ArtCreatorWindow::BuildToolPalette() {
    palette = std::make_shared<UltraCanvasToolbar>("ac-palette", 0, 0, kPaletteWidth, 0);
    palette->SetOrientation(ToolbarOrientation::Vertical);
    ToolbarAppearance app = palette->GetAppearance();
    app.showIconLabels = false;
    app.itemSpacing = 1.0f;
    palette->SetAppearance(app);
    // The buttons keep their size: where the window is too short for all of
    // them the column scrolls (wheel, or the hints at its ends) rather than
    // squeezing every icon into a button too small for it.
    palette->SetOverflowMode(ToolbarOverflowMode::Scroll);
    palette->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto findTool = [this](ArtToolId id) -> const ArtTool* {
        for (const auto& t : tools) if (t->id == id) return t.get();
        return nullptr;
    };
    auto itemOf = [this](const ArtTool& t) {
        ToolGroupItem item;
        item.id = ToolItemId(t.id);
        item.iconPath = ToolIconPath(t.iconFile);
        item.tooltip = t.name;
        if (t.shortcutKey) item.tooltip += std::string(" (") + t.shortcutKey + ")";
        return item;
    };

    std::vector<std::pair<std::string, std::vector<ToolGroupItem>>> slots;
    std::vector<ArtToolId> placed;
    for (const auto& g : PaletteGroups()) {
        std::vector<ToolGroupItem> items;
        for (ArtToolId id : g.tools) {
            if (const ArtTool* t = findTool(id)) { items.push_back(itemOf(*t)); placed.push_back(id); }
        }
        if (!items.empty()) slots.emplace_back(g.id, std::move(items));
    }
    for (const auto& t : tools) {
        if (std::find(placed.begin(), placed.end(), t->id) != placed.end()) continue;
        slots.emplace_back(ToolItemId(t->id), std::vector<ToolGroupItem>{ itemOf(*t) });
    }

    toolSlots.clear();
    for (auto& [groupId, items] : slots) {
        auto b = palette->AddToolGroup("ac-tools-" + groupId, items, [this](const std::string& itemId) {
            for (const auto& t : tools) {
                if (ToolItemId(t->id) == itemId) { SelectTool(t->id); return; }
            }
        });
        b->SetSlotShowsLastPicked(true);
        // The palette's icons are drawn on a 28 px grid (media/icons/
        // artcreator/28) with 2 px strokes on whole units: shown at 28 px
        // they land on whole pixels and stay sharp. Shown at any other size
        // every line falls between two pixels and blurs, as the 24 px icons
        // did at 22 px. The fold-out bar's buttons take the slot's size, icon
        // size and style.
        b->SetIconSize(kToolIcon, kToolIcon);
        b->SetElementSize(Size2Df(kToolButton, kToolButton));
        // Flat: no box around a tool; a soft grey under the pointer and the
        // active tool on a light blue, both rounded.
        ButtonStyle flat = b->GetStyle();
        flat.normalColor = Colors::Transparent;
        flat.borderWidth = 0;
        flat.cornerRadius = 6.0f;
        flat.hoverColor = Color(0, 0, 0, 18);
        flat.pressedColor = Color(0, 103, 192, 46);
        b->SetStyle(flat);
        toolSlots.push_back(b);
    }
    body->AddChild(palette);
}

void ArtCreatorWindow::BuildRightPanel() {
    rightPanel = CreateScrollableContainer("ac-right", 0, 0, kRightWidth, 0);
    rightPanel->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    rightPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    rightPanel->SetPadding(6);
    rightPanel->SetBackgroundColor(Color(244, 244, 246, 255));

    // ----- icon: on top, and only in icon mode - the icon at its size -----
    BuildIconPanel();

    // ----- colour: foreground = fill, background = line -----
    rightPanel->AddChild(PanelTitle("ac-color-title", "Colour (fill / line)"));
    colorPicker = CreateColorPicker("ac-color", fillColour, 0, 0, kRightInner, 300);
    colorPicker->SetUIScale(0.78f);
    colorPicker->SetBackgroundColor(lineColour);
    colorPicker->SetShowAlpha(true);
    // The hue ring is capped by whatever vertical space the controls leave it,
    // so a picker sized by eye draws a wheel narrower than the panel. Ask for
    // the height at which the ring fills the panel's width instead.
    // SetElementSize, not SetSize: SetSize moves only the laid-out bounds, and
    // the panel's next layout pass puts back the size the picker was created
    // with. The declared height is also the picker's flex basis.
    colorPicker->SetElementSize(Size2Df(kRightInner, colorPicker->PreferredHeightForWidth(kRightInner)));
    // Not stretched: the panel's content box is wider than what is visible
    // beside the scrollbar, and a stretched picker lays its hex field and
    // channel values out into the part that is covered.
    colorPicker->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Start);
    colorPicker->onColorChanged = [this](const Color& c) { fillColour = c; ApplyFillColour(c, true); };
    colorPicker->onColorChanging = [this](const Color& c) { fillColour = c; ApplyFillColour(c, false); };
    colorPicker->onBackgroundChanged = [this](const Color& c) { lineColour = c; ApplyLineColour(c, true); };
    colorPicker->onBackgroundChanging = [this](const Color& c) { lineColour = c; ApplyLineColour(c, false); };
    rightPanel->AddChild(colorPicker);

    swatches = CreateColorSwatchBar("ac-swatches", 0, 0, kRightInner, 44);
    swatches->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    swatches->onColorSelected = [this](const Color& c) {
        fillColour = c;
        colorPicker->SetForegroundColor(c, false);
        ApplyFillColour(c, true);
    };
    // Right click on a swatch sets the line (background) colour.
    swatches->onColorAdjustSelected = [this](const Color& c) {
        colorPicker->SetBackgroundColor(c, true);
    };
    rightPanel->AddChild(swatches);

    // ----- tool options -----
    // Straight under the colours, where they are visible without scrolling:
    // the Quick Shape corners and star depth used to sit below the fold.
    optionsTitle = PanelTitle("ac-options-title", "Tool Options");
    rightPanel->AddChild(optionsTitle);
    optionsPanel = std::make_shared<UltraCanvasContainer>("ac-options", 0, 0, kRightInner, 0);
    optionsPanel->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    optionsPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
    rightPanel->AddChild(optionsPanel);

    // ----- fill ramp -----
    rightPanel->AddChild(PanelTitle("ac-fill-title", "Fill"));
    ramp = CreateGradientEditor("ac-ramp", 0, 0, static_cast<int>(kRightInner), 46);
    ramp->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    ramp->onStopsChanged = [this]() { ApplyRamp(true); };
    ramp->onStopsChanging = [this]() { ApplyRamp(false); };
    ramp->onSelectionChanged = [this](int i) {
        if (i < 0 || syncingColours) return;
        syncingColours = true;
        colorPicker->SetForegroundColor(ramp->GetSelectedColor(), false);
        syncingColours = false;
    };
    rightPanel->AddChild(ramp);

    // ----- line gallery -----
    rightPanel->AddChild(PanelTitle("ac-line-title", "Line"));
    linePanel = std::make_shared<UltraCanvasContainer>("ac-line", 0, 0, kRightInner, 0);
    linePanel->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    linePanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
    rightPanel->AddChild(linePanel);
    RebuildLinePanel();

    // ----- layers -----
    rightPanel->AddChild(PanelTitle("ac-layers-title", "Layers"));
    layersPanel = std::make_shared<UltraCanvasContainer>("ac-layers", 0, 0, kRightInner, 0);
    layersPanel->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    layersPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);

    auto buttons = std::make_shared<UltraCanvasToolbar>("ac-layer-buttons", 0, 0, 0, 30);
    ToolbarAppearance app = buttons->GetAppearance();
    app.showIconLabels = false;
    app.backgroundColor = Color(244, 244, 246, 255);
    buttons->SetAppearance(app);
    buttons->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    buttons->AddButton("ac-l-add", "", IconPath("layer-add.svg"), [this]() { CmdLayerAdd(); })->SetTooltip("New layer");
    buttons->AddButton("ac-l-del", "", IconPath("layer-delete.svg"), [this]() { CmdLayerDelete(); })->SetTooltip("Delete layer");
    buttons->AddButton("ac-l-up", "", IconPath("layer-up.svg"), [this]() { CmdLayerMove(1); })->SetTooltip("Move up");
    buttons->AddButton("ac-l-down", "", IconPath("layer-down.svg"), [this]() { CmdLayerMove(-1); })->SetTooltip("Move down");
    layersPanel->AddChild(buttons);

    layerRows = std::make_shared<UltraCanvasContainer>("ac-layer-rows", 0, 0, 0, 0);
    layerRows->layout.SetFlexColumn().SetFlexGap(2).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    layerRows->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    layersPanel->AddChild(layerRows);
    rightPanel->AddChild(layersPanel);

    body->AddChild(rightPanel);
}

void ArtCreatorWindow::BuildStatusBar() {
    statusBar = std::make_shared<UltraCanvasContainer>("ac-status", 0, 0, 0, kStatusHeight);
    statusBar->layout.SetFlexRow().SetFlexGap(16).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    statusBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    statusBar->SetPadding(0, 8);
    statusBar->SetBackgroundColor(Color(228, 228, 232, 255));
    auto mk = [&](const std::string& id, float w, bool grow) {
        auto l = CreateLabel(id, 0, 0, w, kStatusHeight - 2, "");
        l->SetFontSize(11);
        l->layoutItem.SetFlexGrow(grow ? 1.0f : 0.0f).SetFlexShrink(grow ? 1.0f : 0.0f);
        statusBar->AddChild(l);
        return l;
    };
    statusPos = mk("ac-st-pos", 150, false);
    statusZoom = mk("ac-st-zoom", 70, false);
    statusSelection = mk("ac-st-sel", 220, false);
    statusHint = mk("ac-st-hint", 0, true);
    window->AddChild(statusBar);
}

void ArtCreatorWindow::InstallShortcuts() {
    window->InstallEventFilter("ac-shortcuts", [this](const UCEvent& e) -> bool {
        if (e.type != UCEventType::KeyDown) return false;
        if (window->GetActivePopupElement()) return false;
        if (FocusIsTextEntry(window.get())) return false;
        const UCKeys k = e.virtualKey;
        if (e.ctrl) {
            switch (k) {
                case UCKeys::N:
                    if (e.alt) CmdNewWindow(); else if (e.shift) CmdLayerAdd(); else CmdNew();
                    return true;
                case UCKeys::O: CmdOpen(); return true;
                case UCKeys::S: if (e.shift) CmdSaveAs(); else CmdSave(); return true;
                case UCKeys::E: CmdExport(); return true;
                case UCKeys::Q: CmdQuit(); return true;
                case UCKeys::Z: if (e.shift) CmdRedo(); else CmdUndo(); return true;
                case UCKeys::Y: CmdRedo(); return true;
                case UCKeys::X: CmdCut(); return true;
                case UCKeys::C: CmdCopy(); return true;
                case UCKeys::V: CmdPaste(); return true;
                case UCKeys::D: CmdDuplicate(); return true;
                case UCKeys::A: if (e.shift) CmdSelectNone(); else CmdSelectAll(); return true;
                case UCKeys::G: CmdGroup(); return true;
                case UCKeys::U: CmdUngroup(); return true;
                case UCKeys::F: CmdReorder(e.shift ? ZOrderMove::Forward : ZOrderMove::ToFront); return true;
                case UCKeys::B: CmdReorder(e.shift ? ZOrderMove::Backward : ZOrderMove::ToBack); return true;
                case UCKeys::Key0: canvas->ZoomToPage(); return true;
                case UCKeys::Key1: canvas->ZoomToActual(); return true;
                case UCKeys::P: if (!e.shift) break; CmdSnapToPixels(); return true;
                default: break;
            }
            return false;
        }
        if (k == UCKeys::Delete && activeTool != ArtToolId::ShapeEditor) { CmdDelete(); return true; }
        // single-letter tool keys
        if (!e.alt && !e.shift && k >= UCKeys::A && k <= UCKeys::Z) {
            for (const auto& t : tools)
                if (t->shortcutKey && t->shortcutKey == static_cast<char>(k)) { SelectTool(t->id); return true; }
        }
        return false;
    }, { UCEventType::KeyDown });
}

// ===========================================================================
// DOCUMENT
// ===========================================================================

void ArtCreatorWindow::NewDrawing(const ArtCreatorPageResult& page) {
    lastPage = page;
    auto doc = std::make_shared<VectorDocument>();
    const double w = page.landscape ? std::max(page.WidthPoints(), page.HeightPoints()) : std::min(page.WidthPoints(), page.HeightPoints());
    const double h = page.landscape ? std::min(page.WidthPoints(), page.HeightPoints()) : std::max(page.WidthPoints(), page.HeightPoints());
    doc->Size = Size2Dd{w, h};
    doc->ViewBox = Rect2Dd{0, 0, w, h};
    doc->AddLayer("Layer 1");
    EnsureIds(*doc);
    SetDocument(doc, "");
    canvas->SetRulerUnit(page.PointsPerUnit(), ArtCreatorPageResult::UnitSymbol(page.unit));
    SetIconSize(0);
}

void ArtCreatorWindow::SetDocument(std::shared_ptr<VectorDocument> doc, const std::string& path) {
    document = std::move(doc);
    documentPath = path;
    modified = false;
    activeLayerIndex = std::max(0, static_cast<int>(document->Layers.size()) - 1);
    selection->Clear();
    history.SetDocument(document);
    canvas->SetDocument(document);
    canvas->ClearGuides();
    if (document->ViewBox.width <= 0 || document->ViewBox.height <= 0)
        document->ViewBox = Rect2Dd{0, 0, document->Size.width, document->Size.height};
    canvas->ZoomToPage();
    RebuildLayerPanel();
    UpdateUndoButtons();
    UpdateTitle();
    UpdateStatus();
}

// Every vector format goes through the FileLoader, which reads into and
// writes from the shared VectorStorage model with the Vector plugin's
// converters (registered in main.cpp). Without the plugin the lists are empty
// and Open / Save say so.
std::vector<std::string> ArtCreatorWindow::OpenableExtensions() {
    return UltraCanvasFileLoader::GetVectorLoadExtensions();
}

std::vector<std::string> ArtCreatorWindow::SaveableExtensions() {
    return UltraCanvasFileLoader::GetVectorSaveExtensions();
}

bool ArtCreatorWindow::OpenFile(const std::string& path) {
    std::string error;
    std::vector<std::string> warnings;
    auto doc = UltraCanvasFileLoader::LoadVectorDocument(path, error, &warnings);
    if (!doc) {
        std::string msg = error + "\n" + path;
        const auto readable = OpenableExtensions();
        if (!readable.empty() && !UltraCanvasFileLoader::CanLoadVectorDocument(path)) {
            msg += "\nThis build opens:";
            for (const auto& e : readable) msg += " " + e;
        }
        UltraCanvasDialogManager::ShowError(msg, "Open", nullptr, window.get());
        return false;
    }
    if (doc->Layers.empty()) doc->AddLayer("Layer 1");
    EnsureIds(*doc);
    SetDocument(doc, path);
    canvas->SetRulerUnit(lastPage.PointsPerUnit(), ArtCreatorPageResult::UnitSymbol(lastPage.unit));
    // A square page of one of the icon sizes - 24 x 24, the way icons are
    // saved as SVG - opens in icon mode at that size.
    int openedIcon = 0;
    const Rect2Dd vb = document->ViewBox;
    for (int n : ArtIconSizes())
        if (std::fabs(vb.width - n) < 1e-6 && std::fabs(vb.height - n) < 1e-6) openedIcon = n;
    SetIconSize(openedIcon);
    if (statusHint) {
        std::string s = "Opened " + FileNameOf(path);
        if (!warnings.empty()) s += "  (" + std::to_string(warnings.size()) + " reader note" + (warnings.size() == 1 ? "" : "s") + ": " + warnings.front() + ")";
        statusHint->SetText(s);
    }
    return true;
}

bool ArtCreatorWindow::SaveToPath(const std::string& path, const FileSaveImageSize& size) {
    if (!document) return false;
    std::string error;
    std::vector<std::string> warnings;
    // Every type through FileLoader: a vector format through its writer, a
    // bitmap one by drawing the page at `size` with the format's settings.
    if (!UltraCanvasFileLoader::SaveVectorDocument(*document, path, size, error, &warnings)) {
        UltraCanvasDialogManager::ShowError(error + "\n" + path, "Save", nullptr, window.get());
        return false;
    }
    // Only a format that keeps the drawing editable becomes the document's file.
    if (UltraCanvasFileLoader::CanLoadVectorDocument(path)) { documentPath = path; modified = false; }
    UpdateTitle();
    if (statusHint && UltraCanvasFileLoader::CanSaveImage(path)) {
        const Size2Di pixels = size.IsOriginal() ? UltraCanvasFileLoader::GetVectorDocumentPixelSize(*document)
                                                 : Size2Di(size.width, size.height);
        statusHint->SetText("Exported " + FileNameOf(path) + " at " + std::to_string(pixels.width) + " x " +
                            std::to_string(pixels.height) + " px");
    } else if (statusHint) {
        std::string s = "Saved " + FileNameOf(path);
        if (!warnings.empty()) s += "  (" + std::to_string(warnings.size()) + " writer note" + (warnings.size() == 1 ? "" : "s") + ": " + warnings.front() + ")";
        statusHint->SetText(s);
    }
    return true;
}

void ArtCreatorWindow::ShowDialog(const std::shared_ptr<UltraCanvasWindow>& dialog) {
    if (!dialog) return;
    dialog->Create();
    if (window) {
        dialog->SetTransientParent(window.get());
        dialog->CenterOnParent(window.get());
    }
    dialog->Show();
}

void ArtCreatorWindow::ConfirmDiscard(const std::string& question, const std::function<void()>& proceed) {
    UltraCanvasDialogManager::ShowConfirmation(question, "ArtCreator", [proceed](bool yes) { if (yes) proceed(); }, window.get());
}

void ArtCreatorWindow::OnDocumentChanged() {
    modified = true;
    if (selection && document) selection->Rebind(*document);
    if (activeLayerIndex >= static_cast<int>(document->Layers.size())) activeLayerIndex = std::max(0, static_cast<int>(document->Layers.size()) - 1);
    canvas->Refresh();
    RebuildLayerPanel();
    UpdateUndoButtons();
    UpdateTitle();
    UpdateStatus();
    RefreshIconPreview();
    if (auto* t = ActiveTool()) t->OnSelectionChanged(toolContext);
}

void ArtCreatorWindow::OnSelectionChanged() {
    SyncColourPanelFromSelection();
    SyncLinePanelFromSelection();
    if (auto* t = ActiveTool()) t->OnSelectionChanged(toolContext);
    UpdateStatus();
}

void ArtCreatorWindow::UpdateTitle() {
    if (!window) return;
    std::string t = FileNameOf(documentPath);
    if (modified) t = "*" + t;
    window->SetWindowTitle(t + " - ArtCreator " ARTCREATOR_VERSION);
}

void ArtCreatorWindow::UpdateStatus() {
    if (!document || !statusPos) return;
    char buf[160];
    // In icon mode positions and sizes are in the icon's pixels.
    const bool icon = iconSize > 0 && IconPixel() > 0;
    const double k = icon ? IconPixel() : lastPage.PointsPerUnit();
    const char* unit = icon ? "px" : ArtCreatorPageResult::UnitSymbol(lastPage.unit);
    if (pointerInside) std::snprintf(buf, sizeof(buf), "%.1f, %.1f %s", pointerX / k, pointerY / k, unit);
    else std::snprintf(buf, sizeof(buf), "--, --");
    statusPos->SetText(buf);
    std::snprintf(buf, sizeof(buf), "%.0f %%", canvas->GetZoom() * 100.0);
    statusZoom->SetText(buf);
    if (selection->Empty()) std::snprintf(buf, sizeof(buf), "Nothing selected");
    else if (selection->Count() == 1) {
        const Rect2Dd b = selection->Bounds();
        std::snprintf(buf, sizeof(buf), "1 %s  %.1f x %.1f %s", ElementKindName(*selection->First()), b.width / k, b.height / k, unit);
    } else std::snprintf(buf, sizeof(buf), "%zu objects selected", selection->Count());
    statusSelection->SetText(buf);
}

void ArtCreatorWindow::UpdateUndoButtons() {
    if (undoButton) {
        undoButton->SetDisabled(!history.CanUndo());
        undoButton->SetTooltip(history.CanUndo() ? "Undo " + history.UndoLabel() + " (Ctrl+Z)" : "Undo (Ctrl+Z)");
    }
    if (redoButton) {
        redoButton->SetDisabled(!history.CanRedo());
        redoButton->SetTooltip(history.CanRedo() ? "Redo " + history.RedoLabel() + " (Ctrl+Y)" : "Redo (Ctrl+Y)");
    }
}

std::shared_ptr<VectorLayer> ArtCreatorWindow::ActiveLayer() const {
    if (!document || document->Layers.empty()) return nullptr;
    const int i = std::clamp(activeLayerIndex, 0, static_cast<int>(document->Layers.size()) - 1);
    return document->Layers[i];
}

void ArtCreatorWindow::SetActiveLayer(int index) {
    if (!document) return;
    activeLayerIndex = std::clamp(index, 0, std::max(0, static_cast<int>(document->Layers.size()) - 1));
    RebuildLayerPanel();
}

void ArtCreatorWindow::Reselect(const std::vector<std::string>& ids) {
    if (!document) return;
    std::vector<ElementPtr> fresh;
    for (const auto& id : ids) if (auto e = document->FindElementById(id)) fresh.push_back(e);
    selection->Set(fresh);
}

// ===========================================================================
// FILE COMMANDS
// ===========================================================================

void ArtCreatorWindow::CmdNew() {
    auto dlg = std::make_shared<ArtCreatorPageDialog>(lastPage, false);
    dlg->onAccept = [this](const ArtCreatorPageResult& r) {
        auto go = [this, r]() { NewDrawing(r); };
        if (modified) ConfirmDiscard("Discard the unsaved changes and start a new drawing?", go);
        else go();
    };
    ShowDialog(dlg);
}

void ArtCreatorWindow::CmdNewWindow() { OpenWindow({}); }

void ArtCreatorWindow::CmdOpen() {
    const auto exts = OpenableExtensions();
    if (exts.empty()) { OpenFile(""); return; }
    FileDialogOptions opts;
    opts.SetTitle("Open drawing")
        .AddFilter("Drawings", exts)
        .AddFilter("SVG", std::vector<std::string>{ "svg" })
        .AddFilter("Xara", std::vector<std::string>{ "xar" })
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetParentWindow(window.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult r, const std::string& path) {
        if (r != DialogResult::OK || path.empty()) return;
        auto go = [this, path]() { OpenFile(path); };
        if (modified) ConfirmDiscard("Discard the unsaved changes and open " + FileNameOf(path) + "?", go);
        else go();
    });
}

void ArtCreatorWindow::CmdSave() {
    if (!document) return;
    if (documentPath.empty()) { CmdSaveAs(); return; }
    SaveToPath(documentPath);
}

void ArtCreatorWindow::CmdSaveAs() {
    if (!document) return;
    FileDialogOptions opts;
    std::string def = documentPath.empty() ? "untitled.xar" : FileNameOf(documentPath);
    opts.SetTitle("Save drawing as")
        .AddFilter("Xara XAR (keeps effects)", std::vector<std::string>{ "xar" })
        .AddFilter("SVG (shapes, fills, text)", std::vector<std::string>{ "svg" })
        .AddFilter("DXF", std::vector<std::string>{ "dxf" })
        .AddFilter("EMF", std::vector<std::string>{ "emf" })
        .AddFilter("WMF", std::vector<std::string>{ "wmf" })
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetDefaultFileName(def)
        .SetParentWindow(window.get());
    UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult r, const std::string& path) {
        // The dialog hands back the name with the chosen type's extension.
        if (r != DialogResult::OK || path.empty()) return;
        SaveToPath(path);
    });
}

void ArtCreatorWindow::CmdExport() {
    if (!document) return;
    FileDialogOptions opts;
    // An icon is exported as a picture at its size; a drawing as a PDF.
    const std::string ext = iconSize > 0 ? ".png" : ".pdf";
    std::string def = documentPath.empty() ? "untitled" + ext : PathToUtf8(PathFromUtf8(documentPath).stem()) + ext;
    opts.SetTitle("Export drawing");
    // FileLoader's types: every vector writer and every bitmap type this
    // build writes, and the page's size, so a bitmap type's Settings offer
    // any size - a drawing has no pixel size and is drawn sharp at any.
    UltraCanvasFileLoader::AddVectorDocumentSaveTypes(opts, *document);
    if (iconSize > 0) opts.SetDrawingSize(iconSize, IconRasterHeight());
    opts.AddFilter("All files", std::vector<std::string>{ "*" })
        .SetDefaultFileName(def)
        .SetParentWindow(window.get());
    UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult r, const std::string& path,
                                                       const FileSaveImageSize& size) {
        // The dialog hands back the name with the chosen type's extension.
        if (r != DialogResult::OK || path.empty()) return;
        FileSaveImageSize at = size;
        if (iconSize > 0 && at.IsOriginal()) { at.width = iconSize; at.height = IconRasterHeight(); }
        SaveToPath(path, at);
    });
}

void ArtCreatorWindow::CmdDocumentSetup() {
    if (!document) return;
    ArtCreatorPageResult cur = lastPage;
    cur.width = document->Size.width / cur.PointsPerUnit();
    cur.height = document->Size.height / cur.PointsPerUnit();
    cur.landscape = document->Size.width > document->Size.height;
    auto dlg = std::make_shared<ArtCreatorPageDialog>(cur, true);
    dlg->onAccept = [this](const ArtCreatorPageResult& r) {
        lastPage = r;
        const double w = r.landscape ? std::max(r.WidthPoints(), r.HeightPoints()) : std::min(r.WidthPoints(), r.HeightPoints());
        const double h = r.landscape ? std::min(r.WidthPoints(), r.HeightPoints()) : std::max(r.WidthPoints(), r.HeightPoints());
        history.Record("Document Setup", [&]() {
            document->Size = Size2Dd{w, h};
            document->ViewBox = Rect2Dd{0, 0, w, h};
        });
        canvas->SetRulerUnit(r.PointsPerUnit(), ArtCreatorPageResult::UnitSymbol(r.unit));
        SetIconSize(iconSize);   // the pixel grid of the new page
        canvas->ZoomToPage();
    };
    ShowDialog(dlg);
}

void ArtCreatorWindow::CmdQuit() {
    int unsaved = 0;
    for (const auto& editor : OpenWindows()) if (editor && editor->modified) ++unsaved;
    auto go = []() { if (auto* app = UltraCanvasApplication::GetInstance()) app->RequestExit(); };
    if (unsaved == 0) { go(); return; }
    ConfirmDiscard(unsaved == 1 ? "A drawing has unsaved changes. Quit anyway?"
                                : std::to_string(unsaved) + " drawings have unsaved changes. Quit anyway?", go);
}

void ArtCreatorWindow::CmdAbout() {
    UltraCanvasDialogManager::ShowInformation(
            std::string("ArtCreator ") + ARTCREATOR_VERSION + "\n\nVector drawing editor on the UltraCanvas framework:\n"
            "VectorStorage::VectorDocument, the VectorEdit layer, UltraCanvasVectorCanvas,\n"
            "and the Vector plugin's converters for SVG, XAR, PDF, AI, EPS, CDR, EMF, WMF, DXF and DWG.",
            "About ArtCreator", nullptr, window.get());
}

// ===========================================================================
// EDIT COMMANDS
// ===========================================================================

void ArtCreatorWindow::CmdUndo() { history.Undo(); }
void ArtCreatorWindow::CmdRedo() { history.Redo(); }

void ArtCreatorWindow::CmdCopy() {
    clipboard.clear();
    const auto ordered = SortByDrawingOrder(selection->Elements());
    for (const auto& e : ordered) if (e) clipboard.push_back(e->Clone());
    pasteCount = 0;
    PutPictureOnSystemClipboard(ordered);
    if (statusHint && !clipboard.empty()) statusHint->SetText(std::to_string(clipboard.size()) + " object" + (clipboard.size() == 1 ? "" : "s") + " copied");
}

// Copy used to keep the objects to itself, so nothing copied here could be
// pasted in another program. The system clipboard now gets their picture -
// what a paint program, a word processor or a chat takes - at a pixel a
// point, the size UltraPaint renders a drawing at.
void ArtCreatorWindow::PutPictureOnSystemClipboard(const std::vector<ElementPtr>& elements) {
    clipboardPng.clear();
    if (!document || elements.empty()) return;
    std::string error;
    auto layer = RasterizeVectorElements(*document, elements, 1.0, error);
    if (!layer) {
        debugOutput << "ArtCreator: the copy has no picture for other programs: " << error << std::endl;
        return;
    }
    ClipboardDib::RgbaImage image;
    image.width = layer->GetWidth();
    image.height = layer->GetHeight();
    image.pixels.resize(static_cast<size_t>(image.width) * image.height * 4);
    for (int y = 0; y < image.height; ++y) {
        std::copy_n(layer->Row(y), static_cast<size_t>(image.width) * 4,
                    image.pixels.data() + static_cast<size_t>(y) * image.width * 4);
    }
    std::vector<uint8_t> png;
    if (!ClipboardDib::EncodePng(image, png)) return;
    if (auto* cb = GetClipboard(); cb && cb->SetImage(png, "image/png")) clipboardPng = std::move(png);
}

void ArtCreatorWindow::CmdCut() {
    if (selection->Empty()) return;
    CmdCopy();
    CmdDelete();
}

// What was copied last wins. A picture another program put on the system
// clipboard becomes an image object; so does an image file copied in a file
// manager (UltraFiler, Explorer, Finder), and a drawing file comes in as a
// group. This window's own copy comes back as the objects themselves, not
// as their picture. Paste used to read only the in-app copy, so a picture
// or a file copied anywhere else pasted nothing.
void ArtCreatorWindow::CmdPaste() {
    if (!document) return;
    if (PasteFromSystemClipboard()) return;
    PasteCopiedElements();
}

bool ArtCreatorWindow::PasteFromSystemClipboard() {
    auto* cb = GetClipboard();
    if (!cb) return false;
    std::vector<uint8_t> bytes;
    std::string format;
    if (cb->GetImage(bytes, format) && !bytes.empty()) {
        if (!clipboard.empty() && bytes == clipboardPng) return false;   // our own copy
        if (PlacePicture(std::move(bytes), format, "the picture")) return true;
    }
    std::vector<std::string> files;
    if (cb->GetFiles(files)) {
        for (const auto& file : files) {
            if (UltraCanvasFileLoader::CanLoadVectorDocument(file)) return PlaceDrawingFile(file);
            if (IsPictureFile(file)) {
                std::ifstream in(PathFromUtf8(file), std::ios::binary);
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (data.empty()) {
                    UltraCanvasDialogManager::ShowError("Could not read " + file, "Paste", nullptr, window.get());
                    return true;   // answered: the file was what the clipboard held
                }
                return PlacePicture(std::move(data), PictureMimeOf(file), FileNameOf(file));
            }
        }
    }
    return false;
}

// A picture as an image object at a pixel a point - shrunk to the page when
// it is larger - in the middle of the page. PNG, JPEG, GIF and WebP are
// embedded as they are; anything else is turned into PNG first, so a saved
// SVG carries a picture every reader shows.
bool ArtCreatorWindow::PlacePicture(std::vector<uint8_t> bytes, std::string mimeType, const std::string& what) {
    if (!document || bytes.empty()) return false;
    if (mimeType != "image/png" && mimeType != "image/jpeg" && mimeType != "image/gif" && mimeType != "image/webp") {
#ifdef HAS_LIBVIPS
        try {
            std::vector<uint8_t> png = PixelFX::FileIO::SaveToBuffer(PixelFX::FileIO::LoadFromMemory(bytes, ""), ".png");
            if (!png.empty()) {
                bytes = std::move(png);
                mimeType = "image/png";
            }
        } catch (...) {}
#endif
    }
    auto picture = UCImage::LoadFromMemory(bytes);
    if (!picture || picture->GetWidth() <= 0 || picture->GetHeight() <= 0) {
        if (statusHint) statusHint->SetText("Could not read " + what + " on the clipboard");
        return false;
    }

    double w = picture->GetWidth(), h = picture->GetHeight();
    const Rect2Dd page = document->ViewBox;
    if (page.width > 0 && page.height > 0 && (w > page.width || h > page.height)) {
        const double k = std::min(page.width / w, page.height / h);
        w *= k;
        h *= k;
    }
    auto image = std::make_shared<VectorImage>();
    image->Bounds = Rect2Dd(page.x + (page.width - w) / 2, page.y + (page.height - h) / 2, w, h);
    image->MimeType = mimeType;
    image->Source = "data:" + mimeType + ";base64," + Base64Encode(bytes, false);
    PlaceElement(image, "Paste picture");
    if (statusHint) {
        statusHint->SetText("Pasted " + what + " (" + std::to_string(picture->GetWidth()) + " x " +
                            std::to_string(picture->GetHeight()) + " pixels)");
    }
    return true;
}

// A drawing file copied in a file manager: its visible layers' objects, as
// one group in the middle of the page. Gradients, clip paths and symbols it
// defines come along unless the drawing already has one of that name.
bool ArtCreatorWindow::PlaceDrawingFile(const std::string& path) {
    std::string error;
    auto incoming = UltraCanvasFileLoader::LoadVectorDocument(path, error);
    if (!incoming) {
        UltraCanvasDialogManager::ShowError(error + "\n" + path, "Paste", nullptr, window.get());
        return true;   // answered: the file was what the clipboard held
    }
    auto group = std::make_shared<VectorGroup>();
    for (const auto& layer : incoming->Layers) {
        if (!layer || !layer->Visible) continue;
        for (const auto& child : layer->Children) if (child) group->AddChild(child);
    }
    if (group->Children.empty()) {
        if (statusHint) statusHint->SetText(FileNameOf(path) + " has nothing to paste");
        return true;
    }
    RenewIds(group);
    // centre what it draws on the page
    const Rect2Dd box = group->GetBoundingBox();
    const Rect2Dd page = document->ViewBox;
    if (box.width >= 0 && box.height >= 0) {
        TranslateElements({group}, page.x + (page.width - box.width) / 2 - box.x,
                                   page.y + (page.height - box.height) / 2 - box.y);
    }
    PlaceElement(group, "Paste drawing", [&]() {
        for (const auto& [id, definition] : incoming->Definitions) {
            if (definition && !document->Definitions.count(id)) document->Definitions[id] = definition;
        }
    });
    if (statusHint) statusHint->SetText("Pasted " + FileNameOf(path));
    return true;
}

// One undo step that adds the element to the active layer and selects it.
void ArtCreatorWindow::PlaceElement(const ElementPtr& element, const std::string& label,
                                    const std::function<void()>& alongside) {
    auto layer = ActiveLayer();
    if (!layer || !element) return;
    if (element->Id.empty()) element->Id = GenerateId();
    history.Record(label, [&]() {
        if (alongside) alongside();
        layer->AddChild(element);
    });
    Reselect({element->Id});
}

void ArtCreatorWindow::PasteCopiedElements() {
    if (clipboard.empty() || !document) return;
    ++pasteCount;
    std::vector<std::string> ids;
    history.Record("Paste", [&]() {
        auto layer = ActiveLayer();
        if (!layer) return;
        for (const auto& c : clipboard) {
            auto copy = c->Clone();
            copy->Id = GenerateId();
            if (auto* g = dynamic_cast<VectorGroup*>(copy.get())) {
                std::function<void(VectorGroup&)> fix = [&](VectorGroup& grp) {
                    for (auto& k : grp.Children) if (k) { k->Id = GenerateId(); k->Parent = std::dynamic_pointer_cast<VectorGroup>(copy); if (auto* kg = dynamic_cast<VectorGroup*>(k.get())) fix(*kg); }
                };
                fix(*g);
            }
            layer->AddChild(copy);
            TranslateElements({copy}, 12.0 * pasteCount, 12.0 * pasteCount);
            ids.push_back(copy->Id);
        }
    });
    Reselect(ids);
}

void ArtCreatorWindow::CmdDuplicate() {
    if (selection->Empty()) return;
    std::vector<std::string> ids;
    history.Record("Duplicate", [&]() { for (auto& e : DuplicateElements(selection->Elements(), 12, 12)) ids.push_back(e->Id); });
    Reselect(ids);
}

void ArtCreatorWindow::CmdDelete() {
    if (selection->Empty()) return;
    history.Record("Delete", [&]() { DeleteElements(selection->Elements()); });
    selection->Clear();
}

void ArtCreatorWindow::CmdSelectAll() {
    if (!document) return;
    std::vector<ElementPtr> all;
    for (const auto& layer : document->Layers) {
        if (!layer || !layer->Visible || layer->Locked) continue;
        for (const auto& c : layer->Children) if (c) all.push_back(c);
    }
    selection->Set(all);
}

void ArtCreatorWindow::CmdSelectNone() { selection->Clear(); }

void ArtCreatorWindow::CmdReorder(ZOrderMove move) {
    if (selection->Empty()) return;
    auto ids = selection->Ids();
    history.Record(move == ZOrderMove::ToFront ? "Bring to Front" : move == ZOrderMove::ToBack ? "Send to Back"
                   : move == ZOrderMove::Forward ? "Bring Forward" : "Send Backward",
                   [&]() { ReorderElements(selection->Elements(), move); });
    Reselect(ids);
}

void ArtCreatorWindow::CmdGroup() {
    if (selection->Count() < 2) return;
    std::string id;
    history.Record("Group", [&]() { if (auto g = GroupElements(selection->Elements())) id = g->Id; });
    if (!id.empty()) Reselect({id});
}

void ArtCreatorWindow::CmdUngroup() {
    if (selection->Empty()) return;
    std::vector<std::string> ids;
    history.Record("Ungroup", [&]() { for (auto& e : UngroupElements(selection->Elements())) ids.push_back(e->Id); });
    Reselect(ids);
}

// Flips the selection about the centre of its bounds: left-right for
// horizontal, top-bottom for vertical. A scale of -1 on one axis through
// the editing layer, so it composes into each element's Transform like any
// other and the bounds stay where they were.
void ArtCreatorWindow::CmdMirror(bool horizontal) {
    if (selection->Empty()) return;
    auto ids = selection->Ids();
    const Rect2Dd b = selection->Bounds();
    const Point2Dd pivot{b.x + b.width / 2.0, b.y + b.height / 2.0};
    history.Record(horizontal ? "Mirror Horizontally" : "Mirror Vertically", [&]() {
        ScaleElements(selection->Elements(), horizontal ? -1.0 : 1.0, horizontal ? 1.0 : -1.0, pivot);
    });
    Reselect(ids);
}

void ArtCreatorWindow::CmdAlign(AlignMode mode, bool toPage) {
    if (selection->Empty() || (!toPage && selection->Count() < 2) || !document) return;
    auto ids = selection->Ids();
    std::optional<Rect2Dd> ref;
    if (toPage) ref = Rect2Dd(0, 0, document->Size.width, document->Size.height);
    history.Record("Align", [&]() { AlignElements(selection->Elements(), mode, ref); });
    Reselect(ids);
}

void ArtCreatorWindow::CmdDistribute(DistributeMode mode) {
    if (selection->Count() < 3) return;
    auto ids = selection->Ids();
    history.Record("Distribute", [&]() { DistributeElements(selection->Elements(), mode); });
    Reselect(ids);
}

void ArtCreatorWindow::CmdConvertToPath() {
    if (selection->Empty()) return;
    auto ids = selection->Ids();
    history.Record("Convert to Editable Shapes", [&]() { for (auto& e : selection->Elements()) ConvertToPath(e); });
    Reselect(ids);
}

// Xara's Combine Shapes: the result replaces the selection.
void ArtCreatorWindow::CmdCombine(CombineOp op) {
    if (selection->Count() < 2) return;
    static const char* labels[] = { "Add Shapes", "Subtract Shapes", "Intersect Shapes", "Slice Shapes" };
    std::vector<std::string> ids;
    history.Record(labels[static_cast<int>(op)], [&]() {
        for (auto& e : CombineShapes(selection->Elements(), op)) ids.push_back(e->Id);
    });
    if (ids.empty() && toolContext.setStatus) toolContext.setStatus("Combine shapes needs two or more shapes with outlines");
    Reselect(ids);
}

// The front shape of the selection becomes the keyhole the others show
// through.
void ArtCreatorWindow::CmdApplyClipView() {
    if (selection->Count() < 2) return;
    std::string id;
    history.Record("Apply ClipView", [&]() {
        const auto ordered = SortByDrawingOrder(selection->Elements());
        auto parent = ParentOf(ordered.back());
        if (!parent) return;
        auto clip = std::make_shared<VectorClipView>();
        clip->Id = GenerateId("clip");
        clip->Keyholes = 1;
        std::vector<ElementPtr> members;
        members.push_back(ordered.back());
        for (size_t i = 0; i + 1 < ordered.size(); ++i) members.push_back(ordered[i]);
        const int index = IndexInParent(ordered.front());
        ArtToolHelpers::WrapInContainer(clip, parent, index, {});
        int at = 0;
        for (const auto& m : members) ReparentElement(m, clip, at++);
        id = clip->Id;
    });
    if (!id.empty()) Reselect({id});
}

void ArtCreatorWindow::CmdRemoveContainer() {
    if (selection->Empty()) return;
    std::vector<ElementPtr> containers;
    for (const auto& e : selection->Elements())
        if (e && (e->Type == VectorElementType::ClipView || e->Type == VectorElementType::Blend || e->Type == VectorElementType::Mould))
            containers.push_back(e);
    if (containers.empty()) { if (toolContext.setStatus) toolContext.setStatus("Select a clip view, blend or mould to remove"); return; }
    std::vector<std::string> ids;
    history.Record("Remove Container", [&]() { for (auto& e : UngroupElements(containers)) ids.push_back(e->Id); });
    Reselect(ids);
}

void ArtCreatorWindow::CmdRemoveFill() {
    if (selection->Empty()) return;
    auto ids = selection->Ids();
    history.Record("No Fill", [&]() { for (auto& e : selection->Elements()) e->Style.Fill.reset(); });
    Reselect(ids);
}

void ArtCreatorWindow::CmdRemoveLine() {
    if (selection->Empty()) return;
    auto ids = selection->Ids();
    history.Record("No Line", [&]() { for (auto& e : selection->Elements()) e->Style.Stroke.reset(); });
    Reselect(ids);
}

void ArtCreatorWindow::CmdLineWidth(float width) {
    toolOptions.strokeWidth = width;
    if (selection->Empty()) return;
    auto ids = selection->Ids();
    history.Record("Line Width", [&]() {
        for (auto& e : selection->Elements()) {
            if (!e->Style.Stroke.has_value()) { StrokeData st; st.Fill = lineColour; e->Style.Stroke = st; }
            e->Style.Stroke->Width = width;
        }
    });
    Reselect(ids);
}

// A line width in pixels: in icon mode the icon's own, so the line covers
// whole rows of the pixels it is shown at; otherwise the page's 96 DPI pixel
// (0.75 pt), the px of Document Setup.
void ArtCreatorWindow::CmdLineWidthPixels(int pixels) {
    const double pixel = IconPixel() > 0 ? IconPixel() : 72.0 / 96.0;
    CmdLineWidth(static_cast<float>(pixels * pixel));
    if (!statusHint) return;
    std::string hint = "Line width " + std::to_string(pixels) + " px";
    // A line that turns odd (or even) wants the other half of the grid.
    if (IconPixel() > 0 && !selection->Empty())
        hint += pixels % 2 ? " - on pixel centres it is sharp: Snap to Pixels (Ctrl+Shift+P) moves it there"
                           : " - on pixel edges it is sharp: Snap to Pixels (Ctrl+Shift+P) moves it there";
    statusHint->SetText(hint);
}

// ===========================================================================
// LAYER COMMANDS
// ===========================================================================

void ArtCreatorWindow::CmdLayerAdd() {
    if (!document) return;
    const int n = static_cast<int>(document->Layers.size());
    history.Record("New Layer", [&]() {
        auto layer = document->AddLayer("Layer " + std::to_string(n + 1));
        layer->Id = GenerateId("layer");
    });
    SetActiveLayer(static_cast<int>(document->Layers.size()) - 1);
}

void ArtCreatorWindow::CmdLayerDelete() {
    if (!document || document->Layers.size() <= 1) return;
    const int i = activeLayerIndex;
    history.Record("Delete Layer", [&]() {
        if (i >= 0 && i < static_cast<int>(document->Layers.size())) document->Layers.erase(document->Layers.begin() + i);
    });
    selection->Clear();
    SetActiveLayer(std::max(0, i - 1));
}

void ArtCreatorWindow::CmdLayerMove(int delta) {
    if (!document) return;
    const int i = activeLayerIndex, j = i + delta;
    if (j < 0 || j >= static_cast<int>(document->Layers.size())) return;
    history.Record(delta > 0 ? "Move Layer Up" : "Move Layer Down", [&]() { std::swap(document->Layers[i], document->Layers[j]); });
    SetActiveLayer(j);
}

void ArtCreatorWindow::CmdLayerToggle(bool visible) {
    auto layer = ActiveLayer();
    if (!layer) return;
    history.Record(visible ? "Show Layer" : "Hide Layer", [&]() { layer->Visible = visible; });
    if (!visible) selection->Clear();
}

// ===========================================================================
// TOOLS AND PANELS
// ===========================================================================

ArtTool* ArtCreatorWindow::ActiveTool() const {
    for (const auto& t : tools) if (t->id == activeTool) return t.get();
    return nullptr;
}

void ArtCreatorWindow::SelectTool(ArtToolId id) {
    if (auto* old = ActiveTool()) { if (old->id != id) old->Deactivate(toolContext); }
    activeTool = id;
    ArtTool* t = ActiveTool();
    // The slot of the tool's group shows it and is pressed; the others not.
    for (const auto& slot : toolSlots) {
        const bool on = slot->SetCurrentItem(ToolItemId(id));
        if (slot->IsPressed() != on) slot->SetPressed(on);
    }
    if (!t) return;
    t->Activate(toolContext);
    canvas->SetToolCursor(t->Cursor());
    canvas->SetHandleMode(VectorHandleMode::Scale);
    canvas->SetShowSelection(id != ArtToolId::ShapeEditor);
    if (statusHint) statusHint->SetText(t->name + ": " + t->hint);
    RebuildToolOptions();
    canvas->Refresh();
}

void ArtCreatorWindow::RebuildToolOptions() {
    if (!optionsPanel) return;
    optionsPanel->ClearChildren();
    ArtTool* t = ActiveTool();
    if (!t) return;
    optionsTitle->SetText(t->name);
    t->BuildOptions(toolContext, *optionsPanel, [this]() { canvas->Refresh(); });
    float h = 0;
    for (const auto& c : optionsPanel->GetChildren()) h += c->GetHeight() + 3;
    optionsPanel->SetBounds(Rect2Df(0, 0, kRightInner, h));
    rightPanel->RequestRedraw();
}

// The Line panel: arrowheads, a width profile and a brush for new lines
// and for the selection.
void ArtCreatorWindow::RebuildLinePanel() {
    if (!linePanel) return;
    using namespace ArtOptionWidgets;
    linePanel->ClearChildren();
    ArtToolOptions& o = toolOptions;
    AddSliderRow(*linePanel, "ac-ln-width", "Width", 0, 40, o.strokeWidth, 0.5f, false, [this, &o](float v) {
        o.strokeWidth = v;
        if (selection->Empty()) return;
        auto ids = selection->Ids();
        history.Record("Line Width", [&]() {
            for (auto& e : selection->Elements()) {
                if (!e->Style.Stroke.has_value()) { StrokeData st; st.Fill = lineColour; e->Style.Stroke = st; }
                e->Style.Stroke->Width = v;
            }
        }, true);
        Reselect(ids);
    });
    AddDropdown(*linePanel, "ac-ln-start", "Start", ArtLineGallery::ArrowheadNames(), o.lineStartArrow,
                [this, &o](int i) { o.lineStartArrow = i; ApplyLineGallery("Start Arrowhead"); });
    AddDropdown(*linePanel, "ac-ln-end", "End", ArtLineGallery::ArrowheadNames(), o.lineEndArrow,
                [this, &o](int i) { o.lineEndArrow = i; ApplyLineGallery("End Arrowhead"); });
    AddSliderRow(*linePanel, "ac-ln-arrow", "Arrow size", 0.5f, 4.0f, o.lineArrowScale, 0.25f, false,
                 [this, &o](float v) { o.lineArrowScale = v; ApplyLineGallery("Arrowhead Size"); });
    AddDropdown(*linePanel, "ac-ln-profile", "Profile", ArtLineGallery::ProfileNames(), o.lineProfile,
                [this, &o](int i) { o.lineProfile = i; ApplyLineGallery("Width Profile"); });
    AddDropdown(*linePanel, "ac-ln-brush", "Brush", ArtLineGallery::BrushNames(), o.lineBrush,
                [this, &o](int i) { o.lineBrush = i; ApplyLineGallery("Brush"); });
    float h = 0;
    for (const auto& c : linePanel->GetChildren()) h += c->GetHeight() + 3;
    linePanel->SetBounds(Rect2Df(0, 0, kRightInner, h));
    rightPanel->RequestRedraw();
}

void ArtCreatorWindow::ApplyLineGallery(const std::string& label) {
    if (syncingColours || selection->Empty()) return;
    auto ids = selection->Ids();
    history.Record(label, [&]() {
        for (auto& e : selection->Elements()) {
            if (!e) continue;
            if (!e->Style.Stroke.has_value()) { StrokeData st; st.Fill = lineColour; st.Width = toolOptions.strokeWidth; e->Style.Stroke = st; }
            ArtLineGallery::ApplyToStroke(*e->Style.Stroke, toolOptions);
        }
    }, true);
    Reselect(ids);
}

void ArtCreatorWindow::SyncLinePanelFromSelection() {
    if (!linePanel || selection->Count() != 1) return;
    auto e = selection->First();
    if (!e || !e->Style.Stroke.has_value()) return;
    const bool wasSyncing = syncingColours;
    syncingColours = true;
    toolOptions.strokeWidth = e->Style.Stroke->Width;
    ArtLineGallery::ReadFromStroke(*e->Style.Stroke, toolOptions);
    RebuildLinePanel();
    syncingColours = wasSyncing;
}

void ArtCreatorWindow::RebuildLayerPanel() {
    if (!layerRows || !document) return;
    layerRows->ClearChildren();
    const int n = static_cast<int>(document->Layers.size());
    for (int i = n - 1; i >= 0; --i) {
        auto layer = document->Layers[i];
        if (!layer) continue;
        auto row = std::make_shared<UltraCanvasContainer>("ac-lrow-" + std::to_string(i), 0, 0, 0, 26);
        row->layout.SetFlexRow().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto vis = std::make_shared<UltraCanvasCheckbox>("ac-lvis-" + std::to_string(i), 0, 0, 24, 24, "");
        vis->SetChecked(layer->Visible);
        vis->SetTooltip("Visible");
        vis->onStateChanged = [this, i](CheckedState, CheckedState s) {
            if (i >= static_cast<int>(document->Layers.size())) return;
            auto l = document->Layers[i];
            const bool on = s == CheckedState::Checked;
            history.Record(on ? "Show Layer" : "Hide Layer", [&]() { l->Visible = on; });
        };
        vis->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->AddChild(vis);
        auto lock = std::make_shared<UltraCanvasCheckbox>("ac-llock-" + std::to_string(i), 0, 0, 24, 24, "");
        lock->SetChecked(layer->Locked);
        lock->SetTooltip("Locked");
        lock->onStateChanged = [this, i](CheckedState, CheckedState s) {
            if (i >= static_cast<int>(document->Layers.size())) return;
            auto l = document->Layers[i];
            const bool on = s == CheckedState::Checked;
            history.Record(on ? "Lock Layer" : "Unlock Layer", [&]() { l->Locked = on; });
        };
        lock->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->AddChild(lock);
        auto btn = std::make_shared<UltraCanvasButton>("ac-lsel-" + std::to_string(i), 0, 0, 0, 24, layer->Name.empty() ? "Layer" : layer->Name);
        btn->SetCanToggled(true);
        btn->SetPressed(i == activeLayerIndex);
        ButtonStyle st = btn->GetStyle();
        st.fontSize = 11.0f;
        btn->SetStyle(st);
        btn->onToggle = [this, i](bool) { SetActiveLayer(i); };
        btn->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(btn);
        layerRows->AddChild(row);
    }
    float h = 0;
    for (const auto& c : layersPanel->GetChildren()) h += c->GetHeight() + 3;
    layersPanel->SetBounds(Rect2Df(0, 0, kRightInner, h + n * 28.0f));
    rightPanel->RequestRedraw();
}

void ArtCreatorWindow::SyncColourPanelFromSelection() {
    if (syncingColours || !colorPicker) return;
    syncingColours = true;
    if (selection->Count() == 1) {
        auto e = selection->First();
        if (e && e->Style.Fill.has_value()) {
            if (auto* c = std::get_if<Color>(&*e->Style.Fill)) {
                fillColour = *c;
                colorPicker->SetForegroundColor(*c, false);
                ramp->SetStops({{0.0, *c}, {1.0, *c}});
            } else if (auto* g = std::get_if<GradientData>(&*e->Style.Fill)) {
                if (auto* l = std::get_if<LinearGradientData>(g)) ramp->SetStops(l->Stops);
                else if (auto* r = std::get_if<RadialGradientData>(g)) ramp->SetStops(r->Stops);
            }
        }
        if (e && e->Style.Stroke.has_value()) {
            if (auto* c = std::get_if<Color>(&e->Style.Stroke->Fill)) {
                lineColour = *c;
                colorPicker->SetBackgroundColor(*c);
            }
        }
    }
    syncingColours = false;
}

void ArtCreatorWindow::ApplyFillColour(const Color& c, bool commit) {
    if (syncingColours || selection->Empty()) return;
    auto ids = selection->Ids();
    auto apply = [&]() {
        for (auto& e : selection->Elements()) {
            if (!e) continue;
            bool done = false;
            if (e->Style.Fill.has_value()) {
                if (auto* g = std::get_if<GradientData>(&*e->Style.Fill)) {
                    // A gradient: the colour goes to the ramp's selected stop.
                    const int stop = ramp ? ramp->GetSelectedStop() : -1;
                    auto setStop = [&](std::vector<GradientStop>& stops) { if (stop >= 0 && stop < static_cast<int>(stops.size())) { stops[stop].color = c; done = true; } };
                    if (auto* l = std::get_if<LinearGradientData>(g)) setStop(l->Stops);
                    else if (auto* r = std::get_if<RadialGradientData>(g)) setStop(r->Stops);
                }
            }
            if (!done) e->Style.Fill = c;
        }
    };
    if (commit) { history.Record("Fill Colour", apply, true); Reselect(ids); }
    else { apply(); canvas->Refresh(); }
    if (commit) { syncingColours = true; SyncColourPanelFromSelection(); syncingColours = false; }
}

void ArtCreatorWindow::ApplyLineColour(const Color& c, bool commit) {
    if (syncingColours || selection->Empty()) return;
    auto ids = selection->Ids();
    auto apply = [&]() {
        for (auto& e : selection->Elements()) {
            if (!e) continue;
            if (!e->Style.Stroke.has_value()) { StrokeData st; st.Width = toolOptions.strokeWidth; e->Style.Stroke = st; }
            e->Style.Stroke->Fill = c;
        }
    };
    if (commit) { history.Record("Line Colour", apply, true); Reselect(ids); }
    else { apply(); canvas->Refresh(); }
}

void ArtCreatorWindow::ApplyRamp(bool commit) {
    if (syncingColours || selection->Count() != 1 || !ramp) return;
    auto e = selection->First();
    if (!e) return;
    const std::string id = e->Id;
    auto apply = [&]() {
        auto el = document->FindElementById(id);
        if (!el) return;
        if (el->Style.Fill.has_value()) {
            if (auto* g = std::get_if<GradientData>(&*el->Style.Fill)) {
                if (auto* l = std::get_if<LinearGradientData>(g)) { l->Stops = ramp->GetStops(); return; }
                if (auto* r = std::get_if<RadialGradientData>(g)) { r->Stops = ramp->GetStops(); return; }
            }
        }
        // A flat (or no) fill becomes a linear gradient across the shape.
        LinearGradientData lg;
        lg.Start = {0, 0}; lg.End = {1, 0};
        lg.Stops = ramp->GetStops();
        el->Style.Fill = GradientData{lg};
    };
    if (commit) { history.Record("Gradient", apply, true); Reselect({id}); }
    else { apply(); canvas->Refresh(); }
}

void ArtCreatorWindow::PlaceText(double x, double y, const ArtCreatorTextResult& r) {
    if (r.text.empty() || !document) return;
    auto text = std::make_shared<VectorText>();
    text->Position = {x, y};
    text->BaseStyle.FontFamily = r.font;
    text->BaseStyle.FontSize = static_cast<float>(r.size);
    text->BaseStyle.Weight = r.bold ? FontWeight::Bold : FontWeight::Normal;
    text->BaseStyle.Slant = r.italic ? FontSlant::Italic : FontSlant::Normal;
    text->SetText(r.text);
    text->Style.Fill = fillColour;
    text->Id = GenerateId("text");
    const std::string id = text->Id;
    history.Record("Text", [&]() { if (auto layer = ActiveLayer()) layer->AddChild(text); });
    Reselect({id});
}

// ===========================================================================
// ICON MODE
// ===========================================================================

void ArtCreatorWindow::CmdNewIcon(int pixels) {
    auto go = [this, pixels]() {
        // One document unit a pixel, as icons are saved: an SVG of
        // width="24" height="24" viewBox="0 0 24 24".
        auto doc = std::make_shared<VectorDocument>();
        doc->Size = Size2Dd{static_cast<double>(pixels), static_cast<double>(pixels)};
        doc->ViewBox = Rect2Dd{0, 0, static_cast<double>(pixels), static_cast<double>(pixels)};
        doc->AddLayer("Layer 1");
        EnsureIds(*doc);
        SetDocument(doc, "");
        SetIconSize(pixels);
        canvas->ZoomToPage();
        if (statusHint) statusHint->SetText("New " + std::to_string(pixels) + " px icon: draw on the pixel grid, the panel shows it at its size");
    };
    if (modified) ConfirmDiscard("Discard the unsaved changes and start a new icon?", go);
    else go();
}

double ArtCreatorWindow::IconPixel() const {
    if (iconSize <= 0 || !document || document->ViewBox.width <= 0) return 0;
    return document->ViewBox.width / iconSize;
}

int ArtCreatorWindow::IconRasterHeight() const {
    const double p = IconPixel();
    if (p <= 0) return 0;
    return std::max(1, static_cast<int>(std::lround(document->ViewBox.height / p)));
}

void ArtCreatorWindow::SetIconSize(int pixels) {
    const bool wasOn = iconSize > 0;
    iconSize = std::max(0, pixels);
    if (iconSize > 0 && !wasOn) {
        gridBeforeIcon = canvas->GetGrid();
        snapBeforeIcon = canvas->GetSnapOptions();
    }
    const double p = IconPixel();
    toolContext.iconPixel = p;
    if (iconSize > 0 && p > 0) {
        // Major lines every few pixels, and every pixel once the view is
        // close enough for them to be told apart (the canvas shows minor
        // lines from minPixelSpacing on).
        const int major = iconSize <= 32 ? 4 : iconSize <= 96 ? 8 : 16;
        VectorGridSpec grid = gridBeforeIcon;
        grid.spacing = p * major;
        grid.subdivisions = major;
        grid.visible = iconGrid;
        canvas->SetGrid(grid);
        // The pixel grid is snapped to here (IconEvent), knowing the line
        // width; the canvas's own grid snap would only know pixel edges.
        VectorSnapOptions snap = snapBeforeIcon;
        snap.toGrid = false;
        canvas->SetSnapOptions(snap);
        canvas->SetRulerUnit(p, "px");
        // New lines a whole number of pixels wide.
        const float whole = static_cast<float>(std::max(1.0, std::round(toolOptions.strokeWidth / p)) * p);
        if (std::fabs(whole - toolOptions.strokeWidth) > 1e-4f) {
            toolOptions.strokeWidth = whole;
            RebuildLinePanel();
        }
    } else if (wasOn) {
        canvas->SetGrid(gridBeforeIcon);
        canvas->SetSnapOptions(snapBeforeIcon);
        canvas->SetRulerUnit(lastPage.PointsPerUnit(), ArtCreatorPageResult::UnitSymbol(lastPage.unit));
    }
    if (iconSizeBox) {
        int index = 0;
        const auto& sizes = ArtIconSizes();
        for (size_t i = 0; i < sizes.size(); ++i) if (sizes[i] == iconSize) index = static_cast<int>(i) + 1;
        if (iconSizeBox->GetSelectedIndex() != index) iconSizeBox->SetSelectedIndex(index, false);
    }
    if (iconPanel) iconPanel->SetVisible(iconSize > 0);
    RefreshIconPreview();
    UpdateStatus();
    canvas->Refresh();
}

bool ArtCreatorWindow::IconCentred() const {
    const double p = IconPixel();
    if (p <= 0) return false;
    auto odd = [p](double width) { return (std::max(1L, std::lround(width / p)) % 2) == 1; };
    if (activeTool == ArtToolId::Selector || activeTool == ArtToolId::ShapeEditor) {
        // moving or reshaping what is there: the selection's line decides
        for (const auto& e : selection->Elements())
            if (e && e->Style.Stroke.has_value() && e->Style.Stroke->Width > 0) {
                const double scale = std::sqrt(std::fabs(e->GetGlobalTransform().Determinant()));
                return odd(e->Style.Stroke->Width * (scale > 0 ? scale : 1.0));
            }
        return false;
    }
    return toolOptions.shapeStroke && odd(toolOptions.strokeWidth);
}

VectorPointerEvent ArtCreatorWindow::IconEvent(const VectorPointerEvent& e) const {
    const double p = IconPixel();
    if (p <= 0 || !iconSnap) return e;
    VectorPointerEvent out = e;
    const bool centre = IconCentred();
    out.snapped = Point2Dd(SnapToPixel(e.snapped.x, p, document->ViewBox.x, centre),
                           SnapToPixel(e.snapped.y, p, document->ViewBox.y, centre));
    return out;
}

void ArtCreatorWindow::CmdSnapToPixels() {
    if (!document) return;
    const double p = IconPixel();
    if (p <= 0) {
        if (statusHint) statusHint->SetText("Snap to Pixels works on an icon: choose its size under Icon in the panel");
        return;
    }
    // The selection, or everything on the visible, unlocked layers.
    std::vector<ElementPtr> targets = selection->Elements();
    if (targets.empty())
        for (const auto& layer : document->Layers) {
            if (!layer || !layer->Visible || layer->Locked) continue;
            for (const auto& c : layer->Children) if (c) targets.push_back(c);
        }
    if (targets.empty()) return;
    const auto ids = selection->Ids();
    PixelSnapResult result;
    history.Record("Snap to Pixels", [&]() {
        result = SnapToPixels(targets, p, Point2Dd(document->ViewBox.x, document->ViewBox.y));
    });
    Reselect(ids);
    if (statusHint) {
        std::string msg = "Snapped " + std::to_string(result.aligned) + (result.aligned == 1 ? " shape" : " shapes") +
                          " to the " + std::to_string(iconSize) + " px grid";
        if (result.skipped > 0)
            msg += "; " + std::to_string(result.skipped) + " left as " + (result.skipped == 1 ? "it is" : "they are") +
                   " (text, pictures, rotated shapes)";
        statusHint->SetText(msg);
    }
}

void ArtCreatorWindow::BuildIconPanel() {
    using namespace ArtOptionWidgets;
    // Built once and shown only in icon mode; nothing in it is rebuilt, so
    // the size box can switch icon mode off from its own callback.
    iconPanel = std::make_shared<UltraCanvasContainer>("ac-icon", 0, 0, kRightInner, 0);
    iconPanel->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    iconPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
    iconPanel->AddChild(PanelTitle("ac-icon-title", "Icon"));

    std::vector<std::string> sizes = {"Off"};
    for (int n : ArtIconSizes()) sizes.push_back(std::to_string(n) + " x " + std::to_string(n) + " px");
    AddDropdown(*iconPanel, "ac-icon-size", "Size", sizes, 0, [this](int index) {
        const auto& all = ArtIconSizes();
        SetIconSize(index <= 0 || index > static_cast<int>(all.size()) ? 0 : all[index - 1]);
    });
    if (auto row = std::dynamic_pointer_cast<UltraCanvasContainer>(iconPanel->GetChildren().back()))
        if (!row->GetChildren().empty())
            iconSizeBox = std::dynamic_pointer_cast<UltraCanvasDropdown>(row->GetChildren().back());

    // grid, snapping, marks: one row
    auto toggles = std::make_shared<UltraCanvasContainer>("ac-icon-toggles", 0, 0, 0, 24);
    toggles->layout.SetFlexRow().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    toggles->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto toggle = [&](const std::string& id, const std::string& label, float width, bool on,
                      const std::string& tip, std::function<void(bool)> changed) {
        auto cb = std::make_shared<UltraCanvasCheckbox>(id, 0, 0, width, 24, label);
        cb->SetChecked(on);
        cb->SetTooltip(tip);
        cb->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        cb->onStateChanged = [changed](CheckedState, CheckedState n) { changed(n == CheckedState::Checked); };
        toggles->AddChild(cb);
    };
    toggle("ac-icon-grid", "Grid", 62, iconGrid, "Show the icon's pixel grid on the page", [this](bool on) {
        iconGrid = on;
        VectorGridSpec g = canvas->GetGrid();
        g.visible = on;
        canvas->SetGrid(g);
    });
    toggle("ac-icon-snap", "Snap", 64, iconSnap,
           "Snap the pointer to the pixel grid: lines an odd number of pixels wide to pixel centres, "
           "the rest to pixel edges", [this](bool on) { iconSnap = on; });
    toggle("ac-icon-soft", "Mark soft", 110, iconMarkSoft,
           "Mark the pixels the drawing only partly covers, on the page", [this](bool on) { iconMarkSoft = on; canvas->Refresh(); });
    iconPanel->AddChild(toggles);
    AddButtonRow(*iconPanel, "ac-icon-snapnow", {{"Snap to Pixels", [this]() { CmdSnapToPixels(); }}});

    iconPreview = std::make_shared<ArtIconPreview>("ac-icon-preview", kRightInner);
    iconPreview->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
    iconPanel->AddChild(iconPreview);
    iconCaption = CreateLabel("ac-icon-caption", 0, 0, kRightInner, 46, "");
    iconCaption->SetFontSize(11);
    iconCaption->SetWrap(TextWrap::WrapWord);
    iconCaption->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    iconPanel->AddChild(iconCaption);
    iconPanel->SetVisible(false);
    rightPanel->AddChild(iconPanel);
    LayoutIconPanel();
}

void ArtCreatorWindow::LayoutIconPanel() {
    if (!iconPanel) return;
    float h = 0;
    for (const auto& child : iconPanel->GetChildren())
        if (child && child->IsVisible()) h += child->GetHeight() + 3;
    iconPanel->SetBounds(Rect2Df(0, 0, kRightInner, h));
    iconPanel->InvalidateLayout();
    if (rightPanel) rightPanel->RequestRedraw();
}

void ArtCreatorWindow::RefreshIconPreview() {
    if (!iconPreview) return;
    if (iconSize <= 0 || !document || IconPixel() <= 0) {
        iconRaster.reset();
        iconPreview->SetRaster(nullptr);
        if (iconCaption) iconCaption->SetText("");
        LayoutIconPanel();
        return;
    }
    const int w = iconSize, h = IconRasterHeight();
    auto raster = std::make_shared<ArtIconRaster>(RasterizeIcon(*document, w, h));
    iconRaster = raster;
    iconPreview->SetRaster(raster);
    if (iconCaption) {
        std::string text = std::to_string(w) + " x " + std::to_string(h) + " px: ";
        const size_t soft = raster->softPixels.size();
        if (!raster->error.empty()) text = "Nothing drawn yet: " + raster->error;
        else if (raster->coveredPixels == 0) text += "nothing drawn yet";
        else if (soft == 0) text += "sharp, no soft pixels";
        else text += std::to_string(soft) + (soft == 1 ? " soft pixel" : " soft pixels") +
                     " (red). Curves and slants always leave some; a straight edge should leave none.";
        iconCaption->SetText(text);
    }
    LayoutIconPanel();
    canvas->Refresh();
}

void ArtCreatorWindow::DrawIconOverlay(IRenderContext* ctx, const VectorViewTransform& view) {
    if (!ctx || iconSize <= 0 || !iconMarkSoft || !iconRaster || !document) return;
    const double p = IconPixel();
    if (p <= 0 || view.DocToViewLength(p) < 4) return;   // too small on screen to mark
    const Rect2Dd vb = document->ViewBox;
    ctx->SetFillPaint(Color(230, 40, 40, 64));
    for (const auto& px : iconRaster->softPixels)
        ctx->FillRectangle(view.DocToView(Rect2Dd(vb.x + px.x * p, vb.y + px.y * p, p, p)));
}

} // namespace UltraCanvas
