// core/UltraCanvasColorPaletteManager.cpp
// The palette rows under a colour picker, the palette manager (the gallery),
// the palette-entry editor and the palette creator.
// Version: 1.1.0 - popups with a title bar to drag them by; the palette
//                  creator and the user's own palettes; the hovered swatch
//                  floods the rows; fuller presets
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasColorPaletteManager.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasTextUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace UltraCanvas {

namespace {
    // A popup: its 1 px border, kFramePadding round the title bar and the
    // body, and the body's own padding - kPopupPadding from the edge in all.
    constexpr float kPopupPadding = 12.0f;
    constexpr float kFramePadding = 4.0f;
    constexpr float kBodyPadding = kPopupPadding - kFramePadding - 1.0f;
    constexpr float kTitleBarHeight = 26.0f;
    // Everything of a popup's height that is not the body's content.
    constexpr float kPopupChrome = 2.0f * (1.0f + kFramePadding) + kTitleBarHeight +
                                   kFramePadding + kBodyPadding;
    constexpr float kPopupGap = 8.0f;
    constexpr float kGalleryWidth = 320.0f;
    constexpr float kGalleryMaxHeight = 520.0f;
    constexpr float kCardPadding = 8.0f;
    constexpr float kCardRadius = 6.0f;
    constexpr float kCardHeaderHeight = 22.0f;
    constexpr float kSubtitleHeight = 16.0f;
    constexpr float kSectionHeight = 16.0f;
    constexpr float kFooterHeight = 26.0f;
    constexpr float kFieldHeight = 24.0f;
    constexpr float kListScrollbarRoom = 14.0f;
    constexpr float kEditorWidth = 236.0f;
    constexpr float kCreatorWidth = 260.0f;
    // A palette is a GIF palette at most.
    constexpr size_t kMaxCreatorColours = 256;

    Color Hex(uint32_t rgb) {
        return Color(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                     static_cast<uint8_t>((rgb >> 8) & 0xFF),
                     static_cast<uint8_t>(rgb & 0xFF), 255);
    }

    std::vector<Color> HexList(std::initializer_list<uint32_t> values) {
        std::vector<Color> out;
        out.reserve(values.size());
        for (uint32_t v : values) out.push_back(Hex(v));
        return out;
    }

    int HexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // "RRGGBB" (a leading '#' allowed); false for anything else.
    bool ParseHexColor(const std::string& text, Color& out) {
        std::string s = text;
        if (!s.empty() && s[0] == '#') s.erase(0, 1);
        if (s.size() != 6) return false;
        int v[6];
        for (int i = 0; i < 6; ++i) {
            v[i] = HexDigit(s[static_cast<size_t>(i)]);
            if (v[i] < 0) return false;
        }
        out = Color(static_cast<uint8_t>(v[0] * 16 + v[1]),
                    static_cast<uint8_t>(v[2] * 16 + v[3]),
                    static_cast<uint8_t>(v[4] * 16 + v[5]), 255);
        return true;
    }

    std::string HexOf(const Color& c) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02X%02X%02X", c.r, c.g, c.b);
        return std::string(buf);
    }

    // The colours shared out over the hue circle, for the rainbow palette.
    std::vector<Color> HueSteps(int steps, float s, float v) {
        std::vector<Color> out;
        for (int i = 0; i < steps; ++i) {
            out.push_back(HSV(360.0f * static_cast<float>(i) / static_cast<float>(steps), s, v, 255));
        }
        return out;
    }

    std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, float w, float h,
                                                const std::string& text, float size,
                                                FontWeight weight, const Color& color,
                                                TextAlignment align = TextAlignment::Left) {
        auto label = std::make_shared<UltraCanvasLabel>(id, w, h, text);
        label->SetFontSize(size);
        label->SetFontWeight(weight);
        label->SetTextColor(color);
        label->SetAlignment(align, VerticalAlignment::Middle);
        label->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return label;
    }

    // A flat button with a coloured face: the gallery's add buttons (accent)
    // and the popups' footer buttons (plain).
    void StyleFlatButton(UltraCanvasButton& b, const Color& face, const Color& hover,
                         const Color& border, const Color& text, float fontSize,
                         FontWeight weight, float radius) {
        ButtonStyle s = b.GetStyle();
        s.normalColor = face;
        s.hoverColor = hover;
        s.pressedColor = hover;
        s.normalTextColor = text;
        s.hoverTextColor = text;
        s.pressedTextColor = text;
        s.borderColor = border;
        s.borderWidth = border.a > 0 ? 1.0f : 0.0f;
        s.fontSize = fontSize;
        s.fontWeight = weight;
        s.cornerRadius = radius;
        s.textAlign = TextAlignment::Center;
        b.SetStyle(s);
        b.SetAcceptsFocus(false);
    }

    // A square button holding one glyph ("+", "-", a check mark): no padding
    // round the glyph and the size it was given, or the default padding
    // widens a 14 px button to the width of a word.
    void MakeGlyphButton(UltraCanvasButton& b) {
        b.SetPadding(0.0f);
        b.SetGrowToFitText(false);
    }

    // The top-left corner for a popup of `size` next to `anchor` (window
    // coordinates): below it, its right edge on the anchor's, or above it
    // when the window has no room below; inside the window either way.
    Point2Di PopupPosition(const UltraCanvasWindowBase* win, const Rect2Df& anchor,
                           const Size2Df& size) {
        float x = anchor.x + anchor.width - size.width;
        float y = anchor.y + anchor.height + 4.0f;
        if (win) {
            const float winW = static_cast<float>(win->GetWidth());
            const float winH = static_cast<float>(win->GetHeight());
            if (y + size.height > winH && anchor.y - size.height - 4.0f >= 0.0f) {
                y = anchor.y - size.height - 4.0f;
            }
            x = std::clamp(x, 0.0f, std::max(0.0f, winW - size.width));
            y = std::clamp(y, 0.0f, std::max(0.0f, winH - size.height));
        }
        return Point2Di(static_cast<int>(x), static_cast<int>(y));
    }
}

// ===================================================================
// THE POPUPS
// ===================================================================

// The bar across the top of a popup: its title, and what the popup is
// dragged about the window by, as a window by its title bar.
class UltraCanvasColorPaletteTitleBar : public UltraCanvasContainer {
public:
    UltraCanvasColorPaletteTitleBar(const std::string& identifier, UltraCanvasUIElement* popup,
                                    float w, const std::string& title,
                                    const ColorPaletteManagerStyle& st)
        : UltraCanvasContainer(identifier, 0, 0, w, kTitleBarHeight), popup_(popup) {
        SetBackgroundColor(st.cardBackground);
        SetBorders(1.0f, st.cardBorder, std::max(0.0f, st.popupRadius - 2.0f));
        SetPadding(0.0f, 8.0f, 0.0f, 8.0f);
        layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
        layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        SetMouseCursor(UCMouseCursor::SizeAll);
        caption_ = MakeLabel(identifier + "-caption", w - 18.0f, kTitleBarHeight - 2.0f, title,
                             st.fontSize + 2.0f, FontWeight::Bold, st.titleText);
        caption_->SetMouseCursor(UCMouseCursor::SizeAll);
        AddChild(caption_);
    }

