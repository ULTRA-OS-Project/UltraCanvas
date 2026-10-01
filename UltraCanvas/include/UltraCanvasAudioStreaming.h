// include/UltraCanvasAudioStreaming.h
// Live-audio building blocks shared by the recorder's live capture mode and
// the player's push sink: a fixed-size frame, a lock-free single-producer /
// single-consumer ring of interleaved float PCM, and a packetiser that turns
// arbitrary backend chunks into frames of one fixed duration.
//
// Header-only and dependency-free so a call stack, a codec test or a unit
// test can use them without an audio backend.
//
// Threading contract: AudioFrameRing has exactly one writer and one reader;
// each side may be any thread, and neither blocks or allocates after
// construction. AudioFramePacketizer is single-threaded (the audio thread).
// Version: 0.1.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASAUDIOSTREAMING_H
#define ULTRACANVASAUDIOSTREAMING_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace UltraCanvas {

// ===== LIVE FRAME =====
// One block of interleaved 32-bit float PCM, as delivered by
// UltraCanvasAudioRecorder::onLiveFrame or pushed into
// UltraCanvasAudioPlayer::PushSinkFrames. `samples` holds
// frameCount * channels values in [-1, 1] and is valid only for the duration
// of the callback that carries it.
struct AudioLiveFrame {
    const float* samples = nullptr;
    size_t frameCount = 0;          // frames (one frame = one sample per channel)
    int channels = 1;
    int sampleRate = 48000;
    uint64_t firstFrameIndex = 0;   // frames since capture started; a timestamp
};

// ===== SPSC FRAME RING =====
// Bounded queue of interleaved float frames. Push on the producer thread, Pop
// on the consumer thread; both are wait-free. Capacity is fixed at
// construction (or Reset) and the storage never moves afterwards.
class AudioFrameRing {
public:
    AudioFrameRing() = default;
    AudioFrameRing(size_t capacityFrames, int channels) { Reset(capacityFrames, channels); }

    // Not thread-safe: call before either side runs, or after both stopped.
    void Reset(size_t capacityFrames, int channels) {
        channelCount = channels > 0 ? channels : 1;
        // One slot is kept empty to tell "full" from "empty".
        capacitySamples = (capacityFrames + 1) * static_cast<size_t>(channelCount);
        storage.assign(capacitySamples, 0.0f);
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        droppedFrames.store(0, std::memory_order_relaxed);
    }

    int GetChannels() const { return channelCount; }
    size_t GetCapacityFrames() const {
        return capacitySamples ? capacitySamples / channelCount - 1 : 0;
    }

    // Frames queued and not yet popped. Safe from either side.
    size_t GetAvailableFrames() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_acquire);
        size_t used = h >= t ? h - t : capacitySamples - t + h;
        return used / channelCount;
    }
    size_t GetFreeFrames() const { return GetCapacityFrames() - GetAvailableFrames(); }

    // Producer. Copies up to `frames`; returns the number actually queued.
    // Frames that do not fit are dropped and counted, never partially written.
    size_t Push(const float* interleaved, size_t frames) {
        if (!interleaved || frames == 0 || capacitySamples == 0) return 0;
        size_t fit = std::min(frames, GetFreeFrames());
        if (fit < frames) droppedFrames.fetch_add(frames - fit, std::memory_order_relaxed);
        if (fit == 0) return 0;
        size_t samples = fit * channelCount;
        size_t h = head.load(std::memory_order_relaxed);
        size_t first = std::min(samples, capacitySamples - h);
        std::memcpy(storage.data() + h, interleaved, first * sizeof(float));
        if (first < samples)
            std::memcpy(storage.data(), interleaved + first, (samples - first) * sizeof(float));
        head.store((h + samples) % capacitySamples, std::memory_order_release);
        return fit;
    }

    // Consumer. Copies up to `frames` into `out`; returns the number copied.
    // The caller zero-fills the remainder when it needs a full block.
    size_t Pop(float* out, size_t frames) {
        if (!out || frames == 0 || capacitySamples == 0) return 0;
        size_t take = std::min(frames, GetAvailableFrames());
        if (take == 0) return 0;
        size_t samples = take * channelCount;
        size_t t = tail.load(std::memory_order_relaxed);
        size_t first = std::min(samples, capacitySamples - t);
        std::memcpy(out, storage.data() + t, first * sizeof(float));
        if (first < samples)
            std::memcpy(out + first, storage.data(), (samples - first) * sizeof(float));
        tail.store((t + samples) % capacitySamples, std::memory_order_release);
        return take;
    }

    // Consumer side: discard everything queued.
    void Clear() { tail.store(head.load(std::memory_order_acquire), std::memory_order_release); }

    uint64_t GetDroppedFrames() const { return droppedFrames.load(std::memory_order_relaxed); }

