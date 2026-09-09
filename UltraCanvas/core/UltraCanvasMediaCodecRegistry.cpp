// core/UltraCanvasMediaCodecRegistry.cpp
// Audio/video codec registry (see UltraCanvasMediaCodecRegistry.h). Holds the
// built-in codec table this build was compiled with — the ULTRACANVAS_HAS_*
// gates CMake sets — and whatever an application or plugin registers on top of
// it, and answers the "is this that kind of file" / "can we decode it"
// questions from that one place.
// Version: 0.1.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasMediaCodecRegistry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>

namespace UltraCanvas {

namespace {

std::string NormalizeExtension(const std::string& ext) {
    std::string out = ext;
    if (!out.empty() && out[0] == '.') out.erase(0, 1);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string ExtensionOfPath(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return NormalizeExtension(path.substr(dot + 1));
}

// Entries are held by shared_ptr and replaced rather than mutated, so a
// snapshot handed to a caller stays valid however the registry changes after.
using Entry = std::shared_ptr<MediaCodecRegistration>;

struct Registry {
    std::mutex mutex;
    std::vector<Entry> entries;
    bool builtinsRegistered = false;
};

Registry& Store() {
    static Registry registry;
    return registry;
}

// Index of the entry for this kind + extension, or npos. Caller holds the lock.
size_t IndexOf(const std::vector<Entry>& entries, MediaCodecKind kind,
               const std::string& extension) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i]->kind == kind && entries[i]->extension == extension) return i;
    }
    return static_cast<size_t>(-1);
}

// "Wins where it offers more": capabilities are OR-ed, aliases unioned, and
// every other field of the incoming registration is taken when it carries one.
// A registration that only adds an encoder therefore cannot silently drop the
// decoder that was already there — UnregisterMediaCodec is how you replace an
// entry outright.
void MergeInto(MediaCodecRegistration& target, const MediaCodecRegistration& incoming) {
    target.canDecode = target.canDecode || incoming.canDecode;
    target.canEncode = target.canEncode || incoming.canEncode;
    if (!incoming.description.empty()) target.description = incoming.description;
    if (!incoming.provider.empty())    target.provider = incoming.provider;
    if (!incoming.notes.empty())       target.notes = incoming.notes;
    if (incoming.decodeAudio)          target.decodeAudio = incoming.decodeAudio;
    if (incoming.encodeAudio)          target.encodeAudio = incoming.encodeAudio;
    if (incoming.probeFile)            target.probeFile = incoming.probeFile;
    for (const std::string& alias : incoming.aliases) {
        const std::string a = NormalizeExtension(alias);
        if (a.empty() || a == target.extension) continue;
        if (std::find(target.aliases.begin(), target.aliases.end(), a) == target.aliases.end()) {
            target.aliases.push_back(a);
        }
    }
}

// ===== CONTENT PROBES =====

// An MPEG transport stream is 188-byte packets, each starting with the sync
// byte 0x47. Three consecutive boundaries is enough to separate one from a
// TypeScript file that happens to begin with a 'G' — and ".ts" is TypeScript
// far more often than it is video, so nothing may claim it by name alone.
bool LooksLikeMpegTransportStream(const std::string& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f) return false;
    char buf[188 * 2 + 1] = {0};
    f.read(buf, sizeof(buf));
    if (f.gcount() < static_cast<std::streamsize>(sizeof(buf))) return false;
    return buf[0] == 0x47 && buf[188] == 0x47 && buf[188 * 2] == 0x47;
}

// ===== BUILT-INS =====

MediaCodecRegistration Audio(const char* ext, std::vector<std::string> aliases,
                             const char* description, bool decode, bool encode,
                             const char* provider, const char* notes) {
    MediaCodecRegistration c;
    c.extension = ext;
    c.aliases = std::move(aliases);
    c.description = description;
    c.kind = MediaCodecKind::Audio;
    c.canDecode = decode;
    c.canEncode = encode;
    c.provider = provider;
    c.notes = notes;
    return c;
}

MediaCodecRegistration Video(const char* ext, std::vector<std::string> aliases,
                             const char* description, bool decode, bool encode,
                             const char* provider, const char* notes) {
    MediaCodecRegistration c = Audio(ext, std::move(aliases), description,
                                     decode, encode, provider, notes);
    c.kind = MediaCodecKind::Video;
    return c;
}

