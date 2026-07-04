// core/UltraCanvasAlbum.cpp
// Photo / video / music album widget with selectable layout designs, per-item
// crop / zoom / stretch fitting, action icons and visitor / edit / admin modes.
// Version: 1.4.0
// Last Modified: 2026-06-25
// V1.4.0: The self-rendered scrollbar reacts to the mouse — drag the thumb or
//   click the track to scroll (DrawScrollbar and the event handler now share
//   ScrollbarGeometry so a click hits exactly what is drawn).
// V1.3.0: AlbumItem::linkIconPath paints an icon before the subtitle link text
//   (e.g. a YouTube badge), included in the link's clickable hit area.
// V1.2.0: Action-icon background shape is configurable (AlbumActionIconBackground
//   — round / square / rounded-square) with a configurable backing colour
//   (AlbumConfig::actionIconBgColor).
// V1.1.0: Action icons can anchor to any corner of the image or the caption
//   text-block (AlbumConfig::actionAnchor); image corners are now configurable
//   independently of the tile frame (AlbumConfig::imageCornerRadius — square or
//   rounded); the subtitle row can be a clickable link (AlbumItem::link +
//   onLinkClicked, painted in linkColor and underlined).
// V1.0.3: Per-item action gating by media type (AlbumAction::mediaTypes), so an
//   action can be offered for photos only; AlbumItem gained a description field
//   for richer detail / full-size views.
// V1.0.2: Caption title/subtitle block is now vertically centred inside the
//   caption strip; action icons show a hover tooltip (their label); added
//   SetAllItemsFocus() so a single control can set the crop focus point.
// V1.0.1: EnsureLayout() now reflows the tile layout when the content area
//   changes size, so the grid responds to window resize / flex-driven growth.
// Author: UltraCanvas Framework

