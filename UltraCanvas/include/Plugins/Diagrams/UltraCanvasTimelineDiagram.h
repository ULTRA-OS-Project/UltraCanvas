// include/Plugins/Diagrams/UltraCanvasTimelineDiagram.h
// Narrative timeline infographic: an ordered list of events laid out along a
// decorative path, with multiple design presets.
//
// This element covers "Family A" of the timeline proposal - the presentation
// timeline, where items are evenly spaced and time is not necessarily to scale.
// For a date-accurate schedule with milestones and spans use the (planned)
// UltraCanvasTimelineChart; for a full project plan with tasks, dependencies
// and progress use UltraCanvasGanttChart.
//
// See Docs/UltraCanvas/UltraCanvasTimelineDiagramProposal.md for the research
// behind the split.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasCalendarDate.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// TIMELINE DIAGRAM ENUMERATIONS
// =============================================================================

    // Geometry presets. Every design renders the same item list; they differ in
    // how the spine is drawn and where item content is placed.
    enum class TimelineDesign {
        Bar,          // Horizontal spine split into colored segments, nodes alternating on its edges
        Line,         // Thin horizontal axis, nodes on the axis, content on one side
        Alternating,  // Thin axis with content cards zigzagging above and below
        Cards,        // Row of separate cards with colored headers, no decorative spine
        Vertical,     // Top-down spine with content cards left and right
        Serpentine,   // Snaking ribbon wrapping over several runs
        Hanging,      // Axis on top, stems dropping to bubbles of varying size
        Chevron,      // Sequence of arrow blocks pointing forward
        Steps         // Ascending staircase blocks
    };

    enum class TimelineNodeShape {
        Circle,
        RoundedSquare,
        Square,
        Diamond,
        Hexagon,
        Pin,          // Map-pin / teardrop pointing at the spine
        Hidden        // No node marker ("None" is an X11 macro)
    };

    // Which side of the spine an item's content is placed on. "Above" means
    // left for the vertical design, "Below" means right.
    enum class TimelineSidePolicy {
        Alternate,    // Zigzag: item 0 above, item 1 below, ...
        AllAbove,
        AllBelow,
        PerItem       // Use TimelineItem::side (0 falls back to Alternate)
    };

    enum class TimelineColorMode {
        PerItem,          // Each item takes the next palette entry
        Single,           // Everything uses the first palette entry
        GradientAlongPath // Spine is a gradient from the first to the last color
    };

    // How item positions along the spine are computed.
    enum class TimelinePlacement {
        Even,         // Evenly spaced - time is not to scale (the usual infographic)
        Proportional  // Spaced by TimelineItem::dateSerial, with a minimum gap
    };

    enum class TimelineConnectorStyle {
        Hidden,
        Straight,
        Elbow,
        Curved,
        Dashed
    };

    // Container drawn behind an item's title/body text.
    enum class TimelineContentContainer {
        Plain,         // Bare text
        Card,          // Filled rounded card
        OutlinedCard,  // Outlined rounded card
        Bubble         // Filled circle (the Hanging design's default)
    };

    enum class TimelinePalette {
        CorporateBlue,
        Vibrant,
        Pastel,
        Ocean,
        Sunset,
        Forest,
        Slate,
        Mono,
        Custom
    };