    bool OnEvent(const UCEvent& event) override {
        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button != UCMouseButton::Left || !popup_) break;
                dragging_ = true;
                grab_ = event.pointerWindow;
                start_ = popup_->GetPositionInWindow();
                // The release comes back here wherever the pointer is by then.
                if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
                return true;
            case UCEventType::MouseMove:
                if (!dragging_) break;
                MoveTo(start_.x + static_cast<float>(event.pointerWindow.x - grab_.x),
                       start_.y + static_cast<float>(event.pointerWindow.y - grab_.y));
                return true;
            case UCEventType::MouseUp:
                if (!dragging_) break;
                dragging_ = false;
                if (auto* app = UltraCanvasApplication::GetInstance()) {
                    if (app->GetCapturedElement() == this) app->ReleaseMouse();
                }
                return true;
            default:
                break;
        }
        return UltraCanvasContainer::OnEvent(event);
    }

private:
    // Puts the popup's top-left corner at (x, y), inside the window.
    void MoveTo(float x, float y) {
        UltraCanvasWindowBase* win = popup_->GetWindow();
        if (!win) return;
        const float w = popup_->GetWidth();
        const float h = popup_->GetHeight();
        x = std::clamp(x, 0.0f, std::max(0.0f, static_cast<float>(win->GetWidth()) - w));
        y = std::clamp(y, 0.0f, std::max(0.0f, static_cast<float>(win->GetHeight()) - h));
        const Point2Df old = popup_->GetPositionInWindow();
        if (std::fabs(old.x - x) < 0.5f && std::fabs(old.y - y) < 0.5f) return;
        // What the popup and its shadow uncover is the window's to paint again.
        const PopupShadow shadow = popup_->GetPopupShadow();
        win->AddDirtyRectangle(Rect2Di(static_cast<int>(old.x) - shadow.MarginLeft(),
                                       static_cast<int>(old.y) - shadow.MarginTop(),
                                       static_cast<int>(w) + shadow.MarginLeft() + shadow.MarginRight(),
                                       static_cast<int>(h) + shadow.MarginTop() + shadow.MarginBottom()));
        win->RequestWindowComposition();
        popup_->SetElementAbsolutePosition(Point2Df(std::floor(x), std::floor(y)));
    }

    UltraCanvasUIElement* popup_;   // the popup this bar is the top of
    std::shared_ptr<UltraCanvasLabel> caption_;
    bool dragging_ = false;
    Point2Di grab_;                 // where the drag began, in the window
    Point2Df start_;                // the popup's corner then
};

// A rounded white card with a soft shadow, as the tool palette's fold-out:
// a title bar to drag it by, and under it the body its content goes in.
class UltraCanvasColorPalettePopup : public UltraCanvasContainer {
public:
    UltraCanvasColorPalettePopup(const std::string& identifier,
                                 UltraCanvasColorPaletteManager* manager,
                                 const std::string& title, float width)
        : UltraCanvasContainer(identifier, 0, 0, width, 0), owner(manager), popupWidth(width) {
        const ColorPaletteManagerStyle& st = manager->GetManagerStyle();
        SetBackgroundColor(st.popupBackground);
        SetBorders(1.0f, st.popupBorder, st.popupRadius);
        SetPadding(kFramePadding);
        layout.SetFlexColumn().SetFlexGap(kFramePadding)
              .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        radius = st.popupRadius;

        const float frameInner = width - 2.0f * (1.0f + kFramePadding);
        titleBar = std::make_shared<UltraCanvasColorPaletteTitleBar>(identifier + "-titlebar", this,
                                                                     frameInner, title, st);
        AddChild(titleBar);
        body = std::make_shared<UltraCanvasContainer>(identifier + "-body", 0, 0, frameInner, 10);
        body->SetPadding(0.0f, kBodyPadding, kBodyPadding, kBodyPadding);
        body->layout.SetFlexColumn().SetFlexGap(kPopupGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        body->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        AddChild(body);
    }

    PopupShadow GetPopupShadow() const override {
        PopupShadow shadow;
        shadow.color = Color(0, 0, 0, 60);
        shadow.blur = 18;
        shadow.offset = Point2Di(0, 4);
        shadow.cornerRadius = radius;
        return shadow;
    }

    void Close() {
        if (!IsPopupElement()) return;
        if (auto* win = GetWindow()) win->ClosePopup(*this);
    }

    // How wide the body's content is.
    float ContentWidth() const { return popupWidth - 2.0f * kPopupPadding; }

    // Sizes the popup for body content `contentHeight` tall.
    void FitContentHeight(float contentHeight) {
        const float frameInner = popupWidth - 2.0f * (1.0f + kFramePadding);
        body->SetElementSize(Size2Df(frameInner, contentHeight + kBodyPadding));
        SetElementSize(Size2Df(popupWidth, contentHeight + kPopupChrome));
        InvalidateLayout();
    }

    // The manager this popup belongs to; cleared when the manager goes first.
    UltraCanvasColorPaletteManager* owner;
    float popupWidth;
    float radius = 8.0f;
    std::shared_ptr<UltraCanvasColorPaletteTitleBar> titleBar;
    std::shared_ptr<UltraCanvasContainer> body;
};

// ----- the palette manager: the ready-made palettes and the user's own -----
class UltraCanvasColorPaletteGallery : public UltraCanvasColorPalettePopup {
public:
    UltraCanvasColorPaletteGallery(const std::string& identifier,
                                   UltraCanvasColorPaletteManager* manager)
        : UltraCanvasColorPalettePopup(identifier, manager, "Palette manager", kGalleryWidth) {}

