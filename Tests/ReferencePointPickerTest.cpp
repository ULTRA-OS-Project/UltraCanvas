// Tests/ReferencePointPickerTest.cpp
// UltraCanvasReferencePointPicker (the nine points and where they lie in a
// box, SetPoint and its callback) and UltraCanvasSpinner::SetSnapToStep
// (a Decimal value kept off the step grid, stepping from it) and
// onStepsFinished (once per run of steps, not per step).
//
// Usage: ReferencePointPickerTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework

#include "UltraCanvasReferencePointPicker.h"
#include "UltraCanvasSpinner.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) { ++failures; std::printf("FAIL: %s\n", what.c_str()); }
    else std::printf("  ok: %s\n", what.c_str());
}

bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

} // namespace

int main() {
    // ===== the nine points =====
    {
        const Rect2Dd box(10, 20, 100, 40);
        const Point2Dd tl = ReferencePointIn(ReferencePoint::TopLeft, box);
        const Point2Dd c = ReferencePointIn(ReferencePoint::Centre, box);
        const Point2Dd br = ReferencePointIn(ReferencePoint::BottomRight, box);
        const Point2Dd mr = ReferencePointIn(ReferencePoint::MiddleRight, box);
        const Point2Dd bc = ReferencePointIn(ReferencePoint::BottomCentre, box);
        Check(Near(tl.x, 10) && Near(tl.y, 20), "top left is the box's origin");
        Check(Near(c.x, 60) && Near(c.y, 40), "centre is the middle of the box");
        Check(Near(br.x, 110) && Near(br.y, 60), "bottom right is the far corner");
        Check(Near(mr.x, 110) && Near(mr.y, 40), "middle right is on the right edge, half way down");
        Check(Near(bc.x, 60) && Near(bc.y, 60), "bottom centre is on the bottom edge, half way across");
        const Point2Dd f = ReferencePointFraction(ReferencePoint::TopRight);
        Check(Near(f.x, 1) && Near(f.y, 0), "top right is (1, 0) of the box");
        Check(std::string(ReferencePointName(ReferencePoint::MiddleLeft)) == "Middle left", "points have names");
    }

    // ===== the picker =====
    {
        auto picker = CreateReferencePointPicker("picker", 0, 0, 40, 40);
        Check(picker->GetPoint() == ReferencePoint::Centre, "the picker starts on the centre");
        int calls = 0;
        ReferencePoint last = ReferencePoint::Centre;
        picker->onPointChanged = [&](ReferencePoint p) { ++calls; last = p; };
        picker->SetPoint(ReferencePoint::TopLeft);
        Check(picker->GetPoint() == ReferencePoint::TopLeft && calls == 0, "SetPoint without the callback is silent");
        picker->SetPoint(ReferencePoint::BottomRight, true);
        Check(calls == 1 && last == ReferencePoint::BottomRight, "SetPoint with the callback reports the point");
        picker->SetPoint(ReferencePoint::BottomRight, true);
        Check(calls == 1, "picking the same point again reports nothing");
        Check(picker->GetAccessibleValueText() == "Bottom right", "its accessible value is the point's name");
    }

    // ===== Spinner: a value off the step grid =====
    {
        auto snapped = CreateDecimalSpinner("snapped", 0, 0, 90, 22, -1000, 1000, 0, 0.1, 2);
        snapped->SetValue(8.61);
        Check(Near(snapped->GetValue(), 8.6, 1e-9), "by default a value is rounded onto the step grid");

        auto loose = CreateDecimalSpinner("free", 0, 0, 90, 22, -1000, 1000, 0, 0.1, 2);
        loose->SetSnapToStep(false);
        loose->SetValue(8.61);
        Check(Near(loose->GetValue(), 8.61, 1e-9), "SetSnapToStep(false) keeps 8.61");
        loose->SetValue(8.6149);
        Check(Near(loose->GetValue(), 8.61, 1e-9), "and rounds to the spinner's decimals");
        loose->StepUp();
        Check(Near(loose->GetValue(), 8.71, 1e-9), "the buttons step by the step from there");
        loose->SetValue(5000);
        Check(Near(loose->GetValue(), 1000, 1e-9), "the range still clamps");
    }

    // ===== Spinner: one run of steps, one onStepsFinished =====
    {
        auto s = CreateDecimalSpinner("run", 0, 0, 90, 22, -1000, 1000, 0, 1, 1);
        int changed = 0, finished = 0;
        double finishedAt = 0;
        s->onValueChanged = [&](double) { ++changed; };
        s->onStepsFinished = [&](double v) { ++finished; finishedAt = v; };
        UCEvent down;
        down.type = UCEventType::MouseDown;
        down.pointer = Point2Di(84, 5);          // the up button, top right
        UCEvent up = down;
        up.type = UCEventType::MouseUp;
        // A held button repeats as further presses before the release.
        s->OnEvent(down);
        s->OnEvent(down);
        s->OnEvent(down);
        Check(changed == 3 && Near(s->GetValue(), 3), "every step changes the value");
        Check(finished == 0 && s->IsStepping(), "the run is open while the button is held");
        s->OnEvent(up);
        Check(finished == 1 && Near(finishedAt, 3), "releasing the button ends the run once, on its last value");
        s->OnEvent(up);
        Check(finished == 1 && !s->IsStepping(), "a second release reports nothing");
        s->SetValue(10);
        UCEvent leave;
        leave.type = UCEventType::MouseLeave;
        s->OnEvent(leave);
        Check(finished == 1, "SetValue starts no run");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
