// core/UltraCanvasUCDContainer.cpp
// The UCD v2 container: signature and header, raw thumbnail, length-prefixed
// sections, INDX, and the zlib / gzip section compression.
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "UltraCanvasUCDContainer.h"
#include "UltraCanvasFileError.h"   // WriteFileAtomically
#include "UltraCanvasPathUtf8.h"

#include "miniz.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>

namespace UltraCanvas {
namespace UCD {

namespace {
    constexpr uint8_t kGuard = 0x89;
    constexpr size_t kMaxThumbnail = size_t(64) << 20;
    // miniz counts in unsigned int; feed it at most this much at a time.
    constexpr size_t kChunk = size_t(1) << 30;

    // Four printable ASCII characters (a FourCC or a descriptor letter).
    bool IsPrintable(char c) { return c >= 0x20 && c < 0x7F; }

    bool ValidType(const std::string& type) {
        return type.size() == 4 && std::all_of(type.begin(), type.end(), IsPrintable);
    }

    std::string ReadFourCC(const uint8_t* p) {
        return std::string(reinterpret_cast<const char*>(p), 4);
    }

    uint32_t GetU32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    // A gzip member (RFC 1952): its header, then raw deflate, then CRC-32
    // and the length - both checked.
    bool InflateGzip(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t limit) {
        if (size < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 8) return false;
        const uint8_t flags = data[3];
        size_t pos = 10;
        if (flags & 0x04) {                       // FEXTRA
            if (pos + 2 > size) return false;
            pos += 2 + (static_cast<size_t>(data[pos]) | (static_cast<size_t>(data[pos + 1]) << 8));
        }
        for (uint8_t bit : { uint8_t(0x08), uint8_t(0x10) }) {   // FNAME, FCOMMENT
            if (!(flags & bit)) continue;
            while (pos < size && data[pos] != 0) ++pos;
            ++pos;
        }
        if (flags & 0x02) pos += 2;               // FHCRC
        if (pos + 8 > size) return false;

        mz_stream stream{};
        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return false;
        out.clear();
        const uint8_t* in = data + pos;
        size_t inLeft = size - pos - 8;
        uint8_t buffer[65536];
        int status = MZ_OK;
        while (status != MZ_STREAM_END) {
            if (stream.avail_in == 0 && inLeft > 0) {
                stream.next_in = in;
                stream.avail_in = static_cast<unsigned int>(std::min(inLeft, kChunk));
                in += stream.avail_in;
                inLeft -= stream.avail_in;
            }
            stream.next_out = buffer;
            stream.avail_out = sizeof(buffer);
            status = mz_inflate(&stream, MZ_NO_FLUSH);
            if (status != MZ_OK && status != MZ_STREAM_END) { mz_inflateEnd(&stream); return false; }
            const size_t produced = sizeof(buffer) - stream.avail_out;
            if (out.size() + produced > limit) { mz_inflateEnd(&stream); return false; }
            out.insert(out.end(), buffer, buffer + produced);
            if (status == MZ_OK && produced == 0 && stream.avail_in == 0 && inLeft == 0) {
                mz_inflateEnd(&stream);
                return false;   // the data ended before the stream did
            }
        }
        mz_inflateEnd(&stream);
        const uint8_t* trailer = data + size - 8;
        const uint32_t crc = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, out.data(), out.size()));
        return GetU32(trailer) == crc && GetU32(trailer + 4) == static_cast<uint32_t>(out.size());
    }

