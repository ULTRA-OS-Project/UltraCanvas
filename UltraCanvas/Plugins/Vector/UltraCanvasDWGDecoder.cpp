// UltraCanvas/Plugins/Vector/UltraCanvasDWGDecoder.cpp
// Native DWG decoder - see UltraCanvasDWGDecoder.h for scope.
//
// Layout of this file:
//   1. Bit-coded value reader (the DWG "bit codes": B, BB, BS, BL, BD, MC,
//      MS, DD, BE, BT, H, TV/TU, CMC/ENC ...)
//   2. Text conversion (code-page strings and UTF-16 to UTF-8)
//   3. Decompressors: the R2004+ LZ77 variant, the R2007 LZ variant, and
//      the R2007 Reed-Solomon de-interleave
//   4. File loaders: R13-R2000 section locators, R2004+ page/section maps,
//      R2007 page/section maps - each yields the object data, the object
//      map (handle -> offset) and the CLASSES bytes
//   5. Object decoding: common entity/object headers, the handle and string
//      streams, table records and every supported entity
//   6. Assembly of the DXF text (TABLES, BLOCKS, ENTITIES)
// Version: 1.0.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework

#include "UltraCanvasDWGDecoder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace UltraCanvas {
namespace VectorConverter {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ===========================================================================
// 1. Versions and the bit reader
// ===========================================================================

enum class Ver { None = 0, R13, R14, R2000, R2004, R2007, R2010, R2013, R2018 };

Ver VersionFromMagic(const std::string& head) {
    if (head.size() < 6 || head.compare(0, 4, "AC10") != 0) return Ver::None;
    std::string v = head.substr(0, 6);
    if (v == "AC1012" || v == "AC1013") return Ver::R13;
    if (v == "AC1014") return Ver::R14;
    if (v == "AC1015") return Ver::R2000;
    if (v == "AC1018") return Ver::R2004;
    if (v == "AC1021") return Ver::R2007;
    if (v == "AC1024") return Ver::R2010;
    if (v == "AC1027") return Ver::R2013;
    if (v == "AC1032") return Ver::R2018;
    return Ver::None;
}

struct Handle {
    uint8_t code = 0;
    uint64_t value = 0;
};

// Reads the DWG bit codes from a byte buffer. Positions are in bits. Any
// read past the end sets `overrun` and returns zero, so callers can parse
// optimistically and check once.
class BitReader {
public:
    BitReader() = default;
    BitReader(const uint8_t* d, size_t bytes, Ver v) : data(d), size(bytes), ver(v) {}

    size_t Pos() const { return pos; }
    size_t SizeBits() const { return size * 8; }
    void SetPos(size_t bits) { pos = bits; }
    void Advance(long bits) { pos = static_cast<size_t>(static_cast<long>(pos) + bits); }
    bool Overrun() const { return overrun; }
    Ver Version() const { return ver; }
    bool Valid() const { return data != nullptr; }

    uint8_t B() {
        if (pos + 1 > size * 8) { overrun = true; return 0; }
        uint8_t v = (data[pos >> 3] >> (7 - (pos & 7))) & 1;
        ++pos;
        return v;
    }
    uint8_t BB() {
        uint8_t a = B();
        uint8_t b = B();
        return static_cast<uint8_t>((a << 1) | b);
    }
    uint8_t Bits3() {
        uint8_t a = BB();
        uint8_t b = B();
        return static_cast<uint8_t>((a << 1) | b);
    }
    uint8_t RC() {
        if (pos + 8 > size * 8) { overrun = true; pos = size * 8; return 0; }
        size_t byte = pos >> 3;
        unsigned bit = pos & 7;
        uint8_t v;
        if (bit == 0) {
            v = data[byte];
        } else {
            v = static_cast<uint8_t>((data[byte] << bit) | (data[byte + 1] >> (8 - bit)));
        }
        pos += 8;
        return v;
    }
    uint16_t RS() {
        uint16_t lo = RC();
        uint16_t hi = RC();
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    uint16_t RSBE() {
        uint16_t hi = RC();
        uint16_t lo = RC();
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    uint32_t RL() {
        uint32_t lo = RS();
        uint32_t hi = RS();
        return lo | (hi << 16);
    }
    uint64_t RLL() {
        uint64_t lo = RL();
        uint64_t hi = RL();
        return lo | (hi << 32);
    }
    double RD() {
        uint8_t b[8];
        for (auto& x : b) x = RC();
        double d;
        std::memcpy(&d, b, 8);   // little-endian IEEE on every target we build
        return d;
    }
    uint16_t BS() {
        switch (BB()) {
            case 0: return RS();
            case 1: return RC();
            case 2: return 0;
            default: return 256;
        }
    }
    uint32_t BL() {
        switch (BB()) {
            case 0: return RL();
            case 1: return RC();
            case 2: return 0;
            default: return 256;
        }
    }
    uint64_t BLL() {
        unsigned len = Bits3();
        uint64_t result = 0;
        for (unsigned i = 0; i < len; ++i) {
            result |= static_cast<uint64_t>(RC()) << (8 * i);
        }
        return result;
    }
    double BD() {
        switch (BB()) {
            case 0: return RD();
            case 1: return 1.0;
            case 2: return 0.0;
            default: overrun = true; return 0.0;   // 3 is invalid
        }
    }
    // Object type for R2010+.
    uint16_t BOT() {
        switch (BB()) {
            case 0: return RC();
            case 1: return static_cast<uint16_t>(RC() + 0x1f0);
            default: return RS();
        }
    }
    // Modular char, signed.
    int32_t MC() {
        int64_t result = 0;
        for (int i = 0, shift = 0; i < 5; ++i, shift += 7) {
            uint8_t b = RC();
            if (!(b & 0x80)) {
                bool negative = (b & 0x40) != 0;
                b &= 0x3f;
                result |= static_cast<int64_t>(b) << shift;
                return static_cast<int32_t>(negative ? -result : result);
            }
            result |= static_cast<int64_t>(b & 0x7f) << shift;
        }
        overrun = true;
        return 0;
    }
    // Modular char, unsigned.
    uint64_t UMC() {
        uint64_t result = 0;
        for (int i = 0, shift = 0; i < 8; ++i, shift += 7) {
            uint8_t b = RC();
            result |= static_cast<uint64_t>(b & 0x7f) << shift;
            if (!(b & 0x80)) return result;
        }
        overrun = true;
        return 0;
    }
    // Modular short.
    uint32_t MS() {
        uint32_t result = 0;
        for (int i = 0, shift = 0; i < 2; ++i, shift += 15) {
            uint16_t w = RS();
            result |= static_cast<uint32_t>(w & 0x7fff) << shift;
            if (!(w & 0x8000)) return result;
        }
        overrun = true;
        return 0;
    }
    // Bit double with default.
    double DD(double def) {
        switch (BB()) {
            case 0: return def;
            case 3: return RD();
            case 2: {
                uint8_t b[8];
                std::memcpy(b, &def, 8);
                b[4] = RC(); b[5] = RC();
                b[0] = RC(); b[1] = RC(); b[2] = RC(); b[3] = RC();
                double d;
                std::memcpy(&d, b, 8);
                return d;
            }
            default: {   // 1
                uint8_t b[8];
                std::memcpy(b, &def, 8);
                b[0] = RC(); b[1] = RC(); b[2] = RC(); b[3] = RC();
                double d;
                std::memcpy(&d, b, 8);
                return d;
            }
        }
    }
    // Bit extrusion.
    void BE(double& x, double& y, double& z) {
        if (ver >= Ver::R2000 && B()) {
            x = 0; y = 0; z = 1;
            return;
        }
        x = BD(); y = BD(); z = BD();
    }
    // Bit thickness.
    double BT() {
        if (ver >= Ver::R2000 && B()) return 0.0;
        return BD();
    }
    Handle H() {
        Handle h;
        uint8_t first = RC();
        h.code = static_cast<uint8_t>((first >> 4) & 0xf);
        unsigned len = first & 0xf;
        if (len > 8) { overrun = true; return h; }
        uint64_t v = 0;
        for (unsigned i = 0; i < len; ++i) v = (v << 8) | RC();
        h.value = v;
        return h;
    }
    // Raw bytes.
    void Skip(size_t bytes) {
        if (pos + bytes * 8 > size * 8) { overrun = true; pos = size * 8; return; }
        pos += bytes * 8;
    }
    std::string TV();              // code-page string, length BS
    std::string TU();              // UTF-16 string, length BS
    // Version-aware text: pre-R2007 from this stream, R2007+ from `str`.
    std::string T(BitReader& str) {
        if (ver < Ver::R2007) return TV();
        if (!str.Valid()) return {};
        return str.TU();
    }

private:
    const uint8_t* data = nullptr;
    size_t size = 0;
    Ver ver = Ver::None;
    size_t pos = 0;
    bool overrun = false;
};

// ===========================================================================
// 2. Text conversion
// ===========================================================================

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Windows-1252 (the usual drawing code page) to UTF-8. Bytes that are
// already valid UTF-8 sequences pass through unchanged, so R2004+ files
// written by tools that store UTF-8 in TV strings survive too.
std::string CodePageToUtf8(const std::string& s) {
    static const uint16_t cp1252[32] = {
        0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
        0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
    // Valid UTF-8 already?
    bool utf8 = true;
    for (size_t i = 0; i < s.size() && utf8;) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) { utf8 = false; break; }
        for (size_t k = 1; k < n; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) { utf8 = false; break; }
        }
        i += n;
    }
    if (utf8) return s;
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0xA0) AppendUtf8(out, cp1252[c - 0x80]);
        else AppendUtf8(out, c);
    }
    return out;
}

std::string Utf16ToUtf8(const std::vector<uint16_t>& w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = w[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size() &&
            w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            ++i;
        }
        if (cp == 0) break;
        AppendUtf8(out, cp);
    }
    return out;
}

std::string BitReader::TV() {
    unsigned len = BS();
    if (pos + static_cast<size_t>(len) * 8 > size * 8) { overrun = true; return {}; }
    std::string s;
    s.reserve(len);
    for (unsigned i = 0; i < len; ++i) s.push_back(static_cast<char>(RC()));
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return CodePageToUtf8(s);
}

std::string BitReader::TU() {
    unsigned len = BS();
    if (pos + static_cast<size_t>(len) * 16 > size * 8) { overrun = true; return {}; }
    std::vector<uint16_t> w(len);
    for (unsigned i = 0; i < len; ++i) w[i] = RS();
    return Utf16ToUtf8(w);
}

// ===========================================================================
// 3. Decompressors
// ===========================================================================

// The R2004+ LZ77 variant used for system and data pages. Decompresses
// `src` into `dst` (pre-sized to the expected length). Returns false on a
// malformed stream; whatever was produced stays in `dst`.
bool DecompressR2004(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dst) {
    size_t sp = 0, dp = 0;
    const size_t dstSize = dst.size();
    auto byte = [&]() -> unsigned { return sp < srcSize ? src[sp++] : 0; };
    auto literalLength = [&](unsigned opcode) -> unsigned {
        unsigned low = opcode & 0xf;
        if (low == 0) {
            unsigned last;
            while ((last = byte()) == 0 && sp < srcSize) low += 0xFF;
            low += 0xf + last;
        }
        return low + 3;
    };
    auto compressedBytes = [&](unsigned opcode, unsigned mask) -> unsigned {
        unsigned n = opcode & mask;
        if (n == 0) {
            unsigned last;
            while ((last = byte()) == 0 && sp < srcSize) n += 0xFF;
            n += last + mask;
        }
        return n + 2;
    };
    auto copyLiterals = [&](unsigned n) -> unsigned {
        for (unsigned i = 0; i < n; ++i) {
            unsigned b = byte();
            if (dp < dstSize) dst[dp++] = static_cast<uint8_t>(b);
        }
        return byte();
    };

    unsigned opcode = byte();
    if ((opcode & 0xF0) == 0) opcode = copyLiterals(literalLength(opcode));
    while (sp < srcSize && dp < dstSize && opcode != 0x11) {
        unsigned compBytes = 0;
        size_t compOffset = 0;
        if (opcode < 0x10 || opcode >= 0x40) {
            compBytes = (opcode >> 4) - 1;
            unsigned op2 = byte();
            compOffset = (((opcode >> 2) & 3) | (op2 << 2)) + 1;
        } else if (opcode < 0x20) {
            compBytes = compressedBytes(opcode, 7);
            compOffset = (opcode & 8) << 11;
            unsigned b1 = byte(), b2 = byte();
            compOffset |= (b1 >> 2);
            compOffset |= b2 << 6;
            compOffset += 0x4000;
            opcode = b1;
        } else {
            compBytes = compressedBytes(opcode, 0x1f);
            unsigned b1 = byte(), b2 = byte();
            compOffset |= (b1 >> 2);
            compOffset |= b2 << 6;
            compOffset += 1;
            opcode = b1;
        }
        if (compOffset > dp || compOffset == 0) return false;
        for (unsigned i = 0; i < compBytes && dp < dstSize; ++i, ++dp) {
            dst[dp] = dst[dp - compOffset];
        }
        unsigned lit = opcode & 3;
        if (lit == 0) {
            opcode = byte();
            if ((opcode & 0xf0) == 0) lit = literalLength(opcode);
        }
        if (lit) {
            if (dp + lit > dstSize) break;
            opcode = copyLiterals(lit);
        }
    }
    return true;
}

