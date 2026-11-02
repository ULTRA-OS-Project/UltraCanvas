// Tests/ToolbarLayoutTest.cpp
// A toolbar built from groups of command ids, its state read from the
// commands, and the user's arrangement kept across releases.
//
// - The command registry resolves a command's icon by size, reads its live
//   state into a menu entry (Materialize, RefreshMenuStates), and finds two
//   commands given one shortcut, however each spells it - ArtCreator's menu
//   showed Ctrl+Shift+S for Convert to Editable Shapes while the key ran Save
//   As, which this would have caught.
// - MaterializeToolbar puts one separator between two shown groups and none
//   elsewhere, leaves hidden groups, hidden commands and unknown ids out,
//   shows a command once, makes toggles of check commands, widgets of widget
//   commands and one slot of a folded group; RefreshCommandStates follows the
//   commands' state without the program keeping pointers to buttons.
// - SaveToolbarLayout keeps only what the user changed; LoadToolbarLayout
//   merges it over the next release's defaults: a group's new command
//   appears, a new group goes in at its place, a group the release dropped
//   and a command no longer registered disappear; ForgetToolbarLayout is the
//   defaults again.
//
// Headless: no window, the settings file in the temp folder. No image library
// either, so the toolbars are built without icon files (icon resolution is
// checked on its own).
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasMenu.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasToolbarLayout.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

std::string Join(const std::vector<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) out += (out.empty() ? "" : ",") + id;
    return out;
}

// A small program's commands and the state they read.
struct Program {
    UltraCanvasMenuRegistry registry;
    bool canUndo = false;
    std::string undoStep;
    bool grid = false;
    int saved = 0, fronted = 0, backed = 0;

    Program() {
        registry.SetIconResolver([](const std::string& name, int px) {
            return "icons/" + std::to_string(px) + "/" + name;
        });
        MenuCommand cmd("file.new", "File", "New");
        cmd.iconName = "new.svg";
        cmd.shortcut = "Ctrl+N";
        cmd.onClick = []() {};
        registry.Register(cmd);
        registry.RegisterAction("file.open", "File", "Open", "", "Ctrl+O", []() {});
        registry.RegisterAction("file.save", "File", "Save", "", "Ctrl+S", [this]() { ++saved; });
        registry.RegisterAction("file.save-as", "File", "Save As", "", "Ctrl+Shift+S", []() {});
        registry.RegisterAction("file.export", "File", "Export", "", "Ctrl+E", []() {});

        MenuCommand undo("edit.undo", "Edit", "Undo");
        undo.iconName = "undo.svg";
        undo.shortcut = "Ctrl+Z";
        undo.onClick = []() {};
        undo.isEnabled = [this]() { return canUndo; };
        undo.dynamicLabel = [this]() { return undoStep.empty() ? std::string("Undo") : "Undo " + undoStep; };
        registry.Register(undo);

        MenuCommand gridCmd("view.grid", "View", "Grid");
        gridCmd.type = MenuItemType::Checkbox;
        gridCmd.iconName = "grid.svg";
        gridCmd.onToggle = [this](bool on) { grid = on; };
        gridCmd.isChecked = [this]() { return grid; };
        registry.Register(gridCmd);

        MenuCommand quality("view.quality", "View", "Quality");
        quality.makeToolbarWidget = [](const std::string& id) -> std::shared_ptr<UltraCanvasUIElement> {
            return std::make_shared<UltraCanvasSlider>(id, 0, 0, 90, 24);
        };
        registry.Register(quality);

        registry.RegisterAction("arrange.front", "Arrange", "Bring to Front", "", "Ctrl+F", [this]() { ++fronted; });
        registry.RegisterAction("arrange.back", "Arrange", "Send to Back", "", "Ctrl+B", [this]() { ++backed; });
        // The ArtCreator mistake, spelt differently.
        registry.RegisterAction("arrange.convert", "Arrange", "Convert", "", "shift+ctrl+s", []() {});
    }
};

