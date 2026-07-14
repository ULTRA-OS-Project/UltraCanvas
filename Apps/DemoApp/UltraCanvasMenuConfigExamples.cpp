// Apps/DemoApp/UltraCanvasMenuConfigExamples.cpp
// Demonstrates the menu configuration widget by letting the user customise the
// UltraCanvasFilerWidget's file context menu (the right-click menu described in
// PR #119). The command registry and default layout below mirror that menu:
// Cut/Copy/Paste, Rename/Delete, New >, Compress/Extract, Display >, Open With >,
// Print/Share/Attributes/Access and Settings.
//
// This example is self-contained: it models the Filer menu without depending on
// the Filer widget itself, so the configurator can be demonstrated on a real,
// meaningful menu. "Apply" rebuilds a live preview of the right-click menu.
// Version: 1.1.0
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
    // Populate a registry that mirrors the UltraCanvasFilerWidget context menu.
    // Callbacks just log — in the real Filer these invoke CopySelection(),
    // DeleteSelection(), CreateNewDocument(), SetViewType(), etc.
    void SeedFilerRegistry(UltraCanvasMenuRegistry& reg) {
        reg.Clear();

        auto log = [](const std::string& what) {
            return [what]() { debugOutput << "[Filer menu] " << what << std::endl; };
        };
        auto toggle = [](const std::string& what) {
            return [what](bool on) {
                debugOutput << "[Filer menu] " << what << " = " << (on ? "on" : "off") << std::endl;
            };
        };

        // ----- Clipboard -----
        reg.RegisterAction("filer.cut",       "Clipboard", "Cut",       "", "Ctrl+X", log("Cut"));
        reg.RegisterAction("filer.copy",      "Clipboard", "Copy",      "", "Ctrl+C", log("Copy"));
        reg.RegisterAction("filer.paste",     "Clipboard", "Paste",     "", "Ctrl+V", log("Paste"));
        reg.RegisterAction("filer.duplicate", "Clipboard", "Duplicate", "", "Ctrl+D", log("Duplicate"));

        // ----- File operations -----
        reg.RegisterAction("filer.rename",   "File", "Rename",    "", "F2",  log("Rename"));
        reg.RegisterAction("filer.delete",   "File", "Delete",    "", "Del", log("Delete"));
        reg.RegisterAction("filer.openpath", "File", "Open Path", "", "",    log("Open Path"));

        // Open With is a provider-backed, app-populated submenu — model it as an
        // opaque dynamic leaf via the prototype escape hatch.
        {
            MenuCommand openWith("filer.openwith", "File", "Open With");
            openWith.prototype = []() {
                return MenuItemData::Submenu("Open With",
                    []() -> std::vector<MenuItemData> {
                        return {
                            MenuItemData::Action("(app-provided list)", []() {}),
                        };
                    });
            };
            reg.Register(openWith);
        }

        // ----- New document kinds ("New >") -----
        reg.RegisterAction("filer.new.text",        "New Document", "Text",        log("New Text"));
        reg.RegisterAction("filer.new.document",    "New Document", "Document",    log("New Document"));
        reg.RegisterAction("filer.new.spreadsheet", "New Document", "Spreadsheet", log("New Spreadsheet"));
        reg.RegisterAction("filer.new.bitmap",      "New Document", "Bitmap",      log("New Bitmap"));
        reg.RegisterAction("filer.new.vector",      "New Document", "Vector",      log("New Vector"));
        reg.RegisterAction("filer.new.audio",       "New Document", "Audio",       log("New Audio"));
        reg.RegisterAction("filer.new.video",       "New Document", "Video",       log("New Video"));

        // ----- Archive -----
        reg.RegisterAction("filer.compress", "Archive", "Compress", log("Compress"));
        reg.RegisterAction("filer.extract",  "Archive", "Extract",  log("Extract"));

        // ----- Display: view type (radio group 1) -----
        auto radioView = [&](const std::string& id, const std::string& label, bool checked) {
            MenuCommand c(id, "Display", label);
            c.type = MenuItemType::Radio;
            c.radioGroup = 1;
            c.defaultChecked = checked;
            c.onToggle = toggle(label + " view");
            reg.Register(c);
        };
        radioView("filer.view.details", "Details",     true);
        radioView("filer.view.list",    "List",        false);
        radioView("filer.view.thumbs",  "Thumbnails",  false);
        radioView("filer.view.barsize", "Bar Size",    false);
        radioView("filer.view.treemap", "Tree Map",    false);

        // ----- Display: sort field (radio group 2) -----
        auto radioSort = [&](const std::string& id, const std::string& label, bool checked) {
            MenuCommand c(id, "Sort", label);
            c.type = MenuItemType::Radio;
            c.radioGroup = 2;
            c.defaultChecked = checked;
            c.onToggle = toggle("Sort by " + label);
            reg.Register(c);
        };
        radioSort("filer.sort.name",     "Name",     true);
        radioSort("filer.sort.size",     "Size",     false);
        radioSort("filer.sort.type",     "Type",     false);
        radioSort("filer.sort.modified", "Modified", false);
        radioSort("filer.sort.created",  "Created",  false);

        // ----- Display: toggles -----
        {
            MenuCommand hidden("filer.showhidden", "Display", "Show Hidden Files");
            hidden.type = MenuItemType::Checkbox;
            hidden.defaultChecked = false;
            hidden.onToggle = toggle("Show hidden files");
            reg.Register(hidden);

            MenuCommand iconMenu("filer.iconmenu", "Display", "Hover Icon Menu");
            iconMenu.type = MenuItemType::Checkbox;
            iconMenu.defaultChecked = true;
            iconMenu.onToggle = toggle("Hover icon menu");
            reg.Register(iconMenu);
        }

        // ----- Extras -----
        reg.RegisterAction("filer.print",      "Extras", "Print",      log("Print"));
        reg.RegisterAction("filer.share",      "Extras", "Share",      log("Share"));
        reg.RegisterAction("filer.attributes", "Extras", "Attributes", log("Attributes"));
        reg.RegisterAction("filer.access",     "Extras", "Access",     log("Access"));
        reg.RegisterAction("filer.settings",   "Extras", "Settings",   log("Settings"));
    }

    // The default arrangement — a faithful model of the Filer's right-click menu.
    MenuLayoutSet BuildFilerContextLayout() {
        MenuLayoutSet set;
        MenuLayout& menu = set.FindOrAdd("Filer Context Menu");

        MenuLayoutNode newDoc = MenuLayoutNode::Group("New");
        newDoc.children = {
            MenuLayoutNode::Command("filer.new.text"),
            MenuLayoutNode::Command("filer.new.document"),
            MenuLayoutNode::Command("filer.new.spreadsheet"),
            MenuLayoutNode::Command("filer.new.bitmap"),
            MenuLayoutNode::Command("filer.new.vector"),
            MenuLayoutNode::Command("filer.new.audio"),
            MenuLayoutNode::Command("filer.new.video"),
        };

        MenuLayoutNode sort = MenuLayoutNode::Group("Sort By");
        sort.children = {
            MenuLayoutNode::Command("filer.sort.name"),
            MenuLayoutNode::Command("filer.sort.size"),
            MenuLayoutNode::Command("filer.sort.type"),
            MenuLayoutNode::Command("filer.sort.modified"),
            MenuLayoutNode::Command("filer.sort.created"),
        };

        MenuLayoutNode display = MenuLayoutNode::Group("Display");
        display.children = {
            MenuLayoutNode::Command("filer.view.details"),
            MenuLayoutNode::Command("filer.view.list"),
            MenuLayoutNode::Command("filer.view.thumbs"),
            MenuLayoutNode::Command("filer.view.barsize"),
            MenuLayoutNode::Command("filer.view.treemap"),
            MenuLayoutNode::Separator(),
            sort,
            MenuLayoutNode::Separator(),
            MenuLayoutNode::Command("filer.showhidden"),
            MenuLayoutNode::Command("filer.iconmenu"),
        };

        menu.topLevel = {
            MenuLayoutNode::Command("filer.openwith"),
            MenuLayoutNode::Command("filer.openpath"),
            MenuLayoutNode::Separator(),
            MenuLayoutNode::Command("filer.cut"),
            MenuLayoutNode::Command("filer.copy"),
            MenuLayoutNode::Command("filer.paste"),
            MenuLayoutNode::Command("filer.duplicate"),
            MenuLayoutNode::Separator(),
            MenuLayoutNode::Command("filer.rename"),
            MenuLayoutNode::Command("filer.delete"),
            MenuLayoutNode::Separator(),
            newDoc,
            MenuLayoutNode::Command("filer.compress"),
            MenuLayoutNode::Command("filer.extract"),
            MenuLayoutNode::Separator(),
            display,
            MenuLayoutNode::Separator(),
            MenuLayoutNode::Command("filer.print"),
            MenuLayoutNode::Command("filer.share"),
            MenuLayoutNode::Command("filer.attributes"),
            MenuLayoutNode::Command("filer.access"),
            MenuLayoutNode::Separator(),
            MenuLayoutNode::Command("filer.settings"),
        };

        return set;
    }

    std::string DescribeLayout(const MenuLayoutSet& set) {
        size_t items = 0;
        for (const auto& m : set.menus) {
            for (const auto& n : m.topLevel) items += 1 + n.children.size();
        }
        return std::to_string(set.menus.size()) + " menu(s), "
               + std::to_string(items) + " entries";
    }
} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMenuConfigExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("MenuConfigExamples", 0, 0, 1000, 640);
    container->layout.SetFlexColumn().SetFlexGap(8)
             .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    container->SetPadding(12.0f);

    auto title = std::make_shared<UltraCanvasLabel>("MenuConfigTitle",
        "Menu Configurator — customise the Filer file menu");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    container->AddChild(title);

    auto desc = std::make_shared<UltraCanvasLabel>("MenuConfigDesc",
        "Left: the Filer's right-click menu (under the Program root). Middle: add / remove / "
        "reorder. Right: every available Filer command by category. Apply rebuilds the preview "
        "menu now (temporary); Save also writes the layout to disk. Open the 'Filer menu' button "
        "above to try the configured menu.");
    desc->SetWrap(TextWrap::WrapWord);
    container->AddChild(desc);

    // ----- live preview: a menubar whose single entry opens the configured
    // context menu, so Apply is visible and clickable -----
    auto preview = std::make_shared<UltraCanvasMenu>("MenuConfigPreviewBar");
    preview->SetMenuType(MenuType::Menubar);
    preview->size.height = CSSLayout::Dimension::Px(28);
    preview->layoutItem.SetFlexShrink(0);
    container->AddChild(preview);

    auto status = std::make_shared<UltraCanvasLabel>("MenuConfigStatus", "Ready.");
    status->SetTextColor(Color(90, 90, 90, 255));
    container->AddChild(status);

    // ----- registry + default layout (demo-local so the sample commands don't
    // leak into any real application registry) -----
    static UltraCanvasMenuRegistry filerRegistry;
    SeedFilerRegistry(filerRegistry);
    MenuLayoutSet defaults = BuildFilerContextLayout();

    auto applyToPreview = [preview, status](const MenuLayoutSet& set) {
        preview->Clear();
        if (!set.menus.empty()) {
            MenuItemData rootItem;
            rootItem.type = MenuItemType::Submenu;
            rootItem.label = "Filer menu \xE2\x96\xBE"; // "Filer menu ▾"
            rootItem.subItems = MaterializeLayout(set.menus.front(), filerRegistry);
            preview->AddItem(rootItem);
        }
        status->SetText("Applied (preview menu updated): " + DescribeLayout(set));
    };

    // ----- the configuration widget -----
    auto widget = std::make_shared<UltraCanvasMenuConfigWidget>("MenuConfigWidget");
    widget->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    widget->SetRegistry(&filerRegistry);
    widget->SetLayoutSet(defaults);

    widget->onApply = [applyToPreview](const MenuLayoutSet& set) {
        applyToPreview(set);
    };
    widget->onSave = [applyToPreview, status](const MenuLayoutSet& set) {
        applyToPreview(set);
        std::string path = (std::filesystem::temp_directory_path() / "ultracanvas_filer_menu.txt").string();
        bool ok = SaveMenuLayoutSet(set, path);
        status->SetText(ok ? ("Saved to " + path) : "Save failed.");
    };
    widget->onReset = [status]() {
        status->SetText("Reset to the default Filer menu.");
    };
    widget->onCancel = [status]() {
        status->SetText("Cancelled (no changes applied).");
    };

    container->AddChild(widget);

    // Seed the preview so it isn't empty on first view.
    applyToPreview(defaults);

    return container;
}

} // namespace UltraCanvas
