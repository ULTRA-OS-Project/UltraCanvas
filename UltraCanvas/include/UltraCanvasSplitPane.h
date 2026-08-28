// include/UltraCanvasSplitPane.h
// Split pane container that divides content into N draggable panes along one axis
// Version: 1.3.0
// Last Modified: 2026-08-28
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>

namespace UltraCanvas {

    class UltraCanvasSplitPane;

    enum class SplitOrientation {
        Horizontal, // panes laid out left-to-right; splitter is a vertical strip
        Vertical    // panes laid out top-to-bottom; splitter is a horizontal strip
    };

    // Shape of the optional grab handle drawn on the split line.
    enum class SplitterHandleShape {
        NoHandle,       // no handle (default) — the split line is a plain strip
        Square,         // sharp-cornered rectangle
        RoundedSquare,  // rectangle with SplitterHandleStyle::cornerRadius corners
        Round,          // fully rounded: a circle when square, a capsule when elongated
        Image           // the handle IS the image at SplitterHandleStyle::imagePath
                        // (SVG or raster) — no fill, border or grip is drawn
    };

    struct SplitterHandleStyle {
        SplitterHandleShape shape = SplitterHandleShape::NoHandle;

        // Extent across the split line (the "thickness" direction). The splitter
        // strip grows to fit this, so a 16 px handle on a 4 px splitter widens the
        // strip to 16 px and centres the 4 px line inside it.
        int crossSize = 16;
        // Extent along the split line. <= 0 means auto: crossSize for a bare
        // handle, or just enough to wrap the icon group when containsIcons is set.
        int axisLength = 0;
        // Corner radius for SplitterHandleShape::RoundedSquare.
        float cornerRadius = 4.0f;
        // Position along the split line, 0 = start, 0.5 = centred, 1 = end.
        float position = 0.5f;

        Color color       = Color(235, 235, 235, 255);
        Color hoverColor  = Color(205, 220, 248, 255);
        Color activeColor = Color(150, 185, 240, 255);
        Color borderColor = Color(160, 160, 160, 255);   // Transparent = no border
        float borderWidth = 1.0f;

        // Image (SVG or raster) drawn as, or inside, the handle. With
        // SplitterHandleShape::Image the image IS the handle. With any drawn
        // shape the image is centred on top of it and the grip is suppressed.
        // Supply an asset matching the orientation - a tall one for a Horizontal
        // split pane (vertical split line), a wide one for a Vertical one.
        std::string imagePath;
        ImageFitMode imageFit = ImageFitMode::Fill;
        // Draw the image as a mask tinted with imageColor, for monochrome
        // glyphs. Transparent imageColor follows the handle's state color.
        bool  imageAsMask = false;
        Color imageColor  = Colors::Transparent;

        // Grip lines drawn inside the handle. Suppressed when the handle carries
        // icons or an image, since those take that space.
        bool  showGrip      = true;
        int   gripLineCount = 3;
        int   gripSpacing   = 3;
        float gripThickness = 1.0f;
        Color gripColor     = Color(120, 120, 120, 255);

        // When true (default) the icon group of this splitter is drawn inside the
        // handle, and an auto-sized handle grows to wrap it. When false, handle and
        // icons are positioned independently (see SplitterIconStyle::position).
        bool containsIcons = true;
    };

    // One clickable action icon sitting on the split line.
    struct SplitPaneIcon {
        std::string id;         // caller-defined identifier, passed back on click
        std::string iconPath;   // image/SVG path; when empty, `label` is drawn instead
        std::string label;      // short text/glyph fallback, e.g. "\xE2\x80\xB9"
        std::string tooltip;
        bool enabled = true;
        bool visible = true;
        Color iconColor = Colors::Transparent;   // Transparent = use SplitterIconStyle
        std::function<void(size_t splitterIndex, size_t iconIndex)> onClick;
    };

    struct SplitterIconStyle {
        int size    = 16;   // icon box, square
        int spacing = 4;    // gap between consecutive icons
        int padding = 4;    // free space around the icon group
        // Position along the split line, used when the icons are not drawn inside
        // the handle (SplitterHandleStyle::containsIcons == false or no handle).
        float position = 0.5f;

        int   hoverPadding      = 2;     // hover/press background grows the icon box by this
        float hoverCornerRadius = 3.0f;

        bool  useIconAsMask = true;      // tint monochrome icons with the colors below
        Color iconColor         = Color(70, 70, 70, 255);
        Color iconHoverColor    = Color(20, 20, 20, 255);
        Color iconDisabledColor = Color(170, 170, 170, 255);
        Color hoverBackgroundColor   = Color(0, 0, 0, 30);
        Color pressedBackgroundColor = Color(0, 0, 0, 55);

        std::string labelFontFamily = "Arial";
        float       labelFontSize   = 12.0f;
    };

