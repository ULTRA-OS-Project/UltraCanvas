// Tests/VideoFileInfoTest.cpp
// Headless unit test for ReadVideoFileInfo (UltraCanvasVideoFileInfo.h): builds
// MP4, fragmented MP4, WebM and AVI headers in memory, writes each to a
// temporary file, and checks the facts read back - codec and profile, sizes,
// frame rate, frame count, bit rates, rotation, the sound's codec, rate and
// channels, language and title - plus the formatting helpers. No media assets,
// no decoder, no display. Exits non-zero on any failure.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasVideoFileInfo.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok  " : "FAIL", what.c_str());
    if (!cond) ++failures;
}
void CheckEq(const std::string& got, const std::string& want, const std::string& what) {
    Check(got == want, what + " = \"" + got + "\" (want \"" + want + "\")");
}
void CheckEq(long long got, long long want, const std::string& what) {
    Check(got == want, what + " = " + std::to_string(got) + " (want " + std::to_string(want) + ")");
}
void CheckNear(double got, double want, double tol, const std::string& what) {
    Check(std::fabs(got - want) <= tol, what + " = " + std::to_string(got) + " (want " + std::to_string(want) + ")");
}

using Bytes = std::vector<uint8_t>;

void U8(Bytes& b, uint32_t v) { b.push_back(uint8_t(v)); }
void U16(Bytes& b, uint32_t v) { U8(b, v >> 8); U8(b, v); }
void U32(Bytes& b, uint32_t v) { U16(b, v >> 16); U16(b, v); }
void U16L(Bytes& b, uint32_t v) { U8(b, v); U8(b, v >> 8); }
void U32L(Bytes& b, uint32_t v) { U16L(b, v); U16L(b, v >> 16); }
void Str(Bytes& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }
void Zeros(Bytes& b, size_t n) { b.insert(b.end(), n, 0); }
void Append(Bytes& b, const Bytes& more) { b.insert(b.end(), more.begin(), more.end()); }

std::string WriteTemp(const std::string& name, const Bytes& data) {
    const auto path = std::filesystem::temp_directory_path() / ("VideoFileInfoTest_" + name);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return path.string();
}

// ===== ISO BMFF =====

Bytes Box(const std::string& type, const Bytes& body) {
    Bytes b;
    U32(b, uint32_t(8 + body.size()));
    Str(b, type);
    Append(b, body);
    return b;
}
Bytes Boxes(std::initializer_list<Bytes> parts) {
    Bytes b;
    for (const Bytes& p : parts) Append(b, p);
    return b;
}

