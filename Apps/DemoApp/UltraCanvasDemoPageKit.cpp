// Apps/DemoApp/UltraCanvasDemoPageKit.cpp
// The shared demo page look (UltraCanvasDemoPageKit.h).
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemoPageKit.h"

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
            label->SetWrap(TextWrap::WrapWord);
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