    void Build(float height) {
        const ColorPaletteManagerStyle& st = owner->GetManagerStyle();
        const float inner = ContentWidth();
        const float contentH = height - kPopupChrome;
        FitContentHeight(contentH);

        body->AddChild(MakeLabel(GetIdentifier() + "-sub", inner, kSubtitleHeight,
                                 "+ adds a scheme as a new row of swatches",
                                 st.fontSize, FontWeight::Normal, st.mutedText));

        const float listH = std::max(80.0f, contentH - kSubtitleHeight - kFooterHeight - 2.0f * kPopupGap);
        list = CreateScrollableContainer(GetIdentifier() + "-list", 0, 0, inner, listH);
        // Up and down only: the cards are laid out to the list's width.
        ContainerStyle listStyle = list->GetContainerStyle();
        listStyle.autoShowHorizontalScrollbar = false;
        listStyle.forceShowHorizontalScrollbar = false;
        list->SetContainerStyle(listStyle);
        list->layout.SetFlexColumn().SetFlexGap(6.0f)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        list->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        body->AddChild(list);
        RebuildCards();

        auto footer = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "-footer", 0, 0,
                                                             inner, kFooterHeight);
        footer->layout.SetFlexRow().SetFlexGap(6.0f)
                      .SetFlexJustifyContent(CSSLayout::JustifyContent::SpaceBetween)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        footer->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        auto create = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-addpalette", 0, 0, 96, 24,
                                                          "Add palette");
        StyleFlatButton(*create, st.accentColor, st.accentHoverColor, Colors::Transparent,
                        Color(255, 255, 255, 255), st.fontSize + 1.0f, FontWeight::Bold, 5.0f);
        create->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        create->SetTooltip("Make a palette of your own colours");
        create->onClick = [this]() {
            if (!owner) return;
            // Over this window, a little in from its top-left corner.
            const Point2Df at = GetPositionInWindow();
            owner->OpenCreatorAt(Point2Di(static_cast<int>(at.x) + 24,
                                          static_cast<int>(at.y) + static_cast<int>(kTitleBarHeight) + 16));
        };
        auto done = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-done", 0, 0, 72, 24, "Done");
        StyleFlatButton(*done, st.buttonColor, st.buttonHoverColor, st.buttonBorder,
                        st.buttonText, st.fontSize + 1.0f, FontWeight::Normal, 5.0f);
        done->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        done->onClick = [this]() { Close(); };
        footer->AddChild(create);
        footer->AddChild(done);
        body->AddChild(footer);
    }

    // The cards again: the user's own palettes first, then the presets. The
    // old ones are kept until the next time, as the delete button that
    // asked for this may still be in its click handler.
    void RebuildCards() {
        if (!owner || !list) return;
        retired.clear();
        for (const auto& child : list->GetChildren()) retired.push_back(child);
        list->ClearChildren();
        addButtons.clear();

        std::string category;
        int n = 0;
        auto addCards = [&](const std::vector<NamedColorPalette>& palettes, bool own) {
            for (const NamedColorPalette& palette : palettes) {
                const std::string id = GetIdentifier() + (own ? "-u" : "-p") + std::to_string(n++);
                if (palette.category != category) {
                    category = palette.category;
                    AddSection(id, category);
                }
                AddCard(id, palette, own);
            }
        };
        n = 0;
        addCards(owner->GetUserPalettes(), true);
        n = 0;
        addCards(UltraCanvasColorPaletteManager::PresetPalettes(), false);
        RefreshAddButtons();
        list->InvalidateLayout();
    }

    // Each palette's button: "+" while it can be added, a check mark once it
    // is one of the rows.
    void RefreshAddButtons() {
        if (!owner) return;
        const ColorPaletteManagerStyle& st = owner->GetManagerStyle();
        for (auto& [name, button] : addButtons) {
            const bool added = owner->HasPalette(name);
            button->SetText(added ? "✓" : "+");
            if (added) {
                StyleFlatButton(*button, Color(220, 252, 231, 255), Color(220, 252, 231, 255),
                                Colors::Transparent, Color(22, 101, 52, 255),
                                st.fontSize + 2.0f, FontWeight::Bold, 5.0f);
                button->SetTooltip(name + " is under the colour picker");
            } else {
                StyleFlatButton(*button, st.accentColor, st.accentHoverColor,
                                Colors::Transparent, Color(255, 255, 255, 255),
                                st.fontSize + 5.0f, FontWeight::Bold, 5.0f);
                button->SetTooltip("Add " + name + " under the colour picker");
            }
        }
    }

    void AddPalette(const std::string& name) {
        if (!owner || owner->HasPalette(name)) return;
        for (const NamedColorPalette& p : owner->GetUserPalettes()) {
            if (p.name == name) {
                owner->AddPalette(p);
                RefreshAddButtons();
                return;
            }
        }
        for (const NamedColorPalette& preset : UltraCanvasColorPaletteManager::PresetPalettes()) {
            if (preset.name == name) {
                owner->AddPalette(preset);
                break;
            }
        }
        RefreshAddButtons();
    }

    std::shared_ptr<UltraCanvasContainer> list;
    // The add buttons by their palette's name; they live in this popup.
    std::vector<std::pair<std::string, UltraCanvasButton*>> addButtons;

private:
    float CardWidth() const { return ContentWidth() - kListScrollbarRoom; }

    void AddSection(const std::string& id, const std::string& category) {
        const ColorPaletteManagerStyle& st = owner->GetManagerStyle();
        std::string caption = category;
        std::transform(caption.begin(), caption.end(), caption.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        list->AddChild(MakeLabel(id + "-section", CardWidth(), kSectionHeight, caption,
                                 st.fontSize - 1.0f, FontWeight::Bold, st.mutedText));
    }

    // One palette: its name, how many colours, its buttons and a preview.
    // The user's own also have a button that deletes them.
    void AddCard(const std::string& id, const NamedColorPalette& palette, bool own) {
        const ColorPaletteManagerStyle& st = owner->GetManagerStyle();
        const float cardW = CardWidth();
        const float cardInner = cardW - 2.0f * kCardPadding;

        auto bar = std::make_shared<UltraCanvasColorSwatchBar>(id + "-bar", cardInner, 10.0f);
        owner->StyleSwatchBar(*bar, palette.colors.size());
        bar->SetColors(palette.colors);
        const float barH = bar->HeightForWidth(cardInner);
        bar->SetElementSize(Size2Df(cardInner, barH));
        bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // A preview: a click adds the palette, as its button does.
        const std::string name = palette.name;
        bar->onColorSelected = [this, name](const Color&) { AddPalette(name); };

        const float cardH = 2.0f * kCardPadding + kCardHeaderHeight + 6.0f + barH;
        auto card = std::make_shared<UltraCanvasContainer>(id + "-card", 0, 0, cardW, cardH);
        card->SetBackgroundColor(st.cardBackground);
        card->SetBorders(1.0f, st.cardBorder, kCardRadius);
        card->SetPadding(kCardPadding);
        card->layout.SetFlexColumn().SetFlexGap(6.0f)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        card->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        auto header = std::make_shared<UltraCanvasContainer>(id + "-head", 0, 0,
                                                             cardInner, kCardHeaderHeight);
        header->layout.SetFlexRow().SetFlexGap(6.0f)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        header->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        const float countW = 74.0f;
        const float addW = kCardHeaderHeight;
        const float deleteW = own ? kCardHeaderHeight + 6.0f : 0.0f;
        header->AddChild(MakeLabel(id + "-name", cardInner - countW - addW - deleteW - 12.0f,
                                   kCardHeaderHeight, palette.name, st.fontSize + 1.0f,
                                   FontWeight::Bold, st.titleText));
        const size_t count = palette.colors.size();
        header->AddChild(MakeLabel(id + "-count", countW, kCardHeaderHeight,
                                   std::to_string(count) + (count == 1 ? " colour" : " colours"),
                                   st.fontSize, FontWeight::Normal, st.mutedText,
                                   TextAlignment::Right));
        if (own) {
            auto remove = std::make_shared<UltraCanvasButton>(id + "-delete", 0, 0, addW, addW, "×");
            MakeGlyphButton(*remove);
            StyleFlatButton(*remove, st.buttonColor, st.buttonHoverColor, st.buttonBorder,
                            st.buttonText, st.fontSize + 4.0f, FontWeight::Bold, 5.0f);
            remove->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            remove->SetTooltip("Delete " + name + " from my palettes");
            remove->onClick = [this, name]() {
                if (owner) owner->RemoveUserPalette(name);
            };
            header->AddChild(remove);
        }
        auto add = std::make_shared<UltraCanvasButton>(id + "-add", 0, 0, addW, addW, "+");
        MakeGlyphButton(*add);
        add->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        add->onClick = [this, name]() { AddPalette(name); };
        header->AddChild(add);
        addButtons.emplace_back(name, add.get());

        card->AddChild(header);
        card->AddChild(bar);
        list->AddChild(card);
    }

    // The cards of the last rebuild but one.
    std::vector<std::shared_ptr<UltraCanvasUIElement>> retired;
};

// ----- the entry editor: a foreground-only picker for one palette colour -----
class UltraCanvasColorPaletteEntryEditor : public UltraCanvasColorPalettePopup {
public:
    UltraCanvasColorPaletteEntryEditor(const std::string& identifier,
                                       UltraCanvasColorPaletteManager* manager,
                                       int paletteIndex, int entryIndex)
        : UltraCanvasColorPalettePopup(identifier, manager,
                                       "Colour " + std::to_string(entryIndex + 1) + " of " +
                                       manager->GetPalettes()[static_cast<size_t>(paletteIndex)].name,
                                       kEditorWidth),
          palette(paletteIndex), entry(entryIndex) {}

