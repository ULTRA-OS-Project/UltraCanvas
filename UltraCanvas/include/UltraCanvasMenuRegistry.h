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
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasMenu.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace UltraCanvas {

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
//        std::function<void(const std::string&)> onTextInput; // Input

        bool defaultChecked = false; // initial state for Checkbox / Radio
        int  radioGroup = 0;         // group id for Radio items

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

        // ===== MATERIALIZE =====
        // Build one live menu item from a command id. If the id is unknown, returns
        // a disabled placeholder item labelled "<id>" so a stale layout degrades
        // visibly rather than silently dropping the entry.
        MenuItemData Materialize(const std::string& id) const;

        // ===== PROCESS-WIDE DEFAULT =====
        // Most apps register into and read from this shared instance.
        static UltraCanvasMenuRegistry& Default();

    private:
        std::vector<std::unique_ptr<MenuCommand>> commands; // registration order, stable addresses
        std::unordered_map<std::string, size_t> index;      // id -> position in commands
        std::vector<std::string> categoryOrder;             // first-seen category order
    };

} // namespace UltraCanvas
