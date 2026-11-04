// Apps/ArtCreator/ArtCreatorCommands.cpp
// ArtCreator's commands: every action of the menus, the toolbar, the tool
// palette and the keyboard, registered once per window in its
// UltraCanvasCommandRegistry. The menu bar, the toolbar and the palette are
// built from layouts of command ids (the program's defaults below, with the
// user's changes kept in the settings folder); the keyboard runs the command
// whose shortcut a key press is; and the menus and toolbars read each
// command's state - Undo with its step, what needs a selection, the active
// tool - from the command itself (RefreshCommandStates).
// Version: 1.2.0 - a command (but a View one) finishes the text being typed on the page first
// Version: 1.1.0 - the menus are saved with ArtCreator's and merged with a newer version's
//                  (a command it adds appears); the toolbar's commands have their own 24 px icons
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "ArtCreatorWindow.h"
#include "ArtCreatorSettings.h"
#include "UltraCanvasCommandConfigWidget.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace UltraCanvas {

using namespace VectorStorage;
using namespace VectorEdit;

namespace {
    // A tool's command id: "tool.pen", stable whatever the enum's order.
    std::string ToolCommandId(ArtToolId id) {
        switch (id) {
            case ArtToolId::Selector:     return "tool.selector";
            case ArtToolId::ShapeEditor:  return "tool.shape-editor";
            case ArtToolId::Pen:          return "tool.pen";
            case ArtToolId::Freehand:     return "tool.freehand";
            case ArtToolId::Line:         return "tool.line";
            case ArtToolId::Rectangle:    return "tool.rectangle";
            case ArtToolId::Ellipse:      return "tool.ellipse";
            case ArtToolId::QuickShape:   return "tool.quick-shape";
            case ArtToolId::Text:         return "tool.text";
            case ArtToolId::Fill:         return "tool.fill";
            case ArtToolId::Transparency: return "tool.transparency";
            case ArtToolId::Shadow:       return "tool.shadow";
            case ArtToolId::Feather:      return "tool.feather";
            case ArtToolId::Contour:      return "tool.contour";
            case ArtToolId::Bevel:        return "tool.bevel";
            case ArtToolId::Blend:        return "tool.blend";
            case ArtToolId::Mould:        return "tool.mould";
            case ArtToolId::Zoom:         return "tool.zoom";
            case ArtToolId::Push:         return "tool.push";
            default:                      return "tool." + std::to_string(static_cast<int>(id));
        }
    }

    // The palette's groups of tools, top to bottom: a slot each, the others
    // of a group in its fold-out bar.
    struct PaletteGroup {
        const char* id;
        const char* label;
        std::vector<ArtToolId> tools;
    };
    const std::vector<PaletteGroup>& PaletteGroups() {
        using T = ArtToolId;
        static const std::vector<PaletteGroup> groups = {
            { "select",  "Select",  { T::Selector, T::ShapeEditor } },
            { "draw",    "Draw",    { T::Pen, T::Freehand, T::Line } },
            { "shapes",  "Shapes",  { T::Rectangle, T::Ellipse, T::QuickShape } },
            { "text",    "Text",    { T::Text } },
            { "fill",    "Fill",    { T::Fill, T::Transparency } },
            { "effects", "Effects", { T::Shadow, T::Feather, T::Contour, T::Bevel } },   // drawn around a shape
            { "combine", "Combine", { T::Blend, T::Mould } },                             // shapes into new ones
            { "view",    "View",    { T::Zoom, T::Push } },
        };
        return groups;
    }

    bool FileExists(const std::string& path) {
        std::error_code ec;
        return !path.empty() && std::filesystem::is_regular_file(PathFromUtf8(path), ec);
    }

    // The menus' headings ("Align to selection") as commands of their own,
    // so the layouts can hold them.
    MenuCommand Heading(const std::string& id, const std::string& label) {
        MenuCommand c(id, "Headings", label);
        c.prototype = [label]() { return MenuItemData::Header(label); };
        return c;
    }

    // A command as a bare id in a layout.
    MenuLayoutNode Item(const char* id) { return MenuLayoutNode::Command(id); }
    MenuLayoutNode Sep() { return MenuLayoutNode::Separator(); }
    MenuLayoutNode Submenu(const char* label, std::vector<MenuLayoutNode> children) {
        MenuLayoutNode m = MenuLayoutNode::Group(label);
        m.children = std::move(children);
        return m;
    }

    ToolbarLayoutGroup Group(const char* id, const char* label, std::vector<std::string> commands,
                             std::vector<std::string> hidden = {}, bool visible = true) {
        ToolbarLayoutGroup g = ToolbarLayoutGroup::Make(id, label, commands);
        g.hidden = std::move(hidden);
        g.visible = visible;
        return g;
    }

    // The keys a shortcut names, as the registry spells them.
    std::string KeyName(UCKeys k) {
        if (k >= UCKeys::A && k <= UCKeys::Z) return std::string(1, static_cast<char>(k));
        if (k >= UCKeys::Key0 && k <= UCKeys::Key9)
            return std::string(1, static_cast<char>('0' + (static_cast<int>(k) - static_cast<int>(UCKeys::Key0))));
        return std::string();
    }

