// core/UltraCanvasMenuLayout.cpp
// Implementation of the serializable, id-based menu layout.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasMenuLayout.h"
#include "UltraCanvasUtils.h"   // TrimWhitespace, Split
#include <fstream>
#include <sstream>

namespace UltraCanvas {

// ===== MenuLayoutSet lookups =====
    MenuLayout* MenuLayoutSet::Find(const std::string& name) {
        for (auto& m : menus) if (m.name == name) return &m;
        return nullptr;
    }
    const MenuLayout* MenuLayoutSet::Find(const std::string& name) const {
        for (const auto& m : menus) if (m.name == name) return &m;
        return nullptr;
    }
    MenuLayout& MenuLayoutSet::FindOrAdd(const std::string& name) {
        if (MenuLayout* existing = Find(name)) return *existing;
        menus.push_back(MenuLayout{name, {}});
        return menus.back();
    }

// ===== Serialization helpers =====
    namespace {
        std::string KindToString(MenuLayoutNode::Kind k) {
            switch (k) {
                case MenuLayoutNode::Kind::Group:     return "Group";
                case MenuLayoutNode::Kind::Separator: return "Separator";
                case MenuLayoutNode::Kind::Command:
                default:                              return "Command";
            }
        }
        MenuLayoutNode::Kind KindFromString(const std::string& s) {
            if (s == "Group") return MenuLayoutNode::Kind::Group;
            if (s == "Separator") return MenuLayoutNode::Kind::Separator;
            return MenuLayoutNode::Kind::Command;
        }

        void WriteNode(std::ofstream& f, const MenuLayoutNode& n, int depth) {
            f << "[NODE]\n";
            f << "KIND=" << KindToString(n.kind) << "\n";
            f << "DEPTH=" << depth << "\n";
            if (n.kind == MenuLayoutNode::Kind::Command) {
                f << "ID=" << n.commandId << "\n";
                if (!n.label.empty()) f << "LABEL=" << n.label << "\n";
            } else if (n.kind == MenuLayoutNode::Kind::Group) {
                f << "LABEL=" << n.label << "\n";
                if (!n.iconPath.empty()) f << "ICON=" << n.iconPath << "\n";
                for (const auto& c : n.children) WriteNode(f, c, depth + 1);
            }
        }

        // Parse "KEY=value" into (key, value); value may itself contain '='.
        bool SplitKeyValue(const std::string& line, std::string& key, std::string& value) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) return false;
            key = TrimWhitespace(line.substr(0, eq));
            value = line.substr(eq + 1); // keep value verbatim (no trailing trim beyond CR)
            if (!value.empty() && value.back() == '\r') value.pop_back();
            return true;
        }
    } // namespace

// ===== Save =====
    bool SaveMenuLayoutSet(const MenuLayoutSet& set, const std::string& filePath) {
        std::ofstream f(filePath, std::ios::out | std::ios::trunc);
        if (!f.is_open()) return false;

        f << "# UltraCanvas menu layout\n";
        f << "# Auto-generated. Ids refer to commands in the menu registry.\n";
        for (const auto& m : set.menus) {
            f << "[MENU]\n";
            f << "NAME=" << m.name << "\n";
            for (const auto& n : m.topLevel) WriteNode(f, n, 0);
        }
        return f.good();
    }

    bool SaveMenuLayout(const MenuLayout& layout, const std::string& filePath) {
        MenuLayoutSet set;
        set.menus.push_back(layout);
        return SaveMenuLayoutSet(set, filePath);
    }

