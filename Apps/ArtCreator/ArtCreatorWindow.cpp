// Apps/ArtCreator/ArtCreatorWindow.cpp
// ArtCreator main window: menus, toolbars, palette, canvas, panels, status
// bar, shortcuts, file open / save through the Vector plugin's converters,
// and every command.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "ArtCreatorWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
#include "UltraCanvasVectorFormatsPlugin.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#ifndef ARTCREATOR_VERSION
#define ARTCREATOR_VERSION "0.0.0"
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {

using namespace VectorStorage;
using namespace VectorEdit;

namespace {
    constexpr float kMenuHeight = 28.0f;
    constexpr float kToolbarHeight = 38.0f;
    constexpr float kPaletteWidth = 46.0f;
    constexpr float kRightWidth = 300.0f;
    constexpr float kRightInner = 262.0f;
    constexpr float kStatusHeight = 24.0f;

    std::string FileNameOf(const std::string& path) {
        if (path.empty()) return "Untitled";
        return fs::path(path).filename().string();
    }

    std::string ExtensionOf(const std::string& path) {
        std::string e = fs::path(path).extension().string();
        if (!e.empty() && e[0] == '.') e.erase(0, 1);
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return e;
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

std::string ArtCreatorWindow::TexterIconPath(const std::string& file) const {
    return NormalizePath(GetResourcesDir() + "media/icons/texter/" + file);
}

bool ArtCreatorWindow::Initialize(const std::vector<std::string>& paths) {
    WindowConfig config;
    config.title = "ArtCreator";
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
    canvas->onToolPress = [this](const VectorPointerEvent& e) { if (auto* t = ActiveTool()) t->OnPress(toolContext, e); };
    canvas->onToolDrag = [this](const VectorPointerEvent& e) {
        pointerX = e.doc.x; pointerY = e.doc.y; pointerInside = true;
        if (auto* t = ActiveTool()) t->OnDrag(toolContext, e);
        UpdateStatus();
    };
    canvas->onToolRelease = [this](const VectorPointerEvent& e) { if (auto* t = ActiveTool()) t->OnRelease(toolContext, e); };
    canvas->onToolHover = [this](const VectorPointerEvent& e) {
        pointerX = e.doc.x; pointerY = e.doc.y; pointerInside = true;
        if (auto* t = ActiveTool()) t->OnHover(toolContext, e);
        UpdateStatus();
    };
    canvas->onToolDoubleClick = [this](const VectorPointerEvent& e) { if (auto* t = ActiveTool()) t->OnDoubleClick(toolContext, e); };
    canvas->onToolKey = [this](const UCEvent& ev) { auto* t = ActiveTool(); return t && t->OnKey(toolContext, ev); };
    canvas->onDrawOverlay = [this](IRenderContext* ctx, const VectorViewTransform& v) { if (auto* t = ActiveTool()) t->DrawOverlay(toolContext, ctx, v); };
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
            M::ActionWithShortcut("New...", "Ctrl+N", TexterIconPath("add-document.svg"), [this]() { CmdNew(); }),
            M::ActionWithShortcut("New Window", "Ctrl+Alt+N", [this]() { CmdNewWindow(); }),
            M::ActionWithShortcut("Open...", "Ctrl+O", TexterIconPath("folder-open.svg"), [this]() { CmdOpen(); }),
            M::Separator(),
            M::ActionWithShortcut("Save", "Ctrl+S", TexterIconPath("save.svg"), [this]() { CmdSave(); }),
            M::ActionWithShortcut("Save As...", "Ctrl+Shift+S", [this]() { CmdSaveAs(); }),
            M::ActionWithShortcut("Export...", "Ctrl+E", [this]() { CmdExport(); }),
            M::Separator(),
            M::Action("Document Setup...", [this]() { CmdDocumentSetup(); }),
            M::Separator(),
            M::ActionWithShortcut("Quit", "Ctrl+Q", TexterIconPath("exit.svg"), [this]() { CmdQuit(); }),
        })
        .AddSubmenu("Edit", {
            M::ActionWithShortcut("Undo", "Ctrl+Z", TexterIconPath("undo.svg"), [this]() { CmdUndo(); }),
            M::ActionWithShortcut("Redo", "Ctrl+Y", TexterIconPath("redo.svg"), [this]() { CmdRedo(); }),
            M::Separator(),
            M::ActionWithShortcut("Cut", "Ctrl+X", TexterIconPath("scissors.svg"), [this]() { CmdCut(); }),
            M::ActionWithShortcut("Copy", "Ctrl+C", TexterIconPath("copy.svg"), [this]() { CmdCopy(); }),
            M::ActionWithShortcut("Paste", "Ctrl+V", TexterIconPath("paste.svg"), [this]() { CmdPaste(); }),
            M::ActionWithShortcut("Duplicate", "Ctrl+D", [this]() { CmdDuplicate(); }),
            M::ActionWithShortcut("Delete", "Del", [this]() { CmdDelete(); }),
            M::Separator(),
            M::ActionWithShortcut("Select All", "Ctrl+A", [this]() { CmdSelectAll(); }),
            M::ActionWithShortcut("Select None", "Ctrl+Shift+A", [this]() { CmdSelectNone(); }),
        })
        .AddSubmenu("Arrange", {
            M::ActionWithShortcut("Bring to Front", "Ctrl+F", [this]() { CmdReorder(ZOrderMove::ToFront); }),
            M::ActionWithShortcut("Bring Forward", "Ctrl+Shift+F", [this]() { CmdReorder(ZOrderMove::Forward); }),
            M::ActionWithShortcut("Send Backward", "Ctrl+Shift+B", [this]() { CmdReorder(ZOrderMove::Backward); }),
            M::ActionWithShortcut("Send to Back", "Ctrl+B", [this]() { CmdReorder(ZOrderMove::ToBack); }),
            M::Separator(),
            M::ActionWithShortcut("Group", "Ctrl+G", [this]() { CmdGroup(); }),
            M::ActionWithShortcut("Ungroup", "Ctrl+U", [this]() { CmdUngroup(); }),
            M::Separator(),
            M::Header("Align to selection"),
            M::Action("Left Edges", [this]() { CmdAlign(AlignMode::Left, false); }),
            M::Action("Horizontal Centres", [this]() { CmdAlign(AlignMode::HorizontalCenter, false); }),
            M::Action("Right Edges", [this]() { CmdAlign(AlignMode::Right, false); }),
            M::Action("Top Edges", [this]() { CmdAlign(AlignMode::Top, false); }),
            M::Action("Vertical Centres", [this]() { CmdAlign(AlignMode::VerticalCenter, false); }),
            M::Action("Bottom Edges", [this]() { CmdAlign(AlignMode::Bottom, false); }),
            M::Separator(),
            M::Header("Align to page"),
            M::Action("Centre on Page", [this]() { CmdAlign(AlignMode::HorizontalCenter, true); CmdAlign(AlignMode::VerticalCenter, true); }),
            M::Separator(),
            M::Header("Distribute"),
            M::Action("Horizontal Centres Evenly", [this]() { CmdDistribute(DistributeMode::HorizontalCenters); }),
            M::Action("Vertical Centres Evenly", [this]() { CmdDistribute(DistributeMode::VerticalCenters); }),
            M::Action("Equal Horizontal Gaps", [this]() { CmdDistribute(DistributeMode::HorizontalGaps); }),
            M::Action("Equal Vertical Gaps", [this]() { CmdDistribute(DistributeMode::VerticalGaps); }),
            M::Separator(),
            M::ActionWithShortcut("Convert to Editable Shapes", "Ctrl+Shift+S", [this]() { CmdConvertToPath(); }),
        })
        .AddSubmenu("Object", {
            M::Action("No Fill", [this]() { CmdRemoveFill(); }),
            M::Action("No Line", [this]() { CmdRemoveLine(); }),
            M::Separator(),
            M::Header("Line width"),
            M::Action("Hairline (0.25 pt)", [this]() { CmdLineWidth(0.25f); }),
            M::Action("0.5 pt", [this]() { CmdLineWidth(0.5f); }),
            M::Action("1 pt", [this]() { CmdLineWidth(1.0f); }),
            M::Action("2 pt", [this]() { CmdLineWidth(2.0f); }),
            M::Action("4 pt", [this]() { CmdLineWidth(4.0f); }),
            M::Action("8 pt", [this]() { CmdLineWidth(8.0f); }),
        })
        .AddSubmenu("Layer", {
            M::ActionWithShortcut("New Layer", "Ctrl+Shift+N", [this]() { CmdLayerAdd(); }),
            M::Action("Delete Layer", [this]() { CmdLayerDelete(); }),
            M::Separator(),
            M::Action("Move Layer Up", [this]() { CmdLayerMove(1); }),
            M::Action("Move Layer Down", [this]() { CmdLayerMove(-1); }),
            M::Separator(),
            M::Action("Show Layer", [this]() { CmdLayerToggle(true); }),
            M::Action("Hide Layer", [this]() { CmdLayerToggle(false); }),
        })
        .AddSubmenu("View", {
            M::ActionWithShortcut("Zoom In", "+", [this]() { canvas->ZoomIn(); }),
            M::ActionWithShortcut("Zoom Out", "-", [this]() { canvas->ZoomOut(); }),
            M::ActionWithShortcut("Fit Page", "Ctrl+0", [this]() { canvas->ZoomToPage(); }),
            M::Action("Fit Drawing", [this]() { canvas->ZoomToDrawing(); }),
            M::Action("Fit Selection", [this]() { canvas->ZoomToSelection(); }),
            M::ActionWithShortcut("Actual Size", "Ctrl+1", [this]() { canvas->ZoomToActual(); }),
            M::Separator(),
            M::Checkbox("Rulers", true, [this](bool on) { canvas->SetShowRulers(on); }),
            M::Checkbox("Grid", false, [this](bool on) { VectorGridSpec g = canvas->GetGrid(); g.visible = on; canvas->SetGrid(g); }),
            M::Checkbox("Guides", true, [this](bool on) { canvas->SetShowGuides(on); }),
            M::Separator(),
            M::Checkbox("Snap to Grid", false, [this](bool on) { VectorSnapOptions s = canvas->GetSnapOptions(); s.toGrid = on; canvas->SetSnapOptions(s); }),
            M::Checkbox("Snap to Guides", true, [this](bool on) { VectorSnapOptions s = canvas->GetSnapOptions(); s.toGuides = on; canvas->SetSnapOptions(s); }),
            M::Checkbox("Snap to Objects", true, [this](bool on) { VectorSnapOptions s = canvas->GetSnapOptions(); s.toObjects = on; canvas->SetSnapOptions(s); }),
            M::Checkbox("Snap to Page", true, [this](bool on) { VectorSnapOptions s = canvas->GetSnapOptions(); s.toPage = on; canvas->SetSnapOptions(s); }),
            M::Action("Clear Guides", [this]() { canvas->ClearGuides(); }),
        })
        .AddSubmenu("Help", {
            M::Action("About ArtCreator", [this]() { CmdAbout(); }),
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
    toolbar->SetAppearance(app);
    toolbar->AddButton("ac-tb-new", "", TexterIconPath("add-document.svg"), [this]() { CmdNew(); })->SetTooltip("New drawing (Ctrl+N)");
    toolbar->AddButton("ac-tb-open", "", TexterIconPath("folder-open.svg"), [this]() { CmdOpen(); })->SetTooltip("Open (Ctrl+O)");
    toolbar->AddButton("ac-tb-save", "", TexterIconPath("save.svg"), [this]() { CmdSave(); })->SetTooltip("Save (Ctrl+S)");
    toolbar->AddSeparator("ac-tb-s1");
    undoButton = toolbar->AddButton("ac-tb-undo", "", TexterIconPath("undo.svg"), [this]() { CmdUndo(); });
    undoButton->SetTooltip("Undo (Ctrl+Z)");
    redoButton = toolbar->AddButton("ac-tb-redo", "", TexterIconPath("redo.svg"), [this]() { CmdRedo(); });
    redoButton->SetTooltip("Redo (Ctrl+Y)");
    toolbar->AddSeparator("ac-tb-s2");
    toolbar->AddButton("ac-tb-zoomout", "", TexterIconPath("zoom-out.svg"), [this]() { canvas->ZoomOut(); })->SetTooltip("Zoom out (-)");
    toolbar->AddButton("ac-tb-zoomin", "", TexterIconPath("zoom-in.svg"), [this]() { canvas->ZoomIn(); })->SetTooltip("Zoom in (+)");
    toolbar->AddButton("ac-tb-fit", "", IconPath("zoom-page.svg"), [this]() { canvas->ZoomToPage(); })->SetTooltip("Fit page (Ctrl+0)");
    toolbar->AddButton("ac-tb-100", "", IconPath("zoom-100.svg"), [this]() { canvas->ZoomToActual(); })->SetTooltip("Actual size (Ctrl+1)");
    toolbar->AddSeparator("ac-tb-s3");
    toolbar->AddButton("ac-tb-group", "", IconPath("group.svg"), [this]() { CmdGroup(); })->SetTooltip("Group (Ctrl+G)");
    toolbar->AddButton("ac-tb-ungroup", "", IconPath("ungroup.svg"), [this]() { CmdUngroup(); })->SetTooltip("Ungroup (Ctrl+U)");
    toolbar->AddButton("ac-tb-front", "", IconPath("to-front.svg"), [this]() { CmdReorder(ZOrderMove::ToFront); })->SetTooltip("Bring to front (Ctrl+F)");
    toolbar->AddButton("ac-tb-back", "", IconPath("to-back.svg"), [this]() { CmdReorder(ZOrderMove::ToBack); })->SetTooltip("Send to back (Ctrl+B)");
    window->AddChild(toolbar);
}

void ArtCreatorWindow::BuildToolPalette() {
    palette = std::make_shared<UltraCanvasToolbar>("ac-palette", 0, 0, kPaletteWidth, 0);
    palette->SetOrientation(ToolbarOrientation::Vertical);
    ToolbarAppearance app = palette->GetAppearance();
    app.showIconLabels = false;
    app.itemSpacing = 1.0f;
    palette->SetAppearance(app);
    palette->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    toolButtons.clear();
    for (const auto& t : tools) {
        const ArtToolId id = t->id;
        auto b = palette->AddToggleButton("ac-tool-" + std::to_string(static_cast<int>(id)), "", IconPath(t->iconFile),
                                          [this, id](bool) { SelectTool(id); });
        std::string tip = t->name;
        if (t->shortcutKey) tip += std::string(" (") + t->shortcutKey + ")";
        b->SetTooltip(tip);
        b->SetIconSize(22, 22);
        toolButtons.push_back(b);
    }
    body->AddChild(palette);
}

void ArtCreatorWindow::BuildRightPanel() {
    rightPanel = CreateScrollableContainer("ac-right", 0, 0, kRightWidth, 0);
    rightPanel->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    rightPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    rightPanel->SetPadding(6);
    rightPanel->SetBackgroundColor(Color(244, 244, 246, 255));

    // ----- colour: foreground = fill, background = line -----
    rightPanel->AddChild(PanelTitle("ac-color-title", "Colour (fill / line)"));
    colorPicker = CreateColorPicker("ac-color", fillColour, 0, 0, kRightInner, 300);
    colorPicker->SetUIScale(0.78f);
    colorPicker->SetBackgroundColor(lineColour);
    colorPicker->SetShowAlpha(true);
    colorPicker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
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
    rightPanel->AddChild(swatches);

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

    // ----- tool options -----
    optionsTitle = PanelTitle("ac-options-title", "Tool Options");
    rightPanel->AddChild(optionsTitle);
    optionsPanel = std::make_shared<UltraCanvasContainer>("ac-options", 0, 0, kRightInner, 0);
    optionsPanel->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    optionsPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
    rightPanel->AddChild(optionsPanel);

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

std::vector<std::string> ArtCreatorWindow::OpenableExtensions() {
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
    return UltraCanvasVectorFormatsPlugin().GetSupportedExtensions();
#else
    return {};
#endif
}

std::vector<std::string> ArtCreatorWindow::SaveableExtensions() {
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
    return UltraCanvasVectorFormatsPlugin().GetSaveExtensions();
#else
    return {};
#endif
}

bool ArtCreatorWindow::OpenFile(const std::string& path) {
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
    auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(path);
    if (!converter || !converter->CanImport()) {
        UltraCanvasDialogManager::ShowError("No reader for " + FileNameOf(path) + ".\nThis build opens: svg, xar, emf, wmf, dxf, dwg.", "Open", nullptr, window.get());
        return false;
    }
    std::vector<std::string> warnings;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
    auto doc = converter->Import(path, options);
    if (!doc) {
        UltraCanvasDialogManager::ShowError("Could not read " + path + (warnings.empty() ? "" : "\n" + warnings.front()), "Open", nullptr, window.get());
        return false;
    }
    if (doc->Layers.empty()) doc->AddLayer("Layer 1");
    EnsureIds(*doc);
    SetDocument(doc, path);
    canvas->SetRulerUnit(lastPage.PointsPerUnit(), ArtCreatorPageResult::UnitSymbol(lastPage.unit));
    if (statusHint) {
        std::string s = "Opened " + FileNameOf(path);
        if (!warnings.empty()) s += "  (" + std::to_string(warnings.size()) + " reader note" + (warnings.size() == 1 ? "" : "s") + ": " + warnings.front() + ")";
        statusHint->SetText(s);
    }
    return true;
#else
    UltraCanvasDialogManager::ShowError("This build has no vector file formats (ULTRACANVAS_PLUGIN_VECTOR is off).", "Open", nullptr, window.get());
    (void)path;
    return false;
#endif
}

bool ArtCreatorWindow::SaveToPath(const std::string& path) {
    if (!document) return false;
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
    auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(path);
    if (!converter || !converter->CanExport()) {
        UltraCanvasDialogManager::ShowError("No writer for ." + ExtensionOf(path), "Save", nullptr, window.get());
        return false;
    }
    std::vector<std::string> warnings;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
    if (!converter->Export(*document, path, options)) {
        UltraCanvasDialogManager::ShowError("Could not save " + path + (warnings.empty() ? "" : "\n" + warnings.front()), "Save", nullptr, window.get());
        return false;
    }
    // Only a format that keeps the drawing editable becomes the document's file.
    const std::string ext = ExtensionOf(path);
    if (converter->CanImport()) { documentPath = path; modified = false; }
    UpdateTitle();
    if (statusHint) {
        std::string s = "Saved " + FileNameOf(path);
        if (!warnings.empty()) s += "  (" + std::to_string(warnings.size()) + " writer note" + (warnings.size() == 1 ? "" : "s") + ": " + warnings.front() + ")";
        statusHint->SetText(s);
    }
    return true;
#else
    UltraCanvasDialogManager::ShowError("This build has no vector file formats (ULTRACANVAS_PLUGIN_VECTOR is off).", "Save", nullptr, window.get());
    (void)path;
    return false;
#endif
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
    if (auto* t = ActiveTool()) t->OnSelectionChanged(toolContext);
}

void ArtCreatorWindow::OnSelectionChanged() {
    SyncColourPanelFromSelection();
    if (auto* t = ActiveTool()) t->OnSelectionChanged(toolContext);
    UpdateStatus();
}

void ArtCreatorWindow::UpdateTitle() {
    if (!window) return;
    std::string t = FileNameOf(documentPath);
    if (modified) t = "*" + t;
    window->SetWindowTitle(t + " - ArtCreator");
}

void ArtCreatorWindow::UpdateStatus() {
    if (!document || !statusPos) return;
    char buf[160];
    const double k = lastPage.PointsPerUnit();
    if (pointerInside) std::snprintf(buf, sizeof(buf), "%.1f, %.1f %s", pointerX / k, pointerY / k, ArtCreatorPageResult::UnitSymbol(lastPage.unit));
    else std::snprintf(buf, sizeof(buf), "--, --");
    statusPos->SetText(buf);
    std::snprintf(buf, sizeof(buf), "%.0f %%", canvas->GetZoom() * 100.0);
    statusZoom->SetText(buf);
    if (selection->Empty()) std::snprintf(buf, sizeof(buf), "Nothing selected");
    else if (selection->Count() == 1) {
        const Rect2Dd b = selection->Bounds();
        std::snprintf(buf, sizeof(buf), "1 %s  %.1f x %.1f %s", ElementKindName(*selection->First()), b.width / k, b.height / k, ArtCreatorPageResult::UnitSymbol(lastPage.unit));
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
    std::string def = documentPath.empty() ? "untitled.svg" : FileNameOf(documentPath);
    opts.SetTitle("Save drawing as")
        .AddFilter("SVG (keeps everything)", std::vector<std::string>{ "svg" })
        .AddFilter("Xara XAR", std::vector<std::string>{ "xar" })
        .AddFilter("DXF", std::vector<std::string>{ "dxf" })
        .AddFilter("EMF", std::vector<std::string>{ "emf" })
        .AddFilter("WMF", std::vector<std::string>{ "wmf" })
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetDefaultFileName(def)
        .SetParentWindow(window.get());
    UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult r, const std::string& path) {
        if (r != DialogResult::OK || path.empty()) return;
        std::string p = path;
        if (fs::path(p).extension().empty()) p += ".svg";
        SaveToPath(p);
    });
}

void ArtCreatorWindow::CmdExport() {
    if (!document) return;
    FileDialogOptions opts;
    std::string def = documentPath.empty() ? "untitled.pdf" : fs::path(documentPath).stem().string() + ".pdf";
    opts.SetTitle("Export drawing")
        .AddFilter("PDF", std::vector<std::string>{ "pdf" })
        .AddFilter("Adobe Illustrator", std::vector<std::string>{ "ai" })
        .AddFilter("EPS", std::vector<std::string>{ "eps" })
        .AddFilter("CorelDRAW", std::vector<std::string>{ "cdr" })
        .AddFilter("SVG", std::vector<std::string>{ "svg" })
        .AddFilter("Xara XAR", std::vector<std::string>{ "xar" })
        .AddFilter("DXF", std::vector<std::string>{ "dxf" })
        .AddFilter("EMF", std::vector<std::string>{ "emf" })
        .AddFilter("WMF", std::vector<std::string>{ "wmf" })
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetDefaultFileName(def)
        .SetParentWindow(window.get());
    UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult r, const std::string& path) {
        if (r != DialogResult::OK || path.empty()) return;
        std::string p = path;
        if (fs::path(p).extension().empty()) p += ".pdf";
        SaveToPath(p);
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
    for (const auto& e : SortByDrawingOrder(selection->Elements())) if (e) clipboard.push_back(e->Clone());
    pasteCount = 0;
    if (statusHint && !clipboard.empty()) statusHint->SetText(std::to_string(clipboard.size()) + " object" + (clipboard.size() == 1 ? "" : "s") + " copied");
}

void ArtCreatorWindow::CmdCut() {
    if (selection->Empty()) return;
    CmdCopy();
    CmdDelete();
}

void ArtCreatorWindow::CmdPaste() {
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
    for (size_t i = 0; i < tools.size() && i < toolButtons.size(); ++i) {
        const bool on = tools[i]->id == id;
        if (toolButtons[i]->IsPressed() != on) toolButtons[i]->SetPressed(on);
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

} // namespace UltraCanvas
