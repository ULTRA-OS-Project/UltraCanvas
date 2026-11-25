# UltraCanvas Map View Documentation

## Overview

**UltraCanvasMapView** shows a map of the world from a slippy-map tile
server - OpenStreetMap's by default. It comes three ways:

- **The element** `UltraCanvasMapView`: dragged to move, the wheel to zoom,
  a click to put the pin.
- **A map drawn into any rectangle** `DrawMap`: a map card in a list's row,
  a thumbnail in a delegate.
- **A map as pixels** `RenderMapToRgba`: a picture to save, copy or send.

The tiles are fetched in the background through UltraNet, kept in memory and
shared by every map of the program. ChatCube's location window and its map
cards in the chat are built on it.

**File Location**: `include/UltraCanvasMapView.h`, `core/UltraCanvasMapView.cpp`
**Version**: 1.0.0 - from ChatCube's location window, made a framework element
**Author**: UltraCanvas Framework

## Features

- ✅ Web Mercator, the projection every slippy-map server draws, there and back
  (`WebMercator::ToWorld`, `FromWorld`)
- ✅ Drag to move, wheel to zoom, click to put the pin (`onPinMoved`), or pin
  only from code (`SetPinOnClick(false)`)
- ✅ One tile cache for the whole program: a tile fetched once is drawn
  everywhere and never fetched again while the program runs
- ✅ OpenStreetMap's tile usage policy kept: the program's User-Agent, two
  requests at a time, the attribution on every map
- ✅ Another tile server with its own attribution (`MapTiles::SetServer`)

## Quick Start

```cpp
#include "UltraCanvasMapView.h"

#include <memory>

using namespace UltraCanvas;

std::shared_ptr<UltraCanvasMapView> MakeMap() {
    // OpenStreetMap's policy asks every program to name itself
    MapTiles::SetUserAgent("MyApp/1.0 (ULTRA OS)");
    auto map = std::make_shared<UltraCanvasMapView>("map", 0, 0, 480, 320);
    const GeoPoint gate{52.516275, 13.377704};
    map->CentreOn(gate, 16);
    map->SetPin(gate);
    map->onPinMoved = [](const GeoPoint& at) {
        // the person clicked here: at.latitude, at.longitude
        (void)at;
    };
    return map;
}

// A map card in a delegate's row; MapTiles::AddListener tells when the
// tiles that were missing have come, to draw the row again
void PaintCard(IRenderContext* ctx, const Rect2Dd& box, const GeoPoint& place) {
    DrawMap(ctx, box, place, 15, &place, true);
}
```

## Key API

### `UltraCanvasMapView`

| Call | Meaning |
|---|---|
| `CentreOn(point, zoom)` / `GetCentre()` / `GetZoom()` | The view: the point in the middle, the zoom (1 the world, 19 a house). |
| `Zoom(by)` | One step in (+1) or out (-1), about the centre. |
| `SetPin(point)` / `ClearPin()` / `HasPin()` / `GetPin()` | The red pin. |
| `SetPinOnClick(on)` | Whether a click puts the pin where it lands (true by default). |
| `onPinMoved(point)` | A click put the pin there. |
| `onViewChanged()` | The centre or the zoom changed. |

### Drawing without the element

| Call | Meaning |
|---|---|
| `DrawMap(ctx, rect, centre, zoom, pin, attribution)` | The map into `rect`; tiles not here yet are asked for and drawn grey. |
| `RenderMapToRgba(centre, zoom, width, height, pin, out)` | The same as straight RGBA from the tiles here now (`ClipboardDib::EncodePng` makes a PNG of it). |
| `DrawMapPin(ctx, at)` | The pin alone. |

### `MapTiles`

| Call | Meaning |
|---|---|
| `SetUserAgent(text)` | The program's name for the tile server. |
| `SetServer(urlTemplate, attribution)` | Another server: a URL with `{z}`, `{x}`, `{y}`, and the credit it asks for. |
| `Attribution()` | The credit the maps show. |
| `AddListener(fn)` / `RemoveListener(id)` | Told on the UI thread whenever a tile arrives. |

## Notes

- Coordinates are WGS 84 degrees; `WebMercator` clips latitude to ±85.05°,
  as the tiles do.
- The tiles are fetched only while an application runs
  (`UltraCanvasApplicationBase::GetCurrent()`); without one a map draws grey.
- Where the person is: [UltraCanvasGeolocation](UltraCanvasGeolocation.md).
