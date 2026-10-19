// VideoFX/core/VideoFXBeats.cpp
// Beat detection (onset envelope, tempo, beat tracking) and beat-aligned
// slideshow changes.
// Version: 0.5.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "VideoFXBeats.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

namespace VideoFX {
namespace Internal {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kFrame = 512;                 // samples per spectrum: 46 ms at 11025 Hz
constexpr double kMinConfidence = 0.12;     // below this the envelope has no steady beat (noise: ~0.07)

// In-place radix-2 FFT; size a power of two
void Fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

double Median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

} // namespace

std::vector<double> OnsetEnvelope(const std::vector<float>& mono, int sampleRate, int hop) {
    std::vector<double> env;
    if (mono.empty() || sampleRate <= 0 || hop <= 0) return env;
    std::vector<double> window(kFrame);
    for (int i = 0; i < kFrame; ++i) window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / kFrame);
    const size_t frames = mono.size() / static_cast<size_t>(hop) + 1;
    std::vector<double> previous(kFrame / 2, 0.0), current(kFrame / 2);
    std::vector<std::complex<double>> buf(kFrame);
    env.reserve(frames);
    for (size_t f = 0; f < frames; ++f) {
        // Frame f is centred on sample f * hop
        const long long first = static_cast<long long>(f) * hop - kFrame / 2;
        for (int i = 0; i < kFrame; ++i) {
            const long long s = first + i;
            const double v = s >= 0 && s < static_cast<long long>(mono.size()) ? mono[static_cast<size_t>(s)] : 0.0;
            buf[i] = std::complex<double>(v * window[i], 0.0);
        }
        Fft(buf);
        double flux = 0.0;
        for (int k = 0; k < kFrame / 2; ++k) {
            current[k] = std::log1p(1000.0 * std::abs(buf[k]) / (kFrame / 2));
            flux += std::max(0.0, current[k] - previous[k]);   // frame 0 rises from silence
        }
        std::swap(previous, current);
        env.push_back(flux);
    }
    // Take off the local mean (a quarter-second either side), keep the rises
    const int half = std::max(1, static_cast<int>(0.25 * sampleRate / hop));
    std::vector<double> sum(env.size() + 1, 0.0);
    for (size_t i = 0; i < env.size(); ++i) sum[i + 1] = sum[i] + env[i];
    std::vector<double> out(env.size());
    for (size_t i = 0; i < env.size(); ++i) {
        const size_t lo = i >= static_cast<size_t>(half) ? i - half : 0;
        const size_t hi = std::min(env.size(), i + half + 1);
        out[i] = std::max(0.0, env[i] - (sum[hi] - sum[lo]) / static_cast<double>(hi - lo));
    }
    return out;
}

double EstimateTempo(const std::vector<double>& envelope, double hopSeconds, double& confidence) {
    confidence = 0.0;
    if (envelope.size() < 8 || hopSeconds <= 0.0) return 0.0;
    // Smoothed over about +-3 hops first: onsets are a hop or two wide, and a
    // period falling between two hops would otherwise split its peak and lose
    // to its own half tempo, whose double lag may land on one hop
    std::vector<double> e = envelope;
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<double> next(e.size(), 0.0);
        static constexpr double kKernel[5] = {1.0 / 16, 4.0 / 16, 6.0 / 16, 4.0 / 16, 1.0 / 16};
        for (size_t i = 0; i < e.size(); ++i)
            for (int k = -2; k <= 2; ++k) {
                const long long j = static_cast<long long>(i) + k;
                if (j >= 0 && j < static_cast<long long>(e.size())) next[i] += kKernel[k + 2] * e[static_cast<size_t>(j)];
            }
        e.swap(next);
    }
    const double mean = std::accumulate(e.begin(), e.end(), 0.0) / e.size();
    for (double& v : e) v -= mean;
    auto ac = [&](size_t lag) {
        double s = 0.0;
        for (size_t i = 0; i + lag < e.size(); ++i) s += e[i] * e[i + lag];
        return s / static_cast<double>(e.size() - lag);
    };
    const double ac0 = ac(0);
    if (!(ac0 > 1e-12)) return 0.0;
    const size_t minLag = std::max<size_t>(1, static_cast<size_t>(std::floor(60.0 / 240.0 / hopSeconds)));
    const size_t maxLag = std::min(e.size() / 2, static_cast<size_t>(std::ceil(60.0 / 40.0 / hopSeconds)));
    if (maxLag <= minLag + 1) return 0.0;
    // Autocorrelation weighted by a log-normal prior around 120 BPM (one octave wide)
    std::vector<double> raw(maxLag + 2, 0.0), weighted(maxLag + 2, 0.0);
    for (size_t lag = minLag - 1; lag <= maxLag + 1 && lag < e.size(); ++lag) {
        raw[lag] = ac(lag);
        const double bpm = 60.0 / (static_cast<double>(std::max<size_t>(lag, 1)) * hopSeconds);
        const double octaves = std::log2(bpm / 120.0);
        weighted[lag] = raw[lag] * std::exp(-0.5 * octaves * octaves);
    }
    size_t best = minLag;
    for (size_t lag = minLag; lag <= maxLag; ++lag)
        if (weighted[lag] > weighted[best]) best = lag;
    if (!(raw[best] > 0.0)) return 0.0;
    // Parabolic interpolation for a lag between hops
    double lag = static_cast<double>(best);
    const double a = weighted[best - 1], b = weighted[best], c = weighted[best + 1];
    const double denom = a - 2.0 * b + c;
    if (denom < 0.0) lag += std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
    confidence = std::clamp(raw[best] / ac0, 0.0, 1.0);
    return 60.0 / (lag * hopSeconds);
}

