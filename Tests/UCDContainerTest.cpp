// Tests/UCDContainerTest.cpp
// The UCD v2 container (UltraCanvasUCDContainer.h) against its spec
// (Docs/UltraCanvas/UCD-FileFormat-v2.md): the bytes of the signature and
// header, the thumbnail's place, the INDX that points at every section, the
// zlib and gzip section compression, the header-default compression code,
// unknown sections listed rather than refused, and the refusals - a bad
// signature, a version 1 file, a later major version, a truncated file, a
// damaged stream, an encrypted section, a section past its size limit -
// and password protection: the SECU record and encrypted sections on disk,
// a wrong password told from a damaged file, tampering caught, a private
// document without its preview, failing closed without libsodium.
// Headless.
// Version: 1.1.0 - password protection
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "UltraCanvasUCDContainer.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;
static int checks = 0;

static void Check(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; std::printf("  [FAIL] %s\n", what); }
    else std::printf("  [PASS] %s\n", what);
}

static uint32_t U32At(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) |
           (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

static uint64_t U64At(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint64_t>(U32At(b, at)) | (static_cast<uint64_t>(U32At(b, at + 4)) << 32);
}

static uint32_t Crc32(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// A gzip member (RFC 1952) holding `data` in one stored deflate block.
static std::vector<uint8_t> Gzip(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out = { 0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0xFF };
    UCD::ByteWriter w(out);
    w.U8(0x01);                                     // final block, stored
    w.U16(static_cast<uint16_t>(data.size()));
    w.U16(static_cast<uint16_t>(~data.size()));
    w.Bytes(data.data(), data.size());
    w.U32(Crc32(data));
    w.U32(static_cast<uint32_t>(data.size()));
    return out;
}

// A container written byte by byte, for what the Writer never writes:
// the header-default code, gzip, LZMA, an encrypted flag.
static std::vector<uint8_t> HandMade(uint8_t major, uint8_t defaultCompression,
                                     const std::vector<std::pair<std::string, std::vector<uint8_t>>>& sections,
                                     const std::vector<std::pair<uint8_t, uint8_t>>& codesAndFlags) {
    std::vector<uint8_t> out;
    UCD::ByteWriter w(out);
    w.U8(0x89);
    w.Bytes("UCDoc\0\0\0", 8);
    w.U8(0x0D); w.U8(0x0A); w.U8(0x1A);
    w.U8(major); w.U8(1); w.U8(0); w.U8(defaultCompression); w.U8(0); w.U8(0); w.U16(0);
    w.U32(0); w.U32(0);
    for (size_t i = 0; i < sections.size(); ++i) {
        w.FourCC(sections[i].first);
        w.U32(static_cast<uint32_t>(sections[i].second.size()));
        w.U8(codesAndFlags[i].first);
        w.U8(codesAndFlags[i].second);
        w.U16(0);
        w.Bytes(sections[i].second.data(), sections[i].second.size());
    }
    return out;
}

static void TestWriterBytes() {
    std::printf("--- the bytes a writer puts down ---\n");
    std::vector<uint8_t> big(5000);
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i % 7);
    const std::vector<uint8_t> small = { 'h', 'e', 'l', 'l', 'o' };
    const std::vector<uint8_t> thumb = { 0x89, 'P', 'N', 'G', 1, 2, 3 };

    UCD::Writer w("UCBitmap");
    w.SetThumbnailPng(thumb);
    w.AddSection("BMAP", big);
    w.AddSection("META", small, UCD::Compression::Uncompressed);
    std::vector<uint8_t> file;
    std::string err;
    Check(w.WriteToBuffer(file, err), "a container is written");
    Check(file.size() > 28 && file[0] == 0x89 && std::memcmp(&file[1], "UCBitmap", 8) == 0 &&
          file[9] == 0x0D && file[10] == 0x0A && file[11] == 0x1A, "the signature: guard, descriptor, CR LF, EOF");
    Check(file[12] == 2 && file[13] == 2 && file[14] == 0 && file[15] == 1 && file[16] == 0,
          "version 2.2, binary, deflate by default, no key source");
    Check(file[17] == (UCD::FlagThumbnail | UCD::FlagThumbPng) && file[18] == 0 && file[19] == 0,
          "flags: a PNG thumbnail; reserved bytes zero");
    Check(U32At(file, 20) == thumb.size() && U32At(file, 24) == 0, "thumbnail length; no header extension");
    Check(std::equal(thumb.begin(), thumb.end(), file.begin() + 28), "the thumbnail straight after the header");

    // INDX first, uncompressed, pointing at each section's header.
    const size_t indx = 28 + thumb.size();
    Check(std::memcmp(&file[indx], "INDX", 4) == 0 && file[indx + 8] == 0, "INDX is the first section, uncompressed");
    const size_t p = indx + 12;
    Check(file[p] == 1 && U32At(file, p + 4) == 2, "INDX record version 1, two entries");
    bool pointsRight = true;
    const std::vector<uint8_t>* originals[2] = { &big, &small };
    for (int e = 0; e < 2; ++e) {
        const size_t entry = p + 8 + static_cast<size_t>(e) * 20;
        const uint64_t at = U64At(file, entry + 4);
        const uint32_t stored = U32At(file, entry + 12);
        const uint32_t whole = U32At(file, entry + 16);
        pointsRight = pointsRight && at + 12 + stored <= file.size() &&
                      std::memcmp(&file[at], &file[entry], 4) == 0 && U32At(file, static_cast<size_t>(at) + 4) == stored &&
                      whole == originals[e]->size();
    }
    Check(pointsRight, "each INDX entry names its section's header, stored and whole length");
    Check(U32At(file, p + 8 + 12) < big.size(), "a compressible section is stored smaller");
}

static void TestRoundTrip() {
    std::printf("--- what is written reads back ---\n");
    std::vector<uint8_t> big(100000, 7), small = { 1, 2, 3 };
    UCD::Writer w("UCVector");
    w.AddSection("VECT", big);
    w.AddSection("MEDI", small, UCD::Compression::Uncompressed);
    w.AddSection("VECT", { 9 });
    std::vector<uint8_t> file;
    std::string err;
    Check(w.WriteToBuffer(file, err), "written");
    UCD::Reader r;
    Check(r.OpenBuffer(file, err), "read");
    Check(r.GetHeader().descriptor == "UCVector" && !r.GetHeader().HasThumbnail(), "the descriptor, no thumbnail");
    Check(r.GetSections().size() == 4 && r.GetSections()[0].type == "INDX", "INDX and the three sections, in order");
    const auto vects = r.FindSections("VECT");
    Check(vects.size() == 2, "sections of one type in file order");
    std::vector<uint8_t> out;
    Check(vects.size() == 2 && r.Decode(*vects[0], out, err) && out == big, "a deflated section decodes");
    Check(vects.size() == 2 && r.Decode(*vects[1], out, err) && out == std::vector<uint8_t>{ 9 }, "and the next one");
    const auto medi = r.FindSections("MEDI");
    Check(medi.size() == 1 && r.Decode(*medi[0], out, err) && out == small, "an uncompressed one");
    Check(medi.size() == 1 && !r.Decode(*vects[0], out, err, 1000) && !err.empty(),
          "a section that would decompress past the limit is refused");

    // Through a file, and the filer's read of only the header and thumbnail.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ultracanvas-ucd-container";
    std::filesystem::create_directories(dir);
    const std::string path = PathToUtf8(dir / "round.ucd");
    UCD::Writer f("UCBitmap");
    f.SetThumbnailPng({ 0x89, 'P', 'N', 'G', 42 });
    f.AddSection("BMAP", big);
    Check(f.WriteToFile(path, err), "written to a file");
    UCD::Header h;
    std::vector<uint8_t> thumb;
    Check(UCD::ReadHeaderAndThumbnail(path, h, thumb, err) && h.descriptor == "UCBitmap" &&
          thumb == std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 42 }, "the header and thumbnail alone");
    UCD::Reader fr;
    Check(fr.Open(path, err) && fr.FindSections("BMAP").size() == 1, "the file read whole");
    std::filesystem::remove_all(dir);
}

