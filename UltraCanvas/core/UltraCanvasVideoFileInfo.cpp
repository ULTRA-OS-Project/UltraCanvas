// UltraCanvas/core/UltraCanvasVideoFileInfo.cpp
// Reads a video file's technical facts from its container headers: ISO base
// media (MP4 / MOV / 3GP), Matroska / WebM and AVI. See
// UltraCanvasVideoFileInfo.h. Every read is bounds-checked against the bytes
// actually read; nothing here throws or trusts a size field.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasVideoFileInfo.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <locale>
#include <sstream>
#include <vector>

namespace UltraCanvas {

namespace {

using Bytes = std::vector<unsigned char>;

uint16_t U16BE(const unsigned char* p) { return uint16_t((p[0] << 8) | p[1]); }
uint32_t U32BE(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint64_t U64BE(const unsigned char* p) { return (uint64_t(U32BE(p)) << 32) | U32BE(p + 4); }
uint16_t U16LE(const unsigned char* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t U32LE(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

std::string FourCC(const unsigned char* p) { return std::string(reinterpret_cast<const char*>(p), 4); }

// A file opened by its UTF-8 path, read in bounded pieces.
class FileBytes {
public:
    explicit FileBytes(const std::string& utf8Path)
        : in(PathFromUtf8(utf8Path), std::ios::binary) {
        if (!in) return;
        in.seekg(0, std::ios::end);
        const std::streamoff end = in.tellg();
        size = end > 0 ? uint64_t(end) : 0;
    }
    bool IsOpen() const { return static_cast<bool>(in) || size > 0; }
    uint64_t Size() const { return size; }
    Bytes Read(uint64_t pos, size_t count) {
        Bytes out;
        if (pos >= size) return out;
        count = size_t(std::min<uint64_t>(count, size - pos));
        out.resize(count);
        in.clear();
        in.seekg(std::streamoff(pos));
        in.read(reinterpret_cast<char*>(out.data()), std::streamsize(count));
        out.resize(size_t(std::max<std::streamsize>(in.gcount(), 0)));
        return out;
    }
private:
    std::ifstream in;
    uint64_t size = 0;
};

std::string FormatClassic(double value, int decimals) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os.setf(std::ios::fixed);
    os.precision(decimals);
    os << value;
    std::string s = os.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

// Days since 1970-01-01 to a civil date (Howard Hinnant's algorithm).
void CivilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = int(yy + (m <= 2));
}

std::string FormatUnixTimeUtc(int64_t seconds) {
    // Before 1971 or past 2200 is a zeroed or garbage field, not a date.
    if (seconds < 31536000 || seconds > 7258118400LL) return {};
    int64_t days = seconds / 86400;
    int64_t rest = seconds % 86400;
    int y; unsigned m, d;
    CivilFromDays(days, y, m, d);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u %02d:%02d:%02d", y, m, d,
                  int(rest / 3600), int(rest / 60 % 60), int(rest % 60));
    return buf;
}

std::string LevelText(int whole, int tenth) {
    return std::to_string(whole) + (tenth ? "." + std::to_string(tenth) : std::string(".0"));
}

// ----- codec configuration records (shared by MP4 and Matroska) -----

void ParseAvcC(const unsigned char* p, size_t n, VideoFileInfo& out) {
    if (n < 4) return;
    const int profile = p[1], level = p[3];
    const char* name = nullptr;
    switch (profile) {
        case 66:  name = (p[2] & 0x40) ? "Constrained Baseline" : "Baseline"; break;
        case 77:  name = "Main"; break;
        case 88:  name = "Extended"; break;
        case 100: name = "High"; break;
        case 110: name = "High 10"; out.bitDepth = 10; break;
        case 122: name = "High 4:2:2"; break;
        case 244: name = "High 4:4:4 Predictive"; break;
        default: break;
    }
    std::string text = name ? name : "Profile " + std::to_string(profile);
    if (level > 0) text += ", level " + LevelText(level / 10, level % 10);
    out.videoProfile = text;
    if (!out.bitDepth && (profile == 66 || profile == 77 || profile == 88)) out.bitDepth = 8;
    // High profiles carry the bit depth after the parameter sets.
    if (profile >= 100 && n >= 6) {
        size_t pos = 5;
        int sps = p[pos++] & 0x1F;
        for (int i = 0; i < sps && pos + 2 <= n; ++i) pos += 2 + U16BE(p + pos);
        if (pos < n) {
            int pps = p[pos++];
            for (int i = 0; i < pps && pos + 2 <= n; ++i) pos += 2 + U16BE(p + pos);
            if (pos + 3 <= n) out.bitDepth = (p[pos + 1] & 7) + 8;
        }
    }
}

void ParseHvcC(const unsigned char* p, size_t n, VideoFileInfo& out) {
    if (n < 19) return;
    const int profile = p[1] & 0x1F;
    const bool highTier = (p[1] & 0x20) != 0;
    const int level = p[12];
    const char* name = profile == 1 ? "Main" : profile == 2 ? "Main 10"
                     : profile == 3 ? "Main Still Picture" : profile == 4 ? "Range Extensions"
                     : nullptr;
    std::string text = name ? name : "Profile " + std::to_string(profile);
    if (level > 0) text += ", level " + LevelText(level / 30, (level % 30) / 3);
    if (highTier) text += ", high tier";
    out.videoProfile = text;
    out.bitDepth = (p[17] & 7) + 8;
}

void ParseAv1C(const unsigned char* p, size_t n, VideoFileInfo& out) {
    if (n < 3 || !(p[0] & 0x80)) return;
    const int profile = p[1] >> 5;
    const int levelIdx = p[1] & 0x1F;
    const bool highTier = (p[2] & 0x80) != 0;
    const bool highBitDepth = (p[2] & 0x40) != 0, twelveBit = (p[2] & 0x20) != 0;
    std::string text = profile == 0 ? "Main" : profile == 1 ? "High" : profile == 2 ? "Professional"
                     : "Profile " + std::to_string(profile);
    if (levelIdx < 24) text += ", level " + std::to_string(2 + (levelIdx >> 2)) + "." + std::to_string(levelIdx & 3);
    if (highTier) text += ", high tier";
    out.videoProfile = text;
    out.bitDepth = highBitDepth ? (twelveBit ? 12 : 10) : 8;
}

// vpcC: a FullBox body (version / flags first).
void ParseVpcC(const unsigned char* p, size_t n, VideoFileInfo& out) {
    if (n < 7) return;
    const int profile = p[4], level = p[5];
    std::string text = "Profile " + std::to_string(profile);
    if (level > 0) text += ", level " + LevelText(level / 10, level % 10);
    out.videoProfile = text;
    out.bitDepth = p[6] >> 4;
}

// MPEG-4 AudioSpecificConfig: the AAC flavour, its rate and channels.
void ParseAudioSpecificConfig(const unsigned char* p, size_t n, VideoFileInfo& out) {
    if (n < 2) return;
    size_t bit = 0;
    auto bits = [&](int count) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < count; ++i, ++bit) {
            if (bit / 8 >= n) return v << (count - i);
            v = (v << 1) | ((p[bit / 8] >> (7 - bit % 8)) & 1);
        }
        return v;
    };
    static const int kRates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                   22050, 16000, 12000, 11025, 8000, 7350};
    auto readRate = [&]() -> int {
        const uint32_t idx = bits(4);
        if (idx == 15) return int(bits(24));
        return idx < 13 ? kRates[idx] : 0;
    };
    uint32_t aot = bits(5);
    if (aot == 31) aot = 32 + bits(6);
    int rate = readRate();
    const uint32_t channelConfig = bits(4);
    if (aot == 5 || aot == 29) rate = readRate();   // SBR: the output rate follows
    const char* name = nullptr;
    switch (aot) {
        case 1: name = "AAC Main"; break;
        case 2: name = "AAC LC"; break;
        case 3: name = "AAC SSR"; break;
        case 4: name = "AAC LTP"; break;
        case 5: name = "HE-AAC"; break;
        case 23: name = "AAC LD"; break;
        case 29: name = "HE-AAC v2"; break;
        case 39: name = "AAC ELD"; break;
        case 42: name = "xHE-AAC"; break;
        default: name = "AAC"; break;
    }
    out.audioCodec = name;
    if (rate > 0) out.sampleRate = rate;
    static const int kChannels[8] = {0, 1, 2, 3, 4, 5, 6, 8};
    if (channelConfig > 0 && channelConfig < 8) out.channels = kChannels[channelConfig];
    if (aot == 29) out.channels = 2;   // parametric stereo: one channel coded, two played
}

