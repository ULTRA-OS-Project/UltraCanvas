// include/UltraCanvasFilerWidget.h
// Filer folder widget: displays the content of one folder with selectable view
// types (details table, list, thumbnail grids, size bars, treemap — with
// force-directed tree and 3D reserved), sortable by name / size / type / dates,
// with a full file context menu (Copy / Cut / Delete / Duplicate / Rename /
// New / Display / Open with / Compress / Extract / Print / Extras / Settings)
// and an optional small icon menu shown at the top-right of the hovered item.
// A selection info bar under the folder display describes the selection (type,
// size, dates, attributes, image dimensions, media duration / codec, folder
// content counts, multi-selection totals).
// Self-rendered like UltraCanvasAlbum so large folders stay cheap. Image
// thumbnails are decoded asynchronously on worker threads: the folder page
// renders immediately with placeholder glyphs and tiles fill in as decodes
// complete. Video files fill their tiles the same way with a poster frame
// (the first frame of the clip) when a video backend is available.
// Besides clicking, entries are selected with a rubber band: dragging from
// empty space draws a selection rectangle and everything it touches becomes
// the selection (Ctrl adds the rectangle to the selection held before).
// The inline rename editor is a real UltraCanvasTextInput overlaid on the
// item's name, so editing has a movable caret, click-to-position, text
// selection and clipboard support.
// Entries can be dragged: pressing an item and moving a few pixels picks it
// (or the whole selection it belongs to) up. Inside the widget the drag is
// shown as a badge following the cursor with the folder under it highlighted,
// and dropping on that folder moves the files into it (Ctrl drops a copy);
// leaving the widget hands the same set to the native OS drag & drop so it can
// be dropped on any other window or application (the target performs the
// copy/move). A drag never changes the selection, so dragging a file does not
// re-target a preview attached to onSelectionChanged. Files dropped onto the
// widget from other applications are copied into the shown folder, and
// Copy / Cut / Paste (Ctrl+C/X/V) interoperate with the system clipboard
// so files can be exchanged with external file managers. When the clipboard
// holds raw data instead of files (an image or text copied in another
// program), Paste creates a new file with that content in the shown folder.
// The column-based views (Details / List / BarSize) carry draggable
// UltraCanvasSplitPane-style splitters between their columns, and names that
// do not fit their column show the full name in a tooltip. In the tile-shaped
// views (thumbnail grids, treemap) a name wider than the tile wraps onto
// further lines (FilerStyle::captionMaxLines, 2 by default); what does not fit
// even then is dropped from the front of the last line, which opens with "…".
// An explicit file list (ShowFileList) is sorted like a folder listing unless
// SetFileListOrderPreserved() asks for the given order to be kept — for lists
// whose order is the information, such as a most-recently-used history.
// Changes the user makes to a folder's content (create / paste / drop /
// rename / duplicate / delete / compress / extract) are reported through
// onFolderModified, apart from the rescan notification onFolderRefreshed.
// Which file kinds show a real content preview instead of their type glyph is
// selectable per kind (Display > Preview: Bitmaps, Vector graphics, 3D, PDF,
// Text, Docs, Spreadsheets, Videos — all on by default), so a folder full of
// expensive files can be browsed with only the cheap previews switched on.
// The context menu's "Open with >" lists the applications the OS registers
// for the selected files (UltraCanvasFileAssociations, prewarmed in the
// background), the host's own entries, and an "Other application…" picker;
// the host can extend the context menu's Extras submenu via
// extrasMenuProvider.
// Version: 1.15.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTimer.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasMenu;
    class UltraCanvasButton;
    class UltraCanvasTextInput;
    struct DialogConfig;

    // ===== PASTE NAME CONFLICTS =====
    // What to do with a pasted entry whose name already exists in the target
    // folder. KeepBoth gives the pasted entry the next free "name (2)" style
    // name — the behavior a paste without conflicts always has.
    enum class PasteConflictAction {
        KeepBoth,
        Replace,
        Skip
    };

    // ===== HOW THE FOLDER CONTENT IS PRESENTED =====
    enum class FilerViewType {
        Details,             // text columns: name, size, type, dates, attributes, info
        List,                // compact icon+name entries flowing into columns
        ThumbnailsSmall,     // thumbnail grids of growing tile size
        ThumbnailsMedium,
        ThumbnailsBig,
        ThumbnailsMaximized,
        BarSize,             // one bar per entry, length proportional to its size
        TreeMap,             // squarified treemap weighted by entry size
        GourceTree,          // force-directed tree (Gource style) — to be implemented
        View3D               // 3D view — to be implemented
    };

    // ===== SORTABLE FIELDS =====
    enum class FilerSortField {
        Name,
        Size,
        Type,
        ModifiedDate,
        CreatedDate
    };

    // ===== DETAILS-VIEW COLUMNS =====
    // The columns of the Details table, left to right. Each one can be resized
    // by dragging the splitter on its right edge, or from code with
    // SetDetailsColumnWidth(). Note that Attributes and Info are not sortable
    // fields, which is why the columns have their own identifiers rather than
    // reusing FilerSortField.
    enum class FilerDetailsColumn {
        Name,
        Path,        // containing folder — only shown for a file list display
        Size,
        Type,
        ModifiedDate,
        CreatedDate,
        Attributes,
        Info
    };
    // Number of Details columns (the enum is contiguous and Info is last).
    constexpr size_t kFilerDetailsColumnCount =
            static_cast<size_t>(FilerDetailsColumn::Info) + 1;

    // ===== THUMBNAIL DATASET FIELDS =====
    // Extra per-file facts shown under the name in the thumbnail views. Combine
    // as a bitmask (Display > Dataset toggles them). Length applies to audio /
    // video, Dimensions to bitmaps; both are skipped for entries they don't fit.
    enum class FilerDatasetField : uint32_t {
        NoneData     = 0,
        Size         = 1u << 0,
        ModifiedDate = 1u << 1,   // "Edit date"
        CreatedDate  = 1u << 2,   // "Creation date"
        Attributes   = 1u << 3,
        Length       = 1u << 4,   // audio / video duration
        Dimensions   = 1u << 5    // bitmap width × height
    };

    // ===== COARSE FILE CATEGORY (drives icons / colors / type sorting) =====
    enum class FilerFileCategory {
        Folder,
        Image,
        Vector,
        Model3D,       // 3D models (stl, obj, ply, gltf, ...)
        Audio,
        Video,
        Document,
        Text,
        Spreadsheet,
        Archive,
        Executable,
        Other
    };

    // ===== PREVIEWABLE FILE KINDS =====
    // Which kinds of file are shown with a real content preview — a thumbnail
    // rendered from the file itself — instead of the generic type glyph.
    // Combine as a bitmask; the Display > Preview submenu toggles them and
    // every kind is enabled by default. Switching one off makes its entries
    // fall back to the type glyph immediately (and stops the widget from
    // reading those files at all), which is what makes browsing a folder of
    // huge photos, videos or PDFs on a slow volume bearable.
    //
    // The kinds are grouped by what the preview costs to produce, not by
    // FilerFileCategory: PDF is split out of Documents because it renders a
    // page, and CSV / TSV count as Spreadsheets because they preview as a
    // cell grid (their file category stays Text).
    enum class FilerPreviewType : uint32_t {
        NonePreview    = 0,
        Bitmaps        = 1u << 0,   // png / jpeg / gif / webp / tiff / ...
        VectorGraphics = 1u << 1,   // svg / eps / cdr / xar
        Models3D       = 1u << 2,   // stl (rendered), other 3D formats
        PDF            = 1u << 3,   // first page of the document
        Text           = 1u << 4,   // txt / log / json / xml / source code / ...
        Docs           = 1u << 5,   // odt / doc / docx / rtf / md / html / tex
        Spreadsheets   = 1u << 6,   // ods / xls / xlsx / csv / tsv
        Videos         = 1u << 7    // poster frame of the clip
    };
    // Every previewable kind — the default of SetPreviewTypes().
    constexpr uint32_t kFilerAllPreviewTypes = 0xFFu;

    // ===== ONE ENTRY OF THE DISPLAYED FOLDER =====
    struct FilerEntry {
        std::string name;            // file / folder name (no path)
        std::string path;            // full path
        std::string extension;       // lowercase, without the leading dot
        std::string typeName;        // "Folder", "PNG Image", "ZIP Archive", ...
        FilerFileCategory category = FilerFileCategory::Other;

        bool isDirectory = false;
        bool isHidden    = false;
        bool isReadOnly  = false;
        bool isSymlink   = false;
        bool isArchive   = false;    // browsable archive (zip / 7z / ...)

        uint64_t size = 0;           // bytes (uncompressed)
        uint64_t compressedSize = 0; // bytes inside an archive (0 = not compressed)
        // Size used by the size-weighted views (BarSize / TreeMap): for
        // directories this is a lazily computed recursive size, else == size.
        uint64_t effectiveSize = 0;

        std::time_t modifiedTime = 0;
        std::time_t createdTime  = 0;

        std::string attributes;      // compact attribute string, e.g. "D", "RH"
        std::string info;            // extra info column: play duration of audio /
                                     // video, compression factor of archives, ...
        std::string thumbnailPath;   // explicit thumbnail; images fall back to path
    };

    // ===== "NEW >" DOCUMENT KINDS =====
    // The context menu's New submenu offers these; a default set (Text, Doc,
    // Spreadsheet, Bitmap, Vector, Audio, Video) is installed by the widget and
    // can be replaced via SetNewDocumentTypes().
    struct FilerNewDocumentType {
        std::string label;          // menu label, e.g. "Spreadsheet"
        std::string extension;      // created file extension, e.g. "ods"
        std::string templatePath;   // optional file copied as the new document;
                                    // empty = an empty file is created
    };

    // ===== "OPEN WITH >" APPLICATION =====
    struct FilerOpenWithApp {
        std::string label;
        std::string iconPath;
        std::function<void(const std::vector<FilerEntry>&)> onOpen;
    };

    // ===== VISUAL STYLE =====
    struct FilerStyle {
        Color backgroundColor      = Color(255, 255, 255, 255);
        Color textColor            = Color(32, 32, 36, 255);
        Color secondaryTextColor   = Color(120, 120, 126, 255);
        Color headerBackground     = Color(245, 245, 247, 255);
        Color headerTextColor      = Color(70, 70, 76, 255);
        Color gridLineColor        = Color(232, 232, 236, 255);
        Color hoverColor           = Color(238, 244, 252, 255);
        Color selectionColor       = Color(208, 228, 250, 255);
        Color selectionBorderColor = Color(60, 140, 220, 255);
        Color barColor             = Color(90, 160, 235, 255);
        Color barBackground        = Color(240, 240, 244, 255);
        Color iconMenuBackground   = Color(50, 50, 56, 205);
        Color iconMenuGlyphColor   = Color(255, 255, 255, 235);
        Color renameFieldColor     = Color(255, 255, 255, 255);
        Color renameBorderColor    = Color(60, 140, 220, 255);
        Color infoBarBackground    = Color(245, 245, 247, 255);
        Color infoBarTextColor     = Color(50, 50, 56, 255);

        std::string fontFamily;          // empty = system default
        float fontSize        = 12.0f;   // Windows standard UI size (9pt @ 96dpi)
        float smallFontSize   = 11.0f;

        int detailsRowHeight  = 24;      // details / bar-size row height
        int listRowHeight     = 22;      // list mode row height
        int listColumnWidth   = 220;     // list mode column width
        int outerPadding      = 8;
        int tileGap           = 10;      // thumbnail grid gap
        int captionHeight     = 20;      // thumbnail caption strip (first line)

        // Name wrapping in the caption of a thumbnail tile / treemap cell: a
        // name wider than the tile continues on the next line, up to
        // captionMaxLines lines (1 = the old single-line, tail-ellipsized
        // caption). What still does not fit is dropped from the *front* of the
        // last line, which then opens with "…" so the end of the name — its
        // extension — stays readable. Every line past the first adds
        // captionLineHeight to the tile.
        int captionMaxLines   = 2;
        int captionLineHeight = 0;       // 0 = derived from smallFontSize

        // Thumbnail tile edge for the four thumbnail view types.
        int thumbnailSmall     = 72;
        int thumbnailMedium    = 110;
        int thumbnailBig       = 170;
        int thumbnailMaximized = 260;

        // Scale of the folder glyph inside a thumbnail tile's image box
        // (1.0 = fill the box like the file glyphs; UltraFiler uses 0.7).
        float folderIconScale  = 1.0f;

        int iconMenuButtonSize = 20;     // hover icon-menu button edge
        int infoBarHeight      = 26;     // selection info bar under the entries

        // Column splitters of the Details / List / BarSize views. They reuse
        // UltraCanvasSplitPane's divider styling so a resizable Filer column
        // looks and drags exactly like a split-pane divider; splitterHitMargin
        // widens the grab area beyond the painted strip.
        SplitPaneStyle columnSplitter;

        FilerStyle() {
            columnSplitter.splitterThickness = 3;
            columnSplitter.splitterHitMargin = 3;
            columnSplitter.splitterColor = Color(226, 226, 230, 255);
        }
    };

    // ===== THE FILER ELEMENT =====
    // Self-rendered (Album pattern): rows, tiles, treemap cells, the hover icon
    // menu and the scrollbar are all painted inside Render() rather than being
    // child elements, so a folder with thousands of entries stays cheap.
    class UltraCanvasFilerWidget : public UltraCanvasContainer {
    public:
        UltraCanvasFilerWidget(const std::string& identifier,
                               float x, float y, float w, float h);

        UltraCanvasFilerWidget(const std::string& identifier, float w, float h)
            : UltraCanvasFilerWidget(identifier, -1, -1, w, h) {}

        explicit UltraCanvasFilerWidget(const std::string& identifier)
            : UltraCanvasFilerWidget(identifier, -1, -1, -1, -1) {}

        ~UltraCanvasFilerWidget() override;

        // ===== FOLDER =====
        void SetPath(const std::string& folderPath);   // scans + displays the folder
        const std::string& GetPath() const { return currentPath; }
        void Refresh();                                // rescan the current folder

        // ===== FILE LIST (SEARCH RESULTS) =====
        // Display an explicit list of paths (e.g. search results) instead of
        // the folder listing, in the current view mode. The Details view adds
        // a Path column after the name, showing each entry's containing
        // folder. GetPath() keeps returning the folder shown before; SetPath()
        // returns to the normal folder display. Refresh() re-stats the list
        // (vanished files drop out). Pair with SetOpenPathMenuItemVisible()
        // to let the context menu open an entry's containing folder.
        void ShowFileList(const std::vector<std::string>& paths);
        bool IsShowingFileList() const { return fileListMode; }

        // Show a file list exactly in the order the paths were handed over
        // instead of sorting it — for lists whose order is the information
        // (a most-recently-used history, a ranked result list). While this is
        // on, sorting is inert for the file-list display (SetSort and the
        // Details column headers leave the order alone); a folder listing is
        // always sorted.
        void SetFileListOrderPreserved(bool preserved);
        bool IsFileListOrderPreserved() const { return preserveFileListOrder; }

        // ===== VIEW =====
        void SetViewType(FilerViewType type);
        FilerViewType GetViewType() const { return viewType; }

        void SetSort(FilerSortField field, bool ascending);
        void SetSortField(FilerSortField field) { SetSort(field, sortAscending); }
        void SetSortAscending(bool ascending)   { SetSort(sortField, ascending); }
        FilerSortField GetSortField() const { return sortField; }
        bool IsSortAscending() const { return sortAscending; }

        void SetShowHiddenFiles(bool show);
        bool GetShowHiddenFiles() const { return showHiddenFiles; }

        // Background pre-scan of the shown folder's subfolders (one level), so
        // entering one shows its content from memory instead of waiting for a
        // cold directory scan. On by default; turning it off drops the cache
        // and queue. See the FOLDER LISTING PREFETCH section for freshness.
        void SetFolderPrefetchEnabled(bool enabled);
        bool IsFolderPrefetchEnabled() const { return folderPrefetchEnabled; }

        // "Compressed thumbnails": hold finished thumbnails QOI-compressed in
        // memory (roughly 3-4x smaller for photos) instead of as raw ARGB32
        // pixmaps. Tiles being drawn are decompressed on demand into a small
        // hot cache, so scrolling still draws from raw surfaces. Off by
        // default; toggling drops the thumbnail cache and re-decodes lazily.
        void SetCompressedThumbnails(bool enabled);
        bool GetCompressedThumbnails() const { return compressedThumbs.load(); }

        // "Shrink thumbnail rows": in the thumbnail grid views the tiles are
        // square (edge = the selected Small / Medium / Big / Maximized size), so
        // a row of landscape photos leaves a wide empty band above and below
        // each image. When enabled (default), a grid row whose images all
        // display shorter than the tile edge is shortened to the tallest image
        // actually shown in it, closing that gap; a row that contains any
        // full-height item (a folder, a generic-glyph file, a portrait/square or
        // not-yet-measured image) keeps the full edge height. Natural image
        // sizes are read from file headers (no decode). Off restores the fixed
        // square grid.
        void SetShrinkThumbnailRows(bool enabled);
        bool GetShrinkThumbnailRows() const { return shrinkThumbnailRows; }

        // "Flexible tile widths": the thumbnail grids derive their column
        // count from the fixed tile edge, which leaves a strip on the right —
        // too narrow for one more column — empty. That strip grows as the
        // window widens, until the next column suddenly snaps in. When this
        // is enabled (default), the leftover is distributed across the row
        // instead (Explorer-style): every cell widens equally so the grid
        // always fills the width, resizing the window stretches the cells
        // smoothly until another column fits, and the extra room benefits the
        // captions (long names wrap later). Only the cell grows — the image
        // box inside it keeps the square Small / Medium / Big / Maximized
        // edge, centered, so thumbnails keep their size while the window is
        // dragged and the async decode cache is not churned by resizes. Off
        // restores the fixed-width grid with the right-hand gap.
        void SetFlexibleTileWidths(bool enabled);
        bool GetFlexibleTileWidths() const { return flexibleTileWidths; }

        // Snapshot of what the thumbnail cache currently holds — lets an
        // application (or an A/B test) compare the footprint of compressed
        // vs. raw storage. rawBytes is what the same thumbnails would take
        // uncompressed; with compression off, storedBytes == rawBytes.
        struct ThumbCacheStats {
            size_t entries = 0;       // finished thumbnails held
            size_t storedBytes = 0;   // bytes actually held (blobs or raw)
            size_t rawBytes = 0;      // raw ARGB32 size of those thumbnails
            size_t hotEntries = 0;    // decompressed tiles in the hot cache
            size_t hotBytes = 0;
        };
        ThumbCacheStats GetThumbnailCacheStats() const;

        // The small icon menu (Copy / Cut / Rename / Delete) shown at the top
        // right of the hovered item. Also toggled by the Display > Icon-Menu
        // context-menu checkbox.
        void SetHoverIconMenuEnabled(bool enabled);
        bool IsHoverIconMenuEnabled() const { return hoverIconMenu; }

        // Show the "Open Path" context-menu item as the menu's first entry
        // (useful when the widget displays a search result rather than a plain
        // folder). `label` replaces the item's caption, e.g.
        // "Open path (in new tab)".
        void SetOpenPathMenuItemVisible(bool visible,
                                        const std::string& label = "Open Path") {
            showOpenPathItem = visible;
            openPathItemLabel = label;
        }

        // ===== DRAGGING ENTRIES =====
        // Dragging files out of the view (on by default): a press on an item
        // followed by a few pixels of movement picks up that item — or the
        // whole selection when the pressed item is part of it — as a drag.
        // While the cursor stays inside the widget the drag is drawn in-widget
        // (badge under the cursor, drop folder highlighted) and dropping on a
        // folder shown in the view moves the files into it (Ctrl = copy); once
        // the cursor leaves the widget the set is handed to the native OS drag
        // so other windows and applications can accept it. Turning this off
        // leaves presses as plain clicks.
        void SetDragEnabled(bool enabled);
        bool IsDragEnabled() const { return dragEnabled; }

        // The selection info bar shown under the folder display. One line
        // describing the selection: name, type, size, modified date and
        // attributes for a single file, plus pixel dimensions for bitmaps and
        // play length / codec for audio and video (parsed from the file
        // headers, no decoding); recursive content counts and size for a
        // selected folder; the item counts and summed size for a multi
        // selection; a folder summary when nothing is selected. Also toggled
        // by the Display > Info-Bar context-menu checkbox.
        void SetSelectionInfoVisible(bool visible);
        bool IsSelectionInfoVisible() const { return showSelectionInfo; }

        // ===== THUMBNAIL DATASET =====
        // Which extra facts are drawn under the file name in the thumbnail
        // views (Display > Dataset). Changing the set relays out the tiles.
        void SetDatasetField(FilerDatasetField field, bool on);
        bool IsDatasetFieldEnabled(FilerDatasetField field) const;
        void SetDatasetFields(uint32_t mask);
        uint32_t GetDatasetFields() const { return datasetFields; }

        // ===== SELECTIVE PREVIEWS =====
        // Which file kinds get a content preview instead of their type glyph
        // (Display > Preview). All kinds are on by default. Switching a kind
        // off repaints its entries with the type glyph and stops the widget
        // from opening those files for a preview at all; switching it back on
        // re-uses whatever is still cached and decodes the rest in the
        // background as usual.
        void SetPreviewType(FilerPreviewType type, bool on);
        bool IsPreviewTypeEnabled(FilerPreviewType type) const;
        void SetPreviewTypes(uint32_t mask);
        uint32_t GetPreviewTypes() const { return previewTypes; }
        // The preview kind an entry belongs to, or NonePreview for entries
        // that never carry a content preview (folders, audio, archives,
        // programs, unknown types).
        static FilerPreviewType PreviewTypeOf(const FilerEntry& e);

        void SetStyle(const FilerStyle& s);
        const FilerStyle& GetStyle() const { return style; }

        // ===== RESIZABLE COLUMNS =====
        // The column views carry draggable splitters (UltraCanvasSplitPane
        // dividers, styled through FilerStyle::columnSplitter):
        //   Details — one splitter on the right edge of every column, inside
        //             the header strip; dragging it moves width between that
        //             column and the one after it, so the table keeps filling
        //             the widget exactly like split-pane panes do.
        //   List    — one splitter in every gap between the flowing columns;
        //             dragging any of them re-widths all of them (the list
        //             columns are uniform).
        //   BarSize — splitters between the name column, the bar and the size
        //             label column, running the full height of the rows.
        // Widths survive rescans, sorting and view switches. Set them from
        // code to restore a layout saved by the application.
        void SetColumnResizeEnabled(bool enabled);
        bool IsColumnResizeEnabled() const { return columnResizeEnabled; }

        // Details columns. The Name column is the flexible one: it absorbs
        // whatever the other columns leave, so setting its width is equivalent
        // to taking that width from the column next to it.
        void SetDetailsColumnWidth(FilerDetailsColumn column, int pixels);
        int  GetDetailsColumnWidth(FilerDetailsColumn column) const;
        void ResetDetailsColumnWidths();          // back to the built-in widths

        // List view column width (same value as FilerStyle::listColumnWidth).
        void SetListColumnWidth(int pixels);
        int  GetListColumnWidth() const { return style.listColumnWidth; }

        // BarSize columns: the name column on the left and the size label on
        // the right; the bar takes what is left between them. A value column
        // width of 0 means "auto" — as wide as the widest formatted size.
        void SetBarSizeNameColumnWidth(int pixels);
        int  GetBarSizeNameColumnWidth() const { return barSizeNameWidth; }
        void SetBarSizeValueColumnWidth(int pixels);
        int  GetBarSizeValueColumnWidth() const { return barSizeValueWidth; }

        // ===== NAME TOOLTIPS =====
        // Names too long for the space they are drawn in are ellipsized; with
        // this on (default) hovering such a name pops a tooltip with the full
        // name. The hover icon-menu buttons keep their own tooltips: while the
        // cursor is on one of them its action tooltip is shown instead, so in
        // the Details view the name column describes the file and the icon
        // strip over the other columns describes its buttons.
        void SetNameTooltipsEnabled(bool enabled);
        bool AreNameTooltipsEnabled() const { return nameTooltips; }

        // ===== DATA ACCESS =====
        const std::vector<FilerEntry>& GetEntries() const { return entries; }
        std::vector<FilerEntry> GetSelectedEntries() const;
        // The selected indices into GetEntries(), copy-free. Prefer this over
        // GetSelectedEntries() for per-event or per-frame inspection — the
        // latter copies every selected FilerEntry (eight strings each), which
        // for a Select All in a large folder is the whole listing.
        const std::vector<size_t>& GetSelectionIndices() const { return selection; }
        void ClearSelection();
        void SelectAll();
        // Makes `path` the only selected entry and scrolls it into view, as a
        // click on it would (onSelectionChanged fires). False when the path is
        // not among the entries currently displayed. Lets a host point the
        // view at one file after opening its folder.
        bool SelectPath(const std::string& path);
        // Scroll so the first selected entry is fully in view. The scroll is
        // applied against the next recomputed layout, so a resize still in
        // flight — e.g. the host opening a preview pane that narrows the
        // widget — is taken into account instead of the stale geometry.
        void EnsureSelectionVisible();
        // True while an interaction inside the widget claims the Escape key
        // (the inline rename editor, a running item drag, the compress
        // dialog). Hosts with their own window-level Escape shortcut should
        // stand back while this is set so those interactions keep their
        // cancel key.
        bool WantsEscapeKey() const {
            return renamingIndex >= 0 || draggingItems || marqueeActive ||
                   compressDlg.active;
        }

        // ===== FILE OPERATIONS (also wired to the context / icon menus) =====
        // Copy / Cut mirror the selection to the OS clipboard (file-manager
        // formats: text/uri-list + cut/copy marker) besides the internal
        // filer clipboard, so the files can be pasted in external programs.
        // Paste prefers the OS clipboard, falling back to the internal one.
        void CopySelection();
        void CutSelection();
        // Paste into the current folder. Clipboard files are copied (or moved
        // after a Cut); a clipboard image or text without files is written as
        // a new file ("Pasted image.png" / "Pasted text.txt" style names).
        void Paste();
        // Paste `paths` into `folder` (copies, or moves when `cut` is set).
        // A name that already exists in the folder raises the conflict
        // dialog — Replace / Keep both / Skip, optionally applied to all
        // remaining conflicts — and the paste continues with the choice.
        // With `onDone` set the caller owns the post-paste work (refresh,
        // history) and is told whether anything changed; without it the
        // widget refreshes itself and reports to onFolderModified.
        void PasteFilesInto(std::string folder, std::vector<std::string> paths,
                            bool cut,
                            std::function<void(bool changed)> onDone = nullptr);
        void DeleteSelection();    // gated by confirmDelete when set
        void DuplicateSelection(); // copy alongside with a unique name
        void StartRename(size_t entryIndex);   // inline rename editor
        // What a delete that wipes out the whole selection leaves selected.
        // Off (default): nothing — the selection is simply gone. On: the entry
        // that took the deleted one's place becomes the selection (the next
        // one, or the previous one when the deleted entry was last). Hosts
        // that feed a preview pane from onSelectionChanged switch this on
        // while the preview is up, so deleting the previewed file walks the
        // preview on to its neighbour instead of folding the pane away. The
        // new selection is in place before onFolderRefreshed fires, so the
        // host sees one selection change and no empty state in between.
        void SetSelectNextAfterDelete(bool enabled) { selectNextAfterDelete = enabled; }
        bool IsSelectNextAfterDelete() const { return selectNextAfterDelete; }
        // Pack the selection into an archive alongside it. The extension picks
        // the format (e.g. "zip", "7z", "tar", "tar.gz", "tar.bz2", "tar.xz",
        // "tar.zst"); defaults to a .zip archive.
        void CompressSelection(const std::string& extension = "zip");
        void ExtractSelection();   // unpack selected archives alongside
        // The context menu's Extract: the same overlay dialog as Compress,
        // editing the destination folder name (default: the archive's name
        // without its suffix). The icon can be dragged onto a folder to
        // retarget where the archives unpack; several selected archives each
        // unpack into their own subfolder of the named folder.
        void OpenExtractDialog();
        static bool ClipboardHasContent();

        // "New >" document kinds (replaces the default seven).
        void SetNewDocumentTypes(const std::vector<FilerNewDocumentType>& types);
        void CreateNewDocument(const FilerNewDocumentType& type);

        // "Open with >" applications. The submenu lists the applications the
        // OS has registered for the selected files (via
        // UltraCanvasFileAssociations, default application first), then the
        // entries added here, then "Other application…" (a file-dialog picker).
        // The OS lookups are prewarmed on a background thread — the first
        // widget triggers the association-database parse, every folder scan
        // pre-resolves the folder's extensions — so opening the menu never
        // parses anything.
        void AddOpenWithApp(const FilerOpenWithApp& app);
        void ClearOpenWithApps();
        // Off = the pre-1.14 behaviour: only AddOpenWithApp entries.
        void SetSystemOpenWithEnabled(bool enabled) { systemOpenWith = enabled; }
        // When enabled, double-click / Enter on a file launches it with the
        // OS default application — but only while no onFileActivated callback
        // is installed (a host with its own activation handling, like
        // UltraFiler's preview, decides there instead).
        void SetActivateOpensWithDefaultApp(bool enabled) { activateOpensDefault = enabled; }

        // Explorer-semantics activation of one file entry: an executable
        // runs — a native binary (or AppImage) directly, a script through
        // the Run / Open / Cancel dialog — and everything else opens with
        // the OS default application. Hosts with their own onFileActivated
        // call this for the "launch it" part of their handling. Entries
        // inside archives (virtual paths) are ignored.
        void OpenEntryWithOS(const FilerEntry& e);

        // ===== CALLBACKS =====
        std::function<void(const FilerEntry&)> onFileActivated;   // double-click / Enter on a file
        std::function<void(const std::string&)> onPathChanged;    // after SetPath / folder entered
        std::function<void(const std::vector<FilerEntry>&)> onSelectionChanged;
        // After every (re)scan of the shown folder — the listing changed
        // (entries created, deleted, moved in or out, renamed). Hosts refresh
        // their folder description (item counts, status bar) from it. The
        // selection survives a rescan on the files that are still there.
        std::function<void()> onFolderRefreshed;
        // After the user changed a folder's content through this widget — an
        // entry created, pasted, dropped in or out, renamed, duplicated,
        // deleted, packed or extracted. The argument is the folder that
        // changed; normally the displayed one, but a subfolder when that is
        // where the change landed (files dropped onto it, an archive written
        // into it). Unlike onFolderRefreshed this reports *user actions*, not
        // rescans: navigation, sorting, view changes and a plain Refresh()
        // never fire it. A file-list display (ShowFileList) only reports
        // changes whose folder is known, since its entries span many folders.
        std::function<void(const std::string& folderPath)> onFolderModified;
        std::function<void(FilerViewType)> onViewTypeChanged;
        std::function<void(FilerSortField, bool)> onSortChanged;
        // After a column splitter drag (or a programmatic width change) — the
        // application can persist the widths and restore them later.
        std::function<void()> onColumnWidthsChanged;

        // Optional veto for DeleteSelection: return false to abort.
        std::function<bool(const std::vector<FilerEntry>&)> confirmDelete;

        // Extra info column provider (e.g. plays a media header to report the
        // duration). Called once per entry at scan time; empty result keeps the
        // built-in value (compression factor for archive-compressed entries).
        std::function<std::string(const FilerEntry&)> infoProvider;

        // Context-menu hooks. Items without a hook (and no built-in default)
        // are shown disabled.
        std::function<void(const std::vector<FilerEntry>&)> onShare;
        std::function<void(const std::vector<FilerEntry>&)> onPrint;
        std::function<void(const std::vector<FilerEntry>&)> onAttributes;
        std::function<void(const std::vector<FilerEntry>&)> onAccess;
        std::function<void()> onSettings;
        std::function<void(const FilerEntry&)> onOpenPath;  // default: SetPath(parent)
        // Host-provided tail of the context menu's Extras submenu, called
        // every time the menu opens (so item flags can follow the host's
        // state); non-empty results are appended behind a separator. The
        // UltraFiler adds "Open prompt" and its Pin / Unpin submenus here.
        std::function<std::vector<MenuItemData>()> extrasMenuProvider;

        // File-list (search result) display: while active, ScanFolder() builds
        // the entries from these explicit paths instead of listing currentPath.
        bool fileListMode = false;
        std::vector<std::string> fileListPaths;

        // New-document hook: return true when the app handled the creation
        // itself (dialog, template, ...); false lets the widget create the file.
        std::function<bool(const FilerNewDocumentType&, const std::string& folderPath)> onNewDocument;

        std::function<void(const std::string&)> onError;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return true; }

    private:
        // ===== STATE =====
        std::string currentPath;
        std::vector<FilerEntry> entries;          // scanned + sorted
        FilerViewType viewType = FilerViewType::Details;
        FilerSortField sortField = FilerSortField::Name;
        bool sortAscending = true;
        // Leave a file-list display in the order it was given (see
        // SetFileListOrderPreserved).
        bool preserveFileListOrder = false;
        bool showHiddenFiles = false;
        bool hoverIconMenu = true;
        bool showOpenPathItem = false;
        std::string openPathItemLabel = "Open Path";
        bool showSelectionInfo = true;
        bool shrinkThumbnailRows = true;
        bool flexibleTileWidths = true;
        bool columnResizeEnabled = true;
        bool nameTooltips = true;
        // Bitmask of FilerDatasetField values drawn under thumbnail captions.
        uint32_t datasetFields = 0;
        // Bitmask of FilerPreviewType values that may show a content preview.
        uint32_t previewTypes = kFilerAllPreviewTypes;
        FilerStyle style;

        std::vector<size_t> selection;            // indices into `entries`
        // Selection membership as one flag per entry, rebuilt at the top of
        // each paint: the draw functions ask "is this item selected?" once per
        // drawn item, and answering with std::find over `selection` made a
        // repaint O(visible × selected) — after Select All in a big folder
        // every hover-move crawled.
        std::vector<uint8_t> frameSelected;
        int lastClickedIndex = -1;                // anchor for shift-range select
        int hoveredIndex = -1;
        // SetSelectNextAfterDelete: when a delete takes the whole selection
        // away, PerformDeletion parks the neighbour that should inherit it
        // here and the rescan selects that path instead of restoring the
        // (now gone) old selection — one selection change, none of it empty.
        bool selectNextAfterDelete = false;
        std::string selectAfterScanPath;
        // A committed rename, so the rescan can follow the entry from its old
        // path to its new one: the selection is restored by path, and without
        // this the renamed entry would drop out of it (leaving nothing
        // selected, which breaks the very next F2 / Rename command).
        std::string renamedFromPath;
        std::string renamedToPath;

        // Tooltip tracking: what the cursor is currently over, so a tooltip is
        // shown once when the cursor enters it and hidden when it leaves. An
        // icon-menu button wins over the item name underneath it.
        // (NoneTarget, not None: X11 #defines None, same reason
        // FilerDatasetField spells its empty value NoneData.)
        enum class TooltipTarget { NoneTarget, IconButton, ItemName };
        TooltipTarget tooltipTarget = TooltipTarget::NoneTarget;
        size_t tooltipEntry  = 0;
        int    tooltipAction = -1;   // IconMenuAction when target == IconButton

        std::vector<FilerNewDocumentType> newDocumentTypes;
        std::vector<FilerOpenWithApp> openWithApps;
        bool systemOpenWith = true;        // OS-registered apps in "Open with >"
        bool activateOpensDefault = false; // activation fallback (see setter)
        // "Open with > Other application…": file-dialog picker, then launches
        // the selection with the chosen application.
        void OpenSelectionWithChooser();

        // Computed per-entry geometry (content space, before scroll offset).
        struct ItemLayout {
            Rect2Di rect;        // whole row / tile / treemap cell
            Rect2Di imageRect;   // icon / thumbnail sub-region
            size_t  entryIndex;
            // Name lines the tile caption reserves (thumbnail views): 1 unless
            // a name in the same grid row has to wrap. All tiles of a row share
            // it so the grid stays aligned.
            int     captionLines = 1;
        };
        std::vector<ItemLayout> items;
        bool layoutValid = false;
        // Tile heights depend on measured text (wrapped names), so a layout
        // built without a render context — before the widget is on a window —
        // falls back to single-line captions. The first Render() then redoes it
        // with real metrics.
        bool captionLinesMeasured = true;
        // The context of the running Render(), so the layout it triggers can
        // measure text. Null outside a render pass (GetRenderContext() is used
        // then, which is null until the widget is attached to a window).
        IRenderContext* measureContext = nullptr;
        int  contentWidth = 0;
        int  contentHeight = 0;
        int  lastAreaW = -1, lastAreaH = -1;
        // Entry to scroll into view right after the next EnsureLayout() pass
        // (set by EnsureSelectionVisible; -1 = nothing pending).
        int  pendingRevealEntry = -1;
        bool effectiveSizesValid = false;         // recursive dir sizes computed

        // Details-view columns (recomputed with the layout).
        struct DetailsColumn {
            FilerDetailsColumn id = FilerDetailsColumn::Name;
            FilerSortField field;
            std::string title;
            int x = 0, width = 0;
            bool rightAligned = false;
            bool sortable = true;
        };
        std::vector<DetailsColumn> detailsColumns;
        int detailsHeaderHeight = 26;
        // Static description of the Details table, in FilerDetailsColumn order.
        // The Name column's defaultWidth only seeds it — layout re-derives that
        // column from what the others leave.
        struct DetailsColumnSpec {
            FilerDetailsColumn id;
            FilerSortField field;
            const char* title;
            int  defaultWidth;
            bool rightAligned;
            bool sortable;
        };
        static const DetailsColumnSpec kDetailsColumnSpecs[kFilerDetailsColumnCount];
        // Current width of every Details column, indexed by FilerDetailsColumn
        // — the widths the splitters edit, kept across rescans, sorting and
        // view switches. Index 0 (Name) is derived at layout time from what
        // the other columns leave, so it always fills the table out to the
        // widget edge.
        std::vector<int> detailsColumnWidths;

        // BarSize columns: the name column on the left, the size label on the
        // right, the bar in between. 0 = auto for the value column (as wide as
        // the widest formatted size).
        int barSizeNameWidth  = 220;
        int barSizeValueWidth = 0;
        // Measured width of the widest formatted size, remembered from the
        // draw pass so a splitter drag (which has no render context) can start
        // from the width the auto column is actually drawn with.
        mutable int barSizeAutoValueWidth = 0;

        // ===== COLUMN SPLITTERS (UltraCanvasSplitPane-style dividers) =====
        // Rebuilt every frame by the draw pass (like iconMenuHits) so hit
        // testing always matches what was painted: the strips are in widget-
        // local coordinates with scrolling already applied.
        struct ColumnSplitterHit {
            Rect2Di rect;    // painted strip (the grab area adds the hit margin)
            int     index;   // Details: column left of it; List: column
                             // boundary; BarSize: 0 = name|bar, 1 = bar|value
        };
        std::vector<ColumnSplitterHit> columnSplitters;
        int hoveredSplitter  = -1;
        int draggingSplitter = -1;
        int splitterDragStartX = 0;   // pointer x when the drag started
        int splitterDragStartA = 0;   // width of the column left of it
        int splitterDragStartB = 0;   // width of the column right of it

        // Scrolling (vertical everywhere; horizontal in List mode).
        int scrollOffsetX = 0;
        int scrollOffsetY = 0;
        bool draggingScrollbar = false;
        int  scrollbarGrabOffset = 0;

        // Hover icon-menu hit rects (screen-local), rebuilt each frame.
        enum class IconMenuAction { Copy, Cut, Rename, Delete };
        struct IconMenuHit {
            Rect2Di rect;
            size_t  entryIndex;
            IconMenuAction action;
        };
        std::vector<IconMenuHit> iconMenuHits;

        // Inline rename editor: a real UltraCanvasTextInput added as a child
        // while a rename runs, overlaid on the item's name, so editing has a
        // movable caret, click-to-position, selection and clipboard support.
        // Enter commits, Escape cancels, and losing the keyboard focus
        // (clicking anywhere else) commits, Explorer-style. The editor is
        // created fresh per rename and removed when the rename ends.
        int renamingIndex = -1;
        std::shared_ptr<UltraCanvasTextInput> renameInput;
        // Windows-style rename trigger: pressing the name of the entry that is
        // already the sole selection records it here; releasing without a drag
        // arms a one-shot timer that opens the rename editor. The delay is
        // what tells a "second click = rename" apart from a "double-click =
        // open" — a double-click cancels the pending rename.
        int pendingRenameIndex = -1;
        TimerId pendingRenameTimer = InvalidTimerId;

        // ===== DRAGGING ENTRIES (in-widget drag + native OS drag out) =====
        // A left press on an item arms the gesture and captures the mouse, so
        // even a fast flick out of the widget still delivers the move that
        // starts the drag. Past the slop threshold the pressed item — or the
        // whole selection when the press landed inside it — is picked up:
        //   * inside the widget the drag is drawn here (a badge under the
        //     cursor, the folder below it highlighted) and a drop on a folder
        //     of the view moves the files into it (Ctrl drops a copy);
        //   * crossing the widget's border does NOT end the drag: the badge
        //     keeps following the cursor over the rest of the window (drawn
        //     through the window's drag overlay, since a widget cannot paint
        //     outside its own bounds) and a release over another element hands
        //     the files to it as a Drop event — that is how a file reaches a
        //     folder tree or a second filer pane of the same window;
        //   * only when the cursor leaves the WINDOW is the same set handed to
        //     the native OS drag (window->StartNativeFileDrag), where the drop
        //     target performs the copy / move and a move refreshes this view.
        //     Platforms without a native drag keep the window-wide drag alive
        //     instead of dropping the gesture (dragNativeRefused).
        // Dragging never changes the selection: what a press would select is
        // deferred to the release (see pendingSelectIndex / dragCollapseIndex),
        // so dragging a file does not fire onSelectionChanged and does not
        // re-target an attached preview.
        bool dragEnabled = true;
        bool dragOutArmed = false;         // press may still become a drag
        Point2Di dragOutPressPoint;
        int  dragPressIndex = -1;          // entry the press landed on
        bool dragMouseCaptured = false;    // pointer captured for the gesture
        bool draggingItems = false;        // in-widget drag running
        Point2Di dragPos;                  // cursor while dragging (widget-local)
        std::vector<std::string> dragPaths;  // files being dragged
        std::string dragLabel;             // badge text ("photo.png" / "5 items")
        FilerEntry  dragLeadEntry;         // drives the badge icon / thumbnail
        int  dragDropFolderIndex = -1;     // folder highlighted under the cursor
        // Badge size, measured once when the drag starts (the label does not
        // change while it runs) so every move only has to place it.
        Size2Di dragBadgeSize;
        bool dragOverlayShown = false;     // badge registered with the window
        // The native drag was offered the set and refused it (no window, no
        // implementation on this platform, grab denied): don't ask again for
        // the rest of this gesture, keep drawing the drag instead.
        bool dragNativeRefused = false;
        // Press on an already-selected item keeps the (multi-)selection so it
        // can be dragged; the usual "select only this item" collapse is
        // deferred to the release and recorded here (-1 = nothing deferred).
        int dragCollapseIndex = -1;
        // Press on an item outside the selection: selecting it is deferred to
        // the release the same way, so a press that turns into a drag leaves
        // the selection (and any preview fed by it) untouched.
        int pendingSelectIndex = -1;

        // ===== RUBBER-BAND (MARQUEE) SELECTION =====
        // A left press on empty space arms it; moving past the drag slop
        // starts the rectangle. While it runs, every entry it touches is
        // selected (with Ctrl the rectangle adds to the selection held at the
        // press). A press-and-release without movement keeps the old
        // click-on-empty behaviour: the selection is cleared on release.
        bool marqueeArmed = false;         // press may still become a marquee
        bool marqueeActive = false;        // rectangle is being dragged
        bool marqueeAdditive = false;      // Ctrl held at the press
        Point2Di marqueePressLocal;        // widget-local press (slop test)
        Point2Di marqueeAnchor;            // content-space fixed corner
        Point2Di marqueeCurrent;           // content-space moving corner
        std::vector<size_t> marqueeBaseSelection;  // selection at the press

        // ===== ASYNC THUMBNAILS =====
        // Decoding an image for a tile is expensive (full decode + resize).
        // Done inside Render() it blocks the frame until every visible
        // thumbnail is ready, so opening a folder full of photos froze the
        // window. Instead the draw path only consumes pixmaps that finished
        // decoding; missing ones are queued, decoded by background worker
        // threads, and every finished pixmap posts one (coalesced) redraw so
        // tiles fill in as they become available.
        //
        // The decode queue is viewport-driven: each frame rebuilds it from
        // what that frame actually needs — the visible tiles first, then a
        // prefetch band of one viewport in the scroll direction (so the next
        // screenful is usually ready before it scrolls in). Files outside
        // visible + prefetch are never decoded, and pending work that
        // scrolls out of both bands is dropped from the queue instead of
        // wasting a worker on it.
        enum class ThumbState { Pending, Ready, Failed };
        // A Ready slot holds either the raw pixmap (compression off) or a
        // QOI blob (compression on) — never both. `bytes` is whichever is
        // held, `rawBytes` always the uncompressed ARGB32 size.
        struct ThumbSlot {
            ThumbState state = ThumbState::Pending;
            std::shared_ptr<UCPixmap> pixmap;
            std::shared_ptr<std::vector<uint8_t>> qoi;
            size_t bytes = 0;
            size_t rawBytes = 0;
        };
        struct ThumbRequest {
            std::string path;
            int w = 0, h = 0;
            ImageFitMode fit = ImageFitMode::Contain;
            float scale = 1.0f;
            uint64_t generation = 0;
        };
        std::unordered_map<std::string, ThumbSlot> thumbSlots;  // by ThumbSlotKey
        std::deque<ThumbRequest> thumbQueue;
        // Per-frame decode want-list (UI thread only, no lock): filled in
        // priority order while the frame draws (visible tiles) and prefetches
        // (next-screen band), then swapped into thumbQueue in one commit.
        std::vector<ThumbRequest> thumbFrameWants;
        // One decode per file at a time: UCImageRaster instances are shared
        // via the global image cache and are not safe against two threads
        // rasterizing the same instance concurrently.
        std::unordered_set<std::string> thumbPathsInFlight;
        // Compressed mode: LRU of decompressed pixmaps for the tiles being
        // drawn, so repaints never re-inflate. Guarded by thumbMutex.
        struct HotThumb {
            std::shared_ptr<UCPixmap> pixmap;
            size_t bytes = 0;
            uint64_t tick = 0;
        };
        std::unordered_map<std::string, HotThumb> thumbHot;
        size_t thumbHotBytes = 0;
        uint64_t thumbHotTick = 0;
        std::atomic<bool> compressedThumbs{false};
        mutable std::mutex thumbMutex;          // guards slots/queue/generation
        std::condition_variable thumbCond;
        std::vector<std::thread> thumbWorkers;
        bool thumbShutdown = false;
        uint64_t thumbGeneration = 0;           // bumped to drop stale results
        size_t thumbBytes = 0;                  // decoded pixmap bytes held
        std::atomic<bool> thumbRedrawPosted{false};
        // Destructor flips this so a queued PostToUIThread redraw task that
        // outlives the widget becomes a no-op instead of a dangling call.
        std::shared_ptr<std::atomic<bool>> thumbAlive =
                std::make_shared<std::atomic<bool>>(true);

        // Returns the decoded pixmap, or null (draw the placeholder) after
        // queueing a background decode / while one is running / when the
        // file cannot be decoded.
        std::shared_ptr<UCPixmap> AcquireThumbnail(const std::string& path,
                                                   int w, int h,
                                                   ImageFitMode fit,
                                                   float scale);
        void StartThumbnailWorkersLocked();
        void StopThumbnailWorkers();
        void ThumbnailWorkerMain();
        void DropThumbnailCache();              // on rescan / view change
        void PostThumbnailRedraw();
        static std::string ThumbSlotKey(const std::string& path, int w, int h,
                                        ImageFitMode fit, float scale);
        // The image file a tile displays; empty when the entry has none.
        std::string ThumbSourceFor(const FilerEntry& e) const;
        // The exact icon rect + fit mode the draw call will use for an item
        // — prefetch must request identical parameters or its decode would
        // land under a different cache key than the draw looks up.
        void ThumbGeometryForItem(const ItemLayout& item, Rect2Di& outRect,
                                  ImageFitMode& outFit) const;
        // Queues decodes for the tiles in the prefetch band (one viewport
        // past the visible edge in scroll direction).
        void PrefetchThumbnails(IRenderContext* ctx, const Rect2Di& bounds);
        // Swaps thumbFrameWants into the worker queue and forgets pending
        // slots that fell out of the visible + prefetch bands.
        void CommitThumbnailWants();
        // True when `e` may show a content preview right now: it belongs to a
        // preview kind and that kind is enabled in previewTypes.
        bool PreviewEnabledFor(const FilerEntry& e) const;
        // True when a preview of `e` is worth drawing in a box that size. A
        // page-shaped preview (PDF, 3D model, text page) needs a tile; in the
        // icon column of a Details / List row it would be an indistinct
        // smudge, so those rows keep the type glyph.
        static bool PreviewFitsRect(const FilerEntry& e, const Rect2Di& rect);

        // ===== TEXT-CONTENT PREVIEWS (Text / Docs / Spreadsheets) =====
        // Text-shaped files have no image to decode, so their preview is the
        // beginning of their own content drawn as a miniature page: the first
        // lines for text and documents, the first cells as a grid for
        // spreadsheets. Reading and un-wrapping the file (plain text, the
        // XML inside an ODF / OOXML package, tags stripped from HTML) happens
        // on the same background workers as the image decodes; the draw pass
        // only ever consumes a finished snippet and paints the type glyph
        // until then.
        struct TextPreviewSnippet {
            std::vector<std::string> lines;   // first lines, already trimmed
            bool tabular = false;             // cells are '\t'-separated
        };
        enum class TextPreviewState { Pending, Ready, Failed };
        struct TextPreviewSlot {
            TextPreviewState state = TextPreviewState::Pending;
            TextPreviewSnippet snippet;
        };
        std::unordered_map<std::string, TextPreviewSlot> textSlots;  // by path
        std::deque<std::string> textQueue;             // paths to read
        std::vector<std::string> textFrameWants;       // UI thread, per frame
        std::unordered_set<std::string> textPathsInFlight;

        // Copies the finished snippet into `out` and returns true; returns
        // false (draw the glyph) after queueing a background read, while one
        // runs, or when the file has no readable text.
        bool AcquireTextPreview(const FilerEntry& e, TextPreviewSnippet& out);
        // Swaps textFrameWants into the worker queue, like the thumbnails.
        void CommitTextPreviewWants();
        // Draws a snippet as a miniature page (or cell grid) inside `rect`.
        void DrawTextPreview(IRenderContext* ctx, const Rect2Di& rect,
                             const TextPreviewSnippet& snippet);

        // ===== ASYNC FOLDER STATS =====
        // Recursive folder statistics feed the selection info bar (content
        // counts / size of a selected folder) and the size-weighted views
        // (BarSize / TreeMap directory weights). Computing them walks the
        // whole subtree — up to the traversal cap — which on a big or
        // cold-cache tree takes seconds. Done synchronously it stalled the
        // UI: the click that selects a folder froze the window until the
        // walk finished, so opening such a folder took seconds. Instead the
        // stats are computed by a background worker: readers get a
        // not-yet-ready placeholder immediately, and each finished walk
        // posts one (coalesced) redraw so the info bar / layout fills in.
        struct FolderStats {
            uint64_t files = 0;      // recursive
            uint64_t folders = 0;
            uint64_t bytes = 0;
            bool capped = false;     // hit the traversal safety cap
            bool ready = false;      // background walk finished
        };
        std::map<std::string, FolderStats> folderStatsCache; // by folder path
        std::deque<std::string> statsQueue;                  // paths to walk

        // Natural aspect ratio (width / height) of raster image entries, keyed
        // by file path, read from image headers (ProbeImageDimensions, no
        // decode) for the thumbnail-row shrinking. 0 = no usable dimensions or
        // not probed yet (both mean "full height"); an entry present with 0 is
        // also the marker that its probe is already queued. Cleared on rescan.
        // Filled by the same worker as the folder stats: the grid layout asks
        // for every entry of the folder, and opening one file per image on the
        // UI thread is what made a folder of photos take seconds to appear.
        std::unordered_map<std::string, float> aspectCache;
        std::deque<std::string> aspectQueue;                 // headers to read

        // "Extra info" of a media file — image dimensions, audio / video
        // duration + codec — shown in the info bar and the thumbnail dataset
        // lines. Probing means opening the file (and for exotic image
        // containers decoding it), so like the aspects it runs on the worker:
        // EntryExtraInfo() returns the cached text, or "" after queueing the
        // probe (ready == false marks the pending slot so it queues once).
        struct MediaInfoSlot {
            std::string text;
            bool ready = false;
        };
        struct MediaProbeRequest {
            std::string path;
            std::string extension;
            bool isImage = false;    // image dimensions vs. media duration
        };
        std::unordered_map<std::string, MediaInfoSlot> mediaInfoCache;
        std::deque<MediaProbeRequest> mediaQueue;

        std::mutex statsMutex;              // guards caches/queues/generation
        std::condition_variable statsCond;
        std::thread statsWorker;
        bool statsShutdown = false;
        uint64_t statsGeneration = 0;       // bumped to drop stale results
        std::atomic<bool> statsRedrawPosted{false};
        std::atomic<bool> aspectsChanged{false};  // a probe changed a row height

        // Non-blocking: returns the cached stats, or a pending placeholder
        // (ready == false) after queueing a background walk of `path`.
        FolderStats GetFolderStats(const std::string& path);
        void StartFolderStatsWorkerLocked();
        void StopFolderStatsWorker();
        void FolderStatsWorkerMain();
        void PostFolderStatsRedraw();

        std::shared_ptr<UltraCanvasMenu> activePopupMenu;

        // Filer clipboard shared across instances (paths + cut flag).
        static std::vector<std::string> clipboardPaths;
        static bool clipboardCut;

        // ===== SCANNING =====
        // usePrefetched: serve the listing from the prefetch cache when a
        // fresh one is there (SetPath navigations). Refresh and the file-list
        // display always rescan — after a file operation the cache is exactly
        // what must not be shown.
        void ScanFolder(bool usePrefetched = false);
        // The raw listing of a real directory: one readdir plus one stat per
        // entry, no widget state touched — runs on the prefetch worker as well
        // as the UI thread. With includeHidden the hidden entries are listed
        // too (flagged), so a prefetched listing can serve either setting.
        void ScanRealDirectory(const std::string& path, bool includeHidden,
                               std::vector<FilerEntry>& out) const;
        // Fills `e` by stat-ing `path` (name, sizes, times, type info); false
        // when the path no longer exists. Used by the file-list display.
        bool StatEntryForPath(const std::string& path, FilerEntry& e) const;
        void SortEntries();
        void EnsureEffectiveSizes();   // dir weights from the async folder stats
        void ApplyEntryTypeInfo(FilerEntry& e) const;

        // ===== FOLDER LISTING PREFETCH =====
        // After a folder settles, a low-priority worker pre-scans its visible
        // subfolders (one level) so entering one serves the listing from
        // memory instead of a cold directory scan — the win is largest on
        // network volumes and spinning disks. A short grace delay keeps quick
        // successive navigations from triggering wasted scans, and each batch
        // is dropped the moment the user navigates again (generation bump).
        // Freshness on use: the cached listing must be younger than
        // kPrefetchMaxAgeSec AND the directory's mtime must be unchanged;
        // anything else falls back to a normal scan. Oversized listings are
        // scanned but not stored — the scan still warms the OS metadata
        // cache, so the real scan on entry is fast anyway.
        struct PrefetchedListing {
            std::vector<FilerEntry> entries;   // raw listing, hidden included
            std::time_t dirMtime = 0;          // dir mtime when scanned
            std::chrono::steady_clock::time_point when;  // scan time
        };
        std::map<std::string, PrefetchedListing> prefetchCache;
        std::deque<std::string> prefetchLru;   // insertion order for eviction
        size_t prefetchCachedEntries = 0;      // entries held across the cache
        std::deque<std::string> prefetchQueue; // folders to pre-scan
        std::mutex prefetchMutex;
        std::condition_variable prefetchCond;
        std::thread prefetchWorker;
        bool prefetchShutdown = false;
        uint64_t prefetchGeneration = 0;       // bumped per navigation
        bool folderPrefetchEnabled = true;

        // Refills the queue with the current listing's subfolders.
        void QueueFolderPrefetch();
        // Moves a fresh cached listing of `path` into `out` (and drops it from
        // the cache either way); false = miss or stale, scan normally.
        bool TakePrefetchedListing(const std::string& path,
                                   std::vector<FilerEntry>& out);
        void StartFolderPrefetchWorkerLocked();
        void StopFolderPrefetchWorker();
        void FolderPrefetchWorkerMain();

        // ===== LAYOUT =====
        void InvalidateFilerLayout() { layoutValid = false; }
        void EnsureLayout();
        void RecomputeLayout();
        Rect2Di ContentBounds() const;
        int  ThumbnailEdge() const;               // tile edge for the thumb modes
        // Natural width/height of a raster image entry (cached), or 0 when the
        // entry is not a measurable raster image.
        float EntryAspect(const FilerEntry& e);
        // Height an entry's thumbnail actually occupies inside an `edge`-wide
        // tile: `edge` for folders, glyph files, vectors and portrait/square or
        // not-yet-measured images; a smaller value for landscape images. Only
        // shrinks below `edge` when shrinkThumbnailRows is on.
        int  ThumbnailImageHeight(const FilerEntry& e, int edge);
        void LayoutDetails(const Rect2Di& area);
        void LayoutList(const Rect2Di& area);
        // Fills detailsColumnWidths with the built-in widths the first time it
        // is needed (and after a reset).
        void EnsureDetailsColumnWidths();
        // Spec/width indices of the Details columns visible right now — the
        // Path column only shows for a file list (search result) display.
        // Splitter drags and layout go through this so the width slots stay
        // addressed by FilerDetailsColumn even while Path is hidden.
        std::vector<size_t> VisibleDetailsSpecIndices() const;
        // Column geometry of one BarSize row. `autoValueWidth` is the measured
        // width of the widest formatted size, used when the value column is on
        // "auto"; the caller measures it because layout has no render context.
        struct BarSizeColumns {
            int nameX = 0, nameWidth = 0;
            int barX = 0, barWidth = 0;
            int valueX = 0, valueWidth = 0;
        };
        BarSizeColumns BarSizeColumnsFor(const ItemLayout& item,
                                         int autoValueWidth) const;
        // Width the BarSize value column is drawn with (auto = measured).
        int BarSizeValueWidthFor(IRenderContext* ctx) const;
        void LayoutThumbnails(const Rect2Di& area);
        void LayoutBarSize(const Rect2Di& area);
        void LayoutTreeMap(const Rect2Di& area);
        void SquarifyRange(std::vector<std::pair<size_t, double>>& weighted,
                           size_t first, size_t last, Rect2Di rect);

        bool IsHorizontal() const { return viewType == FilerViewType::List; }
        int  MaxScrollY() const;
        int  MaxScrollX() const;
        void ClampScroll();

        struct ScrollbarGeom {
            bool active = false;
            bool horizontal = false;
            Rect2Di track;
            Rect2Di thumb;
            int travel = 0;
            int maxScroll = 0;
        };
        ScrollbarGeom ScrollbarGeometry() const;
        void ScrollThumbTo(int thumbLeadPx);
        void EnsureVisible(size_t entryIndex);
        // The scroll correction of EnsureVisible against the current layout
        // (assumes EnsureLayout already ran).
        void ScrollEntryIntoView(size_t entryIndex);

        // ===== DRAWING =====
        // The folder view + chrome (has several early-return branches); the
        // public Render() calls this and then paints any modal overlay on top.
        void DrawViewContent(IRenderContext* ctx, const Rect2Di& bounds);
        void DrawDetails(IRenderContext* ctx, const Rect2Di& bounds);
        void DrawDetailsHeader(IRenderContext* ctx, const Rect2Di& bounds);
        void DrawDetailsRow(IRenderContext* ctx, const ItemLayout& item, bool hovered);
        void DrawListItem(IRenderContext* ctx, const ItemLayout& item, bool hovered);
        void DrawThumbnailTile(IRenderContext* ctx, const ItemLayout& item, bool hovered);
        void DrawBarSizeRow(IRenderContext* ctx, const ItemLayout& item, bool hovered);
        void DrawTreeMapCell(IRenderContext* ctx, const ItemLayout& item, bool hovered);
        void DrawPlaceholderView(IRenderContext* ctx, const Rect2Di& bounds,
                                 const std::string& message);
        // "Nothing to show" notice for an empty folder / file list: an
        // attention icon above the message, vertically centered in the view.
        void DrawEmptyState(IRenderContext* ctx, const Rect2Di& bounds,
                            const std::string& message);
        void DrawEntryIcon(IRenderContext* ctx, const FilerEntry& e,
                           const Rect2Di& rect,
                           ImageFitMode imageFit = ImageFitMode::Contain);
        void DrawSelectionState(IRenderContext* ctx, const ItemLayout& item, bool hovered);
        // True when the entry sits on the clipboard as a pending "cut", so the
        // view can ghost it until the move completes (Explorer-style).
        bool IsCutEntry(const FilerEntry& e) const;
        void DrawHoverIconMenu(IRenderContext* ctx, const ItemLayout& item);
        void DrawIconMenuGlyph(IRenderContext* ctx, IconMenuAction action,
                               const Rect2Di& button);
        // The selection rectangle of a running rubber-band drag.
        void DrawMarquee(IRenderContext* ctx);
        void DrawCompressDialog(IRenderContext* ctx, const Rect2Di& bounds);
        void DrawScrollbar(IRenderContext* ctx);
        // Rebuilds columnSplitters for the current view and paints the
        // dividers (hovered / dragged ones highlight like a split-pane
        // splitter). No-op for the views without columns.
        void DrawColumnSplitters(IRenderContext* ctx, const Rect2Di& bounds);
        void DrawSelectionInfoBar(IRenderContext* ctx, const Rect2Di& bounds);
        int  InfoBarHeight() const {
            return (showSelectionInfo && style.infoBarHeight > 0)
                    ? style.infoBarHeight : 0;
        }
        bool IsInInfoBar(const Point2Di& localPoint) const;
        // Selection description for the info bar: `primary` is the bold lead
        // (name / "N items selected"), `secondary` the detail run after it.
        void BuildSelectionInfoText(std::string& primary,
                                    std::string& secondary);
        // Cached per-file extra info: "1920 × 1080 px" for bitmaps,
        // "3:45 · H.264" for audio / video. Empty when nothing was probed.
        std::string EntryExtraInfo(const FilerEntry& e);
        std::string EllipsizeText(IRenderContext* ctx, const std::string& text,
                                  int maxWidth) const;
        // Plain cut to a width, without the "…" — for the cells of a
        // spreadsheet preview, where a column is only a few characters wide
        // and the ellipsis would be the only thing left of the value.
        std::string TruncateTextToWidth(IRenderContext* ctx, const std::string& text,
                                        int maxWidth) const;
        // Ellipsizes an entry's name for the space it is drawn in and records
        // whether it had to be shortened, so the hover tooltip only pops for
        // names that are actually cut off.
        std::string EllipsizeEntryName(IRenderContext* ctx, size_t entryIndex,
                                       const std::string& name, int maxWidth);
        // Breaks `text` into at most `maxLines` lines that each fit `maxWidth`,
        // for the captions of the tile-shaped views. Breaks after a separator
        // (space, -, _, .) when one sits in the back half of the line, else at
        // the exact fit — file names are frequently one long "word". When the
        // text still does not fit, the head of what is left is dropped and the
        // last line opens with "…", keeping the end of the name (extension)
        // visible. `outTruncated` reports whether anything was dropped.
        // A text that fits its lines completely is re-broken at the smallest
        // line width needing no extra line, so the lines come out near equal
        // ("CoderBox" / "compiler.png" instead of the greedily filled
        // "CoderBox compiler" / ".png") without changing the line count.
        std::vector<std::string> WrapText(IRenderContext* ctx,
                                          const std::string& text,
                                          int maxWidth, int maxLines,
                                          bool* outTruncated = nullptr) const;
        // The greedy pass behind WrapText: fills each line up to `lineWidth`
        // before breaking. Line count only depends on this pass, so the
        // layout's CaptionLinesFor uses it directly, skipping the balancing.
        std::vector<std::string> WrapTextGreedy(IRenderContext* ctx,
                                                const std::string& text,
                                                int lineWidth, int maxLines,
                                                bool* outTruncated) const;
        // WrapText for an entry name; records whether it had to be shortened,
        // so the hover tooltip only pops for names that are really cut off.
        std::vector<std::string> WrapEntryName(IRenderContext* ctx, size_t entryIndex,
                                               const std::string& name,
                                               int maxWidth, int maxLines);
        // Lines a caption needs for `name` at `maxWidth` (1..captionMaxLines).
        int  CaptionLinesFor(IRenderContext* ctx, const std::string& name,
                             int maxWidth) const;
        int  NameLineHeight() const;            // one wrapped caption line
        int  CaptionBandHeight(int lines) const;// caption strip for `lines`
        // Per-entry "the drawn name is shortened" flags, refreshed by the draw
        // pass for the items it paints (sized with `entries`).
        std::vector<uint8_t> nameTruncated;

        // Thumbnail dataset lines (Display > Dataset): the formatted values of
        // the enabled fields that apply to this entry, top to bottom.
        std::vector<std::string> DatasetLinesFor(const FilerEntry& e);
        // How many enabled dataset fields there are — the number of caption
        // lines reserved per tile so the grid stays aligned across file kinds.
        int  DatasetLineCount() const;
        int  DatasetLineHeight() const;

        // The font size the item name is drawn at in the current view — the
        // rename editor uses the same so editing matches the display exactly
        // (thumbnail / treemap captions use the small size, rows the base size).
        float ItemNameFontSize() const;

        // ===== HIT TESTING =====
        Point2Di ToContentPoint(const Point2Di& localPoint) const;
        int  ItemAt(const Point2Di& contentPoint) const;   // entry index or -1
        // True when a content-space point falls on the item's name text (not its
        // icon) — a click there on the already-selected entry starts an inline
        // rename (Windows style, after a short delay).
        bool IsOnItemName(const ItemLayout& item, const Point2Di& contentPoint) const;
        int  IconMenuActionAt(const Point2Di& localPoint, size_t& outEntry) const;
        int  DetailsHeaderColumnAt(const Point2Di& localPoint) const;
        // Splitter under a widget-local point — its own index (the column it
        // belongs to, see ColumnSplitterHit::index), or -1. The grab area is
        // the painted strip grown by splitterHitMargin on both sides.
        int  ColumnSplitterAt(const Point2Di& localPoint) const;

        // ===== INTERACTION =====
        // Column splitter drag, mirroring UltraCanvasSplitPane's splitter:
        // the press snapshots the two adjacent column widths and every move
        // re-splits that pair by the pointer delta.
        void BeginColumnSplitterDrag(int index, const Point2Di& localPoint);
        void UpdateColumnSplitterDrag(const Point2Di& localPoint);
        void EndColumnSplitterDrag();
        void NotifyColumnWidthsChanged();
        // Shows / hides the hover tooltip for the icon-menu button or the
        // truncated item name under the cursor.
        void UpdateHoverTooltip(const UCEvent& event, const Point2Di& localPoint);
        void HideHoverTooltip();

        void HandleItemClick(int index, bool ctrl, bool shift);
        void ActivateEntry(size_t index);          // double-click / Enter
        void OpenContextMenu(const Point2Di& localPoint);
        std::vector<size_t> SelectionOrItem(int index) const;
        void SelectionToClipboard(bool cut);
        void EntriesToClipboard(const std::vector<FilerEntry>& targets, bool cut);
        // What an action aimed at one entry operates on: the whole selection
        // when that entry is part of it, otherwise just the entry itself. The
        // hover icon-menu buttons use this so pressing one never has to select
        // the entry first (which would re-target an attached preview).
        std::vector<FilerEntry> SelectionOrEntry(size_t entryIndex) const;
        // Delete an explicit set (confirmDelete / the built-in dialog still
        // gate it). DeleteSelection() is this over the selected entries.
        void DeleteEntries(const std::vector<FilerEntry>& victims);
        // Entry that should inherit the selection once `victims` are gone:
        // the first survivor after them, else the last one before them.
        // Empty when the folder holds nothing else.
        std::string NeighbourPathAfterRemoval(
                const std::vector<FilerEntry>& victims) const;
        // Paste fallback when the clipboard holds no files: writes the raw
        // clipboard data (an image or text copied in another program) as a
        // new file into the current folder. False = nothing pastable there.
        bool PasteClipboardDataAsFile();
        // ===== DRAGGING ENTRIES =====
        // Picks up the pressed item (or the selection containing it) and runs
        // the in-widget drag; the pointer stays captured for its duration.
        void BeginItemDrag(const Point2Di& localPoint);
        // Tracks the cursor: highlights the folder under it, keeps the badge
        // following it across the whole window and hands the drag over to the
        // native OS drag once the cursor leaves the window.
        void UpdateItemDrag(const Point2Di& localPoint);
        // Drop: files land in the highlighted folder (moved, or copied when
        // `copy`); a release over another element of the window hands them to
        // it, and a drop on nothing simply ends the drag.
        void FinishItemDrag(const Point2Di& localPoint, bool copy);
        void CancelItemDrag();             // Escape / lost pointer
        void EndDragGesture();             // clears the armed / running state
        // Moves (or copies) `paths` into `destDir`, skipping sources that are
        // already there and folders dropped into themselves. Rescans on change.
        void DropPathsInto(const std::vector<std::string>& paths,
                           const std::string& destDir, bool copy);
        // Folder entry under a widget-local point that the running drag may be
        // dropped on (never one of the dragged items), or -1.
        int  DragDropFolderAt(const Point2Di& localPoint) const;
        // Drop-folder highlight (the badge is drawn by the window overlay).
        void DrawDragFeedback(IRenderContext* ctx, const Rect2Di& bounds);
        // ----- drag badge (window overlay, so it survives the widget border) -
        // Widget-local → window coordinates.
        Point2Di ToWindowPoint(const Point2Di& localPoint) const;
        // Is a window-coordinate point still on the window's client area?
        bool IsInsideWindow(const Point2Di& windowPoint) const;
        // Measures dragLabel into dragBadgeSize (once per gesture).
        void MeasureDragBadge();
        // Badge rectangle for a cursor position, kept inside the window.
        Rect2Di DragBadgeRect(const Point2Di& windowPoint) const;
        // (Re)places the badge overlay on the window / takes it down again.
        void UpdateDragOverlay(const Point2Di& localPoint);
        void HideDragOverlay();
        // Paints the badge; `badgeRect` is in window coordinates.
        void DrawDragBadge(IRenderContext* ctx, const Rect2Di& badgeRect);
        // Release over another element of the same window: the files are
        // offered to it as a Drop event, exactly like an external drop.
        void DeliverInWindowDrop(const Point2Di& windowPoint,
                                 const std::vector<std::string>& paths);
        // Starts the native OS drag of the current selection (drag-out).
        bool StartNativeDragOfSelection();
        // Starts the native OS drag of an explicit file set.
        bool StartNativeDragOfPaths(const std::vector<std::string>& paths);
        // Files dropped onto the widget from other applications / windows are
        // copied into the current folder (sources already there are skipped).
        void AcceptDroppedFiles(const std::vector<std::string>& paths);
        // Commit / abandon the inline rename. `restoreFocus` gives the
        // keyboard focus back to the widget after the editor is removed —
        // the Enter / Escape / programmatic paths want that; the focus-loss
        // path must not steal the focus back from whatever just took it.
        void CommitRename(bool restoreFocus = true);
        void CancelRename(bool restoreFocus = true);
        // The name-field rectangle the rename editor covers (content space).
        Rect2Di RenameFieldRect(const ItemLayout& item) const;
        // Moves the rename editor onto the renamed item (widget-local, scroll
        // applied); called every frame while a rename runs so the editor
        // tracks scrolling and relayouts.
        void PositionRenameInput();
        // Removes the editor child (see CommitRename for restoreFocus).
        void DestroyRenameInput(bool restoreFocus);
        // Deferred Windows-style rename (single click on the selected entry's
        // name): arm the delay timer on release / drop the pending rename.
        void ArmPendingRenameTimer();
        void CancelPendingRename();
        // ===== RUBBER-BAND SELECTION =====
        Rect2Di MarqueeRect() const;       // normalized, content space
        // Tracks the moving corner: auto-scrolls at the viewport edge and
        // reselects the entries the rectangle touches.
        void UpdateMarquee(const Point2Di& localPoint);
        void FinishMarquee();              // release: keep the result
        void CancelMarquee();              // Escape: restore the old selection
        void FireSelectionChanged();
        // Reports a user-made change to onFolderModified. An empty argument
        // means the displayed folder (skipped in file-list mode, where the
        // displayed folder is not where the change landed).
        void NotifyFolderModified(const std::string& folderPath = "");
        void ReportError(const std::string& message);
        std::string UniqueChildPath(const std::string& baseName) const;
        // Same, but in an arbitrary folder (drop target of a drag).
        static std::string UniquePathIn(const std::string& folder,
                                        const std::string& baseName);

        // ===== PASTE CONFLICTS =====
        // One paste in flight: the sources not yet processed and the choices
        // the conflict dialog collected so far.
        struct PendingPaste {
            std::string folder;
            std::vector<std::string> sources;
            size_t next = 0;
            bool cut = false;
            PasteConflictAction action = PasteConflictAction::KeepBoth;
            bool applyToAll = false;   // reuse `action` for later conflicts
            bool changed = false;
            // Failure handling: skip every failing entry / grant each one
            // silent retry, and the per-entry state they consume.
            bool skipFailedForAll = false;
            bool retryFailedForAll = false;
            bool currentRetried = false;
            PasteConflictAction currentAction = PasteConflictAction::KeepBoth;
            std::function<void(bool changed)> onDone;
        };
        std::unique_ptr<PendingPaste> pendingPaste;

        // Processes sources until a name conflict or a failure needs a
        // dialog (which resumes it) or the queue is done (FinishPendingPaste).
        void ContinuePendingPaste();
        void FinishPendingPaste();
        // Copy / move one source into the pending paste's folder, honoring
        // `action` when the name is taken. False = failed, with the reason.
        bool PasteOneEntry(const std::string& src, PasteConflictAction action,
                          std::string& whyFailed);
        // Pastes the entry at `next` (with the failure policy applied) and
        // advances. False = a problem dialog was opened and resumes the queue.
        bool PasteCurrentAndAdvance(PasteConflictAction action);
        // The "already exists" dialog: exclusive Keep both / Replace / Skip
        // switches, a "do this for all remaining conflicts" switch, and
        // Continue / Cancel buttons.
        void ShowPasteConflictDialog(const std::string& src);
        // The paste failure dialog: Try again / Skip, like a failed delete's.
        void ShowPasteProblemDialog(const std::string& src,
                                    const std::string& reason);

        // A two-choice problem dialog in the house style: exclusive switches
        // for proceed (try again / delete anyway / …) vs. skip, a "do this
        // for all" scope switch, and Continue / Cancel buttons. False = modal
        // dialogs are unavailable and the caller must fall back.
        bool ShowProceedSkipDialog(
                DialogConfig& cfg,
                const std::string& proceedLabel, const std::string& skipLabel,
                const std::string& allLabel, bool proceedDefault,
                std::function<void(bool proceed, bool all)> onContinue,
                std::function<void()> onCancel);

        // ===== DELETE PROBLEMS =====
        // What the delete-problem dialog decides for the entry it is about:
        // delete it after all (a write-protected entry, or another try after
        // a failure) or leave it alone.
        enum class DeleteProblemAction { Delete, Skip };

        // One delete in flight: the real-filesystem victims not yet processed
        // and the choices the problem dialogs collected so far.
        struct PendingDelete {
            std::vector<FilerEntry> victims;
            size_t next = 0;
            // "Do this for all" answers, kept per dialog flavor.
            bool protectedForAll = false;          // write-protected entries…
            DeleteProblemAction protectedAction = DeleteProblemAction::Skip;
            bool skipFailedForAll = false;         // skip every failing entry
            bool retryFailedForAll = false;        // one silent retry each
            // The entry at `next`: its write-protected question, once
            // answered, and whether its silent retry has been spent.
            bool currentDecided = false;
            DeleteProblemAction currentAction = DeleteProblemAction::Skip;
            bool currentRetried = false;
            // Folders that really lost an entry, for onFolderModified.
            std::vector<std::string> modifiedFolders;
        };
        std::unique_ptr<PendingDelete> pendingDelete;

        // Processes victims until a problem needs a dialog (which resumes it)
        // or the queue is done (FinishPendingDelete).
        void ContinuePendingDelete();
        void FinishPendingDelete();
        void AdvancePendingDelete();   // to the next victim, forgetting the
                                       // per-entry decisions
        // The problem dialog, in two flavors: a write-protected (locked)
        // entry before the attempt ("Delete it anyway" / "Skip"), or a
        // failed delete ("Try again" / "Skip" with the failure reason).
        void ShowDeleteProblemDialog(const FilerEntry& entry,
                                     bool writeProtected,
                                     const std::string& reason);

        // Executable script activated: Run / Open (view it) / Cancel.
        void ShowRunOrOpenDialog(const FilerEntry& e);

        // ===== RENAME CONFLICTS =====
        // The actual rename plus the selection hand-over and refresh.
        void PerformRename(const std::string& oldPath,
                           const std::string& targetPath);
        // Renaming onto an existing name asks: Replace / Cancel.
        void ShowRenameReplaceDialog(const std::string& oldPath,
                                     const std::string& targetPath);

        // ===== EXTRACT CONFLICTS =====
        // One ExtractSelection in flight: the archives not yet extracted and
        // the choice the conflict dialog collected.
        struct PendingExtract {
            std::vector<FilerEntry> archives;
            size_t next = 0;
            PasteConflictAction action = PasteConflictAction::KeepBoth;
            bool applyToAll = false;
            bool changed = false;
        };
        std::unique_ptr<PendingExtract> pendingExtract;

        // Processes archives until a taken destination folder name needs the
        // dialog (which resumes it) or the queue is done.
        void ContinuePendingExtract();
        void FinishPendingExtract();
        // Extracts the archive at `next` (KeepBoth renames the destination,
        // Replace merges into the existing folder, Skip does not extract).
        void ExtractCurrentAndAdvance(PasteConflictAction action);
        // The "folder already exists" dialog: Keep both / Extract into the
        // existing folder / Skip this archive.
        void ShowExtractConflictDialog(const FilerEntry& archive);

        // Selected entries, or the whole folder when nothing is selected —
        // what Compress / Print / Extras operate on.
        std::vector<FilerEntry> SelectionOrAll() const;

        // ===== COMPRESS / EXTRACT DIALOG (modal in-widget overlay) =====
        // Shown when a format is picked from the context menu's "Compress"
        // submenu, and (in extract mode) by the context menu's "Extract". It
        // previews the archive's file-type icon, lets the name be edited —
        // the archive's base name when compressing, the destination folder
        // name when extracting — and shows the destination folder as smaller
        // text. The icon can be dragged onto any folder in the view to
        // retarget that destination — which is why this is an in-widget
        // overlay rather than a separate modal window (a top-level modal
        // would block the folders behind it).
        struct CompressDialogState {
            bool        active = false;
            // Extract mode: the same panel unpacks the selected archives into
            // a folder named by the editor instead of packing the selection.
            bool        extractMode = false;
            std::string extension;      // archive extension, e.g. "zip", "tar.gz"
                                        // (in extract mode: the source archive's
                                        // suffix, driving the icon tag only)
            std::string formatLabel;    // human label, e.g. "TAR + gzip"; in
                                        // extract mode the archive name / count
            std::string nameBuffer;     // base name (no extension), kept in sync
                                        // with the editor below
            std::string destDir;        // folder the archive is written to
            std::vector<std::string> sourcePaths;  // captured at open time

            // Layout, recomputed each frame (widget-local coordinates).
            Rect2Di panel;
            Rect2Di iconRect;
            Rect2Di nameRect;      // whole name row (editor + extension label)
            Rect2Di nameEditRect;  // the editor alone; measured while drawing
            Rect2Di okRect;
            Rect2Di cancelRect;

            // Icon drag-to-folder interaction.
            bool     draggingIcon = false;
            Point2Di dragPos;               // cursor while dragging (widget-local)
            int      dropFolderIndex = -1;  // folder entry highlighted under the icon

        };
        CompressDialogState compressDlg;

        // The name field is a real UltraCanvasTextInput child, exactly like the
        // inline rename editor: a hand-rolled key handler could only append and
        // backspace at the end, and it went deaf the moment anything else in
        // the window took the keyboard focus.
        std::shared_ptr<UltraCanvasTextInput> compressNameInput;
        // Compress / Cancel are UltraCanvasButton children too, so they carry
        // their own hover, press and disabled painting instead of a private
        // hover flag and a bespoke painter.
        std::shared_ptr<UltraCanvasButton> compressOkButton;
        std::shared_ptr<UltraCanvasButton> compressCancelButton;
        std::shared_ptr<UltraCanvasButton> MakeCompressButton(
                const std::string& identifier, const std::string& label,
                bool primary, std::function<void()> action);
        void DestroyCompressButtons();
        // While the dialog is up it also claims the window's KeyDown stream, so
        // a keystroke reaches the editor even when the focus sits elsewhere.
        bool compressKeyFilterInstalled = false;
        std::string CompressKeyFilterId() const;
        void InstallCompressKeyFilter();
        void RemoveCompressKeyFilter();
        bool HandleCompressFilteredKey(const UCEvent& event);

        void OpenCompressDialog(const std::string& extension,
                                const std::string& formatLabel);
        // Shared dialog chrome (name editor, OK/Cancel buttons, key filter);
        // the state fields must be filled before this is called.
        void OpenArchiveDialogChrome(const std::string& defaultName,
                                     const std::string& okLabel);
        void LayoutCompressDialog(const Rect2Di& bounds);
        void PositionCompressNameInput();
        void DestroyCompressNameInput();
        bool HandleCompressDialogEvent(const UCEvent& event);
        void CommitCompressDialog();
        void CloseCompressDialog();
        // Folder entry index under a widget-local point (ignoring the panel), or
        // -1 when the point is over the panel or not over a folder.
        int  FolderIndexAtLocal(const Point2Di& localPoint) const;
        // Short uppercase tag drawn on the archive icon for a given extension.
        static std::string ArchiveIconTag(const std::string& extension);

        // ===== DELETE CONFIRMATION =====
        // Actually removes the given entries from disk (no confirmation).
        void PerformDeletion(const std::vector<FilerEntry>& victims);
        // Shows the built-in modal confirmation dialog (used when no
        // confirmDelete veto is installed). Deletes on confirm. When a folder is
        // among the victims, a preview of its first entries (with thumbnails) is
        // shown so the user sees what is about to be lost.
        void ShowDeleteConfirmation(const std::vector<FilerEntry>& victims);
    };

    // ===== FACTORY =====
    inline std::shared_ptr<UltraCanvasFilerWidget> CreateFilerWidget(
            const std::string& identifier, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasFilerWidget>(identifier, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasFilerWidget> CreateFilerWidget(
            const std::string& identifier, const std::string& folderPath,
            float x, float y, float w, float h) {
        auto filer = std::make_shared<UltraCanvasFilerWidget>(identifier, x, y, w, h);
        filer->SetPath(folderPath);
        return filer;
    }

} // namespace UltraCanvas
