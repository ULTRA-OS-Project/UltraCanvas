// core/UltraCanvasSegmentedControl.cpp
// Implementation of segmented control component
// Version: 1.0.0
// Last Modified: 2025-10-19
// Author: UltraCanvas Framework

#include "UltraCanvasSegmentedControl.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== LAYOUT CALCULATION =====

    void UltraCanvasSegmentedControl::CalculateLayout(IRenderContext* ctx) {
        segmentRects.clear();
        if (segments.empty()) {
            layoutDirty = false;
            return;
        }

        Rect2Di bounds = GetBounds();
        int availableWidth = bounds.width;

        // Account for overall border
        //availableWidth -= static_cast<int>(style.borderWidth * 2);

        // Calculate segment widths
        std::vector<int> segmentWidths;

        switch (widthMode) {
            case SegmentWidthMode::Equal: {
                int spacing = style.segmentSpacing * (static_cast<int>(segments.size()) - 1);
                int segmentWidth = (availableWidth - spacing) / static_cast<int>(segments.size());
                for (size_t i = 0; i < segments.size(); i++) {
                    segmentWidths.push_back(segmentWidth);
                }
                break;
            }

            case SegmentWidthMode::FitContent: {
                // Calculate content width for each segment
                int totalContentWidth = 0;
                for (const auto& segment : segments) {
                    int contentWidth = CalculateSegmentContentWidth(ctx, segment);
                    segmentWidths.push_back(contentWidth);
                    totalContentWidth += contentWidth;
                }

                // Scale if needed
                int spacing = style.segmentSpacing * (static_cast<int>(segments.size()) - 1);
                float scale = static_cast<float>(availableWidth - spacing) / totalContentWidth;
                for(auto i = 0; i < segmentWidths.size(); i++) {
                    segmentWidths[i] = segmentWidths[i] * scale;
                }
                break;
            }

            case SegmentWidthMode::Custom: {
                for (const auto& segment : segments) {
                    int width = segment.customWidth > 0 ?
                                static_cast<int>(segment.customWidth) :
                                CalculateSegmentContentWidth(ctx, segment);
                    segmentWidths.push_back(width);
                }
                break;
            }
        }

        // Calculate segment rectangles
        int currentX = bounds.x;
        int segmentY = bounds.y;
        int segmentHeight = bounds.height;

        if (style.borderWidth > 0) {
            currentX += static_cast<int>(style.borderWidth);
//            segmentY += static_cast<int>(style.borderWidth);
//            segmentHeight -= static_cast<int>(style.borderWidth * 2);
        }

        for (size_t i = 0; i < segments.size(); i++) {
            Rect2Di rect(currentX, segmentY, segmentWidths[i], segmentHeight);
            segmentRects.push_back(rect);
            currentX += segmentWidths[i] + style.segmentSpacing;
        }

        layoutDirty = false;
    }

    int UltraCanvasSegmentedControl::CalculateSegmentContentWidth(IRenderContext* ctx, const SegmentData& segment) {
        int width = style.paddingHorizontal * 2;

        // Add icon width
        if (segment.HasIcon()) {
            width += style.iconSize;
            if (segment.HasText()) {
                width += style.iconSpacing;
            }
        }

        // Add text width
        if (segment.HasText()) {
            ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);

            int textWidth = 0, textHeight = 0;
            ctx->GetTextDimension(segment.text, textWidth, textHeight);
            width += textWidth;
        }

        return width;
    }

