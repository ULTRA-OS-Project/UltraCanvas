// core/UltraNet/UltraNetMime.cpp
// Implementation of the UltraNet MIME utility: transfer-encoding codecs,
// RFC 2047 header words, message parsing into a decoded part tree, and message
// building. Pure C++ / STL — no libcurl or platform dependency; other
// charsets than UTF-8 and Latin-1 go through iconv where the build has it
// (ULTRANET_HAS_ICONV).
// Version: 0.3.1 - MimeBuild takes a Message-ID passed among the extra headers
// Version: 0.3.0 - HTML messages with a text alternative and cid: pictures
//                  (multipart/alternative + related)
// Version: 0.2.0 - every iconv charset (ISO-2022-JP, Shift_JIS, EUC-*, GB*,
//                  KOI8-R, windows-125x, ...), and raw ISO-2022-JP header text.
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNet/UltraNetMime.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <UltraCanvasUtils.h>

#if defined(ULTRANET_HAS_ICONV)
#include <cerrno>
#include <iconv.h>
#endif

namespace {

// ---- small helpers ---------------------------------------------------------

std::string Lower(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Well-formed UTF-8 (no overlongs, surrogates or values past U+10FFFF).
bool IsValidUtf8(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len; uint32_t cp;
        if (c < 0x80) { ++i; continue; }
        else if ((c >> 5) == 0x6) { len = 2; cp = c & 0x1F; }
        else if ((c >> 4) == 0xE) { len = 3; cp = c & 0x0F; }
        else if ((c >> 3) == 0x1E) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + len > s.size()) return false;
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char d = static_cast<unsigned char>(s[i + k]);
            if ((d >> 6) != 0x2) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

// ISO-8859-1 / Windows-1252 byte stream -> UTF-8.
std::string Latin1ToUtf8(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (unsigned char b : in) {
        if (b < 0x80) out.push_back(static_cast<char>(b));
        else { out.push_back(static_cast<char>(0xC0 | (b >> 6)));
               out.push_back(static_cast<char>(0x80 | (b & 0x3F))); }
    }
    return out;
}

#if defined(ULTRANET_HAS_ICONV)
// `bytes` in `charset` -> UTF-8 through iconv. Returns false when iconv does
// not know the charset. `clean` says whether every byte converted; a byte
// that did not becomes U+FFFD and the conversion carries on after it.
bool IconvToUtf8(const std::string& bytes, const std::string& charset,
                 std::string& out, bool& clean) {
    iconv_t cd = iconv_open("UTF-8", charset.c_str());
    if (cd == reinterpret_cast<iconv_t>(-1)) return false;
    out.clear();
    clean = true;
    std::vector<char> buf(bytes.size() * 4 + 16);
    char* in = const_cast<char*>(bytes.data());
    size_t inLeft = bytes.size();
    while (true) {
        char* o = buf.data();
        size_t oLeft = buf.size();
        const size_t r = inLeft ? iconv(cd, &in, &inLeft, &o, &oLeft)
                                : iconv(cd, nullptr, nullptr, &o, &oLeft);   // flush
        out.append(buf.data(), buf.size() - oLeft);
        if (r != static_cast<size_t>(-1)) {
            if (inLeft == 0) {
                // Emit any shift-back sequence the state still owes.
                char* f = buf.data(); size_t fLeft = buf.size();
                iconv(cd, nullptr, nullptr, &f, &fLeft);
                out.append(buf.data(), buf.size() - fLeft);
                break;
            }
            continue;
        }
        if (errno == E2BIG) continue;          // out holds the part so far
        clean = false;
        if (inLeft == 0) break;
        out += "\xEF\xBF\xBD";                // EILSEQ / EINVAL: U+FFFD, skip a byte
        ++in; --inLeft;
        if (errno == EINVAL) break;            // truncated at the end
    }
    iconv_close(cd);
    return true;
}
#endif

// A charset label as mail clients write it -> the names iconv should try, in
// order. The later ones are supersets for text the strict name refuses (a
// Windows-extended Japanese mail labelled plain iso-2022-jp or shift_jis).
std::vector<std::string> IconvNames(const std::string& cs) {
    if (cs == "iso-2022-jp")
        return { "ISO-2022-JP", "ISO-2022-JP-2", "ISO-2022-JP-3", "CP50221" };
    if (cs == "shift_jis" || cs == "shift-jis" || cs == "sjis" || cs == "x-sjis" ||
        cs == "ms_kanji" || cs == "windows-31j")
        // CP932 first: strict SHIFT_JIS maps 0x5C / 0x7E to the yen sign and
        // overline, breaking every path and URL in the text.
        return { "CP932", "SHIFT_JIS" };
    if (cs == "gb2312" || cs == "gbk" || cs == "x-gbk" || cs == "euc-cn")
        return { "GB18030", "GBK" };
    if (cs == "ks_c_5601-1987" || cs == "euc-kr" || cs == "korean")
        return { "CP949", "EUC-KR" };
    if (cs == "big5" || cs == "x-big5")
        return { "BIG5-HKSCS", "BIG5", "CP950" };
    // Hebrew / Arabic "logical" (-i) and "explicit" (-e) directionality
    // variants: the same bytes as the base charset.
    if (cs.size() > 2 && cs.rfind("iso-8859-", 0) == 0 &&
        (cs.compare(cs.size() - 2, 2, "-i") == 0 || cs.compare(cs.size() - 2, 2, "-e") == 0))
        return { cs.substr(0, cs.size() - 2) };
    if (cs == "unicode-1-1-utf-7") return { "UTF-7" };
    if (cs == "x-mac-roman" || cs == "mac" || cs == "x-mac") return { "MACINTOSH" };
    if (cs == "x-mac-cyrillic" || cs == "x-mac-ukrainian")
        return { "MAC-CYRILLIC", "MACCYRILLIC", "MACUKRAINIAN" };
    if (cs == "hz-gb-2312") return { "HZ-GB-2312", "HZ" };
    if (cs == "tis-620" || cs == "windows-874" || cs == "iso-8859-11")
        return { cs, "CP874", "TIS-620" };
    return { cs };
}

std::string CharsetToUtf8(const std::string& bytes, const std::string& charsetIn) {
    std::string cs = Lower(UltraCanvas::Trim(charsetIn));
    auto star = cs.find('*');                 // strip RFC 2231 language tag
    if (star != std::string::npos) cs = cs.substr(0, star);
    if (cs.empty() || cs == "utf-8" || cs == "utf8" || cs == "us-ascii" || cs == "ascii")
        return bytes;
    const bool latin1 = cs == "iso-8859-1" || cs == "iso8859-1" || cs == "latin1";
    if (latin1) return Latin1ToUtf8(bytes);
#if defined(ULTRANET_HAS_ICONV)
    // iconv knows the rest (windows-1252 included: its 0x80-0x9F are not
    // Latin-1). Take the first name that converts cleanly, else the first
    // that converted at all.
    std::string best;
    bool haveBest = false;
    for (const std::string& name : IconvNames(cs)) {
        std::string out; bool clean = false;
        if (!IconvToUtf8(bytes, name, out, clean)) continue;
        if (clean) return out;
        if (!haveBest) { best = std::move(out); haveBest = true; }
    }
    if (haveBest) return best;
#endif
    if (cs == "windows-1252" || cs == "cp1252") return Latin1ToUtf8(bytes);
    // A charset nobody can convert ("unknown-8bit", "x-user-defined", a
    // typo): keep text that is already UTF-8, and read anything else as
    // windows-1252 / Latin-1 - the commonest 8-bit mail - rather than handing
    // on bytes that are not valid UTF-8.
    if (IsValidUtf8(bytes)) return bytes;
#if defined(ULTRANET_HAS_ICONV)
    { std::string out; bool clean = false;
      if (IconvToUtf8(bytes, "WINDOWS-1252", out, clean) && clean) return out; }
#endif
    return Latin1ToUtf8(bytes);
}

// ---- base64 ----------------------------------------------------------------
// The codec lives in UltraCanvasUtils (UltraCanvas::Base64Encode / Base64Decode);
// this module just routes through it.

// ---- quoted-printable ------------------------------------------------------

std::string QpDecodeImpl(const std::string& in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '=') {
            if (i + 1 < in.size() && (in[i+1] == '\r' || in[i+1] == '\n')) {
                // soft line break
                if (in[i+1] == '\r' && i + 2 < in.size() && in[i+2] == '\n') i += 2;
                else i += 1;
                continue;
            }
            if (i + 2 < in.size()) {
                int hi = HexVal(in[i+1]), lo = HexVal(in[i+2]);
                if (hi >= 0 && lo >= 0) { out.push_back(static_cast<char>((hi << 4) | lo)); i += 2; continue; }
            }
            out.push_back(c);   // stray '='
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string QpEncodeImpl(const std::string& in) {
    static const char* HEX = "0123456789ABCDEF";
    std::string out;
    std::size_t col = 0;
    auto emit = [&](const std::string& s) {
        if (col + s.size() > 75) { out.append("=\r\n"); col = 0; }
        out += s; col += s.size();
    };
    for (unsigned char c : in) {
        if (c == '\n') { out.append("\r\n"); col = 0; continue; }
        if (c == '\r') continue;
        if ((c >= 33 && c <= 126 && c != '=') || c == ' ' || c == '\t') {
            emit(std::string(1, static_cast<char>(c)));
        } else {
            std::string e = "="; e.push_back(HEX[c >> 4]); e.push_back(HEX[c & 0xF]);
            emit(e);
        }
    }
    return out;
}

// ---- RFC 2047 --------------------------------------------------------------

// Q-encoding used inside encoded-words: '_' is space, "=XX" is a byte.
std::string DecodeQWord(const std::string& t) {
    std::string out;
    for (std::size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (c == '_') out.push_back(' ');
        else if (c == '=' && i + 2 < t.size()) {
            int hi = HexVal(t[i+1]), lo = HexVal(t[i+2]);
            if (hi >= 0 && lo >= 0) { out.push_back(static_cast<char>((hi << 4) | lo)); i += 2; }
            else out.push_back(c);
        } else out.push_back(c);
    }
    return out;
}

// Try to parse an encoded-word starting at s[i]; on success fill `out` (UTF-8)
// and set nextI past the "?=".
bool TryEncodedWord(const std::string& s, std::size_t i, std::string& out, std::size_t& nextI) {
    if (i + 1 >= s.size() || s[i] != '=' || s[i+1] != '?') return false;
    std::size_t cs = i + 2;
    std::size_t q1 = s.find('?', cs);
    if (q1 == std::string::npos) return false;
    if (q1 + 2 >= s.size() || s[q1+2] != '?') return false;
    char enc = static_cast<char>(std::toupper(static_cast<unsigned char>(s[q1+1])));
    std::size_t textStart = q1 + 3;
    std::size_t end = s.find("?=", textStart);
    if (end == std::string::npos) return false;

    std::string charset = s.substr(cs, q1 - cs);
    std::string text = s.substr(textStart, end - textStart);
    std::string rawBytes;
    if (enc == 'B') { std::vector<uint8_t> b = UltraCanvas::Base64Decode(text); rawBytes.assign(b.begin(), b.end()); }
    else if (enc == 'Q') { rawBytes = DecodeQWord(text); }
    else return false;

    out = CharsetToUtf8(rawBytes, charset);
    nextI = end + 2;
    return true;
}

// ---- charset guess for unlabelled 8-bit header text -------------------------
//
// RFC 5322 headers are 7-bit; 8-bit text belongs in RFC 2047 encoded-words
// (or is UTF-8, RFC 6532). Plenty of mailers still put raw bytes in their
// local charset straight into Subject: and From:. UTF-8 is recognised by
// being valid; anything else is guessed: the caller's hint (the message
// body's charset - the same mailer wrote both) when it converts cleanly,
// else the candidate whose conversion reads most like text in its own
// script. A guess stays a guess on a few bytes, but a wrong one is no worse
// than the raw bytes shown before, and a hint settles most real cases.

#if defined(ULTRANET_HAS_ICONV)
std::vector<uint32_t> Utf8Codepoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += len;
    }
    return out;
}

enum class Script { Latin, Cyrillic, Greek, Hebrew, Arabic, Japanese, Chinese, Korean };

struct Candidate { const char* charset; Script script; };

// In order of how common unlabelled 8-bit mail in each is; the order breaks
// ties between equally good readings.
const Candidate kCandidates[] = {
    { "WINDOWS-1252", Script::Latin },    { "CP932",        Script::Japanese },
    { "GB18030",      Script::Chinese },  { "CP949",        Script::Korean },
    { "WINDOWS-1251", Script::Cyrillic }, { "BIG5",         Script::Chinese },
    { "EUC-JP",       Script::Japanese }, { "KOI8-R",       Script::Cyrillic },
    { "WINDOWS-1250", Script::Latin },    { "WINDOWS-1253", Script::Greek },
    { "WINDOWS-1256", Script::Arabic },   { "WINDOWS-1255", Script::Hebrew },
};

bool IsLatinLetter(uint32_t c) {
    return (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) ||
           c == 0x152 || c == 0x153 || c == 0x160 || c == 0x161 ||
           c == 0x17D || c == 0x17E || c == 0x178;
}
// Punctuation a real text of any script may carry: quotes, dashes, the
// euro, no-break space, bullets, guillemets, the degree sign, ...
bool IsCommonSymbol(uint32_t c) {
    return c == 0xA0 || c == 0xA9 || c == 0xAB || c == 0xAE || c == 0xB0 ||
           c == 0xBB || c == 0x20AC || (c >= 0x2010 && c <= 0x2026);
}
bool InScript(uint32_t c, Script s) {
    switch (s) {
        case Script::Latin:    return IsLatinLetter(c);
        case Script::Cyrillic: return c >= 0x410 && c <= 0x44F || c == 0x401 || c == 0x451;
        case Script::Greek:    return c >= 0x386 && c <= 0x3CE;
        case Script::Hebrew:   return c >= 0x5D0 && c <= 0x5EA;
        case Script::Arabic:   return c >= 0x621 && c <= 0x64A;
        case Script::Japanese: return (c >= 0x3041 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) ||
                                      (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF01 && c <= 0xFF5E);
        case Script::Chinese:  return (c >= 0x4E00 && c <= 0x9FFF) ||
                                      (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF01 && c <= 0xFF5E);
        case Script::Korean:   return (c >= 0xAC00 && c <= 0xD7A3) ||
                                      (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF01 && c <= 0xFF5E);
    }
    return false;
}

// Byte-level checks where a decoding alone cannot tell: most byte pairs are
// valid GBK / Big5, so the text must use the common-character rows.
bool CommonHanziRows(const std::string& bytes, Script s, const char* charset) {
    const bool big5 = std::string(charset) == "BIG5";
    int pairs = 0, common = 0;
    for (std::size_t i = 0; i + 1 < bytes.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(bytes[i]);
        if (a < 0x81) continue;
        const unsigned char b = static_cast<unsigned char>(bytes[i + 1]);
        ++pairs;
        if (big5) { if ((a >= 0xA4 && a <= 0xC6) || (a == 0xA1 && b >= 0x40)) ++common; }
        else      { if (((a >= 0xB0 && a <= 0xD7) || (a >= 0xA1 && a <= 0xA3)) && b >= 0xA1) ++common; }
        ++i;
    }
    (void)s;
    return pairs > 0 && common * 10 >= pairs * 7;
}

// How much of `bytes` the reading in `c` accounts for as text in its script,
// in non-ASCII input bytes; 0 when it does not read as that script at all.
int ScoreReading(const std::string& bytes, const std::string& utf8, const Candidate& c) {
    int highBytes = 0, run = 0, longestRun = 0;
    for (unsigned char b : bytes) {
        if (b >= 0x80) { ++highBytes; longestRun = std::max(longestRun, ++run); }
        else run = 0;
    }
    int inScript = 0, other = 0, kana = 0, lower = 0, cyr = 0;
    for (uint32_t cp : Utf8Codepoints(utf8)) {
        if (cp < 0x80) continue;
        if (InScript(cp, c.script)) {
            ++inScript;
            if (cp >= 0x3041 && cp <= 0x30FF) ++kana;
            if (c.script == Script::Cyrillic) { ++cyr; if ((cp >= 0x430 && cp <= 0x44F) || cp == 0x451) ++lower; }
        } else if (!IsCommonSymbol(cp)) {
            ++other;
        }
    }
    if (inScript == 0) return 0;
    // Nearly everything must read as that script.
    if (other * 10 > (inScript + other)) return 0;
    switch (c.script) {
        case Script::Latin:
            // Accented letters sit alone inside words ("café", "Grüße");
            // long runs of them are another script read as Latin.
            if (longestRun > 5) return 0;
            break;
        case Script::Japanese:
            // Japanese text has kana; kanji alone is more likely Chinese.
            if (inScript < 2) return 0;
            if (kana == 0) return highBytes / 2;
            break;
        case Script::Chinese:
            if (inScript < 2 || !CommonHanziRows(bytes, c.script, c.charset)) return 0;
            break;
        case Script::Korean: {
            if (inScript < 2) return 0;
            // Common Hangul rows are valid common-row GB2312 too, so both
            // readings score alike. Korean spaces its words, Chinese does
            // not: pure Hangul with a space between words wins the tie.
            bool gap = false, seenHigh = false, spaceAfterHigh = false;
            for (unsigned char b : bytes) {
                if (b >= 0x80) { if (spaceAfterHigh) gap = true; seenHigh = true; }
                else if (b == ' ' && seenHigh) spaceAfterHigh = true;
            }
            if (other == 0 && gap) return highBytes + 1;
            break;
        }
        case Script::Cyrillic:
            // Running text is mostly lower case; the wrong one of KOI8-R and
            // windows-1251 turns it into capitals.
            if (lower * 2 < cyr) return highBytes / 2;
            break;
        default:
            break;
    }
    return highBytes;
}
#endif

// `bytes` (8-bit, not UTF-8, no charset label) -> UTF-8, by `hint` when that
// converts cleanly, else by the best-scoring candidate, else as windows-1252.
std::string GuessCharsetToUtf8(const std::string& bytes, const std::string& hint) {
#if defined(ULTRANET_HAS_ICONV)
    if (!hint.empty()) {
        const std::string cs = Lower(UltraCanvas::Trim(hint));
        if (cs != "us-ascii" && cs != "ascii" && cs != "utf-8" && cs != "utf8") {
            for (const std::string& name : IconvNames(cs)) {
                std::string out; bool clean = false;
                if (IconvToUtf8(bytes, name, out, clean) && clean) return out;
            }
        }
    }
    std::string best;
    int bestScore = 0;
    for (const Candidate& c : kCandidates) {
        std::string out; bool clean = false;
        if (!IconvToUtf8(bytes, c.charset, out, clean) || !clean) continue;
        const int score = ScoreReading(bytes, out, c);
        if (score > bestScore) { bestScore = score; best = std::move(out); }
    }
    if (bestScore > 0) return best;
    { std::string out; bool clean = false;
      if (IconvToUtf8(bytes, "WINDOWS-1252", out, clean) && clean) return out; }
#else
    (void)hint;
#endif
    return Latin1ToUtf8(bytes);
}

bool HasHighBytes(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return true;
    return false;
}

// Header text written in ISO-2022-JP without encoded-words (older Japanese
// mailers): 7-bit, with ESC $ B / ESC $ @ shifting into JIS X 0208. Encoded-
// words are plain ASCII, so converting the whole value first leaves them
// intact for the decoder below.
bool LooksLikeRawIso2022Jp(const std::string& s) {
    bool esc = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0x80) return false;
        if (c == 0x1B && i + 2 < s.size() && s[i + 1] == '$' &&
            (s[i + 2] == 'B' || s[i + 2] == '@'))
            esc = true;
    }
    return esc;
}