// R2007 literal runs are stored with their bytes permuted in 8-byte units
// (and reversed inside the 2- and 3-byte tails); this restores file order.
void CopyR2007Literals(uint8_t* dst, const uint8_t* src, uint32_t length) {
    auto c1 = [&](uint32_t off) { *dst++ = src[off]; };
    auto c2 = [&](uint32_t off) { dst[0] = src[off + 1]; dst[1] = src[off]; dst += 2; };
    auto c3 = [&](uint32_t off) { dst[0] = src[off + 2]; dst[1] = src[off + 1]; dst[2] = src[off]; dst += 3; };
    auto c4 = [&](uint32_t off) { std::memcpy(dst, src + off, 4); dst += 4; };
    auto c8 = [&](uint32_t off) { std::memcpy(dst, src + off, 8); dst += 8; };
    auto c16 = [&](uint32_t off) { std::memcpy(dst, src + off + 8, 8); std::memcpy(dst + 8, src + off, 8); dst += 16; };
    while (length >= 32) {
        c16(16);
        c16(0);
        src += 32;
        length -= 32;
    }
    switch (length) {
        case 0: break;
        case 1: c1(0); break;
        case 2: c2(0); break;
        case 3: c3(0); break;
        case 4: c4(0); break;
        case 5: c1(4); c4(0); break;
        case 6: c1(5); c4(1); c1(0); break;
        case 7: c2(5); c4(1); c1(0); break;
        case 8: c8(0); break;
        case 9: c1(8); c8(0); break;
        case 10: c1(9); c8(1); c1(0); break;
        case 11: c2(9); c8(1); c1(0); break;
        case 12: c4(8); c8(0); break;
        case 13: c1(12); c4(8); c8(0); break;
        case 14: c1(13); c4(9); c8(1); c1(0); break;
        case 15: c2(13); c4(9); c8(1); c1(0); break;
        case 16: c16(0); break;
        case 17: c8(9); c1(8); c8(0); break;
        case 18: c1(17); c16(1); c1(0); break;
        case 19: c3(16); c16(0); break;
        case 20: c4(16); c16(0); break;
        case 21: c1(20); c4(16); c16(0); break;
        case 22: c2(20); c4(16); c16(0); break;
        case 23: c3(20); c4(16); c16(0); break;
        case 24: c8(16); c16(0); break;
        case 25: c8(17); c1(16); c16(0); break;
        case 26: c1(25); c8(17); c1(16); c16(0); break;
        case 27: c2(25); c8(17); c1(16); c16(0); break;
        case 28: c4(24); c8(16); c16(0); break;
        case 29: c1(28); c4(24); c8(16); c16(0); break;
        case 30: c2(28); c4(24); c8(16); c16(0); break;
        case 31: c1(30); c4(26); c8(18); c16(2); c2(0); break;
        default: break;
    }
}

// The R2007 LZ variant.
bool DecompressR2007(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dst) {
    size_t sp = 0, dp = 0;
    const size_t dstSize = dst.size();
    uint32_t length = 0, offset = 0;
    if (srcSize < 2) return false;
    auto avail = [&](size_t n) { return sp + n <= srcSize; };

    auto readLiteralLength = [&](unsigned opcode) -> bool {
        length = opcode + 8;
        if (length == 0x17) {
            if (!avail(1)) return false;
            unsigned n = src[sp++];
            length += n;
            if (n == 0xff) {
                do {
                    if (!avail(2)) return false;
                    n = src[sp++];
                    n |= src[sp++] << 8;
                    length += n;
                } while (n == 0xFFFF);
            }
        }
        return true;
    };
    auto readInstructions = [&](unsigned& opcode) -> bool {
        switch (opcode >> 4) {
            case 0:
                length = (opcode & 0xf) + 0x13;
                if (!avail(2)) return false;
                offset = src[sp++];
                opcode = src[sp++];
                length = ((opcode >> 3) & 0x10) + length;
                offset = ((opcode & 0x78) << 5) + 1 + offset;
                break;
            case 1:
                length = (opcode & 0xf) + 3;
                if (!avail(2)) return false;
                offset = src[sp++];
                opcode = src[sp++];
                offset = ((opcode & 0xf8) << 5) + 1 + offset;
                break;
            case 2:
                if (!avail(2)) return false;
                offset = src[sp++];
                offset = ((src[sp++] << 8) & 0xff00) | offset;
                length = opcode & 7;
                if ((opcode & 8) == 0) {
                    if (!avail(1)) return false;
                    opcode = src[sp++];
                    length = (opcode & 0xf8) + length;
                } else {
                    offset++;
                    if (!avail(1)) return false;
                    length = (src[sp++] << 3) + length;
                    if (!avail(1)) return false;
                    opcode = src[sp++];
                    length = (((opcode & 0xf8) << 8) + length) + 0x100;
                }
                break;
            default:
                length = opcode >> 4;
                offset = opcode & 15;
                if (!avail(1)) return false;
                opcode = src[sp++];
                offset = (((opcode & 0xf8) << 1) + offset) + 1;
                break;
        }
        return true;
    };

    unsigned opcode = src[sp++];
    if ((opcode & 0xf0) == 0x20) {
        sp += 2;
        if (!avail(1)) return false;
        length = src[sp++] & 0x07;
        if (length == 0) return false;
    }
    while (sp < srcSize) {
        if (length == 0) {
            if (!readLiteralLength(opcode)) return false;
        }
        if (dp + length > dstSize || sp + length > srcSize) return false;
        CopyR2007Literals(&dst[dp], &src[sp], length);
        dp += length;
        sp += length;
        length = 0;
        if (sp >= srcSize) return true;
        opcode = src[sp++];
        if (!readInstructions(opcode)) return false;
        while (true) {
            if (dp + length > dstSize) return false;
            if (offset > dp) return false;
            for (uint32_t i = 0; i < length; ++i, ++dp) dst[dp] = dst[dp - offset];
            length = opcode & 7;
            if (length != 0 || sp >= srcSize) break;
            opcode = src[sp++];
            if ((opcode >> 4) == 0) break;
            if ((opcode >> 4) == 0x0f) opcode &= 0xf;
            if (!readInstructions(opcode)) return false;
        }
    }
    return true;
}

// Reed-Solomon de-interleave: the R2007 pages store `blockCount` codewords
// of 255 bytes interleaved byte by byte; the first `dataSize` bytes of each
// codeword are payload. Parity is dropped (no error correction attempted).
std::vector<uint8_t> DecodeRS(const uint8_t* src, size_t srcSize, size_t blockCount,
                              size_t dataSize) {
    std::vector<uint8_t> out(blockCount * dataSize, 0);
    for (size_t i = 0; i < blockCount; ++i) {
        for (size_t j = 0; j < dataSize; ++j) {
            size_t si = j * blockCount + i;
            if (si < srcSize) out[i * dataSize + j] = src[si];
        }
    }
    return out;
}

// ===========================================================================
// 4. File loaders
// ===========================================================================

struct DrawingData {
    Ver ver = Ver::None;
    uint8_t maintVersion = 0;
    std::vector<uint8_t> objects;                   // object data image
    std::vector<uint8_t> objectMap;                 // "Handles" section bytes
    std::vector<uint8_t> classes;                   // "Classes" section bytes
    std::vector<std::pair<uint64_t, uint64_t>> map; // handle, offset in `objects`
    std::string error;
};

// The R13-R2000 object map and its R2004+ twin share one format: sections
// of big-endian size, (UMC handle delta, MC offset delta) pairs, CRC.
void ParseObjectMap(const std::vector<uint8_t>& mapData, DrawingData& d) {
    size_t p = 0;
    while (p + 2 <= mapData.size()) {
        size_t start = p;
        unsigned secSize = (mapData[p] << 8) | mapData[p + 1];
        if (secSize <= 2) break;
        p += 2;
        uint64_t handle = 0;
        int64_t offset = 0;
        BitReader r(mapData.data() + start, std::min(mapData.size() - start, static_cast<size_t>(secSize)), d.ver);
        r.SetPos(16);
        while (r.Pos() / 8 < secSize && !r.Overrun()) {
            uint64_t hoff = r.UMC();
            int32_t ooff = r.MC();
            if (r.Overrun()) break;
            handle += hoff;
            offset += ooff;
            if (offset >= 0) d.map.emplace_back(handle, static_cast<uint64_t>(offset));
        }
        p = start + secSize + 2;   // plus CRC
    }
}

bool LoadR13R2000(const std::string& file, DrawingData& d) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(file.data());
    if (file.size() < 0x19 + 9 * 3) { d.error = "file too short"; return false; }
    BitReader r(bytes, file.size(), d.ver);
    r.SetPos(0x12 * 8);
    d.maintVersion = r.RC();
    r.SetPos(0x15 * 8);
    uint32_t numSections = r.RL();
    if (numSections < 3 || numSections > 6) { d.error = "bad section count"; return false; }
    struct Loc { uint32_t address, size; };
    std::vector<Loc> locs;
    for (uint32_t i = 0; i < numSections; ++i) {
        r.RC();
        Loc l{r.RL(), r.RL()};
        if (static_cast<uint64_t>(l.address) + l.size > file.size()) {
            d.error = "section locator outside file";
            return false;
        }
        locs.push_back(l);
    }
    // The whole file is the object image; map offsets are file offsets.
    d.objects.assign(bytes, bytes + file.size());
    d.classes.assign(bytes + locs[1].address, bytes + locs[1].address + locs[1].size);
    d.objectMap.assign(bytes + locs[2].address, bytes + locs[2].address + locs[2].size);
    ParseObjectMap(d.objectMap, d);
    return true;
}

// ---- R2004+ (also R2010/R2013/R2018) -------------------------------------

struct R2004Page {
    int32_t number;
    uint32_t size;
    uint64_t address;   // file offset
};

struct R2004Info {
    uint64_t size = 0;
    uint32_t numPages = 0;
    uint32_t maxDecompSize = 0;
    uint32_t compressed = 0;
    std::string name;
    struct Page { int32_t number; uint32_t size; uint64_t offset; };
    std::vector<Page> pages;
};

uint32_t LE32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t LE64(const uint8_t* p) {
    return LE32(p) | (static_cast<uint64_t>(LE32(p + 4)) << 32);
}

