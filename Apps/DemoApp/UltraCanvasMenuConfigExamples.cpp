// Apps/DemoApp/UltraCanvasMenuConfigExamples.cpp
// Demonstrates the menu configuration widget end-to-end: a command registry, an
// editable menu layout, and a live "Apply" that rebuilds a preview menu bar.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasMenuConfigWidget.h"
#include "UltraCanvasMenuRegistry.h"
#include "UltraCanvasMenuLayout.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"
#include <filesystem>

namespace UltraCanvas {

namespace {
    // Populate a registry with a representative catalog of commands, grouped by
    // category. Callbacks just log — in a real app they would invoke app actions.
    void SeedDemoRegistry(UltraCanvasMenuRegistry& reg) {
        reg.Clear();

        auto log = [](const std::string& what) {
            return [what]() { debugOutput << "[MenuConfig demo] invoked: " << what << std::endl; };
        };

        // File
        reg.RegisterAction("file.new",   "File", "New",        "", "Ctrl+N", log("New"));
        reg.RegisterAction("file.open",  "File", "Open...",    "", "Ctrl+O", log("Open"));
        reg.RegisterAction("file.save",  "File", "Save",       "", "Ctrl+S", log("Save"));
        reg.RegisterAction("file.saveas","File", "Save As...", "", "Ctrl+Shift+S", log("Save As"));
        reg.RegisterAction("file.print", "File", "Print...",   "", "Ctrl+P", log("Print"));
        reg.RegisterAction("file.quit",  "File", "Quit",       "", "Ctrl+Q", log("Quit"));

        // Edit
        reg.RegisterAction("edit.undo",  "Edit", "Undo",  "", "Ctrl+Z", log("Undo"));
        reg.RegisterAction("edit.redo",  "Edit", "Redo",  "", "Ctrl+Y", log("Redo"));
        reg.RegisterAction("edit.cut",   "Edit", "Cut",   "", "Ctrl+X", log("Cut"));
        reg.RegisterAction("edit.copy",  "Edit", "Copy",  "", "Ctrl+C", log("Copy"));
        reg.RegisterAction("edit.paste", "Edit", "Paste", "", "Ctrl+V", log("Paste"));
        reg.RegisterAction("edit.find",  "Edit", "Find...", "", "Ctrl+F", log("Find"));

        // View
        reg.RegisterAction("view.zoomin",  "View", "Zoom In",  "", "Ctrl++", log("Zoom In"));
        reg.RegisterAction("view.zoomout", "View", "Zoom Out", "", "Ctrl+-", log("Zoom Out"));
        reg.RegisterAction("view.fullscreen", "View", "Full Screen", "", "F11", log("Full Screen"));

        // A checkbox-style command shows non-Action callback wiring.
        MenuCommand wordWrap("view.wordwrap", "View", "Word Wrap");
        wordWrap.type = MenuItemType::Checkbox;
        wordWrap.defaultChecked = true;
        wordWrap.onToggle = [](bool on) {
            debugOutput << "[MenuConfig demo] Word Wrap = " << (on ? "on" : "off") << std::endl;
        };
        reg.Register(wordWrap);

        // Help
        reg.RegisterAction("help.docs",  "Help", "Documentation", "", "F1", log("Documentation"));
        reg.RegisterAction("help.about", "Help", "About",         "", "",   log("About"));

        // A provider-backed dynamic submenu registered via the prototype escape
        // hatch — it stays an opaque, non-decomposable leaf in the editor.
        MenuCommand recent("file.recent", "File", "Recent Files");
        recent.prototype = []() {
            return MenuItemData::Submenu("Recent Files",
                []() -> std::vector<MenuItemData> {
                    return {
                        MenuItemData::Action("(dynamically generated)", []() {}),
                    };
                });
        };
        reg.Register(recent);
    }

