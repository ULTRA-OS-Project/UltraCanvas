// core/UltraRTC/UltraRTCAudioDevices.h
// UltraRTC - the microphone and speaker of a call (internal). The engine
// talks to devices only through this, so the device code - the framework's
// own audio backend - stays in one file.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraRtcInternal {

// Captures from an input device, delivering 48 kHz mono 16-bit PCM in
// whatever block size the backend uses, on the backend's thread.
class MicrophoneCapture {
public:
    virtual ~MicrophoneCapture() = default;
    virtual void Stop() = 0;
};

// Plays 48 kHz mono 16-bit PCM pushed from the network side. Keeps a small
// playout buffer (about 60 ms) and plays silence when it runs dry, so a late
// packet stretches nothing and a burst does not pile up delay.
class SpeakerPlayback {
public:
    virtual ~SpeakerPlayback() = default;
    virtual void Push(const int16_t* samples, size_t count) = 0;
    virtual void Stop() = 0;
};

using PcmCallback = std::function<void(const int16_t* samples, size_t count)>;

// nullptr and `error` when the device cannot be opened (or the build has no
// audio backend).
std::unique_ptr<MicrophoneCapture> OpenMicrophone(const std::string& deviceId, PcmCallback onPcm, std::string& error);
std::unique_ptr<SpeakerPlayback>   OpenSpeaker(const std::string& deviceId, std::string& error);

} // namespace UltraRtcInternal
