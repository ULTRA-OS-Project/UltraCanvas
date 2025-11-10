// core/UltraCanvasLayout.cpp
// Implementation of base layout class
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasLayout.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {
    UltraCanvasLayoutItem::UltraCanvasLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem)
            : element(elem)
    {}

    int UltraCanvasLayoutItem::GetPreferredWidth() const {
        if (element) {
            return element->GetPreferredWidth();
        }
        return 0;
    }

    int UltraCanvasLayoutItem::GetPreferredHeight() const {
        if (element) {
            return element->GetPreferredHeight();
        }
        return 0;
    }

    int UltraCanvasLayoutItem::GetMinimumWidth() const {
        if (element) {
            return element->GetMinWidth();
        }
        return 0;
    }

    int UltraCanvasLayoutItem::GetMinimumHeight() const {
        if (element) {
            return element->GetMinHeight();
        }
        return 0;
    }

    int UltraCanvasLayoutItem::GetMaximumWidth() const {
        if (element) {
            return element->GetMaxWidth();
        }
        return 10000;
    }

    int UltraCanvasLayoutItem::GetMaximumHeight() const {
        if (element) {
            return element->GetMaxHeight();
        }
        return 10000;
    }

    void UltraCanvasLayoutItem::ApplyToElement() {
        if (element) {
            element->SetBounds(computedX, computedY, computedWidth, computedHeight);
        }
    }

    int UltraCanvasLayoutItem::GetTotalMarginHorizontal() const {
        return element ? element->GetTotalMarginHorizontal() : 0;
    }

    int UltraCanvasLayoutItem::GetTotalMarginVertical() const {
        return element ? element->GetTotalMarginVertical() : 0;
    }

    int UltraCanvasLayoutItem::GetMarginLeft() const {
        return element ? element->GetMarginLeft() : 0;
    }

    int UltraCanvasLayoutItem::GetMarginRight() const {
        return element ? element->GetMarginRight() : 0;
    }

    int UltraCanvasLayoutItem::GetMarginTop() const {
        return element ? element->GetMarginTop() : 0;
    }

    int UltraCanvasLayoutItem::GetMarginBottom() const {
        return element ? element->GetMarginBottom() : 0;
    }

    bool UltraCanvasLayoutItem::IsVisible() const {
        return element ? element->IsVisible() : true;
    }

    UltraCanvasLayout::UltraCanvasLayout(UltraCanvasContainer* container) {
        container->SetLayout(this);
    }

    void UltraCanvasLayout::SetParentContainer(UltraCanvasContainer *parent) {
        parentContainer = parent;
        parentContainer->InvalidateLayout();
    }

    void UltraCanvasLayout::SetSpacing(int space) {
        spacing = space;
        InvalidateContainerLayout();
    }

    void UltraCanvasLayout::InvalidateContainerLayout() {
        if (parentContainer) {
            parentContainer->InvalidateLayout();
        }
    }

//    Rect2Di UltraCanvasLayout::GetContentRect(const Rect2Di &containerBounds) const {
//        auto result = Rect2Di(
//                marginLeft + paddingLeft,
//                marginTop + paddingTop,
//                containerBounds.width - GetTotalMarginHorizontal() - GetTotalPaddingHorizontal(),
//                containerBounds.height - GetTotalMarginVertical() - GetTotalPaddingVertical()
//        );
//        if (parentContainer) {
//            auto style = parentContainer->GetContainerStyle();
//            result.x += style.paddingLeft + style.borderWidth;
//            result.y += style.paddingTop + style.borderWidth;
//            result.width -= (style.paddingLeft + style.paddingRight + style.borderWidth * 2);
//            result.height -= (style.paddingTop + style.paddingBottom + style.borderWidth * 2);
//        }
//        return result;
//    }
} // namespace UltraCanvas
