// include/UltraCanvasTheme.h
// The framework theme: the colour roles an element reads so that a window's
// dark or light look is one object every element sees, not a style struct
// filled by hand per element. A role is named for what it is for (the
// panel, the ink, the accent, the focus ring), never for what it looks
// like, so an application fills the roles from its own design tokens once
// and every element that reads the theme follows. An element with colours
// of its own beyond the shared roles - a timeline's clip kinds, a chart's
// series - reads them as named element roles ("video-timeline.playhead")
// with a fallback it derives from the shared ones, so a theme that does not
// name them still looks right.
//
// There is no automatic re-style: an element takes its colours when its
// style is set (ElementStyle::FromTheme), and the application re-applies
// the elements it built when it switches theme, as it re-applies anything
// else that depends on the window's look.
//
// The font half of a look lives here too (ThemeFonts): the interface font
// and the monospace font at a base size, the host's text scale over them,
// and a size per text role (Body, Caption, Small, Heading, Title). FromHost
// builds the theme a desktop asks for - Light or Dark, the high-contrast
// variant of either, the desktop's accent derived into the five accent
// roles, its fonts - from what UltraCanvasHostAppearance read; the
// application's theme policy (ThemePolicy, UltraCanvasApplication) applies
// it at start and on every host change.
// Version: 1.3.0 - TakeHostFonts: the host's fonts and scale onto a theme, for an Own provider
// Version: 1.2.0 - ThemeFonts and ThemeTextRole; LightHighContrast / DarkHighContrast;
//                  FromHost with DeriveAccent; ThemePolicy (T3 of the host theme proposal)
// Version: 1.1.0 - change handlers (AddChangeHandler / RemoveChangeHandler), called after SetCurrent
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasHostAppearance.h"
#include "UltraCanvasRenderContext.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace UltraCanvas {

// The roles every themed element reads
struct ThemeRoles {
    // surfaces, back to front: the window ground and gutters, a panel, an
    // inset well (fields, lanes), a raised surface (headers, menus, buttons
    // at rest), the hover fill, the pressed fill
    Color ground, panel, well, raised, hover, pressed;
    // hairline dividers; control borders and strong ticks
    Color line, lineStrong;
    // text and icons: primary, secondary, placeholders and disabled
    Color ink, inkMuted, inkFaint;
    // the one accent: fills, selection rings; its hover; the tint behind a
    // selected row; text on the accent
    Color accent, accentHover, accentSoft, onAccent;
    // the keyboard focus ring; destructive actions and errors
    Color focus, danger;
};

// The text roles an element's text plays; the size of each is a ratio of
// the base size (ThemeFonts::ui.fontSize): Body 1.0, Caption 0.92 (the 11 pt
// of a 12 pt base), Small 0.85, Heading 1.25, Title 1.5; Mono is the
// monospace font at the body size.
enum class ThemeTextRole { Body, Caption, Small, Heading, Title, Mono };

// The font half of a look: the interface font and the monospace font with
// their base sizes in points, and the host's text scale over them (the
// per-window DPI is applied after, by the window).
struct ThemeFonts {
    FontStyle ui;          // Segoe UI 9, SF 13, Cantarell 11, the bundled Ubuntu 12
    FontStyle mono;        // Consolas, Menlo, the GNOME monospace font, Ubuntu Mono 12
    double scale = 1.0;    // Windows "Text size", GNOME text-scaling-factor
    ThemeFonts();          // the bundled Ubuntu 12 and Ubuntu Mono 12
};

// What the application's theme follows (UltraCanvasApplication::SetThemePolicy).
enum class ThemePolicy {
    FollowHost,   // the default: FromHost(GetHostAppearance()), again on every host change
    Light,        // the framework's Light(), the host's fonts
    Dark,         // Dark(), the host's fonts
    Own           // the application sets its theme itself; a provider may build it from the host
};