std::string AviAudioFormatName(uint16_t tag) {
    switch (tag) {
        case 0x0001: return "PCM";
        case 0x0002: return "ADPCM";
        case 0x0003: return "PCM (float)";
        case 0x0006: return "A-law";
        case 0x0007: return "\xC2\xB5-law";
        case 0x0011: return "IMA ADPCM";
        case 0x0050: return "MP2";
        case 0x0055: return "MP3";
        case 0x00FF: case 0x1610: case 0x706D: return "AAC";
        case 0x0160: case 0x0161: return "WMA";
        case 0x2000: return "AC-3";
        case 0x2001: return "DTS";
        case 0xF1AC: return "FLAC";
        default: {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%04X", tag);
            return buf;
        }
    }
}

std::string VideoFourCCName(std::string fcc, bool fromAvi) {
    if (fromAvi)
        std::transform(fcc.begin(), fcc.end(), fcc.begin(),
                       [](unsigned char c) { return char(std::toupper(c)); });
    struct Map { const char* fcc; const char* name; };
    static const Map mp4[] = {
        {"avc1", "H.264"}, {"avc3", "H.264"}, {"hvc1", "H.265"}, {"hev1", "H.265"},
        {"dvh1", "H.265 (Dolby Vision)"}, {"dvhe", "H.265 (Dolby Vision)"},
        {"vp08", "VP8"}, {"vp09", "VP9"}, {"av01", "AV1"}, {"mp4v", "MPEG-4 Visual"},
        {"s263", "H.263"}, {"h263", "H.263"}, {"mjpa", "Motion JPEG"}, {"mjpb", "Motion JPEG"},
        {"jpeg", "Motion JPEG"}, {"apch", "ProRes 422 HQ"}, {"apcn", "ProRes 422"},
        {"apcs", "ProRes 422 LT"}, {"apco", "ProRes 422 Proxy"}, {"ap4h", "ProRes 4444"},
        {"ap4x", "ProRes 4444 XQ"}, {"dvc ", "DV"}, {"dvcp", "DV"}, {"dv5p", "DVCPRO50"},
        {"mp2v", "MPEG-2"}, {"xd5c", "XDCAM HD422"}, {"AVdn", "DNxHD"}, {"AVdh", "DNxHR"},
        {"png ", "PNG"}, {"rle ", "Animation (RLE)"}, {"raw ", "Uncompressed"},
        {"2vuy", "Uncompressed 4:2:2"}, {"v210", "Uncompressed 10-bit 4:2:2"},
        {"cvid", "Cinepak"}, {"SVQ3", "Sorenson Video 3"}, {"SVQ1", "Sorenson Video"},
        {"theo", "Theora"}, {"ffv1", "FFV1"},
    };
    static const Map avi[] = {
        {"H264", "H.264"}, {"X264", "H.264"}, {"AVC1", "H.264"}, {"DAVC", "H.264"},
        {"H265", "H.265"}, {"X265", "H.265"}, {"HEVC", "H.265"}, {"HVC1", "H.265"},
        {"XVID", "MPEG-4 Visual (Xvid)"}, {"DIVX", "MPEG-4 Visual (DivX)"},
        {"DX50", "MPEG-4 Visual (DivX 5)"}, {"FMP4", "MPEG-4 Visual"}, {"MP4V", "MPEG-4 Visual"},
        {"DIV3", "MS MPEG-4 v3 (DivX 3)"}, {"MP43", "MS MPEG-4 v3"}, {"MP42", "MS MPEG-4 v2"},
        {"MJPG", "Motion JPEG"}, {"DVSD", "DV"}, {"WMV1", "WMV 7"}, {"WMV2", "WMV 8"},
        {"WMV3", "WMV 9"}, {"WVC1", "VC-1"}, {"VP80", "VP8"}, {"VP90", "VP9"},
        {"AV01", "AV1"}, {"MPG2", "MPEG-2"}, {"MPEG", "MPEG-1"}, {"CVID", "Cinepak"},
        {"IV50", "Indeo 5"}, {"IV41", "Indeo 4"}, {"IV32", "Indeo 3"}, {"FFV1", "FFV1"},
        {"HFYU", "HuffYUV"}, {"MSVC", "Microsoft Video 1"}, {"CRAM", "Microsoft Video 1"},
    };
    if (fromAvi) {
        for (const Map& m : avi) if (fcc == m.fcc) return m.name;
    } else {
        for (const Map& m : mp4) if (fcc == m.fcc) return m.name;
    }
    if (fromAvi && (fcc == std::string(4, '\0') || fcc == "RGB " || fcc == "DIB ")) return "Uncompressed";
    std::string trimmed = fcc;
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\0')) trimmed.pop_back();
    return trimmed;
}

std::string AudioFourCCName(const std::string& fcc) {
    struct Map { const char* fcc; const char* name; };
    static const Map map[] = {
        {"mp4a", "AAC"}, {"ac-3", "AC-3"}, {"ec-3", "E-AC-3"}, {"ac-4", "AC-4"},
        {"alac", "ALAC"}, {"Opus", "Opus"}, {"opus", "Opus"}, {"fLaC", "FLAC"},
        {"twos", "PCM"}, {"sowt", "PCM"}, {"lpcm", "PCM"}, {"in24", "PCM"},
        {"in32", "PCM"}, {"fl32", "PCM (float)"}, {"fl64", "PCM (float)"},
        {"raw ", "PCM"}, {"ulaw", "\xC2\xB5-law"}, {"alaw", "A-law"},
        {"ima4", "IMA ADPCM"}, {"samr", "AMR"}, {"sawb", "AMR-WB"}, {".mp3", "MP3"},
        {"mp3 ", "MP3"}, {"dtsc", "DTS"}, {"dtsh", "DTS-HD"}, {"dtsl", "DTS-HD MA"},
        {"dtse", "DTS Express"}, {"mlpa", "TrueHD"}, {"Qclp", "QCELP"}, {"agsm", "GSM"},
    };
    for (const Map& m : map) if (fcc == m.fcc) return m.name;
    std::string trimmed = fcc;
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\0')) trimmed.pop_back();
    return trimmed;
}

// =====================================================================
// ISO base media (MP4 / MOV / 3GP)
// =====================================================================

struct Mp4Track {
    uint32_t trackId = 0;
    uint32_t trexDuration = 0, trexSize = 0;   // fragment defaults (mvex > trex)
    std::string handler;            // "vide", "soun", "sbtl", "subt", "text", ...
    uint32_t timescale = 0;
    uint64_t mediaDuration = 0;
    std::string language;
    int tkhdWidth = 0, tkhdHeight = 0;
    int rotation = 0;
    uint64_t sttsSamples = 0, sttsDelta = 0;
    uint64_t sampleCount = 0, sampleBytes = 0;
    bool sizesKnown = false;
    int64_t declaredBitRate = 0;    // btrt / esds average
    int parH = 0, parV = 0;
    VideoFileInfo codec;            // the codec fields of this track only
};

// Calls `fn(type, body, bodyLength)` for each box in [p, p+n).
template <typename Fn>
void ForEachBox(const unsigned char* p, size_t n, Fn&& fn) {
    size_t pos = 0;
    for (int guard = 0; pos + 8 <= n && guard < 4096; ++guard) {
        uint64_t size = U32BE(p + pos);
        size_t header = 8;
        if (size == 1) {
            if (pos + 16 > n) return;
            size = U64BE(p + pos + 8);
            header = 16;
        } else if (size == 0) {
            size = n - pos;
        }
        if (size < header || size > n - pos) return;
        fn(FourCC(p + pos + 4), p + pos + header, size_t(size - header));
        pos += size_t(size);
    }
}

// A descriptor length in an esds: up to four 7-bit groups.
bool ReadDescriptorLength(const unsigned char* p, size_t n, size_t& pos, size_t& length) {
    length = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= n) return false;
        const unsigned char b = p[pos++];
        length = (length << 7) | (b & 0x7F);
        if (!(b & 0x80)) return true;
    }
    return true;
}

