// core/UltraCanvasUCDContainer.cpp
// The UCD v2 container: signature and header, raw thumbnail, length-prefixed
// sections, INDX, the zlib / gzip section compression, and password
// protection (SECU, XChaCha20-Poly1305 sections under an Argon2id key).
// Version: 1.1.0 - password protection (spec 4.3.1, 4.3.2)
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "UltraCanvasUCDContainer.h"
#include "UltraCanvasFileError.h"   // WriteFileAtomically
#include "UltraCanvasPathUtf8.h"

#include "UltraCrypt/UltraCryptCore.h"
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

    // ----- password protection (spec 4.3.1 / 4.3.2) -----
    constexpr size_t   kNonceSize      = 24;     // XChaCha20-Poly1305
    constexpr size_t   kTagSize        = 16;
    constexpr size_t   kSaltSize       = 16;
    constexpr size_t   kSecuSigned     = 36;     // the bytes the check tag binds
    constexpr size_t   kSecuSize       = kSecuSigned + kNonceSize + kTagSize;
    constexpr uint8_t  kKdfArgon2id    = 1;
    constexpr uint8_t  kAeadXChaCha    = 1;
    constexpr uint32_t kAllPermissions = 0x0F;   // print, copy, edit, form-fill
    // Costs no honest writer chooses: refused before anything is derived.
    constexpr uint32_t kMaxPasses      = 64;
    constexpr uint32_t kMaxMemoryKiB   = 4u * 1024u * 1024u;   // 4 GiB
    const char kCheckLabel[] = "UCD-SECU";

    std::vector<uint8_t> CheckAssociatedData(const uint8_t* secuSigned) {
        std::vector<uint8_t> aad(kCheckLabel, kCheckLabel + 8);
        aad.insert(aad.end(), secuSigned, secuSigned + kSecuSigned);
        return aad;
    }

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
            std::copy(h, h + kSectionHeaderSize, s.header.begin());
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

bool EncryptionAvailable() {
    return UltraCrypt_IsAvailable();
}

Reader::Reader() = default;
Reader::~Reader() = default;
Reader::Reader(Reader&&) noexcept = default;
Reader& Reader::operator=(Reader&&) noexcept = default;

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
    key.reset();
    return true;
}

bool Reader::NeedsPassword() const {
    return header.IsEncrypted() && !key;
}

bool Reader::Unlock(const std::string& password, std::string& error) {
    if (!header.IsEncrypted()) return true;
    if (header.keySource == 2) {
        error = "This document is protected by SuperVault, which this program cannot open yet";
        return false;
    }
    const auto records = FindSections("SECU");
    const char* damaged = "The document is password-protected, but its security record is damaged";
    if (records.empty()) {
        error = "The document is password-protected, but its security record is missing";
        return false;
    }
    const Section& secu = *records.front();
    if (secu.encrypted || secu.stored.size() < kSecuSize) { error = damaged; return false; }
    ByteReader r(secu.stored);
    uint8_t version = 0, kdfId = 0, aeadId = 0;
    uint32_t passes = 0, memoryKiB = 0, lanes = 0, permissions = 0;
    std::vector<uint8_t> salt(kSaltSize), nonce(kNonceSize), tag(kTagSize);
    if (!r.U8(version) || !r.U8(kdfId) || !r.U8(aeadId) || !r.Skip(1) || !r.U32(passes) ||
        !r.U32(memoryKiB) || !r.U32(lanes) || !r.Bytes(salt.data(), kSaltSize) || !r.U32(permissions) ||
        !r.Bytes(nonce.data(), kNonceSize) || !r.Bytes(tag.data(), kTagSize)) {
        error = damaged;
        return false;
    }
    if (version != 1 || kdfId != kKdfArgon2id || aeadId != kAeadXChaCha) {
        error = "The document is protected in a way this program does not know";
        return false;
    }
    if (passes == 0 || passes > kMaxPasses || memoryKiB == 0 || memoryKiB > kMaxMemoryKiB || lanes != 1) {
        error = damaged;
        return false;
    }
    if (!UltraCrypt_IsAvailable()) {
        error = "Opening a password-protected document needs the cryptography library (libsodium), "
                "which this build does not have";
        return false;
    }
    UltraCryptKdfParams kdf;
    kdf.salt = salt;
    kdf.iterations = passes;
    kdf.memoryKiB = memoryKiB;
    kdf.parallelism = lanes;
    kdf.outputLength = 32;
    UltraCryptSecureBuffer secret(password.data(), password.size());
    UltraCryptSecureBuffer derived;
    if (!UltraCrypt_DeriveKeyFromPassword(secret, kdf, derived)) {
        error = "Could not derive the document key";
        return false;
    }
    UltraCryptAeadParams check;
    check.nonce = nonce;
    check.associatedData = CheckAssociatedData(secu.stored.data());
    UltraCryptSecureBuffer nothing;
    if (!UltraCrypt_AeadOpen(derived, check, tag.data(), tag.size(), nothing)) {
        error = kWrongPassword;
        return false;
    }
    key = std::make_unique<UltraCryptSecureBuffer>(std::move(derived));
    return true;
}

