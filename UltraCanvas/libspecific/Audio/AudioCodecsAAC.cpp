// libspecific/Audio/AudioCodecsAAC.cpp
// AAC (M4A / .aac) decode plus a generic platform-media fallback, for the
// formats neither miniaudio nor the Ogg codec libraries can read.
//
// Three routes, tried in this order and each compile-gated so a build without
// them simply reports the format as unsupported:
//   1. FAAD2   (ULTRACANVAS_HAS_FAAD)    - AAC-LC / HE-AAC, fed raw access
//                                          units from the in-tree MP4 demuxer.
//   2. fdk-aac (ULTRACANVAS_HAS_FDKAAC)  - same, when FAAD2 is unavailable.
//   3. GStreamer (ULTRACANVAS_HAS_GST_AUDIO_DECODE) - decodebin over whatever
//      the system has plugins for, which is how ALAC, WMA, AIFF and M4A get
//      decoded on a Linux desktop with no AAC library installed at all.
// Version: 0.1.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#ifdef ULTRACANVAS_ENABLE_AUDIO

#include "AudioCodecsExtra.h"
#include "Mp4AudioDemux.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef ULTRACANVAS_HAS_FAAD
#include <neaacdec.h>
#endif
#ifdef ULTRACANVAS_HAS_FDKAAC
#include <fdk-aac/aacdecoder_lib.h>
#endif
#ifdef ULTRACANVAS_HAS_GST_AUDIO_DECODE
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#endif