Bytes Tkhd(uint32_t trackId, int width, int height, bool rotate90) {
    Bytes b;
    U32(b, 0x00000007);                   // version 0, flags
    U32(b, 0); U32(b, 0);                 // creation / modification
    U32(b, trackId); U32(b, 0); U32(b, 0); // track id, reserved, duration
    Zeros(b, 8); U16(b, 0); U16(b, 0); U16(b, 0); U16(b, 0);
    const int32_t one = 0x10000;
    const int32_t m[9] = {rotate90 ? 0 : one, rotate90 ? one : 0, 0,
                          rotate90 ? -one : 0, rotate90 ? 0 : one, 0, 0, 0, 0x40000000};
    for (int32_t v : m) U32(b, uint32_t(v));
    U32(b, uint32_t(width) << 16);
    U32(b, uint32_t(height) << 16);
    return Box("tkhd", b);
}
Bytes Mdhd(uint32_t timescale, uint32_t duration, const char* lang) {
    Bytes b;
    U32(b, 0); U32(b, 0); U32(b, 0);
    U32(b, timescale); U32(b, duration);
    const uint16_t packed = uint16_t(((lang[0] - 0x60) << 10) | ((lang[1] - 0x60) << 5) | (lang[2] - 0x60));
    U16(b, packed); U16(b, 0);
    return Box("mdhd", b);
}
Bytes Hdlr(const std::string& handler) {
    Bytes b;
    U32(b, 0); U32(b, 0); Str(b, handler); Zeros(b, 12); U8(b, 0);
    return Box("hdlr", b);
}
Bytes Avc1Entry(int width, int height) {
    Bytes e;
    Zeros(e, 6); U16(e, 1);               // reserved, data reference index
    Zeros(e, 16);
    U16(e, uint32_t(width)); U16(e, uint32_t(height));
    U32(e, 0x00480000); U32(e, 0x00480000); U32(e, 0); U16(e, 1);
    Zeros(e, 32); U16(e, 0x18); U16(e, 0xFFFF);
    // avcC: High profile (100), level 4.1, no parameter sets.
    Bytes avcc = {1, 100, 0, 41, 0xFF, 0xE0, 0x00};
    Append(e, Box("avcC", avcc));
    Bytes pasp; U32(pasp, 1); U32(pasp, 1);
    Append(e, Box("pasp", pasp));
    return Box("avc1", e);
}
Bytes Mp4aEntry() {
    Bytes e;
    Zeros(e, 6); U16(e, 1);
    U16(e, 0); U16(e, 0); U32(e, 0);      // version, revision, vendor
    U16(e, 2); U16(e, 16); U16(e, 0); U16(e, 0);
    U32(e, 48000u << 16);
    // esds: ES_Descriptor > DecoderConfig (MPEG-4 audio, 128 kb/s) > ASC (AAC LC, 48 kHz, stereo)
    Bytes esds;
    U32(esds, 0);
    const Bytes asc = {0x11, 0x90};
    Bytes dcd = {0x40, 0x15, 0, 0, 0};
    U32(dcd, 128000); U32(dcd, 128000);
    dcd.push_back(0x05); dcd.push_back(uint8_t(asc.size())); Append(dcd, asc);
    Bytes es = {0x00, 0x02, 0x00, 0x04, uint8_t(dcd.size())};
    Append(es, dcd);
    esds.push_back(0x03); esds.push_back(uint8_t(es.size())); Append(esds, es);
    Append(e, Box("esds", esds));
    return Box("mp4a", e);
}
Bytes Stsd(const Bytes& entry) {
    Bytes b; U32(b, 0); U32(b, 1); Append(b, entry);
    return Box("stsd", b);
}
Bytes Stts(uint32_t samples, uint32_t delta) {
    Bytes b; U32(b, 0); U32(b, 1); U32(b, samples); U32(b, delta);
    return Box("stts", b);
}
Bytes Stsz(uint32_t uniform, uint32_t count) {
    Bytes b; U32(b, 0); U32(b, uniform); U32(b, count);
    return Box("stsz", b);
}
Bytes Ftyp(const std::string& brand) {
    Bytes b; Str(b, brand); U32(b, 0x200); Str(b, brand); Str(b, "mp41");
    return Box("ftyp", b);
}
Bytes Mvhd(uint32_t timescale, uint32_t duration) {
    Bytes b;
    U32(b, 0); U32(b, 0xDC2E2A80u); U32(b, 0);   // version, creation (2021-02-19 ...), modification
    U32(b, timescale); U32(b, duration);
    Zeros(b, 80);
    return Box("mvhd", b);
}