static void TestWhatTheWriterNeverWrites() {
    std::printf("--- header default, gzip, unknown, LZMA, encrypted ---\n");
    const std::vector<uint8_t> words = { 'w', 'o', 'r', 'd', 's' };
    std::vector<uint8_t> zlib;
    UCD::DeflateZlib(words, zlib);
    const std::vector<uint8_t> file = HandMade(2, 2,
        { { "META", Gzip(words) }, { "ZZZZ", { 1, 2 } }, { "PAGE", zlib }, { "FORM", { 0 } }, { "WNDW", words } },
        { { 0xFF, 0 }, { 0, 0 }, { 1, 0 }, { 3, 0 }, { 0, 1 } });
    UCD::Reader r;
    std::string err;
    Check(r.OpenBuffer(file, err), "a hand-made file opens");
    const auto& s = r.GetSections();
    Check(s.size() == 5 && s[1].type == "ZZZZ", "an unknown section type is listed, not refused");
    std::vector<uint8_t> out;
    Check(s.size() == 5 && s[0].compression == UCD::Compression::Gzip && r.Decode(s[0], out, err) && out == words,
          "the header-default code means the header's compression - gzip here - and gzip decodes");
    Check(s.size() == 5 && r.Decode(s[2], out, err) && out == words, "an explicit deflate decodes");
    err.clear();
    Check(s.size() == 5 && !r.Decode(s[3], out, err) && err.find("compression") != std::string::npos,
          "LZMA is reported as a compression this does not read");
    err.clear();
    Check(s.size() == 5 && !r.Decode(s[4], out, err) && err.find("password") != std::string::npos,
          "an encrypted section says it needs a password");

    std::vector<uint8_t> broken = Gzip(words);
    broken[broken.size() - 6] ^= 0xFF;   // the CRC no longer matches
    const std::vector<uint8_t> badGzip = HandMade(2, 2, { { "META", broken } }, { { 0xFF, 0 } });
    UCD::Reader g;
    Check(g.OpenBuffer(badGzip, err) && !g.Decode(g.GetSections()[0], out, err), "a gzip section with a wrong CRC is damaged");
    std::vector<uint8_t> badZlib = zlib;
    badZlib[badZlib.size() - 1] ^= 0xFF;  // the Adler-32 no longer matches
    const std::vector<uint8_t> badDeflate = HandMade(2, 1, { { "META", badZlib } }, { { 1, 0 } });
    UCD::Reader z;
    Check(z.OpenBuffer(badDeflate, err) && !z.Decode(z.GetSections()[0], out, err), "a deflate section with a wrong checksum is damaged");
}