namespace UltraCanvas {
namespace AudioCodecs {

namespace {

// Refuse absurd decode jobs rather than growing the buffer until the process
// dies: 4 hours of 48 kHz stereo float is a little over 2.6 GB, so this caps a
// decode at roughly a gigabyte of PCM.
constexpr size_t kMaxDecodedSamples = 256u * 1024u * 1024u;

[[maybe_unused]] std::string LowerExtensionOf(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string e = path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// Wrap an interleaved float buffer as a UCAudio.
[[maybe_unused]] std::shared_ptr<UCAudio> MakeFloatAudio(
        std::vector<float>& samples, int rate, int channels, AudioFormat sourceFormat) {
    if (channels <= 0 || rate <= 0 || samples.empty()) return nullptr;

    AudioBufferInfo info;
    info.sampleRate = rate;
    info.channels = channels;
    info.sampleType = AudioSampleType::PCM_F32;
    info.frameCount = samples.size() / static_cast<size_t>(channels);
    info.durationSeconds = static_cast<double>(info.frameCount) / rate;
    if (info.frameCount == 0) return nullptr;

    auto audio = std::make_shared<UCAudio>();
    audio->MutableData().resize(info.frameCount * info.BytesPerFrame());
    std::memcpy(audio->MutableData().data(), samples.data(), audio->MutableData().size());
    audio->SetInfo(info);
    audio->SetSourceFormat(sourceFormat);
    return audio;
}

[[maybe_unused]] bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    if (std::fseek(fp, 0, SEEK_END) != 0) { std::fclose(fp); return false; }
    const long size = std::ftell(fp);
    if (size <= 0) { std::fclose(fp); return false; }
    std::rewind(fp);
    out.resize(static_cast<size_t>(size));
    const size_t read = std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    out.resize(read);
    return read > 0;
}

// ===== FAAD2 =====
#ifdef ULTRACANVAS_HAS_FAAD

// RAII around the FAAD2 handle so the several early returns below cannot leak
// a decoder.
struct FaadHandle {
    NeAACDecHandle h = nullptr;
    ~FaadHandle() { if (h) NeAACDecClose(h); }
};

bool ConfigureFaad(NeAACDecHandle h) {
    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(h);
    if (!cfg) return false;
    cfg->outputFormat = FAAD_FMT_FLOAT;   // interleaved float, matching UCAudio
    cfg->downMatrix   = 0;                // keep the source channel layout
    cfg->defObjectType = LC;
    return NeAACDecSetConfiguration(h, cfg) != 0;
}

// Append one decoded frame. `info.samples` counts interleaved samples, not
// frames, and can legitimately be 0 for a frame that only primes the decoder.
bool AppendFaadFrame(const NeAACDecFrameInfo& info, const void* buffer,
                     std::vector<float>& out) {
    if (info.error != 0 || info.samples == 0 || !buffer) return info.error == 0;
    if (out.size() + info.samples > kMaxDecodedSamples) return false;
    const float* src = static_cast<const float*>(buffer);
    out.insert(out.end(), src, src + info.samples);
    return true;
}

// Raw .aac (ADTS/ADIF): FAAD2 parses the transport headers itself, so the whole
// file is handed over and consumed frame by frame.
std::shared_ptr<UCAudio> DecodeRawAacFaad(const std::string& path) {
    std::vector<uint8_t> file;
    if (!ReadWholeFile(path, file)) return nullptr;

    FaadHandle dec;
    dec.h = NeAACDecOpen();
    if (!dec.h || !ConfigureFaad(dec.h)) return nullptr;

    unsigned long rate = 0;
    unsigned char channels = 0;
    const long consumed = NeAACDecInit(dec.h, file.data(),
                                       static_cast<unsigned long>(file.size()),
                                       &rate, &channels);
    if (consumed < 0 || rate == 0 || channels == 0) return nullptr;

    std::vector<float> samples;
    size_t pos = static_cast<size_t>(consumed);
    while (pos < file.size()) {
        NeAACDecFrameInfo info{};
        void* buf = NeAACDecDecode(dec.h, &info, file.data() + pos,
                                   static_cast<unsigned long>(file.size() - pos));
        if (info.error != 0) break;                 // stop at the first bad frame
        if (info.bytesconsumed == 0) break;         // no progress: avoid spinning
        if (!AppendFaadFrame(info, buf, samples)) break;
        pos += info.bytesconsumed;
        // A stream can change its output rate mid-file (SBR kicking in); the
        // last value FAAD2 reports is the one the samples were produced at.
        if (info.samplerate) rate = info.samplerate;
        if (info.channels)   channels = info.channels;
    }

    return MakeFloatAudio(samples, static_cast<int>(rate), channels, AudioFormat::AAC);
}

// M4A: raw access units come from the in-tree demuxer, and the esds
// AudioSpecificConfig initialises the decoder (NeAACDecInit2).
std::shared_ptr<UCAudio> DecodeMp4AacFaad(const std::string& path, const Mp4AudioTrack& track) {
    std::vector<uint8_t> file;
    if (!ReadWholeFile(path, file)) return nullptr;
    if (track.codecConfig.empty()) return nullptr;

    FaadHandle dec;
    dec.h = NeAACDecOpen();
    if (!dec.h || !ConfigureFaad(dec.h)) return nullptr;

    unsigned long rate = 0;
    unsigned char channels = 0;
    if (NeAACDecInit2(dec.h,
                      const_cast<unsigned char*>(track.codecConfig.data()),
                      static_cast<unsigned long>(track.codecConfig.size()),
                      &rate, &channels) != 0) {
        return nullptr;
    }
    if (rate == 0 || channels == 0) return nullptr;

    std::vector<float> samples;
    if (track.duration > 0.0) {
        const size_t estimate = static_cast<size_t>(track.duration * rate) * channels;
        if (estimate < kMaxDecodedSamples) samples.reserve(estimate);
    }

    for (const Mp4AudioSample& s : track.samples) {
        if (s.offset + s.size > file.size()) break;
        NeAACDecFrameInfo info{};
        void* buf = NeAACDecDecode(dec.h, &info, file.data() + s.offset, s.size);
        if (info.error != 0) continue;              // drop a damaged access unit
        if (!AppendFaadFrame(info, buf, samples)) break;
        if (info.samplerate) rate = info.samplerate;
        if (info.channels)   channels = info.channels;
    }

    return MakeFloatAudio(samples, static_cast<int>(rate), channels, AudioFormat::AAC);
}
#endif  // ULTRACANVAS_HAS_FAAD

// ===== FDK-AAC =====
#ifdef ULTRACANVAS_HAS_FDKAAC

struct FdkHandle {
    HANDLE_AACDECODER h = nullptr;
    ~FdkHandle() { if (h) aacDecoder_Close(h); }
};

// fdk-aac emits interleaved integer PCM whose width is fixed when the library
// is built; UCAudio wants float here so both AAC paths produce identical
// buffers.
bool AppendFdkFrames(const INT_PCM* pcm, size_t sampleCount, std::vector<float>& out) {
    if (out.size() + sampleCount > kMaxDecodedSamples) return false;
    constexpr float kScale = (sizeof(INT_PCM) == 2) ? (1.0f / 32768.0f)
                                                    : (1.0f / 2147483648.0f);
    out.reserve(out.size() + sampleCount);
    for (size_t i = 0; i < sampleCount; ++i) out.push_back(static_cast<float>(pcm[i]) * kScale);
    return true;
}

// Drain every frame the decoder can produce from what it has already been
// given. Returns false only on a hard decode error.
bool DrainFdk(HANDLE_AACDECODER h, std::vector<INT_PCM>& scratch,
              std::vector<float>& out, int& rate, int& channels) {
    for (;;) {
        const AAC_DECODER_ERROR err =
            aacDecoder_DecodeFrame(h, scratch.data(), static_cast<INT>(scratch.size()), 0);
        if (err == AAC_DEC_NOT_ENOUGH_BITS) return true;      // needs more input
        if (err != AAC_DEC_OK) return false;

        const CStreamInfo* si = aacDecoder_GetStreamInfo(h);
        if (!si || si->numChannels <= 0 || si->frameSize <= 0) return false;
        rate = si->sampleRate;
        channels = si->numChannels;
        const size_t produced = static_cast<size_t>(si->frameSize) *
                                static_cast<size_t>(si->numChannels);
        if (produced > scratch.size()) return false;
        if (!AppendFdkFrames(scratch.data(), produced, out)) return false;
    }
}

// Push a buffer through the decoder. aacDecoder_Fill takes only as much as its
// internal buffer holds and reports the rest as still-valid, so the caller has
// to loop: fill, drain, fill the remainder.
bool FeedFdk(HANDLE_AACDECODER h, const uint8_t* data, size_t size,
             std::vector<INT_PCM>& scratch, std::vector<float>& out,
             int& rate, int& channels) {
    size_t pos = 0;
    while (pos < size) {
        UCHAR* chunk[1] = { const_cast<UCHAR*>(static_cast<const UCHAR*>(data + pos)) };
        const UINT chunkSize[1] = { static_cast<UINT>(size - pos) };
        UINT valid = chunkSize[0];
        if (aacDecoder_Fill(h, chunk, chunkSize, &valid) != AAC_DEC_OK) return false;
        const UINT consumed = chunkSize[0] - valid;
        if (!DrainFdk(h, scratch, out, rate, channels)) return false;
        if (consumed == 0) break;            // no progress: stop rather than spin
        pos += consumed;
    }
    return true;
}

std::shared_ptr<UCAudio> DecodeAacFdk(const std::string& path, const Mp4AudioTrack* track) {
    std::vector<uint8_t> file;
    if (!ReadWholeFile(path, file)) return nullptr;

    FdkHandle dec;
    dec.h = aacDecoder_Open(track ? TT_MP4_RAW : TT_MP4_ADTS, 1);
    if (!dec.h) return nullptr;

    if (track) {
        if (track->codecConfig.empty()) return nullptr;
        UCHAR* cfg[1] = { const_cast<UCHAR*>(track->codecConfig.data()) };
        const UINT cfgSize[1] = { static_cast<UINT>(track->codecConfig.size()) };
        if (aacDecoder_ConfigRaw(dec.h, cfg, cfgSize) != AAC_DEC_OK) return nullptr;
    }

    // 8 channels x 2048 samples covers every frame size fdk-aac emits, SBR
    // upsampling included.
    std::vector<INT_PCM> scratch(8 * 2048);
    std::vector<float> samples;
    int rate = 0, channels = 0;

    if (track) {
        // Raw access units: one Fill per MP4 sample.
        for (const Mp4AudioSample& s : track->samples) {
            if (s.offset + s.size > file.size()) break;
            if (!FeedFdk(dec.h, file.data() + s.offset, s.size,
                         scratch, samples, rate, channels)) {
                break;              // keep whatever decoded before the error
            }
        }
    } else {
        FeedFdk(dec.h, file.data(), file.size(), scratch, samples, rate, channels);
    }

    return MakeFloatAudio(samples, rate, channels, AudioFormat::AAC);
}
#endif  // ULTRACANVAS_HAS_FDKAAC

// ===== GSTREAMER (generic platform decode) =====
#ifdef ULTRACANVAS_HAS_GST_AUDIO_DECODE

// Decode any file the installed GStreamer plugins understand into interleaved
// float. Mirrors the synchronous, self-contained shape of the video backend's
// thumbnail grab: build the pipeline, run it to EOS pulling samples, tear down.
// No main loop or bus watch thread is needed because appsink's pull API blocks.
std::shared_ptr<UCAudio> DecodeViaGStreamer(const std::string& path) {
    if (!gst_is_initialized()) {
        if (!gst_init_check(nullptr, nullptr, nullptr)) return nullptr;
    }

    gchar* uri = gst_uri_is_valid(path.c_str())
                     ? g_strdup(path.c_str())
                     : gst_filename_to_uri(path.c_str(), nullptr);
    if (!uri) return nullptr;

    // uridecodebin auto-plugs the demuxer and decoder; audioconvert normalises
    // to float and audioresample is a no-op that keeps the caps negotiable.
    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch(
        "uridecodebin name=ucaudio-dec ! audioconvert ! audioresample ! "
        "audio/x-raw,format=F32LE,layout=interleaved ! "
        "appsink name=ucaudio-sink sync=false max-buffers=0",
        &err);
    if (err) g_error_free(err);
    if (!pipeline) { g_free(uri); return nullptr; }

    GstElement* dec  = gst_bin_get_by_name(GST_BIN(pipeline), "ucaudio-dec");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "ucaudio-sink");
    if (dec) {
        // Expose the audio stream only. A cover-art or other non-audio stream
        // would otherwise get a decoded-but-unlinked pad, and the not-linked
        // flow error that follows tears the whole pipeline down.
        GstCaps* audioOnly = gst_caps_from_string("audio/x-raw");
        g_object_set(dec, "uri", uri, "caps", audioOnly,
                     "expose-all-streams", FALSE, nullptr);
        if (audioOnly) gst_caps_unref(audioOnly);
    }
    g_free(uri);

