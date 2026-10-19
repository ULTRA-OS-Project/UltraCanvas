// include/UltraCanvasUCDContainer.h
// The UltraCanvas Document container, version 2 (*.ucd): the 28-byte
// signature and header, the raw thumbnail after it, and the body of
// length-prefixed, independently compressed sections - the layer every UCD
// content type is built on (Docs/UltraCanvas/UCD-FileFormat-v2.md, §1-§4).
//
// What it does not do is interpret a section: a layered image's LAYR / BMAP /
// VECT sections are UCRasterDocument's (SaveToFile / LoadFromFile on a *.ucd),
// a window's WNDW would be the window loader's. It reads and writes the
// container, so each of those deals only in payload bytes.
//
//   UCD::Writer w("UCBitmap");
//   w.SetThumbnailPng(png);
//   w.AddSection("BMAP", payload);                 // deflate by default
//   std::string error;
//   if (!w.WriteToFile(path, error)) ...           // staged: the old file survives a failure
//
//   UCD::Reader r;
//   if (!r.Open(path, error)) ...
//   for (const UCD::Section* s : r.FindSections("BMAP")) {
//       std::vector<uint8_t> bytes;
//       if (r.Decode(*s, bytes, error)) ...        // decompressed payload
//   }
//
// Compression: none and deflate (a zlib stream, RFC 1950) are written; none,
// deflate and gzip (RFC 1952) are read. LZMA is not supported, and neither
// is encryption yet: a password-protected file opens (header, thumbnail and
// section list) but its encrypted sections do not decode - Decode says so.
// Writers always put an INDX first (§4.2.1).
//
// UI-free, no libvips: compression is the vendored miniz, so it builds
// wherever the framework does (wasm included).
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace UCD {

// ===== CONSTANTS =====
constexpr size_t  kFixedHeaderSize   = 28;
constexpr size_t  kSectionHeaderSize = 12;
constexpr size_t  kPreambleSize      = 8;
constexpr uint8_t kVersionMajor      = 2;
constexpr uint8_t kVersionMinor      = 1;

// Header byte 15 / section header byte 8.
enum class Compression : uint8_t {
    Uncompressed  = 0,
    Deflate       = 1,   // zlib stream (RFC 1950)
    Gzip          = 2,   // gzip member (RFC 1952)
    Lzma          = 3,
    HeaderDefault = 0xFF // section header only: the header's default applies
};

// Header byte 17.
enum HeaderFlags : uint8_t {
    FlagThumbnail   = 0x01,
    FlagThumbPng    = 0x02,   // set: the thumbnail is a PNG; clear: HEIC
    FlagEncrypted   = 0x04,
    FlagPrivate     = 0x08,
};

// ===== THE FIXED HEADER =====
struct Header {
    std::string descriptor;          // "UCDoc", "UCBitmap" ... (at most 8 ASCII characters)
    uint8_t versionMajor = kVersionMajor;
    uint8_t versionMinor = kVersionMinor;
    uint8_t bodyEncoding = 0;        // 0 binary, 1 XML, 2 JSON (debug)
    Compression defaultCompression = Compression::Deflate;
    uint8_t keySource = 0;           // 0 none, 1 password, 2 SuperVault
    uint8_t flags = 0;
    uint32_t thumbnailLength = 0;
    uint32_t extensionLength = 0;

    bool HasThumbnail() const { return (flags & FlagThumbnail) != 0 && thumbnailLength > 0; }
    bool ThumbnailIsPng() const { return (flags & FlagThumbPng) != 0; }
    bool IsEncrypted() const { return keySource != 0 || (flags & FlagEncrypted) != 0; }
    bool IsPrivate() const { return (flags & FlagPrivate) != 0; }
};

// The 28 bytes at the start of a file: true when they are a UCD v2 header
// (every signature byte checked, §6) whose major version this reads. False
// with `error` for anything else - a v1 file (`UCD\x01`) says so.
bool ParseHeader(const uint8_t* data, size_t size, Header& out, std::string& error);

// Only what a filer needs: the header and the raw thumbnail bytes (HEIC or
// PNG, per the header), read without touching the body. `thumbnail` is empty
// when the file has none. Thumbnails over 64 MiB are refused.
bool ReadHeaderAndThumbnail(const std::string& path, Header& header,
                            std::vector<uint8_t>& thumbnail, std::string& error);

// ===== ONE SECTION, AS STORED =====
struct Section {
    std::string type;                // FourCC, e.g. "BMAP"
    uint64_t offset = 0;             // file offset of its 12-byte header
    Compression compression = Compression::Uncompressed;   // resolved: never HeaderDefault
    bool encrypted = false;
    std::vector<uint8_t> stored;     // the payload as in the file
};

// ===== READING =====
class Reader {
public:
    // Reads the whole file: header, extension, thumbnail and every section.
    // A truncated section or a bad signature fails; unknown section types
    // are listed like any other (the caller skips what it does not know).
    bool Open(const std::string& path, std::string& error);
    bool OpenBuffer(const std::vector<uint8_t>& bytes, std::string& error);

    const Header& GetHeader() const { return header; }
    const std::vector<uint8_t>& GetThumbnail() const { return thumbnail; }
    const std::vector<Section>& GetSections() const { return sections; }
    // Every section of `type`, in file order - its number in that list is
    // how other sections refer to it (§4.2).
    std::vector<const Section*> FindSections(const std::string& type) const;

    // The payload as it was serialized: decompressed. False with `error` for
    // an encrypted section, an unsupported compression or damaged data.
    // `limit` caps what a section may decompress to, so a damaged or hostile
    // file cannot make the reader allocate without bound.
    bool Decode(const Section& section, std::vector<uint8_t>& out, std::string& error,
                size_t limit = size_t(1) << 31) const;

private:
    Header header;
    std::vector<uint8_t> thumbnail;
    std::vector<Section> sections;
};

// ===== WRITING =====
class Writer {
public:
    explicit Writer(const std::string& descriptor);

    // A PNG thumbnail (HEIC would need an encoder this build may not have).
    void SetThumbnailPng(std::vector<uint8_t> png) { thumbnail = std::move(png); }
    // A section of `type` holding `payload` (as serialized). `compression`:
    // Uncompressed or Deflate; data that is already compressed (a PNG, an ODT)
    // is better stored Uncompressed.
    void AddSection(const std::string& type, std::vector<uint8_t> payload,
                    Compression compression = Compression::Deflate);

    // The whole file, INDX first. False with `error` when a section does not
    // fit the format (a payload over 4 GiB) or a type is not four characters.
    bool WriteToBuffer(std::vector<uint8_t>& out, std::string& error) const;
    // WriteToBuffer through a staged file (WriteFileAtomically): a failure
    // leaves whatever was at `path` as it was.
    bool WriteToFile(const std::string& path, std::string& error) const;

private:
    struct Pending {
        std::string type;
        std::vector<uint8_t> payload;
        Compression compression;
    };
    std::string descriptor;
    std::vector<uint8_t> thumbnail;
    std::vector<Pending> pending;
};

// ===== SUB-FORMAT PREAMBLE (§4.5) =====
// The 8 bytes a VECT / BMAP / VIDS / AUDS / SC3D payload starts with.
void AppendPreamble(std::vector<uint8_t>& out, const std::string& fourcc, uint8_t version);
// False when `payload` is shorter than a preamble.
bool ReadPreamble(const std::vector<uint8_t>& payload, std::string& fourcc, uint8_t& version);

// ===== LITTLE-ENDIAN BYTES =====
// What section payloads are built from and read with.
class ByteWriter {
public:
    explicit ByteWriter(std::vector<uint8_t>& target) : out(target) {}
    void U8(uint8_t v)  { out.push_back(v); }
    void U16(uint16_t v);
    void U32(uint32_t v);
    void U64(uint64_t v);
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void F32(float v);
    void F64(double v);
    void Bytes(const void* data, size_t size);
    void Zeros(size_t count) { out.insert(out.end(), count, 0); }
    void FourCC(const std::string& code);           // padded with spaces to four
    // uint16 length + UTF-8 (cut at 65535 bytes, on a character boundary).
    void String16(const std::string& text);
private:
    std::vector<uint8_t>& out;
};

// Bounds-checked: a read past the end returns false and leaves the reader
// failed (Ok() false), so a parser can read a whole record and check once.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : p(data), n(size) {}
    explicit ByteReader(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
    bool U8(uint8_t& v);
    bool U16(uint16_t& v);
    bool U32(uint32_t& v);
    bool U64(uint64_t& v);
    bool I32(int32_t& v);
    bool F32(float& v);
    bool F64(double& v);
    bool Bytes(void* dst, size_t size);
    bool Skip(size_t size);
    bool FourCC(std::string& code);
    bool String16(std::string& text);
    size_t Position() const { return pos; }
    size_t Remaining() const { return pos <= n ? n - pos : 0; }
    const uint8_t* Here() const { return p + pos; }
    bool Ok() const { return ok; }
private:
    bool Take(size_t size);
    const uint8_t* p;
    size_t n;
    size_t pos = 0;
    bool ok = true;
};

// ===== COMPRESSION =====
// zlib (RFC 1950) streams through miniz - exposed for payloads that nest
// compressed data of their own.
bool DeflateZlib(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);
bool InflateZlib(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t limit);

} // namespace UCD
} // namespace UltraCanvas