std::string DecodeHeaderImpl(const std::string& rawIn, const std::string& hint = "") {
    // Unlabelled text first: raw ISO-2022-JP escapes, or 8-bit bytes that
    // are not UTF-8. Encoded-words are plain ASCII, which every candidate
    // charset leaves as it is, so they are decoded afterwards as usual.
    std::string raw = rawIn;
    if (LooksLikeRawIso2022Jp(rawIn))
        raw = CharsetToUtf8(rawIn, "iso-2022-jp");
    else if (HasHighBytes(rawIn) && !IsValidUtf8(rawIn))
        raw = GuessCharsetToUtf8(rawIn, hint);
    std::string result;
    std::size_t i = 0, n = raw.size();
    bool lastWasWord = false;
    while (i < n) {
        std::string dec; std::size_t j;
        if (TryEncodedWord(raw, i, dec, j)) { result += dec; i = j; lastWasWord = true; continue; }
        if (lastWasWord && std::isspace(static_cast<unsigned char>(raw[i]))) {
            std::size_t k = i;
            while (k < n && std::isspace(static_cast<unsigned char>(raw[k]))) ++k;
            std::string dec2; std::size_t j2;
            if (k < n && TryEncodedWord(raw, k, dec2, j2)) { result += dec2; i = j2; continue; }
        }
        result.push_back(raw[i]); ++i; lastWasWord = false;
    }
    return result;
}

