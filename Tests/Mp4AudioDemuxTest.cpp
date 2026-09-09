// Tests/Mp4AudioDemuxTest.cpp
// Headless unit test for the ISO-BMFF audio demuxer used by the AAC/M4A decode
// path. Builds MP4 buffers in memory (no media assets required) and checks that
// the demuxer reports the right codec, setup data, sample ranges and duration -
// including the 64-bit-offset, uniform-sample-size and multi-chunk layouts real
// encoders produce. Exits non-zero on any failure.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "Mp4AudioDemux.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace UltraCanvas::AudioCodecs;

namespace {

int failures = 0;

void Check(bool cond, const std::string& what) {
    if (cond) {
        std::printf("  ok   %s\n", what.c_str());
    } else {
        std::printf("  FAIL %s\n", what.c_str());
        ++failures;
    }
}

// ===== MP4 BUILDING HELPERS =====

using Bytes = std::vector<uint8_t>;

void PutU16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void PutU32(Bytes& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void PutU64(Bytes& b, uint64_t v) {
    PutU32(b, static_cast<uint32_t>(v >> 32));
    PutU32(b, static_cast<uint32_t>(v));
}
void Append(Bytes& b, const Bytes& other) { b.insert(b.end(), other.begin(), other.end()); }

// A plain 32-bit-size box.
Bytes Box(const char type[5], const Bytes& body) {
    Bytes out;
    PutU32(out, static_cast<uint32_t>(8 + body.size()));
    out.insert(out.end(), type, type + 4);
    Append(out, body);
    return out;
}

// A box using the 64-bit largesize form (size field == 1).
Bytes LargeBox(const char type[5], const Bytes& body) {
    Bytes out;
    PutU32(out, 1);
    out.insert(out.end(), type, type + 4);
    PutU64(out, 16 + body.size());
    Append(out, body);
    return out;
}

// esds carrying an ES_Descriptor -> DecoderConfigDescriptor -> DSI chain.
// Lengths stay under 128 so the single-byte length form is enough.
Bytes MakeEsds(uint8_t objectTypeIndication, const Bytes& asc) {
    Bytes dsi;
    dsi.push_back(0x05);
    dsi.push_back(static_cast<uint8_t>(asc.size()));
    Append(dsi, asc);

    Bytes dcd;
    dcd.push_back(objectTypeIndication);
    dcd.push_back(0x15);                    // streamType=audio, upStream=0
    PutU32(dcd, 0); dcd.resize(dcd.size() - 1);   // bufferSizeDB is 24-bit
    PutU32(dcd, 0);                         // maxBitrate
    PutU32(dcd, 0);                         // avgBitrate
    Append(dcd, dsi);

    Bytes dcdTagged;
    dcdTagged.push_back(0x04);
    dcdTagged.push_back(static_cast<uint8_t>(dcd.size()));
    Append(dcdTagged, dcd);

    Bytes es;
    PutU16(es, 1);                          // ES_ID
    es.push_back(0x00);                     // no dependency / URL / OCR
    Append(es, dcdTagged);

    Bytes esTagged;
    esTagged.push_back(0x03);
    esTagged.push_back(static_cast<uint8_t>(es.size()));
    Append(esTagged, es);

    Bytes body;
    PutU32(body, 0);                        // version + flags
    Append(body, esTagged);
    return Box("esds", body);
}

// A version-0 AudioSampleEntry with the given four-cc and child boxes.
Bytes MakeAudioSampleEntry(const char type[5], uint16_t channels,
                           uint32_t sampleRate, const Bytes& children) {
    Bytes body;
    for (int i = 0; i < 6; ++i) body.push_back(0);   // reserved
    PutU16(body, 1);                                 // data_reference_index
    PutU16(body, 0);                                 // version
    PutU16(body, 0);                                 // revision
    PutU32(body, 0);                                 // vendor
    PutU16(body, channels);
    PutU16(body, 16);                                // sample size
    PutU16(body, 0);                                 // compression id
    PutU16(body, 0);                                 // packet size
    PutU32(body, sampleRate << 16);                  // 16.16 fixed point
    Append(body, children);
    return Box(type, body);
}

Bytes MakeStsd(const Bytes& sampleEntry) {
    Bytes body;
    PutU32(body, 0);            // version + flags
    PutU32(body, 1);            // entry_count
    Append(body, sampleEntry);
    return Box("stsd", body);
}

Bytes MakeStts(uint32_t sampleCount, uint32_t delta) {
    Bytes body;
    PutU32(body, 0);
    PutU32(body, 1);
    PutU32(body, sampleCount);
    PutU32(body, delta);
    return Box("stts", body);
}

Bytes MakeStsc(const std::vector<std::pair<uint32_t, uint32_t>>& runs) {
    Bytes body;
    PutU32(body, 0);
    PutU32(body, static_cast<uint32_t>(runs.size()));
    for (const auto& r : runs) {
        PutU32(body, r.first);      // first_chunk (1-based)
        PutU32(body, r.second);     // samples_per_chunk
        PutU32(body, 1);            // sample_description_index
    }
    return Box("stsc", body);
}

Bytes MakeStsz(uint32_t uniformSize, const std::vector<uint32_t>& sizes) {
    Bytes body;
    PutU32(body, 0);
    PutU32(body, uniformSize);
    PutU32(body, uniformSize ? static_cast<uint32_t>(sizes.size())
                             : static_cast<uint32_t>(sizes.size()));
    if (uniformSize == 0) {
        for (uint32_t s : sizes) PutU32(body, s);
    }
    return Box("stsz", body);
}

Bytes MakeStco(const std::vector<uint64_t>& offsets, bool use64) {
    Bytes body;
    PutU32(body, 0);
    PutU32(body, static_cast<uint32_t>(offsets.size()));
    for (uint64_t o : offsets) {
        if (use64) PutU64(body, o); else PutU32(body, static_cast<uint32_t>(o));
    }
    return Box(use64 ? "co64" : "stco", body);
}

Bytes MakeMdhd(uint32_t timescale, uint32_t duration) {
    Bytes body;
    PutU32(body, 0);            // version 0 + flags
    PutU32(body, 0);            // creation
    PutU32(body, 0);            // modification
    PutU32(body, timescale);
    PutU32(body, duration);
    PutU16(body, 0x55C4);       // language
    PutU16(body, 0);
    return Box("mdhd", body);
}

Bytes MakeHdlr(const char handler[5]) {
    Bytes body;
    PutU32(body, 0);
    PutU32(body, 0);
    body.insert(body.end(), handler, handler + 4);
    for (int i = 0; i < 12; ++i) body.push_back(0);   // reserved
    body.push_back(0);                                // empty name
    return Box("hdlr", body);
}

// Description of the track to synthesise, so each case only states what it
// cares about.
struct TrackSpec {
    std::string sampleEntryType = "mp4a";
    Bytes children;                                   // esds / alac / wave
    uint16_t channels = 2;
    uint32_t sampleRate = 44100;
    uint32_t timescale = 44100;
    uint32_t frameDelta = 1024;
    std::vector<uint32_t> sampleSizes;
    uint32_t uniformSize = 0;
    std::vector<std::pair<uint32_t, uint32_t>> stsc = { {1, 1} };
    bool use64BitOffsets = false;
    const char* handler = "soun";
};

// Build a complete file: ftyp, then mdat holding the sample payload, then moov
// whose chunk offsets point into that mdat. mdat-before-moov is the layout a
// streaming encoder writes, so parsing it exercises the out-of-order case.
Bytes BuildMp4(const TrackSpec& spec, uint32_t sampleCount, Bytes& mdatPayloadOut) {
    Bytes ftyp;
    const char brand[] = "M4A ";
    ftyp.insert(ftyp.end(), brand, brand + 4);
    PutU32(ftyp, 0);
    ftyp.insert(ftyp.end(), brand, brand + 4);
    Bytes ftypBox = Box("ftyp", ftyp);

    // Sample payload: byte i of sample n is (n + 1), so a mis-sliced range is
    // immediately visible in the assertions.
    std::vector<uint32_t> sizes = spec.sampleSizes;
    if (sizes.empty()) sizes.assign(sampleCount, spec.uniformSize ? spec.uniformSize : 16);

    mdatPayloadOut.clear();
    std::vector<uint64_t> localOffsets;         // offsets within the mdat payload
    for (uint32_t i = 0; i < sampleCount; ++i) {
        localOffsets.push_back(mdatPayloadOut.size());
        for (uint32_t b = 0; b < sizes[i]; ++b) {
            mdatPayloadOut.push_back(static_cast<uint8_t>(i + 1));
        }
    }

    const uint64_t mdatBodyStart = ftypBox.size() + 8;   // after the mdat header

    // Chunk offsets: one entry per chunk, derived from the stsc runs.
    std::vector<uint64_t> chunkOffsets;
    {
        uint32_t sampleIndex = 0;
        size_t run = 0;
        uint32_t chunk = 0;
        while (sampleIndex < sampleCount) {
            while (run + 1 < spec.stsc.size() && spec.stsc[run + 1].first <= chunk + 1) ++run;
            chunkOffsets.push_back(mdatBodyStart + localOffsets[sampleIndex]);
            sampleIndex += spec.stsc[run].second;
            ++chunk;
        }
    }

    Bytes stbl;
    Append(stbl, MakeStsd(MakeAudioSampleEntry(spec.sampleEntryType.c_str(), spec.channels,
                                               spec.sampleRate, spec.children)));
    Append(stbl, MakeStts(sampleCount, spec.frameDelta));
    Append(stbl, MakeStsc(spec.stsc));
    Append(stbl, MakeStsz(spec.uniformSize, sizes));
    Append(stbl, MakeStco(chunkOffsets, spec.use64BitOffsets));

    Bytes minf;
    Append(minf, Box("smhd", Bytes(8, 0)));
    Append(minf, Box("stbl", stbl));

    Bytes mdia;
    Append(mdia, MakeMdhd(spec.timescale, sampleCount * spec.frameDelta));
    Append(mdia, MakeHdlr(spec.handler));
    Append(mdia, Box("minf", minf));

    Bytes trak;
    Append(trak, Box("tkhd", Bytes(84, 0)));
    Append(trak, Box("mdia", mdia));

    Bytes moov;
    Append(moov, Box("mvhd", Bytes(100, 0)));
    Append(moov, Box("trak", trak));

    Bytes file;
    Append(file, ftypBox);
    Append(file, Box("mdat", mdatPayloadOut));
    Append(file, Box("moov", moov));
    return file;
}

// AudioSpecificConfig for AAC-LC (object type 2), 44.1 kHz (index 4), stereo.
Bytes AacLcStereo44k() { return Bytes{ 0x12, 0x10 }; }

// ===== CASES =====

void TestAacLcStereo() {
    std::printf("AAC-LC stereo, one sample per chunk\n");
    TrackSpec spec;
    spec.children = MakeEsds(0x40, AacLcStereo44k());
    spec.sampleSizes = { 10, 20, 30, 40 };

    Bytes payload;
    Bytes file = BuildMp4(spec, 4, payload);

    Mp4AudioTrack t;
    Check(Mp4ParseAudioTrack(file.data(), file.size(), t), "parses");
    Check(t.codec == Mp4AudioCodec::AAC, "codec is AAC");
    Check(t.codecName == "mp4a", "four-cc is mp4a");
    Check(t.sampleRate == 44100, "sample rate 44100");
    Check(t.channels == 2, "2 channels");
    Check(t.codecConfig == std::vector<uint8_t>{ 0x12, 0x10 }, "AudioSpecificConfig recovered");
    Check(t.samples.size() == 4, "4 samples");
    Check(!t.fragmented, "not reported as fragmented");

    bool rangesOk = t.samples.size() == 4;
    const uint32_t expectedSizes[4] = { 10, 20, 30, 40 };
    for (size_t i = 0; i < t.samples.size() && rangesOk; ++i) {
        rangesOk = t.samples[i].size == expectedSizes[i] &&
                   t.samples[i].offset + t.samples[i].size <= file.size() &&
                   file[t.samples[i].offset] == static_cast<uint8_t>(i + 1);
    }
    Check(rangesOk, "sample ranges point at the right mdat bytes");

    const double expected = 4.0 * 1024.0 / 44100.0;
    Check(t.duration > expected - 1e-6 && t.duration < expected + 1e-6, "duration from stts");
}

void TestMultiChunkUniformSizeAnd64BitOffsets() {
    std::printf("AAC-LC, multi-sample chunks, uniform stsz, co64\n");
    TrackSpec spec;
    spec.children = MakeEsds(0x40, AacLcStereo44k());
    spec.uniformSize = 8;
    spec.stsc = { {1, 3}, {3, 2} };     // chunks 1-2 hold 3 samples, chunk 3+ holds 2
    spec.use64BitOffsets = true;

    Bytes payload;
    Bytes file = BuildMp4(spec, 8, payload);

    Mp4AudioTrack t;
    Check(Mp4ParseAudioTrack(file.data(), file.size(), t), "parses");
    Check(t.samples.size() == 8, "8 samples across the stsc runs");

    bool ok = t.samples.size() == 8;
    for (size_t i = 0; i < t.samples.size() && ok; ++i) {
        ok = t.samples[i].size == 8 && file[t.samples[i].offset] == static_cast<uint8_t>(i + 1);
    }
    Check(ok, "every sample resolves to its own mdat bytes");
}

void TestHighSampleRateFromAsc() {
    std::printf("96 kHz AAC: the setup data outranks the 16.16 sample entry\n");
    TrackSpec spec;
    // Object type 2 (AAC-LC), frequency index 0 (96000), channel config 1.
    spec.children = MakeEsds(0x40, Bytes{ 0x10, 0x08 });
    spec.sampleRate = 0;                // an encoder that could not express 96k
    spec.channels = 1;
    spec.sampleSizes = { 12, 12 };

    Bytes payload;
    Bytes file = BuildMp4(spec, 2, payload);

    Mp4AudioTrack t;
    Check(Mp4ParseAudioTrack(file.data(), file.size(), t), "parses");
    Check(t.sampleRate == 96000, "sample rate read from the AudioSpecificConfig");
    Check(t.channels == 1, "mono");
}

void TestMp3InMp4IsNotReportedAsAac() {
    std::printf("MP3-in-MP4: objectTypeIndication 0x6B\n");
    TrackSpec spec;
    spec.children = MakeEsds(0x6B, Bytes{ 0x00 });
    spec.sampleSizes = { 16, 16 };

    Bytes payload;
    Bytes file = BuildMp4(spec, 2, payload);

    Mp4AudioTrack t;
    Check(Mp4ParseAudioTrack(file.data(), file.size(), t), "parses");
    Check(t.codec == Mp4AudioCodec::MP3, "codec is MP3, not AAC");
}

void TestAlac() {
    std::printf("ALAC in M4A\n");
    Bytes cookie;
    PutU32(cookie, 0);                          // version + flags
    for (int i = 0; i < 24; ++i) cookie.push_back(static_cast<uint8_t>(i));
    TrackSpec spec;
    spec.sampleEntryType = "alac";
    spec.children = Box("alac", cookie);
    spec.sampleSizes = { 32, 32 };

    Bytes payload;
    Bytes file = BuildMp4(spec, 2, payload);

    Mp4AudioTrack t;
    Check(Mp4ParseAudioTrack(file.data(), file.size(), t), "parses");
    Check(t.codec == Mp4AudioCodec::ALAC, "codec is ALAC");
    Check(t.codecConfig.size() == 24, "magic cookie recovered without version/flags");
}

void TestVideoOnlyFileHasNoAudioTrack() {
    std::printf("Video-only file\n");
    TrackSpec spec;
    spec.handler = "vide";
    spec.children = MakeEsds(0x40, AacLcStereo44k());
    spec.sampleSizes = { 16 };

    Bytes payload;
    Bytes file = BuildMp4(spec, 1, payload);

    Mp4AudioTrack t;
    Check(!Mp4ParseAudioTrack(file.data(), file.size(), t), "no audio track reported");
}

void TestFragmentedFileIsFlagged() {
    std::printf("Fragmented MP4 (mvex, empty sample table)\n");
    Bytes stbl;
    Append(stbl, MakeStsd(MakeAudioSampleEntry("mp4a", 2, 44100,
                                               MakeEsds(0x40, AacLcStereo44k()))));
    Append(stbl, MakeStts(0, 1024));
    Append(stbl, MakeStsc({}));
    Append(stbl, MakeStsz(0, {}));
    Append(stbl, MakeStco({}, false));

    Bytes minf; Append(minf, Box("stbl", stbl));
    Bytes mdia;
    Append(mdia, MakeMdhd(44100, 0));
    Append(mdia, MakeHdlr("soun"));
    Append(mdia, Box("minf", minf));
    Bytes trak; Append(trak, Box("mdia", mdia));
    Bytes moov;
    Append(moov, Box("mvhd", Bytes(100, 0)));
    Append(moov, Box("trak", trak));
    Append(moov, Box("mvex", Bytes(16, 0)));

    Bytes file;
    Append(file, Box("ftyp", Bytes(12, 0)));
    Append(file, Box("moov", moov));

    Mp4AudioTrack t;
    Check(!Mp4ParseAudioTrack(file.data(), file.size(), t), "reports failure");
    Check(t.fragmented, "flags the file as fragmented");
}

void TestLargeBoxForm() {
    std::printf("64-bit box sizes (largesize form)\n");
    // Wrap a valid moov in the largesize form to make sure the header maths for
    // size == 1 is right.
    TrackSpec spec;
    spec.children = MakeEsds(0x40, AacLcStereo44k());
    spec.sampleSizes = { 16, 16 };
    Bytes payload;
    Bytes plain = BuildMp4(spec, 2, payload);

    // Rebuild with the mdat written in the largesize form, which shifts every
    // chunk offset by the extra 8 bytes - so the file must be built knowing it.
    Bytes free = LargeBox("free", Bytes(8, 0));
    Bytes shifted;
    Append(shifted, free);
    Append(shifted, plain);

    Mp4AudioTrack t;
    // The offsets inside `plain` are now stale, so this only asserts the walk
    // survives a largesize box and still finds the moov.
    const bool parsed = Mp4ParseAudioTrack(shifted.data(), shifted.size(), t);
    Check(parsed, "walks past a largesize box and finds the moov");
    Check(t.codec == Mp4AudioCodec::AAC, "codec still identified");
}

void TestTruncatedAndGarbageInput() {
    std::printf("Malformed input\n");
    Mp4AudioTrack t;
    Check(!Mp4ParseAudioTrack(nullptr, 0, t), "null buffer rejected");

    const uint8_t garbage[64] = { 0xDE, 0xAD, 0xBE, 0xEF };
    Check(!Mp4ParseAudioTrack(garbage, sizeof(garbage), t), "garbage rejected");

    TrackSpec spec;
    spec.children = MakeEsds(0x40, AacLcStereo44k());
    spec.sampleSizes = { 16, 16 };
    Bytes payload;
    Bytes file = BuildMp4(spec, 2, payload);
    for (size_t cut = 1; cut < file.size(); cut += 7) {
        Mp4AudioTrack cutTrack;
        Mp4ParseAudioTrack(file.data(), cut, cutTrack);   // must not crash or hang
    }
    Check(true, "every truncation parses without crashing");

    // A box claiming a size larger than the buffer must stop the walk.
    Bytes lying;
    PutU32(lying, 0xFFFFFF00u);
    const char t4[] = "moov";
    lying.insert(lying.end(), t4, t4 + 4);
    lying.resize(64, 0);
    Check(!Mp4ParseAudioTrack(lying.data(), lying.size(), t), "oversized box size rejected");
}

void TestAudioSpecificConfigParser() {
    std::printf("AudioSpecificConfig parser\n");
    int rate = 0, ch = 0, aot = 0;
    const uint8_t lc44[] = { 0x12, 0x10 };
    Check(ParseAudioSpecificConfig(lc44, sizeof(lc44), &rate, &ch, &aot), "AAC-LC 44.1k parses");
    Check(rate == 44100 && ch == 2 && aot == 2, "44100 / 2ch / object type 2");

    // HE-AAC v1: object type 5 (SBR), core frequency index 6 (24000), channel
    // config 2, extension frequency index 3 (48000), core object type 2. The
    // extension rate is what the decoder emits, so that is what must come back.
    const uint8_t sbr48[] = { 0x2B, 0x11, 0x88 };
    Check(ParseAudioSpecificConfig(sbr48, sizeof(sbr48), &rate, &ch, &aot), "HE-AAC parses");
    Check(rate == 48000 && ch == 2 && aot == 5, "extension rate 48000 / 2ch / object type 5");

    // Escape value: 5 bits of 31 then 6 more bits.
    const uint8_t escaped[] = { 0xF8, 0x08, 0x40 };
    Check(ParseAudioSpecificConfig(escaped, sizeof(escaped), &rate, &ch, &aot),
          "escaped object type parses");
    Check(aot >= 32, "escaped object type is 32 or above");

    Check(!ParseAudioSpecificConfig(lc44, 1, &rate, &ch, &aot), "1-byte config rejected");
    Check(!ParseAudioSpecificConfig(nullptr, 0, &rate, &ch, &aot), "null config rejected");
}

} // namespace

int main() {
    std::printf("=== MP4 audio demuxer test ===\n");
    TestAacLcStereo();
    TestMultiChunkUniformSizeAnd64BitOffsets();
    TestHighSampleRateFromAsc();
    TestMp3InMp4IsNotReportedAsAac();
    TestAlac();
    TestVideoOnlyFileHasNoAudioTrack();
    TestFragmentedFileIsFlagged();
    TestLargeBoxForm();
    TestTruncatedAndGarbageInput();
    TestAudioSpecificConfigParser();

    if (failures == 0) {
        std::printf("=== all checks passed ===\n");
        return 0;
    }
    std::printf("=== %d check(s) failed ===\n", failures);
    return 1;
}
