// include/UltraCanvasLayoutItem.h
// Base layout item class with layout-specific derived classes
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_LAYOUT_ITEM_H
#define ULTRACANVAS_LAYOUT_ITEM_H

#include "UltraCanvasCommonTypes.h"
#include <memory>
#include <string>

namespace UltraCanvas {

enum class LayoutItemAlignment {
    Start = 0,        // Left/Top
    Center = 1,       // Center
    End = 2,          // Right/Bottom
    Fill = 3,         // Fill available space
    Auto = 4         // Auto (Flex)
};

// Forward declarations
class UltraCanvasUIElement;

// ===== SIZE MODE ENUMS =====
// Note: SizeMode is already defined in UltraCanvasCommonTypes.h
// enum class SizeMode { Fixed, Auto, Fill, Percent }

// ===== BASE LAYOUT ITEM CLASS =====
class UltraCanvasLayoutItem {
protected:
    // Element reference
    std::shared_ptr<UltraCanvasUIElement> element;
    
    // Computed position and size (set by layout manager)
    float computedX = 0;
    float computedY = 0;
    float computedWidth = 0;
    float computedHeight = 0;
    
    // Visibility and state
    bool visible = true;
    bool enabled = true;
    
    // Margins (space outside the element)
    int marginLeft = 0;
    int marginRight = 0;
    int marginTop = 0;
    int marginBottom = 0;
    
public:
    UltraCanvasLayoutItem() = default;
    explicit UltraCanvasLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem);

    // ===== ELEMENT ACCESS =====
    std::shared_ptr<UltraCanvasUIElement> GetElement() const { return element; }
    void SetElement(std::shared_ptr<UltraCanvasUIElement> elem) { element = elem; }
    
    // ===== COMPUTED GEOMETRY =====
    float GetComputedX() const { return computedX; }
    float GetComputedY() const { return computedY; }
    float GetComputedWidth() const { return computedWidth; }
    float GetComputedHeight() const { return computedHeight; }

    void SetComputedGeometry(float x, float y, float width, float height) {
        computedX = x;
        computedY = y;
        computedWidth = width;
        computedHeight = height;
    }

    Rect2Df GetComputedBounds() const {
        return Rect2Df(computedX, computedY, computedWidth, computedHeight);
    }
    
    // ===== VISIBILITY & STATE =====
    bool IsVisible() const { return visible; }
    void SetVisible(bool vis) { visible = vis; }
    
    bool IsEnabled() const { return enabled; }
    void SetEnabled(bool en) { enabled = en; }
    
    // ===== MARGINS =====
    void SetMargin(int margin) {
        marginLeft = marginRight = marginTop = marginBottom = margin;
    }
    
    void SetMargin(int horizontal, int vertical) {
        marginLeft = marginRight = horizontal;
        marginTop = marginBottom = vertical;
    }
    
    void SetMargin(int left, int top, int right, int bottom) {
        marginLeft = left;
        marginTop = top;
        marginRight = right;
        marginBottom = bottom;
    }
    
    int GetMarginLeft() const { return marginLeft; }
    int GetMarginRight() const { return marginRight; }
    int GetMarginTop() const { return marginTop; }
    int GetMarginBottom() const { return marginBottom; }
    
    int GetTotalMarginHorizontal() const { return marginLeft + marginRight; }
    int GetTotalMarginVertical() const { return marginTop + marginBottom; }
    
    // ===== GEOMETRY WITH MARGINS =====
    Rect2Df GetBoundsWithMargin() const {
        return Rect2Df(
            computedX - marginLeft,
            computedY - marginTop,
            computedWidth + marginLeft + marginRight,
            computedHeight + marginTop + marginBottom
        );
    }
    
    // ===== VIRTUAL INTERFACE =====
    // Derived classes can override to provide layout-specific behavior
    virtual SizeMode GetWidthMode() const { return SizeMode::Auto; }
    virtual SizeMode GetHeightMode() const { return SizeMode::Auto; }
    virtual float GetPreferredWidth() const;
    virtual float GetPreferredHeight() const;
    virtual float GetMinimumWidth() const { return 0; }
    virtual float GetMinimumHeight() const { return 0; }
    virtual float GetMaximumWidth() const { return 10000; }
    virtual float GetMaximumHeight() const { return 10000; }
    
    // Apply computed geometry to the actual element
    virtual void ApplyToElement();
};

// ===== BOX LAYOUT ITEM =====
class UltraCanvasBoxLayoutItem : public UltraCanvasLayoutItem {
private:
    // Size constraints
    SizeMode widthMode = SizeMode::Auto;
    SizeMode heightMode = SizeMode::Auto;
    float fixedWidth = 0;
    float fixedHeight = 0;
    
    // Size limits
    float minWidth = 0;
    float minHeight = 0;
    float maxWidth = 10000;
    float maxHeight = 10000;
    
    // Flex properties (for flexible sizing)
    float stretch = 0;  // How much to stretch relative to other items (0 = no stretch)
    
