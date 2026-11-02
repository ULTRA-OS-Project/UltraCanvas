// core/UltraCanvasToolbarLayout.cpp
// A toolbar as groups of command ids: building the toolbar from one, merging
// the user's changes over the program's defaults, and keeping them in the
// application's settings.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasToolbarLayout.h"

#include <algorithm>
#include <set>

namespace UltraCanvas {

// ===== GROUP AND LAYOUT HELPERS =====

    bool ToolbarLayoutGroup::IsCommandShown(const std::string& commandId) const {
        return std::find(commands.begin(), commands.end(), commandId) != commands.end() &&
               std::find(hidden.begin(), hidden.end(), commandId) == hidden.end();
    }

    std::vector<std::string> ToolbarLayoutGroup::ShownCommands() const {
        std::vector<std::string> shown;
        for (const auto& id : commands) {
            if (std::find(hidden.begin(), hidden.end(), id) == hidden.end()) shown.push_back(id);
        }
        return shown;
    }

    bool ToolbarLayoutGroup::SetCommandShown(const std::string& commandId, bool shown) {
        if (std::find(commands.begin(), commands.end(), commandId) == commands.end()) return false;
        auto it = std::find(hidden.begin(), hidden.end(), commandId);
        if (shown && it != hidden.end()) hidden.erase(it);
        if (!shown && it == hidden.end()) hidden.push_back(commandId);
        return true;
    }

    ToolbarLayoutGroup* ToolbarLayout::FindGroup(const std::string& groupId) {
        for (auto& g : groups) if (g.id == groupId) return &g;
        return nullptr;
    }

    const ToolbarLayoutGroup* ToolbarLayout::FindGroup(const std::string& groupId) const {
        for (const auto& g : groups) if (g.id == groupId) return &g;
        return nullptr;
    }

    bool ToolbarLayout::MoveGroup(const std::string& groupId, int toIndex) {
        const int n = static_cast<int>(groups.size());
        if (toIndex < 0 || toIndex >= n) return false;
        for (int i = 0; i < n; ++i) {
            if (groups[i].id != groupId) continue;
            ToolbarLayoutGroup moved = std::move(groups[i]);
            groups.erase(groups.begin() + i);
            groups.insert(groups.begin() + toIndex, std::move(moved));
            return true;
        }
        return false;
    }

// ===== BUILD THE TOOLBAR =====

    namespace {
        bool IsToggleCommand(const MenuCommand& cmd) {
            return cmd.type == MenuItemType::Checkbox || cmd.type == MenuItemType::Radio;
        }

        // Runs what a pick of the command means: a toggle command is switched
        // on (a tool picked from a folded group), an action runs.
        void RunCommand(const UltraCanvasMenuRegistry& registry, const std::string& id) {
            const MenuCommand* cmd = registry.Find(id);
            if (!cmd) return;
            if (IsToggleCommand(*cmd)) {
                if (cmd->onToggle) cmd->onToggle(true);
            } else if (cmd->onClick) {
                cmd->onClick();
            }
        }
    }

    void MaterializeToolbar(UltraCanvasToolbar& toolbar, const ToolbarLayout& layout,
                            const UltraCanvasMenuRegistry& registry) {
        toolbar.ClearItems();
        toolbar.SetCommandRegistry(&registry);
        ToolbarAppearance look = toolbar.GetAppearance();
        look.iconSize = layout.iconSize;
        look.showIconLabels = layout.showLabels;
        toolbar.SetAppearance(look);
        const int iconPx = look.IconPixelSize().width;
        const UltraCanvasMenuRegistry* reg = &registry;

        std::set<std::string> placed;   // a command shows once, in the first group that has it
        bool firstGroup = true;
        for (const auto& group : layout.groups) {
            if (!group.visible) continue;
            std::vector<const MenuCommand*> cmds;
            for (const auto& id : group.ShownCommands()) {
                const MenuCommand* cmd = registry.Find(id);
                if (cmd && placed.insert(id).second) cmds.push_back(cmd);
            }
            if (cmds.empty()) continue;
            if (!firstGroup) toolbar.AddSeparator("sep." + group.id);
            firstGroup = false;

            // A folded group: one slot with a fold-out bar. A widget cannot
            // be one of its tools, so a group with one stays a row.
            const bool hasWidget = std::any_of(cmds.begin(), cmds.end(),
                                               [](const MenuCommand* c) { return static_cast<bool>(c->makeToolbarWidget); });
            if (group.folded && cmds.size() >= 2 && !hasWidget) {
                std::vector<ToolGroupItem> items;
                bool allToggles = true;
                for (const MenuCommand* cmd : cmds) {
                    items.push_back({ cmd->id, registry.ResolveIcon(*cmd, iconPx), cmd->TooltipText() });
                    if (!IsToggleCommand(*cmd)) allToggles = false;
                }
                auto slot = toolbar.AddToolGroup(group.id, items);
                // A group of tools stays pressed on the one picked; a group of
                // actions is a button, which springs back once it has run.
                slot->onItemSelected = [reg, slotRaw = slot.get(), allToggles](const std::string& id) {
                    RunCommand(*reg, id);
                    if (!allToggles) slotRaw->SetPressed(false);
                };
                continue;
            }

            for (const MenuCommand* cmd : cmds) {
                const std::string id = cmd->id;
                if (cmd->makeToolbarWidget) {
                    if (auto widget = cmd->makeToolbarWidget(id)) toolbar.AddWidget(id, widget);
                    continue;
                }
                const std::string icon = registry.ResolveIcon(*cmd, iconPx);
                // Without an icon the button needs its name to say anything.
                const std::string text = (layout.showLabels || icon.empty()) ? cmd->CurrentLabel() : std::string();
                std::shared_ptr<UltraCanvasButton> button;
                if (IsToggleCommand(*cmd)) {
                    button = toolbar.AddToggleButton(id, text, icon, [reg, id](bool on) {
                        const MenuCommand* c = reg->Find(id);
                        if (c && c->onToggle) c->onToggle(on);
                    });
                } else {
                    button = toolbar.AddButton(id, text, icon, [reg, id]() {
                        const MenuCommand* c = reg->Find(id);
                        if (c && c->onClick) c->onClick();
                    });
                }
                button->SetTooltip(cmd->TooltipText());
            }
        }
        toolbar.RefreshCommandStates();
    }

// ===== MERGE =====

