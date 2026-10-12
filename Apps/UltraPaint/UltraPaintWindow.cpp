// Apps/UltraPaint/UltraPaintWindow.cpp
// UltraPaint main window: composition of the framework elements (menu bar,
// toolbars, paint surface, colour picker, option / layer panels, status
// bar) and every command the menus and shortcuts run, plus the open-window
// registry and the import path a dropped or opened file takes.
// Version: 1.3.0 - the tool palette groups its tools: tools of a kind share one slot
//                  (UltraCanvasToolGroupButton) that shows the last one picked (adaptive)
//                  or the group's main one (static, Settings > Display > Tool palette),
//                  and a bar with the whole group folds out while the pointer rests on it
// Version: 1.2.0 - the mouse pointer magnification glass: a loupe in a corner of the canvas
//                  while the rectangle selection is active, that keeps out of the pointer's
//                  way; Edit > Settings... (Display > Mouse pointer magnification glass)
// Version: 1.1.3 - a double-click on a layer's name renames it in the list (a text
//                  field in its place: Enter or a click elsewhere keeps the name,
//                  Escape the old one); the name buttons say so in their tooltip
// Version: 1.1.2 - the layer list is rebuilt after the click that changed it has been
//                  handled (ScheduleLayerPanelRebuild): rebuilding it under the clicked
//                  row's button freed the button mid-click and crashed
// Version: 1.1.1 - the tool palette shows its tools' 28 px icons (media/icons/ultrapaint/28)
//                  at 28 px, the grid they are drawn on, in 36 px buttons; flat
//                  buttons in the palette and the toolbar; every menu entry has an
//                  icon from the 16 px menu set (media/icons/ultrapaint/16)
// Version: 1.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraPaintWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#ifdef ULTRAPAINT_HAVE_CLIPBOARD_HISTORY
#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasClipboardHistoryView.h"
#include "UltraCanvasPathUtf8.h"
#endif
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasSeparator.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasVectorRaster.h"
#include "UltraCanvasModelRaster.h"
#include "UltraCanvasDebug.h"
#include "../dialogs/UltraCanvasCurvesDialog.h"
#include "../dialogs/UltraCanvasModelViewDialog.h"
#ifdef HAS_LIBVIPS
#include "../dialogs/UltraCanvasImageExportDialog.h"
#include "PixelFX/PixelFX.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

// ULTRAPAINT_VERSION comes from the build alone: CMake reads the first line of
// Docs/UltraPaint/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it as a
// compile definition. No fallback, so a build that lost it fails instead of
// showing a wrong number in the window title.
#ifndef ULTRAPAINT_VERSION
#error "ULTRAPAINT_VERSION is not defined: build through CMake, which reads it from Docs/UltraPaint/CHANGELOG.md"
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {
    // A menu entry with an icon beside its check box.
    MenuItemData WithIcon(MenuItemData item, const std::string& iconPath) {
        item.iconPath = iconPath;
        return item;
    }

    constexpr int   kMenuHeight    = 28;
    constexpr int   kToolbarHeight = 38;
    constexpr int   kPaletteWidth  = 48;
    constexpr int   kToolButton    = 36;   // a tool's button, square, 4 px around its 28 px icon
    constexpr int   kToolIcon      = 28;
    constexpr int   kRightWidth    = 300;
    constexpr int   kRightInner    = 262;   // what is visible next to the scrollbar
    constexpr int   kStatusHeight  = 24;
    constexpr float kRowH          = 24.0f;

    const std::vector<std::string> kImageExtensions = {
        "png", "jpg", "jpeg", "gif", "bmp", "webp", "tiff", "tif", "heic", "heif", "avif", "jxl",
        "tga", "ppm", "pgm", "pbm", "pnm", "psd", "qoi", "ico", "svg", "exr", "hdr", "dng", "cr2", "nef", "arw"
    };

    std::string FileNameOf(const std::string& path) {
        if (path.empty()) return "Untitled";
        return PathToUtf8(PathFromUtf8(path).filename());
    }

    std::string LowerExtOf(const std::string& path) {
        std::string e = PathToUtf8(PathFromUtf8(path).extension());
        if (!e.empty() && e[0] == '.') e.erase(0, 1);
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
        return e;
    }

    // Files the editor will take from a drop: everything the image pipeline
    // decodes, its own projects, any vector artwork this build can rasterize
    // (.svg, .pdf, .dxf, ...) and any 3D model it can read (.stl, .obj,
    // .ply, ... — whatever the Models plugin brought).
    bool IsOpenablePath(const std::string& path) {
        if (UCRasterDocument::IsProjectFile(path)) return true;
        const std::string ext = LowerExtOf(path);
        if (std::find(kImageExtensions.begin(), kImageExtensions.end(), ext) != kImageExtensions.end()) return true;
        return IsVectorGraphicsPath(path) || IsModelGraphicsPath(path);
    }

    // A 3D model: no pixels, and not even a natural size — somebody has to
    // choose a view as well, which is a different question from a drawing's.
    // A project is never one of these even if a plugin claims its extension.
    bool IsModelPath(const std::string& path) {
        if (UCRasterDocument::IsProjectFile(path)) return false;
        return IsModelGraphicsPath(path);
    }

    // Vector artwork: no pixels of its own, so the editor has to be told a
    // size before it can hold it.
    bool IsVectorPath(const std::string& path) {
        if (UCRasterDocument::IsProjectFile(path)) return false;
        if (IsModelGraphicsPath(path)) return false;   // a model is asked differently
        return IsVectorGraphicsPath(path);
    }

    // True for a file the editor cannot simply decode — it has to ask
    // something first. Both of the above qualify.
    bool NeedsImportDialog(const std::string& path) {
        return IsModelPath(path) || IsVectorPath(path);
    }

    // libvips reports a failure as its whole error buffer, which for a missing
    // file is the same line repeated once per loader it tried. A dialog wants
    // the reason, not the log: keep the first few distinct lines.
    std::string ShortError(const std::string& error) {
        std::vector<std::string> lines;
        std::string line;
        for (char c : error) {
            if (c == '\n') {
                if (!line.empty() && std::find(lines.begin(), lines.end(), line) == lines.end()) lines.push_back(line);
                line.clear();
            } else {
                line += c;
            }
        }
        if (!line.empty() && std::find(lines.begin(), lines.end(), line) == lines.end()) lines.push_back(line);
        std::string out;
        for (size_t i = 0; i < lines.size() && i < 3; ++i) {
            if (!out.empty()) out += "\n";
            out += lines[i].size() > 160 ? lines[i].substr(0, 157) + "..." : lines[i];
        }
        if (lines.size() > 3) out += "\n...";
        return out;
    }

    // Resamples a layer, through PixelFX when it is available so a photo
    // scaled into a small canvas does not alias.
    std::shared_ptr<UCRasterLayer> ScaleLayerTo(const UCRasterLayer& src, int w, int h) {
        if (src.GetWidth() <= 0 || src.GetHeight() <= 0) return src.Clone();
#ifdef HAS_LIBVIPS
        try {
            PixelFX::PFXImage img = src.ToPixelFX();
            img = PixelFX::Resample::Resize(img,
                                            static_cast<double>(w) / src.GetWidth(),
                                            static_cast<double>(h) / src.GetHeight(),
                                            PixelFX::Kernel::Lanczos3);
            auto scaled = std::make_shared<UCRasterLayer>();
            if (scaled->FromPixelFX(img) && scaled->GetWidth() == w && scaled->GetHeight() == h) {
                scaled->name = src.name;
                return scaled;
            }
        } catch (...) {
            // fall through to the bilinear path
        }
#endif
        auto copy = src.Clone();
        copy->ResampleBilinear(w, h);
        return copy;
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
}

// ===========================================================================
// LIFECYCLE
// ===========================================================================

UltraPaintWindow::UltraPaintWindow() = default;
UltraPaintWindow::~UltraPaintWindow() = default;

std::string UltraPaintWindow::IconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/ultrapaint/" + file);
}

std::string UltraPaintWindow::MenuIconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/ultrapaint/16/" + file);
}

std::string UltraPaintWindow::ToolIconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/ultrapaint/28/" + file);
}

std::string UltraPaintWindow::TexterIconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/texter/" + file);
}

bool UltraPaintWindow::Initialize(const std::vector<std::string>& paths) {
    WindowConfig config;
    config.title = "UltraPaint " ULTRAPAINT_VERSION;
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

    settings.Load();   // no file yet: the defaults stand
    tools = CreatePaintTools();

    BuildMenuBar();
    BuildToolbar();

    body = std::make_shared<UltraCanvasContainer>("up-body", 0, 0, 0, 0);
    body->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    window->AddChild(body);

    BuildToolPalette();

    surface = CreatePaintSurface("up-surface");
    surface->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    body->AddChild(surface);

    BuildRightPanel();
    BuildMagnifier();
    BuildStatusBar();

    // ----- tool context -----
    toolContext.getDocument = [this]() { return document; };
    toolContext.surface = surface.get();
    toolContext.options = &toolOptions;
    toolContext.foreground = [this]() { return foreground; };
    toolContext.background = [this]() { return background; };
    toolContext.setForeground = [this](const RasterPixel& p) {
        foreground = p;
        if (colorPicker) colorPicker->SetForegroundColor(p.ToColor(), false);
    };
    toolContext.setBackground = [this](const RasterPixel& p) {
        background = p;
        if (colorPicker) colorPicker->SetBackgroundColor(p.ToColor());
    };
    toolContext.setStatus = [this](const std::string& s) { if (statusHint) statusHint->SetText(s); };
    toolContext.requestText = [this](double x, double y) {
        UltraPaintTextResult init;
        init.font = toolOptions.textFont; init.size = toolOptions.textSize; init.bold = toolOptions.textBold;
        auto dlg = std::make_shared<UltraPaintTextDialog>(init);
        dlg->onAccept = [this, x, y](const UltraPaintTextResult& r) {
            toolOptions.textFont = r.font; toolOptions.textSize = r.size; toolOptions.textBold = r.bold;
            PlaceText(x, y, r);
        };
        ShowDialog(dlg);
    };
    toolContext.refreshOptions = [this]() { RebuildToolOptions(); };

    // ----- surface -> tool -----
    surface->onToolPress = [this](const PaintPointerEvent& e) { if (auto* t = ActiveTool()) t->OnPress(toolContext, e); };
    surface->onToolDrag = [this](const PaintPointerEvent& e) {
        pointerX = e.x; pointerY = e.y; pointerInside = e.insideImage;
        if (auto* t = ActiveTool()) t->OnDrag(toolContext, e);
        UpdateStatus();
        UpdateMagnifier(e.x, e.y, e.view);
    };
    surface->onToolRelease = [this](const PaintPointerEvent& e) { if (auto* t = ActiveTool()) t->OnRelease(toolContext, e); };
    surface->onToolHover = [this](const PaintPointerEvent& e) {
        pointerX = e.x; pointerY = e.y; pointerInside = e.insideImage;
        if (auto* t = ActiveTool()) t->OnHover(toolContext, e);
        UpdateStatus();
        UpdateMagnifier(e.x, e.y, e.view);
    };
    surface->onToolDoubleClick = [this](const PaintPointerEvent& e) { if (auto* t = ActiveTool()) t->OnDoubleClick(toolContext, e); };
    surface->onToolKey = [this](const UCEvent& ev) { auto* t = ActiveTool(); return t && t->OnKey(toolContext, ev); };
    surface->onDrawOverlay = [this](IRenderContext* ctx, const PaintViewTransform& v) { if (auto* t = ActiveTool()) t->DrawOverlay(toolContext, ctx, v); };
    surface->onViewChanged = [this]() {
        UpdateStatus();
        // The glass magnifies the view's own zoom, so it follows a zoom change.
        if (magnifier && magnifier->IsVisible() && pointerInside)
            magnifier->SetLoupeView(surface->GetZoom() * settings.magnifierFactor, pointerX, pointerY);
    };
    surface->onFilesDropped = [this](const std::vector<std::string>& files) { HandleDroppedFiles(files); };

    InstallShortcuts();

    window->onWindowClosing = [this]() -> bool {
        if (!document || !document->IsModified()) {
            if (auto dlg = settingsDialog.lock()) dlg->Close();
            return true;
        }
        ConfirmDiscard("The image has unsaved changes. Close anyway?", [this]() {
            document->SetModified(false);
            window->Close();   // comes back through here, and closes the settings window
        });
        return false;
    };

    // ----- first document -----
    if (!paths.empty() && NeedsImportDialog(paths.front())) {
        // Vector artwork needs a raster size, and a 3D model a view as well,
        // before either becomes an image; the dialog that asks waits for
        // Show() — this is also the path a file dropped on the application's
        // icon takes, since the shell starts us with it on the command line.
        pendingImport = paths.front();
        NewImage(lastNewImage);
    } else if (!paths.empty()) {
        OpenFile(paths.front());
        if (!document) NewImage(lastNewImage);
    } else {
        NewImage(lastNewImage);
    }
    SelectTool(PaintToolId::Brush);
    return true;
}

void UltraPaintWindow::Show() {
    if (window) window->Show();
    if (!pendingImport.empty()) {
        const std::string path = pendingImport;
        pendingImport.clear();
        ImportFile(path, false);
    }
}