    constexpr float kToolIconPx = 28.0f;    // the palette's icons, on their 28 px grid
    constexpr float kToolButtonPx = 36.0f;  // a tool's button, square, 4 px around its icon
}

// ===========================================================================
// REGISTRATION
// ===========================================================================

void ArtCreatorWindow::RegisterCommands() {
    commands.Clear();
    commands.SetIconResolver([this](const std::string& name, int px) { return ResolveCommandIcon(name, px); });

    const auto hasSelection = [this]() { return selection && !selection->Empty(); };
    auto action = [this](const char* id, const char* category, const char* label, const char* icon,
                         const char* shortcut, std::function<void()> run,
                         std::function<bool()> enabled = nullptr) {
        MenuCommand c(id, category, label);
        c.iconName = icon ? icon : "";
        c.shortcut = shortcut ? shortcut : "";
        // A text being typed is finished first - it is part of the drawing
        // the command works on - except by the View commands, which only
        // move the view (the editor follows it) and give the keys back to
        // the text: the toolbar button pressed for them took them.
        if (std::string(id).rfind("view.", 0) == 0) {
            c.onClick = [this, run = std::move(run)]() {
                if (run) run();
                if (IsEditingText() && textEditor) textEditor->SetFocus(true);
            };
        } else {
            c.onClick = [this, run = std::move(run)]() { CommitTextEdit(); if (run) run(); };
        }
        c.isEnabled = std::move(enabled);
        commands.Register(c);
    };
    auto check = [this](const char* id, const char* category, const char* label, const char* icon,
                        std::function<bool()> isOn, std::function<void(bool)> set) {
        MenuCommand c(id, category, label);
        c.type = MenuItemType::Checkbox;
        c.iconName = icon ? icon : "";
        c.isChecked = std::move(isOn);
        c.onToggle = std::move(set);
        commands.Register(c);
    };

    // ----- File -----
    action("file.new", "File", "New...", "new.svg", "Ctrl+N", [this]() { CmdNew(); });
    action("file.new-window", "File", "New Window", "new-window.svg", "Ctrl+Alt+N", [this]() { CmdNewWindow(); });
    {
        MenuCommand newIcon("file.new-icon", "File", "New Icon");
        newIcon.iconName = "new-icon.svg";
        newIcon.prototype = [this]() {
            return MenuItemData::Submenu("New Icon", ResolveCommandIcon("new-icon.svg", 16), [this]() {
                std::vector<MenuItemData> sizes;
                for (int n : ArtIconSizes())
                    sizes.push_back(MenuItemData::Action(std::to_string(n) + " x " + std::to_string(n) + " px",
                                                         [this, n]() { CmdNewIcon(n); }));
                return sizes;
            });
        };
        commands.Register(newIcon);
    }
    action("file.open", "File", "Open...", "open.svg", "Ctrl+O", [this]() { CmdOpen(); });
    action("file.save", "File", "Save", "save.svg", "Ctrl+S", [this]() { CmdSave(); });
    action("file.save-as", "File", "Save As...", "save-as.svg", "Ctrl+Shift+S", [this]() { CmdSaveAs(); });
    action("file.export", "File", "Export...", "export.svg", "Ctrl+E", [this]() { CmdExport(); });
    action("file.document-setup", "File", "Document Setup...", "document-setup.svg", "", [this]() { CmdDocumentSetup(); });
    action("file.quit", "File", "Quit", "quit.svg", "Ctrl+Q", [this]() { CmdQuit(); });

    // ----- Edit -----
    {
        MenuCommand undo("edit.undo", "Edit", "Undo");
        undo.iconName = "undo.svg";
        undo.shortcut = "Ctrl+Z";
        undo.onClick = [this]() { CmdUndo(); };
        undo.isEnabled = [this]() { return history.CanUndo(); };
        undo.dynamicLabel = [this]() { return history.CanUndo() ? "Undo " + history.UndoLabel() : std::string("Undo"); };
        commands.Register(undo);
        MenuCommand redo("edit.redo", "Edit", "Redo");
        redo.iconName = "redo.svg";
        redo.shortcut = "Ctrl+Y";
        redo.onClick = [this]() { CmdRedo(); };
        redo.isEnabled = [this]() { return history.CanRedo(); };
        redo.dynamicLabel = [this]() { return history.CanRedo() ? "Redo " + history.RedoLabel() : std::string("Redo"); };
        commands.Register(redo);
    }
    action("edit.cut", "Edit", "Cut", "cut.svg", "Ctrl+X", [this]() { CmdCut(); }, hasSelection);
    action("edit.copy", "Edit", "Copy", "copy.svg", "Ctrl+C", [this]() { CmdCopy(); }, hasSelection);
    action("edit.paste", "Edit", "Paste", "paste.svg", "Ctrl+V", [this]() { CmdPaste(); });
    action("edit.duplicate", "Edit", "Duplicate", "duplicate.svg", "Ctrl+D", [this]() { CmdDuplicate(); }, hasSelection);
    action("edit.delete", "Edit", "Delete", "delete.svg", "Del", [this]() { CmdDelete(); }, hasSelection);
    action("edit.select-all", "Edit", "Select All", "select-all.svg", "Ctrl+A", [this]() { CmdSelectAll(); });
    action("edit.select-none", "Edit", "Select None", "select-none.svg", "Ctrl+Shift+A", [this]() { CmdSelectNone(); }, hasSelection);

    // ----- Arrange -----
    action("arrange.to-front", "Arrange", "Bring to Front", "to-front.svg", "Ctrl+F", [this]() { CmdReorder(ZOrderMove::ToFront); }, hasSelection);
    action("arrange.forward", "Arrange", "Bring Forward", "forward.svg", "Ctrl+Shift+F", [this]() { CmdReorder(ZOrderMove::Forward); }, hasSelection);
    action("arrange.backward", "Arrange", "Send Backward", "backward.svg", "Ctrl+Shift+B", [this]() { CmdReorder(ZOrderMove::Backward); }, hasSelection);
    action("arrange.to-back", "Arrange", "Send to Back", "to-back.svg", "Ctrl+B", [this]() { CmdReorder(ZOrderMove::ToBack); }, hasSelection);
    action("arrange.group", "Arrange", "Group", "group.svg", "Ctrl+G", [this]() { CmdGroup(); }, hasSelection);
    action("arrange.ungroup", "Arrange", "Ungroup", "ungroup.svg", "Ctrl+U", [this]() { CmdUngroup(); }, hasSelection);
    action("arrange.mirror-h", "Arrange", "Mirror Horizontally", "mirror-h.svg", "", [this]() { CmdMirror(true); }, hasSelection);
    action("arrange.mirror-v", "Arrange", "Mirror Vertically", "mirror-v.svg", "", [this]() { CmdMirror(false); }, hasSelection);
    commands.Register(Heading("heading.align-selection", "Align to selection"));
    action("align.left", "Align", "Left Edges", "align-left.svg", "", [this]() { CmdAlign(AlignMode::Left, false); }, hasSelection);
    action("align.hcentre", "Align", "Horizontal Centres", "align-hcentre.svg", "", [this]() { CmdAlign(AlignMode::HorizontalCenter, false); }, hasSelection);
    action("align.right", "Align", "Right Edges", "align-right.svg", "", [this]() { CmdAlign(AlignMode::Right, false); }, hasSelection);
    action("align.top", "Align", "Top Edges", "align-top.svg", "", [this]() { CmdAlign(AlignMode::Top, false); }, hasSelection);
    action("align.vcentre", "Align", "Vertical Centres", "align-vcentre.svg", "", [this]() { CmdAlign(AlignMode::VerticalCenter, false); }, hasSelection);
    action("align.bottom", "Align", "Bottom Edges", "align-bottom.svg", "", [this]() { CmdAlign(AlignMode::Bottom, false); }, hasSelection);
    commands.Register(Heading("heading.align-page", "Align to page"));
    action("align.page", "Align", "Centre on Page", "align-page.svg", "", [this]() {
        CmdAlign(AlignMode::HorizontalCenter, true);
        CmdAlign(AlignMode::VerticalCenter, true);
    }, hasSelection);
    commands.Register(Heading("heading.distribute", "Distribute"));
    action("distribute.hcentres", "Distribute", "Horizontal Centres Evenly", "distribute-h.svg", "", [this]() { CmdDistribute(DistributeMode::HorizontalCenters); }, hasSelection);
    action("distribute.vcentres", "Distribute", "Vertical Centres Evenly", "distribute-v.svg", "", [this]() { CmdDistribute(DistributeMode::VerticalCenters); }, hasSelection);
    action("distribute.hgaps", "Distribute", "Equal Horizontal Gaps", "space-h.svg", "", [this]() { CmdDistribute(DistributeMode::HorizontalGaps); }, hasSelection);
    action("distribute.vgaps", "Distribute", "Equal Vertical Gaps", "space-v.svg", "", [this]() { CmdDistribute(DistributeMode::VerticalGaps); }, hasSelection);
    commands.Register(Heading("heading.combine", "Combine shapes"));
    action("combine.add", "Combine", "Add Shapes", "combine-add.svg", "", [this]() { CmdCombine(CombineOp::Add); }, hasSelection);
    action("combine.subtract", "Combine", "Subtract Shapes", "combine-subtract.svg", "", [this]() { CmdCombine(CombineOp::Subtract); }, hasSelection);
    action("combine.intersect", "Combine", "Intersect Shapes", "combine-intersect.svg", "", [this]() { CmdCombine(CombineOp::Intersect); }, hasSelection);
    action("combine.slice", "Combine", "Slice Shapes", "combine-slice.svg", "", [this]() { CmdCombine(CombineOp::Slice); }, hasSelection);
    action("arrange.clipview", "Arrange", "Apply ClipView", "clipview.svg", "Ctrl+K", [this]() { CmdApplyClipView(); }, hasSelection);
    action("arrange.release", "Arrange", "Remove ClipView, Blend or Mould", "release.svg", "", [this]() { CmdRemoveContainer(); }, hasSelection);
    action("arrange.convert", "Arrange", "Convert to Editable Shapes", "shape-editor.svg", "Ctrl+Shift+C", [this]() { CmdConvertToPath(); }, hasSelection);
    action("arrange.snap-pixels", "Arrange", "Snap to Pixels", "snap-pixels.svg", "Ctrl+Shift+P", [this]() { CmdSnapToPixels(); }, hasSelection);

    // ----- Object -----
    action("object.no-fill", "Object", "No Fill", "no-fill.svg", "", [this]() { CmdRemoveFill(); }, hasSelection);
    action("object.no-line", "Object", "No Line", "no-line.svg", "", [this]() { CmdRemoveLine(); }, hasSelection);
    commands.Register(Heading("heading.line-points", "Line width in points"));
    action("line.pt-hairline", "Line width", "Hairline (0.25 pt)", "line-hairline.svg", "", [this]() { CmdLineWidth(0.25f); }, hasSelection);
    action("line.pt-05", "Line width", "0.5 pt", "line-05.svg", "", [this]() { CmdLineWidth(0.5f); }, hasSelection);
    action("line.pt-1", "Line width", "1 pt", "line-1.svg", "", [this]() { CmdLineWidth(1.0f); }, hasSelection);
    action("line.pt-2", "Line width", "2 pt", "line-2.svg", "", [this]() { CmdLineWidth(2.0f); }, hasSelection);
    action("line.pt-4", "Line width", "4 pt", "line-4.svg", "", [this]() { CmdLineWidth(4.0f); }, hasSelection);
    action("line.pt-8", "Line width", "8 pt", "line-8.svg", "", [this]() { CmdLineWidth(8.0f); }, hasSelection);
    commands.Register(Heading("heading.line-pixels", "Line width in pixels"));
    action("line.px-1", "Line width", "1 px", "line-1.svg", "", [this]() { CmdLineWidthPixels(1); }, hasSelection);
    action("line.px-2", "Line width", "2 px", "line-2.svg", "", [this]() { CmdLineWidthPixels(2); }, hasSelection);
    action("line.px-3", "Line width", "3 px", "line-3.svg", "", [this]() { CmdLineWidthPixels(3); }, hasSelection);
    action("line.px-4", "Line width", "4 px", "line-4.svg", "", [this]() { CmdLineWidthPixels(4); }, hasSelection);

    // ----- Layer -----
    action("layer.new", "Layer", "New Layer", "layer-add.svg", "Ctrl+Shift+N", [this]() { CmdLayerAdd(); });
    action("layer.delete", "Layer", "Delete Layer", "layer-delete.svg", "", [this]() { CmdLayerDelete(); });
    action("layer.up", "Layer", "Move Layer Up", "layer-up.svg", "", [this]() { CmdLayerMove(1); });
    action("layer.down", "Layer", "Move Layer Down", "layer-down.svg", "", [this]() { CmdLayerMove(-1); });
    action("layer.show", "Layer", "Show Layer", "layer-show.svg", "", [this]() { CmdLayerToggle(true); });
    action("layer.hide", "Layer", "Hide Layer", "layer-hide.svg", "", [this]() { CmdLayerToggle(false); });

    // ----- View -----
    action("view.zoom-in", "View", "Zoom In", "zoom-in.svg", "+", [this]() { if (canvas) canvas->ZoomIn(); });
    action("view.zoom-out", "View", "Zoom Out", "zoom-out.svg", "-", [this]() { if (canvas) canvas->ZoomOut(); });
    action("view.fit-page", "View", "Fit Page", "zoom-page.svg", "Ctrl+0", [this]() { if (canvas) canvas->ZoomToPage(); });
    action("view.fit-width", "View", "Fit Page Width", "zoom-width.svg", "Ctrl+2", [this]() { if (canvas) canvas->ZoomToPageWidth(); });
    action("view.fit-drawing", "View", "Fit Drawing", "fit-drawing.svg", "", [this]() { if (canvas) canvas->ZoomToDrawing(); });
    action("view.fit-selection", "View", "Fit Selection", "fit-selection.svg", "", [this]() { if (canvas) canvas->ZoomToSelection(); }, hasSelection);
    action("view.actual", "View", "Actual Size", "zoom-100.svg", "Ctrl+1", [this]() { if (canvas) canvas->ZoomToActual(); });
    check("view.rulers", "View", "Rulers", "rulers.svg",
          [this]() { return !canvas || canvas->GetShowRulers(); },
          [this](bool on) { if (canvas) canvas->SetShowRulers(on); RefreshCommandStates(); });
    check("view.grid", "View", "Grid", "grid.svg",
          [this]() { return canvas && canvas->GetGrid().visible; },
          [this](bool on) { if (!canvas) return; VectorGridSpec g = canvas->GetGrid(); g.visible = on; canvas->SetGrid(g); RefreshCommandStates(); });
    check("view.guides", "View", "Guides", "guides.svg",
          [this]() { return !canvas || canvas->GetShowGuides(); },
          [this](bool on) { if (canvas) canvas->SetShowGuides(on); RefreshCommandStates(); });
    check("snap.grid", "Snap", "Snap to Grid", "snap-grid.svg",
          [this]() { return canvas && canvas->GetSnapOptions().toGrid; },
          [this](bool on) { if (!canvas) return; VectorSnapOptions s = canvas->GetSnapOptions(); s.toGrid = on; canvas->SetSnapOptions(s); RefreshCommandStates(); });
    check("snap.guides", "Snap", "Snap to Guides", "snap-guides.svg",
          [this]() { return !canvas || canvas->GetSnapOptions().toGuides; },
          [this](bool on) { if (!canvas) return; VectorSnapOptions s = canvas->GetSnapOptions(); s.toGuides = on; canvas->SetSnapOptions(s); RefreshCommandStates(); });
    check("snap.objects", "Snap", "Snap to Objects", "snap-objects.svg",
          [this]() { return !canvas || canvas->GetSnapOptions().toObjects; },
          [this](bool on) { if (!canvas) return; VectorSnapOptions s = canvas->GetSnapOptions(); s.toObjects = on; canvas->SetSnapOptions(s); RefreshCommandStates(); });
    check("snap.page", "Snap", "Snap to Page", "snap-page.svg",
          [this]() { return !canvas || canvas->GetSnapOptions().toPage; },
          [this](bool on) { if (!canvas) return; VectorSnapOptions s = canvas->GetSnapOptions(); s.toPage = on; canvas->SetSnapOptions(s); RefreshCommandStates(); });
    action("view.clear-guides", "View", "Clear Guides", "clear-guides.svg", "", [this]() { if (canvas) canvas->ClearGuides(); });
    {
        MenuCommand sizes("view.icon-size", "View", "Icon Size");
        sizes.iconName = "new-icon.svg";
        sizes.prototype = [this]() {
            return MenuItemData::Submenu("Icon Size", ResolveCommandIcon("new-icon.svg", 16), [this]() {
                std::vector<MenuItemData> items;
                items.push_back(MenuItemData::Action(iconSize > 0 ? "Off" : "Off (now)", [this]() { SetIconSize(0); }));
                for (int n : ArtIconSizes()) {
                    std::string label = std::to_string(n) + " x " + std::to_string(n) + " px";
                    if (n == iconSize) label += " (now)";
                    items.push_back(MenuItemData::Action(label, [this, n]() { SetIconSize(n); }));
                }
                return items;
            });
        };
        commands.Register(sizes);
    }
    {
        // Display quality: outlines only, fills and lines, or antialiased -
        // a slider between its caption and its value, as one toolbar item.
        MenuCommand quality("view.quality", "View", "Display Quality");
        quality.makeToolbarWidget = [this](const std::string& id) -> std::shared_ptr<UltraCanvasUIElement> {
            auto box = std::make_shared<UltraCanvasContainer>(id, 0, 0, 0, 0);
            box->layout.SetFlexRow().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Center);
            box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            auto caption = std::make_shared<UltraCanvasLabel>(id + ".caption", 0, 0, 0, 24, "Quality");
            caption->SetFontSize(11);
            caption->SetElementSize(Size2Df(48, 24));
            box->AddChild(caption);
            qualitySlider = CreateHorizontalSlider(id + ".slider", 0, 0, 90, 24, 0, 2);
            qualitySlider->SetStep(1);
            const VectorDisplayQuality now = canvas ? canvas->GetDisplayQuality() : VectorDisplayQuality::Normal;
            qualitySlider->SetValue(static_cast<float>(now));
            qualitySlider->SetValueDisplay(SliderValueDisplay::NoDisplay);
            qualitySlider->SetTooltip("Display quality: outlines / fills and lines / antialiased");
            qualitySlider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            qualitySlider->onValueChanging = [this](float v) { SetDisplayQuality(static_cast<int>(std::lround(v))); };
            qualitySlider->onValueChanged = [this](float v) { SetDisplayQuality(static_cast<int>(std::lround(v))); };
            box->AddChild(qualitySlider);
            qualityLabel = std::make_shared<UltraCanvasLabel>(id + ".value", 0, 0, 0, 24, DisplayQualityName(now));
            qualityLabel->SetFontSize(11);
            qualityLabel->SetElementSize(Size2Df(84, 24));
            box->AddChild(qualityLabel);
            return box;
        };
        commands.Register(quality);
    }
    {
        // View > Toolbars: the toolbar's groups to show or hide, as its
        // right-click menu offers them.
        MenuCommand bars("view.toolbars", "View", "Toolbars");
        bars.iconName = "toolbar.svg";
        bars.prototype = [this]() {
            return MenuItemData::Submenu("Toolbars", ResolveCommandIcon("toolbar.svg", 16), [this]() {
                if (!toolbar || !toolbarLayout) return std::vector<MenuItemData>();
                return ToolbarContextMenuItems(*toolbar, toolbarLayout, commands, "",
                                               [this]() { ApplyToolbarLayout(); SaveLayouts(); },
                                               [this]() { OpenCustomise("t0"); });
            });
        };
        commands.Register(bars);
    }
    action("view.customise", "View", "Customise Menus and Toolbars...", "settings.svg", "", [this]() { OpenCustomise(); });

    // ----- Help -----
    action("help.about", "Help", "About ArtCreator", "about.svg", "", [this]() { CmdAbout(); });

    // ----- Tools: one of them is the active tool -----
    for (const auto& t : tools) {
        const ArtToolId id = t->id;
        MenuCommand c(ToolCommandId(id), "Tools", t->name);
        c.type = MenuItemType::Radio;
        c.radioGroup = 1;
        c.iconName = t->iconFile;
        if (t->shortcutKey) c.shortcut = std::string(1, t->shortcutKey);
        // A click on the active tool's button keeps it active.
        c.onToggle = [this, id](bool) { SelectTool(id); };
        c.isChecked = [this, id]() { return activeTool == id; };
        commands.Register(c);
    }

    for (const auto& conflict : commands.FindShortcutConflicts()) {
        std::string ids;
        for (const auto& id : conflict.commandIds) ids += (ids.empty() ? "" : ", ") + id;
        debugOutput << "ArtCreator: " << conflict.shortcut << " is the shortcut of " << ids << std::endl;
    }
}