void TestRegistry() {
    std::printf("The command registry\n");
    Program p;
    const MenuCommand* fresh = p.registry.Find("file.new");
    Check(p.registry.ResolveIcon(*fresh, 24) == "icons/24/new.svg" &&
                  p.registry.ResolveIcon(*fresh, 16) == "icons/16/new.svg",
          "an icon name is resolved at the size asked for");
    MenuCommand plain("plain", "File", "Plain");
    plain.iconPath = "plain/open.svg";
    Check(p.registry.ResolveIcon(plain, 24) == "plain/open.svg", "a command with only an icon path keeps it");
    Check(fresh->TooltipText() == "New (Ctrl+N)", "the tooltip is the label and the shortcut");

    MenuItemData undo = p.registry.Materialize("edit.undo");
    Check(!undo.enabled && undo.label == "Undo" && undo.iconPath == "icons/16/undo.svg",
          "a menu entry has the command's state now and its 16 px icon");
    p.grid = true;
    Check(p.registry.Materialize("view.grid").checked, "and its checked state");

    const auto conflicts = p.registry.FindShortcutConflicts();
    Check(conflicts.size() == 1 && conflicts[0].shortcut == "Ctrl+Shift+S" &&
                  Join(conflicts[0].commandIds) == "file.save-as,arrange.convert",
          "two commands on one shortcut are found, however each spells it");

    UltraCanvasMenu menu("menu", 0, 0, 200, 0);
    menu.SetMenuType(MenuType::PopupMenu);
    MenuItemData edit = MenuItemData::Submenu("Edit", { p.registry.Materialize("edit.undo") });
    edit.commandId.clear();
    menu.AddItem(edit);
    menu.AddItem(p.registry.Materialize("view.grid"));
    p.canUndo = true;
    p.undoStep = "Move";
    p.grid = false;
    p.registry.RefreshMenuStates(menu);
    const auto& items = menu.GetItems();
    Check(items.size() == 2 && items[0].subItems.size() == 1 && items[0].subItems[0].enabled &&
                  items[0].subItems[0].label == "Undo Move",
          "RefreshMenuStates reads the state into a submenu's entries");
    Check(items.size() == 2 && !items[1].checked, "and a check entry follows the command");
}

void TestGroupHelpers() {
    std::printf("Groups and layouts\n");
    ToolbarLayoutGroup g = ToolbarLayoutGroup::Make("file", "File", { "file.new", "file.open", "file.save" });
    Check(g.SetCommandShown("file.open", false) && !g.IsCommandShown("file.open") &&
                  Join(g.ShownCommands()) == "file.new,file.save",
          "a hidden command stays in its group, out of the shown ones");
    Check(g.SetCommandShown("file.open", true) && Join(g.ShownCommands()) == "file.new,file.open,file.save",
          "and comes back in its place");
    Check(!g.SetCommandShown("view.grid", false), "a command of another group is refused");

    ToolbarLayout layout;
    layout.groups = { ToolbarLayoutGroup::Make("a", "A", {}), ToolbarLayoutGroup::Make("b", "B", {}),
                      ToolbarLayoutGroup::Make("c", "C", {}) };
    Check(layout.MoveGroup("c", 0) && layout.groups[0].id == "c" && layout.groups[2].id == "b",
          "MoveGroup moves a group to an index");
    Check(!layout.MoveGroup("x", 0) && !layout.MoveGroup("a", 3), "and refuses what is not there");
}

ToolbarLayout Defaults() {
    ToolbarLayout layout;
    layout.name = "main";
    layout.groups = {
        ToolbarLayoutGroup::Make("file", "File", { "file.new", "file.open", "file.save" }),
        ToolbarLayoutGroup::Make("history", "History", { "edit.undo" }),
        ToolbarLayoutGroup::Make("view", "View", { "view.grid", "view.quality", "nobody.knows" }),
        ToolbarLayoutGroup::Make("arrange", "Arrange", { "arrange.front", "arrange.back" }),
        ToolbarLayoutGroup::Make("align", "Align", { "file.new" }),   // only a command already shown
    };
    return layout;
}