void ParseEsds(const unsigned char* p, size_t n, Mp4Track& t, bool video) {
    size_t pos = 4;   // version / flags
    while (pos + 2 <= n) {
        const unsigned char tag = p[pos++];
        size_t length = 0;
        if (!ReadDescriptorLength(p, n, pos, length)) return;
        const size_t end = std::min(n, pos + length);
        if (tag == 0x03) {             // ES_Descriptor: its fields, then nested descriptors
            if (pos + 3 > end) return;
            const unsigned char flags = p[pos + 2];
            pos += 3;
            if (flags & 0x80) pos += 2;
            if ((flags & 0x40) && pos < end) pos += 1 + p[pos];
            if (flags & 0x20) pos += 2;
            continue;
        }
        if (tag == 0x04) {             // DecoderConfigDescriptor
            if (pos + 13 > end) return;
            const unsigned char oti = p[pos];
            const uint32_t avg = U32BE(p + pos + 9);
            if (avg) t.declaredBitRate = avg;
            if (video) {
                if (oti == 0x20) t.codec.videoCodec = "MPEG-4 Visual";
                else if (oti >= 0x60 && oti <= 0x65) t.codec.videoCodec = "MPEG-2";
                else if (oti == 0x6A) t.codec.videoCodec = "MPEG-1";
                else if (oti == 0x6C) t.codec.videoCodec = "Motion JPEG";
            } else {
                if (oti == 0x69 || oti == 0x6B) t.codec.audioCodec = "MP3";
                else if (oti >= 0x66 && oti <= 0x68) t.codec.audioCodec = "AAC";
                else if (oti == 0xA5) t.codec.audioCodec = "AC-3";
                else if (oti == 0xA6) t.codec.audioCodec = "E-AC-3";
                else if (oti == 0xA9) t.codec.audioCodec = "DTS";
                else if (oti == 0xAD) t.codec.audioCodec = "Opus";
            }
            pos += 13;
            continue;                  // the DecoderSpecificInfo is nested inside
        }
        if (tag == 0x05) {             // DecoderSpecificInfo
            if (!video && t.codec.audioCodec.rfind("AAC", 0) == 0)
                ParseAudioSpecificConfig(p + pos, end - pos, t.codec);
            return;
        }
        pos = end;
    }
}

void ParseVideoSampleEntryChildren(const unsigned char* p, size_t n, Mp4Track& t) {
    ForEachBox(p, n, [&](const std::string& type, const unsigned char* b, size_t len) {
        if (type == "avcC") ParseAvcC(b, len, t.codec);
        else if (type == "hvcC") ParseHvcC(b, len, t.codec);
        else if (type == "av1C") ParseAv1C(b, len, t.codec);
        else if (type == "vpcC") ParseVpcC(b, len, t.codec);
        else if (type == "esds") ParseEsds(b, len, t, true);
        else if (type == "pasp" && len >= 8) { t.parH = int(U32BE(b)); t.parV = int(U32BE(b + 4)); }
        else if (type == "btrt" && len >= 12 && !t.declaredBitRate) t.declaredBitRate = U32BE(b + 8);
        else if (type == "sinf") {     // encrypted: the original format
            ForEachBox(b, len, [&](const std::string& st, const unsigned char* sb, size_t sl) {
                if (st == "frma" && sl >= 4) t.codec.videoCodec = VideoFourCCName(FourCC(sb), false);
            });
        }
    });
}

void ParseAudioSampleEntryChildren(const unsigned char* p, size_t n, Mp4Track& t) {
    ForEachBox(p, n, [&](const std::string& type, const unsigned char* b, size_t len) {
        if (type == "esds") ParseEsds(b, len, t, false);
        else if (type == "wave") ParseAudioSampleEntryChildren(b, len, t);   // QuickTime wraps esds
        else if (type == "dac3" && len >= 3) {
            static const int kRates[3] = {48000, 44100, 32000};
            static const int kKbps[19] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192,
                                          224, 256, 320, 384, 448, 512, 576, 640};
            static const int kChannels[8] = {2, 1, 2, 3, 3, 4, 4, 5};
            const int fscod = b[0] >> 6;
            const int acmod = (b[1] >> 3) & 7;
            const int lfe = (b[1] >> 2) & 1;
            const int rateCode = ((b[1] & 3) << 3) | (b[2] >> 5);
            if (fscod < 3) t.codec.sampleRate = kRates[fscod];
            t.codec.channels = kChannels[acmod] + lfe;
            if (rateCode < 19 && !t.declaredBitRate) t.declaredBitRate = int64_t(kKbps[rateCode]) * 1000;
        } else if (type == "dec3" && len >= 2) {
            const int kbps = (U16BE(b) >> 3);
            if (kbps && !t.declaredBitRate) t.declaredBitRate = int64_t(kbps) * 1000;
        } else if (type == "dOps" && len >= 8) {
            t.codec.channels = b[1];
            t.codec.sampleRate = 48000;    // Opus always decodes at 48 kHz
        } else if (type == "btrt" && len >= 12 && !t.declaredBitRate) {
            t.declaredBitRate = U32BE(b + 8);
        } else if (type == "sinf") {
            ForEachBox(b, len, [&](const std::string& st, const unsigned char* sb, size_t sl) {
                if (st == "frma" && sl >= 4) t.codec.audioCodec = AudioFourCCName(FourCC(sb));
            });
        }
    });
}

void ParseStsd(const unsigned char* p, size_t n, Mp4Track& t) {
    if (n < 16) return;
    const unsigned char* e = p + 8;          // the first sample entry
    const size_t entrySize = std::min<size_t>(U32BE(e), n - 8);
    if (entrySize < 16) return;
    const std::string fcc = FourCC(e + 4);
    if (t.handler == "vide") {
        t.codec.videoCodec = VideoFourCCName(fcc, false);
        if (entrySize >= 86) {
            t.codec.width = U16BE(e + 32);
            t.codec.height = U16BE(e + 34);
            ParseVideoSampleEntryChildren(e + 86, entrySize - 86, t);
        }
    } else if (t.handler == "soun") {
        t.codec.audioCodec = AudioFourCCName(fcc);
        if (entrySize < 36) return;
        const int version = U16BE(e + 16);
        t.codec.channels = U16BE(e + 24);
        const int sampleSize = U16BE(e + 26);
        t.codec.sampleRate = int(U32BE(e + 32) >> 16);
        size_t children = 36;
        if (version == 1) children = 52;
        else if (version == 2 && entrySize >= 72) {
            uint64_t bits = U64BE(e + 40);
            double rate;
            std::memcpy(&rate, &bits, sizeof(rate));
            if (rate > 0 && rate < 1e7) t.codec.sampleRate = int(std::lround(rate));
            t.codec.channels = int(U32BE(e + 48));
            const uint32_t bitsPerChannel = U32BE(e + 56);
            if (bitsPerChannel) t.codec.bitsPerSample = int(bitsPerChannel);
            children = 72;
        }
        if (t.codec.audioCodec.rfind("PCM", 0) == 0 || fcc == "alac") {
            if (!t.codec.bitsPerSample) t.codec.bitsPerSample = sampleSize;
            if (fcc == "in24") t.codec.bitsPerSample = 24;
            if (fcc == "in32" || fcc == "fl32") t.codec.bitsPerSample = 32;
            if (fcc == "fl64") t.codec.bitsPerSample = 64;
        }
        if (entrySize > children) ParseAudioSampleEntryChildren(e + children, entrySize - children, t);
    }
}

void ParseStbl(const unsigned char* p, size_t n, Mp4Track& t) {
    ForEachBox(p, n, [&](const std::string& type, const unsigned char* b, size_t len) {
        if (type == "stsd") {
            ParseStsd(b, len, t);
        } else if (type == "stts" && len >= 8) {
            const uint32_t count = U32BE(b + 4);
            for (uint32_t i = 0; i < count && 8 + size_t(i) * 8 + 8 <= len; ++i) {
                const uint64_t samples = U32BE(b + 8 + size_t(i) * 8);
                const uint64_t delta = U32BE(b + 12 + size_t(i) * 8);
                t.sttsSamples += samples;
                t.sttsDelta += samples * delta;
            }
        } else if (type == "stsz" && len >= 12) {
            const uint32_t uniform = U32BE(b + 4);
            const uint32_t count = U32BE(b + 8);
            t.sampleCount = count;
            if (uniform) {
                t.sampleBytes = uint64_t(uniform) * count;
                t.sizesKnown = true;
            } else if (12 + size_t(count) * 4 <= len) {
                for (uint32_t i = 0; i < count; ++i) t.sampleBytes += U32BE(b + 12 + size_t(i) * 4);
                t.sizesKnown = true;
            }
        } else if (type == "stz2" && len >= 12) {
            const int field = b[7];
            const uint32_t count = U32BE(b + 8);
            t.sampleCount = count;
            if ((field == 4 || field == 8 || field == 16) && 12 + (size_t(count) * field + 7) / 8 <= len) {
                for (uint32_t i = 0; i < count; ++i) {
                    if (field == 16) t.sampleBytes += U16BE(b + 12 + size_t(i) * 2);
                    else if (field == 8) t.sampleBytes += b[12 + i];
                    else t.sampleBytes += (i & 1) ? (b[12 + i / 2] & 0x0F) : (b[12 + i / 2] >> 4);
                }
                t.sizesKnown = true;
            }
        }
    });
}

