// Plugins/Documents/eBook/MOBIEngine.cpp
// Mobipocket / Kindle engine: PDB records → PalmDOC-decompressed HTML →
// chapters + images, fed to HTML::ElementBuilder like every other engine.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "MOBIEngine.h"

#include "HTMLReader/HTMLDocument.h"   // HTML::ExtractPlainText

#include <algorithm>
#include <cctype>
#include <cstring>

namespace UltraCanvas {

namespace {

// EXTH record types we care about.
enum : uint32_t {
    kExthAuthor = 100,
    kExthPublisher = 101,
    kExthDescription = 103,
    kExthIsbn = 104,
    kExthSubject = 105,
    kExthDate = 106,
    kExthRights = 109,
    kExthCoverOffset = 201,
    kExthUpdatedTitle = 503,
    kExthLanguage = 524,
};

// windows-1252 mapping for bytes 0x80..0x9F (the only range that differs from
// Latin-1). 0x00 marks an undefined slot (kept as the raw byte).
constexpr uint16_t kCp1252High[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178
};

void AppendUTF8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string LowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Case-insensitive substring search from `from`.
size_t FindCI(const std::string& hay, const std::string& needleLower, size_t from) {
    for (size_t i = from; i + needleLower.size() <= hay.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < needleLower.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(hay[i + j])) != needleLower[j]) {
                ok = false;
                break;
            }
        }
        if (ok) return i;
    }
    return std::string::npos;
}

std::string TrimCopy(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

// ============================================================================
// BYTE READERS
// ============================================================================

uint16_t MOBIEngine::ReadBE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t MOBIEngine::ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// ============================================================================
// ENCODING
// ============================================================================

std::string MOBIEngine::Cp1252ToUTF8(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0xA0) {
            uint16_t cp = kCp1252High[c - 0x80];
            AppendUTF8(out, cp ? cp : c);
        } else {
            AppendUTF8(out, c);   // 0xA0..0xFF map 1:1 to U+00A0..U+00FF
        }
    }
    return out;
}

// ============================================================================
// PALMDOC (LZ77) DECOMPRESSION
// ============================================================================

std::vector<uint8_t> MOBIEngine::DecompressPalmDOC(const uint8_t* data, size_t size) {
    std::vector<uint8_t> out;
    out.reserve(size * 4);

    size_t i = 0;
    while (i < size) {
        uint8_t c = data[i++];
        if (c == 0x00) {
            out.push_back(0x00);
        } else if (c <= 0x08) {
            // Copy the next c bytes literally.
            for (int j = 0; j < c && i < size; ++j) out.push_back(data[i++]);
        } else if (c <= 0x7F) {
            out.push_back(c);
        } else if (c <= 0xBF) {
            // 2-byte length/distance pair.
            if (i >= size) break;
            uint16_t pair = static_cast<uint16_t>((c << 8) | data[i++]);
            int distance = (pair >> 3) & 0x07FF;
            int length = (pair & 0x07) + 3;
            if (distance == 0) break;
            for (int j = 0; j < length; ++j) {
                if (distance > static_cast<int>(out.size())) break;
                out.push_back(out[out.size() - distance]);
            }
        } else {
            // 0xC0..0xFF: space followed by (c ^ 0x80).
            out.push_back(' ');
            out.push_back(static_cast<uint8_t>(c ^ 0x80));
        }
    }
    return out;
}

// ============================================================================
// TRAILING DATA ENTRIES
// ============================================================================

namespace {

// Size (in bytes) of the trailing entry at the end of `ptr[0..size)`,
// read as a backwards big-endian base-128 varint (KindleUnpack algorithm).
uint32_t BackwardVarintSize(const uint8_t* ptr, size_t size) {
    uint32_t bitpos = 0, result = 0;
    size_t p = size;
    while (p > 0) {
        uint8_t v = ptr[p - 1];
        result |= static_cast<uint32_t>(v & 0x7F) << bitpos;
        bitpos += 7;
        --p;
        if ((v & 0x80) || bitpos >= 28) break;
    }
    return result;
}

} // namespace

size_t MOBIEngine::TextRecordPayloadSize(const std::vector<uint8_t>& record) const {
    size_t size = record.size();
    // Each set bit above bit 0 marks an appended trailing entry, sized by a
    // backwards varint at the very end.
    for (uint16_t flags = extraDataFlags >> 1; flags; flags >>= 1) {
        if (!(flags & 1)) continue;
        if (size == 0) break;
        uint32_t entry = BackwardVarintSize(record.data(), size);
        if (entry == 0 || entry > size) break;
        size -= entry;
    }
    // Bit 0: a trailing multibyte-overlap run whose length is in the low bits
    // of the last remaining byte.
    if ((extraDataFlags & 1) && size > 0) {
        size_t n = (record[size - 1] & 0x03) + 1;
        size = (n <= size) ? size - n : 0;
    }
    return size;
}

