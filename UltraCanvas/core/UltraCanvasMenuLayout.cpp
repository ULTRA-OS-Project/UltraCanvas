// core/UltraCanvasMenuLayout.cpp
// Implementation of the serializable, id-based menu layout.
// Version: 1.1.0 - the commands the program's menus held, saved with a layout ([KNOWN]),
//                  and MergeMenuLayoutSet
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasMenuLayout.h"
#include "UltraCanvasUtils.h"   // TrimWhitespace, Split
#include <algorithm>
#include <fstream>
#include <sstream>
#include "UltraCanvasPathUtf8.h"

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
        std::ofstream f(UltraCanvas::PathFromUtf8(filePath), std::ios::out | std::ios::trunc);
        if (!f.is_open()) return false;

        f << "# UltraCanvas menu layout\n";
        f << "# Auto-generated. Ids refer to commands in the menu registry.\n";
        // Before the first menu, where a reader that does not know the
        // section passes over its lines.
        if (!set.knownCommands.empty()) {
            f << "[KNOWN]\n";
            for (const auto& id : set.knownCommands) f << "ID=" << id << "\n";
        }
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
        std::ifstream f(UltraCanvas::PathFromUtf8(filePath));
        if (!f.is_open()) return false;

        out.menus.clear();
        out.knownCommands.clear();
        bool inKnown = false;

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

            if (line == "[KNOWN]") {
                flushNode();
                inKnown = true;
                continue;
            }
            if (line == "[MENU]") {
                inKnown = false;
                flushNode();
                out.menus.push_back(MenuLayout{});
                curMenu = &out.menus.back();
                lastAtDepth.clear();
                continue;
            }
            if (line == "[NODE]") {
                inKnown = false;
                flushNode();
                pending = MenuLayoutNode();
                pendingDepth = 0;
                haveNode = true;
                continue;
            }

            std::string key, value;
            if (!SplitKeyValue(line, key, value)) continue;

            if (inKnown) {
                if (key == "ID" && !value.empty()) out.knownCommands.push_back(value);
                continue;
            }
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