void ParseTrak(const unsigned char* p, size_t n, Mp4Track& t) {
    ForEachBox(p, n, [&](const std::string& type, const unsigned char* b, size_t len) {
        if (type == "tkhd" && len >= 84) {
            const bool v1 = b[0] == 1;
            t.trackId = U32BE(b + (v1 ? 20 : 12));
            const size_t matrix = v1 ? 52 : 40;
            if (len >= matrix + 44) {
                const int32_t a = int32_t(U32BE(b + matrix)), bb = int32_t(U32BE(b + matrix + 4));
                const int32_t c = int32_t(U32BE(b + matrix + 12)), d = int32_t(U32BE(b + matrix + 16));
                const int32_t one = 0x10000;
                if (a == 0 && bb == one && c == -one && d == 0) t.rotation = 90;
                else if (a == -one && bb == 0 && c == 0 && d == -one) t.rotation = 180;
                else if (a == 0 && bb == -one && c == one && d == 0) t.rotation = 270;
                t.tkhdWidth = int(U32BE(b + matrix + 36) >> 16);
                t.tkhdHeight = int(U32BE(b + matrix + 40) >> 16);
            }
        } else if (type == "mdia") {
            // hdlr first: stsd is read by the handler it belongs to.
            ForEachBox(b, len, [&](const std::string& mt, const unsigned char* mb, size_t ml) {
                if (mt == "hdlr" && ml >= 12) t.handler = FourCC(mb + 8);
                else if (mt == "mdhd" && ml >= 24) {
                    const bool v1 = mb[0] == 1;
                    if (v1 && ml >= 34) {
                        t.timescale = U32BE(mb + 20);
                        t.mediaDuration = U64BE(mb + 24);
                    } else if (!v1) {
                        t.timescale = U32BE(mb + 12);
                        t.mediaDuration = U32BE(mb + 16);
                    }
                    const size_t langAt = v1 ? 32 : 20;
                    if (ml >= langAt + 2) {
                        const uint16_t lang = U16BE(mb + langAt);
                        if (lang && lang != 0x55C4 /* "und" */ && lang < 0x8000) {
                            std::string s;
                            s += char(((lang >> 10) & 0x1F) + 0x60);
                            s += char(((lang >> 5) & 0x1F) + 0x60);
                            s += char((lang & 0x1F) + 0x60);
                            if (std::all_of(s.begin(), s.end(), [](char ch) { return ch >= 'a' && ch <= 'z'; }))
                                t.language = s;
                        }
                    }
                }
            });
            ForEachBox(b, len, [&](const std::string& mt, const unsigned char* mb, size_t ml) {
                if (mt != "minf") return;
                ForEachBox(mb, ml, [&](const std::string& it, const unsigned char* ib, size_t il) {
                    if (it == "stbl") ParseStbl(ib, il, t);
                });
            });
        }
    });
}

// A text tag: iTunes-style ("data" box) or QuickTime-style (size, language, text).
std::string Mp4TagText(const unsigned char* b, size_t len) {
    std::string text;
    bool iTunes = false;
    ForEachBox(b, len, [&](const std::string& type, const unsigned char* d, size_t dl) {
        if (type == "data" && dl >= 8 && U32BE(d) == 1) {   // type 1: UTF-8
            text.assign(reinterpret_cast<const char*>(d + 8), dl - 8);
            iTunes = true;
        }
    });
    if (!iTunes && len >= 4) {
        const size_t n = std::min<size_t>(U16BE(b), len - 4);
        text.assign(reinterpret_cast<const char*>(b + 4), n);
    }
    while (!text.empty() && (text.back() == '\0' || text.back() == ' ')) text.pop_back();
    return text;
}

void ParseMp4Tags(const unsigned char* p, size_t n, VideoFileInfo& out, int depth);

// A meta box's body: an ISO FullBox (version / flags first) - QuickTime omits them.
void ParseMp4Meta(const unsigned char* b, size_t len, VideoFileInfo& out, int depth) {
    const size_t skip = (len >= 8 && U32BE(b) == 0 && FourCC(b + 4) != "hdlr") ? 4 : 0;
    ParseMp4Tags(b + skip, len - skip, out, depth + 1);
}

void ParseMp4Tags(const unsigned char* p, size_t n, VideoFileInfo& out, int depth) {
    if (depth > 4) return;
    ForEachBox(p, n, [&](const std::string& type, const unsigned char* b, size_t len) {
        if (type == "meta") {
            ParseMp4Meta(b, len, out, depth);
        } else if (type == "ilst") {
            ParseMp4Tags(b, len, out, depth + 1);
        } else if (type == "\xA9nam") {
            if (out.title.empty()) out.title = Mp4TagText(b, len);
        } else if (type == "\xA9too" || type == "\xA9swr" || type == "\xA9" "enc") {
            if (out.encoder.empty()) out.encoder = Mp4TagText(b, len);
        }
    });
}

// A fragmented file (DASH, OBS, a camera's crash-safe recording) keeps its
// samples in moof boxes, each a few hundred bytes, not in the moov's tables:
// add up their run lengths, durations and sizes per track.
void ReadMp4Fragments(FileBytes& file, std::vector<Mp4Track>& tracks) {
    auto trackFor = [&](uint32_t id) -> Mp4Track* {
        for (Mp4Track& t : tracks) if (t.trackId == id) return &t;
        return nullptr;
    };
    uint64_t pos = 0;
    for (int i = 0; i < 200000 && pos + 8 <= file.Size(); ++i) {
        const Bytes bh = file.Read(pos, 16);
        if (bh.size() < 8) break;
        uint64_t size = U32BE(bh.data());
        uint64_t header = 8;
        if (size == 1 && bh.size() >= 16) { size = U64BE(bh.data() + 8); header = 16; }
        else if (size == 0) size = file.Size() - pos;
        if (size < header) break;
        if (std::memcmp(bh.data() + 4, "moof", 4) == 0 && size - header <= (4u << 20)) {
            const Bytes moof = file.Read(pos + header, size_t(size - header));
            ForEachBox(moof.data(), moof.size(), [&](const std::string& type, const unsigned char* b, size_t len) {
                if (type != "traf") return;
                Mp4Track* t = nullptr;
                uint32_t defDuration = 0, defSize = 0;
                ForEachBox(b, len, [&](const std::string& ft, const unsigned char* fb, size_t fl) {
                    if (ft == "tfhd" && fl >= 8) {
                        const uint32_t flags = U32BE(fb) & 0xFFFFFF;
                        t = trackFor(U32BE(fb + 4));
                        if (!t) return;
                        defDuration = t->trexDuration;
                        defSize = t->trexSize;
                        size_t at = 8;
                        if (flags & 0x01) at += 8;
                        if (flags & 0x02) at += 4;
                        if ((flags & 0x08) && at + 4 <= fl) { defDuration = U32BE(fb + at); at += 4; }
                        if ((flags & 0x10) && at + 4 <= fl) defSize = U32BE(fb + at);
                    } else if (ft == "trun" && t && fl >= 8) {
                        const uint32_t flags = U32BE(fb) & 0xFFFFFF;
                        const uint32_t count = U32BE(fb + 4);
                        size_t at = 8;
                        if (flags & 0x01) at += 4;
                        if (flags & 0x04) at += 4;
                        const size_t stride = ((flags & 0x100) ? 4 : 0) + ((flags & 0x200) ? 4 : 0) +
                                              ((flags & 0x400) ? 4 : 0) + ((flags & 0x800) ? 4 : 0);
                        bool sized = !(flags & 0x200) ? defSize != 0 : true;
                        for (uint32_t k = 0; k < count; ++k) {
                            uint32_t duration = defDuration, sampleSize = defSize;
                            if (stride) {
                                if (at + stride > fl) { sized = false; break; }
                                size_t f = at;
                                if (flags & 0x100) { duration = U32BE(fb + f); f += 4; }
                                if (flags & 0x200) sampleSize = U32BE(fb + f);
                                at += stride;
                            }
                            t->sttsDelta += duration;
                            t->sampleBytes += sampleSize;
                        }
                        t->sttsSamples += count;
                        t->sampleCount += count;
                        if (sized) t->sizesKnown = true;
                    }
                });
            });
        }
        pos += size;
    }
    // The fragments' total is the track's length.
    for (Mp4Track& t : tracks)
        if (t.sttsDelta && t.mediaDuration < t.sttsDelta) t.mediaDuration = t.sttsDelta;
}