    struct SplitPaneStyle {
        int splitterThickness = 4;
        int splitterHitMargin = 0;   // extra grab room added to each side of the line
        Color splitterColor       = Color(220, 220, 220, 255);
        Color splitterHoverColor  = Color(180, 200, 240, 255);
        Color splitterActiveColor = Color(120, 160, 230, 255);
        bool  showSplitterBackground = true;

        SplitterHandleStyle handle;
        SplitterIconStyle   icons;
    };

    class UltraCanvasSplitter : public UltraCanvasUIElement {
    public:
        UltraCanvasSplitter(const std::string& id, SplitOrientation orient, UltraCanvasSplitPane* owner);

        void SetIndex(size_t i) { index = i; }
        size_t GetIndex() const { return index; }

        void SetStyle(const SplitPaneStyle& s) { style = s; }

        bool IsDragging() const { return dragging; }

        // Index of the icon under the pointer, or -1. Valid after the last MouseMove.
        int GetHoveredIconIndex() const { return hoveredIcon; }

        // Rect of the handle in element-local coordinates; empty when no handle.
        Rect2Df GetHandleRect() const { return handleRect; }

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        SplitOrientation orientation;
        UltraCanvasSplitPane* owner = nullptr;
        size_t index = 0;
        SplitPaneStyle style;

        bool dragging = false;
        int dragStartGlobalAxis = 0;

        // Geometry cache, recomputed from the current bounds/style/icon list.
        Rect2Df handleRect;                 // empty when shape == NoHandle
        std::vector<Rect2Df> iconRects;     // draw boxes, one per visible icon
        std::vector<Rect2Df> iconHitRects;  // slightly grown click targets
        std::vector<size_t>  iconIndices;   // maps the above onto the owner's icon list

        int hoveredIcon = -1;   // index into the owner's icon list
        int pressedIcon = -1;

        void UpdateGeometry();
        int  HitTestIcon(const Point2Df& localPoint) const;
        bool IsIconActionable(int iconIndex) const;
        void SetHoveredIcon(int iconIndex, const UCEvent& event);
        void DrawHandle(IRenderContext* ctx);
        void DrawIcons(IRenderContext* ctx);
    };

    class UltraCanvasSplitPane : public UltraCanvasContainer {
    public:
        UltraCanvasSplitPane(const std::string& id, float x, float y, float w, float h,
                             SplitOrientation orient);

        UltraCanvasSplitPane(const std::string& id, float w, float h, SplitOrientation orient)
            : UltraCanvasSplitPane(id, -1, -1, w, h, orient) {}

        UltraCanvasSplitPane(const std::string& id, SplitOrientation orient)
            : UltraCanvasSplitPane(id, -1, -1, -1, -1, orient) {}

        explicit UltraCanvasSplitPane(SplitOrientation orient)
            : UltraCanvasSplitPane("", -1, -1, -1, -1, orient) {}

        // ===== PANE MANAGEMENT =====
        std::shared_ptr<UltraCanvasContainer> AddPane(double weight = 1.0);
        std::shared_ptr<UltraCanvasContainer> InsertPane(size_t index, double weight = 1.0);
        void RemovePane(size_t index);
        void RemovePane(UltraCanvasContainer* pane);

        size_t PaneCount() const { return panes.size(); }
        std::shared_ptr<UltraCanvasContainer> GetPane(size_t index) const;
        int GetPaneIndex(UltraCanvasContainer* pane) const;

        // ===== SIZING =====
        void   SetPaneWeight(size_t index, double weight);
        double GetPaneWeight(size_t index) const;
        void   SetPaneMinSize(size_t index, int px);
        void   SetPaneMaxSize(size_t index, int px);
        void   SetPaneSizes(const std::vector<int>& pixelSizes);
        // A pane with a fixed axis size keeps exactly that many pixels
        // through container resizes (maximizing the window included) — only
        // the weighted panes share what a resize changed. Dragging an
        // adjacent splitter still resizes the pane, and the dragged size
        // becomes its new fixed size, so the width the user chose is what
        // survives the next resize. Min/max still clamp it; a SetPaneSizes
        // covering the pane updates the fixed size instead of its weight.
        // 0 returns the pane to weight-based sizing.
        void   SetPaneFixedSize(size_t index, int px);
        int    GetPaneFixedSize(size_t index) const;

        // ===== STYLE / ORIENTATION =====
        SplitOrientation GetOrientation() const { return orientation; }
        void SetSplitPaneStyle(const SplitPaneStyle& s);
        const SplitPaneStyle& GetSplitPaneStyle() const { return splitStyle; }

        // ===== SPLITTER HANDLE =====
        void SetSplitterHandleStyle(const SplitterHandleStyle& s);
        const SplitterHandleStyle& GetSplitterHandleStyle() const { return splitStyle.handle; }
        // Shorthand for the common case: pick a shape and its across-the-line size.
        void SetSplitterHandleShape(SplitterHandleShape shape, int crossSize = 16,
                                    int axisLength = 0, float cornerRadius = 4.0f);
        // Shorthand for an image handle (SplitterHandleShape::Image). Pass an
        // asset sized for the orientation - tall for a Horizontal split pane,
        // wide for a Vertical one.
        void SetSplitterHandleImage(const std::string& imagePath, int crossSize = 14,
                                    int axisLength = 48);

