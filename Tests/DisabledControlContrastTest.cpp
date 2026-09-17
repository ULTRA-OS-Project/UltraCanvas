// Tests/DisabledControlContrastTest.cpp
// One rule, for every control that has a disabled state: a control that
// cannot be used must read as LESS ink than one that can.
//
// It was the other way round. Colours::LightGray (192) was the disabled face
// of checkboxes, radios and segmented controls and is *darker* than
// Colours::ButtonFace (225), and the border stayed at ButtonShadow whatever
// the state - so on a settings page listing one switch per file format, the
// unsupported formats were the strongest thing on the page. The fix is a
// pair of constants, and a constant is exactly the kind of thing somebody
// "tidies" back, which is why this is pinned rather than left to the eye.
//
// Compares luminance, not the raw bytes: what matters is which one looks
// lighter, and the greys these happen to be are not guaranteed to stay grey.
// Version: 1.0.0
// Last Modified: 2026-09-16
// Author: UltraCanvas Framework

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSegmentedControl.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok: %s\n", what.c_str());
    }
}

// Rec. 601 luma, which is close enough to perceived lightness for a
// comparison between two flat UI greys.
double Luminance(const Color& c) {
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

void CheckLighter(const Color& disabled, const Color& enabled,
                  const std::string& what) {
    Check(Luminance(disabled) > Luminance(enabled),
          what + " (disabled " + std::to_string(static_cast<int>(Luminance(disabled))) +
                  " vs enabled " + std::to_string(static_cast<int>(Luminance(enabled))) + ")");
}

}   // namespace

int main() {
    std::printf("== the shared constants\n");
    CheckLighter(Colors::ControlDisabled, Colors::ButtonFace,
                 "a disabled face is lighter than a live one");
    CheckLighter(Colors::ControlDisabledBorder, Colors::ButtonShadow,
                 "a disabled border is lighter than a live one");
    // Still a border: one that matched its own face would leave nothing to
    // show where the control is.
    Check(Luminance(Colors::ControlDisabledBorder) < Luminance(Colors::ControlDisabled),
          "a disabled border is still darker than the face it outlines");

    std::printf("== checkbox\n");
    {
        CheckboxVisualStyle s;
        CheckLighter(s.boxDisabledColor, s.boxColor, "checkbox face");
        CheckLighter(s.boxBorderDisabledColor, s.boxBorderColor, "checkbox border");
        CheckLighter(s.checkmarkDisabledColor, s.checkmarkColor, "checkbox tick");
        // The tick has to stay visible: a checked-but-unavailable switch says
        // something a blank box does not.
        Check(Luminance(s.checkmarkDisabledColor) < Luminance(s.boxDisabledColor),
              "a disabled tick still contrasts with its box");
    }

    std::printf("== radio\n");
    {
        RadioVisualStyle s;
        CheckLighter(s.outerDisabledColor, s.outerColor, "radio face");
        CheckLighter(s.outerBorderDisabledColor, s.outerBorderColor, "radio ring");
        CheckLighter(s.innerDotDisabledColor, s.innerDotColor, "radio dot");
    }

    std::printf("== button and segmented control\n");
    {
        ButtonStyle b;
        CheckLighter(b.disabledColor, b.normalColor, "button face");
        CheckLighter(b.disabledTextColor, b.normalTextColor, "button label");

        SegmentedControlStyle s;
        CheckLighter(s.disabledColor, s.normalColor, "segment face");
        CheckLighter(s.disabledTextColor, s.normalTextColor, "segment label");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