// ============================================================================
// LIFECYCLE
// ============================================================================

bool MOBIEngine::LoadFromMemory(std::vector<uint8_t> data,
                                const std::string& password) {
    (void)password;
    Close();

    ReportProgress(0.1f, "Reading records...");
    if (!ParseRecords(data.data(), data.size())) return false;

    ReportProgress(0.3f, "Reading headers...");
    if (!ParseRecord0()) return false;

    ReportProgress(0.6f, "Decompressing text...");
    if (!DecompressText()) return false;

    ExtractImages();
    BuildMetadata();
    BuildChapters();
    if (chapters.empty()) {
        Fail("MOBI document has no readable text");
        return false;
    }
    BuildTOC();

    metadata.format = format;
    metadata.chapterCount = static_cast<int>(chapters.size());
    metadata.hasCover = !GetCoverImage().empty();

    loaded = true;
    ReportProgress(1.0f, "Complete");
    return true;
}

void MOBIEngine::Close() {
    ResetBaseState();
    records.clear();
    chapters.clear();
    images.clear();
    exth.clear();
    fullHtml.clear();
    fullName.clear();
    coverImageNumber = -1;
    compression = 1;
    textRecordCount = 0;
    firstImageIndex = 0;
    textEncoding = 1252;
    exthCoverOffset = 0xFFFFFFFF;
    extraDataFlags = 0;
    hasExth = false;
    format = EBookFormat::MOBI;
    formatName = "Mobipocket";
}

// ============================================================================
// PDB RECORDS
// ============================================================================

bool MOBIEngine::ParseRecords(const uint8_t* data, size_t size) {
    if (size < 78) {
        Fail("Not a MOBI file: too small");
        return false;
    }

    std::string type(reinterpret_cast<const char*>(data + 60), 4);
    std::string creator(reinterpret_cast<const char*>(data + 64), 4);
    if (!((type == "BOOK" && creator == "MOBI") ||
          (type == "TEXt" && creator == "REAd"))) {
        Fail("Not a MOBI file (type '" + type + "', creator '" + creator + "')");
        return false;
    }

    pdbName.assign(reinterpret_cast<const char*>(data),
                   ::strnlen(reinterpret_cast<const char*>(data), 32));

    uint16_t recordCount = ReadBE16(data + 76);
    if (size < 78 + static_cast<size_t>(recordCount) * 8) {
        Fail("Not a MOBI file: record list truncated");
        return false;
    }

    std::vector<uint32_t> offsets;
    offsets.reserve(recordCount);
    for (int i = 0; i < recordCount; ++i) {
        offsets.push_back(ReadBE32(data + 78 + i * 8));
    }

    records.clear();
    records.reserve(recordCount);
    for (size_t i = 0; i < offsets.size(); ++i) {
        uint32_t start = offsets[i];
        uint32_t end = (i + 1 < offsets.size()) ? offsets[i + 1]
                                                : static_cast<uint32_t>(size);
        if (start > size || end > size || end < start) {
            records.emplace_back();   // tolerate a bad entry
            continue;
        }
        records.emplace_back(data + start, data + end);
    }

    if (records.empty()) {
        Fail("MOBI file has no records");
        return false;
    }
    return true;
}

