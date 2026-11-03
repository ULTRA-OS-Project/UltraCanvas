// Apps/ArtCreator/ArtCreatorWindow.cpp
// ArtCreator main window: menus, toolbars, palette, canvas, panels, status
// bar, shortcuts, file open / save through the Vector plugin's converters,
// and every command.
// Version: 1.11.0 - text is typed on the page (an UltraCanvasRichTextEdit over the canvas, at its
//                  zoom, in the text's font, colour, alignment and spacing) and edited where it
//                  lies; the text bar - font, size, bold, italic, underline, strikethrough,
//                  alignment, line and letter spacing - in the transform bar's place with the
//                  Text tool; the Text dialog is gone
// Version: 1.10.1 - Convert to Editable Shapes is Ctrl+Shift+C: the menu showed Ctrl+Shift+S,
//                  which is Save As
// Version: 1.10.0 - the right panel's sections are SectionCard group boxes on the
//                  section page, as UltraPaint's, with a smaller caption; Tool Options,
//                  Gradient (was Fill) and Line show only while their tool is active; the transform
//                  bar's ratio lock holds for the selector's handles, its percentage
//                  fields have no arrows and every field steps by 1; the Line card also
//                  with the Selector or Shape Editor while a line is selected
// Version: 1.9.0 - the colour picker opens with the design and channels it was left with
//                 (ArtCreatorSettings: picker.design, picker.channels in config.ini)
// Version: 1.8.2 - the colour picker offers its design list, folds its channels and is
//                 as tall as its content (SetAutoHeight)
// Version: 1.8.1 - the colour picker in the framework's light style (ColorPickerStyle::Light)
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
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "ArtCreatorWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardDib.h"   // ClipboardDib::EncodePng
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasTextUtils.h"      // Base64Encode
#include "UltraCanvasVectorRaster.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasTextFormatBar.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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
    constexpr float kMenuHeight = 28.0f;
    constexpr float kToolbarHeight = 38.0f;
    constexpr float kPaletteWidth = 48.0f;
    constexpr float kToolIcon = 28.0f;     // the palette's icons (its buttons: ArtCreatorCommands.cpp)
    constexpr float kRightWidth = 300.0f;
    constexpr float kRightInner = 268.0f;   // what is visible next to the scrollbar, less its gap
    constexpr float kStatusHeight = 24.0f;
    constexpr float kPanelGap = 8.0f;       // between the cards, and around them

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
        return f && (dynamic_cast<UltraCanvasTextInput*>(f) != nullptr ||
                     dynamic_cast<UltraCanvasRichTextEdit*>(f) != nullptr);
    }

    // ui-reuse-exempt: helper that only sizes framework elements; nothing is painted here.
    // One section of the right panel, as UltraPaint's: a SectionCard captioned
    // `title`, its content a column, as wide as what is visible beside the
    // scrollbar and as tall as its content. The caption is smaller than the
    // card's own: it only names the section, the content is what matters.
    std::shared_ptr<UltraCanvasGroupBox> PanelCard(const std::string& id, const std::string& title) {
        auto card = CreateGroupBox(id, 0, 0, kRightInner, 0, title);
        card->SetFrameStyle(GroupBoxFrameStyle::Header);
        GroupBoxVisualStyle style = GroupBoxVisualStyle::SectionCard();
        style.titleFont.fontSize = 9.0f;
        style.titleVerticalPadding = 2.0f;
        card->SetVisualStyle(style);
        card->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        card->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
        return card;
    }

    // What a card leaves its content: its width, less its border and its
    // padding on each side.
    float CardInnerWidth(const UltraCanvasGroupBox& card) {
        const GroupBoxVisualStyle& s = card.GetVisualStyle();
        return kRightInner - 2.0f * (s.borderWidth + s.contentPadding);
    }

    // A line in the sense of the Line card: an open stroke that arrowheads,
    // a width profile and a brush go along - a straight line, a polyline or
    // an open path, not a closed shape.
    bool IsOpenLine(const VectorElement& e) {
        switch (e.Type) {
            case VectorElementType::Line: case VectorElementType::Polyline: return true;
            case VectorElementType::Path: {
                const auto* path = dynamic_cast<const VectorPath*>(&e);
                if (!path || path->Path.Closed) return false;
                for (const auto& c : path->Path.commands)
                    if (c.Type == PathCommandType::ClosePath) return false;
                return true;
            }
            default: return false;
        }
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

    settings.Load();   // no file yet: the defaults stand
    tools = CreateArtTools();
    // Every command once; the menus, toolbars and keys are built from them,
    // in the arrangement the user left (or the program's).
    RegisterCommands();
    LoadLayouts();

    BuildMenuBar();
    BuildToolbar();
    BuildTransformBar();
    BuildTextBar();

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
    toolContext.requestText = [this](const Point2Dd& at, const std::shared_ptr<VectorText>& text) {
        BeginTextEdit(at, text);
    };
    toolContext.selectTool = [this](ArtToolId id) { SelectTool(id); };
    toolContext.refreshOptions = [this]() { RebuildToolOptions(); };
    toolContext.lockAspectRatio = lockAspectRatio;
    toolContext.iconPath = [this](const std::string& file) { return IconPath(file); };
    toolContext.setRotationHandleStyle = [this](VectorRotationHandleStyle style) {
        canvas->SetRotationHandleStyle(style);
        settings.Update([style](ArtCreatorSettings& s) { s.rotationHandle = style; });
    };
    canvas->SetRotationHandleStyle(settings.rotationHandle);

    // ----- canvas -> tool -----
    // In icon mode the pointer goes to the icon's pixel grid first (IconEvent).
    canvas->onToolPress = [this](const VectorPointerEvent& e) { if (auto* t = ActiveTool()) t->OnPress(toolContext, IconEvent(e)); };
    canvas->onToolDrag = [this](const VectorPointerEvent& e) {
        pointerX = e.doc.x; pointerY = e.doc.y; pointerInside = true;
        if (auto* t = ActiveTool()) t->OnDrag(toolContext, IconEvent(e));
        RefreshTransformBar();   // the selection follows a drag live
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
        DrawTextEditFrame(ctx);
        if (auto* t = ActiveTool()) t->DrawOverlay(toolContext, ctx, v);
    };
    canvas->onViewChanged = [this]() { UpdateStatus(); PlaceTextEditor(); };
    canvas->onHandleModeChanged = [this](VectorHandleMode) { ShowCentreGroup(); };
    canvas->onRotationCenterChanged = [this]() { RefreshTransformBar(); };
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
    RefreshCommandStates();   // the checks of View, now the canvas is there
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
    // The entries come from the commands, in the menu layout
    // (ArtCreatorCommands.cpp).
    menuBar = std::make_shared<UltraCanvasMenu>("ac-menubar", 0, 0, 0, kMenuHeight);
    menuBar->SetMenuType(MenuType::Menubar);
    ApplyMenuLayout();
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
    // What does not fit a narrow window goes into the chevron's menu.
    toolbar->SetOverflowMode(ToolbarOverflowMode::Menu);
    // The buttons come from the commands, in the toolbar's groups; a right
    // click shows, hides and folds the groups.
    ApplyToolbarLayout();
    InstallToolbarContextMenu(*toolbar, toolbarLayout, commands,
                              [this]() { ApplyToolbarLayout(); SaveLayouts(); },
                              [this]() { OpenCustomise("t0"); });
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

// ===========================================================================
// TRANSFORM BAR
// ===========================================================================

namespace {
    constexpr float kTransformBarHeight = 50.0f;
    constexpr float kTransformRowHeight = 21.0f;
    // Every field of the bar is this wide, in both rows.
    constexpr float kTransformFieldWidth = 96.0f;
    constexpr double kDegrees = 180.0 / 3.14159265358979323846;
}

void ArtCreatorWindow::BuildTransformBar() {
    transformBar = std::make_shared<UltraCanvasContainer>("ac-transform", 0, 0, 0, kTransformBarHeight);
    transformBar->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    transformBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    transformBar->SetPadding(0, 8);
    transformBar->SetBackgroundColor(Color(228, 228, 232, 255));

    referencePicker = CreateReferencePointPicker("ac-tf-reference", 0, 0, 40, 40);
    referencePicker->SetTooltip("Reference point: the point of the selection that stays put");
    referencePicker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    referencePicker->onPointChanged = [this](ReferencePoint) { RefreshTransformBar(); };
    transformBar->AddChild(referencePicker);

    // A column of two rows, each a caption and a field.
    auto column = [&](const std::string& id, float width) {
        auto c = std::make_shared<UltraCanvasContainer>("ac-tf-col-" + id, 0, 0, width, kTransformBarHeight - 4);
        c->layout.SetFlexColumn().SetFlexGap(4).SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
        c->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        transformBar->AddChild(c);
        return std::make_pair(c, width);
    };
    auto row = [&](const std::pair<std::shared_ptr<UltraCanvasContainer>, float>& col, const std::string& id) {
        auto r = std::make_shared<UltraCanvasContainer>("ac-tf-row-" + id, 0, 0, col.second, kTransformRowHeight);
        r->layout.SetFlexRow().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        r->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        col.first->AddChild(r);
        return r;
    };
    auto caption = [&](const std::shared_ptr<UltraCanvasContainer>& r, const std::string& id, const std::string& text) {
        auto l = CreateLabel("ac-tf-cap-" + id, 0, 0, 14, kTransformRowHeight, text);
        l->SetFontSize(11);
        l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        r->AddChild(l);
    };
    auto icon = [&](const std::shared_ptr<UltraCanvasContainer>& r, const std::string& id, const std::string& file) {
        auto image = CreateImageFromFile("ac-tf-icon-" + id, 0, 0, 16, 16, MenuIconPath(file));
        image->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        r->AddChild(image);
    };
    auto field = [&](const std::shared_ptr<UltraCanvasContainer>& r, const std::string& id, float width,
                     double minimum, double maximum, int decimals, double step, const std::string& tooltip,
                     std::function<void(double, bool)> apply) {
        auto s = CreateSpinner("ac-tf-" + id, 0, 0, width, kTransformRowHeight);
        s->SetDecimals(decimals);
        s->SetSnapToStep(false);
        s->SetRange(minimum, maximum);
        s->SetStep(step);
        s->SetLayout(SpinnerLayout::UpDownRight);
        s->SetButtonThickness(14.0f);
        s->GetStyle().fontStyle.fontSize = 11.0f;
        s->SetTooltip(tooltip);
        s->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // Every step applies at once, so the drawing follows a held arrow;
        // the steps of one run go into one undo step, which the run's end
        // closes.
        s->onValueChanging = [this](double) { if (!syncingTransform) transformStepping = true; };
        s->onValueChanged = [this, apply = std::move(apply)](double v) {
            if (syncingTransform) return;
            const bool step = transformStepping;
            transformStepping = false;
            const size_t before = history.UndoCount();
            apply(v, step && transformRunOpen);
            // Open only once a step made an undo step to merge into.
            if (step && history.UndoCount() > before) transformRunOpen = true;
        };
        s->onStepsFinished = [this](double) { transformRunOpen = false; };
        r->AddChild(s);
        return s;
    };

    constexpr double kFar = 1.0e6;
    // A column of a caption (or an icon) and a field: 14 + 3 + the field.
    constexpr float kColumn = 17 + kTransformFieldWidth;
    auto position = column("pos", kColumn);
    auto posX = row(position, "x");
    caption(posX, "x", "X");
    posXBox = field(posX, "x", kTransformFieldWidth, -kFar, kFar, 2, 1, "Horizontal position of the reference point",
                    [this](double v, bool merge) { ApplyPosition(true, v, merge); });
    auto posY = row(position, "y");
    caption(posY, "y", "Y");
    posYBox = field(posY, "y", kTransformFieldWidth, -kFar, kFar, 2, 1, "Vertical position of the reference point",
                    [this](double v, bool merge) { ApplyPosition(false, v, merge); });

    auto size = column("size", kColumn);
    auto sizeW = row(size, "w");
    caption(sizeW, "w", "W");
    sizeWBox = field(sizeW, "w", kTransformFieldWidth, 0, kFar, 2, 1, "Width of the selection",
                     [this](double v, bool merge) { ApplySize(true, v, merge); });
    auto sizeH = row(size, "h");
    caption(sizeH, "h", "H");
    sizeHBox = field(sizeH, "h", kTransformFieldWidth, 0, kFar, 2, 1, "Height of the selection",
                     [this](double v, bool merge) { ApplySize(false, v, merge); });

    auto scale = column("scale", kColumn);
    auto scaleW = row(scale, "sw");
    caption(scaleW, "sw", "W");
    scaleWBox = field(scaleW, "sw", kTransformFieldWidth, 0.1, 100000, 1, 1, "Scale the width by a percentage",
                      [this](double v, bool merge) { ApplyScalePercent(true, v, merge); });
    scaleWBox->SetSuffix(" %");
    // Typed, not stepped: no arrows (the keys and the wheel still step by 1).
    scaleWBox->SetLayout(SpinnerLayout::FieldOnly);
    auto scaleH = row(scale, "sh");
    caption(scaleH, "sh", "H");
    scaleHBox = field(scaleH, "sh", kTransformFieldWidth, 0.1, 100000, 1, 1, "Scale the height by a percentage",
                      [this](double v, bool merge) { ApplyScalePercent(false, v, merge); });
    scaleHBox->SetSuffix(" %");
    scaleHBox->SetLayout(SpinnerLayout::FieldOnly);

    ratioLockButton = std::make_shared<UltraCanvasButton>("ac-tf-lock", 0, 0, 28, 28, "");
    ratioLockButton->SetIcon(IconPath("lock-ratio.svg"));
    ratioLockButton->SetIconSize(18, 18);
    ratioLockButton->SetCanToggled(true);
    ratioLockButton->SetPressed(lockAspectRatio);
    ratioLockButton->SetTooltip("Lock the aspect ratio: width and height scale together");
    ButtonStyle lockStyle = ratioLockButton->GetStyle();
    lockStyle.cornerRadius = 6.0f;
    ratioLockButton->SetStyle(lockStyle);
    ratioLockButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    // The lock holds for the selector's handles too: dragging one scales
    // both ways alike.
    ratioLockButton->onToggle = [this](bool on) { lockAspectRatio = on; toolContext.lockAspectRatio = on; };
    transformBar->AddChild(ratioLockButton);

    auto angles = column("angles", 19 + kTransformFieldWidth);
    auto rotation = row(angles, "rot");
    icon(rotation, "rot", "rotate.svg");
    rotationBox = field(rotation, "rot", kTransformFieldWidth, -3600, 3600, 1, 1,
                        "Rotation, counter-clockwise, about the reference point (with the Rotate handles, about the rotation centre)",
                        [this](double v, bool merge) { ApplyRotation(v, merge); });
    rotationBox->SetSuffix("\xC2\xB0");
    auto shear = row(angles, "shear");
    icon(shear, "shear", "shear.svg");
    shearBox = field(shear, "shear", kTransformFieldWidth, -89, 89, 1, 1, "Shear: how far the top leans to the right",
                     [this](double v, bool merge) { ApplyShear(v, merge); });
    shearBox->SetSuffix("\xC2\xB0");

    // The rotation centre, while the Selector shows its Rotate handles: a
    // button that switches the fields between the centre's place on the
    // page and how far it lies from the middle of the selection, the two
    // fields, and a reference point picker that puts it on a corner, an
    // edge's middle or the middle of the frame. A centre dragged anywhere
    // else shows there as a ring pointing its way.
    centreGroup = std::make_shared<UltraCanvasContainer>("ac-tf-centre", 0, 0, 0, kTransformBarHeight - 4);
    centreGroup->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    centreGroup->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    transformBar->AddChild(centreGroup);
    centreModeButton = std::make_shared<UltraCanvasButton>("ac-tf-centre-mode", 0, 0, 30, 30, "");
    centreModeButton->SetIconSize(22, 22);
    ButtonStyle modeStyle = centreModeButton->GetStyle();
    modeStyle.cornerRadius = 6.0f;
    centreModeButton->SetStyle(modeStyle);
    centreModeButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    centreModeButton->onClick = [this]() {
        const bool relative = !settings.centreRelative;
        settings.Update([relative](ArtCreatorSettings& s) { s.centreRelative = relative; });
        ShowCentreMode();
        RefreshTransformBar();
    };
    centreGroup->AddChild(centreModeButton);
    auto centreColumn = std::make_shared<UltraCanvasContainer>("ac-tf-col-centre", 0, 0, 27 + kTransformFieldWidth, kTransformBarHeight - 4);
    centreColumn->layout.SetFlexColumn().SetFlexGap(4).SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    centreColumn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    centreGroup->AddChild(centreColumn);
    const std::pair<std::shared_ptr<UltraCanvasContainer>, float> centreCol(centreColumn, 27 + kTransformFieldWidth);
    auto centreCaption = [&](const std::shared_ptr<UltraCanvasContainer>& r, const std::string& id) {
        // Wide enough for "ΔX" in the relative mode.
        auto l = CreateLabel("ac-tf-cap-" + id, 0, 0, 24, kTransformRowHeight, "");
        l->SetFontSize(11);
        l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        r->AddChild(l);
        return l;
    };
    auto centreX = row(centreCol, "cx");
    centreXCaption = centreCaption(centreX, "cx");
    centreXBox = field(centreX, "cx", kTransformFieldWidth, -kFar, kFar, 2, 1, "",
                       [this](double v, bool) { ApplyCentre(true, v); });
    auto centreY = row(centreCol, "cy");
    centreYCaption = centreCaption(centreY, "cy");
    centreYBox = field(centreY, "cy", kTransformFieldWidth, -kFar, kFar, 2, 1, "",
                       [this](double v, bool) { ApplyCentre(false, v); });
    centrePicker = CreateReferencePointPicker("ac-tf-centre-point", 0, 0, 40, 40);
    centrePicker->SetTooltip("Put the rotation centre on a corner, the middle of an edge or the middle of the selection");
    centrePicker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    centrePicker->onPointChanged = [this](ReferencePoint point) {
        if (!canvas || selection->Empty()) return;
        // The middle is the centre's own place: no centre of its own then.
        if (point == ReferencePoint::Centre) canvas->SetRotationCenter(std::nullopt);
        else {
            const Point2Dd f = ReferencePointFraction(point);
            canvas->SetRotationCenter(canvas->SelectionFramePoint(f.x, f.y));
        }
    };
    centreGroup->AddChild(centrePicker);
    centreGroup->SetVisible(false);
    ShowCentreMode();

    window->AddChild(transformBar);
}

void ArtCreatorWindow::ShowCentreMode() {
    if (!centreModeButton) return;
    const bool relative = settings.centreRelative;
    centreModeButton->SetIcon(IconPath(relative ? "rotation-centre-relative.svg" : "rotation-centre.svg"));
    centreModeButton->SetTooltip(relative
        ? "Rotation centre from the middle of the selection - click for its place on the page"
        : "Rotation centre on the page - click for how far it lies from the middle of the selection");
    centreXCaption->SetText(relative ? "\xCE\x94X" : "X");
    centreYCaption->SetText(relative ? "\xCE\x94Y" : "Y");
    centreXBox->SetTooltip(relative ? "How far the rotation centre lies right of the middle of the selection"
                                    : "Horizontal position of the rotation centre");
    centreYBox->SetTooltip(relative ? "How far the rotation centre lies below the middle of the selection"
                                    : "Vertical position of the rotation centre");
}

void ArtCreatorWindow::ShowCentreGroup() {
    if (!centreGroup || !canvas) return;
    const bool on = canvas->GetHandleMode() == VectorHandleMode::Rotate;
    if (centreGroup->IsVisible() == on) return;
    centreGroup->SetVisible(on);
    transformBar->InvalidateLayout();
    RefreshTransformBar();
}

bool ArtCreatorWindow::RotateHandlesShown() const {
    return canvas && canvas->GetHandleMode() == VectorHandleMode::Rotate;
}

Point2Dd ArtCreatorWindow::TurnPivot() const {
    return RotateHandlesShown() ? canvas->GetRotationCenter() : TransformPivot();
}

void ArtCreatorWindow::ApplyCentre(bool horizontal, double value) {
    if (!canvas || selection->Empty()) return;
    double k = 1;
    std::string unit;
    TransformUnit(k, unit);
    Point2Dd c = canvas->GetRotationCenter();
    const Point2Dd origin = settings.centreRelative ? canvas->SelectionFramePoint(0.5, 0.5) : Point2Dd(0, 0);
    if (horizontal) c.x = origin.x + value * k;
    else c.y = origin.y + value * k;
    canvas->SetRotationCenter(c);
}

void ArtCreatorWindow::TransformUnit(double& pointsPerUnit, std::string& symbol) const {
    if (iconSize > 0 && IconPixel() > 0) {
        pointsPerUnit = IconPixel();
        symbol = "px";
    } else {
        pointsPerUnit = lastPage.PointsPerUnit();
        symbol = ArtCreatorPageResult::UnitSymbol(lastPage.unit);
    }
    if (pointsPerUnit <= 0) pointsPerUnit = 1.0;
}

Point2Dd ArtCreatorWindow::TransformPivot() const {
    const ReferencePoint point = referencePicker ? referencePicker->GetPoint() : ReferencePoint::Centre;
    return ReferencePointIn(point, selection->Bounds());
}

void ArtCreatorWindow::SelectionAngles(double& rotation, double& shear) const {
    rotation = 0;
    shear = 0;
    if (selection->Count() != 1) return;
    const auto e = selection->First();
    const Matrix3x3 m = ParentToDocument(e) * e->Transform.value_or(Matrix3x3::Identity());
    // m = R(theta) * [sx sh; 0 sy]: the rotation turns the x axis, the
    // shear is what is left of the y axis once it is turned back.
    const double theta = std::atan2(m.m[1][0], m.m[0][0]);
    const double c = std::cos(theta), s = std::sin(theta);
    const double sh = m.m[0][1] * c + m.m[1][1] * s;
    const double sy = -m.m[0][1] * s + m.m[1][1] * c;
    // The page's y runs down: a positive angle turns clockwise on screen
    // and a positive SkewX leans the top left, so both change sign.
    rotation = -theta * kDegrees;
    shear = std::fabs(sy) > 1e-12 ? -std::atan(sh / sy) * kDegrees : 0.0;
    if (std::fabs(rotation) < 0.005) rotation = 0;
    if (std::fabs(shear) < 0.005) shear = 0;
}

void ArtCreatorWindow::RefreshTransformBar() {
    if (!transformBar || !document) return;
    syncingTransform = true;
    const bool any = !selection->Empty();
    for (UltraCanvasUIElement* e : std::initializer_list<UltraCanvasUIElement*>{
             posXBox.get(), posYBox.get(), sizeWBox.get(), sizeHBox.get(), scaleWBox.get(), scaleHBox.get(),
             rotationBox.get(), shearBox.get(), centreXBox.get(), centreYBox.get(), centrePicker.get()})
        if (e->IsDisabled() == any) e->SetDisabled(!any);
    double k = 1;
    std::string unit;
    TransformUnit(k, unit);
    for (auto* s : {posXBox.get(), posYBox.get(), sizeWBox.get(), sizeHBox.get(), centreXBox.get(), centreYBox.get()}) {
        if (s->GetSuffix() != unit) s->SetSuffix(unit);
    }
    const auto ids = selection->Ids();
    const bool sameSelection = ids == transformBarIds;
    transformBarIds = ids;
    if (any) {
        const Rect2Dd b = selection->Bounds();
        const Point2Dd p = TransformPivot();
        posXBox->SetValue(p.x / k);
        posYBox->SetValue(p.y / k);
        sizeWBox->SetValue(b.width / k);
        sizeHBox->SetValue(b.height / k);
        double rotation = 0, shear = 0;
        SelectionAngles(rotation, shear);
        // The object's angle is only known to a turn: show the one nearest
        // what the field holds, so stepping on from 165 gives 180, 195 ...
        // rather than -165. A new selection starts from -180 to 180.
        if (sameSelection) rotation += 360.0 * std::round((rotationBox->GetValue() - rotation) / 360.0);
        rotationBox->SetValue(rotation);
        shearBox->SetValue(shear);
        // The rotation centre, on the page or from the middle of the frame;
        // on the picker, one of its nine points or a ring pointing its way.
        const Point2Dd c = canvas->GetRotationCenter();
        const Point2Dd origin = settings.centreRelative ? canvas->SelectionFramePoint(0.5, 0.5) : Point2Dd(0, 0);
        centreXBox->SetValue((c.x - origin.x) / k);
        centreYBox->SetValue((c.y - origin.y) / k);
        const Point2Dd f = canvas->SelectionFrameFraction(c);
        std::optional<ReferencePoint> onPoint;
        for (int i = 0; i < 9 && !onPoint; ++i) {
            const Point2Dd p = ReferencePointFraction(static_cast<ReferencePoint>(i));
            if (std::fabs(p.x - f.x) < 1e-6 && std::fabs(p.y - f.y) < 1e-6) onPoint = static_cast<ReferencePoint>(i);
        }
        if (onPoint) centrePicker->SetPoint(*onPoint);
        else centrePicker->SetFreePoint(f);
    } else {
        for (auto* s : {posXBox.get(), posYBox.get(), sizeWBox.get(), sizeHBox.get(), rotationBox.get(), shearBox.get(),
                        centreXBox.get(), centreYBox.get()})
            s->SetValue(0);
        centrePicker->SetPoint(ReferencePoint::Centre);
    }
    scaleWBox->SetValue(100);
    scaleHBox->SetValue(100);
    syncingTransform = false;
}

void ArtCreatorWindow::ApplyPosition(bool horizontal, double value, bool merge) {
    if (selection->Empty()) return;
    double k = 1;
    std::string unit;
    TransformUnit(k, unit);
    const Point2Dd p = TransformPivot();
    const double dx = horizontal ? value * k - p.x : 0.0;
    const double dy = horizontal ? 0.0 : value * k - p.y;
    if (std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9) return;
    auto ids = selection->Ids();
    history.Record("Move", [&]() { TranslateElements(selection->Elements(), dx, dy); }, merge);
    Reselect(ids);
    // A rotation centre of the selection's own goes with it.
    if (canvas->HasRotationCenter()) {
        const Point2Dd c = canvas->GetRotationCenter();
        canvas->SetRotationCenter(Point2Dd(c.x + dx, c.y + dy));
    }
}

void ArtCreatorWindow::ApplySize(bool horizontal, double value, bool merge) {
    if (selection->Empty()) return;
    double k = 1;
    std::string unit;
    TransformUnit(k, unit);
    const Rect2Dd b = selection->Bounds();
    const double current = horizontal ? b.width : b.height;
    // A line along one axis has no size across it to scale.
    if (current < 1e-9 || value <= 0) { RefreshTransformBar(); return; }
    const double factor = value * k / current;
    ApplyScalePercent(horizontal, factor * 100.0, merge);
}

void ArtCreatorWindow::ApplyScalePercent(bool horizontal, double percent, bool merge) {
    if (selection->Empty() || percent <= 0) { RefreshTransformBar(); return; }
    const double factor = percent / 100.0;
    const double sx = horizontal || lockAspectRatio ? factor : 1.0;
    const double sy = !horizontal || lockAspectRatio ? factor : 1.0;
    if (std::fabs(sx - 1.0) < 1e-9 && std::fabs(sy - 1.0) < 1e-9) { RefreshTransformBar(); return; }
    const Point2Dd pivot = TransformPivot();
    auto ids = selection->Ids();
    history.Record("Resize", [&]() { ScaleElements(selection->Elements(), sx, sy, pivot); }, merge);
    Reselect(ids);
}

void ArtCreatorWindow::ApplyRotation(double degrees, bool merge) {
    if (selection->Empty()) return;
    double rotation = 0, shear = 0;
    SelectionAngles(rotation, shear);
    // The shortest turn there: 375 typed over 0 turns by 15.
    const double turn = std::remainder(degrees - rotation, 360.0);
    if (std::fabs(turn) < 1e-9) { RefreshTransformBar(); return; }
    const Point2Dd pivot = TurnPivot();
    auto ids = selection->Ids();
    history.Record("Rotate", [&]() { RotateElements(selection->Elements(), -turn / kDegrees, pivot); }, merge);
    Reselect(ids);
}

void ArtCreatorWindow::ApplyShear(double degrees, bool merge) {
    if (selection->Empty()) return;
    double rotation = 0, shear = 0;
    SelectionAngles(rotation, shear);
    const double target = std::clamp(degrees, -89.0, 89.0);
    // In the object's own turned frame the shear is a SkewX; going from one
    // angle to another adds the difference of their tangents.
    const double delta = std::atan(std::tan(-target / kDegrees) - std::tan(-shear / kDegrees));
    if (std::fabs(delta) < 1e-9) return;
    const double theta = -rotation / kDegrees;
    const Point2Dd pivot = TurnPivot();
    const Matrix3x3 m = Matrix3x3::Translate(pivot.x, pivot.y) * Matrix3x3::Rotate(theta) *
                        Matrix3x3::SkewX(delta) * Matrix3x3::Rotate(-theta) *
                        Matrix3x3::Translate(-pivot.x, -pivot.y);
    auto ids = selection->Ids();
    history.Record("Shear", [&]() { TransformElements(selection->Elements(), m); }, merge);
    Reselect(ids);
}

void ArtCreatorWindow::BuildToolPalette() {
    palette = std::make_shared<UltraCanvasToolbar>("ac-palette", 0, 0, kPaletteWidth, 0);
    palette->SetOrientation(ToolbarOrientation::Vertical);
    ToolbarAppearance app = palette->GetAppearance();
    app.showIconLabels = false;
    app.itemSpacing = 1.0f;
    // The palette's icons are drawn on a 28 px grid (media/icons/artcreator/
    // 28) with 2 px strokes on whole units: shown at 28 px they land on whole
    // pixels and stay sharp; at any other size every line falls between two
    // pixels and blurs.
    app.iconSize = ToolbarIconSize::Custom;
    app.customIconWidth = static_cast<int>(kToolIcon);
    app.customIconHeight = static_cast<int>(kToolIcon);
    palette->SetAppearance(app);
    // The buttons keep their size: where the window is too short for all of
    // them the column scrolls (wheel, or the hints at its ends) rather than
    // squeezing every icon into a button too small for it.
    palette->SetOverflowMode(ToolbarOverflowMode::Scroll);
    palette->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // A slot per group of tools, the others in its fold-out bar
    // (ArtCreatorCommands.cpp); a right click shows, hides and folds them.
    ApplyPaletteLayout();
    InstallToolbarContextMenu(*palette, paletteLayout, commands,
                              [this]() { ApplyPaletteLayout(); SaveLayouts(); },
                              [this]() { OpenCustomise("t1"); });
    body->AddChild(palette);
}

void ArtCreatorWindow::BuildRightPanel() {
    rightPanel = CreateScrollableContainer("ac-right", 0, 0, kRightWidth, 0);
    // Up and down only: the cards are as wide as what the vertical scrollbar
    // leaves, and a horizontal bar would only scroll the strip that one takes.
    ContainerStyle rightStyle = rightPanel->GetContainerStyle();
    rightStyle.autoShowHorizontalScrollbar = false;
    rightPanel->SetContainerStyle(rightStyle);
    rightPanel->layout.SetFlexColumn().SetFlexGap(kPanelGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    rightPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    rightPanel->SetPadding(kPanelGap, 0, kPanelGap, kPanelGap);
    rightPanel->SetBackgroundColor(Colors::SectionPage);

    // ----- icon: on top, and only in icon mode - the icon at its size -----
    BuildIconPanel();

    // ----- colour: foreground = fill, background = line -----
    auto colourPanel = PanelCard("ac-color-card", "Colour (fill / line)");
    const float cardInner = CardInnerWidth(*colourPanel);
    colorPicker = CreateColorPicker("ac-color", fillColour, 0, 0, cardInner, 300);
    // The framework's light picker on the white card: left as constructed it
    // is a charcoal block with a red accent in the middle of a light panel.
    colorPicker->SetStyle(ColorPickerStyle::Light());
    colorPicker->SetUIScale(0.78f);
    colorPicker->SetBackgroundColor(lineColour);
    colorPicker->SetShowAlpha(true);
    // The design list top right (circle, square, rectangle, two sliders), and
    // the HSV / HSL / RGB channels under their disclosure row - both as the
    // user left them (folded the first time).
    colorPicker->SetWheelStyle(settings.pickerDesign);
    colorPicker->SetShowWheelStyleSelector(true);
    colorPicker->SetSlidersCollapsible(true, settings.pickerChannelsOpen);
    colorPicker->onWheelStyleChanged = [this](ColorPickerWheelStyle) { RememberColourPicker(); };
    colorPicker->onSlidersExpandedChanged = [this](bool) { RememberColourPicker(); };
    // As tall as its content: the hue ring fills the card's width, and the
    // card and the cards below move up and down as the channels fold or
    // another design is chosen. The height is also the picker's flex basis.
    colorPicker->SetAutoHeight(true);
    // Not stretched: the picker lays its hex field and channel values out to
    // the width it is given, and that is the card's inner width exactly.
    colorPicker->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Start);
    colorPicker->onColorChanged = [this](const Color& c) { fillColour = c; ApplyFillColour(c, true); };
    colorPicker->onColorChanging = [this](const Color& c) { fillColour = c; ApplyFillColour(c, false); };
    colorPicker->onBackgroundChanged = [this](const Color& c) { lineColour = c; ApplyLineColour(c, true); };
    colorPicker->onBackgroundChanging = [this](const Color& c) { lineColour = c; ApplyLineColour(c, false); };
    colourPanel->AddChild(colorPicker);

    // The palettes under the picker: rows of swatches, more added from the
    // gallery behind the "+" at the end of the last row, and a right click
    // sets one entry. Remembered between starts (picker.palettes).
    palettes = CreateColorPaletteManager("ac-palettes", 0, 0, cardInner);
    palettes->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    // Outlined as the picker above them outlines its fields, so a white
    // swatch still shows its edge on the white card.
    const ColorPickerStyle& pickerStyle = colorPicker->GetStyle();
    ColorPaletteManagerStyle paletteStyle = palettes->GetManagerStyle();
    paletteStyle.swatchBorder         = pickerStyle.fieldBorderColor;
    paletteStyle.swatchHoverBorder    = pickerStyle.mutedTextColor;
    paletteStyle.swatchSelectedBorder = pickerStyle.accentColor;
    paletteStyle.accentColor          = pickerStyle.accentColor;
    paletteStyle.editorPickerStyle    = pickerStyle;
    palettes->SetManagerStyle(paletteStyle);
    std::vector<NamedColorPalette> saved;
    if (UltraCanvasColorPaletteManager::ParsePalettes(settings.palettes, saved)) palettes->SetPalettes(saved);
    std::vector<NamedColorPalette> own;
    if (UltraCanvasColorPaletteManager::ParsePalettes(settings.userPalettes, own)) palettes->SetUserPalettes(own);
    palettes->onColorSelected = [this](const Color& c) {
        fillColour = c;
        colorPicker->SetForegroundColor(c, false);
        ApplyFillColour(c, true);
    };
    palettes->onPalettesChanged = [this]() {
        // Only the rows go to the file; another window's other settings stay.
        const std::string rows = UltraCanvasColorPaletteManager::SerializePalettes(palettes->GetPalettes());
        settings.Update([&rows](ArtCreatorSettings& s) { s.palettes = rows; });
    };
    palettes->onUserPalettesChanged = [this]() {
        const std::string own = UltraCanvasColorPaletteManager::SerializePalettes(palettes->GetUserPalettes());
        settings.Update([&own](ArtCreatorSettings& s) { s.userPalettes = own; });
    };
    // A swatch under the pointer floods the picker too, as the rows, so the
    // colour is seen on the whole of the card.
    palettes->onColorHovered = [this](const Color& c) { colorPicker->SetSurfacePreview(c); };
    palettes->onColorHoverEnded = [this]() { colorPicker->ClearSurfacePreview(); };
    colourPanel->AddChild(palettes);
    rightPanel->AddChild(colourPanel);

    // ----- tool options: captioned with the active tool's name -----
    // Straight under the colours, where they are visible without scrolling:
    // the Quick Shape corners and star depth used to sit below the fold.
    optionsPanel = PanelCard("ac-options", "Tool Options");
    rightPanel->AddChild(optionsPanel);

    // ----- fill ramp: with the Fill tool, under its options -----
    fillPanel = PanelCard("ac-fill", "Gradient");
    ramp = CreateGradientEditor("ac-ramp", 0, 0, static_cast<int>(cardInner), 46);
    ramp->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    ramp->onStopsChanged = [this]() { ApplyRamp(true); };
    ramp->onStopsChanging = [this]() { ApplyRamp(false); };
    ramp->onSelectionChanged = [this](int i) {
        if (i < 0 || syncingColours) return;
        syncingColours = true;
        colorPicker->SetForegroundColor(ramp->GetSelectedColor(), false);
        syncingColours = false;
    };
    fillPanel->AddChild(ramp);
    rightPanel->AddChild(fillPanel);

    // ----- line gallery: with the tools that draw lines -----
    linePanel = PanelCard("ac-line", "Line");
    rightPanel->AddChild(linePanel);
    RebuildLinePanel();

    // ----- layers -----
    layersPanel = PanelCard("ac-layers", "Layers");

    // Flat icon buttons, as UltraPaint's and the Selector's Arrange / Align bars.
    ArtOptionWidgets::AddIconBar(*layersPanel, "ac-layer-buttons", {
        { IconPath("layer-add.svg"), "New layer", [this]() { CmdLayerAdd(); } },
        { IconPath("layer-delete.svg"), "Delete layer", [this]() { CmdLayerDelete(); } },
        { IconPath("layer-up.svg"), "Move up", [this]() { CmdLayerMove(1); } },
        { IconPath("layer-down.svg"), "Move down", [this]() { CmdLayerMove(-1); } },
    });

    layerRows = std::make_shared<UltraCanvasContainer>("ac-layer-rows", 0, 0, 0, 0);
    layerRows->layout.SetFlexColumn().SetFlexGap(2).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    layerRows->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    layersPanel->AddChild(layerRows);
    rightPanel->AddChild(layersPanel);

    body->AddChild(rightPanel);
}

void ArtCreatorWindow::RememberColourPicker() {
    // Whichever window changed its picker last is the one remembered; the
    // other settings stay as the file has them (another window may have
    // changed them since this one opened).
    const ColorPickerWheelStyle design = colorPicker->GetWheelStyle();
    const bool open = colorPicker->GetSlidersExpanded();
    settings.Update([design, open](ArtCreatorSettings& s) {
        s.pickerDesign = design;
        s.pickerChannelsOpen = open;
    });
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
        // Typing on the page: Escape, or Ctrl+Enter, finishes the text
        // (Enter starts a new line in it).
        if (IsEditingText() && (e.virtualKey == UCKeys::Escape ||
                                (e.ctrl && (e.virtualKey == UCKeys::Return || e.virtualKey == UCKeys::NumPadEnter)))) {
            CommitTextEdit();
            return true;
        }
        // The file commands still work while a text is typed (it is
        // finished first); every other key is the text's.
        const bool fileKey = e.ctrl && (e.virtualKey == UCKeys::S || e.virtualKey == UCKeys::O ||
                                        e.virtualKey == UCKeys::N || e.virtualKey == UCKeys::E ||
                                        e.virtualKey == UCKeys::Q);
        if (IsEditingText() && fileKey) return RunShortcut(e);
        if (FocusIsTextEntry(window.get())) return false;
        // Delete is the Shape Editor's own while it is active (its nodes).
        if (!e.ctrl && !e.alt && e.virtualKey == UCKeys::Delete) {
            if (activeTool == ArtToolId::ShapeEditor) return false;
            CmdDelete();
            return true;
        }
        // Ctrl with a letter or digit, and a tool's letter alone: the
        // command whose shortcut that is - the one its menu entry shows.
        if (!e.ctrl && (e.alt || e.shift)) return false;
        return RunShortcut(e);
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
    DropTextEdit();
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
    RefreshCommandStates();
    UpdateTitle();
    UpdateStatus();
    RefreshTransformBar();
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
    RefreshCommandStates();
    UpdateTitle();
    UpdateStatus();
    RefreshIconPreview();
    RefreshTransformBar();
    if (auto* t = ActiveTool()) t->OnSelectionChanged(toolContext);
}

void ArtCreatorWindow::OnSelectionChanged() {
    SyncColourPanelFromSelection();
    SyncLinePanelFromSelection();
    RefreshTransformBar();
    if (auto* t = ActiveTool()) t->OnSelectionChanged(toolContext);
    UpdateToolCards();   // the Line card follows a selected line
    if (activeTool == ArtToolId::Text && !IsEditingText()) SyncTextBar();   // the selected text's attributes
    UpdateStatus();
    RefreshCommandStates();   // what needs a selection follows it
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

void ArtCreatorWindow::CmdUndo() { CommitTextEdit(); history.Undo(); }
void ArtCreatorWindow::CmdRedo() { CommitTextEdit(); history.Redo(); }

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
    if (id != ArtToolId::Text) CommitTextEdit();
    ShowTextBar(id == ArtToolId::Text);
    if (auto* old = ActiveTool()) { if (old->id != id) old->Deactivate(toolContext); }
    activeTool = id;
    ArtTool* t = ActiveTool();
    // The slot of the tool's group shows it and is pressed; the others not.
    if (palette) palette->RefreshCommandStates();
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
    if (t) {
        optionsPanel->SetTitle(t->name);
        t->BuildOptions(toolContext, *optionsPanel, [this]() { canvas->Refresh(); });
    }
    UpdateToolCards();
}

void ArtCreatorWindow::UpdateToolCards() {
    if (!optionsPanel) return;
    const ArtToolId id = activeTool;
    bool lineCard = id == ArtToolId::Pen || id == ArtToolId::Freehand || id == ArtToolId::Line;
    // The Selector and the Shape Editor show it while a line is selected, so
    // its arrowheads and brush are set without changing tools.
    if (!lineCard && (id == ArtToolId::Selector || id == ArtToolId::ShapeEditor)) {
        for (const auto& e : selection->Elements())
            if (e && IsOpenLine(*e)) { lineCard = true; break; }
    }
    // Zoom and Push have nothing to set: no card rather than an empty one.
    const bool hasOptions = !optionsPanel->GetChildren().empty();
    bool changed = false;
    auto show = [&changed](const std::shared_ptr<UltraCanvasGroupBox>& card, bool on) {
        if (card && card->IsVisible() != on) { card->SetVisible(on); changed = true; }
    };
    show(optionsPanel, hasOptions);
    show(fillPanel, id == ArtToolId::Fill);
    show(linePanel, lineCard);
    if (changed) rightPanel->InvalidateLayout();
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
    if (syncingColours) return;
    // While a text is typed, the colour is the text's: it goes into the
    // edit, and the keys go back to the text once the choice is made.
    if (textSession && textEditor) {
        textSession->model->Style.Fill = c;
        textSession->fillChanged = true;
        textEditor->SetStyle(TextEditorStyle());
        if (commit) textEditor->SetFocus(true);
        return;
    }
    if (selection->Empty()) return;
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

// ===========================================================================
// TYPING TEXT ON THE PAGE
// ===========================================================================

namespace {
    // The text engine takes sizes in points at 96 dpi; the drawing's text
    // sizes are in its own units (VectorRenderer::RenderText).
    constexpr float kTextPointsPerUnit = 72.0f / 96.0f;

    FontStyle SpanFont(const VectorTextStyle& style, const VectorTextStyle& base) {
        VectorTextStyle st = style;
        if (st.FontFamily.empty()) st.FontFamily = base.FontFamily;
        if (st.FontSize <= 0) st.FontSize = base.FontSize;
        FontStyle fs = st.ToFontStyle();
        if (fs.fontFamily.empty()) fs.fontFamily = "Sans";
        fs.fontSize *= kTextPointsPerUnit;
        return fs;
    }

    // From one baseline of a text to the next: its line spacing (a factor
    // of its size, 1.2 when it states none) times its size.
    double LineAdvance(const VectorText& text) {
        const double size = std::max(1.0f, text.BaseStyle.FontSize);
        const double factor = text.BaseStyle.LineHeight > 0.0f ? text.BaseStyle.LineHeight : 1.2;
        return size * factor;
    }

    // How far below the top of a line the editor puts its baseline when the
    // line is `advance` tall exactly, as StampEditorParagraphs makes them:
    // measured the way the editor lays it out (its layouts, its absolute
    // line height), in document units.
    double EditorBaseline(IRenderContext* ctx, const FontStyle& font, double advance) {
        double baseline = font.fontSize / kTextPointsPerUnit * 0.8 + (advance - font.fontSize / kTextPointsPerUnit * 1.2) / 2.0;
        if (!ctx) return baseline;
        ctx->PushState();
        if (auto probe = ctx->CreateTextLayout("Ag", false)) {
            probe->SetFontStyle(font);
            if (auto height = TextAttributeFactory::CreateAbsoluteLineHeight(advance)) {
                height->SetRange(0, 2);
                probe->InsertAttribute(std::move(height));
            }
            if (probe->GetBaseline() > 0) baseline = probe->GetBaseline();
        }
        ctx->PopState();
        return baseline;
    }

    // A run of the editor in a span's style, against the text's base style
    // (which is the editor's base font). Underline and strikethrough are the
    // runs' own, always stated.
    RichTextRun RunFromSpan(const VectorTextStyle& span, const VectorTextStyle& base) {
        RichTextRun run;
        const bool baseBold = base.Weight >= FontWeight::Bold;
        const bool baseItalic = base.Slant != FontSlant::Normal;
        run.bold = span.Weight >= FontWeight::Bold && !baseBold;
        run.italic = span.Slant != FontSlant::Normal && !baseItalic;
        run.underline = span.Underline;
        run.strikethrough = span.StrikeThrough;
        if (!span.FontFamily.empty() && span.FontFamily != base.FontFamily) run.fontFamily = span.FontFamily;
        if (span.FontSize > 0 && std::fabs(span.FontSize - base.FontSize) > 1e-3f)
            run.fontSizePt = span.FontSize * kTextPointsPerUnit;
        return run;
    }

    // A text's words as the editor's document: a paragraph for each line -
    // a span placed on a lower baseline starts one, so does a line break in
    // a span's text - and a run for each span.
    std::shared_ptr<UCRichDocument> WordsOfText(const VectorText& text) {
        const double lineAdvance = LineAdvance(text);
        auto words = std::make_shared<UCRichDocument>();
        words->blocks.emplace_back();
        double lineY = text.Position.y;
        for (const auto& span : text.Spans) {
            if (span.Position.has_value() && std::fabs(span.Position->y - lineY) > 1e-6) {
                // Lines left empty in between stay, as empty paragraphs.
                const double lines = lineAdvance > 0 ? std::round((span.Position->y - lineY) / lineAdvance) : 1.0;
                const int count = static_cast<int>(std::clamp(lines, 1.0, 1000.0));
                for (int i = 0; i < count; ++i) words->blocks.emplace_back();
                lineY = span.Position->y;
            }
            const RichTextRun style = RunFromSpan(span.Style, text.BaseStyle);
            size_t start = 0;
            for (;;) {
                const size_t nl = span.Text.find('\n', start);
                const std::string piece = span.Text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                if (!piece.empty()) {
                    RichTextRun run = style;
                    run.text = piece;
                    words->blocks.back().runs.push_back(run);
                }
                if (nl == std::string::npos) break;
                words->blocks.emplace_back();
                lineY += lineAdvance;
                start = nl + 1;
            }
        }
        return words;
    }

    // The editor's lines: a paragraph each, and each line break inside one.
    std::vector<std::vector<const RichTextRun*>> LinesOfWords(const UCRichDocument& words) {
        std::vector<std::vector<const RichTextRun*>> lines;
        for (const auto& block : words.blocks) {
            lines.emplace_back();
            for (const auto& run : block.runs) {
                if (run.lineBreakBefore) lines.emplace_back();
                if (run.mediaIndex >= 0 || run.text.empty()) continue;
                lines.back().push_back(&run);
            }
        }
        return lines;
    }

    // A run's style in the drawing: the text's base style with what the run
    // sets of its own.
    VectorTextStyle StyleOfRun(const RichTextRun& run, const VectorTextStyle& base) {
        VectorTextStyle st = base;
        if (run.bold) st.Weight = FontWeight::Bold;
        if (run.italic) st.Slant = FontSlant::Italic;
        st.Underline = run.underline;
        st.StrikeThrough = run.strikethrough;
        if (!run.fontFamily.empty()) st.FontFamily = run.fontFamily;
        if (run.fontSizePt > 0) st.FontSize = run.fontSizePt / kTextPointsPerUnit;
        return st;
    }

    // The spans of what was typed: one a run, the first of each line after
    // the first placed on its own baseline, a line spacing below the last.
    std::vector<TextSpanData> SpansOfWords(const UCRichDocument& words, const VectorText& model) {
        const double lineAdvance = LineAdvance(model);
        std::vector<TextSpanData> spans;
        const auto lines = LinesOfWords(words);
        for (size_t k = 0; k < lines.size(); ++k) {
            bool first = true;
            for (const RichTextRun* run : lines[k]) {
                TextSpanData span;
                span.Style = StyleOfRun(*run, model.BaseStyle);
                std::string piece = run->text;
                piece.erase(std::remove(piece.begin(), piece.end(), '\n'), piece.end());
                span.Text = piece;
                if (first && k > 0)
                    span.Position = Point2Dd(model.Position.x, model.Position.y + static_cast<double>(k) * lineAdvance);
                first = false;
                spans.push_back(std::move(span));
            }
        }
        return spans;
    }

    bool SameTextStyle(const VectorTextStyle& a, const VectorTextStyle& b) {
        return a.FontFamily == b.FontFamily && std::fabs(a.FontSize - b.FontSize) < 1e-4f &&
               a.Weight == b.Weight && a.Slant == b.Slant && a.Anchor == b.Anchor &&
               std::fabs(a.LetterSpacing - b.LetterSpacing) < 1e-4f && std::fabs(a.LineHeight - b.LineHeight) < 1e-4f &&
               a.Underline == b.Underline && a.StrikeThrough == b.StrikeThrough;
    }

    bool SameSpans(const std::vector<TextSpanData>& a, const std::vector<TextSpanData>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            const auto& x = a[i]; const auto& y = b[i];
            if (x.Text != y.Text || x.Position.has_value() != y.Position.has_value()) return false;
            if (x.Position && (std::fabs(x.Position->x - y.Position->x) > 1e-6 || std::fabs(x.Position->y - y.Position->y) > 1e-6)) return false;
            if (!SameTextStyle(x.Style, y.Style)) return false;
        }
        return true;
    }

    RichTextAlign AlignOfAnchor(TextAnchor anchor) {
        return anchor == TextAnchor::Middle ? RichTextAlign::Center
             : anchor == TextAnchor::End ? RichTextAlign::Right : RichTextAlign::Left;
    }

    TextAnchor AnchorOfAlign(int align) {
        return align == 1 ? TextAnchor::Middle : align == 2 ? TextAnchor::End : TextAnchor::Start;
    }

    int AlignOfTextAnchor(TextAnchor anchor) {
        return anchor == TextAnchor::Middle ? 1 : anchor == TextAnchor::End ? 2 : 0;
    }

    // The frame of the editor: as big as the canvas and over it, so the
    // editor is cut off where the text runs past what the canvas shows
    // instead of drawing over the panels beside it. A press on it - outside
    // the editor - finishes the text; the pointer's moves and the wheel go
    // on to the canvas below (same bounds, so the same coordinates).
    class TextEditorHost : public UltraCanvasContainer {
    public:
        explicit TextEditorHost(const std::string& id) : UltraCanvasContainer(id) {}
        std::function<void()> onPressOutside;
        UltraCanvasUIElement* passTo = nullptr;
        bool OnEvent(const UCEvent& event) override {
            if (event.type == UCEventType::MouseDown || event.type == UCEventType::MouseDoubleClick) {
                if (onPressOutside) onPressOutside();
                return true;
            }
            if (passTo && (event.type == UCEventType::MouseMove || event.type == UCEventType::MouseWheel))
                return passTo->OnEvent(event);
            return UltraCanvasContainer::OnEvent(event);
        }
    };
}

RichTextEditStyle ArtCreatorWindow::TextEditorStyle() const {
    RichTextEditStyle style;
    if (!textSession || !textSession->model) return style;
    const VectorText& t = *textSession->model;
    style.baseFont = SpanFont(t.BaseStyle, t.BaseStyle);
    Color colour = Colors::Black;
    if (t.Style.Fill.has_value())
        if (const auto* c = std::get_if<Color>(&*t.Style.Fill)) colour = *c;
    colour.a = static_cast<uint8_t>(std::clamp(colour.a * t.Style.FillOpacity, 0.0f, 255.0f));
    style.textColor = colour;
    style.backgroundColor = Colors::Transparent;
    style.drawBorder = false;
    style.padding = 0.0f;
    style.blockSpacing = 0.0f;
    style.paragraphLeading = 0.0f;
    style.letterSpacing = t.BaseStyle.LetterSpacing;
    style.selectionColor = Color(64, 128, 255, 96);
    // A caret that shows on any colour: the text's, made opaque, unless it
    // is nearly white.
    const int light = (colour.r * 299 + colour.g * 587 + colour.b * 114) / 1000;
    style.cursorColor = light > 225 ? Color(30, 30, 40, 255) : Color(colour.r, colour.g, colour.b, 255);
    return style;
}

void ArtCreatorWindow::StampEditorParagraphs() {
    if (!textSession || !textEditor) return;
    auto words = textEditor->GetDocument();
    if (!words) return;
    const VectorText& t = *textSession->model;
    const float heightPt = static_cast<float>(LineAdvance(t)) * kTextPointsPerUnit;
    const RichTextAlign align = AlignOfAnchor(t.BaseStyle.Anchor);
    bool changed = false;
    for (auto& block : words->blocks) {
        if (std::fabs(block.lineHeightPt - heightPt) > 1e-4f || block.lineHeightAtLeast || block.align != align) {
            block.lineHeightPt = heightPt;
            block.lineHeightAtLeast = false;
            block.align = align;
            changed = true;
        }
    }
    if (changed) textEditor->InvalidateDocument();
}

void ArtCreatorWindow::BeginTextEdit(const Point2Dd& docPoint, const std::shared_ptr<VectorText>& text) {
    if (!document || !canvas) return;
    CommitTextEdit();
    if (!textMeasure) textMeasure = CreateRenderContext(Size2Di(4, 4), nullptr);

    auto session = std::make_unique<TextEditSession>();
    if (text) {
        session->elementId = text->Id;
        session->model = std::static_pointer_cast<VectorText>(text->Clone());
        session->toDocument = text->GetGlobalTransform();
        // The bar shows the text's own attributes, and a new text starts in them.
        const VectorTextStyle& b = text->BaseStyle;
        if (!b.FontFamily.empty()) toolOptions.textFont = b.FontFamily;
        toolOptions.textSize = std::max(1.0f, b.FontSize);
        toolOptions.textBold = b.Weight >= FontWeight::Bold;
        toolOptions.textItalic = b.Slant != FontSlant::Normal;
        toolOptions.textUnderline = b.Underline;
        toolOptions.textStrike = b.StrikeThrough;
        toolOptions.textAlign = AlignOfTextAnchor(b.Anchor);
        toolOptions.textLineSpacing = (b.LineHeight > 0 ? b.LineHeight : 1.2f) * 100.0f;
        toolOptions.textLetterSpacing = b.LetterSpacing;
    } else {
        auto model = std::make_shared<VectorText>();
        model->Position = docPoint;
        VectorTextStyle& b = model->BaseStyle;
        b.FontFamily = toolOptions.textFont;
        b.FontSize = std::max(1.0f, toolOptions.textSize);
        b.Weight = toolOptions.textBold ? FontWeight::Bold : FontWeight::Normal;
        b.Slant = toolOptions.textItalic ? FontSlant::Italic : FontSlant::Normal;
        b.Underline = toolOptions.textUnderline;
        b.StrikeThrough = toolOptions.textStrike;
        b.Anchor = AnchorOfAlign(toolOptions.textAlign);
        b.LineHeight = std::max(0.1f, toolOptions.textLineSpacing / 100.0f);
        b.LetterSpacing = toolOptions.textLetterSpacing;
        model->Style.Fill = fillColour;
        session->model = model;
        session->toDocument = Matrix3x3::Identity();
    }
    session->startWords = WordsOfText(*session->model);

    if (!textEditorHost) {
        auto host = std::make_shared<TextEditorHost>("ac-text-host");
        host->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
        if (!host->layoutItem.position) host->layoutItem.position = CSSLayout::Position();
        host->SetZIndex(OverlayZOrder::Overlays);
        host->SetBackgroundColor(Colors::Transparent);
        host->SetMouseCursor(UCMouseCursor::Text);
        host->passTo = canvas.get();
        host->onPressOutside = [this]() { CommitTextEdit(); };
        host->SetVisible(false);
        textEditor = std::make_shared<UltraCanvasRichTextEdit>("ac-text-editor", 0, 0, 10, 10);
        textEditor->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
        if (!textEditor->layoutItem.position) textEditor->layoutItem.position = CSSLayout::Position();
        textEditor->SetSpellCheckEnabled(false);
        textEditor->SetShowComments(false);
        textEditor->SetShowFontNotice(false);
        textEditor->SetZoomRange(1.0f / 64.0f, 256.0f);
        // The box grows with what is typed; a new line is set as the others.
        textEditor->onDocumentChanged = [this]() {
            StampEditorParagraphs();
            // A new, empty line of a text underlined or struck through as
            // a whole is typed that way too (Enter leaves the line it ends
            // behind with what it was typed in).
            if (textSession && textEditor && !textSession->arming && !textEditor->HasSelection()) {
                const VectorTextStyle& b = textSession->model->BaseStyle;
                const RichDocPosition caret = textEditor->GetEditor().GetCaret();
                auto words = textEditor->GetDocument();
                if ((b.Underline || b.StrikeThrough) && words && caret.cellRow < 0 &&
                    caret.blockIndex >= 0 && caret.blockIndex < static_cast<int>(words->blocks.size())) {
                    bool empty = true;
                    for (const auto& run : words->blocks[static_cast<size_t>(caret.blockIndex)].runs)
                        empty = empty && run.text.empty();
                    if (empty) {
                        RichCharFormatDelta armed;
                        if (b.Underline) { armed.setUnderline = true; armed.underline = true; }
                        if (b.StrikeThrough) { armed.setStrikethrough = true; armed.strikethrough = true; }
                        textSession->arming = true;   // arming may report a change: not again
                        textEditor->ApplyCharFormat(armed);
                        if (textSession) textSession->arming = false;
                    }
                }
            }
            PlaceTextEditor();
        };
        // The bar follows the caret.
        textEditor->onSelectionChanged = [this]() { SyncTextBar(); };
        // Ctrl+wheel over the text zooms the canvas, which the editor follows.
        textEditor->onZoomChanged = [this](float zoom) {
            if (!textSession || !canvas) return;
            const double scale = std::sqrt(std::max(1e-12, std::fabs(textSession->toDocument.Determinant())));
            const double canvasZoom = zoom / scale;
            if (std::fabs(canvas->GetZoom() - canvasZoom) > 1e-6) canvas->SetZoom(canvasZoom);
            PlaceTextEditor();
        };
        host->AddChild(textEditor);
        body->AddChild(host);
        textEditorHost = host;
    }

    textSession = std::move(session);
    textEditor->SetStyle(TextEditorStyle());
    // The editor edits a copy: the session keeps what it opened with.
    textEditor->SetDocument(std::make_shared<UCRichDocument>(*textSession->startWords));
    StampEditorParagraphs();
    textEditor->GetEditor().SetCaret(textEditor->GetEditor().DocumentEnd());
    if (!text && (toolOptions.textUnderline || toolOptions.textStrike)) {
        // A new text is typed underlined or struck through as the bar says.
        RichCharFormatDelta armed;
        armed.setUnderline = true;     armed.underline = toolOptions.textUnderline;
        armed.setStrikethrough = true; armed.strikethrough = toolOptions.textStrike;
        textEditor->ApplyCharFormat(armed);
    }
    textEditor->SetModified(false);

    // The text itself is left out of the drawing while its words are typed
    // over it, and out of the selection (its handles would frame nothing).
    if (text) {
        text->Style.Visible = false;
        selection->Clear();
    }
    textEditorHost->SetVisible(true);
    PlaceTextEditor();
    SyncTextBar();
    textEditor->SetFocus(true);
    if (statusHint) {
        statusHint->SetText(std::string(text ? "Editing the text" : "Typing a new text") +
                            ": Enter starts a new line; Escape, Ctrl+Enter or a click outside finishes");
    }
    canvas->Refresh();
}

void ArtCreatorWindow::PlaceTextEditor() {
    if (!textSession || !textEditor || !textEditorHost || !canvas) return;
    const VectorText& t = *textSession->model;
    // The text's own scale (its transform and its groups'), so a text drawn
    // twice as large is typed twice as large; a turn or a slant is not
    // followed - the words are typed upright at the text's start.
    const Matrix3x3& m = textSession->toDocument;
    const double scale = std::sqrt(std::max(1e-12, std::fabs(m.Determinant())));
    const double zoom = canvas->GetZoom() * scale;
    if (std::fabs(textEditor->GetZoom() - zoom) > 1e-6) textEditor->SetZoom(static_cast<float>(zoom));

    const Rect2Df c = canvas->GetBounds();
    textEditorHost->layoutItem.position->left = CSSLayout::Dimension::Px(c.x);
    textEditorHost->layoutItem.position->top = CSSLayout::Dimension::Px(c.y);
    textEditorHost->size.width = CSSLayout::Dimension::Px(c.width);
    textEditorHost->size.height = CSSLayout::Dimension::Px(c.height);

    // What is typed, as the drawing will hold it: its widest line sets the
    // editor's width (with room for the next letters, so it never wraps),
    // its lines its height.
    const double advance = LineAdvance(t);
    const double baseline = EditorBaseline(textMeasure.get(), SpanFont(t.BaseStyle, t.BaseStyle), advance);
    double widest = 0;
    int lineCount = 1;
    if (auto words = textEditor->GetDocument()) {
        const auto lines = LinesOfWords(*words);
        lineCount = std::max<int>(1, static_cast<int>(lines.size()));
        for (const auto& line : lines) {
            double width = 0;
            for (const RichTextRun* run : line) {
                const FontStyle fs = SpanFont(StyleOfRun(*run, t.BaseStyle), t.BaseStyle);
                size_t letters = 0;
                for (unsigned char ch : run->text) if ((ch & 0xC0) != 0x80) ++letters;
                width += static_cast<double>(letters) * t.BaseStyle.LetterSpacing;
                if (textMeasure) {
                    textMeasure->SetFontFace(fs.fontFamily, fs.fontWeight, fs.fontSlant);
                    textMeasure->SetFontSize(fs.fontSize);
                    width += textMeasure->GetTextLineWidth(run->text);
                } else {
                    width += static_cast<double>(letters) * fs.fontSize / kTextPointsPerUnit * 0.6;
                }
            }
            widest = std::max(widest, width);
        }
    }

    const double size = std::max(1.0f, t.BaseStyle.FontSize);
    const double contentHeight = std::max<double>(textEditor->GetContentHeight(), advance * lineCount);
    const Point2Dd at = canvas->DocToView(m.Transform(t.Position));
    const double width = std::ceil((widest + size * 2.0) * zoom) + 4.0;
    // The lines start at the text's point, or centre on it, or end there -
    // as the editor aligns them in its box, so the box goes round the point
    // that way.
    double left = at.x;
    if (t.BaseStyle.Anchor == TextAnchor::Middle) left = at.x - width / 2.0;
    else if (t.BaseStyle.Anchor == TextAnchor::End) left = at.x - width;
    const double top = at.y - baseline * zoom;
    Rect2Dd& r = textSession->viewRect;
    r = Rect2Dd(std::floor(left), std::floor(top), width, std::ceil(contentHeight * zoom) + 2.0);
    textEditor->layoutItem.position->left = CSSLayout::Dimension::Px(static_cast<float>(r.x));
    textEditor->layoutItem.position->top = CSSLayout::Dimension::Px(static_cast<float>(r.y));
    textEditor->size.width = CSSLayout::Dimension::Px(static_cast<float>(r.width));
    textEditor->size.height = CSSLayout::Dimension::Px(static_cast<float>(r.height));
    textEditorHost->InvalidateLayout();
    body->InvalidateLayout();
    body->RequestRedraw();
    canvas->Refresh();
}

void ArtCreatorWindow::DrawTextEditFrame(IRenderContext* ctx) {
    if (!ctx || !textSession) return;
    // Where the words go: a dashed frame round the editor, in the canvas's
    // view coordinates (the host lies on the canvas, so the editor's are the same).
    const Rect2Dd& b = textSession->viewRect;
    if (b.width <= 0 || b.height <= 0) return;
    const Rect2Dd r(b.x - 2.5, b.y - 2.5, b.width + 5.0, b.height + 5.0);
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern());
    ctx->SetStrokePaint(Color(255, 255, 255, 160));
    ctx->DrawRectangle(r);
    ctx->SetLineDash(UCDashPattern({4.0, 4.0}));
    ctx->SetStrokePaint(Color(30, 110, 230, 230));
    ctx->DrawRectangle(r);
    ctx->SetLineDash(UCDashPattern());
}

