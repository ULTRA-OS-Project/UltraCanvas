// Tests/UltraCanvasThemeTest.cpp
// The framework theme without a display: the two presets, element roles,
// the current theme, and the styles the timeline and the timecode field
// take from a theme - shared roles mapped, element roles honoured, the
// palette's colour where a theme is silent.
// Version: 1.3.0 - the button variants
// Version: 1.2.0 - the slider's and the switch's element roles
// Version: 1.1.0 - every FromTheme, the change handlers, elements that
//                  follow the theme through a container
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraCanvasTheme.h"
#include "UltraCanvasAlbum.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTimecodeField.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasTooltipTypes.h"
#include "UltraCanvasVideoTimeline.h"

#include <cstdio>

using namespace UltraCanvas;

static int testCount = 0, failCount = 0;
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

    UltraCanvasTheme own = dark;
    own.name = "Own";
    CHECK(!own.HasRole("video-timeline.playhead") && Same(own.Role("video-timeline.playhead", Colors::Red), Colors::Red), "an unnamed element role falls back");
    own.SetRole("video-timeline.playhead", Color(1, 2, 3));
    own.SetRole("video-timeline.clip-video", Color(4, 5, 6));
    CHECK(own.HasRole("video-timeline.playhead") && Same(own.Role("video-timeline.playhead", Colors::Red), Color(1, 2, 3)) && own.ElementRoles().size() == 2,
          "a named element role is read");
    UltraCanvasTheme::SetCurrent(own);
    CHECK(UltraCanvasTheme::Current().name == "Own" && UltraCanvasTheme::Current().dark, "SetCurrent makes it the one in force");

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
    AlbumConfig albumBase;
    albumBase.mode = AlbumMode::UserEdit;
    const AlbumConfig album = AlbumConfig::FromTheme(own, albumBase);
    CHECK(Same(album.backgroundColor, r.panel) && Same(album.itemBackgroundColor, r.raised) && Same(album.borderColor, r.line) &&
          Same(album.captionColor, r.ink) && Same(album.subtitleColor, r.inkMuted) && Same(album.linkColor, r.accent) &&
          Same(album.selectionColor, r.accent) && album.mode == AlbumMode::UserEdit, "album: panel, raised tiles, hairlines, captions, accent; the base's mode");

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
    UltraCanvasTheme::SetCurrent(light);
    follower->SetFollowTheme(false);
    panel->OnThemeChanged(light);
    CHECK(Same(follower->GetStyle().normalColor, dark.roles.raised) && Same(text->GetStyle().textColor, light.roles.ink),
          "SetFollowTheme(false) stops the re-styling");
    panel->ClearChildren();

    UltraCanvasTheme::SetCurrent(light);
    std::printf("\n%d of %d checks passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