bool LoadR2004(const std::string& file, DrawingData& d) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(file.data());
    const size_t fileSize = file.size();
    if (fileSize < 0x100) { d.error = "file too short"; return false; }
    d.maintVersion = bytes[0x12];

    // Encrypted header at 0x80.
    uint8_t hdr[0x6c];
    {
        uint32_t seed = 1;
        for (size_t i = 0; i < sizeof(hdr); ++i) {
            seed = seed * 0x343fd + 0x269ec3;
            hdr[i] = bytes[0x80 + i] ^ static_cast<uint8_t>(seed >> 16);
        }
    }
    if (std::memcmp(hdr, "AcFssFcAJMB", 11) != 0) { d.error = "R2004 header signature missing"; return false; }
    uint32_t sectionMapId = LE32(hdr + 0x50);
    uint64_t sectionMapAddress = LE64(hdr + 0x54) + 0x100;
    int32_t sectionInfoId = static_cast<int32_t>(LE32(hdr + 0x5c));
    uint32_t sectionArraySize = LE32(hdr + 0x60);

    // Reads a system page (page map / section info): 20-byte header then
    // compressed data.
    auto readSystemPage = [&](uint64_t address, uint32_t expectType,
                              std::vector<uint8_t>& out) -> bool {
        if (address + 0x14 > fileSize) return false;
        uint32_t type = LE32(bytes + address);
        uint32_t decompSize = LE32(bytes + address + 4);
        uint32_t compSize = LE32(bytes + address + 8);
        if (type != expectType) return false;
        if (address + 0x14 + compSize > fileSize) compSize = static_cast<uint32_t>(fileSize - address - 0x14);
        if (decompSize > 0x4000000) return false;
        out.assign(decompSize, 0);
        DecompressR2004(bytes + address + 0x14, compSize, out);
        return true;
    };

    // Section page map.
    std::vector<uint8_t> pm;
    if (!readSystemPage(sectionMapAddress, 0x41630e3b, pm)) {
        // Some writers put it elsewhere: scan backwards for the marker.
        bool found = false;
        for (uint64_t a = (fileSize & ~static_cast<uint64_t>(0xff)); a >= 0x120 && !found; a -= 0x20) {
            if (a + 4 <= fileSize && LE32(bytes + a) == 0x41630e3b) {
                found = readSystemPage(a, 0x41630e3b, pm);
            }
            if (a < 0x20) break;
        }
        if (!found) { d.error = "R2004 section page map not found"; return false; }
    }
    std::map<int32_t, R2004Page> pages;
    {
        uint64_t address = 0x100;
        size_t p = 0;
        while (p + 8 <= pm.size()) {
            R2004Page pg;
            pg.number = static_cast<int32_t>(LE32(&pm[p]));
            pg.size = LE32(&pm[p + 4]);
            pg.address = address;
            p += 8;
            if (pg.number <= static_cast<int32_t>(sectionArraySize)) address += pg.size;
            if (pg.number < 0) {
                p += 16;   // gap record: parent, left, right, 0
            }
            pages[pg.number] = pg;
        }
    }
    (void)sectionMapId;

    // Section info.
    auto infoIt = pages.find(sectionInfoId);
    if (infoIt == pages.end()) { d.error = "R2004 section info page missing"; return false; }
    std::vector<uint8_t> info;
    if (!readSystemPage(infoIt->second.address, 0x4163003b, info)) {
        d.error = "R2004 section info unreadable";
        return false;
    }
    std::vector<R2004Info> infos;
    {
        if (info.size() < 20) { d.error = "R2004 section info truncated"; return false; }
        uint32_t numDesc = LE32(&info[0]);
        size_t p = 20;
        for (uint32_t i = 0; i < numDesc && p + 8 + 6 * 4 + 64 <= info.size(); ++i) {
            R2004Info in;
            in.size = LE64(&info[p]); p += 8;
            in.numPages = LE32(&info[p]); p += 4;
            in.maxDecompSize = LE32(&info[p]); p += 4;
            p += 4;   // unknown
            in.compressed = LE32(&info[p]); p += 4;
            p += 4;   // type
            p += 4;   // encrypted
            const char* name = reinterpret_cast<const char*>(&info[p]);
            size_t nameLen = 0;
            while (nameLen < 64 && name[nameLen] != '\0') ++nameLen;
            in.name.assign(name, nameLen);
            p += 64;
            if (in.numPages > 1000000) break;
            for (uint32_t j = 0; j < in.numPages && p + 16 <= info.size(); ++j) {
                R2004Info::Page pg;
                pg.number = static_cast<int32_t>(LE32(&info[p]));
                pg.size = LE32(&info[p + 4]);
                pg.offset = LE64(&info[p + 8]);
                p += 16;
                in.pages.push_back(pg);
            }
            infos.push_back(std::move(in));
        }
    }

    auto readSection = [&](const std::string& name, std::vector<uint8_t>& out) -> bool {
        const R2004Info* in = nullptr;
        for (const auto& i : infos) if (i.name == name) { in = &i; break; }
        if (!in || in->pages.empty()) return false;
        uint64_t total = static_cast<uint64_t>(in->pages.size()) * in->maxDecompSize;
        if (total == 0 || total > 0x2f000000) return false;
        out.assign(total, 0);
        for (const auto& pg : in->pages) {
            auto it = pages.find(pg.number);
            if (it == pages.end()) continue;
            uint64_t address = it->second.address;
            if (address + 32 > fileSize) return false;
            uint32_t es[8];
            uint32_t mask = 0x4164536b ^ static_cast<uint32_t>(address);
            for (int k = 0; k < 8; ++k) es[k] = LE32(bytes + address + 4 * k) ^ mask;
            uint32_t dataSize = es[2], pageSize = es[3], secOffset = es[4];
            if (es[0] != 0x4163043b) continue;
            if (secOffset > total) continue;
            if (in->compressed == 2) {
                if (secOffset + in->maxDecompSize > total) continue;
                if (address + 32 + dataSize > fileSize) dataSize = static_cast<uint32_t>(fileSize - address - 32);
                std::vector<uint8_t> dec(in->maxDecompSize, 0);
                DecompressR2004(bytes + address + 32, dataSize, dec);
                std::memcpy(&out[secOffset], dec.data(), dec.size());
            } else {
                uint64_t n = std::min<uint64_t>(pageSize, in->size > secOffset ? in->size - secOffset : 0);
                if (secOffset + n > total || address + 32 + n > fileSize) continue;
                std::memcpy(&out[secOffset], bytes + address + 32, n);
            }
        }
        if (in->size < total) out.resize(in->size);
        return true;
    };

    if (!readSection("AcDb:AcDbObjects", d.objects)) { d.error = "AcDb:AcDbObjects section missing"; return false; }
    if (!readSection("AcDb:Handles", d.objectMap)) { d.error = "AcDb:Handles section missing"; return false; }
    readSection("AcDb:Classes", d.classes);
    ParseObjectMap(d.objectMap, d);
    return true;
}

// ---- R2007 -----------------------------------------------------------------

bool LoadR2007(const std::string& file, DrawingData& d) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(file.data());
    const size_t fileSize = file.size();
    if (fileSize < 0x80 + 0x3d8 + 0x28) { d.error = "file too short"; return false; }
    d.maintVersion = bytes[0x12];

    // File header: 3 RS blocks of 239 payload bytes.
    std::vector<uint8_t> pedata = DecodeRS(bytes + 0x80, 0x3d8, 3, 239);
    int32_t comprLen = static_cast<int32_t>(LE32(&pedata[24]));
    std::vector<uint8_t> hdr(0x110, 0);
    if (comprLen > 0) {
        size_t n = std::min<size_t>(static_cast<size_t>(comprLen), pedata.size() - 32);
        if (!DecompressR2007(&pedata[32], n, hdr)) { d.error = "R2007 header decompression failed"; return false; }
    } else {
        std::memcpy(hdr.data(), &pedata[32], std::min<size_t>(hdr.size(), pedata.size() - 32));
    }
    auto F = [&](int idx) -> int64_t { return static_cast<int64_t>(LE64(&hdr[idx * 8])); };
    int64_t pagesMapCorrection = F(3);
    int64_t pagesMapOffset = F(7);
    int64_t pagesMapSizeComp = F(10);
    int64_t pagesMapSizeUncomp = F(11);
    int64_t sectionsMapSizeComp = F(22);
    int64_t sectionsMapId = F(24);
    int64_t sectionsMapSizeUncomp = F(25);
    int64_t sectionsMapCorrection = F(27);

    auto readSystemPage = [&](uint64_t address, int64_t sizeComp, int64_t sizeUncomp,
                              int64_t repeat, std::vector<uint8_t>& out) -> bool {
        if (sizeComp <= 0 || sizeUncomp <= 0 || repeat <= 0 || repeat > 0x100000) return false;
        if (static_cast<uint64_t>(sizeUncomp) > 0x4000000) return false;
        int64_t pesize = ((sizeComp + 7) & ~7) * repeat;
        int64_t blockCount = (pesize + 238) / 239;
        int64_t pageSize = (blockCount * 255 + 7) & ~7;
        if (address + pageSize > fileSize) return false;
        std::vector<uint8_t> pe = DecodeRS(bytes + address, static_cast<size_t>(pageSize),
                                           static_cast<size_t>(blockCount), 239);
        out.assign(static_cast<size_t>(sizeUncomp), 0);
        if (sizeComp < sizeUncomp) {
            return DecompressR2007(pe.data(), std::min<size_t>(pe.size(), static_cast<size_t>(sizeComp)), out);
        }
        std::memcpy(out.data(), pe.data(), std::min<size_t>(out.size(), pe.size()));
        return true;
    };

    // Pages map.
    std::vector<uint8_t> pmap;
    uint64_t pmAddr = 0x80 + 0x3d8 + 0x28 + static_cast<uint64_t>(pagesMapOffset);
    if (!readSystemPage(pmAddr, pagesMapSizeComp, pagesMapSizeUncomp, pagesMapCorrection, pmap)) {
        d.error = "R2007 pages map unreadable";
        return false;
    }
    struct Page { uint64_t size, offset; };
    std::map<int64_t, Page> pages;
    {
        uint64_t offset = 0x480;
        for (size_t p = 0; p + 16 <= pmap.size(); p += 16) {
            uint64_t size = LE64(&pmap[p]);
            int64_t id = static_cast<int64_t>(LE64(&pmap[p + 8]));
            pages[id] = Page{size, offset};
            offset += size;
        }
    }
    auto pageIt = pages.find(sectionsMapId);
    if (pageIt == pages.end()) { d.error = "R2007 sections map page missing"; return false; }
    std::vector<uint8_t> smap;
    if (!readSystemPage(pageIt->second.offset, sectionsMapSizeComp, sectionsMapSizeUncomp,
                        sectionsMapCorrection, smap)) {
        d.error = "R2007 sections map unreadable";
        return false;
    }
    struct SecPage { uint64_t offset, size; int64_t id; uint64_t uncomp, comp; };
    struct Section { uint64_t dataSize; std::string name; std::vector<SecPage> pages; };
    std::vector<Section> sections;
    {
        size_t p = 0;
        while (p + 64 <= smap.size()) {
            Section s;
            s.dataSize = LE64(&smap[p]);
            uint64_t nameLength = LE64(&smap[p + 32]);
            uint64_t numPages = LE64(&smap[p + 56]);
            p += 64;
            if (nameLength & 1) ++nameLength;
            if (nameLength > 96 || p + nameLength > smap.size()) break;
            std::vector<uint16_t> w;
            for (uint64_t k = 0; k + 1 < nameLength; k += 2) w.push_back(static_cast<uint16_t>(smap[p + k] | (smap[p + k + 1] << 8)));
            s.name = Utf16ToUtf8(w);
            p += nameLength;
            if (numPages > 0xf0000) break;
            for (uint64_t k = 0; k < numPages && p + 56 <= smap.size(); ++k) {
                SecPage sp;
                sp.offset = LE64(&smap[p]);
                sp.size = LE64(&smap[p + 8]);
                sp.id = static_cast<int64_t>(LE64(&smap[p + 16]));
                sp.uncomp = LE64(&smap[p + 24]);
                sp.comp = LE64(&smap[p + 32]);
                p += 56;
                s.pages.push_back(sp);
            }
            sections.push_back(std::move(s));
        }
    }

    auto readSection = [&](const std::string& name, std::vector<uint8_t>& out) -> bool {
        const Section* s = nullptr;
        for (const auto& x : sections) if (x.name == name) { s = &x; break; }
        if (!s || s->dataSize == 0 || s->dataSize > 0x2f000000) return false;
        out.assign(static_cast<size_t>(s->dataSize), 0);
        for (const auto& sp : s->pages) {
            auto it = pages.find(sp.id);
            if (it == pages.end()) return false;
            if (sp.offset > s->dataSize || sp.uncomp > s->dataSize - sp.offset) return false;
            uint64_t fileOff = it->second.offset;
            uint64_t pageSize = it->second.size;
            if (fileOff + pageSize > fileSize) return false;
            int64_t pesize = (static_cast<int64_t>(sp.comp) + 7) & ~7;
            int64_t blockCount = (pesize + 0xFB - 1) / 0xFB;
            int64_t rsPageSize = (blockCount * 0xFF + 31) & ~31;
            bool rsCoded = sp.comp != sp.uncomp || static_cast<int64_t>(pageSize) == rsPageSize;
            if (rsCoded) {
                std::vector<uint8_t> pe = DecodeRS(bytes + fileOff, static_cast<size_t>(pageSize),
                                                   static_cast<size_t>(blockCount), 0xFB);
                if (sp.comp < sp.uncomp) {
                    std::vector<uint8_t> dec(static_cast<size_t>(sp.uncomp), 0);
                    if (!DecompressR2007(pe.data(), std::min<size_t>(pe.size(), static_cast<size_t>(sp.comp)), dec)) {
                        return false;
                    }
                    std::memcpy(&out[sp.offset], dec.data(), dec.size());
                } else {
                    std::memcpy(&out[sp.offset], pe.data(), std::min<size_t>(pe.size(), static_cast<size_t>(sp.uncomp)));
                }
            } else {
                if (fileOff + sp.uncomp > fileSize) return false;
                std::memcpy(&out[sp.offset], bytes + fileOff, static_cast<size_t>(sp.uncomp));
            }
        }
        return true;
    };

    if (!readSection("AcDb:AcDbObjects", d.objects)) { d.error = "AcDb:AcDbObjects section missing"; return false; }
    if (!readSection("AcDb:Handles", d.objectMap)) { d.error = "AcDb:Handles section missing"; return false; }
    readSection("AcDb:Classes", d.classes);
    ParseObjectMap(d.objectMap, d);
    return true;
}