// ===========================================================================
// WINDOW REGISTRY
// ===========================================================================

std::vector<std::shared_ptr<UltraPaintWindow>>& UltraPaintWindow::OpenWindows() {
    static std::vector<std::shared_ptr<UltraPaintWindow>> windows;
    return windows;
}

int UltraPaintWindow::WindowCount() {
    return static_cast<int>(OpenWindows().size());
}

std::shared_ptr<UltraPaintWindow> UltraPaintWindow::OpenWindow(const std::vector<std::string>& paths) {
    auto editor = std::make_shared<UltraPaintWindow>();
    if (!editor->Initialize(paths)) return nullptr;
    OpenWindows().push_back(editor);
    UltraPaintWindow* raw = editor.get();
    editor->window->onWindowClosed = [raw]() { RetireWindow(raw); };
    editor->Show();

    // A window holds one image, so the rest of a multi-file open each get one
    // of their own. This is what a selection dropped on the application's icon
    // in a dock or taskbar looks like: the desktop expands the launcher's
    // "Exec=UltraPaint %F" with every dropped path at once.
    for (size_t i = 1; i < paths.size(); ++i) OpenWindow({ paths[i] });
    return editor;
}

void UltraPaintWindow::RetireWindow(UltraPaintWindow* closed) {
    // The call comes from inside the window's own PerformClose, and this
    // object owns that window: dropping the last reference here would destroy
    // the window mid-close. Let the event loop come back to it.
    auto drop = [closed]() {
        auto& windows = OpenWindows();
        windows.erase(std::remove_if(windows.begin(), windows.end(),
                                     [closed](const std::shared_ptr<UltraPaintWindow>& w) {
                                         return w.get() == closed;
                                     }),
                      windows.end());
    };
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(drop);
    else drop();
}

// ===========================================================================
// CONSTRUCTION
// ===========================================================================

void UltraPaintWindow::BuildMenuBar() {
    using M = MenuItemData;
    std::vector<MenuItemData> adjustItems, filterItems;
    std::string lastCategory;
    std::vector<MenuItemData>* filterGroup = nullptr;
    for (const auto& f : PaintFilterCatalogue()) {
        const PaintFilter* fp = &f;
        auto item = M::Action(f.name, MenuIconPath(f.id + ".svg"), [this, fp]() { CmdFilter(*fp); });
        if (f.category == "Adjust") {
            adjustItems.push_back(item);
        } else {
            if (f.category != lastCategory) {
                lastCategory = f.category;
                if (!filterItems.empty()) filterItems.push_back(M::Separator());
                filterItems.push_back(M::Header(f.category));
            }
            filterItems.push_back(item);
        }
        (void)filterGroup;
    }
    // Curves sits with the adjustments, right after Levels.
    adjustItems.insert(adjustItems.begin() + std::min<size_t>(4, adjustItems.size()),
                       M::ActionWithShortcut("Curves...", "Ctrl+M", MenuIconPath("curves.svg"), [this]() { CmdCurves(); }));
    // Colour to Alpha changes transparency rather than tone, so it sits on its own at the end.
    adjustItems.push_back(M::Separator());
    adjustItems.push_back(M::Action("Colour to Alpha...", MenuIconPath("colour-to-alpha.svg"), [this]() { CmdColourToAlpha(); }));

    menuBar = MenuBuilder("up-menubar", 0, 0, 0, kMenuHeight)
        .SetType(MenuType::Menubar)
        .AddSubmenu("File", {
            M::ActionWithShortcut("New...", "Ctrl+N", MenuIconPath("new.svg"), [this]() { CmdNew(); }),
            M::ActionWithShortcut("New Window", "Ctrl+Alt+N", MenuIconPath("new-window.svg"), [this]() { CmdNewWindow(); }),
            M::ActionWithShortcut("Open...", "Ctrl+O", MenuIconPath("open.svg"), [this]() { CmdOpen(); }),
            M::Action("Import Image...", MenuIconPath("import.svg"), [this]() { CmdImport(); }),
            M::Separator(),
            M::ActionWithShortcut("Save", "Ctrl+S", MenuIconPath("save.svg"), [this]() { CmdSave(); }),
            M::ActionWithShortcut("Save As...", "Ctrl+Shift+S", MenuIconPath("save-as.svg"), [this]() { CmdSaveAs(); }),
            M::ActionWithShortcut("Export with Options...", "Ctrl+E", MenuIconPath("export.svg"), [this]() { CmdExport(); }),
            M::Separator(),
            M::ActionWithShortcut("Quit", "Ctrl+Q", MenuIconPath("quit.svg"), [this]() { CmdQuit(); }),
        })
        .AddSubmenu("Edit", {
            M::ActionWithShortcut("Undo", "Ctrl+Z", MenuIconPath("undo.svg"), [this]() { CmdUndo(); }),
            M::ActionWithShortcut("Redo", "Ctrl+Y", MenuIconPath("redo.svg"), [this]() { CmdRedo(); }),
            M::Separator(),
            M::ActionWithShortcut("Cut", "Ctrl+X", MenuIconPath("cut.svg"), [this]() { CmdCut(); }),
            M::ActionWithShortcut("Copy", "Ctrl+C", MenuIconPath("copy.svg"), [this]() { CmdCopy(false); }),
            M::ActionWithShortcut("Copy Merged", "Ctrl+Shift+C", MenuIconPath("copy-merged.svg"), [this]() { CmdCopy(true); }),
            M::ActionWithShortcut("Paste as New Layer", "Ctrl+V", MenuIconPath("paste-layer.svg"), [this]() { CmdPaste(); }),
            M::ActionWithShortcut("Paste as New Image", "Ctrl+Shift+V", MenuIconPath("paste-image.svg"), [this]() { CmdPasteAsNew(); }),
            M::ActionWithShortcut("Paste Last Copied Image", "Ctrl+Alt+V", MenuIconPath("paste-last.svg"), [this]() { CmdPasteLastImage(); }),
            M::Separator(),
            M::ActionWithShortcut("Delete", "Del", MenuIconPath("delete.svg"), [this]() { CmdDelete(); }),
            M::ActionWithShortcut("Fill with Foreground", "Alt+Backspace", MenuIconPath("fill-fg.svg"), [this]() { CmdFill(true); }),
            M::ActionWithShortcut("Fill with Background", "Ctrl+Backspace", MenuIconPath("fill-bg.svg"), [this]() { CmdFill(false); }),
            M::Separator(),
            M::Action("Settings...", MenuIconPath("settings.svg"), [this]() { CmdSettings(); }),
        })
        .AddSubmenu("Image", {
            M::Action("Scale Image...", MenuIconPath("scale.svg"), [this]() { CmdScaleImage(); }),
            M::Action("Canvas Size...", MenuIconPath("canvas-size.svg"), [this]() { CmdCanvasSize(); }),
            M::ActionWithShortcut("Crop to Selection", "Ctrl+Shift+X", MenuIconPath("crop-selection.svg"), [this]() { CmdCropToSelection(); }),
            M::Separator(),
            M::Action("Flip Horizontal", MenuIconPath("flip-h.svg"), [this]() { if (document) document->FlipHorizontal(); }),
            M::Action("Flip Vertical", MenuIconPath("flip-v.svg"), [this]() { if (document) document->FlipVertical(); }),
            M::Action("Rotate 90° Clockwise", MenuIconPath("rotate-cw.svg"), [this]() { if (document) { document->Rotate90(true); surface->ZoomToFit(); } }),
            M::Action("Rotate 90° Counter-clockwise", MenuIconPath("rotate-ccw.svg"), [this]() { if (document) { document->Rotate90(false); surface->ZoomToFit(); } }),
            M::Action("Rotate 180°", MenuIconPath("rotate-180.svg"), [this]() { if (document) document->Rotate180(); }),
            M::Separator(),
            M::Action("Flatten Image", MenuIconPath("flatten.svg"), [this]() { CmdFlatten(); }),
        })
        .AddSubmenu("Layer", {
            M::ActionWithShortcut("New Layer", "Ctrl+Shift+N", MenuIconPath("layer-add.svg"), [this]() { CmdLayerAdd(); }),
            M::ActionWithShortcut("Duplicate Layer", "Ctrl+J", MenuIconPath("layer-duplicate.svg"), [this]() { CmdLayerDuplicate(); }),
            M::Action("Delete Layer", MenuIconPath("layer-delete.svg"), [this]() { CmdLayerDelete(); }),
            M::Separator(),
            M::ActionWithShortcut("Merge Down", "Ctrl+Shift+M", MenuIconPath("layer-merge.svg"), [this]() { CmdLayerMergeDown(); }),
            M::Action("Move Layer Up", MenuIconPath("layer-up.svg"), [this]() { CmdLayerMove(1); }),
            M::Action("Move Layer Down", MenuIconPath("layer-down.svg"), [this]() { CmdLayerMove(-1); }),
            M::Separator(),
            M::Action("Layer Properties...", MenuIconPath("layer-props.svg"), [this]() { CmdLayerProperties(); }),
        })
        .AddSubmenu("Select", {
            M::ActionWithShortcut("All", "Ctrl+A", MenuIconPath("select-all.svg"), [this]() { CmdSelectAll(); }),
            M::ActionWithShortcut("None", "Ctrl+Shift+A", MenuIconPath("select-none.svg"), [this]() { CmdSelectNone(); }),
            M::ActionWithShortcut("Invert", "Ctrl+I", MenuIconPath("select-invert.svg"), [this]() { CmdSelectInvert(); }),
            M::Action("From Layer Alpha", MenuIconPath("select-alpha.svg"), [this]() { CmdSelectFromAlpha(); }),
            M::Separator(),
            M::Action("Feather...", MenuIconPath("select-feather.svg"), [this]() { CmdSelectModify(0); }),
            M::Action("Grow...", MenuIconPath("select-grow.svg"), [this]() { CmdSelectModify(1); }),
            M::Action("Shrink...", MenuIconPath("select-shrink.svg"), [this]() { CmdSelectModify(2); }),
        })
        .AddSubmenu("Adjust", adjustItems)
        .AddSubmenu("Filter", filterItems)
        .AddSubmenu("View", {
            M::ActionWithShortcut("Zoom In", "+", MenuIconPath("zoom-in.svg"), [this]() { surface->ZoomIn(); }),
            M::ActionWithShortcut("Zoom Out", "-", MenuIconPath("zoom-out.svg"), [this]() { surface->ZoomOut(); }),
            M::ActionWithShortcut("Fit to Window", "Ctrl+0", MenuIconPath("zoom-fit.svg"), [this]() { surface->ZoomToFit(); }),
            M::ActionWithShortcut("Actual Pixels", "Ctrl+1", MenuIconPath("zoom-100.svg"), [this]() { surface->ZoomToActual(); }),
            M::Separator(),
            WithIcon(M::Checkbox("Pixel Grid (at 8x and above)", true, [this](bool on) { surface->SetShowPixelGrid(on); }),
                     MenuIconPath("pixel-grid.svg")),
            WithIcon(M::Checkbox("Show Selection Outline", true, [this](bool on) { surface->SetShowSelection(on); }),
                     MenuIconPath("selection-outline.svg")),
        })
        .AddSubmenu("Help", {
            M::Action("About UltraPaint", MenuIconPath("about.svg"), [this]() { CmdAbout(); }),
        })
        .Build();
    menuBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    window->AddChild(menuBar);
}

void UltraPaintWindow::BuildToolbar() {
    toolbar = std::make_shared<UltraCanvasToolbar>("up-toolbar", 0, 0, 0, kToolbarHeight);
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    ToolbarAppearance app = toolbar->GetAppearance();
    app.showIconLabels = false;
    app.style = ToolbarStyle::Flat;
    app.hoverBackgroundColor = Color(0, 0, 0, 18);
    toolbar->SetAppearance(app);
    toolbar->AddButton("up-tb-new", "", TexterIconPath("add-document.svg"), [this]() { CmdNew(); })->SetTooltip("New image (Ctrl+N)");
    toolbar->AddButton("up-tb-open", "", TexterIconPath("folder-open.svg"), [this]() { CmdOpen(); })->SetTooltip("Open (Ctrl+O)");
    toolbar->AddButton("up-tb-save", "", TexterIconPath("save.svg"), [this]() { CmdSave(); })->SetTooltip("Save (Ctrl+S)");
    toolbar->AddSeparator("up-tb-s1");
    undoButton = toolbar->AddButton("up-tb-undo", "", TexterIconPath("undo.svg"), [this]() { CmdUndo(); });
    undoButton->SetTooltip("Undo (Ctrl+Z)");
    redoButton = toolbar->AddButton("up-tb-redo", "", TexterIconPath("redo.svg"), [this]() { CmdRedo(); });
    redoButton->SetTooltip("Redo (Ctrl+Y)");
    toolbar->AddSeparator("up-tb-s2");
    toolbar->AddButton("up-tb-zoomout", "", TexterIconPath("zoom-out.svg"), [this]() { surface->ZoomOut(); })->SetTooltip("Zoom out (-)");
    toolbar->AddButton("up-tb-zoomin", "", TexterIconPath("zoom-in.svg"), [this]() { surface->ZoomIn(); })->SetTooltip("Zoom in (+)");
    toolbar->AddButton("up-tb-fit", "", IconPath("zoom-fit.svg"), [this]() { surface->ZoomToFit(); })->SetTooltip("Fit to window (Ctrl+0)");
    toolbar->AddButton("up-tb-100", "", IconPath("zoom-100.svg"), [this]() { surface->ZoomToActual(); })->SetTooltip("Actual pixels (Ctrl+1)");
    toolbar->AddSeparator("up-tb-s3");
    toolbar->AddButton("up-tb-layer", "", IconPath("layer-add.svg"), [this]() { CmdLayerAdd(); })->SetTooltip("New layer (Ctrl+Shift+N)");
    toolbar->AddButton("up-tb-export", "", IconPath("export.svg"), [this]() { CmdExport(); })->SetTooltip("Export with options (Ctrl+E)");
    for (auto& c : toolbar->Children()) {
        if (auto b = std::dynamic_pointer_cast<UltraCanvasButton>(c)) {
            ButtonStyle flat = b->GetStyle();
            flat.cornerRadius = 6.0f;
            flat.pressedColor = Color(0, 0, 0, 30);
            b->SetStyle(flat);
        }
    }
    window->AddChild(toolbar);
}