// Formats the framework knows by name are registered whether or not this build
// can decode them: a recognised-but-unsupported entry is what lets the media
// viewer show an audio transport and the reason it is silent, instead of
// mistaking an .m4a for a picture. The format inventory skips those entries,
// so what it advertises stays exactly what the build can do.
void AddBuiltinAudioCodecs() {
#ifdef ULTRACANVAS_ENABLE_AUDIO
    RegisterMediaCodec(Audio("wav", {}, "Waveform audio", true, true,
                             "miniaudio (dr_wav)", ""));

#ifdef ULTRACANVAS_HAS_LAME
    RegisterMediaCodec(Audio("mp3", {}, "MPEG layer III audio", true, true,
                             "miniaudio (dr_mp3) + LAME", ""));
#else
    RegisterMediaCodec(Audio("mp3", {}, "MPEG layer III audio", true, false,
                             "miniaudio (dr_mp3)", "saving requires LAME (libmp3lame)"));
#endif

#ifdef ULTRACANVAS_HAS_LIBFLAC
    RegisterMediaCodec(Audio("flac", {}, "Free Lossless Audio Codec", true, true,
                             "miniaudio (dr_flac) + libFLAC", ""));
#else
    RegisterMediaCodec(Audio("flac", {}, "Free Lossless Audio Codec", true, false,
                             "miniaudio (dr_flac)", "saving requires libFLAC"));
#endif

#ifdef ULTRACANVAS_HAS_VORBIS
    RegisterMediaCodec(Audio("ogg", { "oga" }, "Ogg Vorbis audio", true, true,
                             "libvorbis (vorbisfile + vorbisenc)", ""));
#else
    RegisterMediaCodec(Audio("ogg", { "oga" }, "Ogg Vorbis audio", false, false,
                             "", "needs libvorbis (vorbisfile + vorbisenc)"));
#endif

    {
        bool opusDecode = false, opusEncode = false;
#ifdef ULTRACANVAS_HAS_OPUSFILE
        opusDecode = true;
#endif
#ifdef ULTRACANVAS_HAS_OPUSENC
        opusEncode = true;
#endif
        RegisterMediaCodec(Audio("opus", {}, "Opus audio", opusDecode, opusEncode,
                                 (opusDecode || opusEncode) ? "opusfile + libopusenc" : "",
                                 (opusDecode || opusEncode) ? ""
                                                            : "needs opusfile / libopusenc"));
    }

    // AAC: the MPEG-4 sample table is read in-tree by Mp4AudioDemux, the
    // bitstream by whichever decoder library CMake found — or by the platform
    // media plugins, which need no build-time dependency at all.
    {
#if defined(ULTRACANVAS_HAS_FAAD)
        const bool aac = true;
        const char* aacProvider = "FAAD2";
#elif defined(ULTRACANVAS_HAS_FDKAAC)
        const bool aac = true;
        const char* aacProvider = "fdk-aac";
#elif defined(ULTRACANVAS_HAS_GST_AUDIO_DECODE)
        const bool aac = true;
        const char* aacProvider = "GStreamer (system plugins)";
#else
        const bool aac = false;
        const char* aacProvider = "";
#endif
        const char* aacNotes = aac
            ? "saving is not supported"
            : "needs FAAD2, fdk-aac, or the GStreamer plugins";
        RegisterMediaCodec(Audio("m4a", { "m4b" }, "MPEG-4 audio (AAC)",
                                 aac, false, aacProvider, aacNotes));
        RegisterMediaCodec(Audio("aac", {}, "Raw AAC bitstream (ADTS)",
                                 aac, false, aacProvider, aacNotes));
    }

    // Formats only the platform media plugins reach. Registered either way so
    // the viewer classifies them; decodable only where the fallback is built.
    {
#ifdef ULTRACANVAS_HAS_GST_AUDIO_DECODE
        const bool platform = true;
        const char* platformProvider = "GStreamer (system plugins)";
        const char* platformNotes = "decode depends on the installed plugins";
#else
        const bool platform = false;
        const char* platformProvider = "";
        const char* platformNotes = "needs the platform media plugins (GStreamer)";
#endif
        RegisterMediaCodec(Audio("wma", {}, "Windows Media Audio",
                                 platform, false, platformProvider, platformNotes));
        RegisterMediaCodec(Audio("aiff", { "aif", "aifc" },
                                 "Audio Interchange File Format",
                                 platform, false, platformProvider,
                                 platform ? "saving is not supported" : platformNotes));
        RegisterMediaCodec(Audio("mka", {}, "Matroska audio",
                                 platform, false, platformProvider, platformNotes));
    }
#endif  // ULTRACANVAS_ENABLE_AUDIO
}

