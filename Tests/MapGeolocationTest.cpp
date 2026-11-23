// Tests/MapGeolocationTest.cpp
// The map's arithmetic and the location service, without a display:
// Web Mercator there and back (the tile a known place falls in at a known
// zoom), a map rendered to pixels from no tiles (grey, the pin in the
// middle), and - where a GeoClue answers on the system bus (Linux) - one
// position asked of the platform backend. Without a location service that
// part is skipped, not failed: CI machines have none.
// Version: 0.1.0
// Author: UltraCanvas Framework

#include "UltraCanvasGeolocation.h"
#include "UltraCanvasMapView.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <string>

using namespace UltraCanvas;

namespace {
int g_failures = 0;

void Check(bool condition, const char* what) {
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++g_failures;
}
} // namespace

int main() {
    std::printf("Web Mercator\n");
    {
        // The Brandenburg Gate at zoom 16 is in tile 35203 / 21493 (the OpenStreetMap wiki's formula)
        const GeoPoint gate{52.516275, 13.377704};
        const Point2Dd w = WebMercator::ToWorld(gate, 16);
        Check(static_cast<int>(w.x / 256) == 35203 && static_cast<int>(w.y / 256) == 21493,
              "ToWorld: the tile a known place lies in");
        const GeoPoint back = WebMercator::FromWorld(w, 16);
        Check(std::fabs(back.latitude - gate.latitude) < 1e-9 && std::fabs(back.longitude - gate.longitude) < 1e-9,
              "FromWorld: back to the same place");
        const Point2Dd origin = WebMercator::ToWorld(GeoPoint{0, 0}, 1);
        Check(std::fabs(origin.x - 256) < 1e-9 && std::fabs(origin.y - 256) < 1e-9,
              "ToWorld: 0,0 is the middle of the world");
    }

    std::printf("RenderMapToRgba\n");
    {
        ClipboardDib::RgbaImage image;
        const GeoPoint at{10, 20};
        // No application: nothing is fetched, every tile is missing
        Check(RenderMapToRgba(at, 5, 64, 48, &at, image) && image.width == 64 && image.height == 48 &&
                  image.pixels.size() == 64u * 48u * 4u,
              "a picture of the size asked");
        const uint8_t* corner = &image.pixels[0];
        Check(corner[0] == 224 && corner[3] == 255, "missing tiles are opaque grey");
        const uint8_t* middle = &image.pixels[(24u * 64u + 32u) * 4u];
        Check(middle[0] == 255 && middle[1] == 255, "the pin's white dot in the middle");
        const uint8_t* red = &image.pixels[(24u * 64u + 38u) * 4u];
        Check(red[0] == 220 && red[1] == 50, "the pin's red disc around it");
        Check(!RenderMapToRgba(at, 5, 0, 10, nullptr, image), "no picture of no size");
    }

    std::printf("Accuracy in words\n");
    Check(UltraCanvasGeolocation::AccuracyText(-1).empty(), "unknown: nothing");
    Check(UltraCanvasGeolocation::AccuracyText(34) == "about 30 m", "34 m: about 30 m");
    Check(UltraCanvasGeolocation::AccuracyText(2400) == "about 2 km", "2.4 km: about 2 km");

    std::printf("The location backend (%s)\n", UltraCanvasGeolocation::BackendName().c_str());
    if (!GeolocationBackend::Available()) {
        std::printf("  SKIP  no location backend in this build\n");
    } else {
        std::promise<GeoPositionResult> promise;
        auto answer = promise.get_future();
        GeolocationBackend::Request(8000, [&promise](const GeoPositionResult& r) { promise.set_value(r); });
        if (answer.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
            Check(false, "the backend answers within its time limit");
        } else {
            const GeoPositionResult r = answer.get();
            if (r.ok) {
                std::printf("  position %.6f, %.6f (accuracy %.0f m)\n", r.latitude, r.longitude, r.accuracyMeters);
                Check(r.latitude >= -90 && r.latitude <= 90 && r.longitude >= -180 && r.longitude <= 180,
                      "a position on the earth");
            } else {
                // No service, or not allowed: an answer in words is right too
                std::printf("  SKIP  no position here: %s\n", r.error.c_str());
                Check(!r.error.empty(), "no position comes with its reason");
            }
        }
    }

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