    bool ParseBody(const std::vector<uint8_t>& bytes, Header& header, std::vector<uint8_t>& thumbnail,
                   std::vector<Section>& sections, std::string& error) {
        if (!ParseHeader(bytes.data(), bytes.size(), header, error)) return false;
        uint64_t pos = kFixedHeaderSize + static_cast<uint64_t>(header.extensionLength);
        if (pos + header.thumbnailLength > bytes.size()) {
            error = "The UCD file is truncated (it ends inside its thumbnail)";
            return false;
        }
        thumbnail.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                         bytes.begin() + static_cast<std::ptrdiff_t>(pos + header.thumbnailLength));
        pos += header.thumbnailLength;
        sections.clear();
        while (pos < bytes.size()) {
            if (bytes.size() - pos < kSectionHeaderSize) {
                error = "The UCD file is truncated (it ends inside a section header)";
                return false;
            }
            const uint8_t* h = bytes.data() + pos;
            Section s;
            s.type = ReadFourCC(h);
            const uint32_t length = GetU32(h + 4);
            const uint8_t compression = h[8];
            s.encrypted = (h[9] & 0x01) != 0;
            s.offset = pos;
            s.compression = compression == static_cast<uint8_t>(Compression::HeaderDefault)
                                ? header.defaultCompression : static_cast<Compression>(compression);
            pos += kSectionHeaderSize;
            if (length > bytes.size() - pos) {
                error = "The UCD file is truncated (section " + s.type + " is cut short)";
                return false;
            }
            s.stored.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                            bytes.begin() + static_cast<std::ptrdiff_t>(pos + length));
            pos += length;
            sections.push_back(std::move(s));
        }
        return true;
    }

    bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error) {
        std::FILE* f = OpenFileUtf8(path, "rb");
        if (!f) { error = "Could not open " + path; return false; }
        std::error_code ec;
        const auto size = std::filesystem::file_size(PathFromUtf8(path), ec);
        if (ec) { std::fclose(f); error = "Could not read " + path; return false; }
        bytes.resize(static_cast<size_t>(size));
        const size_t got = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
        if (got != bytes.size()) { error = "Could not read " + path; return false; }
        return true;
    }
}

// ===========================================================================
// HEADER
// ===========================================================================

bool ParseHeader(const uint8_t* data, size_t size, Header& out, std::string& error) {
    if (size >= 4 && std::memcmp(data, "UCD\x01", 4) == 0) {
        error = "This is a version 1 UltraCanvas document, which this program does not read";
        return false;
    }
    if (size < kFixedHeaderSize || data[0] != kGuard || data[1] != 'U' || data[2] != 'C' ||
        data[9] != 0x0D || data[10] != 0x0A || data[11] != 0x1A) {
        error = "Not an UltraCanvas document (UCD) file";
        return false;
    }
    std::string descriptor;
    for (int i = 1; i <= 8 && data[i] != 0; ++i) {
        if (!IsPrintable(static_cast<char>(data[i]))) {
            error = "Not an UltraCanvas document (UCD) file";
            return false;
        }
        descriptor.push_back(static_cast<char>(data[i]));
    }
    Header h;
    h.descriptor = descriptor;
    h.versionMajor = data[12];
    h.versionMinor = data[13];
    h.bodyEncoding = data[14];
    h.defaultCompression = static_cast<Compression>(data[15]);
    h.keySource = data[16];
    h.flags = data[17];
    h.thumbnailLength = GetU32(data + 20);
    h.extensionLength = GetU32(data + 24);
    if (h.versionMajor != kVersionMajor) {
        error = "UltraCanvas document version " + std::to_string(h.versionMajor) + "." +
                std::to_string(h.versionMinor) + " is not one this program reads";
        return false;
    }
    out = h;
    return true;
}

bool ReadHeaderAndThumbnail(const std::string& path, Header& header,
                            std::vector<uint8_t>& thumbnail, std::string& error) {
    thumbnail.clear();
    std::FILE* f = OpenFileUtf8(path, "rb");
    if (!f) { error = "Could not open " + path; return false; }
    uint8_t buf[kFixedHeaderSize];
    const size_t got = std::fread(buf, 1, sizeof(buf), f);
    bool ok = ParseHeader(buf, got, header, error);
    if (ok && header.HasThumbnail()) {
        if (header.thumbnailLength > kMaxThumbnail) {
            error = "The thumbnail is implausibly large";
            ok = false;
        } else if (kFixedHeaderSize + static_cast<uint64_t>(header.extensionLength) > static_cast<uint64_t>(LONG_MAX) ||
                   std::fseek(f, static_cast<long>(kFixedHeaderSize + header.extensionLength), SEEK_SET) != 0) {
            error = "Could not read the thumbnail";
            ok = false;
        } else {
            thumbnail.resize(header.thumbnailLength);
            if (std::fread(thumbnail.data(), 1, thumbnail.size(), f) != thumbnail.size()) {
                thumbnail.clear();
                error = "The UCD file is truncated (it ends inside its thumbnail)";
                ok = false;
            }
        }
    }
    std::fclose(f);
    return ok;
}

