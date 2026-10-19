// VideoFX/core/VideoFXBeats.h
// Internal: beat detection in a song and the slideshow changes that land on
// its beats. Pure C++, no FFmpeg, unit-tested.
//
//   samples ─ spectral flux ─ onset envelope ─┬─ autocorrelation ─ tempo
//                                             └─ dynamic programming ─ beats
//
// The tempo is the autocorrelation peak of the onset envelope, weighted
// towards 120 BPM so a song is not read at half or double speed; the beats
// are the best-scoring chain of onsets about one period apart (Ellis 2007).
// Version: 0.5.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework
#pragma once

#include <cstddef>
#include <vector>

namespace VideoFX {
namespace Internal {

// Sample rate the analysis runs at: enough for drums and bass, cheap to decode
constexpr int kBeatSampleRate = 11025;

struct BeatAnalysis {
    double bpm = 0.0;               // 0 = no steady beat found
    double confidence = 0.0;        // 0..1, how clearly the envelope repeats at that tempo
    std::vector<double> beats;      // seconds from the start of the samples
};

// Onset strength per hop of `hop` samples: the summed rise of the
// log-magnitude spectrum, with its local mean taken off
std::vector<double> OnsetEnvelope(const std::vector<float>& mono, int sampleRate, int hop);

// Beats per minute of an onset envelope sampled every `hopSeconds`, 40..240
double EstimateTempo(const std::vector<double>& envelope, double hopSeconds, double& confidence);

// Beat times (seconds) of an envelope at the given beat period
std::vector<double> TrackBeats(const std::vector<double>& envelope, double hopSeconds, double periodSeconds);

// The whole analysis of a mono signal
BeatAnalysis AnalyseBeats(const std::vector<float>& mono, int sampleRate);

// When the images of a slideshow change: `images - 1` times, each on a beat
// of `beats` (sorted seconds; extended past either end at their median
// period). beatsPerImage > 0: every image lasts that many beats. Otherwise
// each change is the beat nearest `secondsPerImage` after the one before,
// but at least `minGap` later (less 50 ms, for the beats' own wobble).
// Without beats the changes fall every secondsPerImage.
std::vector<double> BeatAlignedChanges(const std::vector<double>& beats, size_t images, double secondsPerImage,
                                       double minGap, int beatsPerImage);

} // namespace Internal
} // namespace VideoFX