bool MOBIEngine::ParseRecord0() {
    const std::vector<uint8_t>& r0 = records[0];
    if (r0.size() < 16) {
        Fail("MOBI record 0 too small");
        return false;
    }

    compression = ReadBE16(r0.data());
    textRecordCount = ReadBE16(r0.data() + 8);
    uint16_t encryption = ReadBE16(r0.data() + 12);
    if (encryption != 0) {
        Fail("MOBI file is DRM-encrypted");
        return false;
    }
    if (compression == 17480) {
        Fail("HUFF/CDIC-compressed MOBI is not supported");
        return false;
    }
    if (compression != 1 && compression != 2) {
        Fail("Unknown MOBI compression type");
        return false;
    }

    // MOBI header at offset 16 (older PalmDOC-only files omit it).
    if (r0.size() >= 20 && std::memcmp(r0.data() + 16, "MOBI", 4) == 0) {
        const uint8_t* m = r0.data() + 16;
        size_t mSize = r0.size() - 16;
        uint32_t headerLength = ReadBE32(m + 4);

        if (mSize >= 24) textEncoding = ReadBE32(m + 12);
        uint32_t fileVersion = (mSize >= 24) ? ReadBE32(m + 20) : 0;
        if (mSize >= 92) {
            uint32_t fullNameOffset = ReadBE32(m + 84);
            uint32_t fullNameLength = ReadBE32(m + 88);
            if (fullNameOffset > 0 && fullNameOffset < r0.size()) {
                size_t len = std::min(static_cast<size_t>(fullNameLength),
                                      r0.size() - fullNameOffset);
                fullName.assign(reinterpret_cast<const char*>(r0.data() + fullNameOffset), len);
            }
        }
        if (mSize >= 112) firstImageIndex = ReadBE32(m + 108);
        if (mSize >= 132) hasExth = (ReadBE32(m + 128) & 0x40) != 0;
        // extraDataFlags: uint16 at MOBI-header offset 242 (0xF2).
        if (headerLength >= 244 && mSize >= 244) {
            extraDataFlags = ReadBE16(m + 242);
        }

        // A pure-KF8 record 0 (file version >= 8) needs the KF8 fragment
        // reassembly we do not implement; combo .azw3 keep MOBI6 here.
        if (fileVersion >= 8) {
            Fail("KF8-only AZW3 is not supported yet (combo AZW3 and MOBI work)");
            return false;
        }

        if (hasExth) {
            size_t exthOffset = 16 + headerLength;
            if (exthOffset + 12 <= r0.size()) {
                ParseExth(r0.data() + exthOffset, r0.size() - exthOffset);
            }
        }
    }

    // A combo AZW3 carries a KF8 boundary (EXTH 121) pointing at its KF8 half;
    // we read the MOBI6 half but label it honestly.
    if (exth.count(121)) {
        format = EBookFormat::AZW3;
        formatName = "Kindle KF8/AZW3 (MOBI 6 view)";
    } else {
        format = EBookFormat::MOBI;
        formatName = "Mobipocket";
    }
    return true;
}

void MOBIEngine::ParseExth(const uint8_t* data, size_t size) {
    if (size < 12 || std::memcmp(data, "EXTH", 4) != 0) return;
    uint32_t headerLength = ReadBE32(data + 4);
    uint32_t count = ReadBE32(data + 8);
    const uint8_t* ptr = data + 12;
    const uint8_t* end = data + std::min(size, static_cast<size_t>(headerLength));

    for (uint32_t i = 0; i < count && ptr + 8 <= end; ++i) {
        uint32_t type = ReadBE32(ptr);
        uint32_t length = ReadBE32(ptr + 4);
        if (length < 8 || ptr + length > end) break;
        size_t dataLen = length - 8;

        if (type == kExthCoverOffset && dataLen >= 4) {
            exthCoverOffset = ReadBE32(ptr + 8);
        } else {
            exth[type].emplace_back(reinterpret_cast<const char*>(ptr + 8), dataLen);
        }
        ptr += length;
    }
}

// ============================================================================
// TEXT + IMAGES
// ============================================================================

bool MOBIEngine::DecompressText() {
    std::vector<uint8_t> text;
    uint32_t last = std::min<uint32_t>(textRecordCount,
                                       static_cast<uint32_t>(records.size()) - 1);
    for (uint32_t i = 1; i <= last; ++i) {
        const std::vector<uint8_t>& rec = records[i];
        size_t payload = TextRecordPayloadSize(rec);
        if (payload == 0) continue;

        if (compression == 2) {
            std::vector<uint8_t> chunk = DecompressPalmDOC(rec.data(), payload);
            text.insert(text.end(), chunk.begin(), chunk.end());
        } else {
            text.insert(text.end(), rec.begin(), rec.begin() + payload);
        }
    }

    std::string raw(text.begin(), text.end());
    fullHtml = (textEncoding == 65001) ? raw : Cp1252ToUTF8(raw);
    return true;
}

void MOBIEngine::ExtractImages() {
    if (firstImageIndex == 0 || firstImageIndex >= records.size()) return;

    for (size_t i = firstImageIndex; i < records.size(); ++i) {
        const std::vector<uint8_t>& rec = records[i];
        bool isImage = rec.size() >= 4 &&
            ((rec[0] == 0xFF && rec[1] == 0xD8) ||                       // JPEG
             (rec[0] == 0x89 && rec[1] == 'P' && rec[2] == 'N' && rec[3] == 'G') ||
             (rec[0] == 'G' && rec[1] == 'I' && rec[2] == 'F') ||       // GIF
             (rec[0] == 'B' && rec[1] == 'M'));                          // BMP
        // Trailing metadata records (FLIS/FCIS/SRCS/...) stop the image run.
        if (!isImage) {
            if (rec.size() >= 4) {
                std::string sig(reinterpret_cast<const char*>(rec.data()), 4);
                if (sig == "FLIS" || sig == "FCIS" || sig == "SRCS" ||
                    sig == "DATP" || sig == "FDST" || sig == "BOUN") {
                    break;
                }
            }
            images.emplace_back();   // keep 1-based indexing aligned with recindex
            continue;
        }
        images.push_back(rec);
    }

    if (exthCoverOffset != 0xFFFFFFFF) {
        // Cover offset is relative to firstImageIndex → 1-based image number.
        coverImageNumber = static_cast<int>(exthCoverOffset) + 1;
    }
}