// ---- header / structure parsing --------------------------------------------

bool SplitHeadersBody(const std::string& raw, std::string& headers, std::string& body) {
    std::size_t p = raw.find("\r\n\r\n");
    if (p != std::string::npos) { headers = raw.substr(0, p); body = raw.substr(p + 4); return true; }
    p = raw.find("\n\n");
    if (p != std::string::npos) { headers = raw.substr(0, p); body = raw.substr(p + 2); return true; }
    headers = raw; body.clear();
    return false;
}

// Ordered header list (name, value) with folded lines joined.
std::vector<std::pair<std::string, std::string>> ParseHeaderList(const std::string& block) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream is(block);
    std::string line, curName, curVal;
    auto flush = [&]() { if (!curName.empty()) out.emplace_back(curName, UltraCanvas::Trim(curVal)); curName.clear(); curVal.clear(); };
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            curVal += ' '; curVal += UltraCanvas::Trim(line);            // folded continuation
        } else {
            flush();
            std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            curName = UltraCanvas::Trim(line.substr(0, colon));
            curVal  = UltraCanvas::Trim(line.substr(colon + 1));
        }
    }
    flush();
    return out;
}

std::string GetHeader(const std::vector<std::pair<std::string, std::string>>& hs,
                      const std::string& name) {
    std::string ln = Lower(name);
    for (const auto& h : hs) if (Lower(h.first) == ln) return h.second;
    return "";
}

