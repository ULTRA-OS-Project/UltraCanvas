# Menu Configuration Widget

`UltraCanvasMenuConfigWidget` lets end users customise an application's menus at
runtime: reorder entries, add or remove commands, group them into submenus, and
save the result so it survives a restart. It works together with two supporting
pieces of infrastructure — a **command registry** and a **serializable menu
layout** — that together form the "special way to organise menus" that makes menus
reconfigurable.

## Why the extra infrastructure is needed

Menus are normally built imperatively with lambdas:

```cpp
menuBar = MenuBuilder("Bar")
    .AddSubmenu("File", {
        MenuItemData::ActionWithShortcut("Open...", "Ctrl+O", [this]() { OnOpen(); }),
    })
    .Build();
```

A lambda has no stable identity and cannot be serialized, so a menu built this way
cannot be saved, reloaded, or reorganised by id. The solution is to separate the
two concerns:

| Concern | Type | Serializable? |
|---|---|---|
| *What a command does* (label, icon, shortcut, callback) | `MenuCommand` in `UltraCanvasMenuRegistry` | no (holds callbacks) |
| *How the menu is arranged* (which commands, in what order/nesting) | `MenuLayout` / `MenuLayoutSet` | yes (ids + groups only) |

`MenuItemData` gained one optional field, `std::string commandId`, that links a live
menu item back to its registry entry.

## 1. Register commands

Each action the user may place in a menu is registered once, under a stable id and a
category (the category groups commands in the widget's right-hand tree):

```cpp
auto& reg = UltraCanvasMenuRegistry::Default();
reg.RegisterAction("file.open", "File", "Open...", "icons/open.svg", "Ctrl+O",
                   [this]() { OnOpen(); });

// Non-Action commands keep their proper callback slot:
MenuCommand wrap("view.wordwrap", "View", "Word Wrap");
wrap.type = MenuItemType::Checkbox;
wrap.onToggle = [this](bool on) { SetWordWrap(on); };
reg.Register(wrap);

// Dynamic / provider-backed submenus (e.g. "Recent Files") that cannot be
// decomposed into serializable children use the prototype escape hatch. They are
// treated as an opaque, non-editable leaf by the widget:
MenuCommand recent("file.recent", "File", "Recent Files");
recent.prototype = [this]() { return BuildRecentFilesSubmenu(); };
reg.Register(recent);
```

## 2. Describe the default layout

A `MenuLayout` is a tree of `MenuLayoutNode`s (`Command`, `Group`, or `Separator`).
A `MenuLayoutSet` holds several named menus so a menubar and any context menus can be
edited together under one "Program" root.

```cpp
MenuLayoutSet set;
MenuLayout& bar = set.FindOrAdd("Main Menu Bar");
MenuLayoutNode file = MenuLayoutNode::Group("File");
file.children = {
    MenuLayoutNode::Command("file.new"),
    MenuLayoutNode::Command("file.open"),
    MenuLayoutNode::Separator(),
    MenuLayoutNode::Command("file.recent"),
};
bar.topLevel = { file };
```

You can also derive an editable layout from an existing live menu with
`LayoutFromMenu(menu, "Main Menu Bar")` (note: provider-backed submenus introspect
as empty groups, so building the default layout explicitly is preferred when full
fidelity matters).

## 3. Show the widget and wire the buttons

```cpp
auto widget = CreateMenuConfigWidget("MenuConfig", 0, 0, 900, 560);
widget->SetRegistry(&reg);
widget->SetLayoutSet(set);

widget->onApply = [this](const MenuLayoutSet& s) {
    // Temporary: rebuild the live menu(s) now, lost on exit.
    MaterializeInto(*menuBar, s.menus.front(), *registry);
};
widget->onSave = [this](const MenuLayoutSet& s) {
    MaterializeInto(*menuBar, s.menus.front(), *registry);   // apply
    SaveMenuLayoutSet(s, ConfigDir() + "/menu_layout.txt");  // + persist
};
widget->onReset  = [this]() { /* reload defaults and re-seed the widget */ };
widget->onCancel = [this]() { /* close without applying */ };
```

The widget only edits a working copy and reports intent through callbacks — it never
touches live menus itself, so the host decides how apply/save/reset behave.

### Layout of the widget

```
┌ Menu structure ─┐ ┌─────┐ ┌ Available commands ┐
│ Program         │ │Add →│ │ File               │
│  ├ Main Menu Bar│ │←Rem.│ │   New              │
│  │  ├ File      │ │Up   │ │   Open...          │
│  │  │  ├ New    │ │Down │ │ Edit               │
│  │  │  └ Open...│ │Sub  │ │   Copy             │
│  │  └ Edit      │ │Sep  │ │ ...                │
│  └ Context Menu │ └─────┘ └────────────────────┘
├──────────────────────────────────────────────────┤
│ [Reset]                    [Cancel][Apply][Save]  │
└──────────────────────────────────────────────────┘
```

* **Add →** inserts the command selected on the right into the layout (after the
  selected node, or inside it when a group is selected). Double-clicking a command
  adds it too.
* **← Remove** deletes the selected layout entry (the Program root and whole menus
  are structural and cannot be removed this way).
* **Move Up / Move Down** reorder within the current parent.
* **New Submenu / Separator** insert a group or separator at the selection.

## 4. Apply vs Save

* **Apply** rebuilds the live menu in place (`UltraCanvasMenu::Clear()` +
  `AddItem()` via `MaterializeInto`). The change is temporary and lasts until the
  program closes.
* **Save** applies *and* writes the layout to disk via `SaveMenuLayoutSet`. On the
  next launch, load it with `LoadMenuLayoutSet` and `MaterializeInto` instead of the
  hardcoded builder.
* **Reset** reverts the working copy to the last-seeded baseline and (optionally)
  asks the host to reload its defaults.

## Persistence format

Layouts are stored in a self-contained line/section text format (no JSON
dependency). The caller supplies the path, keeping the core free of any
config-directory policy:

```
[MENU]
NAME=Main Menu Bar
[NODE]
KIND=Group
DEPTH=0
LABEL=File
[NODE]
KIND=Command
DEPTH=1
ID=file.new
```

Only ids are stored; labels, icons and shortcuts are resolved from the registry when
the layout is materialized, so they never drift out of sync. An optional per-node
`LABEL` records a user rename. Unknown ids materialize as a disabled `<id>`
placeholder so a stale layout degrades visibly rather than silently dropping
entries.

## Files

| File | Role |
|---|---|
| `include/UltraCanvasMenuRegistry.h`, `core/UltraCanvasMenuRegistry.cpp` | command catalog |
| `include/UltraCanvasMenuLayout.h`, `core/UltraCanvasMenuLayout.cpp` | layout data, persistence, materialize/introspect |
| `include/UltraCanvasMenuConfigWidget.h`, `core/UltraCanvasMenuConfigWidget.cpp` | the widget |
| `Apps/DemoApp/UltraCanvasMenuConfigExamples.cpp` | runnable example (Demo → Widgets → "Menu Configurator") |
