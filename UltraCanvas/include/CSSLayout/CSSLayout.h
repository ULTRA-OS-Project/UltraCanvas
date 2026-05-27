// include/CSSLayout/CSSLayout.h
// CSS-compliant layout engine: type model and Element base class.
// Version: 4.3.0
// Last Modified: 2026-05-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasConfig.h"
#include <cmath>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <variant>

namespace UltraCanvas {
    namespace CSSLayout {

        // ---- Length values (CSS <length> / <percentage> / <flex>) ----

        enum class DimensionUnit { Auto, Pixels, Percent, Fr, MinContent, MaxContent, FitContent };

        struct Dimension {
            DimensionUnit unit = DimensionUnit::Auto;
            float value = 0.f;

            static Dimension Auto();
            static Dimension Px(float v);
            static Dimension Pct(float v);
            static Dimension Fr(float v);

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
        enum class PositionType   { Static, Relative, Absolute, Fixed };
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
        struct AbsoluteItem {
            Dimension left;
            Dimension top;
            Dimension right;
            Dimension bottom;
        };

        // ---- Measure result (extrinsic; cached per constraints) ----
        struct MeasureResult {
            MeasureConstraints key;
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
        };

        struct LayoutItem {
            PositionType position = PositionType::Static;
            LayoutItemData data;
            // Populated when position == Relative/Absolute/Fixed.
            // For Relative: left/top/right/bottom act as a post-layout offset.
            // For Absolute/Fixed: insets define the box against the containing block.
            std::optional<AbsoluteItem> positioned;
        };

        // Note: project-wide Rect2Df is Rect2D<double>; layout engine works in
        // single-precision throughout, so we alias to Rect2D<float> explicitly.
        using LayoutRect = Rect2D<float>;

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
            std::optional<BoxConstraints> constraints;

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

            // Algorithm-specific cached state (e.g. flex line groupings, grid
            // track sizes). Lazily built by the relevant layout algorithm;
            // invalidated alongside `measured` / `intrinsic`. Public read access
            // for tests/profiling via ComputedLayout().
            std::unique_ptr<LayoutComputed> layoutComputed;

            // computed result
            LayoutRect finalBounds;

            virtual ~Element() = default;

            // ===== HIERARCHY =====
            // Children are owned via shared_ptr; parent link is non-owning. Always
            // mutate through these helpers — they keep parent links consistent and
            // bubble cache invalidation up the tree.
            void AddChild(std::shared_ptr<Element> kid);
            void RemoveChild(Element* kid);
            void ClearChildren();

            const std::vector<std::shared_ptr<Element>>& Children() const { return children; }

            Element* Parent() const { return parent; }
            const LayoutComputed* ComputedLayout() const { return layoutComputed.get(); }

            // ===== TWO-PHASE LAYOUT =====
            // Cache wrapper. Subclasses override MeasureCore, not this.
            void Measure(const MeasureConstraints& constraints, const LayoutContext& ctx);

            virtual void MeasureCore(const MeasureConstraints& constraints, const LayoutContext& ctx);

            virtual void Arrange(const LayoutRect& finalRect, const LayoutContext& ctx);

            // Leaf elements (text, images) override this to publish constraint-independent
            // min/max-content sizes. Default: zero.
            virtual void ComputeIntrinsicSizes(const LayoutContext& ctx);

            // Drop this element's caches and bubble UP — every ancestor whose own
            // measure may have depended on this element drops its cache too. Use
            // this for the common case (this element's content/style changed).
            void InvalidateLayout();

            // Drop caches across the whole subtree. Use only when something
            // affecting every descendant has changed (LayoutContext, root font
            // size, DPI, viewport).
            void InvalidateSubtree();

            // TODO: container queries hook (ContainerType + name).
            // TODO: anchor positioning hook.

        private:
            std::vector<std::shared_ptr<Element>> children;
            Element* parent = nullptr;   // non-owning
        };

    } // namespace CSSLayout
} // namespace UltraCanvas
