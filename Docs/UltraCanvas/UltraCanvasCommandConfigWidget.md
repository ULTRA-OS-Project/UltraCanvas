# Menus and Toolbars Editor

<!-- doc-check: void OnOpen(); void SetWordWrap(bool on); MenuItemData BuildRecentFilesSubmenu(); std::string ConfigDir(); std::shared_ptr<UltraCanvasMenu> menuBar; std::shared_ptr<UltraCanvasToolbar> toolbar; UltraCanvasCommandRegistry* registry; UltraCanvasAppSettings settings{"MyApp"}; ToolbarLayout toolbarDefaults; ToolbarLayout toolbarLayout; std::shared_ptr<UltraCanvasWindowBase> window; -->

`UltraCanvasCommandConfigWidget` lets end users customise an application's menus
**and toolbars** at runtime, in one editor: reorder entries, add or remove
commands, group them into submenus or toolbar groups, show or hide a toolbar's
groups and commands, fold a group into one button, and save the result so it
survives a restart. It works together with two supporting pieces of
infrastructure — a **command registry** and **serializable layouts** for menus
and toolbars — that together make menus and toolbars reconfigurable.

It was `UltraCanvasMenuConfigWidget`, which edited menus only; that name, its
header and `CreateMenuConfigWidget` remain as aliases. Its `onApply` / `onSave`
now carry a `CommandLayoutSet`, whose `menus` is the `MenuLayoutSet` they carried
before.

## Contents