#include "UltraCanvasAlbum.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    namespace {
        constexpr int kDragThreshold = 6;       // px before a press becomes a drag
        constexpr int kWheelStep     = 64;      // px per wheel notch

        int clampi(int v, int lo, int hi) {
            return v < lo ? lo : (v > hi ? hi : v);
        }
    }

    UltraCanvasAlbum::UltraCanvasAlbum(const std::string& identifier,
                                       float x, float y, float w, float h)
            : UltraCanvasContainer(identifier, x, y, w, h) {
        SetMouseCursor(UCMouseCursor::Default);
        SetBackgroundColor(config.backgroundColor);
        // The album paints its own tiles; hide the container's child scrollbars.
        ContainerStyle cs = GetContainerStyle();
        cs.autoShowScrollbars = false;
        cs.forceShowVerticalScrollbar = false;
        cs.forceShowHorizontalScrollbar = false;
        SetContainerStyle(cs);
    }

    UltraCanvasAlbum::~UltraCanvasAlbum() = default;

    // ===== CONFIGURATION =====
    void UltraCanvasAlbum::SetConfig(const AlbumConfig& cfg) {
        config = cfg;
        SetBackgroundColor(config.backgroundColor);
        InvalidateAlbumLayout();
        RequestRedraw();
    }

    void UltraCanvasAlbum::SetLayout(AlbumLayout layout) {
        if (config.layout == layout) return;
        config.layout = layout;
        scrollOffsetX = scrollOffsetY = 0;
        InvalidateAlbumLayout();
        RequestRedraw();
    }

    void UltraCanvasAlbum::SetMode(AlbumMode mode) {
        if (config.mode == mode) return;
        config.mode = mode;
        if (mode == AlbumMode::Display) ClearSelection();
        dragging = dragArmed = false;
        dragItem = -1;
        RequestRedraw();
    }

    void UltraCanvasAlbum::SetImageDisplay(AlbumImageDisplay d) {
        config.imageDisplay = d;
        RequestRedraw();
    }

    void UltraCanvasAlbum::SetActionDisplay(AlbumActionDisplay d) {
        config.actionDisplay = d;
        RequestRedraw();
    }

    void UltraCanvasAlbum::SetAllItemsFocus(const Point2Df& focus) {
        config.defaultFocus = focus;
        for (auto& it : items) it.focusPoint = focus;
        RequestRedraw();
    }

    void UltraCanvasAlbum::SetItemsPerRow(int count) {
        config.sizingMode = AlbumSizingMode::ItemsPerRow;
        config.itemsPerRow = std::max(1, count);
        InvalidateAlbumLayout();
        RequestRedraw();
    }

    void UltraCanvasAlbum::SetTargetImageSize(int width, int height) {
        config.sizingMode = AlbumSizingMode::ImageSize;
        config.targetImageWidth  = std::max(16, width);
        config.targetImageHeight = std::max(16, height);
        InvalidateAlbumLayout();
        RequestRedraw();
    }

    // ===== ITEM MANAGEMENT =====
    void UltraCanvasAlbum::AddItem(const AlbumItem& item) {
        items.push_back(item);
        InvalidateAlbumLayout();
        RequestRedraw();
    }

    void UltraCanvasAlbum::SetItems(const std::vector<AlbumItem>& newItems) {
        items = newItems;
        ClearSelection();
        InvalidateAlbumLayout();
        RequestRedraw();
    }

    void UltraCanvasAlbum::ClearItems() {
        items.clear();
        selection.clear();
        hoveredItem = -1;
        scrollOffsetX = scrollOffsetY = 0;
        InvalidateAlbumLayout();
        RequestRedraw();
    }

    AlbumItem* UltraCanvasAlbum::GetItem(size_t index) {
        return index < items.size() ? &items[index] : nullptr;
    }

    bool UltraCanvasAlbum::RemoveItem(size_t index) {
        if (index >= items.size()) return false;
        items.erase(items.begin() + index);
        // Selection indices shift; rebuild conservatively.
        selection.erase(std::remove(selection.begin(), selection.end(), index),
                        selection.end());
        for (auto& s : selection) if (s > index) --s;
        hoveredItem = -1;
        InvalidateAlbumLayout();
        if (onItemRemoved) onItemRemoved(index);
        RequestRedraw();
        return true;
    }

    bool UltraCanvasAlbum::MoveItem(size_t from, size_t to) {
        if (from >= items.size() || to >= items.size() || from == to) return false;
        AlbumItem moved = items[from];
        items.erase(items.begin() + from);
        items.insert(items.begin() + to, moved);
        selection.clear();
        InvalidateAlbumLayout();
        if (onItemsReordered) onItemsReordered(from, to);
        RequestRedraw();
        return true;
    }

    // ===== ACTIONS =====
    void UltraCanvasAlbum::AddAction(const AlbumAction& action) {
        actions.push_back(action);
        RequestRedraw();
    }

    void UltraCanvasAlbum::ClearActions() {
        actions.clear();
        RequestRedraw();
    }

    // ===== SELECTION =====
    void UltraCanvasAlbum::ClearSelection() {
        if (selection.empty()) return;
        selection.clear();
        if (onSelectionChanged) onSelectionChanged(selection);
        RequestRedraw();
    }

    bool UltraCanvasAlbum::IsSelected(size_t index) const {
        return std::find(selection.begin(), selection.end(), index) != selection.end();
    }

    void UltraCanvasAlbum::ToggleSelection(size_t itemIndex) {
        auto it = std::find(selection.begin(), selection.end(), itemIndex);
        if (it != selection.end()) selection.erase(it);
        else selection.push_back(itemIndex);
        if (onSelectionChanged) onSelectionChanged(selection);
        RequestRedraw();
    }

    bool UltraCanvasAlbum::ActionVisibleInMode(const AlbumAction& a) const {
        switch (config.mode) {
            case AlbumMode::Display:  return a.inDisplay;
            case AlbumMode::UserEdit: return a.inUserEdit;
            case AlbumMode::Admin:    return a.inAdmin;
        }
        return false;
    }

    std::vector<int> UltraCanvasAlbum::VisibleActionIndices(size_t itemIndex) const {
        std::vector<int> out;
        const AlbumMediaType type = (itemIndex < items.size())
                ? items[itemIndex].mediaType : AlbumMediaType::Photo;
        for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
            const AlbumAction& a = actions[i];
            if (!ActionVisibleInMode(a)) continue;
            if (!a.mediaTypes.empty() &&
                std::find(a.mediaTypes.begin(), a.mediaTypes.end(), type) == a.mediaTypes.end()) {
                continue;  // action filtered out for this item's media type
            }
            out.push_back(i);
        }
        return out;
    }

    // ===== LAYOUT BOOKKEEPING =====
    void UltraCanvasAlbum::InvalidateAlbumLayout() { layoutValid = false; }

    void UltraCanvasAlbum::EnsureLayout() {
        // Reflow when the content area changed size (e.g. the window resized or
        // the album was grown by a flex parent) — the album has no child
        // elements, so nothing else invalidates the tile layout on resize.
        Rect2Di area = ContentBounds();
        if (area.width != lastAreaW || area.height != lastAreaH) {
            lastAreaW = area.width;
            lastAreaH = area.height;
            layoutValid = false;
        }
        if (!layoutValid) RecomputeLayout();
    }

    Rect2Di UltraCanvasAlbum::ContentBounds() const {
        auto b = GetLocalBounds();
        int p = config.outerPadding;
        return Rect2Di(static_cast<int>(b.x) + p, static_cast<int>(b.y) + p,
                       std::max(0, static_cast<int>(b.width)  - 2 * p),
                       std::max(0, static_cast<int>(b.height) - 2 * p));
    }

    int UltraCanvasAlbum::ResolveColumnCount(int availWidth) const {
        if (config.sizingMode == AlbumSizingMode::ItemsPerRow) {
            return std::max(1, config.itemsPerRow);
        }
        int unit = config.targetImageWidth + config.gap;
        if (unit <= 0) return 1;
        int cols = (availWidth + config.gap) / unit;
        return std::max(1, cols);
    }

    double UltraCanvasAlbum::ItemAspect(const AlbumItem& item) const {
        if (item.aspectRatio > 0.0) return item.aspectRatio;
        auto img = UCImage::Get(ThumbPathFor(item));
        if (img && img->GetHeight() > 0 && img->GetWidth() > 0) {
            return static_cast<double>(img->GetWidth()) / img->GetHeight();
        }
        // Sensible defaults per media type when nothing decodes.
        if (item.mediaType == AlbumMediaType::Music) return 1.0;   // square cover
        if (config.targetImageHeight > 0) {
            return static_cast<double>(config.targetImageWidth) / config.targetImageHeight;
        }
        return 4.0 / 3.0;
    }

    std::string UltraCanvasAlbum::ThumbPathFor(const AlbumItem& item) const {
        if (!item.thumbnailPath.empty()) return item.thumbnailPath;
        if (item.mediaType == AlbumMediaType::Photo) return item.mediaPath;
        return std::string();  // video/music with no thumbnail -> placeholder
    }

    void UltraCanvasAlbum::RecomputeLayout() {
        tiles.clear();
        contentWidth = contentHeight = 0;
        layoutValid = true;

        Rect2Di area = ContentBounds();
        if (area.width <= 0 || area.height <= 0 || items.empty()) return;

        switch (config.layout) {
            case AlbumLayout::UniformGrid: LayoutUniformGrid(area, false); break;
            case AlbumLayout::Cards:       LayoutUniformGrid(area, true);  break;
            case AlbumLayout::Justified:   LayoutJustified(area);          break;
            case AlbumLayout::Masonry:     LayoutMasonry(area);            break;
            case AlbumLayout::Mosaic:      LayoutMosaic(area);             break;
            case AlbumLayout::Filmstrip:   LayoutFilmstrip(area);          break;
        }
        ClampScroll();
    }

    namespace {
        // Height of the caption-below strip for a given config (0 if not Below).
        // Generous enough that the title + subtitle block can sit vertically
        // centred inside the strip (DrawCaption centres it, so the strip must be
        // at least as tall as the real line heights plus top/bottom padding).
        constexpr int kCaptionVPad   = 6;   // padding above and below the block
        constexpr int kCaptionLineGap = 3;  // gap between the title and subtitle
        int CaptionBelowHeight(const AlbumConfig& c) {
            if (c.captionPlacement != AlbumCaptionPlacement::BelowImage) return 0;
            int h = 2 * kCaptionVPad;
            h += static_cast<int>(std::lround(c.titleFontSize * 1.3));
            h += kCaptionLineGap;
            h += static_cast<int>(std::lround(c.subtitleFontSize * 1.3));
            return h;
        }
    }

    void UltraCanvasAlbum::LayoutUniformGrid(const Rect2Di& area, bool cards) {
        int cols = ResolveColumnCount(area.width);
        int gap = config.gap;
        int capH = CaptionBelowHeight(config);

        int cellW = (area.width - (cols - 1) * gap) / std::max(1, cols);
        if (cellW < 1) cellW = 1;

        // Image area aspect: fixed-aspect cells. Default to the target image
        // ratio (ImageSize mode) or a 4:3-ish cell.
        double aspect = (config.targetImageHeight > 0)
                ? static_cast<double>(config.targetImageWidth) / config.targetImageHeight
                : 4.0 / 3.0;
        if (cards) aspect = 1.0;  // cards read best as squares
        int imgH = static_cast<int>(cellW / std::max(0.1, aspect));
        int cellH = imgH + capH;

        int x0 = area.x, y0 = area.y;
        for (size_t i = 0; i < items.size(); ++i) {
            int col = static_cast<int>(i) % cols;
            int row = static_cast<int>(i) / cols;
            int x = x0 + col * (cellW + gap);
            int y = y0 + row * (cellH + gap);
            TileLayout t;
            t.rect = Rect2Di(x, y, cellW, cellH);
            t.imageRect = Rect2Di(x, y, cellW, imgH);
            t.itemIndex = i;
            tiles.push_back(t);
        }
        int rows = (static_cast<int>(items.size()) + cols - 1) / cols;
        contentHeight = rows * cellH + (rows - 1) * gap + 2 * config.outerPadding;
    }

    void UltraCanvasAlbum::LayoutJustified(const Rect2Di& area) {
        int gap = config.gap;
        int capH = CaptionBelowHeight(config);
        int targetH = std::max(60, config.targetRowHeight);
        int y = area.y;
        size_t i = 0;
        const int n = static_cast<int>(items.size());

        while (i < items.size()) {
            // Greedily collect items until the natural row width exceeds area.width.
            std::vector<size_t> row;
            double sumAspect = 0.0;
            size_t j = i;
            while (j < items.size()) {
                double a = std::max(0.2, ItemAspect(items[j]));
                double naturalW = sumAspect * targetH + a * targetH
                                  + static_cast<double>(row.size()) * gap;
                row.push_back(j);
                sumAspect += a;
                ++j;
                if (naturalW >= area.width) break;
            }

            bool lastRow = (j >= items.size());
            int rowGaps = (static_cast<int>(row.size()) - 1) * gap;
            int rowH;
            if (lastRow && (sumAspect * targetH + rowGaps) < area.width) {
                // Don't stretch a short final row to full width.
                rowH = targetH;
            } else {
                double scale = (area.width - rowGaps) / (sumAspect * targetH);
                rowH = static_cast<int>(targetH * std::max(0.1, scale));
            }

            int x = area.x;
            for (size_t k = 0; k < row.size(); ++k) {
                double a = std::max(0.2, ItemAspect(items[row[k]]));
                int w = static_cast<int>(a * rowH);
                if (k == row.size() - 1 && !lastRow) {
                    w = area.x + area.width - x;  // flush the row's right edge
                }
                TileLayout t;
                t.rect = Rect2Di(x, y, w, rowH + capH);
                t.imageRect = Rect2Di(x, y, w, rowH);
                t.itemIndex = row[k];
                tiles.push_back(t);
                x += w + gap;
            }
            y += rowH + capH + gap;
            i = j;
            (void)n;
        }
        contentHeight = (y - area.y) + 2 * config.outerPadding - gap;
    }

    void UltraCanvasAlbum::LayoutMasonry(const Rect2Di& area) {
        int cols = ResolveColumnCount(area.width);
        int gap = config.gap;
        int capH = CaptionBelowHeight(config);
        int colW = (area.width - (cols - 1) * gap) / std::max(1, cols);
        if (colW < 1) colW = 1;

        std::vector<int> colHeights(cols, 0);  // running height per column

        for (size_t i = 0; i < items.size(); ++i) {
            // Pick the shortest column.
            int c = 0;
            for (int k = 1; k < cols; ++k) if (colHeights[k] < colHeights[c]) c = k;

            double a = std::max(0.2, ItemAspect(items[i]));
            int imgH = static_cast<int>(colW / a);
            int x = area.x + c * (colW + gap);
            int y = area.y + colHeights[c];

            TileLayout t;
            t.rect = Rect2Di(x, y, colW, imgH + capH);
            t.imageRect = Rect2Di(x, y, colW, imgH);
            t.itemIndex = i;
            tiles.push_back(t);

            colHeights[c] += imgH + capH + gap;
        }
        int tallest = 0;
        for (int h : colHeights) tallest = std::max(tallest, h);
        contentHeight = tallest + 2 * config.outerPadding - gap;
    }

    void UltraCanvasAlbum::LayoutMosaic(const Rect2Di& area) {
        int cols = std::max(2, ResolveColumnCount(area.width));
        int gap = config.gap;
        int capH = CaptionBelowHeight(config);
        int cellW = (area.width - (cols - 1) * gap) / cols;
        if (cellW < 1) cellW = 1;
        int cellH = static_cast<int>(cellW / std::max(0.1,
                (config.targetImageHeight > 0
                    ? static_cast<double>(config.targetImageWidth) / config.targetImageHeight
                    : 1.0)));

        // Find the featured item (first flagged, else item 0).
        size_t featured = 0;
        for (size_t i = 0; i < items.size(); ++i) if (items[i].featured) { featured = i; break; }

        // Occupancy grid grown on demand.
        std::vector<std::vector<bool>> occ;
        auto ensureRows = [&](int rows) {
            while (static_cast<int>(occ.size()) < rows) occ.emplace_back(cols, false);
        };
        auto place = [&](int r, int c, int span) {
            ensureRows(r + span);
            for (int dr = 0; dr < span; ++dr)
                for (int dc = 0; dc < span; ++dc) occ[r + dr][c + dc] = true;
        };
        auto freeAt = [&](int r, int c, int span) {
            ensureRows(r + span);
            if (c + span > cols) return false;
            for (int dr = 0; dr < span; ++dr)
                for (int dc = 0; dc < span; ++dc) if (occ[r + dr][c + dc]) return false;
            return true;
        };

        // Featured spans 2x2 at the first free 2x2 slot (top-left in practice).
        int fr = 0, fc = 0;
        bool placedFeatured = false;
        for (int r = 0; r < 64 && !placedFeatured; ++r) {
            for (int c = 0; c + 2 <= cols; ++c) {
                if (freeAt(r, c, 2)) { fr = r; fc = c; placedFeatured = true; break; }
            }
        }
        place(fr, fc, 2);
        auto cellRect = [&](int r, int c, int span) {
            int x = area.x + c * (cellW + gap);
            int y = area.y + r * (cellH + gap);
            int w = span * cellW + (span - 1) * gap;
            int h = span * cellH + (span - 1) * gap;
            return Rect2Di(x, y, w, h);
        };
        {
            Rect2Di r = cellRect(fr, fc, 2);
            TileLayout t;
            t.rect = Rect2Di(r.x, r.y, r.width, r.height + capH);
            t.imageRect = r;
            t.itemIndex = featured;
            tiles.push_back(t);
        }

        // Lay the rest into 1x1 cells, scanning row-major and skipping occupied.
        int scanR = 0, scanC = 0;
        auto nextFree = [&]() {
            for (int r = scanR; r < 256; ++r) {
                for (int c = (r == scanR ? scanC : 0); c < cols; ++c) {
                    ensureRows(r + 1);
                    if (!occ[r][c]) { scanR = r; scanC = c; return; }
                }
            }
        };
        for (size_t i = 0; i < items.size(); ++i) {
            if (i == featured) continue;
            nextFree();
            place(scanR, scanC, 1);
            Rect2Di r = cellRect(scanR, scanC, 1);
            TileLayout t;
            t.rect = Rect2Di(r.x, r.y, r.width, r.height + capH);
            t.imageRect = r;
            t.itemIndex = i;
            tiles.push_back(t);
            scanC += 1;
        }

        int rowsUsed = static_cast<int>(occ.size());
        contentHeight = rowsUsed * (cellH + gap) + capH + 2 * config.outerPadding - gap;
    }

    void UltraCanvasAlbum::LayoutFilmstrip(const Rect2Di& area) {
        int gap = config.gap;
        int capH = CaptionBelowHeight(config);
        int stripH = std::min(area.height, std::max(80, config.targetRowHeight) + capH);
        int imgH = stripH - capH;

        int x = area.x;
        int y = area.y;
        for (size_t i = 0; i < items.size(); ++i) {
            double a = std::max(0.2, ItemAspect(items[i]));
            int w = (config.sizingMode == AlbumSizingMode::ImageSize)
                    ? config.targetImageWidth
                    : static_cast<int>(a * imgH);
            TileLayout t;
            t.rect = Rect2Di(x, y, w, stripH);
            t.imageRect = Rect2Di(x, y, w, imgH);
            t.itemIndex = i;
            tiles.push_back(t);
            x += w + gap;
        }
        contentWidth = (x - area.x) + 2 * config.outerPadding - gap;
        contentHeight = stripH + 2 * config.outerPadding;
    }

    int UltraCanvasAlbum::MaxScrollY() const {
        auto b = GetLocalBounds();
        return std::max(0, contentHeight - static_cast<int>(b.height));
    }

    int UltraCanvasAlbum::MaxScrollX() const {
        auto b = GetLocalBounds();
        return std::max(0, contentWidth - static_cast<int>(b.width));
    }

    void UltraCanvasAlbum::ClampScroll() {
        scrollOffsetY = clampi(scrollOffsetY, 0, MaxScrollY());
        scrollOffsetX = clampi(scrollOffsetX, 0, MaxScrollX());
    }

    UltraCanvasAlbum::ScrollbarGeom UltraCanvasAlbum::ScrollbarGeometry() const {
        ScrollbarGeom g;
        auto b = GetLocalBounds();
        constexpr int kBarThickness = 6;
        constexpr int kMinThumb     = 30;
        if (IsHorizontal()) {
            int maxX = MaxScrollX();
            if (maxX <= 0) return g;
            int y = static_cast<int>(b.y + b.height) - kBarThickness - 2;
            double frac = static_cast<double>(b.width) / std::max(1, contentWidth);
            int thumbW = std::max(kMinThumb, static_cast<int>(b.width * frac));
            int travel = std::max(0, static_cast<int>(b.width) - thumbW);
            int tx = static_cast<int>(b.x) + (maxX > 0 ? travel * scrollOffsetX / maxX : 0);
            g.active = true; g.horizontal = true; g.travel = travel; g.maxScroll = maxX;
            g.track = Rect2Di(static_cast<int>(b.x), y, static_cast<int>(b.width), kBarThickness);
            g.thumb = Rect2Di(tx, y, thumbW, kBarThickness);
        } else {
            int maxY = MaxScrollY();
            if (maxY <= 0) return g;
            int x = static_cast<int>(b.x + b.width) - kBarThickness - 2;
            double frac = static_cast<double>(b.height) / std::max(1, contentHeight);
            int thumbH = std::max(kMinThumb, static_cast<int>(b.height * frac));
            int travel = std::max(0, static_cast<int>(b.height) - thumbH);
            int ty = static_cast<int>(b.y) + (maxY > 0 ? travel * scrollOffsetY / maxY : 0);
            g.active = true; g.horizontal = false; g.travel = travel; g.maxScroll = maxY;
            g.track = Rect2Di(x, static_cast<int>(b.y), kBarThickness, static_cast<int>(b.height));
            g.thumb = Rect2Di(x, ty, kBarThickness, thumbH);
        }
        return g;
    }

    void UltraCanvasAlbum::ScrollThumbTo(int thumbLeadPx) {
        ScrollbarGeom g = ScrollbarGeometry();
        if (!g.active || g.travel <= 0) return;
        auto b = GetLocalBounds();
        if (g.horizontal) {
            int rel = clampi(thumbLeadPx - static_cast<int>(b.x), 0, g.travel);
            scrollOffsetX = rel * g.maxScroll / g.travel;
        } else {
            int rel = clampi(thumbLeadPx - static_cast<int>(b.y), 0, g.travel);
            scrollOffsetY = rel * g.maxScroll / g.travel;
        }
        ClampScroll();
    }

    // ===== RENDER =====
    void UltraCanvasAlbum::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        // Container background + borders.
        UltraCanvasUIElement::Render(ctx, dirtyRect);

        EnsureLayout();

        Rect2Di bounds(static_cast<int>(GetLocalBounds().x),
                       static_cast<int>(GetLocalBounds().y),
                       static_cast<int>(GetLocalBounds().width),
                       static_cast<int>(GetLocalBounds().height));

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(bounds));
        ctx->SetFillPaint(config.backgroundColor);
        ctx->FillRectangle(Rect2Dd(bounds));

        if (items.empty()) {
            ctx->SetTextPaint(Color(150, 150, 150));
            ctx->SetFontSize(14.0f);
            ctx->DrawTextInRect("(empty album)", Rect2Dd(bounds));
            ctx->PopState();
            return;
        }

        actionHits.clear();
        linkHits.clear();

        // Translate the whole content by the scroll offset.
        ctx->PushState();
        ctx->Translate(-scrollOffsetX, -scrollOffsetY);
        for (const auto& tile : tiles) {
            // Cull tiles fully outside the viewport.
            int top = tile.rect.y - scrollOffsetY;
            int bottom = top + tile.rect.height;
            int left = tile.rect.x - scrollOffsetX;
            int right = left + tile.rect.width;
            if (bottom < bounds.y || top > bounds.y + bounds.height) continue;
            if (right < bounds.x || left > bounds.x + bounds.width) continue;
            bool hov = (static_cast<int>(tile.itemIndex) == hoveredItem);
            DrawTile(ctx, tile, hov);
        }
        ctx->PopState();

        DrawScrollbar(ctx);
        if (dragging) DrawDragGhost(ctx);
        ctx->PopState();
    }

    void UltraCanvasAlbum::DrawTile(IRenderContext* ctx, const TileLayout& tile, bool hovered) {
        if (tile.itemIndex >= items.size()) return;
        const AlbumItem& item = items[tile.itemIndex];
        Rect2Dd rect(tile.rect);
        double radius = config.cornerRadius;

        // Drop shadow (offset filled rounded rect behind the tile).
        if (config.dropShadow) {
            Rect2Dd sh(rect.x + 2, rect.y + 3, rect.width, rect.height);
            ctx->SetFillPaint(config.shadowColor);
            ctx->FillRoundedRectangle(sh, radius);
        }

        // Tile background.
        ctx->SetFillPaint(config.itemBackgroundColor);
        ctx->FillRoundedRectangle(rect, radius);

        // Master clip: keeps every painted part within the tile silhouette so a
        // square image can never spill past a rounded frame.
        ctx->PushState();
        ctx->ClipRoundedRectangle(rect, radius, radius, radius, radius);

        // The image gets its own rounded clip nested inside the master one, so
        // its corners can be rounded more than the tile frame (or, with a square
        // image radius and a square frame, left square). Only the corners that
        // touch the tile edge are rounded; when a caption strip sits below the
        // image, the image's lower corners stay square so the rounding is not cut
        // into the middle of the tile.
        bool overlay    = (config.captionPlacement == AlbumCaptionPlacement::OverlayBottom);
        bool belowStrip = (config.captionPlacement == AlbumCaptionPlacement::BelowImage);
        double imgR  = ResolveImageRadius();
        double imgBR = belowStrip ? 0.0 : imgR;   // bottom corners square under a strip

        ctx->PushState();
        ctx->ClipRoundedRectangle(Rect2Dd(tile.imageRect), imgR, imgR, imgBR, imgBR);

        // Image (or placeholder for video/music with no thumbnail).
        float zoomExtra = 1.0f;
        if (config.imageDisplay == AlbumImageDisplay::Zoom) zoomExtra = config.zoomFactor;
        if (config.hoverZoom && hovered &&
            (config.imageDisplay == AlbumImageDisplay::Crop ||
             config.imageDisplay == AlbumImageDisplay::Zoom)) {
            zoomExtra *= 1.06f;
        }
        DrawImageInRect(ctx, item, tile.imageRect, zoomExtra);

        // Hover scrim across the image.
        if (config.hoverScrim && hovered) {
            ctx->SetFillPaint(Color(0, 0, 0, 36));
            ctx->FillRectangle(Rect2Dd(tile.imageRect));
        }

        DrawMediaBadge(ctx, item, tile.imageRect);
        // An overlay caption is painted on the image, so it belongs inside the
        // image clip; a below-image strip is drawn after, outside it.
        if (overlay) DrawCaption(ctx, item, tile);
        ctx->PopState();   // image clip

        if (!overlay) DrawCaption(ctx, item, tile);  // BelowImage / Hidden (no-op)
        ctx->PopState();   // master clip

        // Border frame on top (outside the clip so it isn't cut).
        if (config.showBorder && config.borderWidth > 0) {
            ctx->SetStrokePaint(config.borderColor);
            ctx->SetStrokeWidth(config.borderWidth);
            ctx->DrawRoundedRectangle(rect, radius);
        }

        // Selection highlight.
        if (IsSelected(tile.itemIndex)) {
            ctx->SetStrokePaint(config.selectionColor);
            ctx->SetStrokeWidth(std::max(2.0f, config.borderWidth + 1.0f));
            ctx->DrawRoundedRectangle(rect, radius);
        }

        // Action icons last so they sit above the image / scrim.
        DrawActionIcons(ctx, tile, hovered);
    }

    void UltraCanvasAlbum::DrawImageInRect(IRenderContext* ctx, const AlbumItem& item,
                                           const Rect2Di& rect, float zoomExtra) {
        std::string path = ThumbPathFor(item);
        std::shared_ptr<UCImage> img = path.empty() ? nullptr : UCImage::Get(path);
        if (!img || img->GetWidth() <= 0 || img->GetHeight() <= 0) {
            DrawPlaceholder(ctx, item, rect);
            return;
        }

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(rect));

        const double rw = rect.width, rh = rect.height;
        const double iw = img->GetWidth(), ih = img->GetHeight();

        switch (config.imageDisplay) {
            case AlbumImageDisplay::Stretch:
                ctx->DrawImage(*img, Rect2Dd(rect), ImageFitMode::Fill);
                break;
            case AlbumImageDisplay::Fit: {
                // Letterbox onto the tile background.
                ctx->SetFillPaint(config.itemBackgroundColor);
                ctx->FillRectangle(Rect2Dd(rect));
                ctx->DrawImage(*img, Rect2Dd(rect), ImageFitMode::Contain);
                break;
            }
            case AlbumImageDisplay::Crop:
            case AlbumImageDisplay::Zoom:
            default: {
                // Cover the cell (optionally zoomed), placing by the focus point.
                double s = std::max(rw / iw, rh / ih) * std::max(1.0f, zoomExtra);
                double dstW = iw * s, dstH = ih * s;
                double fx = std::max(0.0f, std::min(1.0f, item.focusPoint.x));
                double fy = std::max(0.0f, std::min(1.0f, item.focusPoint.y));
                double dstX = rect.x + (rw - dstW) * fx;
                double dstY = rect.y + (rh - dstH) * fy;
                ctx->DrawImage(*img, Rect2Dd(dstX, dstY, dstW, dstH), ImageFitMode::Fill);
                break;
            }
        }
        ctx->PopState();
    }

    void UltraCanvasAlbum::DrawPlaceholder(IRenderContext* ctx, const AlbumItem& item,
                                           const Rect2Di& rect) {
        // Neutral panel with a large media glyph for video / music covers.
        Color bg = (item.mediaType == AlbumMediaType::Music)
                ? Color(58, 52, 74, 255) : Color(40, 42, 48, 255);
        ctx->SetFillPaint(bg);
        ctx->FillRectangle(Rect2Dd(rect));

        Point2Dd c(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0);
        double r = std::min(rect.width, rect.height) * 0.18;
        ctx->DrawFilledCircle(c, static_cast<float>(r), Color(255, 255, 255, 40));

        ctx->SetFillPaint(Color(255, 255, 255, 200));
        if (item.mediaType == AlbumMediaType::Music) {
            // Eighth note: head + stem.
            double hr = r * 0.42;
            Point2Dd head(c.x - r * 0.18, c.y + r * 0.28);
            ctx->DrawFilledCircle(head, static_cast<float>(hr), Color(255, 255, 255, 220));
            ctx->FillRectangle(Rect2Dd(head.x + hr * 0.7, c.y - r * 0.55,
                                       std::max(2.0, r * 0.14), r * 0.85));
        } else {
            // Play triangle.
            std::vector<Point2Dd> tri = {
                Point2Dd(c.x - r * 0.4, c.y - r * 0.55),
                Point2Dd(c.x - r * 0.4, c.y + r * 0.55),
                Point2Dd(c.x + r * 0.6, c.y)
            };
            ctx->FillLinePath(tri);
        }
    }

    void UltraCanvasAlbum::DrawMediaBadge(IRenderContext* ctx, const AlbumItem& item,
                                          const Rect2Di& imageRect) {
        if (!config.showMediaBadges) return;
        if (item.mediaType == AlbumMediaType::Photo) return;

        int sz = 22;
        int pad = 6;
        Rect2Di b(imageRect.x + pad, imageRect.y + imageRect.height - pad - sz, sz, sz);
        Point2Dd c(b.x + sz / 2.0, b.y + sz / 2.0);
        ctx->DrawFilledCircle(c, sz / 2.0f, config.badgeColor);

        ctx->SetFillPaint(config.badgeIconColor);
        double r = sz * 0.5;
        if (item.mediaType == AlbumMediaType::Music) {
            double hr = r * 0.30;
            Point2Dd head(c.x - r * 0.18, c.y + r * 0.30);
            ctx->DrawFilledCircle(head, static_cast<float>(hr), config.badgeIconColor);
            ctx->FillRectangle(Rect2Dd(head.x + hr * 0.7, c.y - r * 0.55,
                                       std::max(1.5, r * 0.16), r * 0.9));
        } else {
            std::vector<Point2Dd> tri = {
                Point2Dd(c.x - r * 0.32, c.y - r * 0.42),
                Point2Dd(c.x - r * 0.32, c.y + r * 0.42),
                Point2Dd(c.x + r * 0.46, c.y)
            };
            ctx->FillLinePath(tri);
        }
    }

    void UltraCanvasAlbum::DrawCaption(IRenderContext* ctx, const AlbumItem& item,
                                       const TileLayout& tile) {
        if (config.captionPlacement == AlbumCaptionPlacement::Hidden) return;
        if (item.title.empty() && item.subtitle.empty()) return;

        bool overlay = (config.captionPlacement == AlbumCaptionPlacement::OverlayBottom);

        // Measure the real line heights so the title + subtitle block can be
        // centred vertically inside the caption strip (previously the text was
        // pinned to the top, leaving it visually off-centre / partly clipped).
        FontStyle titleFs;
        titleFs.fontFamily = config.fontFamily;
        titleFs.fontSize   = config.titleFontSize;
        titleFs.fontWeight = FontWeight::Bold;
        FontStyle subFs;
        subFs.fontFamily = config.fontFamily;
        subFs.fontSize   = config.subtitleFontSize;

        int titleH = 0, subH = 0;
        if (!item.title.empty()) {
            ctx->SetFontStyle(titleFs);
            titleH = ctx->GetTextLineDimensions(item.title).height;
        }
        if (!item.subtitle.empty()) {
            ctx->SetFontStyle(subFs);
            subH = ctx->GetTextLineDimensions(item.subtitle).height;
        }
        int innerGap = (titleH > 0 && subH > 0) ? kCaptionLineGap : 0;
        int blockH = titleH + subH + innerGap;

        Rect2Di area;
        if (overlay) {
            int h = blockH + 2 * kCaptionVPad;
            area = Rect2Di(tile.imageRect.x, tile.imageRect.y + tile.imageRect.height - h,
                           tile.imageRect.width, h);
            ctx->SetFillPaint(config.overlayScrimColor);
            ctx->FillRectangle(Rect2Dd(area));
        } else {
            // Below: the strip beneath the image.
            area = Rect2Di(tile.rect.x, tile.imageRect.y + tile.imageRect.height,
                           tile.rect.width, tile.rect.height -
                                   (tile.imageRect.y + tile.imageRect.height - tile.rect.y));
        }

        int pad = 8;
        int tx = area.x + pad;
        int ty = area.y + std::max(0, (area.height - blockH) / 2);

        if (!item.title.empty()) {
            ctx->SetFontStyle(titleFs);
            ctx->SetTextPaint(overlay ? Color(255, 255, 255, 255) : config.captionColor);
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(area));
            ctx->DrawText(item.title, Point2Dd(tx, ty));
            ctx->PopState();
            ty += titleH + innerGap;
        }
        if (!item.subtitle.empty()) {
            bool isLink = !item.link.empty();
            ctx->SetFontStyle(subFs);
            Color subColor = isLink ? config.linkColor
                                    : (overlay ? Color(230, 230, 230, 255)
                                               : config.subtitleColor);
            ctx->SetTextPaint(subColor);
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(area));

            // Optional link icon (e.g. a YouTube badge) before the link text.
            int textX = tx;
            int iconW = 0;
            if (isLink && !item.linkIconPath.empty()) {
                auto icon = UCImage::Get(item.linkIconPath);
                if (icon && icon->GetWidth() > 0) {
                    int isz = std::max(1, subH);   // square, matched to the line height
                    ctx->DrawImage(*icon, Rect2Dd(tx, ty, isz, isz), ImageFitMode::Contain);
                    iconW = isz + 4;               // icon + gap before the text
                    textX = tx + iconW;
                }
            }

            ctx->DrawText(item.subtitle, Point2Dd(textX, ty));
            if (isLink) {
                int tw = ctx->GetTextLineWidth(item.subtitle);
                int maxW = area.width - 2 * pad - iconW;
                if (maxW > 0 && tw > maxW) tw = maxW;
                if (config.linkUnderline) {
                    double uy = ty + subH - 1.0;
                    ctx->SetStrokePaint(config.linkColor);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLine(Point2Dd(textX, uy), Point2Dd(textX + tw, uy));
                }
                // Hit rect (icon + text) recorded in content space (matched against
                // scrolled local coordinates by LinkAt, like the action-icon hits).
                linkHits.push_back({ Rect2Di(tx, ty, iconW + std::max(1, tw),
                                             std::max(1, subH)), tile.itemIndex });
            }
            ctx->PopState();
        }
    }

    float UltraCanvasAlbum::ResolveImageRadius() const {
        return config.imageCornerRadius < 0.0f ? config.cornerRadius
                                               : config.imageCornerRadius;
    }

    Rect2Di UltraCanvasAlbum::TextBlockRect(const TileLayout& tile) const {
        if (config.captionPlacement == AlbumCaptionPlacement::BelowImage) {
            int y = tile.imageRect.y + tile.imageRect.height;
            int h = (tile.rect.y + tile.rect.height) - y;
            if (h > 0) return Rect2Di(tile.rect.x, y, tile.rect.width, h);
        }
        // No below-image strip (overlay / hidden captions): fall back to the image.
        return tile.imageRect;
    }

    Rect2Di UltraCanvasAlbum::ActionAnchorRect(const TileLayout& tile,
                                               bool& fromLeft, bool& fromTop) const {
        bool textBlock = false;
        switch (config.actionAnchor) {
            case AlbumActionAnchor::TopLeftImage:         fromLeft = true;  fromTop = true;  break;
            case AlbumActionAnchor::TopRightImage:        fromLeft = false; fromTop = true;  break;
            case AlbumActionAnchor::BottomLeftImage:      fromLeft = true;  fromTop = false; break;
            case AlbumActionAnchor::BottomRightImage:     fromLeft = false; fromTop = false; break;
            case AlbumActionAnchor::TopLeftTextBlock:     fromLeft = true;  fromTop = true;  textBlock = true; break;
            case AlbumActionAnchor::TopRightTextBlock:    fromLeft = false; fromTop = true;  textBlock = true; break;
            case AlbumActionAnchor::BottomLeftTextBlock:  fromLeft = true;  fromTop = false; textBlock = true; break;
            case AlbumActionAnchor::BottomRightTextBlock: fromLeft = false; fromTop = false; textBlock = true; break;
        }
        return textBlock ? TextBlockRect(tile) : tile.imageRect;
    }

    void UltraCanvasAlbum::DrawActionButtonBg(IRenderContext* ctx, const Rect2Di& button) {
        switch (config.actionIconBackground) {
            case AlbumActionIconBackground::Square:
                ctx->SetFillPaint(config.actionIconBgColor);
                ctx->FillRectangle(Rect2Dd(button));
                break;
            case AlbumActionIconBackground::RoundedSquare: {
                double r = std::min(button.width, button.height) * 0.28;
                ctx->SetFillPaint(config.actionIconBgColor);
                ctx->FillRoundedRectangle(Rect2Dd(button), r);
                break;
            }
            case AlbumActionIconBackground::Round:
            default: {
                Point2Dd c(button.x + button.width / 2.0, button.y + button.height / 2.0);
                ctx->DrawFilledCircle(c, button.width / 2.0f, config.actionIconBgColor);
                break;
            }
        }
    }

    void UltraCanvasAlbum::DrawActionIcons(IRenderContext* ctx, const TileLayout& tile,
                                           bool hovered) {
        if (config.actionDisplay == AlbumActionDisplay::Hidden) return;

        std::vector<int> vis = VisibleActionIndices(tile.itemIndex);

        // Resolve the corner the icons hug, and the rect they anchor inside.
        bool fromLeft = false, fromTop = true;
        Rect2Di anchor = ActionAnchorRect(tile, fromLeft, fromTop);
        int sz  = static_cast<int>(config.actionButtonSize);
        int pad = 6;
        int bx  = fromLeft ? anchor.x + pad
                           : anchor.x + anchor.width - pad - sz;
        int by  = fromTop  ? anchor.y + pad
                           : anchor.y + anchor.height - pad - sz;

        // ContextMenu mode: optionally a single kebab icon; the menu itself opens
        // on right-click or when the kebab is clicked.
        if (config.actionDisplay == AlbumActionDisplay::ContextMenu) {
            if (!config.showMenuIcon || vis.empty()) return;
            Rect2Di btn(bx, by, sz, sz);
            DrawActionButtonBg(ctx, btn);
            Point2Dd c(btn.x + sz / 2.0, btn.y + sz / 2.0);
            // Three vertical dots.
            ctx->SetFillPaint(Color(255, 255, 255, 230));
            double dot = sz * 0.09;
            for (int k = -1; k <= 1; ++k) {
                ctx->DrawFilledCircle(Point2Dd(c.x, c.y + k * sz * 0.22),
                                      static_cast<float>(dot), Color(255, 255, 255, 230));
            }
            actionHits.push_back({btn, tile.itemIndex, -1});
            return;
        }

        // Always / OnHover: a row of icon buttons growing away from the corner.
        if (config.actionDisplay == AlbumActionDisplay::OnHover && !hovered) return;
        if (vis.empty()) return;

        int gap  = 4;
        int step = sz + gap;
        int x = bx;
        for (int idx : vis) {
            Rect2Di btn(x, by, sz, sz);
            // Stop once a button would spill past the far edge of the anchor rect.
            if (fromLeft) { if (btn.x + sz > anchor.x + anchor.width) break; }
            else          { if (btn.x < anchor.x) break; }
            DrawActionButtonBg(ctx, btn);
            DrawIconGlyph(ctx, &actions[idx], btn);
            actionHits.push_back({btn, tile.itemIndex, idx});
            x += fromLeft ? step : -step;
        }
    }

    void UltraCanvasAlbum::DrawIconGlyph(IRenderContext* ctx, const AlbumAction* action,
                                         const Rect2Di& button) {
        // Prefer a supplied icon image; otherwise fall back to the first letter.
        if (action && !action->iconPath.empty()) {
            auto img = UCImage::Get(action->iconPath);
            if (img && img->GetWidth() > 0) {
                int inset = std::max(4, button.width / 5);
                Rect2Di ir(button.x + inset, button.y + inset,
                           button.width - 2 * inset, button.height - 2 * inset);
                ctx->DrawImage(*img, Rect2Dd(ir), ImageFitMode::Contain);
                return;
            }
        }
        std::string glyph = (action && !action->label.empty())
                ? std::string(1, action->label[0]) : "•";
        FontStyle fs;
        fs.fontFamily = config.fontFamily;
        fs.fontSize = button.height * 0.5;
        fs.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fs);
        ctx->SetTextPaint(Color(255, 255, 255, 235));
        Size2Di ts = ctx->GetTextLineDimensions(glyph);
        ctx->DrawText(glyph, Point2Dd(button.x + (button.width - ts.width) / 2.0,
                                      button.y + (button.height - ts.height) / 2.0));
    }

    void UltraCanvasAlbum::DrawScrollbar(IRenderContext* ctx) {
        ScrollbarGeom g = ScrollbarGeometry();
        if (!g.active) return;
        // The thumb brightens while it is being dragged so the grab is visible.
        Color thumbColor = draggingScrollbar ? Color(70, 70, 78, 220)
                                             : Color(90, 90, 96, 180);
        if (g.horizontal) {
            ctx->SetFillPaint(Color(0, 0, 0, 30));
            ctx->FillRoundedRectangle(Rect2Dd(g.track.x + 2, g.track.y,
                                              g.track.width - 4, g.track.height), 3);
        } else {
            ctx->SetFillPaint(Color(0, 0, 0, 30));
            ctx->FillRoundedRectangle(Rect2Dd(g.track.x, g.track.y + 2,
                                              g.track.width, g.track.height - 4), 3);
        }
        ctx->SetFillPaint(thumbColor);
        ctx->FillRoundedRectangle(Rect2Dd(g.thumb), 3);
    }

    void UltraCanvasAlbum::DrawDragGhost(IRenderContext* ctx) {
        if (dragItem < 0 || dragItem >= static_cast<int>(items.size())) return;
        // A small translucent marker following the cursor during a reorder.
        int sz = 54;
        Rect2Di g(dragCurrent.x - sz / 2, dragCurrent.y - sz / 2, sz, sz);
        ctx->PushState();
        ctx->SetAlpha(0.8);
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRoundedRectangle(Rect2Dd(g), 6);
        ctx->ClipRoundedRectangle(Rect2Dd(g), 6, 6, 6, 6);
        DrawImageInRect(ctx, items[dragItem], g, 1.0f);
        ctx->PopState();
        ctx->SetStrokePaint(config.selectionColor);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(g), 6);
    }

    // ===== HIT TESTING =====
    Point2Di UltraCanvasAlbum::ToContentPoint(const Point2Di& localPoint) const {
        return Point2Di(localPoint.x + scrollOffsetX, localPoint.y + scrollOffsetY);
    }

    int UltraCanvasAlbum::TileAt(const Point2Di& contentPoint) const {
        for (const auto& t : tiles) {
            if (t.rect.Contains(contentPoint)) return static_cast<int>(t.itemIndex);
        }
        return -1;
    }

    int UltraCanvasAlbum::ActionAt(const Point2Di& localPoint, size_t& outItem,
                                   bool& outIsMenu) const {
        // actionHits were recorded in content space minus scroll (i.e. screen-local
        // after the Translate), so compare against local coordinates directly.
        Point2Di p(localPoint.x, localPoint.y);
        for (const auto& h : actionHits) {
            Rect2Di screenRect(h.rect.x - scrollOffsetX, h.rect.y - scrollOffsetY,
                               h.rect.width, h.rect.height);
            if (screenRect.Contains(p)) {
                outItem = h.itemIndex;
                outIsMenu = (h.actionIndex < 0);
                return h.actionIndex;
            }
        }
        return -2;  // -2 = nothing; -1 is reserved for the kebab/menu hit
    }

    int UltraCanvasAlbum::LinkAt(const Point2Di& localPoint) const {
        // linkHits are in content space; compare against scrolled local coords,
        // matching ActionAt's convention.
        for (const auto& h : linkHits) {
            Rect2Di screenRect(h.rect.x - scrollOffsetX, h.rect.y - scrollOffsetY,
                               h.rect.width, h.rect.height);
            if (screenRect.Contains(localPoint)) return static_cast<int>(h.itemIndex);
        }
        return -1;
    }

    void UltraCanvasAlbum::TriggerAction(int actionIndex, size_t itemIndex) {
        if (actionIndex < 0 || actionIndex >= static_cast<int>(actions.size())) return;
        if (actions[actionIndex].onTrigger) actions[actionIndex].onTrigger(itemIndex);
    }

    void UltraCanvasAlbum::OpenItemContextMenu(size_t itemIndex, const Point2Di& localPoint) {
        std::vector<int> vis = VisibleActionIndices(itemIndex);
        if (vis.empty()) return;
        auto win = GetWindow();
        if (!win) return;

        activePopupMenu = std::make_shared<UltraCanvasMenu>(
                GetIdentifier() + "_ctx", 0, 0, 200, 0);
        activePopupMenu->SetMenuType(MenuType::PopupMenu);
        for (int idx : vis) {
            const AlbumAction& a = actions[idx];
            size_t captured = itemIndex;
            int actionIdx = idx;
            if (!a.iconPath.empty()) {
                activePopupMenu->AddItem(MenuItemData::Action(
                        a.label, a.iconPath,
                        [this, actionIdx, captured]() { TriggerAction(actionIdx, captured); }));
            } else {
                activePopupMenu->AddItem(MenuItemData::Action(
                        a.label,
                        [this, actionIdx, captured]() { TriggerAction(actionIdx, captured); }));
            }
        }

        Point2Di winPos(static_cast<int>(GetXInWindow()) + localPoint.x,
                        static_cast<int>(GetYInWindow()) + localPoint.y);
        PopupElementSettings settings;
        settings.popupOwner = weak_from_this();
        activePopupMenu->OpenMenu(winPos, *win, settings);
    }

    void UltraCanvasAlbum::FinishDrag() {
        if (dragItem >= 0 && dragItem < static_cast<int>(items.size())) {
            Point2Di cp = ToContentPoint(dragCurrent);
            int target = TileAt(cp);
            if (target >= 0 && target != dragItem) {
                MoveItem(static_cast<size_t>(dragItem), static_cast<size_t>(target));
            }
        }
        dragging = false;
        dragArmed = false;
        dragItem = -1;
        RequestRedraw();
    }

    // ===== EVENTS =====
    bool UltraCanvasAlbum::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseLeave: {
                if (hoveredItem != -1) { hoveredItem = -1; RequestRedraw(); }
                if (hoveredActionIndex != -2) {
                    hoveredActionIndex = -2;
                    UltraCanvasTooltipManager::HideTooltip();
                }
                dragArmed = false;
                draggingScrollbar = false;
                return true;
            }
            case UCEventType::MouseWheel: {
                if (IsHorizontal()) {
                    if (MaxScrollX() <= 0) return false;
                    scrollOffsetX -= event.wheelDelta * kWheelStep;
                } else {
                    if (MaxScrollY() <= 0) return false;
                    scrollOffsetY -= event.wheelDelta * kWheelStep;
                }
                ClampScroll();
                RequestRedraw();
                return true;
            }
            case UCEventType::MouseMove: {
                Point2Di local(event.pointer.x, event.pointer.y);
                dragCurrent = local;

                // Dragging the scrollbar thumb tracks the cursor.
                if (draggingScrollbar) {
                    ScrollThumbTo((IsHorizontal() ? local.x : local.y)
                                  - scrollbarGrabOffset);
                    RequestRedraw();
                    return true;
                }

                if (dragArmed && !dragging) {
                    int dx = local.x - dragStart.x;
                    int dy = local.y - dragStart.y;
                    if (dx * dx + dy * dy >= kDragThreshold * kDragThreshold) {
                        dragging = true;
                    }
                }
                int newHover = TileAt(ToContentPoint(local));
                if (dragging || newHover != hoveredItem) {
                    hoveredItem = newHover;
                    RequestRedraw();
                }

                // Hand cursor while hovering a clickable subtitle link.
                int linkHover = LinkAt(local);
                if (linkHover != hoveredLinkItem) {
                    hoveredLinkItem = linkHover;
                    SetMouseCursor(linkHover >= 0 ? UCMouseCursor::Hand
                                                  : UCMouseCursor::Default);
                }

                // Tooltip for the action icon under the cursor (Delete / Edit /
                // Add comment / ...). Show it once when the cursor enters an icon.
                size_t tipItem = 0;
                bool   tipIsMenu = false;
                int    tipAction = ActionAt(local, tipItem, tipIsMenu);
                if (tipAction != hoveredActionIndex || tipItem != hoveredActionItem) {
                    hoveredActionIndex = tipAction;
                    hoveredActionItem  = tipItem;
                    auto win = GetWindow();
                    if (win && tipAction >= 0 &&
                        tipAction < static_cast<int>(actions.size()) &&
                        !actions[tipAction].label.empty()) {
                        UltraCanvasTooltipManager::UpdateAndShowTooltip(
                                win, actions[tipAction].label,
                                Point2Di(event.pointerWindow.x, event.pointerWindow.y));
                    } else {
                        UltraCanvasTooltipManager::HideTooltip();
                    }
                }
                return false;
            }
            case UCEventType::MouseDown: {
                Point2Di local(event.pointer.x, event.pointer.y);

                if (event.button == UCMouseButton::Right) {
                    int item = TileAt(ToContentPoint(local));
                    if (item >= 0) {
                        OpenItemContextMenu(static_cast<size_t>(item), local);
                        return true;
                    }
                    return false;
                }
                if (event.button != UCMouseButton::Left) return false;

                // Scrollbar interaction takes precedence over the tiles beneath it.
                {
                    ScrollbarGeom g = ScrollbarGeometry();
                    if (g.active) {
                        // Expand the thin visual track into a comfortable target.
                        Rect2Di hit = g.track;
                        if (g.horizontal) { hit.y -= 6; hit.height += 8; }
                        else              { hit.x -= 6; hit.width  += 8; }
                        if (hit.Contains(local)) {
                            if (g.thumb.Contains(local)) {
                                scrollbarGrabOffset = g.horizontal ? (local.x - g.thumb.x)
                                                                   : (local.y - g.thumb.y);
                            } else {
                                // Clicked the track: centre the thumb on the
                                // cursor, then keep dragging from there.
                                scrollbarGrabOffset =
                                        (g.horizontal ? g.thumb.width : g.thumb.height) / 2;
                                ScrollThumbTo((g.horizontal ? local.x : local.y)
                                              - scrollbarGrabOffset);
                            }
                            draggingScrollbar = true;
                            RequestRedraw();
                            return true;
                        }
                    }
                }

                // Action buttons take precedence over the tile.
                size_t hitItem = 0;
                bool isMenu = false;
                int act = ActionAt(local, hitItem, isMenu);
                if (act != -2) {
                    if (isMenu) OpenItemContextMenu(hitItem, local);
                    else        TriggerAction(act, hitItem);
                    return true;
                }

                // Subtitle link row: takes precedence over a plain tile click.
                int linkItem = LinkAt(local);
                if (linkItem >= 0) {
                    if (onLinkClicked) onLinkClicked(static_cast<size_t>(linkItem));
                    return true;
                }

                int item = TileAt(ToContentPoint(local));
                if (item >= 0) {
                    dragItem = item;
                    dragStart = local;
                    dragCurrent = local;
                    // Reordering only in edit / admin modes.
                    dragArmed = (config.mode != AlbumMode::Display);
                    return true;
                }
                return false;
            }
            case UCEventType::MouseUp: {
                if (event.button != UCMouseButton::Left) return false;
                if (draggingScrollbar) {
                    draggingScrollbar = false;
                    RequestRedraw();
                    return true;
                }
                if (dragging) { FinishDrag(); return true; }

                if (dragItem >= 0) {
                    Point2Di local(event.pointer.x, event.pointer.y);
                    int upItem = TileAt(ToContentPoint(local));
                    if (upItem == dragItem) {
                        if (config.allowSelection && config.mode != AlbumMode::Display) {
                            ToggleSelection(static_cast<size_t>(dragItem));
                        }
                        if (onItemClicked) onItemClicked(static_cast<size_t>(dragItem));
                    }
                }
                dragArmed = false;
                dragItem = -1;
                return true;
            }
            case UCEventType::MouseDoubleClick: {
                Point2Di local(event.pointer.x, event.pointer.y);
                int item = TileAt(ToContentPoint(local));
                if (item >= 0 && onItemActivated) {
                    onItemActivated(static_cast<size_t>(item));
                    return true;
                }
                return false;
            }
            default:
                break;
        }
        return UltraCanvasContainer::OnEvent(event);
    }

} // namespace UltraCanvas