// ===========================================================================
// READER
// ===========================================================================

bool Reader::Open(const std::string& path, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes, error)) return false;
    return OpenBuffer(bytes, error);
}

bool Reader::OpenBuffer(const std::vector<uint8_t>& bytes, std::string& error) {
    Header h;
    std::vector<uint8_t> thumb;
    std::vector<Section> list;
    if (!ParseBody(bytes, h, thumb, list, error)) return false;
    header = std::move(h);
    thumbnail = std::move(thumb);
    sections = std::move(list);
    return true;
}

std::vector<const Section*> Reader::FindSections(const std::string& type) const {
    std::vector<const Section*> out;
    for (const Section& s : sections) if (s.type == type) out.push_back(&s);
    return out;
}

bool Reader::Decode(const Section& section, std::vector<uint8_t>& out, std::string& error, size_t limit) const {
    if (section.encrypted) {
        error = "Section " + section.type + " is encrypted; password-protected UltraCanvas documents "
                "cannot be opened yet";
        return false;
    }
    switch (section.compression) {
        case Compression::Uncompressed:
            if (section.stored.size() > limit) { error = "Section " + section.type + " is too large"; return false; }
            out = section.stored;
            return true;
        case Compression::Deflate:
            if (InflateZlib(section.stored.data(), section.stored.size(), out, limit)) return true;
            error = "Section " + section.type + " is damaged (its compressed data does not decode)";
            return false;
        case Compression::Gzip:
            if (InflateGzip(section.stored.data(), section.stored.size(), out, limit)) return true;
            error = "Section " + section.type + " is damaged (its compressed data does not decode)";
            return false;
        default:
            error = "Section " + section.type + " uses a compression (" +
                    std::to_string(static_cast<int>(section.compression)) + ") this program does not read";
            return false;
    }
}

// ===========================================================================
// WRITER
// ===========================================================================

Writer::Writer(const std::string& d) : descriptor(d) {}

void Writer::AddSection(const std::string& type, std::vector<uint8_t> payload, Compression compression) {
    pending.push_back({ type, std::move(payload), compression });
}

bool Writer::WriteToBuffer(std::vector<uint8_t>& out, std::string& error) const {
    if (descriptor.empty() || descriptor.size() > 8 ||
        !std::all_of(descriptor.begin(), descriptor.end(), IsPrintable)) {
        error = "\"" + descriptor + "\" is not a UCD type descriptor";
        return false;
    }
    if (thumbnail.size() > UINT32_MAX) { error = "The thumbnail is too large"; return false; }

    // Each section as it will be stored.
    struct Stored { const Pending* source; std::vector<uint8_t> bytes; };
    std::vector<Stored> stored;
    stored.reserve(pending.size());
    for (const Pending& p : pending) {
        if (!ValidType(p.type)) { error = "\"" + p.type + "\" is not a section type"; return false; }
        if (p.payload.size() > UINT32_MAX) {
            error = "Section " + p.type + " is larger than a UCD section can hold (4 GiB)";
            return false;
        }
        Stored s{ &p, {} };
        if (p.compression == Compression::Deflate) {
            if (!DeflateZlib(p.payload, s.bytes)) { error = "Could not compress section " + p.type; return false; }
        } else if (p.compression == Compression::Uncompressed) {
            s.bytes = p.payload;
        } else {
            error = "Section " + p.type + ": only no compression or deflate is written";
            return false;
        }
        if (s.bytes.size() > UINT32_MAX) {
            error = "Section " + p.type + " is larger than a UCD section can hold (4 GiB)";
            return false;
        }
        stored.push_back(std::move(s));
    }

    out.clear();
    ByteWriter w(out);
    // ----- signature and fixed header (§2) -----
    w.U8(kGuard);
    std::string padded = descriptor;
    padded.resize(8, '\0');
    w.Bytes(padded.data(), 8);
    w.U8(0x0D); w.U8(0x0A); w.U8(0x1A);
    w.U8(kVersionMajor);
    w.U8(kVersionMinor);
    w.U8(0);                                              // binary body
    w.U8(static_cast<uint8_t>(Compression::Deflate));     // default compression
    w.U8(0);                                              // key source: none
    w.U8(thumbnail.empty() ? 0 : static_cast<uint8_t>(FlagThumbnail | FlagThumbPng));
    w.U16(0);
    w.U32(static_cast<uint32_t>(thumbnail.size()));
    w.U32(0);                                             // no header extension
    w.Bytes(thumbnail.data(), thumbnail.size());

    // ----- INDX (§4.2.1), first, uncompressed -----
    const uint64_t indexPayload = 8 + 20ull * stored.size();
    uint64_t at = out.size() + kSectionHeaderSize + indexPayload;
    std::vector<uint8_t> index;
    ByteWriter iw(index);
    iw.U8(1); iw.Zeros(3);
    iw.U32(static_cast<uint32_t>(stored.size()));
    for (const Stored& s : stored) {
        iw.FourCC(s.source->type);
        iw.U64(at);
        iw.U32(static_cast<uint32_t>(s.bytes.size()));
        iw.U32(static_cast<uint32_t>(s.source->payload.size()));
        at += kSectionHeaderSize + s.bytes.size();
    }
    auto sectionHeader = [&](const std::string& type, size_t length, Compression c) {
        w.FourCC(type);
        w.U32(static_cast<uint32_t>(length));
        w.U8(static_cast<uint8_t>(c));
        w.U8(0);                                          // not encrypted
        w.U16(0);
    };
    sectionHeader("INDX", index.size(), Compression::Uncompressed);
    w.Bytes(index.data(), index.size());
    for (const Stored& s : stored) {
        sectionHeader(s.source->type, s.bytes.size(), s.source->compression);
        w.Bytes(s.bytes.data(), s.bytes.size());
    }
    return true;
}