- [Why the extra infrastructure is needed](#why-the-extra-infrastructure-is-needed)
- [1. Register commands](#1-register-commands)
- [2. Describe the default layout](#2-describe-the-default-layout)
- [3. Show the widget and wire the buttons](#3-show-the-widget-and-wire-the-buttons)
  - [In a window](#in-a-window)
  - [Layout of the widget](#layout-of-the-widget)
- [4. Apply vs Save](#4-apply-vs-save)
- [Persistence format](#persistence-format)
- [Files](#files)

## Why the extra infrastructure is needed

Menus are normally built imperatively with lambdas:

```cpp
menuBar = MenuBuilder("Bar", 0, 0, 800, 28)
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
| *What a command does* (label, icon, shortcut, callback) | `MenuCommand` in `UltraCanvasCommandRegistry` | no (holds callbacks) |
| *How the menu is arranged* (which commands, in what order/nesting) | `MenuLayout` / `MenuLayoutSet` | yes (ids + groups only) |
| *How a toolbar is arranged* (groups of commands, shown or not) | `ToolbarLayout` ([Toolbar Layout](UltraCanvasToolbarLayout.md)) | yes (ids + groups only) |

The registry was `UltraCanvasMenuRegistry`; that name is an alias
(`UltraCanvasMenuRegistry.h`). A program with several windows gives each window its
own registry, so every command reaches the window it belongs to.

`MenuItemData` gained one optional field, `std::string commandId`, that links a live
menu item back to its registry entry.

## 1. Register commands

Each action the user may place in a menu is registered once, under a stable id and a
category (the category groups commands in the widget's right-hand tree):

```cpp
auto& reg = UltraCanvasCommandRegistry::Default();
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

The same registry builds toolbars ([Toolbar Layout](UltraCanvasToolbarLayout.md)),
and a command carries what a toolbar needs as well: `iconName`, resolved per
size by `SetIconResolver` / `ResolveIcon`; a `tooltip`; live state -
`isEnabled`, `isChecked`, `dynamicLabel` - which `Materialize` reads into the
entry it builds and `RefreshMenuStates(menu)` reads again into a menu built
earlier; and `makeToolbarWidget` for a command that is a widget on a toolbar.
`FindShortcutConflicts()` lists the shortcuts given to more than one command.

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
auto widget = CreateCommandConfigWidget("Customise", 0, 0, 980, 640);
widget->SetRegistry(registry);
widget->SetLayoutSet(set);                                        // the menus
widget->AddToolbar(toolbarLayout, toolbarDefaults, "Main toolbar");   // as it is, and the program's

widget->onApply = [this](const CommandLayoutSet& s) {
    // Temporary: rebuild the live menu and toolbar now, lost on exit.
    MaterializeInto(*menuBar, s.menus.menus.front(), *registry);
    if (const ToolbarLayout* bar = s.FindToolbar("main")) MaterializeToolbar(*toolbar, *bar, *registry);
};
widget->onSave = [this](const CommandLayoutSet& s) {
    MaterializeInto(*menuBar, s.menus.menus.front(), *registry);           // apply
    SaveMenuLayoutSet(s.menus, ConfigDir() + "/menu_layout.txt");          // + persist
    if (const ToolbarLayout* bar = s.FindToolbar("main")) {
        MaterializeToolbar(*toolbar, *bar, *registry);
        SaveToolbarLayout(settings, *bar, toolbarDefaults);
    }
};
widget->onReset  = [this]() { /* the menus are back as given, the toolbars as the program has them */ };
widget->onCancel = [this]() { /* close without applying */ };
```

The widget only edits a working copy and reports intent through callbacks — it never
touches live menus or toolbars itself, so the host decides how apply/save/reset behave.

### In a window

`ShowCommandConfigDialog` opens the widget in a dialog window of its own over the
program's window. Cancel and Save close it after the widget's callbacks have run;
Apply leaves it open. The window keeps itself alive until it is closed.

```cpp
window = ShowCommandConfigDialog(menuBar->GetWindow(), widget, "Customise Menus and Toolbars");
```

### Layout of the widget

```
┌ Menus and toolbars ─────┐ ┌──────────┐ ┌ Available commands ┐
│ Program                 │ │ Add ›    │ │ [Filter commands ] │
│  ├ Menus                │ │ ‹ Remove │ │ File               │
│  │  └ Main Menu Bar     │ │ Move Up  │ │   New              │
│  │     ├ File ...       │ │ Move Down│ │   Open...          │
│  └ Toolbars             │ │ New Group│ │ Edit               │
│     └ Main toolbar      │ │ Separator│ │   Undo             │
│        ├ ☑ File         │ │ Fold     │ │ ...                │
│        │  ├ ☑ New       │ └──────────┘ └────────────────────┘
│        │  └ ☐ Export    │
│        └ ☑ Zoom - folded│
│ Name [File      ]       │
│ Icon size [24 px▾] ☐ Labels
├──────────────────────────────────────────────────────────────┤
│ Preview: Main toolbar   [🗋][📂] │ [↶][↷] │ [🔍▾]               │
│ [Reset]                                  [Cancel][Apply][Save] │
└──────────────────────────────────────────────────────────────┘
```

* **Add** (a chevron pointing right) inserts the command selected on the right after
  the selected entry: into a selected submenu or toolbar group, or a toolbar's last
  group. Double-clicking a command adds it too. On a toolbar, a command already in
  the group is shown again rather than added twice, and a catalog filter
  (`SetCatalogueFilter`) can refuse commands a toolbar does not take.
* **Remove** (a chevron pointing left) deletes the selected menu entry. On a toolbar,
  a command or group of the program's is hidden (unchecked) — it is the program's
  to drop — and one the user added goes. The Program root, the headings and whole
  menus and toolbars are structural and cannot be removed.
* **Move Up / Move Down** reorder within the current parent: menu entries, a
  toolbar's groups, a group's commands.
* **New Group** inserts a submenu in a menu, or a group of the user's on a toolbar,
  after the selected group. **Name** renames the selected menu entry or submenu, or a
  group of the user's (a program's group keeps its name).
* **Separator** inserts a menu separator; toolbars separate their groups themselves.
* **Fold** folds the selected toolbar group into one button whose tools fold out, or
  unfolds it.
* The **check boxes** of a toolbar's groups and commands show or hide them; a hidden
  group keeps which of its commands are shown. **Icon size** and **Labels** are the
  selected toolbar's.
* The **preview** shows the selected toolbar as the edits leave it; its buttons run
  nothing.

Every action is a public method too — `AddSelectedCommand`, `RemoveSelected`,
`MoveSelected`, `AddGroup`, `AddSeparator`, `ToggleFoldSelected`, `RenameSelected`,
`SetLayoutNodeChecked`, `SetSelectedToolbarIconSize`, `SetSelectedToolbarShowLabels`
— with `SelectLayoutNode` and `SelectCatalogueCommand` to choose what they act on.

## 4. Apply vs Save

* **Apply** rebuilds the live menu in place (`UltraCanvasMenu::Clear()` +
  `AddItem()` via `MaterializeInto`). The change is temporary and lasts until the
  program closes.
* **Save** applies *and* writes the layout to disk via `SaveMenuLayoutSet`. On the
  next launch, load it with `LoadMenuLayoutSet` and `MaterializeInto` instead of the
  hardcoded builder.
* **Reset** reverts the menus to the layout last given (`SetLayoutSet`) and each
  toolbar to the program's defaults given with it (`AddToolbar`), then calls
  `onReset`. A toolbar's kept arrangement is dropped with `ForgetToolbarLayout`.

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
| `include/UltraCanvasCommandRegistry.h`, `core/UltraCanvasCommandRegistry.cpp` | command catalog |
| `include/UltraCanvasMenuLayout.h`, `core/UltraCanvasMenuLayout.cpp` | layout data, persistence, materialize/introspect |
| `include/UltraCanvasToolbarLayout.h`, `core/UltraCanvasToolbarLayout.cpp` | toolbar layouts, persistence, the toolbar's context menu |
| `include/UltraCanvasCommandConfigWidget.h`, `core/UltraCanvasCommandConfigWidget.cpp` | the widget, `ShowCommandConfigDialog` |
| `include/UltraCanvasMenuConfigWidget.h`, `include/UltraCanvasMenuRegistry.h` | the former names, as aliases |
| `Apps/DemoApp/UltraCanvasMenuConfigExamples.cpp` | runnable example (Demo → Widgets → "Menus and Toolbars") that customises a model of the `UltraCanvasFilerWidget`'s file context menu and toolbar |
| `Tests/CommandConfigWidgetTest.cpp` | the editor's edits, preview, Apply and Reset |
