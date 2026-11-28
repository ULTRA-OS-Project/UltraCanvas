// OS/MSWindows/UltraCanvasWindowsGeolocation.cpp
// UltraCanvasGeolocation on Windows: Windows.Devices.Geolocation through
// C++/WinRT, built where CMake finds its projection header
// (ULTRACANVAS_HAVE_WINRT_GEOLOCATION; the MSYS2 cppwinrt package or the
// Windows SDK). A request runs on a thread of its own in the multithreaded
// apartment: RequestAccessAsync, then a Geolocator's GetGeopositionAsync,
// both waited for there. Windows decides by "Location services" and "Let
// desktop apps access your location" (Settings > Privacy & security).
// Version: 1.0.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework

#include "UltraCanvasGeolocation.h"

#if defined(_WIN32) && defined(ULTRACANVAS_HAVE_WINRT_GEOLOCATION)

#include <windows.h>

#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>

#include <chrono>
#include <string>
#include <thread>

namespace UltraCanvas {
namespace GeolocationBackend {

namespace {

std::string Narrow(const winrt::hstring& text) {
    return winrt::to_string(text);
}

GeoPositionResult Locate(int timeoutMs) {
    using namespace winrt::Windows::Devices::Geolocation;
    GeoPositionResult result;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        // already initialised on this thread: fine
    }
    try {
        const GeolocationAccessStatus access = Geolocator::RequestAccessAsync().get();
        if (access != GeolocationAccessStatus::Allowed) {
            result.error = access == GeolocationAccessStatus::Denied
                               ? "Windows does not let this program use the location (Settings > Privacy & "
                                 "security > Location)."
                               : "Windows could not say whether this program may use the location.";
            return result;
        }
        Geolocator locator;
        locator.DesiredAccuracy(PositionAccuracy::High);
        const auto maxAge = std::chrono::minutes(1);
        const auto timeout = std::chrono::milliseconds(timeoutMs);
        Geoposition position = locator.GetGeopositionAsync(maxAge, timeout).get();
        const Geocoordinate coordinate = position.Coordinate();
        const BasicGeoposition point = coordinate.Point().Position();
        result.ok = true;
        result.latitude = point.Latitude;
        result.longitude = point.Longitude;
        result.accuracyMeters = coordinate.Accuracy();
    } catch (const winrt::hresult_error& e) {
        result.error = "Windows found no location: " + Narrow(e.message());
    } catch (...) {
        result.error = "Windows found no location.";
    }
    return result;
}

} // namespace

bool Available() {
    return true;
}

std::string Name() {
    return "Windows";
}

void Request(int timeoutMs, std::function<void(const GeoPositionResult&)> answer) {
    std::thread([timeoutMs, answer = std::move(answer)]() {
        const GeoPositionResult r = Locate(timeoutMs);
        winrt::uninit_apartment();
        answer(r);
    }).detach();
}

} // namespace GeolocationBackend
} // namespace UltraCanvas

#endif
