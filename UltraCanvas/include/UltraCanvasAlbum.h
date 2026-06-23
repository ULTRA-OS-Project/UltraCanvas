// include/UltraCanvasAlbum.h
// Photo / video / music album widget: a self-rendered media grid with selectable
// layout designs, per-item crop / zoom / stretch fitting, action icons and
// visitor / user-edit / admin modes. A companion to UltraCanvasSlideshow.
// Version: 1.3.0
// Last Modified: 2026-06-21
// V1.3.0: AlbumItem::linkIconPath draws an icon (e.g. a YouTube badge) before
//   the subtitle link text.
// V1.2.0: AlbumActionIconBackground (round / square / rounded-square) plus
//   actionIconBgColor for the action-icon backing.
// V1.1.0: AlbumActionAnchor (8 image / text-block corners) for action-icon
//   placement; AlbumConfig::imageCornerRadius for square / rounded image
//   corners independent of the tile frame; AlbumItem::link + onLinkClicked for
//   a clickable subtitle (link-styled) row.
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasMenu;

    // ===== KIND OF MEDIA AN ITEM REPRESENTS =====
    // Photos draw their own bitmap; video / music draw a thumbnail (cover art /
    // poster frame) plus a type badge so the album can mix all three.
    enum class AlbumMediaType {
        Photo,
        Video,
        Music
    };

    // ===== WHO IS LOOKING AT THE ALBUM =====
    // The mode gates which actions are offered and whether items can be
    // reordered / removed. The same album instance can be flipped between modes
    // at runtime (e.g. an owner toggling an "Edit" button).
    enum class AlbumMode {
        Display,   // visitor — view only, click opens / plays an item
        UserEdit,  // owner   — reorder, remove, set focus point, owner actions
        Admin      // admin   — everything in UserEdit plus moderation actions
    };

    // ===== ALBUM LAYOUT DESIGNS =====
    // The catalogue surveyed for common photo-album / gallery products:
    //   UniformGrid — fixed columns of equal cells (Google Photos squares,
    //                 classic CMS galleries). Cheapest, most predictable.
    //   Justified   — rows of a common height, each item keeps its aspect ratio
    //                 so widths vary and the row edge stays flush (Flickr,
    //                 Google Photos "comfortable", Lightroom).
    //   Masonry     — fixed column width, items flow into the shortest column so
    //                 heights vary and there are no gaps (Pinterest, Unsplash).
    //   Mosaic      — a featured hero spans a 2x2 block, the rest fill a grid
    //                 around it (magazine / "featured cover" album front pages).
    //   Filmstrip   — a single horizontally-scrolling row (music cover shelves,
    //                 story reels, "more like this" strips).
    //   Cards       — a uniform grid where every tile is a captioned card with a
    //                 frame and shadow (album/playlist browsers, store fronts).
    enum class AlbumLayout {
        UniformGrid,
        Justified,
        Masonry,
        Mosaic,
        Filmstrip,
        Cards
    };

    // ===== HOW ITEM SIZE IS DRIVEN =====
    enum class AlbumSizingMode {
        ItemsPerRow,  // caller fixes the column count; cell size is derived
        ImageSize     // caller fixes a target image size; column count is derived
    };

    // ===== HOW A SINGLE IMAGE IS FITTED INTO ITS CELL =====
    // Crop / Zoom keep the aspect ratio; Stretch distorts; Fit letterboxes.
    enum class AlbumImageDisplay {
        Crop,     // cover the cell and crop the overflow around the focus point
        Zoom,     // like Crop but zoomed in further (config.zoomFactor) on focus
        Stretch,  // stretch to the exact cell, distorting aspect ratio
        Fit       // contain the whole image, letterboxing the remainder
    };

    // ===== WHEN / HOW PER-ITEM ACTION ICONS APPEAR =====
    // (AlwaysVisible rather than "Always" because X11/Xlib.h #defines Always.)
    enum class AlbumActionDisplay {
        AlwaysVisible,  // action buttons are always painted on the tile
        OnHover,        // action buttons fade in only while the tile is hovered
        ContextMenu,    // actions live in a right-click popup (optionally a kebab icon)
        Hidden          // no actions surfaced
    };

    // ===== WHICH CORNER THE ACTION ICON(S) ANCHOR TO =====
    // The kebab / action buttons sit in one corner of either the image area or
    // the caption "text block" beneath it. A row of buttons grows horizontally
    // away from the anchored corner (rightward from a *Left corner, leftward
    // from a *Right one). The TextBlock anchors fall back to the image area when
    // there is no below-image caption strip (overlay / hidden captions).
    enum class AlbumActionAnchor {
        TopLeftImage,
        TopRightImage,
        BottomLeftImage,
        BottomRightImage,
        TopLeftTextBlock,
        TopRightTextBlock,
        BottomLeftTextBlock,
        BottomRightTextBlock
    };

    // ===== SHAPE OF THE ACTION-ICON BACKGROUND =====
    // The translucent backing drawn behind each kebab / action glyph.
    enum class AlbumActionIconBackground {
        Round,         // a circle (the classic overlay look)
        Square,        // a plain square
        RoundedSquare  // a square with rounded corners
    };

    // ===== WHERE THE TITLE / SUBTITLE STRIP SITS =====
    // (BelowImage rather than "Below" because X11/X.h #defines Below.)
    enum class AlbumCaptionPlacement {
        BelowImage,     // a strip beneath the image (cards / store look)
        OverlayBottom,  // floated over the bottom of the image on a scrim
        Hidden
    };

    // ===== ONE ENTRY IN THE ALBUM =====
    struct AlbumItem {
        std::string mediaPath;      // photo file, or the video / music file itself
        std::string thumbnailPath;  // explicit cover/poster; falls back to mediaPath
        std::string title;          // primary caption line
        std::string subtitle;       // secondary line (artist, date, duration, ...)
        std::string description;    // longer related text (shown in a full-size view)
        std::string link;           // optional link target for the subtitle row; when
                                    // set, the subtitle is drawn as a clickable link
                                    // and onLinkClicked fires instead of a tile click
        std::string linkIconPath;   // optional icon drawn before the link text (e.g.
                                    // a YouTube badge); only used when `link` is set
        std::string id;             // optional stable id passed back to callbacks

        AlbumMediaType mediaType = AlbumMediaType::Photo;

        // Focus point (0..1) used when the image is cropped / zoomed. (0,0) keeps
        // the top-left, (0.5,0.5) centers, (1,1) keeps the bottom-right.
        Point2Df focusPoint{0.5f, 0.5f};

        // Known aspect ratio (width / height). 0 = derive from the bitmap; used by
        // Justified / Masonry so the layout can run without decoding every image.
        double aspectRatio = 0.0;

        // Promote this item in the Mosaic layout (spans a 2x2 block). The first
        // featured item wins; if none is flagged, item 0 is treated as featured.
        bool featured = false;
    };

    // ===== A USER-DEFINED PER-ITEM ACTION =====
    // Rendered as an overlay icon button and/or a context-menu row depending on
    // config.actionDisplay. Availability is gated per mode so a "Delete" action
    // can be hidden from visitors while an "Open" action is shown to everyone.
    struct AlbumAction {
        std::string id;
        std::string label;      // tooltip / context-menu text
        std::string iconPath;   // overlay button + menu icon (optional)

        bool inDisplay  = true;
        bool inUserEdit = true;
        bool inAdmin    = true;

        // Restrict the action to specific media types (e.g. a "View full size"
        // action that only applies to photos). Empty = available for every type.
        std::vector<AlbumMediaType> mediaTypes;

        // index is the position of the item in the album's item vector.
        std::function<void(size_t /*index*/)> onTrigger;
    };

    // ===== TOP-LEVEL CONFIGURATION =====
    struct AlbumConfig {
        AlbumLayout layout = AlbumLayout::UniformGrid;
        AlbumMode   mode   = AlbumMode::Display;

        // ----- Sizing: by items-per-row OR by a target image size -----
        AlbumSizingMode sizingMode = AlbumSizingMode::ItemsPerRow;
        int   itemsPerRow      = 4;     // used when sizingMode == ItemsPerRow
        int   targetImageWidth = 220;   // used when sizingMode == ImageSize (px)
        int   targetImageHeight= 165;   // target cell height for fixed-aspect grids
        int   targetRowHeight  = 200;   // Justified row height / Filmstrip height (px)

        int   gap          = 8;         // spacing between tiles (px)
        int   outerPadding = 12;        // padding around the whole album (px)

        // ----- Image fitting -----
        AlbumImageDisplay imageDisplay = AlbumImageDisplay::Crop;
        float zoomFactor = 1.25f;       // extra magnification for Zoom display
        Point2Df defaultFocus{0.5f, 0.5f};

        // ----- Actions -----
        AlbumActionDisplay actionDisplay = AlbumActionDisplay::OnHover;
        AlbumActionAnchor  actionAnchor  = AlbumActionAnchor::TopRightImage;
        bool showMenuIcon = true;       // ContextMenu mode: draw a kebab (⋮) icon
        float actionButtonSize = 28.0f; // diameter / side of each action button (px)
        // Background drawn behind each action glyph (shape + colour).
        AlbumActionIconBackground actionIconBackground = AlbumActionIconBackground::Round;
        Color actionIconBgColor = Color(0, 0, 0, 120);

        // ----- Appearance -----
        Color backgroundColor     = Color(245, 245, 247, 255);
        Color itemBackgroundColor = Color(255, 255, 255, 255);
        bool  showBorder   = true;
        Color borderColor  = Color(208, 208, 214, 255);
        float borderWidth  = 1.0f;
        float cornerRadius = 6.0f;        // tile (frame / background) corner radius
        // Image corner rounding, independent of the tile frame:
        //   < 0  -> follow cornerRadius (the image inherits the tile rounding)
        //   == 0 -> square image corners
        //   > 0  -> an explicit image corner radius
        // Only the corners that meet the tile edge are rounded; when a caption
        // strip sits below the image, the image's lower corners stay square so
        // the rounding is not cut into the middle of the tile.
        float imageCornerRadius = -1.0f;
        bool  dropShadow   = false;
        Color shadowColor  = Color(0, 0, 0, 40);

        // ----- Captions -----
        AlbumCaptionPlacement captionPlacement = AlbumCaptionPlacement::BelowImage;
        Color captionColor   = Color(30, 30, 30, 255);
        Color subtitleColor  = Color(120, 120, 120, 255);
        Color overlayScrimColor = Color(0, 0, 0, 130);  // caption / hover scrim
        float titleFontSize    = 14.0f;
        float subtitleFontSize = 11.0f;
        std::string fontFamily;            // empty = system default

        // Subtitle-as-link styling. When an item has a non-empty `link`, its
        // subtitle row is drawn in linkColor (and optionally underlined); a
        // click on that row fires onLinkClicked instead of selecting the tile.
        Color linkColor     = Color(26, 115, 232, 255);  // #1A73E8
        bool  linkUnderline = true;

        // ----- Media-type badges -----
        bool  showMediaBadges = true;      // play icon (video) / note icon (music)
        Color badgeColor      = Color(0, 0, 0, 150);
        Color badgeIconColor  = Color(255, 255, 255, 255);

        // ----- Selection (edit / admin) -----
        bool  allowSelection = false;
        Color selectionColor = Color(0, 122, 224, 255);

        // ----- Hover -----
        bool  hoverScrim   = true;         // darken a tile slightly on hover
        bool  hoverZoom    = false;        // subtle image zoom on hover (Crop/Zoom)
    };

    // ===== THE ALBUM ELEMENT =====
    // Self-rendered like the slideshow: tiles, captions, badges and action icons
    // are all painted inside Render() rather than as child elements, so a large
    // album stays cheap and the layout can be swapped by a single enum.
    class UltraCanvasAlbum : public UltraCanvasContainer {
    public:
        UltraCanvasAlbum(const std::string& identifier,
                         float x, float y, float w, float h);

        UltraCanvasAlbum(const std::string& identifier, float w, float h)
            : UltraCanvasAlbum(identifier, -1, -1, w, h) {}

        explicit UltraCanvasAlbum(const std::string& identifier)
            : UltraCanvasAlbum(identifier, -1, -1, -1, -1) {}

        ~UltraCanvasAlbum() override;

        // ===== CONFIGURATION =====
        void SetConfig(const AlbumConfig& cfg);
        const AlbumConfig& GetConfig() const { return config; }

        void SetLayout(AlbumLayout layout);
        AlbumLayout GetLayout() const { return config.layout; }

        void SetMode(AlbumMode mode);
        AlbumMode GetMode() const { return config.mode; }

        void SetImageDisplay(AlbumImageDisplay d);
        void SetActionDisplay(AlbumActionDisplay d);

        // Set the crop / zoom focus point (0..1) on every item at once. Used by
        // the "Crop focus" control: it only has a visible effect under the Crop /
        // Zoom image-fit modes, which place the image by each item's focus point.
        void SetAllItemsFocus(const Point2Df& focus);

        // Sizing helpers
        void SetItemsPerRow(int count);
        void SetTargetImageSize(int width, int height);

        // ===== ITEM MANAGEMENT =====
        void AddItem(const AlbumItem& item);
        void SetItems(const std::vector<AlbumItem>& items);
        void ClearItems();
        size_t GetItemCount() const { return items.size(); }
        const std::vector<AlbumItem>& GetItems() const { return items; }
        AlbumItem* GetItem(size_t index);

        bool RemoveItem(size_t index);
        bool MoveItem(size_t from, size_t to);

        // ===== ACTIONS =====
        void AddAction(const AlbumAction& action);
        void ClearActions();
        const std::vector<AlbumAction>& GetActions() const { return actions; }

        // ===== SELECTION =====
        const std::vector<size_t>& GetSelectedIndices() const { return selection; }
        void ClearSelection();
        bool IsSelected(size_t index) const;

        // ===== EVENTS =====
        std::function<void(size_t)> onItemClicked;        // single click / tap
        std::function<void(size_t)> onLinkClicked;        // subtitle link row clicked
        std::function<void(size_t)> onItemActivated;      // double-click: open / play
        std::function<void(size_t, size_t)> onItemsReordered;  // (from, to) — edit modes
        std::function<void(size_t)> onItemRemoved;        // after RemoveItem
        std::function<void(const std::vector<size_t>&)> onSelectionChanged;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== STATE =====
        AlbumConfig config;
        std::vector<AlbumItem> items;
        std::vector<AlbumAction> actions;
        std::vector<size_t> selection;

        // Computed tile geometry (in content space — before scroll offset).
        struct TileLayout {
            Rect2Di rect;       // the whole tile (image + caption-below strip)
            Rect2Di imageRect;  // image sub-region of the tile
            size_t  itemIndex;
        };
        std::vector<TileLayout> tiles;
        bool layoutValid = false;
        int  contentWidth  = 0;   // used by Filmstrip (horizontal scroll)
        int  contentHeight = 0;   // used by the vertical layouts

        // Content-area size the tiles were last laid out for. The album has no
        // child elements (it self-renders), so it has no Arrange override to
        // react to resize; instead EnsureLayout() reflows when this changes.
        int  lastAreaW = -1;
        int  lastAreaH = -1;

        // Scrolling — handled manually so the self-render is independent of the
        // container's child-driven scrollbars (the album has no child elements).
        int  scrollOffsetX = 0;
        int  scrollOffsetY = 0;

        // Hover / interaction
        int  hoveredItem = -1;

        // Which action icon the cursor currently sits on, so a tooltip is shown
        // once on enter rather than re-issued on every mouse move. Mirrors the
        // ActionAt() return convention: -2 = none, -1 = the kebab/menu icon.
        int    hoveredActionIndex = -2;
        size_t hoveredActionItem  = 0;

        // Action-button hit rects recomputed each frame.
        struct ActionHit {
            Rect2Di rect;
            size_t  itemIndex;
            int     actionIndex;  // index into `actions`, or -1 for the kebab/menu icon
        };
        std::vector<ActionHit> actionHits;

        // Subtitle-link hit rects recomputed each frame (items with a `link`).
        struct LinkHit {
            Rect2Di rect;
            size_t  itemIndex;
        };
        std::vector<LinkHit> linkHits;
        int hoveredLinkItem = -1;   // item whose link the cursor is over, or -1

        // Drag-reorder state (UserEdit / Admin).
        bool   dragging = false;
        bool   dragArmed = false;
        int    dragItem = -1;
        Point2Di dragStart;
        Point2Di dragCurrent;

        std::shared_ptr<UltraCanvasMenu> activePopupMenu;

        // ===== LAYOUT =====
        void InvalidateAlbumLayout();
        void EnsureLayout();
        void RecomputeLayout();
        Rect2Di ContentBounds() const;     // inner rect (local) minus outer padding
        int  ResolveColumnCount(int availWidth) const;

        void LayoutUniformGrid(const Rect2Di& area, bool cards);
        void LayoutJustified(const Rect2Di& area);
        void LayoutMasonry(const Rect2Di& area);
        void LayoutMosaic(const Rect2Di& area);
        void LayoutFilmstrip(const Rect2Di& area);

        // ===== HELPERS =====
        bool   IsHorizontal() const { return config.layout == AlbumLayout::Filmstrip; }
        double ItemAspect(const AlbumItem& item) const;   // width / height
        std::string ThumbPathFor(const AlbumItem& item) const;
        bool   ActionVisibleInMode(const AlbumAction& a) const;
        // Action indices offered for a given item: gated by the current mode and
        // by the action's optional media-type filter.
        std::vector<int> VisibleActionIndices(size_t itemIndex) const;
        // The caption / text-block rect beneath the image (falls back to the
        // image rect when there is no below-image caption strip).
        Rect2Di TextBlockRect(const TileLayout& tile) const;
        // The rect the action icons anchor inside, plus which corner they hug.
        Rect2Di ActionAnchorRect(const TileLayout& tile,
                                 bool& fromLeft, bool& fromTop) const;
        // Effective image corner radius for a tile (resolves imageCornerRadius).
        float   ResolveImageRadius() const;
        int    MaxScrollY() const;
        int    MaxScrollX() const;
        void   ClampScroll();

        // ===== DRAWING =====
        void DrawTile(IRenderContext* ctx, const TileLayout& tile, bool hovered);
        void DrawImageInRect(IRenderContext* ctx, const AlbumItem& item,
                             const Rect2Di& rect, float zoomExtra);
        void DrawPlaceholder(IRenderContext* ctx, const AlbumItem& item,
                             const Rect2Di& rect);
        void DrawMediaBadge(IRenderContext* ctx, const AlbumItem& item,
                            const Rect2Di& imageRect);
        void DrawCaption(IRenderContext* ctx, const AlbumItem& item,
                         const TileLayout& tile);
        void DrawActionIcons(IRenderContext* ctx, const TileLayout& tile, bool hovered);
        // The translucent backing behind an action glyph (round / square / rounded).
        void DrawActionButtonBg(IRenderContext* ctx, const Rect2Di& button);
        void DrawIconGlyph(IRenderContext* ctx, const AlbumAction* action,
                           const Rect2Di& button);
        void DrawScrollbar(IRenderContext* ctx);
        void DrawDragGhost(IRenderContext* ctx);

        // ===== HIT TESTING =====
        // localPoint is element-local (already scroll-compensated by the caller).
        int  TileAt(const Point2Di& contentPoint) const;       // -> item index or -1
        int  ActionAt(const Point2Di& localPoint, size_t& outItem, bool& outIsMenu) const;
        int  LinkAt(const Point2Di& localPoint) const;         // -> item index or -1
        Point2Di ToContentPoint(const Point2Di& localPoint) const;

        void OpenItemContextMenu(size_t itemIndex, const Point2Di& localPoint);
        void TriggerAction(int actionIndex, size_t itemIndex);
        void ToggleSelection(size_t itemIndex);
        void FinishDrag();
    };

    // ===== FACTORY =====
    inline std::shared_ptr<UltraCanvasAlbum> CreateAlbum(
            const std::string& identifier, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasAlbum>(identifier, x, y, w, h);
    }

} // namespace UltraCanvas
