// include/CSSLayout/CSSLayout.h
// CSS-compliant layout engine: type model and Element base class.
// Version: 4.8.1
// Last Modified: 2026-06-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasConfig.h"
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

namespace UltraCanvas {
    namespace CSSLayout {

        // ---- Length values (CSS <length> / <percentage> / <flex>) ----

        enum class DimensionUnit { Auto, Pixels, Percent, Fr, MinContent, MaxContent, FitContent,
                                   ViewportWidth, ViewportHeight, Em, Rem };

        struct Dimension {
            DimensionUnit unit = DimensionUnit::Auto;
            float value = 0.f;

            static Dimension Auto();
            static Dimension Px(float v);
            static Dimension Pct(float v);
            static Dimension Fr(float v);
            static Dimension Vw(float v);   // % of viewport width
            static Dimension Vh(float v);   // % of viewport height
            static Dimension Em(float v);   // multiple of current font size
            static Dimension Rem(float v);  // multiple of root font size

            bool isAuto() const { return unit == DimensionUnit::Auto; }
        };

        struct BoxSize {
            Dimension width;
            Dimension height;
        };

        struct BoxConstraints {
            Dimension minWidth;
            Dimension minHeight;
            Dimension maxWidth;
            Dimension maxHeight;
        };

        struct EdgeSizes {
            Dimension top;
            Dimension right;
            Dimension bottom;
            Dimension left;
        };

        // Resolved-pixel insets (output of resolving EdgeSizes against a parent).
        struct InsetsPx {
            float top = 0, right = 0, bottom = 0, left = 0;
            float horizontal() const { return left + right; }
            float vertical()   const { return top + bottom; }
        };

        struct Gap {
            Dimension row;
            Dimension column;
        };

        // ---- Layout context: viewport, font sizes, DPI, direction ----
        // TODO: real writing-mode (horizontal-tb / vertical-rl / sideways) — only LTR now.
        enum class TextDirection { Ltr, Rtl };

        struct LayoutContext {
            float viewportWidth  = 0.f;   // for vw
            float viewportHeight = 0.f;   // for vh
            float rootFontSizePx = 16.f;  // for rem
            float fontSizePx     = 16.f;  // for em (per-element; refined as we descend)
            float devicePixelRatio = 1.f;
            TextDirection direction = TextDirection::Ltr;

            bool operator==(const LayoutContext& o) const {
                return viewportWidth    == o.viewportWidth
                    && viewportHeight   == o.viewportHeight
                    && rootFontSizePx   == o.rootFontSizePx
                    && fontSizePx       == o.fontSizePx
                    && devicePixelRatio == o.devicePixelRatio
                    && direction        == o.direction;
            }
            bool operator!=(const LayoutContext& o) const { return !(*this == o); }
        };

        // ---- Measure constraints (Flutter-style box constraints) ----

        enum class ConstraintMode {
            Exact,        // must be exactly this (tight)
            AtMost,       // <= available (loose / max-content clamp)
            Unbounded     // intrinsic / infinite available space
        };

        struct AxisConstraint {
            ConstraintMode mode = ConstraintMode::AtMost;
            float available = INFINITY;   // ignored when Unbounded

            bool operator==(const AxisConstraint& o) const {
                if (mode != o.mode) return false;
                if (mode == ConstraintMode::Unbounded) return true;
                return available == o.available;
            }
        };

        struct MeasureConstraints {
            AxisConstraint horizontal;
            AxisConstraint vertical;
            bool operator==(const MeasureConstraints& o) const {
                return horizontal == o.horizontal && vertical == o.vertical;
            }
        };

        // ---- Box / display ----

        enum class DisplayType    { Block, Flex, Grid, Inline, InlineBlock, NoDisplay };
        enum class BoxSizing      { ContentBox, BorderBox };
        // AbsoluteUI: positioned exactly like Absolute (against the padding-box),
        // but ALSO contributes to the container's measured size during Measure
        // (the container grows to cover left+width / top+height). Opt-in; plain
        // Absolute keeps the standard CSS behavior of not affecting parent size.
        enum class PositionType   { Static, Relative, Absolute, Fixed, AbsoluteUI };
        enum class Overflow       { Visible, Hidden, Scroll, Auto };
        enum class Visibility     { Visible, Hidden };  // Hidden reserves space (unlike NoDisplay)

