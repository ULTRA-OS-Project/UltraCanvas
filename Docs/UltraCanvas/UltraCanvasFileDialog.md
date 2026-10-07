# The file dialog

`UltraCanvasFileDialog` (`UltraCanvasModalDialog.h`) is the framework's own
open / save / select-folder dialog. It is built from elements, the same on
every platform:

- a path field with an **Up** button and six view buttons (Details, List and
  four icon sizes);
- a folder tree (Home, the user's places and every mounted drive, loaded as
  each node is expanded) beside the listing of the current folder, which is an
  `UltraCanvasFilerWidget` with its icons, thumbnails, sorting and keyboard -
  but without the Filer's hover icon menu (Copy / Cut / Rename / Delete over
  the file under the pointer): a picker chooses files rather than managing
  them. `hoverIconMenu` below brings it back;
- the file-name field, and below it the file-type filter: a dropdown, or a row
  of toggle buttons (see [Filter toggles](#filter-toggles)).

## Opening one

Most code goes through `UltraCanvasFileLoader` (`UltraCanvasFileLoader.h`). Its
four calls show this dialog while
`UltraCanvasDialogManager::SetUseNativeDialogs(false)` is in force (the
default), and the platform's picker while it is `true`:

```cpp
FileDialogOptions opts;
opts.SetTitle("Open picture")
    .AddFilter("Images", std::vector<std::string>{ "png", "jpg", "jpeg", "webp" })
    .AddFilter("All files", std::vector<std::string>{ "*" })
    .SetParentWindow(window.get());

UltraCanvasFileLoader::OpenFileDialog(opts,
        [](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK) return;   // Cancel, or nothing chosen
            // ... open path (UTF-8: convert with PathFromUtf8)
        });
```

| Call | Mode | Callback receives |
|---|---|---|
| `OpenFileDialog` | `FileDialogType::Open` | one path |
| `OpenMultipleFilesDialog` | `FileDialogType::OpenMultiple` | `std::vector<std::string>` of paths |
| `SaveFileDialog` | `FileDialogType::Save` | one path (the file need not exist) |
| `SelectFolderDialog` | `FileDialogType::SelectFolder` | one folder |

The callback runs when the dialog closes. `DialogResult::OK` always comes with
at least one path; anything else means the user cancelled. Paths are UTF-8.
An Open or Save that succeeds is added to the recent-files list unless the
options say `SetRegisterAsRecent(false)`.

`FileDialogOptions` carries the title, `SetInitialDirectory`,
`SetDefaultFileName`, `SetDefaultExtension` (Save: what a name typed without
one gets when the file type supplies none - see [Save names](#save-names)),
the filters (`AddFilter(description, extension or extensions)`, undotted,
`"*"` for everything), `SetShowHidden`, `SetFilterToggles`,
`SetConfirmOverwrite`, `SetHoverIconMenu` and the parent window.

### Building it yourself

When a caller wants this dialog whatever the native-dialogs setting says, it
creates the dialog from a `FileDialogConfig` itself:

```cpp
FileDialogConfig config;
config.title = "Choose a folder of photos";
config.dialogType = FileDialogType::SelectFolder;
config.initialDirectory = startFolder;   // empty: the last used folder

auto dialog = UltraCanvasDialogManager::CreateFileDialog(config);
dialog->onFileSelected = [](const std::string& folder) { /* ... */ };
UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parentWindow.get());
```

A config starts without filters: the dialog then lists every file under an
"All Files" entry. A folder picker ignores filters.

| `FileDialogConfig` field | |
|---|---|
| `dialogType` | `Open`, `OpenMultiple`, `Save` or `SelectFolder` |
| `initialDirectory` | Where it opens. Empty means the last used folder (below), else the working directory |
| `defaultFileName` | Put into the name field (Save) |
| `defaultExtension` | Save: added (undotted) to a typed name that has none when the chosen type supplies none; never swaps an extension the name has |
| `filters`, `selectedFilterIndex` | `FileFilter{description, extensions}`; the index is the dropdown's first choice |
| `allowMultipleSelection` | Set by `OpenMultiple`: the listing takes a multi-selection |
| `showHiddenFiles` | List dot-files / hidden files |
| `filterToggles` | Toggle buttons instead of the dropdown |
| `confirmOverwrite` | Save asks before replacing an existing file (default `true`) |
| `hoverIconMenu` | The listing shows the Filer's hover icon menu (default `false`) |
| `width`, `height` | 900 × 560 by default; the size the user left it at wins |

The result arrives through `onFileSelected(path)` (single modes),
`onFilesSelected(paths)` (`OpenMultiple`), or `GetSelectedFilePath()` /
`GetSelectedFilePaths()` once `ShowDialog`'s result callback reports
`DialogResult::OK`. `onDirectoryChanged(folder)` fires as the user moves
between folders.

## What the user can do

- **Open** accepts a selected file, a name typed into the name field, a path
  typed relative to the folder shown, or a double-click / Enter in the
  listing. A typed folder name opens that folder instead of accepting it. A
  typed name that is not a file gets a **File Not Found** notice and the
  dialog stays open on it, as the platforms' open dialogs do.
- **Save** accepts any name in a folder that exists (a path typed into a
  folder that is not there gets a **Folder Not Found** notice), with the
  chosen file type's extension - see [Save names](#save-names). A name that is already a
  file gets a **Replace File** question first ("… already exists. Do you want
  to replace it?"), as the platforms' own save dialogs ask. **No** leaves the
  dialog open on that name. A caller that asks itself, or appends to the
  file, turns this off with `confirmOverwrite = false`
  (`FileDialogOptions::SetConfirmOverwrite(false)`).
- **Select folder** lists folders only. OK answers the folder highlighted in
  the listing, else the folder being shown.
- Return in the path field goes to the folder typed there. A file path opens
  its folder and puts the name into the name field.

Keyboard handling (Tab order, Enter, Escape, mnemonics) is the same as every
modal dialog: see [UltraCanvasDialogKeyboard.md](UltraCanvasDialogKeyboard.md).

## Save names

The Save callback receives a path and nothing else, so an application picks
the format to write from the path's extension. The dialog therefore hands
back a name that carries the extension of the file type chosen under "Files
of type" - `ApplySaveExtension(name, type, offered)` in
`UltraCanvasModalDialog.h`, with PNG, JPEG (`jpg`, `jpeg`) and All files
offered:

| Typed | Chosen | Saved as |
|---|---|---|
| `photo` or `photo.` | PNG | `photo.png` |
| `photo.png`, `photo.PNG` | PNG | as typed |
| `photo.jpeg` | JPEG | as typed - any of the type's extensions stands |
| `photo.png` | JPEG | `photo.jpg` - another offered type's extension is swapped |
| `Report v1.2` | PNG | `Report v1.2.png` - an extension no type offers is kept |
| anything | All files (`*`) | as typed |
| `notes` | All files (`*`), `defaultExtension` `txt` | `notes.txt` - the default fills in a missing extension |
| `notes.md` | All files (`*`), `defaultExtension` `txt` | as typed - the default never swaps one |

- `FileDialogConfig::defaultExtension` (`SetDefaultExtension` through
  `UltraCanvasFileLoader`) is the fallback when the type filter gives the name
  nothing: no filters, "All files" chosen, or a type without an extension. It
  only fills in a missing extension (`.profile` counts as having none), and it
  goes on after the type's rule, so the chosen type always wins.
- The extension goes on **before** the Replace File question, so the question
  names the file that will be written: "picture" with PNG chosen asks about
  `picture.png`. The name field shows the name as it will be saved.
- Switching the type rewrites the name the same way ("photo.png" becomes
  "photo.jpg"), as the platforms' own save dialogs do.
- The dialog opens on the type of `defaultFileName` when the type the caller
  chose does not fit it (`FindFilterForName`): suggesting "holiday.jpg" with
  PNG first in the list opens on JPEG rather than saving "holiday.png".
- With filter toggles on, a name of any type that is on stands; any other
  takes the first one's extension.
- The native dialogs follow the same rule. Windows is given the chosen
  type's extension as its default (`SetDefaultExtension`) and the result is
  checked as above; the GTK chooser rewrites the name when the type changes
  and when it is accepted. Where that produces a different name that is
  already a file, they ask about replacing it - GTK leaves the chooser open
  on **No**, Windows cancels. The macOS panel already insists on one of the
  offered extensions.

## Filter toggles

For an Open dialog whose filters are *kinds* of file rather than one format
each, a dropdown of long extension lists is hard to read. With
`filterToggles` set (`FileDialogOptions::SetFilterToggles(true)` through the
loader), the "Files of type" dropdown becomes a **Show:** row of toggle
buttons, one per filter, labelled with the filter's description:

```cpp
FileDialogOptions opts;
opts.SetTitle("Open media").SetFilterToggles(true)
    .AddFilter("Images", imageExtensions)
    .AddFilter("Audio",  audioExtensions)
    .AddFilter("Video",  videoExtensions)
    .AddFilter("All files", std::vector<std::string>{ "*" })
    .SetParentWindow(parentWindow.get());
UltraCanvasFileLoader::OpenMultipleFilesDialog(opts, onPicked);
```

- Any number of buttons can be on. The listing shows the files that any
  button that is on matches. Folders and archives are always listed.
- All buttons start on except an "All files" (`*`) filter, which would match
  everything and make the others pointless. If every filter is `*`, they all
  start on.
- The last button that is on cannot be switched off, so the listing never
  goes empty.
- The extensions stay one hover away: the row's tooltip lists them per
  button, cut to 24 with a count for a long list.
- A native dialog has no toggles. `UltraCanvasFileLoader` hands it the same
  filters as a list, headed by an extra "All supported files" filter that
  matches every one of them.

`UltraCanvasMediaViewer`'s Open button is the reference use. It sorts every
extension the viewer opens into Images (bitmaps, vector drawings, 3D models),
Audio, Video, Documents and Text with the viewer's own `ClassifyFile`, and
leaves out a kind the build cannot show.

## What it remembers

The view, the window size, the Details column widths and the last used folder
are kept between showings in `FileDialog.conf` in the UltraCanvas settings
folder (`UltraCanvasFileDialogSettings.h`). That folder is
`%APPDATA%\UltraCanvas` on Windows, `~/Library/Application Support/UltraCanvas`
on macOS, and `$XDG_CONFIG_HOME/UltraCanvas` (else `~/.config/UltraCanvas`)
elsewhere. Every application shares the file. The last used folder is either
one for all applications (Global) or kept per application (Individual); ULTRA
OS settings switches between them. A caller that names an
`initialDirectory` always opens there. The file is rewritten only when a
close changed something in it; a file that cannot be written or read is
reported once per run on `debugOutput`, and the dialog carries on with the
defaults.

## Native or framework

The system dialogs together (message boxes, input, print) are described in
[UltraCanvasSystemDialogs.md](UltraCanvasSystemDialogs.md).

`UltraCanvasDialogManager::SetUseNativeDialogs(bool)` decides for the whole
application: message boxes and `UltraCanvasFileLoader`'s file dialogs follow
it together. An application that wants this dialog for files but the
platform's message boxes builds the dialog itself as above. UltraViewer,
UltraCleaner, UltraAIApp and UltraAuthenticator leave native dialogs off.