// Parse "value; key=val; key2=\"val2\"" -> mediaType/value + params map.
void ParseValueWithParams(const std::string& header, std::string& value,
                          std::map<std::string, std::string>& params) {
    std::size_t semi = header.find(';');
    value = Lower(UltraCanvas::Trim(header.substr(0, semi)));
    if (semi == std::string::npos) return;
    std::string rest = header.substr(semi + 1);
    std::size_t i = 0;
    while (i < rest.size()) {
        std::size_t eq = rest.find('=', i);
        if (eq == std::string::npos) break;
        std::string key = Lower(UltraCanvas::Trim(rest.substr(i, eq - i)));
        std::size_t vstart = eq + 1;
        while (vstart < rest.size() && std::isspace(static_cast<unsigned char>(rest[vstart]))) ++vstart;
        std::string val;
        if (vstart < rest.size() && rest[vstart] == '"') {
            std::size_t vend = rest.find('"', vstart + 1);
            if (vend == std::string::npos) vend = rest.size();
            val = rest.substr(vstart + 1, vend - vstart - 1);
            i = vend + 1;
            std::size_t nsemi = rest.find(';', i);
            i = (nsemi == std::string::npos) ? rest.size() : nsemi + 1;
        } else {
            std::size_t vend = rest.find(';', vstart);
            if (vend == std::string::npos) vend = rest.size();
            val = UltraCanvas::Trim(rest.substr(vstart, vend - vstart));
            i = (vend == rest.size()) ? rest.size() : vend + 1;
        }
        if (!key.empty()) params[key] = val;
    }
}

// ---- RFC 2231 parameter values ---------------------------------------------
// Reassembles split / percent-encoded parameters such as
//   name*0*=utf-8''%D0%9C...; name*1*=...; name*2*=....pdf   (continuations)
//   filename*=utf-8''%e2%82%ac.txt                          (single extended)
//   filename="notes.txt"                                     (plain / RFC 2047)

