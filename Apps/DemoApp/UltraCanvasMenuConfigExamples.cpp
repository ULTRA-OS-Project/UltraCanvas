// Apps/DemoApp/UltraCanvasMenuConfigExamples.cpp
// Demonstrates the menus-and-toolbars editor (UltraCanvasCommandConfigWidget)
// on a model of the UltraCanvasFilerWidget: its file context menu (Cut/Copy/
// Paste, Rename/Delete, New >, Compress/Extract, Display >, Open With >,
// Print/Share/Attributes/Access and Settings) and a toolbar of the same
// commands in groups. One command registry builds both, and one editor edits
// both.
//
// This example is self-contained: it models the Filer without depending on the
// Filer widget itself. "Apply" rebuilds the preview menu and the toolbar; a
// right click on the toolbar shows its groups, folds the group under the
// pointer, hides a command, and "Customise Toolbars" selects it in the editor.
// Version: 2.0.0 - one editor for the menu and a toolbar (UltraCanvasCommandConfigWidget):
//                  the Filer toolbar built from groups (MaterializeToolbar) with its
//                  right-click menu (InstallToolbarContextMenu)
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): the preview
//                  menu, status and configurator in one card that fills the area
// Version: 1.1.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasCommandConfigWidget.h"
#include "UltraCanvasCommandRegistry.h"
#include "UltraCanvasMenuLayout.h"
#include "UltraCanvasToolbarLayout.h"
#include "UltraCanvasAppSettings.h"
#include "UltraCanvasConfig.h"
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
    void SeedFilerRegistry(UltraCanvasCommandRegistry& reg) {
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
        reg.RegisterAction("filer.cut",       Tr("Clipboard"), Tr("Cut"),       "", "Ctrl+X", log("Cut"));
        reg.RegisterAction("filer.copy",      Tr("Clipboard"), Tr("Copy"),      "", "Ctrl+C", log("Copy"));
        reg.RegisterAction("filer.paste",     Tr("Clipboard"), Tr("Paste"),     "", "Ctrl+V", log("Paste"));
        reg.RegisterAction("filer.duplicate", Tr("Clipboard"), Tr("Duplicate"), "", "Ctrl+D", log("Duplicate"));

        // ----- File operations -----
        reg.RegisterAction("filer.rename",   Tr("File"), Tr("Rename"),    "", "F2",  log("Rename"));
        reg.RegisterAction("filer.delete",   Tr("File"), Tr("Delete"),    "", "Del", log("Delete"));
        reg.RegisterAction("filer.openpath", Tr("File"), Tr("Open Path"), "", "",    log("Open Path"));

        // Open With is a provider-backed, app-populated submenu — model it as an
        // opaque dynamic leaf via the prototype escape hatch.
        {
            MenuCommand openWith("filer.openwith", Tr("File"), Tr("Open With"));
            openWith.prototype = []() {
                return MenuItemData::Submenu(Tr("Open With"),
                    []() -> std::vector<MenuItemData> {
                        return {
                            MenuItemData::Action(Tr("(app-provided list)"), []() {}),
                        };
                    });
            };
            reg.Register(openWith);
        }

        // ----- New document kinds ("New >") -----
        reg.RegisterAction("filer.new.text",        Tr("New Document"), Tr("Text"),        log("New Text"));
        reg.RegisterAction("filer.new.document",    Tr("New Document"), Tr("Document"),    log("New Document"));
        reg.RegisterAction("filer.new.spreadsheet", Tr("New Document"), Tr("Spreadsheet"), log("New Spreadsheet"));
        reg.RegisterAction("filer.new.bitmap",      Tr("New Document"), Tr("Bitmap"),      log("New Bitmap"));
        reg.RegisterAction("filer.new.vector",      Tr("New Document"), Tr("Vector"),      log("New Vector"));
        reg.RegisterAction("filer.new.audio",       Tr("New Document"), Tr("Audio"),       log("New Audio"));
        reg.RegisterAction("filer.new.video",       Tr("New Document"), Tr("Video"),       log("New Video"));

        // ----- Archive -----
        reg.RegisterAction("filer.compress", Tr("Archive"), Tr("Compress"), log("Compress"));
        reg.RegisterAction("filer.extract",  Tr("Archive"), Tr("Extract"),  log("Extract"));

        // ----- Display: view type (radio group 1) -----
        auto radioView = [&](const std::string& id, const std::string& label, bool checked) {
            MenuCommand c(id, Tr("Display"), label);
            c.type = MenuItemType::Radio;
            c.radioGroup = 1;
            c.defaultChecked = checked;
            c.onToggle = toggle(label + " view");
            reg.Register(c);
        };
        radioView("filer.view.details", Tr("Details"),     true);
        radioView("filer.view.list",    Tr("List"),        false);
        radioView("filer.view.thumbs",  Tr("Thumbnails"),  false);
        radioView("filer.view.barsize", Tr("Bar Size"),    false);
        radioView("filer.view.treemap", Tr("Tree Map"),    false);

        // ----- Display: sort field (radio group 2) -----
        auto radioSort = [&](const std::string& id, const std::string& label, bool checked) {
            MenuCommand c(id, Tr("Sort"), label);
            c.type = MenuItemType::Radio;
            c.radioGroup = 2;
            c.defaultChecked = checked;
            c.onToggle = toggle("Sort by " + label);
            reg.Register(c);
        };
        radioSort("filer.sort.name",     Tr("Name"),     true);
        radioSort("filer.sort.size",     Tr("Size"),     false);
        radioSort("filer.sort.type",     Tr("Type"),     false);
        radioSort("filer.sort.modified", Tr("Modified"), false);
        radioSort("filer.sort.created",  Tr("Created"),  false);

        // ----- Display: toggles -----
        {
            MenuCommand hidden("filer.showhidden", Tr("Display"), Tr("Show Hidden Files"));
            hidden.type = MenuItemType::Checkbox;
            hidden.defaultChecked = false;
            hidden.onToggle = toggle("Show hidden files");
            reg.Register(hidden);

            MenuCommand iconMenu("filer.iconmenu", Tr("Display"), Tr("Hover Icon Menu"));
            iconMenu.type = MenuItemType::Checkbox;
            iconMenu.defaultChecked = true;
            iconMenu.onToggle = toggle("Hover icon menu");
            reg.Register(iconMenu);
        }

        // ----- Extras -----
        reg.RegisterAction("filer.print",      Tr("Extras"), Tr("Print"),      log("Print"));
        reg.RegisterAction("filer.share",      Tr("Extras"), Tr("Share"),      log("Share"));
        reg.RegisterAction("filer.attributes", Tr("Extras"), Tr("Attributes"), log("Attributes"));
        reg.RegisterAction("filer.access",     Tr("Extras"), Tr("Access"),     log("Access"));
        reg.RegisterAction("filer.settings",   Tr("Extras"), Tr("Settings"),   log("Settings"));

        // The icons, by name: the shared line set has one size, so the
        // resolver gives the same file to the menu and the toolbar.
        const std::pair<const char*, const char*> icons[] = {
            { "filer.cut", "cut.svg" }, { "filer.copy", "copy.svg" }, { "filer.paste", "paste.svg" },
            { "filer.duplicate", "duplicate.svg" }, { "filer.rename", "rename.svg" },
            { "filer.delete", "delete.svg" }, { "filer.compress", "compress.svg" },
            { "filer.extract", "extract.svg" }, { "filer.view.details", "detail-view.svg" },
            { "filer.view.list", "view-type.svg" }, { "filer.view.thumbs", "thumbnails.svg" },
            { "filer.showhidden", "hidden-files.svg" }, { "filer.print", "print.svg" },
            { "filer.settings", "settings.svg" }, { "filer.openwith", "open-with.svg" },
        };
        for (const auto& [id, file] : icons) {
            if (const MenuCommand* found = reg.Find(id)) {
                MenuCommand withIcon = *found;
                withIcon.iconName = file;
                reg.Register(withIcon);
            }
        }
        reg.SetIconResolver([](const std::string& name, int) {
            return NormalizePath(GetResourcesDir() + "media/icons/menu/" + name);
        });
    }

    // The default toolbar: the same commands, a group per function.
    ToolbarLayout BuildFilerToolbarDefaults() {
        ToolbarLayout bar;
        bar.name = "filer-toolbar";
        ToolbarLayoutGroup view = ToolbarLayoutGroup::Make("view", Tr("View"),
                { "filer.view.details", "filer.view.list", "filer.view.thumbs" });
        view.folded = true;
        ToolbarLayoutGroup extras = ToolbarLayoutGroup::Make("extras", Tr("Extras"), { "filer.print", "filer.settings" });
        extras.visible = false;   // offered, off until the user turns it on
        bar.groups = {
            ToolbarLayoutGroup::Make("clipboard", Tr("Clipboard"),
                                     { "filer.cut", "filer.copy", "filer.paste", "filer.duplicate" }),
            ToolbarLayoutGroup::Make("file", Tr("File"), { "filer.rename", "filer.delete" }),
            ToolbarLayoutGroup::Make("archive", Tr("Archive"), { "filer.compress", "filer.extract" }),
            view,
            ToolbarLayoutGroup::Make("display", Tr("Display"), { "filer.showhidden" }),
            extras,
        };
        return bar;
    }

    // The default arrangement — a faithful model of the Filer's right-click menu.
    MenuLayoutSet BuildFilerContextLayout() {
        MenuLayoutSet set;
        MenuLayout& menu = set.FindOrAdd(Tr("Filer Context Menu"));

        MenuLayoutNode newDoc = MenuLayoutNode::Group(Tr("New"));
        newDoc.children = {
            MenuLayoutNode::Command("filer.new.text"),
            MenuLayoutNode::Command("filer.new.document"),
            MenuLayoutNode::Command("filer.new.spreadsheet"),
            MenuLayoutNode::Command("filer.new.bitmap"),
            MenuLayoutNode::Command("filer.new.vector"),
            MenuLayoutNode::Command("filer.new.audio"),
            MenuLayoutNode::Command("filer.new.video"),
        };

        MenuLayoutNode sort = MenuLayoutNode::Group(Tr("Sort By"));
        sort.children = {
            MenuLayoutNode::Command("filer.sort.name"),
            MenuLayoutNode::Command("filer.sort.size"),
            MenuLayoutNode::Command("filer.sort.type"),
            MenuLayoutNode::Command("filer.sort.modified"),
            MenuLayoutNode::Command("filer.sort.created"),
        };

        MenuLayoutNode display = MenuLayoutNode::Group(Tr("Display"));
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

    std::string DescribeLayout(const CommandLayoutSet& set) {
        size_t items = 0;
        for (const auto& m : set.menus.menus) {
            for (const auto& n : m.topLevel) items += 1 + n.children.size();
        }
        size_t groups = 0;
        for (const auto& t : set.toolbars) {
            for (const auto& g : t.groups) if (g.visible) ++groups;
        }
        return TrF("{0}, {1}; {2}, {3}",
                   TrN("{n} menu", "{n} menus", static_cast<long long>(set.menus.menus.size())),
                   TrN("{n} entry", "{n} entries", static_cast<long long>(items)),
                   TrN("{n} toolbar", "{n} toolbars", static_cast<long long>(set.toolbars.size())),
                   TrN("{n} group shown", "{n} groups shown", static_cast<long long>(groups)));
    }
} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMenuConfigExamples() {
    auto container = DemoKit::CreatePage("MenuConfigExamples",
        Tr("Menus and Toolbars - one editor for the Filer's menu and toolbar"),
        Tr("Left: the Filer's right-click menu and its toolbar, the toolbar's groups and commands "
        "with a box each - shown or not. Middle: add, remove, reorder, a new group, fold a "
        "group into one button. Right: every Filer command by category. Apply rebuilds the menu "
        "and the toolbar below now; Save also keeps them. Right-click the toolbar for its "
        "groups."));
    // The editor's lists scroll themselves: the page is the area's height and
    // the editor takes what the texts leave.
    container->SetFillsArea(true);

    auto card = DemoKit::CreateCard("MenuConfigCard");
    DemoKit::Add(container, card, 1);
    card->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);

    // ----- live previews: a menubar whose single entry opens the configured
    // context menu, and the Filer toolbar -----
    auto previewMenu = std::make_shared<UltraCanvasMenu>("MenuConfigPreviewBar");
    previewMenu->SetMenuType(MenuType::Menubar);
    previewMenu->size.height = CSSLayout::Dimension::Px(28);
    DemoKit::Add(card, previewMenu);

    auto toolbar = std::make_shared<UltraCanvasToolbar>("MenuConfigFilerToolbar", 0, 0, 0, 38);
    toolbar->SetOverflowMode(ToolbarOverflowMode::Menu);
    DemoKit::Add(card, toolbar);

    auto status = DemoKit::AddText(card, "MenuConfigStatus", Tr("Ready."));
    status->SetTextColor(Color(90, 90, 90, 255));

    // ----- registry + defaults (demo-local so the sample commands don't
    // leak into any real application registry) -----
    static UltraCanvasCommandRegistry filerRegistry;
    SeedFilerRegistry(filerRegistry);
    const MenuLayoutSet menuDefaults = BuildFilerContextLayout();
    const ToolbarLayout toolbarDefaults = BuildFilerToolbarDefaults();
    auto toolbarLayout = std::make_shared<ToolbarLayout>(toolbarDefaults);

    auto applyMenu = [previewMenu](const MenuLayoutSet& set) {
        previewMenu->Clear();
        if (!set.menus.empty()) {
            MenuItemData rootItem;
            rootItem.type = MenuItemType::Submenu;
            rootItem.label = Tr("Filer menu");
            rootItem.subItems = MaterializeLayout(set.menus.front(), filerRegistry);
            previewMenu->AddItem(rootItem);
        }
    };

    // ----- the editor -----
    auto widget = std::make_shared<UltraCanvasCommandConfigWidget>("MenuConfigWidget");
    widget->SetRegistry(&filerRegistry);
    widget->SetLayoutSet(menuDefaults);
    widget->AddToolbar(*toolbarLayout, toolbarDefaults, Tr("Filer toolbar"));
    UltraCanvasCommandConfigWidget* editor = widget.get();

    // The toolbar, rebuilt from its layout; its right-click menu edits that
    // layout, and the editor then shows the change.
    auto refreshEditor = [editor, toolbarLayout, toolbarDefaults]() {
        editor->ClearToolbars();
        editor->AddToolbar(*toolbarLayout, toolbarDefaults, Tr("Filer toolbar"));
    };
    MaterializeToolbar(*toolbar, *toolbarLayout, filerRegistry);
    InstallToolbarContextMenu(*toolbar, toolbarLayout, filerRegistry,
        [refreshEditor, status]() {
            refreshEditor();
            status->SetText(Tr("Toolbar changed from its right-click menu."));
        },
        [editor, status]() {
            editor->SelectLayoutNode("t0");
            status->SetText(Tr("The Filer toolbar is selected in the editor below."));
        });

    auto applyAll = [applyMenu, toolbar, toolbarLayout, status](const CommandLayoutSet& set) {
        applyMenu(set.menus);
        if (const ToolbarLayout* bar = set.FindToolbar("filer-toolbar")) {
            *toolbarLayout = *bar;
            MaterializeToolbar(*toolbar, *toolbarLayout, filerRegistry);
        }
        status->SetText(TrF("Applied (menu and toolbar updated): {0}", DescribeLayout(set)));
    };
    widget->onApply = applyAll;
    widget->onSave = [applyAll, status, toolbarDefaults](const CommandLayoutSet& set) {
        applyAll(set);
        const std::filesystem::path folder = std::filesystem::temp_directory_path();
        const std::string menuPath = PathToUtf8(folder / "ultracanvas_filer_menu.txt");
        auto settings = UltraCanvasAppSettings::AtFile(PathToUtf8(folder / "ultracanvas_filer_settings.json"));
        settings.Load();
        bool ok = SaveMenuLayoutSet(set.menus, menuPath);
        if (const ToolbarLayout* bar = set.FindToolbar("filer-toolbar")) {
            ok = SaveToolbarLayout(settings, *bar, toolbarDefaults) && ok;
        }
        status->SetText(ok ? TrF("Saved to {0} and {1}", menuPath, settings.File()) : Tr("Save failed."));
    };
    widget->onReset = [status]() {
        status->SetText(Tr("Reset to the default Filer menu and toolbar."));
    };
    widget->onCancel = [status]() {
        status->SetText(Tr("Cancelled (no changes applied)."));
    };

    DemoKit::Add(card, widget, 1);
    widget->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);

    // Seed the preview menu so it isn't empty on first view.
    applyMenu(menuDefaults);

    return container;
}

} // namespace UltraCanvas
