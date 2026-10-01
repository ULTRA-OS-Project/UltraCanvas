// Tests/AudioStreamingTest.cpp
// Unit tests for the live-audio building blocks in
// UltraCanvasAudioStreaming.h: the SPSC frame ring the player's sink is
// built on, and the fixed-duration packetiser behind the recorder's live
// capture mode. Header-only, so no audio backend or device is involved.
// Version: 0.1.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "UltraCanvasAudioStreaming.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while (0)

static void TestFramesForMilliseconds() {
    CHECK(AudioFramesForMilliseconds(10, 48000) == 480);
    CHECK(AudioFramesForMilliseconds(20, 16000) == 320);
    CHECK(AudioFramesForMilliseconds(10, 44100) == 441);
    CHECK(AudioFramesForMilliseconds(0, 48000) == 0);
    CHECK(AudioFramesForMilliseconds(10, 0) == 0);
}

static void TestRingBasics() {
    AudioFrameRing ring(4, 2);                 // 4 stereo frames
    CHECK(ring.GetCapacityFrames() == 4);
    CHECK(ring.GetAvailableFrames() == 0);
    CHECK(ring.GetFreeFrames() == 4);

    float in[6] = {1, 2, 3, 4, 5, 6};          // 3 frames
    CHECK(ring.Push(in, 3) == 3);
    CHECK(ring.GetAvailableFrames() == 3);
    CHECK(ring.GetFreeFrames() == 1);

    float out[8] = {0};
    CHECK(ring.Pop(out, 2) == 2);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 4);
    CHECK(ring.GetAvailableFrames() == 1);

    // Wrap around the end of storage: 3 free, push 3 (one of them wraps).
    float in2[6] = {7, 8, 9, 10, 11, 12};
    CHECK(ring.Push(in2, 3) == 3);
    CHECK(ring.GetAvailableFrames() == 4);
    CHECK(ring.GetFreeFrames() == 0);
    CHECK(ring.Pop(out, 4) == 4);
    CHECK(out[0] == 5 && out[1] == 6);
    CHECK(out[2] == 7 && out[3] == 8);
    CHECK(out[4] == 9 && out[5] == 10);
    CHECK(out[6] == 11 && out[7] == 12);
    CHECK(ring.GetAvailableFrames() == 0);

    // Pop from empty: nothing copied, nothing changed.
    CHECK(ring.Pop(out, 2) == 0);
}

static void TestRingOverflowDropsAndCounts() {
    AudioFrameRing ring(3, 1);
    float in[5] = {1, 2, 3, 4, 5};
    CHECK(ring.Push(in, 5) == 3);              // only 3 fit
    CHECK(ring.GetDroppedFrames() == 2);
    CHECK(ring.Push(in, 1) == 0);              // full: all dropped
    CHECK(ring.GetDroppedFrames() == 3);
    float out[3];
    CHECK(ring.Pop(out, 3) == 3);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);
    ring.Clear();
    CHECK(ring.GetAvailableFrames() == 0);
    ring.Push(in, 2);
    ring.Clear();
    CHECK(ring.GetAvailableFrames() == 0);
    CHECK(ring.GetFreeFrames() == 3);
}

static void TestRingTwoThreads() {
    // Producer pushes a counter sequence in odd-sized chunks; consumer pops
    // in other odd sizes; the stream must arrive intact and in order.
    const size_t total = 200000;
    AudioFrameRing ring(1024, 1);
    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        std::vector<float> chunk;
        size_t next = 0;
        size_t sizes[] = {7, 13, 1, 64, 3, 128, 5};
        size_t si = 0;
        while (next < total) {
            size_t n = std::min(sizes[si++ % 7], total - next);
            chunk.resize(n);
            for (size_t i = 0; i < n; ++i) chunk[i] = static_cast<float>(next + i);
            size_t pushed = 0;
            while (pushed < n) {
                size_t got = ring.Push(chunk.data() + pushed, n - pushed);
                pushed += got;
                if (got == 0) std::this_thread::yield();
            }
            next += n;
        }
        producerDone.store(true, std::memory_order_release);
    });

    size_t expect = 0;
    bool ordered = true;
    std::vector<float> out(256);
    size_t sizes[] = {11, 2, 97, 256, 1, 33};
    size_t si = 0;
    while (expect < total) {
        size_t want = sizes[si++ % 6];
        size_t got = ring.Pop(out.data(), want);
        for (size_t i = 0; i < got; ++i) {
            if (out[i] != static_cast<float>(expect + i)) ordered = false;
        }
        expect += got;
        if (got == 0) {
            if (producerDone.load(std::memory_order_acquire) && ring.GetAvailableFrames() == 0 &&
                expect < total) {
                // Would mean frames were lost; the loop below reports it.
                break;
            }
            std::this_thread::yield();
        }
    }
    producer.join();
    CHECK(ordered);
    CHECK(expect == total);
    // GetDroppedFrames() counts every frame a Push could not fit, including
    // the ones this producer then retried, so it is a pacing signal rather
    // than a loss count here; what matters is that nothing was lost or
    // reordered, which the two checks above establish.
}