class UltraCanvasTheme {
public:
    std::string name;                   // "Light", "Dark", "Host Dark", an app's own
    bool dark = false;                  // what an element's own presets should assume
    bool highContrast = false;          // the high-contrast variant: every ink at 7:1
    ThemeRoles roles;
    ThemeFonts fonts;

    // The size a text role gets, in points: the base size by the role's
    // ratio by the host's scale; and the font itself - the interface font
    // at that size, the monospace font for Mono.
    double FontSize(ThemeTextRole role) const;
    FontStyle Font(ThemeTextRole role) const;

    // Roles an element adds beyond the shared ones, by name
    // ("video-timeline.clip-video-edge"): the app sets them from its design,
    // the element reads them with a fallback of its own
    void SetRole(const std::string& role, const Color& color) { elementRoles[role] = color; }
    bool HasRole(const std::string& role) const { return elementRoles.count(role) > 0; }
    Color Role(const std::string& role, const Color& fallback) const {
        auto found = elementRoles.find(role);
        return found == elementRoles.end() ? fallback : found->second;
    }
    const std::unordered_map<std::string, Color>& ElementRoles() const { return elementRoles; }

    // The framework's two looks: Light from the colours the elements default
    // to (Colors::WindowBackground, ButtonFace, Selection ...), Dark from the
    // greys the dark menu, tooltip and text area already use
    static UltraCanvasTheme Light();
    static UltraCanvasTheme Dark();
    // Black on white and white on black, every ink at 7:1, for a desktop
    // that asks for high contrast.
    static UltraCanvasTheme LightHighContrast();
    static UltraCanvasTheme DarkHighContrast();

    // The theme a desktop asks for: Light() or Dark() by host.dark, the
    // high-contrast variant by host.highContrast, the accent roles derived
    // from host.accent when it has one, the fonts and the text scale the
    // host's; named "Host Light", "Host Dark", "Host Light High Contrast",
    // "Host Dark High Contrast".
    static UltraCanvasTheme FromHost(const HostAppearance& host);
    // The host's fonts and text scale onto this theme, as FromHost puts
    // them: the interface font and the monospace font where the host names
    // one, the scale where it is positive; the theme's own stay otherwise.
    // An Own provider calls it on the theme it built, so the application's
    // colours come with the desktop's fonts.
    void TakeHostFonts(const HostAppearance& host);

    // One accent into the five accent roles, over the roles' surfaces:
    // `accent` is darkened or lightened until `onAccent` (white or the dark
    // ink, whichever reads better) reaches `onAccentContrast` on it (4.5:1,
    // 7:1 for high contrast); `accentHover` is the accent moved 8 % toward
    // the ink; `accentSoft` the accent at 18 % over panel when dark, 14 %
    // when light; `focus` the accent unless it fails 3:1 on ground, then
    // lineStrong. Pure.
    static ThemeRoles DeriveAccent(ThemeRoles roles, const Color& accent, bool dark, double onAccentContrast = 4.5);
    // WCAG 2 relative luminance and contrast ratio (1 to 21). Pure.
    static double Luminance(const Color& color);
    static double Contrast(const Color& a, const Color& b);

    // The theme in force: Light until an application sets another. Read it
    // where a style is built with no theme given; set it before building the
    // window, and again to switch: every change handler is called after it,
    // and the application's walks its windows so that each element that
    // follows the theme (UltraCanvasUIElement::SetFollowTheme) re-styles
    // itself. An element styled by hand keeps its look.
    static const UltraCanvasTheme& Current();
    static void SetCurrent(const UltraCanvasTheme& theme);

    // Called after SetCurrent with the new theme. The application installs
    // one at start-up; an app with panels of its own may add another and
    // remove it by the id returned.
    using ChangeHandler = std::function<void(const UltraCanvasTheme&)>;
    static int AddChangeHandler(ChangeHandler handler);
    static void RemoveChangeHandler(int id);

private:
    std::unordered_map<std::string, Color> elementRoles;
};

} // namespace UltraCanvas
