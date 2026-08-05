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
// complete.
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
// Version: 1.7.0
// Last Modified: 2026-08-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTimer.h"
#include <atomic>
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
        Audio,
        Video,
        Document,
        Text,
        Spreadsheet,
        Archive,
        Executable,
        Other
    };

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
        void ClearSelection();
        void SelectAll();

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
        void DeleteSelection();    // gated by confirmDelete when set
        void DuplicateSelection(); // copy alongside with a unique name
        void StartRename(size_t entryIndex);   // inline rename editor
        // Pack the selection into an archive alongside it. The extension picks
        // the format (e.g. "zip", "7z", "tar", "tar.gz", "tar.bz2", "tar.xz",
        // "tar.zst"); defaults to a .zip archive.
        void CompressSelection(const std::string& extension = "zip");
        void ExtractSelection();   // unpack selected archives alongside
        static bool ClipboardHasContent();

        // "New >" document kinds (replaces the default seven).
        void SetNewDocumentTypes(const std::vector<FilerNewDocumentType>& types);
        void CreateNewDocument(const FilerNewDocumentType& type);

        // "Open with >" applications.
        void AddOpenWithApp(const FilerOpenWithApp& app);
        void ClearOpenWithApps();

        // ===== CALLBACKS =====
        std::function<void(const FilerEntry&)> onFileActivated;   // double-click / Enter on a file
        std::function<void(const std::string&)> onPathChanged;    // after SetPath / folder entered
        std::function<void(const std::vector<FilerEntry>&)> onSelectionChanged;
        // After every (re)scan of the shown folder — the listing changed
        // (entries created, deleted, moved in or out, renamed). Hosts refresh
        // their folder description (item counts, status bar) from it. The
        // selection survives a rescan on the files that are still there.
        std::function<void()> onFolderRefreshed;
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
        bool showHiddenFiles = false;
        bool hoverIconMenu = true;
        bool showOpenPathItem = false;
        std::string openPathItemLabel = "Open Path";
        bool showSelectionInfo = true;
        bool shrinkThumbnailRows = true;
        bool columnResizeEnabled = true;
        bool nameTooltips = true;
        // Bitmask of FilerDatasetField values drawn under thumbnail captions.
        uint32_t datasetFields = 0;
        FilerStyle style;

        // Natural aspect ratio (width / height) of raster image entries, keyed
        // by file path, filled lazily from image headers (ProbeImageDimensions,
        // no decode) for the thumbnail-row shrinking. 0 = probed but has no
        // usable dimensions (treated as full-height); absent = not yet probed.
        // Cleared on rescan.
        std::unordered_map<std::string, float> aspectCache;

        std::vector<size_t> selection;            // indices into `entries`
        int lastClickedIndex = -1;                // anchor for shift-range select
        int hoveredIndex = -1;

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

        // Inline rename editor.
        int renamingIndex = -1;
        std::string renameBuffer;
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
        //   * once the cursor leaves the widget the same set is handed to the
        //     native OS drag (window->StartNativeFileDrag), where the drop
        //     target performs the copy / move and a move refreshes this view.
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
        // Press on an already-selected item keeps the (multi-)selection so it
        // can be dragged; the usual "select only this item" collapse is
        // deferred to the release and recorded here (-1 = nothing deferred).
        int dragCollapseIndex = -1;
        // Press on an item outside the selection: selecting it is deferred to
        // the release the same way, so a press that turns into a drag leaves
        // the selection (and any preview fed by it) untouched.
        int pendingSelectIndex = -1;

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
        std::mutex statsMutex;              // guards cache/queue/generation
        std::condition_variable statsCond;
        std::thread statsWorker;
        bool statsShutdown = false;
        uint64_t statsGeneration = 0;       // bumped to drop stale results
        std::atomic<bool> statsRedrawPosted{false};

        // Non-blocking: returns the cached stats, or a pending placeholder
        // (ready == false) after queueing a background walk of `path`.
        FolderStats GetFolderStats(const std::string& path);
        void StartFolderStatsWorkerLocked();
        void StopFolderStatsWorker();
        void FolderStatsWorkerMain();
        void PostFolderStatsRedraw();

        mutable std::map<std::string, std::string> mediaInfoCache;  // path -> extra info

        std::shared_ptr<UltraCanvasMenu> activePopupMenu;

        // Filer clipboard shared across instances (paths + cut flag).
        static std::vector<std::string> clipboardPaths;
        static bool clipboardCut;

        // ===== SCANNING =====
        void ScanFolder();
        // Fills `e` by stat-ing `path` (name, sizes, times, type info); false
        // when the path no longer exists. Used by the file-list display.
        bool StatEntryForPath(const std::string& path, FilerEntry& e) const;
        void SortEntries();
        void EnsureEffectiveSizes();   // dir weights from the async folder stats
        void ApplyEntryTypeInfo(FilerEntry& e) const;

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
        void DrawRenameEditor(IRenderContext* ctx, const ItemLayout& item);
        void DrawCompressDialog(IRenderContext* ctx, const Rect2Di& bounds);
        void DrawDialogButton(IRenderContext* ctx, const Rect2Di& rect,
                              const std::string& label, bool primary, bool hovered);
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
        std::string EntryExtraInfo(const FilerEntry& e) const;
        std::string EllipsizeText(IRenderContext* ctx, const std::string& text,
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
        std::vector<std::string> WrapText(IRenderContext* ctx,
                                          const std::string& text,
                                          int maxWidth, int maxLines,
                                          bool* outTruncated = nullptr) const;
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
        std::vector<std::string> DatasetLinesFor(const FilerEntry& e) const;
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
        // Paste fallback when the clipboard holds no files: writes the raw
        // clipboard data (an image or text copied in another program) as a
        // new file into the current folder. False = nothing pastable there.
        bool PasteClipboardDataAsFile();
        // ===== DRAGGING ENTRIES =====
        // Picks up the pressed item (or the selection containing it) and runs
        // the in-widget drag; the pointer stays captured for its duration.
        void BeginItemDrag(const Point2Di& localPoint);
        // Tracks the cursor: highlights the folder under it and hands the drag
        // over to the native OS drag once the cursor leaves the widget.
        void UpdateItemDrag(const Point2Di& localPoint);
        // Drop: files land in the highlighted folder (moved, or copied when
        // `copy`); a drop anywhere else is a no-op.
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
        // Drop-folder highlight + the badge that follows the cursor.
        void DrawDragFeedback(IRenderContext* ctx, const Rect2Di& bounds);
        // Starts the native OS drag of the current selection (drag-out).
        bool StartNativeDragOfSelection();
        // Starts the native OS drag of an explicit file set.
        bool StartNativeDragOfPaths(const std::vector<std::string>& paths);
        // Files dropped onto the widget from other applications / windows are
        // copied into the current folder (sources already there are skipped).
        void AcceptDroppedFiles(const std::vector<std::string>& paths);
        void CommitRename();
        void CancelRename();
        // Deferred Windows-style rename (single click on the selected entry's
        // name): arm the delay timer on release / drop the pending rename.
        void ArmPendingRenameTimer();
        void CancelPendingRename();
        bool HandleRenameKey(const UCEvent& event);
        void FireSelectionChanged();
        void ReportError(const std::string& message);
        std::string UniqueChildPath(const std::string& baseName) const;
        // Same, but in an arbitrary folder (drop target of a drag).
        static std::string UniquePathIn(const std::string& folder,
                                        const std::string& baseName);

        // Selected entries, or the whole folder when nothing is selected —
        // what Compress / Print / Extras operate on.
        std::vector<FilerEntry> SelectionOrAll() const;

        // ===== COMPRESS DIALOG (modal in-widget overlay) =====
        // Shown when a format is picked from the context menu's "Compress"
        // submenu. It previews the archive's file-type icon, lets the name be
        // edited, and shows the destination folder as smaller text. The icon can
        // be dragged onto any folder in the view to retarget that destination —
        // which is why this is an in-widget overlay rather than a separate modal
        // window (a top-level modal would block the folders behind it).
        struct CompressDialogState {
            bool        active = false;
            std::string extension;      // archive extension, e.g. "zip", "tar.gz"
            std::string formatLabel;    // human label, e.g. "TAR + gzip"
            std::string nameBuffer;     // editable base name (no extension)
            std::string destDir;        // folder the archive is written to
            std::vector<std::string> sourcePaths;  // captured at open time

            // Layout, recomputed each frame (widget-local coordinates).
            Rect2Di panel;
            Rect2Di iconRect;
            Rect2Di nameRect;
            Rect2Di okRect;
            Rect2Di cancelRect;

            // Icon drag-to-folder interaction.
            bool     draggingIcon = false;
            Point2Di dragPos;               // cursor while dragging (widget-local)
            int      dropFolderIndex = -1;  // folder entry highlighted under the icon

            bool     nameFocused = true;
            bool     okHover = false;
            bool     cancelHover = false;
        };
        CompressDialogState compressDlg;

        void OpenCompressDialog(const std::string& extension,
                                const std::string& formatLabel);
        void LayoutCompressDialog(const Rect2Di& bounds);
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