        // ---- Flex ----

        enum class FlexDirection { Row, RowReverse, Column, ColumnReverse };
        enum class FlexWrap      { NoWrap, Wrap, WrapReverse };

        // ---- Alignment (CSS Box Alignment, shared across flex & grid) ----

        enum class JustifyContent {
            Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly, Stretch,
            FlexStart, FlexEnd
        };
        enum class AlignContent {
            Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly, Stretch
        };
        enum class AlignItems {
            Start, End, Center, Stretch, Baseline, FirstBaseline, LastBaseline
        };
        enum class AlignSelf {
            Auto, Start, End, Center, Stretch, Baseline
        };
        enum class JustifyItems { Start, End, Center, Stretch };
        enum class JustifySelf  { Auto, Start, End, Center, Stretch };

        // ---- Grid ----

        enum class GridAutoFlow { Row, Column, RowDense, ColumnDense };

        enum class GridTrackSizeKind { Fixed, Percent, Fr, Auto, MinContent, MaxContent, FitContent, MinMax };
        struct GridTrackSize {
            GridTrackSizeKind kind = GridTrackSizeKind::Auto;
            Dimension value;                // for Fixed/Percent/Fr/FitContent
            Dimension minValue, maxValue;   // for MinMax
        };

        enum class GridLineKind { Auto, Line, Span, Named };
        struct GridLine {
            GridLineKind type = GridLineKind::Auto;
            int index = 0;           // line number or span count
            std::string name;        // for Named  // TODO: named lines lookup not implemented yet
        };

        struct GridTrackList {
            std::vector<GridTrackSize> tracks;
            // TODO: repeat(auto-fill/auto-fit) — caller must pre-expand for now.
        };

        struct GridItem {
            GridLine columnStart, columnEnd;
            GridLine rowStart, rowEnd;
            JustifySelf justifySelf = JustifySelf::Stretch;
            AlignSelf   alignSelf   = AlignSelf::Stretch;
        };

        // ---- Flex item ----
        struct FlexItem {
            float grow = 0.0f;
            float shrink = 1.0f;
            Dimension basis;        // Auto means use main-axis size
            AlignSelf alignSelf = AlignSelf::Auto;
            int order = 0;
        };

        // ---- Positioning (orthogonal to display) ----
        // For position: relative -> top/left/right/bottom act as a post-layout offset.
        // For position: absolute/fixed -> insets define the box against its containing block.
        struct Position {
            Dimension top;
            Dimension right;
            Dimension bottom;
            Dimension left;
        };

        // ---- Measure result (extrinsic; cached per constraints + layout context) ----
        struct MeasureResult {
            MeasureConstraints key;
            LayoutContext ctxKey;   // measurement also depends on viewport/font/DPI (vw/vh/em/rem)
            bool valid = false;
            float measuredWidth = 0;
            float measuredHeight = 0;
            float baseline = 0;       // first-baseline, for align Baseline
        };

        // ---- Intrinsic sizes (constraint-independent; cached once) ----
        struct IntrinsicSizes {
            bool valid = false;
            float minContentWidth = 0, maxContentWidth = 0;
            float minContentHeight = 0, maxContentHeight = 0;
        };

        struct BoxModel {
            EdgeSizes margin;
            EdgeSizes padding;
            EdgeSizes border;

            BoxSizing boxSizing = BoxSizing::ContentBox;

            Overflow overflowX = Overflow::Visible;
            Overflow overflowY = Overflow::Visible;
        };

        struct FlexLayout {
            FlexDirection direction = FlexDirection::Row;
            FlexWrap wrap = FlexWrap::NoWrap;

            JustifyContent justifyContent = JustifyContent::Start;
            AlignItems alignItems = AlignItems::Stretch;
            AlignContent alignContent = AlignContent::Stretch;

            Gap gap;
        };

        struct GridLayout {
            GridTrackList columns;
            GridTrackList rows;

            Dimension columnGap = Dimension::Px(0.0f);
            Dimension rowGap    = Dimension::Px(0.0f);

            GridAutoFlow autoFlow = GridAutoFlow::Row;

            JustifyItems justifyItems = JustifyItems::Stretch;
            AlignItems   alignItems   = AlignItems::Stretch;