// ============================================================================
// METADATA
// ============================================================================

std::string MOBIEngine::GetExthString(uint32_t type) const {
    auto it = exth.find(type);
    return (it != exth.end() && !it->second.empty()) ? it->second.front() : std::string();
}

std::vector<std::string> MOBIEngine::GetExthStrings(uint32_t type) const {
    auto it = exth.find(type);
    return it != exth.end() ? it->second : std::vector<std::string>();
}

void MOBIEngine::BuildMetadata() {
    auto decode = [this](const std::string& s) {
        return (textEncoding == 65001) ? s : Cp1252ToUTF8(s);
    };

    std::string title = GetExthString(kExthUpdatedTitle);
    if (title.empty()) title = fullName;
    if (title.empty()) title = pdbName;
    metadata.title = TrimCopy(decode(title));

    for (const std::string& a : GetExthStrings(kExthAuthor)) {
        std::string author = TrimCopy(decode(a));
        if (!author.empty()) metadata.authors.push_back(author);
    }
    metadata.publisher = TrimCopy(decode(GetExthString(kExthPublisher)));
    metadata.description = TrimCopy(decode(GetExthString(kExthDescription)));
    metadata.isbn = TrimCopy(GetExthString(kExthIsbn));
    metadata.language = TrimCopy(GetExthString(kExthLanguage));
    metadata.publicationDate = TrimCopy(GetExthString(kExthDate));
    metadata.rights = TrimCopy(decode(GetExthString(kExthRights)));
    for (const std::string& s : GetExthStrings(kExthSubject)) {
        std::string subject = TrimCopy(decode(s));
        if (!subject.empty()) metadata.subjects.push_back(subject);
    }
}

// ============================================================================
// CHAPTERS
// ============================================================================

namespace {

// Strip tags/entities and collapse whitespace — for chapter titles.
std::string HeadingText(const std::string& html) {
    std::string plain = HTML::ExtractPlainText(html);
    return TrimCopy(plain);
}

// First <h1..h6> heading text inside a chapter fragment, or "".
std::string FirstHeading(const std::string& html) {
    for (char level = '1'; level <= '6'; ++level) {
        std::string open = std::string("<h") + level;
        size_t a = FindCI(html, open, 0);
        while (a != std::string::npos) {
            size_t gt = html.find('>', a);
            if (gt == std::string::npos) break;
            std::string closeTag = std::string("</h") + level;
            size_t close = FindCI(html, closeTag, gt);
            if (close == std::string::npos) break;
            std::string text = HeadingText(html.substr(gt + 1, close - gt - 1));
            if (!text.empty()) return text;
            a = FindCI(html, open, close);
        }
    }
    return {};
}

} // namespace