bool ReadMp4(FileBytes& file, VideoFileInfo& out) {
    const Bytes head = file.Read(0, 16);
    if (head.size() < 12) return false;
    const std::string first = FourCC(head.data() + 4);
    if (first != "ftyp" && first != "moov" && first != "mdat" && first != "wide" &&
        first != "free" && first != "skip" && first != "pnot")
        return false;

    out.container = "MPEG-4";
    if (first == "ftyp") {
        out.brand = FourCC(head.data() + 8);
        if (out.brand == "qt  ") out.container = "QuickTime";
        else if (out.brand.rfind("3gp", 0) == 0) out.container = "3GPP";
        else if (out.brand.rfind("3g2", 0) == 0) out.container = "3GPP2";
        while (!out.brand.empty() && out.brand.back() == ' ') out.brand.pop_back();
    } else {
        out.container = "QuickTime";   // no ftyp: an old QuickTime movie
    }

    // Find the moov box among the top-level boxes - at the start for a file
    // made for streaming, after mdat for most cameras.
    Bytes moov;
    uint64_t pos = 0;
    for (int i = 0; i < 256 && pos + 8 <= file.Size(); ++i) {
        const Bytes bh = file.Read(pos, 16);
        if (bh.size() < 8) break;
        uint64_t size = U32BE(bh.data());
        uint64_t header = 8;
        if (size == 1 && bh.size() >= 16) { size = U64BE(bh.data() + 8); header = 16; }
        else if (size == 0) size = file.Size() - pos;
        if (size < header) break;
        if (std::memcmp(bh.data() + 4, "moov", 4) == 0) {
            // Bounded: a two-hour film's sample tables run to tens of MB.
            moov = file.Read(pos + header, size_t(std::min<uint64_t>(size - header, 64ull << 20)));
            break;
        }
        pos += size;
    }
    if (moov.empty()) return true;   // the container, but no header to read

    std::vector<Mp4Track> tracks;
    uint32_t movieScale = 0;
    uint64_t movieDuration = 0, fragmentDuration = 0;
    bool fragmented = false;
    struct Trex { uint32_t trackId, duration, size; };
    std::vector<Trex> trex;
    ForEachBox(moov.data(), moov.size(), [&](const std::string& type, const unsigned char* b, size_t len) {
        if (type == "mvhd" && len >= 20) {
            const bool v1 = b[0] == 1;
            if (v1 && len >= 32) {
                out.creationTime = FormatUnixTimeUtc(int64_t(U64BE(b + 4)) - 2082844800LL);
                movieScale = U32BE(b + 20);
                movieDuration = U64BE(b + 24);
            } else if (!v1) {
                out.creationTime = FormatUnixTimeUtc(int64_t(U32BE(b + 4)) - 2082844800LL);
                movieScale = U32BE(b + 12);
                movieDuration = U32BE(b + 16);
            }
        } else if (type == "trak") {
            tracks.emplace_back();
            ParseTrak(b, len, tracks.back());
        } else if (type == "mvex") {
            fragmented = true;
            ForEachBox(b, len, [&](const std::string& mt, const unsigned char* mb, size_t ml) {
                if (mt == "mehd" && ml >= 8)
                    fragmentDuration = (mb[0] == 1 && ml >= 12) ? U64BE(mb + 4) : U32BE(mb + 4);
                else if (mt == "trex" && ml >= 24)
                    trex.push_back({U32BE(mb + 4), U32BE(mb + 12), U32BE(mb + 16)});
            });
        } else if (type == "udta") {
            ParseMp4Tags(b, len, out, 0);
        } else if (type == "meta") {
            ParseMp4Meta(b, len, out, 0);
        }
    });

    if (fragmented) {
        for (Mp4Track& t : tracks)
            for (const Trex& x : trex)
                if (x.trackId == t.trackId) { t.trexDuration = x.duration; t.trexSize = x.size; }
        ReadMp4Fragments(file, tracks);
    }

    if (movieScale) {
        const uint64_t d = movieDuration && movieDuration != 0xFFFFFFFFull && movieDuration != ~0ull
                         ? movieDuration : fragmentDuration;
        if (d) out.duration = double(d) / movieScale;
    }

    double longestTrack = 0;
    for (const Mp4Track& t : tracks) {
        const double seconds = t.timescale && t.mediaDuration ? double(t.mediaDuration) / t.timescale : 0.0;
        const int64_t measured = t.sizesKnown && seconds > 0 ? int64_t(double(t.sampleBytes) * 8.0 / seconds) : 0;
        if (t.handler == "vide" || t.handler == "soun") longestTrack = std::max(longestTrack, seconds);
        if (t.handler == "vide") {
            ++out.videoTracks;
            if (out.hasVideo) continue;
            out.hasVideo = true;
            out.videoCodec = t.codec.videoCodec;
            out.videoProfile = t.codec.videoProfile;
            out.bitDepth = t.codec.bitDepth;
            out.width = t.codec.width ? t.codec.width : t.tkhdWidth;
            out.height = t.codec.height ? t.codec.height : t.tkhdHeight;
            out.displayWidth = out.width;
            out.displayHeight = out.height;
            if (t.parH > 0 && t.parV > 0 && t.parH != t.parV)
                out.displayWidth = int(std::lround(double(out.width) * t.parH / t.parV));
            else if (t.tkhdWidth > 0 && t.tkhdHeight > 0)
                { out.displayWidth = t.tkhdWidth; out.displayHeight = t.tkhdHeight; }
            out.rotation = t.rotation;
            const uint64_t frames = t.sttsSamples ? t.sttsSamples : t.sampleCount;
            out.frameCount = int64_t(frames);
            if (t.sttsSamples && t.sttsDelta && t.timescale)
                out.frameRate = double(t.sttsSamples) * t.timescale / double(t.sttsDelta);
            out.videoBitRate = measured ? measured : t.declaredBitRate;
        } else if (t.handler == "soun") {
            ++out.audioTracks;
            if (out.hasAudio) continue;
            out.hasAudio = true;
            out.audioCodec = t.codec.audioCodec;
            out.sampleRate = t.codec.sampleRate;
            out.channels = t.codec.channels;
            out.bitsPerSample = t.codec.bitsPerSample;
            out.audioBitRate = measured ? measured : t.declaredBitRate;
            out.audioLanguage = t.language;
        } else if (t.handler == "sbtl" || t.handler == "subt" || t.handler == "text" ||
                   t.handler == "clcp") {
            ++out.subtitleTracks;
        }
    }
    if (out.duration <= 0) out.duration = longestTrack;
    return true;
}

// =====================================================================
// Matroska / WebM
// =====================================================================

namespace Ebml {
constexpr uint64_t Header = 0x1A45DFA3, DocType = 0x4282;
constexpr uint64_t Segment = 0x18538067, SeekHead = 0x114D9B74, Seek = 0x4DBB,
                   SeekID = 0x53AB, SeekPosition = 0x53AC;
constexpr uint64_t Info = 0x1549A966, TimestampScale = 0x2AD7B1, Duration = 0x4489,
                   Title = 0x7BA9, MuxingApp = 0x4D80, WritingApp = 0x5741, DateUTC = 0x4461;
constexpr uint64_t Tracks = 0x1654AE6B, TrackEntry = 0xAE, TrackUID = 0x73C5, TrackType = 0x83,
                   CodecID = 0x86, CodecPrivate = 0x63A2, DefaultDuration = 0x23E383,
                   Language = 0x22B59C, LanguageBCP47 = 0x22B59D;
constexpr uint64_t Video = 0xE0, PixelWidth = 0xB0, PixelHeight = 0xBA, DisplayWidth = 0x54B0,
                   DisplayHeight = 0x54BA, DisplayUnit = 0x54B2, Colour = 0x55B0,
                   BitsPerChannel = 0x55B2, ProjectionPoseRoll = 0x7675, Projection = 0x7670;
constexpr uint64_t Audio = 0xE1, SamplingFrequency = 0xB5, OutputSamplingFrequency = 0x78B5,
                   Channels = 0x9F, BitDepth = 0x6264;
constexpr uint64_t Cluster = 0x1F43B675, Tags = 0x1254C367, Tag = 0x7373, Targets = 0x63C0,
                   TagTrackUID = 0x63C5, SimpleTag = 0x67C8, TagName = 0x45A3, TagString = 0x4487;
} // namespace Ebml

struct MkvTrack {
    uint64_t uid = 0;
    int type = 0;                   // 1 video, 2 audio, 17 subtitle
    std::string codecId, language;
    Bytes codecPrivate;
    uint64_t defaultDurationNs = 0;
    int pixelWidth = 0, pixelHeight = 0, displayWidth = 0, displayHeight = 0, displayUnit = 0;
    int bitsPerChannel = 0, rotation = 0;
    double samplingFrequency = 0, outputSamplingFrequency = 0;
    int channels = 0, bitDepth = 0;
    int64_t tagBitRate = 0, tagFrames = 0;
};

class MkvReader {
public:
    MkvReader(FileBytes& f, VideoFileInfo& o) : file(f), out(o) {}

