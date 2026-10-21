// core/UltraCanvasColorSwatchBar.cpp
// Platform-independent colour swatch strip implementation.
// Version: 1.2.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "UltraCanvasColorSwatchBar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

UltraCanvasColorSwatchBar::UltraCanvasColorSwatchBar(const std::string& identifier,
                                                     float x, float y,
                                                     float w, float h)
        : UltraCanvasUIElement(identifier, x, y, w, h) {
    mouseCursor = UCMouseCursor::Default;
}

// ===================================================================
// THE COLOURS
// ===================================================================

void UltraCanvasColorSwatchBar::SetColors(const std::vector<Color>& colors) {
    // Keep the selection on the same colour when it survives the change; a
    // palette swap that silently moved the selection to another colour would
    // look like the bar picked one by itself.
    const bool hadSelection = selectedIndex_ >= 0;
    const Color previous = GetSelectedColor();
    colors_ = colors;
    selectedIndex_ = -1;
    hoverSlot_ = -1;
    if (hadSelection) SelectColor(previous, false);
    RequestRedraw();
}

void UltraCanvasColorSwatchBar::SetColorAt(int index, const Color& c) {
    if (index < 0 || index >= static_cast<int>(colors_.size())) return;
    colors_[static_cast<size_t>(index)] = c;
    RequestRedraw();
}

void UltraCanvasColorSwatchBar::SetWrap(bool wrap) {
    if (wrap == wrap_) return;
    wrap_ = wrap;
    hoverSlot_ = -1;
    RequestRedraw();
}

void UltraCanvasColorSwatchBar::SetShowCheckeredEntry(bool show) {
    if (show == showCheckered_) return;
    showCheckered_ = show;
    if (!show) checkeredSelected_ = false;
    hoverSlot_ = -1;
    RequestRedraw();
}

std::vector<Color> UltraCanvasColorSwatchBar::GrayscalePalette(int steps) {
    steps = std::max(2, steps);
    std::vector<Color> out;
    out.reserve(static_cast<size_t>(steps));
    for (int i = 0; i < steps; ++i) {
        // White first, black last: a backdrop palette reads left-to-right from
        // the lightest to the darkest.
        const int v = 255 - (255 * i) / (steps - 1);
        const auto c = static_cast<uint8_t>(v);
        out.emplace_back(c, c, c, 255);
    }
    return out;
}

std::vector<Color> UltraCanvasColorSwatchBar::ColorPalette() {
    return {
        Color(229,  57,  53, 255),   // red
        Color(244, 124,  32, 255),   // orange
        Color(253, 216,  53, 255),   // yellow
        Color(124, 179,  66, 255),   // light green
        Color( 34, 139,  87, 255),   // green
        Color(  0, 150, 136, 255),   // teal
        Color( 30, 156, 214, 255),   // light blue
        Color( 25,  73, 160, 255),   // blue
        Color(103,  58, 183, 255),   // violet
        Color(171,  71, 188, 255),   // magenta
        Color(236, 118, 160, 255),   // pink
        Color(121,  85,  72, 255),   // brown
    };
}

std::vector<Color> UltraCanvasColorSwatchBar::DefaultPalette() {
    std::vector<Color> out = GrayscalePalette(6);
    const std::vector<Color> colors = ColorPalette();
    out.insert(out.end(), colors.begin(), colors.end());
    return out;
}

// ===================================================================
// SELECTION
// ===================================================================

Color UltraCanvasColorSwatchBar::GetSelectedColor() const {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(colors_.size())) {
        return Color(0, 0, 0, 255);
    }
    return colors_[static_cast<size_t>(selectedIndex_)];
}

void UltraCanvasColorSwatchBar::SelectColor(const Color& c, bool runCallback) {
    int found = -1;
    for (size_t i = 0; i < colors_.size(); ++i) {
        // Alpha is ignored: a palette entry stands for a colour, and the host
        // may keep its own opacity for it.
        if (colors_[i].r == c.r && colors_[i].g == c.g && colors_[i].b == c.b) {
            found = static_cast<int>(i);
            break;
        }
    }
    selectedIndex_ = found;
    checkeredSelected_ = false;
    RequestRedraw();
    if (runCallback && found >= 0 && onColorSelected) {
        onColorSelected(colors_[static_cast<size_t>(found)]);
    }
}