// Percent-decode "%XX" sequences. RFC 2231 does NOT treat '+' as a space.
std::string PercentDecode(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = HexVal(s[i+1]), lo = HexVal(s[i+2]);
            if (hi >= 0 && lo >= 0) { out.push_back(static_cast<char>((hi << 4) | lo)); i += 2; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

// Split an extended value "charset'lang'pct-encoded" into its charset and the
// percent-decoded bytes. The charset'lang' prefix is optional (only present on
// section 0 of a continuation, or on a single extended value).
void SplitExtendedValue(const std::string& v, std::string& charset, std::string& bytes) {
    std::size_t a = v.find('\'');
    std::size_t b = (a == std::string::npos) ? std::string::npos : v.find('\'', a + 1);
    if (a != std::string::npos && b != std::string::npos) {
        charset = v.substr(0, a);
        bytes   = PercentDecode(v.substr(b + 1));
    } else {
        charset.clear();
        bytes = PercentDecode(v);
    }
}

// Reassemble the parameter named `base` from a params map, decoding to UTF-8.
// Returns "" when the parameter is absent.
std::string ReconstructParam(const std::map<std::string, std::string>& params,
                             const std::string& base) {
    // 1. Continuations: base*0[*], base*1[*], ...
    if (params.count(base + "*0") || params.count(base + "*0*")) {
        std::string charset, assembled;
        for (int n = 0; ; ++n) {
            const std::string k = base + "*" + std::to_string(n);
            auto enc = params.find(k + "*");   // extended (percent-encoded) section
            auto lit = params.find(k);          // literal section
            if (enc != params.end()) {
                std::string cs, bytes;
                SplitExtendedValue(enc->second, cs, bytes);  // prefix only on section 0
                if (n == 0) charset = cs;
                assembled += bytes;
            } else if (lit != params.end()) {
                assembled += lit->second;
            } else {
                break;
            }
        }
        return CharsetToUtf8(assembled, charset);
    }
    // 2. Single extended: base*
    if (auto it = params.find(base + "*"); it != params.end()) {
        std::string cs, bytes;
        SplitExtendedValue(it->second, cs, bytes);
        return CharsetToUtf8(bytes, cs);
    }
    // 3. Plain value (possibly an RFC 2047 encoded-word).
    if (auto it = params.find(base); it != params.end())
        return DecodeHeaderImpl(it->second);
    return "";
}

std::vector<std::string> SplitMultipart(const std::string& body, const std::string& boundary) {
    std::vector<std::string> parts;
    const std::string d = "--" + boundary;
    std::size_t pos = body.find(d);
    if (pos == std::string::npos) return parts;
    pos += d.size();
    while (pos <= body.size()) {
        if (pos + 1 < body.size() && body[pos] == '-' && body[pos+1] == '-') break; // closing
        std::size_t lineEnd = body.find('\n', pos);
        if (lineEnd == std::string::npos) break;
        std::size_t partStart = lineEnd + 1;
        std::size_t next = body.find(d, partStart);
        if (next == std::string::npos) break;
        std::size_t partEnd = next;
        if (partEnd >= 2 && body.compare(partEnd - 2, 2, "\r\n") == 0) partEnd -= 2;
        else if (partEnd >= 1 && body[partEnd - 1] == '\n') partEnd -= 1;
        parts.push_back(body.substr(partStart, partEnd - partStart));
        pos = next + d.size();
    }
    return parts;
}

void ParsePart(const std::string& raw, UltraNetMimePart& part) {
    std::string headerBlock, body;
    SplitHeadersBody(raw, headerBlock, body);
    auto hs = ParseHeaderList(headerBlock);
    for (const auto& h : hs) part.headers[h.first] = h.second;

    std::string ct = GetHeader(hs, "content-type");
    std::map<std::string, std::string> ctParams;
    std::string mediaType;
    ParseValueWithParams(ct, mediaType, ctParams);
    if (mediaType.empty()) mediaType = "text/plain";
    part.mediaType = mediaType;
    part.charset = ctParams.count("charset") ? Lower(ctParams["charset"]) : "";
    part.transferEncoding = Lower(UltraCanvas::Trim(GetHeader(hs, "content-transfer-encoding")));

    std::string cd = GetHeader(hs, "content-disposition");
    std::map<std::string, std::string> cdParams;
    std::string disp;
    ParseValueWithParams(cd, disp, cdParams);
    part.disposition = disp;

    // Content-Disposition filename takes priority over Content-Type name; both
    // may be RFC 2231 split / percent-encoded or RFC 2047 encoded-words.
    std::string filename = ReconstructParam(cdParams, "filename");
    if (filename.empty()) filename = ReconstructParam(ctParams, "name");
    part.filename = filename;   // already fully decoded to UTF-8

    std::string cid = UltraCanvas::Trim(GetHeader(hs, "content-id"));
    if (!cid.empty() && cid.front() == '<' && cid.back() == '>') cid = cid.substr(1, cid.size() - 2);
    part.contentId = cid;

    if (mediaType.rfind("multipart/", 0) == 0 && ctParams.count("boundary")) {
        part.isMultipart = true;
        for (const auto& sub : SplitMultipart(body, ctParams["boundary"])) {
            UltraNetMimePart child;
            ParsePart(sub, child);
            part.children.push_back(std::move(child));
        }
    } else {
        if (part.transferEncoding == "base64") {
            part.body = UltraCanvas::Base64Decode(body);
        } else if (part.transferEncoding == "quoted-printable") {
            std::string s = QpDecodeImpl(body);
            part.body.assign(s.begin(), s.end());
        } else {
            part.body.assign(body.begin(), body.end());
        }
    }
}

// ---- display body / attachment walking -------------------------------------

bool FindSpecificText(const UltraNetMimePart& part, const std::string& mt, std::string& out) {
    if (!part.isMultipart) {
        if (part.mediaType == mt) { out = CharsetToUtf8(std::string(part.body.begin(), part.body.end()), part.charset); return true; }
        return false;
    }
    for (const auto& c : part.children) if (FindSpecificText(c, mt, out)) return true;
    return false;
}

bool FindDisplay(const UltraNetMimePart& part, std::string& out, bool& isHtml) {
    if (!part.isMultipart) {
        if (part.mediaType == "text/html")  { out = CharsetToUtf8(std::string(part.body.begin(), part.body.end()), part.charset); isHtml = true;  return true; }
        if (part.mediaType == "text/plain") {
            // A downloadable .txt attachment is not the display body.
            if (part.disposition == "attachment") return false;
            out = CharsetToUtf8(std::string(part.body.begin(), part.body.end()), part.charset); isHtml = false; return true;
        }
        return false;
    }
    if (part.mediaType == "multipart/alternative") {
        if (FindSpecificText(part, "text/html",  out)) { isHtml = true;  return true; }
        if (FindSpecificText(part, "text/plain", out)) { isHtml = false; return true; }
    }
    for (const auto& c : part.children) if (FindDisplay(c, out, isHtml)) return true;
    return false;
}

void WalkAttachments(const UltraNetMimePart& part, std::vector<UltraNetMimeAttachmentView>& out,
                     bool includeInline) {
    if (part.isMultipart) {
        for (const auto& c : part.children) WalkAttachments(c, out, includeInline);
        return;
    }
    const bool isText = part.mediaType.rfind("text/", 0) == 0;
    // A part marked inline that carries a Content-ID is a picture of the HTML
    // body (its cid: link), even with a file name - not an attachment.
    const bool bodyPicture = part.disposition == "inline" && !part.contentId.empty();
    bool isAttachment = (part.disposition == "attachment") ||
                        (!part.filename.empty() && !isText && !bodyPicture);
    bool isInline = (part.disposition == "inline") || (!part.contentId.empty() && !isText);
    if (isAttachment || (isInline && includeInline)) {
        UltraNetMimeAttachmentView v;
        v.filename = part.filename;
        v.mediaType = part.mediaType;
        v.contentId = part.contentId;
        v.isInline = isInline && !isAttachment;
        v.data = part.body;
        out.push_back(std::move(v));
    }
}

// ---- build helpers ---------------------------------------------------------

std::string CommaJoin(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) out += ", "; out += v[i]; }
    return out;
}