// ===========================================================================
// 5. Object decoding
// ===========================================================================

// Fixed object types (the CLASSES table maps everything >= 500).
struct FixedType { uint16_t type; const char* name; bool entity; };
const FixedType kFixedTypes[] = {
    {1, "TEXT", true}, {2, "ATTRIB", true}, {3, "ATTDEF", true}, {4, "BLOCK", true},
    {5, "ENDBLK", true}, {6, "SEQEND", true}, {7, "INSERT", true}, {8, "MINSERT", true},
    {10, "VERTEX_2D", true}, {11, "VERTEX_3D", true}, {12, "VERTEX_MESH", true},
    {13, "VERTEX_PFACE", true}, {14, "VERTEX_PFACE_FACE", true}, {15, "POLYLINE_2D", true},
    {16, "POLYLINE_3D", true}, {17, "ARC", true}, {18, "CIRCLE", true}, {19, "LINE", true},
    {20, "DIMENSION_ORDINATE", true}, {21, "DIMENSION_LINEAR", true},
    {22, "DIMENSION_ALIGNED", true}, {23, "DIMENSION_ANG3PT", true},
    {24, "DIMENSION_ANG2LN", true}, {25, "DIMENSION_RADIUS", true},
    {26, "DIMENSION_DIAMETER", true}, {27, "POINT", true}, {28, "3DFACE", true},
    {29, "POLYLINE_PFACE", true}, {30, "POLYLINE_MESH", true}, {31, "SOLID", true},
    {32, "TRACE", true}, {33, "SHAPE", true}, {34, "VIEWPORT", true}, {35, "ELLIPSE", true},
    {36, "SPLINE", true}, {37, "REGION", true}, {38, "3DSOLID", true}, {39, "BODY", true},
    {40, "RAY", true}, {41, "XLINE", true}, {42, "DICTIONARY", false}, {43, "OLEFRAME", true},
    {44, "MTEXT", true}, {45, "LEADER", true}, {46, "TOLERANCE", true}, {47, "MLINE", true},
    {48, "BLOCK_CONTROL", false}, {49, "BLOCK_HEADER", false}, {50, "LAYER_CONTROL", false},
    {51, "LAYER", false}, {52, "STYLE_CONTROL", false}, {53, "STYLE", false},
    {56, "LTYPE_CONTROL", false}, {57, "LTYPE", false}, {60, "VIEW_CONTROL", false},
    {61, "VIEW", false}, {62, "UCS_CONTROL", false}, {63, "UCS", false},
    {64, "VPORT_CONTROL", false}, {65, "VPORT", false}, {66, "APPID_CONTROL", false},
    {67, "APPID", false}, {68, "DIMSTYLE_CONTROL", false}, {69, "DIMSTYLE", false},
    {70, "VX_CONTROL", false}, {71, "VX", false}, {72, "GROUP", false},
    {73, "MLINESTYLE", false}, {74, "OLE2FRAME", true}, {75, "DUMMY", false},
    {76, "LONG_TRANSACTION", false}, {77, "LWPOLYLINE", true}, {78, "HATCH", true},
    {79, "XRECORD", false}, {80, "PLACEHOLDER", false}, {81, "VBA_PROJECT", false},
    {82, "LAYOUT", false}, {498, "ACAD_PROXY_ENTITY", true}, {499, "ACAD_PROXY_OBJECT", false},
};

// Fixed types whose data stream carries strings (R2007+ string stream).
bool FixedTypeHasStrings(uint16_t t) {
    switch (t) {
        case 5: case 6: case 7: case 8: case 10: case 11: case 12: case 13: case 14:
        case 15: case 16: case 17: case 18: case 19: case 27: case 28: case 29: case 30:
        case 31: case 32: case 33: case 35: case 36: case 40: case 41: case 47:
        case 48: case 50: case 52: case 56: case 60: case 62: case 64: case 66: case 68:
        case 70: case 75: case 76: case 77: case 80:
            return false;
        default:
            return true;
    }
}

struct ClassInfo {
    std::string dxfname;
    bool entity = false;
};

struct Ent {
    uint64_t handle = 0;
    std::string type;               // DXF entity name
    int entmode = 0;
    uint64_t owner = 0;
    uint64_t layer = 0;
    int ltypeFlags = 0;             // 0 bylayer, 1 byblock, 2 continuous, 3 handle
    uint64_t ltype = 0;
    int color = 256;
    bool hasRgb = false;
    uint32_t rgb = 0;
    int linewt = 29;                // DWG code: 29 bylayer
    bool invisible = false;
    uint64_t prev = 0, next = 0;    // R13-R2000 entity links
    uint64_t style = 0;             // TEXT/ATTRIB/MTEXT
    uint64_t block = 0;             // INSERT/DIMENSION
    std::vector<uint64_t> owned;    // R2004+ vertices / attribs
    uint64_t firstOwned = 0, lastOwned = 0;
    bool hasAttribs = false;
    std::string body;               // entity-specific DXF tags
};

struct LayerRec {
    std::string name;
    int color = 7;
    bool hasRgb = false;
    uint32_t rgb = 0;
    bool off = false, frozen = false, plot = true;
    int linewt = 31;
    uint64_t ltype = 0;
};
struct LtypeRec { std::string name; std::vector<double> dashes; };
struct StyleRec { std::string name; std::string font; };
struct BlockRec {
    std::string name;
    double bx = 0, by = 0, bz = 0;
    int flags = 0;
    bool xref = false;
    std::vector<uint64_t> entities;   // R2004+
    uint64_t first = 0, last = 0;     // R13-R2000
    uint64_t blockEntity = 0, endblk = 0;
};

const int kLineweights[] = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50,
                            53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};

int DwgLineweightToDxf(int code) {
    if (code >= 0 && code < 24) return kLineweights[code];
    if (code == 29) return -1;
    if (code == 30) return -2;
    return -3;
}

double Deg(double rad) { return rad * 180.0 / kPi; }

// DXF text emitter.
class DxfOut {
public:
    void Tag(int code, const std::string& v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%3d\n", code);
        s += buf;
        s += v;
        s += '\n';
    }
    void Tag(int code, int v) { Tag(code, std::to_string(v)); }
    void Tag(int code, double v) {
        if (!std::isfinite(v)) v = 0;
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.16g", v);
        Tag(code, std::string(buf));
    }
    void Pt(int code, double x, double y) { Tag(code, x); Tag(code + 10, y); }
    void Pt(int code, double x, double y, double z) { Tag(code, x); Tag(code + 10, y); Tag(code + 20, z); }
    void Append(const std::string& raw) { s += raw; }
    std::string& Str() { return s; }
private:
    std::string s;
};

std::string OneLine(std::string s) {
    for (char& c : s) if (c == '\n' || c == '\r') c = ' ';
    return s;
}

class Decoder {
public:
    Decoder(DrawingData& d, DWGDecodeResult& res, const std::function<void(const std::string&)>& w)
            : data(d), ver(d.ver), result(res), warn(w) {}

    void Run() {
        ParseClasses();
        for (const auto& [handle, offset] : data.map) {
            (void)handle;
            DecodeObject(offset);
        }
        Assemble();
    }

private:
    DrawingData& data;
    Ver ver;
    DWGDecodeResult& result;
    const std::function<void(const std::string&)>& warn;

    std::map<uint16_t, ClassInfo> classes;
    std::map<uint64_t, Ent> ents;             // all entities by handle
    std::vector<uint64_t> entOrder;           // map order
    std::map<uint64_t, LayerRec> layers;
    std::map<uint64_t, LtypeRec> ltypes;
    std::map<uint64_t, StyleRec> styles;
    std::map<uint64_t, BlockRec> blocks;
    std::map<std::string, int> skippedTypes;
    unsigned unreadable = 0;

    // ----- CLASSES -----------------------------------------------------------

    void ParseClasses() {
        const auto& c = data.classes;
        if (c.size() < 20) return;
        static const uint8_t sentinel[16] = {0x8D, 0xA1, 0xC4, 0xB8, 0xC4, 0xA9, 0xF8, 0xC5,
                                             0xC0, 0xDC, 0xF4, 0x5F, 0xE7, 0xCF, 0xB6, 0x8A};
        size_t start = 0;
        bool found = false;
        for (size_t i = 0; i + 16 <= c.size() && i < 4096; ++i) {
            if (std::memcmp(&c[i], sentinel, 16) == 0) { start = i; found = true; break; }
        }
        BitReader r(c.data(), c.size(), ver);
        if (found) r.SetPos((start + 16) * 8);
        uint32_t size = r.RL();
        if (ver < Ver::R2004) {
            size_t endBit = r.Pos() + static_cast<size_t>(size) * 8;
            if (endBit > r.SizeBits()) endBit = r.SizeBits();
            while (r.Pos() + 16 < endBit && !r.Overrun()) {
                uint16_t number = r.BS();
                r.BS();                       // proxy flags
                r.TV();                       // application name
                r.TV();                       // C++ class name
                std::string dxf = r.TV();
                r.B();                        // was a proxy
                uint16_t itemClass = r.BS();
                if (r.Overrun()) break;
                classes[number] = ClassInfo{dxf, itemClass == 0x1f2};
            }
            return;
        }
        bool hasHsize = (ver >= Ver::R2010 && data.maintVersion > 3) || ver >= Ver::R2018;
        if (hasHsize) r.RL();
        uint32_t bitsize = 0;
        if (ver >= Ver::R2007) bitsize = r.RL();
        uint16_t maxNum = r.BS();
        r.RC(); r.RC(); r.B();
        if (maxNum < 500 || maxNum > 5000) return;
        BitReader str;
        if (ver >= Ver::R2007) {
            // Section string stream: flag bit just before the handle area.
            str = BitReader(c.data(), c.size(), ver);
            size_t startBit = start * 8 + bitsize + (hasHsize ? 191 : 159);
            str.SetPos(startBit);
            if (str.B()) {
                startBit -= 16;
                str.SetPos(startBit);
                uint32_t dataSize = str.RS();
                if (dataSize & 0x8000) {
                    startBit -= 16;
                    str.SetPos(startBit);
                    uint32_t hi = str.RS();
                    dataSize = (dataSize & 0x7fff) | (hi << 15);
                }
                startBit -= dataSize;
                str.SetPos(startBit);
            } else {
                str = BitReader();
            }
        }
        unsigned n = maxNum - 499;
        for (unsigned i = 0; i < n && !r.Overrun(); ++i) {
            uint16_t number = r.BS();
            r.BS();
            std::string dxf;
            if (ver >= Ver::R2007) {
                if (str.Valid()) { str.TU(); str.TU(); dxf = str.TU(); }
            } else {
                r.TV(); r.TV(); dxf = r.TV();
            }
            r.B();
            uint16_t itemClass = r.BS();
            r.BL(); r.BS(); r.BS(); r.BL(); r.BL();
            if (r.Overrun()) break;
            classes[number] = ClassInfo{dxf, itemClass == 0x1f2};
        }
    }

    // ----- Object framing ----------------------------------------------------

    struct Streams {
        BitReader dat, hdl, str;
        size_t bitsize = 0;
    };

    static void SetupStringStream(Streams& s) {
        size_t bitsize = s.bitsize;
        if (bitsize < 17) return;
        BitReader str = s.dat;
        str.SetPos(bitsize - 1);
        if (!str.B()) return;
        size_t startBit = bitsize - 17;
        str.SetPos(startBit);
        uint32_t dataSize = str.RS();
        if (dataSize & 0x8000) {
            startBit = bitsize - 33;
            str.SetPos(startBit);
            uint32_t hi = str.RS();
            dataSize = (dataSize & 0x7fff) | (hi << 15);
        }
        if (dataSize > startBit) return;
        str.SetPos(startBit - dataSize);
        s.str = str;
    }

    uint64_t Abs(const Handle& h, uint64_t own) const {
        switch (h.code) {
            case 6: return own + 1;
            case 8: return own - 1;
            case 10: return own + h.value;
            case 12: return own - h.value;
            default: return h.value;
        }
    }

