# UltraCanvas Reference Point Picker

## Contents

- [Overview](#overview)
- [Use](#use)
- [Example](#example)
- [Key API](#key-api)
- [Notes](#notes)

## Overview

**UltraCanvasReferencePointPicker** is a 3 x 3 grid of points standing for
the corners, the edge midpoints and the centre of a box. A drawing program's
transform bar uses it to say which point of the selection stays where it is
when the selection is scaled, rotated or sheared, and which point its X and Y
fields give the position of - the small square of dots at the left of the
selector bar in Xara, Illustrator, InDesign or Affinity.

**File Location**: `include/UltraCanvasReferencePointPicker.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Use

- Click a point - anywhere in the element picks the nearest one.
- Arrow keys move between the points while it has the focus; Home picks the
  centre.
- The picked point is drawn larger in the accent colour, the point under the
  pointer darker; a disabled picker draws in grey and ignores the pointer.

`ReferencePointIn(point, box)` gives the point's position in a box, and
`ReferencePointFraction(point)` its place as fractions (0, 0.5 or 1) of the
box's width and height, so a caller never repeats the 3 x 3 arithmetic.

## Example

```cpp
#include "UltraCanvasReferencePointPicker.h"
#include "UltraCanvasContainer.h"
using namespace UltraCanvas;

void AddPicker(UltraCanvasContainer& bar, std::function<Rect2Dd()> selectionBounds) {
    auto picker = CreateReferencePointPicker("reference", 0, 0, 40, 40, ReferencePoint::Centre);
    picker->SetTooltip("The point of the selection that stays put");
    picker->onPointChanged = [selectionBounds](ReferencePoint point) {
        const Point2Dd pivot = ReferencePointIn(point, selectionBounds());
        (void)pivot;   // scale / rotate / show X and Y about this point
    };
    bar.AddChild(picker);
}
```

## Key API

```cpp
enum class ReferencePoint {
    TopLeft, TopCentre, TopRight,
    MiddleLeft, Centre, MiddleRight,
    BottomLeft, BottomCentre, BottomRight
};
Point2Dd    ReferencePointFraction(ReferencePoint point);   // (0..1, 0..1)
Point2Dd    ReferencePointIn(ReferencePoint point, const Rect2Dd& box);
const char* ReferencePointName(ReferencePoint point);       // "Top left", ...

class UltraCanvasReferencePointPicker : public UltraCanvasUIElement {
    void SetPoint(ReferencePoint p, bool runCallback = false);
    ReferencePoint GetPoint() const;
    // A point that is none of the nine, as fractions of the box: no point
    // lit, a ring in its direction kept inside the frame. Clicking one of
    // the nine (or SetPoint) clears it.
    void SetFreePoint(const std::optional<Point2Dd>& fraction);
    const std::optional<Point2Dd>& GetFreePoint() const;
    bool HasFreePoint() const;
    ReferencePointPickerStyle& GetStyle();
    void SetStyle(const ReferencePointPickerStyle& s);
    std::function<void(ReferencePoint)> onPointChanged;   // the user picked a point
};

std::shared_ptr<UltraCanvasReferencePointPicker> CreateReferencePointPicker(
    const std::string& identifier, float x, float y, float w, float h,
    ReferencePoint point = ReferencePoint::Centre);
```

`ReferencePointPickerStyle` holds the frame, background, point, hover,
selected, focus and disabled colours, the frame width and corner radius, and
the side of a point and of the picked point.

## Notes

- The grid is the largest square that fits the element, centred in it; 32 to
  48 px suits a two-row toolbar.
- To assistive technology it is a group whose value is the picked point's
  name.
- A free point stands for a point the user put somewhere else - ArtCreator's
  rotation centre dragged off the nine points. Its fractions may lie outside
  0..1; the ring is clamped to the frame, so it still shows which way the
  point lies. To assistive technology its value is then "Free point".
- ArtCreator's transform bar is the reference user
  (`Apps/ArtCreator/ArtCreatorWindow.cpp`, `BuildTransformBar`).
