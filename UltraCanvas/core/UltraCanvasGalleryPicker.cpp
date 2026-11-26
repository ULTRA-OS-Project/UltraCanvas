// core/UltraCanvasGalleryPicker.cpp
// Pick one of many choices shown as pictures - see the header.
// ui-reuse-exempt: UltraCanvasGalleryPicker is the element; its tiles draw live elements, not controls.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasGalleryPicker.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTheme.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

UltraCanvasGalleryPicker::UltraCanvasGalleryPicker(const std::string& identifier, float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

void UltraCanvasGalleryPicker::SetItems(const std::vector<GalleryPickerItem>& list) {
    items = list;
    // A picture without a size of its own takes the tile's.
    for (GalleryPickerItem& item : items) {
        if (item.picture && (item.picture->GetWidth() <= 0 || item.picture->GetHeight() <= 0))
            item.picture->SetBounds(Rect2Df(0.0f, 0.0f, static_cast<float>(tileWidth - 12), static_cast<float>(pictureHeight)));
    }
    if (selected >= static_cast<int>(items.size())) selected = -1;
    hovered = -1;
    placedForWidth = -1;
    RequestRedraw();
}

void UltraCanvasGalleryPicker::SetSelectedIndex(int index) {
    if (index < -1 || index >= static_cast<int>(items.size())) index = -1;
    if (index == selected) return;
    selected = index;
    RequestRedraw();
}

void UltraCanvasGalleryPicker::SetTileSize(int width, int height, int pictureH) {
    tileWidth = std::max(48, width);
    tileHeight = std::max(48, height);
    pictureHeight = std::clamp(pictureH, 16, tileHeight - 16);
    placedForWidth = -1;
    SetItems(items);
}

int UltraCanvasGalleryPicker::ColumnsFor(int width) const {
    return std::max(1, (width - 2 * padding + gap) / (tileWidth + gap));
}

void UltraCanvasGalleryPicker::Place(int width) const {
    if (placedForWidth == width && placed.size() == items.size()) return;
    placedForWidth = width;
    placed.assign(items.size(), Placed());
    headings.clear();
    const int columns = ColumnsFor(width);
    int y = padding, column = 0, row = -1;
    std::string section;
    for (size_t i = 0; i < items.size(); ++i) {
        const bool newSection = i == 0 || items[i].section != section;
        if (newSection) {
            if (i > 0) y += tileHeight + gap;              // past the last row of the section before
            section = items[i].section;
            if (!section.empty()) {
                headings.emplace_back(section, y);
                y += headingHeight;
            }
            column = 0;
            ++row;
        } else if (column == columns) {
            column = 0;
            ++row;
            y += tileHeight + gap;
        }
        placed[i].box = Rect2Di(padding + column * (tileWidth + gap), y, tileWidth, tileHeight);
        placed[i].row = row;
        placed[i].column = column;
        ++column;
    }
}

int UltraCanvasGalleryPicker::ContentHeightFor(int width) const {
    Place(width);
    if (placed.empty()) return 2 * padding;
    return placed.back().box.y + tileHeight + padding;
}

Rect2Di UltraCanvasGalleryPicker::TileBox(int index) const {
    Place(static_cast<int>(GetWidth()));
    if (index < 0 || index >= static_cast<int>(placed.size())) return Rect2Di(0, 0, 0, 0);
    return placed[static_cast<size_t>(index)].box;
}

int UltraCanvasGalleryPicker::SectionTop(const std::string& section) const {
    Place(static_cast<int>(GetWidth()));
    for (const auto& [name, top] : headings) {
        if (name == section) return top;
    }
    return -1;
}

int UltraCanvasGalleryPicker::TileAt(int x, int y) const {
    Place(static_cast<int>(GetWidth()));
    for (size_t i = 0; i < placed.size(); ++i) {
        const Rect2Di& box = placed[i].box;
        if (x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height) return static_cast<int>(i);
    }
    return -1;
}

void UltraCanvasGalleryPicker::ScrollIntoView(int index) {
    UltraCanvasContainer* view = GetParentContainer();
    const Rect2Di box = TileBox(index);
    if (!view || box.width == 0) return;
    const int top = static_cast<int>(GetY()) + box.y - padding;
    const int bottom = static_cast<int>(GetY()) + box.y + box.height + padding;
    const int at = view->GetVerticalScrollPosition();
    const int seen = static_cast<int>(view->GetHeight());
    if (top < at) view->ScrollToVertical(std::max(0, top));
    else if (bottom > at + seen) view->ScrollToVertical(std::max(0, bottom - seen));
}

void UltraCanvasGalleryPicker::ScrollToSection(const std::string& section) {
    UltraCanvasContainer* view = GetParentContainer();
    const int top = SectionTop(section);
    if (view && top >= 0) view->ScrollToVertical(std::max(0, static_cast<int>(GetY()) + top - padding));
}

void UltraCanvasGalleryPicker::Pick(int index, bool tell) {
    if (index < 0 || index >= static_cast<int>(items.size())) return;
    const bool changed = index != selected;
    selected = index;
    ScrollIntoView(index);
    RequestRedraw();
    if (changed && tell && onSelectionChanged) onSelectionChanged(index);
}

int UltraCanvasGalleryPicker::Neighbour(int index, int dx, int dy) const {
    Place(static_cast<int>(GetWidth()));
    if (placed.empty()) return -1;
    if (index < 0) return 0;
    if (dx != 0) return std::clamp(index + dx, 0, static_cast<int>(placed.size()) - 1);
    // Up or down: the nearest tile of the row above or below, by column.
    const int row = placed[static_cast<size_t>(index)].row + dy;
    const int column = placed[static_cast<size_t>(index)].column;
    int best = -1;
    for (size_t i = 0; i < placed.size(); ++i) {
        if (placed[i].row != row) continue;
        if (best < 0 || std::abs(placed[i].column - column) < std::abs(placed[static_cast<size_t>(best)].column - column))
            best = static_cast<int>(i);
    }
    return best < 0 ? index : best;
}

void UltraCanvasGalleryPicker::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    (void)dirtyRect;
    if (!ctx) return;
    const UltraCanvasTheme& theme = UltraCanvasTheme::Current();
    const ThemeRoles& roles = theme.roles;
    Place(static_cast<int>(GetWidth()));
    ctx->PushState();
    // The sections' headings.
    FontStyle heading = theme.Font(ThemeTextRole::Body);
    heading.fontWeight = FontWeight::Bold;
    for (const auto& [name, top] : headings) {
        ctx->SetFontStyle(heading);
        ctx->SetTextPaint(roles.ink);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->DrawTextInRect(name, Rect2Dd(padding, top, GetWidth() - 2 * padding, headingHeight - 4));
        ctx->SetStrokePaint(roles.line);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(padding, top + headingHeight - 5.5), Point2Dd(GetWidth() - padding, top + headingHeight - 5.5));
    }
    const FontStyle caption = theme.Font(ThemeTextRole::Caption);
    for (size_t i = 0; i < items.size(); ++i) {
        const Rect2Di& box = placed[i].box;
        const bool isPicked = static_cast<int>(i) == selected;
        const bool isHovered = static_cast<int>(i) == hovered;
        const Rect2Dd frame(box.x + 0.5, box.y + 0.5, box.width - 1.0, box.height - 1.0);
        ctx->SetFillPaint(isPicked ? roles.accentSoft : isHovered ? roles.hover : roles.panel);
        ctx->FillRoundedRectangle(frame, 6.0);
        ctx->SetStrokePaint(isPicked ? roles.accent : roles.line);
        ctx->SetStrokeWidth(isPicked ? 2.0f : 1.0f);
        ctx->DrawRoundedRectangle(frame, 6.0);
        // The picture, on white as a chart on a page.
        const Rect2Dd picture(box.x + 6, box.y + 6, box.width - 12, pictureHeight);
        ctx->SetFillPaint(Colors::White);
        ctx->FillRectangle(picture);
        if (items[i].picture) {
            ctx->PushState();
            ctx->ClipRect(picture);
            // A picture laid out larger than the tile is drawn scaled down,
            // centred.
            const double width = std::max(1.0f, items[i].picture->GetWidth());
            const double height = std::max(1.0f, items[i].picture->GetHeight());
            const double scale = std::min(1.0, std::min(picture.width / width, picture.height / height));
            ctx->Translate(picture.x + (picture.width - width * scale) / 2.0, picture.y + (picture.height - height * scale) / 2.0);
            if (scale < 1.0) ctx->Scale(scale, scale);
            items[i].picture->Render(ctx, Rect2Df(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)));
            ctx->PopState();
        }
        if (items[i].faded) {
            ctx->SetFillPaint(Color(255, 255, 255, 110));
            ctx->FillRectangle(picture);
        }
        ctx->SetFontStyle(caption);
        ctx->SetTextPaint(roles.ink);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->DrawTextInRect(items[i].title, Rect2Dd(box.x + 4, box.y + 8 + pictureHeight, box.width - 8,
                                                    box.height - pictureHeight - 10));
    }
    // The keyboard focus: a ring round the picked tile.
    if (IsFocused() && selected >= 0 && selected < static_cast<int>(placed.size())) {
        const Rect2Di& box = placed[static_cast<size_t>(selected)].box;
        ctx->SetStrokePaint(roles.focus);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(box.x - 2.5, box.y - 2.5, box.width + 5.0, box.height + 5.0), 8.0);
    }
    ctx->PopState();
}