    void DecodeObject(uint64_t offset) {
        const auto& img = data.objects;
        if (offset + 2 > img.size()) return;
        BitReader f(img.data(), img.size(), ver);
        f.SetPos(offset * 8);
        uint32_t size = f.MS();
        uint64_t hss = 0;
        if (ver >= Ver::R2010) hss = f.UMC();
        if (f.Overrun() || size == 0) return;
        size_t objStart = f.Pos() / 8;
        if (objStart + size > img.size()) return;

        Streams s;
        s.dat = BitReader(img.data() + objStart, size, ver);
        uint16_t type = ver >= Ver::R2010 ? s.dat.BOT() : s.dat.BS();

        std::string name;
        bool isEntity = false;
        bool known = false;
        if (type >= 500) {
            auto it = classes.find(type);
            if (it != classes.end()) { name = it->second.dxfname; isEntity = it->second.entity; known = true; }
        } else {
            for (const auto& ft : kFixedTypes) {
                if (ft.type == type) { name = ft.name; isEntity = ft.entity; known = true; break; }
            }
        }
        if (!known) return;

        if (ver >= Ver::R2000 && ver <= Ver::R2007) {
            s.bitsize = s.dat.RL();
        } else if (ver >= Ver::R2010) {
            if (hss > static_cast<uint64_t>(size) * 8) return;
            s.bitsize = size * 8 - static_cast<size_t>(hss);
        }
        if (ver >= Ver::R2007) {
            bool hasStrings = type >= 500 || FixedTypeHasStrings(type);
            if (hasStrings) SetupStringStream(s);
        }
        s.hdl = s.dat;
        if (s.bitsize) s.hdl.SetPos(s.bitsize);

        Handle handle = s.dat.H();
        if (handle.value == 0 || s.dat.Overrun()) return;

        // Extended entity data.
        while (true) {
            uint16_t eedSize = s.dat.BS();
            if (!eedSize || s.dat.Overrun()) break;
            s.dat.H();
            s.dat.Skip(eedSize);
        }
        if (s.dat.Overrun()) { ++unreadable; return; }

        if (isEntity) DecodeEntity(s, handle.value, type, name, size);
        else DecodeNonEntity(s, handle.value, type, name);
    }

    // ----- Common entity data ------------------------------------------------

    void DecodeEntity(Streams& s, uint64_t handle, uint16_t type, const std::string& name,
                      uint32_t size) {
        BitReader& dat = s.dat;
        Ent e;
        e.handle = handle;

        if (dat.B()) {   // preview / proxy graphics present
            uint64_t n = ver >= Ver::R2010 ? dat.BLL() : dat.RL();
            if (n > size) { ++unreadable; return; }
            dat.Skip(static_cast<size_t>(n));
        }
        if (ver <= Ver::R14) {
            s.bitsize = dat.RL();
            if (s.bitsize > static_cast<size_t>(size) * 8) { ++unreadable; return; }
            s.hdl = dat;
            s.hdl.SetPos(s.bitsize);
        }
        e.entmode = dat.BB();
        uint32_t numReactors = dat.BL();
        bool isbylayerlt = false, xdicMissing = false, nolinks = false;
        if (ver <= Ver::R14) isbylayerlt = dat.B();
        if (ver >= Ver::R2004) xdicMissing = dat.B();
        if (ver <= Ver::R2000) nolinks = dat.B();
        if (ver >= Ver::R2013) dat.B();   // has_ds_data

        bool colorHandle = false;
        if (ver >= Ver::R2004) {
            uint16_t raw = dat.BS();
            e.color = raw & 0x1ff;
            unsigned flags = raw >> 8;
            if (flags & 0x80) { e.rgb = dat.BL() & 0xffffff; e.hasRgb = true; }
            if (flags & 0x40) colorHandle = true;
            if (flags & 0x20) dat.BL();   // transparency
            if ((flags & 0x41) == 0x41) dat.T(s.str);
            if ((flags & 0x42) == 0x42) dat.T(s.str);
        } else {
            e.color = dat.BS();
        }
        dat.BD();   // linetype scale
        int plotstyleFlags = 0, materialFlags = 0, shadowFlags = 0;
        bool vs1 = false, vs2 = false, vs3 = false;
        if (ver >= Ver::R2000) {
            e.ltypeFlags = dat.BB();
            plotstyleFlags = dat.BB();
        } else {
            e.ltypeFlags = isbylayerlt ? 0 : 3;
        }
        if (ver >= Ver::R2007) {
            materialFlags = dat.BB();
            shadowFlags = dat.RC();
        }
        if (ver >= Ver::R2010) {
            vs1 = dat.B(); vs2 = dat.B(); vs3 = dat.B();
        }
        e.invisible = (dat.BS() & 1) != 0;
        if (ver >= Ver::R2000) e.linewt = dat.RC();
        if (dat.Overrun()) { ++unreadable; return; }

        // Entity-specific data. An unsupported or unreadable entity keeps
        // its common handles (owner, links) so the R13-R2000 entity chains
        // stay intact; it is stored with an empty type and never emitted.
        bool wanted = ParseEntityBody(e, s, type, name);
        bool bodyOk = wanted && !dat.Overrun();
        if (wanted && !bodyOk) ++unreadable;
        if (!bodyOk) { e.type.clear(); e.body.clear(); }

        // Handle stream.
        BitReader& h = s.hdl;
        if (colorHandle) h.H();
        if (e.entmode == 0) e.owner = Abs(h.H(), handle);
        if (numReactors > 10000) { ++unreadable; return; }
        for (uint32_t i = 0; i < numReactors; ++i) h.H();
        if (ver < Ver::R2004 || !xdicMissing) h.H();
        if (ver <= Ver::R14) {
            e.layer = Abs(h.H(), handle);
            if (!isbylayerlt) e.ltype = Abs(h.H(), handle);
        }
        if (ver <= Ver::R2000) {
            if (!nolinks) {
                e.prev = Abs(h.H(), handle);
                e.next = Abs(h.H(), handle);
            } else {
                e.prev = handle - 1;
                e.next = handle + 1;
            }
        }
        if (ver >= Ver::R2000) {
            e.layer = Abs(h.H(), handle);
            if (e.ltypeFlags == 3) e.ltype = Abs(h.H(), handle);
        }
        if (ver >= Ver::R2007) {
            if (materialFlags == 3) h.H();
            if (shadowFlags == 3) h.H();
        }
        if (ver >= Ver::R2000 && plotstyleFlags == 3) h.H();
        if (ver >= Ver::R2010) {
            if (vs1) h.H();
            if (vs2) h.H();
            if (vs3) h.H();
        }
        if (bodyOk) ParseEntityHandles(e, s, type, name);

        entOrder.push_back(handle);
        ents[handle] = std::move(e);
    }

