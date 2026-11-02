// core/UltraCanvasMenuRegistry.cpp
// Implementation of the id-addressable menu command catalog.
// Version: 1.1.0 - icons resolved by size, live state, shortcut conflicts, RefreshMenuStates
// Version: 1.0.1 - the commented-out Input callback copy is gone
// Author: UltraCanvas Framework

#include "UltraCanvasMenuRegistry.h"
#include <algorithm>
#include <cctype>
#include <map>

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
        item.label = cmd->CurrentLabel();
        item.iconPath = ResolveIcon(*cmd, 16);
        item.shortcut = cmd->shortcut;
        item.commandId = id;
        item.enabled = cmd->IsEnabledNow();
        item.checked = cmd->IsCheckedNow();
        item.radioGroup = cmd->radioGroup;
        item.onClick = cmd->onClick;
        item.onToggle = cmd->onToggle;
        return item;
    }

    std::string UltraCanvasMenuRegistry::ResolveIcon(const MenuCommand& command, int pixelSize) const {
        if (!command.iconName.empty() && iconResolver) {
            std::string file = iconResolver(command.iconName, pixelSize);
            if (!file.empty()) return file;
        }
        return command.iconPath;
    }

    namespace {
        // "shift+ctrl+s" and "Ctrl+Shift+S" alike: the modifiers in a fixed
        // order, then the key, all in capitals.
        std::string NormalizeShortcut(const std::string& shortcut) {
            std::vector<std::string> parts;
            std::string part;
            for (char ch : shortcut) {
                if (ch == '+' && !part.empty()) { parts.push_back(part); part.clear(); continue; }
                if (ch != ' ') part += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            if (!part.empty()) parts.push_back(part);   // "Ctrl++" ends on its key "+"
            if (parts.empty()) return "";
            std::string key = parts.back();
            parts.pop_back();
            std::sort(parts.begin(), parts.end());
            std::string out;
            for (const auto& p : parts) out += p + "+";
            return out + key;
        }
    }

    std::vector<UltraCanvasMenuRegistry::ShortcutConflict> UltraCanvasMenuRegistry::FindShortcutConflicts() const {
        std::vector<ShortcutConflict> conflicts;
        std::map<std::string, size_t> byShortcut;   // normalized -> index in `conflicts`
        for (const auto& cmd : commands) {
            const std::string key = NormalizeShortcut(cmd->shortcut);
            if (key.empty()) continue;
            auto found = byShortcut.find(key);
            if (found == byShortcut.end()) {
                byShortcut[key] = conflicts.size();
                conflicts.push_back({ cmd->shortcut, { cmd->id } });
            } else {
                conflicts[found->second].commandIds.push_back(cmd->id);
            }
        }
        conflicts.erase(std::remove_if(conflicts.begin(), conflicts.end(),
                                       [](const ShortcutConflict& c) { return c.commandIds.size() < 2; }),
                        conflicts.end());
        return conflicts;
    }

    namespace {
        // True when a label changed: the menu's width may have.
        bool RefreshItems(std::vector<MenuItemData>& items, const UltraCanvasMenuRegistry& registry) {
            bool relabelled = false;
            for (auto& item : items) {
                if (!item.subItems.empty() && RefreshItems(item.subItems, registry)) relabelled = true;
                if (item.commandId.empty()) continue;
                const MenuCommand* cmd = registry.Find(item.commandId);
                if (!cmd || cmd->prototype) continue;
                item.enabled = cmd->IsEnabledNow();
                if (cmd->isChecked) item.checked = cmd->isChecked();
                // Only a label that moves: a fixed one may carry the user's rename.
                if (cmd->dynamicLabel) {
                    std::string label = cmd->dynamicLabel();
                    if (label != item.label) { item.label = std::move(label); relabelled = true; }
                }
            }
            return relabelled;
        }
    }

    void UltraCanvasMenuRegistry::RefreshMenuStates(UltraCanvasMenu& menu) const {
        if (RefreshItems(menu.GetItems(), *this)) menu.InvalidateLayout();
        menu.RequestRedraw();
    }

    UltraCanvasMenuRegistry& UltraCanvasMenuRegistry::Default() {
        static UltraCanvasMenuRegistry instance;
        return instance;
    }

} // namespace UltraCanvas
