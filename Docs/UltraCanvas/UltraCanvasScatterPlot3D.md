# UltraCanvasScatterPlot3D Documentation

## Overview

`UltraCanvasScatterPlot3DElement` renders an (x, y, z) point cloud inside a
perspective axes box with mouse orbit/zoom, an optional ground grid, depth
cueing and an optional 3D correlation (best-fit) line. It is the
three-dimensional companion of
[`UltraCanvasScatterPlotElement`](UltraCanvasScatterPlotElement.md) and is
built for spotting spatial correlations and outliers in trivariate data.

The renderer is pure software (painter's algorithm over `IRenderContext`), so
it works in every build regardless of `ULTRACANVAS_ENABLE_GL`. Points and the
correlation-line segments are depth sorted together, so the line threads
through the cloud with correct occlusion and no depth buffer.

## Class Declaration

```cpp
namespace UltraCanvas {
    class UltraCanvasScatterPlot3DElement : public UltraCanvasChartElementBase
}
```

**Header File:** `UltraCanvas/include/Plugins/Charts/UltraCanvasScatterPlot3D.h`  
**Implementation:** `UltraCanvas/Plugins/Charts/UltraCanvasScatterPlot3D.cpp`  
**Version:** 1.0.0  
**Last Modified:** 2026-07-29  

## Features

- **True 3D point cloud:** data `x`, `y`, `z` from the standard
  `IChartDataSource` / `ChartDataPoint` model; `z` is the vertical axis
- **Orbit & zoom camera:** drag to orbit, mouse wheel to zoom, view presets
- **3D correlation line:** principal axis of the cloud (orthogonal
  least-squares fit), depth sorted into the points
- **Depth cueing:** perspective point sizing plus optional fade toward the
  background for distance perception
- **Per-point colors:** `ChartDataPoint::color` overrides the element color
  (mark outliers, encode categories)
- **Perspective axes:** tick labels, titles and an optional ground grid that
  re-anchor to the corner nearest the camera while orbiting
- **Tooltips:** hover a point to see its label and X/Y/Z values

## Data Model

The element consumes the standard chart data source; each `ChartDataPoint`
contributes its `x`, `y` and `z` members. Data is normalized into a `[-1,1]³`
world cube: data **x** runs left-right, data **y** recedes into the scene and
data **z** is vertical (the usual mathematical 3D plot layout).

```cpp
auto data = std::make_shared<ChartDataVector>();
for (int i = 0; i < 200; ++i) {
    double t = SampleParameter(i);
    ChartDataPoint p(t * 0.9 + Noise(), t * 0.7 + Noise(), t * 1.1 + Noise(),
                     "Sample " + std::to_string(i));
    if (IsOutlier(i)) p.color = Color(220, 60, 60, 255);   // red outliers
    data->AddPoint(p);
}
scatter3D->SetDataSource(data);
```

## Enumerations

### PointShape3D

```cpp
enum class PointShape3D {
    Circle,   // default
    Square,
    Diamond
}
```

## Configuration Methods

### Point Appearance

```cpp
void SetPointColor(const Color& color);      // default Color(0, 102, 204)
void SetPointSize(double size);              // screen radius at orbit distance, default 5.0
void SetPointShape(PointShape3D shape);      // default Circle
void SetDepthFade(bool enabled, double strength = 0.55);
```

`SetPointSize` sets the on-screen radius of a point at the camera's orbit
distance; nearer points grow and farther points shrink with perspective.
`SetDepthFade` additionally fades far points toward the background color
(`strength` 0…1).

### Correlation Line

The correlation line is the **principal axis** of the point cloud — the
orthogonal least-squares (total least squares) fit minimizing perpendicular
distances, computed as the dominant eigenvector of the covariance matrix. It
spans the extent of the data along that axis.

```cpp
void SetShowCorrelationLine(bool show);            // default false
void SetCorrelationLineColor(const Color& color);  // default Color(220, 60, 60)
void SetCorrelationLineWidth(float width);         // default 2.5f
bool GetCorrelationLine(Vec3& centroid, Vec3& direction) const;
```

`GetCorrelationLine` returns the fit in data space (point on the line and unit
direction). It returns `false` with fewer than 2 points or a degenerate
(zero-variance) cloud.

### Camera

```cpp
void SetCamera(double yawRadians, double pitchRadians, double cameraDistance);
void SetFieldOfView(double radians);   // default 0.62
void SetEnableOrbit(bool on);          // default true
double GetYaw() const;
double GetPitch() const;
double GetDistance() const;
void ViewIsometric();   // default view
void ViewTop();
void ViewFront();
```

While `enableOrbit` is on, dragging with the left mouse button orbits the
camera and the mouse wheel zooms.

### Axes and Grid

```cpp
void SetShowAxes3D(bool on);                       // default true
void SetShowGroundGrid(bool on,
        const Color& c = Color(210, 210, 210, 160)); // default on
void SetAxisTitles(const std::string& x, const std::string& y, const std::string& z);
void SetTickCounts(int xTicks, int yTicks, int zTicks);   // defaults 5, 5, 4
void SetXTickFormatter(ValueFormatter fn);   // std::function<std::string(double)>
void SetYTickFormatter(ValueFormatter fn);
void SetZTickFormatter(ValueFormatter fn);
void SetAxisColors(const Color& line, const Color& label);
void SetAxisLabelFontSize(float size);
void SetRotateAxisLabels(bool on);           // default true
void SetTitleColor(const Color& c);
```

The two ground axes are drawn along the box edges meeting at the corner
nearest the camera and the vertical axis rises from the far end of the x
axis, so labels stay in front of the cloud while orbiting.

### Inherited from UltraCanvasChartElementBase

`SetDataSource`, `SetChartTitle`, `SetBackgroundColor`, `SetEnableTooltips`,
`SetSeriesName` and `SetCustomTooltipGenerator` work as on every chart
element. The default tooltip shows the point label plus X, Y and Z values.

## Factory Function

```cpp
std::shared_ptr<UltraCanvasScatterPlot3DElement> CreateScatterPlot3DElement(
    const std::string& id, int x, int y, int width, int height);
```

## Usage Example

```cpp
#include "Plugins/Charts/UltraCanvasScatterPlot3D.h"

// Build a correlated cloud with red outliers
auto data = std::make_shared<ChartDataVector>();
std::mt19937 gen(42);
std::uniform_real_distribution<> tDist(-100.0, 100.0);
std::uniform_real_distribution<> noise(-8.0, 8.0);
std::uniform_real_distribution<> outlierNoise(-55.0, 55.0);

for (int i = 0; i < 90; ++i) {
    bool outlier = (i % 9) == 8;
    auto& spread = outlier ? outlierNoise : noise;
    double t = tDist(gen);
    ChartDataPoint p(t * 0.9 + spread(gen),
                     t * 0.7 + spread(gen),
                     t * 1.1 + spread(gen),
                     outlier ? "Outlier" : "Inlier");
    p.color = outlier ? Color(220, 60, 60, 255) : Color(0, 102, 204, 255);
    data->AddPoint(p);
}

auto scatter3D = CreateScatterPlot3DElement("cloud3D", 20, 20, 640, 480);
scatter3D->SetDataSource(data);
scatter3D->SetChartTitle("3D Correlation Cloud");
scatter3D->SetAxisTitles("X", "Y", "Z");
scatter3D->SetPointSize(4.5);
scatter3D->SetShowCorrelationLine(true);
window->AddChild(scatter3D);

// Read back the fitted axis
Vec3 centroid, direction;
if (scatter3D->GetCorrelationLine(centroid, direction)) {
    // direction is a unit vector in data space
}
```

A live example runs in the DemoApp under **Charts → Scatter Plot Chart**
(`Apps/DemoApp/UltraCanvasBasicChartsExamples.cpp`).

## Performance Notes

- Every visible point is projected and depth sorted each frame
  (`O(n log n)`); clouds of tens of thousands of points stay interactive.
- The correlation fit is `O(n)` per frame while the line is shown.
- For very large clouds prefer `Circle` points (single fill call) and
  consider disabling the correlation line while orbiting.

## Related Components

- `UltraCanvasScatterPlotElement`: 2D scatter with least-squares trend line
- `UltraCanvasContourSurface3DElement`: 3D height-field surface (same camera
  and projection model)
- `UltraCanvasBubbleChart`: 2D scatter with value-scaled point sizes
- `ChartDataVector`: Standard data source implementation

## Version History

- **1.0.0** (2026-07-29): Initial implementation
  - Software-projected 3D point cloud with orbit/zoom camera
  - Principal-axis correlation line, depth sorted into the cloud
  - Perspective axes, ground grid, depth cueing, per-point colors, tooltips
