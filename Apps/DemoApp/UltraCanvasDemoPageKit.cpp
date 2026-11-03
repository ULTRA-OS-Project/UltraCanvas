// Apps/DemoApp/UltraCanvasDemoPageKit.cpp
// The shared demo page look (UltraCanvasDemoPageKit.h).
// Version: 1.1.0 - ApplyContentMinWidth
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {
namespace DemoKit {

    namespace {

        ContainerStyle PlainStyle() {
            ContainerStyle style;
            style.autoShowScrollbars = false;
            return style;
        }

        std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text,
                                                    float size, FontWeight weight, const Color& color) {
            auto label = std::make_shared<UltraCanvasLabel>(id, text);
            label->SetFontSize(size);
            label->SetFontWeight(weight);
            label->SetTextColor(color);
            // Between words, and inside one only when it is longer than the
            // line (a code sample in a caption) - never cut off.
            label->SetWrap(TextWrap::WrapWordChar);
            label->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
            return label;
        }

        bool IsRow(const std::shared_ptr<UltraCanvasContainer>& parent) {
            return parent->layout.display == CSSLayout::DisplayType::Flex &&
                   std::holds_alternative<CSSLayout::FlexLayout>(parent->layout.data) &&
                   std::get<CSSLayout::FlexLayout>(parent->layout.data).direction == CSSLayout::FlexDirection::Row;
        }

    } // namespace

    std::shared_ptr<DemoPage> CreatePage(const std::string& id, const std::string& title,
                                         const std::string& subtitle) {
        auto page = std::make_shared<DemoPage>(id);
        page->SetBackgroundColor(PageBackground());
        page->SetPadding(kPagePadding);
        page->SetContainerStyle(PlainStyle());
        page->layout.SetFlexColumn().SetFlexGap(kPageGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        if (!title.empty()) Add(page, CreateTitle(id + "Title", title));
        if (!subtitle.empty()) Add(page, CreateSubtitle(id + "Subtitle", subtitle));
        return page;
    }

    std::shared_ptr<UltraCanvasLabel> CreateTitle(const std::string& id, const std::string& text) {
        return MakeLabel(id, text, 18, FontWeight::Bold, TitleText());
    }

    std::shared_ptr<UltraCanvasLabel> CreateSubtitle(const std::string& id, const std::string& text) {
        return MakeLabel(id, text, 11, FontWeight::Normal, SubtitleText());
    }

    std::shared_ptr<UltraCanvasLabel> CreateHeading(const std::string& id, const std::string& text) {
        return MakeLabel(id, text, 13, FontWeight::Bold, TitleText());
    }

    std::shared_ptr<UltraCanvasLabel> CreateText(const std::string& id, const std::string& text) {
        return MakeLabel(id, text, 12, FontWeight::Normal, BodyText());
    }

    std::shared_ptr<UltraCanvasLabel> CreateCaption(const std::string& id, const std::string& text) {
        return MakeLabel(id, text, 10, FontWeight::Normal, CaptionText());
    }

    std::shared_ptr<UltraCanvasLabel> CreateBullets(const std::string& id, const std::vector<std::string>& items) {
        std::string text;
        for (const auto& item : items) {
            if (!text.empty()) text += "\n";
            text += "• " + item;
        }
        return MakeLabel(id, text, 11, FontWeight::Normal, BodyText());
    }

    std::shared_ptr<UltraCanvasContainer> CreateCard(const std::string& id, const std::string& title,
                                                     const std::string& description) {
        auto card = std::make_shared<UltraCanvasContainer>(id);
        card->SetBackgroundColor(CardBackground());
        card->SetBorders(1.0f, CardBorder(), kCardRadius);
        card->SetPadding(kCardPadding);
        card->SetContainerStyle(PlainStyle());
        card->layout.SetFlexColumn().SetFlexGap(kCardGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        if (!title.empty()) Add(card, CreateHeading(id + "Title", title));
        if (!description.empty()) Add(card, CreateCaption(id + "Desc", description));
        return card;
    }

    std::shared_ptr<UltraCanvasContainer> CreateRow(const std::string& id, float gap, bool wrap) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->SetContainerStyle(PlainStyle());
        row->layout.SetFlexRow()
                   .SetFlexWrap(wrap ? CSSLayout::FlexWrap::Wrap : CSSLayout::FlexWrap::NoWrap)
                   .SetFlexGap(gap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        return row;
    }

    std::shared_ptr<UltraCanvasContainer> CreateColumn(const std::string& id, float gap) {
        auto column = std::make_shared<UltraCanvasContainer>(id);
        column->SetContainerStyle(PlainStyle());
        column->layout.SetFlexColumn().SetFlexGap(gap)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        return column;
    }

    void MakeInFlow(const std::shared_ptr<UltraCanvasUIElement>& element) {
        if (!element) return;
        if (element->layoutItem.positionType == CSSLayout::PositionType::AbsoluteUI ||
            element->layoutItem.positionType == CSSLayout::PositionType::Absolute) {
            element->layoutItem.SetPositionType(CSSLayout::PositionType::Static);
            element->layoutItem.position.reset();
        }
    }

    void Add(const std::shared_ptr<UltraCanvasContainer>& parent,
             const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
        if (!parent || !child) return;
        MakeInFlow(child);
        parent->AddChild(child);
        if (IsRow(parent)) {
            child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.0f : 0.0f);
        } else {
            child->layoutItem.SetFlexGrow(grow).SetFlexShrink(0.0f)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        }
    }

    void AddFixed(const std::shared_ptr<UltraCanvasContainer>& parent,
                  const std::shared_ptr<UltraCanvasUIElement>& child) {
        if (!parent || !child) return;
        MakeInFlow(child);
        parent->AddChild(child);
        child->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Start);
        // A fixed width is a floor, not a wish: the column measures "at
        // most" its own width and would cut the element down to it.
        if (child->size.width.unit == CSSLayout::DimensionUnit::Pixels) {
            CSSLayout::BoxConstraints limits = child->boxConstraints.value_or(CSSLayout::BoxConstraints{});
            limits.minWidth = child->size.width;
            child->boxConstraints = limits;
        }
    }

    void AddStretched(const std::shared_ptr<UltraCanvasContainer>& parent,
                      const std::shared_ptr<UltraCanvasUIElement>& child) {
        if (!parent || !child) return;
        child->size.width = CSSLayout::Dimension::Auto();
        Add(parent, child, IsRow(parent) ? 1.0f : 0.0f);
    }

    namespace {
        // A pixel width, else 0.
        float PixelWidth(const CSSLayout::Dimension& d) {
            return d.unit == CSSLayout::DimensionUnit::Pixels ? d.value : 0.0f;
        }

        // Whether the element shows what is wider than itself by scrolling.
        bool ScrollsSideways(UltraCanvasUIElement& element) {
            auto* container = dynamic_cast<UltraCanvasContainer*>(&element);
            if (!container) return false;
            const ContainerStyle& style = container->GetContainerStyle();
            return style.forceShowHorizontalScrollbar ||
                   (style.autoShowScrollbars && style.autoShowHorizontalScrollbar);
        }

        // The border-box width `element` needs for its fixed-size content,
        // 0 for none. A container with no width of its own is given it as
        // its min-width on the way back up (noted in `given`), so a card
        // sharing a row never comes out narrower than the chart in it - it
        // wraps instead.
        float RequireMinWidth(UltraCanvasUIElement& element,
                              std::vector<std::pair<UltraCanvasUIElement*, CSSLayout::Dimension>>& given) {
            if (!element.IsVisible()) return 0.0f;
            const auto position = element.layoutItem.positionType;
            if (position != CSSLayout::PositionType::Static &&
                position != CSSLayout::PositionType::Relative) return 0.0f;
            float own = PixelWidth(element.size.width);
            if (element.boxConstraints) own = std::max(own, PixelWidth(element.boxConstraints->minWidth));
            // A button with text needs at least its text, stretched or not
            // (known once it can be measured: the page's first draws).
            if (auto* button = dynamic_cast<UltraCanvasButton*>(&element)) {
                if (!button->GetText().empty() && button->GetRenderContext()) {
                    CSSLayout::LayoutContext context;
                    const float text = button->MeasureOwnContent(std::nullopt, context).width +
                                       button->GetTotalPaddingHorizontal() + button->GetTotalBorderHorizontal();
                    own = std::max(own, std::ceil(text));
                }
            }
            // A set width is the element's whole box; what is inside it is
            // its own business (it clips or scrolls it).
            if (own > 0.0f) return own;
            // A row that does not wrap needs its children side by side; any
            // other container, the widest of them (a wrapping row stacks
            // them when it has to).
            bool sideBySide = false;
            float gap = 0.0f;
            if (element.layout.display == CSSLayout::DisplayType::Flex &&
                std::holds_alternative<CSSLayout::FlexLayout>(element.layout.data)) {
                const auto& flex = std::get<CSSLayout::FlexLayout>(element.layout.data);
                sideBySide = flex.direction == CSSLayout::FlexDirection::Row &&
                             flex.wrap == CSSLayout::FlexWrap::NoWrap;
                gap = PixelWidth(flex.gap.column);
            }
            float widest = 0.0f;
            int counted = 0;
            for (auto& child : element.Children()) {
                auto* ui = dynamic_cast<UltraCanvasUIElement*>(child.get());
                if (!ui) continue;
                float needed = RequireMinWidth(*ui, given);
                // Side by side, a child that never shrinks takes at least
                // its pixel basis.
                if (sideBySide && std::holds_alternative<CSSLayout::FlexItem>(ui->layoutItem.data)) {
                    const auto& item = std::get<CSSLayout::FlexItem>(ui->layoutItem.data);
                    if (item.shrink == 0.0f) needed = std::max(needed, PixelWidth(item.basis));
                }
                if (needed <= 0.0f) continue;
                const float outer = needed + ui->GetTotalMarginHorizontal();
                widest = sideBySide ? widest + outer + (counted ? gap : 0.0f) : std::max(widest, outer);
                ++counted;
            }
            // What a sideways-scrolling container holds is reached by
            // scrolling: its children got their widths, it keeps its own.
            if (widest <= 0.0f || ScrollsSideways(element)) return 0.0f;
            const float needed = widest + element.GetTotalPaddingHorizontal() + element.GetTotalBorderHorizontal();
            CSSLayout::BoxConstraints limits = element.boxConstraints.value_or(CSSLayout::BoxConstraints{});
            given.emplace_back(&element, limits.minWidth);
            limits.minWidth = CSSLayout::Dimension::Px(needed);
            element.boxConstraints = limits;
            return needed;
        }
    } // namespace

    float ApplyContentMinWidth(DemoPage& page) {
        // Back to what the elements had, then worked out afresh: a width the
        // kit set last time is not a fixed width this time.
        for (auto& [element, before] : page.kitMinWidths) {
            if (element->boxConstraints) element->boxConstraints->minWidth = before;
        }
        page.kitMinWidths.clear();
        return RequireMinWidth(page, page.kitMinWidths);
    }

    void DemoPage::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        UltraCanvasContainer::Arrange(finalRect, ctx);
        if (minWidthPasses >= 3) return;
        ++minWidthPasses;
        auto widths = [this]() {
            std::vector<float> values;
            for (auto& entry : kitMinWidths) values.push_back(entry.first->boxConstraints->minWidth.value);
            return values;
        };
        const auto before = widths();
        ApplyContentMinWidth(*this);
        if (widths() != before) {
            // The rows and cards answer from their cached measurements,
            // which knew nothing of the new min-widths: forget them all, and
            // lay the window out again once this pass is over (marked
            // during it, the window would mark itself laid out at its end).
            InvalidateSubtree();
            std::weak_ptr<bool> stillHere = alive;
            if (auto* app = UltraCanvasApplication::GetInstance()) {
                app->PostToUIThread([this, stillHere]() {
                    if (stillHere.expired()) return;
                    InvalidateLayout();
                    RequestRedraw();
                });
            }
        }
    }

    std::shared_ptr<UltraCanvasContainer> AddRow(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                 const std::string& id, float gap) {
        auto row = CreateRow(id, gap);
        Add(parent, row);
        return row;
    }

    std::shared_ptr<UltraCanvasContainer> AddCard(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                  const std::string& id, const std::string& title,
                                                  const std::string& description, float minWidth) {
        auto card = CreateCard(id, title, description);
        if (IsRow(parent)) {
            MakeInFlow(card);
            parent->AddChild(card);
            card->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                            .SetFlexBasis(CSSLayout::Dimension::Px(minWidth));
        } else {
            Add(parent, card);
        }
        return card;
    }

    std::shared_ptr<UltraCanvasLabel> AddText(const std::shared_ptr<UltraCanvasContainer>& parent,
                                              const std::string& id, const std::string& text) {
        auto label = CreateText(id, text);
        Add(parent, label);
        return label;
    }

    std::shared_ptr<UltraCanvasLabel> AddCaption(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                 const std::string& id, const std::string& text) {
        auto label = CreateCaption(id, text);
        Add(parent, label);
        return label;
    }

    std::shared_ptr<UltraCanvasLabel> AddHeading(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                 const std::string& id, const std::string& text) {
        auto label = CreateHeading(id, text);
        Add(parent, label);
        return label;
    }

    std::shared_ptr<UltraCanvasContainer> AddNotes(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                   const std::string& id, const std::string& title,
                                                   const std::vector<std::string>& items) {
        auto card = AddCard(parent, id, title);
        Add(card, CreateBullets(id + "List", items));
        return card;
    }

} // namespace DemoKit
} // namespace UltraCanvas
