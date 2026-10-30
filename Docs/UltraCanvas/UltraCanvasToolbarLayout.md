# Toolbar Layout — toolbars built from commands

`UltraCanvasToolbarLayout.h` describes a toolbar as data: **groups of command
ids**, each group one function of the program (*File*, *Zoom*, *Arrange*).
The user can show, hide, reorder and fold the groups and turn single
commands off, and the arrangement is kept in the application's settings
across restarts and releases. `MaterializeToolbar` turns a layout and a
command registry into the toolbar's buttons.

It is phase 1 of the toolbar configuration in
[`UltraCanvasToolbarConfigInvestigation.md`](UltraCanvasToolbarConfigInvestigation.md);
the editor (phase 2) is built on it.

## Contents

- [Why a layout](#why-a-layout)
- [1. Register the commands](#1-register-the-commands)
  - [Icons by size](#icons-by-size)
  - [Live state](#live-state)
  - [Widgets and shortcuts](#widgets-and-shortcuts)
- [2. Describe the default groups](#2-describe-the-default-groups)
- [3. Build the toolbar](#3-build-the-toolbar)
- [4. Keep the user's arrangement](#4-keep-the-users-arrangement)
- [Files](#files)

## Why a layout

A toolbar built with `AddButton(..., [this]() { ... })` holds lambdas, which
have no identity: nothing can save it, move one of its groups, or hide a
button and bring it back. A program also declares each command twice or
three times - as a menu entry, a toolbar button, a case in its shortcut
handler - and the copies drift apart (ArtCreator's menu once showed a
shortcut its keyboard handler gave to another command).

The layout separates the two:

| Concern | Type | Saved? |
|---|---|---|
| What a command is: label, icon, shortcut, callback, live state | `MenuCommand` in `UltraCanvasMenuRegistry` | no (holds callbacks) |
| How a toolbar is arranged: groups, order, what is shown | `ToolbarLayout` / `ToolbarLayoutGroup` | yes (ids only) |

The registry is the one menus are built from
([Menu Configuration Widget](UltraCanvasMenuConfigWidget.md)), so a command
registered once is the same in the menu, on the toolbar and in the shortcut
list.

## 1. Register the commands

<!-- doc-check: void CmdUndo(); bool CanUndo(); std::string UndoStepName(); bool gridShown; std::string IconFolder(); -->

```cpp
UltraCanvasMenuRegistry registry;   // one per window: the callbacks reach that window

MenuCommand undo("edit.undo", "Edit", "Undo");
undo.iconName = "undo.svg";          // the file name; the resolver picks the size
undo.shortcut = "Ctrl+Z";
undo.onClick = [this]() { CmdUndo(); };
undo.isEnabled = [this]() { return CanUndo(); };
undo.dynamicLabel = [this]() { return "Undo " + UndoStepName(); };
registry.Register(undo);

MenuCommand grid("view.grid", "View", "Grid");
grid.type = MenuItemType::Checkbox;  // a toggle button on a toolbar, a check in a menu
grid.iconName = "grid.svg";
grid.onToggle = [this](bool on) { gridShown = on; };
grid.isChecked = [this]() { return gridShown; };
registry.Register(grid);
```

### Icons by size

A line icon is sharp only at the pixel grid it was drawn for, so a program
keeps one folder per size (16 px for menus, 24 for toolbars...). A command
names its icon by file name, and the registry's resolver picks the file for
the size it is drawn at:

```cpp
registry.SetIconResolver([this](const std::string& name, int px) {
    return IconFolder() + std::to_string(px) + "/" + name;
});
std::string toolbarIcon = registry.ResolveIcon(*registry.Find("edit.undo"), 24);
```

Without a resolver, or for a command with only `iconPath`, the path is used
as it is. `Materialize` gives a menu entry the 16 px icon.

### Live state

`isEnabled`, `isChecked` and `dynamicLabel` are read whenever a menu or
toolbar built from the command is refreshed, so the program never keeps
pointers to the buttons - which the user may have removed:

```cpp
// After an edit, a selection, an undo:
// toolbar->RefreshCommandStates();
// registry.RefreshMenuStates(*menuBar);
```

`MenuCommand::TooltipText()` is the toolbar's tooltip: `tooltip` when set,
else the current label and `" (shortcut)"` - *Undo Move (Ctrl+Z)*.

### Widgets and shortcuts

A command that is a widget on the toolbar rather than a button - a slider,
a field - makes it:

```cpp
MenuCommand quality("view.quality", "View", "Quality");
quality.makeToolbarWidget = [](const std::string& id) -> std::shared_ptr<UltraCanvasUIElement> {
    return std::make_shared<UltraCanvasSlider>(id, 0, 0, 90, 24);
};
registry.Register(quality);
```

`FindShortcutConflicts()` lists the shortcuts given to more than one
command, with their ids; `"Ctrl+Shift+S"` and `"shift+ctrl+s"` are one
shortcut. A program can check it once at start in a debug build.

## 2. Describe the default groups

```cpp
ToolbarLayout defaults;
defaults.name = "main";   // the settings key: one per toolbar
defaults.groups = {
    ToolbarLayoutGroup::Make("file",    "File",    { "file.new", "file.open", "file.save" }),
    ToolbarLayoutGroup::Make("history", "History", { "edit.undo", "edit.redo" }),
    ToolbarLayoutGroup::Make("view",    "View",    { "view.grid", "view.quality" }),
};
ToolbarLayoutGroup align = ToolbarLayoutGroup::Make("align", "Align", { "arrange.align-left", "arrange.align-right" });
align.visible = false;   // offered, off until the user turns it on
align.folded = true;     // one slot with a fold-out bar
defaults.groups.push_back(align);
```

A group's fields: `id`, `label`, `iconName` (the slot's icon when folded),
`commands` (its full set, in order), `hidden` (those the user turned off),
`visible`, `folded`, `userMade`. `SetCommandShown`, `IsCommandShown` and
`ShownCommands` work on `hidden`; a hidden command stays in its group and
comes back in its place. `ToolbarLayout::FindGroup` and `MoveGroup` find and
reorder groups.

## 3. Build the toolbar

<!-- doc-check: UltraCanvasMenuRegistry registry; ToolbarLayout layout; -->

```cpp
auto toolbar = std::make_shared<UltraCanvasToolbar>("main", 0, 0, 0, 38);
MaterializeToolbar(*toolbar, layout, registry);
```

`MaterializeToolbar` clears the toolbar and, for each shown group with
something to show, adds a separator after the previous group and the group's
commands:

- an Action command is a button, a Checkbox or Radio command a toggle button,
  a command with `makeToolbarWidget` its widget;
- a folded group of two or more is one `UltraCanvasToolGroupButton` slot
  under the group's id: a group of toggle commands (tools) stays pressed on
  the one picked, a group of actions springs back once it has run;
- each item's id is its command id, so `toolbar->GetWidget("edit.undo")`
  finds the Undo button while it is shown;
- a command the registry does not know, a hidden command and a hidden group
  leave nothing behind - no gap, no second separator - and a command in two
  groups shows in the first.

The layout's `iconSize` and `showLabels` become the toolbar's appearance. The
toolbar keeps a pointer to the registry (`SetCommandRegistry`), which must
outlive it, and reads the commands' state at the end; call
`RefreshCommandStates()` again wherever the state changes.

## 4. Keep the user's arrangement

<!-- doc-check: UltraCanvasMenuRegistry registry; ToolbarLayout defaults; -->

```cpp
UltraCanvasAppSettings settings("MyApp");   // <settings folder>/MyApp/settings.json
settings.Load();
ToolbarLayout layout = LoadToolbarLayout(settings, defaults, &registry);

// ... the user hides a group and turns a command off ...
layout.FindGroup("history")->visible = false;
layout.FindGroup("file")->SetCommandShown("file.open", false);
SaveToolbarLayout(settings, layout, defaults);

// Reset: the program's defaults from now on.
ForgetToolbarLayout(settings, "main");
```

Only what the user changed is kept, under `toolbars.<name>`:

```json
{ "toolbars": { "main": {
    "iconSize": 16,
    "groups": [
      { "id": "history", "visible": false },
      { "id": "file", "hidden": ["file.open"] },
      { "id": "view" },
      { "id": "mine", "user": true, "label": "My commands", "commands": ["file.save"] }
] } } }
```

What is in a group the program defines comes from the program's defaults
every time, so `LoadToolbarLayout` (`MergeToolbarLayout`) can bring a saved
layout forward to a new release:

- a command the release adds to a group shows, and the user's hidden ones
  stay hidden;
- a group new in the release goes in after the group it follows in the
  defaults;
- a group the release no longer has is dropped, and with the registry given,
  so is every command it does not know - in the user's own groups too.

## Files

| File | Contents |
|---|---|
| `UltraCanvas/include/UltraCanvasToolbarLayout.h` | `ToolbarLayoutGroup`, `ToolbarLayout`, `MaterializeToolbar`, `MergeToolbarLayout`, `SaveToolbarLayout`, `LoadToolbarLayout`, `ForgetToolbarLayout` |
| `UltraCanvas/include/UltraCanvasMenuRegistry.h` | `MenuCommand` (with `iconName`, `tooltip`, `isEnabled`, `isChecked`, `dynamicLabel`, `makeToolbarWidget`), `SetIconResolver`, `ResolveIcon`, `FindShortcutConflicts`, `RefreshMenuStates` |
| `UltraCanvas/include/UltraCanvasToolbar.h` | `AddWidget`, `SetCommandRegistry`, `RefreshCommandStates` |
| `Tests/ToolbarLayoutTest.cpp` | the registry, building, refreshing, saving and merging |