void ArtCreatorWindow::DropTextEdit() {
    if (!textSession) return;
    textSession.reset();
    if (textEditorHost) textEditorHost->SetVisible(false);
    if (body) body->RequestRedraw();
}

void ArtCreatorWindow::CommitTextEdit() {
    if (!textSession) return;
    // Null from here on, so a commit that re-enters (a focus change, the
    // history's notifications) finds nothing to commit twice.
    std::unique_ptr<TextEditSession> session = std::move(textSession);
    std::shared_ptr<UCRichDocument> words = textEditor ? textEditor->GetDocument() : nullptr;
    if (textEditorHost) textEditorHost->SetVisible(false);
    if (window && textEditor && window->GetFocusedElement() == textEditor.get()) canvas->SetFocus(true);
    if (body) body->RequestRedraw();
    if (!document) return;

    ElementPtr existing = session->elementId.empty() ? nullptr : document->FindElementById(session->elementId);
    auto* edited = existing ? dynamic_cast<VectorText*>(existing.get()) : nullptr;
    if (edited) edited->Style.Visible = session->model->Style.Visible;
    canvas->Refresh();
    if (!words) return;

    VectorText& model = *session->model;
    std::vector<TextSpanData> spans = SpansOfWords(*words, model);
    bool blank = true;
    for (const auto& span : spans) {
        if (span.Text.find_first_not_of(" \t") != std::string::npos) { blank = false; break; }
    }

    if (blank) {
        // Nothing typed: no text. An edited text emptied is deleted.
        if (edited) {
            const std::string id = session->elementId;
            history.Record("Delete Text", [&]() {
                if (auto e = document->FindElementById(id))
                    if (auto parent = e->Parent.lock()) parent->RemoveChild(id);
            });
            if (statusHint) statusHint->SetText("The text was emptied, so it was deleted");
        } else if (statusHint) {
            statusHint->SetText("Nothing was typed");
        }
        return;
    }

    if (edited) {
        // Looked at, not changed: no undo step.
        if (SameSpans(edited->Spans, spans) && SameTextStyle(edited->BaseStyle, model.BaseStyle) && !session->fillChanged) {
            Reselect({session->elementId});
            return;
        }
        const std::string id = session->elementId;
        history.Record("Edit Text", [&]() {
            auto e = document->FindElementById(id);
            auto* t = e ? dynamic_cast<VectorText*>(e.get()) : nullptr;
            if (!t) return;
            t->BaseStyle = model.BaseStyle;
            if (session->fillChanged) t->Style.Fill = model.Style.Fill;
            t->Spans = spans;
        });
        Reselect({id});
        if (statusHint) statusHint->SetText("Changed the text");
        return;
    }

    auto text = std::static_pointer_cast<VectorText>(model.Clone());
    text->Spans = spans;
    text->Id = GenerateId("text");
    const std::string id = text->Id;
    history.Record("Text", [&]() { if (auto layer = ActiveLayer()) layer->AddChild(text); });
    Reselect({id});
    if (statusHint) statusHint->SetText("Added the text: click it with the Text tool, or double-click it, to edit it");
}