namespace {
    // The palette's groups, top to bottom: tools of a kind share one slot,
    // the group's main tool first. The slot shows the tool last picked from
    // its group (an adaptive toolbar) or always the main one (a static
    // toolbar, Settings > Display > Tool palette), and resting the pointer
    // on it folds out a bar with the whole group. A group of one tool is a
    // plain button. A tool missing here still gets a slot of its own, after
    // these, so a tool added to CreatePaintTools() never goes missing.
    struct PaletteGroup {
        const char* id;
        std::vector<PaintToolId> tools;
    };
    const std::vector<PaletteGroup>& PaletteGroups() {
        using T = PaintToolId;
        static const std::vector<PaletteGroup> groups = {
            { "select",  { T::RectSelect, T::EllipseSelect, T::Lasso, T::MagicWand } },
            { "pen",     { T::Brush, T::Pencil, T::Airbrush } },
            { "lines",   { T::Line, T::Polyline, T::Arrow, T::Curve } },
            { "shapes",  { T::Rectangle, T::Ellipse } },
            { "morph",   { T::Smudge, T::Clone } },          // reshape the pixels already there
            { "shading", { T::Dodge, T::Burn } },
            { "move",    { T::Move } },
            { "crop",    { T::Crop } },
            { "picker",  { T::Eyedropper } },
            { "eraser",  { T::Eraser } },
            { "fill",    { T::Fill, T::Gradient } },
            { "text",    { T::Text } },
            { "view",    { T::Zoom, T::Pan } },
        };
        return groups;
    }

    // A tool's id among the palette's group items.
    std::string ToolItemId(PaintToolId id) {
        return "tool-" + std::to_string(static_cast<int>(id));
    }
}

void UltraPaintWindow::BuildToolPalette() {
    palette = std::make_shared<UltraCanvasToolbar>("up-palette", 0, 0, kPaletteWidth, 0);
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

    auto findTool = [this](PaintToolId id) -> const PaintTool* {
        for (const auto& t : tools) if (t->id == id) return t.get();
        return nullptr;
    };
    auto itemOf = [this](const PaintTool& t) {
        ToolGroupItem item;
        item.id = ToolItemId(t.id);
        item.iconPath = ToolIconPath(t.iconFile);
        item.tooltip = t.name;
        if (t.shortcutKey) item.tooltip += std::string(" (") + t.shortcutKey + ")";
        return item;
    };

    std::vector<std::pair<std::string, std::vector<ToolGroupItem>>> slots;
    std::vector<PaintToolId> placed;
    for (const auto& g : PaletteGroups()) {
        std::vector<ToolGroupItem> items;
        for (PaintToolId id : g.tools) {
            if (const PaintTool* t = findTool(id)) { items.push_back(itemOf(*t)); placed.push_back(id); }
        }
        if (!items.empty()) slots.emplace_back(g.id, std::move(items));
    }
    for (const auto& t : tools) {
        if (std::find(placed.begin(), placed.end(), t->id) != placed.end()) continue;
        slots.emplace_back(ToolItemId(t->id), std::vector<ToolGroupItem>{ itemOf(*t) });
    }

    toolSlots.clear();
    for (auto& [groupId, items] : slots) {
        auto b = palette->AddToolGroup("up-tools-" + groupId, items, [this](const std::string& itemId) {
            for (const auto& t : tools) {
                if (ToolItemId(t->id) == itemId) { SelectTool(t->id); return; }
            }
        });
        b->SetSlotShowsLastPicked(settings.paletteAdaptive);
        // The palette's icons are drawn on a 28-unit grid (media/icons/
        // ultrapaint/28) with 2-unit strokes on whole units: shown at 28 px
        // they land on whole pixels and stay sharp. The 24-unit set beside
        // them is the same tools for 24 px and smaller (menus). Shown at a
        // size of neither, every line falls between two pixels and blurs, as
        // the old 24-unit icons did at 22 px. The fold-out bar's buttons
        // take the slot's size, icon size and style.
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

void UltraPaintWindow::BuildRightPanel() {
    rightPanel = CreateScrollableContainer("up-right", 0, 0, kRightWidth, 0);
    rightPanel->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    rightPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    rightPanel->SetPadding(6);
    rightPanel->SetBackgroundColor(Color(244, 244, 246, 255));

    // ----- colour -----
    rightPanel->AddChild(PanelTitle("up-color-title", "Colour"));
    colorPicker = CreateColorPicker("up-color", foreground.ToColor(), 0, 0, kRightInner, 300);
    colorPicker->SetUIScale(0.78f);
    colorPicker->SetBackgroundColor(background.ToColor());
    colorPicker->SetShowAlpha(true);
    // The hue ring is capped by whatever vertical space the controls leave it,
    // so a picker sized by eye draws a wheel narrower than the panel. Ask for
    // the height at which the ring fills the panel's width instead.
    const float pickerH = colorPicker->PreferredHeightForWidth(kRightInner);
    colorPicker->SetSize(kRightInner, pickerH);
    // Not stretched: the panel's content box is wider than what is visible
    // beside the scrollbar, and a stretched picker lays its hex field and
    // channel values out into the part that is covered.
    colorPicker->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Start)
                           .SetFlexBasis(CSSLayout::Dimension::Px(pickerH));
    colorPicker->onColorChanged = [this](const Color& c) { foreground = RasterPixel(c); };
    colorPicker->onColorChanging = [this](const Color& c) { foreground = RasterPixel(c); };
    colorPicker->onBackgroundChanged = [this](const Color& c) { background = RasterPixel(c); };
    colorPicker->onBackgroundChanging = [this](const Color& c) { background = RasterPixel(c); };
    rightPanel->AddChild(colorPicker);

    swatches = CreateColorSwatchBar("up-swatches", 0, 0, kRightInner, 44);
    swatches->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    swatches->onColorSelected = [this](const Color& c) {
        foreground = RasterPixel(c);
        colorPicker->SetForegroundColor(c, false);
    };
    // Right click on a swatch sets the background colour.
    swatches->onColorAdjustSelected = [this](const Color& c) {
        colorPicker->SetBackgroundColor(c, true);
    };
    rightPanel->AddChild(swatches);

    // ----- tool options -----
    optionsTitle = PanelTitle("up-options-title", "Tool Options");
    rightPanel->AddChild(optionsTitle);
    optionsPanel = std::make_shared<UltraCanvasContainer>("up-options", 0, 0, kRightInner, 0);
    optionsPanel->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    optionsPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
    rightPanel->AddChild(optionsPanel);

    // ----- layers -----
    rightPanel->AddChild(PanelTitle("up-layers-title", "Layers"));
    layersPanel = std::make_shared<UltraCanvasContainer>("up-layers", 0, 0, kRightInner, 0);
    layersPanel->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    layersPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);

    auto buttons = std::make_shared<UltraCanvasToolbar>("up-layer-buttons", 0, 0, 0, 30);
    ToolbarAppearance app = buttons->GetAppearance();
    app.showIconLabels = false;
    app.backgroundColor = Color(244, 244, 246, 255);
    buttons->SetAppearance(app);
    buttons->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    buttons->AddButton("up-l-add", "", IconPath("layer-add.svg"), [this]() { CmdLayerAdd(); })->SetTooltip("New layer");
    buttons->AddButton("up-l-dup", "", IconPath("layer-duplicate.svg"), [this]() { CmdLayerDuplicate(); })->SetTooltip("Duplicate layer");
    buttons->AddButton("up-l-del", "", IconPath("layer-delete.svg"), [this]() { CmdLayerDelete(); })->SetTooltip("Delete layer");
    buttons->AddButton("up-l-up", "", IconPath("layer-up.svg"), [this]() { CmdLayerMove(1); })->SetTooltip("Move up");
    buttons->AddButton("up-l-down", "", IconPath("layer-down.svg"), [this]() { CmdLayerMove(-1); })->SetTooltip("Move down");
    buttons->AddButton("up-l-merge", "", IconPath("layer-merge.svg"), [this]() { CmdLayerMergeDown(); })->SetTooltip("Merge down");
    buttons->AddButton("up-l-props", "", IconPath("layer-props.svg"), [this]() { CmdLayerProperties(); })->SetTooltip("Layer properties");
    layersPanel->AddChild(buttons);

    auto opRow = std::make_shared<UltraCanvasContainer>("up-l-oprow", 0, 0, 0, kRowH);
    opRow->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    opRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto opLabel = CreateLabel("up-l-oplabel", 0, 0, 52, kRowH, "Opacity");
    opLabel->SetFontSize(11);
    opLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    opRow->AddChild(opLabel);
    layerOpacity = CreateHorizontalSlider("up-l-opacity", 0, 0, 100, kRowH, 0, 100);
    layerOpacity->SetStep(1);
    layerOpacity->SetValue(100);
    layerOpacity->SetValueDisplay(SliderValueDisplay::NoDisplay);
    layerOpacity->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    layerOpacity->onValueChanged = [this](float v) {
        if (document) document->SetLayerOpacity(document->GetActiveLayerIndex(), v / 100.0f);
    };
    opRow->AddChild(layerOpacity);
    layerBlend = CreateDropdown("up-l-blend", 0, 0, 96, kRowH);
    for (RasterBlendMode m : AllRasterBlendModes()) layerBlend->AddItem(RasterBlendModeName(m));
    layerBlend->SetSelectedIndex(0, false);
    layerBlend->onSelectionChanged = [this](int i, const DropdownItem&) {
        if (document) document->SetLayerBlendMode(document->GetActiveLayerIndex(), AllRasterBlendModes()[static_cast<size_t>(std::max(0, i))]);
    };
    layerBlend->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    opRow->AddChild(layerBlend);
    layersPanel->AddChild(opRow);

    layerRows = std::make_shared<UltraCanvasContainer>("up-layer-rows", 0, 0, 0, 0);
    layerRows->layout.SetFlexColumn().SetFlexGap(2).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    layerRows->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    layersPanel->AddChild(layerRows);
    rightPanel->AddChild(layersPanel);

    body->AddChild(rightPanel);
}

void UltraPaintWindow::BuildStatusBar() {
    statusBar = std::make_shared<UltraCanvasContainer>("up-status", 0, 0, 0, kStatusHeight);
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
    statusPos  = mk("up-st-pos", 130, false);
    statusZoom = mk("up-st-zoom", 80, false);
    statusSize = mk("up-st-size", 160, false);
    statusHint = mk("up-st-hint", 0, true);
    window->AddChild(statusBar);
}