void UltraCanvasColorSwatchBar::SelectCheckered(bool runCallback) {
    selectedIndex_ = -1;
    checkeredSelected_ = true;
    RequestRedraw();
    if (runCallback && onCheckeredSelected) onCheckeredSelected();
}

void UltraCanvasColorSwatchBar::ClearSelection() {
    selectedIndex_ = -1;
    checkeredSelected_ = false;
    RequestRedraw();
}

// ===================================================================
// GEOMETRY
// ===================================================================

int UltraCanvasColorSwatchBar::SlotCount() const {
    return static_cast<int>(colors_.size()) + (showCheckered_ ? 1 : 0);
}

int UltraCanvasColorSwatchBar::ColorIndexOfSlot(int slot) const {
    const int index = showCheckered_ ? slot - 1 : slot;
    if (index < 0 || index >= static_cast<int>(colors_.size())) return -1;
    return index;
}

void UltraCanvasColorSwatchBar::SetSwatchSize(float size) {
    style_.swatchSize = std::max(style_.minSwatchSize, size);
    RequestRedraw();
}

float UltraCanvasColorSwatchBar::GetPreferredWidth() const {
    const int n = SlotCount();
    if (n <= 0) return 2.0f * style_.padding;
    return 2.0f * style_.padding + n * style_.swatchSize +
           (n - 1) * style_.spacing;
}

// The gap between swatches: the style's, until the row would not fit even at
// the minimum swatch size — the gaps are the first thing to give, because a
// colour pushed off the end is a colour nobody can click.
float UltraCanvasColorSwatchBar::SpacingForBounds() const {
    const int n = SlotCount();
    if (n <= 1) return style_.spacing;
    const float room = GetWidth() - 2.0f * style_.padding -
                       n * style_.minSwatchSize;
    if (room <= 0.0f) return 0.0f;
    return std::min(style_.spacing, room / static_cast<float>(n - 1));
}

// The edge length that fits `n` swatches into the element: as close to the
// preferred size as the width allows, never taller than the element itself,
// and floored at minSwatchSize — except in a strip too narrow even for that,
// where the swatches keep shrinking rather than the row losing colours off
// its end.
float UltraCanvasColorSwatchBar::SwatchSizeForBounds() const {
    const int n = SlotCount();
    if (n <= 0) return 0.0f;
    const float spacing = SpacingForBounds();
    const float availW = std::max(0.0f, GetWidth()  - 2.0f * style_.padding -
                                        (n - 1) * spacing);
    const float availH = std::max(0.0f, GetHeight() - 2.0f * style_.padding);
    const float perSwatch = availW / static_cast<float>(n);
    float size = std::min(style_.swatchSize, perSwatch);
    if (availH > 0.0f) size = std::min(size, availH);
    const float floorSize = std::min(style_.minSwatchSize, perSwatch);
    return std::max(1.0f, std::floor(std::max(size, floorSize)));
}

float UltraCanvasColorSwatchBar::GetEffectiveSwatchSize() const {
    return ComputeGeometry().size;
}

// A wrapping bar keeps its swatches at least minSwatchSize: as many lines as
// that takes, the colours shared out evenly over them (16 colours on two
// lines are 8 + 8, not 14 + 2), each as large as the line's width allows up
// to the preferred size.
void UltraCanvasColorSwatchBar::WrapMetrics(float width, int n, float& size,
                                            int& perLine, int& lines) const {
    size = 0.0f;
    perLine = 0;
    lines = 0;
    if (n <= 0) return;
    const float sp = style_.spacing;
    const float availW = std::max(1.0f, width - 2.0f * style_.padding);
    const float minSize = std::max(1.0f, style_.minSwatchSize);
    const int maxPerLine = std::max(1, static_cast<int>(std::floor((availW + sp) / (minSize + sp))));
    lines = (n + maxPerLine - 1) / maxPerLine;
    perLine = (n + lines - 1) / lines;
    const float fit = (availW - (perLine - 1) * sp) / static_cast<float>(perLine);
    size = std::max(1.0f, std::floor(std::min(style_.swatchSize, fit)));
}

float UltraCanvasColorSwatchBar::HeightForWidth(float width) const {
    const int n = SlotCount();
    if (n <= 0) return 2.0f * style_.padding;
    if (!wrap_) return 2.0f * style_.padding + style_.swatchSize;
    float size = 0.0f;
    int perLine = 0, lines = 0;
    WrapMetrics(width, n, size, perLine, lines);
    return 2.0f * style_.padding + lines * size + (lines - 1) * style_.spacing;
}

