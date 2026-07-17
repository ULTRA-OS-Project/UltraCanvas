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
// Self-rendered like UltraCanvasAlbum so large folders stay cheap.
// Version: 1.1.0
// Last Modified: 2026-07-16
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <string>
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
        float fontSize        = 13.0f;
        float smallFontSize   = 11.0f;

        int detailsRowHeight  = 24;      // details / bar-size row height
        int listRowHeight     = 22;      // list mode row height
        int listColumnWidth   = 220;     // list mode column width
        int outerPadding      = 8;
        int tileGap           = 10;      // thumbnail grid gap
        int captionHeight     = 20;      // thumbnail caption strip

        // Thumbnail tile edge for the four thumbnail view types.
        int thumbnailSmall     = 72;
        int thumbnailMedium    = 110;
        int thumbnailBig       = 170;
        int thumbnailMaximized = 260;

        int iconMenuButtonSize = 20;     // hover icon-menu button edge
        int infoBarHeight      = 26;     // selection info bar under the entries
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

        // The small icon menu (Copy / Cut / Rename / Delete) shown at the top
        // right of the hovered item. Also toggled by the Display > Icon-Menu
        // context-menu checkbox.
        void SetHoverIconMenuEnabled(bool enabled);
        bool IsHoverIconMenuEnabled() const { return hoverIconMenu; }

        // Show the "Open Path" context-menu item (useful when the widget
        // displays a search result rather than a plain folder).
        void SetOpenPathMenuItemVisible(bool visible) { showOpenPathItem = visible; }

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

        void SetStyle(const FilerStyle& s);
        const FilerStyle& GetStyle() const { return style; }

        // ===== DATA ACCESS =====
        const std::vector<FilerEntry>& GetEntries() const { return entries; }
        std::vector<FilerEntry> GetSelectedEntries() const;
        void ClearSelection();
        void SelectAll();

        // ===== FILE OPERATIONS (also wired to the context / icon menus) =====
        void CopySelection();      // to the filer clipboard (shared by instances)
        void CutSelection();
        void Paste();              // into the current folder
        void DeleteSelection();    // gated by confirmDelete when set
        void DuplicateSelection(); // copy alongside with a unique name
        void StartRename(size_t entryIndex);   // inline rename editor
        void CompressSelection();  // pack the selection into a .zip alongside
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
        std::function<void(FilerViewType)> onViewTypeChanged;
        std::function<void(FilerSortField, bool)> onSortChanged;

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
        bool showSelectionInfo = true;
        FilerStyle style;

        std::vector<size_t> selection;            // indices into `entries`
        int lastClickedIndex = -1;                // anchor for shift-range select
        int hoveredIndex = -1;

        // Hover icon-menu tooltip tracking: the action / entry currently under
        // the cursor, so the tooltip is shown once when the cursor enters a
        // button and hidden when it leaves.
        int    hoveredIconAction = -1;
        size_t hoveredIconEntry  = 0;

        std::vector<FilerNewDocumentType> newDocumentTypes;
        std::vector<FilerOpenWithApp> openWithApps;

        // Computed per-entry geometry (content space, before scroll offset).
        struct ItemLayout {
            Rect2Di rect;        // whole row / tile / treemap cell
            Rect2Di imageRect;   // icon / thumbnail sub-region
            size_t  entryIndex;
        };
        std::vector<ItemLayout> items;
        bool layoutValid = false;
        int  contentWidth = 0;
        int  contentHeight = 0;
        int  lastAreaW = -1, lastAreaH = -1;
        bool effectiveSizesValid = false;         // recursive dir sizes computed

        // Details-view columns (recomputed with the layout).
        struct DetailsColumn {
            FilerSortField field;
            std::string title;
            int x = 0, width = 0;
            bool rightAligned = false;
            bool sortable = true;
        };
        std::vector<DetailsColumn> detailsColumns;
        int detailsHeaderHeight = 26;

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

        // Selection info bar caches, computed on demand and cleared on rescan.
        struct FolderStats {
            uint64_t files = 0;      // recursive
            uint64_t folders = 0;
            uint64_t bytes = 0;
            bool capped = false;     // hit the traversal safety cap
        };
        mutable std::map<std::string, FolderStats> folderStatsCache;
        mutable std::map<std::string, std::string> mediaInfoCache;  // path -> extra info

        std::shared_ptr<UltraCanvasMenu> activePopupMenu;

        // Filer clipboard shared across instances (paths + cut flag).
        static std::vector<std::string> clipboardPaths;
        static bool clipboardCut;

        // ===== SCANNING =====
        void ScanFolder();
        void SortEntries();
        void EnsureEffectiveSizes();              // recursive dir sizes (capped)
        void ApplyEntryTypeInfo(FilerEntry& e) const;

        // ===== LAYOUT =====
        void InvalidateFilerLayout() { layoutValid = false; }
        void EnsureLayout();
        void RecomputeLayout();
        Rect2Di ContentBounds() const;
        int  ThumbnailEdge() const;               // tile edge for the thumb modes
        void LayoutDetails(const Rect2Di& area);
        void LayoutList(const Rect2Di& area);
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
                           const Rect2Di& rect);
        void DrawSelectionState(IRenderContext* ctx, const ItemLayout& item, bool hovered);
        void DrawHoverIconMenu(IRenderContext* ctx, const ItemLayout& item);
        void DrawIconMenuGlyph(IRenderContext* ctx, IconMenuAction action,
                               const Rect2Di& button);
        void DrawRenameEditor(IRenderContext* ctx, const ItemLayout& item);
        void DrawScrollbar(IRenderContext* ctx);
        void DrawSelectionInfoBar(IRenderContext* ctx, const Rect2Di& bounds);
        int  InfoBarHeight() const {
            return (showSelectionInfo && style.infoBarHeight > 0)
                    ? style.infoBarHeight : 0;
        }
        bool IsInInfoBar(const Point2Di& localPoint) const;
        // Selection description for the info bar: `primary` is the bold lead
        // (name / "N items selected"), `secondary` the detail run after it.
        void BuildSelectionInfoText(std::string& primary,
                                    std::string& secondary) const;
        const FolderStats& GetFolderStats(const std::string& path) const;
        // Cached per-file extra info: "1920 × 1080 px" for bitmaps,
        // "3:45 · H.264" for audio / video. Empty when nothing was probed.
        std::string EntryExtraInfo(const FilerEntry& e) const;
        std::string EllipsizeText(IRenderContext* ctx, const std::string& text,
                                  int maxWidth) const;

        // ===== HIT TESTING =====
        Point2Di ToContentPoint(const Point2Di& localPoint) const;
        int  ItemAt(const Point2Di& contentPoint) const;   // entry index or -1
        int  IconMenuActionAt(const Point2Di& localPoint, size_t& outEntry) const;
        int  DetailsHeaderColumnAt(const Point2Di& localPoint) const;

        // ===== INTERACTION =====
        void HandleItemClick(int index, bool ctrl, bool shift);
        void ActivateEntry(size_t index);          // double-click / Enter
        void OpenContextMenu(const Point2Di& localPoint);
        std::vector<size_t> SelectionOrItem(int index) const;
        void SelectionToClipboard(bool cut);
        void CommitRename();
        void CancelRename();
        bool HandleRenameKey(const UCEvent& event);
        void FireSelectionChanged();
        void ReportError(const std::string& message);
        std::string UniqueChildPath(const std::string& baseName) const;

        // Selected entries, or the whole folder when nothing is selected —
        // what Compress / Print / Extras operate on.
        std::vector<FilerEntry> SelectionOrAll() const;

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
