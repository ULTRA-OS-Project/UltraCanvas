// core/UltraCanvasTextFormatBar.cpp
// The text formatting bar: see include/UltraCanvasTextFormatBar.h.
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "UltraCanvasTextFormatBar.h"

#include "UltraCanvasApplication.h"     // GetInstalledFontFamilies, PostToUIThread
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"          // GetResourcesDir
#include "UltraCanvasDropdown.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSeparator.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasUtils.h"           // NormalizePath

#include <algorithm>

namespace UltraCanvas {

namespace {
    constexpr float kField = 26.0f;      // the fields' height
    constexpr float kButton = 30.0f;     // the toggles' side

    std::string IconPath(const std::string& file) {
        return NormalizePath(GetResourcesDir() + "media/icons/textformat/" + file);
    }
}

// ===== EDITOR HELPERS =====

RichCharFormatDelta TextFormatCharDelta(TextFormatAttribute attribute, const TextFormat& f, float pointsPerUnit) {
    RichCharFormatDelta d;
    switch (attribute) {
        case TextFormatAttribute::Font: d.setFontFamily = true; d.fontFamily = f.fontFamily; break;
        case TextFormatAttribute::Size: d.setFontSize = true; d.fontSizePt = std::max(0.1f, f.size) * pointsPerUnit; break;
        case TextFormatAttribute::Bold: d.setBold = true; d.bold = f.bold; break;
        case TextFormatAttribute::Italic: d.setItalic = true; d.italic = f.italic; break;
        case TextFormatAttribute::Underline: d.setUnderline = true; d.underline = f.underline; break;
        case TextFormatAttribute::Strikethrough: d.setStrikethrough = true; d.strikethrough = f.strikethrough; break;
        default: break;
    }
    return d;
}

TextFormat TextFormatAtCaret(const RichCharFormatState& state, const TextFormat& base, float pointsPerUnit) {
    using Tri = RichCharFormatState::Tri;
    TextFormat f = base;
    if (!state.fontFamilyMixed && !state.fontFamily.empty()) f.fontFamily = state.fontFamily;
    if (!state.fontSizeMixed && state.fontSizePt > 0.0f && pointsPerUnit > 0.0f) f.size = state.fontSizePt / pointsPerUnit;
    if (state.bold == Tri::On) f.bold = true;
    if (state.italic == Tri::On) f.italic = true;
    if (state.underline != Tri::Mixed) f.underline = state.underline == Tri::On;
    if (state.strikethrough != Tri::Mixed) f.strikethrough = state.strikethrough == Tri::On;
    return f;
}

// ===== THE BAR =====

UltraCanvasTextFormatBar::UltraCanvasTextFormatBar(const std::string& identifier, float height)
    : UltraCanvasContainer(identifier, 0, 0, 0, height) {
    Build(height);
}

UltraCanvasTextFormatBar::~UltraCanvasTextFormatBar() {
    if (alive) *alive = false;
}

void UltraCanvasTextFormatBar::Build(float height) {
    layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    SetPadding(0, 8);
    (void)height;

    auto rule = [this]() {
        auto line = std::make_shared<UltraCanvasSeparator>(true, 1, 28, Color(190, 190, 198, 255));
        line->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        line->SetMargin(0, 4);
        AddChild(line);
        rules.push_back(line);
    };

    // ----- Font -----
    fontBox = CreateDropdown(GetIdentifier() + "-font", 0, 0, 210, kField);
    DropdownStyle fontStyle = fontBox->GetStyle();
    fontStyle.maxVisibleItems = 18;
    fontStyle.itemHeight = 26.0f;
    fontStyle.fontSize = 12.0f;
    fontBox->SetStyle(fontStyle);
    fontBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    fontBox->onSelectionChanged = [this](int, const DropdownItem& item) {
        if (showing || item.value.empty()) return;
        format.fontFamily = item.value;
        Changed(TextFormatAttribute::Font);
    };
    fontBox->onDropdownClosed = [this]() { ControlDone(); };
    AddChild(fontBox);
    RefreshFonts();

    // A field: every step applies at once.
    auto field = [this](const std::string& id, float width, double minimum, double maximum, int decimals,
                        double step, const std::string& suffix, const std::string& tooltip) {
        auto s = CreateSpinner(GetIdentifier() + "-" + id, 0, 0, width, kField);
        s->SetDecimals(decimals);
        s->SetSnapToStep(false);
        s->SetRange(minimum, maximum);
        s->SetStep(step);
        s->SetLayout(SpinnerLayout::UpDownRight);
        s->SetButtonThickness(14.0f);
        s->SetSuffix(suffix);
        s->SetTooltip(tooltip);
        s->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        s->onStepsFinished = [this](double) { ControlDone(); };
        AddChild(s);
        return s;
    };
    sizeBox = field("size", 84, 1, 2000, 1, 1, " pt", "Size");
    sizeBox->onValueChanged = [this](double v) {
        if (showing) return;
        format.size = static_cast<float>(v);
        Changed(TextFormatAttribute::Size);
    };
    rule();

    // ----- Bold, Italic, Underline, Strikethrough -----
    auto toggle = [this](const std::string& id, const std::string& icon, const std::string& tooltip,
                         std::function<void(bool)> set, TextFormatAttribute attribute) {
        auto b = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-" + id, 0, 0, kButton, kButton, "");
        b->SetIcon(IconPath(icon));
        b->SetIconSize(22, 22);
        b->SetUseIconAsMask(true);
        b->SetCanToggled(true);
        b->SetTooltip(tooltip);
        ButtonStyle st = b->GetStyle();
        st.cornerRadius = 6.0f;
        b->SetStyle(st);
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        b->onToggle = [this, set = std::move(set), attribute](bool on) {
            if (showing) return;
            set(on);
            Changed(attribute);
            ControlDone();
        };
        AddChild(b);
        return b;
    };
    boldButton = toggle("bold", "bold.svg", "Bold (Ctrl+B while typing)",
                        [this](bool on) { format.bold = on; }, TextFormatAttribute::Bold);
    italicButton = toggle("italic", "italic.svg", "Italic (Ctrl+I while typing)",
                          [this](bool on) { format.italic = on; }, TextFormatAttribute::Italic);
    underlineButton = toggle("underline", "underline.svg", "Underline (Ctrl+U while typing)",
                             [this](bool on) { format.underline = on; }, TextFormatAttribute::Underline);
    strikeButton = toggle("strike", "strike.svg", "Strikethrough",
                          [this](bool on) { format.strikethrough = on; }, TextFormatAttribute::Strikethrough);
    rule();

    // ----- Alignment -----
    alignSwitch = CreateSegmentedControl(GetIdentifier() + "-align", 0, 0, 108, kButton);
    SegmentedControlStyle alignStyle = alignSwitch->GetStyle();
    alignStyle.cornerRadius = 6.0f;
    alignStyle.iconAsMask = true;
    alignStyle.iconSize = 20;
    alignSwitch->SetStyle(alignStyle);
    alignSwitch->SetWidthMode(SegmentWidthMode::Equal);
    alignSwitch->AddSegment("", IconPath("align-left.svg"));
    alignSwitch->AddSegment("", IconPath("align-centre.svg"));
    alignSwitch->AddSegment("", IconPath("align-right.svg"));
    alignSwitch->SetSelectedIndex(0);
    alignSwitch->SetTooltip("Alignment: the lines start at, centre on or end at the text's point");
    alignSwitch->SetElementSize(Size2Df(108, kButton));
    alignSwitch->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    alignSwitch->onSegmentSelected = [this](int index) {
        if (showing) return;
        format.align = static_cast<TextFormatAlign>(std::clamp(index, 0, 2));
        Changed(TextFormatAttribute::Align);
        ControlDone();
    };
    AddChild(alignSwitch);
    rule();

    // ----- Line spacing and letter spacing -----
    auto icon = [this](const std::string& id, const std::string& file, const std::string& tooltip) {
        auto image = CreateImageFromFile(GetIdentifier() + "-icon-" + id, 0, 0, 16, 16, IconPath(file));
        image->SetTooltip(tooltip);
        image->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        AddChild(image);
        return image;
    };
    lineSpacingIcon = icon("line", "line-spacing.svg", "Line spacing");
    lineSpacingBox = field("line", 84, 10, 1000, 0, 5, " %",
                           "Line spacing: from one baseline to the next, in % of the size");
    lineSpacingBox->onValueChanged = [this](double v) {
        if (showing) return;
        format.lineSpacing = static_cast<float>(v);
        Changed(TextFormatAttribute::LineSpacing);
    };
    letterSpacingIcon = icon("letter", "letter-spacing.svg", "Letter spacing");
    letterSpacingBox = field("letter", 84, -100, 1000, 1, 0.5, " pt",
                             "Letter spacing (tracking): added between every two letters; below 0 sets them closer");
    letterSpacingBox->onValueChanged = [this](double v) {
        if (showing) return;
        format.letterSpacing = static_cast<float>(v);
        Changed(TextFormatAttribute::LetterSpacing);
    };

    ApplyTheme(UltraCanvasTheme::Current());
    ShowFormat();
}

void UltraCanvasTextFormatBar::RefreshFonts() {
    families = GetInstalledFontFamilies();
    // The generic names first: a text that names one finds it.
    for (const char* generic : { "Monospace", "Serif", "Sans" }) {
        if (std::find(families.begin(), families.end(), generic) == families.end())
            families.insert(families.begin(), generic);
    }
    missingFamily.clear();
    FillFontList();
    ShowFormat();
}

void UltraCanvasTextFormatBar::FillFontList() {
    if (!fontBox) return;
    showing = true;
    fontBox->ClearItems();
    // A font this computer lacks (a file from elsewhere) is named all the
    // same, at the top.
    if (!missingFamily.empty()) {
        DropdownItem item(missingFamily, missingFamily);
        item.fontFamily = missingFamily;
        item.tooltip = "Not installed on this computer: drawn in a substitute";
        fontBox->AddItem(item);
    }
    for (const auto& family : families) {
        DropdownItem item(family, family);
        item.fontFamily = family;          // shown in itself
        fontBox->AddItem(item);
    }
    fontBox->SetTooltip("Font (" + std::to_string(families.size()) + " installed) - type its first letters to find one");
    showing = false;
}

void UltraCanvasTextFormatBar::SetFormat(const TextFormat& f) {
    format = f;
    ShowFormat();
}

void UltraCanvasTextFormatBar::ShowFormat() {
    if (fontBox) {
        const std::string family = format.fontFamily.empty() ? std::string("Sans") : format.fontFamily;
        auto it = std::find(families.begin(), families.end(), family);
        if (it == families.end() && family != missingFamily) {
            missingFamily = family;
            FillFontList();
        }
        showing = true;
        const int offset = missingFamily.empty() ? 0 : 1;
        fontBox->SetSelectedIndex(it == families.end() ? 0 : static_cast<int>(it - families.begin()) + offset, false);
        showing = false;
    }
    showing = true;
    if (sizeBox) sizeBox->SetValue(format.size);
    if (boldButton) boldButton->SetPressed(format.bold);
    if (italicButton) italicButton->SetPressed(format.italic);
    if (underlineButton) underlineButton->SetPressed(format.underline);
    if (strikeButton) strikeButton->SetPressed(format.strikethrough);
    if (alignSwitch) alignSwitch->SetSelectedIndex(static_cast<int>(format.align));
    if (lineSpacingBox) lineSpacingBox->SetValue(format.lineSpacing);
    if (letterSpacingBox) letterSpacingBox->SetValue(format.letterSpacing);
    showing = false;
}

void UltraCanvasTextFormatBar::Changed(TextFormatAttribute attribute) {
    if (onFormatChanged) onFormatChanged(attribute, format);
}

void UltraCanvasTextFormatBar::ControlDone() {
    if (!onControlDone) return;
    std::weak_ptr<bool> token = alive;
    auto done = [this, token]() {
        auto still = token.lock();
        if (still && *still && onControlDone) onControlDone();
    };
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(done);
    else done();
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasTextFormatBar::ControlOf(TextFormatAttribute attribute) const {
    switch (attribute) {
        case TextFormatAttribute::Font: return fontBox;
        case TextFormatAttribute::Size: return sizeBox;
        case TextFormatAttribute::Bold: return boldButton;
        case TextFormatAttribute::Italic: return italicButton;
        case TextFormatAttribute::Underline: return underlineButton;
        case TextFormatAttribute::Strikethrough: return strikeButton;
        case TextFormatAttribute::Align: return alignSwitch;
        case TextFormatAttribute::LineSpacing: return lineSpacingBox;
        case TextFormatAttribute::LetterSpacing: return letterSpacingBox;
    }
    return nullptr;
}

void UltraCanvasTextFormatBar::SetAttributeVisible(TextFormatAttribute attribute, bool visible) {
    if (auto control = ControlOf(attribute)) control->SetVisible(visible);
    if (attribute == TextFormatAttribute::LineSpacing && lineSpacingIcon) lineSpacingIcon->SetVisible(visible);
    if (attribute == TextFormatAttribute::LetterSpacing && letterSpacingIcon) letterSpacingIcon->SetVisible(visible);
    // A rule only between two groups that both show something.
    const bool styles = IsAttributeVisible(TextFormatAttribute::Bold) || IsAttributeVisible(TextFormatAttribute::Italic) ||
                        IsAttributeVisible(TextFormatAttribute::Underline) || IsAttributeVisible(TextFormatAttribute::Strikethrough);
    const bool align = IsAttributeVisible(TextFormatAttribute::Align);
    const bool spacing = IsAttributeVisible(TextFormatAttribute::LineSpacing) || IsAttributeVisible(TextFormatAttribute::LetterSpacing);
    if (rules.size() == 3) {
        rules[0]->SetVisible(styles);
        rules[1]->SetVisible(align);
        rules[2]->SetVisible(spacing);
    }
    InvalidateLayout();
    RequestRedraw();
}

bool UltraCanvasTextFormatBar::IsAttributeVisible(TextFormatAttribute attribute) const {
    auto control = ControlOf(attribute);
    return control && control->IsVisible();
}

void UltraCanvasTextFormatBar::SetSizeUnit(const std::string& suffix, double minimum, double maximum,
                                           int decimals, double step) {
    if (sizeBox) {
        sizeBox->SetSuffix(suffix);
        sizeBox->SetRange(minimum, maximum);
        sizeBox->SetDecimals(decimals);
        sizeBox->SetStep(step);
    }
    if (!letterUnitOwn && letterSpacingBox) letterSpacingBox->SetSuffix(suffix);
    ShowFormat();
}

void UltraCanvasTextFormatBar::SetLetterSpacingUnit(const std::string& suffix, double minimum, double maximum,
                                                    int decimals, double step) {
    letterUnitOwn = true;
    if (letterSpacingBox) {
        letterSpacingBox->SetSuffix(suffix);
        letterSpacingBox->SetRange(minimum, maximum);
        letterSpacingBox->SetDecimals(decimals);
        letterSpacingBox->SetStep(step);
    }
    ShowFormat();
}

void UltraCanvasTextFormatBar::ApplyTheme(const UltraCanvasTheme& theme) {
    const ThemeRoles& r = theme.roles;
    SetBackgroundColor(r.panel);
    if (fontBox) fontBox->SetStyle(DropdownStyle::FromTheme(theme, fontBox->GetStyle()));
    for (auto* b : { boldButton.get(), italicButton.get(), underlineButton.get(), strikeButton.get() }) {
        if (!b) continue;
        ButtonStyle st = ButtonStyle::FromTheme(theme, b->GetStyle(), ButtonVariant::Standard);
        st.useIconAsMask = true;
        st.cornerRadius = 6.0f;
        b->SetStyle(st);
    }
    for (auto* s : { sizeBox.get(), lineSpacingBox.get(), letterSpacingBox.get() }) {
        if (!s) continue;
        SpinnerVisualStyle& st = s->GetStyle();
        st.backgroundColor = r.well;
        st.borderColor = r.line;
        st.focusBorderColor = r.focus;
        st.textColor = r.ink;
        st.caretColor = r.accent;
        st.buttonColor = r.raised;
        st.buttonHoverColor = r.hover;
        st.buttonPressedColor = r.pressed;
        st.buttonBorderColor = r.line;
        st.glyphColor = r.inkMuted;
        s->RequestRedraw();
    }
    if (alignSwitch) {
        SegmentedControlStyle st = alignSwitch->GetStyle();
        st.normalColor = r.raised;
        st.hoverColor = r.hover;
        st.selectedColor = r.accent;
        st.normalTextColor = r.ink;
        st.hoverTextColor = r.ink;
        st.selectedTextColor = r.onAccent;
        st.borderColor = r.line;
        st.separatorColor = r.line;
        alignSwitch->SetStyle(st);
    }
    for (auto* image : { lineSpacingIcon.get(), letterSpacingIcon.get() })
        if (image) image->SetTintColor(r.inkMuted);
    for (auto& rule : rules) rule->SetColor(r.line);
    RequestRedraw();
}

} // namespace UltraCanvas