    bool Read() {
        uint64_t pos = 0, id = 0, size = 0;
        bool unknown = false;
        if (!ReadElementHeader(pos, file.Size(), id, size, unknown) || id != Ebml::Header) return false;
        const uint64_t headerEnd = std::min(file.Size(), pos + size);
        std::string docType = "matroska";
        Walk(pos, headerEnd, 0, [&](uint64_t eid, uint64_t at, uint64_t len) {
            if (eid == Ebml::DocType) docType = ReadString(at, len);
        });
        out.container = docType == "webm" ? "WebM" : "Matroska";

        pos = headerEnd;
        if (!ReadElementHeader(pos, file.Size(), id, size, unknown) || id != Ebml::Segment) return true;
        segmentData = pos;
        const uint64_t segmentEnd = unknown ? file.Size() : std::min(file.Size(), pos + size);
        ReadLevel1(segmentData, segmentEnd);
        // Elements the SeekHead points to that came after the first Cluster.
        for (size_t i = 0; i < seekTargets.size() && i < 16; ++i) {
            const uint64_t at = seekTargets[i];
            if (std::find(visited.begin(), visited.end(), at) != visited.end()) continue;
            ReadLevel1(at, segmentEnd, true);
        }
        Finish();
        return true;
    }

private:
    FileBytes& file;
    VideoFileInfo& out;
    uint64_t segmentData = 0;
    uint64_t timestampScale = 1000000;
    double durationTicks = 0;
    std::vector<MkvTrack> tracks;
    std::vector<uint64_t> seekTargets, visited;

    bool ReadVarInt(uint64_t& pos, uint64_t end, bool keepMarker, uint64_t& value, bool& unknown) {
        unknown = false;
        if (pos >= end) return false;
        const Bytes b = file.Read(pos, 8);
        if (b.empty() || b[0] == 0) return false;
        int len = 1;
        while (len <= 8 && !(b[0] & (0x80 >> (len - 1)))) ++len;
        if (len > 8 || size_t(len) > b.size()) return false;
        value = keepMarker ? b[0] : uint64_t(b[0] & (0xFF >> len));
        for (int i = 1; i < len; ++i) value = (value << 8) | b[i];
        if (!keepMarker) {
            const uint64_t allOnes = (len == 8) ? 0x00FFFFFFFFFFFFFFull : ((1ull << (7 * len)) - 1);
            unknown = value == allOnes;
        }
        pos += uint64_t(len);
        return true;
    }

    bool ReadElementHeader(uint64_t& pos, uint64_t end, uint64_t& id, uint64_t& size, bool& unknown) {
        bool idUnknown = false;
        return ReadVarInt(pos, end, true, id, idUnknown) && ReadVarInt(pos, end, false, size, unknown);
    }

    // Calls fn(id, dataPos, dataSize) for each child in [pos, end); stops at a Cluster.
    template <typename Fn>
    void Walk(uint64_t pos, uint64_t end, int depth, Fn&& fn) {
        for (int guard = 0; pos < end && guard < 4096; ++guard) {
            uint64_t id = 0, size = 0;
            bool unknown = false;
            if (!ReadElementHeader(pos, end, id, size, unknown)) return;
            if (id == Ebml::Cluster) return;
            const uint64_t len = unknown ? end - pos : std::min(size, end - pos);
            fn(id, pos, len);
            if (unknown) return;
            pos += len;
        }
        (void)depth;
    }