// ===========================================================================
// THE TEXT BAR
// ===========================================================================

void ArtCreatorWindow::BuildTextBar() {
    // The framework's text bar (UltraCanvasTextFormatBar); sizes in points,
    // the drawing's units.
    textBar = CreateTextFormatBar("ac-textbar", 50.0f);
    textBar->SetSizeUnit(" pt", 1, 2000, 1, 1);
    textBar->SetLetterSpacingUnit(" pt", -100, 1000, 1, 0.5);
    textBar->SetBackgroundColor(Color(228, 228, 232, 255));   // the transform bar's, whose place it takes
    textBar->onFormatChanged = [this](TextFormatAttribute attribute, const TextFormat& f) {
        toolOptions.textFont = f.fontFamily;
        toolOptions.textSize = f.size;
        toolOptions.textBold = f.bold;
        toolOptions.textItalic = f.italic;
        toolOptions.textUnderline = f.underline;
        toolOptions.textStrike = f.strikethrough;
        toolOptions.textAlign = static_cast<int>(f.align);
        toolOptions.textLineSpacing = f.lineSpacing;
        toolOptions.textLetterSpacing = f.letterSpacing;
        static const char* names[] = { "font", "size", "bold", "italic", "underline", "strike",
                                       "align", "line-spacing", "letter-spacing" };
        ApplyTextAttribute(names[static_cast<int>(attribute)]);
    };
    // After a control of the bar, the keys go back to the text being typed.
    textBar->onControlDone = [this]() {
        if (IsEditingText() && textEditor && textEditorHost && textEditorHost->IsVisible()) textEditor->SetFocus(true);
    };
    textBar->SetVisible(false);
    window->AddChild(textBar);
    SyncTextBar();
}

