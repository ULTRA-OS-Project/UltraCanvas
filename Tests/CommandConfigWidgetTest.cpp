// Tests/CommandConfigWidgetTest.cpp
// One editor for a program's menus and toolbars (UltraCanvasCommandConfigWidget).
//
// The menu editor edited menus only; a program's toolbars are built from the
// same commands (MaterializeToolbar), so it now edits both, in one tree. What
// the test pins:
//
// - The tree holds the menus and the toolbars - groups and their commands with
//   a check box each - under the "Program" root.
// - On a toolbar: Add puts the catalog's command after the selected one, and
//   shows a hidden command again instead of doubling it; Remove hides the
//   program's commands and groups (they are the program's) and drops what the
//   user added; Move Up / Down reorder groups and commands; New Group makes a
//   group of the user's, which can be renamed (the program's cannot); Fold
//   folds a group; a check box hides or shows a group or a command; the icon
//   size and labels are the toolbar's; a catalog filter refuses commands.
// - The menus edit as they did: Add, Separator, New Group (a submenu), Move,
//   Remove.
// - The preview shows the selected toolbar as edited and runs nothing.
// - Apply hands over menus and toolbars together; Reset puts the menus back as
//   they were given and the toolbars back to the program's defaults.
//
// Headless: no window; the commands have no icons.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasCommandConfigWidget.h"
#include "UltraCanvasMenuConfigWidget.h"   // the old name still compiles

#include <cstdio>
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

struct Fixture {
    UltraCanvasCommandRegistry registry;
    int ran = 0;
    std::shared_ptr<UltraCanvasCommandConfigWidget> widget;
    ToolbarLayout defaults;

    Fixture() {
        for (const char* id : { "a", "b", "c", "d" }) {
            registry.RegisterAction(id, "Edit", std::string("Cmd ") + id, [this]() { ++ran; });
        }
        MenuCommand grid("grid", "View", "Grid");
        grid.type = MenuItemType::Checkbox;
        registry.Register(grid);

        MenuLayoutSet menus;
        MenuLayout& menu = menus.FindOrAdd("Main");
        menu.topLevel = { MenuLayoutNode::Command("a"), MenuLayoutNode::Separator(), MenuLayoutNode::Command("b") };

        defaults.name = "main";
        defaults.groups = { ToolbarLayoutGroup::Make("edit", "Edit", { "a", "b" }),
                            ToolbarLayoutGroup::Make("view", "View", { "c" }) };

        widget = std::make_shared<UltraCanvasCommandConfigWidget>("w", 0, 0, 900, 600);
        widget->SetRegistry(&registry);
        widget->SetLayoutSet(menus);
        widget->AddToolbar(defaults, defaults, "Main toolbar");
    }
    const ToolbarLayout& Bar() const { return widget->GetLayoutSet().toolbars.front(); }
    const ToolbarLayoutGroup& Group(size_t i) const { return Bar().groups[i]; }
    const std::vector<MenuLayoutNode>& Menu() const { return widget->GetLayoutSet().menus.menus.front().topLevel; }
    bool Has(const std::string& nodeId) const { return widget->GetLayoutTree()->FindNode(nodeId) != nullptr; }
    void Add(const std::string& at, const std::string& command) {
        widget->SelectLayoutNode(at);
        widget->SelectCatalogueCommand(command);
        widget->AddSelectedCommand();
    }
    void Press(const std::string& suffix) {
        if (auto* b = dynamic_cast<UltraCanvasButton*>(widget->FindChildById("w_" + suffix)); b && b->onClick) b->onClick();
    }
};

std::string MenuKinds(const std::vector<MenuLayoutNode>& nodes) {
    std::string out;
    for (const auto& n : nodes) {
        if (!out.empty()) out += ",";
        out += n.IsSeparator() ? "-" : n.IsGroup() ? "[" + n.label + "]" : n.commandId;
    }
    return out;
}