    uint64_t ReadUInt(uint64_t at, uint64_t len) {
        const Bytes d = file.Read(at, size_t(std::min<uint64_t>(len, 8)));
        uint64_t v = 0;
        for (unsigned char c : d) v = (v << 8) | c;
        return v;
    }
    double ReadFloat(uint64_t at, uint64_t len) {
        const Bytes d = file.Read(at, size_t(std::min<uint64_t>(len, 8)));
        if (d.size() == 4) { uint32_t u = U32BE(d.data()); float f; std::memcpy(&f, &u, 4); return f; }
        if (d.size() == 8) { uint64_t u = U64BE(d.data()); double v; std::memcpy(&v, &u, 8); return v; }
        return 0;
    }
    std::string ReadString(uint64_t at, uint64_t len) {
        const Bytes d = file.Read(at, size_t(std::min<uint64_t>(len, 4096)));
        std::string s(d.begin(), d.end());
        while (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }

    void ReadLevel1(uint64_t pos, uint64_t end, bool single = false) {
        for (int guard = 0; pos < end && guard < 1024; ++guard) {
            const uint64_t start = pos;
            uint64_t id = 0, size = 0;
            bool unknown = false;
            if (!ReadElementHeader(pos, end, id, size, unknown)) return;
            if (id == Ebml::Cluster && !single) return;
            const uint64_t len = unknown ? end - pos : std::min(size, end - pos);
            visited.push_back(start);
            if (id == Ebml::SeekHead) ReadSeekHead(pos, pos + len);
            else if (id == Ebml::Info) ReadInfo(pos, pos + len);
            else if (id == Ebml::Tracks) ReadTracks(pos, pos + len);
            else if (id == Ebml::Tags) ReadTags(pos, pos + len);
            if (single || unknown) return;
            pos += len;
        }
    }

    void ReadSeekHead(uint64_t pos, uint64_t end) {
        Walk(pos, end, 1, [&](uint64_t id, uint64_t at, uint64_t len) {
            if (id != Ebml::Seek) return;
            uint64_t target = 0, position = 0;
            bool havePosition = false;
            Walk(at, at + len, 2, [&](uint64_t sid, uint64_t sat, uint64_t slen) {
                if (sid == Ebml::SeekID) target = ReadUInt(sat, slen);
                else if (sid == Ebml::SeekPosition) { position = ReadUInt(sat, slen); havePosition = true; }
            });
            if (havePosition && (target == Ebml::Info || target == Ebml::Tracks || target == Ebml::Tags))
                seekTargets.push_back(segmentData + position);
        });
    }

    void ReadInfo(uint64_t pos, uint64_t end) {
        Walk(pos, end, 1, [&](uint64_t id, uint64_t at, uint64_t len) {
            if (id == Ebml::TimestampScale) { const uint64_t v = ReadUInt(at, len); if (v) timestampScale = v; }
            else if (id == Ebml::Duration) durationTicks = ReadFloat(at, len);
            else if (id == Ebml::Title) out.title = ReadString(at, len);
            else if (id == Ebml::WritingApp) out.encoder = ReadString(at, len);
            else if (id == Ebml::MuxingApp && out.encoder.empty()) out.encoder = ReadString(at, len);
            else if (id == Ebml::DateUTC && len == 8) {
                const int64_t ns = int64_t(ReadUInt(at, len));
                out.creationTime = FormatUnixTimeUtc(ns / 1000000000LL + 978307200LL);
            }
        });
    }

    void ReadTracks(uint64_t pos, uint64_t end) {
        Walk(pos, end, 1, [&](uint64_t id, uint64_t at, uint64_t len) {
            if (id != Ebml::TrackEntry || tracks.size() >= 64) return;
            MkvTrack t;
            Walk(at, at + len, 2, [&](uint64_t tid, uint64_t tat, uint64_t tlen) {
                switch (tid) {
                    case Ebml::TrackUID: t.uid = ReadUInt(tat, tlen); break;
                    case Ebml::TrackType: t.type = int(ReadUInt(tat, tlen)); break;
                    case Ebml::CodecID: t.codecId = ReadString(tat, tlen); break;
                    case Ebml::CodecPrivate:
                        if (tlen <= 65536) t.codecPrivate = file.Read(tat, size_t(tlen));
                        break;
                    case Ebml::DefaultDuration: t.defaultDurationNs = ReadUInt(tat, tlen); break;
                    case Ebml::Language: if (t.language.empty()) t.language = ReadString(tat, tlen); break;
                    case Ebml::LanguageBCP47: t.language = ReadString(tat, tlen); break;
                    case Ebml::Video:
                        Walk(tat, tat + tlen, 3, [&](uint64_t vid, uint64_t vat, uint64_t vlen) {
                            if (vid == Ebml::PixelWidth) t.pixelWidth = int(ReadUInt(vat, vlen));
                            else if (vid == Ebml::PixelHeight) t.pixelHeight = int(ReadUInt(vat, vlen));
                            else if (vid == Ebml::DisplayWidth) t.displayWidth = int(ReadUInt(vat, vlen));
                            else if (vid == Ebml::DisplayHeight) t.displayHeight = int(ReadUInt(vat, vlen));
                            else if (vid == Ebml::DisplayUnit) t.displayUnit = int(ReadUInt(vat, vlen));
                            else if (vid == Ebml::Colour)
                                Walk(vat, vat + vlen, 4, [&](uint64_t cid, uint64_t cat, uint64_t clen) {
                                    if (cid == Ebml::BitsPerChannel) t.bitsPerChannel = int(ReadUInt(cat, clen));
                                });
                            else if (vid == Ebml::Projection)
                                Walk(vat, vat + vlen, 4, [&](uint64_t pid, uint64_t pat, uint64_t plen) {
                                    if (pid != Ebml::ProjectionPoseRoll) return;
                                    const int roll = int(std::lround(ReadFloat(pat, plen)));
                                    t.rotation = ((-roll % 360) + 360) % 360;
                                });
                        });
                        break;
                    case Ebml::Audio:
                        Walk(tat, tat + tlen, 3, [&](uint64_t aid, uint64_t aat, uint64_t alen) {
                            if (aid == Ebml::SamplingFrequency) t.samplingFrequency = ReadFloat(aat, alen);
                            else if (aid == Ebml::OutputSamplingFrequency) t.outputSamplingFrequency = ReadFloat(aat, alen);
                            else if (aid == Ebml::Channels) t.channels = int(ReadUInt(aat, alen));
                            else if (aid == Ebml::BitDepth) t.bitDepth = int(ReadUInt(aat, alen));
                        });
                        break;
                    default: break;
                }
            });
            tracks.push_back(std::move(t));
        });
    }

    // mkvmerge's statistics tags: BPS and NUMBER_OF_FRAMES per track.
    void ReadTags(uint64_t pos, uint64_t end) {
        Walk(pos, end, 1, [&](uint64_t id, uint64_t at, uint64_t len) {
            if (id != Ebml::Tag) return;
            uint64_t trackUid = 0;
            std::vector<std::pair<std::string, std::string>> simple;
            Walk(at, at + len, 2, [&](uint64_t gid, uint64_t gat, uint64_t glen) {
                if (gid == Ebml::Targets) {
                    Walk(gat, gat + glen, 3, [&](uint64_t tid, uint64_t tat, uint64_t tlen) {
                        if (tid == Ebml::TagTrackUID) trackUid = ReadUInt(tat, tlen);
                    });
                } else if (gid == Ebml::SimpleTag) {
                    std::string name, value;
                    Walk(gat, gat + glen, 3, [&](uint64_t sid, uint64_t sat, uint64_t slen) {
                        if (sid == Ebml::TagName) name = ReadString(sat, slen);
                        else if (sid == Ebml::TagString) value = ReadString(sat, slen);
                    });
                    if (!name.empty()) simple.emplace_back(name, value);
                }
            });
            auto number = [](const std::string& s) -> int64_t {
                int64_t v = 0;
                for (char c : s) { if (c < '0' || c > '9') return 0; v = v * 10 + (c - '0'); }
                return v;
            };
            for (const auto& [name, value] : simple) {
                if (!trackUid) {
                    if ((name == "TITLE") && out.title.empty()) out.title = value;
                    else if ((name == "ENCODER") && out.encoder.empty()) out.encoder = value;
                    continue;
                }
                for (MkvTrack& t : tracks) {
                    if (t.uid != trackUid) continue;
                    if (name == "BPS") t.tagBitRate = number(value);
                    else if (name == "NUMBER_OF_FRAMES") t.tagFrames = number(value);
                }
            }
        });
    }

    void Finish() {
        if (durationTicks > 0) out.duration = durationTicks * double(timestampScale) / 1e9;
        for (const MkvTrack& t : tracks) {
            if (t.type == 1) {
                ++out.videoTracks;
                if (out.hasVideo) continue;
                out.hasVideo = true;
                const std::string& id = t.codecId;
                const unsigned char* cp = t.codecPrivate.data();
                const size_t cn = t.codecPrivate.size();
                if (id == "V_MPEG4/ISO/AVC") { out.videoCodec = "H.264"; ParseAvcC(cp, cn, out); }
                else if (id == "V_MPEGH/ISO/HEVC") { out.videoCodec = "H.265"; ParseHvcC(cp, cn, out); }
                else if (id == "V_AV1") { out.videoCodec = "AV1"; ParseAv1C(cp, cn, out); }
                else if (id == "V_VP8") out.videoCodec = "VP8";
                else if (id == "V_VP9") out.videoCodec = "VP9";
                else if (id == "V_THEORA") out.videoCodec = "Theora";
                else if (id == "V_MPEG2") out.videoCodec = "MPEG-2";
                else if (id == "V_MPEG1") out.videoCodec = "MPEG-1";
                else if (id.rfind("V_MPEG4/ISO", 0) == 0) out.videoCodec = "MPEG-4 Visual";
                else if (id == "V_MJPEG") out.videoCodec = "Motion JPEG";
                else if (id == "V_PRORES") out.videoCodec = "ProRes";
                else if (id == "V_FFV1") out.videoCodec = "FFV1";
                else if (id == "V_MS/VFW/FOURCC" && cn >= 20) out.videoCodec = VideoFourCCName(FourCC(cp + 16), true);
                else out.videoCodec = id.size() > 2 && id[1] == '_' ? id.substr(2) : id;
                out.width = t.pixelWidth;
                out.height = t.pixelHeight;
                out.displayWidth = t.pixelWidth;
                out.displayHeight = t.pixelHeight;
                if (t.displayWidth > 0 && t.displayHeight > 0 && t.pixelHeight > 0) {
                    if (t.displayUnit == 0) {
                        out.displayWidth = t.displayWidth;
                        out.displayHeight = t.displayHeight;
                    } else {   // an aspect ratio, not pixels: keep the height
                        out.displayWidth = int(std::lround(double(t.pixelHeight) * t.displayWidth / t.displayHeight));
                    }
                }
                if (t.bitsPerChannel > 0) out.bitDepth = t.bitsPerChannel;
                out.rotation = t.rotation;
                if (t.defaultDurationNs) out.frameRate = 1e9 / double(t.defaultDurationNs);
                if (t.tagFrames) out.frameCount = t.tagFrames;
                else if (out.frameRate > 0 && durationTicks > 0)
                    out.frameCount = int64_t(std::llround(out.frameRate * durationTicks * double(timestampScale) / 1e9));
                out.videoBitRate = t.tagBitRate;
            } else if (t.type == 2) {
                ++out.audioTracks;
                if (out.hasAudio) continue;
                out.hasAudio = true;
                const std::string& id = t.codecId;
                if (id.rfind("A_AAC", 0) == 0) {
                    out.audioCodec = "AAC";
                    ParseAudioSpecificConfig(t.codecPrivate.data(), t.codecPrivate.size(), out);
                }
                else if (id == "A_OPUS") out.audioCodec = "Opus";
                else if (id == "A_VORBIS") out.audioCodec = "Vorbis";
                else if (id == "A_FLAC") out.audioCodec = "FLAC";
                else if (id == "A_MPEG/L3") out.audioCodec = "MP3";
                else if (id == "A_MPEG/L2") out.audioCodec = "MP2";
                else if (id == "A_AC3") out.audioCodec = "AC-3";
                else if (id == "A_EAC3") out.audioCodec = "E-AC-3";
                else if (id == "A_DTS") out.audioCodec = "DTS";
                else if (id == "A_TRUEHD") out.audioCodec = "TrueHD";
                else if (id == "A_ALAC") out.audioCodec = "ALAC";
                else if (id.rfind("A_PCM/FLOAT", 0) == 0) out.audioCodec = "PCM (float)";
                else if (id.rfind("A_PCM", 0) == 0) out.audioCodec = "PCM";
                else out.audioCodec = id.size() > 2 && id[1] == '_' ? id.substr(2) : id;
                const double rate = t.outputSamplingFrequency > 0 ? t.outputSamplingFrequency : t.samplingFrequency;
                if (!out.sampleRate && rate > 0) out.sampleRate = int(std::lround(rate));
                if (id == "A_OPUS") out.sampleRate = 48000;
                if (t.channels > 0) out.channels = t.channels;
                if (out.audioCodec.rfind("PCM", 0) == 0 || id == "A_FLAC" || id == "A_ALAC")
                    out.bitsPerSample = t.bitDepth;
                out.audioBitRate = t.tagBitRate;
                if (t.language != "und") out.audioLanguage = t.language;
            } else if (t.type == 17) {
                ++out.subtitleTracks;
            }
        }
    }
};

// =====================================================================
// AVI
// =====================================================================

bool ReadAvi(FileBytes& file, VideoFileInfo& out) {
    const Bytes head = file.Read(0, 12);
    if (head.size() < 12 || std::memcmp(head.data(), "RIFF", 4) != 0 ||
        std::memcmp(head.data() + 8, "AVI ", 4) != 0)
        return false;
    out.container = "AVI";

    uint32_t usPerFrame = 0, totalFrames = 0, odmlFrames = 0;
    int headerWidth = 0, headerHeight = 0;
    double videoSeconds = 0;

    // hdrl > avih, strl > strh / strf, odml > dmlh. Pass the stream's type
    // from its strh to the strf after it.
    std::string streamType;
    auto parseChunks = [&](auto&& self, const unsigned char* p, size_t n, int depth) -> void {
        if (depth > 4) return;
        size_t pos = 0;
        for (int guard = 0; pos + 8 <= n && guard < 1024; ++guard) {
            const unsigned char* c = p + pos;
            const uint32_t size = U32LE(c + 4);
            if (size > n - pos - 8) break;
            const unsigned char* body = c + 8;
            if (std::memcmp(c, "LIST", 4) == 0 && size >= 4) {
                if (std::memcmp(body, "strl", 4) == 0) streamType.clear();
                self(self, body + 4, size - 4, depth + 1);
            } else if (std::memcmp(c, "avih", 4) == 0 && size >= 40) {
                usPerFrame = U32LE(body);
                totalFrames = U32LE(body + 16);
                headerWidth = int(U32LE(body + 32));
                headerHeight = int(U32LE(body + 36));
            } else if (std::memcmp(c, "dmlh", 4) == 0 && size >= 4) {
                odmlFrames = U32LE(body);
            } else if (std::memcmp(c, "strh", 4) == 0 && size >= 36) {
                streamType = FourCC(body);
                const uint32_t scale = U32LE(body + 20), rate = U32LE(body + 24);
                const uint32_t length = U32LE(body + 32);
                if (streamType == "vids") {
                    ++out.videoTracks;
                    if (!out.hasVideo) {
                        out.hasVideo = true;
                        out.videoCodec = VideoFourCCName(FourCC(body + 4), true);
                        if (scale && rate) {
                            out.frameRate = double(rate) / scale;
                            if (length) videoSeconds = double(length) * scale / rate;
                        }
                    } else {
                        streamType = "vids2";   // a later video stream: its strf is not ours
                    }
                } else if (streamType == "auds") {
                    ++out.audioTracks;
                    if (out.hasAudio) streamType = "auds2";
                } else if (streamType == "txts") {
                    ++out.subtitleTracks;
                }
            } else if (std::memcmp(c, "strf", 4) == 0) {
                if (streamType == "vids" && size >= 20) {
                    out.width = int(int32_t(U32LE(body + 4)));
                    out.height = std::abs(int(int32_t(U32LE(body + 8))));
                    const std::string compression = FourCC(body + 16);
                    if (out.videoCodec.empty() || out.videoCodec == "Uncompressed")
                        out.videoCodec = VideoFourCCName(compression, true);
                } else if (streamType == "auds" && size >= 16) {
                    out.hasAudio = true;
                    out.audioCodec = AviAudioFormatName(U16LE(body));
                    out.channels = U16LE(body + 2);
                    out.sampleRate = int(U32LE(body + 4));
                    out.audioBitRate = int64_t(U32LE(body + 8)) * 8;
                    const int bits = U16LE(body + 14);
                    if (out.audioCodec.rfind("PCM", 0) == 0) out.bitsPerSample = bits;
                }
            } else if (std::memcmp(c, "INAM", 4) == 0 && out.title.empty()) {
                out.title.assign(reinterpret_cast<const char*>(body), size);
            } else if (std::memcmp(c, "ISFT", 4) == 0 && out.encoder.empty()) {
                out.encoder.assign(reinterpret_cast<const char*>(body), size);
            }
            pos += 8 + size_t(size) + (size & 1);
        }
    };

    // Top-level chunks after "RIFF....AVI ": read hdrl and INFO, skip movi.
    uint64_t pos = 12;
    const uint64_t riffEnd = std::min<uint64_t>(file.Size(), 8 + uint64_t(U32LE(head.data() + 4)));
    for (int i = 0; i < 64 && pos + 12 <= riffEnd; ++i) {
        const Bytes h = file.Read(pos, 12);
        if (h.size() < 12) break;
        const uint32_t size = U32LE(h.data() + 4);
        if (std::memcmp(h.data(), "LIST", 4) == 0 && size >= 4 &&
            (std::memcmp(h.data() + 8, "hdrl", 4) == 0 || std::memcmp(h.data() + 8, "INFO", 4) == 0)) {
            const Bytes list = file.Read(pos + 12, size_t(std::min<uint32_t>(size - 4, 8u << 20)));
            parseChunks(parseChunks, list.data(), list.size(), 0);
        }
        pos += 8 + uint64_t(size) + (size & 1);
    }

    for (std::string* s : {&out.title, &out.encoder})
        while (!s->empty() && (s->back() == '\0' || s->back() == ' ')) s->pop_back();
    if (!out.width && headerWidth) { out.width = headerWidth; out.height = headerHeight; }
    out.displayWidth = out.width;
    out.displayHeight = out.height;
    const uint32_t frames = odmlFrames > totalFrames ? odmlFrames : totalFrames;
    if (out.hasVideo) out.frameCount = frames;
    if (out.frameRate <= 0 && usPerFrame) out.frameRate = 1e6 / usPerFrame;
    if (videoSeconds > 0) out.duration = videoSeconds;
    else if (usPerFrame && frames) out.duration = double(usPerFrame) * frames / 1e6;
    if (out.hasVideo && frames && out.frameRate > 0 && videoSeconds <= 0)
        out.duration = double(frames) / out.frameRate;
    return true;
}

} // namespace

