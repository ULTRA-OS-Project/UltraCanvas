# A Toolbar Configuration Widget — Investigation

Status: **Investigation — phase 1 built.** The command catalogue, the
group model, building a toolbar from it and keeping the user's arrangement
(§4.1–§4.5, §6 phase 1) exist now and are documented in
[`UltraCanvasToolbarLayout.md`](UltraCanvasToolbarLayout.md). The editor
(§4.4) and the changes on the bar itself (§4.6) are phase 2 and are not
built; their names are recorded in `scripts/doc_examples_baseline.txt`
until they are.

**Since written (2026-10-30):** three of the gaps it found are fixed —
`groupSpacing` is read (the gap across a separator), `ToolbarOverflowMode::Menu`
works (what does not fit goes into a chevron's menu), and tree rows can be
dragged (`UltraCanvasTreeView::SetNodeDragEnabled`, `onNodeDragDrop`) — and
ArtCreator's *Convert to Editable Shapes* is Ctrl+Shift+C, so Ctrl+Shift+S is
Save As in the menu too. The text below is kept as measured; the notes in
§2.1, §2.3, §3.2, §7 and §8 say what changed.

The question asked was: *what would it take for UltraCanvas to offer a
toolbar configuration widget, so that every program can let its users
choose and arrange the icons on its toolbars, with the icons in groups that
each cover one function? Use ArtCreator as the first example.*

The answer has four parts:

1. **Half of it is already built, for menus.** `UltraCanvasMenuRegistry`
   (a catalogue of commands under stable ids), `MenuLayout` (a list of ids
   that can be saved) and `UltraCanvasMenuConfigWidget` (the editor) exist
   and work. No application uses them yet; only a DemoApp page does. A
   toolbar needs the same three parts, and two of them can be shared. §2.
2. **The toolbar has no idea of a group.** `UltraCanvasToolbar` holds a flat
   row of child widgets. A "group" today is whatever lies between two
   separators, so nothing can hide, move or name one.
   `ToolbarAppearance::groupSpacing` was declared and never read (now
   fixed). §2.1.
3. **ArtCreator declares each command three times and the copies have
   drifted apart.** It declares each one as a menu item, again as a
   toolbar button and again as a case in the shortcut switch. For example,
   the menu shows Ctrl+Shift+S for *Convert to Editable Shapes*, but
   pressing Ctrl+Shift+S runs *Save As*. A shared command catalogue fixes
   this as a side effect. §3.
4. **The new parts are small and can be named.** They are a group-aware
   toolbar layout, live enabled and checked state for each command, icons
   resolved by size, a context-menu hook on the toolbar, and the editor
   itself. §4 designs them, §5 sizes them and §6 orders them into phases.

Everything below was checked against the tree at framework `0.9.417` /
ArtCreator `0.8.6` (2026-10-30), not assumed.

Author: UltraCanvas Framework
Last Modified: 2026-10-30

---

## 1. What "toolbar configuration" has to mean

Programs mean several different things by a customisable toolbar. These
are the ones this design covers and the ones it leaves out:

| Capability | In scope | Notes |
|---|---|---|
| Show or hide a **whole group** ("Zoom", "Arrange") | **Yes** | The main request. Done from a right-click on the toolbar and from the editor. |
| Show or hide **one icon** within a group | **Yes** | A group can be partly shown. |
| **Reorder groups**, and icons within a group | **Yes** | In the editor, and by dragging on the bar itself (§4.6). |
| **Add** a command that is in no group by default (Align Left, Cut, Snap to Grid) | **Yes** | It comes from the catalogue of every command the program has registered. |
| **Make a group of your own** and name it | **Yes** | For example, "My shortcuts" with Export, Snap to Pixels and Clip View. |
| **Fold a group into one slot** with a fold-out bar | **Yes** | `UltraCanvasToolGroupButton` already does the folding. This adds a per-group switch for it. |
| Icon size, and showing a label under each icon | **Yes** | These are already fields of `ToolbarAppearance`. The editor exposes them and saves them. |
| **Reset** to the program's defaults | **Yes** | Per toolbar. |
| Save the layout so it **survives a restart** | **Yes** | Through `UltraCanvasAppSettings`, with one key per toolbar (§4.5). |
| Move a toolbar to another edge, or float it | No | `ToolbarPosition` exists, but docking is a window-layout feature, not toolbar content. Treat it separately. |
| Redefine **keyboard shortcuts** | No, but prepared | The command catalogue (§4.1) is what a shortcut editor would need. It belongs in a separate investigation. |
| Configure the small icon bars inside ArtCreator's panels (Fill, Shadow, Bevel ...) | No | They are each tied to one panel's tool and are not a toolbar of the program. |

## 2. What exists today

### 2.1 `UltraCanvasToolbar` (1.10.0)

`UltraCanvasToolbar` (`UltraCanvasToolbar.h`, 574 lines; `.cpp`, 1,171
lines) is a flex container. Its items are real child widgets, created by
`AddButton`, `AddToggleButton`, `AddToolGroup`, `AddSeparator`, `AddLabel`
and the other `Add*` calls. What a configuration feature can build on:

| Already there | Use for configuration |
|---|---|
| Items have string ids. `GetWidget(id)`, `GetItemIndex(id)`, `GetItemOrder()` | A saved layout can refer to items by id. |
| `RemoveItem(id)`, `ClearItems()`, `MoveItem(from, to)` | The toolbar can be rebuilt from a layout. |
| `EnableItemReordering(true)` and `onItemReordered` | The bar can already be rearranged by dragging, one item at a time. |
| `AddToolGroup(id, items, onSelect)` puts several tools in one slot, with a fold-out bar | Gives the "fold a group into one slot" mode for free. |
| `ToolbarOverflowMode::Scroll` | A bar the user has filled up stays usable. |
| `ToolbarAppearance::iconSize` / `IconPixelSize()` / `showIconLabels` | The icon-size and label options. |

What it lacks:

- **There is no group.** A group exists only as a separator in the child
  list. `ToolbarAppearance::groupSpacing` is set by `ToolbarAppearance::Ribbon()`,
  but nothing in `core/` or `Apps/` read it. *(Fixed since: it is the gap
  across a separator now.)* The Ribbon example in
  `UltraCanvasToolbarExamples.md` builds its labelled groups by hand from
  `AddLabel` + `AddSpacer(20)`.
- **There is no way to save it.** `GetItemOrder()` returns the ids, but no
  call turns ids back into buttons. Only the program's own lambdas can do
  that, and a lambda has no id.
- **There is no hook for a right-click.** `OnEvent` handles only
  `UCMouseButton::Left`, at `UltraCanvasToolbar.cpp:633`, `:767` and `:855`.
  A program that wants a "Customise toolbar" context menu has nowhere to
  attach one.
- **There is no state binding.** The program enables, disables and
  re-labels its buttons by keeping a `shared_ptr` to each one (ArtCreator's
  `undoButton` / `redoButton`, `ArtCreatorWindow.cpp:1388-1396`). Once the
  user can remove a button, that pointer can refer to a button that is no
  longer on the bar.

### 2.2 The menu configuration stack (built, unused)

| Piece | File | What it does |
|---|---|---|
| `MenuCommand` / `UltraCanvasMenuRegistry` | `UltraCanvasMenuRegistry.h` (98 lines) | A catalogue of commands. Each command has an id (`"file.open"`), a category, a label, an icon path, shortcut text, a type (Action / Checkbox / Radio), callbacks and a `prototype` escape hatch for dynamic submenus. `Default()` is the shared instance for the whole process. |
| `MenuLayout` / `MenuLayoutSet` | `UltraCanvasMenuLayout.h` (105 lines) | A tree of command ids, groups and separators, made of plain data. It is saved in a line/section text format and turned back into live menu items by `MaterializeLayout` / `MaterializeInto`. |
| `UltraCanvasMenuConfigWidget` | `UltraCanvasMenuConfigWidget.h` (117 lines), `.cpp` (420 lines) | Two `UltraCanvasTreeView`s in an `UltraCanvasSplitPane`, holding the layout and the catalogue. Add / Remove / Up / Down / Separator / Submenu buttons; `onApply`, `onSave`, `onReset` and `onCancel` callbacks. |

The only code that uses it is `Apps/DemoApp/UltraCanvasMenuConfigExamples.cpp`.
No application registers commands. That is a risk, because the API has
never been tested in a real program. It is also an opportunity:
ArtCreator can adopt the registry for its menu and its toolbar in the same
change, and so become the first real user of both.

The registry is close to what a toolbar needs, but not quite:

| A toolbar button needs | `MenuCommand` has |
|---|---|
| An icon at **24 px** (toolbar) or **28 px** (palette) | One `iconPath`. ArtCreator's menu icons are in `artcreator/16/`. |
| A **tooltip** ("Undo Move (Ctrl+Z)") | A `label` and separate `shortcut` text. A tooltip can be built from these two. |
| **Enabled now?** (Undo when there is no history) | No such field. `MenuItemData::enabled` is a fixed value. |
| **Checked now?** (Snap to Grid as a toggle button) | `defaultChecked`, the state at creation time only. |
| A **non-button item** (ArtCreator's Quality slider) | `prototype` makes a `MenuItemData`, not a widget. |

### 2.3 Other pieces the design reuses

- **`UltraCanvasAppSettings`** (`UltraCanvasAppSettings.h`) — one JSON file
  per application, `<settings folder>/<App>/settings.json`, with typed
  values and dotted keys. Its header calls it "the one place" for an
  application's preferences. A toolbar layout is one value in it (§4.5).
- **`UltraCanvasTreeView`** — draws optional checkboxes with a `Mixed`
  state (`SetShowCheckboxes`, `onNodeCheckChanged`). That is the right
  control for "this group is partly shown". Its `onNodeDragDrop` callback
  was declared but never called. *(Fixed since: `SetNodeDragEnabled(true)`
  lets a row be dragged onto another, so the editor can offer dragging.)*
- **`UltraCanvasToolGroupButton`** — a slot showing one tool, with a fold-out
  bar holding the others. This is how a folded group is drawn.
- **`UltraCanvasMenu::ShowPopupAt`** with `MenuItemData::Checkbox` — the
  right-click menu that lists the groups.

### 2.4 How the applications build toolbars

Nine applications construct an `UltraCanvasToolbar`. Every one of them
hard-codes the sequence, as `AddButton(...)` calls with separators between
them:

| Application | `AddButton` / `AddToggleButton` / `AddToolGroup` calls | Configurable today |
|---|---|---|
| DemoApp | 81 | — |
| Texter | 31 | Whole toolbars on or off (*Main Toolbar*, *Markdown Toolbar* in the View menu) |
| UltraPaint | 19 | — |
| ArtCreator | 17 | — |
| UltraDesktop, UltraPress | 3 each | UltraDesktop's taskbar reorders by dragging |
| UltraMail, UltraVideo, UltraCanvasStart | 0 (items added as children) | UltraMail's account bar reorders by dragging |

The ArtCreator, UltraPaint and Texter toolbars use the same groups —
*File*, *History*, *Zoom* — written out three times. After ArtCreator,
those three are the next customers.

## 3. ArtCreator, measured

### 3.1 Its toolbars

**The main toolbar** (`ArtCreatorWindow::BuildToolbar`,
`ArtCreatorWindow.cpp:545-600`) has 17 buttons and widgets in six groups,
divided by five separators:

| Group (proposed id) | Items today | Commands that fit the group but live only in the menu |
|---|---|---|
| `file` | New, Open, Save | New Window, Save As, Export, Document Setup |
| `history` | Undo, Redo | — |
| `zoom` | Zoom out, Zoom in, Fit page, Actual size | Fit Drawing, Fit Selection |
| `arrange` | Group, Ungroup, Bring to front, Send to back | Bring Forward, Send Backward |
| `mirror` | Mirror horizontally, Mirror vertically | — |
| `quality` | The "Quality" caption, a slider and a value label | — |

**The tool palette** (`ArtCreatorWindow::BuildToolPalette`, `:942-1010`)
is a vertical toolbar of eight `UltraCanvasToolGroupButton` slots, defined
in `PaletteGroups()` (`:921-934`): *select*, *draw*, *shapes*, *text*,
*fill*, *effects*, *combine*, *view*. These 19 tools already come in named
groups. The groups exist in data but cannot be edited.

**The menu** (`BuildMenuBar`, `:404-540`) holds about 75 more commands.
They form obvious groups that are not on any toolbar today:

| Candidate group | Commands |
|---|---|
| `edit` | Cut, Copy, Paste, Duplicate, Delete, Select All, Select None |
| `align` | Left / Horizontal centre / Right / Top / Vertical centre / Bottom edges, Centre on page |
| `distribute` | Horizontal centres, Vertical centres, Equal horizontal gaps, Equal vertical gaps |
| `combine` | Add, Subtract, Intersect, Slice, Apply ClipView, Remove container |
| `layer` | New, Delete, Move up, Move down, Show, Hide |
| `line` | No Fill, No Line, the line widths |
| `guides` | Rulers, Grid, Guides (checkboxes), Clear Guides |
| `snap` | To Grid / Guides / Objects / Page (checkboxes), Snap to Pixels |

### 3.2 Each command declared three times

Take *Bring to Front*:

```
ArtCreatorWindow.cpp:441   M::ActionWithShortcut("Bring to Front", "Ctrl+F", MenuIconPath("to-front.svg"), [this]() { CmdReorder(ZOrderMove::ToFront); })
ArtCreatorWindow.cpp:569   toolbar->AddButton("ac-tb-front", "", IconPath("to-front.svg"), [this]() { CmdReorder(ZOrderMove::ToFront); })->SetTooltip("Bring to front (Ctrl+F)");
ArtCreatorWindow.cpp:1197  case UCKeys::F: CmdReorder(e.shift ? ZOrderMove::Forward : ZOrderMove::ToFront); return true;
```

The label, the shortcut and the action are each written three times, and
nothing keeps the copies in step. They have already drifted apart:

- **A shortcut that does something else.** The Arrange menu shows
  *Convert to Editable Shapes — Ctrl+Shift+S* (`:478`). `InstallShortcuts`
  (`:1185`) sends Ctrl+Shift+S to *Save As*. The menu therefore advertises
  a shortcut that runs a different command. *(Fixed since: Convert to
  Editable Shapes is Ctrl+Shift+C.)*
- **Two icons for one command.** The menu's *Open* uses `artcreator/16/open.svg`.
  The toolbar's uses the shared `menu/folder.svg`.
- **Names that differ.** The menu says *Fit Page* and *Actual Size*. The
  tooltips say *Fit page* and *Actual size*.
- **State updated on one copy only.** `UpdateUndoButtons` (`:1388`) disables
  the toolbar's Undo button and puts the step's name in its tooltip. The
  menu's Undo item is always enabled and always reads *Undo*.

With one catalogue entry per command, each of these facts is written once.
The menu, the toolbar, the right-click menu and the shortcut dispatcher all
read the same entry.

### 3.3 Icons by size

ArtCreator keeps its icons in three folders, one per size, because a line
icon is sharp only at the pixel grid it was drawn for (see `IconPixelSize()`):

| Folder | Size | Used by | Files |
|---|---|---|---|
| `media/icons/artcreator/16/` | 16 px | menus | 78 |
| `media/icons/artcreator/` | 24 px | main toolbar, panel bars | 61 |
| `media/icons/artcreator/28/` | 28 px | tool palette | 19 |
| `media/icons/menu/` | 24 px | shared set (New, Open, Save, Undo, Zoom) | 59 |

Of the 75 distinct icons the menu uses, **25 have no 24 px file under the
same name** in either 24 px folder. They include `rulers`, `grid`'s
neighbours `guides` and `snap-*`, the `line-*` widths, `layer-show` /
`layer-hide`, `export`, `save-as` and `snap-pixels`. These are exactly the
commands a user is most likely to add to a toolbar. A command must
therefore name its icon **by file name** and leave the size to the place
that draws it (§4.1), and the 25 missing 24 px icons are drawing work for
the ArtCreator phase (§6, phase 3).

## 4. Design

The design has six parts. Each part names the existing type it extends.

```
 UltraCanvasCommandRegistry ──(ids)──▶ ToolbarLayout ──Materialize──▶ UltraCanvasToolbar
   (= MenuRegistry, extended)            (groups of ids,                (live buttons, kept
          │                               saved in AppSettings)          in step with state)
          │                                     ▲
          └──────── catalogue ────────▶ UltraCanvasToolbarConfigWidget
                                         (the editor; also opened from
                                          the toolbar's right-click menu)
```

### 4.1 One command catalogue for menus, toolbars and shortcuts

Extend `MenuCommand` rather than add a second registry. A program then
registers each command once, and the menu editor and the toolbar editor
offer the same catalogue.

<!-- The fields below are MenuCommand's, listed outside the struct, so the
     doc check cannot place them; they exist (UltraCanvasMenuRegistry.h). -->

```cpp
// UltraCanvasMenuRegistry.h, MenuCommand gains:
std::string iconName;                    // "to-front.svg": resolved per size by the registry's icon resolver
std::string tooltip;                     // optional; default: label + " (" + shortcut + ")"
std::function<bool()> isEnabled;         // live state; empty = always enabled
std::function<bool()> isChecked;         // live state for Checkbox / Radio; empty = defaultChecked
std::function<std::string()> dynamicLabel;   // "Undo Move"; empty = label
// A command that is a widget rather than a button (ArtCreator's Quality slider).
std::function<std::shared_ptr<UltraCanvasUIElement>(const std::string& itemId)> makeToolbarWidget;

// UltraCanvasMenuRegistry gains:
// Turns an icon name and a pixel size into a file. Set once per program:
// ArtCreator maps 16 to artcreator/16/, 24 to artcreator/ (then menu/), 28 to artcreator/28/.
void SetIconResolver(std::function<std::string(const std::string& iconName, int pixelSize)> resolver);
std::string ResolveIcon(const MenuCommand& command, int pixelSize) const;
```

Existing callers are not affected: `iconPath` keeps working, and
`iconName` is consulted only when it is set. Whether to rename the type to
`UltraCanvasCommandRegistry` and keep `UltraCanvasMenuRegistry` as an alias
is an open question (§8). Because no application uses the registry yet,
now is the cheapest time to rename it.

### 4.2 `ToolbarLayout`: groups of command ids

```cpp
// UltraCanvasToolbarLayout.h — plain data, no callbacks
struct ToolbarLayoutGroup {
    std::string id;                      // "zoom": stable, chosen by the program
    std::string label;                   // "Zoom": shown in the editor and in a ribbon-style caption
    std::string iconName;                // the slot's icon when the group is folded
    std::vector<std::string> commands;   // command ids in order (the group's full set)
    std::vector<std::string> hidden;     // the commands of `commands` the user turned off
    bool visible = true;                 // the whole group on or off
    bool folded = false;                 // one UltraCanvasToolGroupButton slot instead of a row
    bool userMade = false;               // made in the editor; may be renamed and deleted
};

struct ToolbarLayout {
    std::string name;                    // "ac-main", "ac-palette": one per toolbar
    std::vector<ToolbarLayoutGroup> groups;  // in display order
    ToolbarIconSize iconSize = ToolbarIconSize::Medium;
    bool showLabels = false;
};
```

Three rules make the layout behave predictably:

- **A group is the unit of separation.** The toolbar puts a separator, or
  `groupSpacing` in the Ribbon style, between two visible groups and never
  inside one. The program no longer adds separators itself, so a hidden
  group never leaves two separators next to each other.
- **A hidden command stays in its group.** It is listed in `hidden`, not
  removed. Showing it again puts it back in its old place.
- **Defaults are merged in.** When a newer version of the program
  registers a command or group that the saved layout does not know, the
  merge (§4.5) adds it at its default place. A saved id that is no longer
  registered is dropped, and the drop is logged. Unlike a menu, which
  shows a stale id as a disabled placeholder (`Materialize`), a toolbar
  should not show an empty icon.

### 4.3 Turning a layout into a toolbar, and keeping it current

```cpp
// Rebuilds `toolbar` from `layout`: ClearItems(), then one button per visible command,
// one UltraCanvasToolGroupButton per folded group, makeToolbarWidget for widget commands,
// and a separator between groups. Each item's id is the command id, so
// GetWidget("edit.undo") finds the Undo button if it is shown.
void MaterializeToolbar(UltraCanvasToolbar& toolbar, const ToolbarLayout& layout,
                        const UltraCanvasMenuRegistry& registry);

// UltraCanvasToolbar gains:
// Re-reads isEnabled / isChecked / dynamicLabel for every item that came from a command.
// The program calls it where it calls UpdateUndoButtons today; the menu reads the same
// functions when it opens.
void RefreshCommandStates();
```

`RefreshCommandStates` replaces the stored `undoButton` / `redoButton`
pointers. The program says *when* state may have changed. The command
says *what* the state is. Whether the button is on the bar at the moment
no longer matters to the program.

### 4.4 The editor: `UltraCanvasToolbarConfigWidget`

It works like `UltraCanvasMenuConfigWidget`: it edits a working copy,
reports what the user did through callbacks, and never touches the live
toolbar itself.

```
┌ Customise toolbars ──────────────────────────────────────────────────────┐
│ Toolbar: [ Main toolbar      ▾ ]   Icon size: [ 24 px ▾ ]  ☐ Show labels  │
├──────────────────────────────┬──────┬────────────────────────────────────┤
│ ON THE TOOLBAR               │      │ AVAILABLE COMMANDS                 │
│ ▾ ☑ File                     │ Add ◀│ ▾ File                             │
│     ☑ New                    │      │     New Window                     │
│     ☑ Open                   │Remove│     Save As                        │
│     ☑ Save                   │  ▶   │     Export                         │
│     ☐ Export                 │      │ ▾ Arrange                          │
│ ▾ ☑ History                  │  ▲   │     Bring Forward                  │
│ ▸ ◩ Zoom        (3 of 4)     │  ▼   │     Align Left Edges               │
│ ▸ ☑ Arrange   [folded]       │      │ ▸ Edit                             │
│ ▸ ☐ Align                    │ New  │ ▸ Layer                            │
│ ▸ ☑ Quality                  │group │ ▸ View                             │
│                              │Rename│                                    │
│                              │ Fold │  [ Filter commands…           ]    │
├──────────────────────────────┴──────┴────────────────────────────────────┤
│ Preview: [🗋][📂][💾] │ [↶][↷] │ [−][+][⤢] │ [▣▾] │ Quality ━━●━ Antialiased│
├──────────────────────────────────────────────────────────────────────────┤
│                                  [ Reset ]  [ Cancel ]  [ Apply ] [ Save ]│
└──────────────────────────────────────────────────────────────────────────┘
```

It is assembled from existing elements, as AGENTS.md requires:

| Part | Element |
|---|---|
| Toolbar picker, icon size | `UltraCanvasDropdown` |
| Show labels | `UltraCanvasCheckbox` |
| Layout tree, with group and command check flags (`Mixed` for "3 of 4") | `UltraCanvasTreeView` with `SetShowCheckboxes(true)` |
| Catalogue, by category | `UltraCanvasTreeView` (as in the menu editor) |
| Filter | `UltraCanvasTextInput` |
| Middle buttons, footer | `UltraCanvasButton` |
| Preview | a real `UltraCanvasToolbar`, materialised from the working copy on every edit |
| Host window | `UltraCanvasModalDialog::AddDialogElement` |

```cpp
class UltraCanvasToolbarConfigWidget : public UltraCanvasContainer {
public:
    explicit UltraCanvasToolbarConfigWidget(const std::string& id);
    void SetRegistry(UltraCanvasMenuRegistry* registry);
    // One entry per toolbar the program offers. `defaults` is what Reset returns to.
    void AddToolbar(const std::string& title, const ToolbarLayout& current, const ToolbarLayout& defaults);
    // Commands that may not go on this toolbar (the palette takes tools only).
    void SetCatalogueFilter(const std::string& toolbarName, std::function<bool(const MenuCommand&)> accepts);

    std::function<void(const std::vector<ToolbarLayout>&)> onApply;   // rebuild the live toolbars now
    std::function<void(const std::vector<ToolbarLayout>&)> onSave;    // apply and persist
    std::function<void()> onCancel;
};

// The usual case in one call: a modal dialog around the widget, wired to
// MaterializeToolbar and SaveToolbarLayouts.
void ShowToolbarConfigDialog(UltraCanvasWindowBase& parent,
                             const std::vector<UltraCanvasToolbar*>& toolbars,
                             UltraCanvasAppSettings& settings);
```

### 4.5 Persistence

Use `UltraCanvasAppSettings`, under `toolbars.<name>`. This is a new
choice: `MenuLayout` writes its own line/section text format, which
predates `UltraCanvasAppSettings`.

```json
{ "toolbars": { "ac-main": {
    "iconSize": 24, "labels": false,
    "groups": [
      { "id": "file",    "hidden": ["file.export"] },
      { "id": "history" },
      { "id": "zoom",    "hidden": ["view.fit-drawing", "view.fit-selection"] },
      { "id": "arrange", "folded": true },
      { "id": "align",   "visible": false },
      { "id": "mine", "label": "My shortcuts", "user": true,
        "commands": ["file.export", "arrange.snap-pixels", "arrange.clipview"] }
] } } }
```

Only the user's changes are stored: order, visibility, folding, the
hidden ids, and the full contents of groups the user made. Contents of a
program-defined group come from the program's defaults every time. That
is why the merge in §4.2 is cheap, and it means a new command that a
release adds to `zoom` appears for users who never customised the group.

```cpp
bool SaveToolbarLayout(UltraCanvasAppSettings& settings, const ToolbarLayout& layout, const ToolbarLayout& defaults);
// Reads toolbars.<defaults.name>: the defaults with the saved changes applied, unknown ids dropped.
ToolbarLayout LoadToolbarLayout(const UltraCanvasAppSettings& settings, const ToolbarLayout& defaults,
                                const UltraCanvasMenuRegistry* registry = nullptr);
```

ArtCreator keeps its other settings in its own `config.ini`
(`ArtCreatorSettings.h`). Toolbars would be its first entries in
`ArtCreator/settings.json`. Moving the colour-picker settings across is
separate work and is not needed here.

### 4.6 Changes made on the bar itself

The editor is the complete tool, but the common changes should not need it:

- **Right-click on any toolbar** shows a popup with one checkbox per group
  (*☑ File ☑ History ☐ Align …*), then *Fold "Zoom"* for the group under
  the pointer, and *Customise toolbars…* at the end. This needs one new
  hook on the toolbar, `onContextMenu(const Point2Di& pos, const std::string& itemId)`.
  The hook is useful beyond configuration.
- **Ctrl+drag a group** on the bar reorders the group as a block. Today's
  `EnableItemReordering` moves single items. The group-aware version moves
  the items of a group together and refuses a drop that would split
  another group. A plain drag within a group reorders icons inside that
  group.
- **Alt+drag an icon off the bar** hides it (`hidden` in its group). This
  is optional; the point is that nothing needs the editor for this action.

## 5. What is missing, and how big it is

| # | Piece | Where | Size |
|---|---|---|---|
| 1 | `MenuCommand`: `iconName`, `tooltip`, `isEnabled`, `isChecked`, `dynamicLabel`, `makeToolbarWidget`; the registry's icon resolver | `UltraCanvasMenuRegistry.{h,cpp}` | ~80 lines |
| 2 | Menus honour `isEnabled` / `isChecked` / `dynamicLabel` when they open | `UltraCanvasMenuRegistry::Materialize`, `UltraCanvasMenu` | ~40 lines |
| 3 | `ToolbarLayout`, merge with defaults, AppSettings load and save | new `UltraCanvasToolbarLayout.{h,cpp}` | ~300 lines |
| 4 | `MaterializeToolbar`, `RefreshCommandStates`, a separator between groups, `groupSpacing` honoured | `UltraCanvasToolbar.{h,cpp}` + layout file | ~200 lines |
| 5 | `onContextMenu` (right button), and the standard group popup | `UltraCanvasToolbar.cpp` | ~80 lines |
| 6 | Group-aware drag reordering | `UltraCanvasToolbar.cpp` (extends the reorder code at `HandleReorderEvent`) | ~150 lines |
| 7 | `UltraCanvasToolbarConfigWidget` + `ShowToolbarConfigDialog` | new files | ~700 lines; the menu editor is 537 lines and is the model |
| 8 | Docs: a component doc, a row in `UltraCanvasUIElements.md`, a section in `UltraCanvasToolbarExamples.md`, a DemoApp page | `Docs/`, `Apps/DemoApp/` | — |
| 9 | Tests: layout merge, save → load, materialise → `GetItemOrder`, state refresh | `Tests/` | ~250 lines |

The framework side comes to about 1,500 lines, about half of it the editor.
Nothing needs a new dependency or platform code.

## 6. Phases

**Phase 1: the command catalogue and the group model (framework).**
Items 1–4 and 9. They are useful without the editor: a program gets
toolbars built from data, separators that are always right, and state that
stays in step. *Built:* `UltraCanvasToolbarLayout.h` (`ToolbarLayout`,
`MaterializeToolbar`, `MergeToolbarLayout`, `Save` / `Load` /
`ForgetToolbarLayout`), the `MenuCommand` fields and registry calls of §4.1
plus `FindShortcutConflicts` and `RefreshMenuStates`, the toolbar's
`AddWidget` and `RefreshCommandStates`, and `Tests/ToolbarLayoutTest.cpp`.
Two choices made on the way: the registry keeps its name (§8, question 1,
is still open; a rename is cheap while nothing but the DemoApp uses it), and
menus take live state through `RefreshMenuStates`, called where the program
calls `RefreshCommandStates`, rather than at every opening.

**Phase 2: the editor and the right-click menu (framework).** Items 5, 7
and 8, plus a DemoApp page beside the menu-configuration page that uses
both on one sample program.

**Phase 3: ArtCreator.**
1. Register its ~95 commands (the 75 menu commands, the 19 tools, and
   Quality) under ids such as `file.new`, `edit.undo`, `arrange.to-front`,
   `view.zoom-in`, `tool.pen`. Give each its `iconName`, shortcut,
   `isEnabled` (Undo, Redo, everything that needs a selection) and
   `isChecked` (Rulers, Grid, the snaps).
2. `BuildMenuBar` becomes `MaterializeInto(menuBar, defaultMenuLayout, registry)`.
   The menu now greys out Undo correctly, and each shortcut is written in
   one place.
3. `BuildToolbar` becomes `MaterializeToolbar` with the six default groups
   from §3.1. The eight candidate groups of §3.1 are registered as well,
   hidden by default.
4. `BuildToolPalette` becomes `MaterializeToolbar` over `PaletteGroups()`.
   Every group is folded by default, which is how it looks today. The
   palette's catalogue filter accepts `tool.*` only.
5. `InstallShortcuts` becomes a lookup from key to command id in the
   registry. The single-letter tool keys remain as they are.
6. Draw the 25 missing 24 px icons (§3.3).
7. *View → Toolbars* lists the groups and ends with *Customise Toolbars…*.

**Phase 4: the other programs.** UltraPaint and Texter first, because they
share ArtCreator's *File / History / Zoom* groups. Texter's existing *Main
Toolbar* / *Markdown Toolbar* switches become two entries in the editor's
toolbar picker.

## 7. Risks and decisions

- **The registry has no real users yet.** Phase 3 will find the gaps that
  the DemoApp page never exercised. Budget for API changes there, and make
  them before a second application adopts it.
- **`Default()` is shared by the whole process.** ArtCreator opens a window
  per document (*New Window*), and every command captures `this`. Two
  windows registering `edit.undo` into `Default()` overwrite each other.
  Each window needs its own registry, or the callbacks must reach the
  *active* window. Recommendation: one registry per window, passed to
  `MaterializeToolbar` and to the editor, and `Default()` kept only for
  single-window programs.
- **Dragging tree rows.** This was not available when this was written:
  `UltraCanvasTreeView::onNodeDragDrop` was never called. It is now
  (`SetNodeDragEnabled`), so the editor can take drags as well as its Add,
  Remove, Up and Down buttons, and the menu editor can gain them too.
- **Toolbar width.** A user can add more groups than fit. Recommendation:
  the main toolbar uses `ToolbarOverflowMode::Scroll`, as the palette
  already does, so nothing is cut off. `ToolbarOverflowMode::Menu` was
  empty `TODO` stubs when this was written. It works now: the groups that
  do not fit go into a chevron's menu, which suits a main toolbar better
  than scrolling.
- **Shortcuts in the catalogue.** Once shortcuts are registry data,
  conflicts such as Ctrl+Shift+S can be detected
  (`registry.FindShortcutConflicts()`). That check is a cheap addition to
  phase 1 and pays for itself immediately.

## 8. Questions for the owner

1. **Rename** `UltraCanvasMenuRegistry` to `UltraCanvasCommandRegistry`
   (keeping an alias) now, while it has no users? Recommended: yes.
2. **One editor or two?** Phase 2 adds a toolbar editor next to the menu
   editor. A later step could put both in one *Customise* dialog with a
   *Menus* tab and a *Toolbars* tab (`UltraCanvasTabbedContainer`).
   Recommended: build the widgets separately and offer the tabbed dialog
   as a convenience.
3. ~~**Which shortcut does ArtCreator mean by Ctrl+Shift+S?**~~ Settled:
   Save As keeps it, and Convert to Editable Shapes is Ctrl+Shift+C.
4. **Should a layout be shared across the suite?** For example, a user who
   hides *Zoom* in UltraPaint might expect it hidden in ArtCreator too.
   Recommended: no. Each program keeps its own `settings.json` entry,
   because the groups are the program's own.