void TestTree() {
    std::printf("One tree for menus and toolbars\n");
    Fixture f;
    Check(f.Has("menus") && f.Has("m0") && f.Has("m0.2") && f.Has("toolbars") && f.Has("t0") &&
                  f.Has("t0.0") && f.Has("t0.0.1") && f.Has("t0.1.0"),
          "the menus, the toolbar, its groups and their commands are all in it");
    TreeNode* group = f.widget->GetLayoutTree()->FindNode("t0.0");
    TreeNode* menuRow = f.widget->GetLayoutTree()->FindNode("m0.0");
    Check(group && group->data.showCheckbox && group->data.checkState == TreeCheckState::Checked &&
                  menuRow && !menuRow->data.showCheckbox,
          "a toolbar group has a check box (checked: shown), a menu entry none");
    Check(f.widget->GetPreview() && f.widget->GetPreview()->IsVisible() &&
                  Join(f.widget->GetPreview()->GetItemOrder()) == "a,b,sep.view,c",
          "the preview is the toolbar as it is");
    std::shared_ptr<UltraCanvasMenuConfigWidget> old = CreateMenuConfigWidget("old", 0, 0, 400, 300);
    Check(old != nullptr, "UltraCanvasMenuConfigWidget is the same editor under its old name");
}

void TestToolbarEdits() {
    std::printf("Editing a toolbar\n");
    Fixture f;
    f.Add("t0.0.0", "d");
    Check(Join(f.Group(0).commands) == "a,d,b" && f.widget->GetSelectedLayoutNode() == "t0.0.1",
          "Add puts the command after the selected one and selects it");

    Check(f.widget->SetLayoutNodeChecked("t0.0.0", false) && !f.Group(0).IsCommandShown("a"),
          "a command's box hides it");
    Check(Join(f.widget->GetPreview()->GetItemOrder()) == "d,b,sep.view,c", "and the preview follows");
    f.Add("t0.0", "a");
    Check(Join(f.Group(0).commands) == "a,d,b" && f.Group(0).IsCommandShown("a"),
          "adding a command already in the group shows it again, once");

    f.widget->SelectLayoutNode("t0.0.2");   // b, the program's
    f.widget->RemoveSelected();
    Check(Join(f.Group(0).commands) == "a,d,b" && !f.Group(0).IsCommandShown("b"),
          "Remove hides the program's command, which stays in its group");
    f.widget->SelectLayoutNode("t0.0.1");   // d, the user's
    f.widget->RemoveSelected();
    Check(Join(f.Group(0).commands) == "a,b", "and drops one the user added");

    f.widget->SelectLayoutNode("t0.0.0");
    f.widget->MoveSelected(+1);
    Check(Join(f.Group(0).commands) == "b,a" && f.widget->GetSelectedLayoutNode() == "t0.0.1",
          "Move Down moves a command within its group, and the selection with it");
    f.widget->SelectLayoutNode("t0.1");
    f.widget->MoveSelected(-1);
    Check(f.Bar().groups[0].id == "view" && f.widget->GetSelectedLayoutNode() == "t0.0",
          "Move Up moves a group");

    f.widget->SelectLayoutNode("t0.0");
    f.widget->AddGroup();
    Check(f.Bar().groups.size() == 3 && f.Group(1).userMade && f.widget->GetSelectedLayoutNode() == "t0.1",
          "New Group makes a group of the user's after the selected one");
    Check(f.widget->RenameSelected("Mine") && f.Group(1).label == "Mine", "which can be named");
    f.Add("t0.1", "d");
    Check(Join(f.Group(1).commands) == "d", "and filled");
    f.widget->SelectLayoutNode("t0.0");
    Check(!f.widget->RenameSelected("Renamed") && f.Group(0).label == "View", "a program's group keeps its name");

    f.widget->SelectLayoutNode("t0.2");     // edit, the program's
    f.widget->ToggleFoldSelected();
    Check(f.Group(2).folded, "Fold folds a group");
    f.widget->RemoveSelected();
    Check(f.Bar().groups.size() == 3 && !f.Group(2).visible, "Remove hides a program's group");
    f.widget->SelectLayoutNode("t0.1");     // mine
    f.widget->RemoveSelected();
    Check(f.Bar().groups.size() == 2 && f.Bar().groups[1].id == "edit", "and drops the user's own");

    f.widget->SelectLayoutNode("t0");
    f.widget->SetSelectedToolbarIconSize(ToolbarIconSize::Large);
    f.widget->SetSelectedToolbarShowLabels(true);
    Check(f.Bar().iconSize == ToolbarIconSize::Large && f.Bar().showLabels &&
                  f.widget->GetPreview()->GetAppearance().iconSize == ToolbarIconSize::Large,
          "icon size and labels are the toolbar's, and the preview's");

    const size_t before = f.Bar().groups.size();
    f.widget->AddSeparator();
    Check(f.Bar().groups.size() == before, "a toolbar takes no separator: its groups are separated");

    f.widget->SetCatalogueFilter("main", [](const MenuCommand& c) { return c.id == "c"; });
    f.Add("t0.0", "d");
    Check(Join(f.Group(0).commands) == "c", "a catalog filter refuses what the toolbar does not take");
}