void ArtCreatorWindow::ShowTextBar(bool show) {
    if (!textBar || !transformBar) return;
    if (textBar->IsVisible() == show) return;
    textBar->SetVisible(show);
    transformBar->SetVisible(!show);
    if (show) SyncTextBar();
    window->InvalidateLayout();
    window->RequestRedraw();
}

void ArtCreatorWindow::SyncTextBar() {
    if (!textBar) return;
    // What the bar shows: the caret's (or the selected words') attributes
    // in the text being typed, else the first selected text's, else what a
    // new text starts in.
    auto formatOf = [](const VectorTextStyle& st) {
        TextFormat f;
        f.fontFamily = st.FontFamily.empty() ? std::string("Sans") : st.FontFamily;
        f.size = std::max(0.1f, st.FontSize);
        f.bold = st.Weight >= FontWeight::Bold;
        f.italic = st.Slant != FontSlant::Normal;
        f.underline = st.Underline;
        f.strikethrough = st.StrikeThrough;
        f.align = static_cast<TextFormatAlign>(AlignOfTextAnchor(st.Anchor));
        f.lineSpacing = (st.LineHeight > 0 ? st.LineHeight : 1.2f) * 100.0f;
        f.letterSpacing = st.LetterSpacing;
        return f;
    };
    TextFormat shown;
    shown.fontFamily = toolOptions.textFont;
    shown.size = toolOptions.textSize;
    shown.bold = toolOptions.textBold;
    shown.italic = toolOptions.textItalic;
    shown.underline = toolOptions.textUnderline;
    shown.strikethrough = toolOptions.textStrike;
    shown.align = static_cast<TextFormatAlign>(std::clamp(toolOptions.textAlign, 0, 2));
    shown.lineSpacing = toolOptions.textLineSpacing;
    shown.letterSpacing = toolOptions.textLetterSpacing;
    if (textSession && textEditor) {
        shown = TextFormatAtCaret(textEditor->GetFormatState(), formatOf(textSession->model->BaseStyle), kTextPointsPerUnit);
    } else {
        for (const auto& e : selection->Elements()) {
            if (auto* t = e ? dynamic_cast<VectorText*>(e.get()) : nullptr) {
                shown = formatOf(t->BaseStyle);
                // Underlined or struck through all along, as the spans say.
                bool under = !t->Spans.empty(), strike = !t->Spans.empty();
                for (const auto& span : t->Spans) { under = under && span.Style.Underline; strike = strike && span.Style.StrikeThrough; }
                shown.underline = under;
                shown.strikethrough = strike;
                break;
            }
        }
    }
    textBar->SetFormat(shown);
}

