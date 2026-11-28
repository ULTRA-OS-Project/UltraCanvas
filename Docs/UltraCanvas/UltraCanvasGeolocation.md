# UltraCanvas Geolocation Documentation

## Overview

**UltraCanvasGeolocation** asks the system's location service where this
device is, once, and answers on the UI thread: latitude, longitude and how
far off it may be. ChatCube's *My position* is built on it.

**File Location**: `include/UltraCanvasGeolocation.h`, `core/UltraCanvasGeolocation.cpp`,
`OS/Linux/UltraCanvasLinuxGeolocation.cpp`, `OS/MacOS/UltraCanvasMacOSGeolocation.mm`,
`OS/MSWindows/UltraCanvasWindowsGeolocation.cpp`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Quick Start

```cpp
#include "UltraCanvasGeolocation.h"

#include <cstdio>

using namespace UltraCanvas;

void WhereAmI() {
    if (!UltraCanvasGeolocation::IsAvailable()) return;   // hide the button instead
    UltraCanvasGeolocation::RequestPosition([](const GeoPositionResult& r) {
        if (r.ok) {
            std::printf("%f, %f (%s)\n", r.latitude, r.longitude,
                        UltraCanvasGeolocation::AccuracyText(r.accuracyMeters).c_str());
        } else {
            std::printf("not known: %s\n", r.error.c_str());
        }
    });
}
```

## Key API

| Call | Meaning |
|---|---|
| `IsAvailable()` | Whether this build has a location backend. True does not promise a position. |
| `BackendName()` | "GeoClue", "CoreLocation", "Windows" or "none". |
| `RequestPosition(done, timeoutMs)` | One position; `done` runs on the UI thread exactly once - with the position, or with the reason there is none (`error`), at the latest after `timeoutMs` (20 s by default). |
| `AccuracyText(metres)` | "about 30 m", "about 2 km"; empty when not known. |

`GeoPositionResult`: `ok`, `latitude`, `longitude` (WGS 84 degrees),
`accuracyMeters` (-1 when not said), `error`.

## The backends

| Platform | How |
|---|---|
| Linux | GeoClue 2 on the system D-Bus: a client of the manager, `DesktopId` the executable's name (its `.desktop` file's), accuracy "exact", `Start`, the first `LocationUpdated`, the Location object's `Latitude`, `Longitude`, `Accuracy`, `Stop`. On a thread of its own with its own GLib main context. GeoClue asks the person through the desktop's agent (GNOME Shell, KDE, ...) - without an agent it gives no client. Its sources are Wi-Fi, a GPS or modem, a static file (`/etc/geolocation`) and the network's address. |
| macOS | CoreLocation: a `CLLocationManager` on the main thread, "when in use" permission asked the first time, `requestLocation`. The bundle's `Info.plist` must carry `NSLocationWhenInUseUsageDescription`, or macOS refuses without asking: `package-macos.sh` writes it for the apps in its `location_usage` list (ChatCube, UltraDesktop). |
| Windows | `Windows.Devices.Geolocation` through C++/WinRT (`Geolocator::RequestAccessAsync`, `GetGeopositionAsync`), built where CMake finds `winrt/Windows.Devices.Geolocation.h` (the MSYS2 `cppwinrt` package, the Windows SDK) - `ULTRACANVAS_HAVE_WINRT_GEOLOCATION`. Windows decides by *Location services* and *Let desktop apps access your location*. |
| BSD, Android, WebAssembly | None yet: `IsAvailable()` false. |

## Notes

- A position from Wi-Fi or the network's address can be off by a kilometre
  or more: show `AccuracyText`, or zoom a map out to match.
- `Tests/MapGeolocationTest` asks the backend where one answers and skips
  where none does. To try GeoClue without a desktop: a static
  `/etc/geolocation` (latitude, longitude, altitude, accuracy - one a line),
  `[static-source] enable=true` and a section for the program in
  `/etc/geoclue/geoclue.conf`, and GeoClue's demo agent
  (`/usr/libexec/geoclue-2.0/demos/agent`) running.
- A map to show it on: [UltraCanvasMapView](UltraCanvasMapView.md).
