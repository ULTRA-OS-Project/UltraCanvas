// include/UltraCanvasMenuRegistry.h
// Catalog of stable, id-addressable menu commands that a menu can be built from.
//
// The menu system builds items from [this]-capturing lambdas with no stable
// identity, which cannot be serialized or reorganized. The registry solves that:
// an application registers every available action once, under a stable string id
// and a category. A persisted MenuLayout (see UltraCanvasMenuLayout.h) then refers
// to those ids, and the registry "materializes" each id back into a live
// MenuItemData (resolving the label/icon/shortcut and the correct callback).
//
// The same catalog builds toolbars (UltraCanvasToolbarLayout.h): a command
// names its icon by file name (iconName) and the registry's icon resolver
// picks the file for the size it is drawn at - 16 px in a menu, 24 in a
// toolbar - and its state is read live (isEnabled, isChecked, dynamicLabel)
// whenever the menu or toolbar built from it is refreshed.
//
// Version: 1.1.0 - toolbars too: iconName + SetIconResolver / ResolveIcon, tooltip,
//                  live state (isEnabled, isChecked, dynamicLabel), makeToolbarWidget,
//                  FindShortcutConflicts; Materialize reads the live state
// Version: 1.0.1 - the commented-out Input callback slot is gone (MenuItemType::Input is reserved, unimplemented)
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasMenu.h"
#include <memory>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace UltraCanvas {

    class UltraCanvasUIElement;

// ===== A SINGLE REGISTERED COMMAND =====
// One entry in the catalog. `type` selects which callback slot is used when the
// command is materialized into a live menu item.
    struct MenuCommand {
        std::string id;              // stable identifier, e.g. "file.open"
        std::string category = "General"; // grouping in the catalog tree
        std::string label;           // display text
        std::string iconPath;        // optional icon
        std::string shortcut;        // optional shortcut text, e.g. "Ctrl+O"
        MenuItemType type = MenuItemType::Action;

        // Exactly one of these is used, chosen by `type`:
        std::function<void()> onClick;                    // Action / Submenu-less
        std::function<void(bool)> onToggle;               // Checkbox / Radio

        bool defaultChecked = false; // initial state for Checkbox / Radio
        int  radioGroup = 0;         // group id for Radio items

        // ----- Toolbars and live state -----
        // The icon by file name ("to-front.svg"), resolved per size by the
        // registry's icon resolver (SetIconResolver). Wins over iconPath when
        // set and a resolver is installed.
        std::string iconName;
        // A toolbar button's tooltip; empty: the label, then " (shortcut)".
        std::string tooltip;
        // Live state, read whenever a menu or toolbar built from the command
        // is refreshed. Empty: always enabled / defaultChecked / label.
        std::function<bool()> isEnabled;
        std::function<bool()> isChecked;
        std::function<std::string()> dynamicLabel;   // "Undo Move"
        // A command that is a widget on a toolbar rather than a button (a
        // slider, a field): makes it, given the item id it is added under.
        std::function<std::shared_ptr<UltraCanvasUIElement>(const std::string& itemId)> makeToolbarWidget;

        // The state now, through the functions above when they are set.
        std::string CurrentLabel() const { return dynamicLabel ? dynamicLabel() : label; }
        bool IsEnabledNow() const { return isEnabled ? isEnabled() : true; }
        bool IsCheckedNow() const { return isChecked ? isChecked() : defaultChecked; }
        // The tooltip, or the current label and " (shortcut)".
        std::string TooltipText() const {
            if (!tooltip.empty()) return tooltip;
            const std::string text = CurrentLabel();
            return shortcut.empty() ? text : text + " (" + shortcut + ")";
        }

        // Escape hatch for dynamic / provider-backed submenus (e.g. "Recent Files")
        // whose children are generated at open time and cannot be serialized. When
        // set, Materialize() returns prototype() verbatim and the command is treated
        // as an opaque, non-decomposable leaf by the configuration widget.
        std::function<MenuItemData()> prototype;

        MenuCommand() = default;
        MenuCommand(std::string id_, std::string category_, std::string label_)
                : id(std::move(id_)), category(std::move(category_)), label(std::move(label_)) {}
    };

// ===== THE REGISTRY =====
    class UltraCanvasMenuRegistry {
    public:
        // ===== REGISTRATION =====
        // Registering an id that already exists overwrites the previous command.
        void Register(const MenuCommand& command);

        // Convenience overloads for the common Action case.
        void RegisterAction(const std::string& id, const std::string& category,
                            const std::string& label, std::function<void()> onClick);
        void RegisterAction(const std::string& id, const std::string& category,
                            const std::string& label, const std::string& iconPath,
                            const std::string& shortcut, std::function<void()> onClick);

        void Clear();
        bool Contains(const std::string& id) const;
        size_t Size() const { return commands.size(); }

        // ===== LOOKUP =====
        // Returned pointers are stable for the registry's lifetime (nodes are
        // heap-allocated), but become invalid after Clear().
        const MenuCommand* Find(const std::string& id) const;

        // Categories in first-registered order.
        std::vector<std::string> Categories() const { return categoryOrder; }

        // Commands within a category, in registration order.
        std::vector<const MenuCommand*> InCategory(const std::string& category) const;

        // ===== ICONS =====
        // Turns a command's iconName and a pixel size into a file; set once
        // per program. ArtCreator, say, maps 16 to its 16/ folder, 24 to its
        // main one, 28 to 28/. The size is the one the icon is drawn at, as a
        // line icon is sharp only on the grid it was drawn for.
        using IconResolver = std::function<std::string(const std::string& iconName, int pixelSize)>;
        void SetIconResolver(IconResolver resolver) { iconResolver = std::move(resolver); }
        // The command's icon at `pixelSize`: the resolver's file for its
        // iconName, else its iconPath, else "".
        std::string ResolveIcon(const MenuCommand& command, int pixelSize) const;

        // ===== SHORTCUTS =====
        // The shortcuts given to more than one command, each with the ids that
        // share it, in registration order. "Ctrl+Shift+S" and "shift+ctrl+s"
        // are one shortcut.
        struct ShortcutConflict {
            std::string shortcut;              // as the first command spells it
            std::vector<std::string> commandIds;
        };
        std::vector<ShortcutConflict> FindShortcutConflicts() const;

        // ===== MATERIALIZE =====
        // Build one live menu item from a command id. If the id is unknown, returns
        // a disabled placeholder item labelled "<id>" so a stale layout degrades
        // visibly rather than silently dropping the entry. The item has the
        // command's state as it is now (enabled, checked, label) and its icon
        // at the menu's 16 px.
        MenuItemData Materialize(const std::string& id) const;
        // Re-reads every command's state into `menu`'s entries that came
        // from this registry (by MenuItemData::commandId), submenus included:
        // enabled, checked and - for a command with a dynamicLabel - the
        // label. A program calls it where its state changes, as it does a
        // toolbar's RefreshCommandStates.
        void RefreshMenuStates(UltraCanvasMenu& menu) const;

        // ===== PROCESS-WIDE DEFAULT =====
        // Most apps register into and read from this shared instance.
        static UltraCanvasMenuRegistry& Default();

    private:
        std::vector<std::unique_ptr<MenuCommand>> commands; // registration order, stable addresses
        std::unordered_map<std::string, size_t> index;      // id -> position in commands
        std::vector<std::string> categoryOrder;             // first-seen category order
        IconResolver iconResolver;
    };

} // namespace UltraCanvas
