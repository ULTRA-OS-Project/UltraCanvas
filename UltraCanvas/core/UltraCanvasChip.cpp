// core/UltraCanvasChip.cpp
// Platform-independent chip and tag-input implementation.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasChip.h"
#include "UltraCanvasApplication.h"
#include <cctype>

namespace UltraCanvas {

// ===================================================================
// UltraCanvasChip
// ===================================================================

    UltraCanvasChip::UltraCanvasChip(const std::string& identifier,
                                     float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
        if (h <= 0) SetHeight(style.height);
    }

    void UltraCanvasChip::SetIcon(const std::string& path) {
        iconPath = path;
        iconImage = path.empty() ? nullptr : UCImage::Get(path);
        RequestRedraw();
    }

    void UltraCanvasChip::SetSelected(bool s) {
        if (selected == s) return;
        selected = s;
        if (onSelectedChanged) onSelectedChanged(selected);
        RequestRedraw();
    }

    float UltraCanvasChip::GetPreferredWidth(IRenderContext* ctx) const {
        ctx->SetFontStyle(style.fontStyle);
        float w = style.paddingH;
        if (iconImage) w += style.iconSize + style.spacing;
        w += ctx->GetTextDimension(label).x;
        if (closable) w += style.spacing + style.closeSize;
        w += style.paddingH;
        return w;
    }

    Rect2Df UltraCanvasChip::CloseRect() const {
        if (!closable) return Rect2Df(0, 0, 0, 0);
        Rect2Df b = GetLocalBounds();
        float cs = style.closeSize;
        return Rect2Df(b.width - style.paddingH - cs, (b.height - cs) / 2.0f, cs, cs);
    }

    void UltraCanvasChip::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (autoWidth) {
            float pw = GetPreferredWidth(ctx);
            if (std::abs(pw - GetWidth()) > 0.5f) SetWidth(pw);
        }
        Rect2Df b = GetLocalBounds();
        bool disabled = IsDisabled();
        bool sel = selectable && selected;

        Color fill, txt = style.textColor;
        if (disabled)          { fill = style.disabledFillColor; txt = style.disabledTextColor; }
        else if (sel)          { fill = style.selectedFillColor; txt = style.selectedTextColor; }
        else if (IsPressed())  { fill = style.pressedFillColor; }
        else if (IsHovered())  { fill = style.hoverFillColor; }
        else                   { fill = style.fillColor; }

        float radius = std::min(style.cornerRadius, b.height / 2.0f);

        if (variant == ChipVariant::Filled) {
            ctx->DrawFilledRectangle(b, fill, sel ? 0.0f : style.borderWidth,
                                     style.borderColor, radius);
        } else { // Outlined
            Color bord = sel ? style.selectedFillColor
                       : (disabled ? style.disabledFillColor : style.borderColor);
            Color obg  = sel ? style.selectedFillColor
                       : (IsHovered() && !disabled ? style.hoverFillColor : Colors::Transparent);
            ctx->DrawFilledRectangle(b, obg, style.borderWidth, bord, radius);
            txt = sel ? style.selectedTextColor
                : (disabled ? style.disabledTextColor : style.textColor);
        }

        float x = b.x + style.paddingH;
        float cy = b.y + b.height / 2.0f;

        if (iconImage) {
            ctx->DrawImage(*iconImage,
                           Rect2Dd(x, cy - style.iconSize / 2.0f, style.iconSize, style.iconSize),
                           ImageFitMode::Contain);
            x += style.iconSize + style.spacing;
        }

        ctx->SetFontStyle(style.fontStyle);
        ctx->SetTextPaint(txt);
        Point2Di ts = ctx->GetTextDimension(label);
        ctx->DrawText(label, Point2Di(static_cast<int>(x), static_cast<int>(cy - ts.y / 2.0f)));