    void Build() {
        const ColorPaletteManagerStyle& st = owner->GetManagerStyle();
        const NamedColorPalette& p = owner->GetPalettes()[static_cast<size_t>(palette)];
        original = p.colors[static_cast<size_t>(entry)];
        const float inner = ContentWidth();

        picker = CreateColorPicker(GetIdentifier() + "-picker", original, 0, 0, inner, 300);
        picker->SetStyle(st.editorPickerStyle);
        picker->SetUIScale(st.editorPickerScale);
        picker->SetShowAlpha(false);
        // The one colour of the entry: no background swatch to mix it up with.
        picker->SetShowBackgroundColor(false);
        picker->SetSlidersCollapsible(true, false);
        picker->SetAutoHeight(true);
        picker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        picker->onColorChanging = [this](const Color& c) {
            if (owner) owner->SetEntryColor(palette, entry, c, false);
        };
        picker->onColorChanged = [this](const Color& c) {
            if (owner) owner->SetEntryColor(palette, entry, c, true);
        };
        // Opening the channels makes the picker taller: so is the popup.
        picker->onSlidersExpandedChanged = [this](bool) { FitSize(); };
        body->AddChild(picker);

        auto footer = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "-footer", 0, 0,
                                                             inner, kFooterHeight);
        footer->layout.SetFlexRow().SetFlexGap(6.0f)
                      .SetFlexJustifyContent(CSSLayout::JustifyContent::End)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        footer->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        auto revert = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-revert", 0, 0, 72, 24, "Revert");
        StyleFlatButton(*revert, st.buttonColor, st.buttonHoverColor, st.buttonBorder,
                        st.buttonText, st.fontSize + 1.0f, FontWeight::Normal, 5.0f);
        revert->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        revert->SetTooltip("Put the colour back as it was");
        revert->onClick = [this]() {
            if (owner) owner->SetEntryColor(palette, entry, original, true);
            Close();
        };
        auto done = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-done", 0, 0, 72, 24, "Done");
        StyleFlatButton(*done, st.accentColor, st.accentHoverColor, Colors::Transparent,
                        Color(255, 255, 255, 255), st.fontSize + 1.0f, FontWeight::Bold, 5.0f);
        done->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        done->onClick = [this]() { Close(); };
        footer->AddChild(revert);
        footer->AddChild(done);
        body->AddChild(footer);
        FitSize();
    }

    void FitSize() {
        FitContentHeight(picker->GetHeight() + kFooterHeight + kPopupGap);
    }

    int palette;
    int entry;
    Color original;
    std::shared_ptr<UltraCanvasColorPicker> picker;
};

// ----- the palette creator: a name and colours, made into a palette -----
class UltraCanvasColorPaletteCreator : public UltraCanvasColorPalettePopup {
public:
    UltraCanvasColorPaletteCreator(const std::string& identifier,
                                   UltraCanvasColorPaletteManager* manager)
        : UltraCanvasColorPalettePopup(identifier, manager, "New palette", kCreatorWidth) {}

    void Build() {
        const ColorPaletteManagerStyle& st = owner->GetManagerStyle();
        const float inner = ContentWidth();

        // ----- the name -----
        auto nameRow = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "-namerow", 0, 0,
                                                              inner, kFieldHeight);
        nameRow->layout.SetFlexRow().SetFlexGap(6.0f)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        nameRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        const float captionW = 48.0f;
        nameRow->AddChild(MakeLabel(GetIdentifier() + "-namecaption", captionW, kFieldHeight, "Name",
                                    st.fontSize + 1.0f, FontWeight::Normal, st.titleText));
        name = CreateTextInput(GetIdentifier() + "-name", 0, 0, inner - captionW - 6.0f, kFieldHeight);
        name->SetPlaceholder(owner->UniquePaletteName("My palette"));
        name->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        nameRow->AddChild(name);
        body->AddChild(nameRow);

        // ----- the colour to add, or the chosen swatch's -----
        picker = CreateColorPicker(GetIdentifier() + "-picker", Color(37, 99, 235, 255), 0, 0, inner, 300);
        picker->SetStyle(st.editorPickerStyle);
        picker->SetUIScale(st.editorPickerScale);
        picker->SetShowAlpha(false);
        picker->SetShowBackgroundColor(false);
        picker->SetSlidersCollapsible(true, false);
        picker->SetAutoHeight(true);
        picker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // With a swatch chosen, the picker sets that colour.
        picker->onColorChanging = [this](const Color& c) { SetChosen(c); };
        picker->onColorChanged = [this](const Color& c) { SetChosen(c); };
        picker->onSlidersExpandedChanged = [this](bool) { FitSize(); };
        body->AddChild(picker);

        auto buttons = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "-edit", 0, 0,
                                                              inner, kFieldHeight);
        buttons->layout.SetFlexRow().SetFlexGap(6.0f)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        buttons->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        auto add = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-addcolour", 0, 0, 96, 24,
                                                       "Add colour");
        StyleFlatButton(*add, st.buttonColor, st.buttonHoverColor, st.buttonBorder,
                        st.buttonText, st.fontSize + 1.0f, FontWeight::Bold, 5.0f);
        add->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        add->SetTooltip("Add the picker's colour to the palette");
        add->onClick = [this]() { AddColour(); };
        remove = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-removecolour", 0, 0, 72, 24,
                                                     "Remove");
        StyleFlatButton(*remove, st.buttonColor, st.buttonHoverColor, st.buttonBorder,
                        st.buttonText, st.fontSize + 1.0f, FontWeight::Normal, 5.0f);
        remove->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        remove->SetTooltip("Take the chosen colour out of the palette");
        remove->onClick = [this]() { RemoveChosen(); };
        buttons->AddChild(add);
        buttons->AddChild(remove);
        body->AddChild(buttons);

        // ----- the palette so far -----
        status = MakeLabel(GetIdentifier() + "-status", inner, kSubtitleHeight, "",
                           st.fontSize, FontWeight::Normal, st.mutedText);
        body->AddChild(status);
        swatches = std::make_shared<UltraCanvasColorSwatchBar>(GetIdentifier() + "-swatches", inner, 10.0f);
        swatches->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        swatches->onColorSelected = [this](const Color& c) {
            chosen = swatches->GetSelectedIndex();
            picker->SetColor(c, false);
            Refresh();
        };
        body->AddChild(swatches);

        auto footer = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "-footer", 0, 0,
                                                             inner, kFooterHeight);
        footer->layout.SetFlexRow().SetFlexGap(6.0f)
                      .SetFlexJustifyContent(CSSLayout::JustifyContent::End)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        footer->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        auto cancel = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-cancel", 0, 0, 72, 24, "Cancel");
        StyleFlatButton(*cancel, st.buttonColor, st.buttonHoverColor, st.buttonBorder,
                        st.buttonText, st.fontSize + 1.0f, FontWeight::Normal, 5.0f);
        cancel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        cancel->onClick = [this]() { Close(); };
        create = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-create", 0, 0, 72, 24, "Create");
        StyleFlatButton(*create, st.accentColor, st.accentHoverColor, Colors::Transparent,
                        Color(255, 255, 255, 255), st.fontSize + 1.0f, FontWeight::Bold, 5.0f);
        create->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        create->SetTooltip("Keep the palette under My palettes and add it under the colour picker");
        create->onClick = [this]() { Create(); };
        footer->AddChild(cancel);
        footer->AddChild(create);
        body->AddChild(footer);
        Refresh();
    }

    // Appends the picker's colour; nothing is chosen afterwards, so the
    // picker is free for the next one.
    void AddColour() {
        if (colours.size() >= kMaxCreatorColours) return;
        Color c = picker->GetColor();
        c.a = 255;
        colours.push_back(c);
        chosen = -1;
        Refresh();
    }

    void RemoveChosen() {
        if (chosen < 0 || chosen >= static_cast<int>(colours.size())) return;
        colours.erase(colours.begin() + chosen);
        chosen = -1;
        Refresh();
    }

    void Create() {
        if (!owner || colours.empty()) return;
        std::string wanted = Trim(name->GetText());
        if (wanted.empty()) wanted = "My palette";
        owner->AddUserPalette({wanted, UltraCanvasColorPaletteManager::UserPaletteCategory(), colours});
        Close();
    }

    void FitSize() {
        FitContentHeight(kFieldHeight + picker->GetHeight() + kFieldHeight + kSubtitleHeight +
                         swatches->GetHeight() + kFooterHeight + 5.0f * kPopupGap);
    }

    std::vector<Color> colours;
    int chosen = -1;   // the swatch the picker edits, -1: none