bool ReadVideoFileInfo(const std::string& utf8Path, VideoFileInfo& out) {
    out = VideoFileInfo{};
    FileBytes file(utf8Path);
    if (!file.IsOpen() || file.Size() < 12) return false;

    bool read = false;
    const Bytes head = file.Read(0, 4);
    if (head.size() == 4 && U32BE(head.data()) == Ebml::Header) {
        MkvReader reader(file, out);
        read = reader.Read();
    } else if (head.size() == 4 && std::memcmp(head.data(), "RIFF", 4) == 0) {
        read = ReadAvi(file, out);
    } else {
        read = ReadMp4(file, out);
    }
    if (!read) { out = VideoFileInfo{}; return false; }

    if (out.duration > 0) out.overallBitRate = int64_t(double(file.Size()) * 8.0 / out.duration);
    if (out.hasVideo && (out.displayWidth <= 0 || out.displayHeight <= 0)) {
        out.displayWidth = out.width;
        out.displayHeight = out.height;
    }
    return true;
}

std::string DescribeAudioChannels(int channels) {
    switch (channels) {
        case 0: return {};
        case 1: return "Mono";
        case 2: return "Stereo";
        case 3: return "2.1";
        case 6: return "5.1";
        case 8: return "7.1";
        default: return std::to_string(channels) + " channels";
    }
}

std::string FormatBitRate(int64_t bitsPerSecond) {
    if (bitsPerSecond <= 0) return {};
    if (bitsPerSecond >= 1000000)
        return FormatClassic(double(bitsPerSecond) / 1e6, bitsPerSecond >= 100000000 ? 0 : 2) + " Mb/s";
    if (bitsPerSecond >= 1000) return std::to_string((bitsPerSecond + 500) / 1000) + " kb/s";
    return std::to_string(bitsPerSecond) + " b/s";
}

std::string FormatFrameRate(double fps) {
    if (!(fps > 0)) return {};
    return FormatClassic(fps, 3);
}

std::string FormatSampleRate(int hertz) {
    if (hertz <= 0) return {};
    return FormatClassic(hertz / 1000.0, 3) + " kHz";
}

std::string DescribeAspectRatio(int width, int height) {
    if (width <= 0 || height <= 0) return {};
    const double ratio = double(width) / height;
    struct Known { int w, h; };
    static const Known known[] = {{16, 9}, {4, 3}, {3, 2}, {5, 4}, {16, 10}, {1, 1},
                                  {21, 9}, {9, 16}, {3, 4}, {2, 3}, {4, 5}, {10, 16}};
    for (const Known& k : known)
        if (std::fabs(ratio - double(k.w) / k.h) < 0.01) return std::to_string(k.w) + ":" + std::to_string(k.h);
    return FormatClassic(ratio, 2) + ":1";
}

std::string FormatMediaDuration(double seconds, bool withMillis) {
    if (!(seconds > 0)) return {};
    int64_t ms = int64_t(std::llround(seconds * 1000.0));
    if (!withMillis) ms = (ms + 500) / 1000 * 1000;
    const int64_t total = ms / 1000;
    const int64_t h = total / 3600, m = total / 60 % 60, s = total % 60;
    char buf[48];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", (long long)h, (long long)m, (long long)s);
    else       std::snprintf(buf, sizeof(buf), "%lld:%02lld", (long long)m, (long long)s);
    std::string text = buf;
    if (withMillis) {
        std::snprintf(buf, sizeof(buf), ".%03lld", (long long)(ms % 1000));
        text += buf;
    }
    return text;
}

} // namespace UltraCanvas