bool Writer::WriteToFile(const std::string& path, std::string& error) const {
    std::vector<uint8_t> bytes;
    if (!WriteToBuffer(bytes, error)) return false;
    const std::string failure = WriteFileAtomically(path, [&bytes](const std::string& staged) -> std::string {
        std::FILE* f = OpenFileUtf8(staged, "wb");
        if (!f) return "Could not create " + staged;
        const bool written = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        const bool closed = std::fclose(f) == 0;
        return written && closed ? std::string() : "Could not write " + staged;
    });
    if (!failure.empty()) { error = failure; return false; }
    return true;
}

// ===========================================================================
// PREAMBLE
// ===========================================================================

void AppendPreamble(std::vector<uint8_t>& out, const std::string& fourcc, uint8_t version) {
    ByteWriter w(out);
    w.FourCC(fourcc);
    w.U8(version);
    w.Zeros(3);
}

bool ReadPreamble(const std::vector<uint8_t>& payload, std::string& fourcc, uint8_t& version) {
    if (payload.size() < kPreambleSize) return false;
    fourcc = ReadFourCC(payload.data());
    version = payload[4];
    return true;
}

// ===========================================================================
// BYTES
// ===========================================================================

void ByteWriter::U16(uint16_t v) { U8(static_cast<uint8_t>(v)); U8(static_cast<uint8_t>(v >> 8)); }
void ByteWriter::U32(uint32_t v) { for (int i = 0; i < 4; ++i) U8(static_cast<uint8_t>(v >> (8 * i))); }
void ByteWriter::U64(uint64_t v) { for (int i = 0; i < 8; ++i) U8(static_cast<uint8_t>(v >> (8 * i))); }
void ByteWriter::F32(float v) { uint32_t bits; std::memcpy(&bits, &v, 4); U32(bits); }
void ByteWriter::F64(double v) { uint64_t bits; std::memcpy(&bits, &v, 8); U64(bits); }
void ByteWriter::Bytes(const void* data, size_t size) {
    if (size == 0) return;
    const uint8_t* b = static_cast<const uint8_t*>(data);
    out.insert(out.end(), b, b + size);
}
void ByteWriter::FourCC(const std::string& code) {
    std::string c = code.substr(0, 4);
    c.resize(4, ' ');
    Bytes(c.data(), 4);
}
void ByteWriter::String16(const std::string& text) {
    size_t length = std::min<size_t>(text.size(), 65535);
    // Never split a UTF-8 character: back up over continuation bytes.
    while (length < text.size() && length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) --length;
    U16(static_cast<uint16_t>(length));
    Bytes(text.data(), length);
}