static void TestRefusals() {
    std::printf("--- what is not a UCD v2 file ---\n");
    UCD::Writer w("UCDoc");
    w.AddSection("META", { 1, 2, 3, 4 });
    std::vector<uint8_t> good;
    std::string err;
    w.WriteToBuffer(good, err);
    UCD::Reader r;

    std::vector<uint8_t> bad = good;
    bad[10] = 0x0D;   // the CR LF pair altered, as a text-mode transfer would
    err.clear();
    Check(!r.OpenBuffer(bad, err) && !err.empty(), "a damaged signature is refused");
    err.clear();
    const std::vector<uint8_t> v1 = { 'U', 'C', 'D', 0x01, 0, 0, 0, 0 };
    Check(!r.OpenBuffer(v1, err) && err.find("version 1") != std::string::npos, "a v1 file says it is version 1");
    bad = good;
    bad[12] = 3;
    err.clear();
    Check(!r.OpenBuffer(bad, err) && err.find("version 3") != std::string::npos, "a later major version is refused");
    bad = good;
    bad.pop_back();
    err.clear();
    Check(!r.OpenBuffer(bad, err) && err.find("truncated") != std::string::npos, "a file cut short is refused");
    bad.assign(good.begin(), good.end() - 10);
    err.clear();
    Check(!r.OpenBuffer(bad, err) && err.find("truncated") != std::string::npos, "and one cut inside a section");
    bad = good;
    bad[13] = 7;   // a later minor version: read the same way
    Check(r.OpenBuffer(bad, err) && r.GetHeader().versionMinor == 7, "a later minor version opens");

    std::vector<uint8_t> out;
    UCD::Writer badType("UCDoc");
    badType.AddSection("AB", { 1 });
    Check(!badType.WriteToBuffer(out, err), "a section type that is not four characters is refused");
    UCD::Writer badDescriptor("UCDocumentLong");
    Check(!badDescriptor.WriteToBuffer(out, err), "a descriptor over eight characters is refused");
}

