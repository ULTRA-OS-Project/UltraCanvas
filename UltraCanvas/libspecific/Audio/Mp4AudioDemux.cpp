// libspecific/Audio/Mp4AudioDemux.cpp
// ISO base media file format audio demuxer (see Mp4AudioDemux.h). Pure byte
// pushing - no external dependency, so it compiles into every audio build and
// can be unit tested on its own.
// Version: 0.1.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "Mp4AudioDemux.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace UltraCanvas {
namespace AudioCodecs {

namespace {

// ===== BIG-ENDIAN READERS =====
// Every field in an ISO-BMFF box is big endian regardless of host byte order.
uint16_t RdU16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
uint32_t RdU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}
uint64_t RdU64(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32(p)) << 32) | RdU32(p + 4);
}
constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 8)  |
            static_cast<uint32_t>(static_cast<unsigned char>(d));
}
std::string FourCCToString(uint32_t t) {
    char s[5] = { static_cast<char>((t >> 24) & 0xFF), static_cast<char>((t >> 16) & 0xFF),
                  static_cast<char>((t >> 8) & 0xFF),  static_cast<char>(t & 0xFF), 0 };
    for (int i = 0; i < 4; ++i) {
        if (static_cast<unsigned char>(s[i]) < 0x20 || static_cast<unsigned char>(s[i]) > 0x7E) {
            s[i] = '?';
        }
    }
    return std::string(s);
}

// ===== BOX ITERATION =====
struct Box {
    uint32_t type = 0;
    const uint8_t* body = nullptr;
    uint64_t bodySize = 0;
};

// Walk the sibling boxes in [data, data+size). `fn` returns false to stop
// early. Malformed sizes (zero-length headers, sizes running past the parent)
// terminate the walk rather than looping or reading out of bounds.
template <class F>
void ForEachBox(const uint8_t* data, uint64_t size, F&& fn) {
    uint64_t pos = 0;
    while (pos + 8 <= size) {
        uint64_t boxSize = RdU32(data + pos);
        const uint32_t type = RdU32(data + pos + 4);
        uint64_t headerSize = 8;
        if (boxSize == 1) {                 // 64-bit largesize follows the type
            if (pos + 16 > size) return;
            boxSize = RdU64(data + pos + 8);
            headerSize = 16;
        } else if (boxSize == 0) {          // "extends to the end of the parent"
            boxSize = size - pos;
        }
        if (boxSize < headerSize || pos + boxSize > size) return;
        Box b;
        b.type = type;
        b.body = data + pos + headerSize;
        b.bodySize = boxSize - headerSize;
        if (!fn(b)) return;
        pos += boxSize;
    }
}

// Find the first child box of `type`. Returns false when absent.
bool FindBox(const uint8_t* data, uint64_t size, uint32_t type, Box& out) {
    bool found = false;
    ForEachBox(data, size, [&](const Box& b) {
        if (b.type != type) return true;
        out = b;
        found = true;
        return false;
    });
    return found;
}

// ===== MPEG-4 DESCRIPTORS (esds) =====