            // TODO: grid-template-areas string syntax.
            // TODO: subgrid.
            // TODO: masonry.
        };

        using LayoutItemData = std::variant<
                std::monostate,
                FlexItem,
                GridItem>;

        using LayoutData = std::variant<
                std::monostate,
                FlexLayout,
                GridLayout>;

        struct Layout {
            DisplayType display = DisplayType::Block;
            LayoutData  data;

            // need for Show/Hide to restore correct display
            DisplayType prevDisplay = DisplayType::Block;

            // ---- Flex configuration (initializes data to FlexLayout on first call) ----
            Layout& SetFlex(FlexDirection d = FlexDirection::Row,
                            FlexWrap w = FlexWrap::NoWrap);
            Layout& SetFlexDirection(FlexDirection d);
            Layout& SetFlexRow()    { return SetFlexDirection(FlexDirection::Row); }
            Layout& SetFlexColumn() { return SetFlexDirection(FlexDirection::Column); }
            Layout& SetFlexWrap(FlexWrap w);
            Layout& SetFlexGap(float gap);                       // both axes
            Layout& SetFlexGap(float row, float column);
            Layout& SetFlexJustifyContent(JustifyContent jc);
            Layout& SetFlexAlignItems(AlignItems ai);
            Layout& SetGridAlignItems(AlignItems ai);
            Layout& SetFlexAlignContent(AlignContent ac);

            // ---- Grid configuration (initializes data to GridLayout on first call) ----
            Layout& SetGrid();
            Layout& SetGridColumns(std::vector<GridTrackSize> tracks);
            Layout& SetGridRows   (std::vector<GridTrackSize> tracks);
            Layout& SetGridGap(float gap);
            Layout& SetGridGap(float row, float column);
            Layout& SetGridAutoFlow(GridAutoFlow f);
            Layout& SetDisplay(DisplayType dt);
            Layout& Show();
            Layout& Hide();
        };

        struct LayoutItem {
            PositionType positionType = PositionType::Static;
            LayoutItemData data;
            // Populated when position == Relative/Absolute/Fixed.
            // For Relative: left/top/right/bottom act as a post-layout offset.
            // For Absolute/Fixed: insets define the box against the containing block.
            std::optional<Position> position;

            // ---- Positioning kind / insets ----
            LayoutItem& SetPositionType(PositionType p) { positionType = p; return *this; }
            LayoutItem& SetPositionInsets(const Position& insets) { position = insets; return *this; }

            // ---- Flex item properties (initializes data to FlexItem on first call) ----
            LayoutItem& SetFlexGrow(float g);
            LayoutItem& SetFlexShrink(float s);
            LayoutItem& SetFlexBasis(const Dimension& b);
            LayoutItem& SetFlex(float g, float s, const Dimension& b);
            LayoutItem& SetAlignSelf(AlignSelf a);
            LayoutItem& SetFlexOrder(int o);

            // ---- Grid item properties (initializes data to GridItem on first call) ----
            LayoutItem& SetGridColumn(GridLine start, GridLine end);
            LayoutItem& SetGridRow   (GridLine start, GridLine end);
            LayoutItem& SetGridRowColSimplified(int row, int col, int rowSpan = 1, int colSpan = 1);
            LayoutItem& SetJustifySelf(JustifySelf j);
            LayoutItem& SetGridAlignSelf(AlignSelf a);
        };

        // Per-element cache for an algorithm's intermediate state (flex line
        // groupings + flex distribution; grid placements + track sizing).
        // Owned by Element; the concrete type lives in the algorithm's .cpp
        // file. Invalidated by Element::InvalidateLayout / InvalidateSubtree.
        class LayoutComputed {
        public:
            virtual ~LayoutComputed() = default;
            MeasureConstraints key;
            bool valid = false;
            int  recomputeCount = 0;   // bumps every cache miss; useful for tests/profiling
        };

        class Element {
        public:
            // identity
            std::string id;

            // box model
            BoxModel box;

            // sizing
            BoxSize size;
            std::optional<BoxConstraints> boxConstraints;

            float aspectRatio = 0.0f;
            // TODO: replaced-element intrinsic size (images/video) — supply via subclass.

            // visibility / stacking
            Visibility visibility = Visibility::Visible;
            int zIndex = 0;   // applies when position != Static

            // layout
            Layout layout;
            LayoutItem layoutItem;

