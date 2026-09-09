// core/UltraCanvasFontViewer.cpp
// The glyph browser - see UltraCanvasFontViewer.h.
//
// A self-rendered view in the sense the house rules allow: the grid of glyph
// cells is *content*, painted the way the filer paints its tiles and the album
// its thumbnails. Everything that takes input is a real element - the range
// and face pickers are UltraCanvasDropdown, the size control an
// UltraCanvasSlider, the information line an UltraCanvasLabel, and the bar on
// the right an UltraCanvasScrollbar. Nothing here hand-rolls a control.
//
// Glyphs are rasterized through UltraCanvasFontFace, one cell at a time, and
// cached by (entry, device-pixel cell edge). Scrolling therefore only costs
// the cells that newly come into view, and changing the size drops the cache
// rather than showing stale sizes.
// Version: 1.0.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework

#include "UltraCanvasFontViewer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace UltraCanvas {

    namespace {
        // Below this a glyph is a smudge; above it one cell fills the pane.
        constexpr int kMinCellSize = 20;
        constexpr int kMaxCellSize = 200;
        constexpr int kControlBarHeight = 30;
        constexpr int kInfoBarHeight = 22;
        constexpr int kScrollbarWidth = 14;
        constexpr int kWheelStep = 60;

        std::string FormatCodepoint(uint32_t cp) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "U+%04X", cp);
            return buf;
        }
    } // namespace

    UltraCanvasFontViewer::UltraCanvasFontViewer(const std::string& identifier,
                                                 float x, float y, float w, float h)
        : UltraCanvasContainer(identifier, x, y, w, h) {
        // The shared animator every self-rendered view scrolls with, so a
        // wheel notch here eases exactly as it does in the filer.
        scrollAnim.Bind([this] { return static_cast<double>(scrollOffsetY); },
                        [this](double v) {
                            scrollOffsetY = static_cast<int>(std::lround(v));
                            SyncScrollbar();
                            RequestRedraw();
                        });
        BuildControls();
    }

    UltraCanvasFontViewer::~UltraCanvasFontViewer() = default;

    // ===== CHROME =====

    void UltraCanvasFontViewer::BuildControls() {
        facePicker = CreateDropdown(GetIdentifier() + "-face", 0, 0, 150, 24);
        facePicker->onSelectionChanged = [this](int index, const DropdownItem&) {
            if (syncingControls) return;
            SetFaceIndex(index);
        };
        facePicker->SetVisible(false);   // only a collection needs it
        AddChild(facePicker);

        rangePicker = CreateDropdown(GetIdentifier() + "-range", 0, 0, 220, 24);
        rangePicker->onSelectionChanged = [this](int index, const DropdownItem&) {
            if (syncingControls) return;
            if (index >= 0) ShowRange(static_cast<size_t>(index));
        };
        AddChild(rangePicker);

        sizeSlider = CreateHorizontalSlider(GetIdentifier() + "-size", 0, 0, 120, 24,
                                            static_cast<float>(kMinCellSize),
                                            static_cast<float>(kMaxCellSize));
        sizeSlider->SetValue(static_cast<float>(cellSize));
        sizeSlider->onValueChanged = [this](float v) {
            if (syncingControls) return;
            SetCellSize(static_cast<int>(std::lround(v)));
        };
        AddChild(sizeSlider);

        infoLabel = CreateLabel(GetIdentifier() + "-info", 0, 0, 100, 18, "");
        AddChild(infoLabel);

        scrollbar = std::make_shared<UltraCanvasScrollbar>(
                GetIdentifier() + "-scroll", 0, 0, kScrollbarWidth, 100,
                ScrollbarOrientation::Vertical);
        scrollbar->onScrollChange = [this](int position) {
            if (syncingControls) return;
            // Dragging a thumb must track the pointer exactly rather than
            // chase it, so the animation is cancelled rather than retargeted.
            scrollAnim.Cancel();
            scrollOffsetY = position;
            ClampScroll();
            RequestRedraw();
        };
        AddChild(scrollbar);

        LayoutControls();
    }

    void UltraCanvasFontViewer::SetControlsVisible(bool visible) {
        if (controlsVisible == visible) return;
        controlsVisible = visible;
        if (facePicker) facePicker->SetVisible(visible && faceCount > 1);
        if (rangePicker) rangePicker->SetVisible(visible);
        if (sizeSlider) sizeSlider->SetVisible(visible);
        if (infoLabel) infoLabel->SetVisible(visible);
        LayoutControls();
        ClampScroll();
        RequestRedraw();
    }

    void UltraCanvasFontViewer::SetStyle(const FontViewerStyle& s) {
        style = s;
        DropGlyphCache();
        LayoutControls();
        RequestRedraw();
    }

    // Chrome the viewer places itself has to say so in CSS terms. SetBounds
    // writes finalBounds only, and the parent's next Arrange pass overwrites
    // it: an in-flow child is re-stacked at the top-left however it was
    // placed, which puts the whole control bar in one column over the grid.
    static void PlaceChild(const std::shared_ptr<UltraCanvasUIElement>& el,
                           float x, float y, float w, float h) {
        if (!el) return;
        el->SetElementSize(Size2Df(w, h));
        el->SetElementAbsolutePosition(Point2Df(x, y));
        el->SetBounds(Rect2Df(x, y, w, h));
    }

    void UltraCanvasFontViewer::LayoutControls() {
        const float w = GetWidth(), h = GetHeight();
        if (w <= 0 || h <= 0) return;
        const float pad = static_cast<float>(style.contentPadding);

        if (controlsVisible) {
            float cursor = pad;
            const float rowY = (kControlBarHeight - 24) * 0.5f;
            if (facePicker) {
                // Placed whether or not it is shown: only a collection needs
                // it, but the rule "every control is out of flow" holds for
                // all of them or it holds for none.
                const bool collection = faceCount > 1;
                facePicker->SetVisible(collection);
                PlaceChild(facePicker, cursor, rowY, 150, 24);
                if (collection) cursor += 150 + pad;
            }
            if (rangePicker) {
                rangePicker->SetVisible(true);
                // Give the range picker whatever is left before the slider.
                const float sliderRoom = 120 + pad * 2;
                const float width = std::min(
                        280.0f, std::max(120.0f, w - cursor - sliderRoom - pad));
                PlaceChild(rangePicker, cursor, rowY, width, 24);
                cursor += width + pad;
            }
            if (sizeSlider) {
                sizeSlider->SetVisible(true);
                PlaceChild(sizeSlider, std::max(cursor, w - 120 - pad), rowY, 120, 24);
            }
            if (infoLabel) {
                infoLabel->SetVisible(true);
                PlaceChild(infoLabel, pad, h - kInfoBarHeight + 3.0f,
                           std::max(10.0f, w - pad * 2), 16);
            }
        }

        if (scrollbar) {
            const Rect2Di grid = GridArea();
            PlaceChild(scrollbar, static_cast<float>(grid.x + grid.width),
                       static_cast<float>(grid.y),
                       static_cast<float>(kScrollbarWidth),
                       static_cast<float>(std::max(1, grid.height)));
        }
        SyncScrollbar();
    }

    void UltraCanvasFontViewer::ReflowForSize() {
        const float w = GetWidth(), h = GetHeight();
        if (w == reflowedWidth && h == reflowedHeight) return;
        reflowedWidth = w;
        reflowedHeight = h;
        // The column count is a function of the width, so everything derived
        // from it - the row count, the scroll range, where each cell sits -
        // changes with a resize. Re-flow before anything reads them.
        LayoutControls();
        ClampScroll();
        RequestRedraw();
    }

    void UltraCanvasFontViewer::SetBounds(const Rect2Df& b) {
        UltraCanvasContainer::SetBounds(b);
        ReflowForSize();
    }

    void UltraCanvasFontViewer::Arrange(const Rect2Df& finalRect,
                                        const CSSLayout::LayoutContext& ctx) {
        UltraCanvasContainer::Arrange(finalRect, ctx);
        // A viewer inside a flex or grid parent is sized here and never
        // through SetBounds, so without this the control bar keeps whatever
        // placement it was given at construction - which, at a size of zero,
        // is none at all, and the layout engine stacks it in a column.
        ReflowForSize();
    }

    // ===== THE FILE =====

    bool UltraCanvasFontViewer::LoadFont(const std::string& filePath, int faceIndex) {
        CloseFont();
        if (!face.Open(filePath, faceIndex)) {
            UpdateInfoText();
            RequestRedraw();
            return false;
        }
        // How many faces the container holds. ReadFontFileInfo is the only
        // thing that answers that, and it is one extra open of the header.
        FontFileInfo info;
        faceCount = ReadFontFileInfo(filePath, info) ? info.faceCount : 1;

        scrollOffsetY = 0;
        selectedEntry = NoEntry;
        hoveredEntry = NoEntry;
        RebuildFaceList();
        RebuildRangeList();
        LayoutControls();
        ClampScroll();
        UpdateInfoText();
        RequestRedraw();
        return true;
    }

    void UltraCanvasFontViewer::CloseFont() {
        face.Close();
        faceCount = 0;
        scrollOffsetY = 0;
        selectedEntry = NoEntry;
        hoveredEntry = NoEntry;
        scrollAnim.Cancel();
        DropGlyphCache();
        RebuildFaceList();
        RebuildRangeList();
        UpdateInfoText();
        RequestRedraw();
    }

    bool UltraCanvasFontViewer::SetFaceIndex(int index) {
        if (!face.IsOpen() || index == face.FaceIndex()) return false;
        const std::string path = face.Path();
        if (index < 0 || (faceCount > 0 && index >= faceCount)) return false;
        return LoadFont(path, index);
    }

    void UltraCanvasFontViewer::RebuildFaceList() {
        if (!facePicker) return;
        syncingControls = true;
        facePicker->ClearItems();
        if (face.IsOpen() && faceCount > 1) {
            FontFileInfo info;
            if (ReadFontFileInfo(face.Path(), info)) {
                for (const FontFaceInfo& f : info.faces) {
                    std::string label = f.fullName.empty() ? f.family : f.fullName;
                    if (label.empty()) label = "Face " + std::to_string(f.index + 1);
                    facePicker->AddItem(label);
                }
            }
            facePicker->SetSelectedIndex(face.FaceIndex(), false);
            facePicker->SetVisible(controlsVisible);
        } else {
            facePicker->SetVisible(false);
        }
        syncingControls = false;
    }

    void UltraCanvasFontViewer::RebuildRangeList() {
        if (!rangePicker) return;
        syncingControls = true;
        rangePicker->ClearItems();
        for (const FontCoverageRange& r : face.Ranges()) {
            rangePicker->AddItem(r.name + "  (" + std::to_string(r.count) + ")");
        }
        if (!face.Ranges().empty()) rangePicker->SetSelectedIndex(0, false);
        syncingControls = false;
    }

    // ===== GEOMETRY =====

    int UltraCanvasFontViewer::CellStride() const {
        return cellSize + style.cellSpacing + style.captionHeight;
    }

    Rect2Di UltraCanvasFontViewer::GridArea() const {
        const int w = static_cast<int>(GetWidth());
        const int h = static_cast<int>(GetHeight());
        const int top = controlsVisible ? kControlBarHeight : 0;
        const int bottom = controlsVisible ? kInfoBarHeight : 0;
        return Rect2Di(style.contentPadding, top + style.contentPadding,
                       std::max(0, w - style.contentPadding * 2 - kScrollbarWidth),
                       std::max(0, h - top - bottom - style.contentPadding * 2));
    }

    int UltraCanvasFontViewer::GetColumnCount() const {
        const Rect2Di grid = GridArea();
        const int stride = cellSize + style.cellSpacing;
        if (stride <= 0 || grid.width <= 0) return 1;
        return std::max(1, (grid.width + style.cellSpacing) / stride);
    }

    int UltraCanvasFontViewer::GetRowCount() const {
        const size_t n = face.Glyphs().size();
        if (n == 0) return 0;
        const int columns = GetColumnCount();
        return static_cast<int>((n + columns - 1) / columns);
    }

    int UltraCanvasFontViewer::GetMaxScroll() const {
        const Rect2Di grid = GridArea();
        const int content = GetRowCount() * CellStride();
        return std::max(0, content - grid.height);
    }

    Rect2Di UltraCanvasFontViewer::CellRect(size_t entry) const {
        const Rect2Di grid = GridArea();
        const int columns = GetColumnCount();
        const int row = static_cast<int>(entry) / columns;
        const int col = static_cast<int>(entry) % columns;
        return Rect2Di(grid.x + col * (cellSize + style.cellSpacing),
                       grid.y + row * CellStride() - scrollOffsetY,
                       cellSize, cellSize + style.captionHeight);
    }

    size_t UltraCanvasFontViewer::EntryAtPoint(int localX, int localY) const {
        const Rect2Di grid = GridArea();
        if (localX < grid.x || localX >= grid.x + grid.width) return NoEntry;
        if (localY < grid.y || localY >= grid.y + grid.height) return NoEntry;
        const int columns = GetColumnCount();
        const int stride = cellSize + style.cellSpacing;
        const int col = (localX - grid.x) / stride;
        // A point in the gap between two columns belongs to neither.
        if (col >= columns || (localX - grid.x) - col * stride >= cellSize)
            return NoEntry;
        const int row = (localY - grid.y + scrollOffsetY) / CellStride();
        if (row < 0) return NoEntry;
        const size_t entry = static_cast<size_t>(row) * columns + col;
        if (entry >= face.Glyphs().size()) return NoEntry;
        return entry;
    }

    void UltraCanvasFontViewer::ClampScroll() {
        scrollOffsetY = std::clamp(scrollOffsetY, 0, GetMaxScroll());
        SyncScrollbar();
    }

    void UltraCanvasFontViewer::SyncScrollbar() {
        syncingControls = true;
        if (scrollbar) {
            const Rect2Di grid = GridArea();
            scrollbar->SetScrollDimensions(std::max(1, grid.height),
                                           std::max(1, GetRowCount() * CellStride()));
            scrollbar->SetScrollPosition(scrollOffsetY);
            // A font that fits, or no font at all, has nothing to scroll: a
            // full-height thumb down the edge is noise.
            scrollbar->SetVisible(GetMaxScroll() > 0);
        }
        // The picker names the range you are looking at, not the last one
        // picked: a wheel out of Basic Latin and into Latin Extended-A has to
        // move it, or it stands there contradicting the grid. Every scroll
        // path passes through here, which is why the two travel together.
        if (rangePicker && !face.Ranges().empty()) {
            const size_t top = static_cast<size_t>(
                    (scrollOffsetY / std::max(1, CellStride())) * GetColumnCount());
            const auto& ranges = face.Ranges();
            size_t which = 0;
            for (size_t i = 0; i < ranges.size(); ++i) {
                if (ranges[i].firstEntry > top) break;
                which = i;
            }
            if (rangePicker->GetSelectedIndex() != static_cast<int>(which))
                rangePicker->SetSelectedIndex(static_cast<int>(which), false);
        }
        syncingControls = false;
    }

    // ===== NAVIGATION =====

    void UltraCanvasFontViewer::SetCellSize(int pixels) {
        const int next = std::clamp(pixels, kMinCellSize, kMaxCellSize);
        if (next == cellSize) return;
        // Keep the glyph at the top of the view in place across a resize,
        // otherwise dragging the slider walks the reader through the font.
        const int columns = GetColumnCount();
        const size_t topEntry = static_cast<size_t>(
                (scrollOffsetY / std::max(1, CellStride())) * columns);
        cellSize = next;
        DropGlyphCache();
        if (sizeSlider) {
            syncingControls = true;
            sizeSlider->SetValue(static_cast<float>(cellSize));
            syncingControls = false;
        }
        scrollAnim.Cancel();
        ScrollToEntry(topEntry);
        RequestRedraw();
    }

    void UltraCanvasFontViewer::ShowRange(size_t rangeIndex) {
        if (rangeIndex >= face.Ranges().size()) return;
        ScrollToEntry(face.Ranges()[rangeIndex].firstEntry);
    }

    void UltraCanvasFontViewer::ScrollToCodepoint(uint32_t codepoint) {
        const size_t entry = face.FindCodepoint(codepoint);
        if (entry < face.Glyphs().size()) ScrollToEntry(entry);
    }

    void UltraCanvasFontViewer::ScrollToEntry(size_t entry) {
        if (entry >= face.Glyphs().size()) return;
        const int columns = GetColumnCount();
        const int row = static_cast<int>(entry) / columns;
        const Rect2Di grid = GridArea();
        const int rowTop = row * CellStride();
        const int rowBottom = rowTop + CellStride();
        int target = scrollOffsetY;
        if (rowTop < scrollOffsetY) target = rowTop;
        else if (rowBottom > scrollOffsetY + grid.height)
            target = rowBottom - grid.height;
        target = std::clamp(target, 0, GetMaxScroll());
        if (target == scrollOffsetY) { SyncScrollbar(); return; }
        scrollAnim.Jump(target, 0, GetMaxScroll());
        scrollOffsetY = target;
        SyncScrollbar();
        RequestRedraw();
    }

    void UltraCanvasFontViewer::SetSelectedEntry(size_t entry) {
        const size_t next = entry < face.Glyphs().size() ? entry : NoEntry;
        if (next == selectedEntry) return;
        selectedEntry = next;
        if (selectedEntry != NoEntry) ScrollToEntry(selectedEntry);
        UpdateInfoText();
        RequestRedraw();
        if (onGlyphSelected) onGlyphSelected(selectedEntry);
    }

    void UltraCanvasFontViewer::MoveSelection(int columns, int rows) {
        if (face.Glyphs().empty()) return;
        const int perRow = GetColumnCount();
        long long base = selectedEntry == NoEntry
                                 ? 0
                                 : static_cast<long long>(selectedEntry);
        base += columns + static_cast<long long>(rows) * perRow;
        base = std::clamp<long long>(
                base, 0, static_cast<long long>(face.Glyphs().size()) - 1);
        SetSelectedEntry(static_cast<size_t>(base));
    }

    // ===== INFORMATION LINE =====

    void UltraCanvasFontViewer::UpdateInfoText() {
        if (!infoLabel) return;
        if (!face.IsOpen()) { infoLabel->SetText("No font loaded"); return; }

        const FontFaceInfo& info = face.Info();
        std::string text = info.family;
        if (!info.subfamily.empty() && info.subfamily != "Regular")
            text += " " + info.subfamily;
        text += "  -  " + std::to_string(face.Glyphs().size()) + " glyphs";
        if (!face.GlyphsAreByCodepoint())
            text += " (no character map: listed by glyph index)";

        const size_t shown = selectedEntry != NoEntry ? selectedEntry : hoveredEntry;
        if (shown != NoEntry && shown < face.Glyphs().size()) {
            const FontGlyphEntry& g = face.Glyphs()[shown];
            text += "   |   ";
            text += face.GlyphsAreByCodepoint()
                            ? FormatCodepoint(g.codepoint)
                            : ("glyph " + std::to_string(g.glyphIndex));
            const std::string name = face.GlyphName(shown);
            if (!name.empty()) text += "  " + name;
        }
        infoLabel->SetText(text);
    }

    // ===== GLYPH CELLS =====

    void UltraCanvasFontViewer::DropGlyphCache() { glyphCache.clear(); }

    std::shared_ptr<UCPixmap> UltraCanvasFontViewer::GlyphPixmap(size_t entry,
                                                                float deviceScale) {
        if (entry >= face.Glyphs().size()) return nullptr;
        const int devEdge = std::max(
                1, static_cast<int>(std::lround(cellSize * std::max(1.0f, deviceScale))));
        const uint64_t key = (static_cast<uint64_t>(entry) << 16) |
                             static_cast<uint64_t>(devEdge & 0xFFFF);
        auto it = glyphCache.find(key);
        if (it != glyphCache.end()) return it->second;

        // Bounded rather than clever: the cells that scrolled away are the
        // least likely to be wanted next, but tracking that costs more than
        // re-rasterizing a screenful, so a full cache is simply dropped.
        if (glyphCache.size() >= kGlyphCacheLimit) glyphCache.clear();

        FontGlyphOptions options;
        options.textColor = style.glyphColor;
        options.backgroundColor = Colors::Transparent;
        auto pm = face.RenderGlyph(entry, cellSize, cellSize, deviceScale, options);
        glyphCache.emplace(key, pm);
        return pm;
    }

    // ===== PAINT =====

    void UltraCanvasFontViewer::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx || !IsVisible()) return;
        // Everything below is in the element's own coordinates: the context
        // arrives translated to our origin, the way every other self-rendered
        // view in the framework paints.
        const Rect2Df local = GetLocalBounds();

        ctx->SetFillPaint(style.background);
        ctx->FillRectangle(Rect2Dd(local));

        if (face.Glyphs().empty()) {
            // An empty state that says which of the two reasons it is, in the
            // middle of the grid area - the whole element starts under the
            // control bar, which would hide the line behind the pickers.
            const Rect2Di area = GridArea();
            FontStyle fs;
            fs.fontFamily = style.fontFamily;
            fs.fontSize = style.fontSize + 1;
            ctx->SetFontStyle(fs);
            ctx->SetTextAlignment(TextAlignment::Center);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            ctx->SetTextPaint(style.captionColor);
            ctx->DrawTextInRect(face.IsOpen() ? "This font contains no glyphs"
                                              : "No font loaded",
                                Rect2Dd(area.x, area.y, area.width, area.height));
            UltraCanvasContainer::Render(ctx, dirtyRect);
            return;
        }

        const Rect2Di grid = GridArea();
        const float scale = ctx->GetDeviceScale();
        const int columns = GetColumnCount();
        const int stride = CellStride();

        // Only the rows that are on screen are rasterized: a CJK face has tens
        // of thousands of glyphs and a screenful is a hundred.
        const int firstRow = std::max(0, scrollOffsetY / stride);
        const int lastRow = std::min(GetRowCount() - 1,
                                     (scrollOffsetY + grid.height) / stride);

        ctx->ClipRect(Rect2Dd(grid.x, grid.y, grid.width, grid.height));

        FontStyle captionFont;
        captionFont.fontFamily = style.fontFamily;
        captionFont.fontSize = style.fontSize - 1;

        for (int row = firstRow; row <= lastRow; ++row) {
            for (int col = 0; col < columns; ++col) {
                const size_t entry = static_cast<size_t>(row) * columns + col;
                if (entry >= face.Glyphs().size()) break;

                const Rect2Di cell = CellRect(entry);
                const Rect2Dd box(cell.x, cell.y, cellSize, cellSize);

                const bool isSelected = entry == selectedEntry;
                const bool isHovered = entry == hoveredEntry;
                ctx->SetFillPaint(isSelected ? style.selectedBackground
                                             : isHovered ? style.hoverBackground
                                                         : style.cellBackground);
                ctx->FillRectangle(box);
                ctx->SetStrokePaint(isSelected ? style.selectedBorder
                                               : style.cellBorder);
                ctx->DrawRectangle(box);

                if (auto pm = GlyphPixmap(entry, scale)) {
                    ctx->DrawPixmap(*pm, box, ImageFitMode::Contain);
                }

                if (style.captionHeight > 0) {
                    const FontGlyphEntry& g = face.Glyphs()[entry];
                    ctx->SetFontStyle(captionFont);
                    ctx->SetTextPaint(style.captionColor);
                    ctx->DrawTextInRect(
                            face.GlyphsAreByCodepoint()
                                    ? FormatCodepoint(g.codepoint)
                                    : std::to_string(g.glyphIndex),
                            Rect2Dd(box.x, box.y + cellSize, cellSize,
                                    style.captionHeight));
                }
            }
        }
        ctx->ClearClipRect();

        // The children - pickers, slider, label, scrollbar - draw themselves.
        UltraCanvasContainer::Render(ctx, dirtyRect);
    }

    // ===== INPUT =====

    bool UltraCanvasFontViewer::OnEvent(const UCEvent& event) {
        // Children first: a dropdown that is open owns the pointer.
        if (UltraCanvasContainer::OnEvent(event)) return true;

        switch (event.type) {
            case UCEventType::MouseWheel: {
                if (GetMaxScroll() <= 0) return false;
                scrollAnim.AnimateBy(-event.wheelDelta * kWheelStep, 0, GetMaxScroll());
                return true;
            }
            case UCEventType::MouseMove: {
                const size_t under = EntryAtPoint(event.pointer.x, event.pointer.y);
                if (under != hoveredEntry) {
                    hoveredEntry = under;
                    UpdateInfoText();
                    RequestRedraw();
                }
                return false;   // hovering does not consume the move
            }
            case UCEventType::MouseLeave: {
                if (hoveredEntry != NoEntry) {
                    hoveredEntry = NoEntry;
                    UpdateInfoText();
                    RequestRedraw();
                }
                return false;
            }
            case UCEventType::MouseDown: {
                const size_t under = EntryAtPoint(event.pointer.x, event.pointer.y);
                if (under == NoEntry) return false;
                SetSelectedEntry(under);
                return true;
            }
            case UCEventType::MouseDoubleClick: {
                const size_t under = EntryAtPoint(event.pointer.x, event.pointer.y);
                if (under == NoEntry) return false;
                SetSelectedEntry(under);
                if (onGlyphActivated) onGlyphActivated(under);
                return true;
            }
            case UCEventType::KeyDown: {
                const Rect2Di grid = GridArea();
                const int page = std::max(1, grid.height / std::max(1, CellStride()));
                switch (event.virtualKey) {
                    case UCKeys::Left:  MoveSelection(-1, 0); return true;
                    case UCKeys::Right: MoveSelection(1, 0);  return true;
                    case UCKeys::Up:    MoveSelection(0, -1); return true;
                    case UCKeys::Down:  MoveSelection(0, 1);  return true;
                    case UCKeys::PageUp:   MoveSelection(0, -page); return true;
                    case UCKeys::PageDown: MoveSelection(0, page);  return true;
                    case UCKeys::Home:
                        SetSelectedEntry(0);
                        return true;
                    case UCKeys::End:
                        SetSelectedEntry(face.Glyphs().empty()
                                                 ? NoEntry
                                                 : face.Glyphs().size() - 1);
                        return true;
                    default: break;
                }
                return false;
            }
            default: break;
        }
        return false;
    }

} // namespace UltraCanvas