static void TestBytes() {
    std::printf("--- preamble and byte helpers ---\n");
    std::vector<uint8_t> b;
    UCD::AppendPreamble(b, "PNG", 1);
    std::string code;
    uint8_t version = 0;
    Check(b.size() == 8 && UCD::ReadPreamble(b, code, version) && code == "PNG " && version == 1 &&
          b[5] == 0 && b[6] == 0 && b[7] == 0, "a preamble: space-padded FourCC, version, reserved zero");
    Check(!UCD::ReadPreamble({ 1, 2, 3 }, code, version), "a payload shorter than a preamble has none");

    std::string long2;   // 40000 two-byte characters: 80000 bytes
    for (int i = 0; i < 40000; ++i) long2 += "\xC3\xA9";
    std::vector<uint8_t> s;
    UCD::ByteWriter w(s);
    w.String16(long2);
    w.F64(-2.5);
    w.I32(-7);
    UCD::ByteReader r(s);
    std::string back;
    double d = 0;
    int32_t i32 = 0;
    Check(r.String16(back) && back.size() == 65534 && back == long2.substr(0, 65534),
          "a string over 65535 bytes is cut at a character boundary");
    Check(r.F64(d) && d == -2.5 && r.I32(i32) && i32 == -7, "doubles and signed integers round-trip");
    uint32_t more = 0;
    Check(!r.U32(more) && !r.Ok(), "a read past the end fails and leaves the reader failed");
}

// Where in `file` section `type` starts (its 12-byte header); 0 when absent.
static size_t SectionAt(const std::vector<uint8_t>& file, const std::string& type) {
    UCD::Reader r;
    std::string err;
    if (!r.OpenBuffer(file, err)) return 0;
    for (const UCD::Section& s : r.GetSections()) if (s.type == type) return static_cast<size_t>(s.offset);
    return 0;
}

