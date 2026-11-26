# Files, text and localisation

## Every user file goes through `UltraCanvasFileLoader`

A file a **person** opens or saves - through a dialog, a drop, a command line
argument, a recent-files list - goes through
[`UltraCanvasFileLoader`](Elements/UltraCanvasFileLoader.md)
(`UltraCanvasFileLoader.h`). It owns the dialogs, the readers and writers of
every format, the inventory of what can be opened and saved, save settings,
transparent decompression and URLs. Only a program's *own* internal files
(settings, caches, logs) are read and written directly.

In depth: [`Docs/Modules/FileLoader/README.md`](../Modules/FileLoader/README.md).

```cpp
// Ask for a file, then load its bytes (a local path or a URL; .gz / .zst ... are unpacked).
FileDialogOptions opts;
opts.title = Tr("Open data");
opts.filters = {FileFilter(Tr("CSV files"), "csv"), FileFilter(Tr("All files"), "*")};
UltraCanvasFileLoader::OpenFileDialog(opts, [](DialogResult result, const std::string& path) {
    if (result != DialogResult::OK) return;
    FileBytesResult file = UltraCanvasFileLoader::LoadFile(path);
    if (!file.success) { /* file.error says why */ return; }
    // file.bytes, file.contentType ...
});
```

What the loader covers:

| Need | Entry points |
|---|---|
| Dialogs | `OpenFileDialog`, `OpenMultipleFilesDialog`, `SaveFileDialog`, `SelectFolderDialog` (native or the framework's own, by the user's setting) |
| Raw bytes | `LoadFile(pathOrUrl)`, `SaveFile(path, bytes, error)` (written atomically) |
| Typed objects | images, audio, video, vector drawings (`LoadVectorDocument`, `SaveVectorDocument` - with a size, a drawing saved as a bitmap), text and office documents (`OpenTextDocument`, `SaveTextDocument`), spreadsheets, presentations, 3D models |
| Text from any file | text extraction for search and previews (`TextExtractionResult`, `WhyNoTextExtraction`) |
| Format inventory | `GetSupportedLoadExtensions`, `GetSupportedSaveExtensions`, a dialog's file types |
| Save settings | per-format options (`HasSaveSettings`, `ShowSaveSettingsDialog`) |
| Recent files | `NotifyRecentFile` (automatic unless `registerAsRecent = false`) |

Formats are provided by plug-ins (`UltraCanvas/Plugins/`) and can be extended;
archives are opened through VirtualFS.

## Paths are UTF-8

Every path the API takes or returns is a UTF-8 `std::string`. Convert at the
boundary with `std::filesystem` using `PathToUtf8` / `PathFromUtf8`
(`UltraCanvasPathUtf8.h`) - never `path.string()` or `fs::path(str)`, which
use the system code page on Windows and corrupt non-ASCII names:

```cpp
for (const auto& entry : std::filesystem::directory_iterator(PathFromUtf8(folder))) {
    std::string name = PathToUtf8(entry.path().filename());
}
std::ifstream in(PathFromUtf8(utf8Path), std::ios::binary);
```

<!-- doc-check: std::string folder; std::string utf8Path; -->

`scripts/check_path_string.py` enforces it.

## Text is UTF-8

All strings in the API are UTF-8. `UltraCanvasUtilsUtf8.h` and
`UltraCanvasTextUtils.h` have the helpers for code points, grapheme-safe
cutting and case. Numbers written **into a file or protocol** use
`FormatFloatClassic` / `FormatFixedClassic` and are read with
`TryParseFloat` / `ParseFloatClassic` - never `std::to_string(double)`,
`std::stof` or a locale-dependent stream - so a file written in a German
locale reads back in an English one.

## Translations: `Tr`

Every text a person reads goes through `Tr` (`UltraLocalisation/UltraLocalisation.h`),
keyed by the English text written at the call site:

```cpp
std::string save     = Tr("Save");                                   // "Speichern"
std::string open     = TrC("button", "Open");                        // with a context
std::string files    = TrN("{n} file", "{n} files", count);          // the language's plural rule
std::string progress = TrF("Deleted {0} of {1}", done, count);       // formatted
```

<!-- doc-check: int count = 0; int done = 0; -->

Translations come from three layers - the application's catalogue, the
library's, and the standard set every application shares - in
`media/locale/<language>/`. After adding texts, `python3 scripts/extract_ui_strings.py`
updates the English catalogues and `python3 scripts/check_locale_catalogues.py`
checks them. The language follows the desktop (`UltraCanvasSystemLanguage.h`);
`UltraCanvasLocalisationEditor` lets a translator edit catalogues in place.

What is **not** translated: element identifiers, code, parser input, proper
names in sample data, and strings the code compares.

In depth: [`Docs/Modules/UltraLocalisation/README.md`](../Modules/UltraLocalisation/README.md).

## Clipboard

```cpp
SetClipboardText("Copied text");
std::string text;
if (GetClipboardText(text)) { /* ... */ }
UltraCanvasClipboard* clipboard = GetClipboard();   // images, rich text, files, formats
```

Text elements already support cut / copy / paste; the clipboard history is
`UltraCanvasClipboardHistory`.

## Settings and the program's own files

- `UltraCanvasAppSettings.h` - an application's settings, saved for it.
- `UltraCanvasSettingsFolder.h`, `UltraCanvasDataFolder.h`,
  `UltraCanvasDiskCache.h` - where a program keeps its own files.
- `UltraCanvasSettingsWatcher.h` - reacting when settings change outside.
- `UltraCanvasJSON.h` - JSON ([doc](../UltraCanvas/UltraCanvasJSON.md)).
- HTML / CSS / entities - only through the HTMLReader module
  ([doc](../UltraCanvas/UltraCanvasHTMLReader.md)).

## File-system services

Folder watching (`UltraCanvasFolderWatcher`), trash (`UltraCanvasTrash`),
file locks, file times, file associations, desktop entries, shell links,
removable drives, network places, portable devices, previews and thumbnails,
the host's file icons - each has a header in `UltraCanvas/include/` and a doc
in `Docs/UltraCanvas/`.

Next: [Modules](Modules.md)