    ToolbarLayout MergeToolbarLayout(const ToolbarLayout& defaults, const ToolbarLayout& saved,
                                     const UltraCanvasMenuRegistry* registry) {
        const auto known = [registry](const std::string& id) { return !registry || registry->Contains(id); };
        const auto keepKnown = [&known](std::vector<std::string>& ids) {
            ids.erase(std::remove_if(ids.begin(), ids.end(), [&known](const std::string& id) { return !known(id); }),
                      ids.end());
        };
        // Hidden ids only of the group's own commands, each once.
        const auto keepHiddenOf = [](ToolbarLayoutGroup& g) {
            std::vector<std::string> kept;
            for (const auto& id : g.hidden) {
                if (std::find(g.commands.begin(), g.commands.end(), id) != g.commands.end() &&
                    std::find(kept.begin(), kept.end(), id) == kept.end()) kept.push_back(id);
            }
            g.hidden = std::move(kept);
        };

        ToolbarLayout result;
        result.name = defaults.name;
        result.iconSize = saved.iconSize;
        result.showLabels = saved.showLabels;

        for (const auto& sg : saved.groups) {
            if (sg.id.empty() || result.FindGroup(sg.id)) continue;
            ToolbarLayoutGroup g;
            if (sg.userMade) {
                g = sg;
            } else if (const ToolbarLayoutGroup* dg = defaults.FindGroup(sg.id); dg && !dg->userMade) {
                // What is in a program's group is the program's; how it shows is the user's.
                g = *dg;
                g.visible = sg.visible;
                g.folded = sg.folded;
                g.hidden = sg.hidden;
            } else {
                continue;   // a group this release no longer has
            }
            keepKnown(g.commands);
            keepHiddenOf(g);
            result.groups.push_back(std::move(g));
        }

        // Groups the saved layout does not know - new in this release - go in
        // after the group they follow in the defaults, or first.
        for (size_t i = 0; i < defaults.groups.size(); ++i) {
            const ToolbarLayoutGroup& dg = defaults.groups[i];
            if (result.FindGroup(dg.id)) continue;
            size_t insertAt = 0;
            for (size_t j = i; j-- > 0;) {
                auto it = std::find_if(result.groups.begin(), result.groups.end(),
                                       [&](const ToolbarLayoutGroup& g) { return g.id == defaults.groups[j].id; });
                if (it != result.groups.end()) { insertAt = static_cast<size_t>(it - result.groups.begin()) + 1; break; }
            }
            ToolbarLayoutGroup g = dg;
            keepKnown(g.commands);
            keepHiddenOf(g);
            result.groups.insert(result.groups.begin() + static_cast<std::ptrdiff_t>(insertAt), std::move(g));
        }
        return result;
    }

// ===== PERSIST =====

    namespace {
        int IconSizeToPixels(ToolbarIconSize size) {
            ToolbarAppearance look;
            look.iconSize = size;
            return look.IconPixelSize().width;
        }

        bool IconSizeFromPixels(int px, ToolbarIconSize& out) {
            switch (px) {
                case 16: out = ToolbarIconSize::Small; return true;
                case 24: out = ToolbarIconSize::Medium; return true;
                case 32: out = ToolbarIconSize::Large; return true;
                case 48: out = ToolbarIconSize::ExtraLarge; return true;
                case 64: out = ToolbarIconSize::Huge; return true;
                default: return false;
            }
        }