    GstBus* bus = gst_element_get_bus(pipeline);
    auto cleanup = [&]() {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
        if (bus)  gst_object_unref(bus);
        if (dec)  gst_object_unref(dec);
        if (sink) gst_object_unref(sink);
    };

    if (!dec || !sink) { cleanup(); return nullptr; }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        cleanup();
        return nullptr;
    }

    std::vector<float> samples;
    int rate = 0, channels = 0;

    for (;;) {
        // A file no installed plugin can decode fails on the bus rather than at
        // set_state, so the error has to be checked between pulls.
        if (bus) {
            if (GstMessage* msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
                gst_message_unref(msg);
                break;
            }
        }
        // try_pull rather than pull: a wedged decoder must not hang the caller
        // (this runs on whatever thread asked to open the file) forever.
        GstSample* sample =
            gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 10 * GST_SECOND);
        if (!sample) break;               // EOS, error, or the decoder stalled

        if (rate == 0) {
            if (GstCaps* caps = gst_sample_get_caps(sample)) {
                if (GstStructure* st = gst_caps_get_structure(caps, 0)) {
                    gst_structure_get_int(st, "rate", &rate);
                    gst_structure_get_int(st, "channels", &channels);
                }
            }
        }

        bool overflow = false;
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            const size_t count = map.size / sizeof(float);
            if (samples.size() + count > kMaxDecodedSamples) {
                overflow = true;
            } else {
                const float* src = reinterpret_cast<const float*>(map.data);
                samples.insert(samples.end(), src, src + count);
            }
            gst_buffer_unmap(buffer, &map);
        }
        gst_sample_unref(sample);
        if (overflow) break;
    }

    cleanup();

    // The source format is only cosmetic here; derive it from the extension so
    // an .m4a still reports AAC rather than Unknown.
    const AudioFormat fmt = AudioFormatFromExtension(LowerExtensionOf(path));
    return MakeFloatAudio(samples, rate, channels, fmt);
}
#endif  // ULTRACANVAS_HAS_GST_AUDIO_DECODE

