// Tests/MenuLayoutMergeTest.cpp
// A menu layout the user arranged, brought forward to a newer program.
//
// A saved menu layout used to be loaded as it was: a command a later release
// added to its menus never appeared for a user who had customised them. Saved
// with the program's defaults, a layout now records the commands those
// defaults held ([KNOWN]); merged with a newer program's defaults
// (MergeMenuLayoutSet), it gains each command added since - at its place,
// after the entry it follows, in the submenu of the same label, a new submenu
// or menu whole - while a command the user removed stays removed and one the
// user moved is not added twice.
//
// Headless; the file goes to the temp folder.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasCommandRegistry.h"
#include "UltraCanvasMenuLayout.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

MenuLayoutNode C(const char* id) { return MenuLayoutNode::Command(id); }
MenuLayoutNode Sep() { return MenuLayoutNode::Separator(); }
MenuLayoutNode G(const char* label, std::vector<MenuLayoutNode> children) {
    MenuLayoutNode g = MenuLayoutNode::Group(label);
    g.children = std::move(children);
    return g;
}

// "open,new,-,save" - a submenu as "Label[...]".
std::string Show(const std::vector<MenuLayoutNode>& nodes) {
    std::string out;
    for (const auto& n : nodes) {
        if (!out.empty()) out += ",";
        if (n.IsSeparator()) out += "-";
        else if (n.IsGroup()) out += n.label + "[" + Show(n.children) + "]";
        else out += n.commandId;
    }
    return out;
}

std::string MenuOf(const MenuLayoutSet& set, const std::string& name) {
    const MenuLayout* m = set.Find(name);
    return m ? Show(m->topLevel) : "<none>";
}

MenuLayoutSet Release1() {
    MenuLayoutSet set;
    set.FindOrAdd("Bar").topLevel = {
        G("File", { C("new"), C("open"), Sep(), C("save") }),
        G("Edit", { C("undo"), C("redo") }),
    };
    return set;
}

MenuLayoutSet Release2() {
    MenuLayoutSet set;
    set.FindOrAdd("Bar").topLevel = {
        G("File", { C("new"), C("new-window"), C("open"), Sep(), C("save"), C("save-as"), C("export") }),
        G("Edit", { C("undo"), C("redo"), Sep(), C("cut"),
                    G("Select", { C("select-all"), C("select-none") }) }),
        G("Help", { C("about") }),
    };
    set.FindOrAdd("Context").topLevel = { C("cut"), C("about") };
    return set;
}

// The user's arrangement of release 1: File reordered, Redo removed.
MenuLayoutSet UsersArrangement() {
    MenuLayoutSet set;
    set.FindOrAdd("Bar").topLevel = {
        G("File", { C("open"), C("new"), Sep(), C("save") }),
        G("Edit", { C("undo") }),
    };
    set.knownCommands = MenuLayoutCommandIds(Release1());
    return set;
}

void TestMerge() {
    std::printf("A newer program's commands come into the user's menus\n");
    const MenuLayoutSet merged = MergeMenuLayoutSet(Release2(), UsersArrangement());
    Check(MenuOf(merged, "Bar") ==
                  "File[open,new,new-window,-,save,save-as,export],Edit[undo,cut,Select[select-all,select-none]],Help[about]",
          "each new command after the one it follows, a new submenu and a new top menu whole (got " +
                  MenuOf(merged, "Bar") + ")");
    Check(MenuOf(merged, "Context") == "cut,about", "a menu the user never had comes in whole");
    Check(merged.knownCommands == MenuLayoutCommandIds(Release2()), "and the result knows this release's commands");

    // The user already has Export, in Edit (a command of a plug-in, say):
    // release 2 bringing it to File does not put it there a second time.
    MenuLayoutSet moved = UsersArrangement();
    moved.FindOrAdd("Bar").topLevel[1].children.push_back(C("export"));
    const std::string bar = MenuOf(MergeMenuLayoutSet(Release2(), moved), "Bar");
    size_t exports = 0;
    for (size_t at = bar.find("export"); at != std::string::npos; at = bar.find("export", at + 1)) ++exports;
    Check(exports == 1 && bar.find("save-as,export") == std::string::npos,
          "a command the user already has elsewhere is not added again (got " + bar + ")");

    // Merging what was merged changes nothing: nothing is new any more.
    Check(MenuOf(MergeMenuLayoutSet(Release2(), merged), "Bar") == MenuOf(merged, "Bar"),
          "merging again adds nothing");

    // A file saved without the record: what it holds is what its program had.
    MenuLayoutSet unrecorded = UsersArrangement();
    unrecorded.knownCommands.clear();
    Check(MenuOf(MergeMenuLayoutSet(Release2(), unrecorded), "Bar").find("undo,redo") != std::string::npos,
          "without a record of the program's commands, a removed command comes back once");

    UltraCanvasCommandRegistry registry;
    for (const char* id : { "new", "new-window", "save", "save-as", "export", "undo", "cut", "select-all",
                            "select-none", "about" }) {
        registry.RegisterAction(id, "Test", id, []() {});
    }
    Check(MenuOf(MergeMenuLayoutSet(Release2(), UsersArrangement(), &registry), "Bar").find("open") == std::string::npos,
          "with a registry, a command it does not know is dropped");
}

void TestFile() {
    std::printf("Saved with the program's defaults, loaded with the next ones\n");
    const std::string file = PathToUtf8(std::filesystem::temp_directory_path() / "ultracanvas-menu-merge-test.txt");
    MenuLayoutSet users = UsersArrangement();
    users.knownCommands.clear();
    Check(SaveMenuLayoutSet(users, file, Release1()), "the user's menus are saved with release 1's commands");

    std::ifstream in(PathFromUtf8(file));
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Check(text.find("[KNOWN]") != std::string::npos && text.find("[KNOWN]") < text.find("[MENU]"),
          "the record is written before the first menu, where an older reader passes over it");

    MenuLayoutSet raw;
    Check(LoadMenuLayoutSet(raw, file) && Show(raw.menus[0].topLevel) == "File[open,new,-,save],Edit[undo]" &&
                  raw.knownCommands == MenuLayoutCommandIds(Release1()),
          "read back as saved, with the record");

    MenuLayoutSet merged;
    Check(LoadMenuLayoutSet(merged, file, Release2()) && MenuOf(merged, "Bar").find("new-window") != std::string::npos &&
                  MenuOf(merged, "Bar").find("redo") == std::string::npos,
          "loaded with release 2's defaults: new commands in, the removed one still out");

    MenuLayoutSet none;
    Check(!LoadMenuLayoutSet(none, file + ".missing", Release2()) && MenuOf(none, "Bar") == MenuOf(Release2(), "Bar"),
          "no file: the defaults");
    std::error_code ec;
    std::filesystem::remove(PathFromUtf8(file), ec);
}

}  // namespace

int main() {
    TestMerge();
    TestFile();
    if (g_failures == 0) {
        std::printf("All menu merge checks passed.\n");
        return 0;
    }
    std::printf("%d check(s) FAILED.\n", g_failures);
    return 1;
}