void TestMenuEdits() {
    std::printf("Editing a menu\n");
    Fixture f;
    f.Add("m0.0", "c");
    Check(MenuKinds(f.Menu()) == "a,c,-,b" && f.widget->GetSelectedLayoutNode() == "m0.1",
          "Add puts the command after the selected entry");
    f.widget->AddSeparator();
    Check(MenuKinds(f.Menu()) == "a,c,-,-,b", "Separator adds one");
    f.widget->RemoveSelected();
    Check(MenuKinds(f.Menu()) == "a,c,-,b", "Remove takes it out");
    f.widget->SelectLayoutNode("m0.3");
    f.widget->AddGroup();
    Check(MenuKinds(f.Menu()) == "a,c,-,b,[New Submenu]", "New Group makes a submenu");
    Check(f.widget->RenameSelected("More") && f.Menu()[4].label == "More", "which can be named");
    f.Add("m0.4", "d");
    Check(f.Menu()[4].children.size() == 1 && f.Menu()[4].children[0].commandId == "d",
          "a command added on a submenu goes into it");
    f.widget->SelectLayoutNode("m0.0");
    f.widget->MoveSelected(+1);
    Check(MenuKinds(f.Menu()) == "c,a,-,b,[More]", "Move Down moves an entry");
}

void TestPreviewAndResult() {
    std::printf("Preview, Apply and Reset\n");
    Fixture f;
    auto undo = std::dynamic_pointer_cast<UltraCanvasButton>(f.widget->GetPreview()->GetWidget("a"));
    Check(undo && !undo->onClick, "the preview's buttons run nothing");

    CommandLayoutSet applied;
    int applies = 0;
    f.widget->onApply = [&](const CommandLayoutSet& set) { applied = set; ++applies; };
    f.Add("t0.1.0", "d");
    f.Add("m0.2", "c");
    f.Press("Apply");
    Check(applies == 1 && applied.menus.menus.size() == 1 && applied.toolbars.size() == 1 &&
                  Join(applied.FindToolbar("main")->groups[1].commands) == "c,d" &&
                  MenuKinds(applied.menus.menus[0].topLevel) == "a,-,b,c",
          "Apply hands over the menus and the toolbars together");

    int resets = 0;
    f.widget->onReset = [&]() { ++resets; };
    f.widget->SelectLayoutNode("t0.0");
    f.widget->ToggleFoldSelected();
    f.Press("Reset");
    Check(resets == 1 && Join(f.Group(1).commands) == "c" && !f.Group(0).folded &&
                  MenuKinds(f.Menu()) == "a,-,b",
          "Reset puts the menus back as given and the toolbar back to the program's defaults");

    f.widget->ClearToolbars();
    Check(f.widget->GetLayoutSet().toolbars.empty() && !f.Has("toolbars") && !f.widget->GetPreview()->IsVisible(),
          "with no toolbars, no toolbar heading and no preview");
}

}  // namespace

int main() {
    TestTree();
    TestToolbarEdits();
    TestMenuEdits();
    TestPreviewAndResult();
    if (g_failures == 0) {
        std::printf("All command editor checks passed.\n");
        return 0;
    }
    std::printf("%d check(s) FAILED.\n", g_failures);
    return 1;
}
