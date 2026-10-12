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
  of toggle buttons (see [Filter toggles](#filter-toggles)). In a Save dialog
  the name has the **icon of the type being saved** in front of it - drag it
  onto a folder to save there ([The file icon](#the-file-icon)) - and a
  **Settings** button after it, for how that type is written
  ([Save settings](#save-settings)).

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
options say `SetRegisterAsRecent(false)` - by the framework dialog itself
(`FileDialogConfig::addToRecent`), or by the loader after a native one.

`FileDialogOptions` carries the title, `SetInitialDirectory`,
`SetDefaultFileName`, the filters (`AddFilter(description, extension or
extensions)`, undotted, `"*"` for everything), `SetShowHidden`,
`SetFilterToggles`, `SetConfirmOverwrite`, `SetHoverIconMenu`,
`SetDefaultExtension`, `SetSaveSettingsButton`, `SetImageSize` and the parent
window.

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
| `defaultExtension` | Save: added to a name that still has no extension after the chosen type's - under All files, or with no filters (undotted, e.g. `"png"`) |
| `filters`, `selectedFilterIndex` | `FileFilter{description, extensions}`; the index is the dropdown's first choice |
| `allowMultipleSelection` | Set by `OpenMultiple`: the listing takes a multi-selection |
| `showHiddenFiles` | List dot-files / hidden files |
| `filterToggles` | Toggle buttons instead of the dropdown |
| `confirmOverwrite` | Save asks before replacing an existing file (default `true`) |
| `hoverIconMenu` | The listing shows the Filer's hover icon menu (default `false`) |
| `validateNames` | Save refuses a name the file system cannot hold, saying why, and stays open (default `true`) - see [Save names](#save-names) |
| `addToRecent` | Open, Open multiple and Save add what they accept to the system's recent files (default `true`; `UltraCanvasFileLoader` sets it from `SetRegisterAsRecent`) |
| `saveSettingsButton` | Save: the Settings button after the name, for the chosen type's [save settings](#save-settings) (default `true`; `FileDialogOptions::SetSaveSettingsButton`). Off for a caller whose writer does not read them |
| `imageWidth`, `imageHeight` | Save: the size of the picture being saved (`FileDialogOptions::SetImageSize`). A bitmap type's Settings then offer [another size](#the-pictures-size) for this save; `GetSaveImageSize()` is the size chosen |
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
- A name the file system cannot hold is refused with the reason ("… cannot
  be used as a file name: it contains ":"") and the dialog stays open on it,
  unless `validateNames` is off. `InvalidFileNameReason(name)` holds the
  rules for the system it runs on: empty, "." and "..", control characters
  and names over 255 (bytes on POSIX, UTF-16 units on Windows) everywhere;
  on Windows also `< > : " / \ | ? *`, a trailing dot or space, and the device
  names `CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9`, `LPT1`-`LPT9` with any
  extension. `FileNameRules::Windows` / `Posix` check a name for the other
  system.
- Under All files a name without an extension stays without one, so there
  is no format to pick - unless a default extension is given
  (`FileDialogOptions::SetDefaultExtension("png")`, or
  `FileDialogConfig::defaultExtension`), which such a name then gets
  ("photo" -> "photo.png", `ApplyDefaultExtension`), before the Replace
  File question like the rest. Don't add a default extension of your own
  after the dialog: it would bypass that question. The framework's savers
  (`UCRasterDocument::SaveToFile`, `UltraCanvasFileLoader::SaveVectorDocument`)
  refuse a name without one with a message asking for an extension.
- The native dialogs follow the same rules. Windows is given the chosen
  type's extension as its default (`SetDefaultExtension`) - the caller's
  default extension when the type names none, or there are no filters - and
  the result is checked as above; the GTK chooser rewrites the name when the
  type changes and when it is accepted. Where that produces a different name
  that is already a file, they ask about replacing it - GTK leaves the
  chooser open on **No**, Windows cancels. The macOS panel already insists
  on one of the offered extensions; when none names one, it is given the
  default extension as its only one, other extensions still allowed, and
  adds it and asks itself. Android's document picker is offered the
  suggested name with the default extension added.

## The file icon

A Save dialog shows, in front of the file name, the icon of the file it is
about to write - the type the name has with the chosen type's extension
(`GetSaveTypeName()`), drawn as the listing draws that type
(`UltraCanvasFilerWidget::DrawTypeIcon`, so it follows Display > File icons
too). It changes as the name is typed and as the type is switched: "photo"
with JPEG chosen is a JPEG, and "notes.txt" under All files a text file.

The icon is what RISC OS's save box has always had: **drag it onto a folder**
and that folder is where the file goes. While it is dragged, a badge with the
icon and the name follows the pointer, and the folder a release would choose
is shown the way a file drag shows it - framed in the listing (a folder entry,
or over empty space the folder the listing shows), highlighted in the folder
tree (a place or a drive). Released there, the dialog opens that folder; the
name stays as it was, so OK (or Return) then saves into it. A release over
anything else, over a file, or onto a folder inside an archive the listing is
browsing changes nothing, and Escape ends the drag without closing the
dialog. The drag stays inside the dialog: there is no file yet to hand to
another window.

The icon is an element of its own, `UltraCanvasFileDragIcon`
(`UltraCanvasFileDragIcon.h`), for any other save box that wants one. It
carries a name, not a file: the elements under it are sent `DragEnter` /
`DragOver` / `DragLeave` with `dragMimeType` set to
`UltraCanvasFileDragIcon::kDragMimeType` and no `droppedFiles`, so nothing
that accepts files copies anything, and the release goes to `onDropped`,
which asks the target where the drop lands while it still shows it:

<!-- doc-check: std::shared_ptr<UltraCanvasContainer> row; std::shared_ptr<UltraCanvasFilerWidget> listing; std::shared_ptr<UltraCanvasTreeView> tree; void SaveInto(const std::string& folder); -->
```cpp
auto icon = CreateFileDragIcon("SaveIcon", "report.pdf", 0, 0, 32, 32);
icon->SetIconPainter([listing = listing.get()](IRenderContext* ctx, const std::string& name,
                                               bool isFolder, const Rect2Di& rect) {
    listing->DrawTypeIcon(ctx, name, isFolder, rect);
});
// The tree highlights the rows a drop can land on.
tree->onFilesDragAccept = [icon = icon.get()](TreeNode* node) {
    return node != nullptr && icon->IsDragging();
};
icon->onDropped = [listing = listing.get(), tree = tree.get()](UltraCanvasUIElement* target,
                                                               const Point2Di&) {
    for (UltraCanvasUIElement* e = target; e; e = e->GetParentContainer()) {
        if (e == listing) { SaveInto(listing->GetIncomingDropFolder()); return; }
        if (e == tree) {
            if (TreeNode* node = tree->GetDropTargetNode()) SaveInto(node->data.nodeId);
            return;
        }
    }
};
row->AddChild(icon);
```

A press that is released without moving is a click (`onClick`).

## Save settings

How a file is written is often a choice: JPEG's quality, PNG's compression,
whether a TIFF is LZW or Deflate. The **Settings** button after the name in a
Save dialog shows the settings of the type being saved - a checkbox, a slider
or a list per setting - and stores them on OK. For a picture of known size a
bitmap type's dialog also has the picture's size for this save
([The picture's size](#the-pictures-size)). The button is greyed out while
the type has nothing to set ("notes.txt", a name of no type, BMP without a
picture size), and its tooltip names the type. The dialog's **Defaults** button puts every setting back to
the format's default; Cancel keeps what was there.

The settings belong to the format, not to the dialog: they are kept per
format for **every application**, between runs, in `SaveSettings.conf` in the
UltraCanvas settings folder ([What it remembers](#what-it-remembers)), one
`<format>.<setting>=<value>` line for each setting the user has set. They are
reached through `UltraCanvasFileLoader` (`UltraCanvasFileSaveSettings.h` holds
the types):

| Call | |
|---|---|
| `HasSaveSettings(extensionOrPath)` | The type has settings. `"jpg"`, `".JPEG"`, `"photo.jpe"` and a whole path all name JPEG |
| `FindSaveFormat(extensionOrPath, format)` | The format: its name, extensions (the first is its key) and `FileSaveSettingSpec`s |
| `GetSaveSettings(extensionOrPath)` | Every setting's value: the user's where they set it, the default elsewhere |
| `HasStoredSaveSettings(extensionOrPath)` | The user has set any of them |
| `SetSaveSettings(settings)` / `ResetSaveSettings(extensionOrPath)` | Store (merged over what was there; a value is clamped or replaced by the default when it is not one the setting can hold, and a key the format lacks is dropped) / forget |
| `ShowSaveSettingsDialog(extensionOrPath, parent, onClosed)` | The settings dialog the Settings button opens; with `pictureWidth, pictureHeight, size` also the [Size row](#the-pictures-size), `onClosed` getting the size chosen |
| `RegisterSaveFormat(format)` | A plugin's or an application's own format (or one replacing a built-in one: same first extension) |
| `GetImageExportOptions(path, options)` | The image writer's `UCImageSave::ImageExportOptions` for `path`, from its format's settings; `GetImageExportOptions(path, size, options)` at a [chosen size](#the-pictures-size) too |

Built in are the bitmap formats the image writer takes options for, each with
only the settings that writer honours: PNG (compression, colours, interlace,
transparency), JPEG (quality, progressive, optimised coding, subsampling),
WebP, AVIF, HEIF, JPEG XL and JPEG 2000 (quality, lossless, effort or speed,
bit depth), GIF (colours, dither, interlace), TIFF and OpenEXR (compression),
Netpbm (binary), TGA, PCX, SGI and Photoshop (RLE), DPX and Cineon (bit
depth). BMP, ICO, QOI, HDR, FITS and Farbfeld are written without options.

A writer reads them back. `UCRasterDocument::SaveToFile` does so itself when
the caller passes no options and the user has stored settings for the format
(until then it keeps the writer's own defaults), so UltraPaint's Save honours
the button with no change of its own. Any other writer asks:

<!-- doc-check: void WriteMyFormat(const std::string& path, int level, bool fast); -->
```cpp
// Once, at start-up: the format and what its writer can be told.
FileSaveFormat mine;
mine.name = "My format";
mine.extensions = {"myf"};
mine.settings = {
    FileSaveSettingSpec::Number("level", "Compression", 1, 9, 6, "9 writes the smallest file"),
    FileSaveSettingSpec::Toggle("fast", "Fast mode", false),
    FileSaveSettingSpec::Choice("profile", "Profile",
                                {{"web", "For the web"}, {"print", "For print"}}, "web")};
UltraCanvasFileLoader::RegisterSaveFormat(mine);

// When writing: whatever the Save dialog's Settings button stored.
auto write = [](const std::string& path) {
    const FileSaveSettings settings = UltraCanvasFileLoader::GetSaveSettings(path);
    WriteMyFormat(path, settings.GetInt("level", 6), settings.GetBool("fast"));
};
write("drawing.myf");
```

A setting key is letters, digits, `-` and `_`; a key with anything else could
not be read back from the file and is left out with a debug message. A caller
whose writer ignores the settings turns the button off
(`FileDialogOptions::SetSaveSettingsButton(false)`), so the dialog does not
offer a choice that changes nothing.

### The picture's size

A bitmap is often wanted at another size than the picture has - a smaller
copy for the web, an icon. When the caller tells the Save dialog how big the
picture is (`FileDialogOptions::SetImageSize(width, height)`, or
`FileDialogConfig::imageWidth` / `imageHeight`), the Settings of a **bitmap
type** start with a **Size** row: width × height in pixels, opened on the
picture's own size, and **Keep proportions** under it (on: change one side
and the other follows the picture's shape; off: both are free). The row is
there for every pixel format, so BMP, ICO and QOI - which have no settings of
their own - get a Settings dialog with the size alone. Defaults puts the
picture's size back with the rest.

The size is **not** a setting of the format: it belongs to this picture, so
it is not stored and the next Save starts on the next picture's own size. It
comes back with the save instead:

<!-- doc-check: std::shared_ptr<UCRasterDocument> doc; -->
```cpp
FileDialogOptions opts;
opts.SetTitle("Save image as")
    .AddFilter("PNG", std::string("png"))
    .AddFilter("JPEG", std::vector<std::string>{ "jpg", "jpeg" })
    .SetImageSize(doc->GetWidth(), doc->GetHeight())
    .SetParentWindow(window.get());
UltraCanvasFileLoader::SaveFileDialog(opts, [doc](DialogResult r, const std::string& path,
                                                  const FileSaveImageSize& size) {
    if (r != DialogResult::OK) return;
    UCImageSave::ImageExportOptions options;   // the format's settings, at that size
    std::string error;
    if (!size.IsOriginal() && UltraCanvasFileLoader::GetImageExportOptions(path, size, options))
        doc->SaveToFile(path, error, &options);
    else
        doc->SaveToFile(path, error);           // the stored settings, if any
});
```

`FileSaveImageSize` is the picture's own size while `IsOriginal()` - nothing
chosen, a type that is not a bitmap format, or the platform's save dialog,
which has no Settings. A dialog built directly reports it with
`UltraCanvasFileDialog::GetSaveImageSize()`, and
`ShowSaveSettingsDialog(extensionOrPath, pictureWidth, pictureHeight, size,
parent, onClosed)` offers the same row anywhere else. UltraPaint's Save As
passes its picture's size.

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
folder (`UltraCanvasFileDialogSettings.h`); the [save settings](#save-settings)
are in `SaveSettings.conf` beside it. That folder is
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
The platforms' save dialogs have neither the file icon nor the Settings
button; the save settings still apply to what is written, and an application
can offer `UltraCanvasFileLoader::ShowSaveSettingsDialog` itself.