void MOBIEngine::BuildChapters() {
    if (fullHtml.empty()) return;

    // Rewrite Mobipocket image references to resolvable resource hrefs:
    //   <img recindex="0001" ...>          → <img src="mobiimg/1" ...>
    //   <img src="kindle:embed:0001?...">  → <img src="mobiimg/1" ...>
    auto matchesCI = [&](const std::string& s, size_t pos, const char* lit) {
        size_t n = std::strlen(lit);
        if (pos + n > s.size()) return false;
        for (size_t k = 0; k < n; ++k) {
            if (std::tolower(static_cast<unsigned char>(s[pos + k])) != lit[k]) return false;
        }
        return true;
    };

    std::string html;
    html.reserve(fullHtml.size());
    for (size_t i = 0; i < fullHtml.size();) {
        // recindex="N"
        if (matchesCI(fullHtml, i, "recindex=")) {
            size_t j = i + 9;
            char quote = (j < fullHtml.size() && (fullHtml[j] == '"' || fullHtml[j] == '\'')) ? fullHtml[j] : 0;
            if (quote) ++j;
            std::string digits;
            while (j < fullHtml.size() && std::isdigit(static_cast<unsigned char>(fullHtml[j]))) {
                digits += fullHtml[j++];
            }
            if (quote && j < fullHtml.size() && fullHtml[j] == quote) ++j;
            long n = digits.empty() ? 0 : std::stol(digits);
            html += "src=\"mobiimg/" + std::to_string(n) + "\"";
            i = j;
            continue;
        }
        html += fullHtml[i++];
    }
    // kindle:embed:NNNN → mobiimg/N (strip leading zeros and any query).
    for (size_t k = FindCI(html, "kindle:embed:", 0); k != std::string::npos;
         k = FindCI(html, "kindle:embed:", k)) {
        size_t d = k + 13;
        std::string digits;
        while (d < html.size() && std::isdigit(static_cast<unsigned char>(html[d]))) {
            digits += html[d++];
        }
        size_t endTok = d;
        while (endTok < html.size() && html[endTok] != '"' && html[endTok] != '\'') ++endTok;
        long n = digits.empty() ? 0 : std::stol(digits);
        html.replace(k, endTok - k, "mobiimg/" + std::to_string(n));
        k += 8;
    }

    // Split on Mobipocket page-break markers.
    std::vector<size_t> breaks;
    for (size_t p = FindCI(html, "<mbp:pagebreak", 0); p != std::string::npos;
         p = FindCI(html, "<mbp:pagebreak", p + 1)) {
        breaks.push_back(p);
    }

    std::vector<std::string> pieces;
    if (breaks.empty()) {
        pieces.push_back(html);
    } else {
        if (breaks.front() > 0) pieces.push_back(html.substr(0, breaks.front()));
        for (size_t i = 0; i < breaks.size(); ++i) {
            size_t start = breaks[i];
            size_t end = (i + 1 < breaks.size()) ? breaks[i + 1] : html.size();
            pieces.push_back(html.substr(start, end - start));
        }
    }

    int ordinal = 0;
    for (const std::string& piece : pieces) {
        // Skip pieces with no visible text and no image.
        if (HeadingText(piece).empty() && FindCI(piece, "<img", 0) == std::string::npos) {
            continue;
        }
        EBookChapter chapter;
        chapter.content = piece;
        chapter.title = FirstHeading(piece);
        if (chapter.title.empty()) {
            chapter.title = "Section " + std::to_string(++ordinal);
        }
        chapters.push_back(std::move(chapter));
    }

    // Everything got filtered (e.g. a book that is one untitled blob): keep it.
    if (chapters.empty() && !HeadingText(html).empty()) {
        EBookChapter chapter;
        chapter.content = html;
        chapter.title = metadata.title.empty() ? "Book" : metadata.title;
        chapters.push_back(std::move(chapter));
    }
}

void MOBIEngine::BuildTOC() {
    tableOfContents.clear();
    tableOfContents.reserve(chapters.size());
    for (size_t i = 0; i < chapters.size(); ++i) {
        EBookTOCEntry entry(chapters[i].title, chapters[i].href,
                            static_cast<int>(i));
        entry.index = static_cast<int>(i);
        tableOfContents.push_back(std::move(entry));
    }
}

// ============================================================================
// CONTENT ACCESS
// ============================================================================

EBookChapter MOBIEngine::GetChapter(int index) const {
    if (index < 0 || index >= static_cast<int>(chapters.size())) return {};
    return chapters[index];
}

std::string MOBIEngine::GetStylesheets() const {
    return {};   // MOBI carries presentational HTML; no separate stylesheet.
}

std::vector<uint8_t> MOBIEngine::GetResource(const std::string& href) const {
    // Accept "mobiimg/N" (and a bare "N").
    std::string id = href;
    size_t slash = id.find_last_of('/');
    if (slash != std::string::npos) id = id.substr(slash + 1);
    if (id.empty()) return {};
    for (char c : id) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return {};
    }
    long n = std::stol(id);
    if (n < 1 || n > static_cast<long>(images.size())) return {};
    return images[n - 1];
}

std::vector<uint8_t> MOBIEngine::GetCoverImage() const {
    if (coverImageNumber >= 1 && coverImageNumber <= static_cast<int>(images.size())) {
        const std::vector<uint8_t>& cover = images[coverImageNumber - 1];
        if (!cover.empty()) return cover;
    }
    for (const std::vector<uint8_t>& img : images) {
        if (!img.empty()) return img;
    }
    return {};
}

// ============================================================================
// REGISTRATION
// ============================================================================

void RegisterMOBIEngine() {
    RegisterEBookEngine({"mobi", "prc", "azw", "azw3"}, [] {
        return std::static_pointer_cast<IEBookEngine>(std::make_shared<MOBIEngine>());
    });
}

} // namespace UltraCanvas
