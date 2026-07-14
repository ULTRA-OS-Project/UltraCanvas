// core/UltraCanvasMenuRegistry.cpp
// Implementation of the id-addressable menu command catalog.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasMenuRegistry.h"
#include <algorithm>

namespace UltraCanvas {

    void UltraCanvasMenuRegistry::Register(const MenuCommand& command) {
        if (command.id.empty()) {
            return; // an id-less command can never be referenced from a layout
        }

        auto it = index.find(command.id);
        if (it != index.end()) {
            // Overwrite in place, preserving position/order.
            *commands[it->second] = command;
        } else {
            index[command.id] = commands.size();
            commands.push_back(std::make_unique<MenuCommand>(command));
        }

        // Track category order on first sighting.
        const std::string& cat = command.category;
        if (std::find(categoryOrder.begin(), categoryOrder.end(), cat) == categoryOrder.end()) {
            categoryOrder.push_back(cat);
        }
    }

    void UltraCanvasMenuRegistry::RegisterAction(const std::string& id, const std::string& category,
                                                 const std::string& label, std::function<void()> onClick) {
        RegisterAction(id, category, label, "", "", std::move(onClick));
    }

    void UltraCanvasMenuRegistry::RegisterAction(const std::string& id, const std::string& category,
                                                 const std::string& label, const std::string& iconPath,
                                                 const std::string& shortcut, std::function<void()> onClick) {
        MenuCommand cmd(id, category, label);
        cmd.iconPath = iconPath;
        cmd.shortcut = shortcut;
        cmd.type = MenuItemType::Action;
        cmd.onClick = std::move(onClick);
        Register(cmd);
    }

    void UltraCanvasMenuRegistry::Clear() {
        commands.clear();
        index.clear();
        categoryOrder.clear();
    }

    bool UltraCanvasMenuRegistry::Contains(const std::string& id) const {
        return index.find(id) != index.end();
    }

    const MenuCommand* UltraCanvasMenuRegistry::Find(const std::string& id) const {
        auto it = index.find(id);
        if (it == index.end()) return nullptr;
        return commands[it->second].get();
    }

    std::vector<const MenuCommand*> UltraCanvasMenuRegistry::InCategory(const std::string& category) const {
        std::vector<const MenuCommand*> out;
        for (const auto& cmd : commands) {
            if (cmd->category == category) out.push_back(cmd.get());
        }
        return out;
    }

    MenuItemData UltraCanvasMenuRegistry::Materialize(const std::string& id) const {
        const MenuCommand* cmd = Find(id);
        if (!cmd) {
            // Unknown id: visible, disabled placeholder so stale layouts are obvious.
            MenuItemData missing;
            missing.type = MenuItemType::Action;
            missing.label = "<" + id + ">";
            missing.enabled = false;
            missing.commandId = id;
            return missing;
        }

        // Dynamic / provider-backed command: return the prototype as-is.
        if (cmd->prototype) {
            MenuItemData item = cmd->prototype();
            if (item.commandId.empty()) item.commandId = id;
            return item;
        }

        MenuItemData item;
        item.type = cmd->type;
        item.label = cmd->label;
        item.iconPath = cmd->iconPath;
        item.shortcut = cmd->shortcut;
        item.commandId = id;
        item.checked = cmd->defaultChecked;
        item.radioGroup = cmd->radioGroup;
        item.onClick = cmd->onClick;
        item.onToggle = cmd->onToggle;
        item.onTextInput = cmd->onTextInput;
        return item;
    }

    UltraCanvasMenuRegistry& UltraCanvasMenuRegistry::Default() {
        static UltraCanvasMenuRegistry instance;
        return instance;
    }

} // namespace UltraCanvas
