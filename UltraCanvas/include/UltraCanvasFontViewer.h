// include/UltraCanvasFontViewer.h
// Browse every glyph in a font file: a scrolling grid of the font's own
// glyphs, a picker for the ranges it covers, and a size control.
//
// The companion to UltraCanvasFontFile's thumbnail. A specimen answers "what
// does this font look like"; this answers "what is actually in it" - which
// characters a font covers is the question you have when choosing between two
// downloads, and it is the one a single line of sample text cannot answer.
//
// The font does not have to be installed. The grid rasterizes through
// UltraCanvasFontFace, which goes straight at the file with FreeType, so a
// folder of candidates can be browsed before any of them is registered.
//
// Embedded by UltraCanvasMediaViewer as the display for MediaKind::Font, and
// usable on its own - a font manager, or a font picker that wants to show
// coverage rather than just a family name.
// Version: 1.0.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASFONTVIEWER_H
#define ULTRACANVASFONTVIEWER_H

#include "UltraCanvasContainer.h"
#include "UltraCanvasFontFile.h"
#include "UltraCanvasSmoothScroll.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasDropdown;
    class UltraCanvasSlider;
    class UltraCanvasLabel;
    class UltraCanvasScrollbar;

    // ===== HOW THE GRID LOOKS =====
    struct FontViewerStyle {
        Color background = Color(250, 250, 252, 255);
        Color cellBackground = Color(255, 255, 255, 255);
        Color cellBorder = Color(226, 226, 230, 255);
        Color glyphColor = Color(26, 26, 28, 255);
        Color hoverBackground = Color(232, 240, 254, 255);
        Color selectedBackground = Color(198, 220, 252, 255);
        Color selectedBorder = Color(66, 133, 244, 255);
        // The codepoint printed under each cell. Set captionHeight to 0 to
        // drop it and give the whole cell to the glyph.
        Color captionColor = Color(120, 124, 132, 255);
        int captionHeight = 12;
        int cellSpacing = 6;
        int contentPadding = 8;
        float fontSize = 11.0f;
        std::string fontFamily;   // empty = the application's default
    };

    // ===== THE GLYPH BROWSER =====
    class UltraCanvasFontViewer : public UltraCanvasContainer {
    public:
        UltraCanvasFontViewer(const std::string& identifier,
                              float x, float y, float w, float h);
        ~UltraCanvasFontViewer() override;

        // ===== THE FILE =====
        // Opens one face of a font file and shows its coverage. Returns false
        // when the file cannot be read; the previous font is closed either
        // way. Loading the file that is already shown re-reads it.
        bool LoadFont(const std::string& filePath, int faceIndex = 0);
        void CloseFont();
        bool IsFontLoaded() const { return face.IsOpen(); }
        const std::string& GetFontPath() const { return face.Path(); }
        // The open face, for a host that wants its metadata (family, glyph
        // count, licence) without re-reading the file.
        const UltraCanvasFontFace& GetFace() const { return face; }
        // Which face of a collection (.ttc) is shown, and how many there are.
        int GetFaceIndex() const { return face.FaceIndex(); }
        int GetFaceCount() const { return faceCount; }
        bool SetFaceIndex(int index);

        // ===== THE GRID =====
        // Edge of one glyph cell in logical pixels; clamped to a legible
        // range. Changing it rebuilds the layout and drops the glyph cache.
        void SetCellSize(int pixels);
        int GetCellSize() const { return cellSize; }

        // Scroll so that a range - an index into GetFace().Ranges() - or a
        // particular character is at the top of the view. Both are no-ops
        // when the font does not have them.
        void ShowRange(size_t rangeIndex);
        void ScrollToCodepoint(uint32_t codepoint);
        void ScrollToEntry(size_t entry);

        // ===== SELECTION =====
        // An index into GetFace().Glyphs(), or NoEntry when nothing is
        // selected. Selecting scrolls the glyph into view.
        static constexpr size_t NoEntry = static_cast<size_t>(-1);
        size_t GetSelectedEntry() const { return selectedEntry; }
        void SetSelectedEntry(size_t entry);
        // Fired when the selection changes, with NoEntry when it is cleared.
        std::function<void(size_t entry)> onGlyphSelected;
        // Fired when a cell is double-clicked - a host's cue to do something
        // with the character (insert it, copy it).
        std::function<void(size_t entry)> onGlyphActivated;

        // ===== CHROME =====
        // The control bar above the grid and the information line below it.
        // A host with its own chrome turns them off and drives the viewer
        // through the API instead.
        void SetControlsVisible(bool visible);
        bool AreControlsVisible() const { return controlsVisible; }
        void SetStyle(const FontViewerStyle& s);
        const FontViewerStyle& GetStyle() const { return style; }

        // ===== ELEMENT =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return true; }
        // Resizing re-flows the grid: the column count comes from the width,
        // so the scroll range and every cell position move with it. Both
        // entries matter - a viewer positioned by hand arrives through
        // SetBounds, one inside a flex or grid parent through Arrange, and the
        // layout engine never calls the former.
        void SetBounds(const Rect2Df& b) override;
        void Arrange(const Rect2Df& finalRect,
                     const CSSLayout::LayoutContext& ctx) override;

        // ===== LAYOUT, EXPOSED FOR TESTS =====
        // The grid geometry is the part worth pinning down: a wrong column
        // count or a scroll range that does not reach the last row is a bug
        // nobody sees until a font is open in front of them.
        int GetColumnCount() const;
        int GetRowCount() const;
        int GetMaxScroll() const;
        int GetScrollOffset() const { return scrollOffsetY; }
        // The entry under a point in the viewer's own coordinates, or NoEntry.
        size_t EntryAtPoint(int localX, int localY) const;
        // Where a cell sits, in the same coordinates - for a host anchoring a
        // popup over a glyph, and the inverse of EntryAtPoint for tests. The
        // rectangle covers the glyph and its caption, and is off-screen for an
        // entry that is scrolled out of view.
        Rect2Di GetCellRect(size_t entry) const { return CellRect(entry); }

    private:
        void BuildControls();
        void LayoutControls();
        void RebuildRangeList();
        void RebuildFaceList();
        void DropGlyphCache();
        void UpdateInfoText();
        void SyncScrollbar();
        void ClampScroll();
        // The tail both SetBounds and Arrange run: place the chrome, keep the
        // scroll in range, repaint. A no-op when the size has not changed,
        // since a layout pass can arrive without one.
        void ReflowForSize();
        // Cell edge in device pixels for the current scale, and the stride
        // from one cell to the next including the gap.
        int CellStride() const;
        Rect2Di GridArea() const;
        Rect2Di CellRect(size_t entry) const;
        std::shared_ptr<UCPixmap> GlyphPixmap(size_t entry, float deviceScale);
        void MoveSelection(int columns, int rows);

        UltraCanvasFontFace face;
        int faceCount = 0;
        FontViewerStyle style;

        // The size the chrome was last placed for, so a layout pass that does
        // not resize us does not re-place it.
        float reflowedWidth = -1.0f;
        float reflowedHeight = -1.0f;

        int cellSize = 56;
        int scrollOffsetY = 0;
        UltraCanvasSmoothScroll scrollAnim;

        size_t hoveredEntry = NoEntry;
        size_t selectedEntry = NoEntry;

        bool controlsVisible = true;
        std::shared_ptr<UltraCanvasDropdown> facePicker;
        std::shared_ptr<UltraCanvasDropdown> rangePicker;
        std::shared_ptr<UltraCanvasSlider> sizeSlider;
        std::shared_ptr<UltraCanvasLabel> infoLabel;
        std::shared_ptr<UltraCanvasScrollbar> scrollbar;
        // Suppress the control callbacks while the viewer moves them itself.
        bool syncingControls = false;

        // Rasterized cells, keyed by entry and by the device-pixel cell edge
        // they were drawn for, so a size change does not show stale sizes.
        // Bounded: a CJK face has tens of thousands of glyphs and only the
        // ones that have been on screen are worth keeping.
        std::unordered_map<uint64_t, std::shared_ptr<UCPixmap>> glyphCache;
        static constexpr size_t kGlyphCacheLimit = 4096;
    };

    inline std::shared_ptr<UltraCanvasFontViewer> CreateFontViewer(
            const std::string& identifier,
            float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasFontViewer>(identifier, x, y, w, h);
    }

} // namespace UltraCanvas

#endif // ULTRACANVASFONTVIEWER_H