// =============================================================================
// TIMELINE DIAGRAM DATA STRUCTURES
// =============================================================================

    // Days since 1970-01-01, matching GanttDate::serial, so a Gantt task date
    // can be assigned directly: item.SetDate(task.start.serial).
    inline long TimelineDateSerial(int year, int month, int day) {
        return CalendarDaysFromCivil(year, month, day);
    }

    struct TimelineItem {
        std::string caption;    // Big period label: "2015", "Q3", "Phase 1"
        std::string title;      // Short heading
        std::string body;       // Paragraph; wrapped, "\n" starts a new paragraph
        std::string iconGlyph;  // Short text/glyph drawn inside the node

        // Fully transparent = take the next palette entry
        Color accentColor = Color(0, 0, 0, 0);

        // Optional real date, used by TimelinePlacement::Proportional
        long dateSerial = 0;
        bool hasDate = false;

        // Per-item overrides; 0 = automatic
        double nodeSize = 0.0;    // Node radius
        double stemLength = 0.0;  // Hanging design connector length
        int side = 0;             // -1 above/left, +1 below/right, 0 = policy

        std::string tooltip;      // Overrides the generated tooltip
        bool highlighted = false; // Extra emphasis (a "current" item)

        TimelineItem() = default;
        explicit TimelineItem(const std::string& cap) : caption(cap) {}
        TimelineItem(const std::string& cap, const std::string& head)
                : caption(cap), title(head) {}
        TimelineItem(const std::string& cap, const std::string& head, const std::string& text)
                : caption(cap), title(head), body(text) {}

        void SetDate(long serial) { dateSerial = serial; hasDate = true; }
        void SetDate(int year, int month, int day) {
            SetDate(TimelineDateSerial(year, month, day));
        }
    };

    // Every visual knob. ApplyDesign() replaces the whole struct with a preset;
    // EditStyle() + StyleChanged() tweaks it in place.
    struct TimelineDiagramStyle {
        // ===== SPINE =====
        double spineThickness = 4.0;
        double spineCornerRadius = 4.0;   // Rounded caps on segmented spines
        double segmentGap = 0.0;          // Gap between Bar design segments
        bool showSpine = true;
        bool showSpineArrow = false;      // Arrowhead at the end of the spine

        // ===== NODES =====
        TimelineNodeShape nodeShape = TimelineNodeShape::Circle;
        double nodeRadius = 16.0;
        double nodeBorderWidth = 3.0;
        bool showNodes = true;
        bool showNodeNumbers = false;     // 1, 2, 3 ... inside the node
        bool filledNodes = true;          // Filled with the accent, or hollow

        // ===== CONNECTORS =====
        TimelineConnectorStyle connectorStyle = TimelineConnectorStyle::Straight;
        double connectorWidth = 2.0;
        double connectorLength = 34.0;    // Default stem length

        // ===== CONTENT =====
        TimelineContentContainer contentContainer = TimelineContentContainer::Plain;
        double contentPadding = 10.0;
        double cornerRadius = 8.0;
        double itemGap = 14.0;            // Horizontal gap between items
        bool showCaptions = true;
        bool showTitles = true;
        bool showBodies = true;
        int maxBodyLines = 6;             // Extra lines are ellipsized

        // ===== DECORATION =====
        bool showShadows = false;
        double shadowOffset = 3.0;
        int shadowAlpha = 40;
        bool showScaleRow = false;        // Independent label track along the spine

        // ===== FONTS =====
        float captionFontSize = 20.0f;
        float itemTitleFontSize = 13.0f;
        float bodyFontSize = 10.5f;
        float nodeFontSize = 13.0f;
        float scaleFontSize = 11.0f;
        float minFontSize = 7.0f;         // Auto-shrink floor

        // ===== COLORS =====
        Color spineColor = Color(210, 214, 220, 255);
        Color textColor = Color(38, 42, 51, 255);
        Color mutedTextColor = Color(110, 116, 128, 255);
        Color cardColor = Color(255, 255, 255, 255);
        Color cardBorderColor = Color(224, 227, 233, 255);
        Color connectorColor = Color(180, 186, 196, 255);
        Color nodeTextColor = Color(255, 255, 255, 255);

        // ===== DESIGN-SPECIFIC =====
        int serpentineItemsPerRun = 4;
        double serpentineTurnRadius = 40.0;
        int hangingTiers = 2;             // Vertical stagger tiers for bubbles
        double stepRise = 0.0;            // Steps design; 0 = automatic
    };

    // Identifies one item for hit testing. index < 0 means "no item".
    struct TimelineItemRef {
        int index = -1;

        bool IsValid() const { return index >= 0; }
        bool operator==(const TimelineItemRef& o) const { return index == o.index; }
        bool operator!=(const TimelineItemRef& o) const { return index != o.index; }
    };