        // ===== SPLITTER ACTION ICONS =====
        // Icons sit on the split line, centred across it and grouped at
        // SplitterIconStyle::position (or inside the handle) along it. Any number
        // of icons per splitter; splitter `i` is the line between pane `i` and
        // pane `i + 1`.
        void SetSplitterIconStyle(const SplitterIconStyle& s);
        const SplitterIconStyle& GetSplitterIconStyle() const { return splitStyle.icons; }

        size_t SplitterCount() const { return splitters.size(); }

        void   SetSplitterIcons(size_t splitterIndex, const std::vector<SplitPaneIcon>& icons);
        size_t AddSplitterIcon(size_t splitterIndex, const SplitPaneIcon& icon);
        size_t AddSplitterIcon(const SplitPaneIcon& icon) { return AddSplitterIcon(0, icon); }
        bool   InsertSplitterIcon(size_t splitterIndex, size_t iconIndex, const SplitPaneIcon& icon);
        bool   RemoveSplitterIcon(size_t splitterIndex, size_t iconIndex);
        void   ClearSplitterIcons(size_t splitterIndex);
        void   ClearAllSplitterIcons();

        size_t SplitterIconCount(size_t splitterIndex) const;
        const std::vector<SplitPaneIcon>& GetSplitterIcons(size_t splitterIndex) const;
        const SplitPaneIcon* GetSplitterIcon(size_t splitterIndex, size_t iconIndex) const;
        void SetSplitterIconEnabled(size_t splitterIndex, size_t iconIndex, bool enabled);
        void SetSplitterIconVisible(size_t splitterIndex, size_t iconIndex, bool visible);
        void SetSplitterIconTooltip(size_t splitterIndex, size_t iconIndex, const std::string& tooltip);
        void SetSplitterIconPath(size_t splitterIndex, size_t iconIndex, const std::string& iconPath);

        // ===== CALLBACKS =====
        std::function<void(size_t splitterIndex)>       onSplitterDragStart;
        std::function<void(size_t splitterIndex)>       onSplitterDragEnd;
        std::function<void(const std::vector<double>&)> onWeightsChanged;
        // Fired after the icon's own onClick handler (if any).
        std::function<void(size_t splitterIndex, size_t iconIndex, const SplitPaneIcon& icon)>
                onSplitterIconClicked;

        // ===== OVERRIDES =====
        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;
        void SetBounds(const Rect2Df& b) override;

        // ===== INTERNAL (called by UltraCanvasSplitter) =====
        void BeginSplitterDrag(size_t splitterIndex);
        void UpdateSplitterDrag(size_t splitterIndex, int deltaAxisPixels);
        void EndSplitterDrag(size_t splitterIndex);
        void TriggerSplitterIcon(size_t splitterIndex, size_t iconIndex);
        // Axis thickness every splitter strip occupies: the visual line plus its hit
        // margin, widened to fit the handle and any icons.
        int EffectiveSplitterThickness() const;

    private:
        struct PaneSlot {
            std::shared_ptr<UltraCanvasContainer> pane;
            double weight  = 1.0;
            int    minSize = 0;
            int    maxSize = 0;   // 0 = unlimited
            int    fixedSize = 0; // 0 = weight-based (see SetPaneFixedSize)
        };

        SplitOrientation orientation;
        SplitPaneStyle splitStyle;
        std::vector<PaneSlot> panes;
        std::vector<std::shared_ptr<UltraCanvasSplitter>> splitters;
        // Per-splitter action icons. Kept here rather than on the splitter objects
        // so they survive the splitter recreation that pane add/remove triggers.
        std::vector<std::vector<SplitPaneIcon>> splitterIcons;

        // Drag-start snapshot (axis pixel sizes of all panes at MouseDown)
        std::vector<int> dragStartPaneSizes;

        void EnsureSplitterCountMatches();
        void PushStyleToSplitters();
        void RebindSplitterIndices();
        // Lays out panes/splitters through the CSS engine (Measure+Arrange) so each pane's
        // Container::Arrange runs UpdateScrollability at its real split size.
        void PerformLayout(const CSSLayout::LayoutContext& ctx);
        std::vector<int> ComputePaneSizes(int availableAxis) const;
        int AxisLength() const;
    };

    // ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasSplitPane> CreateHorizontalSplitPane(
            const std::string& id, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasSplitPane>(id, x, y, w, h, SplitOrientation::Horizontal);
    }

    inline std::shared_ptr<UltraCanvasSplitPane> CreateVerticalSplitPane(
            const std::string& id, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasSplitPane>(id, x, y, w, h, SplitOrientation::Vertical);
    }

} // namespace UltraCanvas
