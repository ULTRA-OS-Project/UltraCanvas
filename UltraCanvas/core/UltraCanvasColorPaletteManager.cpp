// core/UltraCanvasColorPaletteManager.cpp
// The palette rows under a colour picker, the palette gallery and the
// palette-entry editor.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "UltraCanvasColorPaletteManager.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasTextUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace UltraCanvas {

namespace {
    constexpr float kPopupPadding = 12.0f;
    constexpr float kPopupGap = 8.0f;
    constexpr float kGalleryWidth = 320.0f;
    constexpr float kGalleryMaxHeight = 520.0f;
    constexpr float kCardPadding = 8.0f;
    constexpr float kCardRadius = 6.0f;
    constexpr float kCardHeaderHeight = 22.0f;
    constexpr float kTitleHeight = 18.0f;
    constexpr float kSubtitleHeight = 16.0f;
    constexpr float kSectionHeight = 16.0f;
    constexpr float kFooterHeight = 26.0f;
    constexpr float kListScrollbarRoom = 14.0f;
    constexpr float kEditorWidth = 236.0f;

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

// A rounded white card with a soft shadow, as the tool palette's fold-out.
class UltraCanvasColorPalettePopup : public UltraCanvasContainer {
public:
    UltraCanvasColorPalettePopup(const std::string& identifier,
                                 UltraCanvasColorPaletteManager* manager)
        : UltraCanvasContainer(identifier, 0, 0, 0, 0), owner(manager) {
        const ColorPaletteManagerStyle& st = manager->GetManagerStyle();
        SetBackgroundColor(st.popupBackground);
        SetBorders(1.0f, st.popupBorder, st.popupRadius);
        SetPadding(kPopupPadding);
        layout.SetFlexColumn().SetFlexGap(kPopupGap)
              .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        radius = st.popupRadius;
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

    // The manager this popup belongs to; cleared when the manager goes first.
    UltraCanvasColorPaletteManager* owner;
    float radius = 8.0f;
};

// ----- the gallery: the ready-made palettes, each with its add button -----
class UltraCanvasColorPaletteGallery : public UltraCanvasColorPalettePopup {
public:
    UltraCanvasColorPaletteGallery(const std::string& identifier,
                                   UltraCanvasColorPaletteManager* manager)
        : UltraCanvasColorPalettePopup(identifier, manager) {}

