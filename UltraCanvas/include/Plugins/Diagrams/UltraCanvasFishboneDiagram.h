// include/Plugins/Diagrams/UltraCanvasFishboneDiagram.h
// Fishbone (Ishikawa) cause-and-effect diagram with eight design presets
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
//
// Renders the model in UltraCanvasFishboneModel.h. The design enum picks the
// geometry; every other option below is orthogonal to it and combines freely,
// the same separation UltraCanvasSWOTDiagram and UltraCanvasTimelineDiagram use.
//
// See Docs/UltraCanvas/UltraCanvasFishboneDiagram.md for the guide and
// Docs/UltraCanvas/UltraCanvasFishboneDiagramProposal.md for the research.
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Diagrams/UltraCanvasFishboneModel.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// FISHBONE ENUMERATIONS
// =============================================================================

    // Geometry presets. Every design draws the same document; they differ in
    // how the spine is drawn, how ribs meet it and where the causes sit.
    enum class FishboneDesign {
        Classic,       // Textbook herringbone: thin spine, straight angled ribs,
                       //   category boxes at the rib tips, causes on the twigs
        SpineChips,    // Solid tapered spine arrow, category pills on the spine
                       //   ending in circular icon badges, dot-terminated twigs
        CrossedRibs,   // One full-length rib per pair of categories, crossing the
                       //   spine; chips at both outer ends, bullet cause blocks
        Bracket,       // Parallelogram bones (slanted edge + shelf) with
                       //   horizontal dot leaders and chips at the outer corner
        ChevronSpine,  // Spine as a chain of chevron blocks; hairline leaders to
                       //   floating pills, each with its own bullet list
        Columns,       // Rib-less: a tinted panel per category straddling the
                       //   spine, icon badge at the far end, bullet list inside
        Vertical,      // Classic rotated: spine top to bottom, ribs left/right
        Compact        // All categories on one side of a low spine
    };

    // Which side of the spine a category's rib leaves from. "Above" means left
    // for the Vertical design, "Below" means right.
    enum class FishboneSidePolicy {
        Alternate,      // Category 0 above, 1 below, 2 above, ... (the default)
        AllAbove,
        AllBelow,
        PerCategory     // Use FishboneCategory::side (0 falls back to Alternate)
    };

    enum class FishboneHeadShape {
        Arrow,      // Stroked arrowhead on the spine
        Triangle,   // Large filled triangle (the infographic head)
        Box,        // Rounded box holding the effect text
        FishHead,   // Rounded fish head with an eye
        Hidden
    };

    enum class FishboneTailShape {
        Hidden,
        Chevron,    // A single chevron notch
        Triangle,   // Filled triangle
        FishTail    // Two-lobed decorative tail
    };

    // Where the effect text goes. All five reference designs surveyed put it in
    // the title and leave the head decorative, so Title is the default.
    enum class FishboneEffectPlacement {
        Title,      // Drawn as the element title above the diagram
        HeadBox,    // Drawn inside a box at the head of the spine
        Hidden
    };

    enum class FishboneCauseMarker {
        Dot,        // Filled circle at the twig end
        Bullet,     // Small bullet before the text
        Numbered,   // 1. 2. 3. before the text
        Hidden
    };

    // Where a category's name is drawn. Designs pick a sensible default; this
    // overrides it.
    enum class FishboneLabelPlacement {
        Auto,
        RibTip,       // Chip at the outer end of the rib
        OnSpine,      // Pill sitting on the spine at the rib root
        BesideSpine,  // Plain text next to the spine
        PanelHeader   // Header inside the category panel (Columns)
    };

    enum class FishbonePalette {
        CorporateBlue,
        Vibrant,
        Pastel,
        Ocean,
        Sunset,
        Forest,
        Slate,
        Mono,
        Custom      // Use the colors passed to SetCustomPalette()
    };

// =============================================================================
// SELECTION HANDLE
// =============================================================================

    // Identifies one drawn thing: a category (cause < 0), a cause
    // (subCause < 0) or a sub-cause.
    struct FishboneRef {
        int category = -1;
        int cause    = -1;
        int subCause = -1;

        bool IsValid() const { return category >= 0; }
        bool IsCategory() const { return category >= 0 && cause < 0; }
        bool IsCause() const { return cause >= 0 && subCause < 0; }
        bool IsSubCause() const { return subCause >= 0; }

        bool operator==(const FishboneRef& o) const {
            return category == o.category && cause == o.cause && subCause == o.subCause;
        }
        bool operator!=(const FishboneRef& o) const { return !(*this == o); }
    };

