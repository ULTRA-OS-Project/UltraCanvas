// core/ClipboardHistory/UltraCanvasClipboardHistory.cpp
// The persistent clipboard history: SQLite index (UltraDatabase), payloads as
// encrypted files named by a keyed hash, image thumbnails, retention, the
// recorder lease. See include/UltraCanvasClipboardHistory.h.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasFileError.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "DataFormats/UltraCanvasJSON.h"
#include <UltraCrypt/UltraCryptCore.h>
#ifdef ULTRACANVAS_HAS_DATABASE
#include <UltraDatabase/UltraDatabase.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <unordered_map>

namespace UltraCanvas {

namespace {

namespace fs = std::filesystem;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

std::vector<uint8_t> Bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::string String(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

// Cut at a UTF-8 character boundary at or before `maxBytes`.
std::string Utf8Prefix(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
    return text.substr(0, n);
}

std::string Trimmed(const std::string& text) {
    const char* space = " \t\r\n\f\v";
    const size_t begin = text.find_first_not_of(space);
    if (begin == std::string::npos) return "";
    const size_t end = text.find_last_not_of(space);
    return text.substr(begin, end - begin + 1);
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        start = end + 1;
    }
    return lines;
}

// The first line with something on it, tabs as spaces, at most 200 bytes.
std::string FirstLine(const std::string& text) {
    for (const std::string& raw : SplitLines(text)) {
        std::string line = Trimmed(raw);
        if (line.empty()) continue;
        std::replace(line.begin(), line.end(), '\t', ' ');
        if (line.size() > 200) line = Utf8Prefix(line, 200) + "\xE2\x80\xA6";   // …
        return line;
    }
    return "";
}

bool IsUrl(const std::string& text) {
    if (text.empty() || text.size() > 4096) return false;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) return false;
    }
    const size_t colon = text.find("://");
    if (colon == std::string::npos || colon == 0 || colon + 3 >= text.size()) return false;
    if (!std::isalpha(static_cast<unsigned char>(text[0]))) return false;
    for (size_t i = 1; i < colon; ++i) {
        const char c = text[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.') return false;
    }
    return true;
}

bool IsColour(const std::string& text) {
    if (text.empty() || text.size() > 64) return false;
    if (text[0] == '#') {
        const size_t digits = text.size() - 1;
        if (digits != 3 && digits != 4 && digits != 6 && digits != 8) return false;
        return std::all_of(text.begin() + 1, text.end(),
                           [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
    }
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* prefix : {"rgb(", "rgba(", "hsl(", "hsla("}) {
        if (lower.rfind(prefix, 0) == 0 && lower.back() == ')') {
            return std::all_of(lower.begin() + std::strlen(prefix), lower.end() - 1, [](char c) {
                return std::isdigit(static_cast<unsigned char>(c)) || c == ',' || c == '.' || c == ' ' ||
                       c == '%' || c == '/' || c == '-' || c == 'd' || c == 'e' || c == 'g';
            });
        }
    }
    return false;
}

// Source code reads as lines that end in ; { } or carry an operator prose
// does not use. Half the lines must, and there must be a bracket or an = sign.
bool LooksLikeCode(const std::string& text) {
    if (text.find_first_of("(){}[];=") == std::string::npos) return false;
    int lines = 0, marked = 0;
    for (const std::string& raw : SplitLines(text)) {
        const std::string line = Trimmed(raw);
        if (line.empty()) continue;
        ++lines;
        const char last = line.back();
        if (last == ';' || last == '{' || last == '}' ||
            line.find("->") != std::string::npos || line.find("::") != std::string::npos ||
            line.find("==") != std::string::npos || line.find("=>") != std::string::npos ||
            line.find("&&") != std::string::npos || line.rfind("#include", 0) == 0 ||
            line.rfind("def ", 0) == 0 || line.rfind("import ", 0) == 0 ||
            line.rfind("function ", 0) == 0 || line.rfind("return ", 0) == 0) {
            ++marked;
        }
    }
    return lines > 0 && marked * 2 >= lines;
}

int CountLines(const std::string& text) {
    if (text.empty()) return 0;
    int lines = 1;
    for (char c : text) lines += c == '\n';
    if (text.back() == '\n') --lines;
    return std::max(lines, 1);
}

std::string FileName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    return name.empty() ? path : name;
}

// "copy"/"cut", then the paths.
bool ParseFileList(const std::vector<uint8_t>& data, std::vector<std::string>& paths, bool& cut) {
    paths.clear();
    cut = false;
    const std::vector<std::string> lines = SplitLines(String(data));
    if (lines.empty()) return false;
    cut = lines[0] == "cut";
    for (size_t i = 1; i < lines.size(); ++i) {
        if (!lines[i].empty()) paths.push_back(lines[i]);
    }
    return !paths.empty();
}

std::vector<uint8_t> MakeFileList(const std::vector<std::string>& paths, bool cut) {
    std::string list = cut ? "cut" : "copy";
    for (const std::string& path : paths) {
        if (path.empty()) continue;
        list += '\n';
        list += path;
    }
    return Bytes(list);
}

std::string NormaliseImageMime(std::string format) {
    std::transform(format.begin(), format.end(), format.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (format.rfind("image/", 0) == 0) return format;
    if (format.empty() || format == "png") return "image/png";
    if (format == "jpg" || format == "jpeg") return "image/jpeg";
    if (format == "bmp" || format == "dib") return "image/bmp";
    return "image/" + format;
}

uint32_t ReadLe32(const std::vector<uint8_t>& data, size_t at) {
    if (at + 4 > data.size()) return 0;
    return static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8) |
           (static_cast<uint32_t>(data[at + 2]) << 16) | (static_cast<uint32_t>(data[at + 3]) << 24);
}

void AppendLe32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

// Windows hands images over as a bare CF_DIB: BITMAPINFO and pixels without
// the 14-byte BITMAPFILEHEADER an image decoder looks for. Kept as it came (it
// goes back to the clipboard as it came); this only makes it decodable.
std::vector<uint8_t> DecodableImage(const ClipboardFormat& image) {
    const auto& data = image.data;
    if (image.mime != "image/bmp" || data.size() < 40 || (data[0] == 'B' && data[1] == 'M')) return data;
    const uint32_t headerSize = ReadLe32(data, 0);
    const uint16_t bitCount = static_cast<uint16_t>(data[14] | (data[15] << 8));
    const uint32_t compression = ReadLe32(data, 16);
    const uint32_t coloursUsed = ReadLe32(data, 32);
    const uint32_t colours = coloursUsed ? coloursUsed : (bitCount <= 8 ? (1u << bitCount) : 0u);
    const uint32_t masks = (compression == 3 && headerSize == 40) ? 12u : 0u;   // BI_BITFIELDS
    std::vector<uint8_t> file = {'B', 'M'};
    AppendLe32(file, static_cast<uint32_t>(14 + data.size()));
    AppendLe32(file, 0);
    AppendLe32(file, 14 + headerSize + masks + colours * 4);
    file.insert(file.end(), data.begin(), data.end());
    return file;
}

// Image bytes start with their format's signature. A clipboard owner that
// answers every request with the same bytes (xclip does) would otherwise
// turn a text into an "image".
bool LooksLikeImage(const std::vector<uint8_t>& d, const std::string& mime) {
    if (d.size() < 12) return false;
    if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return true;
    if (d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return true;                          // JPEG
    if (d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8') return true;
    if ((d[0] == 'I' && d[1] == 'I' && d[2] == 42 && d[3] == 0) ||
        (d[0] == 'M' && d[1] == 'M' && d[2] == 0 && d[3] == 42)) return true;                // TIFF
    if (d[0] == 'B' && d[1] == 'M') return true;
    if (d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
        d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P') return true;
    if (mime == "image/bmp") {   // a bare CF_DIB: its BITMAPINFOHEADER's size first
        const uint32_t header = ReadLe32(d, 0);
        return header == 12 || header == 40 || header == 52 || header == 56 || header == 108 || header == 124;
    }
    return false;
}

// FNV-1a, for digests when this build has no hash (no libsodium).
uint64_t Fnv1a(const uint8_t* data, size_t size, uint64_t seed) {
    uint64_t hash = seed;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string Hex64(uint64_t value) {
    static const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<size_t>(i)] = digits[value & 0xF];
        value >>= 4;
    }
    return out;
}

bool ReadWholeFile(const fs::path& path, std::vector<uint8_t>& out) {
    out.clear();
    std::FILE* file = OpenFileUtf8(PathToUtf8(path), "rb");
    if (!file) return false;
    uint8_t buffer[65536];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) out.insert(out.end(), buffer, buffer + n);
    const bool ok = !std::ferror(file);
    std::fclose(file);
    return ok;
}

// Written beside the target and renamed into place, owner-only.
bool WriteOwnerOnlyFile(const fs::path& path, const std::vector<uint8_t>& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const std::string failure = WriteFileAtomically(PathToUtf8(path), [&data](const std::string& staged) -> std::string {
        std::FILE* file = OpenFileUtf8(staged, "wb");
        if (!file) return "cannot create " + staged;
        const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();
        const bool closed = std::fclose(file) == 0;
        return ok && closed ? "" : "cannot write " + staged;
    });
    if (!failure.empty()) return false;
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    return true;
}

// ===== SEARCH FOLDING =====
void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// U+00C0-U+00FF and U+0100-U+017F as the lower-case letter without its
// accent; '#' marks the few that are two letters or no letter at all.
const char* kLatin1Base = "aaaaaa#ceeeeiiiidnooooo#ouuuuy##aaaaaa#ceeeeiiiidnooooo#ouuuuy#y";
const char* kLatinExtABase =
        "aaaaaacccccccc" "dddd" "eeeeeeeeee" "gggggggg" "hhhh" "iiiiiiiiii" "##" "jj" "kkk"
        "llllllllll" "nnnnnnnnn" "oooooo" "##" "rrrrrr" "ssssssss" "tttttt" "uuuuuuuuuuuu"
        "ww" "yyy" "zzzzzz" "s";

void AppendFolded(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp >= 'A' && cp <= 'Z' ? cp + 32 : cp);
        return;
    }
    if (cp >= 0xC0 && cp <= 0xFF) {
        const char base = kLatin1Base[cp - 0xC0];
        if (base != '#') { out += base; return; }
        if (cp == 0xC6 || cp == 0xE6) { out += "ae"; return; }
        if (cp == 0xDE || cp == 0xFE) { out += "th"; return; }
        if (cp == 0xDF) { out += "ss"; return; }
        AppendUtf8(out, cp);   // × and ÷
        return;
    }
    if (cp >= 0x100 && cp <= 0x17F) {
        const char base = kLatinExtABase[cp - 0x100];
        if (base != '#') { out += base; return; }
        out += (cp == 0x132 || cp == 0x133) ? "ij" : "oe";
        return;
    }
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) { AppendUtf8(out, cp + 0x20); return; }   // Greek
    if (cp >= 0x410 && cp <= 0x42F) { AppendUtf8(out, cp + 0x20); return; }                  // Cyrillic
    if (cp >= 0x400 && cp <= 0x40F) { AppendUtf8(out, cp + 0x50); return; }
    AppendUtf8(out, cp);
}

