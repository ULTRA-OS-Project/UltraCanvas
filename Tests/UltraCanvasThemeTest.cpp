// Tests/UltraCanvasThemeTest.cpp
// The framework theme without a display: the two presets, element roles,
// the current theme, and the styles the timeline and the timecode field
// take from a theme - shared roles mapped, element roles honoured, the
// palette's colour where a theme is silent.
// Version: 1.11.0 - the containers and pickers: container, group box, breadcrumb, pagination, the colour picker, swatch bar
//                   and palette manager, the rich text edit
// Version: 1.10.0 - the list elements: the scrollbar, list view, tree view, columns tree view and filer widget
// Version: 1.9.0 - the input elements: checkbox, radio, segmented control, spinner, stepper, rating, the date and time pickers
// Version: 1.8.0 - following by default (SetFollowThemeByDefault), the SetStyle rule, fonts by role in the readers
// Version: 1.7.0 - fonts and text roles, the high-contrast presets, DeriveAccent and FromHost
// Version: 1.6.0 - the contrast the presets keep
// Version: 1.5.0 - the modal dialog style
// Version: 1.4.0 - the label variants
// Version: 1.3.0 - the button variants
// Version: 1.2.0 - the slider's and the switch's element roles
// Version: 1.1.0 - every FromTheme, the change handlers, elements that
//                  follow the theme through a container
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#include "UltraCanvasTheme.h"
#include "UltraCanvasHostAppearance.h"
#include "UltraCanvasAlbum.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasColorPaletteManager.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasPagination.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasDatePicker.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasRating.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasStepper.h"
#include "UltraCanvasTimePicker.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <cmath>
#include "UltraCanvasTimecodeField.h"
#ifndef UC_TEST_HAS_ALBUM
#define UC_TEST_HAS_ALBUM 1
#endif
#ifndef UC_TEST_HAS_VIDEO_TIMELINE
#define UC_TEST_HAS_VIDEO_TIMELINE 1
#endif
#include "UltraCanvasToolbar.h"
#include "UltraCanvasTooltipTypes.h"
#include "UltraCanvasVideoTimeline.h"

#include <cstdio>

using namespace UltraCanvas;