const char* Mp4CodecName(Mp4AudioCodec codec) {
    switch (codec) {
        case Mp4AudioCodec::AAC:  return "AAC";
        case Mp4AudioCodec::ALAC: return "Apple Lossless (ALAC)";
        case Mp4AudioCodec::MP3:  return "MP3-in-MP4";
        case Mp4AudioCodec::AC3:  return "Dolby AC-3";
        case Mp4AudioCodec::PCM:  return "uncompressed PCM";
        default:                  return nullptr;
    }
}

} // namespace

// ===== PUBLIC SURFACE =====

bool CanDecodeAac() {
#if defined(ULTRACANVAS_HAS_FAAD) || defined(ULTRACANVAS_HAS_FDKAAC) || \
    defined(ULTRACANVAS_HAS_GST_AUDIO_DECODE)
    return true;
#else
    return false;
#endif
}

bool HasPlatformAudioDecoder() {
#ifdef ULTRACANVAS_HAS_GST_AUDIO_DECODE
    return true;
#else
    return false;
#endif
}

std::shared_ptr<UCAudio> TryDecodeAac(const std::string& path) {
    if (LooksLikeRawAac(path)) {
#ifdef ULTRACANVAS_HAS_FAAD
        if (auto audio = DecodeRawAacFaad(path)) return audio;
#endif
#ifdef ULTRACANVAS_HAS_FDKAAC
        if (auto audio = DecodeAacFdk(path, nullptr)) return audio;
#endif
        return nullptr;
    }

    if (!LooksLikeIsoBmff(path)) return nullptr;

    Mp4AudioTrack track;
    if (!Mp4ProbeAudioTrack(path, track)) return nullptr;
    if (track.codec != Mp4AudioCodec::AAC || track.samples.empty()) return nullptr;

#ifdef ULTRACANVAS_HAS_FAAD
    if (auto audio = DecodeMp4AacFaad(path, track)) return audio;
#endif
#ifdef ULTRACANVAS_HAS_FDKAAC
    if (auto audio = DecodeAacFdk(path, &track)) return audio;
#endif
    (void)track;
    return nullptr;
}