bool UltraCanvasGalleryPicker::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown: {
            const int hit = TileAt(event.pointer.x, event.pointer.y);
            SetFocus(true);
            if (hit >= 0) Pick(hit, true);
            return true;
        }
        case UCEventType::MouseDoubleClick: {
            const int hit = TileAt(event.pointer.x, event.pointer.y);
            if (hit < 0) return false;
            Pick(hit, true);
            if (onItemActivated) onItemActivated(hit);
            return true;
        }
        case UCEventType::MouseMove: {
            const int hit = TileAt(event.pointer.x, event.pointer.y);
            if (hit != hovered) {
                hovered = hit;
                RequestRedraw();
            }
            return false;
        }
        case UCEventType::MouseLeave:
            if (hovered >= 0) {
                hovered = -1;
                RequestRedraw();
            }
            return false;
        case UCEventType::KeyDown: {
            const int columns = ColumnsFor(static_cast<int>(GetWidth()));
            int next = -1;
            switch (event.virtualKey) {
                case UCKeys::Left: next = Neighbour(selected, -1, 0); break;
                case UCKeys::Right: next = Neighbour(selected, 1, 0); break;
                case UCKeys::Up: next = Neighbour(selected, 0, -1); break;
                case UCKeys::Down: next = Neighbour(selected, 0, 1); break;
                case UCKeys::Home: next = items.empty() ? -1 : 0; break;
                case UCKeys::End: next = static_cast<int>(items.size()) - 1; break;
                case UCKeys::PageUp: next = std::max(0, selected - columns * 2); break;
                case UCKeys::PageDown: next = std::min(static_cast<int>(items.size()) - 1, selected + columns * 2); break;
                case UCKeys::Return:
                    if (selected >= 0 && onItemActivated) onItemActivated(selected);
                    return selected >= 0;
                default:
                    return false;
            }
            if (next >= 0) Pick(next, true);
            return true;
        }
        default:
            return UltraCanvasUIElement::OnEvent(event);
    }
}

} // namespace UltraCanvas