void TestMp4() {
    std::printf("MP4 (progressive)\n");
    const Bytes video = Box("trak", Boxes({
        Tkhd(1, 1280, 720, true),
        Box("mdia", Boxes({Mdhd(30000, 120120, "und"), Hdlr("vide"),
            Box("minf", Box("stbl", Boxes({Stsd(Avc1Entry(1280, 720)), Stts(120, 1001),
                                           Stsz(10000, 120)})))}))}));
    const Bytes audio = Box("trak", Boxes({
        Tkhd(2, 0, 0, false),
        Box("mdia", Boxes({Mdhd(48000, 192192, "eng"), Hdlr("soun"),
            Box("minf", Box("stbl", Boxes({Stsd(Mp4aEntry()), Stts(188, 1024), Stsz(500, 188)})))}))}));
    Bytes data; Str(data, "Test title");
    Bytes dataBox; U32(dataBox, 1); U32(dataBox, 0); Append(dataBox, data);
    Bytes metaBody; U32(metaBody, 0);
    Append(metaBody, Hdlr("mdir"));
    Append(metaBody, Box("ilst", Box("\xA9nam", Box("data", dataBox))));
    const Bytes udta = Box("udta", Box("meta", metaBody));
    Bytes file = Boxes({Ftyp("isom"), Box("moov", Boxes({Mvhd(1000, 4004), video, audio, udta}))});
    Bytes mdat(64, 0);
    Append(file, Box("mdat", mdat));

    VideoFileInfo v;
    Check(ReadVideoFileInfo(WriteTemp("a.mp4", file), v), "read");
    CheckEq(v.container, "MPEG-4", "container");
    CheckEq(v.brand, "isom", "brand");
    CheckNear(v.duration, 4.004, 1e-6, "duration");
    CheckEq(v.videoCodec, "H.264", "video codec");
    CheckEq(v.videoProfile, "High, level 4.1", "profile");
    CheckEq(v.width, 1280, "width");
    CheckEq(v.height, 720, "height");
    CheckEq(v.displayWidth, 1280, "display width");
    CheckEq(FormatFrameRate(v.frameRate), "29.97", "frame rate");
    CheckEq(v.frameCount, 120, "frames");
    CheckEq(v.videoBitRate, (long long)(120.0 * 10000 * 8 / 4.004), "video bit rate");
    CheckEq(v.rotation, 90, "rotation");
    CheckEq(v.audioCodec, "AAC LC", "audio codec");
    CheckEq(v.sampleRate, 48000, "sample rate");
    CheckEq(v.channels, 2, "channels");
    CheckEq(v.audioLanguage, "eng", "language");
    CheckEq(v.audioBitRate, (long long)(188.0 * 500 * 8 / 4.004), "audio bit rate (measured)");
    CheckEq(v.videoTracks, 1, "video tracks");
    CheckEq(v.audioTracks, 1, "audio tracks");
    CheckEq(v.title, "Test title", "title");
    Check(!v.creationTime.empty(), "creation time set: " + v.creationTime);
    Check(v.overallBitRate > 0, "overall bit rate");
}

void TestFragmentedMp4() {
    std::printf("MP4 (fragmented)\n");
    const Bytes video = Box("trak", Boxes({
        Tkhd(1, 640, 480, false),
        Box("mdia", Boxes({Mdhd(15360, 0, "und"), Hdlr("vide"),
            Box("minf", Box("stbl", Boxes({Stsd(Avc1Entry(640, 480)), Stts(0, 0), Stsz(0, 0)})))}))}));
    Bytes trex; U32(trex, 0); U32(trex, 1); U32(trex, 1); U32(trex, 256); U32(trex, 0); U32(trex, 0);
    Bytes file = Boxes({Ftyp("iso5"), Box("moov", Boxes({Mvhd(1000, 0), video,
                                                         Box("mvex", Box("trex", trex))}))});
    for (int fragment = 0; fragment < 2; ++fragment) {
        Bytes mfhd; U32(mfhd, 0); U32(mfhd, uint32_t(fragment + 1));
        Bytes tfhd; U32(tfhd, 0x020000); U32(tfhd, 1);
        Bytes trun; U32(trun, 0x000201); U32(trun, 60); U32(trun, 0);
        for (int i = 0; i < 60; ++i) U32(trun, 500);
        Append(file, Box("moof", Boxes({Box("mfhd", mfhd), Box("traf", Boxes({Box("tfhd", tfhd), Box("trun", trun)}))})));
        Append(file, Box("mdat", Bytes(16, 0)));
    }
    VideoFileInfo v;
    Check(ReadVideoFileInfo(WriteTemp("frag.mp4", file), v), "read");
    CheckEq(v.frameCount, 120, "frames from the fragments");
    CheckEq(FormatFrameRate(v.frameRate), "60", "frame rate from trex defaults");
    CheckNear(v.duration, 2.0, 1e-6, "duration from the fragments");
    CheckEq(v.videoBitRate, 240000, "bit rate from trun sizes");
}

// ===== Matroska / WebM =====