    // Reads the entity-specific data stream fields into e.body. Returns
    // false for entity types that are not emitted.
    bool ParseEntityBody(Ent& e, Streams& s, uint16_t type, const std::string& name) {
        BitReader& d = s.dat;
        DxfOut o;
        auto pt3 = [&](int code) { double x = d.BD(), y = d.BD(), z = d.BD(); o.Pt(code, x, y, z); };
        auto pt2 = [&](int code) { double x = d.RD(), y = d.RD(); o.Pt(code, x, y); };
        auto ext = [&](int code) {   // bit-extrusion (BE)
            double x, y, z;
            d.BE(x, y, z);
            if (x != 0 || y != 0 || z != 1) o.Pt(code, x, y, z);
        };
        auto ext3 = [&](int code) {  // plain 3BD extrusion
            double x = d.BD(), y = d.BD(), z = d.BD();
            if (x != 0 || y != 0 || z != 1) o.Pt(code, x, y, z);
        };
        const std::string& n = type >= 500 ? name : name;

        if (n == "LINE") {
            e.type = "LINE";
            double x1, y1, z1 = 0, x2, y2, z2 = 0;
            if (ver <= Ver::R14) {
                x1 = d.BD(); y1 = d.BD(); z1 = d.BD();
                x2 = d.BD(); y2 = d.BD(); z2 = d.BD();
            } else {
                bool zZero = d.B();
                x1 = d.RD(); x2 = d.DD(x1);
                y1 = d.RD(); y2 = d.DD(y1);
                if (!zZero) { z1 = d.RD(); z2 = d.DD(z1); }
            }
            o.Pt(10, x1, y1, z1);
            o.Pt(11, x2, y2, z2);
            d.BT();
            ext(210);
        } else if (n == "POINT") {
            e.type = "POINT";
            pt3(10);
            d.BT();
            ext(210);
        } else if (n == "CIRCLE") {
            e.type = "CIRCLE";
            pt3(10);
            o.Tag(40, d.BD());
            d.BT();
            ext(210);
        } else if (n == "ARC") {
            e.type = "ARC";
            pt3(10);
            o.Tag(40, d.BD());
            d.BT();
            ext(210);
            o.Tag(50, Deg(d.BD()));
            o.Tag(51, Deg(d.BD()));
        } else if (n == "ELLIPSE") {
            e.type = "ELLIPSE";
            pt3(10);
            pt3(11);
            ext3(210);
            o.Tag(40, d.BD());
            o.Tag(41, d.BD());
            o.Tag(42, d.BD());
        } else if (n == "SOLID" || n == "TRACE") {
            e.type = "SOLID";
            d.BT();
            double elev = d.BD();
            for (int i = 0; i < 4; ++i) {
                double x = d.RD(), y = d.RD();
                o.Pt(10 + i, x, y, elev);
            }
            ext(210);
        } else if (n == "3DFACE") {
            e.type = "3DFACE";
            if (ver <= Ver::R14) {
                for (int i = 0; i < 4; ++i) pt3(10 + i);
                o.Tag(70, static_cast<int>(d.BS()));
            } else {
                bool noFlags = d.B();
                bool zZero = d.B();
                double x = d.RD(), y = d.RD(), z = zZero ? 0.0 : d.RD();
                o.Pt(10, x, y, z);
                for (int i = 1; i < 4; ++i) {
                    x = d.DD(x); y = d.DD(y); z = d.DD(z);
                    o.Pt(10 + i, x, y, z);
                }
                if (!noFlags) o.Tag(70, static_cast<int>(d.BS()));
            }
        } else if (n == "TEXT" || n == "ATTRIB" || n == "ATTDEF") {
            if (n == "ATTDEF") return false;
            e.type = n;
            ParseTextBody(s, o);
        } else if (n == "MTEXT") {
            e.type = "MTEXT";
            pt3(10);
            double ex = d.BD(), ey = d.BD(), ez = d.BD();
            if (ex != 0 || ey != 0 || ez != 1) o.Pt(210, ex, ey, ez);
            double xx = d.BD(), xy = d.BD(), xz = d.BD();
            o.Pt(11, xx, xy, xz);
            o.Tag(41, d.BD());                     // rect width
            if (ver >= Ver::R2007) o.Tag(46, d.BD());
            o.Tag(40, d.BD());                     // text height
            o.Tag(71, static_cast<int>(d.BS()));   // attachment
            o.Tag(72, static_cast<int>(d.BS()));   // flow direction
            d.BD(); d.BD();                        // extents
            std::string text = d.T(s.str);
            EmitLongText(o, text);
        } else if (n == "INSERT" || n == "MINSERT") {
            e.type = "INSERT";
            pt3(10);
            double sx = 1, sy = 1, sz = 1;
            if (ver <= Ver::R14) {
                sx = d.BD(); sy = d.BD(); sz = d.BD();
            } else {
                switch (d.BB()) {
                    case 3: break;
                    case 1: sy = d.DD(1.0); sz = d.DD(1.0); break;
                    case 2: sx = d.RD(); sy = sz = sx; break;
                    default: sx = d.RD(); sy = d.DD(sx); sz = d.DD(sx); break;
                }
            }
            o.Tag(41, sx); o.Tag(42, sy); o.Tag(43, sz);
            o.Tag(50, Deg(d.BD()));
            ext3(210);
            e.hasAttribs = d.B();
            if (e.hasAttribs) o.Tag(66, 1);
            uint32_t numOwned = 0;
            if (ver >= Ver::R2004 && e.hasAttribs) numOwned = d.BL();
            if (n == "MINSERT") {
                o.Tag(70, static_cast<int>(d.BS()));
                o.Tag(71, static_cast<int>(d.BS()));
                o.Tag(44, d.BD());
                o.Tag(45, d.BD());
            }
            e.owned.resize(std::min<uint32_t>(numOwned, 100000));
        } else if (n == "LWPOLYLINE") {
            e.type = "LWPOLYLINE";
            unsigned flag = d.BS();
            double constWidth = 0, elevation = 0;
            if (flag & 4) constWidth = d.BD();
            if (flag & 8) elevation = d.BD();
            if (flag & 2) d.BD();   // thickness
            double ex = 0, ey = 0, ez = 1;
            if (flag & 1) { ex = d.BD(); ey = d.BD(); ez = d.BD(); }
            uint32_t numPoints = d.BL();
            uint32_t numBulges = 0, numIds = 0, numWidths = 0;
            if (flag & 16) numBulges = d.BL();
            if (ver >= Ver::R2010 && (flag & 1024)) numIds = d.BL();
            if (flag & 32) numWidths = d.BL();
            if (numPoints > 200000 || numBulges > numPoints || numWidths > numPoints || d.Overrun()) return false;
            std::vector<double> xs(numPoints), ys(numPoints);
            if (ver <= Ver::R14) {
                for (uint32_t i = 0; i < numPoints; ++i) { xs[i] = d.RD(); ys[i] = d.RD(); }
            } else if (numPoints) {
                xs[0] = d.RD(); ys[0] = d.RD();
                for (uint32_t i = 1; i < numPoints; ++i) { xs[i] = d.DD(xs[i - 1]); ys[i] = d.DD(ys[i - 1]); }
            }
            std::vector<double> bulges(numBulges);
            for (auto& b : bulges) b = d.BD();
            for (uint32_t i = 0; i < numIds; ++i) d.BL();
            std::vector<std::pair<double, double>> widths(numWidths);
            for (auto& w : widths) { w.first = d.BD(); w.second = d.BD(); }
            o.Tag(90, static_cast<int>(numPoints));
            o.Tag(70, ((flag & 512) ? 1 : 0) | ((flag & 256) ? 128 : 0));
            if (flag & 4) o.Tag(43, constWidth);
            if (flag & 8) o.Tag(38, elevation);
            for (uint32_t i = 0; i < numPoints; ++i) {
                o.Pt(10, xs[i], ys[i]);
                if (i < numWidths) { o.Tag(40, widths[i].first); o.Tag(41, widths[i].second); }
                if (i < numBulges && bulges[i] != 0.0) o.Tag(42, bulges[i]);
            }
            if (flag & 1) o.Pt(210, ex, ey, ez);
        } else if (n == "POLYLINE_2D") {
            e.type = "POLYLINE";
            unsigned flag = d.BS();
            unsigned curve = d.BS();
            double sw = d.BD(), ew = d.BD();
            d.BT();
            double elev = d.BD();
            o.Tag(66, 1);
            o.Pt(10, 0.0, 0.0, elev);
            o.Tag(70, static_cast<int>(flag));
            if (sw != 0) o.Tag(40, sw);
            if (ew != 0) o.Tag(41, ew);
            if (curve) o.Tag(75, static_cast<int>(curve));
            ext(210);
            if (ver >= Ver::R2004) e.owned.resize(std::min<uint32_t>(d.BL(), 1000000));
        } else if (n == "POLYLINE_3D") {
            e.type = "POLYLINE";
            unsigned curve = d.RC();
            unsigned flag = d.RC();
            o.Tag(66, 1);
            o.Pt(10, 0.0, 0.0, 0.0);
            o.Tag(70, static_cast<int>(flag | 8));
            if (curve) o.Tag(75, static_cast<int>(curve));
            if (ver >= Ver::R2004) e.owned.resize(std::min<uint32_t>(d.BL(), 1000000));
        } else if (n == "POLYLINE_PFACE") {
            e.type = "POLYLINE";
            unsigned numVerts = d.BS();
            unsigned numFaces = d.BS();
            o.Tag(66, 1);
            o.Pt(10, 0.0, 0.0, 0.0);
            o.Tag(70, 64);
            o.Tag(71, static_cast<int>(numVerts));
            o.Tag(72, static_cast<int>(numFaces));
            if (ver >= Ver::R2004) e.owned.resize(std::min<uint32_t>(d.BL(), 1000000));
        } else if (n == "POLYLINE_MESH") {
            e.type = "POLYLINE";
            unsigned flag = d.BS();
            unsigned curve = d.BS();
            unsigned m = d.BS(), nn = d.BS();
            d.BS(); d.BS();   // densities
            o.Tag(66, 1);
            o.Pt(10, 0.0, 0.0, 0.0);
            o.Tag(70, static_cast<int>(flag | 16));
            o.Tag(71, static_cast<int>(m));
            o.Tag(72, static_cast<int>(nn));
            if (curve) o.Tag(75, static_cast<int>(curve));
            if (ver >= Ver::R2004) e.owned.resize(std::min<uint32_t>(d.BL(), 1000000));
        } else if (n == "VERTEX_MESH" || n == "VERTEX_PFACE") {
            e.type = "VERTEX";
            d.RC();
            pt3(10);
            o.Tag(70, n == "VERTEX_MESH" ? 64 : 192);
        } else if (n == "VERTEX_PFACE_FACE") {
            e.type = "VERTEX";
            o.Pt(10, 0.0, 0.0, 0.0);
            o.Tag(70, 128);
            for (int k = 0; k < 4; ++k) o.Tag(71 + k, static_cast<int>(static_cast<int16_t>(d.BS())));
        } else if (n == "VERTEX_2D") {
            e.type = "VERTEX";
            unsigned flag = d.RC();
            pt3(10);
            double sw = d.BD(), ew;
            if (sw < 0) { sw = -sw; ew = sw; } else ew = d.BD();
            double bulge = d.BD();
            if (ver >= Ver::R2010) d.BL();
            d.BD();   // tangent direction
            if (sw != 0) o.Tag(40, sw);
            if (ew != 0) o.Tag(41, ew);
            if (bulge != 0) o.Tag(42, bulge);
            o.Tag(70, static_cast<int>(flag));
        } else if (n == "VERTEX_3D") {
            e.type = "VERTEX";
            unsigned flag = d.RC();
            pt3(10);
            o.Tag(70, static_cast<int>(flag | 32));
        } else if (n == "SPLINE") {
            e.type = "SPLINE";
            uint32_t scenario = d.BL();
            uint32_t splineflags = 0;
            if (ver >= Ver::R2013) {
                splineflags = d.BL();
                uint32_t knotparam = d.BL();
                if (splineflags & 1) scenario = 2;
                if (knotparam == 15) scenario = 1;
            }
            uint32_t degree = d.BL();
            if (scenario & 1) {
                bool rational = d.B();
                bool closed = d.B();
                bool periodic = d.B();
                d.BD(); d.BD();   // tolerances
                uint32_t numKnots = d.BL();
                uint32_t numCtrl = d.BL();
                bool weighted = d.B();
                if (numKnots > 100000 || numCtrl > 100000 || d.Overrun()) return false;
                o.Tag(70, (closed ? 1 : 0) | (periodic ? 2 : 0) | (rational ? 4 : 0) | 8);
                o.Tag(71, static_cast<int>(degree));
                o.Tag(72, static_cast<int>(numKnots));
                o.Tag(73, static_cast<int>(numCtrl));
                for (uint32_t i = 0; i < numKnots; ++i) o.Tag(40, d.BD());
                for (uint32_t i = 0; i < numCtrl; ++i) {
                    pt3(10);
                    if (weighted) o.Tag(41, d.BD());
                }
            } else {
                o.Tag(70, 8 | (splineflags & 1 ? 1 : 0));
                o.Tag(71, static_cast<int>(degree));
                d.BD();           // fit tolerance
                double tx = d.BD(), ty = d.BD(), tz = d.BD();
                o.Pt(12, tx, ty, tz);
                tx = d.BD(); ty = d.BD(); tz = d.BD();
                o.Pt(13, tx, ty, tz);
                uint32_t numFit = d.BL();
                if (numFit > 100000 || d.Overrun()) return false;
                o.Tag(74, static_cast<int>(numFit));
                for (uint32_t i = 0; i < numFit; ++i) pt3(11);
            }
        } else if (n == "HATCH" || n == "MPOLYGON") {
            if (n == "MPOLYGON") return false;
            e.type = "HATCH";
            if (!ParseHatchBody(e, s, o)) return false;
        } else if (n == "LEADER") {
            e.type = "LEADER";
            d.B();
            o.Tag(73, static_cast<int>(d.BS()));   // annotation type
            o.Tag(72, static_cast<int>(d.BS()));   // path type
            uint32_t numPoints = d.BL();
            if (numPoints > 10000 || d.Overrun()) return false;
            o.Tag(76, static_cast<int>(numPoints));
            for (uint32_t i = 0; i < numPoints; ++i) pt3(10);
        } else if (n.compare(0, 10, "DIMENSION_") == 0) {
            e.type = "DIMENSION";
            if (ver >= Ver::R2010) d.RC();
            {
                double ex = d.BD(), ey = d.BD(), ez = d.BD();
                if (ex != 0 || ey != 0 || ez != 1) o.Pt(210, ex, ey, ez);
            }
            pt2(11);
            d.BD();                 // elevation
            unsigned flag1 = d.RC();
            std::string userText = d.T(s.str);
            if (!userText.empty()) o.Tag(1, OneLine(userText));
            d.BD(); d.BD();         // text rotation, horizontal direction
            d.BD(); d.BD(); d.BD(); // insertion scale
            d.BD();                 // insertion rotation
            if (ver >= Ver::R2000) { d.BS(); d.BS(); d.BD(); d.BD(); }
            if (ver >= Ver::R2007) { d.B(); d.B(); d.B(); }
            pt2(12);
            int dimType = 0;
            if (n == "DIMENSION_ALIGNED") dimType = 1;
            else if (n == "DIMENSION_ANG2LN") dimType = 2;
            else if (n == "DIMENSION_DIAMETER") dimType = 3;
            else if (n == "DIMENSION_RADIUS") dimType = 4;
            else if (n == "DIMENSION_ANG3PT") dimType = 5;
            else if (n == "DIMENSION_ORDINATE") dimType = 6;
            o.Tag(70, dimType | 32 | ((flag1 & 1) ? 0 : 128));
            if (n == "DIMENSION_LINEAR" || n == "DIMENSION_ALIGNED") {
                pt3(13); pt3(14); pt3(10);
            } else if (n == "DIMENSION_ORDINATE") {
                pt3(10); pt3(13); pt3(14);
            } else if (n == "DIMENSION_ANG3PT") {
                pt3(10); pt3(13); pt3(14); pt3(15);
            } else if (n == "DIMENSION_ANG2LN") {
                pt2(10); pt3(13); pt3(14); pt3(15); pt3(16);
            } else if (n == "DIMENSION_RADIUS") {
                pt3(10); pt3(15);
            } else {
                pt3(15); pt3(10);
            }
        } else if (n == "BLOCK") {
            e.type = "BLOCK";
            std::string blockName = d.T(s.str);
            o.Tag(2, OneLine(blockName));
        } else if (n == "ENDBLK" || n == "SEQEND") {
            e.type = n;
        } else if (n == "VIEWPORT") {
            return false;
        } else {
            ++skippedTypes[n];
            return false;
        }
        e.body = std::move(o.Str());
        return true;
    }

    void EmitLongText(DxfOut& o, const std::string& text) {
        std::string t;
        t.reserve(text.size());
        for (char c : text) {
            if (c == '\r') continue;
            if (c == '\n') { t += "\\P"; continue; }
            t.push_back(c);
        }
        // DXF splits long MTEXT into 250-byte chunks: code 3 pieces then 1.
        size_t pos = 0;
        while (t.size() - pos > 250) {
            size_t n = 250;
            while (n > 0 && (static_cast<unsigned char>(t[pos + n]) & 0xC0) == 0x80) --n;
            if (n == 0) n = 250;
            o.Tag(3, t.substr(pos, n));
            pos += n;
        }
        o.Tag(1, t.substr(pos));
    }

    void ParseTextBody(Streams& s, DxfOut& o) {
        BitReader& d = s.dat;
        double elevation = 0, ix, iy, ax, ay, ex = 0, ey = 0, ez = 1;
        double oblique = 0, rotation = 0, height, width = 1;
        int generation = 0, halign = 0, valign = 0;
        std::string text;
        if (ver <= Ver::R14) {
            elevation = d.BD();
            ix = d.RD(); iy = d.RD();
            ax = d.RD(); ay = d.RD();
            ex = d.BD(); ey = d.BD(); ez = d.BD();
            d.BD();   // thickness
            oblique = d.BD();
            rotation = d.BD();
            height = d.BD();
            width = d.BD();
            text = d.TV();
            generation = d.BS();
            halign = d.BS();
            valign = d.BS();
        } else {
            unsigned df = d.RC();
            if (!(df & 0x01)) elevation = d.RD();
            ix = d.RD(); iy = d.RD();
            ax = ix; ay = iy;
            if (!(df & 0x02)) { ax = d.DD(ix); ay = d.DD(iy); }
            d.BE(ex, ey, ez);
            d.BT();
            if (!(df & 0x04)) oblique = d.RD();
            if (!(df & 0x08)) rotation = d.RD();
            height = d.RD();
            if (!(df & 0x10)) width = d.RD();
            text = d.T(s.str);
            if (!(df & 0x20)) generation = d.BS();
            if (!(df & 0x40)) halign = d.BS();
            if (!(df & 0x80)) valign = d.BS();
        }
        o.Pt(10, ix, iy, elevation);
        o.Pt(11, ax, ay, elevation);
        o.Tag(40, height);
        if (width != 1.0) o.Tag(41, width);
        if (rotation != 0) o.Tag(50, Deg(rotation));
        if (oblique != 0) o.Tag(51, Deg(oblique));
        if (generation) o.Tag(71, generation);
        if (halign) o.Tag(72, halign);
        if (valign) o.Tag(73, valign);
        if (ex != 0 || ey != 0 || ez != 1) o.Pt(210, ex, ey, ez);
        o.Tag(1, OneLine(text));
    }