    void Build(float height) {
        const ColorPaletteManagerStyle& st = owner->GetManagerStyle();
        const float inner = kGalleryWidth - 2.0f * kPopupPadding;
        SetElementSize(Size2Df(kGalleryWidth, height));

        AddChild(MakeLabel(GetIdentifier() + "-title", inner, kTitleHeight, "Colour palettes",
                           st.fontSize + 3.0f, FontWeight::Bold, st.titleText));
        AddChild(MakeLabel(GetIdentifier() + "-sub", inner, kSubtitleHeight,
                           "+ adds a scheme as a new row of swatches",
                           st.fontSize, FontWeight::Normal, st.mutedText));

        const float listH = std::max(80.0f, height - 2.0f * kPopupPadding - kTitleHeight -
                                            kSubtitleHeight - kFooterHeight - 3.0f * kPopupGap);
        list = CreateScrollableContainer(GetIdentifier() + "-list", 0, 0, inner, listH);
        // Up and down only: the cards are laid out to the list's width.
        ContainerStyle listStyle = list->GetContainerStyle();
        listStyle.autoShowHorizontalScrollbar = false;
        listStyle.forceShowHorizontalScrollbar = false;
        list->SetContainerStyle(listStyle);
        list->layout.SetFlexColumn().SetFlexGap(6.0f)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        list->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        AddChild(list);

        const float cardW = inner - kListScrollbarRoom;
        const float cardInner = cardW - 2.0f * kCardPadding;
        std::string category;
        int n = 0;
        for (const NamedColorPalette& preset : UltraCanvasColorPaletteManager::PresetPalettes()) {
            const std::string id = GetIdentifier() + "-p" + std::to_string(n++);
            if (preset.category != category) {
                category = preset.category;
                std::string caption = category;
                std::transform(caption.begin(), caption.end(), caption.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                auto section = MakeLabel(id + "-section", cardW, kSectionHeight, caption,
                                         st.fontSize - 1.0f, FontWeight::Bold, st.mutedText);
                list->AddChild(section);
            }

            auto bar = std::make_shared<UltraCanvasColorSwatchBar>(id + "-bar", cardInner, 10.0f);
            owner->StyleSwatchBar(*bar, preset.colors.size());
            bar->SetColors(preset.colors);
            const float barH = bar->HeightForWidth(cardInner);
            bar->SetElementSize(Size2Df(cardInner, barH));
            bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            // A preview: a click adds the palette, as its button does.
            const std::string name = preset.name;
            bar->onColorSelected = [this, name](const Color&) { AddPreset(name); };

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
            header->AddChild(MakeLabel(id + "-name", cardInner - countW - addW - 12.0f,
                                       kCardHeaderHeight, preset.name, st.fontSize + 1.0f,
                                       FontWeight::Bold, st.titleText));
            const size_t count = preset.colors.size();
            header->AddChild(MakeLabel(id + "-count", countW, kCardHeaderHeight,
                                       std::to_string(count) + (count == 1 ? " colour" : " colours"),
                                       st.fontSize, FontWeight::Normal, st.mutedText,
                                       TextAlignment::Right));
            auto add = std::make_shared<UltraCanvasButton>(id + "-add", 0, 0, addW, addW, "+");
            MakeGlyphButton(*add);
            add->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            add->onClick = [this, name]() { AddPreset(name); };
            header->AddChild(add);
            addButtons.emplace_back(name, add.get());

            card->AddChild(header);
            card->AddChild(bar);
            list->AddChild(card);
        }
        RefreshAddButtons();

        auto footer = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "-footer", 0, 0,
                                                             inner, kFooterHeight);
        footer->layout.SetFlexRow().SetFlexGap(6.0f)
                      .SetFlexJustifyContent(CSSLayout::JustifyContent::End)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        footer->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        auto done = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-done", 0, 0, 72, 24, "Done");
        StyleFlatButton(*done, st.buttonColor, st.buttonHoverColor, st.buttonBorder,
                        st.buttonText, st.fontSize + 1.0f, FontWeight::Normal, 5.0f);
        done->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        done->onClick = [this]() { Close(); };
        footer->AddChild(done);
        AddChild(footer);
    }

    // Each preset's button: "+" while it can be added, a check mark once it
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

    void AddPreset(const std::string& name) {
        if (!owner || owner->HasPalette(name)) return;
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
};

// ----- the entry editor: a foreground-only picker for one palette colour -----
class UltraCanvasColorPaletteEntryEditor : public UltraCanvasColorPalettePopup {
public:
    UltraCanvasColorPaletteEntryEditor(const std::string& identifier,
                                       UltraCanvasColorPaletteManager* manager,
                                       int paletteIndex, int entryIndex)
        : UltraCanvasColorPalettePopup(identifier, manager),
          palette(paletteIndex), entry(entryIndex) {}

    void Build() {
        const ColorPaletteManagerStyle& st = owner->GetManagerStyle();
        const NamedColorPalette& p = owner->GetPalettes()[static_cast<size_t>(palette)];
        original = p.colors[static_cast<size_t>(entry)];
        const float inner = kEditorWidth - 2.0f * kPopupPadding;

        AddChild(MakeLabel(GetIdentifier() + "-title", inner, kTitleHeight,
                           "Colour " + std::to_string(entry + 1) + " of " + p.name,
                           st.fontSize + 2.0f, FontWeight::Bold, st.titleText));

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
        AddChild(picker);

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
        AddChild(footer);
        FitSize();
    }

