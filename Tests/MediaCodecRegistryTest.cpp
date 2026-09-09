// Tests/MediaCodecRegistryTest.cpp
// Headless test for the audio/video codec registry: that the built-ins this
// build compiled with are registered, that "recognised" and "decodable" stay
// separate answers, that the format inventory advertises only the decodable
// ones, and that a codec registered by an application is reached by
// classification, by the inventory and by UCAudio's load/save paths.
// Exits non-zero on any failure.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasMediaCodecRegistry.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasAudio.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using namespace UltraCanvas;

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

bool InventoryHas(MediaFormatCategory category, const std::string& ext) {
    for (const auto& f : UltraCanvasSupportedFormats::GetByCategory(category)) {
        if (f.MatchesExtension(ext)) return true;
    }
    return false;
}

bool RegistryHas(MediaCodecKind kind, const std::string& ext) {
    for (const auto& c : GetRegisteredMediaCodecs(kind)) {
        if (c.MatchesExtension(ext)) return true;
    }
    return false;
}

void WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

// ===== CASES =====

void TestBuiltinsAreRegistered() {
    std::printf("Built-in codecs\n");
    const auto audio = GetRegisteredMediaCodecs(MediaCodecKind::Audio);
#ifdef ULTRACANVAS_ENABLE_AUDIO
    Check(!audio.empty(), "the audio backend registered its formats");
    Check(RegistryHas(MediaCodecKind::Audio, "wav"), "wav is registered");
    Check(IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/song.wav"), "a .wav is audio");
    Check(CanDecodeMediaFile(MediaCodecKind::Audio, "/tmp/song.wav"), "wav decodes");
    Check(CanEncodeMediaExtension(MediaCodecKind::Audio, "wav"), "wav encodes");

    // The point of the registry: an .m4a is audio whether or not this build can
    // decode it, so the viewer shows a player and a reason instead of guessing
    // the file is a picture.
    Check(IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/track.m4a"),
          "an .m4a is audio even where no AAC decoder was found");
    Check(IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/book.m4b"),
          "the m4b alias resolves to the same entry");
    Check(!CanEncodeMediaExtension(MediaCodecKind::Audio, "m4a"), "m4a does not encode");
#else
    Check(audio.empty(), "no audio backend, so no audio codecs are registered");
    Check(!IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/song.wav"),
          "nothing classifies as audio");
#endif
}

void TestInventoryOnlyAdvertisesWhatDecodes() {
    std::printf("Inventory versus registry\n");
    bool consistent = true;
    std::string firstBad;
    for (const auto& c : GetRegisteredMediaCodecs(MediaCodecKind::Audio)) {
        const bool supported = c.canDecode || c.canEncode;
        const bool listed = InventoryHas(MediaFormatCategory::Audio, c.extension);
        // A content-probed entry is deliberately absent: the inventory is keyed
        // on extensions alone and could not honour the probe.
        const bool expected = supported && !c.probeFile;
        if (listed != expected) {
            consistent = false;
            if (firstBad.empty()) firstBad = c.extension;
        }
    }
    Check(consistent, "every audio entry is listed exactly when it is supported" +
                      (firstBad.empty() ? std::string() : " (" + firstBad + ")"));

    consistent = true;
    firstBad.clear();
    for (const auto& c : GetRegisteredMediaCodecs(MediaCodecKind::Video)) {
        const bool listed = InventoryHas(MediaFormatCategory::Video, c.extension);
        const bool expected = (c.canDecode || c.canEncode) && !c.probeFile;
        if (listed != expected) {
            consistent = false;
            if (firstBad.empty()) firstBad = c.extension;
        }
    }
    Check(consistent, "every video entry is listed exactly when it is supported" +
                      (firstBad.empty() ? std::string() : " (" + firstBad + ")"));
}

void TestTypeScriptIsNotVideo() {
    std::printf("The .ts collision\n");
    const std::string source = "/tmp/uc-codec-test-source.ts";
    const std::string stream = "/tmp/uc-codec-test-stream.ts";

    const char* ts = "export function greet(name: string) { return `hi ${name}`; }\n";
    std::vector<uint8_t> code(ts, ts + std::char_traits<char>::length(ts));
    code.resize(1024, ' ');
    WriteFile(source, code);

    // 188-byte packets, each starting with the MPEG-TS sync byte.
    std::vector<uint8_t> packets(188 * 4, 0x00);
    for (size_t i = 0; i < packets.size(); i += 188) packets[i] = 0x47;
    WriteFile(stream, packets);

    Check(!IsMediaFileOfKind(MediaCodecKind::Video, source),
          "a TypeScript file is not classified as video");
#ifdef ULTRACANVAS_ENABLE_VIDEO
#if defined(__linux__) || defined(_WIN32)
    Check(IsMediaFileOfKind(MediaCodecKind::Video, stream),
          "a real transport stream is classified as video");
#endif
#endif
    Check(!InventoryHas(MediaFormatCategory::Video, "ts") ||
              !RegistryHas(MediaCodecKind::Video, "ts"),
          "the content-probed .ts never reaches the extension-keyed inventory");

    std::remove(source.c_str());
    std::remove(stream.c_str());
}

// A stand-in for an application-supplied codec: a "format" whose file is just
// a frame count, decoded into that many frames of silence.
std::shared_ptr<UCAudio> DecodeFakeFormat(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    std::string magic(4, '\0');
    f.read(magic.data(), 4);
    if (magic != "UCPX") return nullptr;
    uint32_t frames = 0;
    f.read(reinterpret_cast<char*>(&frames), sizeof(frames));
    if (!f || frames == 0) return nullptr;

    AudioBufferInfo info;
    info.sampleRate = 8000;
    info.channels = 1;
    info.sampleType = AudioSampleType::PCM_S16;
    info.frameCount = frames;
    info.durationSeconds = static_cast<double>(frames) / info.sampleRate;
    std::vector<int16_t> pcm(frames, 0);
    return UCAudio::FromRawPCM(pcm.data(), pcm.size() * sizeof(int16_t), info);
}

bool encodeCalled = false;
bool EncodeFakeFormat(const std::string& path, const UCAudio& audio) {
    encodeCalled = true;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write("UCPX", 4);
    const uint32_t frames = static_cast<uint32_t>(audio.GetInfo().frameCount);
    f.write(reinterpret_cast<const char*>(&frames), sizeof(frames));
    return static_cast<bool>(f);
}

void TestApplicationSuppliedCodec() {
    std::printf("A codec registered by an application\n");
    const std::string path = "/tmp/uc-codec-test-sample.ucpx";

    Check(!IsMediaFileOfKind(MediaCodecKind::Audio, path),
          "the extension is unknown before registration");

    MediaCodecRegistration codec;
    codec.extension = ".UCPX";                    // dot and case are normalised
    codec.aliases = { "ucpx2" };
    codec.description = "UltraCanvas test tone";
    codec.kind = MediaCodecKind::Audio;
    codec.canDecode = true;
    codec.provider = "MediaCodecRegistryTest";
    codec.decodeAudio = DecodeFakeFormat;
    RegisterMediaCodec(codec);

    Check(IsMediaFileOfKind(MediaCodecKind::Audio, path), "registration makes it audio");
    Check(IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/x.UCPX2"),
          "the alias matches, case-insensitively");
    Check(CanDecodeMediaFile(MediaCodecKind::Audio, path), "it reports as decodable");
    Check(InventoryHas(MediaFormatCategory::Audio, "ucpx"),
          "the format inventory picks it up, so file dialogs offer it");
    Check(!CanEncodeMediaExtension(MediaCodecKind::Audio, "ucpx"),
          "no encoder was registered yet");

    // Write a sample file, then read it back through the public UCAudio path:
    // that is the part that proves the plugin is wired into loading rather
    // than merely described.
    {
        std::ofstream f(path, std::ios::binary);
        const uint32_t frames = 4000;
        f.write("UCPX", 4);
        f.write(reinterpret_cast<const char*>(&frames), sizeof(frames));
    }
    auto loaded = UCAudio::LoadFromFile(path);
    Check(loaded && loaded->IsValid(), "UCAudio::LoadFromFile reaches the plugin decoder");
    if (loaded && loaded->IsValid()) {
        Check(loaded->GetInfo().frameCount == 4000, "it returns the plugin's buffer");
        Check(loaded->GetSourcePath() == path, "the source path is still recorded");
    }

    // Registering again adds the encoder without dropping the decoder.
    MediaCodecRegistration withEncoder;
    withEncoder.extension = "ucpx";
    withEncoder.kind = MediaCodecKind::Audio;
    withEncoder.canEncode = true;
    withEncoder.encodeAudio = EncodeFakeFormat;
    RegisterMediaCodec(withEncoder);

    Check(CanDecodeMediaFile(MediaCodecKind::Audio, path),
          "re-registering keeps the decoder it already had");
    Check(CanEncodeMediaExtension(MediaCodecKind::Audio, "ucpx"), "and gains the encoder");

    const auto all = GetRegisteredMediaCodecs(MediaCodecKind::Audio);
    const auto count = std::count_if(all.begin(), all.end(),
                                     [](const MediaCodecRegistration& c) {
                                         return c.extension == "ucpx";
                                     });
    Check(count == 1, "re-registering upgrades the entry rather than duplicating it");

    const std::string outPath = "/tmp/uc-codec-test-written.ucpx";
    encodeCalled = false;
    AudioBufferInfo info;
    info.sampleRate = 8000;
    info.channels = 1;
    info.frameCount = 100;
    std::vector<int16_t> pcm(100, 0);
    auto source = UCAudio::FromRawPCM(pcm.data(), pcm.size() * sizeof(int16_t), info);
    Check(source->SaveToFile(outPath, AudioFormat::Unknown),
          "UCAudio::SaveToFile reaches the plugin encoder");
    Check(encodeCalled, "the plugin encoder actually ran");
    std::remove(outPath.c_str());

    UnregisterMediaCodec(MediaCodecKind::Audio, "ucpx");
    Check(!IsMediaFileOfKind(MediaCodecKind::Audio, path), "unregistering removes it");
    Check(!InventoryHas(MediaFormatCategory::Audio, "ucpx"),
          "and takes it out of the inventory again");
    std::remove(path.c_str());
}

void TestRecognisedButUnsupported() {
    std::printf("Recognised but unsupported\n");
    MediaCodecRegistration codec;
    codec.extension = "ucnope";
    codec.description = "A format nothing here decodes";
    codec.kind = MediaCodecKind::Audio;
    codec.notes = "needs a decoder nobody installed";
    RegisterMediaCodec(codec);

    Check(IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/x.ucnope"),
          "classified as audio, so the caller shows an audio-shaped error");
    Check(!CanDecodeMediaFile(MediaCodecKind::Audio, "/tmp/x.ucnope"), "but not decodable");
    Check(!InventoryHas(MediaFormatCategory::Audio, "ucnope"),
          "and never advertised as a supported format");

    UnregisterMediaCodec(MediaCodecKind::Audio, "ucnope");
}

void TestProbeGating() {
    std::printf("Content-probed registration\n");
    MediaCodecRegistration codec;
    codec.extension = "ucprobe";
    codec.description = "Probed format";
    codec.kind = MediaCodecKind::Audio;
    codec.canDecode = true;
    codec.probeFile = [](const std::string& p) { return p.find("yes") != std::string::npos; };
    RegisterMediaCodec(codec);

    Check(IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/yes.ucprobe"), "probe accepts");
    Check(!IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/no.ucprobe"), "probe rejects");
    Check(!InventoryHas(MediaFormatCategory::Audio, "ucprobe"),
          "a probed format stays out of the extension-keyed inventory");
    Check(FindMediaCodecByExtension(MediaCodecKind::Audio, "ucprobe") != nullptr,
          "but an extension lookup still finds it, for the save path");

    UnregisterMediaCodec(MediaCodecKind::Audio, "ucprobe");
}

void TestKindsAreIndependent() {
    std::printf("Audio and video are separate namespaces\n");
    MediaCodecRegistration a;
    a.extension = "ucdual";
    a.kind = MediaCodecKind::Audio;
    a.canDecode = true;
    a.description = "Dual audio";
    RegisterMediaCodec(a);

    MediaCodecRegistration v = a;
    v.kind = MediaCodecKind::Video;
    v.description = "Dual video";
    RegisterMediaCodec(v);

    Check(IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/x.ucdual"), "audio entry stands");
    Check(IsMediaFileOfKind(MediaCodecKind::Video, "/tmp/x.ucdual"), "video entry stands");

    UnregisterMediaCodec(MediaCodecKind::Audio, "ucdual");
    Check(!IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/x.ucdual"), "audio removed");
    Check(IsMediaFileOfKind(MediaCodecKind::Video, "/tmp/x.ucdual"),
          "removing one kind leaves the other alone");
    UnregisterMediaCodec(MediaCodecKind::Video, "ucdual");
}

void TestExtensionEdgeCases() {
    std::printf("Extension handling\n");
    Check(!IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/no-extension-here"),
          "a file with no extension matches nothing");
    Check(!IsMediaFileOfKind(MediaCodecKind::Audio, ""), "an empty path matches nothing");
    Check(!IsMediaFileOfKind(MediaCodecKind::Audio, "/home/user.dir/file"),
          "a dot in a directory name is not an extension");
#ifdef ULTRACANVAS_ENABLE_AUDIO
    Check(IsMediaFileOfKind(MediaCodecKind::Audio, "/tmp/SONG.WAV"),
          "matching is case-insensitive");
#endif
    MediaCodecRegistration empty;
    empty.kind = MediaCodecKind::Audio;
    RegisterMediaCodec(empty);   // no extension: must be ignored, not stored
    const auto all = GetRegisteredMediaCodecs(MediaCodecKind::Audio);
    Check(std::none_of(all.begin(), all.end(),
                       [](const MediaCodecRegistration& c) { return c.extension.empty(); }),
          "a registration with no extension is refused");
}

} // namespace

int main() {
    std::printf("=== media codec registry test ===\n");
    TestBuiltinsAreRegistered();
    TestInventoryOnlyAdvertisesWhatDecodes();
    TestTypeScriptIsNotVideo();
    TestApplicationSuppliedCodec();
    TestRecognisedButUnsupported();
    TestProbeGating();
    TestKindsAreIndependent();
    TestExtensionEdgeCases();

    if (failures == 0) {
        std::printf("=== all checks passed ===\n");
        return 0;
    }
    std::printf("=== %d check(s) failed ===\n", failures);
    return 1;
}