    bool ParseHatchBody(Ent& e, Streams& s, DxfOut& o) {
        BitReader& d = s.dat;
        (void)e;
        bool gradient = false;
        std::string gradientName;
        if (ver >= Ver::R2004) {
            gradient = d.BL() != 0;
            d.BL();
            double angle = d.BD();
            double shift = d.BD();
            uint32_t single = d.BL();
            double tint = d.BD();
            uint32_t numColors = d.BL();
            if (numColors > 1000) return false;
            std::vector<uint32_t> rgbs;
            for (uint32_t i = 0; i < numColors; ++i) {
                d.BD();
                // CMC (R2004+): index BS, rgb BL, flag RC, names in str.
                d.BS();
                uint32_t rgb = d.BL();
                unsigned flag = d.RC();
                if (flag & 1) d.T(s.str);
                if (flag & 2) d.T(s.str);
                rgbs.push_back(rgb);
            }
            gradientName = d.T(s.str);
            if (gradient) {
                o.Tag(450, 1);
                o.Tag(460, angle);
                o.Tag(461, shift);
                o.Tag(452, static_cast<int>(single));
                o.Tag(462, tint);
                o.Tag(453, static_cast<int>(numColors));
                for (uint32_t rgb : rgbs) {
                    unsigned method = rgb >> 24;
                    if (method == 0xc3) o.Tag(63, static_cast<int>(rgb & 0xff));
                    else o.Tag(421, static_cast<int>(rgb & 0xffffff));
                }
                o.Tag(470, OneLine(gradientName));
            }
        }
        double elevation = d.BD();
        double ex = d.BD(), ey = d.BD(), ez = d.BD();
        if (ex == 0 && ey == 0) ez = ez <= 0 ? -1 : 1;
        std::string name = d.T(s.str);
        bool solid = d.B();
        bool associative = d.B();
        uint32_t numPaths = d.BL();
        if (numPaths > 10000 || d.Overrun()) return false;
        o.Pt(10, 0.0, 0.0, elevation);
        if (ex != 0 || ey != 0 || ez != 1) o.Pt(210, ex, ey, ez);
        o.Tag(2, OneLine(name));
        o.Tag(70, solid ? 1 : 0);
        o.Tag(71, associative ? 1 : 0);
        o.Tag(91, static_cast<int>(numPaths));
        bool hasDerived = false;
        for (uint32_t p = 0; p < numPaths; ++p) {
            uint32_t flag = d.BL();
            hasDerived = hasDerived || (flag & 4);
            o.Tag(92, static_cast<int>(flag));
            if (!(flag & 2)) {
                uint32_t numSegs = d.BL();
                if (numSegs > 10000 || d.Overrun()) return false;
                o.Tag(93, static_cast<int>(numSegs));
                for (uint32_t k = 0; k < numSegs; ++k) {
                    unsigned ct = d.RC();
                    o.Tag(72, static_cast<int>(ct));
                    if (ct == 1) {
                        double x1 = d.RD(), y1 = d.RD(), x2 = d.RD(), y2 = d.RD();
                        o.Pt(10, x1, y1); o.Pt(11, x2, y2);
                    } else if (ct == 2) {
                        double cx = d.RD(), cy = d.RD();
                        double r = d.BD(), a1 = d.BD(), a2 = d.BD();
                        bool ccw = d.B();
                        o.Pt(10, cx, cy); o.Tag(40, r); o.Tag(50, Deg(a1)); o.Tag(51, Deg(a2));
                        o.Tag(73, ccw ? 1 : 0);
                    } else if (ct == 3) {
                        double cx = d.RD(), cy = d.RD(), mx = d.RD(), my = d.RD();
                        double ratio = d.BD(), a1 = d.BD(), a2 = d.BD();
                        bool ccw = d.B();
                        o.Pt(10, cx, cy); o.Pt(11, mx, my); o.Tag(40, ratio);
                        o.Tag(50, Deg(a1)); o.Tag(51, Deg(a2)); o.Tag(73, ccw ? 1 : 0);
                    } else if (ct == 4) {
                        uint32_t degree = d.BL();
                        bool rational = d.B();
                        bool periodic = d.B();
                        uint32_t numKnots = d.BL();
                        uint32_t numCtrl = d.BL();
                        if (numKnots > 10000 || numCtrl > 10000 || d.Overrun()) return false;
                        o.Tag(94, static_cast<int>(degree));
                        o.Tag(73, rational ? 1 : 0);
                        o.Tag(74, periodic ? 1 : 0);
                        o.Tag(95, static_cast<int>(numKnots));
                        o.Tag(96, static_cast<int>(numCtrl));
                        for (uint32_t i = 0; i < numKnots; ++i) o.Tag(40, d.BD());
                        for (uint32_t i = 0; i < numCtrl; ++i) {
                            double x = d.RD(), y = d.RD();
                            o.Pt(10, x, y);
                            if (rational) o.Tag(42, d.BD());
                        }
                        if (ver >= Ver::R2010) {
                            uint32_t numFit = d.BL();
                            if (numFit > 10000) return false;
                            o.Tag(97, static_cast<int>(numFit));
                            if (numFit) {
                                for (uint32_t i = 0; i < numFit; ++i) { double x = d.RD(), y = d.RD(); o.Pt(11, x, y); }
                                double x = d.RD(), y = d.RD(); o.Pt(12, x, y);
                                x = d.RD(); y = d.RD(); o.Pt(13, x, y);
                            }
                        }
                    } else {
                        return false;
                    }
                    if (d.Overrun()) return false;
                }
            } else {
                bool bulges = d.B();
                bool closed = d.B();
                uint32_t numVerts = d.BL();
                if (numVerts > 200000 || d.Overrun()) return false;
                o.Tag(72, bulges ? 1 : 0);
                o.Tag(73, closed ? 1 : 0);
                o.Tag(93, static_cast<int>(numVerts));
                for (uint32_t i = 0; i < numVerts; ++i) {
                    double x = d.RD(), y = d.RD();
                    o.Pt(10, x, y);
                    if (bulges) o.Tag(42, d.BD());
                }
            }
            uint32_t numBoundary = d.BL();
            if (numBoundary > 10000) return false;
            o.Tag(97, static_cast<int>(numBoundary));
            // Boundary handles live in the handle stream; consumed in
            // ParseEntityHandles via e.owned sizing is unnecessary - we
            // never read past them.
        }
        (void)hasDerived;
        o.Tag(75, static_cast<int>(d.BS()));
        o.Tag(76, static_cast<int>(d.BS()));
        return !d.Overrun();
    }

    void ParseEntityHandles(Ent& e, Streams& s, uint16_t type, const std::string& n) {
        BitReader& h = s.hdl;
        (void)type;
        if (n == "TEXT" || n == "ATTRIB") {
            e.style = Abs(h.H(), e.handle);
        } else if (n == "MTEXT") {
            // style handle sits in the data stream order for MTEXT
            // (FIELD_HANDLE0 style before linespace) - it is in hdl.
            e.style = Abs(h.H(), e.handle);
        } else if (n == "INSERT" || n == "MINSERT") {
            e.block = Abs(h.H(), e.handle);
            if (e.hasAttribs) {
                if (ver <= Ver::R2000) {
                    e.firstOwned = Abs(h.H(), e.handle);
                    e.lastOwned = Abs(h.H(), e.handle);
                } else {
                    for (auto& v : e.owned) v = Abs(h.H(), e.handle);
                }
                h.H();   // seqend
            }
        } else if (n == "POLYLINE_2D" || n == "POLYLINE_3D" || n == "POLYLINE_PFACE" ||
                   n == "POLYLINE_MESH") {
            if (ver <= Ver::R2000) {
                e.firstOwned = Abs(h.H(), e.handle);
                e.lastOwned = Abs(h.H(), e.handle);
            } else {
                for (auto& v : e.owned) v = Abs(h.H(), e.handle);
            }
            h.H();   // seqend
        } else if (n.compare(0, 10, "DIMENSION_") == 0) {
            h.H();   // dimstyle
            e.block = Abs(h.H(), e.handle);
        }
    }

    // ----- Non-entity objects ------------------------------------------------

    void DecodeNonEntity(Streams& s, uint64_t handle, uint16_t type, const std::string& n) {
        (void)type;
        if (n != "LAYER" && n != "LTYPE" && n != "STYLE" && n != "BLOCK_HEADER") return;
        BitReader& d = s.dat;
        if (ver <= Ver::R14) {
            s.bitsize = d.RL();
            if (s.bitsize > d.SizeBits()) { ++unreadable; return; }
            s.hdl = d;
            s.hdl.SetPos(s.bitsize);
        }
        uint32_t numReactors = d.BL();
        bool xdicMissing = false;
        if (ver >= Ver::R2004) xdicMissing = d.B();
        if (ver >= Ver::R2013) d.B();
        if (numReactors > 10000 || d.Overrun()) { ++unreadable; return; }

        // Table record header.
        std::string name = d.T(s.str);
        if (ver <= Ver::R2004) { d.B(); d.BS(); d.B(); }
        else d.BS();

        BitReader& h = s.hdl;
        h.H();                                        // owner
        for (uint32_t i = 0; i < numReactors; ++i) h.H();
        if (ver < Ver::R2004 || !xdicMissing) h.H();  // xdic
        h.H();                                        // xref

        if (n == "LAYER") {
            LayerRec L;
            L.name = name;
            if (ver <= Ver::R14) {
                L.frozen = d.B();
                L.off = d.B();
                d.B();
                d.B();
            } else {
                unsigned f = d.BS();
                L.frozen = f & 1;
                L.off = (f & 2) != 0;
                L.plot = (f & 16) != 0;
                L.linewt = static_cast<int>((f & 0x03E0) >> 5);
            }
            ReadCMC(s, L.color, L.hasRgb, L.rgb);
            if (ver <= Ver::R14 && L.color < 0) { L.off = true; L.color = -L.color; }
            if (d.Overrun()) { ++unreadable; return; }
            if (ver >= Ver::R2000) h.H();   // plotstyle
            if (ver >= Ver::R2007) h.H();   // material
            L.ltype = Abs(h.H(), handle);
            layers[handle] = std::move(L);
        } else if (n == "LTYPE") {
            LtypeRec T;
            T.name = name;
            d.T(s.str);              // description
            d.BD();                  // pattern length
            d.RC();                  // alignment
            unsigned numDashes = d.RC();
            for (unsigned i = 0; i < numDashes && !d.Overrun(); ++i) {
                double len = d.BD();
                d.BS();              // complex shape code
                d.RD(); d.RD();      // offsets
                d.BD(); d.BD();      // scale, rotation
                d.BS();              // shape flag
                T.dashes.push_back(len);
            }
            if (d.Overrun()) { ++unreadable; return; }
            ltypes[handle] = std::move(T);
        } else if (n == "STYLE") {
            StyleRec S;
            S.name = name;
            d.B(); d.B();
            d.BD(); d.BD(); d.BD();
            d.RC();
            d.BD();
            S.font = d.T(s.str);
            if (d.Overrun()) { ++unreadable; return; }
            styles[handle] = std::move(S);
        } else if (n == "BLOCK_HEADER") {
            BlockRec B;
            B.name = name;
            bool anonymous = d.B();
            bool hasAttrs = d.B();
            bool isXref = d.B();
            bool overlaid = d.B();
            if (ver >= Ver::R2000) d.B();
            B.flags = (anonymous ? 1 : 0) | (hasAttrs ? 2 : 0) | (isXref ? 4 : 0) | (overlaid ? 8 : 0);
            B.xref = isXref || overlaid;
            uint32_t numOwned = 0;
            if (ver >= Ver::R2004 && !B.xref) numOwned = d.BL();
            B.bx = d.BD(); B.by = d.BD(); B.bz = d.BD();
            d.T(s.str);   // xref path
            uint32_t numInserts = 0;
            if (ver >= Ver::R2000) {
                while (d.RC() && !d.Overrun()) ++numInserts;
                d.T(s.str);   // description
                uint32_t previewSize = d.BL();
                if (previewSize > 0xa00000) { ++unreadable; return; }
                d.Skip(previewSize);
            }
            if (d.Overrun() || numOwned > 0xf00000) { ++unreadable; return; }
            B.blockEntity = Abs(h.H(), handle);
            if (ver <= Ver::R2000) {
                if (!B.xref) {
                    B.first = Abs(h.H(), handle);
                    B.last = Abs(h.H(), handle);
                }
            } else {
                B.entities.resize(numOwned);
                for (auto& v : B.entities) v = Abs(h.H(), handle);
            }
            B.endblk = Abs(h.H(), handle);
            (void)numInserts;
            blocks[handle] = std::move(B);
        }
    }