        if (closable) {
            Rect2Df cr = CloseRect();
            Point2Dd cc(cr.x + cr.width / 2.0f, cr.y + cr.height / 2.0f);
            if (closeHovered && !disabled) {
                ctx->SetFillPaint(style.closeHoverBg);
                ctx->FillCircle(cc, cr.width / 2.0f);
            }
            Color xc = disabled ? style.disabledTextColor
                     : (closeHovered ? style.closeHoverColor
                        : (variant == ChipVariant::Outlined ? txt : style.closeColor));
            float p = cr.width * 0.28f;
            ctx->SetStrokePaint(xc);
            ctx->SetStrokeWidth(1.6f);
            ctx->DrawLine(Point2Dd(cr.x + p, cr.y + p),
                          Point2Dd(cr.x + cr.width - p, cr.y + cr.height - p));
            ctx->DrawLine(Point2Dd(cr.x + cr.width - p, cr.y + p),
                          Point2Dd(cr.x + p, cr.y + cr.height - p));
        }
    }

    bool UltraCanvasChip::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));

        switch (event.type) {
            case UCEventType::MouseDown:
                if (closable && CloseRect().Contains(p)) {
                    if (onClose) onClose();
                    return true;
                }
                SetPressed(true);
                return true;
            case UCEventType::MouseUp: {
                bool was = IsPressed();
                SetPressed(false);
                if (was && GetLocalBounds().Contains(p) &&
                    !(closable && CloseRect().Contains(p))) {
                    if (selectable) SetSelected(!selected);
                    if (onClick) onClick();
                }
                return true;
            }
            case UCEventType::MouseMove: {
                bool ch = closable && CloseRect().Contains(p);
                if (ch != closeHovered) { closeHovered = ch; RequestRedraw(); }
                mouseCursor = (ch || onClick || selectable) ? UCMouseCursor::Hand
                                                            : UCMouseCursor::Default;
                return true;
            }
            case UCEventType::MouseEnter:
                SetHovered(true);
                return true;
            case UCEventType::MouseLeave:
                SetHovered(false);
                SetPressed(false);
                closeHovered = false;
                RequestRedraw();
                return true;
            case UCEventType::KeyDown:
                if (selectable && IsFocused() &&
                    (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    SetSelected(!selected);
                    return true;
                }
                break;
            default:
                break;
        }
        return false;
    }