static void TestPasswordProtection() {
    std::printf("--- password protection ---\n");
    std::vector<uint8_t> pixels(20000);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>("SECRETPIXELS"[i % 12]);
    const std::vector<uint8_t> thumb = { 0x89, 'P', 'N', 'G', 7 };
    std::string err;

    if (!UCD::EncryptionAvailable()) {
        UCD::Writer w("UCBitmap");
        w.SetPassword("pw");
        w.AddSection("BMAP", pixels);
        std::vector<uint8_t> out;
        Check(!w.WriteToBuffer(out, err) && !err.empty(),
              "without libsodium a protected write fails rather than writing it in the clear");
        std::printf("  (libsodium not available: the rest is skipped)\n");
        return;
    }

    UCD::Writer w("UCBitmap");
    w.SetThumbnailPng(thumb);
    w.SetPassword("correct horse \xC3\xA9");
    w.AddSection("BMAP", pixels);
    w.AddSection("META", { 'm', 'e', 't', 'a' }, UCD::Compression::Uncompressed);
    std::vector<uint8_t> file;
    Check(w.WriteToBuffer(file, err), "a protected document is written");
    Check(file[16] == 1 && (file[17] & UCD::FlagEncrypted) && (file[17] & UCD::FlagThumbnail),
          "key source 1 (password), ENCRYPTED set, the thumbnail kept");
    const size_t indx = 28 + thumb.size();
    Check(std::memcmp(&file[indx], "INDX", 4) == 0, "INDX is still first");
    const size_t secu = SectionAt(file, "SECU");
    Check(secu == indx + 12 + U32At(file, indx + 4), "SECU follows it");
    Check(secu && file[secu + 8] == 0 && file[secu + 9] == 0 && U32At(file, secu + 4) == 76 &&
          file[secu + 12] == 1 && file[secu + 13] == 1 && file[secu + 14] == 1 && U32At(file, secu + 12 + 12) == 1,
          "SECU: uncompressed, in the clear, 76 bytes; Argon2id, XChaCha20-Poly1305, one lane");
    const size_t bmap = SectionAt(file, "BMAP");
    Check(bmap && (file[bmap + 9] & 1) && file[bmap + 8] == 1, "BMAP: encrypted, and says it was deflated before");
    const std::string plain = "SECRETPIXELS";
    Check(std::search(file.begin(), file.end(), plain.begin(), plain.end()) == file.end(),
          "nothing of the content is readable in the file");
    const std::string metaText = "meta";
    Check(std::search(file.begin(), file.end(), metaText.begin(), metaText.end()) == file.end(),
          "an uncompressed section is encrypted too");

    UCD::Reader r;
    Check(r.OpenBuffer(file, err) && r.NeedsPassword(), "it opens, and says it needs a password");
    Check(r.GetThumbnail() == thumb, "the thumbnail is readable without one");
    std::vector<uint8_t> out;
    const auto bmaps = r.FindSections("BMAP");
    err.clear();
    Check(bmaps.size() == 1 && !r.Decode(*bmaps[0], out, err) && err.find("password") != std::string::npos,
          "an encrypted section does not decode before the password");
    err.clear();
    Check(!r.Unlock("correct horse", err) && err.find("not right") != std::string::npos && r.NeedsPassword(),
          "a wrong password is told as such");
    Check(r.Unlock("correct horse \xC3\xA9", err) && !r.NeedsPassword(), "the right one unlocks it");
    Check(bmaps.size() == 1 && r.Decode(*bmaps[0], out, err) && out == pixels, "and the section decrypts and inflates");
    const auto metas = r.FindSections("META");
    Check(metas.size() == 1 && r.Decode(*metas[0], out, err) && out == std::vector<uint8_t>{ 'm', 'e', 't', 'a' },
          "an uncompressed one too");

    // Tampering: a byte of ciphertext, a byte of the header (the associated data).
    std::vector<uint8_t> bad = file;
    bad[bmap + 12 + 40] ^= 0x01;
    UCD::Reader t;
    err.clear();
    Check(t.OpenBuffer(bad, err) && t.Unlock("correct horse \xC3\xA9", err) &&
          !t.Decode(*t.FindSections("BMAP")[0], out, err) && err.find("damaged") != std::string::npos,
          "a changed byte of ciphertext is caught, as damage - not as a wrong password");
    bad = file;
    bad[bmap + 10] = 1;   // the reserved bytes of its header
    UCD::Reader u;
    Check(u.OpenBuffer(bad, err) && u.Unlock("correct horse \xC3\xA9", err) &&
          !u.Decode(*u.FindSections("BMAP")[0], out, err), "an edited section header fails the tag");
    bad = file;
    bad[secu + 12 + 8] = 0xFF; bad[secu + 12 + 9] = 0xFF; bad[secu + 12 + 10] = 0xFF; bad[secu + 12 + 11] = 0xFF;
    UCD::Reader v;
    err.clear();
    Check(v.OpenBuffer(bad, err) && !v.Unlock("correct horse \xC3\xA9", err) && err.find("damaged") != std::string::npos,
          "a SECU asking for 4 TiB of memory is refused before anything is derived");
    bad = file;
    bad[secu + 12 + 4] = 1;   // one pass instead of the stored cost
    UCD::Reader x;
    Check(x.OpenBuffer(bad, err) && !x.Unlock("correct horse \xC3\xA9", err), "lowered costs do not pass the check");

    // Every save draws a new salt and new nonces.
    std::vector<uint8_t> again;
    w.WriteToBuffer(again, err);
    Check(again.size() == file.size() && again != file, "saving again writes other bytes");

    // A private document leaves the preview out.
    UCD::Writer hidden("UCBitmap");
    hidden.SetThumbnailPng(thumb);
    hidden.SetPassword("pw", true);
    hidden.AddSection("BMAP", pixels);
    std::vector<uint8_t> priv;
    Check(hidden.WriteToBuffer(priv, err) && (priv[17] & UCD::FlagPrivate) && !(priv[17] & UCD::FlagThumbnail) &&
          U32At(priv, 20) == 0, "hidePreview: PRIVATE, and no thumbnail");
    UCD::Reader pr;
    Check(pr.OpenBuffer(priv, err) && pr.GetHeader().IsPrivate() && pr.GetThumbnail().empty() &&
          pr.Unlock("pw", err), "and it still opens with its password");

    // An empty password protects nothing; an unprotected file needs no Unlock.
    UCD::Writer open("UCBitmap");
    open.SetPassword("pw");
    open.SetPassword("");
    open.AddSection("BMAP", { 1 });
    std::vector<uint8_t> clear;
    UCD::Reader cr;
    Check(open.WriteToBuffer(clear, err) && clear[16] == 0 && cr.OpenBuffer(clear, err) && !cr.NeedsPassword() &&
          cr.Unlock("anything", err), "an empty password writes an unprotected file");
}

int main() {
    std::printf("========================================\n   UCD Container Suite\n========================================\n");
    TestWriterBytes();
    TestRoundTrip();
    TestWhatTheWriterNeverWrites();
    TestRefusals();
    TestBytes();
    TestPasswordProtection();
    std::printf("Tests: %d, failed: %d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