    // Build the default editable layout: a main menu bar plus a context menu, both
    // held under the single "Program" root inside the widget.
    MenuLayoutSet BuildDefaultLayout() {
        MenuLayoutSet set;

        MenuLayout& bar = set.FindOrAdd("Main Menu Bar");
        {
            MenuLayoutNode file = MenuLayoutNode::Group("File");
            file.children = {
                MenuLayoutNode::Command("file.new"),
                MenuLayoutNode::Command("file.open"),
                MenuLayoutNode::Command("file.recent"),
                MenuLayoutNode::Separator(),
                MenuLayoutNode::Command("file.save"),
                MenuLayoutNode::Command("file.saveas"),
                MenuLayoutNode::Separator(),
                MenuLayoutNode::Command("file.quit"),
            };
            MenuLayoutNode edit = MenuLayoutNode::Group("Edit");
            edit.children = {
                MenuLayoutNode::Command("edit.undo"),
                MenuLayoutNode::Command("edit.redo"),
                MenuLayoutNode::Separator(),
                MenuLayoutNode::Command("edit.cut"),
                MenuLayoutNode::Command("edit.copy"),
                MenuLayoutNode::Command("edit.paste"),
            };
            MenuLayoutNode view = MenuLayoutNode::Group("View");
            view.children = {
                MenuLayoutNode::Command("view.zoomin"),
                MenuLayoutNode::Command("view.zoomout"),
                MenuLayoutNode::Command("view.wordwrap"),
            };
            bar.topLevel = { file, edit, view };
        }

        MenuLayout& ctx = set.FindOrAdd("Editor Context Menu");
        ctx.topLevel = {
            MenuLayoutNode::Command("edit.cut"),
            MenuLayoutNode::Command("edit.copy"),
            MenuLayoutNode::Command("edit.paste"),
            MenuLayoutNode::Separator(),
            MenuLayoutNode::Command("edit.find"),
        };

        return set;
    }

    std::string DescribeLayout(const MenuLayoutSet& set) {
        size_t items = 0;
        for (const auto& m : set.menus) {
            for (const auto& n : m.topLevel) {
                items += 1 + n.children.size();
            }
        }
        return std::to_string(set.menus.size()) + " menu(s), "
               + std::to_string(items) + " top-level/first-level entries";
    }
} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMenuConfigExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("MenuConfigExamples", 0, 0, 1000, 640);
    container->layout.SetFlexColumn().SetFlexGap(8)
             .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    container->SetPadding(12.0f);

    auto title = std::make_shared<UltraCanvasLabel>("MenuConfigTitle", "Menu Configuration Widget");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    container->AddChild(title);

    auto desc = std::make_shared<UltraCanvasLabel>("MenuConfigDesc",
        "Left: the menu structure (Program root holds every menu). Middle: add/remove/reorder. "
        "Right: available commands. Apply rebuilds the preview now; Save also writes to disk.");
    desc->SetWrap(TextWrap::WrapWord);
    container->AddChild(desc);

    // ----- live preview menu bar (reflects Apply) -----
    auto preview = std::make_shared<UltraCanvasMenu>("MenuConfigPreviewBar");
    preview->SetMenuType(MenuType::Menubar);
    preview->size.height = CSSLayout::Dimension::Px(28);
    preview->layoutItem.SetFlexShrink(0);
    container->AddChild(preview);

    auto status = std::make_shared<UltraCanvasLabel>("MenuConfigStatus", "Ready.");
    status->SetTextColor(Color(90, 90, 90, 255));
    container->AddChild(status);

    // ----- registry + default layout -----
    // Use a demo-local registry so the sample commands don't leak into any real
    // application menu registry.
    static UltraCanvasMenuRegistry demoRegistry;
    SeedDemoRegistry(demoRegistry);
    MenuLayoutSet defaults = BuildDefaultLayout();

    auto applyToPreview = [preview, status](const MenuLayoutSet& set) {
        if (!set.menus.empty()) {
            MaterializeInto(*preview, set.menus.front(), demoRegistry);
        }
        status->SetText("Applied (preview updated): " + DescribeLayout(set));
    };

    // ----- the configuration widget -----
    auto widget = std::make_shared<UltraCanvasMenuConfigWidget>("MenuConfigWidget");
    widget->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    widget->SetRegistry(&demoRegistry);
    widget->SetLayoutSet(defaults);

    widget->onApply = [applyToPreview](const MenuLayoutSet& set) {
        applyToPreview(set);
    };
    widget->onSave = [applyToPreview, status](const MenuLayoutSet& set) {
        applyToPreview(set);
        std::string path = (std::filesystem::temp_directory_path() / "ultracanvas_menu_layout.txt").string();
        bool ok = SaveMenuLayoutSet(set, path);
        status->SetText(ok ? ("Saved to " + path) : "Save failed.");
    };
    widget->onReset = [status]() {
        status->SetText("Reset to defaults.");
    };
    widget->onCancel = [status]() {
        status->SetText("Cancelled (no changes applied).");
    };

    container->AddChild(widget);

    // Seed the preview with the default layout so it isn't empty on first view.
    applyToPreview(defaults);

    return container;
}

} // namespace UltraCanvas