    // Alignment within allocated space
    LayoutItemAlignment alignment = LayoutItemAlignment::Start;
    
public:
    UltraCanvasBoxLayoutItem() = default;
    explicit UltraCanvasBoxLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem);
    
    // ===== SIZE MODE =====
    SizeMode GetWidthMode() const override { return widthMode; }
    SizeMode GetHeightMode() const override { return heightMode; }
    
    void SetWidthMode(SizeMode mode) { widthMode = mode; }
    void SetHeightMode(SizeMode mode) { heightMode = mode; }
    void SetSizeMode(SizeMode width, SizeMode height) {
        widthMode = width;
        heightMode = height;
    }
    
    // ===== FIXED SIZES =====
    void SetFixedWidth(float width) {
        widthMode = SizeMode::Fixed;
        fixedWidth = width;
    }
    
    void SetFixedHeight(float height) {
        heightMode = SizeMode::Fixed;
        fixedHeight = height;
    }
    
    void SetFixedSize(float width, float height) {
        SetFixedWidth(width);
        SetFixedHeight(height);
    }
    
    float GetFixedWidth() const { return fixedWidth; }
    float GetFixedHeight() const { return fixedHeight; }
    
    // ===== SIZE LIMITS =====
    void SetMinimumWidth(float width) { minWidth = width; }
    void SetMinimumHeight(float height) { minHeight = height; }
    void SetMinimumSize(float width, float height) {
        minWidth = width;
        minHeight = height;
    }
    
    void SetMaximumWidth(float width) { maxWidth = width; }
    void SetMaximumHeight(float height) { maxHeight = height; }
    void SetMaximumSize(float width, float height) {
        maxWidth = width;
        maxHeight = height;
    }
    
    float GetMinimumWidth() const override { return minWidth; }
    float GetMinimumHeight() const override { return minHeight; }
    float GetMaximumWidth() const override { return maxWidth; }
    float GetMaximumHeight() const override { return maxHeight; }
    
    // ===== STRETCH (FLEX GROW) =====
    void SetStretch(float stretchFactor) { stretch = stretchFactor; }
    float GetStretch() const { return stretch; }
    
    // ===== ALIGNMENT =====
    void SetAlignment(LayoutItemAlignment align) { alignment = align; }
    LayoutItemAlignment GetAlignment() const { return alignment; }
    
    // ===== PREFERRED SIZE =====
    float GetPreferredWidth() const override;
    float GetPreferredHeight() const override;
};

// ===== GRID LAYOUT ITEM =====
class UltraCanvasGridLayoutItem : public UltraCanvasLayoutItem {
private:
    // Grid position
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    
    // Size constraints
    SizeMode widthMode = SizeMode::Fill;  // Grid items default to Fill
    SizeMode heightMode = SizeMode::Fill;
    float fixedWidth = 0;
    float fixedHeight = 0;
    
    // Size limits
    float minWidth = 0;
    float minHeight = 0;
    float maxWidth = 10000;
    float maxHeight = 10000;
    
    // Alignment within cell
    LayoutItemAlignment horizontalAlignment = LayoutItemAlignment::Fill;
    LayoutItemAlignment verticalAlignment = LayoutItemAlignment::Fill;
    
public:
    UltraCanvasGridLayoutItem() = default;
    explicit UltraCanvasGridLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem);

    // ===== GRID POSITION =====
    void SetRow(int r) { row = r; }
    void SetColumn(int c) { column = c; }
    void SetPosition(int r, int c) {
        row = r;
        column = c;
    }
    
    int GetRow() const { return row; }
    int GetColumn() const { return column; }
    
    // ===== SPANNING =====
    void SetRowSpan(int span) { rowSpan = span; }
    void SetColumnSpan(int span) { columnSpan = span; }
    void SetSpan(int rowSpan, int colSpan) {
        this->rowSpan = rowSpan;
        this->columnSpan = colSpan;
    }
    
    int GetRowSpan() const { return rowSpan; }
    int GetColumnSpan() const { return columnSpan; }
    
    // ===== SIZE MODE =====
    SizeMode GetWidthMode() const override { return widthMode; }
    SizeMode GetHeightMode() const override { return heightMode; }
    
    void SetWidthMode(SizeMode mode) { widthMode = mode; }
    void SetHeightMode(SizeMode mode) { heightMode = mode; }
    void SetSizeMode(SizeMode width, SizeMode height) {
        widthMode = width;
        heightMode = height;
    }
    
    // ===== FIXED SIZES =====
    void SetFixedWidth(float width) {
        widthMode = SizeMode::Fixed;
        fixedWidth = width;
    }
    
    void SetFixedHeight(float height) {
        heightMode = SizeMode::Fixed;
        fixedHeight = height;
    }
    
    void SetFixedSize(float width, float height) {
        SetFixedWidth(width);
        SetFixedHeight(height);
    }
    
    float GetFixedWidth() const { return fixedWidth; }
    float GetFixedHeight() const { return fixedHeight; }
    
    // ===== SIZE LIMITS =====
    void SetMinimumWidth(float width) { minWidth = width; }
    void SetMinimumHeight(float height) { minHeight = height; }
    void SetMinimumSize(float width, float height) {
        minWidth = width;
        minHeight = height;
    }
    
    void SetMaximumWidth(float width) { maxWidth = width; }
    void SetMaximumHeight(float height) { maxHeight = height; }
    void SetMaximumSize(float width, float height) {
        maxWidth = width;
        maxHeight = height;
    }
    
    float GetMinimumWidth() const override { return minWidth; }
    float GetMinimumHeight() const override { return minHeight; }
    float GetMaximumWidth() const override { return maxWidth; }
    float GetMaximumHeight() const override { return maxHeight; }
    
    // ===== ALIGNMENT =====
    void SetHorizontalAlignment(LayoutItemAlignment align) { horizontalAlignment = align; }
    void SetVerticalAlignment(LayoutItemAlignment align) { verticalAlignment = align; }
    void SetAlignment(LayoutItemAlignment horizontal, LayoutItemAlignment vertical) {
        horizontalAlignment = horizontal;
        verticalAlignment = vertical;
    }
    
    LayoutItemAlignment GetHorizontalAlignment() const { return horizontalAlignment; }
    LayoutItemAlignment GetVerticalAlignment() const { return verticalAlignment; }
    
    // ===== PREFERRED SIZE =====
    float GetPreferredWidth() const override;
    float GetPreferredHeight() const override;
};

