// VideoFX/core/VideoFXMusic.h
// Internal: the arithmetic of background music - its volume envelope, the
// ducker that lowers it under the segments' own sound, and the slideshow
// length that matches a song. Pure C++, no FFmpeg, unit-tested.
// Version: 0.5.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFXTypes.h"

#include <cstddef>
#include <string>
#include <vector>

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
// (release), so speech does not make it pump between words. The level, the
// threshold and the three times come from the music settings
// (duckingLevel, duckingThresholdDb, duckingAttack / Hold / Release).
class MusicDucker {
public:
    explicit MusicDucker(const VideoFXMusic& music);
    // Advance by `seconds`; returns the gain (duckingLevel..1) at the block's end
    double Update(double rms, double seconds);
    double Gain() const { return gain; }

private:
    double level;
    double threshold;                             // linear RMS, 0..1
    double attack, hold, release;                 // seconds
    double gain = 1.0;
    double quiet = 1e9;                           // seconds since the sound was last present
};

// Seconds two songs of `first` and `second` seconds blend over: the asked
// `crossfade`, but at most half of either song (a length <= 0 is unknown and
// does not limit it), so a short song is never swallowed by its neighbours
double CrossfadeSeconds(double crossfade, double first, double second);

// How long a song list plays once through: the songs' lengths minus the
// overlaps between them (`start` is cut from the first song). 0 when any
// length is unknown.
double PlaylistSeconds(const std::vector<double>& lengths, double crossfade, double start);

// Gains of the outgoing and incoming song at `x` (0..1) through a crossfade:
// equal power, so the loudness holds steady through the blend
void CrossfadeGains(double x, double& outgoing, double& incoming);

// Seconds per image so `images` photos joined by `transition`-second overlaps
// last `musicSeconds`: n*s - (n-1)*t = music. At least 1 s and twice the
// transition.
double SlideshowSecondsForMusic(double musicSeconds, size_t images, double transition);

} // namespace Internal
} // namespace VideoFX