    void ReadCMC(Streams& s, int& index, bool& hasRgb, uint32_t& rgb) {
        BitReader& d = s.dat;
        int16_t idx = static_cast<int16_t>(d.BS());
        index = idx;
        hasRgb = false;
        if (ver >= Ver::R2004) {
            uint32_t v = d.BL();
            unsigned flag = d.RC();
            if (flag & 1) d.T(s.str);
            if (flag & 2) d.T(s.str);
            unsigned method = v >> 24;
            if (method == 0xc3) {
                index = static_cast<int>(v & 0xff);
                if (index == 0) index = idx;
            } else if (method == 0xc2) {
                hasRgb = true;
                rgb = v & 0xffffff;
                if (index == 0 || index == 256) index = 7;
            } else if (method == 0xc0) {
                index = 256;
            } else if (method == 0xc1) {
                index = 0;
            }
        }
    }

    // ----- Assembly ----------------------------------------------------------

    static bool IEquals(const std::string& a, const char* b) {
        size_t n = std::strlen(b);
        if (a.size() != n) return false;
        for (size_t i = 0; i < n; ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    }

    std::string LayerName(uint64_t h) const {
        auto it = layers.find(h);
        return it != layers.end() && !it->second.name.empty() ? it->second.name : "0";
    }
    std::string LtypeName(const Ent& e) const {
        switch (e.ltypeFlags) {
            case 1: return "BYBLOCK";
            case 2: return "CONTINUOUS";
            case 3: {
                auto it = ltypes.find(e.ltype);
                if (it != ltypes.end()) return it->second.name;
                return {};
            }
            default: return {};
        }
    }

    void EmitCommon(DxfOut& o, const Ent& e, bool paperSpace) {
        o.Tag(8, OneLine(LayerName(e.layer)));
        if (paperSpace) o.Tag(67, 1);
        if (e.color != 256) o.Tag(62, e.color);
        if (e.hasRgb) o.Tag(420, static_cast<int>(e.rgb));
        std::string lt = LtypeName(e);
        if (!lt.empty()) o.Tag(6, OneLine(lt));
        if (e.linewt != 29) o.Tag(370, DwgLineweightToDxf(e.linewt));
        if (e.invisible) o.Tag(60, 1);
        if (e.style) {
            auto it = styles.find(e.style);
            if (it != styles.end() && !it->second.name.empty()) o.Tag(7, OneLine(it->second.name));
        }
        if (e.block) {
            auto it = blocks.find(e.block);
            if (it != blocks.end()) o.Tag(2, OneLine(it->second.name));
        }
    }

    // Children of an owner (vertices of a polyline, attribs of an insert):
    // the R2004+ handle list, else the R13-R2000 first/last chain, else
    // every entity that names the owner, in handle order.
    std::vector<uint64_t> Children(const Ent& owner, const char* childType) {
        std::vector<uint64_t> out;
        if (!owner.owned.empty()) {
            for (uint64_t h : owner.owned) {
                auto it = ents.find(h);
                if (it != ents.end() && it->second.type == childType) out.push_back(h);
            }
            if (!out.empty()) return out;
        }
        if (owner.firstOwned) {
            uint64_t h = owner.firstOwned;
            std::set<uint64_t> seen;
            while (h && !seen.count(h)) {
                seen.insert(h);
                auto it = ents.find(h);
                if (it == ents.end() || it->second.type != childType) break;
                out.push_back(h);
                if (h == owner.lastOwned) break;
                h = it->second.next;
            }
            if (!out.empty()) return out;
        }
        for (const auto& [h, e] : ents) {
            if (e.owner == owner.handle && e.type == childType) out.push_back(h);
        }
        return out;
    }

    void EmitEntity(DxfOut& o, const Ent& e, bool paperSpace) {
        if (e.type.empty() || e.type == "VERTEX" || e.type == "SEQEND" || e.type == "ENDBLK" ||
            e.type == "BLOCK" || e.type == "ATTRIB") {
            return;   // emitted through their owners only
        }
        o.Tag(0, e.type);
        EmitCommon(o, e, paperSpace);
        o.Append(e.body);
        if (e.type == "POLYLINE") {
            for (uint64_t vh : Children(e, "VERTEX")) {
                const Ent& v = ents[vh];
                o.Tag(0, "VERTEX");
                o.Tag(8, OneLine(LayerName(e.layer)));
                o.Append(v.body);
            }
            o.Tag(0, "SEQEND");
        } else if (e.type == "INSERT" && e.hasAttribs) {
            for (uint64_t ah : Children(e, "ATTRIB")) {
                const Ent& a = ents[ah];
                o.Tag(0, "ATTRIB");
                EmitCommon(o, a, paperSpace);
                o.Append(a.body);
            }
            o.Tag(0, "SEQEND");
        }
        ++result.entities;
    }

    // Entities of a block header: R2004+ list, R13-R2000 chain, else owner
    // grouping (map order).
    std::vector<uint64_t> BlockEntities(uint64_t blockHandle, const BlockRec& b, int entmodeFilter) {
        std::vector<uint64_t> out;
        std::set<uint64_t> seen;
        auto add = [&](uint64_t h) { if (ents.count(h) && seen.insert(h).second) out.push_back(h); };
        for (uint64_t h : b.entities) add(h);
        if (out.empty() && b.first) {
            uint64_t h = b.first;
            std::set<uint64_t> visited;
            while (h && !visited.count(h)) {
                visited.insert(h);
                auto it = ents.find(h);
                if (it == ents.end()) break;
                add(h);
                if (h == b.last) break;
                h = it->second.next;
            }
        }
        // Anything the list or chain missed but which names this block as
        // its owner (or sits in the requested space) goes at the end.
        for (uint64_t h : entOrder) {
            const Ent& e = ents[h];
            if ((e.entmode == 0 && e.owner == blockHandle) ||
                (entmodeFilter && e.entmode == entmodeFilter)) {
                add(h);
            }
        }
        return out;
    }

    void Assemble() {
        DxfOut o;
        o.Tag(0, "SECTION"); o.Tag(2, "HEADER");
        o.Tag(9, "$ACADVER"); o.Tag(1, "AC1015");
        o.Tag(0, "ENDSEC");

        // TABLES
        o.Tag(0, "SECTION"); o.Tag(2, "TABLES");
        o.Tag(0, "TABLE"); o.Tag(2, "LTYPE");
        for (const auto& [h, t] : ltypes) {
            o.Tag(0, "LTYPE"); o.Tag(2, OneLine(t.name)); o.Tag(70, 0);
            o.Tag(72, 65); o.Tag(73, static_cast<int>(t.dashes.size()));
            double total = 0;
            for (double d : t.dashes) total += std::fabs(d);
            o.Tag(40, total);
            for (double d : t.dashes) o.Tag(49, d);
        }
        o.Tag(0, "ENDTAB");
        o.Tag(0, "TABLE"); o.Tag(2, "LAYER");
        for (const auto& [h, L] : layers) {
            o.Tag(0, "LAYER"); o.Tag(2, OneLine(L.name));
            o.Tag(70, L.frozen ? 1 : 0);
            int c = L.color <= 0 || L.color > 255 ? 7 : L.color;
            o.Tag(62, (L.off || L.frozen) ? -c : c);
            if (L.hasRgb) o.Tag(420, static_cast<int>(L.rgb));
            auto lt = ltypes.find(L.ltype);
            o.Tag(6, lt != ltypes.end() ? OneLine(lt->second.name) : std::string("CONTINUOUS"));
            o.Tag(290, L.plot ? 1 : 0);
            o.Tag(370, DwgLineweightToDxf(L.linewt));
        }
        o.Tag(0, "ENDTAB");
        o.Tag(0, "TABLE"); o.Tag(2, "STYLE");
        for (const auto& [h, S] : styles) {
            o.Tag(0, "STYLE"); o.Tag(2, OneLine(S.name)); o.Tag(70, 0);
            o.Tag(3, OneLine(S.font));
        }
        o.Tag(0, "ENDTAB");
        o.Tag(0, "ENDSEC");

        // Identify model / paper space headers.
        uint64_t mspace = 0, pspace = 0;
        for (const auto& [h, b] : blocks) {
            if (IEquals(b.name, "*Model_Space")) mspace = h;
            else if (IEquals(b.name, "*Paper_Space")) pspace = h;
        }

        // BLOCKS
        o.Tag(0, "SECTION"); o.Tag(2, "BLOCKS");
        for (const auto& [h, b] : blocks) {
            if (h == mspace || h == pspace || b.xref) continue;
            if (IEquals(b.name, "*Model_Space") || b.name.compare(0, 12, "*Paper_Space") == 0) continue;
            std::vector<uint64_t> list = BlockEntities(h, b, 0);
            o.Tag(0, "BLOCK");
            o.Tag(8, "0");
            o.Tag(2, OneLine(b.name));
            o.Tag(70, b.flags);
            o.Pt(10, b.bx, b.by, b.bz);
            o.Tag(3, OneLine(b.name));
            for (uint64_t eh : list) EmitEntity(o, ents[eh], false);
            o.Tag(0, "ENDBLK");
            o.Tag(8, "0");
            ++result.blocks;
        }
        o.Tag(0, "ENDSEC");
        unsigned blockEntities = result.entities;
        result.entities = 0;

        // ENTITIES: model space, then (only if model space is empty) paper space.
        o.Tag(0, "SECTION"); o.Tag(2, "ENTITIES");
        std::vector<uint64_t> model;
        if (mspace) {
            model = BlockEntities(mspace, blocks[mspace], 2);
        }
        if (model.empty()) {
            for (uint64_t h : entOrder) {
                const Ent& e = ents[h];
                if (e.entmode == 2 || (mspace && e.entmode == 0 && e.owner == mspace)) model.push_back(h);
            }
        }
        // Filter out anything that is not top-level.
        std::vector<uint64_t> top;
        for (uint64_t h : model) {
            const Ent& e = ents[h];
            if (e.type.empty() || e.type == "VERTEX" || e.type == "SEQEND" || e.type == "ATTRIB" ||
                e.type == "BLOCK" || e.type == "ENDBLK") continue;
            top.push_back(h);
        }
        if (top.empty()) {
            for (uint64_t h : entOrder) {
                const Ent& e = ents[h];
                if (e.entmode == 1 || (pspace && e.entmode == 0 && e.owner == pspace)) {
                    if (e.type.empty() || e.type == "VERTEX" || e.type == "SEQEND" || e.type == "ATTRIB" ||
                        e.type == "BLOCK" || e.type == "ENDBLK") continue;
                    top.push_back(h);
                }
            }
            if (!top.empty() && warn) warn("DWG import: model space is empty, showing paper space");
        }
        for (uint64_t h : top) EmitEntity(o, ents[h], false);
        o.Tag(0, "ENDSEC");
        o.Tag(0, "EOF");

        result.dxf = std::move(o.Str());
        (void)blockEntities;
        for (const auto& [name, count] : skippedTypes) result.skipped += count;
        if (!skippedTypes.empty() && warn) {
            std::string msg = "DWG import: unsupported entity types skipped:";
            for (const auto& [name, count] : skippedTypes) {
                msg += " " + name + " (x" + std::to_string(count) + ")";
            }
            warn(msg);
        }
        if (unreadable && warn) {
            warn("DWG import: " + std::to_string(unreadable) + " object(s) could not be decoded");
        }
    }
};

}   // anonymous namespace

// ===========================================================================
// Public entry points
// ===========================================================================

bool DWGDecoderSupportsVersion(const std::string& head) {
    return VersionFromMagic(head) != Ver::None;
}

DWGDecodeResult DecodeDWG(const std::string& file,
                          const std::function<void(const std::string&)>& warn) {
    DWGDecodeResult res;
    DrawingData d;
    d.ver = VersionFromMagic(file);
    if (d.ver == Ver::None) {
        res.error = file.size() >= 6 && file.compare(0, 2, "AC") == 0
                    ? "unsupported DWG version " + file.substr(0, 6)
                    : "not a DWG file";
        return res;
    }
    res.version = file.substr(0, 6);
    bool loaded = false;
    if (d.ver <= Ver::R2000) loaded = LoadR13R2000(file, d);
    else if (d.ver == Ver::R2007) loaded = LoadR2007(file, d);
    else loaded = LoadR2004(file, d);
    if (!loaded) {
        res.error = d.error.empty() ? "unreadable file structure" : d.error;
        return res;
    }
    if (d.map.empty()) {
        res.error = "empty object map";
        return res;
    }
    Decoder dec(d, res, warn);
    dec.Run();
    res.ok = true;
    return res;
}

} // namespace VectorConverter
} // namespace UltraCanvas
