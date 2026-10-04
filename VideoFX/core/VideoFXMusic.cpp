// VideoFX/core/VideoFXMusic.cpp
// Background music arithmetic: envelope, ducking, slideshow length.
// Version: 0.4.1
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework

#include "VideoFXMusic.h"

#include <algorithm>
#include <cmath>

namespace VideoFX {
namespace Internal {

namespace {
    bool InRange(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
}

bool ValidateMusic(const VideoFXMusic& m, std::string& error) {
    if (!m.IsSet()) return true;
    if (!InRange(m.volume, 0.0, 4.0)) { error = "Music volume must be 0..4"; return false; }
    if (!InRange(m.start, 0.0, 1e6)) { error = "Music start must be >= 0"; return false; }
    if (!InRange(m.fadeIn, 0.0, 600.0) || !InRange(m.fadeOut, 0.0, 600.0)) {
        error = "Music fades must be 0..600 seconds";
        return false;
    }
    if (!InRange(m.duckingLevel, 0.0, 1.0)) { error = "Music ducking level must be 0..1"; return false; }
    if (!InRange(m.duckingThresholdDb, -90.0, 0.0)) {
        error = "Music ducking threshold must be -90..0 dBFS";
        return false;
    }
    if (!InRange(m.duckingAttack, 0.001, 10.0)) { error = "Music ducking attack must be 0.001..10 seconds"; return false; }
    if (!InRange(m.duckingHold, 0.0, 30.0)) { error = "Music ducking hold must be 0..30 seconds"; return false; }
    if (!InRange(m.duckingRelease, 0.001, 30.0)) {
        error = "Music ducking release must be 0.001..30 seconds";
        return false;
    }
    return true;
}

double MusicEnvelope(const VideoFXMusic& m, double t, double total) {
    double g = m.volume;
    if (m.fadeIn > 0.0) g *= std::clamp(t / m.fadeIn, 0.0, 1.0);
    if (m.fadeOut > 0.0 && total > 0.0) g *= std::clamp((total - t) / m.fadeOut, 0.0, 1.0);
    return g;
}

MusicDucker::MusicDucker(const VideoFXMusic& m)
    : level(m.duckingLevel),
      threshold(std::pow(10.0, m.duckingThresholdDb / 20.0)),
      attack(m.duckingAttack),
      hold(m.duckingHold),
      release(m.duckingRelease) {}

double MusicDucker::Update(double rms, double seconds) {
    if (level >= 1.0 || seconds <= 0.0) return gain;
    if (rms > threshold) quiet = 0.0;
    else quiet += seconds;
    const bool duck = quiet < hold;
    const double target = duck ? level : 1.0;
    const double tau = target < gain ? attack : release;
    gain += (target - gain) * (1.0 - std::exp(-seconds / tau));
    return gain;
}

double SlideshowSecondsForMusic(double musicSeconds, size_t images, double transition) {
    if (images == 0 || !(musicSeconds > 0.0)) return 0.0;
    const double n = static_cast<double>(images);
    const double t = std::max(0.0, transition);
    const double s = (musicSeconds + (n - 1.0) * t) / n;
    return std::max({s, 1.0, 2.0 * t});
}

} // namespace Internal
} // namespace VideoFX
