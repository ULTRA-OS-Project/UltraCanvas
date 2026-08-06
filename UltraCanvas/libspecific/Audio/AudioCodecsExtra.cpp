// libspecific/Audio/AudioCodecsExtra.cpp
// Optional audio encoders/decoders backed by system codec libraries:
// libFLAC (FLAC encode), libvorbis/vorbisenc/vorbisfile (Ogg Vorbis
// encode+decode), libopusenc (Opus encode), opusfile (Opus decode) and
// LAME (MP3 encode). Each is compile-gated on the ULTRACANVAS_HAS_* define
// its CMake detection sets; anything missing degrades to false / null.
// Version: 0.1.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework

#ifdef ULTRACANVAS_ENABLE_AUDIO

#include "AudioCodecsExtra.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef ULTRACANVAS_HAS_LIBFLAC
#include <FLAC/stream_encoder.h>
#endif
#ifdef ULTRACANVAS_HAS_VORBIS
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>
#endif
#ifdef ULTRACANVAS_HAS_OPUSENC
#include <opusenc.h>
#endif
#ifdef ULTRACANVAS_HAS_OPUSFILE
#include <opusfile.h>
#endif
#ifdef ULTRACANVAS_HAS_LAME
#include <lame/lame.h>
#endif

namespace UltraCanvas {
namespace AudioCodecs {

namespace {

// ===== SHARED HELPERS =====

// fopen with UTF-8 paths on every platform (the narrow CRT fopen on Windows
// is limited to the system code page, mirroring the miniaudio backend).
FILE* OpenFileUtf8(const std::string& path, const char* mode) {
#if defined(_WIN32) || defined(_WIN64)
    std::wstring wpath = Utf8ToWide(path);
    std::wstring wmode(mode, mode + std::strlen(mode));
    return _wfopen(wpath.c_str(), wmode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

// Sample accessor: interleaved sample `idx` (= frame * channels + ch) as
// float in [-1, 1], for any of the four UCAudio sample layouts.
inline float SampleAsFloat(const uint8_t* data, AudioSampleType t, size_t idx) {
    switch (t) {
        case AudioSampleType::PCM_S16: {
            const int16_t* p = reinterpret_cast<const int16_t*>(data);
            return static_cast<float>(p[idx] / 32768.0);
        }
        case AudioSampleType::PCM_S24: {
            const uint8_t* p = data + idx * 3;
            int32_t v = (static_cast<int32_t>(p[0])) |
                        (static_cast<int32_t>(p[1]) << 8) |
                        (static_cast<int32_t>(p[2]) << 16);
            if (v & 0x800000) v |= ~0xFFFFFF;   // sign-extend 24 -> 32 bit
            return static_cast<float>(v / 8388608.0);
        }
        case AudioSampleType::PCM_S32: {
            const int32_t* p = reinterpret_cast<const int32_t*>(data);
            return static_cast<float>(p[idx] / 2147483648.0);
        }
        case AudioSampleType::PCM_F32:
        default: {
            const float* p = reinterpret_cast<const float*>(data);
            return p[idx];
        }
    }
}

#ifdef ULTRACANVAS_HAS_LIBFLAC
// Same sample as a signed integer at `bps` bits, taking the direct integer
// path where the source already is integer PCM (bit-exact, no float detour).
inline int32_t SampleAsInt(const uint8_t* data, AudioSampleType t, size_t idx, int bps) {
    switch (t) {
        case AudioSampleType::PCM_S16: {
            const int16_t* p = reinterpret_cast<const int16_t*>(data);
            int32_t v = p[idx];
            return bps == 16 ? v : v << (bps - 16);
        }
        case AudioSampleType::PCM_S24: {
            const uint8_t* p = data + idx * 3;
            int32_t v = (static_cast<int32_t>(p[0])) |
                        (static_cast<int32_t>(p[1]) << 8) |
                        (static_cast<int32_t>(p[2]) << 16);
            if (v & 0x800000) v |= ~0xFFFFFF;
            return bps == 24 ? v : v >> (24 - bps);
        }
        case AudioSampleType::PCM_S32: {
            const int32_t* p = reinterpret_cast<const int32_t*>(data);
            return p[idx] >> (32 - bps);
        }
        case AudioSampleType::PCM_F32:
        default: {
            const float* p = reinterpret_cast<const float*>(data);
            float s = std::clamp(p[idx], -1.0f, 1.0f);
            const int32_t maxV = (1 << (bps - 1)) - 1;
            return static_cast<int32_t>(std::lrintf(s * maxV));
        }
    }
}
#endif

constexpr size_t kChunkFrames = 4096;

// ===== FLAC ENCODE (libFLAC) =====
#ifdef ULTRACANVAS_HAS_LIBFLAC
bool EncodeFLAC(const std::string& path, const UCAudio& audio) {
    const AudioBufferInfo& info = audio.GetInfo();
    if (info.channels < 1 || info.channels > 8) return false;
    if (!FLAC__format_sample_rate_is_valid(static_cast<uint32_t>(info.sampleRate))) return false;

    // Keep 16-bit sources bit-exact; everything wider (or float) becomes 24-bit.
    const int bps = (info.sampleType == AudioSampleType::PCM_S16) ? 16 : 24;

    FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
    if (!enc) return false;
    FLAC__stream_encoder_set_channels(enc, static_cast<uint32_t>(info.channels));
    FLAC__stream_encoder_set_bits_per_sample(enc, static_cast<uint32_t>(bps));
    FLAC__stream_encoder_set_sample_rate(enc, static_cast<uint32_t>(info.sampleRate));
    FLAC__stream_encoder_set_compression_level(enc, 5);
    FLAC__stream_encoder_set_total_samples_estimate(enc, info.frameCount);

    FILE* fp = OpenFileUtf8(path, "wb");
    if (!fp) { FLAC__stream_encoder_delete(enc); return false; }
    if (FLAC__stream_encoder_init_FILE(enc, fp, nullptr, nullptr) !=
            FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        FLAC__stream_encoder_delete(enc);   // libFLAC closed fp on init
        return false;
    }

    const uint8_t* data = audio.GetData();
    const int ch = info.channels;
    std::vector<FLAC__int32> buf(kChunkFrames * ch);
    bool ok = true;
    for (size_t pos = 0; pos < info.frameCount && ok; pos += kChunkFrames) {
        const size_t n = std::min(kChunkFrames, info.frameCount - pos);
        for (size_t i = 0; i < n * ch; ++i) {
            buf[i] = SampleAsInt(data, info.sampleType, pos * ch + i, bps);
        }
        ok = FLAC__stream_encoder_process_interleaved(enc, buf.data(),
                                                      static_cast<uint32_t>(n));
    }
    ok = FLAC__stream_encoder_finish(enc) && ok;   // also closes fp
    FLAC__stream_encoder_delete(enc);
    return ok;
}
#endif

// ===== OGG VORBIS ENCODE (libvorbisenc + libogg) =====
#ifdef ULTRACANVAS_HAS_VORBIS
bool EncodeVorbis(const std::string& path, const UCAudio& audio) {
    const AudioBufferInfo& info = audio.GetInfo();
    if (info.channels < 1) return false;

    vorbis_info vi;
    vorbis_info_init(&vi);
    if (vorbis_encode_init_vbr(&vi, info.channels, info.sampleRate, 0.4f) != 0) {
        vorbis_info_clear(&vi);
        return false;
    }
    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "ENCODER", "UltraCanvas");
    vorbis_dsp_state vd;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block vb;
    vorbis_block_init(&vd, &vb);
    ogg_stream_state os;
    ogg_stream_init(&os, 0x55434156);   // fixed serial: single-stream files

    FILE* fp = OpenFileUtf8(path, "wb");
    bool ok = fp != nullptr;

    if (ok) {
        ogg_packet header, headerComm, headerCode;
        vorbis_analysis_headerout(&vd, &vc, &header, &headerComm, &headerCode);
        ogg_stream_packetin(&os, &header);
        ogg_stream_packetin(&os, &headerComm);
        ogg_stream_packetin(&os, &headerCode);
        ogg_page og;
        while (ogg_stream_flush(&os, &og) != 0) {
            ok = ok && std::fwrite(og.header, 1, og.header_len, fp) == static_cast<size_t>(og.header_len)
                    && std::fwrite(og.body, 1, og.body_len, fp) == static_cast<size_t>(og.body_len);
        }
    }

    const uint8_t* data = audio.GetData();
    const int ch = info.channels;
    size_t pos = 0;
    bool eos = false;
    while (ok && !eos) {
        const size_t n = std::min(kChunkFrames, info.frameCount - pos);
        if (n == 0) {
            vorbis_analysis_wrote(&vd, 0);   // signal end of input
        } else {
            float** buffer = vorbis_analysis_buffer(&vd, static_cast<int>(n));
            for (int c = 0; c < ch; ++c) {
                for (size_t i = 0; i < n; ++i) {
                    buffer[c][i] = SampleAsFloat(data, info.sampleType, (pos + i) * ch + c);
                }
            }
            vorbis_analysis_wrote(&vd, static_cast<int>(n));
            pos += n;
        }
        while (ok && vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while (ok && vorbis_bitrate_flushpacket(&vd, &op) != 0) {
                ogg_stream_packetin(&os, &op);
                ogg_page og;
                while (!eos && ogg_stream_pageout(&os, &og) != 0) {
                    ok = ok && std::fwrite(og.header, 1, og.header_len, fp) == static_cast<size_t>(og.header_len)
                            && std::fwrite(og.body, 1, og.body_len, fp) == static_cast<size_t>(og.body_len);
                    if (ogg_page_eos(&og)) eos = true;
                }
            }
        }
    }

    if (fp) std::fclose(fp);
    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    return ok && eos;
}
#endif

// ===== OPUS ENCODE (libopusenc) =====
#ifdef ULTRACANVAS_HAS_OPUSENC
extern "C" {
static int OpusWriteCb(void* user, const unsigned char* ptr, opus_int32 len) {
    FILE* fp = static_cast<FILE*>(user);
    return std::fwrite(ptr, 1, static_cast<size_t>(len), fp) == static_cast<size_t>(len) ? 0 : 1;
}
static int OpusCloseCb(void*) { return 0; }   // caller owns the FILE*
}

bool EncodeOpus(const std::string& path, const UCAudio& audio) {
    const AudioBufferInfo& info = audio.GetInfo();
    if (info.channels < 1 || info.channels > 8) return false;
    // libopusenc resamples internally; it accepts input rates 8 kHz - 768 kHz.
    if (info.sampleRate < 8000 || info.sampleRate > 768000) return false;

    FILE* fp = OpenFileUtf8(path, "wb");
    if (!fp) return false;

    OggOpusComments* comments = ope_comments_create();
    ope_comments_add(comments, "ENCODER", "UltraCanvas");
    const int family = info.channels <= 2 ? 0 : 1;
    int err = 0;
    OpusEncCallbacks cbs = { OpusWriteCb, OpusCloseCb };
    OggOpusEnc* enc = ope_encoder_create_callbacks(&cbs, fp, comments,
                                                   info.sampleRate, info.channels,
                                                   family, &err);
    if (!enc || err != OPE_OK) {
        ope_comments_destroy(comments);
        std::fclose(fp);
        return false;
    }

    const uint8_t* data = audio.GetData();
    const int ch = info.channels;
    std::vector<float> buf(kChunkFrames * ch);
    bool ok = true;
    for (size_t pos = 0; pos < info.frameCount && ok; pos += kChunkFrames) {
        const size_t n = std::min(kChunkFrames, info.frameCount - pos);
        for (size_t i = 0; i < n * ch; ++i) {
            buf[i] = SampleAsFloat(data, info.sampleType, pos * ch + i);
        }
        ok = ope_encoder_write_float(enc, buf.data(), static_cast<int>(n)) == OPE_OK;
    }
    ok = ope_encoder_drain(enc) == OPE_OK && ok;
    ope_encoder_destroy(enc);
    ope_comments_destroy(comments);
    std::fclose(fp);
    return ok;
}
#endif

// ===== MP3 ENCODE (LAME) =====
#ifdef ULTRACANVAS_HAS_LAME
bool EncodeMP3(const std::string& path, const UCAudio& audio) {
    const AudioBufferInfo& info = audio.GetInfo();
    if (info.channels < 1 || info.channels > 2) return false;   // MP3 is mono/stereo only

    lame_t gf = lame_init();
    if (!gf) return false;
    lame_set_in_samplerate(gf, info.sampleRate);
    lame_set_num_channels(gf, info.channels);
    lame_set_VBR(gf, vbr_default);
    lame_set_quality(gf, 5);
    if (lame_init_params(gf) < 0) {
        lame_close(gf);
        return false;
    }

    // "w+b": lame_mp3_tags_fid rewinds the stream to patch the Xing/VBR header.
    FILE* fp = OpenFileUtf8(path, "w+b");
    if (!fp) {
        lame_close(gf);
        return false;
    }

    const uint8_t* data = audio.GetData();
    const int ch = info.channels;
    std::vector<float> left(kChunkFrames), right(kChunkFrames);
    std::vector<unsigned char> mp3(kChunkFrames * 5 / 4 + 7200);
    bool ok = true;
    for (size_t pos = 0; pos < info.frameCount && ok; pos += kChunkFrames) {
        const size_t n = std::min(kChunkFrames, info.frameCount - pos);
        for (size_t i = 0; i < n; ++i) {
            left[i] = SampleAsFloat(data, info.sampleType, (pos + i) * ch);
            right[i] = ch == 2 ? SampleAsFloat(data, info.sampleType, (pos + i) * ch + 1)
                               : left[i];
        }
        int written = lame_encode_buffer_ieee_float(gf, left.data(), right.data(),
                                                    static_cast<int>(n),
                                                    mp3.data(), static_cast<int>(mp3.size()));
        ok = written >= 0 &&
             std::fwrite(mp3.data(), 1, static_cast<size_t>(written), fp) ==
                 static_cast<size_t>(written);
    }
    if (ok) {
        int written = lame_encode_flush(gf, mp3.data(), static_cast<int>(mp3.size()));
        ok = written >= 0 &&
             std::fwrite(mp3.data(), 1, static_cast<size_t>(written), fp) ==
                 static_cast<size_t>(written);
    }
    if (ok) lame_mp3_tags_fid(gf, fp);   // VBR header for correct duration display
    lame_close(gf);
    std::fclose(fp);
    return ok;
}
#endif

// ===== OGG VORBIS DECODE (libvorbisfile) =====
#ifdef ULTRACANVAS_HAS_VORBIS
std::shared_ptr<UCAudio> DecodeVorbis(const std::string& path) {
    FILE* fp = OpenFileUtf8(path, "rb");
    if (!fp) return nullptr;
    OggVorbis_File vf;
    if (ov_open(fp, &vf, nullptr, 0) != 0) {   // on success ov_clear owns fp
        std::fclose(fp);
        return nullptr;
    }
    const vorbis_info* vi = ov_info(&vf, -1);
    if (!vi || vi->channels < 1) {
        ov_clear(&vf);
        return nullptr;
    }
    const int ch = vi->channels;
    const long rate = vi->rate;

    std::vector<float> samples;
    if (ogg_int64_t total = ov_pcm_total(&vf, -1); total > 0) {
        samples.reserve(static_cast<size_t>(total) * ch);
    }
    for (;;) {
        float** pcm = nullptr;
        int section = 0;
        long n = ov_read_float(&vf, &pcm, static_cast<int>(kChunkFrames), &section);
        if (n == 0) break;          // EOF
        if (n < 0) continue;        // hole in stream: skip
        for (long i = 0; i < n; ++i) {
            for (int c = 0; c < ch; ++c) samples.push_back(pcm[c][i]);
        }
    }
    ov_clear(&vf);

    AudioBufferInfo info;
    info.sampleRate = static_cast<int>(rate);
    info.channels = ch;
    info.sampleType = AudioSampleType::PCM_F32;
    info.frameCount = samples.size() / ch;
    info.durationSeconds = rate > 0 ? static_cast<double>(info.frameCount) / rate : 0.0;

    auto audio = std::make_shared<UCAudio>();
    audio->MutableData().resize(samples.size() * sizeof(float));
    std::memcpy(audio->MutableData().data(), samples.data(),
                samples.size() * sizeof(float));
    audio->SetInfo(info);
    audio->SetSourceFormat(AudioFormat::OGG);
    return audio;
}
#endif

// ===== OPUS DECODE (opusfile) =====
#ifdef ULTRACANVAS_HAS_OPUSFILE
extern "C" {
static int OpusFileReadCb(void* stream, unsigned char* ptr, int nbytes) {
    return static_cast<int>(std::fread(ptr, 1, static_cast<size_t>(nbytes),
                                       static_cast<FILE*>(stream)));
}
static int OpusFileSeekCb(void* stream, opus_int64 offset, int whence) {
#if defined(_WIN32) || defined(_WIN64)
    return _fseeki64(static_cast<FILE*>(stream), offset, whence);
#else
    return fseeko(static_cast<FILE*>(stream), static_cast<off_t>(offset), whence);
#endif
}
static opus_int64 OpusFileTellCb(void* stream) {
#if defined(_WIN32) || defined(_WIN64)
    return _ftelli64(static_cast<FILE*>(stream));
#else
    return static_cast<opus_int64>(ftello(static_cast<FILE*>(stream)));
#endif
}
static int OpusFileCloseCb(void* stream) {
    return std::fclose(static_cast<FILE*>(stream));
}
}

std::shared_ptr<UCAudio> DecodeOpus(const std::string& path) {
    FILE* fp = OpenFileUtf8(path, "rb");
    if (!fp) return nullptr;
    OpusFileCallbacks cbs = { OpusFileReadCb, OpusFileSeekCb,
                              OpusFileTellCb, OpusFileCloseCb };
    int err = 0;
    OggOpusFile* of = op_open_callbacks(fp, &cbs, nullptr, 0, &err);
    if (!of) {              // on failure the close callback was not invoked
        std::fclose(fp);
        return nullptr;
    }
    const int ch = op_channel_count(of, -1);
    if (ch < 1) {
        op_free(of);
        return nullptr;
    }

    std::vector<float> samples;
    if (ogg_int64_t total = op_pcm_total(of, -1); total > 0) {
        samples.reserve(static_cast<size_t>(total) * ch);
    }
    std::vector<float> buf(kChunkFrames * ch);
    for (;;) {
        int n = op_read_float(of, buf.data(), static_cast<int>(buf.size()), nullptr);
        if (n == 0) break;          // EOF
        if (n < 0) { op_free(of); return nullptr; }
        samples.insert(samples.end(), buf.begin(), buf.begin() + n * ch);
    }
    op_free(of);

    AudioBufferInfo info;
    info.sampleRate = 48000;        // Opus always decodes at 48 kHz
    info.channels = ch;
    info.sampleType = AudioSampleType::PCM_F32;
    info.frameCount = samples.size() / ch;
    info.durationSeconds = static_cast<double>(info.frameCount) / 48000.0;

    auto audio = std::make_shared<UCAudio>();
    audio->MutableData().resize(samples.size() * sizeof(float));
    std::memcpy(audio->MutableData().data(), samples.data(),
                samples.size() * sizeof(float));
    audio->SetInfo(info);
    audio->SetSourceFormat(AudioFormat::Opus);
    return audio;
}
#endif

// True when the file starts with the Ogg capture pattern ("OggS") — the
// container both Vorbis and Opus files live in.
bool LooksLikeOgg(const std::string& path) {
    FILE* fp = OpenFileUtf8(path, "rb");
    if (!fp) return false;
    unsigned char magic[4] = {0};
    size_t n = std::fread(magic, 1, 4, fp);
    std::fclose(fp);
    return n == 4 && std::memcmp(magic, "OggS", 4) == 0;
}

} // namespace

// ===== PUBLIC SURFACE =====

bool CanEncode(AudioFormat format) {
    switch (format) {
#ifdef ULTRACANVAS_HAS_LIBFLAC
        case AudioFormat::FLAC: return true;
#endif
#ifdef ULTRACANVAS_HAS_VORBIS
        case AudioFormat::OGG: return true;
#endif
#ifdef ULTRACANVAS_HAS_OPUSENC
        case AudioFormat::Opus: return true;
#endif
#ifdef ULTRACANVAS_HAS_LAME
        case AudioFormat::MP3: return true;
#endif
        default: return false;
    }
}

bool Encode(const std::string& path, const UCAudio& audio, AudioFormat format) {
    if (!audio.IsValid() || audio.GetInfo().frameCount == 0) return false;
    switch (format) {
#ifdef ULTRACANVAS_HAS_LIBFLAC
        case AudioFormat::FLAC: return EncodeFLAC(path, audio);
#endif
#ifdef ULTRACANVAS_HAS_VORBIS
        case AudioFormat::OGG: return EncodeVorbis(path, audio);
#endif
#ifdef ULTRACANVAS_HAS_OPUSENC
        case AudioFormat::Opus: return EncodeOpus(path, audio);
#endif
#ifdef ULTRACANVAS_HAS_LAME
        case AudioFormat::MP3: return EncodeMP3(path, audio);
#endif
        default: return false;
    }
}

std::shared_ptr<UCAudio> TryDecode(const std::string& path) {
    if (!LooksLikeOgg(path)) return nullptr;
#ifdef ULTRACANVAS_HAS_VORBIS
    if (auto audio = DecodeVorbis(path)) return audio;
#endif
#ifdef ULTRACANVAS_HAS_OPUSFILE
    if (auto audio = DecodeOpus(path)) return audio;
#endif
    return nullptr;
}

} // namespace AudioCodecs
} // namespace UltraCanvas

#endif // ULTRACANVAS_ENABLE_AUDIO