            // caches
            MeasureResult  measured;    // extrinsic, keyed by MeasureConstraints
            IntrinsicSizes intrinsic;   // constraint-independent

            // True once Arrange() has run and finalBounds (plus any subclass
            // post-layout setup performed in Arranged()) reflect the current
            // tree. Cleared by InvalidateLayout / InvalidateSubtree
            // The window render loop uses IsLayoutValid() to decide whether
            // a geometry pass is needed.
            bool arrangeValid = false;

            // Algorithm-specific cached state (e.g. flex line groupings, grid
            // track sizes). Lazily built by the relevant layout algorithm;
            // invalidated alongside `measured` / `intrinsic`. Public read access
            // for tests/profiling via ComputedLayout().
            std::unique_ptr<LayoutComputed> layoutComputed;

            // computed result
            Rect2Df finalBounds;

            virtual ~Element() = default;

            // ===== HIERARCHY =====
            // Children are owned via shared_ptr; parent link is non-owning. Always
            // mutate through these helpers — they keep parent links consistent and
            // bubble cache invalidation up the tree.
            void AddChild(std::shared_ptr<Element> kid);
            void RemoveChild(Element* kid);
            void ClearChildren();
            // Stable-sort the children vector in place by an arbitrary comparator.
            // Provided so subclasses can sort (e.g. by z-index) without exposing
            // the private vector. Does NOT invalidate layout (the resolved
            // measure/arrange doesn't depend on sibling order for any current
            // algorithm except Grid's auto-flow, which re-collects per Measure).
            using ChildComparator = std::function<bool(const std::shared_ptr<Element>&,
                                                       const std::shared_ptr<Element>&)>;
            void SortChildren(const ChildComparator& cmp);

            // Set the parent pointer WITHOUT inserting into the parent's
            // children vector. Used by hosts that own a child via their own
            // unique_ptr (e.g. a Container's scrollbars) but still want the
            // child's GetPositionInWindow / InvalidateRect walk to find them.
            // This breaks the parent ↔ children-of-parent invariant — callers
            // must NOT also AddChild() the same element.
            void SetParentNonOwning(Element* p) { parent = p; }

            const std::vector<std::shared_ptr<Element>>& Children() const { return children; }

            Element* Parent() const { return parent; }
            const LayoutComputed* ComputedLayout() const { return layoutComputed.get(); }

            // ===== TWO-PHASE LAYOUT =====
            // Cache wrapper + display-type dispatch (Flex / Grid / Block). Leaf
            // widgets do NOT override this — they override MeasureOwnContent to
            // publish their content-box size; the block path folds it in.
            void Measure(const MeasureConstraints& constraints, const LayoutContext& ctx);

            // Intrinsic content-box size of this element's OWN content (text,
            // image, internal parts). EXCLUDES padding/border and ignores child
            // Elements. `definiteContentWidth` is the resolved content width when
            // known (so wrapping content reports the right height); nullopt asks
            // for the max-content width. Default {0,0}: no own content (pure
            // container / leaf base). The block layout combines this with any
            // child-derived size, then applies size.*, the constraint, and min/max.
            virtual Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth,
                                              const LayoutContext& ctx);

            virtual void Arrange(const Rect2Df& finalRect, const LayoutContext& ctx);

            // Whether the most recent Arrange() result is still current (nothing
            // has invalidated layout since). The window render loop gates its
            // geometry pass on this.
            bool IsLayoutValid() const { return arrangeValid; }

            // Leaf elements (text, images) override this to publish constraint-independent
            // min/max-content sizes. Default: zero.
            virtual void ComputeIntrinsicSizes(const LayoutContext& ctx);

            // Drop this element's caches and bubble UP — every ancestor whose own
            // measure may have depended on this element drops its cache too. Use
            // this for the common case (this element's content/style changed).
            virtual void InvalidateLayout();

            // Drop caches across the whole subtree. Use only when something
            // affecting every descendant has changed (LayoutContext, root font
            // size, DPI, viewport).
            void InvalidateSubtree();

            // TODO: container queries hook (ContainerType + name).
            // TODO: anchor positioning hook.
        protected:
            std::vector<std::shared_ptr<Element>> children;
            Element* parent = nullptr;   // non-owning
        };

    } // namespace CSSLayout
} // namespace UltraCanvas
