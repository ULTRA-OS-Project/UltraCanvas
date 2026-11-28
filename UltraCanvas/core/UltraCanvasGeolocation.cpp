// core/UltraCanvasGeolocation.cpp
// See the header: the answer moved to the UI thread, the time limit, and
// the backend for builds without one.
// Version: 1.0.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework

#include "UltraCanvasGeolocation.h"

#include "UltraCanvasApplication.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>

namespace UltraCanvas {

namespace {

// One request's answer: whichever comes first - the backend's or the time
// limit's - is the one `done` hears
struct Pending {
    std::mutex mutex;
    bool answered = false;
    std::function<void(const GeoPositionResult&)> done;
    TimerId timer = 0;
};

void Deliver(const std::shared_ptr<Pending>& pending, const GeoPositionResult& result) {
    {
        std::lock_guard<std::mutex> lock(pending->mutex);
        if (pending->answered) return;
        pending->answered = true;
    }
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) return;
    app->PostToUIThread([pending, result]() {
        if (pending->timer) {
            if (auto* a = UltraCanvasApplicationBase::GetCurrent()) a->StopTimer(pending->timer);
            pending->timer = 0;
        }
        if (pending->done) pending->done(result);
        pending->done = nullptr;
    });
}

} // namespace

bool UltraCanvasGeolocation::IsAvailable() {
    return GeolocationBackend::Available();
}

std::string UltraCanvasGeolocation::BackendName() {
    return GeolocationBackend::Name();
}

void UltraCanvasGeolocation::RequestPosition(std::function<void(const GeoPositionResult&)> done, int timeoutMs) {
    auto pending = std::make_shared<Pending>();
    pending->done = std::move(done);
    if (timeoutMs <= 0) timeoutMs = 20000;
    if (!GeolocationBackend::Available()) {
        GeoPositionResult r;
        r.error = Tr("This system cannot tell where it is.");
        Deliver(pending, r);
        return;
    }
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        pending->timer = app->StartTimer(static_cast<unsigned>(timeoutMs), false, [pending](TimerId) {
            pending->timer = 0;
            GeoPositionResult r;
            r.error = Tr("The location service did not answer in time.");
            Deliver(pending, r);
        });
    }
    // A little longer for the backend's own wait, so the core's limit is
    // the one heard
    GeolocationBackend::Request(timeoutMs + 1000, [pending](const GeoPositionResult& r) { Deliver(pending, r); });
}

std::string UltraCanvasGeolocation::AccuracyText(double accuracyMeters) {
    if (accuracyMeters < 0) return {};
    if (accuracyMeters < 1000) {
        const long m = std::lround(accuracyMeters < 10 ? accuracyMeters : std::round(accuracyMeters / 10) * 10);
        return TrF("about {0} m", std::to_string(m));
    }
    const long km = std::lround(accuracyMeters / 1000.0);
    return TrF("about {0} km", std::to_string(km));
}

#ifndef ULTRACANVAS_GEOLOCATION_NATIVE
namespace GeolocationBackend {
bool Available() { return false; }
std::string Name() { return "none"; }
void Request(int, std::function<void(const GeoPositionResult&)> answer) {
    GeoPositionResult r;
    r.error = "This system cannot tell where it is.";
    answer(r);
}
} // namespace GeolocationBackend
#endif

} // namespace UltraCanvas
