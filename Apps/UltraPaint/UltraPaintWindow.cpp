// Apps/UltraPaint/UltraPaintWindow.cpp
// UltraPaint main window: composition of the framework elements (menu bar,
// toolbars, paint surface, colour picker, option / layer panels, status
// bar) and every command the menus and shortcuts run.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraPaintWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasSeparator.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"
#include "../dialogs/UltraCanvasCurvesDialog.h"
#ifdef HAS_LIBVIPS
#include "../dialogs/UltraCanvasImageExportDialog.h"
#include "PixelFX/PixelFX.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {
    constexpr int   kMenuHeight    = 28;
    constexpr int   kToolbarHeight = 38;
    constexpr int   kPaletteWidth  = 46;
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
        return fs::path(path).filename().string();
    }

    std::string HexOf(const RasterPixel& p) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", p.r, p.g, p.b);
        return buf;
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

std::string UltraPaintWindow::TexterIconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/texter/" + file);
}

bool UltraPaintWindow::Initialize(const std::vector<std::string>& paths) {
    WindowConfig config;
    config.title = "UltraPaint";
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
        dlg->Create();
        dlg->Show();
    };
    toolContext.refreshOptions = [this]() { RebuildToolOptions(); };

    // ----- surface -> tool -----
    surface->onToolPress = [this](const PaintPointerEvent& e) { if (auto* t = ActiveTool()) t->OnPress(toolContext, e); };
    surface->onToolDrag = [this](const PaintPointerEvent& e) {
        pointerX = e.x; pointerY = e.y; pointerInside = e.insideImage;
        if (auto* t = ActiveTool()) t->OnDrag(toolContext, e);
        UpdateStatus();
    };
    surface->onToolRelease = [this](const PaintPointerEvent& e) { if (auto* t = ActiveTool()) t->OnRelease(toolContext, e); };
    surface->onToolHover = [this](const PaintPointerEvent& e) {
        pointerX = e.x; pointerY = e.y; pointerInside = e.insideImage;
        if (auto* t = ActiveTool()) t->OnHover(toolContext, e);
        UpdateStatus();
    };
    surface->onToolDoubleClick = [this](const PaintPointerEvent& e) { if (auto* t = ActiveTool()) t->OnDoubleClick(toolContext, e); };
    surface->onToolKey = [this](const UCEvent& ev) { auto* t = ActiveTool(); return t && t->OnKey(toolContext, ev); };
    surface->onDrawOverlay = [this](IRenderContext* ctx, const PaintViewTransform& v) { if (auto* t = ActiveTool()) t->DrawOverlay(toolContext, ctx, v); };
    surface->onViewChanged = [this]() { UpdateStatus(); };
    surface->onFilesDropped = [this](const std::vector<std::string>& files) { if (!files.empty()) OpenFile(files.front()); };

    InstallShortcuts();

    window->onWindowClosing = [this]() -> bool {
        if (!document || !document->IsModified()) return true;
        ConfirmDiscard("The image has unsaved changes. Close anyway?", [this]() {
            document->SetModified(false);
            window->Close();
        });
        return false;
    };

    // ----- first document -----
    if (!paths.empty()) {
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
        auto item = M::Action(f.name, [this, fp]() { CmdFilter(*fp); });
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
                       M::ActionWithShortcut("Curves...", "Ctrl+M", [this]() { CmdCurves(); }));

    menuBar = MenuBuilder("up-menubar", 0, 0, 0, kMenuHeight)
        .SetType(MenuType::Menubar)
        .AddSubmenu("File", {
            M::ActionWithShortcut("New...", "Ctrl+N", TexterIconPath("add-document.svg"), [this]() { CmdNew(); }),
            M::ActionWithShortcut("Open...", "Ctrl+O", TexterIconPath("folder-open.svg"), [this]() { CmdOpen(); }),
            M::Separator(),
            M::ActionWithShortcut("Save", "Ctrl+S", TexterIconPath("save.svg"), [this]() { CmdSave(); }),
            M::ActionWithShortcut("Save As...", "Ctrl+Shift+S", [this]() { CmdSaveAs(); }),
            M::ActionWithShortcut("Export with Options...", "Ctrl+E", IconPath("export.svg"), [this]() { CmdExport(); }),
            M::Separator(),
            M::ActionWithShortcut("Quit", "Ctrl+Q", TexterIconPath("exit.svg"), [this]() { CmdQuit(); }),
        })
        .AddSubmenu("Edit", {
            M::ActionWithShortcut("Undo", "Ctrl+Z", TexterIconPath("undo.svg"), [this]() { CmdUndo(); }),
            M::ActionWithShortcut("Redo", "Ctrl+Y", TexterIconPath("redo.svg"), [this]() { CmdRedo(); }),
            M::Separator(),
            M::ActionWithShortcut("Cut", "Ctrl+X", TexterIconPath("scissors.svg"), [this]() { CmdCut(); }),
            M::ActionWithShortcut("Copy", "Ctrl+C", TexterIconPath("copy.svg"), [this]() { CmdCopy(false); }),
            M::ActionWithShortcut("Copy Merged", "Ctrl+Shift+C", [this]() { CmdCopy(true); }),
            M::ActionWithShortcut("Paste as New Layer", "Ctrl+V", TexterIconPath("paste.svg"), [this]() { CmdPaste(); }),
            M::ActionWithShortcut("Paste as New Image", "Ctrl+Shift+V", [this]() { CmdPasteAsNew(); }),
            M::Separator(),
            M::ActionWithShortcut("Delete", "Del", [this]() { CmdDelete(); }),
            M::ActionWithShortcut("Fill with Foreground", "Alt+Backspace", [this]() { CmdFill(true); }),
            M::ActionWithShortcut("Fill with Background", "Ctrl+Backspace", [this]() { CmdFill(false); }),
        })
        .AddSubmenu("Image", {
            M::Action("Scale Image...", [this]() { CmdScaleImage(); }),
            M::Action("Canvas Size...", [this]() { CmdCanvasSize(); }),
            M::ActionWithShortcut("Crop to Selection", "Ctrl+Shift+X", [this]() { CmdCropToSelection(); }),
            M::Separator(),
            M::Action("Flip Horizontal", [this]() { if (document) document->FlipHorizontal(); }),
            M::Action("Flip Vertical", [this]() { if (document) document->FlipVertical(); }),
            M::Action("Rotate 90° Clockwise", [this]() { if (document) { document->Rotate90(true); surface->ZoomToFit(); } }),
            M::Action("Rotate 90° Counter-clockwise", [this]() { if (document) { document->Rotate90(false); surface->ZoomToFit(); } }),
            M::Action("Rotate 180°", [this]() { if (document) document->Rotate180(); }),
            M::Separator(),
            M::Action("Flatten Image", [this]() { CmdFlatten(); }),
        })
        .AddSubmenu("Layer", {
            M::ActionWithShortcut("New Layer", "Ctrl+Shift+N", IconPath("layer-add.svg"), [this]() { CmdLayerAdd(); }),
            M::ActionWithShortcut("Duplicate Layer", "Ctrl+J", IconPath("layer-duplicate.svg"), [this]() { CmdLayerDuplicate(); }),
            M::Action("Delete Layer", IconPath("layer-delete.svg"), [this]() { CmdLayerDelete(); }),
            M::Separator(),
            M::ActionWithShortcut("Merge Down", "Ctrl+Shift+M", IconPath("layer-merge.svg"), [this]() { CmdLayerMergeDown(); }),
            M::Action("Move Layer Up", IconPath("layer-up.svg"), [this]() { CmdLayerMove(1); }),
            M::Action("Move Layer Down", IconPath("layer-down.svg"), [this]() { CmdLayerMove(-1); }),
            M::Separator(),
            M::Action("Layer Properties...", IconPath("layer-props.svg"), [this]() { CmdLayerProperties(); }),
        })
        .AddSubmenu("Select", {
            M::ActionWithShortcut("All", "Ctrl+A", [this]() { CmdSelectAll(); }),
            M::ActionWithShortcut("None", "Ctrl+Shift+A", [this]() { CmdSelectNone(); }),
            M::ActionWithShortcut("Invert", "Ctrl+I", [this]() { CmdSelectInvert(); }),
            M::Action("From Layer Alpha", [this]() { CmdSelectFromAlpha(); }),
            M::Separator(),
            M::Action("Feather...", [this]() { CmdSelectModify(0); }),
            M::Action("Grow...", [this]() { CmdSelectModify(1); }),
            M::Action("Shrink...", [this]() { CmdSelectModify(2); }),
        })
        .AddSubmenu("Adjust", adjustItems)
        .AddSubmenu("Filter", filterItems)
        .AddSubmenu("View", {
            M::ActionWithShortcut("Zoom In", "+", IconPath("zoom.svg"), [this]() { surface->ZoomIn(); }),
            M::ActionWithShortcut("Zoom Out", "-", [this]() { surface->ZoomOut(); }),
            M::ActionWithShortcut("Fit to Window", "Ctrl+0", IconPath("zoom-fit.svg"), [this]() { surface->ZoomToFit(); }),
            M::ActionWithShortcut("Actual Pixels", "Ctrl+1", IconPath("zoom-100.svg"), [this]() { surface->ZoomToActual(); }),
            M::Separator(),
            M::Checkbox("Pixel Grid (at 8x and above)", true, [this](bool on) { surface->SetShowPixelGrid(on); }),
            M::Checkbox("Show Selection Outline", true, [this](bool on) { surface->SetShowSelection(on); }),
        })
        .AddSubmenu("Help", {
            M::Action("About UltraPaint", [this]() { CmdAbout(); }),
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
    window->AddChild(toolbar);
}

void UltraPaintWindow::BuildToolPalette() {
    palette = std::make_shared<UltraCanvasToolbar>("up-palette", 0, 0, kPaletteWidth, 0);
    palette->SetOrientation(ToolbarOrientation::Vertical);
    ToolbarAppearance app = palette->GetAppearance();
    app.showIconLabels = false;
    app.itemSpacing = 1.0f;
    palette->SetAppearance(app);
    palette->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    toolButtons.clear();
    for (const auto& t : tools) {
        const PaintToolId id = t->id;
        auto b = palette->AddToggleButton("up-tool-" + std::to_string(static_cast<int>(id)), "", IconPath(t->iconFile),
                                          [this, id](bool) { SelectTool(id); });
        std::string tip = t->name;
        if (t->shortcutKey) tip += std::string(" (") + t->shortcutKey + ")";
        b->SetTooltip(tip);
        b->SetIconSize(22, 22);
        toolButtons.push_back(b);
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
    colorPicker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
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
                case UCKeys::N: if (e.shift) CmdLayerAdd(); else CmdNew(); return true;
                case UCKeys::O: CmdOpen(); return true;
                case UCKeys::S: if (e.shift) CmdSaveAs(); else CmdSave(); return true;
                case UCKeys::E: CmdExport(); return true;
                case UCKeys::Q: CmdQuit(); return true;
                case UCKeys::Z: if (e.shift) CmdRedo(); else CmdUndo(); return true;
                case UCKeys::Y: CmdRedo(); return true;
                case UCKeys::X: if (e.shift) CmdCropToSelection(); else CmdCut(); return true;
                case UCKeys::C: CmdCopy(e.shift); return true;
                case UCKeys::V: if (e.shift) CmdPasteAsNew(); else CmdPaste(); return true;
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
    document = std::move(doc);
    document->onStateChanged = [this]() { UpdateUndoButtons(); UpdateTitle(); };
    surface->SetDocument(document);
    // the surface installed its own structure hook; chain the panel refresh
    auto surfaceStructure = document->onStructureChanged;
    document->onStructureChanged = [this, surfaceStructure]() {
        if (surfaceStructure) surfaceStructure();
        RebuildLayerPanel();
        UpdateStatus();
    };
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
    auto doc = std::make_shared<UCRasterDocument>();
    std::string error;
    if (!doc->LoadFromFile(path, error)) {
        UltraCanvasDialogManager::ShowError("Could not open " + path + "\n" + error, "Open", nullptr, window.get());
        return;
    }
    SetDocument(doc, FileNameOf(path));
    if (statusHint) statusHint->SetText("Opened " + FileNameOf(path));
}

bool UltraPaintWindow::SaveToPath(const std::string& path) {
    if (!document) return false;
    std::string error;
    if (!document->SaveToFile(path, error)) {
        UltraCanvasDialogManager::ShowError("Could not save " + path + "\n" + error, "Save", nullptr, window.get());
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
    window->SetWindowTitle(t + " - UltraPaint");
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
    for (size_t i = 0; i < tools.size() && i < toolButtons.size(); ++i) {
        const bool on = tools[i]->id == id;
        if (toolButtons[i]->IsPressed() != on) toolButtons[i]->SetPressed(on);
    }
    if (!t) return;
    t->Activate(toolContext);
    surface->SetToolCursor(t->Cursor());
    surface->SetCursorRadius(t->CursorRadius(toolContext));
    surface->SetCursorSquare(t->CursorSquare(toolContext));
    if (statusHint) statusHint->SetText(t->name + ": " + t->hint);
    RebuildToolOptions();
    surface->Refresh();
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

void UltraPaintWindow::RebuildLayerPanel() {
    if (!layerRows || !document) return;
    layerRows->ClearChildren();
    const int n = document->GetLayerCount();
    const int active = document->GetActiveLayerIndex();
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
        std::string label = layer->name;
        if (layer->locked) label += "  [locked]";
        auto btn = std::make_shared<UltraCanvasButton>("up-lsel-" + std::to_string(i), 0, 0, 0, 24, label);
        btn->SetCanToggled(true);
        btn->SetPressed(i == active);
        ButtonStyle st = btn->GetStyle();
        st.fontSize = 11.0f;
        btn->SetStyle(st);
        btn->onToggle = [this, i](bool) {
            document->SetActiveLayerIndex(i);
            RebuildLayerPanel();
        };
        btn->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(btn);
        layerRows->AddChild(row);
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
    dlg->Create();
    dlg->Show();
}

void UltraPaintWindow::CmdOpen() {
    FileDialogOptions opts;
    std::vector<std::string> exts = kImageExtensions;
    exts.push_back("ucraster");
    opts.SetTitle("Open image")
        .AddFilter("Images and UltraPaint projects", exts)
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
        if (r != DialogResult::OK || path.empty()) return;
        std::string p = path;
        if (fs::path(p).extension().empty()) p += ".png";
        SaveToPath(p);
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
                    UltraCanvasDialogManager::ShowError("Could not export " + path + "\n" + error, "Export", nullptr, window.get());
                else if (statusHint) statusHint->SetText("Exported " + FileNameOf(path));
            };
            dlg->Create();
            dlg->Show();
        } catch (const std::exception& e) {
            UltraCanvasDialogManager::ShowError(std::string("Export failed: ") + e.what(), "Export", nullptr, window.get());
        }
    });
#else
    UltraCanvasDialogManager::ShowError("Export needs a build with libvips.", "Export", nullptr, window.get());
#endif
}