// ===== POLICY AS JSON =====
JSONValue PolicyToJson(const ClipboardHistoryPolicy& policy) {
    JSONValue root = JSONValue::MakeObject();
    root.Set("maxEntries", static_cast<int64_t>(policy.maxEntries));
    root.Set("maxAgeDays", static_cast<int64_t>(policy.maxAgeDays));
    root.Set("maxTotalBytes", static_cast<int64_t>(policy.maxTotalBytes));
    root.Set("maxEntryBytes", static_cast<int64_t>(policy.maxEntryBytes));
    root.Set("recordingPaused", policy.recordingPaused);
    root.Set("imageThumbnails", policy.imageThumbnails);
    JSONValue excluded = JSONValue::MakeArray();
    for (const std::string& name : policy.excludedApplications) excluded.Append(JSONValue(name));
    root.Set("excludedApplications", std::move(excluded));
    return root;
}

ClipboardHistoryPolicy PolicyFromJson(const JSONValue& root) {
    ClipboardHistoryPolicy policy = ClipboardHistoryPolicy::Defaults();
    if (!root.IsObject()) return policy;
    policy.maxEntries = static_cast<int>(std::clamp<int64_t>(root.Get("maxEntries").GetInteger(policy.maxEntries), 10, 100000));
    policy.maxAgeDays = static_cast<int>(std::clamp<int64_t>(root.Get("maxAgeDays").GetInteger(policy.maxAgeDays), 1, 3650));
    policy.maxTotalBytes = static_cast<uint64_t>(std::max<int64_t>(root.Get("maxTotalBytes").GetInteger(
            static_cast<int64_t>(policy.maxTotalBytes)), 1 << 20));
    policy.maxEntryBytes = static_cast<uint64_t>(std::max<int64_t>(root.Get("maxEntryBytes").GetInteger(
            static_cast<int64_t>(policy.maxEntryBytes)), 1 << 10));
    policy.recordingPaused = root.Get("recordingPaused").GetBoolean(policy.recordingPaused);
    policy.imageThumbnails = root.Get("imageThumbnails").GetBoolean(policy.imageThumbnails);
    const JSONValue& excluded = root.Get("excludedApplications");
    if (excluded.IsArray()) {
        policy.excludedApplications.clear();
        for (size_t i = 0; i < excluded.GetSize(); ++i) {
            const std::string name = Trimmed(excluded.At(i).GetString(""));
            if (!name.empty()) policy.excludedApplications.push_back(name);
        }
    }
    return policy;
}

// What a snapshot is, for the entry row.
struct Description {
    ClipboardEntryKind kind = ClipboardEntryKind::Text;
    std::string title;
    std::string preview;
    int fileCount = 0;
    bool cut = false;
    int lineCount = 0;
    const ClipboardFormat* image = nullptr;
};

bool Describe(const ClipboardSnapshot& snapshot, Description& out) {
    out = Description{};
    if (const ClipboardFormat* files = snapshot.Find(ClipboardMime::Files)) {
        std::vector<std::string> paths;
        if (!ParseFileList(files->data, paths, out.cut)) return false;
        out.kind = ClipboardEntryKind::Files;
        out.fileCount = static_cast<int>(paths.size());
        out.title = FileName(paths[0]);
        if (paths.size() >= 2) out.title += ", " + FileName(paths[1]);
        if (paths.size() == 3) out.title += " and 1 more";
        if (paths.size() > 3) out.title += " and " + std::to_string(paths.size() - 2) + " more";
        std::string all;
        for (const std::string& path : paths) all += path + "\n";
        out.preview = Utf8Prefix(all, 4096);
        return true;
    }
    if (const ClipboardFormat* image = snapshot.FindImage()) {
        out.kind = ClipboardEntryKind::Image;
        out.image = image;
        out.title = "Image";
        return true;
    }
    const std::string text = snapshot.GetText();
    if (Trimmed(text).empty()) return false;
    const std::string trimmed = Trimmed(text);
    const ClipboardFormat* html = snapshot.Find(ClipboardMime::Html);
    if (html && !html->data.empty()) out.kind = ClipboardEntryKind::RichText;
    else if (IsUrl(trimmed)) out.kind = ClipboardEntryKind::Link;
    else if (IsColour(trimmed)) out.kind = ClipboardEntryKind::Colour;
    else if (LooksLikeCode(text)) out.kind = ClipboardEntryKind::Code;
    else out.kind = ClipboardEntryKind::Text;
    out.title = out.kind == ClipboardEntryKind::Link || out.kind == ClipboardEntryKind::Colour
            ? Utf8Prefix(trimmed, 400) : FirstLine(text);
    out.preview = Utf8Prefix(text, 4096);
    out.lineCount = CountLines(text);
    return true;
}

std::atomic<int> g_connectionNumber{0};

} // namespace

// ===== PUBLIC HELPERS =====
std::string ClipboardEntryKindName(ClipboardEntryKind kind) {
    switch (kind) {
        case ClipboardEntryKind::Text: return "Text";
        case ClipboardEntryKind::Code: return "Code";
        case ClipboardEntryKind::RichText: return "Formatted text";
        case ClipboardEntryKind::Link: return "Link";
        case ClipboardEntryKind::Colour: return "Colour";
        case ClipboardEntryKind::Image: return "Image";
        case ClipboardEntryKind::Files: return "Files";
    }
    return "Text";
}

std::string FoldForClipboardSearch(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = c;
        size_t length = 1;
        if (c >= 0xF0) { cp = c & 0x07; length = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; length = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; length = 2; }
        if (i + length > text.size()) { length = 1; cp = c; }   // a truncated sequence: kept as a byte
        for (size_t k = 1; k < length; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) { length = 1; cp = c; break; }
            cp = (cp << 6) | (next & 0x3F);
        }
        AppendFolded(out, cp);
        i += length;
    }
    return out;
}