private:
    std::vector<float> storage;
    size_t capacitySamples = 0;
    int channelCount = 1;
    alignas(64) std::atomic<size_t> head{0};   // next write index, samples
    alignas(64) std::atomic<size_t> tail{0};   // next read index, samples
    std::atomic<uint64_t> droppedFrames{0};
};

// ===== FIXED-DURATION PACKETISER =====
// Backends hand over whatever block size the device likes; a codec or a call
// wants exactly 10 ms (or 20 ms) per frame. Feed it chunks of any length and
// it emits complete frames of `frameSize` frames, keeping the remainder for
// the next chunk. `firstFrameIndex` on each emitted frame counts frames since
// Reset, so consumers can timestamp without a clock.
class AudioFramePacketizer {
public:
    AudioFramePacketizer() = default;
    AudioFramePacketizer(size_t framesPerPacket, int channels, int sampleRate) {
        Reset(framesPerPacket, channels, sampleRate);
    }

    void Reset(size_t framesPerPacket, int channels, int sampleRate) {
        frameSize = framesPerPacket;
        channelCount = channels > 0 ? channels : 1;
        rate = sampleRate;
        pending.clear();
        pending.reserve(frameSize * channelCount * 2);
        emitted = 0;
    }

    size_t GetFrameSize() const { return frameSize; }
    size_t GetPendingFrames() const { return channelCount ? pending.size() / channelCount : 0; }

    // Returns the number of complete frames emitted through `sink`. With a
    // frameSize of 0 the chunk is passed through as one frame, unchanged.
    size_t Feed(const float* interleaved, size_t frames,
                const std::function<void(const AudioLiveFrame&)>& sink) {
        if (!interleaved || frames == 0 || !sink) return 0;
        if (frameSize == 0) {
            sink(Make(interleaved, frames));
            emitted += frames;
            return 1;
        }
        size_t out = 0;
        size_t packetSamples = frameSize * channelCount;
        // Fast path: nothing pending, so whole packets can go straight from
        // the input without a copy.
        size_t offset = 0;
        if (pending.empty()) {
            while (frames - offset >= frameSize) {
                sink(Make(interleaved + offset * channelCount, frameSize));
                emitted += frameSize;
                offset += frameSize;
                ++out;
            }
        }
        if (offset < frames) {
            pending.insert(pending.end(), interleaved + offset * channelCount,
                           interleaved + frames * channelCount);
            size_t consumed = 0;
            while (pending.size() - consumed >= packetSamples) {
                sink(Make(pending.data() + consumed, frameSize));
                emitted += frameSize;
                consumed += packetSamples;
                ++out;
            }
            if (consumed) pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
        return out;
    }

    // Emit whatever is pending, zero-padded to a full frame. Returns false
    // when nothing was pending.
    bool Flush(const std::function<void(const AudioLiveFrame&)>& sink) {
        if (pending.empty() || !sink || frameSize == 0) return false;
        pending.resize(frameSize * channelCount, 0.0f);
        sink(Make(pending.data(), frameSize));
        emitted += frameSize;
        pending.clear();
        return true;
    }

private:
    AudioLiveFrame Make(const float* samples, size_t frames) const {
        AudioLiveFrame f;
        f.samples = samples;
        f.frameCount = frames;
        f.channels = channelCount;
        f.sampleRate = rate;
        f.firstFrameIndex = emitted;
        return f;
    }

    size_t frameSize = 0;
    int channelCount = 1;
    int rate = 48000;
    std::vector<float> pending;
    uint64_t emitted = 0;
};

// Frames in `ms` milliseconds at `sampleRate`, rounded to the nearest frame.
inline size_t AudioFramesForMilliseconds(int ms, int sampleRate) {
    if (ms <= 0 || sampleRate <= 0) return 0;
    return static_cast<size_t>((static_cast<int64_t>(ms) * sampleRate + 500) / 1000);
}

} // namespace UltraCanvas

#endif // ULTRACANVASAUDIOSTREAMING_H