// ===========================================================================
// THE PROGRAM'S ARRANGEMENTS
// ===========================================================================

MenuLayoutSet ArtCreatorWindow::DefaultMenuLayout() {
    MenuLayoutSet set;
    MenuLayout& bar = set.FindOrAdd("Menu bar");
    bar.topLevel = {
        Submenu("File", { Item("file.new"), Item("file.new-window"), Item("file.new-icon"), Item("file.open"), Sep(),
                       Item("file.save"), Item("file.save-as"), Item("file.export"), Sep(),
                       Item("file.document-setup"), Sep(), Item("file.quit") }),
        Submenu("Edit", { Item("edit.undo"), Item("edit.redo"), Sep(),
                       Item("edit.cut"), Item("edit.copy"), Item("edit.paste"), Item("edit.duplicate"), Item("edit.delete"), Sep(),
                       Item("edit.select-all"), Item("edit.select-none") }),
        Submenu("Arrange", { Item("arrange.to-front"), Item("arrange.forward"), Item("arrange.backward"), Item("arrange.to-back"), Sep(),
                          Item("arrange.group"), Item("arrange.ungroup"), Sep(),
                          Item("arrange.mirror-h"), Item("arrange.mirror-v"), Sep(),
                          Item("heading.align-selection"), Item("align.left"), Item("align.hcentre"), Item("align.right"),
                          Item("align.top"), Item("align.vcentre"), Item("align.bottom"), Sep(),
                          Item("heading.align-page"), Item("align.page"), Sep(),
                          Item("heading.distribute"), Item("distribute.hcentres"), Item("distribute.vcentres"),
                          Item("distribute.hgaps"), Item("distribute.vgaps"), Sep(),
                          Item("heading.combine"), Item("combine.add"), Item("combine.subtract"), Item("combine.intersect"),
                          Item("combine.slice"), Sep(),
                          Item("arrange.clipview"), Item("arrange.release"), Sep(),
                          Item("arrange.convert"), Item("arrange.snap-pixels") }),
        Submenu("Object", { Item("object.no-fill"), Item("object.no-line"), Sep(),
                         Item("heading.line-points"), Item("line.pt-hairline"), Item("line.pt-05"), Item("line.pt-1"),
                         Item("line.pt-2"), Item("line.pt-4"), Item("line.pt-8"),
                         Item("heading.line-pixels"), Item("line.px-1"), Item("line.px-2"), Item("line.px-3"), Item("line.px-4") }),
        Submenu("Layer", { Item("layer.new"), Item("layer.delete"), Sep(), Item("layer.up"), Item("layer.down"), Sep(),
                        Item("layer.show"), Item("layer.hide") }),
        Submenu("View", { Item("view.zoom-in"), Item("view.zoom-out"), Item("view.fit-page"), Item("view.fit-width"),
                       Item("view.fit-drawing"),
                       Item("view.fit-selection"), Item("view.actual"), Sep(),
                       Item("view.rulers"), Item("view.grid"), Item("view.guides"), Sep(),
                       Item("snap.grid"), Item("snap.guides"), Item("snap.objects"), Item("snap.page"), Item("view.clear-guides"), Sep(),
                       Item("view.icon-size"), Sep(),
                       Item("view.toolbars"), Item("view.customise") }),
        Submenu("Help", { Item("help.about") }),
    };
    return set;
}