namespace {

// One code point from `text` at `i`; a broken sequence is taken byte by byte.
uint32_t DecodeUtf8(const std::string& text, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    uint32_t cp = c;
    size_t length = 1;
    if (c >= 0xF0) { cp = c & 0x07; length = 4; }
    else if (c >= 0xE0) { cp = c & 0x0F; length = 3; }
    else if (c >= 0xC0) { cp = c & 0x1F; length = 2; }
    if (i + length > text.size()) { length = 1; cp = c; }
    for (size_t k = 1; k < length; ++k) {
        const unsigned char next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) { length = 1; cp = c; break; }
        cp = (cp << 6) | (next & 0x3F);
    }
    i += length;
    return cp;
}

// Latin-1, Latin Extended-A (its pairs), Greek and Cyrillic.
bool PairedEvenUpper(uint32_t cp) { return (cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177); }
bool PairedOddUpper(uint32_t cp) { return (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E); }

uint32_t UpperOf(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
    if (cp == 0xFF) return 0x178;
    if (cp == 0x131) return 'I';
    if (cp == 0x17F) return 'S';
    if (PairedEvenUpper(cp)) return cp % 2 == 1 ? cp - 1 : cp;
    if (PairedOddUpper(cp)) return cp % 2 == 0 ? cp - 1 : cp;
    if (cp >= 0x3B1 && cp <= 0x3C9) return cp == 0x3C2 ? 0x3A3 : cp - 0x20;
    if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;
    if (cp >= 0x450 && cp <= 0x45F) return cp - 0x50;
    return cp;
}

uint32_t LowerOf(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 0x20;
    if (cp == 0x178) return 0xFF;
    if (cp == 0x130) return 'i';
    if (PairedEvenUpper(cp)) return cp % 2 == 0 ? cp + 1 : cp;
    if (PairedOddUpper(cp)) return cp % 2 == 1 ? cp + 1 : cp;
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) return cp + 0x20;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
    if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;
    return cp;
}

// Letters and digits, and the apostrophe inside a word; dashes, quotes and
// the rest of General Punctuation (U+2000-206F) separate words.
bool IsWordCharacter(uint32_t cp) {
    if (cp < 0x80) return std::isalnum(static_cast<int>(cp)) || cp == '\'';
    if (cp == 0xD7 || cp == 0xF7 || (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F)) return false;
    return cp >= 0xC0;
}

} // namespace

std::vector<ClipboardEntryKind> PreferredClipboardKinds(const std::vector<std::string>& categories,
                                                        const std::vector<std::string>& mimeTypes) {
    using Kind = ClipboardEntryKind;
    auto has = [&categories](const char* category) {
        return std::find(categories.begin(), categories.end(), category) != categories.end();
    };
    // What it is, by the freedesktop menu categories. A viewer shows
    // pictures; nothing is pasted into it.
    const bool viewer = has("Viewer");
    if (!viewer && (has("RasterGraphics") || has("2DGraphics") || has("VectorGraphics") ||
                    has("3DGraphics") || has("Photography") || has("Graphics"))) {
        return {Kind::Image, Kind::Colour};
    }
    if (has("FileManager")) return {Kind::Files};
    if (has("TextEditor") || has("IDE") || has("Development") || has("TerminalEmulator")) {
        return {Kind::Code, Kind::Text, Kind::Link};
    }
    if (has("WebBrowser")) return {Kind::Link, Kind::Text};

    // What it opens, when the categories say nothing of the above.
    size_t images = 0, texts = 0, others = 0;
    for (const std::string& mime : mimeTypes) {
        if (mime == "inode/directory") return {Kind::Files};
        if (mime.rfind("image/", 0) == 0) ++images;
        else if (mime.rfind("text/", 0) == 0) ++texts;
        else ++others;
    }
    if (!viewer && images > 0 && texts == 0 && others == 0) return {Kind::Image, Kind::Colour};
    if (texts > 0 && images == 0 && others == 0) return {Kind::Text, Kind::Code, Kind::Link};
    return {};
}

std::vector<uint8_t> ClipboardImageFile(const ClipboardFormat& image, std::string& extension) {
    std::string format = image.mime.rfind("image/", 0) == 0 ? image.mime.substr(6) : "png";
    format = format.substr(0, format.find(';'));
    if (format == "jpeg") format = "jpg";
    else if (format == "svg+xml") format = "svg";
    else if (format == "x-icon" || format == "vnd.microsoft.icon") format = "ico";
    extension = format.empty() ? "png" : format;
    return DecodableImage(image);
}

std::string EditClipboardText(const std::string& text, ClipboardTextEdit edit) {
    if (edit == ClipboardTextEdit::Trim || edit == ClipboardTextEdit::JoinLines) {
        std::vector<std::string> lines;
        for (std::string line : SplitLines(text)) {
            const size_t end = line.find_last_not_of(" \t");
            line = end == std::string::npos ? std::string() : line.substr(0, end + 1);
            if (edit == ClipboardTextEdit::JoinLines) {
                line = Trimmed(line);
                if (line.empty()) continue;
            }
            lines.push_back(std::move(line));
        }
        while (!lines.empty() && Trimmed(lines.front()).empty()) lines.erase(lines.begin());
        while (!lines.empty() && Trimmed(lines.back()).empty()) lines.pop_back();
        std::string out;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i) out += edit == ClipboardTextEdit::JoinLines ? " " : "\n";
            out += lines[i];
        }
        return out;
    }
    std::string out;
    out.reserve(text.size());
    bool startOfWord = true;
    bool startOfSentence = true;
    size_t i = 0;
    while (i < text.size()) {
        const uint32_t cp = DecodeUtf8(text, i);
        const bool word = IsWordCharacter(cp);
        uint32_t mapped = cp;
        switch (edit) {
            case ClipboardTextEdit::Upper: mapped = UpperOf(cp); break;
            case ClipboardTextEdit::Lower: mapped = LowerOf(cp); break;
            case ClipboardTextEdit::Title: mapped = word && startOfWord ? UpperOf(cp) : LowerOf(cp); break;
            case ClipboardTextEdit::Sentence:
                mapped = word && startOfSentence ? UpperOf(cp) : LowerOf(cp);
                if (word) startOfSentence = false;
                break;
            default: break;
        }
        AppendUtf8(out, mapped);
        startOfWord = !word;
        if (cp == '.' || cp == '!' || cp == '?' || cp == '\n') startOfSentence = true;
    }
    return out;
}

ClipboardHistoryPolicy ClipboardHistoryPolicy::Defaults() {
    ClipboardHistoryPolicy policy;
    policy.excludedApplications = {"UltraPassword", "UltraAuthenticator", "KeePassXC", "Bitwarden", "1Password"};
    return policy;
}

const ClipboardFormat* ClipboardSnapshot::Find(const std::string& mime) const {
    for (const auto& format : formats) {
        if (format.mime == mime) return &format;
    }
    return nullptr;
}

const ClipboardFormat* ClipboardSnapshot::FindImage() const {
    const ClipboardFormat* found = nullptr;
    for (const auto& format : formats) {
        if (format.mime.rfind("image/", 0) != 0 || format.data.empty()) continue;
        if (format.mime == "image/png") return &format;
        if (!found) found = &format;
    }
    return found;
}

std::string ClipboardSnapshot::GetText() const {
    const ClipboardFormat* text = Find(ClipboardMime::Text);
    return text ? String(text->data) : std::string();
}

void ClipboardSnapshot::SetText(const std::string& text) {
    for (auto& format : formats) {
        if (format.mime == ClipboardMime::Text) { format.data = Bytes(text); return; }
    }
    formats.push_back({ClipboardMime::Text, Bytes(text)});
}

bool ClipboardSnapshot::IsEmpty() const {
    return std::none_of(formats.begin(), formats.end(), [](const ClipboardFormat& f) { return !f.data.empty(); });
}

// ===== THE STORE =====
struct UltraCanvasClipboardHistory::Impl {
    fs::path dir;
    std::string dirUtf8;
    fs::path keyPath;
    std::string connection;
    bool open = false;
    std::unique_ptr<UltraCryptSecureBuffer> key;   // null: this build cannot encrypt
    std::string lastError;
    // Decrypted title and preview, and both folded for searching, by entry
    // id; an id's content never changes.
    struct CachedText {
        std::string title;
        std::string preview;
        std::string folded;
    };
    std::unordered_map<int64_t, CachedText> textCache;
    const CachedText& Cache(int64_t id, std::string title, std::string preview) {
        CachedText cached;
        cached.folded = FoldForClipboardSearch(title + "\n" + preview);
        cached.title = std::move(title);
        cached.preview = std::move(preview);
        return textCache[id] = std::move(cached);
    }