// Descriptor headers carry a tag byte plus a length encoded 7 bits at a time,
// high bit set on every byte but the last (at most four bytes).
bool ReadDescHeader(const uint8_t*& p, const uint8_t* end, uint8_t& tag, uint64_t& len) {
    if (p >= end) return false;
    tag = *p++;
    len = 0;
    for (int i = 0; i < 4; ++i) {
        if (p >= end) return false;
        const uint8_t b = *p++;
        len = (len << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return static_cast<uint64_t>(end - p) >= len;
}

// Collect the DecoderSpecificInfo (the AudioSpecificConfig for AAC) and the
// objectTypeIndication out of a descriptor chain, recursing through the
// ES_Descriptor / DecoderConfigDescriptor nesting.
void WalkDescriptors(const uint8_t* p, const uint8_t* end,
                     std::vector<uint8_t>& dsi, uint8_t& objectType, int depth) {
    if (depth > 8) return;
    while (p < end) {
        uint8_t tag = 0;
        uint64_t len = 0;
        if (!ReadDescHeader(p, end, tag, len)) return;
        const uint8_t* body = p;
        const uint8_t* bodyEnd = p + len;
        p = bodyEnd;

        if (tag == 0x03) {                          // ES_Descriptor
            if (bodyEnd - body < 3) continue;
            const uint8_t* q = body + 2;            // ES_ID
            const uint8_t flags = *q++;
            if (flags & 0x80) { if (bodyEnd - q < 2) continue; q += 2; }   // dependsOn
            if (flags & 0x40) {                                            // URL
                if (q >= bodyEnd) continue;
                const uint8_t urlLen = *q++;
                if (bodyEnd - q < urlLen) continue;
                q += urlLen;
            }
            if (flags & 0x20) { if (bodyEnd - q < 2) continue; q += 2; }   // OCR_ES_Id
            WalkDescriptors(q, bodyEnd, dsi, objectType, depth + 1);
        } else if (tag == 0x04) {                   // DecoderConfigDescriptor
            if (bodyEnd - body < 13) continue;
            objectType = body[0];
            WalkDescriptors(body + 13, bodyEnd, dsi, objectType, depth + 1);
        } else if (tag == 0x05) {                   // DecoderSpecificInfo
            dsi.assign(body, bodyEnd);
        }
    }
}

// esds box body: 4 bytes version/flags, then the descriptor chain.
bool ParseEsds(const uint8_t* body, uint64_t size,
               std::vector<uint8_t>& dsi, uint8_t& objectType) {
    if (size < 4) return false;
    WalkDescriptors(body + 4, body + size, dsi, objectType, 0);
    return !dsi.empty() || objectType != 0;
}

// ===== SAMPLE TABLE =====
struct StscEntry {
    uint32_t firstChunk = 0;        // 1-based
    uint32_t samplesPerChunk = 0;
};

struct SampleTable {
    uint32_t sampleCount = 0;
    uint32_t uniformSize = 0;             // non-zero => every sample this size
    std::vector<uint32_t> sizes;          // used when uniformSize == 0
    std::vector<uint64_t> chunkOffsets;
    std::vector<StscEntry> stsc;
    uint64_t mediaDuration = 0;           // in media timescale units, from stts
};

bool ParseStsz(const Box& b, SampleTable& t) {
    if (b.bodySize < 12) return false;
    t.uniformSize = RdU32(b.body + 4);
    t.sampleCount = RdU32(b.body + 8);
    if (t.uniformSize != 0) return true;
    if (b.bodySize < 12 + static_cast<uint64_t>(t.sampleCount) * 4) return false;
    t.sizes.resize(t.sampleCount);
    for (uint32_t i = 0; i < t.sampleCount; ++i) t.sizes[i] = RdU32(b.body + 12 + i * 4);
    return true;
}

// stz2 packs the sizes at 4, 8 or 16 bits each.
bool ParseStz2(const Box& b, SampleTable& t) {
    if (b.bodySize < 12) return false;
    const uint8_t fieldSize = b.body[7];
    t.sampleCount = RdU32(b.body + 8);
    t.uniformSize = 0;
    const uint8_t* p = b.body + 12;
    const uint64_t avail = b.bodySize - 12;
    t.sizes.resize(t.sampleCount);
    if (fieldSize == 16) {
        if (avail < static_cast<uint64_t>(t.sampleCount) * 2) return false;
        for (uint32_t i = 0; i < t.sampleCount; ++i) t.sizes[i] = RdU16(p + i * 2);
    } else if (fieldSize == 8) {
        if (avail < t.sampleCount) return false;
        for (uint32_t i = 0; i < t.sampleCount; ++i) t.sizes[i] = p[i];
    } else if (fieldSize == 4) {
        if (avail < (static_cast<uint64_t>(t.sampleCount) + 1) / 2) return false;
        for (uint32_t i = 0; i < t.sampleCount; ++i) {
            t.sizes[i] = (i & 1) ? (p[i / 2] & 0x0F) : (p[i / 2] >> 4);
        }
    } else {
        return false;
    }
    return true;
}

bool ParseStco(const Box& b, SampleTable& t, bool is64) {
    if (b.bodySize < 8) return false;
    const uint32_t count = RdU32(b.body + 4);
    const uint32_t width = is64 ? 8u : 4u;
    if (b.bodySize < 8 + static_cast<uint64_t>(count) * width) return false;
    t.chunkOffsets.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        t.chunkOffsets[i] = is64 ? RdU64(b.body + 8 + i * 8) : RdU32(b.body + 8 + i * 4);
    }
    return true;
}

bool ParseStsc(const Box& b, SampleTable& t) {
    if (b.bodySize < 8) return false;
    const uint32_t count = RdU32(b.body + 4);
    if (b.bodySize < 8 + static_cast<uint64_t>(count) * 12) return false;
    t.stsc.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        t.stsc[i].firstChunk      = RdU32(b.body + 8 + i * 12);
        t.stsc[i].samplesPerChunk = RdU32(b.body + 8 + i * 12 + 4);
    }
    return true;
}