ToolbarLayout ArtCreatorWindow::DefaultToolbarLayout() {
    ToolbarLayout bar;
    bar.name = "ac-toolbar";
    bar.groups = {
        Group("file", "File", { "file.new", "file.open", "file.save", "file.save-as", "file.export" },
              { "file.save-as", "file.export" }),
        Group("history", "History", { "edit.undo", "edit.redo" }),
        Group("zoom", "Zoom", { "view.zoom-out", "view.zoom-in", "view.fit-page", "view.fit-width", "view.actual",
                                "view.fit-drawing", "view.fit-selection" },
              { "view.fit-drawing", "view.fit-selection" }),
        Group("arrange", "Arrange", { "arrange.group", "arrange.ungroup", "arrange.to-front", "arrange.to-back",
                                      "arrange.forward", "arrange.backward" },
              { "arrange.forward", "arrange.backward" }),
        Group("mirror", "Mirror", { "arrange.mirror-h", "arrange.mirror-v" }),
        Group("quality", "Quality", { "view.quality" }),
        // Offered, off until the user turns them on.
        Group("edit", "Edit", { "edit.cut", "edit.copy", "edit.paste", "edit.duplicate", "edit.delete" }, {}, false),
        Group("align", "Align", { "align.left", "align.hcentre", "align.right", "align.top", "align.vcentre",
                                  "align.bottom", "align.page" }, {}, false),
        Group("distribute", "Distribute", { "distribute.hcentres", "distribute.vcentres", "distribute.hgaps",
                                            "distribute.vgaps" }, {}, false),
        Group("combine", "Combine", { "combine.add", "combine.subtract", "combine.intersect", "combine.slice",
                                      "arrange.clipview", "arrange.release" }, {}, false),
        Group("layer", "Layer", { "layer.new", "layer.delete", "layer.up", "layer.down" }, {}, false),
        Group("line", "Line", { "object.no-fill", "object.no-line", "line.pt-hairline", "line.pt-1", "line.pt-2",
                                "line.pt-4" }, {}, false),
        Group("guides", "Guides", { "view.rulers", "view.grid", "view.guides", "view.clear-guides" }, {}, false),
        Group("snap", "Snap", { "snap.grid", "snap.guides", "snap.objects", "snap.page", "arrange.snap-pixels" },
              {}, false),
    };
    return bar;
}