std::string Rfc2822Date() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S +0000", &tm);
    return buf;
}

std::string GenBoundary() {
    static std::atomic<uint64_t> counter{0};
    std::ostringstream os;
    os << "----=_UltraNet_"
       << std::chrono::system_clock::now().time_since_epoch().count()
       << "_" << counter.fetch_add(1);
    return os.str();
}

std::string GenMessageId() {
    std::random_device rd;
    std::ostringstream os;
    os << "<" << std::hex << rd() << rd() << "@ultranet.local>";
    return os.str();
}

bool ReservedHeader(const std::string& name) {
    std::string n = Lower(name);
    return n == "from" || n == "to" || n == "cc" || n == "bcc" || n == "subject" ||
           n == "date" || n == "message-id" || n == "mime-version" ||
           n == "content-type" || n == "content-transfer-encoding";
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

std::string UltraNet_Base64Encode(const std::vector<uint8_t>& data, bool wrap76Cols) {
    return UltraCanvas::Base64Encode(data, wrap76Cols);
}
bool UltraNet_Base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    out = UltraCanvas::Base64Decode(text);
    return true;
}
std::string UltraNet_QuotedPrintableEncode(const std::string& text) { return QpEncodeImpl(text); }
std::string UltraNet_QuotedPrintableDecode(const std::string& text) { return QpDecodeImpl(text); }

std::string UltraNet_MimeDecodeHeader(const std::string& raw) { return DecodeHeaderImpl(raw); }
std::string UltraNet_MimeDecodeHeader(const std::string& raw, const std::string& fallbackCharset) {
    return DecodeHeaderImpl(raw, fallbackCharset);
}

std::string UltraNet_MimeEncodeHeader(const std::string& utf8Value, bool useBase64) {
    bool ascii = true;
    for (unsigned char c : utf8Value) if (c >= 0x80) { ascii = false; break; }
    if (ascii) return utf8Value;
    if (useBase64) {
        std::vector<uint8_t> b(utf8Value.begin(), utf8Value.end());
        return "=?UTF-8?B?" + UltraCanvas::Base64Encode(b, false) + "?=";
    }
    std::string q;
    for (unsigned char c : utf8Value) {
        if (c == ' ') q.push_back('_');
        else if ((c >= 33 && c <= 126) && c != '=' && c != '?' && c != '_')
            q.push_back(static_cast<char>(c));
        else { static const char* H = "0123456789ABCDEF"; q.push_back('='); q.push_back(H[c >> 4]); q.push_back(H[c & 0xF]); }
    }
    return "=?UTF-8?Q?" + q + "?=";
}

namespace {
// RFC 2047 §5(3): an encoded-word standing in a phrase - which is what a
// display name is - may only carry letters, digits and "!*+-/" besides the
// "=?", "?" and "_" of its own syntax. The subject encoder is looser (it
// passes every printable through), and a comma or a quote left literal there
// would split or unbalance the address list it sits in.
std::string EncodeWordInPhrase(const std::string& utf8Value) {
    static const char* H = "0123456789ABCDEF";
    std::string q;
    for (unsigned char c : utf8Value) {
        const bool safe = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                          (c >= 'a' && c <= 'z') ||
                          c == '!' || c == '*' || c == '+' || c == '-' || c == '/';
        if (c == ' ')   q.push_back('_');
        else if (safe)  q.push_back(static_cast<char>(c));
        else { q.push_back('='); q.push_back(H[c >> 4]); q.push_back(H[c & 0xF]); }
    }
    return "=?UTF-8?Q?" + q + "?=";
}
} // namespace

std::string UltraNet_MimeEncodeAddress(const std::string& utf8Address, bool useBase64) {
    bool ascii = true;
    for (unsigned char c : utf8Address) if (c >= 0x80) { ascii = false; break; }
    if (ascii) return utf8Address;

    // "Display Name <local@domain>": only the name may be encoded - an
    // encoded-word inside the angle-addr is not an address any more. An address
    // with no display name has nothing encodable, so it is passed through and
    // left for the server to reject or accept (IDN/SMTPUTF8 territory).
    const std::size_t lt = utf8Address.rfind('<');
    if (lt == std::string::npos || utf8Address.find('>', lt) == std::string::npos)
        return utf8Address;

    std::string name = UltraCanvas::Trim(utf8Address.substr(0, lt));
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
        name = name.substr(1, name.size() - 2);
    if (name.empty()) return utf8Address;

    // Base64 needs no phrase-specific escaping: its alphabet is already inside
    // what a phrase allows.
    const std::string encodedName = useBase64 ? UltraNet_MimeEncodeHeader(name, true)
                                              : EncodeWordInPhrase(name);
    return encodedName + " " + utf8Address.substr(lt);
}