    void FitSize() {
        const float h = 2.0f * kPopupPadding + kTitleHeight + picker->GetHeight() +
                        kFooterHeight + 2.0f * kPopupGap;
        SetElementSize(Size2Df(kEditorWidth, h));
        InvalidateLayout();
    }

    int palette;
    int entry;
    Color original;
    std::shared_ptr<UltraCanvasColorPicker> picker;
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
    } else {
        s.minSwatchSize = 8.0f;
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
    UltraCanvasContainer::Render(ctx, dirtyRect);
}

// ===================================================================
// POPUPS
// ===================================================================

void UltraCanvasColorPaletteManager::ClosePopups() {
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
        out.push_back({"Rainbow", "Basics", HueSteps(24, 0.85f, 0.95f)});
        // ----- moods -----
        out.push_back({"Pastel", "Moods", HexList({0xFFB3BA, 0xFFDFBA, 0xFFFFBA, 0xBAFFC9, 0xBAE1FF, 0xD7BAFF,
                                                   0xF8C8DC, 0xC1E1C1, 0xFDFD96, 0xAEC6CF, 0xCBAACB, 0xFFD1DC})});
        out.push_back({"Earth", "Moods", HexList({0x3E2723, 0x5D4037, 0x795548, 0xA1887F, 0xD7CCC8, 0x8D6E63,
                                                  0x6D4C41, 0xA67B5B, 0xC19A6B, 0x556B2F, 0x808000, 0xBDB76B})});
        out.push_back({"Skin tones", "Moods", HexList({0xFFDFC4, 0xF0D5BE, 0xEECEB3, 0xE1B899, 0xE5C298,
                                                       0xC68642, 0xA1665E, 0x8D5524, 0x6B4226, 0x3D2210})});
        out.push_back({"Sunset", "Moods", HexList({0x0B1D51, 0x2E1760, 0x6A0572, 0xAB1D5F,
                                                   0xE8505B, 0xF9A03F, 0xF6D365, 0xFFF3B0})});
        out.push_back({"Ocean", "Moods", HexList({0x03045E, 0x023E8A, 0x0077B6, 0x0096C7,
                                                  0x00B4D8, 0x48CAE4, 0x90E0EF, 0xCAF0F8})});
        out.push_back({"Forest", "Moods", HexList({0x1B2D1B, 0x2D4A22, 0x3F6B2A, 0x5A8A3A,
                                                   0x7FA65A, 0xA9C47F, 0xD9E4B5, 0x6B4F2A})});
        out.push_back({"Neon", "Moods", HexList({0xFF00FF, 0x00FFFF, 0x39FF14, 0xFFFF00,
                                                 0xFF3131, 0xFF6EC7, 0x7DF9FF, 0xBC13FE})});
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
        out.push_back({"Game Boy", "Retro", HexList({0x0F380F, 0x306230, 0x8BAC0F, 0x9BBC0F})});
        out.push_back({"PICO-8", "Retro",
                       HexList({0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
                                0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA})});
        out.push_back({"Commodore 64", "Retro",
                       HexList({0x000000, 0xFFFFFF, 0x880000, 0xAAFFEE, 0xCC44CC, 0x00CC55, 0x0000AA, 0xEEEE77,
                                0xDD8855, 0x664400, 0xFF7777, 0x333333, 0x777777, 0xAAFF66, 0x0088FF, 0xBBBBBB})});
        out.push_back({"VGA 16", "Retro",
                       HexList({0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
                                0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF})});
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
        // The category comes back from the preset of that name, if any.
        for (const auto& preset : PresetPalettes()) {
            if (preset.name == p.name) p.category = preset.category;
        }
        if (!p.colors.empty()) parsed.push_back(std::move(p));
    }
    if (parsed.empty()) return false;
    out = std::move(parsed);
    return true;
}

} // namespace UltraCanvas
