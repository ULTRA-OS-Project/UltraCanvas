// include/UltraCanvasLayoutCompat.h
// TRANSITIONAL compatibility shim for the retired UltraCanvasLayout system
// (BoxLayout / FlexLayout / GridLayout). These inline factory shims configure
// the engine-side `container->layout` (CSSLayout::Layout) so existing call
// sites compile while we migrate them to the direct API:
//
//   c->layout.SetFlexColumn().SetFlexGap(10);
//   c->AddChild(btn);
//   c->AddSpacer(8);
//   c->AddStretchSpacer(1);
//
// New code should prefer the direct form. This header will be deleted once
// all consumers are migrated.
//
// Version: 1.0.0
// Last Modified: 2026-05-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"

namespace UltraCanvas {

    // Old enum aliases.
    enum class BoxLayoutDirection { Horizontal = 0, Vertical = 1 };
    enum class LayoutAlignment { Start, Center, End, Fill };
    // Note: SizeMode is defined in UltraCanvasCommonTypes.h; reuse that one.

    // Map old LayoutAlignment → new CSSLayout::AlignSelf.
    inline CSSLayout::AlignSelf toAlignSelf(LayoutAlignment a) {
        switch (a) {
            case LayoutAlignment::Start:  return CSSLayout::AlignSelf::Start;
            case LayoutAlignment::Center: return CSSLayout::AlignSelf::Center;
            case LayoutAlignment::End:    return CSSLayout::AlignSelf::End;
            case LayoutAlignment::Fill:   return CSSLayout::AlignSelf::Stretch;
        }
        return CSSLayout::AlignSelf::Auto;
    }

    // Item wrapper exposing the old AddUIElement chaining API on top of the
    // engine's LayoutItem. Returned by AddUIElement / InsertUIElement /
    // AddSpacer / AddStretch. All setters return `this` for chaining.
    class LegacyLayoutItem {
    public:
        std::shared_ptr<UltraCanvasUIElement> element;

        explicit LegacyLayoutItem(std::shared_ptr<UltraCanvasUIElement> e) : element(std::move(e)) {}