void UltraPaintWindow::InstallShortcuts() {
    window->InstallEventFilter("up-shortcuts", [this](const UCEvent& e) -> bool {
        if (e.type != UCEventType::KeyDown) return false;
        if (window->GetActivePopupElement()) return false;
        if (FocusIsTextEntry(window.get())) return false;
        const UCKeys k = e.virtualKey;
        if (e.ctrl) {
            switch (k) {
                case UCKeys::N:
                    if (e.alt) CmdNewWindow();
                    else if (e.shift) CmdLayerAdd();
                    else CmdNew();
                    return true;
                case UCKeys::O: CmdOpen(); return true;
                case UCKeys::S: if (e.shift) CmdSaveAs(); else CmdSave(); return true;
                case UCKeys::E: CmdExport(); return true;
                case UCKeys::Q: CmdQuit(); return true;
                case UCKeys::Z: if (e.shift) CmdRedo(); else CmdUndo(); return true;
                case UCKeys::Y: CmdRedo(); return true;
                case UCKeys::X: if (e.shift) CmdCropToSelection(); else CmdCut(); return true;
                case UCKeys::C: CmdCopy(e.shift); return true;
                case UCKeys::V:
                    if (e.alt) CmdPasteLastImage();
                    else if (e.shift) CmdPasteAsNew();
                    else CmdPaste();
                    return true;
                case UCKeys::A: if (e.shift) CmdSelectNone(); else CmdSelectAll(); return true;
                case UCKeys::I: CmdSelectInvert(); return true;
                case UCKeys::J: CmdLayerDuplicate(); return true;
                case UCKeys::M: if (e.shift) CmdLayerMergeDown(); else CmdCurves(); return true;
                case UCKeys::Backspace: CmdFill(false); return true;
                case UCKeys::Key0: surface->ZoomToFit(); return true;
                case UCKeys::Key1: surface->ZoomToActual(); return true;
                default: break;
            }
            return false;
        }
        if (e.alt && k == UCKeys::Backspace) { CmdFill(true); return true; }
        if (k == UCKeys::Delete) { CmdDelete(); return true; }
        if (k == UCKeys::X && !e.shift) {
            // swap colours
            std::swap(foreground, background);
            colorPicker->SetForegroundColor(foreground.ToColor(), false);
            colorPicker->SetBackgroundColor(background.ToColor());
            return true;
        }
        if (k == UCKeys::D && e.shift) {
            foreground = RasterPixel(0, 0, 0, 255); background = RasterPixel(255, 255, 255, 255);
            colorPicker->SetForegroundColor(foreground.ToColor(), false);
            colorPicker->SetBackgroundColor(background.ToColor());
            return true;
        }
        // single-letter tool keys (not while a tool is mid-drag)
        if (!e.alt && !e.shift && k >= UCKeys::A && k <= UCKeys::Z) {
            for (const auto& t : tools) {
                if (t->shortcutKey && t->shortcutKey == static_cast<char>(k)) { SelectTool(t->id); return true; }
            }
        }
        return false;
    }, { UCEventType::KeyDown });
}

// ===========================================================================
// DOCUMENT
// ===========================================================================

void UltraPaintWindow::SetDocument(std::shared_ptr<UCRasterDocument> doc, const std::string& title) {
    (void)title;
    if (document && documentListener) document->RemoveListener(documentListener);
    document = std::move(doc);
    renamingLayer = -1;
    // The window's own listener beside the surfaces' (the editing view and
    // the magnifying glass each add theirs in SetDocument): the panels and
    // the title follow the document without chaining behind anyone's slot.
    UCRasterDocument::Listener l;
    l.stateChanged = [this]() { UpdateUndoButtons(); UpdateTitle(); };
    l.structureChanged = [this]() { ScheduleLayerPanelRebuild(); UpdateStatus(); };
    documentListener = document->AddListener(std::move(l));
    surface->SetDocument(document);
    if (magnifier) magnifier->SetDocument(document);
    surface->ZoomToFit();
    RebuildLayerPanel();
    UpdateUndoButtons();
    UpdateTitle();
    UpdateStatus();
}

void UltraPaintWindow::NewImage(const UltraPaintNewImageResult& r) {
    lastNewImage = r;
    RasterPixel bg(255, 255, 255, 255);
    switch (r.background) {
        case 1: bg = RasterPixel(0, 0, 0, 255); break;
        case 2: bg = RasterPixel(0, 0, 0, 0); break;
        case 3: bg = foreground; break;
        case 4: bg = background; break;
        default: break;
    }
    SetDocument(std::make_shared<UCRasterDocument>(r.width, r.height, bg), "Untitled");
    if (statusHint) statusHint->SetText("New " + std::to_string(r.width) + " x " + std::to_string(r.height) + " image");
}

void UltraPaintWindow::OpenFile(const std::string& path) {
    // Vector artwork carries no pixels and a model carries neither pixels
    // nor a view, so neither can just be opened: the import dialog asks first.
    if (NeedsImportDialog(path)) {
        ImportFile(path, false);
        return;
    }
    LoadIntoWindow(path);
}

bool UltraPaintWindow::LoadIntoWindow(const std::string& path) {
    auto doc = std::make_shared<UCRasterDocument>();
    std::string error;
    if (!doc->LoadFromFile(path, error)) {
        UltraCanvasDialogManager::ShowError("Could not open " + path + "\n" + ShortError(error), "Open", nullptr, window.get());
        return false;
    }
    SetDocument(doc, FileNameOf(path));
    if (statusHint) statusHint->SetText("Opened " + FileNameOf(path));
    return true;
}

// ===========================================================================
// IMPORT: DROPPED FILES AND VECTOR ARTWORK
// ===========================================================================

void UltraPaintWindow::HandleDroppedFiles(const std::vector<std::string>& files) {
    std::vector<std::string> usable;
    for (const auto& f : files) {
        if (IsOpenablePath(f)) usable.push_back(f);
    }
    if (usable.empty()) {
        if (statusHint) {
            statusHint->SetText(files.empty() ? "Nothing was dropped"
                                              : "UltraPaint cannot open " + FileNameOf(files.front()));
        }
        return;
    }
    const std::string first = usable.front();
    ImportFile(first, true, std::vector<std::string>(usable.begin() + 1, usable.end()));
}

void UltraPaintWindow::ImportModel(const std::string& path, bool fromDrop,
                                   const std::vector<std::string>& alsoFiles) {
    // A model needs a view before it needs a size, so it gets the framework's
    // 3D import dialog: the media viewer with its top bars off, which is the
    // same 3D pane the file manager and the viewer use, plus the bitmap size
    // and the background. Whatever the user orbits to is what gets rendered.
    std::vector<ModelViewAction> actions;
    if (fromDrop && document && document->IsValid()) {
        actions.push_back(ModelViewAction{"merge", "Merge image", false});
        actions.push_back(ModelViewAction{"window", "Open new window", true});
    } else {
        actions.push_back(ModelViewAction{"open", "Open", true});
    }

    ShowModelViewDialog(path, actions, window.get(),
                        [this, path, alsoFiles](UltraCanvasModelViewDialog& dialog,
                                                const ModelViewResult& view) {
        // Render from the mesh the dialog's viewer already holds — asking it
        // rather than the path keeps a big model from being parsed twice.
        std::string error;
        std::shared_ptr<UCRasterLayer> layer = dialog.Rasterize(error);
        if (!layer) {
            UltraCanvasDialogManager::ShowError("Could not render " + path + "\n" + ShortError(error),
                                                "Import", nullptr, window.get());
            return;
        }

        auto incoming = std::make_shared<UCRasterDocument>(layer->GetWidth(), layer->GetHeight(),
                                                           view.background);
        incoming->GetLayer(0)->CopyFrom(*layer, 0, 0);
        incoming->SetLayerName(0, layer->name);
        incoming->InvalidateComposite();
        incoming->ClearHistory();
        // A rendered view is an unsaved image: it has no file of its own, so
        // Save asks where to put it rather than writing pixels over the model.
        incoming->SetModified(true);

        UltraPaintImportResult result;
        if (view.actionId == "merge")       result.action = UltraPaintImportResult::Action::Merge;
        else if (view.actionId == "window") result.action = UltraPaintImportResult::Action::NewWindow;
        else                                result.action = UltraPaintImportResult::Action::Open;
        ApplyImport(path, result, incoming);

        // The rest of a multi-file drop follow the same answer; each is read
        // on its own terms (a model at its default view, a drawing at its
        // natural size).
        UltraPaintImportResult forTheRest = result;
        forTheRest.width = 0;
        forTheRest.height = 0;
        forTheRest.page = 0;
        for (const auto& more : alsoFiles) ApplyImport(more, forTheRest);
    });
}

void UltraPaintWindow::ImportFile(const std::string& path, bool fromDrop,
                                  const std::vector<std::string>& alsoFiles) {
    if (IsModelPath(path)) {
        ImportModel(path, fromDrop, alsoFiles);
        return;
    }

    UltraPaintImportRequest request;
    request.path = path;
    request.extraFiles = alsoFiles.size();
    request.offerMerge = fromDrop && document && document->IsValid();
    if (document) {
        request.canvasWidth = document->GetWidth();
        request.canvasHeight = document->GetHeight();
    }

    // The file has to be measured before the dialog can show a size: vector
    // artwork reports its natural size without rendering, and a bitmap is
    // decoded here so the dialog can say how big it is (and so merging it
    // does not decode it a second time).
    std::shared_ptr<UCRasterDocument> preloaded;
    if (IsVectorPath(path)) {
        const VectorSourceInfo info = InspectVectorFile(path);
        if (!info.ok) {
            UltraCanvasDialogManager::ShowError("Could not read " + path + "\n" + ShortError(info.error),
                                                "Open", nullptr, window.get());
            return;
        }
        request.vector = true;
        request.naturalWidth = info.naturalWidth;
        request.naturalHeight = info.naturalHeight;
        request.pageCount = info.pageCount;
        request.provider = info.provider;
    } else {
        preloaded = std::make_shared<UCRasterDocument>();
        std::string error;
        if (!preloaded->LoadFromFile(path, error)) {
            UltraCanvasDialogManager::ShowError("Could not open " + path + "\n" + ShortError(error),
                                                "Open", nullptr, window.get());
            return;
        }
        request.naturalWidth = preloaded->GetWidth();
        request.naturalHeight = preloaded->GetHeight();
        if (!fromDrop) {
            // Opening a bitmap from the File menu asks nothing.
            SetDocument(preloaded, FileNameOf(path));
            if (statusHint) statusHint->SetText("Opened " + FileNameOf(path));
            return;
        }
    }

    auto dialog = std::make_shared<UltraPaintImportDialog>(request);
    dialog->onAccept = [this, path, alsoFiles, preloaded](const UltraPaintImportResult& result) {
        if (result.action == UltraPaintImportResult::Action::Cancel) return;
        ApplyImport(path, result, preloaded);
        // The rest of a multi-file drop take the same action, but the size the
        // dialog settled on belongs to the file it was shown for: the others
        // come in at their own natural size.
        UltraPaintImportResult forTheRest = result;
        forTheRest.width = 0;
        forTheRest.height = 0;
        forTheRest.page = 0;
        for (const auto& more : alsoFiles) ApplyImport(more, forTheRest);
    };
    // A drop answers a gesture made on this window, so the question belongs
    // over it rather than wherever the window manager would drop it.
    ShowDialog(dialog);
}

void UltraPaintWindow::ApplyImport(const std::string& path, const UltraPaintImportResult& result,
                                   std::shared_ptr<UCRasterDocument> preloaded) {
    if (result.action == UltraPaintImportResult::Action::Cancel) return;

    auto incoming = preloaded ? preloaded : LoadDocument(path, result);
    if (!incoming) return;

    switch (result.action) {
        case UltraPaintImportResult::Action::Merge:
            MergeDocument(incoming, path, result.scaleToFit);
            break;
        case UltraPaintImportResult::Action::NewWindow: {
            auto editor = OpenWindow({});
            if (!editor) {
                UltraCanvasDialogManager::ShowError("Could not open another UltraPaint window",
                                                    "New Window", nullptr, window.get());
                return;
            }
            editor->SetDocument(incoming, FileNameOf(path));
            if (editor->statusHint) editor->statusHint->SetText("Opened " + FileNameOf(path));
            break;
        }
        case UltraPaintImportResult::Action::Open:
            SetDocument(incoming, FileNameOf(path));
            if (statusHint) statusHint->SetText("Opened " + FileNameOf(path));
            break;
        default:
            break;
    }
}

std::shared_ptr<UCRasterDocument> UltraPaintWindow::LoadDocument(const std::string& path,
                                                                 const UltraPaintImportResult& result) {
    if (IsModelPath(path)) {
        // Reached for the other files of a multi-file drop: nobody framed
        // these, so they come in at the viewer's opening view.
        ModelRasterOptions options;
        if (result.width > 0) options.width = result.width;
        if (result.height > 0) options.height = result.height;
        std::string error;
        auto layer = RasterizeModelFile(path, options, error);
        if (!layer) {
            UltraCanvasDialogManager::ShowError("Could not render " + path + "\n" + ShortError(error),
                                                "Import", nullptr, window.get());
            return nullptr;
        }
        auto doc = std::make_shared<UCRasterDocument>(layer->GetWidth(), layer->GetHeight(),
                                                      RasterPixel(0, 0, 0, 0));
        doc->GetLayer(0)->CopyFrom(*layer, 0, 0);
        doc->SetLayerName(0, layer->name);
        doc->InvalidateComposite();
        doc->ClearHistory();
        doc->SetModified(true);
        return doc;
    }

    if (IsVectorPath(path)) {
        VectorRasterOptions options;
        options.width = std::max(0, result.width);
        options.height = std::max(0, result.height);
        options.page = std::max(0, result.page);
        std::string error;
        auto layer = RasterizeVectorFile(path, options, error);
        if (!layer) {
            UltraCanvasDialogManager::ShowError("Could not rasterize " + path + "\n" + ShortError(error),
                                                "Open", nullptr, window.get());
            return nullptr;
        }
        // A rasterized drawing starts as an unsaved image: writing it back to
        // the .svg it came from would throw the vector data away, so it keeps
        // no file path and Save asks where to put it.
        auto doc = std::make_shared<UCRasterDocument>(layer->GetWidth(), layer->GetHeight(),
                                                      RasterPixel(0, 0, 0, 0));
        doc->GetLayer(0)->CopyFrom(*layer, 0, 0);
        doc->SetLayerName(0, layer->name);
        doc->InvalidateComposite();
        doc->ClearHistory();
        doc->SetModified(true);
        return doc;
    }

    auto doc = std::make_shared<UCRasterDocument>();
    std::string error;
    if (!doc->LoadFromFile(path, error)) {
        UltraCanvasDialogManager::ShowError("Could not open " + path + "\n" + ShortError(error),
                                            "Open", nullptr, window.get());
        return nullptr;
    }
    return doc;
}