void TestMaterialize() {
    std::printf("MaterializeToolbar\n");
    Program p;
    p.registry.SetIconResolver(nullptr);   // no icon files to load here
    ToolbarLayout layout = Defaults();
    layout.FindGroup("file")->SetCommandShown("file.save", false);
    layout.FindGroup("arrange")->folded = true;
    auto toolbar = std::make_shared<UltraCanvasToolbar>("main", 0, 0, 0, 38);
    MaterializeToolbar(*toolbar, layout, p.registry);
    Check(Join(toolbar->GetItemOrder()) ==
                  "file.new,file.open,sep.history,edit.undo,sep.view,view.grid,view.quality,sep.arrange,arrange",
          "one separator between two shown groups; hidden and unknown commands out; a command once");

    auto undo = std::dynamic_pointer_cast<UltraCanvasButton>(toolbar->GetWidget("edit.undo"));
    Check(undo && undo->IsDisabled() && undo->GetTooltip() == "Undo (Ctrl+Z)",
          "an item has its command's id, state and tooltip");
    p.canUndo = true;
    p.undoStep = "Move";
    toolbar->RefreshCommandStates();
    Check(undo && !undo->IsDisabled() && undo->GetTooltip() == "Undo Move (Ctrl+Z)",
          "RefreshCommandStates follows the command");

    auto grid = std::dynamic_pointer_cast<UltraCanvasButton>(toolbar->GetWidget("view.grid"));
    Check(grid && grid->CanToggle() && !grid->IsPressed(), "a check command is a toggle button");
    if (grid && grid->onToggle) grid->onToggle(true);
    Check(p.grid, "which switches the command");
    p.grid = false;
    if (grid) grid->SetPressed(true);
    toolbar->RefreshCommandStates();
    Check(grid && !grid->IsPressed(), "and is pressed as the command says");

    Check(std::dynamic_pointer_cast<UltraCanvasSlider>(toolbar->GetWidget("view.quality")) != nullptr,
          "a widget command is its widget");

    auto slot = std::dynamic_pointer_cast<UltraCanvasToolGroupButton>(toolbar->GetWidget("arrange"));
    Check(slot && slot->GetItemCount() == 2 && slot->GetItems()[1].tooltip == "Send to Back (Ctrl+B)",
          "a folded group is one slot of its commands");
    if (slot) slot->SelectItem("arrange.back");
    Check(p.backed == 1 && slot && !slot->IsPressed(), "picking an action there runs it and the slot springs back");

    layout.FindGroup("history")->visible = false;
    layout.FindGroup("view")->visible = false;
    layout.iconSize = ToolbarIconSize::Large;
    MaterializeToolbar(*toolbar, layout, p.registry);
    Check(Join(toolbar->GetItemOrder()) == "file.new,file.open,sep.arrange,arrange",
          "hidden groups leave no separator behind");
    Check(toolbar->GetAppearance().iconSize == ToolbarIconSize::Large, "the layout's icon size is the bar's");
    auto fresh = std::dynamic_pointer_cast<UltraCanvasButton>(toolbar->GetWidget("file.new"));
    Check(fresh && fresh->GetIconWidth() == 32, "and the buttons' icons are drawn at it");
}