// ===== FLEX LAYOUT ITEM =====
class UltraCanvasFlexLayoutItem : public UltraCanvasLayoutItem {
private:
    // Flex properties
    float flexGrow = 0;      // How much to grow (0 = don't grow)
    float flexShrink = 1;    // How much to shrink (1 = normal shrinking)
    float flexBasis = 0;     // Base size before growing/shrinking (0 = auto)
    
    // Size constraints
    SizeMode widthMode = SizeMode::Auto;
    SizeMode heightMode = SizeMode::Auto;
    float fixedWidth = 0;
    float fixedHeight = 0;
    
    // Size limits
    float minWidth = 0;
    float minHeight = 0;
    float maxWidth = 10000;
    float maxHeight = 10000;
    
    // Alignment
    LayoutItemAlignment alignSelf = LayoutItemAlignment::Auto;  // Override container alignment for this item
    
public:
    UltraCanvasFlexLayoutItem() = default;
    explicit UltraCanvasFlexLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem);
    
    // ===== FLEX PROPERTIES =====
    void SetFlexGrow(float grow) { flexGrow = grow; }
    void SetFlexShrink(float shrink) { flexShrink = shrink; }
    void SetFlexBasis(float basis) { flexBasis = basis; }
    void SetFlex(float grow, float shrink, float basis) {
        flexGrow = grow;
        flexShrink = shrink;
        flexBasis = basis;
    }
    
    float GetFlexGrow() const { return flexGrow; }
    float GetFlexShrink() const { return flexShrink; }
    float GetFlexBasis() const { return flexBasis; }
    
    // ===== SIZE MODE =====
    SizeMode GetWidthMode() const override { return widthMode; }
    SizeMode GetHeightMode() const override { return heightMode; }
    
    void SetWidthMode(SizeMode mode) { widthMode = mode; }
    void SetHeightMode(SizeMode mode) { heightMode = mode; }
    void SetSizeMode(SizeMode width, SizeMode height) {
        widthMode = width;
        heightMode = height;
    }
    
    // ===== FIXED SIZES =====
    void SetFixedWidth(float width) {
        widthMode = SizeMode::Fixed;
        fixedWidth = width;
    }
    
    void SetFixedHeight(float height) {
        heightMode = SizeMode::Fixed;
        fixedHeight = height;
    }
    
    void SetFixedSize(float width, float height) {
        SetFixedWidth(width);
        SetFixedHeight(height);
    }
    
    float GetFixedWidth() const { return fixedWidth; }
    float GetFixedHeight() const { return fixedHeight; }
    
    // ===== SIZE LIMITS =====
    void SetMinimumWidth(float width) { minWidth = width; }
    void SetMinimumHeight(float height) { minHeight = height; }
    void SetMinimumSize(float width, float height) {
        minWidth = width;
        minHeight = height;
    }
    
    void SetMaximumWidth(float width) { maxWidth = width; }
    void SetMaximumHeight(float height) { maxHeight = height; }
    void SetMaximumSize(float width, float height) {
        maxWidth = width;
        maxHeight = height;
    }
    
    float GetMinimumWidth() const override { return minWidth; }
    float GetMinimumHeight() const override { return minHeight; }
    float GetMaximumWidth() const override { return maxWidth; }
    float GetMaximumHeight() const override { return maxHeight; }
    
    // ===== ALIGNMENT =====
    void SetAlignSelf(LayoutItemAlignment align) { alignSelf = align; }
    LayoutItemAlignment GetAlignSelf() const { return alignSelf; }
    
    // ===== PREFERRED SIZE =====
    float GetPreferredWidth() const override;
    float GetPreferredHeight() const override;
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_LAYOUT_ITEM_H