private:
    void SetChosen(const Color& c) {
        if (chosen < 0 || chosen >= static_cast<int>(colours.size())) return;
        Color opaque = c;
        opaque.a = 255;
        colours[static_cast<size_t>(chosen)] = opaque;
        swatches->SetColorAt(chosen, opaque);
    }

    // The swatches, the line under them and the buttons, after a change.
    void Refresh() {
        const float inner = ContentWidth();
        owner->StyleSwatchBar(*swatches, colours.size());
        swatches->SetColors(colours);
        if (chosen >= 0) swatches->SelectColor(colours[static_cast<size_t>(chosen)], false);
        else swatches->ClearSelection();
        swatches->SetElementSize(Size2Df(inner, colours.empty() ? 1.0f : swatches->HeightForWidth(inner)));
        const size_t n = colours.size();
        if (n == 0) {
            status->SetText("Pick a colour, then Add colour");
        } else if (chosen >= 0) {
            status->SetText("Colour " + std::to_string(chosen + 1) + " of " + std::to_string(n) +
                            " - the picker sets it");
        } else {
            status->SetText(std::to_string(n) + (n == 1 ? " colour" : " colours") +
                            " - click one to change it");
        }
        remove->SetDisabled(chosen < 0);
        create->SetDisabled(n == 0);
        FitSize();
    }

    std::shared_ptr<UltraCanvasTextInput> name;
    std::shared_ptr<UltraCanvasColorPicker> picker;
    std::shared_ptr<UltraCanvasButton> remove;
    std::shared_ptr<UltraCanvasButton> create;
    std::shared_ptr<UltraCanvasLabel> status;
    std::shared_ptr<UltraCanvasColorSwatchBar> swatches;
};

// ===================================================================
// CONSTRUCTION
// ===================================================================

UltraCanvasColorPaletteManager::UltraCanvasColorPaletteManager(const std::string& identifier,
                                                               float x, float y, float w, float h)
        : UltraCanvasContainer(identifier, x, y, w, h) {
    layout.SetFlexColumn().SetFlexGap(style_.rowGap)
          .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    palettes_.push_back(DefaultPalette());
    Rebuild();
}

UltraCanvasColorPaletteManager::~UltraCanvasColorPaletteManager() {
    // A window closes its popups before its elements go, so a popup still
    // open here is on a window that is still there.
    ClosePopups();
    if (gallery_) gallery_->owner = nullptr;
    if (editor_) editor_->owner = nullptr;
    if (creator_) creator_->owner = nullptr;
}

// ===================================================================
// THE PALETTES
// ===================================================================

void UltraCanvasColorPaletteManager::SetPalettes(const std::vector<NamedColorPalette>& palettes) {
    ClosePopups();
    palettes_.clear();
    for (const auto& p : palettes) {
        if (!p.colors.empty()) palettes_.push_back(p);
    }
    if (palettes_.empty()) palettes_.push_back(DefaultPalette());
    selectedRow_ = selectedEntry_ = -1;
    Rebuild();
}

void UltraCanvasColorPaletteManager::AddPalette(const NamedColorPalette& palette) {
    if (palette.colors.empty()) return;
    palettes_.push_back(palette);
    Rebuild();
    NotifyChanged();
}

void UltraCanvasColorPaletteManager::RemovePalette(int index) {
    if (index < 0 || index >= GetPaletteCount() || GetPaletteCount() <= 1) return;
    // The editor works on an index that is about to move.
    if (editor_) {
        editor_->Close();
        editor_->owner = nullptr;
    }
    palettes_.erase(palettes_.begin() + index);
    if (selectedRow_ == index) selectedRow_ = selectedEntry_ = -1;
    else if (selectedRow_ > index) --selectedRow_;
    Rebuild();
    if (gallery_) gallery_->RefreshAddButtons();
    NotifyChanged();
}

bool UltraCanvasColorPaletteManager::HasPalette(const std::string& name) const {
    for (const auto& p : palettes_) {
        if (p.name == name) return true;
    }
    return false;
}

void UltraCanvasColorPaletteManager::SetEntryColor(int palette, int entry, const Color& c, bool notify) {
    if (palette < 0 || palette >= GetPaletteCount()) return;
    auto& colors = palettes_[static_cast<size_t>(palette)].colors;
    if (entry < 0 || entry >= static_cast<int>(colors.size())) return;
    Color opaque = c;
    opaque.a = 255;
    colors[static_cast<size_t>(entry)] = opaque;
    if (palette < static_cast<int>(rows_.size())) rows_[static_cast<size_t>(palette)].bar->SetColorAt(entry, opaque);
    if (notify) NotifyChanged();
}

void UltraCanvasColorPaletteManager::NotifyChanged() {
    if (onPalettesChanged) onPalettesChanged();
}

// ===================================================================
// THE USER'S OWN PALETTES
// ===================================================================

void UltraCanvasColorPaletteManager::SetUserPalettes(const std::vector<NamedColorPalette>& palettes) {
    userPalettes_.clear();
    for (const auto& p : palettes) {
        if (p.colors.empty()) continue;
        userPalettes_.push_back(p);
        userPalettes_.back().category = UserPaletteCategory();
    }
    if (gallery_ && gallery_->IsPopupElement()) gallery_->RebuildCards();
}

std::string UltraCanvasColorPaletteManager::AddUserPalette(const NamedColorPalette& palette) {
    if (palette.colors.empty()) return {};
    NamedColorPalette own = palette;
    own.name = UniquePaletteName(palette.name);
    own.category = UserPaletteCategory();
    userPalettes_.push_back(own);
    AddPalette(own);
    if (gallery_ && gallery_->IsPopupElement()) gallery_->RebuildCards();
    if (onUserPalettesChanged) onUserPalettesChanged();
    return own.name;
}

void UltraCanvasColorPaletteManager::RemoveUserPalette(const std::string& name) {
    const auto it = std::find_if(userPalettes_.begin(), userPalettes_.end(),
                                 [&name](const NamedColorPalette& p) { return p.name == name; });
    if (it == userPalettes_.end()) return;
    userPalettes_.erase(it);
    if (gallery_ && gallery_->IsPopupElement()) gallery_->RebuildCards();
    if (onUserPalettesChanged) onUserPalettesChanged();
}

std::string UltraCanvasColorPaletteManager::UniquePaletteName(const std::string& wanted) const {
    // As SerializePalettes writes it, so the name comes back the same.
    std::string base;
    for (char c : Trim(wanted)) {
        if (c != '|' && c != ';' && c != '\n' && c != '\r') base += c;
    }
    if (base.empty()) base = "Palette";
    auto taken = [this](const std::string& name) {
        if (HasPalette(name)) return true;
        for (const auto& p : userPalettes_) if (p.name == name) return true;
        for (const auto& p : PresetPalettes()) if (p.name == name) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        const std::string name = base + " " + std::to_string(i);
        if (!taken(name)) return name;
    }
}

// ===================================================================
// SELECTION
// ===================================================================