void TestPersist() {
    std::printf("Saving and loading\n");
    Program p;
    const std::string file = PathToUtf8(std::filesystem::temp_directory_path() / "ultracanvas-toolbar-layout-test.json");
    std::error_code ec;
    std::filesystem::remove(PathFromUtf8(file), ec);
    auto settings = UltraCanvasAppSettings::AtFile(file);
    settings.Load();

    const ToolbarLayout defaults = Defaults();
    ToolbarLayout loaded = LoadToolbarLayout(settings, defaults, &p.registry);
    Check(loaded.groups.size() == 5 && Join(loaded.FindGroup("view")->commands) == "view.grid,view.quality",
          "nothing kept: the defaults, less the commands nobody registered");

    // The user's changes.
    ToolbarLayout mine = defaults;
    mine.MoveGroup("arrange", 0);
    mine.FindGroup("history")->visible = false;
    mine.FindGroup("file")->SetCommandShown("file.open", false);
    mine.FindGroup("arrange")->folded = true;
    ToolbarLayoutGroup own = ToolbarLayoutGroup::Make("mine", "My commands", { "file.save", "gone.command" });
    own.userMade = true;
    mine.groups.push_back(own);
    mine.iconSize = ToolbarIconSize::Small;
    mine.showLabels = true;
    Check(SaveToolbarLayout(settings, mine, defaults), "the layout is saved");

    const JSONValue& kept = settings.Document().Get("toolbars").Get("main");
    const JSONValue& fileEntry = kept.Get("groups").At(1);
    Check(fileEntry.Get("id").GetString() == "file" && !fileEntry.Contains("commands") &&
                  fileEntry.Get("hidden").GetSize() == 1,
          "a program's group is kept as its changes only, not its commands");

    auto reread = UltraCanvasAppSettings::AtFile(file);
    reread.Load();
    loaded = LoadToolbarLayout(reread, defaults);
    std::vector<std::string> order;
    for (const auto& g : loaded.groups) order.push_back(g.id);
    Check(Join(order) == "arrange,file,history,view,align,mine", "the order comes back");
    Check(!loaded.FindGroup("history")->visible && loaded.FindGroup("arrange")->folded &&
                  !loaded.FindGroup("file")->IsCommandShown("file.open"),
          "and the visibility, folding and hidden commands");
    Check(loaded.FindGroup("mine") && loaded.FindGroup("mine")->userMade &&
                  loaded.FindGroup("mine")->label == "My commands",
          "and the user's own group");
    Check(loaded.iconSize == ToolbarIconSize::Small && loaded.showLabels, "and the icon size and labels");

    // The next release: Export joins File, Snap is new after View, Arrange is gone.
    ToolbarLayout next = defaults;
    next.FindGroup("file")->commands.push_back("file.export");
    next.groups.erase(next.groups.begin() + 3);   // arrange
    auto snap = ToolbarLayoutGroup::Make("snap", "Snap", { "view.grid" });
    next.groups.insert(next.groups.begin() + 3, snap);   // after view
    loaded = LoadToolbarLayout(reread, next, &p.registry);
    order.clear();
    for (const auto& g : loaded.groups) order.push_back(g.id);
    Check(Join(order) == "file,history,view,snap,align,mine",
          "a new group goes in at its place, a dropped one is gone (got " + Join(order) + ")");
    Check(Join(loaded.FindGroup("file")->ShownCommands()) == "file.new,file.save,file.export",
          "a command the release added to a group shows, the user's hidden one stays hidden");
    Check(Join(loaded.FindGroup("mine")->commands) == "file.save",
          "a command no longer registered is dropped from the user's group");

    Check(ForgetToolbarLayout(reread, "main"), "the kept layout is forgotten");
    loaded = LoadToolbarLayout(reread, defaults);
    Check(loaded.groups.size() == 5 && loaded.FindGroup("history")->visible && !loaded.FindGroup("mine"),
          "and the defaults are back");
    std::filesystem::remove(PathFromUtf8(file), ec);
}

}  // namespace

int main() {
    TestRegistry();
    TestGroupHelpers();
    TestMaterialize();
    TestPersist();
    if (g_failures == 0) {
        std::printf("All toolbar layout checks passed.\n");
        return 0;
    }
    std::printf("%d check(s) FAILED.\n", g_failures);
    return 1;
}