void ArtCreatorWindow::ApplyTextAttribute(const std::string& attribute) {
    if (!document) return;
    const bool character = attribute == "font" || attribute == "size" || attribute == "bold" ||
                           attribute == "italic" || attribute == "underline" || attribute == "strike";
    // An attribute in a text's style, as the options now say.
    auto applyTo = [&](VectorTextStyle& style) {
        if (attribute == "font") style.FontFamily = toolOptions.textFont;
        else if (attribute == "size") style.FontSize = std::max(1.0f, toolOptions.textSize);
        else if (attribute == "bold") style.Weight = toolOptions.textBold ? FontWeight::Bold : FontWeight::Normal;
        else if (attribute == "italic") style.Slant = toolOptions.textItalic ? FontSlant::Italic : FontSlant::Normal;
        else if (attribute == "underline") style.Underline = toolOptions.textUnderline;
        else if (attribute == "strike") style.StrikeThrough = toolOptions.textStrike;
        else if (attribute == "align") style.Anchor = AnchorOfAlign(toolOptions.textAlign);
        else if (attribute == "line-spacing") style.LineHeight = std::max(0.1f, toolOptions.textLineSpacing / 100.0f);
        else if (attribute == "letter-spacing") style.LetterSpacing = toolOptions.textLetterSpacing;
    };

    if (textSession && textEditor) {
        if (character && textEditor->HasSelection()) {
            // The selected words take it, as spans of their own.
            static const std::map<std::string, TextFormatAttribute> which = {
                { "font", TextFormatAttribute::Font }, { "size", TextFormatAttribute::Size },
                { "bold", TextFormatAttribute::Bold }, { "italic", TextFormatAttribute::Italic },
                { "underline", TextFormatAttribute::Underline }, { "strike", TextFormatAttribute::Strikethrough } };
            const RichCharFormatDelta delta = TextFormatCharDelta(which.at(attribute), textBar->GetFormat(), kTextPointsPerUnit);
            textEditor->ApplyCharFormat(delta);
        } else {
            // Nothing selected, or a paragraph attribute: the whole text -
            // its base style, and every run gives up its own setting of it.
            applyTo(textSession->model->BaseStyle);
            if (auto words = textEditor->GetDocument()) {
                for (auto& block : words->blocks) {
                    for (auto& run : block.runs) {
                        if (attribute == "font") run.fontFamily.clear();
                        else if (attribute == "size") run.fontSizePt = 0.0f;
                        else if (attribute == "bold") run.bold = false;
                        else if (attribute == "italic") run.italic = false;
                        else if (attribute == "underline") run.underline = toolOptions.textUnderline;
                        else if (attribute == "strike") run.strikethrough = toolOptions.textStrike;
                    }
                }
            }
            if (attribute == "underline" || attribute == "strike") {
                // ... and what is typed next.
                RichCharFormatDelta armed;
                if (attribute == "underline") { armed.setUnderline = true; armed.underline = toolOptions.textUnderline; }
                else { armed.setStrikethrough = true; armed.strikethrough = toolOptions.textStrike; }
                textEditor->ApplyCharFormat(armed);
            }
            textEditor->SetStyle(TextEditorStyle());
            StampEditorParagraphs();
            textEditor->InvalidateDocument();
        }
        textEditor->SetFocus(true);
        PlaceTextEditor();
        SyncTextBar();
        return;
    }

    // Otherwise the selected texts, as one undo step (a run of a spinner's
    // steps as one).
    std::vector<std::string> ids;
    for (const auto& e : selection->Elements())
        if (e && e->Type == VectorElementType::Text) ids.push_back(e->Id);
    if (ids.empty()) return;
    static const std::map<std::string, std::string> labels = {
        { "font", "Text Font" }, { "size", "Text Size" }, { "bold", "Text Bold" }, { "italic", "Text Italic" },
        { "underline", "Text Underline" }, { "strike", "Text Strikethrough" }, { "align", "Text Alignment" },
        { "line-spacing", "Line Spacing" }, { "letter-spacing", "Letter Spacing" } };
    const auto label = labels.find(attribute);
    if (label == labels.end()) return;
    const auto all = selection->Ids();
    history.Record(label->second, [&]() {
        for (const auto& id : ids) {
            auto e = document->FindElementById(id);
            auto* t = e ? dynamic_cast<VectorText*>(e.get()) : nullptr;
            if (!t) continue;
            const double oldAdvance = LineAdvance(*t);
            applyTo(t->BaseStyle);
            for (auto& span : t->Spans) applyTo(span.Style);
            // The lines move to the new spacing (the size's or the line
            // spacing's): each stays the same number of lines down.
            const double newAdvance = LineAdvance(*t);
            if (oldAdvance > 0 && std::fabs(newAdvance - oldAdvance) > 1e-9) {
                for (auto& span : t->Spans) {
                    if (!span.Position) continue;
                    const double lines = std::round((span.Position->y - t->Position.y) / oldAdvance);
                    span.Position->y = t->Position.y + lines * newAdvance;
                }
            }
        }
    }, attribute == "size" || attribute == "line-spacing" || attribute == "letter-spacing");
    Reselect(all);
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
    RefreshTransformBar();   // lengths in icon pixels, or the page's unit
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
    iconPanel = PanelCard("ac-icon", "Icon");
    const float cardInner = CardInnerWidth(*iconPanel);

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

    iconPreview = std::make_shared<ArtIconPreview>("ac-icon-preview", cardInner);
    iconPreview->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
    iconPanel->AddChild(iconPreview);
    iconCaption = CreateLabel("ac-icon-caption", 0, 0, cardInner, 46, "");
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