void ParseStts(const Box& b, SampleTable& t) {
    if (b.bodySize < 8) return;
    const uint32_t count = RdU32(b.body + 4);
    if (b.bodySize < 8 + static_cast<uint64_t>(count) * 8) return;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t n     = RdU32(b.body + 8 + i * 8);
        const uint64_t delta = RdU32(b.body + 8 + i * 8 + 4);
        total += n * delta;
    }
    t.mediaDuration = total;
}

// Expand the chunk-oriented tables into a flat list of (offset, size) ranges.
// Any range that would run past the end of the file truncates the list instead
// of handing the caller a pointer it cannot read.
bool BuildSampleList(const SampleTable& t, uint64_t fileSize,
                     std::vector<Mp4AudioSample>& out) {
    if (t.sampleCount == 0 || t.chunkOffsets.empty() || t.stsc.empty()) return false;
    out.clear();
    out.reserve(t.sampleCount);

    size_t stscIndex = 0;
    uint32_t sampleIndex = 0;
    for (size_t chunk = 0; chunk < t.chunkOffsets.size() && sampleIndex < t.sampleCount; ++chunk) {
        // stsc runs are keyed on the first chunk they apply to; advance while
        // the next run starts at or before the chunk being filled.
        while (stscIndex + 1 < t.stsc.size() &&
               t.stsc[stscIndex + 1].firstChunk <= chunk + 1) {
            ++stscIndex;
        }
        const uint32_t perChunk = t.stsc[stscIndex].samplesPerChunk;
        uint64_t offset = t.chunkOffsets[chunk];
        for (uint32_t i = 0; i < perChunk && sampleIndex < t.sampleCount; ++i, ++sampleIndex) {
            const uint32_t size = t.uniformSize ? t.uniformSize : t.sizes[sampleIndex];
            if (size == 0) continue;
            if (offset > fileSize || size > fileSize - offset) return !out.empty();
            out.push_back({ offset, size });
            offset += size;
        }
    }
    return !out.empty();
}

// ===== SAMPLE ENTRY =====

Mp4AudioCodec CodecFromFourCC(uint32_t type) {
    switch (type) {
        case FourCC('m','p','4','a'): return Mp4AudioCodec::AAC;   // refined via esds
        case FourCC('a','l','a','c'): return Mp4AudioCodec::ALAC;
        case FourCC('.','m','p','3'): return Mp4AudioCodec::MP3;
        case FourCC('a','c','-','3'):
        case FourCC('e','c','-','3'): return Mp4AudioCodec::AC3;
        case FourCC('l','p','c','m'):
        case FourCC('s','o','w','t'):
        case FourCC('t','w','o','s'):
        case FourCC('r','a','w',' '):
        case FourCC('i','n','2','4'):
        case FourCC('i','n','3','2'):
        case FourCC('f','l','3','2'):
        case FourCC('f','l','6','4'): return Mp4AudioCodec::PCM;
        default:                      return Mp4AudioCodec::Unknown;
    }
}