// ===== Load =====
    bool LoadMenuLayoutSet(MenuLayoutSet& out, const std::string& filePath) {
        std::ifstream f(filePath);
        if (!f.is_open()) return false;

        out.menus.clear();

        MenuLayout* curMenu = nullptr;
        // lastAtDepth[d] = pointer to the most recently placed node at depth d in
        // the current menu. Pointers into a vector<MenuLayoutNode> stay valid while
        // we only push into the *current parent's* children (never an ancestor's),
        // which is exactly the traversal order the file guarantees.
        std::vector<MenuLayoutNode*> lastAtDepth;

        bool haveNode = false;
        MenuLayoutNode pending;
        int pendingDepth = 0;

        auto flushNode = [&]() {
            if (!haveNode) return;
            haveNode = false;
            if (!curMenu) return;

            int d = pendingDepth;
            if (d < 1) {
                curMenu->topLevel.push_back(pending);
                lastAtDepth.assign(1, &curMenu->topLevel.back());
            } else {
                if (static_cast<int>(lastAtDepth.size()) < d) {
                    d = static_cast<int>(lastAtDepth.size()); // clamp malformed depth
                }
                if (d < 1) { // still nothing to parent to -> treat as top-level
                    curMenu->topLevel.push_back(pending);
                    lastAtDepth.assign(1, &curMenu->topLevel.back());
                    return;
                }
                MenuLayoutNode* parent = lastAtDepth[d - 1];
                parent->children.push_back(pending);
                lastAtDepth.resize(d);
                lastAtDepth.push_back(&parent->children.back());
            }
        };

        std::string rawLine;
        while (std::getline(f, rawLine)) {
            std::string line = TrimWhitespace(rawLine);
            if (line.empty() || line[0] == '#') continue;

            if (line == "[MENU]") {
                flushNode();
                out.menus.push_back(MenuLayout{});
                curMenu = &out.menus.back();
                lastAtDepth.clear();
                continue;
            }
            if (line == "[NODE]") {
                flushNode();
                pending = MenuLayoutNode();
                pendingDepth = 0;
                haveNode = true;
                continue;
            }

            std::string key, value;
            if (!SplitKeyValue(line, key, value)) continue;

            if (!haveNode) {
                // Menu-level fields.
                if (key == "NAME" && curMenu) curMenu->name = value;
                continue;
            }

            // Node-level fields.
            if (key == "KIND")       pending.kind = KindFromString(value);
            else if (key == "DEPTH") { try { pendingDepth = std::stoi(value); } catch (...) { pendingDepth = 0; } }
            else if (key == "ID")    pending.commandId = value;
            else if (key == "LABEL") pending.label = value;
            else if (key == "ICON")  pending.iconPath = value;
        }
        flushNode();
        return true;
    }

    bool LoadMenuLayout(MenuLayout& out, const std::string& filePath) {
        MenuLayoutSet set;
        if (!LoadMenuLayoutSet(set, filePath)) return false;
        if (set.menus.empty()) return false;
        out = set.menus.front();
        return true;
    }

// ===== Materialize =====
    namespace {
        MenuItemData MaterializeNode(const MenuLayoutNode& n, const UltraCanvasMenuRegistry& reg) {
            switch (n.kind) {
                case MenuLayoutNode::Kind::Separator:
                    return MenuItemData::Separator();
                case MenuLayoutNode::Kind::Group: {
                    MenuItemData item;
                    item.type = MenuItemType::Submenu;
                    item.label = n.label;
                    item.iconPath = n.iconPath;
                    for (const auto& c : n.children) {
                        item.subItems.push_back(MaterializeNode(c, reg));
                    }
                    return item;
                }
                case MenuLayoutNode::Kind::Command:
                default: {
                    MenuItemData item = reg.Materialize(n.commandId);
                    if (!n.label.empty()) item.label = n.label; // user rename override
                    return item;
                }
            }
        }
    } // namespace

    std::vector<MenuItemData> MaterializeLayout(const MenuLayout& layout,
                                                const UltraCanvasMenuRegistry& registry) {
        std::vector<MenuItemData> out;
        out.reserve(layout.topLevel.size());
        for (const auto& n : layout.topLevel) {
            out.push_back(MaterializeNode(n, registry));
        }
        return out;
    }

    void MaterializeInto(UltraCanvasMenu& menu, const MenuLayout& layout,
                         const UltraCanvasMenuRegistry& registry) {
        menu.Clear();
        for (auto& item : MaterializeLayout(layout, registry)) {
            menu.AddItem(item);
        }
    }

// ===== Introspect =====
    namespace {
        MenuLayoutNode NodeFromItem(const MenuItemData& item) {
            if (item.type == MenuItemType::Separator) {
                return MenuLayoutNode::Separator();
            }
            if (item.type == MenuItemType::Submenu) {
                MenuLayoutNode group = MenuLayoutNode::Group(item.label, item.iconPath);
                for (const auto& sub : item.subItems) {
                    group.children.push_back(NodeFromItem(sub));
                }
                return group;
            }
            // Command leaf: prefer the stable id; fall back to the label as a
            // placeholder id so the entry is at least visible/movable.
            if (!item.commandId.empty()) {
                return MenuLayoutNode::Command(item.commandId);
            }
            MenuLayoutNode n = MenuLayoutNode::Command("", item.label);
            return n;
        }
    } // namespace

    MenuLayout LayoutFromMenu(UltraCanvasMenu& menu, const std::string& name) {
        MenuLayout layout;
        layout.name = name;
        for (const auto& item : menu.GetItems()) {
            layout.topLevel.push_back(NodeFromItem(item));
        }
        return layout;
    }

} // namespace UltraCanvas
