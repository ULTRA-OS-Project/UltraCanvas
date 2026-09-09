// libspecific/Audio/Mp4AudioDemux.h
// Dependency-free ISO base media file format (MP4/M4A/M4B/MOV) audio demuxer.
// Walks moov/trak/mdia/minf/stbl, identifies the audio sample entry and yields
// the per-sample byte ranges plus the codec's setup data (the esds
// AudioSpecificConfig for AAC, the magic cookie for ALAC), so an AAC decoder
// library can be fed raw access units without pulling in a media framework.
// Container parsing only: nothing here decodes audio.
// Version: 0.1.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace AudioCodecs {

// Codecs an MPEG-4 audio sample entry can carry. Anything the demuxer does not
// recognise is reported as Unknown with the raw four-cc kept in `codecName`, so
// callers can say what they found rather than just "unsupported".
enum class Mp4AudioCodec {
    Unknown,
    AAC,        // mp4a with an MPEG-4/MPEG-2 audio object type
    ALAC,       // Apple Lossless
    MP3,        // mp4a with objectTypeIndication 0x69 / 0x6B, or ".mp3"
    AC3,        // ac-3 / ec-3
    PCM         // lpcm / sowt / twos / raw / in24 / fl32 ...
};

// One access unit: a byte range inside the file.
struct Mp4AudioSample {
    uint64_t offset = 0;
    uint32_t size = 0;
};

struct Mp4AudioTrack {
    Mp4AudioCodec codec = Mp4AudioCodec::Unknown;
    std::string   codecName;              // sample-entry four-cc, e.g. "mp4a"
    int           sampleRate = 0;         // from the setup data when available
    int           channels = 0;
    // AAC: the AudioSpecificConfig from the esds DecoderSpecificInfo.
    // ALAC: the magic cookie from the "alac" child box. Empty when the sample
    // entry carried no setup data (valid for PCM, fatal for AAC).
    std::vector<uint8_t> codecConfig;
    std::vector<Mp4AudioSample> samples;  // in decode order
    double        duration = 0.0;         // seconds, from stts / mdhd
    bool          fragmented = false;     // moof-based: stbl holds no samples
};

// True when the first box of the file is a recognisable ISO-BMFF top-level box
// (ftyp/moov/mdat/free/skip/wide/styp). Cheap enough to gate the full parse.
bool LooksLikeIsoBmff(const std::string& path);

// True when the file begins with an ADTS syncword or an ADIF magic — a raw AAC
// bitstream with no container.
bool LooksLikeRawAac(const std::string& path);

// Parse `fileData` and fill `out` with the first audio track found. Returns
// false when the buffer is not ISO-BMFF, carries no audio track, or the sample
// table is unusable. A fragmented file parses far enough to set `fragmented`
// (and the codec/setup data when the moov holds a sample entry) and then
// returns false, because the sample ranges live in moof boxes this demuxer
// deliberately does not walk.
bool Mp4ParseAudioTrack(const uint8_t* fileData, size_t fileSize, Mp4AudioTrack& out);

// Read `path` into memory and hand it to Mp4ParseAudioTrack.
bool Mp4ProbeAudioTrack(const std::string& path, Mp4AudioTrack& out);

// Decode an AudioSpecificConfig header far enough to report the sample rate,
// channel count and audio object type. Returns false on a truncated/invalid
// config. `sampleRate`/`channels`/`objectType` may each be null.
bool ParseAudioSpecificConfig(const uint8_t* asc, size_t size,
                              int* sampleRate, int* channels, int* objectType);

} // namespace AudioCodecs
} // namespace UltraCanvas