// An AudioSampleEntry is a box header followed by a fixed preamble whose length
// depends on its version field, then codec-specific child boxes.
void ParseAudioSampleEntry(const Box& entry, Mp4AudioTrack& out) {
    out.codecName = FourCCToString(entry.type);
    out.codec = CodecFromFourCC(entry.type);
    if (entry.bodySize < 28) return;

    const uint16_t version = RdU16(entry.body + 8);
    out.channels   = RdU16(entry.body + 16);
    out.sampleRate = static_cast<int>(RdU32(entry.body + 24) >> 16);   // 16.16 fixed

    uint64_t childOffset = 28;
    if (version == 1)      childOffset += 16;
    else if (version == 2) childOffset += 36;
    if (childOffset >= entry.bodySize) return;

    const uint8_t* children = entry.body + childOffset;
    const uint64_t childSize = entry.bodySize - childOffset;

    ForEachBox(children, childSize, [&](const Box& c) {
        if (c.type == FourCC('e','s','d','s')) {
            std::vector<uint8_t> dsi;
            uint8_t oti = 0;
            if (ParseEsds(c.body, c.bodySize, dsi, oti)) {
                out.codecConfig = std::move(dsi);
                // objectTypeIndication tells AAC apart from the MP3-in-MP4 case.
                if (oti == 0x69 || oti == 0x6B) out.codec = Mp4AudioCodec::MP3;
                else if (oti == 0x40 || oti == 0x66 || oti == 0x67 || oti == 0x68) {
                    out.codec = Mp4AudioCodec::AAC;
                }
            }
            return false;
        }
        if (c.type == FourCC('a','l','a','c')) {
            // The magic cookie sits after the 4-byte version/flags word.
            if (c.bodySize > 4) out.codecConfig.assign(c.body + 4, c.body + c.bodySize);
            out.codec = Mp4AudioCodec::ALAC;
            return true;
        }
        if (c.type == FourCC('w','a','v','e')) {
            // QuickTime nests the esds one level deeper.
            Box esds;
            if (FindBox(c.body, c.bodySize, FourCC('e','s','d','s'), esds)) {
                std::vector<uint8_t> dsi;
                uint8_t oti = 0;
                if (ParseEsds(esds.body, esds.bodySize, dsi, oti)) {
                    out.codecConfig = std::move(dsi);
                    if (oti == 0x69 || oti == 0x6B) out.codec = Mp4AudioCodec::MP3;
                }
            }
            return true;
        }
        return true;
    });
}

// ===== TRACK WALK =====

// mdhd: version byte + 3 flag bytes, then creation/modification/timescale/
// duration - 32-bit fields in version 0, 64-bit creation/modification/duration
// in version 1.
uint32_t ParseMdhdTimescale(const Box& b) {
    if (b.bodySize < 4) return 0;
    const uint8_t version = b.body[0];
    if (version == 1) return b.bodySize >= 28 ? RdU32(b.body + 20) : 0;
    return b.bodySize >= 16 ? RdU32(b.body + 12) : 0;
}

bool IsSoundHandler(const Box& hdlr) {
    // version/flags(4) pre_defined(4) handler_type(4)
    return hdlr.bodySize >= 12 && RdU32(hdlr.body + 8) == FourCC('s','o','u','n');
}

