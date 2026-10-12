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
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

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

class UltraCanvasTheme {
public:
    std::string name;                   // "Light", "Dark", an app's own
    bool dark = false;                  // what an element's own presets should assume
    ThemeRoles roles;

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

    // The theme in force: Light until an application sets another. Read it
    // where a style is built with no theme given; set it before building the
    // window, and again (re-applying the elements) to switch.
    static const UltraCanvasTheme& Current();
    static void SetCurrent(const UltraCanvasTheme& theme);

private:
    std::unordered_map<std::string, Color> elementRoles;
};

} // namespace UltraCanvas