std::vector<double> TrackBeats(const std::vector<double>& envelope, double hopSeconds, double periodSeconds) {
    std::vector<double> beats;
    const size_t n = envelope.size();
    if (n == 0 || hopSeconds <= 0.0 || periodSeconds <= 0.0) return beats;
    const double period = periodSeconds / hopSeconds;          // in hops
    if (period < 2.0) return beats;
    double sd = 0.0;
    for (double v : envelope) sd += v * v;
    sd = std::sqrt(sd / n);
    if (!(sd > 0.0)) return beats;
    // Each hop's best score as the latest beat of a chain: its own onset plus
    // the best earlier beat, penalised the further the gap is from one period
    constexpr double kTightness = 100.0;
    std::vector<double> score(n);
    std::vector<long long> from(n, -1);
    const long long lo = static_cast<long long>(std::llround(2.0 * period));
    const long long hi = std::max<long long>(1, static_cast<long long>(std::llround(period / 2.0)));
    for (size_t t = 0; t < n; ++t) {
        const double local = envelope[t] / sd;
        double best = 0.0;
        long long at = -1;
        for (long long tau = static_cast<long long>(t) - lo; tau <= static_cast<long long>(t) - hi; ++tau) {
            if (tau < 0) continue;
            const double r = std::log(static_cast<double>(static_cast<long long>(t) - tau) / period);
            const double cand = score[static_cast<size_t>(tau)] - kTightness * r * r;
            if (at < 0 || cand > best) { best = cand; at = tau; }
        }
        score[t] = local + (at >= 0 ? std::max(0.0, best) : 0.0);
        from[t] = at >= 0 && best > 0.0 ? at : -1;
    }
    // The chain ends at the best score within the last period
    const size_t tailFrom = n > static_cast<size_t>(period) ? n - static_cast<size_t>(period) : 0;
    size_t end = tailFrom;
    for (size_t t = tailFrom; t < n; ++t)
        if (score[t] > score[end]) end = t;
    std::vector<size_t> chain;
    for (long long t = static_cast<long long>(end); t >= 0; t = from[static_cast<size_t>(t)])
        chain.push_back(static_cast<size_t>(t));
    std::reverse(chain.begin(), chain.end());
    for (size_t t : chain) beats.push_back(static_cast<double>(t) * hopSeconds);
    return beats;
}

BeatAnalysis AnalyseBeats(const std::vector<float>& mono, int sampleRate) {
    BeatAnalysis a;
    const int hop = std::max(1, sampleRate / 86);              // about 11.6 ms
    const double hopSeconds = static_cast<double>(hop) / sampleRate;
    const std::vector<double> env = OnsetEnvelope(mono, sampleRate, hop);
    double confidence = 0.0;
    const double bpm = EstimateTempo(env, hopSeconds, confidence);
    if (!(bpm > 0.0) || confidence < kMinConfidence) return a;
    a.bpm = bpm;
    a.confidence = confidence;
    a.beats = TrackBeats(env, hopSeconds, 60.0 / bpm);
    // The onset of a frame peaks a quarter frame before the sound reaches its
    // centre (the window's steepest rise): move the beats onto the sound
    const double lead = kFrame / 4.0 / sampleRate;
    for (double& b : a.beats) b += lead;
    return a;
}

std::vector<double> BeatAlignedChanges(const std::vector<double>& beats, size_t images, double secondsPerImage,
                                       double minGap, int beatsPerImage) {
    std::vector<double> changes;
    if (images < 2) return changes;
    const double gap = std::max(minGap, 0.0);
    std::vector<double> diffs;
    for (size_t i = 1; i < beats.size(); ++i) diffs.push_back(beats[i] - beats[i - 1]);
    const double period = Median(diffs);
    if (beats.size() < 2 || !(period > 0.0)) {
        const double step = std::max(secondsPerImage, gap);
        for (size_t k = 1; k < images; ++k) changes.push_back(step * static_cast<double>(k));
        return changes;
    }
    // Beat i, on the detected beats and past them on a grid at the median period
    const long long count = static_cast<long long>(beats.size());
    auto beatAt = [&](long long i) {
        if (i < 0) return beats.front() + static_cast<double>(i) * period;
        if (i >= count) return beats.back() + static_cast<double>(i - count + 1) * period;
        return beats[static_cast<size_t>(i)];
    };
    long long index = -static_cast<long long>(std::ceil(beats.front() / period));  // the grid's beat at or after 0
    while (beatAt(index) < 0.0) ++index;

    if (beatsPerImage > 0) {
        const long long step = std::max<long long>(beatsPerImage,
                                                   static_cast<long long>(std::ceil(gap / period - 0.1)));
        for (size_t k = 1; k < images; ++k) {
            index += step;
            changes.push_back(beatAt(index));
        }
        return changes;
    }
    // Detected beats wobble by a few milliseconds: one a hair short of the
    // minimum gap still counts, rather than pushing the change a beat later
    constexpr double kSlack = 0.05;
    double previous = 0.0;
    for (size_t k = 1; k < images; ++k) {
        const double target = previous + secondsPerImage;
        while (beatAt(index) < previous + gap - kSlack) ++index;
        long long best = index;
        for (long long i = index; beatAt(i) <= target + period; ++i)
            if (std::abs(beatAt(i) - target) < std::abs(beatAt(best) - target)) best = i;
        changes.push_back(beatAt(best));
        previous = beatAt(best);
        index = best + 1;
    }
    return changes;
}

} // namespace Internal
} // namespace VideoFX