bool ByteReader::Take(size_t size) {
    if (!ok || size > Remaining()) { ok = false; return false; }
    return true;
}
bool ByteReader::U8(uint8_t& v) { if (!Take(1)) return false; v = p[pos++]; return true; }
bool ByteReader::U16(uint16_t& v) {
    if (!Take(2)) return false;
    v = static_cast<uint16_t>(p[pos] | (p[pos + 1] << 8));
    pos += 2;
    return true;
}
bool ByteReader::U32(uint32_t& v) { if (!Take(4)) return false; v = GetU32(p + pos); pos += 4; return true; }
bool ByteReader::U64(uint64_t& v) {
    if (!Take(8)) return false;
    v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[pos + static_cast<size_t>(i)];
    pos += 8;
    return true;
}
bool ByteReader::I32(int32_t& v) { uint32_t u; if (!U32(u)) return false; v = static_cast<int32_t>(u); return true; }
bool ByteReader::F32(float& v) { uint32_t u; if (!U32(u)) return false; std::memcpy(&v, &u, 4); return true; }
bool ByteReader::F64(double& v) { uint64_t u; if (!U64(u)) return false; std::memcpy(&v, &u, 8); return true; }
bool ByteReader::Bytes(void* dst, size_t size) {
    if (!Take(size)) return false;
    if (size) std::memcpy(dst, p + pos, size);
    pos += size;
    return true;
}
bool ByteReader::Skip(size_t size) { if (!Take(size)) return false; pos += size; return true; }
bool ByteReader::FourCC(std::string& code) {
    if (!Take(4)) return false;
    code = ReadFourCC(p + pos);
    pos += 4;
    return true;
}
bool ByteReader::String16(std::string& text) {
    uint16_t length;
    if (!U16(length) || !Take(length)) return false;
    text.assign(reinterpret_cast<const char*>(p + pos), length);
    pos += length;
    return true;
}

// ===========================================================================
// COMPRESSION
// ===========================================================================

bool DeflateZlib(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    mz_stream stream{};
    if (mz_deflateInit(&stream, MZ_DEFAULT_LEVEL) != MZ_OK) return false;
    out.clear();
    const uint8_t* next = in.data();
    size_t left = in.size();
    uint8_t buffer[65536];
    int status = MZ_OK;
    while (status != MZ_STREAM_END) {
        if (stream.avail_in == 0 && left > 0) {
            stream.next_in = next;
            stream.avail_in = static_cast<unsigned int>(std::min(left, kChunk));
            next += stream.avail_in;
            left -= stream.avail_in;
        }
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        status = mz_deflate(&stream, left == 0 ? MZ_FINISH : MZ_NO_FLUSH);
        if (status != MZ_OK && status != MZ_STREAM_END && status != MZ_BUF_ERROR) {
            mz_deflateEnd(&stream);
            return false;
        }
        out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
    }
    mz_deflateEnd(&stream);
    return true;
}

bool InflateZlib(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t limit) {
    mz_stream stream{};
    if (mz_inflateInit(&stream) != MZ_OK) return false;
    out.clear();
    const uint8_t* next = data;
    size_t left = size;
    uint8_t buffer[65536];
    int status = MZ_OK;
    while (status != MZ_STREAM_END) {
        if (stream.avail_in == 0 && left > 0) {
            stream.next_in = next;
            stream.avail_in = static_cast<unsigned int>(std::min(left, kChunk));
            next += stream.avail_in;
            left -= stream.avail_in;
        }
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        status = mz_inflate(&stream, MZ_NO_FLUSH);
        if (status != MZ_OK && status != MZ_STREAM_END) { mz_inflateEnd(&stream); return false; }
        const size_t produced = sizeof(buffer) - stream.avail_out;
        if (out.size() + produced > limit) { mz_inflateEnd(&stream); return false; }
        out.insert(out.end(), buffer, buffer + produced);
        if (status == MZ_OK && produced == 0 && stream.avail_in == 0 && left == 0) {
            mz_inflateEnd(&stream);
            return false;   // the data ended before the stream did
        }
    }
    mz_inflateEnd(&stream);
    return true;
}

} // namespace UCD
} // namespace UltraCanvas
