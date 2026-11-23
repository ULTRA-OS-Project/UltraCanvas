// include/UltraCanvasGeolocation.h
// Where this device is: one position, asked of the system's location
// service, the answer on the UI thread.
//
//   if (UltraCanvasGeolocation::IsAvailable()) {
//       UltraCanvasGeolocation::RequestPosition([](const GeoPositionResult& r) {
//           if (r.ok) map->CentreOn(GeoPoint{r.latitude, r.longitude}, 16);
//           else status->SetText(r.error);       // "Location services are off", ...
//       });
//   }
//
// The backends:
//   Linux, BSD  GeoClue 2 over the system D-Bus (org.freedesktop.GeoClue2):
//               a client of the manager, the program's desktop id given,
//               started, and the first LocationUpdated read. GeoClue asks
//               the person through the desktop's agent where one runs.
//   macOS       CoreLocation (CLLocationManager requestLocation), asking for
//               "when in use" permission the first time; the bundle's
//               Info.plist needs NSLocationWhenInUseUsageDescription
//               (package-macos.sh writes it for the apps that ask).
//   Windows     Windows.Devices.Geolocation (C++/WinRT Geolocator), where the
//               build has its projection headers; Windows' "Let desktop apps
//               access your location" decides.
//   Android, WebAssembly: not yet (IsAvailable() false).
//
// A position comes with its accuracy in metres: a Wi-Fi or IP position can
// be off by a kilometre or more, and a caller should say so.
//
// Docs: Docs/UltraCanvas/UltraCanvasGeolocation.md
// Version: 1.0.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASGEOLOCATION_H
#define ULTRACANVASGEOLOCATION_H

#include <functional>
#include <string>

namespace UltraCanvas {

struct GeoPositionResult {
    bool ok = false;
    double latitude = 0.0;          // degrees, WGS 84
    double longitude = 0.0;
    double accuracyMeters = -1.0;   // the radius it may be off by; -1 when not said
    std::string error;              // why not, for the person, when !ok
};

class UltraCanvasGeolocation {
public:
    // Whether this build has a location backend at all. True does not
    // promise a position: the service may be off, or the person may refuse.
    static bool IsAvailable();
    // The name of the backend ("GeoClue", "CoreLocation", "Windows", "none")
    static std::string BackendName();
    // Asks once; `done` runs on the UI thread, exactly once, with the
    // position or the reason there is none - at the latest after
    // `timeoutMs`. Several requests at once are answered each.
    static void RequestPosition(std::function<void(const GeoPositionResult&)> done, int timeoutMs = 20000);
    // A short words-and-numbers form of the accuracy: "about 30 m", "about 2 km"
    static std::string AccuracyText(double accuracyMeters);
};

// Which builds have a native backend: the core supplies the "none" one
// for the rest. Windows: when CMake found the C++/WinRT projection of
// Windows.Devices.Geolocation (ULTRACANVAS_HAVE_WINRT_GEOLOCATION).
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(ULTRACANVAS_HAVE_WINRT_GEOLOCATION)
    #define ULTRACANVAS_GEOLOCATION_NATIVE 1
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_OSX
        #define ULTRACANVAS_GEOLOCATION_NATIVE 1
    #endif
#endif

// The platform's half (OS/<Platform>/UltraCanvas*Geolocation.*). Request
// may answer on any thread; the core moves the answer to the UI thread.
namespace GeolocationBackend {
    bool Available();
    std::string Name();
    void Request(int timeoutMs, std::function<void(const GeoPositionResult&)> answer);
} // namespace GeolocationBackend

} // namespace UltraCanvas

#endif // ULTRACANVASGEOLOCATION_H