void UltraPaintWindow::MergeDocument(const std::shared_ptr<UCRasterDocument>& incoming,
                                     const std::string& path, bool scaleToFit) {
    if (!document || !incoming || !incoming->IsValid()) return;
    auto image = incoming->Flatten();
    if (!image || !image->IsValid()) return;

    const int canvasW = document->GetWidth(), canvasH = document->GetHeight();
    if (scaleToFit && (image->GetWidth() > canvasW || image->GetHeight() > canvasH)) {
        const double k = std::min(static_cast<double>(canvasW) / image->GetWidth(),
                                  static_cast<double>(canvasH) / image->GetHeight());
        image = ScaleLayerTo(*image, std::max(1, static_cast<int>(std::lround(image->GetWidth() * k))),
                                     std::max(1, static_cast<int>(std::lround(image->GetHeight() * k))));
    }

    auto layer = std::make_shared<UCRasterLayer>(canvasW, canvasH, FileNameOf(path));
    layer->CopyFrom(*image, (canvasW - image->GetWidth()) / 2, (canvasH - image->GetHeight()) / 2);
    document->AddLayer(layer);
    SelectTool(PaintToolId::Move);
    if (statusHint) {
        const bool cropped = image->GetWidth() > canvasW || image->GetHeight() > canvasH;
        statusHint->SetText("Merged " + FileNameOf(path) + " as a new layer" +
                            (cropped ? " (cropped to the canvas)" : ""));
    }
}

void UltraPaintWindow::ShowDialog(const std::shared_ptr<UltraCanvasWindow>& dialog) {
    if (!dialog) return;
    dialog->Create();
    if (window) {
        // Transient: the desktop then knows this window belongs to the editor,
        // keeps it above it, and moves it with it. Without that a dialog is
        // just another application window — the window manager is free to put
        // it behind the editor or to maximise it over the image, and either
        // way the user sees nothing happen.
        dialog->SetTransientParent(window.get());
        dialog->CenterOnParent(window.get());
    }
    dialog->Show();
}

bool UltraPaintWindow::SaveToPath(const std::string& path) {
    if (!document) return false;
    std::string error;
    if (!document->SaveToFile(path, error)) {
        UltraCanvasDialogManager::ShowError("Could not save " + path + "\n" + ShortError(error), "Save", nullptr, window.get());
        return false;
    }
    UpdateTitle();
    if (statusHint) statusHint->SetText("Saved " + FileNameOf(path));
    return true;
}

void UltraPaintWindow::ConfirmDiscard(const std::string& question, const std::function<void()>& proceed) {
    UltraCanvasDialogManager::ShowConfirmation(question, "UltraPaint", [proceed](bool yes) { if (yes) proceed(); }, window.get());
}

void UltraPaintWindow::UpdateTitle() {
    if (!window) return;
    std::string t = document ? FileNameOf(document->GetFilePath()) : "Untitled";
    if (document && document->IsModified()) t = "*" + t;
    window->SetWindowTitle(t + " - UltraPaint " ULTRAPAINT_VERSION);
}

void UltraPaintWindow::UpdateStatus() {
    if (!document || !statusPos) return;
    char buf[96];
    if (pointerInside) std::snprintf(buf, sizeof(buf), "%d, %d", static_cast<int>(std::floor(pointerX)), static_cast<int>(std::floor(pointerY)));
    else std::snprintf(buf, sizeof(buf), "--, --");
    statusPos->SetText(buf);
    std::snprintf(buf, sizeof(buf), "%.0f %%", surface->GetZoom() * 100.0);
    statusZoom->SetText(buf);
    std::snprintf(buf, sizeof(buf), "%d x %d px, %d layer%s", document->GetWidth(), document->GetHeight(),
                  document->GetLayerCount(), document->GetLayerCount() == 1 ? "" : "s");
    statusSize->SetText(buf);
}

void UltraPaintWindow::UpdateUndoButtons() {
    if (!document) return;
    if (undoButton) {
        undoButton->SetDisabled(!document->CanUndo());
        undoButton->SetTooltip(document->CanUndo() ? "Undo " + document->GetUndoLabel() + " (Ctrl+Z)" : "Undo (Ctrl+Z)");
    }
    if (redoButton) {
        redoButton->SetDisabled(!document->CanRedo());
        redoButton->SetTooltip(document->CanRedo() ? "Redo " + document->GetRedoLabel() + " (Ctrl+Y)" : "Redo (Ctrl+Y)");
    }
}

// ===========================================================================
// TOOLS
// ===========================================================================

PaintTool* UltraPaintWindow::ActiveTool() const {
    for (const auto& t : tools) if (t->id == activeTool) return t.get();
    return nullptr;
}

void UltraPaintWindow::SelectTool(PaintToolId id) {
    if (auto* old = ActiveTool()) { if (old->id != id) old->Deactivate(toolContext); }
    activeTool = id;
    PaintTool* t = ActiveTool();
    // The slot of the tool's group shows it (on an adaptive toolbar) and is
    // pressed; every other slot is let go - however the tool was picked:
    // in the palette, by its shortcut key or by a command (Paste picks Move).
    for (const auto& slot : toolSlots) {
        const bool on = slot->SetCurrentItem(ToolItemId(id));
        if (slot->IsPressed() != on) slot->SetPressed(on);
    }
    if (!t) return;
    t->Activate(toolContext);
    surface->SetToolCursor(t->Cursor());
    surface->SetCursorRadius(t->CursorRadius(toolContext));
    surface->SetCursorSquare(t->CursorSquare(toolContext));
    if (statusHint) statusHint->SetText(t->name + ": " + t->hint);
    RebuildToolOptions();
    UpdateMagnifierVisibility();
    surface->Refresh();
}

// ===========================================================================
// THE MOUSE POINTER MAGNIFICATION GLASS
// ===========================================================================

namespace {
    // How far the glass keeps from the canvas's edges, and how near the
    // pointer may come before it moves to another corner.
    constexpr float kMagnifierMargin = 12.0f;
    constexpr float kMagnifierAvoidDistance = 48.0f;
}

void UltraPaintWindow::BuildMagnifier() {
    // The frame is the glass's border; it lies over the canvas, out of the
    // body's flex flow, above the surface, and takes no pointer - the
    // surface under it gets every event, so a drag can pass beneath it.
    magnifierFrame = std::make_shared<UltraCanvasContainer>("up-magnifier-frame");
    magnifierFrame->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    magnifierFrame->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
    if (!magnifierFrame->layoutItem.position) magnifierFrame->layoutItem.position = CSSLayout::Position();
    magnifierFrame->SetZIndex(OverlayZOrder::Overlays);
    magnifierFrame->SetBackgroundColor(Color(40, 40, 44, 255));
    magnifierFrame->SetBorderLeft(1, Color(255, 255, 255, 200));
    magnifierFrame->SetBorderRight(1, Color(255, 255, 255, 200));
    magnifierFrame->SetBorderTop(1, Color(255, 255, 255, 200));
    magnifierFrame->SetBorderBottom(1, Color(255, 255, 255, 200));
    magnifierFrame->SetPadding(1);
    magnifierFrame->SetInteractive(false);
    magnifierFrame->SetVisible(false);

    magnifier = CreatePaintSurface("up-magnifier");
    magnifier->SetLoupeMode(true);
    magnifier->SetCanvasColor(Color(40, 40, 44, 255));
    magnifier->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // The rubber band of the tool, magnified along with the pixels.
    magnifier->onDrawOverlay = [this](IRenderContext* ctx, const PaintViewTransform& v) {
        if (auto* t = ActiveTool()) t->DrawOverlay(toolContext, ctx, v);
    };
    magnifierFrame->AddChild(magnifier);
    body->AddChild(magnifierFrame);
    ApplySettings();
}

void UltraPaintWindow::ApplySettings() {
    for (const auto& slot : toolSlots) slot->SetSlotShowsLastPicked(settings.paletteAdaptive);
    if (!magnifierFrame) return;
    const float size = static_cast<float>(std::clamp(settings.magnifierSize,
            UltraPaintSettings::kMinMagnifierSize, UltraPaintSettings::kMaxMagnifierSize));
    magnifierFrame->size.width  = CSSLayout::Dimension::Px(size);
    magnifierFrame->size.height = CSSLayout::Dimension::Px(size);
    PlaceMagnifier(magnifierCorner);
    UpdateMagnifierVisibility();
    if (magnifier && magnifier->IsVisible() && pointerInside)
        magnifier->SetLoupeView(surface->GetZoom() * settings.magnifierFactor, pointerX, pointerY);
}

void UltraPaintWindow::UpdateMagnifierVisibility() {
    if (!magnifierFrame) return;
    PaintTool* t = ActiveTool();
    const bool show = settings.magnifierEnabled && t && t->WantsMagnifier() && document;
    if (show == magnifierFrame->IsVisible()) return;
    magnifierFrame->SetVisible(show);
    if (show) {
        // Until the pointer moves, the glass shows the middle of the image,
        // or the last place the pointer was.
        const double ix = pointerInside ? pointerX : document->GetWidth() * 0.5;
        const double iy = pointerInside ? pointerY : document->GetHeight() * 0.5;
        magnifier->SetLoupeView(surface->GetZoom() * settings.magnifierFactor, ix, iy);
        PlaceMagnifier(magnifierCorner);
    }
    body->RequestRedraw();
}

Rect2Df UltraPaintWindow::MagnifierRectForCorner(int corner) const {
    const Rect2Df s = surface->GetBounds();   // in the body, where the frame lives
    const float size = static_cast<float>(std::clamp(settings.magnifierSize,
            UltraPaintSettings::kMinMagnifierSize, UltraPaintSettings::kMaxMagnifierSize));
    const float x = (corner % 2 == 0) ? s.x + kMagnifierMargin : s.x + s.width - size - kMagnifierMargin;
    const float y = (corner < 2) ? s.y + kMagnifierMargin : s.y + s.height - size - kMagnifierMargin;
    return Rect2Df(x, y, size, size);
}

void UltraPaintWindow::PlaceMagnifier(int corner) {
    if (!magnifierFrame || !surface) return;
    magnifierCorner = std::clamp(corner, 0, 3);
    const Rect2Df r = MagnifierRectForCorner(magnifierCorner);
    magnifierFrame->layoutItem.position->left = CSSLayout::Dimension::Px(r.x);
    magnifierFrame->layoutItem.position->top  = CSSLayout::Dimension::Px(r.y);
    magnifierFrame->InvalidateLayout();
    body->RequestRedraw();
}

void UltraPaintWindow::UpdateMagnifier(double imageX, double imageY, const Point2Di& view) {
    if (!magnifier || !magnifierFrame || !magnifierFrame->IsVisible()) return;
    magnifier->SetLoupeView(surface->GetZoom() * settings.magnifierFactor, imageX, imageY);

    // The canvas may have been resized since the glass was placed (the window
    // or a panel): its corner is where it was placed in, not where it is.
    {
        const Rect2Df want = MagnifierRectForCorner(magnifierCorner);
        const Rect2Df have = magnifierFrame->GetBounds();
        if (std::fabs(want.x - have.x) > 0.5f || std::fabs(want.y - have.y) > 0.5f) PlaceMagnifier(magnifierCorner);
    }

    // Keep out of the pointer's way: when it comes within reach of the
    // glass, move to the corner farthest from it.
    const Rect2Df s = surface->GetBounds();
    const Point2Df p(s.x + static_cast<float>(view.x), s.y + static_cast<float>(view.y));
    auto distance = [&p](const Rect2Df& r) {
        const float dx = std::max({r.x - p.x, 0.0f, p.x - (r.x + r.width)});
        const float dy = std::max({r.y - p.y, 0.0f, p.y - (r.y + r.height)});
        return std::sqrt(dx * dx + dy * dy);
    };
    if (distance(MagnifierRectForCorner(magnifierCorner)) >= kMagnifierAvoidDistance) return;
    int best = magnifierCorner;
    float bestDistance = -1.0f;
    for (int c = 0; c < 4; ++c) {
        const float d = distance(MagnifierRectForCorner(c));
        if (d > bestDistance) { bestDistance = d; best = c; }
    }
    if (best != magnifierCorner) PlaceMagnifier(best);
}

void UltraPaintWindow::RebuildToolOptions() {
    if (!optionsPanel) return;
    optionsPanel->ClearChildren();
    PaintTool* t = ActiveTool();
    if (!t) return;
    optionsTitle->SetText(t->name);
    t->BuildOptions(toolContext, *optionsPanel, [this]() {
        if (auto* a = ActiveTool()) {
            surface->SetCursorRadius(a->CursorRadius(toolContext));
            surface->SetCursorSquare(a->CursorSquare(toolContext));
        }
    });
    // size the panel to its rows
    float h = 0;
    for (const auto& c : optionsPanel->GetChildren()) h += c->GetHeight() + 3;
    optionsPanel->SetBounds(Rect2Df(0, 0, kRightInner, h));
    rightPanel->RequestRedraw();
}