static void TestPacketizerFixedFrames() {
    AudioFramePacketizer p(480, 1, 48000);     // 10 ms mono at 48 kHz
    std::vector<size_t> sizes;
    std::vector<uint64_t> starts;
    std::vector<float> firstSamples;
    auto sink = [&](const AudioLiveFrame& f) {
        sizes.push_back(f.frameCount);
        starts.push_back(f.firstFrameIndex);
        firstSamples.push_back(f.samples[0]);
        CHECK(f.channels == 1);
        CHECK(f.sampleRate == 48000);
    };

    // Backend period of 441 frames: the first chunk is short, so nothing
    // emits; the second completes one frame and leaves 402 pending.
    std::vector<float> chunk(441);
    for (size_t i = 0; i < 441; ++i) chunk[i] = static_cast<float>(i);
    CHECK(p.Feed(chunk.data(), 441, sink) == 0);
    CHECK(p.GetPendingFrames() == 441);
    for (size_t i = 0; i < 441; ++i) chunk[i] = static_cast<float>(441 + i);
    CHECK(p.Feed(chunk.data(), 441, sink) == 1);
    CHECK(p.GetPendingFrames() == 402);
    CHECK(sizes.size() == 1 && sizes[0] == 480);
    CHECK(starts[0] == 0);
    CHECK(firstSamples[0] == 0.0f);

    // A large chunk: 402 pending + 2000 = 2402 → 5 frames, 2 pending.
    std::vector<float> big(2000, 1.0f);
    CHECK(p.Feed(big.data(), 2000, sink) == 5);
    CHECK(p.GetPendingFrames() == 2);
    CHECK(sizes.size() == 6);
    CHECK(starts[1] == 480 && starts[5] == 480 * 5);

    // Flush pads the 2 pending frames to a full packet.
    CHECK(p.Flush(sink));
    CHECK(sizes.size() == 7 && sizes[6] == 480);
    CHECK(p.GetPendingFrames() == 0);
    CHECK(!p.Flush(sink));                     // nothing left
}

static void TestPacketizerFastPathAndStereo() {
    AudioFramePacketizer p(4, 2, 16000);       // 4 stereo frames per packet
    std::vector<AudioLiveFrame> seen;
    std::vector<std::vector<float>> copies;
    auto sink = [&](const AudioLiveFrame& f) {
        seen.push_back(f);
        copies.emplace_back(f.samples, f.samples + f.frameCount * f.channels);
    };
    // 10 frames = 20 samples: two whole packets straight from the input, 2 frames pending.
    std::vector<float> in(20);
    for (size_t i = 0; i < 20; ++i) in[i] = static_cast<float>(i);
    CHECK(p.Feed(in.data(), 10, sink) == 2);
    CHECK(seen.size() == 2);
    CHECK(seen[0].samples == in.data());       // fast path: no copy
    CHECK(seen[1].samples == in.data() + 8);
    CHECK(p.GetPendingFrames() == 2);
    // 2 more frames complete the third packet from the pending buffer.
    float tail[4] = {100, 101, 102, 103};
    CHECK(p.Feed(tail, 2, sink) == 1);
    CHECK(copies[2].size() == 8);
    CHECK(copies[2][0] == 16 && copies[2][3] == 19 && copies[2][4] == 100 && copies[2][7] == 103);
    CHECK(seen[2].firstFrameIndex == 8);
}

static void TestPacketizerPassThrough() {
    AudioFramePacketizer p(0, 1, 8000);        // frameSize 0: chunks pass through
    size_t calls = 0;
    uint64_t lastStart = 0;
    auto sink = [&](const AudioLiveFrame& f) { ++calls; lastStart = f.firstFrameIndex; };
    float a[3] = {1, 2, 3};
    float b[5] = {4, 5, 6, 7, 8};
    CHECK(p.Feed(a, 3, sink) == 1);
    CHECK(p.Feed(b, 5, sink) == 1);
    CHECK(calls == 2);
    CHECK(lastStart == 3);
    CHECK(!p.Flush(sink));
}

int main() {
    TestFramesForMilliseconds();
    TestRingBasics();
    TestRingOverflowDropsAndCounts();
    TestRingTwoThreads();
    TestPacketizerFixedFrames();
    TestPacketizerFastPathAndStereo();
    TestPacketizerPassThrough();
    if (failures) {
        std::fprintf(stderr, "AudioStreamingTest: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("AudioStreamingTest: all checks passed\n");
    return EXIT_SUCCESS;
}
