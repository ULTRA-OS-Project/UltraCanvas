// VideoFX/core/VideoFXMusic.h
// Internal: the arithmetic of background music - its volume envelope, the
// ducker that lowers it under the segments' own sound, and the slideshow
// length that matches a song. Pure C++, no FFmpeg, unit-tested.
// Version: 0.4.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFXTypes.h"

#include <cstddef>
#include <string>

namespace VideoFX {
namespace Internal {

// Check a music setting's values (the file itself is checked by the exporter)
bool ValidateMusic(const VideoFXMusic& music, std::string& error);

// Gain at `t` seconds of an export lasting `total` seconds (0 = unknown, no
// fade-out): volume x fade-in x fade-out, before ducking
double MusicEnvelope(const VideoFXMusic& music, double t, double total);

// Lowers the music while the segments' own sound is present. Fed one block
// at a time with that sound's RMS level (0..1 full scale): goes down quickly
// (attack), waits for a pause before coming back (hold), comes back slowly
// (release), so speech does not make it pump between words.
class MusicDucker {
public:
    explicit MusicDucker(double duckingLevel) : level(duckingLevel) {}
    // Advance by `seconds`; returns the gain (duckingLevel..1) at the block's end
    double Update(double rms, double seconds);
    double Gain() const { return gain; }

    static constexpr double kThreshold = 0.015;   // about -36 dBFS: speech, not hiss
    static constexpr double kAttack = 0.12;       // seconds (time constant)
    static constexpr double kHold = 0.6;
    static constexpr double kRelease = 0.8;

private:
    double level;
    double gain = 1.0;
    double quiet = 1e9;                           // seconds since the sound was last present
};

// Seconds per image so `images` photos joined by `transition`-second overlaps
// last `musicSeconds`: n*s - (n-1)*t = music. At least 1 s and twice the
// transition.
double SlideshowSecondsForMusic(double musicSeconds, size_t images, double transition);

} // namespace Internal
} // namespace VideoFX