void UltraPaintWindow::ScheduleLayerPanelRebuild() {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) { RebuildLayerPanel(); return; }
    if (layerPanelRebuildPending) return;
    layerPanelRebuildPending = true;
    std::weak_ptr<bool> alive = lifeToken;
    app->PostToUIThread([this, alive]() {
        if (alive.expired()) return;
        layerPanelRebuildPending = false;
        RebuildLayerPanel();
    });
}

void UltraPaintWindow::StartLayerRename(int index) {
    if (!document || !document->GetLayer(index)) return;
    document->SetActiveLayerIndex(index);
    renamingLayer = index;
    // From the name button's own double-click: the rebuild that swaps the
    // button for the field runs once that click has been handled.
    ScheduleLayerPanelRebuild();
}

void UltraPaintWindow::FinishLayerRename(bool keep, const std::string& name) {
    const int index = renamingLayer;
    if (index < 0) return;
    // First: the rebuild below removes the field, and its focus loss then
    // finds no rename to finish again.
    renamingLayer = -1;
    const std::string trimmed = Trim(name);
    if (keep && document && document->GetLayer(index) && !trimmed.empty())
        document->SetLayerName(index, trimmed);   // undoable, as "Rename Layer"
    ScheduleLayerPanelRebuild();   // the name button back, also when nothing changed
}

void UltraPaintWindow::RebuildLayerPanel() {
    if (!layerRows || !document) return;
    layerRows->ClearChildren();
    const int n = document->GetLayerCount();
    const int active = document->GetActiveLayerIndex();
    if (renamingLayer >= n) renamingLayer = -1;   // the layer went (undo, delete)
    std::shared_ptr<UltraCanvasTextInput> renameField;
    // top of the stack first
    for (int i = n - 1; i >= 0; --i) {
        auto layer = document->GetLayer(i);
        if (!layer) continue;
        auto row = std::make_shared<UltraCanvasContainer>("up-lrow-" + std::to_string(i), 0, 0, 0, 26);
        row->layout.SetFlexRow().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto vis = std::make_shared<UltraCanvasCheckbox>("up-lvis-" + std::to_string(i), 0, 0, 24, 24, "");
        vis->SetChecked(layer->visible);
        vis->SetTooltip("Visible");
        vis->onStateChanged = [this, i](CheckedState, CheckedState s) { document->SetLayerVisible(i, s == CheckedState::Checked); };
        vis->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->AddChild(vis);
        if (i == renamingLayer) {
            auto field = CreateTextInput("up-lname-" + std::to_string(i), 0, 0, 0, 24);
            TextInputStyle ts = field->GetStyle();
            ts.fontStyle.fontSize = 11.0f;
            field->SetStyle(ts);
            field->SetShowValidationState(false);   // a name, not a form
            field->SetText(layer->name);
            field->onEnterPressed = [this](const std::string& text) {
                FinishLayerRename(true, text);
                return true;
            };
            field->onEscapePressed = [this]() {
                FinishLayerRename(false, "");
                return true;
            };
            // A click anywhere else keeps the name, as Enter does.
            UltraCanvasTextInput* raw = field.get();
            field->onFocusLost = [this, raw]() { FinishLayerRename(true, raw->GetText()); };
            field->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(field);
            renameField = field;
        } else {
            std::string label = layer->name;
            if (layer->locked) label += "  [locked]";
            auto btn = std::make_shared<UltraCanvasButton>("up-lsel-" + std::to_string(i), 0, 0, 0, 24, label);
            btn->SetCanToggled(true);
            btn->SetPressed(i == active);
            btn->SetTooltip("Double-click to rename");
            ButtonStyle st = btn->GetStyle();
            st.fontSize = 11.0f;
            btn->SetStyle(st);
            btn->onToggle = [this, i](bool) {
                document->SetActiveLayerIndex(i);
                // Also when `i` was already active: the click released the
                // button, and the rebuild presses it again.
                ScheduleLayerPanelRebuild();
            };
            // The first click has selected the layer; the second renames it.
            btn->onDoubleClick = [this, i]() { StartLayerRename(i); };
            btn->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(btn);
        }
        layerRows->AddChild(row);
    }
    if (renameField) {
        // In the window now: it can take the keyboard. The whole name is
        // selected, so typing replaces it.
        renameField->SetFocus(true);
        renameField->SelectAll();
    }
    layerRows->SetBounds(Rect2Df(0, 0, kRightInner, static_cast<float>(n) * 28.0f));
    if (auto layer = document->GetActiveLayer()) {
        layerOpacity->SetValue(layer->opacity * 100.0f);
        const auto& modes = AllRasterBlendModes();
        const auto it = std::find(modes.begin(), modes.end(), layer->blendMode);
        layerBlend->SetSelectedIndex(it == modes.end() ? 0 : static_cast<int>(it - modes.begin()), false);
    }
    layersPanel->SetBounds(Rect2Df(0, 0, kRightInner, 30 + kRowH + 6 + static_cast<float>(n) * 28.0f + 8));
    rightPanel->RequestRedraw();
}

void UltraPaintWindow::PlaceText(double x, double y, const UltraPaintTextResult& r) {
#ifdef HAS_LIBVIPS
    if (!document || r.text.empty()) return;
    auto layer = document->GetActiveLayer();
    if (!layer || layer->locked) return;
    try {
        const std::string font = r.font + (r.bold ? " Bold " : " ") + std::to_string(std::max(4, r.size));
        PixelFX::PFXImage mask = PixelFX::Generate::Text(r.text, font, 0, 0, 72);
        mask = PixelFX::Conversion::CastUchar(mask);
        if (mask.Bands() > 1) mask = PixelFX::Colour::ExtractBand(mask, 0);
        std::vector<uint8_t> bytes = PixelFX::Conversion::ToMemory(mask);
        const int mw = mask.Width(), mh = mask.Height();
        if (mw <= 0 || mh <= 0 || bytes.size() < static_cast<size_t>(mw) * mh) return;
        auto before = layer->Clone();
        const Rect2Di changed = RasterPaint::StampMask(*layer, &document->GetSelection(), bytes.data(), mw, mh,
                                                       static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)),
                                                       foreground);
        document->RecordEdit("Text", document->GetActiveLayerIndex(), changed, *before);
    } catch (const std::exception& e) {
        UltraCanvasDialogManager::ShowError(std::string("Could not render the text: ") + e.what(), "Text", nullptr, window.get());
    }
#else
    (void)x; (void)y; (void)r;
    UltraCanvasDialogManager::ShowError("The text tool needs a build with libvips.", "Text", nullptr, window.get());
#endif
}

// ===========================================================================
// FILE COMMANDS
// ===========================================================================

void UltraPaintWindow::CmdNew() {
    auto dlg = std::make_shared<UltraPaintNewImageDialog>(lastNewImage);
    dlg->onAccept = [this](const UltraPaintNewImageResult& r) {
        auto go = [this, r]() { NewImage(r); };
        if (document && document->IsModified()) ConfirmDiscard("Discard the unsaved changes and start a new image?", go);
        else go();
    };
    ShowDialog(dlg);
}

void UltraPaintWindow::CmdNewWindow() {
    if (!OpenWindow({})) {
        UltraCanvasDialogManager::ShowError("Could not open another UltraPaint window",
                                            "New Window", nullptr, window.get());
    }
}

// Everything this build will take in the Open / Import dialogs: the bitmap
// formats, the editor's own projects, and whatever vector artwork it can
// rasterize (which depends on libvips and on the registered plugins).
std::vector<std::string> UltraPaintWindow::OpenableExtensions() {
    std::vector<std::string> exts = kImageExtensions;
    exts.push_back("ucraster");
    for (const auto& v : GetVectorRasterExtensions()) {
        if (std::find(exts.begin(), exts.end(), v) == exts.end()) exts.push_back(v);
    }
    for (const auto& m : GetModelRasterExtensions()) {
        if (std::find(exts.begin(), exts.end(), m) == exts.end()) exts.push_back(m);
    }
    return exts;
}

void UltraPaintWindow::CmdOpen() {
    FileDialogOptions opts;
    opts.SetTitle("Open image")
        .AddFilter("Images, drawings and UltraPaint projects", OpenableExtensions())
        .AddFilter("UltraPaint projects", std::vector<std::string>{ "ucraster" })
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetParentWindow(window.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult r, const std::string& path) {
        if (r != DialogResult::OK || path.empty()) return;
        auto go = [this, path]() { OpenFile(path); };
        if (document && document->IsModified()) ConfirmDiscard("Discard the unsaved changes and open " + FileNameOf(path) + "?", go);
        else go();
    });
}

void UltraPaintWindow::CmdImport() {
    FileDialogOptions opts;
    opts.SetTitle("Import image")
        .AddFilter("Images, drawings and UltraPaint projects", OpenableExtensions())
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetParentWindow(window.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult r, const std::string& path) {
        if (r != DialogResult::OK || path.empty()) return;
        // The same question a dropped file asks: merge it into this image, or
        // give it a window of its own.
        ImportFile(path, true);
    });
}

void UltraPaintWindow::CmdSave() {
    if (!document) return;
    if (document->GetFilePath().empty()) { CmdSaveAs(); return; }
    // A layered document saved to a flat format loses its layers: say so once.
    if (document->GetLayerCount() > 1 && !UCRasterDocument::IsProjectFile(document->GetFilePath())) {
        ConfirmDiscard("Saving to " + FileNameOf(document->GetFilePath()) + " flattens the layers in the file (the open image keeps them). Continue?",
                       [this]() { SaveToPath(document->GetFilePath()); });
        return;
    }
    SaveToPath(document->GetFilePath());
}

void UltraPaintWindow::CmdSaveAs() {
    if (!document) return;
    FileDialogOptions opts;
    std::string def = document->GetFilePath().empty() ? "untitled.png" : FileNameOf(document->GetFilePath());
    opts.SetTitle("Save image as")
        .AddFilter("PNG", std::vector<std::string>{ "png" })
        .AddFilter("JPEG", std::vector<std::string>{ "jpg", "jpeg" })
        .AddFilter("WebP", std::vector<std::string>{ "webp" })
        .AddFilter("TIFF", std::vector<std::string>{ "tif", "tiff" })
        .AddFilter("AVIF", std::vector<std::string>{ "avif" })
        .AddFilter("BMP", std::vector<std::string>{ "bmp" })
        .AddFilter("UltraPaint project (layers kept)", std::vector<std::string>{ "ucraster" })
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetDefaultFileName(def)
        .SetParentWindow(window.get());
    UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult r, const std::string& path) {
        // The dialog hands back the name with the chosen type's extension.
        if (r != DialogResult::OK || path.empty()) return;
        SaveToPath(path);
    });
}

void UltraPaintWindow::CmdExport() {
#ifdef HAS_LIBVIPS
    if (!document) return;
    FileDialogOptions opts;
    opts.SetTitle("Export image")
        .AddFilter("Images", kImageExtensions)
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetDefaultFileName(document->GetFilePath().empty() ? "untitled.png" : FileNameOf(document->GetFilePath()))
        .SetParentWindow(window.get());
    UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult r, const std::string& path) {
        if (r != DialogResult::OK || path.empty()) return;
        try {
            auto flat = document->Flatten();
            vips::VImage vimg = flat->ToPixelFX();
            auto dlg = std::make_shared<UltraCanvasImageExportDialog>(vimg);
            dlg->SetFileName(path);
            dlg->onSave = [this, path](const UCImageSave::ImageExportOptions& options) {
                std::string error;
                if (!document->SaveToFile(path, error, &options))
                    UltraCanvasDialogManager::ShowError("Could not export " + path + "\n" + ShortError(error), "Export", nullptr, window.get());
                else if (statusHint) statusHint->SetText("Exported " + FileNameOf(path));
            };
            ShowDialog(dlg);
        } catch (const std::exception& e) {
            UltraCanvasDialogManager::ShowError(std::string("Export failed: ") + e.what(), "Export", nullptr, window.get());
        }
    });
#else
    UltraCanvasDialogManager::ShowError("Export needs a build with libvips.", "Export", nullptr, window.get());
#endif
}

void UltraPaintWindow::CmdQuit() {
    // Quit ends the whole application, so every window's unsaved work is at
    // stake — not only this one's.
    int unsaved = 0;
    for (const auto& editor : OpenWindows()) {
        if (editor && editor->document && editor->document->IsModified()) ++unsaved;
    }
    auto go = []() { if (auto* app = UltraCanvasApplication::GetInstance()) app->RequestExit(); };
    if (unsaved == 0) { go(); return; }
    ConfirmDiscard(unsaved == 1 ? "An image has unsaved changes. Quit anyway?"
                                : std::to_string(unsaved) + " images have unsaved changes. Quit anyway?",
                   go);
}

// ===========================================================================
// EDIT COMMANDS
// ===========================================================================

void UltraPaintWindow::CmdUndo() { if (document) { document->Undo(); UpdateUndoButtons(); } }
void UltraPaintWindow::CmdRedo() { if (document) { document->Redo(); UpdateUndoButtons(); } }

