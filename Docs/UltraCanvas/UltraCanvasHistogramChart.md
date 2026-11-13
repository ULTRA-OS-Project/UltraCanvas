# UltraCanvasHistogramChart

A picture's histogram: how many pixels have each of the 256 levels, for red,
green and blue together or for one channel at a time - the panel a photo
editor shows beside an image.

```cpp
#include "Plugins/Charts/Engine/Charts/UltraCanvasHistogramChart.h"
```

## Contents

- [Overview](#overview)
- [Quick start](#quick-start)
- [Counting pixels yourself](#counting-pixels-yourself)
- [Appearance and interaction](#appearance-and-interaction)
- [Demo](#demo)
- [Tests](#tests)

## Overview

| Display (`HistogramDisplay`) | Shows |
|---|---|
| `Combined` | red, green and blue overlaid, each in its own colour; where they overlap the colours mix |
| `Red`, `Green`, `Blue` | one channel alone |
| `Luminosity` | each pixel's brightness, Rec. 601 luma (0.299 R + 0.587 G + 0.114 B) |

The chart is built on the chart engine: levels 0 to 255 run along the bottom,
pixel counts up the side, linear or logarithmic. Each channel is a stepped,
filled outline, one flat top per level. Hovering shows the level's counts for
every channel shown, with their share of the pixels, and draws a thin rule
through the level. In `Combined`, the legend switches a channel off and on.

The counting lives in a UI-free model, `UltraCanvasHistogramModel.h`, which a
program can use without drawing anything. It is part of the core library, not
of the charts, so it is there in a build without them: the Curves dialog
counts its histogram backdrop with it too (see
[UltraCanvasCurveEditor.md](UltraCanvasCurveEditor.md#filling-the-histogram)).

## Quick start

```cpp
auto histogram = CreateHistogramChart("Histogram", 0, 0, 640, 340);
histogram->SetImage(UCImage::Get("photo.jpg"));          // counts the picture's pixels
histogram->SetDisplay(HistogramDisplay::Combined);       // or Red, Green, Blue, Luminosity
histogram->SetScale(HistogramScale::Logarithmic);        // tall spikes stop flattening the rest
```

A picture the person chooses comes through the file loader, as every user
file does:

```cpp
FileDialogOptions options;
options.title = "Open a picture";
UltraCanvasFileLoader::OpenImage(options, [histogram](const FileLoadResult& result, std::shared_ptr<UCImage> image) {
    if (result.IsSuccess() && image) histogram->SetImage(image);
});
```

`SetImage` counts the picture at its own size, every pixel once, and leaves
fully transparent pixels out: they have no colour. It returns false, and the
chart is empty, for an image without pixels.

## Counting pixels yourself

`ComputeImageHistogram` counts any pixel buffer - Cairo's premultiplied
ARGB32 (un-premultiplied first, so a half-transparent red counts as red), its
opaque XRGB32, or plain RGBA8 and RGB8 bytes:

```cpp
#include "UltraCanvasHistogramModel.h"

std::vector<uint8_t> rgba(640 * 480 * 4);                // from a camera, a decoder, ...
ImageHistogram counts = ComputeImageHistogram(rgba.data(), 640, 480, 640 * 4,
                                              HistogramPixelFormat::RGBA8);
uint64_t reds = counts.CountAt(HistogramChannel::Red, 200);   // pixels with red = 200
HistogramStatistics luma = counts.Statistics(HistogramChannel::Luminosity);
// luma.mean, luma.median, luma.standardDeviation, luma.minimum, luma.maximum,
// luma.shadowsClipped and luma.highlightsClipped (shares of the pixels at 0 and 255)
int fifth = counts.Percentile(HistogramChannel::Luminosity, 0.05);
histogram->SetHistogram(counts);
```

A large picture can be counted a strip of rows at a time, the strips added
with `Add`; `RgbTotal()` gives red, green and blue added level by level, the
values a curve applied to all three channels maps:

```cpp
ImageHistogram whole;
for (int top = 0; top < 480; top += 120)
    whole.Add(ComputeImageHistogram(rgba.data() + top * 640 * 4, 640, 120, 640 * 4,
                                    HistogramPixelFormat::RGBA8));
std::array<uint64_t, 256> allChannels = whole.RgbTotal();
```

`ClippedRange` gives the levels a channel spans once a fraction of its pixels
is let go at each end - what an auto contrast stretches to fill 0..255
(UltraPaint's *Auto contrast* lets 0.2 % go at each end):

```cpp
HistogramRange red = whole.ClippedRange(HistogramChannel::Red, 0.002);   // red.low, red.high
```

## Appearance and interaction

```cpp
histogram->SetChannelColor(HistogramChannel::Red, Color(255, 80, 80, 255));   // alpha 0 = the default
histogram->SetFillOpacity(0.5f);                       // the outline stays solid
histogram->SetAxisTitles("Level", "Pixels");
histogram->onLevelHover = [histogram](int level) {     // -1 when the pointer leaves
    std::string text = histogram->DescribeLevel(level);    // "Level 128\nRed: 1,234 pixels (0.52%)..."
};
std::vector<HistogramChannel> drawn = histogram->ShownChannels();
```

The chart's title, theme, legend position and tooltips are the engine's
(`SetChartTitle`, `SetTheme`, `SetLegendPosition`, `SetEnableTooltips`).

## Demo

**Charts > Histogram Chart** shows a picture on the left - `artist.jpg` from
`media/images` to start with - and its histogram on the right. Underneath are
the choice of picture (or **Open a picture**, through the file loader), of
channels and of the logarithmic scale, each shown channel's statistics and the
level under the pointer.

## Tests

`Tests/HistogramModelTest.cpp` counts every pixel layout, premultiplied and
transparent pixels, luminosity, statistics and percentiles, adding strips,
the red-green-blue total and the clipped range. `Tests/CurveHistogramTest.cpp` fills the Curves
backdrop from it.
`Tests/HistogramChartRenderTest.cpp` draws Combined and each channel, the
logarithmic scale, the hover read-out and the legend, and counts a decoded
picture through `SetImage`.