ToolbarLayout ArtCreatorWindow::DefaultPaletteLayout() {
    ToolbarLayout bar;
    bar.name = "ac-palette";
    // The palette's icons are drawn on a 28 px grid (media/icons/artcreator/
    // 28): shown at 28 px their 2 px strokes land on whole pixels.
    bar.iconSize = ToolbarIconSize::Custom;
    bar.separateGroups = false;
    for (const auto& g : PaletteGroups()) {
        std::vector<std::string> ids;
        for (ArtToolId id : g.tools) ids.push_back(ToolCommandId(id));
        ToolbarLayoutGroup group = ToolbarLayoutGroup::Make(g.id, g.label, ids);
        group.folded = true;
        bar.groups.push_back(group);
    }
    // A tool in no group gets one of its own, so a tool added to
    // CreateArtTools() never goes missing.
    std::vector<std::string> placed;
    for (const auto& g : bar.groups) placed.insert(placed.end(), g.commands.begin(), g.commands.end());
    for (int i = 0; i < static_cast<int>(ArtToolId::Count); ++i) {
        const std::string id = ToolCommandId(static_cast<ArtToolId>(i));
        if (std::find(placed.begin(), placed.end(), id) != placed.end()) continue;
        ToolbarLayoutGroup own = ToolbarLayoutGroup::Make(id, id, { id });
        own.folded = true;
        bar.groups.push_back(own);
    }
    return bar;
}