        LegacyLayoutItem* SetStretch(float s) {
            if (element) element->layoutItem.SetFlexGrow(s);
            return this;
        }
        LegacyLayoutItem* SetCrossAlignment(LayoutAlignment a) {
            if (element) element->layoutItem.SetAlignSelf(toAlignSelf(a));
            return this;
        }
        LegacyLayoutItem* SetMainAlignment(LayoutAlignment /*a*/) {
            // Main-axis alignment is a container-level concern in CSS
            // (justify-content). Per-item main alignment isn't representable
            // in flex; silently ignore — old callers used this only for
            // visual centering, which the container's align-items handles.
            return this;
        }
        LegacyLayoutItem* SetWidthMode(SizeMode m) {
            // Translate the legacy "Fill on width" mode to align-self: Stretch
            // (cross axis). Other modes are no-ops here.
            if (element && m == SizeMode::Fill) {
                element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            }
            return this;
        }
        LegacyLayoutItem* SetHeightMode(SizeMode m) {
            if (element && m == SizeMode::Fill) {
                element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            }
            return this;
        }
        LegacyLayoutItem* SetSizeMode(SizeMode w, SizeMode h) {
            SetWidthMode(w); SetHeightMode(h); return this;
        }
        LegacyLayoutItem* SetFlexGrow(float g)   { return SetStretch(g); }
        LegacyLayoutItem* SetFlexShrink(float s) {
            if (element) element->layoutItem.SetFlexShrink(s);
            return this;
        }
        LegacyLayoutItem* SetFlexBasis(float b)  {
            if (element) element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(b));
            return this;
        }
        LegacyLayoutItem* SetFixedWidth(float)   { return this; }  // honored by element's own size
        LegacyLayoutItem* SetFixedHeight(float)  { return this; }
        LegacyLayoutItem* SetMinimumSize(float, float) { return this; }
        LegacyLayoutItem* SetMaximumSize(float, float) { return this; }
        LegacyLayoutItem* SetAlignSelf(LayoutAlignment a) { return SetCrossAlignment(a); }
    };

    // Lightweight adapter returned by Create*Layout() functions. Provides
    // the old method names (AddUIElement, AddSpacing, AddStretch,
    // SetSpacing, etc.) by forwarding to the new engine API on the
    // container's layout / children.
    class LegacyLayoutAdapter {
    public:
        UltraCanvasContainer* container = nullptr;
        bool isRow = false;

        LegacyLayoutAdapter() = default;
        LegacyLayoutAdapter(UltraCanvasContainer* c, bool row) : container(c), isRow(row) {}

        LegacyLayoutItem* AddUIElement(std::shared_ptr<UltraCanvasUIElement> elem,
                                       float stretch = 0.0f) {
            if (!container || !elem) {
                static LegacyLayoutItem dead{nullptr};
                return &dead;
            }
            container->AddChild(elem);
            if (stretch > 0) elem->layoutItem.SetFlexGrow(stretch);
            ownedItems.emplace_back(std::make_unique<LegacyLayoutItem>(elem));
            return ownedItems.back().get();
        }

        LegacyLayoutItem* InsertUIElement(std::shared_ptr<UltraCanvasUIElement> elem, int /*index*/) {
            // Insertion order: append for now; honoring `index` would need
            // a reorder primitive. Most call sites use AddUIElement.
            return AddUIElement(elem);
        }

        void AddSpacing(int size) {
            if (container) container->AddSpacer(static_cast<float>(size));
        }
        void AddStretch(int stretch = 1) {
            if (container) container->AddStretchSpacer(static_cast<float>(stretch));
        }

        void SetSpacing(int n) {
            if (container) container->layout.SetFlexGap(static_cast<float>(n));
        }
        void SetGap(int n) { SetSpacing(n); }
        void SetGap(int row, int col) {
            if (container) container->layout.SetFlexGap(static_cast<float>(row), static_cast<float>(col));
        }

        void SetDirection(BoxLayoutDirection d) {
            if (!container) return;
            if (d == BoxLayoutDirection::Horizontal) container->layout.SetFlexRow();
            else                                     container->layout.SetFlexColumn();
            isRow = (d == BoxLayoutDirection::Horizontal);
        }

        void SetDefaultCrossAxisAlignment(LayoutAlignment a) {
            if (!container) return;
            CSSLayout::AlignItems ai;
            switch (a) {
                case LayoutAlignment::Start:  ai = CSSLayout::AlignItems::Start;  break;
                case LayoutAlignment::Center: ai = CSSLayout::AlignItems::Center; break;
                case LayoutAlignment::End:    ai = CSSLayout::AlignItems::End;    break;
                case LayoutAlignment::Fill:
                default:                      ai = CSSLayout::AlignItems::Stretch; break;
            }
            container->layout.SetAlignItems(ai);
        }
        void SetDefaultMainAxisAlignment(LayoutAlignment a) {
            if (!container) return;
            CSSLayout::JustifyContent jc;
            switch (a) {
                case LayoutAlignment::Center: jc = CSSLayout::JustifyContent::Center; break;
                case LayoutAlignment::End:    jc = CSSLayout::JustifyContent::End;    break;
                case LayoutAlignment::Fill:   jc = CSSLayout::JustifyContent::Stretch; break;
                case LayoutAlignment::Start:
                default:                      jc = CSSLayout::JustifyContent::Start; break;
            }
            container->layout.SetJustifyContent(jc);
        }

        void ClearItems() {
            if (container) container->ClearChildren();
            ownedItems.clear();
        }

    private:
        std::vector<std::unique_ptr<LegacyLayoutItem>> ownedItems;
    };

    // Owned adapters are stored on the container itself so the LegacyLayoutItem
    // pointers stay valid for the container's lifetime.
    inline std::unordered_map<UltraCanvasContainer*, std::unique_ptr<LegacyLayoutAdapter>>&
    GetLegacyAdapterRegistry() {
        static std::unordered_map<UltraCanvasContainer*, std::unique_ptr<LegacyLayoutAdapter>> r;
        return r;
    }

    inline LegacyLayoutAdapter* MakeLegacyAdapter(UltraCanvasContainer* c, bool isRow) {
        auto& reg = GetLegacyAdapterRegistry();
        auto& slot = reg[c];
        slot = std::make_unique<LegacyLayoutAdapter>(c, isRow);
        return slot.get();
    }

    // ===== Legacy factories =====

    inline LegacyLayoutAdapter* CreateHBoxLayout(UltraCanvasContainer* c) {
        if (!c) return nullptr;
        c->layout.SetFlexRow();
        return MakeLegacyAdapter(c, true);
    }

    inline LegacyLayoutAdapter* CreateVBoxLayout(UltraCanvasContainer* c) {
        if (!c) return nullptr;
        c->layout.SetFlexColumn();
        return MakeLegacyAdapter(c, false);
    }

    enum class FlexDirection { Row = 0, RowReverse = 1, Column = 2, ColumnReverse = 3 };
    enum class FlexWrap      { NoWrap = 0, Wrap = 1, WrapReverse = 2 };
    enum class FlexJustifyContent {
        Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly
    };
    enum class FlexAlignItems   { Start, End, Center, Stretch, Baseline };
    enum class FlexAlignContent { Start, End, Center, Stretch, SpaceBetween, SpaceAround };

    // Flex adapter exposing old SetFlexWrap / SetJustifyContent / SetAlignItems etc.
    class LegacyFlexAdapter : public LegacyLayoutAdapter {
    public:
        using LegacyLayoutAdapter::LegacyLayoutAdapter;
        using LegacyLayoutAdapter::AddUIElement;

        // Old FlexLayout::AddUIElement(elem, grow, shrink, basis) overload.
        LegacyLayoutItem* AddUIElement(std::shared_ptr<UltraCanvasUIElement> elem,
                                       float grow, float shrink, float basis) {
            LegacyLayoutItem* item = LegacyLayoutAdapter::AddUIElement(elem, grow);
            if (item && item->element) {
                item->element->layoutItem.SetFlexShrink(shrink);
                if (basis > 0) item->element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(basis));
            }
            return item;
        }

        void SetFlexWrap(FlexWrap w) {
            if (!container) return;
            switch (w) {
                case FlexWrap::NoWrap:      container->layout.SetFlexWrap(CSSLayout::FlexWrap::NoWrap); break;
                case FlexWrap::Wrap:        container->layout.SetFlexWrap(CSSLayout::FlexWrap::Wrap); break;
                case FlexWrap::WrapReverse: container->layout.SetFlexWrap(CSSLayout::FlexWrap::WrapReverse); break;
            }
        }
        void SetJustifyContent(FlexJustifyContent jc) {
            if (!container) return;
            CSSLayout::JustifyContent v;
            switch (jc) {
                case FlexJustifyContent::Start:        v = CSSLayout::JustifyContent::Start; break;
                case FlexJustifyContent::End:          v = CSSLayout::JustifyContent::End; break;
                case FlexJustifyContent::Center:       v = CSSLayout::JustifyContent::Center; break;
                case FlexJustifyContent::SpaceBetween: v = CSSLayout::JustifyContent::SpaceBetween; break;
                case FlexJustifyContent::SpaceAround:  v = CSSLayout::JustifyContent::SpaceAround; break;
                case FlexJustifyContent::SpaceEvenly:  v = CSSLayout::JustifyContent::SpaceEvenly; break;
            }
            container->layout.SetJustifyContent(v);
        }
        void SetAlignItems(FlexAlignItems ai) {
            if (!container) return;
            CSSLayout::AlignItems v;
            switch (ai) {
                case FlexAlignItems::Start:    v = CSSLayout::AlignItems::Start; break;
                case FlexAlignItems::End:      v = CSSLayout::AlignItems::End; break;
                case FlexAlignItems::Center:   v = CSSLayout::AlignItems::Center; break;
                case FlexAlignItems::Stretch:  v = CSSLayout::AlignItems::Stretch; break;
                case FlexAlignItems::Baseline: v = CSSLayout::AlignItems::Baseline; break;
            }
            container->layout.SetAlignItems(v);
        }
        void SetAlignContent(FlexAlignContent ac) {
            if (!container) return;
            CSSLayout::AlignContent v;
            switch (ac) {
                case FlexAlignContent::Start:        v = CSSLayout::AlignContent::Start; break;
                case FlexAlignContent::End:          v = CSSLayout::AlignContent::End; break;
                case FlexAlignContent::Center:       v = CSSLayout::AlignContent::Center; break;
                case FlexAlignContent::Stretch:      v = CSSLayout::AlignContent::Stretch; break;
                case FlexAlignContent::SpaceBetween: v = CSSLayout::AlignContent::SpaceBetween; break;
                case FlexAlignContent::SpaceAround:  v = CSSLayout::AlignContent::SpaceAround; break;
            }
            container->layout.SetAlignContent(v);
        }
    };

    inline LegacyFlexAdapter* CreateFlexLayout(UltraCanvasContainer* c,
                                               FlexDirection d = FlexDirection::Row) {
        if (!c) return nullptr;
        CSSLayout::FlexDirection ed;
        switch (d) {
            case FlexDirection::Row:           ed = CSSLayout::FlexDirection::Row; break;
            case FlexDirection::RowReverse:    ed = CSSLayout::FlexDirection::RowReverse; break;
            case FlexDirection::Column:        ed = CSSLayout::FlexDirection::Column; break;
            case FlexDirection::ColumnReverse: ed = CSSLayout::FlexDirection::ColumnReverse; break;
        }
        c->layout.SetFlexDirection(ed);
        auto& reg = GetLegacyAdapterRegistry();
        auto fa = std::make_unique<LegacyFlexAdapter>(c, d == FlexDirection::Row);
        LegacyFlexAdapter* raw = fa.get();
        reg[c] = std::move(fa);
        return raw;
    }

    // ===== Grid compat =====
    enum class GridSizeMode { Fixed, Auto, Percent, Star };
    struct GridRowColumnDefinition {
        GridSizeMode sizeMode = GridSizeMode::Auto;
        int size = 0;
        int minSize = -1;
        int maxSize = -1;

        static GridRowColumnDefinition Fixed(int px)     { return {GridSizeMode::Fixed,   px, -1, -1}; }
        static GridRowColumnDefinition Auto()            { return {GridSizeMode::Auto,    0,  -1, -1}; }
        static GridRowColumnDefinition Percent(int pct)  { return {GridSizeMode::Percent, pct, -1, -1}; }
        static GridRowColumnDefinition Star(int w = 1)   { return {GridSizeMode::Star,    w,  -1, -1}; }
    };

    inline CSSLayout::GridTrackSize toGridTrack(const GridRowColumnDefinition& d) {
        CSSLayout::GridTrackSize ts;
        switch (d.sizeMode) {
            case GridSizeMode::Fixed:
                ts.kind  = CSSLayout::GridTrackSizeKind::Fixed;
                ts.value = CSSLayout::Dimension::Px(static_cast<float>(d.size));
                break;
            case GridSizeMode::Percent:
                ts.kind  = CSSLayout::GridTrackSizeKind::Percent;
                ts.value = CSSLayout::Dimension::Pct(static_cast<float>(d.size));
                break;
            case GridSizeMode::Star:
                ts.kind  = CSSLayout::GridTrackSizeKind::Fr;
                ts.value = CSSLayout::Dimension::Fr(static_cast<float>(d.size));
                break;
            case GridSizeMode::Auto:
            default:
                ts.kind = CSSLayout::GridTrackSizeKind::Auto;
                break;
        }
        return ts;
    }

    class LegacyGridAdapter {
    public:
        UltraCanvasContainer* container = nullptr;
        int rows = 0, cols = 0;

        LegacyGridAdapter(UltraCanvasContainer* c, int r, int co) : container(c), rows(r), cols(co) {
            if (container) {
                container->layout.SetGrid();
                container->layout.SetGridColumns(std::vector<CSSLayout::GridTrackSize>(cols));
                container->layout.SetGridRows   (std::vector<CSSLayout::GridTrackSize>(rows));
            }
        }

        void SetRowDefinition(int row, const GridRowColumnDefinition& def) {
            if (!container) return;
            auto& gl = std::get<CSSLayout::GridLayout>(container->layout.data);
            if (row >= 0 && row < (int)gl.rows.tracks.size()) {
                gl.rows.tracks[row] = toGridTrack(def);
            }
        }
        void SetColumnDefinition(int col, const GridRowColumnDefinition& def) {
            if (!container) return;
            auto& gl = std::get<CSSLayout::GridLayout>(container->layout.data);
            if (col >= 0 && col < (int)gl.columns.tracks.size()) {
                gl.columns.tracks[col] = toGridTrack(def);
            }
        }
        void SetGap(int n) {
            if (container) container->layout.SetGridGap(static_cast<float>(n));
        }
        void SetSpacing(int n) { SetGap(n); }
        void SetGridSize(int r, int c) {
            if (container) {
                container->layout.SetGridColumns(std::vector<CSSLayout::GridTrackSize>(c));
                container->layout.SetGridRows   (std::vector<CSSLayout::GridTrackSize>(r));
            }
            rows = r; cols = c;
        }

        LegacyLayoutItem* AddUIElement(std::shared_ptr<UltraCanvasUIElement> elem,
                                       int row, int col, int rowSpan = 1, int colSpan = 1) {
            if (!container || !elem) {
                static LegacyLayoutItem dead{nullptr};
                return &dead;
            }
            container->AddChild(elem);
            CSSLayout::GridLine cs; cs.type = CSSLayout::GridLineKind::Line; cs.index = col + 1;
            CSSLayout::GridLine rs; rs.type = CSSLayout::GridLineKind::Line; rs.index = row + 1;
            CSSLayout::GridLine ce; ce.type = CSSLayout::GridLineKind::Span; ce.index = colSpan;
            CSSLayout::GridLine re; re.type = CSSLayout::GridLineKind::Span; re.index = rowSpan;
            elem->layoutItem.SetGridColumn(cs, ce).SetGridRow(rs, re);
            ownedItems.emplace_back(std::make_unique<LegacyLayoutItem>(elem));
            return ownedItems.back().get();
        }

    private:
        std::vector<std::unique_ptr<LegacyLayoutItem>> ownedItems;
    };

    inline std::unordered_map<UltraCanvasContainer*, std::unique_ptr<LegacyGridAdapter>>&
    GetLegacyGridRegistry() {
        static std::unordered_map<UltraCanvasContainer*, std::unique_ptr<LegacyGridAdapter>> r;
        return r;
    }

    inline LegacyGridAdapter* CreateGridLayout(UltraCanvasContainer* c, int rows, int cols) {
        if (!c) return nullptr;
        auto ga = std::make_unique<LegacyGridAdapter>(c, rows, cols);
        LegacyGridAdapter* raw = ga.get();
        GetLegacyGridRegistry()[c] = std::move(ga);
        return raw;
    }

} // namespace UltraCanvas