        JSONValue StringArray(const std::vector<std::string>& values) {
            JSONValue array = JSONValue::MakeArray();
            for (const auto& v : values) array.Append(JSONValue(v));
            return array;
        }

        std::vector<std::string> StringsOf(const JSONValue& array) {
            std::vector<std::string> out;
            for (const auto& v : array.GetElements()) {
                if (v.IsString() && !v.GetString().empty()) out.push_back(v.GetString());
            }
            return out;
        }
    }

    bool SaveToolbarLayout(UltraCanvasAppSettings& settings, const ToolbarLayout& layout,
                           const ToolbarLayout& defaults) {
        if (layout.name.empty()) return false;
        JSONValue kept = JSONValue::MakeObject();
        if (layout.iconSize != defaults.iconSize) kept.Set("iconSize", IconSizeToPixels(layout.iconSize));
        if (layout.showLabels != defaults.showLabels) kept.Set("labels", layout.showLabels);

        JSONValue groups = JSONValue::MakeArray();
        for (const auto& g : layout.groups) {
            JSONValue entry = JSONValue::MakeObject();
            entry.Set("id", g.id);
            const ToolbarLayoutGroup* dg = defaults.FindGroup(g.id);
            if (g.userMade || !dg) {
                // The user's own group, whole.
                entry.Set("user", true);
                entry.Set("label", g.label);
                if (!g.iconName.empty()) entry.Set("iconName", g.iconName);
                entry.Set("commands", StringArray(g.commands));
                if (!g.hidden.empty()) entry.Set("hidden", StringArray(g.hidden));
                if (!g.visible) entry.Set("visible", false);
                if (g.folded) entry.Set("folded", true);
            } else {
                // A program's group: only how it differs.
                if (g.visible != dg->visible) entry.Set("visible", g.visible);
                if (g.folded != dg->folded) entry.Set("folded", g.folded);
                if (g.hidden != dg->hidden) entry.Set("hidden", StringArray(g.hidden));
            }
            groups.Append(std::move(entry));
        }
        kept.Set("groups", std::move(groups));

        JSONValue& document = settings.Document();
        JSONValue toolbars = document.Get("toolbars").IsObject() ? document.Get("toolbars") : JSONValue::MakeObject();
        toolbars.Set(layout.name, std::move(kept));
        document.Set("toolbars", std::move(toolbars));
        return settings.Save();
    }

    ToolbarLayout LoadToolbarLayout(const UltraCanvasAppSettings& settings, const ToolbarLayout& defaults,
                                    const UltraCanvasMenuRegistry* registry) {
        const JSONValue& kept = settings.Document().Get("toolbars").Get(defaults.name);
        if (!kept.IsObject()) return MergeToolbarLayout(defaults, defaults, registry);

        ToolbarLayout saved;
        saved.name = defaults.name;
        saved.iconSize = defaults.iconSize;
        saved.showLabels = defaults.showLabels;
        if (kept.Get("iconSize").IsNumber()) {
            IconSizeFromPixels(static_cast<int>(kept.Get("iconSize").GetInteger()), saved.iconSize);
        }
        if (kept.Get("labels").IsBoolean()) saved.showLabels = kept.Get("labels").GetBoolean();

        const JSONValue& groups = kept.Get("groups");
        if (!groups.IsArray()) {
            // Only the options were kept: the groups are the defaults'.
            saved.groups = defaults.groups;
        }
        for (const auto& entry : groups.GetElements()) {
            if (!entry.IsObject() || !entry.Get("id").IsString()) continue;
            ToolbarLayoutGroup g;
            g.id = entry.Get("id").GetString();
            g.userMade = entry.Get("user").GetBoolean(false);
            if (const ToolbarLayoutGroup* dg = defaults.FindGroup(g.id); dg && !g.userMade) {
                g = *dg;   // the flags start from the program's
            } else {
                g.label = entry.Get("label").GetString(g.id);
                g.iconName = entry.Get("iconName").GetString();
                g.commands = StringsOf(entry.Get("commands"));
            }
            if (entry.Get("visible").IsBoolean()) g.visible = entry.Get("visible").GetBoolean();
            if (entry.Get("folded").IsBoolean()) g.folded = entry.Get("folded").GetBoolean();
            if (entry.Get("hidden").IsArray()) g.hidden = StringsOf(entry.Get("hidden"));
            saved.groups.push_back(std::move(g));
        }
        return MergeToolbarLayout(defaults, saved, registry);
    }

    bool ForgetToolbarLayout(UltraCanvasAppSettings& settings, const std::string& name) {
        JSONValue& document = settings.Document();
        if (!document.Get("toolbars").IsObject()) return settings.Save();
        JSONValue toolbars = document.Get("toolbars");
        toolbars.Remove(name);
        document.Set("toolbars", std::move(toolbars));
        return settings.Save();
    }

} // namespace UltraCanvas