// Fill `out` from one trak box. Returns false when the trak is not audio or its
// tables are unusable.
bool ParseTrak(const Box& trak, uint64_t fileSize, Mp4AudioTrack& out) {
    Box mdia, hdlr, mdhd, minf, stbl;
    if (!FindBox(trak.body, trak.bodySize, FourCC('m','d','i','a'), mdia)) return false;
    if (!FindBox(mdia.body, mdia.bodySize, FourCC('h','d','l','r'), hdlr)) return false;
    if (!IsSoundHandler(hdlr)) return false;
    if (!FindBox(mdia.body, mdia.bodySize, FourCC('m','i','n','f'), minf)) return false;
    if (!FindBox(minf.body, minf.bodySize, FourCC('s','t','b','l'), stbl)) return false;

    uint32_t timescale = 0;
    if (FindBox(mdia.body, mdia.bodySize, FourCC('m','d','h','d'), mdhd)) {
        timescale = ParseMdhdTimescale(mdhd);
    }

    Box stsd;
    if (FindBox(stbl.body, stbl.bodySize, FourCC('s','t','s','d'), stsd) && stsd.bodySize > 8) {
        // version/flags(4) entry_count(4), then the sample entries as boxes.
        ForEachBox(stsd.body + 8, stsd.bodySize - 8, [&](const Box& entry) {
            ParseAudioSampleEntry(entry, out);
            return false;                     // first entry only
        });
    }

    SampleTable table;
    bool haveSizes = false;
    bool haveOffsets = false;
    ForEachBox(stbl.body, stbl.bodySize, [&](const Box& b) {
        if (b.type == FourCC('s','t','s','z')) haveSizes   = ParseStsz(b, table);
        else if (b.type == FourCC('s','t','z','2')) haveSizes = ParseStz2(b, table);
        else if (b.type == FourCC('s','t','c','o')) haveOffsets = ParseStco(b, table, false);
        else if (b.type == FourCC('c','o','6','4')) haveOffsets = ParseStco(b, table, true);
        else if (b.type == FourCC('s','t','s','c')) ParseStsc(b, table);
        else if (b.type == FourCC('s','t','t','s')) ParseStts(b, table);
        return true;
    });

    if (timescale > 0 && table.mediaDuration > 0) {
        out.duration = static_cast<double>(table.mediaDuration) / timescale;
    }
    if (!haveSizes || !haveOffsets) return false;
    return BuildSampleList(table, fileSize, out.samples);
}

// ===== ASC BIT READER =====
class BitReader {
public:
    BitReader(const uint8_t* d, size_t n) : data(d), bits(n * 8) {}
    // Returns 0 past the end; callers check Remaining() before the reads that
    // must be present.
    uint32_t Read(int count) {
        uint32_t v = 0;
        for (int i = 0; i < count; ++i) {
            const size_t bit = pos + i;
            v <<= 1;
            if (bit < bits) v |= (data[bit >> 3] >> (7 - (bit & 7))) & 1u;
        }
        pos += count;
        return v;
    }
    bool Ok() const { return pos <= bits; }
private:
    const uint8_t* data;
    size_t bits;
    size_t pos = 0;
};

const int kAscSampleRates[13] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350
};

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
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

} // namespace

// ===== PUBLIC SURFACE =====

bool ParseAudioSpecificConfig(const uint8_t* asc, size_t size,
                              int* sampleRate, int* channels, int* objectType) {
    if (!asc || size < 2) return false;
    BitReader br(asc, size);
    int aot = static_cast<int>(br.Read(5));
    if (aot == 31) aot = 32 + static_cast<int>(br.Read(6));      // escape value

    const uint32_t freqIndex = br.Read(4);
    int rate = 0;
    if (freqIndex == 0x0F) {
        rate = static_cast<int>(br.Read(24));                    // explicit rate
    } else if (freqIndex < 13) {
        rate = kAscSampleRates[freqIndex];
    }

    const uint32_t channelConfig = br.Read(4);

    // Explicit hierarchical SBR/PS signalling (HE-AAC v1/v2): the frequency
    // read above is the *core* rate and the extension rate that follows is what
    // the decoder actually outputs - usually double. Reporting the core rate
    // here would halve the duration of every HE-AAC file.
    if (aot == 5 || aot == 29) {
        const uint32_t extIndex = br.Read(4);
        int extRate = 0;
        if (extIndex == 0x0F)      extRate = static_cast<int>(br.Read(24));
        else if (extIndex < 13)    extRate = kAscSampleRates[extIndex];
        if (br.Ok() && extRate > 0) rate = extRate;
    }

    if (!br.Ok() || rate <= 0) return false;

    // Channel configuration 0 means "described in the bitstream"; the decoder
    // reports the real count after init, so leave it at 0 here.
    int ch = 0;
    if (channelConfig >= 1 && channelConfig <= 7) {
        ch = (channelConfig == 7) ? 8 : static_cast<int>(channelConfig);
    }

    if (sampleRate) *sampleRate = rate;
    if (channels)   *channels = ch;
    if (objectType) *objectType = aot;
    return true;
}