    bool Fail(const std::string& message) {
        lastError = message;
        debugOutput << "UltraCanvasClipboardHistory: " << message << std::endl;
        return false;
    }

    fs::path BlobPath(const std::string& digest) const {
        return dir / "blobs" / PathFromUtf8(digest.substr(0, 2)) / PathFromUtf8(digest + ".bin");
    }
    fs::path ThumbPath(const std::string& digest) const {
        return dir / "thumbs" / PathFromUtf8(digest + ".png");
    }

    // ----- sealing: 0x01 nonce(24) ciphertext+tag, or 0x00 plain -----
    std::vector<uint8_t> Seal(const void* data, size_t size) const {
        std::vector<uint8_t> out;
        if (key) {
            UltraCryptAeadParams params;
            params.associatedData = Bytes("ultraclipboard");
            std::vector<uint8_t> ciphertext;
            if (UltraCrypt_AeadSeal(*key, params, data, size, ciphertext)) {
                out.reserve(1 + params.nonce.size() + ciphertext.size());
                out.push_back(0x01);
                out.insert(out.end(), params.nonce.begin(), params.nonce.end());
                out.insert(out.end(), ciphertext.begin(), ciphertext.end());
                return out;
            }
        }
        out.reserve(size + 1);
        out.push_back(0x00);
        const auto* bytes = static_cast<const uint8_t*>(data);
        out.insert(out.end(), bytes, bytes + size);
        return out;
    }
    std::vector<uint8_t> Seal(const std::string& text) const { return Seal(text.data(), text.size()); }

    bool Unseal(const std::vector<uint8_t>& sealed, std::vector<uint8_t>& out) const {
        out.clear();
        if (sealed.empty()) return true;
        if (sealed[0] == 0x00) {
            out.assign(sealed.begin() + 1, sealed.end());
            return true;
        }
        const size_t nonceSize = UltraCrypt_GetNonceSize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
        if (sealed[0] != 0x01 || !key || sealed.size() < 1 + nonceSize) return false;
        UltraCryptAeadParams params;
        params.associatedData = Bytes("ultraclipboard");
        params.nonce.assign(sealed.begin() + 1, sealed.begin() + 1 + static_cast<long>(nonceSize));
        UltraCryptSecureBuffer plain;
        if (!UltraCrypt_AeadOpen(*key, params, sealed.data() + 1 + nonceSize, sealed.size() - 1 - nonceSize, plain)) {
            return false;
        }
        out.assign(plain.Data(), plain.Data() + plain.GetSize());
        return true;
    }
    std::string UnsealText(const std::vector<uint8_t>& sealed) const {
        std::vector<uint8_t> plain;
        return Unseal(sealed, plain) ? String(plain) : std::string();
    }

    // Keyed, so a digest on disk says nothing about short content.
    std::string Digest(const void* data, size_t size) const {
        std::vector<uint8_t> mac;
        if (key && UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA256, *key, data, size, mac)) {
            return UltraCrypt_ToHex(mac);
        }
        if (UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, data, size, mac)) return UltraCrypt_ToHex(mac);
        const auto* bytes = static_cast<const uint8_t*>(data);
        return Hex64(Fnv1a(bytes, size, 1469598103934665603ull)) + Hex64(Fnv1a(bytes, size, 0x9E3779B97F4A7C15ull));
    }

    bool WriteBlob(const std::string& digest, const std::vector<uint8_t>& data) const {
        const fs::path path = BlobPath(digest);
        std::error_code ec;
        if (fs::exists(path, ec)) return true;
        return WriteOwnerOnlyFile(path, Seal(data.data(), data.size()));
    }

    bool ReadBlob(const std::string& digest, std::vector<uint8_t>& out) const {
        std::vector<uint8_t> sealed;
        return ReadWholeFile(BlobPath(digest), sealed) && Unseal(sealed, out);
    }

    bool LoadKey() {
        key.reset();
        if (!UltraCrypt_IsAeadAvailable(UltraCryptAeadAlgorithm::XChaCha20Poly1305) ||
            !UltraCrypt_IsHashAvailable(UltraCryptHashAlgorithm::SHA256)) {
            return true;   // stored unencrypted; GetStats says so
        }
        const size_t keySize = UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
        std::vector<uint8_t> bytes;
        if (!ReadWholeFile(keyPath, bytes) || bytes.size() != keySize) {
            bytes.assign(keySize, 0);
            if (!UltraCrypt_RandomBytes(bytes.data(), bytes.size())) return Fail("no random bytes for the history key");
            if (!WriteOwnerOnlyFile(keyPath, bytes)) return Fail("cannot write the history key " + PathToUtf8(keyPath));
        }
        key = std::make_unique<UltraCryptSecureBuffer>(bytes.data(), bytes.size());
        std::fill(bytes.begin(), bytes.end(), 0);
        return true;
    }

#ifdef ULTRACANVAS_HAS_DATABASE
    bool Exec(const std::string& sql, const UltraDbParams& params = {}) {
        if (UltraDbResult r = UltraDb_Exec(connection, sql, params); !r) return Fail(r.message);
        return true;
    }
    bool Query(const std::string& sql, const UltraDbParams& params, UltraDbResultSet& rows) {
        if (UltraDbResult r = UltraDb_Query(connection, sql, params, rows); !r) return Fail(r.message);
        return true;
    }
    std::string Meta(const std::string& name) {
        UltraDbResultSet rows;
        if (!Query("SELECT value FROM meta WHERE key = ?", {name}, rows) || rows.Empty()) return "";
        return rows.Row(0)[0].AsString();
    }
    bool SetMeta(const std::string& name, const std::string& value) {
        return Exec("INSERT INTO meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
                    {name, value});
    }
    void BumpGeneration() {
        Exec("UPDATE meta SET value = CAST(CAST(value AS INTEGER) + 1 AS TEXT) WHERE key = 'generation'");
    }

    // Removes rows for good, then the payload files no entry uses any more.
    void HardDelete(const std::vector<int64_t>& ids) {
        if (ids.empty()) return;
        std::set<std::string> blobs;
        for (int64_t id : ids) {
            UltraDbResultSet rows;
            if (Query("SELECT blob FROM formats WHERE entry_id = ?", {id}, rows)) {
                for (const auto& row : rows) blobs.insert(row[0].AsString());
            }
            Exec("DELETE FROM formats WHERE entry_id = ?", {id});
            Exec("DELETE FROM entries WHERE id = ?", {id});
            textCache.erase(id);
        }
        for (const std::string& blob : blobs) {
            UltraDbResultSet rows;
            if (!Query("SELECT COUNT(*) FROM formats WHERE blob = ?", {blob}, rows)) continue;
            if (!rows.Empty() && rows.Row(0)[0].AsInt64() > 0) continue;
            std::error_code ec;
            fs::remove(BlobPath(blob), ec);
            fs::remove(ThumbPath(blob), ec);
        }
    }

    void Wipe() {
        Exec("DELETE FROM formats");
        Exec("DELETE FROM entries");
        textCache.clear();
        std::error_code ec;
        fs::remove_all(dir / "blobs", ec);
        fs::remove_all(dir / "thumbs", ec);
        BumpGeneration();
    }
#endif
};

UltraCanvasClipboardHistory::UltraCanvasClipboardHistory() : impl(std::make_unique<Impl>()) {}

UltraCanvasClipboardHistory::~UltraCanvasClipboardHistory() {
    Close();
}

std::string UltraCanvasClipboardHistory::DefaultDirectory() {
    fs::path root;
#if defined(_WIN32)
    if (const std::string local = GetEnvUtf8("LOCALAPPDATA"); !local.empty()) root = PathFromUtf8(local);
#elif defined(__APPLE__)
    if (const std::string home = GetEnvUtf8("HOME"); !home.empty()) {
        root = PathFromUtf8(home) / "Library" / "Application Support";
    }
#else
    if (const std::string data = GetEnvUtf8("XDG_DATA_HOME"); !data.empty()) root = PathFromUtf8(data);
    else if (const std::string home = GetEnvUtf8("HOME"); !home.empty()) root = PathFromUtf8(home) / ".local" / "share";
#endif
    return root.empty() ? "" : PathToUtf8(root / "ultraos" / "clipboard");
}