// =============================================================================
// TIMELINE DIAGRAM ELEMENT CLASS
// =============================================================================

    class UltraCanvasTimelineDiagram : public UltraCanvasChartElementBase {
    private:
        // ===== DATA =====
        std::vector<TimelineItem> items;
        std::string subtitle;
        std::vector<std::string> scaleLabels;   // Optional independent axis track

        // ===== CONFIGURATION =====
        TimelineDesign design = TimelineDesign::Bar;
        TimelineDiagramStyle style;
        TimelineSidePolicy sidePolicy = TimelineSidePolicy::Alternate;
        TimelineColorMode colorMode = TimelineColorMode::PerItem;
        TimelinePlacement placement = TimelinePlacement::Even;
        TimelinePalette palette = TimelinePalette::CorporateBlue;
        std::vector<Color> customPalette;
        bool darkTheme = false;
        bool reverseOrder = false;   // Draw the sequence right-to-left / bottom-up
        int currentIndex = -1;       // Items past this render as "pending"

        // ===== INTERACTION STATE =====
        TimelineItemRef hoveredItem;
        TimelineItemRef selectedItem;

        // ===== LAYOUT CACHE (element-local coordinates) =====
        struct ItemLayout {
            Point2Dd nodeCenter;
            double nodeRadius = 0.0;
            int side = -1;              // -1 above/left, +1 below/right
            Rect2Dd contentRect;        // Title + body block
            Rect2Dd captionRect;        // Big caption label
            Rect2Dd blockRect;          // Chevron / Steps / Cards / Bar segment
            Point2Dd connectorFrom;
            Point2Dd connectorTo;
            Rect2Dd hitRect;
            Color color;
            bool hasConnector = false;
            bool hasBlock = false;
        };
        struct TimelineLayout {
            Rect2Dd diagramArea;
            std::vector<ItemLayout> itemLayouts;
            std::vector<Point2Dd> spinePoints;   // Polyline the spine follows
            Rect2Dd spineRect;                   // Straight designs
            double contentScale = 1.0;           // Auto-shrink factor for fonts
            bool valid = false;
        };
        TimelineLayout layout;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasTimelineDiagram(const std::string& id, int x, int y, int w, int h);

        // ===== DESIGN & STYLE =====
        // Changes the geometry only, keeping the current style.
        void SetDesign(TimelineDesign d);
        TimelineDesign GetDesign() const { return design; }

        // Replaces the whole style with the design's preset, then switches to it.
        void ApplyDesign(TimelineDesign d);

        void SetStyle(const TimelineDiagramStyle& s);
        const TimelineDiagramStyle& GetStyle() const { return style; }
        TimelineDiagramStyle& EditStyle() { return style; }
        void StyleChanged();

        void SetDarkTheme(bool dark);
        bool GetDarkTheme() const { return darkTheme; }

        // ===== PALETTE & COLOR =====
        void SetPalette(TimelinePalette p);
        TimelinePalette GetPalette() const { return palette; }
        void SetCustomPalette(const std::vector<Color>& colors);
        const std::vector<Color>& GetPaletteColors() const;

        void SetColorMode(TimelineColorMode mode);
        TimelineColorMode GetColorMode() const { return colorMode; }

        // ===== LAYOUT OPTIONS =====
        void SetSidePolicy(TimelineSidePolicy policy);
        TimelineSidePolicy GetSidePolicy() const { return sidePolicy; }

        void SetPlacement(TimelinePlacement p);
        TimelinePlacement GetPlacement() const { return placement; }

        void SetReverseOrder(bool reverse);
        bool GetReverseOrder() const { return reverseOrder; }

        // Items with an index above this render in a "pending" (hollow) style.
        // -1 disables the effect.
        void SetCurrentIndex(int index);
        int GetCurrentIndex() const { return currentIndex; }

        // ===== TITLES =====
        // The main title comes from the base class (SetChartTitle).
        void SetSubtitle(const std::string& text);
        const std::string& GetSubtitle() const { return subtitle; }

        // Independent label track drawn along the spine (e.g. Jan..Dec),
        // decoupled from the item list. Enable with style.showScaleRow.
        void SetScaleLabels(const std::vector<std::string>& labels);
        const std::vector<std::string>& GetScaleLabels() const { return scaleLabels; }

        // ===== DATA MANAGEMENT =====
        void AddItem(const TimelineItem& item);
        void AddItem(const std::string& caption, const std::string& title,
                     const std::string& body = "");
        void InsertItem(size_t index, const TimelineItem& item);
        void SetItems(const std::vector<TimelineItem>& list);
        void RemoveItem(size_t index);
        void MoveItem(size_t from, size_t to);
        void ClearItems();

        const std::vector<TimelineItem>& GetItems() const { return items; }
        size_t CountItems() const { return items.size(); }
        const TimelineItem& GetItem(size_t index) const { return items[index]; }
        TimelineItem& EditItem(size_t index) { return items[index]; }
        void ItemsChanged();   // Call after EditItem()

        // Loads a small built-in company-history example
        void LoadSampleData();

        // ===== SELECTION =====
        // Selection and tooltips are controlled by the base class:
        // SetEnableSelection(bool) / SetEnableTooltips(bool)
        int GetSelectedIndex() const { return selectedItem.index; }
        void SetSelectedIndex(int index);
        void ClearSelection();

        // ===== GEOMETRY QUERIES =====
        // Valid after the first render; return false when the layout is stale
        // or the index is out of range.
        bool GetItemNodeCenter(size_t index, Point2Dd& outCenter) const;
        bool GetItemContentRect(size_t index, Rect2Dd& outRect) const;

        // ===== CALLBACKS =====
        std::function<void(size_t, const TimelineItem&)> onItemSelect;
        std::function<void(size_t, const TimelineItem&)> onItemHover;
        std::function<void(size_t, const TimelineItem&)> onItemDoubleClick;
        std::function<void()> onSelectionChange;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== THEME HELPERS =====
        Color ItemColor(size_t index) const;
        Color CardFillColor() const;
        Color TextColor() const;
        Color MutedTextColor() const;
        bool IsPending(size_t index) const;

        // ===== LAYOUT =====
        void InvalidateLayout() { layout.valid = false; }
        void UpdateLayout(IRenderContext* ctx);
        double TitleBandHeight() const;
        int SideForItem(size_t index) const;
        // Normalized 0..1 positions of every item along the spine
        std::vector<double> ItemPositions() const;

        void LayoutBar(IRenderContext* ctx);
        void LayoutLine(IRenderContext* ctx);
        void LayoutAlternating(IRenderContext* ctx);
        void LayoutCards(IRenderContext* ctx);
        void LayoutVertical(IRenderContext* ctx);
        void LayoutSerpentine(IRenderContext* ctx);
        void LayoutHanging(IRenderContext* ctx);
        void LayoutChevron(IRenderContext* ctx);
        void LayoutSteps(IRenderContext* ctx);

        // ===== DRAWING =====
        void DrawTitles(IRenderContext* ctx);
        void DrawSpine(IRenderContext* ctx);
        void DrawSerpentineRibbon(IRenderContext* ctx);
        void DrawScaleRow(IRenderContext* ctx);
        void DrawItems(IRenderContext* ctx);
        void DrawConnector(IRenderContext* ctx, const ItemLayout& il);
        void DrawNode(IRenderContext* ctx, size_t index, const ItemLayout& il);
        void DrawNodeShape(IRenderContext* ctx, const Point2Dd& center, double radius,
                           TimelineNodeShape shape);
        void DrawContent(IRenderContext* ctx, size_t index, const ItemLayout& il);
        void DrawBubbleContent(IRenderContext* ctx, size_t index, const ItemLayout& il);
        void DrawBlock(IRenderContext* ctx, size_t index, const ItemLayout& il);
        void DrawCaption(IRenderContext* ctx, size_t index, const ItemLayout& il);
        void DrawShadowRect(IRenderContext* ctx, const Rect2Dd& rect, double radius);
        void DrawHighlight(IRenderContext* ctx, size_t index, const ItemLayout& il);

        // ===== TEXT HELPERS =====
        // Wraps an item's caption/title/body into the given width and returns
        // the total height. Shared by the layout pass (so cards can size
        // themselves to their text) and by the drawing pass.
        double BuildContentLines(IRenderContext* ctx, size_t index, double width,
                                 double maxHeight, bool captionInside,
                                 std::vector<std::string>& captionLines,
                                 std::vector<std::string>& titleLines,
                                 std::vector<std::string>& bodyLines) const;

        std::vector<std::string> WrapText(IRenderContext* ctx, const std::string& text,
                                          double maxWidth, int maxLines) const;
        // Wraps text so every line fits inside a circle of the given radius.
        std::vector<std::string> WrapTextInCircle(IRenderContext* ctx, const std::string& text,
                                                  double radius, double lineHeight) const;
        void DrawCenteredLines(IRenderContext* ctx, const std::vector<std::string>& lines,
                               const Point2Dd& center, double lineHeight) const;
        float ScaledFont(float size) const;

        // ===== EVENT HELPERS =====
        TimelineItemRef FindItemAt(const Point2Di& pos) const;
        bool HandleItemClick(const UCEvent& event);
        bool HandleItemDoubleClick(const UCEvent& event);
        std::string BuildItemTooltip(size_t index) const;
    };

// =============================================================================
// SAMPLE DATA
// =============================================================================

    namespace TimelineDiagramSamples {
        // Six-step company history, matching the classic year-timeline layout
        std::vector<TimelineItem> CompanyHistory();
        // Twelve months with dates set, for the Hanging / Serpentine designs
        std::vector<TimelineItem> ProjectYear(int year);
    }

// =============================================================================
// STYLE PRESETS
// =============================================================================

    namespace TimelineDiagramStyles {
        TimelineDiagramStyle CreateForDesign(TimelineDesign design);
        std::vector<Color> GetPaletteColors(TimelinePalette palette);
    }

// =============================================================================
// FACTORY FUNCTIONS - FOLLOW EXISTING ULTRACANVAS PATTERNS
// =============================================================================

    std::shared_ptr<UltraCanvasTimelineDiagram> CreateTimelineDiagramElement(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasTimelineDiagram> CreateTimelineDiagramElement(
            const std::string& id, int x, int y, int width, int height,
            TimelineDesign design);

    std::shared_ptr<UltraCanvasTimelineDiagram> CreateTimelineDiagramWithData(
            const std::string& id, int x, int y, int width, int height,
            const std::vector<TimelineItem>& items, TimelineDesign design);

} // namespace UltraCanvas