std::string ArtCreatorWindow::ResolveCommandIcon(const std::string& name, int pixels) const {
    if (name.empty()) return std::string();
    // The toolbar's Open has always been the shared folder.
    const std::string shared = (name == "open.svg") ? "folder.svg" : name;
    std::vector<std::string> tries;
    if (pixels <= 16) {
        tries = { MenuIconPath(name), IconPath(name), SharedIconPath(shared), ToolIconPath(name) };
    } else if (pixels >= 28) {
        tries = { ToolIconPath(name), IconPath(name), SharedIconPath(shared), MenuIconPath(name) };
    } else {
        // 24 px: the app's set, the framework's shared set; else the 16 px
        // drawing shown larger, rather than no icon at all.
        tries = { IconPath(name), SharedIconPath(shared), MenuIconPath(name), ToolIconPath(name) };
    }
    for (const auto& path : tries) if (FileExists(path)) return path;
    return std::string();
}

// ===========================================================================
// THE USER'S ARRANGEMENTS
// ===========================================================================

namespace {
    std::string MenuLayoutFile() {
        const std::filesystem::path folder = ArtCreatorSettings::ConfigFolder();
        return folder.empty() ? std::string() : PathToUtf8(folder / "menus.txt");
    }
}

void ArtCreatorWindow::LoadLayouts() {
    // The user's menus brought forward to this version's: a command added
    // since they were saved appears at its place, one they removed does not.
    const std::string menuFile = MenuLayoutFile();
    if (menuFile.empty() || !LoadMenuLayoutSet(menuLayout, menuFile, DefaultMenuLayout(), &commands)) {
        menuLayout = DefaultMenuLayout();
    }

    UltraCanvasAppSettings layouts("ArtCreator");
    layouts.Load();
    toolbarLayout = std::make_shared<ToolbarLayout>(LoadToolbarLayout(layouts, DefaultToolbarLayout(), &commands));
    paletteLayout = std::make_shared<ToolbarLayout>(LoadToolbarLayout(layouts, DefaultPaletteLayout(), &commands));
}