// Video decoding stays with the platform backend (GStreamer / Media Foundation
// / AVFoundation); what is registered here is that backend's demuxer/muxer
// matrix. canEncode is true only for the containers the capture session can
// actually mux — MuxerFor() in the backend — while everything else it can
// demux is registered decode-only rather than left out, because omitting it
// made the open dialog refuse files the player then played perfectly well.
void AddBuiltinVideoCodecs() {
#ifdef ULTRACANVAS_ENABLE_VIDEO
#if defined(__linux__)
    const char* p = "GStreamer";
    const char* n = "codec availability depends on installed GStreamer plugins";
    RegisterMediaCodec(Video("mp4",  { "m4v" }, "MPEG-4 container",      true, true,  p, n));
    RegisterMediaCodec(Video("mov",  {},        "QuickTime movie",       true, true,  p, n));
    RegisterMediaCodec(Video("mkv",  {},        "Matroska video",        true, true,  p, n));
    RegisterMediaCodec(Video("webm", {},        "WebM video",            true, true,  p, n));
    RegisterMediaCodec(Video("avi",  {},        "AVI video",             true, true,  p, n));
    RegisterMediaCodec(Video("wmv",  { "asf" }, "Windows Media video",   true, false, p, n));
    RegisterMediaCodec(Video("flv",  {},        "Flash video",           true, false, p, n));
    RegisterMediaCodec(Video("mpg",  { "mpeg", "mpe", "m2v" },
                             "MPEG program stream",                      true, false, p, n));
    RegisterMediaCodec(Video("ogv",  { "ogm" }, "Ogg video",             true, false, p, n));
    RegisterMediaCodec(Video("3gp",  { "3g2" }, "3GPP video",            true, false, p, n));
    RegisterMediaCodec(Video("m2ts", { "mts" }, "MPEG transport stream", true, false, p, n));
#elif defined(_WIN32)
    const char* p = "Media Foundation";
    const char* n = "codec availability depends on installed Media Foundation codecs";
    RegisterMediaCodec(Video("mp4",  { "m4v" }, "MPEG-4 container",      true, true,  p, n));
    RegisterMediaCodec(Video("mov",  {},        "QuickTime movie",       true, false, p, n));
    RegisterMediaCodec(Video("mkv",  {},        "Matroska video",        true, false, p, n));
    RegisterMediaCodec(Video("webm", {},        "WebM video",            true, false, p, n));
    RegisterMediaCodec(Video("avi",  {},        "AVI video",             true, false, p, n));
    RegisterMediaCodec(Video("wmv",  { "asf" }, "Windows Media video",   true, false, p, n));
    RegisterMediaCodec(Video("3gp",  { "3g2" }, "3GPP video",            true, false, p, n));
    RegisterMediaCodec(Video("m2ts", { "mts" }, "MPEG transport stream", true, false, p, n));
#else   // macOS / AVFoundation
    const char* p = "AVFoundation";
    const char* n = "";
    RegisterMediaCodec(Video("mp4",  { "m4v" }, "MPEG-4 container",      true, true,  p, n));
    RegisterMediaCodec(Video("mov",  {},        "QuickTime movie",       true, true,  p, n));
    RegisterMediaCodec(Video("3gp",  { "3g2" }, "3GPP video",            true, false, p, n));
#endif

#if defined(__linux__) || defined(_WIN32)
    // The bare ".ts", decided by content — see LooksLikeMpegTransportStream.
    {
        MediaCodecRegistration ts = Video("ts", {}, "MPEG transport stream",
                                          true, false, p, n);
        ts.probeFile = LooksLikeMpegTransportStream;
        RegisterMediaCodec(ts);
    }
#endif
#endif  // ULTRACANVAS_ENABLE_VIDEO
}

// Caller must NOT hold the lock: the Add* helpers go back through the public
// RegisterMediaCodec, which takes it.
void EnsureBuiltins() {
    {
        std::lock_guard<std::mutex> lock(Store().mutex);
        if (Store().builtinsRegistered) return;
        Store().builtinsRegistered = true;    // set first: the Add* calls re-enter
    }
    AddBuiltinAudioCodecs();
    AddBuiltinVideoCodecs();
}

} // namespace

