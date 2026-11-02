// include/UltraCanvasToolbarLayout.h
// A toolbar described as data: groups of command ids, each group one function
// of the program ("File", "Zoom", "Arrange"), which the user can show, hide,
// reorder and fold, and which is kept across restarts.
//
// A toolbar built with AddButton calls holds lambdas, which have no identity:
// nothing can save it, move a group of it, or hide one of its buttons and get
// it back. The layout is the arrangement only - which groups in which order,
// which of their commands shown, folded into one slot or not - and refers to
// commands by id. UltraCanvasMenuRegistry, which menus are built from, says
// what each command is: label, icon, shortcut, callback, live state. So one
// command, registered once, is the same in the menu, on the toolbar and in a
// shortcut, and MaterializeToolbar turns a layout and a registry into the
// toolbar's buttons.
//
// A group is the unit of separation: the toolbar puts a separator between two
// groups it shows and never inside one, so a hidden group never leaves two
// separators side by side. A command the user turns off stays in its group,
// listed in `hidden`, and comes back in its place.
//
// Saving keeps only what the user changed (SaveToolbarLayout): the order of
// the groups, which are shown, folded or partly hidden, and the groups the
// user made. What is in a group the program defines comes from the program's
// defaults every time, so a command a new release adds to "Zoom" appears for
// everyone who never touched that group, and LoadToolbarLayout merges the two
// - a new group goes in at its default place, an id no longer registered is
// dropped.
//
// doc-check: example
//   UltraCanvas::UltraCanvasMenuRegistry registry;
//   registry.RegisterAction("file.new", "File", "New", "", "Ctrl+N", []() {});
//   registry.RegisterAction("file.open", "File", "Open", "", "Ctrl+O", []() {});
//   registry.RegisterAction("view.zoom-in", "View", "Zoom In", "", "+", []() {});
//   UltraCanvas::ToolbarLayout defaults;
//   defaults.name = "main";
//   defaults.groups = { UltraCanvas::ToolbarLayoutGroup::Make("file", "File", { "file.new", "file.open" }),
//                       UltraCanvas::ToolbarLayoutGroup::Make("zoom", "Zoom", { "view.zoom-in" }) };
//   UltraCanvas::UltraCanvasAppSettings settings("MyApp");
//   settings.Load();
//   UltraCanvas::ToolbarLayout layout = UltraCanvas::LoadToolbarLayout(settings, defaults, &registry);
//   auto toolbar = std::make_shared<UltraCanvas::UltraCanvasToolbar>("main", 0, 0, 0, 38);
//   UltraCanvas::MaterializeToolbar(*toolbar, layout, registry);
//   toolbar->RefreshCommandStates();               // after anything that changes a command's state
//   layout.FindGroup("zoom")->visible = false;      // the user hid a group
//   UltraCanvas::MaterializeToolbar(*toolbar, layout, registry);
//   UltraCanvas::SaveToolbarLayout(settings, layout, defaults);
//
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasAppSettings.h"
#include "UltraCanvasMenuRegistry.h"
#include "UltraCanvasToolbar.h"

#include <string>
#include <vector>

namespace UltraCanvas {

// ===== ONE GROUP =====
    struct ToolbarLayoutGroup {
        std::string id;                      // "zoom": stable, chosen by the program
        std::string label;                   // "Zoom": the editor's name for it
        std::string iconName;                // the slot's icon when the group is folded
        std::vector<std::string> commands;   // command ids in order (the group's full set)
        std::vector<std::string> hidden;     // the commands of `commands` the user turned off
        bool visible = true;                 // the whole group on or off
        bool folded = false;                 // one UltraCanvasToolGroupButton slot instead of a row
        bool userMade = false;               // made by the user; may be renamed and deleted

        static ToolbarLayoutGroup Make(const std::string& id, const std::string& label,
                                       const std::vector<std::string>& commands) {
            ToolbarLayoutGroup group;
            group.id = id;
            group.label = label;
            group.commands = commands;
            return group;
        }
        bool IsCommandShown(const std::string& commandId) const;
        // `commands` less `hidden`, in order.
        std::vector<std::string> ShownCommands() const;
        // Hide or show one of the group's commands; false when it is not one.
        bool SetCommandShown(const std::string& commandId, bool shown);
    };

// ===== ONE TOOLBAR =====
    struct ToolbarLayout {
        std::string name;                         // "ac-main": one per toolbar, the settings key
        std::vector<ToolbarLayoutGroup> groups;   // in display order
        ToolbarIconSize iconSize = ToolbarIconSize::Medium;
        bool showLabels = false;

        ToolbarLayoutGroup* FindGroup(const std::string& groupId);
        const ToolbarLayoutGroup* FindGroup(const std::string& groupId) const;
        // Moves the group `groupId` to `toIndex` among the groups; false when
        // there is no such group or index.
        bool MoveGroup(const std::string& groupId, int toIndex);
    };

// ===== BUILD THE TOOLBAR =====
    // Rebuilds `toolbar` from `layout`: ClearItems, the layout's icon size and
    // labels, then for each shown group with something to show, a separator
    // after the previous group and its commands - one button each (a toggle
    // for a Checkbox or Radio command, the command's makeToolbarWidget for a
    // widget command), or one UltraCanvasToolGroupButton slot for a folded
    // group of two or more. Each item's id is its command id, so
    // GetWidget("edit.undo") finds the Undo button while it is shown; a folded
    // group's slot has the group's id. A command the registry does not know
    // is left out. The toolbar keeps a pointer to `registry`
    // (SetCommandRegistry), which must outlive it, and its state is read in
    // at the end (RefreshCommandStates).
    void MaterializeToolbar(UltraCanvasToolbar& toolbar, const ToolbarLayout& layout,
                            const UltraCanvasMenuRegistry& registry);

// ===== MERGE AND PERSIST =====
    // The defaults with the user's changes `saved` applied: the saved order of
    // the groups, their visibility, folding and hidden commands, and the
    // user's own groups whole. A program group missing from `saved` (new in
    // this release) goes in after the group it follows in the defaults; a
    // program group no longer in the defaults is dropped; a hidden id that is
    // not in its group is dropped. With `registry`, commands it does not know
    // are dropped from every group.
    ToolbarLayout MergeToolbarLayout(const ToolbarLayout& defaults, const ToolbarLayout& saved,
                                     const UltraCanvasMenuRegistry* registry = nullptr);

    // Writes what `layout` changes from `defaults` under
    // "toolbars.<layout.name>" in `settings` and saves the file; false when
    // it cannot be written.
    bool SaveToolbarLayout(UltraCanvasAppSettings& settings, const ToolbarLayout& layout,
                           const ToolbarLayout& defaults);
    // The layout kept under "toolbars.<defaults.name>" merged over the
    // defaults (MergeToolbarLayout); the defaults when nothing is kept.
    // `settings` must have been loaded.
    ToolbarLayout LoadToolbarLayout(const UltraCanvasAppSettings& settings, const ToolbarLayout& defaults,
                                    const UltraCanvasMenuRegistry* registry = nullptr);
    // Drops the kept layout of the toolbar `name` and saves the file: the
    // program's defaults from then on (an editor's Reset).
    bool ForgetToolbarLayout(UltraCanvasAppSettings& settings, const std::string& name);

} // namespace UltraCanvas