// ===== RENDERING IMPLEMENTATION =====

    void UltraCanvasSegmentedControl::Render() {
        if (!IsVisible()) return;

        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        ctx->PushState();

        // Update layout if needed
        if (layoutDirty) {
            CalculateLayout(ctx);
        }

        // Update animation
        if (style.enableAnimation && selectionAnimationProgress < 1.0f) {
            UpdateAnimation();
        }

        // Render based on style
        RenderSegments(ctx);

        ctx->PopState();
    }

    void UltraCanvasSegmentedControl::RenderSegments(IRenderContext* ctx) {
        Rect2Di bounds = GetBounds();

        // Draw outer border
        ctx->SetStrokePaint(style.borderColor);
        ctx->SetStrokeWidth(style.borderWidth);
        if (style.normalColor.a > 0) {
            ctx->SetFillPaint(style.normalColor);
            ctx->FillRoundedRectangle(bounds, style.cornerRadius);
        }

        // Render each segment
        for (size_t i = 0; i < segments.size(); i++) {
            RenderSegment(ctx, static_cast<int>(i));
        }

        ctx->SetStrokePaint(style.borderColor);
        ctx->SetStrokeWidth(style.borderWidth);
        ctx->DrawRoundedRectangle(bounds, style.cornerRadius);

        // Draw separators
        if (style.separatorWidth > 0) {
            ctx->SetStrokePaint(style.separatorColor);
            ctx->SetStrokeWidth(style.separatorWidth);

            for (size_t i = 1; i < segmentRects.size(); i++) {
                int x = segmentRects[i].x;
                ctx->DrawLine(
                        x, bounds.y + style.borderWidth,
                        x, bounds.y + bounds.height - style.borderWidth
                );
            }
        }
    }

    void UltraCanvasSegmentedControl::RenderSegment(IRenderContext* ctx, int index) {
        if (index < 0 || index >= static_cast<int>(segmentRects.size())) return;

        const Rect2Di& rect = segmentRects[index];
        const SegmentData& segment = segments[index];

        // Determine segment state colors
        Color bgColor, textColor;
        bool isSelected = (index == selectedIndex);
        bool isHovered = (index == hoveredIndex) && segment.enabled;
        bool isPressed = (index == pressedIndex) && segment.enabled;

        if (!segment.enabled) {
            bgColor = style.disabledColor;
            textColor = style.disabledTextColor;
        } else if (isSelected) {
            bgColor = style.selectedColor;
            textColor = style.selectedTextColor;
        } else if (isPressed) {
            bgColor = style.hoverColor;
            textColor = style.hoverTextColor;
        } else if (isHovered) {
            bgColor = style.hoverColor;
            textColor = style.hoverTextColor;
        } else {
            bgColor = style.normalColor;
            textColor = style.normalTextColor;
        }

        // Apply animation if transitioning
        if (style.enableAnimation && selectionAnimationProgress < 1.0f) {
            if (index == animationFromIndex || index == animationToIndex) {
                // Blend colors during animation
                Color fromColor = (index == animationFromIndex) ? style.selectedColor : style.normalColor;
                Color toColor = (index == animationToIndex) ? style.selectedColor : style.normalColor;

                float t = selectionAnimationProgress;
                bgColor = Color(
                        static_cast<uint8_t>(fromColor.r * (1 - t) + toColor.r * t),
                        static_cast<uint8_t>(fromColor.g * (1 - t) + toColor.g * t),
                        static_cast<uint8_t>(fromColor.b * (1 - t) + toColor.b * t),
                        static_cast<uint8_t>(fromColor.a * (1 - t) + toColor.a * t)
                );

                Color fromTextColor = (index == animationFromIndex) ? style.selectedTextColor : style.normalTextColor;
                Color toTextColor = (index == animationToIndex) ? style.selectedTextColor : style.normalTextColor;

                textColor = Color(
                        static_cast<uint8_t>(fromTextColor.r * (1 - t) + toTextColor.r * t),
                        static_cast<uint8_t>(fromTextColor.g * (1 - t) + toTextColor.g * t),
                        static_cast<uint8_t>(fromTextColor.b * (1 - t) + toTextColor.b * t),
                        static_cast<uint8_t>(fromTextColor.a * (1 - t) + toTextColor.a * t)
                );
            }
        }

        // Draw segment background
        if (bgColor.a > 0) {
            ctx->SetFillPaint(bgColor);

            // For bordered style, clip to rounded corners
            float radius = style.cornerRadius;
            bool isFirst = (index == 0);
            bool isLast = (index == static_cast<int>(segments.size()) - 1);

            if (isFirst || isLast) {
                // Use rounded rectangle with selective corners
                // Create clipping path
                ctx->ClearPath();

                if (isFirst && !isLast) {
                    // Round left corners only
                    ctx->MoveTo(rect.x + radius, rect.y);
                    ctx->LineTo(rect.x + rect.width, rect.y);
                    ctx->LineTo(rect.x + rect.width, rect.y + rect.height);
                    ctx->LineTo(rect.x + radius, rect.y + rect.height);
                    ctx->Arc(rect.x + radius, rect.y + rect.height - radius, radius, M_PI/2, M_PI);
                    ctx->Arc(rect.x + radius, rect.y + radius, radius, M_PI, 3*M_PI/2);
                } else if (isLast && !isFirst) {
                    // Round right corners only
                    ctx->MoveTo(rect.x + rect.width - radius, rect.y + rect.height);
                    ctx->LineTo(rect.x, rect.y + rect.height);
                    ctx->LineTo(rect.x, rect.y);
                    ctx->LineTo(rect.x + rect.width - radius, rect.y);
                    ctx->Arc(rect.x + rect.width - radius + 1, rect.y + radius, radius, -M_PI_2, 0);
                    ctx->Arc(rect.x + rect.width - radius + 1, rect.y + rect.height - radius, radius, 0, M_PI_2);
                } else {
                    // Single segment - round all corners
                    ctx->RoundedRect(rect.x, rect.y, rect.width, rect.height, radius);
                }

                ctx->ClosePath();
                ctx->Fill();
            } else {
                // Middle segments - no rounding
                ctx->FillRectangle(rect.x, rect.y, rect.width, rect.height);
            }
        }

        // Calculate content layout
        int contentX = rect.x + style.paddingHorizontal;
        int contentY = rect.y + rect.height / 2;
        int totalContentWidth = 0;

        // Calculate total content width for centering
//        if (segment.HasIcon()) {
//            totalContentWidth += style.iconSize;
//            if (segment.HasText()) {
//                totalContentWidth += style.iconSpacing;
//            }
//        }

//        if (segment.HasText()) {
//            ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
//            ctx->SetFontSize(style.fontSize);
//
//            int textWidth = 0, textHeight = 0;
//            ctx->GetTextDimension(segment.text, textWidth, textHeight);
//            totalContentWidth += textWidth;
//        }

        // Center content horizontally
//        contentX = rect.x + (rect.width - totalContentWidth) / 2;

        // Render icon if present
        if (segment.HasIcon()) {
            int iconX = contentX;
            int iconY = contentY - style.iconSize / 2;
            ctx->DrawImage(segment.iconPath, iconX, iconY, style.iconSize, style.iconSize);
            contentX += style.iconSize + style.iconSpacing;
        }

        // Render text
        if (segment.HasText()) {
            ctx->SetTextPaint(textColor);
            ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);
            ctx->SetTextAlignment(segment.alignment);
            int textWidth = 0, textHeight = 0;
            ctx->GetTextDimension(segment.text, textWidth, textHeight);

            Rect2Df textRect(contentX, contentY - textHeight / 2, rect.width - ((contentX - rect.x) + style.paddingHorizontal), textHeight);
            ctx->DrawTextInRect(segment.text, textRect);
        }
    }