// ---- IMAP modified UTF-7 (RFC 3501 §5.1.3) ---------------------------------

namespace {

// Append one Unicode code point as UTF-8.
void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
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

// Modified BASE64 alphabet value (RFC 3501: standard alphabet with ',' for
// '/'); -1 for any character that is not part of the alphabet.
int ModBase64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == ',') return 63;
    return -1;
}

// Decode one "&...-" shift (chars is the run between '&' and '-', without them)
// of modified BASE64 into UTF-16BE code units, transcoded to UTF-8, appended to
// `out`. Returns false on a malformed run (caller then falls back to raw).
bool DecodeUtf7Shift(const std::string& chars, std::string& out) {
    // Modified BASE64 -> a bitstream -> 16-bit UTF-16 code units.
    std::vector<uint16_t> units;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : chars) {
        int v = ModBase64Val(c);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 16) {
            bits -= 16;
            units.push_back(static_cast<uint16_t>((acc >> bits) & 0xFFFF));
        }
    }
    // Any leftover bits must be zero padding (< 6 bits worth); reject stray 1s.
    if (bits >= 6) return false;
    if (bits > 0 && (acc & ((1u << bits) - 1)) != 0) return false;

    // UTF-16BE -> code points (combine surrogate pairs).
    for (std::size_t i = 0; i < units.size(); ++i) {
        uint16_t u = units[i];
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 >= units.size()) return false;
            uint16_t lo = units[i + 1];
            if (lo < 0xDC00 || lo > 0xDFFF) return false;
            uint32_t cp = 0x10000 + ((static_cast<uint32_t>(u - 0xD800) << 10) |
                                     static_cast<uint32_t>(lo - 0xDC00));
            AppendUtf8(out, cp);
            ++i;
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            return false;   // lone low surrogate
        } else {
            AppendUtf8(out, u);
        }
    }
    return true;
}

} // namespace

std::string UltraNet_ImapUtf7Decode(const std::string& mUtf7) {
    // Fast path: a plain-ASCII name with no shift character is already correct.
    if (mUtf7.find('&') == std::string::npos) return mUtf7;

    std::string out;
    out.reserve(mUtf7.size());
    for (std::size_t i = 0; i < mUtf7.size(); ) {
        char c = mUtf7[i];
        if (c != '&') { out.push_back(c); ++i; continue; }
        // '&' begins a shift. "&-" is a literal '&'.
        std::size_t dash = mUtf7.find('-', i + 1);
        if (dash == std::string::npos) return mUtf7;   // unterminated: give up, show raw
        if (dash == i + 1) { out.push_back('&'); i = dash + 1; continue; }
        if (!DecodeUtf7Shift(mUtf7.substr(i + 1, dash - i - 1), out))
            return mUtf7;   // malformed: never show worse than the raw name
        i = dash + 1;
    }
    return out;
}

bool UltraNet_MimeParse(const std::string& rawMessage, UltraNetMimeMessage& out) {
    if (rawMessage.empty()) return false;
    out = UltraNetMimeMessage{};
    ParsePart(rawMessage, out.root);

    auto get = [&](const char* name) -> std::string {
        std::string ln = Lower(name);
        for (const auto& kv : out.root.headers) if (Lower(kv.first) == ln) return kv.second;
        return "";
    };
    // The charset of the message's first text part: the charset its mailer
    // most likely wrote any unlabelled 8-bit header text in, too.
    std::string hint;
    {
        std::vector<const UltraNetMimePart*> stack{ &out.root };
        while (!stack.empty() && hint.empty()) {
            const UltraNetMimePart* p = stack.back(); stack.pop_back();
            if (!p->charset.empty()) hint = p->charset;
            for (auto it = p->children.rbegin(); it != p->children.rend(); ++it)
                stack.push_back(&*it);
        }
    }
    out.subject   = DecodeHeaderImpl(get("subject"), hint);
    out.from      = DecodeHeaderImpl(get("from"), hint);
    out.date      = get("date");
    out.messageId = get("message-id");
    out.inReplyTo = get("in-reply-to");
    auto splitAddrs = [&hint](const std::string& v, std::vector<std::string>& dst) {
        std::size_t i = 0;
        while (i < v.size()) {
            std::size_t comma = v.find(',', i);
            if (comma == std::string::npos) comma = v.size();
            std::string a = UltraCanvas::Trim(v.substr(i, comma - i));
            if (!a.empty()) dst.push_back(DecodeHeaderImpl(a, hint));
            i = comma + 1;
        }
    };
    splitAddrs(get("to"), out.to);
    splitAddrs(get("cc"), out.cc);
    return true;
}

bool UltraNet_MimeGetDisplayBody(const UltraNetMimeMessage& message,
                                 std::string& outText, bool& outIsHtml) {
    outText.clear(); outIsHtml = false;
    return FindDisplay(message.root, outText, outIsHtml);
}

void UltraNet_MimeCollectAttachments(const UltraNetMimeMessage& message,
                                     std::vector<UltraNetMimeAttachmentView>& out,
                                     bool includeInline) {
    out.clear();
    WalkAttachments(message.root, out, includeInline);
}