// Beside the desktop's own settings, apart from the data it unlocks.
std::string UltraCanvasClipboardHistory::DefaultKeyPath() {
    fs::path root;
#if defined(_WIN32)
    if (const std::string appData = GetEnvUtf8("APPDATA"); !appData.empty()) root = PathFromUtf8(appData);
#else
    if (const std::string config = GetEnvUtf8("XDG_CONFIG_HOME"); !config.empty()) root = PathFromUtf8(config);
    else if (const std::string home = GetEnvUtf8("HOME"); !home.empty()) root = PathFromUtf8(home) / ".config";
#endif
    return root.empty() ? "" : PathToUtf8(root / "ultraos" / "clipboard.key");
}

bool UltraCanvasClipboardHistory::IsAvailable() {
#ifdef ULTRACANVAS_HAS_DATABASE
    return true;
#else
    return false;
#endif
}

const std::string& UltraCanvasClipboardHistory::GetLastError() const { return impl->lastError; }
const std::string& UltraCanvasClipboardHistory::GetDirectory() const { return impl->dirUtf8; }
bool UltraCanvasClipboardHistory::IsOpen() const { return impl->open; }

#ifdef ULTRACANVAS_HAS_DATABASE

namespace {

const char* kSchemaV1 = R"SQL(
CREATE TABLE entries(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    kind INTEGER NOT NULL,
    title BLOB NOT NULL,
    preview BLOB NOT NULL,
    source_app TEXT NOT NULL DEFAULT '',
    copied_at INTEGER NOT NULL,
    last_used_at INTEGER NOT NULL,
    use_count INTEGER NOT NULL DEFAULT 0,
    pinned INTEGER NOT NULL DEFAULT 0,
    size_bytes INTEGER NOT NULL DEFAULT 0,
    digest TEXT NOT NULL,
    width INTEGER NOT NULL DEFAULT 0,
    height INTEGER NOT NULL DEFAULT 0,
    file_count INTEGER NOT NULL DEFAULT 0,
    cut INTEGER NOT NULL DEFAULT 0,
    line_count INTEGER NOT NULL DEFAULT 0,
    thumbnail TEXT NOT NULL DEFAULT '',
    deleted_at INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX entries_order ON entries(deleted_at, pinned, last_used_at);
CREATE INDEX entries_digest ON entries(digest);
CREATE TABLE formats(
    entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,
    mime TEXT NOT NULL,
    blob TEXT NOT NULL,
    size_bytes INTEGER NOT NULL,
    PRIMARY KEY(entry_id, mime)
);
CREATE INDEX formats_blob ON formats(blob);
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE recorder(
    id INTEGER PRIMARY KEY CHECK (id = 1),
    holder TEXT NOT NULL,
    priority INTEGER NOT NULL,
    heartbeat INTEGER NOT NULL
);
INSERT INTO meta(key, value) VALUES('generation', '0');
)SQL";

const char* kEntryColumns =
        "id, kind, title, preview, source_app, copied_at, last_used_at, use_count, pinned, size_bytes, "
        "width, height, file_count, cut, line_count, thumbnail";

} // namespace