// ===================================================================
// UltraCanvasTagInput
// ===================================================================

    UltraCanvasTagInput::UltraCanvasTagInput(const std::string& identifier,
                                             float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Text;
        if (h <= 0) SetHeight(style.chipStyle.height + 2 * style.padding);
    }

    std::string UltraCanvasTagInput::Trim(const std::string& s) const {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    bool UltraCanvasTagInput::HasTag(const std::string& tag) const {
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    }

    void UltraCanvasTagInput::FireChanged() {
        if (onTagsChanged) onTagsChanged(tags);
    }

    bool UltraCanvasTagInput::AddTag(const std::string& tag, bool runCallback) {
        std::string t = Trim(tag);
        if (t.empty()) return false;
        if (!allowDuplicates && HasTag(t)) return false;
        if (maxTags > 0 && GetTagCount() >= maxTags) return false;
        if (validator && !validator(t)) return false;
        tags.push_back(t);
        if (runCallback) {
            if (onTagAdded) onTagAdded(t);
            FireChanged();
        }
        RequestRedraw();
        return true;
    }

    void UltraCanvasTagInput::RemoveTag(int index, bool runCallback) {
        if (index < 0 || index >= GetTagCount()) return;
        std::string removed = tags[index];
        tags.erase(tags.begin() + index);
        if (runCallback) {
            if (onTagRemoved) onTagRemoved(removed);
            FireChanged();
        }
        RequestRedraw();
    }

    bool UltraCanvasTagInput::RemoveTag(const std::string& tag, bool runCallback) {
        auto it = std::find(tags.begin(), tags.end(), tag);
        if (it == tags.end()) return false;
        RemoveTag(static_cast<int>(it - tags.begin()), runCallback);
        return true;
    }

    void UltraCanvasTagInput::ClearTags(bool runCallback) {
        if (tags.empty()) return;
        tags.clear();
        if (runCallback) FireChanged();
        RequestRedraw();
    }

    void UltraCanvasTagInput::SetTags(const std::vector<std::string>& newTags) {
        tags = newTags;
        RequestRedraw();
    }

    bool UltraCanvasTagInput::CommitBuffer() {
        std::string t = Trim(editBuffer);
        if (t.empty()) { editBuffer.clear(); RequestRedraw(); return false; }
        if (AddTag(t, true)) {
            editBuffer.clear();
            RequestRedraw();
            return true;
        }
        return false;   // rejected (duplicate / invalid / full) — keep the text
    }

    void UltraCanvasTagInput::ComputeLayout(IRenderContext* ctx) const {
        chipBoxes.clear();
        Rect2Df b = GetLocalBounds();
        const ChipStyle& cs = style.chipStyle;
        float pad = style.padding;
        float innerL = b.x + pad;
        float innerR = b.x + b.width - pad;
        float rowH = cs.height;
        float x = innerL;
        float y = b.y + pad;

        ctx->SetFontStyle(cs.fontStyle);
        for (int i = 0; i < GetTagCount(); ++i) {
            float tw = ctx->GetTextDimension(tags[i]).x;
            float cw = cs.paddingH + tw + cs.spacing + cs.closeSize + cs.paddingH;
            if (x > innerL && x + cw > innerR) {   // wrap
                x = innerL;
                y += rowH + style.rowSpacing;
            }
            Rect2Df rect(x, y, cw, rowH);
            Rect2Df closeRect(x + cw - cs.paddingH - cs.closeSize,
                              y + (rowH - cs.closeSize) / 2.0f, cs.closeSize, cs.closeSize);
            chipBoxes.push_back({i, rect, closeRect});
            x += cw + style.chipSpacing;
        }

        float remaining = innerR - x;
        if (remaining < style.minInputWidth && x > innerL) {
            x = innerL;
            y += rowH + style.rowSpacing;
            remaining = innerR - x;
        }
        inputRect = Rect2Df(x, y, std::max(remaining, style.minInputWidth), rowH);
        contentHeight = (y + rowH + pad) - b.y;
    }

    void UltraCanvasTagInput::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        ComputeLayout(ctx);
        Rect2Df b = GetLocalBounds();

        Color border = IsFocused() ? style.focusBorderColor : style.borderColor;
        ctx->DrawFilledRectangle(b, style.backgroundColor, style.borderWidth,
                                 border, style.cornerRadius);

        const ChipStyle& cs = style.chipStyle;
        for (const auto& box : chipBoxes) {
            float radius = std::min(cs.cornerRadius, box.rect.height / 2.0f);
            ctx->DrawFilledRectangle(box.rect, cs.fillColor, cs.borderWidth, cs.borderColor, radius);

            ctx->SetFontStyle(cs.fontStyle);
            ctx->SetTextPaint(cs.textColor);
            Point2Di ts = ctx->GetTextDimension(tags[box.index]);
            ctx->DrawText(tags[box.index],
                          Point2Di(static_cast<int>(box.rect.x + cs.paddingH),
                                   static_cast<int>(box.rect.y + box.rect.height / 2.0f - ts.y / 2.0f)));

            Rect2Df cr = box.closeRect;
            bool hovered = (hoveredChip == box.index);
            Point2Dd cc(cr.x + cr.width / 2.0f, cr.y + cr.height / 2.0f);
            if (hovered) {
                ctx->SetFillPaint(cs.closeHoverBg);
                ctx->FillCircle(cc, cr.width / 2.0f);
            }
            Color xc = hovered ? cs.closeHoverColor : cs.closeColor;
            float p = cr.width * 0.28f;
            ctx->SetStrokePaint(xc);
            ctx->SetStrokeWidth(1.6f);
            ctx->DrawLine(Point2Dd(cr.x + p, cr.y + p),
                          Point2Dd(cr.x + cr.width - p, cr.y + cr.height - p));
            ctx->DrawLine(Point2Dd(cr.x + cr.width - p, cr.y + p),
                          Point2Dd(cr.x + p, cr.y + cr.height - p));
        }

        // Inline editor.
        ctx->SetFontStyle(style.fontStyle);
        float tyc = inputRect.y + inputRect.height / 2.0f;
        bool showPlaceholder = editBuffer.empty() && tags.empty() && !IsFocused();
        if (showPlaceholder) {
            ctx->SetTextPaint(style.placeholderColor);
            Point2Di ps = ctx->GetTextDimension(placeholder);
            ctx->DrawText(placeholder, Point2Di(static_cast<int>(inputRect.x),
                                                static_cast<int>(tyc - ps.y / 2.0f)));
        } else if (!editBuffer.empty()) {
            ctx->SetTextPaint(style.textColor);
            Point2Di es = ctx->GetTextDimension(editBuffer);
            ctx->DrawText(editBuffer, Point2Di(static_cast<int>(inputRect.x),
                                               static_cast<int>(tyc - es.y / 2.0f)));
        }
        if (IsFocused()) {
            int caretX = static_cast<int>(inputRect.x) +
                         (editBuffer.empty() ? 0 : ctx->GetTextDimension(editBuffer).x) + 1;
            ctx->SetStrokePaint(style.caretColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Dd(caretX, inputRect.y + 4),
                          Point2Dd(caretX, inputRect.y + inputRect.height - 4));
        }

        // Grow to fit wrapped rows.
        if (autoHeight && std::abs(contentHeight - b.height) > 0.5f) {
            SetHeight(contentHeight);
        }
    }

    bool UltraCanvasTagInput::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        switch (event.type) {
            case UCEventType::MouseDown:  return HandleMouseDown(event);
            case UCEventType::MouseMove:  return HandleMouseMove(event);
            case UCEventType::MouseLeave:
                if (hoveredChip != -1) { hoveredChip = -1; RequestRedraw(); }
                return true;
            case UCEventType::KeyDown:    return HandleKeyDown(event);
            case UCEventType::KeyChar:    return HandleKeyChar(event);
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasTagInput::HandleMouseDown(const UCEvent& event) {
        Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
        if (!GetLocalBounds().Contains(p)) return false;
        SetFocus(true);
        for (const auto& box : chipBoxes) {
            if (box.closeRect.Contains(p)) {
                RemoveTag(box.index, true);
                return true;
            }
        }
        RequestRedraw();
        return true;
    }

    bool UltraCanvasTagInput::HandleMouseMove(const UCEvent& event) {
        Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
        int nh = -1;
        for (const auto& box : chipBoxes) {
            if (box.closeRect.Contains(p)) { nh = box.index; break; }
        }
        mouseCursor = (nh >= 0) ? UCMouseCursor::Hand : UCMouseCursor::Text;
        if (nh != hoveredChip) { hoveredChip = nh; RequestRedraw(); }
        return true;
    }

    bool UltraCanvasTagInput::HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return false;
        switch (event.virtualKey) {
            case UCKeys::Return:
            case UCKeys::NumPadEnter:
                CommitBuffer();
                return true;
            case UCKeys::Backspace:
                if (!editBuffer.empty()) {
                    editBuffer.pop_back();
                    RequestRedraw();
                    return true;
                }
                if (!tags.empty()) {
                    RemoveTag(GetTagCount() - 1, true);
                    return true;
                }
                return false;
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasTagInput::HandleKeyChar(const UCEvent& event) {
        if (!IsFocused()) return false;
        char c = event.character;
        if (c == '\n' || c == '\r' || c == ',') {   // commit separators
            CommitBuffer();
            return true;
        }
        if (static_cast<unsigned char>(c) >= 32 && c != 127) {
            editBuffer.push_back(c);
            RequestRedraw();
            return true;
        }
        return false;
    }

} // namespace UltraCanvas