// ===== PUBLIC SURFACE =====

bool MediaCodecRegistration::MatchesExtension(const std::string& ext) const {
    const std::string e = NormalizeExtension(ext);
    if (e.empty()) return false;
    if (e == extension) return true;
    return std::find(aliases.begin(), aliases.end(), e) != aliases.end();
}

void RegisterMediaCodec(const MediaCodecRegistration& codec) {
    MediaCodecRegistration normalized = codec;
    normalized.extension = NormalizeExtension(normalized.extension);
    if (normalized.extension.empty()) return;
    std::vector<std::string> aliases;
    for (const std::string& alias : normalized.aliases) {
        const std::string a = NormalizeExtension(alias);
        if (a.empty() || a == normalized.extension) continue;
        if (std::find(aliases.begin(), aliases.end(), a) == aliases.end()) aliases.push_back(a);
    }
    normalized.aliases = std::move(aliases);

    std::lock_guard<std::mutex> lock(Store().mutex);
    auto& entries = Store().entries;
    const size_t at = IndexOf(entries, normalized.kind, normalized.extension);
    if (at == static_cast<size_t>(-1)) {
        entries.push_back(std::make_shared<MediaCodecRegistration>(std::move(normalized)));
        return;
    }
    // Copy-on-write so a snapshot someone else is holding does not change
    // under them.
    auto merged = std::make_shared<MediaCodecRegistration>(*entries[at]);
    MergeInto(*merged, normalized);
    entries[at] = std::move(merged);
}

void UnregisterMediaCodec(MediaCodecKind kind, const std::string& extension) {
    const std::string ext = NormalizeExtension(extension);
    if (ext.empty()) return;
    std::lock_guard<std::mutex> lock(Store().mutex);
    auto& entries = Store().entries;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const Entry& e) {
                                     return e->kind == kind && e->extension == ext;
                                 }),
                  entries.end());
}

void RegisterBuiltinMediaCodecs() {
    EnsureBuiltins();
}

std::vector<MediaCodecRegistration> GetRegisteredMediaCodecs(MediaCodecKind kind) {
    EnsureBuiltins();
    std::vector<MediaCodecRegistration> out;
    std::lock_guard<std::mutex> lock(Store().mutex);
    for (const Entry& e : Store().entries) {
        if (e->kind == kind) out.push_back(*e);
    }
    return out;
}

std::shared_ptr<const MediaCodecRegistration> FindMediaCodecForFile(
        MediaCodecKind kind, const std::string& filePath) {
    EnsureBuiltins();
    const std::string ext = ExtensionOfPath(filePath);
    if (ext.empty()) return nullptr;

    Entry match;
    {
        std::lock_guard<std::mutex> lock(Store().mutex);
        for (const Entry& e : Store().entries) {
            if (e->kind == kind && e->MatchesExtension(ext)) { match = e; break; }
        }
    }
    // The probe runs outside the lock: it touches the filesystem, and a codec
    // author's callback must never be able to deadlock the registry.
    if (!match) return nullptr;
    if (match->probeFile && !match->probeFile(filePath)) return nullptr;
    return match;
}

std::shared_ptr<const MediaCodecRegistration> FindMediaCodecByExtension(
        MediaCodecKind kind, const std::string& extension) {
    EnsureBuiltins();
    const std::string ext = NormalizeExtension(extension);
    if (ext.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(Store().mutex);
    for (const Entry& e : Store().entries) {
        if (e->kind == kind && e->MatchesExtension(ext)) return e;
    }
    return nullptr;
}

bool IsMediaFileOfKind(MediaCodecKind kind, const std::string& filePath) {
    return FindMediaCodecForFile(kind, filePath) != nullptr;
}

bool CanDecodeMediaFile(MediaCodecKind kind, const std::string& filePath) {
    const auto codec = FindMediaCodecForFile(kind, filePath);
    return codec && codec->canDecode;
}

bool CanEncodeMediaExtension(MediaCodecKind kind, const std::string& extension) {
    const auto codec = FindMediaCodecByExtension(kind, extension);
    return codec && codec->canEncode;
}

} // namespace UltraCanvas