static int testCount = 0, failCount = 0;
static bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
// WCAG 2 relative luminance and contrast ratio
static double Luminance(const Color& c) {
    auto channel = [](int v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}
static double Contrast(const Color& a, const Color& b) {
    const double la = Luminance(a), lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

#define CHECK(cond, name) do { ++testCount; if (cond) std::printf("  PASS  %s\n", name); else { ++failCount; std::printf("  FAIL  %s (line %d)\n", name, __LINE__); } } while (0)

static bool Same(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

int main() {
    std::printf("UltraCanvasTheme\n");
    const UltraCanvasTheme light = UltraCanvasTheme::Light();
    const UltraCanvasTheme dark = UltraCanvasTheme::Dark();
    CHECK(!light.dark && dark.dark && light.name == "Light" && dark.name == "Dark", "the two presets");
    CHECK(Same(light.roles.ground, Colors::WindowBackground) && Same(light.roles.accent, Colors::Selection) && Same(light.roles.ink, Colors::TextDefault),
          "Light is the framework's default colours");
    CHECK(dark.roles.panel.r < 60 && dark.roles.ink.r > 200, "Dark is dark");
    CHECK(Same(UltraCanvasTheme::Current().roles.ground, light.roles.ground) && !UltraCanvasTheme::Current().dark, "Light until an app sets another");
    CHECK(Near(Contrast(Colors::White, Colors::Black), 21.0, 0.01) && Near(Contrast(Color(118, 118, 118), Colors::White), 4.54, 0.01),
          "the contrast measure: black on white 21:1, #767676 on white 4.54:1");
    for (const UltraCanvasTheme* t : {&light, &dark}) {   // the readability the presets promise (UltraCanvasTheme.md)
        const ThemeRoles& r = t->roles;
        const std::string n = t->name + ": ";
        const Color all[] = {r.ground, r.panel, r.well, r.raised, r.hover, r.pressed};
        bool inkOk = true, focusOk = true;
        for (const Color& s : all) { inkOk = inkOk && Contrast(r.ink, s) >= 4.5; focusOk = focusOk && Contrast(r.focus, s) >= 3.0; }
        CHECK(inkOk, (n + "ink reads 4.5:1 on every surface").c_str());
        CHECK(focusOk, (n + "the focus ring shows 3:1 on every surface").c_str());
        CHECK(Contrast(r.inkMuted, r.ground) >= 4.5 && Contrast(r.inkMuted, r.panel) >= 4.5 && Contrast(r.inkMuted, r.well) >= 4.5 &&
              Contrast(r.inkMuted, r.raised) >= 4.5 && Contrast(r.inkMuted, r.hover) >= 4.5,
              (n + "muted ink reads 4.5:1 down to hover").c_str());
        CHECK(Contrast(r.inkFaint, r.panel) >= 4.5 && Contrast(r.inkFaint, r.well) >= 4.5 && Contrast(r.inkFaint, r.raised) >= 4.5,
              (n + "faint ink reads 4.5:1 on panel, well and raised").c_str());
        CHECK(Contrast(r.lineStrong, r.panel) >= 3.0 && Contrast(r.lineStrong, r.raised) >= 3.0,
              (n + "a control border shows 3:1 on panel and raised").c_str());
        CHECK(Contrast(r.danger, r.panel) >= 4.5 && Contrast(r.onAccent, r.accent) >= 4.49,
              (n + "danger reads on the panel, text on the accent reads").c_str());
    }

    UltraCanvasTheme own = dark;
    own.name = "Own";
    CHECK(!own.HasRole("video-timeline.playhead") && Same(own.Role("video-timeline.playhead", Colors::Red), Colors::Red), "an unnamed element role falls back");
    own.SetRole("video-timeline.playhead", Color(1, 2, 3));
    own.SetRole("video-timeline.clip-video", Color(4, 5, 6));
    CHECK(own.HasRole("video-timeline.playhead") && Same(own.Role("video-timeline.playhead", Colors::Red), Color(1, 2, 3)) && own.ElementRoles().size() == 2,
          "a named element role is read");
    UltraCanvasTheme::SetCurrent(own);
    CHECK(UltraCanvasTheme::Current().name == "Own" && UltraCanvasTheme::Current().dark, "SetCurrent makes it the one in force");

    // The timeline is a selectable widget (ULTRACANVAS_WIDGETS): a lean core
    // that builds it as a runtime module has no VideoTimelineStyle to link.
#if UC_TEST_HAS_VIDEO_TIMELINE
    std::printf("VideoTimelineStyle::FromTheme\n");
    const VideoTimelineStyle fromOwn = VideoTimelineStyle::FromTheme(own);
    CHECK(Same(fromOwn.background, own.roles.panel) && Same(fromOwn.lane, own.roles.well) && Same(fromOwn.header, own.roles.raised) &&
          Same(fromOwn.headerSelected, own.roles.accentSoft) && Same(fromOwn.ink, own.roles.ink) && Same(fromOwn.focus, own.roles.focus),
          "the shared roles fill the surfaces, ink, accent and focus");
    CHECK(Same(fromOwn.playhead, Color(1, 2, 3)) && Same(fromOwn.clipVideo, Color(4, 5, 6)), "the element roles the theme names");
    const VideoTimelineStyle darkPalette = VideoTimelineStyle::DarkPalette();
    CHECK(Same(fromOwn.clipAudio, darkPalette.clipAudio) && Same(fromOwn.waveform, darkPalette.waveform) && Same(fromOwn.beat, darkPalette.beat),
          "the dark palette's colours where a dark theme is silent");
    CHECK(fromOwn.dropSoft.r == own.roles.focus.r && fromOwn.dropSoft.a == 31 && fromOwn.rangeFill.r == own.roles.accent.r && fromOwn.rangeFill.a == 36,
          "the drop and range tints are the focus and accent roles, translucent");
    const VideoTimelineStyle fromLight = VideoTimelineStyle::FromTheme(light);
    CHECK(Same(fromLight.clipVideo, VideoTimelineStyle::LightPalette().clipVideo) && Same(fromLight.background, Colors::White),
          "a light theme: the light palette's clip colours, the panel role behind the lanes");
    CHECK(Same(VideoTimelineStyle::FromTheme().playhead, Color(1, 2, 3)), "FromTheme() reads the current theme");
    CHECK(fromOwn.rulerHeight == darkPalette.rulerHeight && fromOwn.fontFamily == darkPalette.fontFamily, "sizes and fonts stay the defaults");

#endif

    std::printf("TimecodeFieldStyle::FromTheme\n");
    const TimecodeFieldStyle field = TimecodeFieldStyle::FromTheme(own);
    CHECK(Same(field.background, own.roles.well) && Same(field.border, own.roles.lineStrong) && Same(field.borderFocused, own.roles.focus) &&
          Same(field.borderInvalid, own.roles.danger), "the well, the border, the focus ring, danger");
    CHECK(Same(field.ink, own.roles.ink) && Same(field.inkFaint, own.roles.inkFaint) && Same(field.selection, own.roles.accent) &&
          Same(field.onSelection, own.roles.onAccent) && Same(field.sign, own.roles.accent), "ink, the selected segment and the sign from the accent");
    CHECK(field.fontFamily == TimecodeFieldStyle().fontFamily && field.fontSize == TimecodeFieldStyle().fontSize, "fonts stay the defaults");

    std::printf("FromTheme for the panel elements\n");
    const ThemeRoles& r = own.roles;
    const ButtonStyle button = ButtonStyle::FromTheme(own);
    CHECK(Same(button.normalColor, r.raised) && Same(button.hoverColor, r.hover) && Same(button.pressedColor, r.pressed) &&
          Same(button.disabledColor, r.panel) && Same(button.normalTextColor, r.ink) && Same(button.disabledTextColor, r.inkFaint) &&
          Same(button.borderColor, r.lineStrong) && Same(button.focusedColor, r.focus), "button: raised at rest, hover, pressed, ink, border, focus");
    CHECK(Same(button.iconMaskColor, Colors::Transparent), "button: icons keep following the text colour");
    const LabelStyle label = LabelStyle::FromTheme(own);
    CHECK(Same(label.textColor, r.ink) && Same(label.disabledTextColor, r.inkFaint) && label.selectionColor.r == r.accent.r && label.selectionColor.a == 100,
          "label: ink, faint ink, the accent behind a selection");
    LabelStyle labelBase;
    labelBase.lineHeightPx = 17.f;
    CHECK(Same(LabelStyle::FromTheme(own, labelBase, LabelVariant::Muted).textColor, r.inkMuted) &&
          Same(LabelStyle::FromTheme(own, labelBase, LabelVariant::Faint).textColor, r.inkFaint) &&
          Same(LabelStyle::FromTheme(own, labelBase, LabelVariant::Heading).textColor, r.ink) &&
          LabelStyle::FromTheme(own, labelBase, LabelVariant::Muted).lineHeightPx == 17.f,
          "label: Muted and Faint take the muted and faint ink, Heading ink; the base's shape stays");
    UltraCanvasTheme namedLabels = own;
    namedLabels.SetRole("label.heading", Color(14, 14, 14));
    namedLabels.SetRole("label.muted", Color(15, 15, 15));
    CHECK(Same(LabelStyle::FromTheme(namedLabels, labelBase, LabelVariant::Heading).textColor, Color(14, 14, 14)) &&
          Same(LabelStyle::FromTheme(namedLabels, labelBase, LabelVariant::Muted).textColor, Color(15, 15, 15)),
          "label: the variants' element roles where a theme names them");
    const DropdownStyle dropdown = DropdownStyle::FromTheme(own);
    CHECK(Same(dropdown.normalColor, r.raised) && Same(dropdown.listBackgroundColor, r.raised) && Same(dropdown.itemSelectedColor, r.accentSoft) &&
          Same(dropdown.checkboxCheckedColor, r.accent) && Same(dropdown.checkmarkColor, r.onAccent) && Same(dropdown.focusBorderColor, r.focus),
          "dropdown: the raised field and list, the accent tint and check");
    const SliderVisualStyle slider = SliderVisualStyle::FromTheme(own);
    CHECK(Same(slider.trackColor, r.pressed) && Same(slider.activeTrackColor, r.accent) && Same(slider.handleColor, r.raised) &&
          Same(slider.handleBorderColor, r.lineStrong) && Same(slider.textColor, r.ink), "slider: track, accent, raised handle, ink");
    const SwitchVisualStyle sw = SwitchVisualStyle::FromTheme(own);
    CHECK(Same(sw.trackOffColor, r.pressed) && Same(sw.trackOnColor, r.accent) && Same(sw.thumbColor, r.raised) && Same(sw.onTextColor, r.onAccent) &&
          Same(sw.base.textColor, r.ink) && Same(sw.sideLabelInactiveColor, r.inkFaint), "switch: track off and on, thumb, labels");
    UltraCanvasTheme named = own;
    named.SetRole("slider.handle", Color(7, 7, 7));
    named.SetRole("slider.handle-border", Color(8, 8, 8));
    named.SetRole("switch.thumb", Color(9, 9, 9));
    named.SetRole("switch.thumb-border", Color(10, 10, 10));
    CHECK(Same(SliderVisualStyle::FromTheme(named).handleColor, Color(7, 7, 7)) && Same(SliderVisualStyle::FromTheme(named).handleBorderColor, Color(8, 8, 8)) &&
          Same(SwitchVisualStyle::FromTheme(named).thumbColor, Color(9, 9, 9)) && Same(SwitchVisualStyle::FromTheme(named).thumbBorderColor, Color(10, 10, 10)),
          "slider and switch: the handle and thumb element roles where a theme names them");
    CHECK(Same(SliderVisualStyle::FromTheme(own).handleBorderColor, r.lineStrong) && Same(SwitchVisualStyle::FromTheme(own).thumbBorderColor, r.lineStrong),
          "slider and switch: the shared roles where it does not");
    const TextInputStyle input = TextInputStyle::FromTheme(own);
    CHECK(Same(input.backgroundColor, r.well) && Same(input.borderColor, r.lineStrong) && Same(input.focusBorderColor, r.focus) &&
          Same(input.textColor, r.ink) && Same(input.placeholderColor, r.inkFaint) && Same(input.invalidBorderColor, r.danger) &&
          Same(input.disabledBackgroundColor, r.panel), "text input: the well, borders, ink, placeholder, danger, disabled");
    CHECK(input.borderRadius == TextInputStyle::Default().borderRadius, "text input: the default's shape");
    TextInputStyle inputBase = TextInputStyle::Default();
    inputBase.borderRadius = 11;
    CHECK(TextInputStyle::FromTheme(own, inputBase).borderRadius == 11 && Same(TextInputStyle::FromTheme(own, inputBase).textColor, r.ink),
          "text input: over a base, its shape stays");
    ButtonStyle buttonBase;
    buttonBase.cornerRadius = 13;
    CHECK(ButtonStyle::FromTheme(own, buttonBase).cornerRadius == 13, "button: over a base, its shape stays");
    const ButtonStyle ghost = ButtonStyle::FromTheme(own, buttonBase, ButtonVariant::Ghost);
    CHECK(ghost.normalColor.a == 0 && Same(ghost.hoverColor, r.hover) && Same(ghost.pressedColor, r.pressed) && ghost.borderColor.a == 0 &&
          Same(ghost.normalTextColor, r.ink) && ghost.cornerRadius == 13, "button: Ghost has no fill at rest, the hover and pressed fills, ink");
    const ButtonStyle primary = ButtonStyle::FromTheme(own, buttonBase, ButtonVariant::Primary);
    CHECK(Same(primary.normalColor, r.accent) && Same(primary.hoverColor, r.accentHover) && Same(primary.normalTextColor, r.onAccent) &&
          primary.borderColor.a == 0, "button: Primary is the accent with text on it");
    const ButtonStyle selected = ButtonStyle::FromTheme(own, buttonBase, ButtonVariant::Selected);
    CHECK(Same(selected.normalColor, r.accentSoft) && Same(selected.hoverColor, r.accentSoft) && Same(selected.normalTextColor, r.accent),
          "button: Selected is the soft accent fill with the accent for ink");
    UltraCanvasTheme namedButtons = own;
    namedButtons.SetRole("button.primary", Color(11, 11, 11));
    namedButtons.SetRole("button.ghost-hover", Color(12, 12, 12));
    namedButtons.SetRole("button.selected", Color(13, 13, 13));
    CHECK(Same(ButtonStyle::FromTheme(namedButtons, buttonBase, ButtonVariant::Primary).normalColor, Color(11, 11, 11)) &&
          Same(ButtonStyle::FromTheme(namedButtons, buttonBase, ButtonVariant::Ghost).hoverColor, Color(12, 12, 12)) &&
          Same(ButtonStyle::FromTheme(namedButtons, buttonBase, ButtonVariant::Selected).normalColor, Color(13, 13, 13)),
          "button: the variants' element roles where a theme names them");
    const ToolbarAppearance toolbar = ToolbarAppearance::FromTheme(own);
    CHECK(Same(toolbar.backgroundColor, r.panel) && Same(toolbar.foregroundColor, r.ink) && Same(toolbar.separatorColor, r.line) &&
          Same(toolbar.hoverBackgroundColor, r.hover) && Same(toolbar.activeBackgroundColor, r.pressed) && Same(toolbar.disabledForegroundColor, r.inkFaint),
          "toolbar: the panel, ink, separators, hover, active, disabled");
    SplitPaneStyle paneBase;
    paneBase.splitterThickness = 9;
    const SplitPaneStyle pane = SplitPaneStyle::FromTheme(own, paneBase);
    CHECK(Same(pane.splitterColor, r.ground) && Same(pane.splitterHoverColor, r.lineStrong) && Same(pane.splitterActiveColor, r.accent) && pane.splitterThickness == 9,
          "split pane: ground, strong line, accent; the base's thickness");
#if UC_TEST_HAS_ALBUM   // a selectable widget (ULTRACANVAS_WIDGETS), absent from a lean core
    AlbumConfig albumBase;
    albumBase.mode = AlbumMode::UserEdit;
    const AlbumConfig album = AlbumConfig::FromTheme(own, albumBase);
    CHECK(Same(album.backgroundColor, r.panel) && Same(album.itemBackgroundColor, r.raised) && Same(album.borderColor, r.line) &&
          Same(album.captionColor, r.ink) && Same(album.subtitleColor, r.inkMuted) && Same(album.linkColor, r.accent) &&
          Same(album.selectionColor, r.accent) && album.mode == AlbumMode::UserEdit, "album: panel, raised tiles, hairlines, captions, accent; the base's mode");
#endif

    ModalDialogStyle dialogBase;
    dialogBase.buttonFontSize = 19.f;
    const ModalDialogStyle dialogStyle = ModalDialogStyle::FromTheme(own, dialogBase);
    CHECK(Same(dialogStyle.messageTextColor, r.ink) && Same(dialogStyle.detailsTextColor, r.inkMuted)
          && Near(dialogStyle.messageFontSize, own.FontSize(ThemeTextRole::Body), 1e-6) && dialogStyle.buttonFontSize == 19.f,
          "modal dialog: the message in ink at the body size, the details muted; the base's button size stays");

    std::printf("FromTheme for the input elements\n");
    {
        const double bodySize = own.FontSize(ThemeTextRole::Body);
        CheckboxVisualStyle cbBase;
        cbBase.boxSize = 20;
        cbBase.base.textSpacing = 9;
        const CheckboxVisualStyle cb = CheckboxVisualStyle::FromTheme(own, cbBase);
        CHECK(Same(cb.boxColor, r.well) && Same(cb.boxBorderColor, r.lineStrong) && Same(cb.boxHoverColor, r.hover) && Same(cb.boxPressedColor, r.pressed)
              && Same(cb.boxDisabledColor, r.panel) && Same(cb.boxBorderDisabledColor, r.line), "checkbox: the well with the control border, hover, pressed, the panel disabled");
        CHECK(Same(cb.checkmarkColor, r.accent) && Same(cb.checkmarkHoverColor, r.accent) && Same(cb.checkmarkDisabledColor, r.inkFaint)
              && Same(cb.base.textColor, r.ink) && Same(cb.base.textDisabledColor, r.inkFaint) && Same(cb.base.focusRingColor, r.focus.WithAlpha(128)),
              "checkbox: the mark in the accent, the label in ink, the focus ring");
        CHECK(cb.base.fontFamily == own.fonts.ui.fontFamily && Near(cb.base.fontSize, bodySize, 1e-6) && cb.boxSize == 20 && cb.base.textSpacing == 9,
              "checkbox: the body font; the base's box size and spacing stay");
        UltraCanvasTheme marked = own;
        marked.SetRole("checkbox.mark", Color(1, 2, 3));
        marked.SetRole("radio.dot", Color(4, 5, 6));
        CHECK(Same(CheckboxVisualStyle::FromTheme(marked).checkmarkColor, Color(1, 2, 3)) && Same(RadioVisualStyle::FromTheme(marked).innerDotColor, Color(4, 5, 6)),
              "the element roles checkbox.mark and radio.dot are honoured");
        const RadioVisualStyle rd = RadioVisualStyle::FromTheme(own);
        CHECK(Same(rd.outerColor, r.well) && Same(rd.outerBorderColor, r.lineStrong) && Same(rd.outerHoverColor, r.hover) && Same(rd.outerDisabledColor, r.panel)
              && Same(rd.innerDotColor, r.accent) && Same(rd.innerDotDisabledColor, r.inkFaint) && Same(rd.base.textColor, r.ink),
              "radio: the ring from the well, the dot in the accent, the label in ink");
        const SegmentedControlStyle sg = SegmentedControlStyle::FromTheme(own);
        CHECK(Same(sg.normalColor, r.raised) && Same(sg.selectedColor, r.accent) && Same(sg.hoverColor, r.hover) && Same(sg.disabledColor, r.panel)
              && Same(sg.normalTextColor, r.ink) && Same(sg.selectedTextColor, r.onAccent) && Same(sg.disabledTextColor, r.inkFaint)
              && Same(sg.borderColor, r.lineStrong) && Same(sg.separatorColor, r.line) && Near(sg.fontSize, bodySize, 1e-6),
              "segmented control: raised segments, the selected one in the accent, the body font");
        const SpinnerVisualStyle sp = SpinnerVisualStyle::FromTheme(own);
        CHECK(Same(sp.backgroundColor, r.well) && Same(sp.borderColor, r.lineStrong) && Same(sp.focusBorderColor, r.focus) && Same(sp.textColor, r.ink)
              && Same(sp.caretColor, r.ink) && Same(sp.disabledBackgroundColor, r.panel) && Same(sp.disabledTextColor, r.inkFaint),
              "spinner: the field from the well, the focus ring, ink");
        CHECK(Same(sp.buttonColor, r.raised) && Same(sp.buttonHoverColor, r.hover) && Same(sp.buttonPressedColor, r.pressed) && Same(sp.buttonBorderColor, r.line)
              && Same(sp.glyphColor, r.inkMuted) && Same(sp.glyphDisabledColor, r.inkFaint) && Near(sp.fontStyle.fontSize, bodySize, 1e-9),
              "spinner: raised step buttons with muted glyphs, the body font");
        const StepperStyle st = StepperStyle::FromTheme(own);
        CHECK(Same(st.completedColor, r.accent) && Same(st.activeColor, r.accent) && Same(st.upcomingColor, r.lineStrong) && Same(st.errorColor, r.danger)
              && Same(st.markerBackground, r.raised) && Same(st.completedGlyphColor, r.onAccent) && Same(st.upcomingGlyphColor, r.inkMuted)
              && Same(st.connectorColor, r.line) && Same(st.connectorCompletedColor, r.accent), "stepper: markers and connectors from the accent, danger for an error");
        CHECK(Same(st.titleColor, r.ink) && Same(st.activeTitleColor, r.accent) && Same(st.descriptionColor, r.inkMuted) && Same(st.disabledTextColor, r.inkFaint)
              && Near(st.titleFontSize, bodySize, 1e-6) && Near(st.descriptionFontSize, own.FontSize(ThemeTextRole::Caption), 1e-6)
              && Same(st.errorGlyphColor, StepperStyle().errorGlyphColor), "stepper: titles at the body size, descriptions at the caption size, the error glyph its own");
        RatingStyle rtBase;
        rtBase.symbolSize = 40;
        const RatingStyle rt = RatingStyle::FromTheme(own, rtBase);
        CHECK(Same(rt.offColor, r.lineStrong) && Same(rt.onColor, RatingStyle().onColor) && Same(rt.hoverColor, RatingStyle().hoverColor) && rt.symbolSize == 40
              && Near(rt.fontStyle.fontSize, bodySize, 1e-9), "rating: empty symbols from the control border, the amber kept, the base's size");
        UltraCanvasTheme starred = own;
        starred.SetRole("rating.on", Color(7, 8, 9));
        CHECK(Same(RatingStyle::FromTheme(starred).onColor, Color(7, 8, 9)), "the element role rating.on is honoured");
        const CalendarStyle cal = CalendarStyle::FromTheme(own);
        CHECK(Same(cal.backgroundColor, r.raised) && Same(cal.borderColor, r.line) && Same(cal.headerTextColor, r.ink) && Same(cal.navArrowColor, r.inkMuted)
              && Same(cal.weekdayTextColor, r.inkMuted) && Same(cal.cellTextColor, r.ink) && Same(cal.adjacentMonthColor, r.inkFaint) && Same(cal.hoverColor, r.hover),
              "calendar: the raised panel, ink for the cells, muted and faint ink around them");
        CHECK(Same(cal.selectedColor, r.accent) && Same(cal.selectedTextColor, r.onAccent) && Same(cal.todayBorderColor, r.accent) && Same(cal.focusBorderColor, r.focus)
              && Same(cal.rangeFillColor, r.accentSoft) && Same(cal.blockedStrikeColor, r.danger.WithAlpha(200)) && Same(cal.footerTextColor, r.accent)
              && Same(cal.weekendTextColor, CalendarStyle().weekendTextColor) && Near(cal.fontSize, bodySize, 1e-6) && Near(cal.headerFontSize, bodySize, 1e-6),
              "calendar: the selection, today and the footer from the accent, the weekend colour kept, the body font");
        DatePickerStyle dpBase;
        dpBase.calendarStyle.cellWidth = 50;
        const DatePickerStyle dp = DatePickerStyle::FromTheme(own, dpBase);
        CHECK(Same(dp.backgroundColor, r.well) && Same(dp.borderColor, r.lineStrong) && Same(dp.focusBorderColor, r.focus) && Same(dp.textColor, r.ink)
              && Same(dp.placeholderColor, r.inkFaint) && Same(dp.iconColor, r.inkMuted) && Same(dp.caretColor, r.ink) && Same(dp.disabledColor, r.panel)
              && Same(dp.calendarStyle.selectedColor, r.accent) && dp.calendarStyle.cellWidth == 50 && Near(dp.fontSize, bodySize, 1e-6),
              "date picker: the field from the well, its calendar from the theme over the base's metrics");
        const TimePickerStyle tp = TimePickerStyle::FromTheme(own);
        CHECK(Same(tp.backgroundColor, r.well) && Same(tp.borderColor, r.lineStrong) && Same(tp.textColor, r.ink) && Same(tp.popupBackgroundColor, r.raised)
              && Same(tp.popupBorderColor, r.line) && Same(tp.separatorTextColor, r.inkMuted) && Same(tp.footerTextColor, r.accent),
              "time picker: the field from the well, the popup raised with a hairline");
        CHECK(Same(tp.clockHeaderColor, r.accent) && Same(tp.clockHeaderTextColor, r.onAccent) && Same(tp.clockFaceColor, r.pressed) && Same(tp.clockNumberColor, r.ink)
              && Same(tp.clockSelectionColor, r.accent) && Same(tp.clockSelectionTextColor, r.onAccent) && tp.clockHeaderFontSize == TimePickerStyle().clockHeaderFontSize
              && Near(tp.fontSize, bodySize, 1e-6), "time picker: the clock's header and selection in the accent, its face the pressed fill, the header size kept");
    }

    std::printf("FromTheme for the list elements\n");
    {
        ScrollbarStyle sbBase = ScrollbarStyle::Minimal();
        const ScrollbarStyle sb = ScrollbarStyle::FromTheme(own, sbBase);
        CHECK(sb.trackColor.r == r.panel.r && sb.trackColor.a == sbBase.trackColor.a && sb.thumbColor.r == r.lineStrong.r && sb.thumbColor.a == sbBase.thumbColor.a
              && Same(sb.trackBorderColor, r.line) && Same(sb.thumbBorderColor, r.lineStrong) && sb.thumbHoverColor.r == r.inkFaint.r && sb.thumbPressedColor.r == r.inkMuted.r,
              "scrollbar: the panel for the track and the control border for the thumb at the base's alpha, faint and muted ink hovered and pressed");
        CHECK(Same(sb.arrowColor, r.inkMuted) && Same(sb.arrowHoverColor, r.ink) && Same(sb.arrowBackgroundColor, r.panel) && Same(sb.arrowBackgroundHoverColor, r.hover)
              && sb.trackSize == sbBase.trackSize && sb.thumbCornerRadius == sbBase.thumbCornerRadius, "scrollbar: the arrows in muted ink on the panel; the base's sizes stay");
        UltraCanvasTheme barred = own;
        barred.SetRole("scrollbar.thumb", Color(9, 9, 9));
        CHECK(Same(ScrollbarStyle::FromTheme(barred).thumbColor, Color(9, 9, 9, 255)), "the element role scrollbar.thumb is honoured");
        ListViewStyle lvBase;
        lvBase.rowHeight = 30;
        const ListViewStyle lv = ListViewStyle::FromTheme(own, lvBase);
        CHECK(Same(lv.backgroundColor, r.well) && Same(lv.headerBackgroundColor, r.panel) && Same(lv.headerTextColor, r.ink) && Same(lv.gridLineColor, r.line)
              && Same(lv.alternateRowColor, r.panel) && Same(lv.selectionBackgroundColor, r.accent) && Same(lv.hoverBackgroundColor, r.hover),
              "list view: the well, the panel header with ink, hairlines, the accent selection");
        CHECK(Near(lv.headerFontSize, own.FontSize(ThemeTextRole::Caption), 1e-6) && lv.rowHeight == 30 && Same(lv.scrollbarStyle.trackBorderColor, r.line),
              "list view: the header at the caption size, the base's row height, its scrollbar from the theme");
        TreeColumnStyle tcBase;
        tcBase.columnGap = 14;
        const TreeColumnStyle tc = TreeColumnStyle::FromTheme(own, tcBase);
        CHECK(Same(tc.columnSeparatorColor, r.line) && Same(tc.headerBackground, r.panel) && Same(tc.headerTextColor, r.ink) && Same(tc.headerBorderColor, r.line)
              && Same(tc.groupHeaderBackground, TreeColumnStyle().groupHeaderBackground) && tc.columnGap == 14,
              "columns tree: hairline separators, the panel header, the section header's bar kept, the base's gap");
        UltraCanvasTheme grouped = own;
        grouped.SetRole("columns-tree.group-header", Color(3, 3, 3));
        CHECK(Same(TreeColumnStyle::FromTheme(grouped).groupHeaderBackground, Color(3, 3, 3, 255)), "the element role columns-tree.group-header is honoured");
        FilerStyle fsBase;
        fsBase.tileGap = 16;
        const FilerStyle fs = FilerStyle::FromTheme(own, fsBase);
        CHECK(Same(fs.backgroundColor, r.well) && Same(fs.textColor, r.ink) && Same(fs.secondaryTextColor, r.inkMuted) && Same(fs.headerBackground, r.panel)
              && Same(fs.gridLineColor, r.line) && Same(fs.hoverColor, r.hover) && Same(fs.selectionColor, r.accentSoft) && Same(fs.selectionBorderColor, r.accent),
              "filer: the well, ink and muted ink, the panel header, hairlines, the soft accent selection with the accent round it");
        CHECK(Same(fs.barColor, r.accent) && Same(fs.barBackground, r.panel) && Same(fs.loadingBarColor, r.accent) && Same(fs.renameFieldColor, r.well)
              && Same(fs.renameBorderColor, r.accent) && Same(fs.renameTextColor, r.ink) && Same(fs.infoBarBackground, r.panel) && Same(fs.infoBarTextColor, r.ink),
              "filer: the bars and the rename editor from the accent, the info bar on the panel");
        CHECK(Same(fs.iconMenuBackground, FilerStyle().iconMenuBackground) && Same(fs.extensionTagBackground, FilerStyle().extensionTagBackground)
              && fs.fontFamily == own.fonts.ui.fontFamily && Near(fs.fontSize, own.FontSize(ThemeTextRole::Body), 1e-6)
              && Near(fs.smallFontSize, own.FontSize(ThemeTextRole::Caption), 1e-6) && fs.tileGap == 16,
              "filer: the overlays kept, the interface font at the body and caption sizes, the base's gap");
    }

    std::printf("FromTheme for the containers and pickers\n");
    {
        const double bodySize = own.FontSize(ThemeTextRole::Body);
        ContainerStyle csBase;
        csBase.autoShowScrollbars = true;
        const ContainerStyle cs = ContainerStyle::FromTheme(own, csBase);
        CHECK(cs.autoShowScrollbars && Same(cs.scrollbarStyle.trackBorderColor, r.line) && Same(cs.scrollbarStyle.thumbBorderColor, r.lineStrong),
              "container: the scrollbars from the theme, the base's flags kept");
        const GroupBoxVisualStyle gb = GroupBoxVisualStyle::FromTheme(own);
        CHECK(gb.backgroundColor.a == 0 && Same(gb.borderColor, r.line) && Same(gb.titleColor, r.ink) && Same(gb.disabledTitleColor, r.inkFaint)
              && Same(gb.activatorOnColor, r.accent) && Same(gb.activatorOffColor, r.lineStrong) && Same(gb.activatorKnobColor, r.raised)
              && Same(gb.infoIconColor, r.inkMuted) && Same(gb.closeIconHoverColor, r.ink) && Same(gb.closeHoverBackgroundColor, r.hover)
              && Near(gb.titleFont.fontSize, bodySize, 1e-9), "group box: a transparent frame stays so, hairline, ink caption at the body size, the activator from the accent");
        const GroupBoxVisualStyle card = GroupBoxVisualStyle::FromTheme(own, GroupBoxVisualStyle::SectionCard());
        CHECK(Same(card.backgroundColor, r.raised) && card.cornerRadius == GroupBoxVisualStyle::SectionCard().cornerRadius
              && card.titleFont.fontWeight == GroupBoxVisualStyle::SectionCard().titleFont.fontWeight,
              "group box: a filled card takes the raised surface, its radius and bold caption kept");
        const BreadcrumbStyle bc = BreadcrumbStyle::FromTheme(own);
        CHECK(bc.backgroundColor.a == 0 && Same(bc.itemTextColor, r.inkMuted) && Same(bc.itemHoverTextColor, r.accent) && Same(bc.currentItemTextColor, r.ink)
              && Same(bc.disabledItemTextColor, r.inkFaint) && Same(bc.itemHoverBackgroundColor, r.hover) && Same(bc.separatorColor, r.inkFaint)
              && Same(bc.levelIndicatorColor, r.accent) && Same(bc.levelIndicatorTextColor, r.onAccent) && Near(bc.fontStyle.fontSize, bodySize, 1e-9),
              "breadcrumb: muted items, the accent hovered, ink for the current one, the level badge in the accent, the body font");
        const BreadcrumbStyle pills = BreadcrumbStyle::FromTheme(own, BreadcrumbStyle::Pills());
        CHECK(pills.itemStyle == BreadcrumbStyle::Pills().itemStyle && (BreadcrumbStyle::Pills().currentItemBackgroundColor.a == 0 || Same(pills.currentItemBackgroundColor, r.accentSoft)),
              "breadcrumb: the Pills shape kept, a filled current item on the soft accent");
        const PaginationStyle pg = PaginationStyle::FromTheme(own);
        CHECK(Same(pg.cellColor, r.raised) && Same(pg.cellHoverColor, r.hover) && Same(pg.cellPressedColor, r.pressed) && Same(pg.cellBorderColor, r.lineStrong)
              && Same(pg.cellTextColor, r.ink) && Same(pg.currentColor, r.accent) && Same(pg.currentTextColor, r.onAccent) && Same(pg.disabledCellColor, r.panel)
              && Same(pg.disabledTextColor, r.inkFaint) && Same(pg.navGlyphColor, r.inkMuted) && Near(pg.fontStyle.fontSize, bodySize, 1e-9),
              "pagination: raised cells, the current page in the accent, the body font");
        const ColorPickerStyle cp = ColorPickerStyle::FromTheme(own);
        CHECK(Same(cp.backgroundColor, r.panel) && Same(cp.panelColor, r.raised) && Same(cp.borderColor, r.line) && Same(cp.textColor, r.ink)
              && Same(cp.mutedTextColor, r.inkMuted) && Same(cp.fieldColor, r.well) && Same(cp.fieldBorderColor, r.lineStrong) && Same(cp.accentColor, r.accent)
              && Same(cp.markerColor, ColorPickerStyle::Light().markerColor) && Near(cp.fontSize, bodySize, 1e-6) && cp.fontFamily == own.fonts.ui.fontFamily,
              "colour picker: the panel surface, raised panels, fields from the well, the accent, the markers kept, the body font");
        CHECK(ColorPickerStyle::FromTheme(dark).uiScale == ColorPickerStyle::Dark().uiScale && Same(ColorPickerStyle::FromTheme(dark).backgroundColor, dark.roles.panel),
              "colour picker without a base: Dark() under a dark theme, the roles over it");
        const ColorSwatchBarStyle sw = ColorSwatchBarStyle::FromTheme(own);
        CHECK(sw.background.a == 0 && Same(sw.border, r.lineStrong) && Same(sw.hoverBorder, r.inkMuted) && Same(sw.selectedBorder, r.accent)
              && Same(sw.checkerDark, ColorSwatchBarStyle().checkerDark), "swatch bar: the control border, muted ink hovered, the accent selected, the checkerboard kept");
        ColorPaletteManagerStyle pmBase;
        pmBase.swatchSize = 20;
        const ColorPaletteManagerStyle pm = ColorPaletteManagerStyle::FromTheme(own, pmBase);
        CHECK(Same(pm.swatchBorder, r.line) && Same(pm.swatchSelectedBorder, r.accent) && Same(pm.buttonColor, r.raised) && Same(pm.buttonText, r.ink)
              && Same(pm.accentColor, r.accent) && Same(pm.accentHoverColor, r.accentHover) && Same(pm.popupBackground, r.raised) && Same(pm.cardBackground, r.panel)
              && Same(pm.titleText, r.ink) && Same(pm.mutedText, r.inkMuted) && Near(pm.fontSize, own.FontSize(ThemeTextRole::Caption), 1e-6) && pm.swatchSize == 20
              && Same(pm.editorPickerStyle.fieldColor, r.well), "palette manager: hairlines and the accent, raised buttons and popups, the caption size, its editor's picker from the theme");
        RichTextEditStyle rtBase;
        rtBase.baseFont.fontFamily = "Georgia";
        rtBase.baseFont.fontSize = 16;
        const RichTextEditStyle rt = RichTextEditStyle::FromTheme(own, rtBase);
        CHECK(Same(rt.textColor, r.ink) && Same(rt.backgroundColor, r.well) && Same(rt.pageColor, r.well) && Same(rt.deskColor, r.panel) && Same(rt.selectionColor, r.accentSoft)
              && Same(rt.cursorColor, r.ink) && Same(rt.linkColor, r.accent) && Same(rt.quoteTextColor, r.inkMuted) && Same(rt.codeBackgroundColor, r.panel)
              && Same(rt.tableBorderColor, r.lineStrong) && Same(rt.borderColor, r.lineStrong), "rich text edit: ink on the well, the page the well, the desk the panel, the accent for the selection and links");
        CHECK(rt.baseFont.fontFamily == "Georgia" && Near(rt.baseFont.fontSize, 16, 1e-9) && Same(rt.commentHighlightColor, RichTextEditStyle().commentHighlightColor)
              && Same(rt.insertionColor, RichTextEditStyle().insertionColor), "rich text edit: the document's font and the comment and tracked-change colours stay");
        UltraCanvasTheme paper = own;
        paper.SetRole("rich-text.page", Colors::White);
        CHECK(Same(RichTextEditStyle::FromTheme(paper).pageColor, Colors::White), "the element role rich-text.page is honoured");
    }

    std::printf("Menu and tooltip from the roles\n");
    const MenuStyle menu = MenuStyle::FromTheme(own);
    CHECK(Same(menu.backgroundColor, r.raised) && Same(menu.borderColor, r.line) && Same(menu.hoverColor, r.hover) && Same(menu.textColor, r.ink) &&
          Same(menu.shortcutColor, r.inkMuted) && Same(menu.disabledTextColor, r.inkFaint) && Same(menu.selectedColor, r.accentSoft),
          "menu: the raised panel, hairlines, hover, ink, shortcuts, disabled, selected");
    CHECK(menu.itemHeight == MenuStyle::Default().itemHeight && menu.borderRadius == MenuStyle::Default().borderRadius, "menu: Default's shape");
    const TooltipStyle tip = TooltipStyle::FromTheme(own);
    CHECK(tip.backgroundColor.r == r.raised.r && tip.backgroundColor.a == TooltipStyle::Dark().backgroundColor.a && Same(tip.textColor, r.ink) &&
          Same(tip.secondaryTextColor, r.inkMuted) && Same(tip.borderColor, r.line), "tooltip: the raised panel at Dark's translucency, ink, muted ink");
    CHECK(Same(TooltipStyle::FromTheme(light).textColor, light.roles.ink) && TooltipStyle::FromTheme(light).backgroundColor.a == TooltipStyle::Light().backgroundColor.a,
          "tooltip: a light theme starts from Light()");

    std::printf("change handlers\n");
    int calls = 0;
    std::string seen;
    const int handler = UltraCanvasTheme::AddChangeHandler([&](const UltraCanvasTheme& t) { ++calls; seen = t.name; });
    UltraCanvasTheme::SetCurrent(dark);
    CHECK(calls == 1 && seen == "Dark", "SetCurrent calls the handler with the new theme");
    UltraCanvasTheme::RemoveChangeHandler(handler);
    UltraCanvasTheme::SetCurrent(light);
    CHECK(calls == 1, "a removed handler is not called");
    int inner = 0;
    const int outer = UltraCanvasTheme::AddChangeHandler([&](const UltraCanvasTheme&) {
        UltraCanvasTheme::AddChangeHandler([&](const UltraCanvasTheme&) { ++inner; });
    });
    UltraCanvasTheme::SetCurrent(dark);
    CHECK(inner == 0, "a handler added during a change is not called for that change");
    UltraCanvasTheme::RemoveChangeHandler(outer);
    UltraCanvasTheme::SetCurrent(light);
    CHECK(inner == 1, "and is called for the next");

    std::printf("elements that follow the theme\n");
    auto panel = std::make_shared<UltraCanvasContainer>("panel", 0, 0, 200, 100);
    auto follower = std::make_shared<UltraCanvasButton>("follow", 0, 0, 80, 24, "Follow");
    auto styled = std::make_shared<UltraCanvasButton>("styled", 0, 0, 80, 24, "Styled");
    auto text = std::make_shared<UltraCanvasLabel>("text", 0, 0, 80, 24, "Text");
    panel->AddChild(follower);
    panel->AddChild(styled);
    panel->AddChild(text);
    const Color ownRest = Color(9, 8, 7);
    ButtonStyle byHand;
    byHand.normalColor = ownRest;
    styled->SetStyle(byHand);
    CHECK(!follower->GetFollowTheme() && !Same(follower->GetStyle().normalColor, light.roles.raised), "an element does not follow the theme by default");
    follower->SetFollowTheme(true);
    text->SetFollowTheme(true);
    CHECK(follower->GetFollowTheme() && Same(follower->GetStyle().normalColor, light.roles.raised) && Same(text->GetStyle().textColor, light.roles.ink),
          "SetFollowTheme(true) styles the element from the current theme at once");
    panel->OnThemeChanged(dark);
    CHECK(Same(follower->GetStyle().normalColor, dark.roles.raised) && Same(follower->GetStyle().normalTextColor, dark.roles.ink) &&
          Same(text->GetStyle().textColor, dark.roles.ink), "a theme change reaches the children through the container");
    CHECK(Same(styled->GetStyle().normalColor, ownRest), "an element styled by hand keeps its look");
    auto tool = std::make_shared<UltraCanvasButton>("tool", 0, 0, 24, 24, "");
    tool->SetThemeVariant(ButtonVariant::Ghost);
    tool->SetFollowTheme(true);
    UltraCanvasTheme::SetCurrent(dark);
    tool->SetThemeVariant(ButtonVariant::Selected);
    CHECK(tool->GetThemeVariant() == ButtonVariant::Selected && Same(tool->GetStyle().normalColor, dark.roles.accentSoft),
          "SetThemeVariant re-styles a following button at once");
    tool->SetThemeVariant(ButtonVariant::Ghost);
    CHECK(tool->GetStyle().normalColor.a == 0 && Same(tool->GetStyle().hoverColor, dark.roles.hover), "and back to a ghost");
    auto note = std::make_shared<UltraCanvasLabel>("note", 0, 0, 80, 24, "Note");
    note->SetFollowTheme(true);
    note->SetThemeVariant(LabelVariant::Muted);
    CHECK(note->GetThemeVariant() == LabelVariant::Muted && Same(note->GetStyle().textColor, dark.roles.inkMuted),
          "SetThemeVariant re-styles a following label at once");
    UltraCanvasTheme::SetCurrent(light);
    follower->SetFollowTheme(false);
    panel->OnThemeChanged(light);
    CHECK(Same(follower->GetStyle().normalColor, dark.roles.raised) && Same(text->GetStyle().textColor, light.roles.ink),
          "SetFollowTheme(false) stops the re-styling");
    panel->ClearChildren();

    std::printf("following by default\n");
    {
        CHECK(!UltraCanvasUIElement::FollowThemeByDefault(), "the default is off until a policy sets it");
        UltraCanvasTheme::SetCurrent(dark);
        UltraCanvasUIElement::SetFollowThemeByDefault(true);
        auto fresh = std::make_shared<UltraCanvasButton>("fresh", 0, 0, 80, 24, "Fresh");
        CHECK(fresh->GetFollowTheme() && Same(fresh->GetStyle().normalColor, dark.roles.raised) && Same(fresh->GetStyle().normalTextColor, dark.roles.ink),
              "a new element follows and is styled from the current theme at once");
        CHECK(fresh->GetStyle().fontFamily == dark.fonts.ui.fontFamily && Near(fresh->GetStyle().fontSize, dark.FontSize(ThemeTextRole::Body), 1e-6),
              "with the theme's interface font at the body size");
        auto freshText = std::make_shared<UltraCanvasLabel>("fresh-text", 0, 0, 80, 24, "Text");
        CHECK(freshText->GetFollowTheme() && Same(freshText->GetStyle().textColor, dark.roles.ink)
              && Near(freshText->GetStyle().fontStyle.fontSize, dark.FontSize(ThemeTextRole::Body), 1e-9), "a new label too");
        auto freshMenu = std::make_shared<UltraCanvasMenu>("fresh-menu");
        CHECK(freshMenu->GetFollowTheme() && Same(freshMenu->GetStyle().backgroundColor, dark.roles.raised)
              && Near(freshMenu->GetStyle().font.fontSize, dark.FontSize(ThemeTextRole::Body), 1e-9), "a new menu: Dark() with the roles and the body font");
        auto freshInput = std::make_shared<UltraCanvasTextInput>("fresh-input", 0, 0, 120, 24);
        auto freshSlider = std::make_shared<UltraCanvasSlider>("fresh-slider", 0, 0, 120, 24);
        auto freshSwitch = std::make_shared<UltraCanvasSwitch>("fresh-switch", 0, 0, 120, 24, "On");
        auto freshPick = std::make_shared<UltraCanvasDropdown>("fresh-pick", 0, 0, 120, 24);
        CHECK(Same(freshInput->GetStyle().backgroundColor, dark.roles.well) && Same(freshSlider->GetStyle().activeTrackColor, dark.roles.accent)
              && Same(freshSwitch->GetVisualStyle().trackOnColor, dark.roles.accent) && Same(freshPick->GetStyle().normalColor, dark.roles.raised),
              "a new text input, slider, switch and dropdown are styled from the theme");
        auto freshBox = std::make_shared<UltraCanvasCheckbox>("fresh-box", "Box");
        auto freshRadio = std::make_shared<UltraCanvasRadio>("fresh-radio", 0, 0, 100, 24, "Radio");
        auto freshSeg = std::make_shared<UltraCanvasSegmentedControl>("fresh-seg", 0, 0, 200, 24);
        auto freshSpin = std::make_shared<UltraCanvasSpinner>("fresh-spin", 0, 0, 100, 24);
        auto freshStep = std::make_shared<UltraCanvasStepper>("fresh-step", 0, 0, 300, 60);
        auto freshStars = std::make_shared<UltraCanvasRating>("fresh-stars", 0, 0, 160, 32);
        auto freshCal = std::make_shared<UltraCanvasCalendarView>("fresh-cal");
        auto freshDate = std::make_shared<UltraCanvasDatePicker>("fresh-date");
        auto freshTime = std::make_shared<UltraCanvasTimePicker>("fresh-time");
        auto freshClock = std::make_shared<UltraCanvasTimeClockView>("fresh-clock", 0, 0, 200, 200);
        CHECK(freshBox->GetFollowTheme() && Same(freshBox->GetVisualStyle().boxColor, dark.roles.well) && Same(freshRadio->GetVisualStyle().innerDotColor, dark.roles.accent)
              && Same(freshSeg->GetStyle().selectedColor, dark.roles.accent) && Same(freshSpin->GetStyle().backgroundColor, dark.roles.well)
              && Same(freshStep->GetStyle().activeColor, dark.roles.accent) && Same(freshStars->GetStyle().offColor, dark.roles.lineStrong),
              "a new checkbox, radio, segmented control, spinner, stepper and rating are styled from the theme");
        CHECK(freshCal->GetFollowTheme() && Same(freshCal->GetStyle().cellTextColor, dark.roles.ink) && Same(freshDate->GetStyle().backgroundColor, dark.roles.well)
              && Same(freshDate->GetCalendar()->GetStyle().selectedColor, dark.roles.accent) && Same(freshTime->GetStyle().clockHeaderColor, dark.roles.accent)
              && Same(freshClock->GetStyle().clockNumberColor, dark.roles.ink), "so are a new calendar, date picker (its calendar with it), time picker and clock view");
        freshBox->SetVisualStyle(CheckboxVisualStyle());
        freshSeg->SetStyle(SegmentedControlStyle());
        freshSpin->SetStyle(SpinnerVisualStyle());
        freshDate->SetStyle(DatePickerStyle());
        CHECK(!freshBox->GetFollowTheme() && !freshSeg->GetFollowTheme() && !freshSpin->GetFollowTheme() && !freshDate->GetFollowTheme(),
              "a hand-set style stops their following too");
        auto freshBar = std::make_shared<UltraCanvasScrollbar>("fresh-bar", 0, 0, 12, 100, ScrollbarOrientation::Vertical);
        auto freshList = std::make_shared<UltraCanvasListView>("fresh-list", 0, 0, 200, 100);
        auto freshTree = std::make_shared<UltraCanvasTreeView>("fresh-tree", 0, 0, 200, 100);
        auto freshCols = std::make_shared<UltraCanvasColumnsTreeView>("fresh-cols", 0, 0, 200, 100);
        CHECK(freshBar->GetFollowTheme() && Same(freshBar->GetStyle().trackBorderColor, dark.roles.line) && Same(freshList->GetStyle().backgroundColor, dark.roles.well)
              && Same(freshTree->GetTextColor(), dark.roles.ink) && Same(freshTree->GetSelectionColor(), dark.roles.accent)
              && Same(freshTree->GetSelectionTextColor(), dark.roles.onAccent) && Same(freshTree->GetVerticalScrollbarStyle().trackBorderColor, dark.roles.line),
              "a new scrollbar, list view and tree view are styled from the theme, the tree's selected text on the accent, its scrollbar with it");
        UltraCanvasUIElement::SetFollowThemeByDefault(false);
        CHECK(UltraCanvasTreeView("plain").GetSelectionTextColor().a == 0, "a tree that does not follow keeps the row's own text colour on a selected row");
        UltraCanvasUIElement::SetFollowThemeByDefault(true);
        auto freshPanel = std::make_shared<UltraCanvasContainer>("fresh-panel", 0, 0, 200, 100);
        auto freshGroup = std::make_shared<UltraCanvasGroupBox>("fresh-group", 0, 0, 200, 100, "Group");
        auto freshCrumbs = std::make_shared<UltraCanvasBreadcrumb>("fresh-crumbs", 0, 0, 300, 24);
        auto freshPages = std::make_shared<UltraCanvasPagination>("fresh-pages", 0, 0, 300, 32);
        auto freshPicker = std::make_shared<UltraCanvasColorPicker>("fresh-picker", 0, 0, 260, 300);
        auto freshSwatches = std::make_shared<UltraCanvasColorSwatchBar>("fresh-swatches", 0, 0, 300, 28);
        auto freshRich = std::make_shared<UltraCanvasRichTextEdit>("fresh-rich", 0, 0, 300, 200);
        CHECK(freshPanel->GetFollowTheme() && Same(freshPanel->GetContainerStyle().scrollbarStyle.trackBorderColor, dark.roles.line)
              && Same(freshGroup->GetVisualStyle().titleColor, dark.roles.ink) && Same(freshCrumbs->GetStyle().currentItemTextColor, dark.roles.ink)
              && Same(freshPages->GetStyle().currentColor, dark.roles.accent) && Same(freshPicker->GetStyle().fieldColor, dark.roles.well)
              && Same(freshSwatches->GetStyle().selectedBorder, dark.roles.accent) && Same(freshRich->GetStyle().textColor, dark.roles.ink),
              "a new container, group box, breadcrumb, pagination, colour picker, swatch bar and rich text edit are styled from the theme");
        ContainerStyle noScroll = freshPanel->GetContainerStyle();
        noScroll.autoShowScrollbars = false;
        freshPanel->SetContainerStyle(noScroll);
        CHECK(freshPanel->GetFollowTheme(), "SetContainerStyle is behaviour, not a look: the container keeps following");
        freshGroup->SetVisualStyle(GroupBoxVisualStyle::Card());
        freshCrumbs->SetStyle(BreadcrumbStyle::Compact());
        freshPicker->SetStyle(ColorPickerStyle::Light());
        freshRich->SetStyle(RichTextEditStyle());
        CHECK(!freshGroup->GetFollowTheme() && !freshCrumbs->GetFollowTheme() && !freshPicker->GetFollowTheme() && !freshRich->GetFollowTheme(),
              "a hand-set style stops their following");
        CHECK(Same(freshCols->GetColumnStyle().headerBackground, dark.roles.panel) && Same(freshCols->GetTextColor(), dark.roles.ink),
              "a new columns tree view: its columns and its tree part");
        freshBar->SetStyle(ScrollbarStyle::Modern());
        freshList->SetStyle(ListViewStyle());
        freshCols->SetColumnStyle(TreeColumnStyle());
        CHECK(!freshBar->GetFollowTheme() && !freshList->GetFollowTheme() && !freshCols->GetFollowTheme(), "a hand-set style stops their following");
        freshTree->SetTextColor(Colors::Red);
        freshTree->OnThemeChanged(light);
        CHECK(freshTree->GetFollowTheme() && Same(freshTree->GetTextColor(), light.roles.ink),
              "the tree has no style struct: a colour set by hand does not stop the following, the next change puts the theme's back");
        // a style set by hand stops the following; the look is kept
        ButtonStyle own;
        own.normalColor = ownRest;
        fresh->SetStyle(own);
        CHECK(!fresh->GetFollowTheme() && Same(fresh->GetStyle().normalColor, ownRest), "SetStyle by hand stops the following");
        UltraCanvasTheme::SetCurrent(light);
        fresh->OnThemeChanged(light);
        CHECK(Same(fresh->GetStyle().normalColor, ownRest), "and the element keeps its look through a change");
        fresh->SetFollowTheme(true);
        CHECK(fresh->GetFollowTheme() && Same(fresh->GetStyle().normalColor, light.roles.raised),
              "SetStyle then SetFollowTheme(true): the roles over the given style, following again");
        fresh->SetThemeVariant(ButtonVariant::Ghost);
        CHECK(fresh->GetFollowTheme() && fresh->GetStyle().normalColor.a == 0 && Same(fresh->GetStyle().hoverColor, light.roles.hover),
              "SetThemeVariant re-styles without stopping the following");
        UltraCanvasTheme::SetCurrent(dark);
        fresh->OnThemeChanged(dark);
        CHECK(Same(fresh->GetStyle().hoverColor, dark.roles.hover), "the theme's own SetStyle keeps the following: the next change reaches it");
        freshText->SetStyle(freshText->GetStyle());
        CHECK(!freshText->GetFollowTheme(), "a label's SetStyle by hand stops its following");
        freshMenu->SetStyle(MenuStyle::Default());
        freshInput->SetStyle(TextInputStyle::Default());
        freshSlider->SetStyle(SliderVisualStyle());
        freshSwitch->SetVisualStyle(SwitchVisualStyle());
        freshPick->SetStyle(DropdownStyle());
        CHECK(!freshMenu->GetFollowTheme() && !freshInput->GetFollowTheme() && !freshSlider->GetFollowTheme()
              && !freshSwitch->GetFollowTheme() && !freshPick->GetFollowTheme(), "so does a menu's, a text input's, a slider's, a switch's and a dropdown's");
        UltraCanvasUIElement::SetFollowThemeByDefault(false);
        auto later = std::make_shared<UltraCanvasButton>("later", 0, 0, 80, 24, "Later");
        CHECK(!later->GetFollowTheme() && !Same(later->GetStyle().normalColor, dark.roles.raised), "the default off again: a new element does not follow");
        UltraCanvasTheme::SetCurrent(light);
    }

    std::printf("fonts by role in the readers\n");
    {
        UltraCanvasTheme t = UltraCanvasTheme::Light();
        t.fonts.ui.fontFamily = "Noto Sans";
        t.fonts.ui.fontSize = 10;
        t.fonts.mono.fontFamily = "Fira Code";
        t.fonts.scale = 1.25;
        const double body = t.FontSize(ThemeTextRole::Body), caption = t.FontSize(ThemeTextRole::Caption), heading = t.FontSize(ThemeTextRole::Heading);
        ButtonStyle bold;
        bold.fontWeight = FontWeight::Bold;
        const ButtonStyle b = ButtonStyle::FromTheme(t, bold);
        CHECK(b.fontFamily == "Noto Sans" && Near(b.fontSize, body, 1e-6) && b.fontStyle.fontFamily == "Noto Sans" && Near(b.fontStyle.fontSize, body, 1e-9)
              && b.fontWeight == FontWeight::Bold, "a button: the interface font at the body size (12.5), its weight kept");
        CHECK(Near(LabelStyle::FromTheme(t, LabelStyle(), LabelVariant::Body).fontStyle.fontSize, body, 1e-9)
              && Near(LabelStyle::FromTheme(t, LabelStyle(), LabelVariant::Heading).fontStyle.fontSize, heading, 1e-9)
              && LabelStyle::FromTheme(t, LabelStyle(), LabelVariant::Muted).fontStyle.fontFamily == "Noto Sans",
              "a label: the body size, a heading the heading size");
        const DropdownStyle d = DropdownStyle::FromTheme(t);
        CHECK(d.fontFamily == "Noto Sans" && Near(d.fontSize, body, 1e-6), "a dropdown: the body size");
        const TextInputStyle i = TextInputStyle::FromTheme(t);
        CHECK(i.fontStyle.fontFamily == "Noto Sans" && Near(i.fontStyle.fontSize, body, 1e-9), "a text input: the body size");
        const MenuStyle m = MenuStyle::FromTheme(t);
        CHECK(m.font.fontFamily == "Noto Sans" && Near(m.font.fontSize, body, 1e-9), "a menu: the body size");
        const TooltipStyle tip = TooltipStyle::FromTheme(t);
        CHECK(tip.fontFamily == "Noto Sans" && Near(tip.fontSize, caption, 1e-6) && Near(tip.titleFontSize, body, 1e-6),
              "a tooltip: the caption size, its title the body size");
        const SwitchVisualStyle sw = SwitchVisualStyle::FromTheme(t);
        CHECK(sw.base.fontFamily == "Noto Sans" && Near(sw.base.fontSize, body, 1e-6), "a switch's label: the body size");
        CHECK(Near(SliderVisualStyle::FromTheme(t).fontStyle.fontSize, caption, 1e-9), "a slider's value: the caption size");
        const ModalDialogStyle dlg = ModalDialogStyle::FromTheme(t);
        CHECK(Near(dlg.messageFontSize, body, 1e-6) && Near(dlg.detailsFontSize, caption, 1e-6) && dlg.buttonFontSize == 0.0f,
              "a dialog: the message at the body size, the details at the caption size, the buttons' left alone");
#if UC_TEST_HAS_ALBUM   // a selectable widget (ULTRACANVAS_WIDGETS), absent from a lean core
        AlbumConfig keep;
        keep.titleFontSize = 20;
        const AlbumConfig a = AlbumConfig::FromTheme(t, keep);
        CHECK(a.fontFamily == "Noto Sans" && a.titleFontSize == 20, "an album: the interface font, its own sizes");
#endif
        const TimecodeFieldStyle tc = TimecodeFieldStyle::FromTheme(t);
        CHECK(tc.fontFamily == "Fira Code" && tc.fontSize == 12.0f, "the timecode field: the theme's monospace family, the design's pixel size");
#if UC_TEST_HAS_VIDEO_TIMELINE
        const VideoTimelineStyle vt = VideoTimelineStyle::FromTheme(t);
        CHECK(vt.fontFamily == "Noto Sans" && vt.monoFamily == "Fira Code" && vt.labelSize == 12.0f,
              "the timeline: the theme's families, the design's pixel sizes");
#endif
    }


    std::printf("Fonts and text roles\n");
    {
        UltraCanvasTheme t = UltraCanvasTheme::Light();
        CHECK(t.fonts.ui.fontFamily == "Ubuntu" && Near(t.fonts.ui.fontSize, 12.0, 1e-9) && t.fonts.mono.fontFamily == "Ubuntu Mono"
              && Near(t.fonts.scale, 1.0, 1e-9), "a preset carries the bundled fonts at 12 pt, scale 1");
        CHECK(Near(t.FontSize(ThemeTextRole::Body), 12.0, 1e-9) && Near(t.FontSize(ThemeTextRole::Caption), 11.0, 1e-9)
              && Near(t.FontSize(ThemeTextRole::Small), 10.25, 1e-9) && Near(t.FontSize(ThemeTextRole::Heading), 15.0, 1e-9)
              && Near(t.FontSize(ThemeTextRole::Title), 18.0, 1e-9), "the roles' sizes from a 12 pt base: 12, 11, 10.25, 15, 18");
        CHECK(t.Font(ThemeTextRole::Heading).fontFamily == "Ubuntu" && Near(t.Font(ThemeTextRole::Heading).fontSize, 15.0, 1e-9),
              "Font(Heading) is the interface font at the heading size");
        CHECK(t.Font(ThemeTextRole::Mono).fontFamily == "Ubuntu Mono" && Near(t.Font(ThemeTextRole::Mono).fontSize, 12.0, 1e-9),
              "Font(Mono) is the monospace font at the body size");
        t.fonts.ui.fontFamily = "Segoe UI";
        t.fonts.ui.fontSize = 9.0;
        t.fonts.scale = 1.25;
        CHECK(Near(t.FontSize(ThemeTextRole::Body), 11.25, 1e-9) && Near(t.FontSize(ThemeTextRole::Caption), 10.25, 1e-9),
              "the host's scale multiplies: Segoe UI 9 at 1.25 is 11.25, its caption 10.25 (to the quarter point)");
        CHECK(Near(UltraCanvasTheme::Luminance(Colors::White), 1.0, 1e-9) && Near(UltraCanvasTheme::Contrast(Colors::White, Colors::Black), 21.0, 0.01),
              "the theme's own luminance and contrast measure");
    }

    std::printf("High contrast presets\n");
    for (const UltraCanvasTheme& t : {UltraCanvasTheme::LightHighContrast(), UltraCanvasTheme::DarkHighContrast()}) {
        const ThemeRoles& r = t.roles;
        const std::string n = t.name + ": ";
        CHECK(t.highContrast && (t.dark == (t.name == "Dark High Contrast")), (n + "flagged high contrast, dark as named").c_str());
        const Color surfaces[] = {r.ground, r.panel, r.well, r.raised};
        bool inks = true;
        for (const Color& s : surfaces) {
            inks = inks && Contrast(r.ink, s) >= 7.0 && Contrast(r.inkMuted, s) >= 7.0 && Contrast(r.inkFaint, s) >= 7.0;
        }
        CHECK(inks, (n + "every ink reads 7:1 on ground, panel, well and raised").c_str());
        CHECK(Contrast(r.ink, r.hover) >= 7.0 && Contrast(r.ink, r.pressed) >= 7.0, (n + "the ink reads 7:1 on hover and pressed").c_str());
        CHECK(Contrast(r.onAccent, r.accent) >= 7.0, (n + "text on the accent reads 7:1").c_str());
        CHECK(Contrast(r.focus, r.ground) >= 7.0 && Contrast(r.lineStrong, r.panel) >= 7.0, (n + "the focus ring and a control border read 7:1").c_str());
    }

    std::printf("DeriveAccent and FromHost\n");
    {
        const Color accents[] = {Color(0x00, 0x78, 0xd4), Color(0x35, 0x84, 0xe4), Color(0xc8, 0x88, 0x00), Color(0xed, 0x5b, 0x00),
                                 Color(0xe6, 0x2d, 0x42), Color(0xd5, 0x61, 0x99), Color(0x91, 0x41, 0xac), Color(0x6f, 0x83, 0x96),
                                 Color(0x3a, 0x94, 0x4a), Color(0x21, 0x90, 0xa4), Color(255, 255, 0), Color(128, 128, 128), Color(255, 255, 255), Color(0, 0, 0)};
        bool okLight = true, okDark = true, okHigh = true;
        for (const Color& a : accents) {
            const ThemeRoles l = UltraCanvasTheme::DeriveAccent(UltraCanvasTheme::Light().roles, a, false);
            const ThemeRoles d = UltraCanvasTheme::DeriveAccent(UltraCanvasTheme::Dark().roles, a, true);
            const ThemeRoles h = UltraCanvasTheme::DeriveAccent(UltraCanvasTheme::DarkHighContrast().roles, a, true, 7.0);
            okLight = okLight && Contrast(l.onAccent, l.accent) >= 4.5 && Contrast(l.focus, l.ground) >= 3.0;
            okDark = okDark && Contrast(d.onAccent, d.accent) >= 4.5 && Contrast(d.focus, d.ground) >= 3.0;
            okHigh = okHigh && Contrast(h.onAccent, h.accent) >= 7.0;
        }
        CHECK(okLight, "every accent, Windows' and GNOME's and the hard ones (yellow, grey, white, black), reads 4.5:1 under its text on Light");
        CHECK(okDark, "the same on Dark, the focus ring 3:1 on ground");
        CHECK(okHigh, "at 7:1 for high contrast");
        const ThemeRoles blue = UltraCanvasTheme::DeriveAccent(UltraCanvasTheme::Light().roles, Color(0x00, 0x78, 0xd4), false);
        CHECK(Same(blue.accent, Color(0x00, 0x78, 0xd4, 255)) && Same(blue.onAccent, Colors::White) && Same(blue.focus, blue.accent),
              "Windows' blue stays itself: white reads on it, it is the focus ring");
        const ThemeRoles yellow = UltraCanvasTheme::DeriveAccent(UltraCanvasTheme::Light().roles, Color(255, 255, 0), false);
        CHECK(Same(yellow.onAccent, UltraCanvasTheme::Light().roles.ink) && Contrast(yellow.focus, UltraCanvasTheme::Light().roles.ground) >= 3.0,
              "a yellow accent takes the dark ink, and a focus ring that shows on the ground");
        CHECK(!Same(blue.accentSoft, blue.accent) && !Same(blue.accentHover, blue.accent), "hover and soft are derived, not the accent itself");

        HostAppearance host = DefaultHostAppearance();
        UltraCanvasTheme fromDefault = UltraCanvasTheme::FromHost(host);
        CHECK(fromDefault.name == "Host Light" && !fromDefault.dark && !fromDefault.highContrast
              && Same(fromDefault.roles.accent, UltraCanvasTheme::Light().roles.accent) && fromDefault.fonts.ui.fontFamily == "Ubuntu",
              "the default host gives Host Light with the preset's accent and the bundled fonts");
        host.dark = true;
        host.accent = Color(0xe9, 0x54, 0x20);
        host.uiFont.fontFamily = "Noto Sans";
        host.uiFont.fontSize = 10;
        host.monoFont.fontFamily = "Fira Code";
        host.textScale = 1.25;
        UltraCanvasTheme fromDark = UltraCanvasTheme::FromHost(host);
        CHECK(fromDark.name == "Host Dark" && fromDark.dark && Same(fromDark.roles.panel, UltraCanvasTheme::Dark().roles.panel),
              "a dark host gives Host Dark on the Dark preset");
        CHECK(Contrast(fromDark.roles.onAccent, fromDark.roles.accent) >= 4.5 && !Same(fromDark.roles.accent, UltraCanvasTheme::Dark().roles.accent),
              "its accent is the host's, derived");
        CHECK(fromDark.fonts.ui.fontFamily == "Noto Sans" && fromDark.fonts.mono.fontFamily == "Fira Code" && Near(fromDark.fonts.scale, 1.25, 1e-9)
              && Near(fromDark.FontSize(ThemeTextRole::Body), 12.5, 1e-9), "its fonts and scale are the host's: Noto Sans 10 at 1.25 is 12.5");
        host.highContrast = true;
        UltraCanvasTheme fromHigh = UltraCanvasTheme::FromHost(host);
        CHECK(fromHigh.name == "Host Dark High Contrast" && fromHigh.highContrast && Contrast(fromHigh.roles.onAccent, fromHigh.roles.accent) >= 7.0,
              "a high-contrast dark host gives the dark high-contrast preset with the accent at 7:1");
        host.dark = false;
        CHECK(UltraCanvasTheme::FromHost(host).name == "Host Light High Contrast", "a high-contrast light host the light one");
    }

    UltraCanvasTheme::SetCurrent(light);
    std::printf("\n%d of %d checks passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