void UltraCanvasColorPaletteManager::SelectInRow(int row, int entry) {
    selectedRow_ = row;
    selectedEntry_ = entry;
    // One mark across all the rows: the colour picked last.
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (static_cast<int>(i) != row) rows_[i].bar->ClearSelection();
    }
}

// The pointer onto a swatch of `row` (entry >= 0), or off that row's bar.
void UltraCanvasColorPaletteManager::HoverInRow(int row, int entry) {
    if (entry >= 0 && row >= 0 && row < GetPaletteCount() &&
        entry < static_cast<int>(palettes_[static_cast<size_t>(row)].colors.size())) {
        hoverRow_ = row;
        hoverColor_ = palettes_[static_cast<size_t>(row)].colors[static_cast<size_t>(entry)];
        hoverColor_.a = 255;
        RequestRedraw();
        if (onColorHovered) onColorHovered(hoverColor_);
        return;
    }
    // Another row's swatch may already have taken over.
    if (entry < 0 && hoverRow_ == row) {
        hoverRow_ = -1;
        RequestRedraw();
        if (onColorHoverEnded) onColorHoverEnded();
    }
}

void UltraCanvasColorPaletteManager::SelectColor(const Color& c) {
    for (size_t i = 0; i < rows_.size(); ++i) {
        rows_[i].bar->SelectColor(c, false);
        if (rows_[i].bar->HasColorSelected()) {
            SelectInRow(static_cast<int>(i), rows_[i].bar->GetSelectedIndex());
            return;
        }
    }
    ClearSelection();
}

void UltraCanvasColorPaletteManager::ClearSelection() {
    selectedRow_ = selectedEntry_ = -1;
    for (auto& r : rows_) r.bar->ClearSelection();
}

// ===================================================================
// THE ROWS
// ===================================================================

void UltraCanvasColorPaletteManager::SetManagerStyle(const ColorPaletteManagerStyle& s) {
    style_ = s;
    layout.SetFlexGap(style_.rowGap);
    Rebuild();
}

float UltraCanvasColorPaletteManager::BarWidth(float width) const {
    return std::max(20.0f, width - style_.buttonSize - style_.buttonGap);
}

// The fewer colours a palette has, the larger its swatches may be: a row of
// a dozen reads like the strip it replaced, 256 make a compact block.
void UltraCanvasColorPaletteManager::StyleSwatchBar(UltraCanvasColorSwatchBar& bar,
                                                    size_t colourCount) const {
    ColorSwatchBarStyle s = bar.GetStyle();
    s.border = style_.swatchBorder;
    s.hoverBorder = style_.swatchHoverBorder;
    s.selectedBorder = style_.swatchSelectedBorder;
    s.swatchSize = style_.swatchSize;
    s.padding = 1.0f;
    if (colourCount <= 32) {
        s.minSwatchSize = 10.0f;
        s.spacing = 2.0f;
        s.cornerRadius = 2.0f;
    } else if (colourCount <= 64) {
        s.minSwatchSize = 9.0f;
        s.spacing = 2.0f;
        s.cornerRadius = 1.5f;
    } else if (colourCount <= 216) {
        s.minSwatchSize = 8.0f;
        s.spacing = 1.0f;
        s.cornerRadius = 1.0f;
    } else {
        // A whole GIF palette: small enough for 32 a line, so 256 colours
        // are eight full lines rather than eleven and a stub.
        s.minSwatchSize = 6.0f;
        s.spacing = 1.0f;
        s.cornerRadius = 1.0f;
    }
    s.selectedBorderWidth = 2.0f;
    bar.SetStyle(s);
    bar.SetWrap(true);
}

std::shared_ptr<UltraCanvasButton> UltraCanvasColorPaletteManager::MakeMicroButton(
        const std::string& id, const std::string& text, const std::string& tooltip) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, style_.buttonSize, style_.buttonSize, text);
    MakeGlyphButton(*b);
    StyleFlatButton(*b, style_.buttonColor, style_.buttonHoverColor, style_.buttonBorder,
                    style_.buttonText, style_.buttonSize - 2.0f, FontWeight::Bold, 3.0f);
    b->SetTooltip(tooltip);
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return b;
}

float UltraCanvasColorPaletteManager::HeightForWidth(float width) const {
    const float barW = BarWidth(width);
    float h = 0.0f;
    const bool several = palettes_.size() > 1;
    for (size_t i = 0; i < palettes_.size(); ++i) {
        UltraCanvasColorSwatchBar probe("", barW, 10.0f);
        StyleSwatchBar(probe, palettes_[i].colors.size());
        probe.SetColors(palettes_[i].colors);
        const bool last = (i + 1 == palettes_.size());
        const int buttons = (several ? 1 : 0) + (last ? 1 : 0);
        const float buttonsH = buttons * style_.buttonSize + std::max(0, buttons - 1) * 2.0f;
        h += std::max(probe.HeightForWidth(barW), buttonsH);
        if (i > 0) h += style_.rowGap;
    }
    return h;
}