std::vector<const Section*> Reader::FindSections(const std::string& type) const {
    std::vector<const Section*> out;
    for (const Section& s : sections) if (s.type == type) out.push_back(&s);
    return out;
}

bool Reader::Decode(const Section& section, std::vector<uint8_t>& out, std::string& error, size_t limit) const {
    const uint8_t* data = section.stored.data();
    size_t size = section.stored.size();
    std::vector<uint8_t> decrypted;
    if (section.encrypted) {
        if (!key) {
            error = "Section " + section.type + " is encrypted: the document needs its password";
            return false;
        }
        if (size < kNonceSize + kTagSize) {
            error = "Section " + section.type + " is damaged (it does not decrypt)";
            return false;
        }
        // Nonce, ciphertext, tag; the header as stored is the associated data.
        UltraCryptAeadParams params;
        params.nonce.assign(data, data + kNonceSize);
        params.associatedData.assign(section.header.begin(), section.header.end());
        UltraCryptSecureBuffer plain;
        if (!UltraCrypt_AeadOpen(*key, params, data + kNonceSize, size - kNonceSize, plain)) {
            error = "Section " + section.type + " is damaged (it does not decrypt)";
            return false;
        }
        decrypted.assign(plain.Data(), plain.Data() + plain.GetSize());
        data = decrypted.data();
        size = decrypted.size();
    }
    switch (section.compression) {
        case Compression::Uncompressed:
            if (size > limit) { error = "Section " + section.type + " is too large"; return false; }
            out.assign(data, data + size);
            return true;
        case Compression::Deflate:
            if (InflateZlib(data, size, out, limit)) return true;
            error = "Section " + section.type + " is damaged (its compressed data does not decode)";
            return false;
        case Compression::Gzip:
            if (InflateGzip(data, size, out, limit)) return true;
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
Writer::~Writer() = default;

void Writer::SetPassword(const std::string& text, bool hide) {
    if (text.empty()) {
        password.reset();
        hidePreview = false;
        return;
    }
    password = std::make_unique<UltraCryptSecureBuffer>(text.data(), text.size());
    hidePreview = hide;
}

void Writer::SetPassword(const UltraCryptSecureBuffer& text, bool hide) {
    if (text.IsEmpty()) {
        password.reset();
        hidePreview = false;
        return;
    }
    password = std::make_unique<UltraCryptSecureBuffer>(text.Clone());
    hidePreview = hide;
}

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

    // The document key and its SECU record (spec 4.3.1), when protected.
    UltraCryptSecureBuffer key;
    std::vector<uint8_t> secu;
    if (password) {
        if (!UltraCrypt_IsAvailable()) {
            error = "Password protection needs the cryptography library (libsodium), which this build does not have";
            return false;
        }
        UltraCryptKdfParams kdf = UltraCrypt_RecommendedKdfParams();
        kdf.salt.clear();                      // a new salt for every save
        kdf.outputLength = 32;
        if (!UltraCrypt_DeriveKeyFromPassword(*password, kdf, key) || kdf.salt.size() != kSaltSize) {
            error = "Could not derive the document key";
            return false;
        }
        ByteWriter sw(secu);
        sw.U8(1);
        sw.U8(kKdfArgon2id);
        sw.U8(kAeadXChaCha);
        sw.U8(0);
        sw.U32(kdf.iterations);
        sw.U32(kdf.memoryKiB);
        sw.U32(kdf.parallelism);
        sw.Bytes(kdf.salt.data(), kSaltSize);
        sw.U32(kAllPermissions);
        UltraCryptAeadParams check;            // a fresh nonce is generated
        check.associatedData = CheckAssociatedData(secu.data());
        std::vector<uint8_t> tag;
        const uint8_t nothing = 0;
        if (!UltraCrypt_AeadSeal(key, check, &nothing, 0, tag) || check.nonce.size() != kNonceSize ||
            tag.size() != kTagSize) {
            error = "Could not seal the document's security record";
            return false;
        }
        sw.Bytes(check.nonce.data(), kNonceSize);
        sw.Bytes(tag.data(), kTagSize);
    }

    // Each section as it will be stored: compressed, then - protected -
    // encrypted with its own header as the associated data (spec 4.3.2).
    struct Stored {
        std::string type;
        std::array<uint8_t, 12> header{};
        std::vector<uint8_t> bytes;
        size_t whole = 0;                      // serialized length, for the INDX
    };
    auto makeHeader = [](const std::string& type, size_t length, Compression c, bool encrypted) {
        std::array<uint8_t, 12> h{};
        std::vector<uint8_t> b;
        ByteWriter w(b);
        w.FourCC(type);
        w.U32(static_cast<uint32_t>(length));
        w.U8(static_cast<uint8_t>(c));
        w.U8(encrypted ? 0x01 : 0x00);
        w.U16(0);
        std::copy(b.begin(), b.end(), h.begin());
        return h;
    };
    std::vector<Stored> stored;
    stored.reserve(pending.size() + 1);
    if (password) {
        Stored record{ "SECU", makeHeader("SECU", secu.size(), Compression::Uncompressed, false), secu, secu.size() };
        stored.push_back(std::move(record));
    }
    for (const Pending& p : pending) {
        if (!ValidType(p.type)) { error = "\"" + p.type + "\" is not a section type"; return false; }
        if (p.payload.size() > UINT32_MAX) {
            error = "Section " + p.type + " is larger than a UCD section can hold (4 GiB)";
            return false;
        }
        Stored s{ p.type, {}, {}, p.payload.size() };
        if (p.compression == Compression::Deflate) {
            if (!DeflateZlib(p.payload, s.bytes)) { error = "Could not compress section " + p.type; return false; }
        } else if (p.compression == Compression::Uncompressed) {
            s.bytes = p.payload;
        } else {
            error = "Section " + p.type + ": only no compression or deflate is written";
            return false;
        }
        if (s.bytes.size() > UINT32_MAX - kNonceSize - kTagSize) {
            error = "Section " + p.type + " is larger than a UCD section can hold (4 GiB)";
            return false;
        }
        if (password) {
            s.header = makeHeader(p.type, kNonceSize + s.bytes.size() + kTagSize, p.compression, true);
            UltraCryptAeadParams params;       // a fresh nonce per section
            params.associatedData.assign(s.header.begin(), s.header.end());
            std::vector<uint8_t> sealed;
            if (!UltraCrypt_AeadSeal(key, params, s.bytes.data(), s.bytes.size(), sealed) ||
                params.nonce.size() != kNonceSize) {
                error = "Could not encrypt section " + p.type;
                return false;
            }
            s.bytes = params.nonce;
            s.bytes.insert(s.bytes.end(), sealed.begin(), sealed.end());
        } else {
            s.header = makeHeader(p.type, s.bytes.size(), p.compression, false);
        }
        stored.push_back(std::move(s));
    }

    // A private document keeps no preview outside its encryption (spec 2.4).
    const bool privateDoc = password && hidePreview;
    const std::vector<uint8_t> noThumbnail;
    const std::vector<uint8_t>& thumb = privateDoc ? noThumbnail : thumbnail;
    uint8_t flags = thumb.empty() ? 0 : static_cast<uint8_t>(FlagThumbnail | FlagThumbPng);
    if (password) flags |= FlagEncrypted;
    if (privateDoc) flags |= FlagPrivate;

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
    w.U8(password ? 1 : 0);                               // key source: none / password
    w.U8(flags);
    w.U16(0);
    w.U32(static_cast<uint32_t>(thumb.size()));
    w.U32(0);                                             // no header extension
    w.Bytes(thumb.data(), thumb.size());

    // ----- INDX (§4.2.1), first, uncompressed and never encrypted -----
    const uint64_t indexPayload = 8 + 20ull * stored.size();
    uint64_t at = out.size() + kSectionHeaderSize + indexPayload;
    std::vector<uint8_t> index;
    ByteWriter iw(index);
    iw.U8(1); iw.Zeros(3);
    iw.U32(static_cast<uint32_t>(stored.size()));
    for (const Stored& s : stored) {
        iw.FourCC(s.type);
        iw.U64(at);
        iw.U32(static_cast<uint32_t>(s.bytes.size()));
        iw.U32(static_cast<uint32_t>(s.whole));
        at += kSectionHeaderSize + s.bytes.size();
    }
    const auto indexHeader = makeHeader("INDX", index.size(), Compression::Uncompressed, false);
    w.Bytes(indexHeader.data(), indexHeader.size());
    w.Bytes(index.data(), index.size());
    for (const Stored& s : stored) {
        w.Bytes(s.header.data(), s.header.size());
        w.Bytes(s.bytes.data(), s.bytes.size());
    }
    return true;
}

bool Writer::WriteToFile(const std::string& path, std::string& error) const {
    std::vector<uint8_t> bytes;
    if (!WriteToBuffer(bytes, error)) return false;
    const std::string failure = WriteFileAtomically(path, [&bytes](const std::string& staged) -> std::string {
        errno = 0;
        std::FILE* f = OpenFileUtf8(staged, "wb");
        if (!f) return "Could not create the file. " + FileWriteFailureReason(staged, errno);
        const bool written = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        const bool closed = std::fclose(f) == 0;
        if (written && closed) return std::string();
        const int err = errno;
        // The staged file exists now; the probe asks the folder about it.
        return "Could not write the file. " + FileWriteFailureReason(staged, err);
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