std::shared_ptr<UCAudio> TryDecodePlatform(const std::string& path) {
#ifdef ULTRACANVAS_HAS_GST_AUDIO_DECODE
    return DecodeViaGStreamer(path);
#else
    (void)path;
    return nullptr;
#endif
}

std::string DescribeDecodeFailure(const std::string& path) {
    // What a build without an AAC decoder should tell the user to install. The
    // GStreamer route needs no rebuild, so it is named first.
    const std::string installHint =
        " Installing the GStreamer plugins enables it, or build UltraCanvas "
        "against FAAD2 (libfaad) or fdk-aac.";

    if (LooksLikeRawAac(path)) {
        return CanDecodeAac()
            ? std::string("The AAC stream could not be decoded - it is damaged, "
                          "or it uses a profile the decoder does not implement.")
            : "This is AAC audio, which this build has no decoder for." + installHint;
    }
    if (!LooksLikeIsoBmff(path)) return {};

    Mp4AudioTrack track;
    const bool parsed = Mp4ProbeAudioTrack(path, track);
    if (!parsed) {
        if (track.fragmented) {
            return "This is a fragmented MP4. Only files whose sample table is "
                   "complete in the moov box can be played.";
        }
        if (track.codecName.empty()) {
            return "No playable audio track was found in this MPEG-4 file.";
        }
    }

    if (track.codec == Mp4AudioCodec::AAC) {
        if (!CanDecodeAac()) {
            return "This file holds AAC audio, which this build has no decoder for." +
                   installHint;
        }
        return "The AAC track could not be decoded - it is damaged, or it uses a "
               "profile the decoder does not implement.";
    }

    const char* name = Mp4CodecName(track.codec);
    const std::string codec =
        name ? std::string(name)
             : (track.codecName.empty() ? std::string("an unrecognised codec")
                                        : "the '" + track.codecName + "' codec");
    std::string message = "This MPEG-4 file holds " + codec +
                          " audio, which this build cannot decode.";
    if (!HasPlatformAudioDecoder()) {
        message += " Installing the GStreamer plugins would let the system decoder "
                   "handle it.";
    } else {
        message += " The installed system media plugins do not cover it either.";
    }
    return message;
}

} // namespace AudioCodecs
} // namespace UltraCanvas

#endif  // ULTRACANVAS_ENABLE_AUDIO