UltraCanvasColorSwatchBar::Geometry UltraCanvasColorSwatchBar::ComputeGeometry() const {
    Geometry g;
    const int n = SlotCount();
    if (n <= 0) return g;
    if (wrap_) {
        int lines = 0;
        WrapMetrics(GetWidth(), n, g.size, g.perLine, lines);
        g.spacing = style_.spacing;
        // From the top-left: a block of colours reads as a table, and the
        // host puts its own controls after the last line.
        g.originX = style_.padding;
        g.originY = style_.padding;
        return g;
    }
    g.size = SwatchSizeForBounds();
    g.spacing = SpacingForBounds();
    g.perLine = n;
    const float rowW = n * g.size + (n - 1) * g.spacing;
    // Centred in both directions: the leftover space sits around the row rather
    // than after it, so the strip stays under the middle of the image above it.
    g.originX = std::max(style_.padding, (GetWidth() - rowW) * 0.5f);
    g.originY = std::max(style_.padding, (GetHeight() - g.size) * 0.5f);
    return g;
}

Rect2Df UltraCanvasColorSwatchBar::SwatchRect(int slot, const Geometry& g) const {
    if (slot < 0 || slot >= SlotCount() || g.perLine <= 0) return Rect2Df(0, 0, 0, 0);
    const int col = slot % g.perLine;
    const int line = slot / g.perLine;
    return Rect2Df(g.originX + col * (g.size + g.spacing),
                   g.originY + line * (g.size + g.spacing), g.size, g.size);
}

Rect2Df UltraCanvasColorSwatchBar::SwatchRect(int slot) const {
    return SwatchRect(slot, ComputeGeometry());
}

int UltraCanvasColorSwatchBar::SlotAt(const Point2Di& local) const {
    const Geometry g = ComputeGeometry();
    if (g.perLine <= 0 || g.size <= 0.0f) return -1;
    // Straight from the grid rather than testing every swatch: a 256-colour
    // palette is hit-tested on every pointer move.
    const float step = g.size + g.spacing;
    const float fx = static_cast<float>(local.x) - g.originX;
    const float fy = static_cast<float>(local.y) - g.originY;
    if (fx < 0.0f || fy < 0.0f) return -1;
    const int col = static_cast<int>(fx / step);
    const int line = static_cast<int>(fy / step);
    if (col >= g.perLine) return -1;
    const int slot = line * g.perLine + col;
    if (slot >= SlotCount()) return -1;
    if (!SwatchRect(slot, g).Contains(static_cast<float>(local.x),
                                      static_cast<float>(local.y))) {
        return -1;
    }
    return slot;
}

std::string UltraCanvasColorSwatchBar::HexOf(const Color& c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return std::string(buf);
}

// ===================================================================
// RENDERING
// ===================================================================

void UltraCanvasColorSwatchBar::DrawCheckerboard(IRenderContext* ctx,
                                                 const Rect2Df& r) const {
    ctx->DrawFilledRectangle(Rect2Dd(r.x, r.y, r.width, r.height),
                             style_.checkerLight, 0.0f);
    const float cell = std::max(2.0f, style_.checkerCell);
    ctx->PushState();
    ctx->ClipRect(Rect2Dd(r.x, r.y, r.width, r.height));
    for (int row = 0; row * cell < r.height; ++row) {
        for (int col = (row % 2 == 0) ? 1 : 0; col * cell < r.width; col += 2) {
            const float x0 = r.x + col * cell;
            const float y0 = r.y + row * cell;
            const float w  = std::min(cell, r.x + r.width  - x0);
            const float h  = std::min(cell, r.y + r.height - y0);
            if (w > 0.0f && h > 0.0f) {
                ctx->DrawFilledRectangle(Rect2Dd(x0, y0, w, h),
                                         style_.checkerDark, 0.0f);
            }
        }
    }
    ctx->PopState();
}