void UltraPaintWindow::CmdCopy(bool merged) {
    if (!document) return;
    Point2Di origin;
    auto layer = merged ? document->CopySelectionMerged(origin) : document->CopySelection(origin);
    if (!layer || !layer->IsValid()) return;
    clipboardLayer = layer;
    clipboardOrigin = origin;
    clipboardPng.clear();
#ifdef HAS_LIBVIPS
    try {
        std::vector<uint8_t> png = PixelFX::FileIO::SaveToBuffer(layer->ToPixelFX(), ".png");
        if (!png.empty()) {
            if (auto* cb = GetClipboard(); cb && cb->SetImage(png, "image/png")) clipboardPng = std::move(png);
        }
    } catch (...) {}
#endif
    if (statusHint) statusHint->SetText("Copied " + std::to_string(layer->GetWidth()) + " x " + std::to_string(layer->GetHeight()) + " pixels");
}

void UltraPaintWindow::CmdCut() {
    if (!document) return;
    CmdCopy(false);
    document->DeleteSelection();
}

void UltraPaintWindow::CmdDelete() {
    if (document) document->DeleteSelection();
}

void UltraPaintWindow::CmdFill(bool fg) {
    if (document) document->FillSelection(fg ? foreground : background);
}

namespace {
    // A picture from the system clipboard as a layer; null when it cannot be
    // decoded. Every backend hands images over as encoded file bytes (PNG
    // on Windows, whatever the source offered elsewhere).
    std::shared_ptr<UCRasterLayer> DecodeClipboardImage(std::vector<uint8_t>& bytes) {
#ifdef HAS_LIBVIPS
        try {
            auto layer = std::make_shared<UCRasterLayer>();
            if (layer->FromPixelFX(PixelFX::FileIO::LoadFromMemory(bytes, ""))) return layer;
        } catch (...) {}
#else
        (void)bytes;
#endif
        return nullptr;
    }
}

// What a paste brings in, in the order a person expects. First the picture
// on the system clipboard - copied in another application, or here. Then an
// image file copied in a file manager (UltraFiler, Explorer, Finder): a file
// list is all the clipboard holds then, and paste used to ignore it and
// report "Nothing to paste". Last this window's own copy, which is the only
// one there is when no system clipboard is. The picture this window put on
// the clipboard itself comes back as the in-app copy: it holds the pixels
// exactly and knows where they were copied from. That used to be decided by
// size, so a different picture of the same size copied in another
// application pasted this window's old copy instead.
UltraPaintWindow::PasteSource UltraPaintWindow::ReadPasteSource() {
    PasteSource source;
    if (auto* cb = GetClipboard()) {
        std::vector<uint8_t> bytes;
        std::string format;
        if (cb->GetImage(bytes, format) && !bytes.empty()) {
            if (clipboardLayer && !clipboardPng.empty() && bytes == clipboardPng) {
                source.image = clipboardLayer;
                source.fromHere = true;
                return source;
            }
            source.image = DecodeClipboardImage(bytes);
            if (source.image && source.image->IsValid()) return source;
            source.image.reset();
        }
        std::vector<std::string> files;
        if (cb->GetFiles(files)) {
            for (const auto& file : files) {
                if (IsOpenablePath(file)) {
                    source.file = file;
                    return source;
                }
            }
        }
    }
    source.image = clipboardLayer;
    source.fromHere = clipboardLayer != nullptr;
    return source;
}

void UltraPaintWindow::CmdPaste() {
    if (!document) return;
    PasteSource source = ReadPasteSource();
    std::string layerName = "Pasted";
    if (!source.file.empty()) {
        // A copied file comes in whole, flattened, at its natural size -
        // drawings and models included, rendered the way a drop renders the
        // extra files it carries.
        auto incoming = LoadDocument(source.file, UltraPaintImportResult{});
        if (!incoming) return;   // LoadDocument said why
        source.image = incoming->Flatten();
        layerName = FileNameOf(source.file);
    }
    auto img = source.image;
    if (!img || !img->IsValid()) {
        OfferLastCopiedImage([this](std::shared_ptr<UCRasterLayer> last) {
            PasteAsLayer(std::move(last), "Last copied image", false, "the last copied image, ");
        });
        return;
    }
    PasteAsLayer(img, layerName, source.fromHere, source.file.empty() ? std::string() : FileNameOf(source.file) + ", ");
}

void UltraPaintWindow::PasteAsLayer(std::shared_ptr<UCRasterLayer> img, const std::string& layerName, bool fromHere,
                                    const std::string& what) {
    if (!document || !img || !img->IsValid()) return;
    auto layer = std::make_shared<UCRasterLayer>(document->GetWidth(), document->GetHeight(), layerName);
    // paste where it was copied from when that still fits, else centred
    int ox = clipboardOrigin.x, oy = clipboardOrigin.y;
    if (!fromHere || ox + img->GetWidth() > document->GetWidth() || oy + img->GetHeight() > document->GetHeight()) {
        ox = (document->GetWidth() - img->GetWidth()) / 2;
        oy = (document->GetHeight() - img->GetHeight()) / 2;
    }
    layer->CopyFrom(*img, ox, oy);
    document->AddLayer(layer);
    SelectTool(PaintToolId::Move);
    if (statusHint) {
        statusHint->SetText("Pasted " + what + std::to_string(img->GetWidth()) + " x " +
                            std::to_string(img->GetHeight()) + " pixels");
    }
}

void UltraPaintWindow::CmdPasteLastImage() {
    if (!document) return;
    std::string when;
    auto img = LastCopiedImage(when);
    if (!img) {
        if (statusHint) statusHint->SetText("There is no image in the clipboard history");
        return;
    }
    PasteAsLayer(img, "Last copied image", false, "the image copied " + when + ", ");
}

// Paste found nothing - text was copied after the picture, say, or nothing
// at all since this window opened. Rather than "Nothing to paste", the
// picture copied last is offered, saying when it was copied: pasted only
// when asked, so a picture from an hour ago never arrives unannounced.
void UltraPaintWindow::OfferLastCopiedImage(const std::function<void(std::shared_ptr<UCRasterLayer>)>& use) {
    std::string when;
    auto img = LastCopiedImage(when);
    if (!img) {
        if (statusHint) statusHint->SetText("Nothing to paste");
        return;
    }
    const std::string question = "There is no picture on the clipboard. Paste the image copied " + when + " (" +
                                 std::to_string(img->GetWidth()) + " x " + std::to_string(img->GetHeight()) +
                                 " pixels)?";
    UltraCanvasDialogManager::ShowConfirmation(question, "UltraPaint", [use, img](bool yes) {
        if (yes) use(img);
    }, window.get());
}

std::shared_ptr<UCRasterLayer> UltraPaintWindow::LastCopiedImage(std::string& when) {
#ifdef ULTRAPAINT_HAVE_CLIPBOARD_HISTORY
    namespace fs = std::filesystem;
    // Only a history the desktop or UltraClipboard keeps: none is started here.
    std::error_code ec;
    if (!UltraCanvasClipboardHistory::IsAvailable() ||
        !fs::is_directory(PathFromUtf8(UltraCanvasClipboardHistory::DefaultDirectory()), ec)) {
        return nullptr;
    }
    UltraCanvasClipboardHistory history;
    if (!history.Open()) return nullptr;
    ClipboardHistoryQuery query;
    query.kinds = {ClipboardEntryKind::Image};
    query.newestFirst = true;
    query.limit = 1;
    const std::vector<ClipboardHistoryEntry> entries = history.List(query);
    if (entries.empty()) return nullptr;
    ClipboardSnapshot snapshot;
    if (!history.ReadFormats(entries[0].id, snapshot.formats)) return nullptr;
    const ClipboardFormat* image = snapshot.FindImage();
    if (!image) return nullptr;
    std::string extension;
    std::vector<uint8_t> bytes = ClipboardImageFile(*image, extension);
    auto layer = DecodeClipboardImage(bytes);
    if (!layer || !layer->IsValid()) return nullptr;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    when = FormatClipboardAge(entries[0].lastUsedAt, now);
    return layer;
#else
    (void)when;
    return nullptr;
#endif
}

void UltraPaintWindow::CmdPasteAsNew() {
    PasteSource source = ReadPasteSource();
    std::shared_ptr<UCRasterDocument> fromFile;
    if (!source.file.empty()) {
        // The file's own layers come along, but not its path: this is a new,
        // unsaved image, so Save asks where to put it instead of writing
        // over the file that was copied.
        fromFile = LoadDocument(source.file, UltraPaintImportResult{});
        if (!fromFile) return;   // LoadDocument said why
        fromFile->SetFilePath("");
        fromFile->ClearHistory();
        fromFile->SetModified(true);
    }
    auto img = source.image;
    if (fromFile) {
        auto go = [this, fromFile]() { SetDocument(fromFile, "Untitled"); };
        if (document && document->IsModified()) ConfirmDiscard("Discard the unsaved changes and paste as a new image?", go);
        else go();
        return;
    }
    if (!img || !img->IsValid()) {
        OfferLastCopiedImage([this](std::shared_ptr<UCRasterLayer> last) { PasteAsNewImage(std::move(last)); });
        return;
    }
    PasteAsNewImage(img);
}

void UltraPaintWindow::PasteAsNewImage(std::shared_ptr<UCRasterLayer> img) {
    if (!img || !img->IsValid()) return;
    auto go = [this, img]() {
        auto doc = std::make_shared<UCRasterDocument>(img->GetWidth(), img->GetHeight(), RasterPixel(0, 0, 0, 0));
        doc->GetLayer(0)->CopyFrom(*img, 0, 0);
        doc->InvalidateComposite();
        doc->ClearHistory();
        doc->SetModified(true);
        SetDocument(doc, "Untitled");
    };
    if (document && document->IsModified()) ConfirmDiscard("Discard the unsaved changes and paste as a new image?", go);
    else go();
}

// ===========================================================================
// IMAGE COMMANDS
// ===========================================================================

void UltraPaintWindow::CmdScaleImage() {
    if (!document) return;
    auto dlg = std::make_shared<UltraPaintResizeDialog>(document->GetWidth(), document->GetHeight(), false);
    dlg->onAccept = [this](const UltraPaintResizeResult& r) { document->ScaleImage(r.width, r.height); surface->ZoomToFit(); };
    ShowDialog(dlg);
}

void UltraPaintWindow::CmdCanvasSize() {
    if (!document) return;
    auto dlg = std::make_shared<UltraPaintResizeDialog>(document->GetWidth(), document->GetHeight(), true);
    dlg->onAccept = [this](const UltraPaintResizeResult& r) {
        const int col = r.anchor % 3, row = r.anchor / 3;
        const int dx = (r.width - document->GetWidth()) * col / 2;
        const int dy = (r.height - document->GetHeight()) * row / 2;
        document->ResizeCanvas(r.width, r.height, dx, dy);
        surface->ZoomToFit();
    };
    ShowDialog(dlg);
}

void UltraPaintWindow::CmdCropToSelection() {
    if (!document) return;
    // The Crop tool's dragged rectangle looks exactly like a selected area -
    // the rest of the image is dimmed around it - so this is the command the
    // user reaches for to apply it. Take it before asking for a selection.
    if (auto* tool = ActiveTool()) {
        if (tool->ApplyPendingCrop(toolContext)) return;
    }
    if (!document->GetSelection().IsActive()) {
        if (statusHint) statusHint->SetText("Nothing is selected: drag an area with a selection tool, or with the Crop tool, first");
        return;
    }
    document->CropTo(document->GetSelection().GetBounds());
    surface->ZoomToFit();
}

void UltraPaintWindow::CmdFlatten() { if (document) document->FlattenImage(); }

// ===========================================================================
// LAYER COMMANDS
// ===========================================================================

void UltraPaintWindow::CmdLayerAdd() { if (document) document->AddLayer(); }
void UltraPaintWindow::CmdLayerDuplicate() { if (document) document->DuplicateLayer(document->GetActiveLayerIndex()); }
void UltraPaintWindow::CmdLayerDelete() {
    if (!document) return;
    if (document->GetLayerCount() <= 1) { if (statusHint) statusHint->SetText("The last layer cannot be deleted"); return; }
    document->RemoveLayer(document->GetActiveLayerIndex());
}
void UltraPaintWindow::CmdLayerMergeDown() { if (document) document->MergeLayerDown(document->GetActiveLayerIndex()); }
void UltraPaintWindow::CmdLayerMove(int delta) {
    if (!document) return;
    const int i = document->GetActiveLayerIndex();
    document->MoveLayer(i, i + delta);
}

void UltraPaintWindow::CmdLayerProperties() {
    if (!document) return;
    auto layer = document->GetActiveLayer();
    if (!layer) return;
    UltraPaintLayerProps init;
    init.name = layer->name; init.opacity = layer->opacity; init.visible = layer->visible; init.locked = layer->locked;
    const auto& modes = AllRasterBlendModes();
    const auto it = std::find(modes.begin(), modes.end(), layer->blendMode);
    init.blendIndex = it == modes.end() ? 0 : static_cast<int>(it - modes.begin());
    auto dlg = std::make_shared<UltraPaintLayerDialog>(init);
    dlg->onAccept = [this](const UltraPaintLayerProps& r) {
        const int i = document->GetActiveLayerIndex();
        document->SetLayerName(i, r.name);
        document->SetLayerOpacity(i, r.opacity);
        document->SetLayerBlendMode(i, AllRasterBlendModes()[static_cast<size_t>(std::max(0, r.blendIndex))]);
        document->SetLayerVisible(i, r.visible);
        document->SetLayerLocked(i, r.locked);
    };
    ShowDialog(dlg);
}