Bytes El(uint32_t id, const Bytes& body) {
    Bytes b;
    if (id > 0xFFFFFF) U8(b, id >> 24);
    if (id > 0xFFFF) U8(b, id >> 16);
    if (id > 0xFF) U8(b, id >> 8);
    U8(b, id);
    if (body.size() < 127) U8(b, 0x80 | uint32_t(body.size()));
    else { U8(b, 0x40 | uint32_t(body.size() >> 8)); U8(b, uint32_t(body.size())); }
    Append(b, body);
    return b;
}
Bytes UInt(uint64_t v) {
    Bytes b;
    for (int i = 7; i >= 0; --i) if (v >> (i * 8) || !b.empty() || i == 0) b.push_back(uint8_t(v >> (i * 8)));
    return b;
}
Bytes Float64(double d) {
    uint64_t u; std::memcpy(&u, &d, 8);
    Bytes b; U32(b, uint32_t(u >> 32)); U32(b, uint32_t(u));
    return b;
}
Bytes Text(const std::string& s) { return Bytes(s.begin(), s.end()); }

void TestWebM() {
    std::printf("WebM\n");
    Bytes file = El(0x1A45DFA3, El(0x4282, Text("webm")));
    const Bytes info = El(0x1549A966, Boxes({El(0x2AD7B1, UInt(1000000)), El(0x4489, Float64(3000.0)),
                                             El(0x7BA9, Text("A WebM clip")), El(0x5741, Text("Test writer"))}));
    const Bytes videoTrack = El(0xAE, Boxes({El(0x73C5, UInt(1)), El(0x83, UInt(1)), El(0x86, Text("V_VP9")),
                                             El(0x23E383, UInt(40000000)),
                                             El(0xE0, Boxes({El(0xB0, UInt(640)), El(0xBA, UInt(360))}))}));
    const Bytes audioTrack = El(0xAE, Boxes({El(0x73C5, UInt(2)), El(0x83, UInt(2)), El(0x86, Text("A_OPUS")),
                                             El(0x22B59C, Text("ger")),
                                             El(0xE1, Boxes({El(0xB5, Float64(48000.0)), El(0x9F, UInt(2))}))}));
    const Bytes tracks = El(0x1654AE6B, Boxes({videoTrack, audioTrack}));
    // Segment of unknown size, as a live writer leaves it, then a Cluster.
    Bytes segment = {0x18, 0x53, 0x80, 0x67, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    Append(segment, info);
    Append(segment, tracks);
    Append(segment, El(0x1F43B675, El(0xE7, UInt(0))));
    Append(file, segment);

    VideoFileInfo v;
    Check(ReadVideoFileInfo(WriteTemp("c.webm", file), v), "read");
    CheckEq(v.container, "WebM", "container");
    CheckNear(v.duration, 3.0, 1e-9, "duration");
    CheckEq(v.videoCodec, "VP9", "video codec");
    CheckEq(v.width, 640, "width");
    CheckEq(v.height, 360, "height");
    CheckEq(FormatFrameRate(v.frameRate), "25", "frame rate");
    CheckEq(v.frameCount, 75, "frames");
    CheckEq(v.audioCodec, "Opus", "audio codec");
    CheckEq(v.sampleRate, 48000, "sample rate");
    CheckEq(v.channels, 2, "channels");
    CheckEq(v.audioLanguage, "ger", "language");
    CheckEq(v.title, "A WebM clip", "title");
    CheckEq(v.encoder, "Test writer", "encoder");
}

// ===== AVI =====

Bytes Chunk(const std::string& id, const Bytes& body) {
    Bytes b; Str(b, id); U32L(b, uint32_t(body.size())); Append(b, body);
    if (body.size() & 1) b.push_back(0);
    return b;
}
Bytes List(const std::string& type, const Bytes& body) {
    Bytes inner; Str(inner, type); Append(inner, body);
    return Chunk("LIST", inner);
}

void TestAvi() {
    std::printf("AVI\n");
    Bytes avih;
    U32L(avih, 40000); U32L(avih, 0); U32L(avih, 0); U32L(avih, 0x10);
    U32L(avih, 50); U32L(avih, 0); U32L(avih, 2); U32L(avih, 0);
    U32L(avih, 320); U32L(avih, 240); Zeros(avih, 16);
    Bytes vstrh; Str(vstrh, "vids"); Str(vstrh, "XVID"); U32L(vstrh, 0); U32L(vstrh, 0); U32L(vstrh, 0);
    U32L(vstrh, 1); U32L(vstrh, 25); U32L(vstrh, 0); U32L(vstrh, 50); Zeros(vstrh, 20);
    Bytes vstrf; U32L(vstrf, 40); U32L(vstrf, 320); U32L(vstrf, 240); U16L(vstrf, 1); U16L(vstrf, 24);
    Str(vstrf, "XVID"); Zeros(vstrf, 20);
    Bytes astrh; Str(astrh, "auds"); U32L(astrh, 0); U32L(astrh, 0); U32L(astrh, 0); U32L(astrh, 0);
    U32L(astrh, 1); U32L(astrh, 44100); U32L(astrh, 0); U32L(astrh, 88200); Zeros(astrh, 20);
    Bytes astrf; U16L(astrf, 0x55); U16L(astrf, 2); U32L(astrf, 44100); U32L(astrf, 16000);
    U16L(astrf, 1); U16L(astrf, 0);
    const Bytes hdrl = List("hdrl", Boxes({Chunk("avih", avih),
                                           List("strl", Boxes({Chunk("strh", vstrh), Chunk("strf", vstrf)})),
                                           List("strl", Boxes({Chunk("strh", astrh), Chunk("strf", astrf)}))}));
    const Bytes body = Boxes({hdrl, List("INFO", Chunk("ISFT", Text("Test muxer"))), List("movi", Bytes(8, 0))});
    Bytes file; Str(file, "RIFF"); U32L(file, uint32_t(4 + body.size())); Str(file, "AVI "); Append(file, body);

    VideoFileInfo v;
    Check(ReadVideoFileInfo(WriteTemp("e.avi", file), v), "read");
    CheckEq(v.container, "AVI", "container");
    CheckEq(v.videoCodec, "MPEG-4 Visual (Xvid)", "video codec");
    CheckEq(v.width, 320, "width");
    CheckEq(v.height, 240, "height");
    CheckEq(FormatFrameRate(v.frameRate), "25", "frame rate");
    CheckEq(v.frameCount, 50, "frames");
    CheckNear(v.duration, 2.0, 1e-9, "duration");
    CheckEq(v.audioCodec, "MP3", "audio codec");
    CheckEq(v.sampleRate, 44100, "sample rate");
    CheckEq(v.channels, 2, "channels");
    CheckEq(v.audioBitRate, 128000, "audio bit rate");
    CheckEq(v.encoder, "Test muxer", "encoder");
}

void TestOthers() {
    std::printf("Not a video / helpers\n");
    VideoFileInfo v;
    Bytes text; Str(text, "This is just some text, not a video at all.\n");
    Check(!ReadVideoFileInfo(WriteTemp("text.txt", text), v), "a text file is not read");
    Check(!ReadVideoFileInfo(WriteTemp("missing-dir/none.mp4", {}), v), "a missing file is not read");
    CheckEq(FormatBitRate(1684000), "1.68 Mb/s", "FormatBitRate Mb/s");
    CheckEq(FormatBitRate(128218), "128 kb/s", "FormatBitRate kb/s");
    CheckEq(FormatFrameRate(30000.0 / 1001.0), "29.97", "FormatFrameRate NTSC");
    CheckEq(FormatFrameRate(24000.0 / 1001.0), "23.976", "FormatFrameRate film");
    CheckEq(FormatSampleRate(44100), "44.1 kHz", "FormatSampleRate 44.1");
    CheckEq(FormatSampleRate(22050), "22.05 kHz", "FormatSampleRate 22.05");
    CheckEq(DescribeAspectRatio(1920, 1080), "16:9", "aspect 16:9");
    CheckEq(DescribeAspectRatio(1920, 804), "2.39:1", "aspect scope");
    CheckEq(DescribeAudioChannels(6), "5.1", "channels 5.1");
    CheckEq(FormatMediaDuration(83.456), "1:23", "duration short");
    CheckEq(FormatMediaDuration(3723.5, true), "1:02:03.500", "duration with millis");
}

} // namespace

int main() {
    TestMp4();
    TestFragmentedMp4();
    TestWebM();
    TestAvi();
    TestOthers();
    if (failures) {
        std::printf("VideoFileInfoTest: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("VideoFileInfoTest: all passed\n");
    return 0;
}