void ArtCreatorWindow::SaveLayouts() {
    const std::string menuFile = MenuLayoutFile();
    if (!menuFile.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(PathFromUtf8(menuFile).parent_path(), ec);
        if (!SaveMenuLayoutSet(menuLayout, menuFile, DefaultMenuLayout()))
            debugOutput << "ArtCreator: could not write " << menuFile << std::endl;
    }
    // Read first: the file holds the settings of every toolbar, and of
    // other windows that saved since this one loaded.
    UltraCanvasAppSettings layouts("ArtCreator");
    layouts.Load();
    if (toolbarLayout) SaveToolbarLayout(layouts, *toolbarLayout, DefaultToolbarLayout());
    if (paletteLayout) SaveToolbarLayout(layouts, *paletteLayout, DefaultPaletteLayout());
}

void ArtCreatorWindow::ApplyMenuLayout() {
    if (!menuBar || menuLayout.menus.empty()) return;
    MaterializeInto(*menuBar, menuLayout.menus.front(), commands);
}

void ArtCreatorWindow::ApplyToolbarLayout() {
    if (!toolbar || !toolbarLayout) return;
    MaterializeToolbar(*toolbar, *toolbarLayout, commands);
    // Flat buttons with rounded corners, as the panels' icon bars.
    for (const auto& item : toolbar->GetItems()) {
        if (auto b = std::dynamic_pointer_cast<UltraCanvasButton>(item)) {
            ButtonStyle flat = b->GetStyle();
            flat.cornerRadius = 6.0f;
            flat.pressedColor = Color(0, 0, 0, 30);
            b->SetStyle(flat);
        }
    }
}

