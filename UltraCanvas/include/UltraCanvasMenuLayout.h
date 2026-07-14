// include/UltraCanvasMenuLayout.h
// Serializable, id-based description of a menu's structure.
//
// A MenuLayout is pure data: a tree of command ids and submenu groups. It carries
// no callbacks, so it can be saved to disk and reloaded. The registry
// (UltraCanvasMenuRegistry) turns a layout back into live MenuItemData by resolving
// each command id to its registered action. This is the "special way to organise
// menus" that makes menus reconfigurable at runtime and persistable across restarts.
//
// Persistence uses a self-contained line/section text format (no JSON dependency),
// mirroring the style used by the applications' own config files. The caller
// supplies the file path so this module stays free of any config-directory policy.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasMenu.h"
#include "UltraCanvasMenuRegistry.h"
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== ONE NODE IN A LAYOUT TREE =====
    struct MenuLayoutNode {
        enum class Kind { Command, Group, Separator };

        Kind kind = Kind::Command;
        std::string commandId;   // Kind::Command — id into the registry
        std::string label;       // Kind::Group label; optional rename for a Command
        std::string iconPath;    // optional Kind::Group icon
        std::vector<MenuLayoutNode> children; // Kind::Group children

        MenuLayoutNode() = default;

        static MenuLayoutNode Command(const std::string& id, const std::string& labelOverride = "") {
            MenuLayoutNode n;
            n.kind = Kind::Command;
            n.commandId = id;
            n.label = labelOverride;
            return n;
        }
        static MenuLayoutNode Group(const std::string& label, const std::string& iconPath = "") {
            MenuLayoutNode n;
            n.kind = Kind::Group;
            n.label = label;
            n.iconPath = iconPath;
            return n;
        }
        static MenuLayoutNode Separator() {
            MenuLayoutNode n;
            n.kind = Kind::Separator;
            return n;
        }

        bool IsGroup() const { return kind == Kind::Group; }
        bool IsCommand() const { return kind == Kind::Command; }
        bool IsSeparator() const { return kind == Kind::Separator; }
    };

// ===== ONE NAMED MENU =====
    struct MenuLayout {
        std::string name;                     // logical menu name, e.g. "MainMenuBar"
        std::vector<MenuLayoutNode> topLevel; // top-level entries (menubar submenus, etc.)
    };

// ===== A COLLECTION OF NAMED MENUS =====
// The configuration widget shows every menu in the set under a single "Program"
// root, so a menubar and any number of context menus can be edited together and
// saved to one file.
    struct MenuLayoutSet {
        std::vector<MenuLayout> menus;

        MenuLayout* Find(const std::string& name);
        const MenuLayout* Find(const std::string& name) const;
        MenuLayout& FindOrAdd(const std::string& name);
    };

// ===== PERSISTENCE (line/section text format) =====
    bool SaveMenuLayoutSet(const MenuLayoutSet& set, const std::string& filePath);
    bool LoadMenuLayoutSet(MenuLayoutSet& out, const std::string& filePath);

    // Single-layout convenience wrappers (one menu per file).
    bool SaveMenuLayout(const MenuLayout& layout, const std::string& filePath);
    bool LoadMenuLayout(MenuLayout& out, const std::string& filePath);

// ===== MATERIALIZE (layout -> live menu items) =====
    std::vector<MenuItemData> MaterializeLayout(const MenuLayout& layout,
                                                const UltraCanvasMenuRegistry& registry);

    // Rebuild a live menu in place from a layout: Clear() + AddItem() for each
    // top-level entry. Works for a persistent menubar or a freshly-created popup.
    void MaterializeInto(UltraCanvasMenu& menu, const MenuLayout& layout,
                         const UltraCanvasMenuRegistry& registry);

// ===== INTROSPECT (live menu -> layout) =====
// Build an editable layout from an existing menu. Command leaves are captured by
// their commandId (empty id => captured by label as an unresolvable placeholder).
// Provider-backed dynamic submenus introspect as empty groups, since their
// children only exist at open time; prefer building the default MenuLayout
// explicitly when full fidelity matters.
    MenuLayout LayoutFromMenu(UltraCanvasMenu& menu, const std::string& name);

} // namespace UltraCanvas