bool LooksLikeIsoBmff(const std::string& path) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    uint8_t head[8] = {0};
    const size_t n = std::fread(head, 1, sizeof(head), fp);
    std::fclose(fp);
    if (n < 8) return false;
    switch (RdU32(head + 4)) {
        case FourCC('f','t','y','p'):
        case FourCC('s','t','y','p'):
        case FourCC('m','o','o','v'):
        case FourCC('m','d','a','t'):
        case FourCC('f','r','e','e'):
        case FourCC('s','k','i','p'):
        case FourCC('w','i','d','e'):
        case FourCC('p','n','o','t'):
            return true;
        default:
            return false;
    }
}

bool LooksLikeRawAac(const std::string& path) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    uint8_t head[4] = {0};
    const size_t n = std::fread(head, 1, sizeof(head), fp);
    std::fclose(fp);
    if (n < 4) return false;
    if (std::memcmp(head, "ADIF", 4) == 0) return true;
    // ADTS syncword: 12 set bits, then a zero layer field.
    return head[0] == 0xFF && (head[1] & 0xF6) == 0xF0;
}

bool Mp4ParseAudioTrack(const uint8_t* fileData, size_t fileSize, Mp4AudioTrack& out) {
    if (!fileData || fileSize < 16) return false;
    out = Mp4AudioTrack{};

    Box moov;
    if (!FindBox(fileData, fileSize, FourCC('m','o','o','v'), moov)) return false;

    bool parsed = false;
    ForEachBox(moov.body, moov.bodySize, [&](const Box& b) {
        if (b.type != FourCC('t','r','a','k')) return true;
        Mp4AudioTrack candidate;
        if (ParseTrak(b, fileSize, candidate)) {
            out = std::move(candidate);
            parsed = true;
            return false;
        }
        // Keep the codec identity of an audio trak whose sample table was empty
        // (the fragmented case) so the caller can name what it found.
        if (candidate.codec != Mp4AudioCodec::Unknown || !candidate.codecName.empty()) {
            out = std::move(candidate);
        }
        return true;
    });

    if (!parsed) {
        // mvex in the moov marks a fragmented file: the samples live in moof
        // boxes, which this demuxer does not walk.
        Box mvex;
        out.fragmented = FindBox(moov.body, moov.bodySize, FourCC('m','v','e','x'), mvex);
        return false;
    }

    // The setup data is authoritative for AAC: the sample entry's 16.16 rate
    // cannot express 88.2/96 kHz, and HE-AAC signals its rate there too.
    if (out.codec == Mp4AudioCodec::AAC && !out.codecConfig.empty()) {
        int rate = 0, ch = 0;
        if (ParseAudioSpecificConfig(out.codecConfig.data(), out.codecConfig.size(),
                                     &rate, &ch, nullptr)) {
            if (rate > 0) out.sampleRate = rate;
            if (ch > 0)   out.channels = ch;
        }
    }
    return true;
}

bool Mp4ProbeAudioTrack(const std::string& path, Mp4AudioTrack& out) {
    std::vector<uint8_t> data;
    if (!ReadFileBytes(path, data)) return false;
    return Mp4ParseAudioTrack(data.data(), data.size(), out);
}

} // namespace AudioCodecs
} // namespace UltraCanvas