bool UltraCanvasClipboardHistory::Open(const std::string& directory, const std::string& keyPath) {
    Close();
    Impl& d = *impl;
    d.lastError.clear();
    if (directory.empty()) return d.Fail("no folder for the clipboard history");
    d.dir = PathFromUtf8(directory);
    d.dirUtf8 = directory;
    d.keyPath = keyPath.empty() ? d.dir / "clipboard.key" : PathFromUtf8(keyPath);

    std::error_code ec;
    fs::create_directories(d.dir, ec);
    if (ec) return d.Fail("cannot create " + directory + ": " + ec.message());
    fs::permissions(d.dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (!d.LoadKey()) return false;

    UltraDbConnectionConfig config;
    config.name = "ultracanvas-clipboard-history-" + std::to_string(g_connectionNumber.fetch_add(1));
    config.driver = "sqlite";
    config.database = PathToUtf8(d.dir / "history.db");
    if (UltraDbResult r = UltraDb_RegisterConnection(config); !r) return d.Fail("opening the history: " + r.message);
    d.connection = config.name;
    d.Exec("PRAGMA journal_mode=WAL");
    d.Exec("PRAGMA synchronous=NORMAL");
    const std::vector<UltraDbMigration> steps = {{1, "clipboard history", kSchemaV1}};
    if (UltraDbResult m = UltraDb_Migrate(d.connection, steps); !m) {
        UltraDb_CloseConnection(d.connection);
        d.connection.clear();
        return d.Fail("preparing the history: " + m.message);
    }
    std::error_code permissionError;
    fs::permissions(d.dir / "history.db", fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, permissionError);
    d.open = true;

    // A history written under another key (or encrypted, now that this build
    // cannot decrypt) cannot be read: start it again rather than show noise.
    const std::string wasEncrypted = d.Meta("encrypted");
    const std::string check = d.Meta("keycheck");
    bool readable = true;
    if (d.key) {
        std::vector<uint8_t> sealed, plain;
        if (!check.empty() && (!UltraCrypt_FromHex(check, sealed) || !d.Unseal(sealed, plain) ||
                               String(plain) != "ultraclipboard")) {
            readable = false;
        }
    } else if (wasEncrypted == "1") {
        readable = false;
    }
    if (!readable) {
        debugOutput << "UltraCanvasClipboardHistory: the key does not open this history; starting it again" << std::endl;
        d.Wipe();
    }
    if (d.key && (check.empty() || !readable)) d.SetMeta("keycheck", UltraCrypt_ToHex(d.Seal(std::string("ultraclipboard"))));
    d.SetMeta("encrypted", d.key ? "1" : "0");
    if (d.Meta("policy").empty()) {
        d.SetMeta("policy", JSON::Serialize(PolicyToJson(ClipboardHistoryPolicy::Defaults())));
    }
    Prune();
    return true;
}

void UltraCanvasClipboardHistory::Close() {
    Impl& d = *impl;
    if (!d.connection.empty()) UltraDb_CloseConnection(d.connection);
    d.connection.clear();
    d.open = false;
    d.key.reset();
    d.textCache.clear();
}

namespace {

ClipboardHistoryEntry EntryFromRow(const UltraDbRow& row) {
    ClipboardHistoryEntry entry;
    entry.id = row[0].AsInt64();
    entry.kind = static_cast<ClipboardEntryKind>(std::clamp<int64_t>(row[1].AsInt64(), 0, 6));
    entry.sourceApplication = row[4].AsString();
    entry.copiedAt = row[5].AsInt64();
    entry.lastUsedAt = row[6].AsInt64();
    entry.useCount = static_cast<int>(row[7].AsInt64());
    entry.pinned = row[8].AsInt64() != 0;
    entry.sizeBytes = static_cast<uint64_t>(row[9].AsInt64());
    entry.width = static_cast<int>(row[10].AsInt64());
    entry.height = static_cast<int>(row[11].AsInt64());
    entry.fileCount = static_cast<int>(row[12].AsInt64());
    entry.cut = row[13].AsInt64() != 0;
    entry.lineCount = static_cast<int>(row[14].AsInt64());
    return entry;
}

} // namespace

ClipboardRecordResult UltraCanvasClipboardHistory::Record(const ClipboardSnapshot& snapshot, int64_t* entryId) {
    return RecordContent(snapshot, entryId, true);
}

ClipboardRecordResult UltraCanvasClipboardHistory::RecordContent(const ClipboardSnapshot& snapshot, int64_t* entryId,
                                                                 bool applyPolicy) {
    Impl& d = *impl;
    if (entryId) *entryId = 0;
    if (!d.open) return ClipboardRecordResult::Failed;
    const ClipboardHistoryPolicy policy = GetPolicy();
    if (applyPolicy && policy.recordingPaused) return ClipboardRecordResult::Paused;
    if (applyPolicy && !snapshot.sourceApplication.empty()) {
        const std::string source = FoldForClipboardSearch(snapshot.sourceApplication);
        for (const std::string& excluded : policy.excludedApplications) {
            if (FoldForClipboardSearch(excluded) == source) return ClipboardRecordResult::Excluded;
        }
    }
    uint64_t size = 0;
    for (const auto& format : snapshot.formats) size += format.data.size();
    if (size > policy.maxEntryBytes) return ClipboardRecordResult::TooLarge;

    Description description;
    if (snapshot.IsEmpty() || !Describe(snapshot, description)) return ClipboardRecordResult::Empty;

    // One digest per payload (its file name), one per copy (its identity).
    std::vector<std::pair<const ClipboardFormat*, std::string>> payloads;
    std::string identity;
    for (const auto& format : snapshot.formats) {
        if (format.data.empty()) continue;
        payloads.emplace_back(&format, d.Digest(format.data.data(), format.data.size()));
    }
    std::sort(payloads.begin(), payloads.end(),
              [](const auto& a, const auto& b) { return a.first->mime < b.first->mime; });
    for (const auto& payload : payloads) identity += payload.first->mime + ":" + payload.second + "\n";
    const std::string digest = d.Digest(identity.data(), identity.size());
    const int64_t now = NowMs();

    UltraDbResultSet existing;
    if (!d.Query("SELECT id FROM entries WHERE digest = ? AND deleted_at = 0 LIMIT 1", {digest}, existing)) {
        return ClipboardRecordResult::Failed;
    }
    if (!existing.Empty()) {
        const int64_t id = existing.Row(0)[0].AsInt64();
        d.Exec("UPDATE entries SET last_used_at = ?, use_count = use_count + 1 WHERE id = ?", {now, id});
        d.BumpGeneration();
        if (entryId) *entryId = id;
        return ClipboardRecordResult::MovedToTop;
    }

    for (const auto& payload : payloads) {
        if (!d.WriteBlob(payload.second, payload.first->data)) {
            d.Fail("cannot store a copy in " + d.dirUtf8);
            return ClipboardRecordResult::Failed;
        }
    }

    int width = 0, height = 0;
    std::string thumbnail;
    if (description.image) {
        std::string imageDigest;
        for (const auto& payload : payloads) {
            if (payload.first == description.image) imageDigest = payload.second;
        }
        auto image = UCImage::LoadFromMemory(DecodableImage(*description.image));
        if (image && image->GetWidth() > 0) {
            width = image->GetWidth();
            height = image->GetHeight();
            description.title = "Image " + std::to_string(width) + " \xC3\x97 " + std::to_string(height);
            if (policy.imageThumbnails && !imageDigest.empty()) {
                const fs::path thumbPath = d.ThumbPath(imageDigest);
                std::error_code ec;
                fs::create_directories(thumbPath.parent_path(), ec);
                if (fs::exists(thumbPath, ec)) {
                    thumbnail = imageDigest;
                } else {
                    UCImageSave::ImageExportOptions options;
                    options.targetWidth = 96;
                    options.targetHeight = 96;
                    options.maintainAspectRatio = true;
                    options.preserveMetadata = false;
                    options.format = UCImageSaveFormat::PNG;
                    if (image->Save(PathToUtf8(thumbPath), options).empty() && fs::exists(thumbPath, ec)) {
                        thumbnail = imageDigest;
                    }
                }
            }
        }
    }

    UltraDbResult error;
    const UltraDbHandle tx = UltraDb_Begin(d.connection, &error);
    if (tx == UltraDbInvalidHandle) {
        d.Fail("recording a copy: " + error.message);
        return ClipboardRecordResult::Failed;
    }
    UltraDbResult step = UltraDb_ExecInTx(tx,
            "INSERT INTO entries(kind, title, preview, source_app, copied_at, last_used_at, use_count, pinned, "
            "size_bytes, digest, width, height, file_count, cut, line_count, thumbnail) "
            "VALUES(?, ?, ?, ?, ?, ?, 1, 0, ?, ?, ?, ?, ?, ?, ?, ?)",
            {static_cast<int64_t>(description.kind), UltraDbValue::Blob(d.Seal(description.title)),
             UltraDbValue::Blob(d.Seal(description.preview)), snapshot.sourceApplication, now, now,
             static_cast<int64_t>(size), digest, static_cast<int64_t>(width), static_cast<int64_t>(height),
             static_cast<int64_t>(description.fileCount), static_cast<int64_t>(description.cut ? 1 : 0),
             static_cast<int64_t>(description.lineCount), thumbnail});
    const int64_t id = step.lastInsertId;
    for (const auto& payload : payloads) {
        if (!step) break;
        step = UltraDb_ExecInTx(tx, "INSERT INTO formats(entry_id, mime, blob, size_bytes) VALUES(?, ?, ?, ?)",
                                {id, payload.first->mime, payload.second,
                                 static_cast<int64_t>(payload.first->data.size())});
    }
    if (step) step = UltraDb_ExecInTx(tx,
            "UPDATE meta SET value = CAST(CAST(value AS INTEGER) + 1 AS TEXT) WHERE key = 'generation'", {});
    if (!step) {
        UltraDb_Rollback(tx);
        d.Fail("recording a copy: " + step.message);
        return ClipboardRecordResult::Failed;
    }
    if (UltraDbResult c = UltraDb_Commit(tx); !c) {
        d.Fail("recording a copy: " + c.message);
        return ClipboardRecordResult::Failed;
    }
    d.Cache(id, description.title, description.preview);
    if (entryId) *entryId = id;
    Prune();
    return ClipboardRecordResult::Recorded;
}

std::vector<ClipboardHistoryEntry> UltraCanvasClipboardHistory::List(const ClipboardHistoryQuery& query) {
    Impl& d = *impl;
    std::vector<ClipboardHistoryEntry> result;
    if (!d.open) return result;
    UltraDbResultSet rows;
    const char* order = query.newestFirst ? " ORDER BY last_used_at DESC, id DESC"
                                          : " ORDER BY pinned DESC, last_used_at DESC, id DESC";
    if (!d.Query(std::string("SELECT ") + kEntryColumns + " FROM entries WHERE deleted_at = 0" + order, {}, rows)) {
        return result;
    }

    // The query's words, every one of which must match; kind:, from: and
    // pinned work as filters for whoever types rather than clicks.
    std::vector<std::string> words;
    std::vector<ClipboardEntryKind> kinds = query.kinds;
    std::string fromApplication;
    bool pinnedOnly = query.pinnedOnly;
    {
        std::string word;
        const std::string folded = FoldForClipboardSearch(query.text);
        auto flush = [&]() {
            if (word.empty()) return;
            if (word == "pinned" || word == "is:pinned") pinnedOnly = true;
            else if (word.rfind("from:", 0) == 0) fromApplication = word.substr(5);
            else if (word.rfind("kind:", 0) == 0) {
                const std::string name = word.substr(5);
                for (int k = 0; k <= 6; ++k) {
                    const auto kind = static_cast<ClipboardEntryKind>(k);
                    const std::string kindName = FoldForClipboardSearch(ClipboardEntryKindName(kind));
                    if (kindName.rfind(name, 0) == 0 || (name == "color" && kind == ClipboardEntryKind::Colour) ||
                        (name == "rich" && kind == ClipboardEntryKind::RichText) ||
                        (name == "file" && kind == ClipboardEntryKind::Files)) {
                        kinds.push_back(kind);
                    }
                }
            } else words.push_back(word);
            word.clear();
        };
        for (char c : folded) {
            if (c == ' ' || c == '\t' || c == '\n') flush();
            else word += c;
        }
        flush();
    }

    for (const auto& row : rows) {
        ClipboardHistoryEntry entry = EntryFromRow(row);
        auto found = d.textCache.find(entry.id);
        const Impl::CachedText& cached = found != d.textCache.end()
                ? found->second
                : d.Cache(entry.id, d.UnsealText(row[2].AsBlob()), d.UnsealText(row[3].AsBlob()));
        entry.title = cached.title;
        entry.preview = cached.preview;
        const std::string thumb = row[15].AsString();
        if (!thumb.empty()) entry.thumbnailPath = PathToUtf8(d.ThumbPath(thumb));

        if (pinnedOnly && !entry.pinned) continue;
        if (!kinds.empty() && std::find(kinds.begin(), kinds.end(), entry.kind) == kinds.end()) continue;
        if (!fromApplication.empty() &&
            FoldForClipboardSearch(entry.sourceApplication).find(fromApplication) == std::string::npos) {
            continue;
        }
        if (!words.empty()) {
            const std::string extra = FoldForClipboardSearch(entry.sourceApplication + "\n" +
                                                             ClipboardEntryKindName(entry.kind));
            const bool all = std::all_of(words.begin(), words.end(), [&](const std::string& w) {
                return cached.folded.find(w) != std::string::npos || extra.find(w) != std::string::npos;
            });
            if (!all) continue;
        }
        result.push_back(std::move(entry));
        if (query.limit > 0 && result.size() >= query.limit) break;
    }
    return result;
}

std::optional<ClipboardHistoryEntry> UltraCanvasClipboardHistory::Get(int64_t id) {
    Impl& d = *impl;
    if (!d.open) return std::nullopt;
    UltraDbResultSet rows;
    if (!d.Query(std::string("SELECT ") + kEntryColumns + " FROM entries WHERE id = ? AND deleted_at = 0",
                 {id}, rows) || rows.Empty()) {
        return std::nullopt;
    }
    ClipboardHistoryEntry entry = EntryFromRow(rows.Row(0));
    entry.title = d.UnsealText(rows.Row(0)[2].AsBlob());
    entry.preview = d.UnsealText(rows.Row(0)[3].AsBlob());
    const std::string thumb = rows.Row(0)[15].AsString();
    if (!thumb.empty()) entry.thumbnailPath = PathToUtf8(d.ThumbPath(thumb));
    return entry;
}

bool UltraCanvasClipboardHistory::ReadFormats(int64_t id, std::vector<ClipboardFormat>& formats) {
    Impl& d = *impl;
    formats.clear();
    if (!d.open) return false;
    UltraDbResultSet rows;
    if (!d.Query("SELECT mime, blob FROM formats WHERE entry_id = ? ORDER BY mime", {id}, rows)) return false;
    for (const auto& row : rows) {
        ClipboardFormat format;
        format.mime = row[0].AsString();
        if (!d.ReadBlob(row[1].AsString(), format.data)) {
            formats.clear();
            return d.Fail("a stored copy cannot be read (entry " + std::to_string(id) + ")");
        }
        formats.push_back(std::move(format));
    }
    return !formats.empty();
}

std::string UltraCanvasClipboardHistory::ReadText(int64_t id) {
    std::vector<ClipboardFormat> formats;
    if (!ReadFormats(id, formats)) return "";
    for (const auto& format : formats) {
        if (format.mime == ClipboardMime::Text) return String(format.data);
    }
    return "";
}

ClipboardHistoryStats UltraCanvasClipboardHistory::GetStats() {
    Impl& d = *impl;
    ClipboardHistoryStats stats;
    stats.encrypted = d.key != nullptr;
    if (!d.open) return stats;
    UltraDbResultSet rows;
    if (d.Query("SELECT COUNT(*), COALESCE(SUM(pinned), 0), COALESCE(SUM(size_bytes), 0) FROM entries "
                "WHERE deleted_at = 0", {}, rows) && !rows.Empty()) {
        stats.entries = static_cast<size_t>(rows.Row(0)[0].AsInt64());
        stats.pinned = static_cast<size_t>(rows.Row(0)[1].AsInt64());
        stats.bytes = static_cast<uint64_t>(rows.Row(0)[2].AsInt64());
    }
    return stats;
}

bool UltraCanvasClipboardHistory::MarkUsed(int64_t id) {
    Impl& d = *impl;
    if (!d.open || !d.Exec("UPDATE entries SET last_used_at = ?, use_count = use_count + 1 WHERE id = ?",
                           {NowMs(), id})) {
        return false;
    }
    d.BumpGeneration();
    return true;
}

bool UltraCanvasClipboardHistory::SetPinned(int64_t id, bool pinned) {
    Impl& d = *impl;
    if (!d.open || !d.Exec("UPDATE entries SET pinned = ? WHERE id = ?", {static_cast<int64_t>(pinned ? 1 : 0), id})) {
        return false;
    }
    d.BumpGeneration();
    return true;
}

bool UltraCanvasClipboardHistory::Remove(int64_t id) {
    Impl& d = *impl;
    if (!d.open || !d.Exec("UPDATE entries SET deleted_at = ? WHERE id = ? AND deleted_at = 0", {NowMs(), id})) {
        return false;
    }
    d.BumpGeneration();
    return true;
}

bool UltraCanvasClipboardHistory::Restore(int64_t id) {
    Impl& d = *impl;
    if (!d.open || !d.Exec("UPDATE entries SET deleted_at = 0 WHERE id = ?", {id})) return false;
    d.BumpGeneration();
    return true;
}

int64_t UltraCanvasClipboardHistory::Replace(int64_t id, const ClipboardSnapshot& content, bool keepOriginal) {
    Impl& d = *impl;
    const auto original = Get(id);
    if (!d.open || !original) return 0;

    // An edit is kept whether or not recording is paused or its source is
    // excluded: the person asked for it.
    ClipboardSnapshot edited = content;
    if (edited.sourceApplication.empty()) edited.sourceApplication = original->sourceApplication;
    int64_t newId = 0;
    const ClipboardRecordResult result = RecordContent(edited, &newId, false);
    if (result != ClipboardRecordResult::Recorded && result != ClipboardRecordResult::MovedToTop) return 0;
    if (newId == id) return id;
    if (!keepOriginal) {
        if (original->pinned) SetPinned(newId, true);
        d.Exec("UPDATE entries SET deleted_at = 1 WHERE id = ?", {id});   // long past the undo window
        Prune();
    }
    d.BumpGeneration();
    return newId;
}

size_t UltraCanvasClipboardHistory::Clear(bool includePinned) {
    Impl& d = *impl;
    if (!d.open) return 0;
    UltraDbResultSet rows;
    if (!d.Query(includePinned ? "SELECT id FROM entries WHERE deleted_at = 0"
                               : "SELECT id FROM entries WHERE deleted_at = 0 AND pinned = 0", {}, rows)) {
        return 0;
    }
    std::vector<int64_t> ids;
    for (const auto& row : rows) ids.push_back(row[0].AsInt64());
    d.HardDelete(ids);
    d.BumpGeneration();
    return ids.size();
}

void UltraCanvasClipboardHistory::Prune() {
    Impl& d = *impl;
    if (!d.open) return;
    const ClipboardHistoryPolicy policy = GetPolicy();
    const int64_t now = NowMs();
    std::vector<int64_t> doomed;

    UltraDbResultSet removed;
    if (d.Query("SELECT id FROM entries WHERE deleted_at > 0 AND deleted_at < ?",
                {now - static_cast<int64_t>(kUndoSeconds) * 1000}, removed)) {
        for (const auto& row : removed) doomed.push_back(row[0].AsInt64());
    }

    UltraDbResultSet pinnedSize;
    uint64_t total = 0;
    if (d.Query("SELECT COALESCE(SUM(size_bytes), 0) FROM entries WHERE deleted_at = 0 AND pinned = 1", {},
                pinnedSize) && !pinnedSize.Empty()) {
        total = static_cast<uint64_t>(pinnedSize.Row(0)[0].AsInt64());
    }
    UltraDbResultSet live;
    if (d.Query("SELECT id, last_used_at, size_bytes FROM entries WHERE deleted_at = 0 AND pinned = 0 "
                "ORDER BY last_used_at DESC, id DESC", {}, live)) {
        const int64_t oldest = now - static_cast<int64_t>(policy.maxAgeDays) * 86400000;
        int kept = 0;
        for (const auto& row : live) {
            const uint64_t size = static_cast<uint64_t>(row[2].AsInt64());
            const bool tooMany = kept >= policy.maxEntries;
            const bool tooOld = row[1].AsInt64() < oldest;
            const bool tooBig = total + size > policy.maxTotalBytes && kept > 0;
            if (tooMany || tooOld || tooBig) {
                doomed.push_back(row[0].AsInt64());
            } else {
                ++kept;
                total += size;
            }
        }
    }
    if (doomed.empty()) return;
    d.HardDelete(doomed);
    d.BumpGeneration();
}

ClipboardHistoryPolicy UltraCanvasClipboardHistory::GetPolicy() {
    Impl& d = *impl;
    if (!d.open) return ClipboardHistoryPolicy::Defaults();
    JSONParseResult parsed;
    const JSONValue root = JSON::Parse(d.Meta("policy"), &parsed);
    return parsed.success ? PolicyFromJson(root) : ClipboardHistoryPolicy::Defaults();
}

bool UltraCanvasClipboardHistory::SetPolicy(const ClipboardHistoryPolicy& policy) {
    Impl& d = *impl;
    if (!d.open || !d.SetMeta("policy", JSON::Serialize(PolicyToJson(policy)))) return false;
    d.BumpGeneration();
    Prune();
    return true;
}

uint64_t UltraCanvasClipboardHistory::GetGeneration() {
    Impl& d = *impl;
    if (!d.open) return 0;
    UltraDbResultSet rows;
    if (!d.Query("SELECT CAST(value AS INTEGER) FROM meta WHERE key = 'generation'", {}, rows) || rows.Empty()) {
        return 0;
    }
    return static_cast<uint64_t>(rows.Row(0)[0].AsInt64());
}

bool UltraCanvasClipboardHistory::AcquireRecorder(const std::string& holder, int priority) {
    Impl& d = *impl;
    if (!d.open || holder.empty()) return false;
    UltraDbResult error;
    const UltraDbHandle tx = UltraDb_Begin(d.connection, &error);
    if (tx == UltraDbInvalidHandle) return false;
    UltraDbResultSet rows;
    if (!UltraDb_QueryInTx(tx, "SELECT holder, priority, heartbeat FROM recorder WHERE id = 1", {}, rows)) {
        UltraDb_Rollback(tx);
        return false;
    }
    const int64_t now = NowMs();
    bool take = rows.Empty();
    if (!take) {
        const auto& row = rows.Row(0);
        take = row[0].AsString() == holder || priority > row[1].AsInt64() ||
               now - row[2].AsInt64() > static_cast<int64_t>(kLeaseSeconds) * 1000;
    }
    if (take && !UltraDb_ExecInTx(tx, "INSERT INTO recorder(id, holder, priority, heartbeat) VALUES(1, ?, ?, ?) "
                                      "ON CONFLICT(id) DO UPDATE SET holder = excluded.holder, "
                                      "priority = excluded.priority, heartbeat = excluded.heartbeat",
                                  {holder, static_cast<int64_t>(priority), now})) {
        UltraDb_Rollback(tx);
        return false;
    }
    return UltraDb_Commit(tx) && take;
}

void UltraCanvasClipboardHistory::ReleaseRecorder(const std::string& holder) {
    Impl& d = *impl;
    if (d.open) d.Exec("DELETE FROM recorder WHERE id = 1 AND holder = ?", {holder});
}

#else // no UltraDatabase in this build: the history is unavailable.

bool UltraCanvasClipboardHistory::Open(const std::string&, const std::string&) {
    return impl->Fail("this build has no database (libsqlite3), so it keeps no clipboard history");
}
void UltraCanvasClipboardHistory::Close() { impl->open = false; }
ClipboardRecordResult UltraCanvasClipboardHistory::Record(const ClipboardSnapshot&, int64_t* entryId) {
    if (entryId) *entryId = 0;
    return ClipboardRecordResult::Failed;
}
ClipboardRecordResult UltraCanvasClipboardHistory::RecordContent(const ClipboardSnapshot&, int64_t* entryId, bool) {
    if (entryId) *entryId = 0;
    return ClipboardRecordResult::Failed;
}
std::vector<ClipboardHistoryEntry> UltraCanvasClipboardHistory::List(const ClipboardHistoryQuery&) { return {}; }
std::optional<ClipboardHistoryEntry> UltraCanvasClipboardHistory::Get(int64_t) { return std::nullopt; }
bool UltraCanvasClipboardHistory::ReadFormats(int64_t, std::vector<ClipboardFormat>& formats) {
    formats.clear();
    return false;
}
std::string UltraCanvasClipboardHistory::ReadText(int64_t) { return ""; }
ClipboardHistoryStats UltraCanvasClipboardHistory::GetStats() { return {}; }
bool UltraCanvasClipboardHistory::MarkUsed(int64_t) { return false; }
bool UltraCanvasClipboardHistory::SetPinned(int64_t, bool) { return false; }
bool UltraCanvasClipboardHistory::Remove(int64_t) { return false; }
bool UltraCanvasClipboardHistory::Restore(int64_t) { return false; }
int64_t UltraCanvasClipboardHistory::Replace(int64_t, const ClipboardSnapshot&, bool) { return 0; }
size_t UltraCanvasClipboardHistory::Clear(bool) { return 0; }
void UltraCanvasClipboardHistory::Prune() {}
ClipboardHistoryPolicy UltraCanvasClipboardHistory::GetPolicy() { return ClipboardHistoryPolicy::Defaults(); }
bool UltraCanvasClipboardHistory::SetPolicy(const ClipboardHistoryPolicy&) { return false; }
uint64_t UltraCanvasClipboardHistory::GetGeneration() { return 0; }
bool UltraCanvasClipboardHistory::AcquireRecorder(const std::string&, int) { return false; }
void UltraCanvasClipboardHistory::ReleaseRecorder(const std::string&) {}

#endif // ULTRACANVAS_HAS_DATABASE

// ===== THE LIVE CLIPBOARD =====
bool CaptureClipboard(UltraCanvasClipboard& clipboard, ClipboardSnapshot& snapshot) {
    snapshot = ClipboardSnapshot{};
    std::vector<std::string> paths;
    bool cut = false;
    if (clipboard.GetFiles(paths, cut) && !paths.empty()) {
        snapshot.formats.push_back({ClipboardMime::Files, MakeFileList(paths, cut)});
        return true;
    }
    std::vector<uint8_t> image;
    std::string format;
    if (clipboard.GetImage(image, format) && LooksLikeImage(image, NormaliseImageMime(format))) {
        snapshot.formats.push_back({NormaliseImageMime(format), std::move(image)});
        return true;
    }
    std::string text;
    if (clipboard.GetText(text) && !Trimmed(text).empty()) {
        snapshot.SetText(text);
        // Kept when it is markup: an owner that hands its text to every
        // request would otherwise make every copy "formatted".
        std::string html;
        if (clipboard.GetHtml(html) && html != text && html.find('<') != std::string::npos &&
            html.find('>') != std::string::npos) {
            snapshot.formats.push_back({ClipboardMime::Html, Bytes(html)});
        }
        return true;
    }
    return false;
}

bool RestoreToClipboard(UltraCanvasClipboard& clipboard, const std::vector<ClipboardFormat>& formats) {
    ClipboardSnapshot snapshot;
    snapshot.formats = formats;
    if (const ClipboardFormat* files = snapshot.Find(ClipboardMime::Files)) {
        std::vector<std::string> paths;
        bool cut = false;
        if (ParseFileList(files->data, paths, cut)) return clipboard.SetFiles(paths, cut);
    }
    if (const ClipboardFormat* image = snapshot.FindImage()) {
        return clipboard.SetImage(image->data, image->mime);
    }
    const std::string text = snapshot.GetText();
    if (const ClipboardFormat* html = snapshot.Find(ClipboardMime::Html)) {
        if (!html->data.empty()) return clipboard.SetHtml(String(html->data), text);
    }
    return !text.empty() && clipboard.SetText(text);
}

// ===== THE RECORDER =====
void UltraCanvasClipboardRecorder::Attach(UltraCanvasClipboardHistory* historyToRecordInto,
                                          UltraCanvasClipboard* clipboardToWatch,
                                          const std::string& role, int recorderPriority) {
    Detach();
    history = historyToRecordInto;
    clipboard = clipboardToWatch;
    priority = recorderPriority;
    std::string unique;
    if (!UltraCrypt_GenerateUuidV4(unique)) unique = std::to_string(NowMs());
    holder = role + ":" + unique;
    recording = false;
    capturedInitial = false;
    lastLeaseCheck = 0;
    onClipboard = 0;
}

void UltraCanvasClipboardRecorder::Detach() {
    if (history && recording) history->ReleaseRecorder(holder);
    history = nullptr;
    clipboard = nullptr;
    recording = false;
}

void UltraCanvasClipboardRecorder::Tick() {
    if (!history || !clipboard || !history->IsOpen()) return;
    UltraCanvasClipboardBackend* backend = clipboard->GetBackend();
    if (!backend) return;

    const int64_t now = NowMs();
    if (lastLeaseCheck == 0 || now - lastLeaseCheck >= 2000) {
        const bool was = recording;
        recording = history->AcquireRecorder(holder, priority);
        lastLeaseCheck = now;
        if (was && !recording) capturedInitial = false;
    }

    bool changed = backend->HasClipboardChanged();
    if (changed) backend->ResetChangeState();
    if (!recording) return;
    if (!capturedInitial) {
        // What is on the clipboard when recording starts counts as a copy.
        capturedInitial = true;
        changed = true;
    }
    if (!changed) return;
    if (backend->IsClipboardMarkedSecret()) {   // a password manager's copy
        onClipboard = 0;
        return;
    }

    ClipboardSnapshot snapshot;
    if (!CaptureClipboard(*clipboard, snapshot)) {
        // Nothing to read. When that is because the program that copied has
        // quit (on X11 the clipboard leaves with it), put the copy back - the
        // freedesktop clipboard manager's job, done here from the history.
        if (onClipboard != 0 && !backend->HasClipboardOwner()) {
            std::vector<ClipboardFormat> formats;
            if (history->ReadFormats(onClipboard, formats)) RestoreToClipboard(*clipboard, formats);
        }
        onClipboard = 0;
        return;
    }
    if (sourceProvider) snapshot.sourceApplication = sourceProvider();
    int64_t entryId = 0;
    const ClipboardRecordResult result = history->Record(snapshot, &entryId);
    onClipboard = entryId;
    if ((result == ClipboardRecordResult::Recorded || result == ClipboardRecordResult::MovedToTop) && onRecorded) {
        onRecorded(entryId, result);
    }
}

} // namespace UltraCanvas