// ===========================================================================
// SELECT COMMANDS
// ===========================================================================

void UltraPaintWindow::CmdSelectAll() { if (document) { document->GetSelection().SelectAll(); document->CommitSelectionChange("Select All"); } }
void UltraPaintWindow::CmdSelectNone() { if (document) { document->GetSelection().SelectNone(); document->CommitSelectionChange("Deselect"); } }
void UltraPaintWindow::CmdSelectInvert() { if (document) { document->GetSelection().Invert(); document->CommitSelectionChange("Invert Selection"); } }

void UltraPaintWindow::CmdSelectFromAlpha() {
    if (!document) return;
    auto layer = document->GetActiveLayer();
    if (!layer) return;
    std::vector<uint8_t> mask(static_cast<size_t>(layer->GetWidth()) * layer->GetHeight());
    for (int y = 0; y < layer->GetHeight(); ++y) {
        const uint8_t* row = layer->Row(y);
        for (int x = 0; x < layer->GetWidth(); ++x) mask[static_cast<size_t>(y) * layer->GetWidth() + x] = row[static_cast<size_t>(x) * 4 + 3];
    }
    document->GetSelection().SetMask(mask);
    document->CommitSelectionChange("Select from Alpha");
}

void UltraPaintWindow::CmdSelectModify(int kind) {
    if (!document) return;
    if (!document->GetSelection().IsActive()) { if (statusHint) statusHint->SetText("Nothing is selected"); return; }
    const char* titles[] = { "Feather Selection", "Grow Selection", "Shrink Selection" };
    const char* prompts[] = { "Feather radius (pixels):", "Grow by (pixels):", "Shrink by (pixels):" };
    UltraCanvasDialogManager::ShowInputDialog(prompts[kind], titles[kind], "4", InputType::Number,
        [this, kind](DialogResult r, const std::string& text) {
            if (r != DialogResult::OK) return;
            const int n = std::clamp(std::atoi(text.c_str()), 0, 500);
            if (n <= 0) return;
            UCRasterSelection& sel = document->GetSelection();
            if (kind == 0) sel.Feather(n); else if (kind == 1) sel.Grow(n); else sel.Shrink(n);
            document->CommitSelectionChange(kind == 0 ? "Feather" : kind == 1 ? "Grow Selection" : "Shrink Selection");
        }, window.get());
}

// ===========================================================================
// ADJUST / FILTER
// ===========================================================================

#ifdef HAS_LIBVIPS
void UltraPaintWindow::BeginPreview() {
    if (!document) return;
    auto layer = document->GetActiveLayer();
    if (!layer) return;
    previewOriginal = layer->Clone();
    previewLayerIndex = document->GetActiveLayerIndex();
    previewActive = true;
}

void UltraPaintWindow::ShowPreview(const std::function<PixelFX::PFXImage(const PixelFX::PFXImage&)>& op) {
    if (!previewActive || !document) return;
    auto layer = document->GetLayer(previewLayerIndex);
    if (!layer || !previewOriginal) return;
    if (!op) {   // preview off: show the original
        layer->CopyFrom(*previewOriginal, 0, 0);
        document->NotifyChanged(layer->GetRect());
        return;
    }
    try {
        UCRasterLayer result;
        if (!result.FromPixelFX(op(previewOriginal->ToPixelFX())) ||
            result.GetWidth() != layer->GetWidth() || result.GetHeight() != layer->GetHeight()) return;
        const UCRasterSelection& sel = document->GetSelection();
        layer->CopyFrom(*previewOriginal, 0, 0);
        const Rect2Di area = sel.IsActive() ? sel.GetBounds() : layer->GetRect();
        for (int y = area.y; y < area.y + area.height; ++y) {
            const uint8_t* s = result.Row(y);
            uint8_t* d = layer->Row(y);
            for (int x = area.x; x < area.x + area.width; ++x) {
                const int cov = sel.Coverage(x, y);
                if (!cov) continue;
                const uint8_t* sp = s + static_cast<size_t>(x) * 4;
                uint8_t* dp = d + static_cast<size_t>(x) * 4;
                for (int c = 0; c < 4; ++c) dp[c] = static_cast<uint8_t>(dp[c] + ((sp[c] - dp[c]) * cov + 127) / 255);
            }
        }
        document->NotifyChanged(layer->GetRect());
    } catch (const std::exception& e) {
        if (statusHint) statusHint->SetText(std::string("Filter failed: ") + e.what());
    }
}

void UltraPaintWindow::EndPreview(bool commit, const std::string& label,
                                  const std::function<PixelFX::PFXImage(const PixelFX::PFXImage&)>& op) {
    if (!previewActive || !document) return;
    previewActive = false;
    auto layer = document->GetLayer(previewLayerIndex);
    if (layer && previewOriginal) {
        // put the original back silently, then apply through the document so
        // the change is one undo entry
        layer->CopyFrom(*previewOriginal, 0, 0);
        document->InvalidateComposite();
        if (commit && op) {
            if (!document->ApplyFilterToLayer(label, previewLayerIndex, op) && statusHint)
                statusHint->SetText(label + " could not be applied");
        } else {
            document->NotifyChanged(layer->GetRect());
        }
    }
    previewOriginal.reset();
    previewLayerIndex = -1;
}
#endif

void UltraPaintWindow::CmdFilter(const PaintFilter& filter) {
#ifdef HAS_LIBVIPS
    if (!document || !filter.apply) return;
    auto layer = document->GetActiveLayer();
    if (!layer || layer->locked) { if (statusHint) statusHint->SetText("The active layer is locked"); return; }
    if (previewActive) { if (statusHint) statusHint->SetText("Finish the open filter dialog first"); return; }
    auto apply = filter.apply;
    if (!filter.HasParams()) {
        std::vector<float> none;
        if (!document->ApplyFilter(filter.name, [apply, none](const PixelFX::PFXImage& img) { return apply(img, none); }) && statusHint)
            statusHint->SetText(filter.name + " could not be applied");
        return;
    }
    BeginPreview();
    auto dlg = std::make_shared<UltraPaintFilterDialog>(filter);
    const std::string label = filter.name.substr(0, filter.name.find("..."));
    dlg->onPreview = [this, apply](const std::vector<float>& values) {
        if (values.empty()) { ShowPreview(nullptr); return; }
        ShowPreview([apply, values](const PixelFX::PFXImage& img) { return apply(img, values); });
    };
    dlg->onAccept = [this, apply, label](const std::vector<float>& values) {
        EndPreview(true, label, [apply, values](const PixelFX::PFXImage& img) { return apply(img, values); });
    };
    dlg->onCancel = [this, label]() { EndPreview(false, label, nullptr); };
    ShowDialog(dlg);
    // initial preview with the defaults
    ShowPreview([apply, v = dlg->GetValues()](const PixelFX::PFXImage& img) { return apply(img, v); });
#else
    (void)filter;
    UltraCanvasDialogManager::ShowError("Filters need a build with libvips.", "Filter", nullptr, window.get());
#endif
}

void UltraPaintWindow::CmdCurves() {
#ifdef HAS_LIBVIPS
    if (!document) return;
    auto layer = document->GetActiveLayer();
    if (!layer || layer->locked) return;
    if (previewActive) { if (statusHint) statusHint->SetText("Finish the open filter dialog first"); return; }
    BeginPreview();
    auto dlg = std::make_shared<UltraCanvasCurvesDialog>(ToneCurveSet());
    // histogram backdrop from the layer
    {
        std::vector<uint32_t> bins[4];
        for (auto& b : bins) b.assign(256, 0);
        for (int y = 0; y < layer->GetHeight(); ++y) {
            const uint8_t* row = layer->Row(y);
            for (int x = 0; x < layer->GetWidth(); ++x) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4;
                if (!p[3]) continue;
                ++bins[1][p[0]]; ++bins[2][p[1]]; ++bins[3][p[2]];
                ++bins[0][(p[0] + p[1] + p[2]) / 3];
            }
        }
        dlg->SetHistogram(ToneCurveChannel::RGB, bins[0]);
        dlg->SetHistogram(ToneCurveChannel::Red, bins[1]);
        dlg->SetHistogram(ToneCurveChannel::Green, bins[2]);
        dlg->SetHistogram(ToneCurveChannel::Blue, bins[3]);
    }
    auto opFor = [](const ToneCurveSet& set) {
        return [set](const PixelFX::PFXImage& img) -> PixelFX::PFXImage {
            if (set.IsIdentity()) return img;
            std::array<std::array<uint8_t, 256>, 3> luts = set.BuildChannelLuts();
            std::vector<std::vector<uint8_t>> tables;
            for (const auto& l : luts) tables.emplace_back(l.begin(), l.end());
            return PaintFilterOnRGB(img, [&](const PixelFX::PFXImage& rgb) { return PixelFX::Colour::MapLut(rgb, tables); });
        };
    };
    dlg->onCurvesChanged = [this, opFor](const ToneCurveSet& set) { ShowPreview(opFor(set)); };
    dlg->onAccept = [this, opFor](const ToneCurveSet& set) { EndPreview(!set.IsIdentity(), "Curves", opFor(set)); };
    dlg->onCancel = [this]() { EndPreview(false, "Curves", nullptr); };
    ShowDialog(dlg);
#else
    UltraCanvasDialogManager::ShowError("Curves needs a build with libvips.", "Curves", nullptr, window.get());
#endif
}

void UltraPaintWindow::CmdColourToAlpha() {
#ifdef HAS_LIBVIPS
    if (!document) return;
    auto layer = document->GetActiveLayer();
    if (!layer || layer->locked) { if (statusHint) statusHint->SetText("The active layer is locked"); return; }
    if (previewActive) { if (statusHint) statusHint->SetText("Finish the open filter dialog first"); return; }
    BeginPreview();

    UltraPaintColourToAlphaParams initial;
    initial.colour = foreground.ToColor();
    initial.colour.a = 255;

    auto opFor = [](const UltraPaintColourToAlphaParams& p) {
        return [p](const PixelFX::PFXImage& img) -> PixelFX::PFXImage {
            if (p.transparency <= 0) return img;
            return PixelFX::Colour::ColourToAlpha(img,
                    { static_cast<double>(p.colour.r), static_cast<double>(p.colour.g), static_cast<double>(p.colour.b) },
                    p.tolerance, p.softness, p.transparency / 100.0, p.despill);
        };
    };

    auto dlg = std::make_shared<UltraPaintColourToAlphaDialog>(initial);
    dlg->onPreview = [this, opFor](const UltraPaintColourToAlphaParams& p, bool enabled) {
        // Not a ternary: opFor returns a lambda, so the two arms have no common type.
        if (enabled) ShowPreview(opFor(p)); else ShowPreview(nullptr);
    };
    dlg->onAccept = [this, opFor](const UltraPaintColourToAlphaParams& p) {
        EndPreview(p.transparency > 0, "Colour to Alpha", opFor(p));
    };
    dlg->onCancel = [this]() { EndPreview(false, "Colour to Alpha", nullptr); };
    ShowDialog(dlg);
    ShowPreview(opFor(initial));
#else
    UltraCanvasDialogManager::ShowError("Colour to Alpha needs a build with libvips.", "Colour to Alpha", nullptr, window.get());
#endif
}

// ===========================================================================
// HELP
// ===========================================================================

void UltraPaintWindow::CmdSettings() {
    // Raise the open window instead of opening a second one.
    if (auto open = settingsDialog.lock()) { open->Show(); return; }
    auto dlg = std::make_shared<UltraPaintSettingsDialog>(&settings);
    // The dialog reads this window's settings, so it must not outlive it:
    // the editor's own close takes it down (onWindowClosing), and the token
    // guards the callbacks for the turn of the loop in between.
    std::weak_ptr<bool> alive = lifeToken;
    dlg->onChanged = [this, alive]() { if (!alive.expired()) ApplySettings(); };
    dlg->onWindowClosed = [this, alive]() { if (!alive.expired()) settingsDialog.reset(); };
    settingsDialog = dlg;
    ShowDialog(dlg);
}

void UltraPaintWindow::CmdAbout() {
    std::string text = "UltraPaint " ULTRAPAINT_VERSION "\n\n"
                       "Bitmap editor built on the UltraCanvas framework.\n"
                       "Raster editing: UCRasterDocument, brush engine, UltraCanvasPaintSurface.\n"
#ifdef HAS_LIBVIPS
                       "Filters and file formats: PixelFX (libvips " + PixelFX::GetVersion() + ").";
#else
                       "This build has no libvips: filters and most file formats are unavailable.";
#endif
    UltraCanvasDialogManager::ShowInformation(text, "About UltraPaint", nullptr, window.get());
}

} // namespace UltraCanvas