void UltraCanvasColorPaletteManager::Rebuild() {
    // The rows go, and the pointer is over none of the new ones yet.
    if (hoverRow_ >= 0) {
        hoverRow_ = -1;
        if (onColorHoverEnded) onColorHoverEnded();
    }
    // The widgets of the last rebuild but one are no longer anyone's.
    retired_.clear();
    for (auto& r : rows_) retired_.push_back(r.row);
    rows_.clear();
    ClearChildren();

    const float width = GetWidth();
    builtWidth_ = width;
    const float barW = BarWidth(width);
    const bool several = palettes_.size() > 1;

    for (size_t i = 0; i < palettes_.size(); ++i) {
        const NamedColorPalette& p = palettes_[i];
        const std::string id = GetIdentifier() + "-row" + std::to_string(i);
        const int index = static_cast<int>(i);
        const bool last = (i + 1 == palettes_.size());
        Row r;

        r.bar = std::make_shared<UltraCanvasColorSwatchBar>(id + "-bar", barW, 10.0f);
        StyleSwatchBar(*r.bar, p.colors.size());
        r.bar->SetColors(p.colors);
        const float barH = r.bar->HeightForWidth(barW);
        r.bar->SetElementSize(Size2Df(barW, barH));
        r.bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        r.bar->onColorSelected = [this, index](const Color& c) {
            SelectInRow(index, rows_[static_cast<size_t>(index)].bar->GetSelectedIndex());
            if (onColorSelected) onColorSelected(c);
        };
        r.bar->onColorContextClicked = [this, index](int entry, const Point2Di& pos) {
            OpenEntryEditor(index, entry, pos);
        };
        r.bar->onColorHovered = [this, index](int entry) { HoverInRow(index, entry); };
        if (selectedRow_ == index && selectedEntry_ >= 0 &&
            selectedEntry_ < static_cast<int>(p.colors.size())) {
            r.bar->SelectColor(p.colors[static_cast<size_t>(selectedEntry_)], false);
        }

        // The buttons' column: "-" on every row while there are several,
        // and "+" on the last.
        auto buttons = std::make_shared<UltraCanvasContainer>(id + "-buttons", 0, 0,
                                                              style_.buttonSize, 10.0f);
        buttons->layout.SetFlexColumn().SetFlexGap(2.0f);
        buttons->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        int count = 0;
        if (several) {
            r.remove = MakeMicroButton(id + "-remove", "−", "Remove " + p.name);
            r.remove->onClick = [this, index]() { RemovePalette(index); };
            buttons->AddChild(r.remove);
            ++count;
        }
        if (last) {
            r.add = MakeMicroButton(id + "-add", "+", "Add a colour palette");
            r.add->onClick = [this, add = r.add.get()]() {
                OpenPaletteGallery(add->GetBoundsInWindow());
            };
            buttons->AddChild(r.add);
            ++count;
        }
        const float buttonsH = count * style_.buttonSize + std::max(0, count - 1) * 2.0f;
        buttons->SetElementSize(Size2Df(style_.buttonSize, std::max(1.0f, buttonsH)));
        const float rowH = std::max(barH, buttonsH);

        r.row = std::make_shared<UltraCanvasContainer>(id, 0, 0, width, rowH);
        r.row->layout.SetFlexRow().SetFlexGap(style_.buttonGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        r.row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        r.row->AddChild(r.bar);
        r.row->AddChild(buttons);
        if (!p.name.empty()) r.row->SetTooltip(p.name);
        AddChild(r.row);
        rows_.push_back(std::move(r));
    }
    FitHeight();
}

void UltraCanvasColorPaletteManager::FitHeight() {
    const float h = std::max(1.0f, HeightForWidth(GetWidth()));
    // In the layout too, as the colour picker's auto height: a manager in a
    // flex column is given size.height by the next layout pass.
    size.height = CSSLayout::Dimension::Px(h);
    SetHeight(h);
    InvalidateLayout();
    if (auto* parent = GetParentContainer()) parent->InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasColorPaletteManager::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    // Laid out to another width than the rows were built for: the swatches
    // wrap differently, so the rows are built again.
    if (std::fabs(GetWidth() - builtWidth_) > 0.5f && GetWidth() > 0.0f) Rebuild();
    // The colour under the pointer floods the rows, as a hovered swatch
    // floods the colour picker, so it can be judged on a large area.
    if (hoverRow_ >= 0) {
        ctx->SetFillPaint(hoverColor_);
        ctx->FillRectangle(Rect2Dd(0, 0, GetWidth(), GetHeight()));
    }
    UltraCanvasContainer::Render(ctx, dirtyRect);
}

// ===================================================================
// POPUPS
// ===================================================================

void UltraCanvasColorPaletteManager::ClosePopups() {
    // The creator first: it is opened over the palette manager.
    if (creator_) creator_->Close();
    if (gallery_) gallery_->Close();
    if (editor_) editor_->Close();
}

void UltraCanvasColorPaletteManager::OpenPaletteGallery(const Rect2Df& anchorWindow) {
    UltraCanvasWindowBase* win = GetWindow();
    if (!win) return;
    ClosePopups();
    if (gallery_) gallery_->owner = nullptr;
    gallery_ = std::make_shared<UltraCanvasColorPaletteGallery>(GetIdentifier() + "-gallery", this);
    const float height = std::min(kGalleryMaxHeight,
                                  std::max(200.0f, static_cast<float>(win->GetHeight()) - 16.0f));
    gallery_->Build(height);

    PopupElementSettings settings;
    settings.popupOwner = weak_from_this();
    settings.closeByEscapeKey = true;
    settings.closeByClickOutside = true;
    UltraCanvasTooltipManager::HideTooltip();
    win->OpenPopup(PopupPosition(win, anchorWindow, Size2Df(kGalleryWidth, height)),
                   *gallery_, settings);
}

void UltraCanvasColorPaletteManager::OpenEntryEditor(int palette, int entry, const Point2Di& windowPos) {
    if (palette < 0 || palette >= GetPaletteCount()) return;
    if (entry < 0 || entry >= static_cast<int>(palettes_[static_cast<size_t>(palette)].colors.size())) return;
    UltraCanvasWindowBase* win = GetWindow();
    if (!win) return;
    ClosePopups();
    if (editor_) editor_->owner = nullptr;
    editor_ = std::make_shared<UltraCanvasColorPaletteEntryEditor>(GetIdentifier() + "-editor",
                                                                   this, palette, entry);
    editor_->Build();

    PopupElementSettings settings;
    settings.popupOwner = weak_from_this();
    settings.closeByEscapeKey = true;
    settings.closeByClickOutside = true;
    UltraCanvasTooltipManager::HideTooltip();
    // Below the swatch that was clicked, centred on it where the window allows.
    const Size2Df size(kEditorWidth, editor_->GetHeight());
    const Rect2Df anchor(static_cast<float>(windowPos.x) - kEditorWidth * 0.5f,
                         static_cast<float>(windowPos.y) - 6.0f, kEditorWidth, 12.0f);
    win->OpenPopup(PopupPosition(win, anchor, size), *editor_, settings);
}

void UltraCanvasColorPaletteManager::OpenPaletteCreator(const Rect2Df& anchorWindow) {
    UltraCanvasWindowBase* win = GetWindow();
    if (!win) return;
    ClosePopups();
    // Built first for its size, which says where it goes.
    OpenCreatorAt(Point2Di(0, 0));
    if (!creator_ || !creator_->IsPopupElement()) return;
    creator_->SetElementAbsolutePosition(Point2Df(PopupPosition(
            win, anchorWindow, Size2Df(kCreatorWidth, creator_->GetHeight()))));
}

// The creator with its top-left corner at `windowPos` (kept inside the
// window), over whatever is open - the palette manager it was asked from.
void UltraCanvasColorPaletteManager::OpenCreatorAt(const Point2Di& windowPos) {
    UltraCanvasWindowBase* win = GetWindow();
    if (!win) return;
    if (editor_) editor_->Close();
    if (creator_) {
        creator_->Close();
        creator_->owner = nullptr;
    }
    creator_ = std::make_shared<UltraCanvasColorPaletteCreator>(GetIdentifier() + "-creator", this);
    creator_->Build();

    PopupElementSettings settings;
    settings.popupOwner = weak_from_this();
    settings.closeByEscapeKey = true;
    settings.closeByClickOutside = true;
    UltraCanvasTooltipManager::HideTooltip();
    const float maxX = std::max(0.0f, static_cast<float>(win->GetWidth()) - kCreatorWidth);
    const float maxY = std::max(0.0f, static_cast<float>(win->GetHeight()) - creator_->GetHeight());
    const Point2Di at(static_cast<int>(std::clamp(static_cast<float>(windowPos.x), 0.0f, maxX)),
                      static_cast<int>(std::clamp(static_cast<float>(windowPos.y), 0.0f, maxY)));
    win->OpenPopup(at, *creator_, settings);
}

// ===================================================================
// READY-MADE PALETTES
// ===================================================================

NamedColorPalette UltraCanvasColorPaletteManager::DefaultPalette() {
    return {"Basic", "Basics", UltraCanvasColorSwatchBar::DefaultPalette()};
}

const std::vector<NamedColorPalette>& UltraCanvasColorPaletteManager::PresetPalettes() {
    static const std::vector<NamedColorPalette> presets = [] {
        std::vector<NamedColorPalette> out;
        // ----- basics -----
        out.push_back(DefaultPalette());
        out.push_back({"Greys", "Basics", UltraCanvasColorSwatchBar::GrayscalePalette(16)});
        // Two full lines of 16 under a picker or in the gallery.
        out.push_back({"Rainbow", "Basics", HueSteps(32, 0.85f, 0.95f)});
        // ----- moods -----
        // Sixteen colours each, which fill a row under a picker as the basic
        // eighteen do; a palette saved with the first eight or twelve comes
        // back with all sixteen (ParsePalettes), so the new ones go last.
        out.push_back({"Pastel", "Moods", HexList({0xFFB3BA, 0xFFDFBA, 0xFFFFBA, 0xBAFFC9, 0xBAE1FF, 0xD7BAFF,
                                                   0xF8C8DC, 0xC1E1C1, 0xFDFD96, 0xAEC6CF, 0xCBAACB, 0xFFD1DC,
                                                   0xB5EAD7, 0xE2F0CB, 0xFFDAC1, 0xC7CEEA})});
        out.push_back({"Earth", "Moods", HexList({0x3E2723, 0x5D4037, 0x795548, 0xA1887F, 0xD7CCC8, 0x8D6E63,
                                                  0x6D4C41, 0xA67B5B, 0xC19A6B, 0x556B2F, 0x808000, 0xBDB76B,
                                                  0x8B5A2B, 0xCD853F, 0x6B8E23, 0xE3C9A8})});
        out.push_back({"Skin tones", "Moods", HexList({0xFFDFC4, 0xF0D5BE, 0xEECEB3, 0xE1B899, 0xE5C298,
                                                       0xC68642, 0xA1665E, 0x8D5524, 0x6B4226, 0x3D2210,
                                                       0xFFE0BD, 0xFFCD94, 0xD2A679, 0xB47B5A, 0x7A4E36, 0x4A2C1D})});
        out.push_back({"Sunset", "Moods", HexList({0x0B1D51, 0x2E1760, 0x6A0572, 0xAB1D5F,
                                                   0xE8505B, 0xF9A03F, 0xF6D365, 0xFFF3B0,
                                                   0x3A0CA3, 0x7209B7, 0xC9184A, 0xFF5E5B,
                                                   0xFF7F50, 0xFFB347, 0xFFD8A8, 0xFDE2E4})});
        out.push_back({"Ocean", "Moods", HexList({0x03045E, 0x023E8A, 0x0077B6, 0x0096C7,
                                                  0x00B4D8, 0x48CAE4, 0x90E0EF, 0xCAF0F8,
                                                  0x001219, 0x005F73, 0x0A9396, 0x94D2BD,
                                                  0x006D77, 0x2A9D8F, 0x83C5BE, 0xEDF6F9})});
        out.push_back({"Forest", "Moods", HexList({0x1B2D1B, 0x2D4A22, 0x3F6B2A, 0x5A8A3A,
                                                   0x7FA65A, 0xA9C47F, 0xD9E4B5, 0x6B4F2A,
                                                   0x0B3D20, 0x145A32, 0x1E8449, 0x52BE80,
                                                   0x4A3728, 0x7D5A3C, 0x8B6B3E, 0xC2B280})});
        out.push_back({"Neon", "Moods", HexList({0xFF00FF, 0x00FFFF, 0x39FF14, 0xFFFF00,
                                                 0xFF3131, 0xFF6EC7, 0x7DF9FF, 0xBC13FE,
                                                 0xFF5F1F, 0xCCFF00, 0x1F51FF, 0xFF1493,
                                                 0x0FF0FC, 0xFFAC1C, 0x8F00FF, 0x00FF7F})});
        // ----- design systems -----
        out.push_back({"Material", "Design systems",
                       HexList({0xF44336, 0xE91E63, 0x9C27B0, 0x673AB7, 0x3F51B5, 0x2196F3, 0x03A9F4,
                                0x00BCD4, 0x009688, 0x4CAF50, 0x8BC34A, 0xCDDC39, 0xFFEB3B, 0xFFC107,
                                0xFF9800, 0xFF5722, 0x795548, 0x9E9E9E, 0x607D8B})});
        out.push_back({"Nord", "Design systems",
                       HexList({0x2E3440, 0x3B4252, 0x434C5E, 0x4C566A, 0xD8DEE9, 0xE5E9F0, 0xECEFF4, 0x8FBCBB,
                                0x88C0D0, 0x81A1C1, 0x5E81AC, 0xBF616A, 0xD08770, 0xEBCB8B, 0xA3BE8C, 0xB48EAD})});
        out.push_back({"Solarized", "Design systems",
                       HexList({0x002B36, 0x073642, 0x586E75, 0x657B83, 0x839496, 0x93A1A1, 0xEEE8D5, 0xFDF6E3,
                                0xB58900, 0xCB4B16, 0xDC322F, 0xD33682, 0x6C71C4, 0x268BD2, 0x2AA198, 0x859900})});
        // ----- retro machines -----
        out.push_back({"Commodore 64", "Retro",
                       HexList({0x000000, 0xFFFFFF, 0x880000, 0xAAFFEE, 0xCC44CC, 0x00CC55, 0x0000AA, 0xEEEE77,
                                0xDD8855, 0x664400, 0xFF7777, 0x333333, 0x777777, 0xAAFF66, 0x0088FF, 0xBBBBBB})});
        // ----- indexed (GIF) -----
        {
            // The 6 x 6 x 6 colour cube browsers once agreed on.
            std::vector<Color> web;
            for (int r = 0; r < 6; ++r)
                for (int g = 0; g < 6; ++g)
                    for (int b = 0; b < 6; ++b)
                        web.emplace_back(static_cast<uint8_t>(r * 51), static_cast<uint8_t>(g * 51),
                                         static_cast<uint8_t>(b * 51), 255);
            out.push_back({"Web-safe 216", "Indexed (GIF)", web});
        }
        {
            // 3 bits of red, 3 of green, 2 of blue: one byte per pixel, a
            // whole GIF palette.
            std::vector<Color> rgb332;
            for (int i = 0; i < 256; ++i) {
                rgb332.emplace_back(static_cast<uint8_t>(((i >> 5) & 7) * 255 / 7),
                                    static_cast<uint8_t>(((i >> 2) & 7) * 255 / 7),
                                    static_cast<uint8_t>((i & 3) * 255 / 3), 255);
            }
            out.push_back({"GIF 256 (RGB 3-3-2)", "Indexed (GIF)", rgb332});
        }
        {
            std::vector<Color> grey;
            for (int i = 0; i < 256; ++i) {
                const auto v = static_cast<uint8_t>(i);
                grey.emplace_back(v, v, v, 255);
            }
            out.push_back({"GIF 256 greys", "Indexed (GIF)", grey});
        }
        return out;
    }();
    return presets;
}

// ===================================================================
// PERSISTENCE
// ===================================================================

std::string UltraCanvasColorPaletteManager::SerializePalettes(const std::vector<NamedColorPalette>& palettes) {
    std::string out;
    for (const auto& p : palettes) {
        if (p.colors.empty()) continue;
        if (!out.empty()) out += ';';
        for (char c : p.name) {
            if (c != '|' && c != ';' && c != '\n' && c != '\r') out += c;
        }
        out += '|';
        for (size_t i = 0; i < p.colors.size(); ++i) {
            if (i > 0) out += ',';
            out += HexOf(p.colors[i]);
        }
    }
    return out;
}

bool UltraCanvasColorPaletteManager::ParsePalettes(const std::string& text, std::vector<NamedColorPalette>& out) {
    std::vector<NamedColorPalette> parsed;
    for (const std::string& part : Split(text, ';')) {
        const size_t bar = part.find('|');
        if (bar == std::string::npos) continue;
        NamedColorPalette p;
        p.name = Trim(part.substr(0, bar));
        for (const std::string& hex : Split(part.substr(bar + 1), ',')) {
            Color c;
            if (ParseHexColor(Trim(hex), c)) p.colors.push_back(c);
        }
        // The category comes back from the preset of that name, if any -
        // and the colours a preset has gained since it was saved, when the
        // saved ones are its first ones unchanged.
        for (const auto& preset : PresetPalettes()) {
            if (preset.name != p.name) continue;
            p.category = preset.category;
            if (!p.colors.empty() && p.colors.size() < preset.colors.size() &&
                std::equal(p.colors.begin(), p.colors.end(), preset.colors.begin())) {
                p.colors = preset.colors;
            }
        }
        if (!p.colors.empty()) parsed.push_back(std::move(p));
    }
    if (parsed.empty()) return false;
    out = std::move(parsed);
    return true;
}

} // namespace UltraCanvas