// ===== Merge with a newer program's defaults =====
    namespace {
        void CollectIds(const std::vector<MenuLayoutNode>& nodes, std::vector<std::string>& out) {
            for (const auto& n : nodes) {
                if (n.IsCommand() && !n.commandId.empty() &&
                    std::find(out.begin(), out.end(), n.commandId) == out.end()) out.push_back(n.commandId);
                if (n.IsGroup()) CollectIds(n.children, out);
            }
        }

        bool Has(const std::vector<std::string>& ids, const std::string& id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        // Whether two entries are the same one: a command by its id, a
        // submenu by its label. Separators are never anchors.
        bool SameEntry(const MenuLayoutNode& a, const MenuLayoutNode& b) {
            if (a.kind != b.kind) return false;
            if (a.IsCommand()) return a.commandId == b.commandId;
            if (a.IsGroup()) return a.label == b.label;
            return false;
        }

        struct MergeContext {
            const std::vector<std::string>* known = nullptr;     // what the saved layout's program had
            const std::vector<std::string>* present = nullptr;   // what the saved layout holds anywhere
        };

        bool IsNew(const MenuLayoutNode& n, const MergeContext& ctx) {
            return n.IsCommand() && !n.commandId.empty() && !Has(*ctx.known, n.commandId) &&
                   !Has(*ctx.present, n.commandId);
        }

        // The part of a submenu of the defaults that is new: its new commands
        // and the submenus holding some, none of the rest.
        bool NewPart(const MenuLayoutNode& group, const MergeContext& ctx, MenuLayoutNode& out) {
            out = group;
            out.children.clear();
            for (const auto& c : group.children) {
                if (c.IsCommand() && IsNew(c, ctx)) {
                    out.children.push_back(c);
                } else if (c.IsGroup()) {
                    MenuLayoutNode sub;
                    if (NewPart(c, ctx, sub)) out.children.push_back(sub);
                }
            }
            return !out.children.empty();
        }

        // Brings the new entries of `defaults` into `saved`, list by list.
        void MergeList(std::vector<MenuLayoutNode>& saved, const std::vector<MenuLayoutNode>& defaults,
                       const MergeContext& ctx) {
            for (size_t i = 0; i < defaults.size(); ++i) {
                const MenuLayoutNode& d = defaults[i];
                if (d.IsSeparator()) continue;

                // The same submenu in the saved list: merge into it.
                if (d.IsGroup()) {
                    auto same = std::find_if(saved.begin(), saved.end(),
                                             [&d](const MenuLayoutNode& s) { return SameEntry(s, d); });
                    if (same != saved.end()) {
                        MergeList(same->children, d.children, ctx);
                        continue;
                    }
                }

                MenuLayoutNode adding;
                if (d.IsCommand()) {
                    if (!IsNew(d, ctx)) continue;
                    adding = d;
                } else if (!NewPart(d, ctx, adding)) {
                    continue;   // a submenu with nothing new in it
                }

                // After the entry it follows in the defaults, or first.
                size_t at = 0;
                for (size_t j = i; j-- > 0;) {
                    auto anchor = std::find_if(saved.begin(), saved.end(),
                                               [&](const MenuLayoutNode& s) { return SameEntry(s, defaults[j]); });
                    if (anchor != saved.end()) { at = static_cast<size_t>(anchor - saved.begin()) + 1; break; }
                }
                saved.insert(saved.begin() + static_cast<std::ptrdiff_t>(at), adding);
            }
        }

        void DropUnknown(std::vector<MenuLayoutNode>& nodes, const UltraCanvasCommandRegistry& registry) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&registry](const MenuLayoutNode& n) {
                return n.IsCommand() && !n.commandId.empty() && !registry.Contains(n.commandId);
            }), nodes.end());
            for (auto& n : nodes) if (n.IsGroup()) DropUnknown(n.children, registry);
        }
    } // namespace

    std::vector<std::string> MenuLayoutCommandIds(const MenuLayoutSet& set) {
        std::vector<std::string> ids;
        for (const auto& m : set.menus) CollectIds(m.topLevel, ids);
        return ids;
    }

    MenuLayoutSet MergeMenuLayoutSet(const MenuLayoutSet& defaults, const MenuLayoutSet& saved,
                                     const UltraCanvasCommandRegistry* registry) {
        MenuLayoutSet result = saved;
        const std::vector<std::string> present = MenuLayoutCommandIds(saved);
        // Without a record, what the saved layout holds is what its program had.
        const std::vector<std::string> known = saved.knownCommands.empty() ? present : saved.knownCommands;
        MergeContext ctx;
        ctx.known = &known;
        ctx.present = &present;

        for (const auto& d : defaults.menus) {
            if (MenuLayout* menu = result.Find(d.name)) {
                MergeList(menu->topLevel, d.topLevel, ctx);
            } else {
                result.menus.push_back(d);   // a menu new to this program
            }
        }
        if (registry) {
            for (auto& m : result.menus) DropUnknown(m.topLevel, *registry);
        }
        result.knownCommands = MenuLayoutCommandIds(defaults);
        return result;
    }

    bool SaveMenuLayoutSet(const MenuLayoutSet& set, const std::string& filePath,
                           const MenuLayoutSet& defaults) {
        MenuLayoutSet withKnown = set;
        withKnown.knownCommands = MenuLayoutCommandIds(defaults);
        return SaveMenuLayoutSet(withKnown, filePath);
    }

    bool LoadMenuLayoutSet(MenuLayoutSet& out, const std::string& filePath,
                           const MenuLayoutSet& defaults, const UltraCanvasCommandRegistry* registry) {
        MenuLayoutSet saved;
        if (!LoadMenuLayoutSet(saved, filePath) || saved.menus.empty()) {
            out = defaults;
            out.knownCommands = MenuLayoutCommandIds(defaults);
            return false;
        }
        out = MergeMenuLayoutSet(defaults, saved, registry);
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
        MenuItemData MaterializeNode(const MenuLayoutNode& n, const UltraCanvasCommandRegistry& reg) {
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
                                                const UltraCanvasCommandRegistry& registry) {
        std::vector<MenuItemData> out;
        out.reserve(layout.topLevel.size());
        for (const auto& n : layout.topLevel) {
            out.push_back(MaterializeNode(n, registry));
        }
        return out;
    }

    void MaterializeInto(UltraCanvasMenu& menu, const MenuLayout& layout,
                         const UltraCanvasCommandRegistry& registry) {
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