void UltraPaintWindow::CmdQuit() {
    auto go = []() { if (auto* app = UltraCanvasApplication::GetInstance()) app->RequestExit(); };
    if (document && document->IsModified()) ConfirmDiscard("The image has unsaved changes. Quit anyway?", go);
    else go();
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
#ifdef HAS_LIBVIPS
    try {
        std::vector<uint8_t> png = PixelFX::FileIO::SaveToBuffer(layer->ToPixelFX(), ".png");
        if (!png.empty()) if (auto* cb = GetClipboard()) cb->SetImage(png, "image/png");
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
    // The clipboard image as a layer: the system clipboard first (another
    // application may have put a picture there), then the in-app copy.
    std::shared_ptr<UCRasterLayer> ClipboardImage(const std::shared_ptr<UCRasterLayer>& internal) {
#ifdef HAS_LIBVIPS
        if (auto* cb = GetClipboard()) {
            std::vector<uint8_t> bytes;
            std::string format;
            if (cb->GetImage(bytes, format) && !bytes.empty()) {
                try {
                    auto layer = std::make_shared<UCRasterLayer>();
                    if (layer->FromPixelFX(PixelFX::FileIO::LoadFromMemory(bytes, ""))) {
                        // Prefer the in-app copy when it is byte-identical in size
                        // (it carries straight alpha exactly); otherwise the system one.
                        if (!internal || internal->GetWidth() != layer->GetWidth() || internal->GetHeight() != layer->GetHeight())
                            return layer;
                    }
                } catch (...) {}
            }
        }
#endif
        return internal;
    }
}

void UltraPaintWindow::CmdPaste() {
    if (!document) return;
    auto img = ClipboardImage(clipboardLayer);
    if (!img || !img->IsValid()) { if (statusHint) statusHint->SetText("Nothing to paste"); return; }
    auto layer = std::make_shared<UCRasterLayer>(document->GetWidth(), document->GetHeight(), "Pasted");
    // paste where it was copied from when that still fits, else centred
    int ox = clipboardOrigin.x, oy = clipboardOrigin.y;
    if (img != clipboardLayer || ox + img->GetWidth() > document->GetWidth() || oy + img->GetHeight() > document->GetHeight()) {
        ox = (document->GetWidth() - img->GetWidth()) / 2;
        oy = (document->GetHeight() - img->GetHeight()) / 2;
    }
    layer->CopyFrom(*img, ox, oy);
    document->AddLayer(layer);
    SelectTool(PaintToolId::Move);
}

void UltraPaintWindow::CmdPasteAsNew() {
    auto img = ClipboardImage(clipboardLayer);
    if (!img || !img->IsValid()) { if (statusHint) statusHint->SetText("Nothing to paste"); return; }
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
    dlg->Create();
    dlg->Show();
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
    dlg->Create();
    dlg->Show();
}

void UltraPaintWindow::CmdCropToSelection() {
    if (!document) return;
    if (!document->GetSelection().IsActive()) { if (statusHint) statusHint->SetText("Select an area to crop to first"); return; }
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
    dlg->Create();
    dlg->Show();
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
    dlg->Create();
    dlg->Show();
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
    dlg->Create();
    dlg->Show();
#else
    UltraCanvasDialogManager::ShowError("Curves needs a build with libvips.", "Curves", nullptr, window.get());
#endif
}

// ===========================================================================
// HELP
// ===========================================================================

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