// =============================================================================
// FISHBONE DIAGRAM ELEMENT
// =============================================================================

    class UltraCanvasFishboneDiagram : public UltraCanvasChartElementBase {
    private:
        // ===== DATA =====
        FishboneDocument document;

        // ===== GEOMETRY & STYLE =====
        FishboneDesign          design          = FishboneDesign::SpineChips;
        FishboneSidePolicy      sidePolicy      = FishboneSidePolicy::Alternate;
        FishboneHeadShape       headShape       = FishboneHeadShape::Triangle;
        FishboneTailShape       tailShape       = FishboneTailShape::Chevron;
        FishboneEffectPlacement effectPlacement = FishboneEffectPlacement::Title;
        FishboneCauseMarker     causeMarker     = FishboneCauseMarker::Dot;
        FishboneLabelPlacement  labelPlacement  = FishboneLabelPlacement::Auto;
        FishbonePalette         palette         = FishbonePalette::Vibrant;
        std::vector<Color>      customPalette;

        bool   darkTheme      = false;
        double ribAngleDegrees = 60.0;  // Angle between a rib and the spine
        double spineThickness  = 3.0;

        // ===== VISUAL TOGGLES =====
        bool showCategoryIcons = true;
        bool showCauses        = true;
        bool showSubCauses     = true;
        bool showSpineCaption  = false;
        bool showWaypoints     = false;   // Small chevrons along the spine
        bool showWeights       = false;   // Numeric weight badge on a cause
        std::string spineCaption = "Potential Causes";

        // ===== FONTS =====
        float titleFontSize    = 18.0f;
        float categoryFontSize = 12.0f;
        float causeFontSize    = 10.5f;
        float effectFontSize   = 13.0f;

        // ===== INTERACTION STATE =====
        FishboneRef hoveredItem;
        FishboneRef selectedItem;

        // ===== LAYOUT CACHE (element-local coordinates) =====
        struct CauseSlot {
            FishboneRef ref;
            Point2Dd    marker;              // Twig terminator / bullet position
            Rect2Dd     textRect;            // Where the label is drawn
            Rect2Dd     hitRect;             // Slightly padded, used for picking
            std::vector<std::string> lines;  // Wrapped label
            bool        rightAligned = false;
        };
        struct CategorySlot {
            int      index = -1;
            int      side  = -1;             // -1 above/left, +1 below/right
            Point2Dd root;                   // Where the rib meets the spine
            Point2Dd tip;                    // Outer end of the rib
            Rect2Dd  labelRect;              // Chip / pill / header
            Rect2Dd  panelRect;              // Columns design only
            Point2Dd badgeCenter;
            double   badgeRadius = 0.0;
            Color    accent;
            std::vector<CauseSlot> causes;
            size_t   hiddenCauses = 0;       // Overflowed, reported as "+N more"
        };
        struct FishboneLayout {
            Rect2Dd  diagramArea;
            Point2Dd spineStart;             // Tail end
            Point2Dd spineEnd;               // Head end
            Rect2Dd  headRect;
            double   ribAngle   = 0.0;       // Radians, after tightening
            double   ribLength  = 0.0;
            float    causeFont  = 10.5f;     // After auto-shrink
            bool     vertical   = false;
            std::vector<CategorySlot> categories;
            bool     valid = false;
        };
        FishboneLayout layout;

    public:
        // ===== CONSTRUCTION =====
        UltraCanvasFishboneDiagram(const std::string& id, int x, int y, int w, int h);

        // ===== DESIGN & STYLE =====
        void SetDesign(FishboneDesign d);
        FishboneDesign GetDesign() const { return design; }

        void SetSidePolicy(FishboneSidePolicy policy);
        FishboneSidePolicy GetSidePolicy() const { return sidePolicy; }

        void SetHeadShape(FishboneHeadShape shape);
        FishboneHeadShape GetHeadShape() const { return headShape; }

        void SetTailShape(FishboneTailShape shape);
        FishboneTailShape GetTailShape() const { return tailShape; }

        void SetEffectPlacement(FishboneEffectPlacement placement);
        FishboneEffectPlacement GetEffectPlacement() const { return effectPlacement; }

        void SetCauseMarker(FishboneCauseMarker marker);
        FishboneCauseMarker GetCauseMarker() const { return causeMarker; }

        void SetLabelPlacement(FishboneLabelPlacement placement);
        FishboneLabelPlacement GetLabelPlacement() const { return labelPlacement; }

        void SetPalette(FishbonePalette p);
        FishbonePalette GetPalette() const { return palette; }
        void SetCustomPalette(const std::vector<Color>& colors);

        void SetDarkTheme(bool dark);
        bool GetDarkTheme() const { return darkTheme; }

        // Angle between a rib and the spine, in degrees. Clamped to 25..80; the
        // solver may steepen it further when the ribs would otherwise collide.
        void SetRibAngle(double degrees);
        double GetRibAngle() const { return ribAngleDegrees; }

        void SetSpineThickness(double thickness);
        double GetSpineThickness() const { return spineThickness; }

        // ===== VISUAL TOGGLES =====
        void SetShowCategoryIcons(bool show);
        bool GetShowCategoryIcons() const { return showCategoryIcons; }

        void SetShowCauses(bool show);
        bool GetShowCauses() const { return showCauses; }

        void SetShowSubCauses(bool show);
        bool GetShowSubCauses() const { return showSubCauses; }

        void SetShowSpineCaption(bool show);
        bool GetShowSpineCaption() const { return showSpineCaption; }
        void SetSpineCaption(const std::string& caption);
        const std::string& GetSpineCaption() const { return spineCaption; }

        void SetShowWaypoints(bool show);
        bool GetShowWaypoints() const { return showWaypoints; }

        void SetShowWeights(bool show);
        bool GetShowWeights() const { return showWeights; }

        void SetCauseFontSize(float size);
        float GetCauseFontSize() const { return causeFontSize; }
        void SetCategoryFontSize(float size);
        float GetCategoryFontSize() const { return categoryFontSize; }

        // ===== DOCUMENT =====
        void SetDocument(const FishboneDocument& doc);
        const FishboneDocument& GetDocument() const { return document; }

        void SetEffect(const std::string& text);
        const std::string& GetEffect() const { return document.effect; }

        // Returns the new category's index
        size_t AddCategory(const std::string& title);
        size_t AddCategory(const FishboneCategory& category);
        void SetCategories(const std::vector<FishboneCategory>& categories);
        const std::vector<FishboneCategory>& GetCategories() const { return document.categories; }
        size_t CountCategories() const { return document.categories.size(); }
        void RemoveCategory(size_t index);
        void ClearCategories();

        void SetCategoryTitle(size_t index, const std::string& title);
        void SetCategoryAccent(size_t index, const Color& color);
        void SetCategoryIcon(size_t index, const std::string& iconPath);
        void SetCategoryGlyph(size_t index, const std::string& glyph);
        void SetCategorySide(size_t index, int side);
        void SetCategoryCollapsed(size_t index, bool collapsed);
        bool GetCategoryCollapsed(size_t index) const;

        // Returns the new cause's index, or SIZE_MAX when the category is unknown
        size_t AddCause(size_t categoryIndex, const std::string& text);
        size_t AddCause(size_t categoryIndex, const FishboneCause& cause);
        size_t AddSubCause(size_t categoryIndex, size_t causeIndex, const std::string& text);
        void RemoveCause(size_t categoryIndex, size_t causeIndex);
        void ClearCauses(size_t categoryIndex);
        size_t CountCauses(size_t categoryIndex) const;
        size_t CountAllCauses(bool includeSubCauses = false) const;

        // Highlight / root-cause / weight, addressed by the same ref used for
        // selection so a click can drive them directly.
        void SetCauseHighlighted(const FishboneRef& ref, bool highlighted);
        void SetCauseRootCause(const FishboneRef& ref, bool isRootCause);
        void SetCauseWeight(const FishboneRef& ref, double weight);
        const FishboneCause* FindCause(const FishboneRef& ref) const;

        // ===== PRESETS, SAMPLES & TEXT INTERCHANGE =====
        void LoadCategoryPreset(FishboneCategoryPreset preset);  // Keeps the effect
        void LoadSampleData();                                   // Quality-control example

        bool LoadOutline(const std::string& text, std::string* error = nullptr);
        std::string ExportOutline() const;
        bool LoadMermaid(const std::string& text, std::string* error = nullptr);
        std::string ExportMermaid() const;

        std::vector<std::string> Validate() const { return ValidateFishbone(document); }

        // ===== SELECTION =====
        // Selection and tooltips are controlled by the base class:
        // SetEnableSelection(bool) / SetEnableTooltips(bool)
        FishboneRef GetSelectedItem() const { return selectedItem; }
        FishboneRef GetHoveredItem() const { return hoveredItem; }
        void SetSelectedItem(const FishboneRef& ref);
        void ClearSelection();

        // ===== GEOMETRY QUERIES (element-local, valid after the first render) =====
        bool GetSpineLine(Point2Dd& start, Point2Dd& end) const;
        bool GetCategoryLabelRect(size_t index, Rect2Dd& rect) const;
        bool GetCategoryColor(size_t index, Color& color) const;

        // ===== CALLBACKS =====
        std::function<void(size_t, const FishboneCategory&)> onCategorySelect;
        std::function<void(const FishboneRef&, const FishboneCause&)> onCauseSelect;
        std::function<void(const FishboneRef&, const FishboneCause&)> onCauseHover;
        std::function<void(const FishboneRef&, const FishboneCause&)> onCauseDoubleClick;
        std::function<void()> onSelectionChange;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== THEME =====
        Color PaletteColor(size_t index) const;
        Color CategoryAccent(size_t index) const;
        Color DiagramTextColor() const;
        Color MutedTextColor() const;
        Color SpineColor() const;
        Color SurfaceColor() const;

        // ===== LAYOUT =====
        void InvalidateLayout() { layout.valid = false; }
        void UpdateLayout(IRenderContext* ctx);
        void SolveRibLayout(IRenderContext* ctx);      // Classic/SpineChips/Bracket/CrossedRibs/Compact
        void SolveVerticalLayout(IRenderContext* ctx);
        void SolveLeaderLayout(IRenderContext* ctx);   // ChevronSpine
        void SolveColumnLayout(IRenderContext* ctx);   // Columns
        int  ResolveSide(size_t index) const;
        double HeadReserve(IRenderContext* ctx) const;
        double TailReserve() const;

        // Fills slot.causes for a rib-style category. ribDir is the unit vector
        // from the root toward the tip; labelWidth is the wrap budget. Returns
        // the along-rib distance actually consumed, which the solver uses to
        // trim the rib back to its content.
        double BuildRibCauses(IRenderContext* ctx, CategorySlot& slot, size_t categoryIndex,
                              const Point2Dd& ribDir, double ribLength, double labelWidth,
                              bool labelsRightAligned);

        // Fills slot.causes as a stacked bullet list inside a rect.
        void BuildListCauses(IRenderContext* ctx, CategorySlot& slot, size_t categoryIndex,
                             const Rect2Dd& area, bool centered);

        // ===== HIT TESTING =====
        FishboneRef FindItemAt(const Point2Di& pos) const;

        // ===== DRAWING =====
        void RenderClassic(IRenderContext* ctx);
        void RenderSpineChips(IRenderContext* ctx);
        void RenderCrossedRibs(IRenderContext* ctx);
        void RenderBracket(IRenderContext* ctx);
        void RenderChevronSpine(IRenderContext* ctx);
        void RenderColumns(IRenderContext* ctx);
        void RenderVertical(IRenderContext* ctx);

        void DrawSpine(IRenderContext* ctx, bool solidArrow);
        void DrawHead(IRenderContext* ctx);
        void DrawTail(IRenderContext* ctx);
        void DrawSpineCaption(IRenderContext* ctx);
        void DrawWaypoints(IRenderContext* ctx);
        void DrawCategoryChip(IRenderContext* ctx, const CategorySlot& slot,
                              bool filled);
        void DrawCategoryBadge(IRenderContext* ctx, const CategorySlot& slot);
        void DrawCauses(IRenderContext* ctx, const CategorySlot& slot,
                        bool drawLeader, bool markerAtSpineSide);
        void DrawSelectionHighlight(IRenderContext* ctx, const Rect2Dd& rect,
                                    const Color& accent, bool selected);

        // ===== TEXT HELPERS =====
        std::vector<std::string> WrapText(IRenderContext* ctx, const std::string& text,
                                          double maxWidth) const;
        // Shortens text with a trailing ellipsis so it fits maxWidth
        std::string ElideText(IRenderContext* ctx, const std::string& text,
                              double maxWidth) const;
        void DrawTextLines(IRenderContext* ctx, const std::vector<std::string>& lines,
                           const Rect2Dd& rect, bool rightAligned, double lineHeight);
        std::string BuildTooltip(const FishboneRef& ref) const;
        std::string CauseLabelText(const FishboneRef& ref, const FishboneCause& cause,
                                   size_t ordinal) const;
    };

// =============================================================================
// FACTORY FUNCTIONS - FOLLOW EXISTING ULTRACANVAS PATTERNS
// =============================================================================

    std::shared_ptr<UltraCanvasFishboneDiagram> CreateFishboneDiagramElement(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasFishboneDiagram> CreateFishboneDiagramElement(
            const std::string& id, int x, int y, int width, int height,
            FishboneDesign design);

} // namespace UltraCanvas