// ===== ANIMATION =====

    void UltraCanvasSegmentedControl::UpdateAnimation() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - animationStartTime
        ).count();

        float progress = elapsed / (style.animationDuration * 1000.0f);
        selectionAnimationProgress = std::min(1.0f, progress);

        if (selectionAnimationProgress >= 1.0f) {
            animationFromIndex = -1;
            animationToIndex = -1;
        }

        RequestRedraw();
    }

// ===== EVENT HANDLING =====

    bool UltraCanvasSegmentedControl::OnEvent(const UCEvent& event) {
        if (!IsVisible() || !IsActive()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);

            case UCEventType::MouseUp:
                return HandleMouseUp(event);

            case UCEventType::MouseMove:
                return HandleMouseMove(event);

            case UCEventType::MouseLeave:
                hoveredIndex = -1;
                RequestRedraw();
                return false;

            case UCEventType::KeyDown:
                return HandleKeyDown(event);

            default:
                return false;
        }
    }

    bool UltraCanvasSegmentedControl::HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        int index = GetSegmentAtPosition(event.x, event.y);
        if (index >= 0 && segments[index].enabled) {
            pressedIndex = index;
            RequestRedraw();
            return true;
        }

        return false;
    }

    bool UltraCanvasSegmentedControl::HandleMouseUp(const UCEvent& event) {
        if (pressedIndex < 0) return false;

        bool wasPressed = pressedIndex >= 0;
        int clickedIndex = pressedIndex;
        pressedIndex = -1;

        if (wasPressed && Contains(event.x, event.y)) {
            int index = GetSegmentAtPosition(event.x, event.y);
            if (index == clickedIndex && segments[index].enabled) {
                // Handle click
                if (allowNoSelection && selectedIndex == index) {
                    SetSelectedIndex(-1);
                } else {
                    SetSelectedIndex(index);
                }

                if (onSegmentClick) {
                    onSegmentClick(index);
                }

                RequestRedraw();
                return true;
            }
        }

        RequestRedraw();
        return false;
    }

    bool UltraCanvasSegmentedControl::HandleMouseMove(const UCEvent& event) {
        if (!Contains(event.x, event.y)) {
            if (hoveredIndex != -1) {
                hoveredIndex = -1;
                RequestRedraw();
            }
            return false;
        }

        int index = GetSegmentAtPosition(event.x, event.y);
        if (index != hoveredIndex) {
            hoveredIndex = index;

            if (hoveredIndex >= 0 && onSegmentHover) {
                onSegmentHover(hoveredIndex);
            }

            RequestRedraw();
        }

        return hoveredIndex >= 0;
    }

    bool UltraCanvasSegmentedControl::HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return false;

        int newIndex = selectedIndex;

        switch (event.virtualKey) {
            case UCKeys::LeftArrow:
            case UCKeys::UpArrow:
                // Move to previous enabled segment
                if (newIndex > 0) {
                    newIndex--;
                    while (newIndex >= 0 && !segments[newIndex].enabled) {
                        newIndex--;
                    }
                    if (newIndex >= 0) {
                        SetSelectedIndex(newIndex);
                        return true;
                    }
                }
                break;

            case UCKeys::RightArrow:
            case UCKeys::DownArrow:
                // Move to next enabled segment
                if (newIndex < static_cast<int>(segments.size()) - 1) {
                    newIndex++;
                    while (newIndex < static_cast<int>(segments.size()) && !segments[newIndex].enabled) {
                        newIndex++;
                    }
                    if (newIndex < static_cast<int>(segments.size())) {
                        SetSelectedIndex(newIndex);
                        return true;
                    }
                }
                break;

            case UCKeys::Home:
                // Select first enabled segment
                for (int i = 0; i < static_cast<int>(segments.size()); i++) {
                    if (segments[i].enabled) {
                        SetSelectedIndex(i);
                        return true;
                    }
                }
                break;

            case UCKeys::End:
                // Select last enabled segment
                for (int i = static_cast<int>(segments.size()) - 1; i >= 0; i--) {
                    if (segments[i].enabled) {
                        SetSelectedIndex(i);
                        return true;
                    }
                }
                break;
        }

        return false;
    }

    int UltraCanvasSegmentedControl::GetSegmentAtPosition(int x, int y) const {
        for (size_t i = 0; i < segmentRects.size(); i++) {
            if (segmentRects[i].Contains(x, y)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int UltraCanvasSegmentedControl::AddSegment(const std::string &text, TextAlignment alignment) {
        segments.push_back(SegmentData(text, alignment));
        layoutDirty = true;

        // Select first segment by default
        if (segments.size() == 1 && allowNoSelection == false) {
            SetSelectedIndex(0);
        }

        return static_cast<int>(segments.size() - 1);
    }

    int UltraCanvasSegmentedControl::AddSegment(const std::string &text, const std::string &iconPath, TextAlignment alignment) {
        segments.push_back(SegmentData(text, iconPath, alignment));
        layoutDirty = true;

        if (segments.size() == 1 && allowNoSelection == false) {
            SetSelectedIndex(0);
        }

        return static_cast<int>(segments.size() - 1);
    }

    int UltraCanvasSegmentedControl::InsertSegment(int index, const std::string &text, TextAlignment alignment) {
        if (index < 0 || index > static_cast<int>(segments.size())) return -1;

        segments.insert(segments.begin() + index, SegmentData(text, alignment));
        layoutDirty = true;

        // Adjust selected index if needed
        if (selectedIndex >= index) {
            selectedIndex++;
        }

        return index;
    }

    void UltraCanvasSegmentedControl::RemoveSegment(int index) {
        if (index < 0 || index >= static_cast<int>(segments.size())) return;

        segments.erase(segments.begin() + index);
        layoutDirty = true;

        // Adjust selected index
        if (selectedIndex == index) {
            selectedIndex = -1;
            if (!allowNoSelection && !segments.empty()) {
                SetSelectedIndex(std::min(index, static_cast<int>(segments.size()) - 1));
            }
        } else if (selectedIndex > index) {
            selectedIndex--;
        }
    }

    void UltraCanvasSegmentedControl::ClearSegments() {
        segments.clear();
        selectedIndex = -1;
        hoveredIndex = -1;
        pressedIndex = -1;
        layoutDirty = true;
    }

    void UltraCanvasSegmentedControl::SetSegmentText(int index, const std::string &text) {
        if (index >= 0 && index < static_cast<int>(segments.size())) {
            segments[index].text = text;
            layoutDirty = true;
        }
    }
    std::string UltraCanvasSegmentedControl::GetSegmentText(int index) const {
        if (index >= 0 && index < static_cast<int>(segments.size())) {
            return segments[index].text;
        }
        return "";
    }

    void UltraCanvasSegmentedControl::SetSegmentIcon(int index, const std::string &iconPath) {
        if (index >= 0 && index < static_cast<int>(segments.size())) {
            segments[index].iconPath = iconPath;
            layoutDirty = true;
        }
    }

    void UltraCanvasSegmentedControl::SetSegmentEnabled(int index, bool enabled) {
        if (index >= 0 && index < static_cast<int>(segments.size())) {
            segments[index].enabled = enabled;

            // Deselect if disabling selected segment
            if (!enabled && selectedIndex == index) {
                selectedIndex = -1;
                if (!allowNoSelection) {
                    // Find next enabled segment
                    for (int i = 0; i < static_cast<int>(segments.size()); i++) {
                        if (segments[i].enabled) {
                            SetSelectedIndex(i);
                            break;
                        }
                    }
                }
            }
        }
    }

    bool UltraCanvasSegmentedControl::IsSegmentEnabled(int index) const {
        if (index >= 0 && index < static_cast<int>(segments.size())) {
            return segments[index].enabled;
        }
        return false;
    }

    void UltraCanvasSegmentedControl::SetSegmentWidth(int index, float width) {
        if (index >= 0 && index < static_cast<int>(segments.size())) {
            segments[index].customWidth = width;
            layoutDirty = true;
        }
    }

    void UltraCanvasSegmentedControl::SetSelectedIndex(int index) {
        if (index < -1 || index >= static_cast<int>(segments.size())) return;

        // Check if no selection is allowed
        if (index == -1 && !allowNoSelection && !segments.empty()) return;

        // Check if segment is enabled
        if (index >= 0 && !segments[index].enabled) return;

        if (selectedIndex != index) {
            int oldIndex = selectedIndex;

            // Start animation if enabled
            if (style.enableAnimation && oldIndex >= 0 && index >= 0) {
                animationFromIndex = oldIndex;
                animationToIndex = index;
                selectionAnimationProgress = 0.0f;
                animationStartTime = std::chrono::steady_clock::now();
            }

            selectedIndex = index;

            if (onSegmentSelected) {
                onSegmentSelected(index);
            }

            RequestRedraw();
        }
    }

    int UltraCanvasSegmentedControl::GetSelectedIndex() const {
        return selectedIndex;
    }

    std::string UltraCanvasSegmentedControl::GetSelectedText() const {
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(segments.size())) {
            return segments[selectedIndex].text;
        }
        return "";
    }

    void UltraCanvasSegmentedControl::SetAllowNoSelection(bool allow) {
        allowNoSelection = allow;

        // If no selection not allowed and nothing selected, select first enabled segment
        if (!allow && selectedIndex == -1 && !segments.empty()) {
            for (int i = 0; i < static_cast<int>(segments.size()); i++) {
                if (segments[i].enabled) {
                    SetSelectedIndex(i);
                    break;
                }
            }
        }
    }

    void UltraCanvasSegmentedControl::SetWidthMode(SegmentWidthMode mode) {
        widthMode = mode;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasSegmentedControl::SetStyle(SegmentedControlStyle st) {
        style = st;
        layoutDirty = true;
        RequestRedraw();
    }
} // namespace UltraCanvas