void UltraCanvasColorSwatchBar::Render(IRenderContext* ctx,
                                       const Rect2Df& /*dirtyRect*/) {
    if (!IsVisible()) return;
    // The render context is already translated to this element's top-left, so
    // everything below is drawn in element-local coordinates.
    const Rect2Df b = Rect2Df(0, 0, GetWidth(), GetHeight());

    ctx->PushState();
    ctx->ClipRect(Rect2Dd(b.x, b.y, b.width, b.height));
    if (style_.background.a > 0) {
        ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height),
                                 style_.background, 0.0f);
    }

    const int n = SlotCount();
    const Geometry g = ComputeGeometry();
    for (int slot = 0; slot < n; ++slot) {
        const Rect2Df r = SwatchRect(slot, g);
        if (r.width <= 0.0f || r.height <= 0.0f) continue;

        const int colorIndex = ColorIndexOfSlot(slot);
        const bool selected = IsCheckeredSlot(slot)
                              ? checkeredSelected_
                              : (colorIndex >= 0 && colorIndex == selectedIndex_);
        const bool hovered = (slot == hoverSlot_);

        if (IsCheckeredSlot(slot)) {
            DrawCheckerboard(ctx, r);
        } else if (colorIndex >= 0) {
            ctx->DrawFilledRectangle(Rect2Dd(r.x, r.y, r.width, r.height),
                                     colors_[static_cast<size_t>(colorIndex)],
                                     0.0f, Colors::Transparent,
                                     style_.cornerRadius);
        }

        // The border carries the state: the accent colour marks the chosen
        // swatch, a light outline the one under the pointer.
        const Color borderColor = selected ? style_.selectedBorder
                                 : hovered ? style_.hoverBorder
                                           : style_.border;
        const float borderWidth = selected ? style_.selectedBorderWidth
                                           : style_.borderWidth;
        if (borderWidth > 0.0f && borderColor.a > 0) {
            ctx->DrawFilledRectangle(Rect2Dd(r.x, r.y, r.width, r.height),
                                     Colors::Transparent, borderWidth,
                                     borderColor, style_.cornerRadius);
        }
    }
    ctx->PopState();
}

// ===================================================================
// EVENTS
// ===================================================================

bool UltraCanvasColorSwatchBar::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    switch (event.type) {
        case UCEventType::MouseMove: {
            const int slot = SlotAt(event.pointer);
            mouseCursor = (slot >= 0) ? UCMouseCursor::Hand : UCMouseCursor::Default;
            if (slot != hoverSlot_) {
                hoverSlot_ = slot;
                // The tooltip names what the pointer is over: the colour's hex
                // value, or what the checkerboard stands for.
                const int colorIndex = ColorIndexOfSlot(slot);
                if (IsCheckeredSlot(slot)) {
                    SetTooltip("Checkered (no background colour)");
                } else if (colorIndex >= 0) {
                    SetTooltip(HexOf(colors_[static_cast<size_t>(colorIndex)]));
                } else {
                    SetTooltip("");
                }
                RequestRedraw();
            }
            return slot >= 0;
        }

        case UCEventType::MouseLeave: {
            if (hoverSlot_ >= 0) {
                hoverSlot_ = -1;
                SetTooltip("");
                RequestRedraw();
            }
            return false;
        }

        // A rapid second click arrives as a double-click instead of a
        // MouseDown; clicking through the palette must react to every click.
        case UCEventType::MouseDown:
        case UCEventType::MouseDoubleClick: {
            const int slot = SlotAt(event.pointer);
            if (slot < 0) return false;
            if (event.button == UCMouseButton::Right) {
                const int colorIndex = ColorIndexOfSlot(slot);
                if (colorIndex >= 0 && onColorContextClicked) {
                    onColorContextClicked(colorIndex, event.pointerWindow);
                    return true;
                }
                if (colorIndex < 0 || !onColorAdjustSelected) return false;
                onColorAdjustSelected(colors_[static_cast<size_t>(colorIndex)]);
                return true;
            }
            if (event.button != UCMouseButton::Left) return false;
            if (IsCheckeredSlot(slot)) {
                SelectCheckered(true);
            } else {
                const int colorIndex = ColorIndexOfSlot(slot);
                if (colorIndex < 0) return false;
                selectedIndex_ = colorIndex;
                checkeredSelected_ = false;
                RequestRedraw();
                if (onColorSelected) {
                    onColorSelected(colors_[static_cast<size_t>(colorIndex)]);
                }
            }
            return true;
        }

        default:
            break;
    }
    return false;
}

} // namespace UltraCanvas