void ArtCreatorWindow::ApplyPaletteLayout() {
    if (!palette || !paletteLayout) return;
    MaterializeToolbar(*palette, *paletteLayout, commands);
    for (const auto& item : palette->GetItems()) {
        auto b = std::dynamic_pointer_cast<UltraCanvasButton>(item);
        if (!b) continue;
        if (auto slot = std::dynamic_pointer_cast<UltraCanvasToolGroupButton>(b)) slot->SetSlotShowsLastPicked(true);
        // The fold-out bar's buttons take the slot's size, icon size and style.
        b->SetIconSize(static_cast<int>(kToolIconPx), static_cast<int>(kToolIconPx));
        b->SetElementSize(Size2Df(kToolButtonPx, kToolButtonPx));
        // Flat: no box around a tool; a soft grey under the pointer and the
        // active tool on a light blue, both rounded.
        ButtonStyle flat = b->GetStyle();
        flat.normalColor = Colors::Transparent;
        flat.borderWidth = 0;
        flat.cornerRadius = 6.0f;
        flat.hoverColor = Color(0, 0, 0, 18);
        flat.pressedColor = Color(0, 103, 192, 46);
        b->SetStyle(flat);
    }
    palette->RefreshCommandStates();
}

void ArtCreatorWindow::RefreshCommandStates() {
    if (toolbar) toolbar->RefreshCommandStates();
    if (palette) palette->RefreshCommandStates();
    if (menuBar) commands.RefreshMenuStates(*menuBar);
}

void ArtCreatorWindow::OpenCustomise(const std::string& selectNode) {
    if (customiseWindow) {
        customiseWindow->RaiseAndFocus();
        return;
    }
    auto editor = std::make_shared<UltraCanvasCommandConfigWidget>("ac-customise");
    editor->SetRegistry(&commands);
    editor->SetLayoutSet(menuLayout);
    editor->AddToolbar(*toolbarLayout, DefaultToolbarLayout(), "Toolbar");
    editor->AddToolbar(*paletteLayout, DefaultPaletteLayout(), "Tool palette");
    // The palette takes tools, and only tools.
    editor->SetCatalogueFilter("ac-palette", [](const MenuCommand& c) { return c.id.rfind("tool.", 0) == 0; });
    auto apply = [this](const CommandLayoutSet& set) {
        menuLayout = set.menus;
        if (const ToolbarLayout* bar = set.FindToolbar("ac-toolbar")) *toolbarLayout = *bar;
        if (const ToolbarLayout* bar = set.FindToolbar("ac-palette")) *paletteLayout = *bar;
        ApplyMenuLayout();
        ApplyToolbarLayout();
        ApplyPaletteLayout();
        RefreshCommandStates();
    };
    editor->onApply = apply;
    editor->onSave = [this, apply](const CommandLayoutSet& set) {
        apply(set);
        SaveLayouts();
    };
    // The window goes when the editor's Save or Cancel closes it.
    editor->onCancel = []() {};
    customiseWindow = ShowCommandConfigDialog(window.get(), editor, "Customise Menus and Toolbars");
    if (!customiseWindow) return;
    customiseWindow->onWindowClosed = [this]() { customiseWindow.reset(); };
    if (!selectNode.empty()) editor->SelectLayoutNode(selectNode);
}

// ===========================================================================
// THE KEYBOARD
// ===========================================================================

bool ArtCreatorWindow::RunShortcut(const UCEvent& e) {
    const std::string key = KeyName(e.virtualKey);
    if (key.empty()) return false;
    std::string shortcut;
    if (e.ctrl) shortcut += "Ctrl+";
    if (e.alt) shortcut += "Alt+";
    if (e.shift) shortcut += "Shift+";
    shortcut += key;
    const MenuCommand* cmd = commands.FindByShortcut(shortcut);
    // Ctrl+Shift+Z is Redo too, as everywhere.
    if (!cmd && shortcut == "Ctrl+Shift+Z") cmd = commands.Find("edit.redo");
    if (!cmd || !cmd->IsEnabledNow()) return cmd != nullptr;
    if (cmd->type == MenuItemType::Checkbox) {
        if (cmd->onToggle) cmd->onToggle(!cmd->IsCheckedNow());
    } else if (cmd->type == MenuItemType::Radio) {
        if (cmd->onToggle) cmd->onToggle(true);
    } else if (cmd->onClick) {
        cmd->onClick();
    }
    return true;
}

} // namespace UltraCanvas