namespace {

// One attachment as a MIME entity (headers, blank line, base64 content).
std::string AttachmentEntity(const UltraNetMimeBuildAttachment& a) {
    const std::string ct = a.mediaType.empty() ? "application/octet-stream" : a.mediaType;
    std::ostringstream os;
    os << "Content-Type: " << ct << "; name=\"" << a.filename << "\"\r\n"
       << "Content-Disposition: " << (a.isInline ? "inline" : "attachment")
       << "; filename=\"" << a.filename << "\"\r\n";
    if (!a.contentId.empty()) os << "Content-ID: <" << a.contentId << ">\r\n";
    os << "Content-Transfer-Encoding: base64\r\n\r\n"
       << UltraCanvas::Base64Encode(a.data, true);
    return os.str();
}

// A text part, quoted-printable.
std::string TextEntity(const std::string& contentType, const std::string& text) {
    return "Content-Type: " + contentType + "\r\n"
           "Content-Transfer-Encoding: quoted-printable\r\n\r\n"
           + QpEncodeImpl(text) + "\r\n";
}

std::string MultipartEntity(const std::string& subtype, const std::string& boundary,
                            const std::vector<std::string>& parts, const std::string& extra = "") {
    std::ostringstream os;
    os << "Content-Type: multipart/" << subtype << "; boundary=\"" << boundary << "\"" << extra
       << "\r\n\r\n";
    for (const auto& part : parts) os << "--" << boundary << "\r\n" << part << "\r\n";
    os << "--" << boundary << "--\r\n";
    return os.str();
}

// The body of an HTML message with a text alternative and/or inline
// pictures (the tree is in the header), wrapped in multipart/mixed when
// there are attachments besides.
std::string BuildRichBody(const UltraNetMimeBuildInput& in, const std::string& bodyCt,
                          const std::string& boundary) {
    std::vector<std::string> relatedParts;
    std::vector<std::string> attachmentParts;
    relatedParts.push_back(TextEntity(bodyCt, in.body));
    for (const auto& a : in.attachments) {
        if (a.isInline && !a.contentId.empty()) relatedParts.push_back(AttachmentEntity(a));
        else attachmentParts.push_back(AttachmentEntity(a));
    }
    // The inner boundaries lead with their own word: one that began with the
    // outer boundary would contain its delimiter line ("--B" in "--B_rel"),
    // which RFC 2046 forbids and a parser splits on.
    const std::string html = relatedParts.size() > 1
        ? MultipartEntity("related", "rel_" + boundary, relatedParts, "; type=\"text/html\"")
        : relatedParts.front();

    std::string body = html;
    if (!in.alternativeText.empty()) {
        std::string textCt = "text/plain";
        if (!in.bodyCharset.empty()) textCt += "; charset=" + in.bodyCharset;
        body = MultipartEntity("alternative", "alt_" + boundary,
                               {TextEntity(textCt, in.alternativeText), html});
    }
    if (attachmentParts.empty()) return body;
    attachmentParts.insert(attachmentParts.begin(), body);
    return MultipartEntity("mixed", boundary, attachmentParts);
}

} // namespace

std::string UltraNet_MimeBuild(const UltraNetMimeBuildInput& in) {
    std::ostringstream os;
    const bool hasAtt = !in.attachments.empty();
    const std::string boundary = in.boundary.empty() ? GenBoundary() : in.boundary;
    const std::string date = in.date.empty() ? Rfc2822Date() : in.date;
    // The Message-ID: the one given, else one passed among the extra headers
    // (an SMTP caller's UltraNetMailMessage::headers - a mail client that also
    // files the message in Sent or Drafts needs the copies to carry the same
    // ID), else a new one. Message-ID stays reserved there: it is written once.
    std::string msgId = in.messageId;
    if (msgId.empty())
        for (const auto& [name, value] : in.extraHeaders)
            if (Lower(name) == "message-id" && !value.empty()) msgId = value;
    if (msgId.empty()) msgId = GenMessageId();

    std::string bodyCt = in.bodyMediaType.empty() ? "text/plain" : in.bodyMediaType;
    if (bodyCt.rfind("text/", 0) == 0 && !in.bodyCharset.empty())
        bodyCt += "; charset=" + in.bodyCharset;

    // Address headers carry a display name the user typed, so they go through
    // the same encoded-word treatment as the subject - a raw "Fröhling" byte in
    // a header is not a legal message and is what an 8-bit-clean server is
    // free to mangle.
    auto encodeAll = [&](const std::vector<std::string>& v) {
        std::vector<std::string> out;
        out.reserve(v.size());
        for (const auto& a : v) out.push_back(UltraNet_MimeEncodeAddress(a));
        return out;
    };

    os << "From: " << UltraNet_MimeEncodeAddress(in.from) << "\r\n";
    if (!in.to.empty()) os << "To: " << CommaJoin(encodeAll(in.to)) << "\r\n";
    if (!in.cc.empty()) os << "Cc: " << CommaJoin(encodeAll(in.cc)) << "\r\n";
    os << "Subject: " << UltraNet_MimeEncodeHeader(in.subject) << "\r\n"
       << "Date: " << date << "\r\n"
       << "Message-ID: " << msgId << "\r\n"
       << "MIME-Version: 1.0\r\n";

    for (const auto& [name, value] : in.extraHeaders)
        if (!ReservedHeader(name)) os << name << ": " << value << "\r\n";

    bool hasRelated = false;
    for (const auto& a : in.attachments)
        if (a.isInline && !a.contentId.empty()) hasRelated = true;
    if (in.bodyMediaType == "text/html" && (!in.alternativeText.empty() || hasRelated)) {
        os << BuildRichBody(in, bodyCt, boundary);
        return os.str();
    }

    if (!hasAtt) {
        os << "Content-Type: " << bodyCt << "\r\n"
           << "Content-Transfer-Encoding: 8bit\r\n\r\n"
           << in.body;
        return os.str();
    }

    os << "Content-Type: multipart/mixed; boundary=\"" << boundary << "\"\r\n\r\n"
       << "This is a multi-part message in MIME format.\r\n"
       << "--" << boundary << "\r\n"
       << "Content-Type: " << bodyCt << "\r\n"
       << "Content-Transfer-Encoding: 8bit\r\n\r\n"
       << in.body << "\r\n";

    for (const auto& a : in.attachments) {
        const std::string ct = a.mediaType.empty() ? "application/octet-stream" : a.mediaType;
        os << "--" << boundary << "\r\n"
           << "Content-Type: " << ct << "; name=\"" << a.filename << "\"\r\n"
           << "Content-Disposition: " << (a.isInline ? "inline" : "attachment")
           << "; filename=\"" << a.filename << "\"\r\n";
        if (a.isInline && !a.contentId.empty())
            os << "Content-ID: <" << a.contentId << ">\r\n";
        os << "Content-Transfer-Encoding: base64\r\n\r\n"
           << UltraCanvas::Base64Encode(a.data, true);
    }
    os << "--" << boundary << "--\r\n";
    return os.str();
}
