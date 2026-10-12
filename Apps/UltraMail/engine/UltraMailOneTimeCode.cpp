// Apps/UltraMail/engine/UltraMailOneTimeCode.cpp
// One-time codes in a message: see UltraMailOneTimeCode.h.
// Version: 0.1.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailOneTimeCode.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace UltraMail {

namespace {

// ===== Text helpers (UTF-8, byte offsets) =====

bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }
bool IsAsciiUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool IsAsciiLower(char c) { return c >= 'a' && c <= 'z'; }
bool IsAsciiAlnum(char c) { return IsAsciiDigit(c) || IsAsciiUpper(c) || IsAsciiLower(c); }

// The code point starting at `i` and its length; a malformed byte reads as
// itself, one byte long.
uint32_t DecodeAt(const std::string& s, std::size_t i, std::size_t& length) {
    const auto b = static_cast<unsigned char>(s[i]);
    length = 1;
    if (b < 0x80) return b;
    const int extra = (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : 0;
    if (extra == 0) return b;
    uint32_t cp = b & (0x3Fu >> extra);
    for (int k = 1; k <= extra; ++k) {
        const std::size_t at = i + static_cast<std::size_t>(k);
        if (at >= s.size()) return b;
        const auto c = static_cast<unsigned char>(s[at]);
        if ((c & 0xC0) != 0x80) return b;
        cp = (cp << 6) | (c & 0x3Fu);
    }
    length = static_cast<std::size_t>(extra) + 1;
    return cp;
}

// A letter for word boundaries: ASCII letters and everything outside ASCII
// except spaces and punctuation that mail text commonly carries.
bool IsLetterCodePoint(uint32_t cp) {
    if (cp < 0x80) return IsAsciiUpper(static_cast<char>(cp)) || IsAsciiLower(static_cast<char>(cp));
    if (cp == 0xA0 || cp == 0xAB || cp == 0xBB) return false;              // nbsp « »
    if (cp >= 0x2000 && cp <= 0x206F) return false;                        // general punctuation
    if (cp >= 0x3000 && cp <= 0x303F) return false;                        // CJK punctuation
    if (cp >= 0xFF00 && cp <= 0xFF0F) return false;                        // fullwidth ! " # ...
    if (cp >= 0xFF1A && cp <= 0xFF20) return false;                        // fullwidth : ; < = > ? @
    return true;
}

// Lower case for the scripts whose code words have capitals - Latin
// (ASCII, Latin-1), Greek and Cyrillic. Every mapping keeps the byte length,
// so offsets into the result are offsets into the original.
std::string Lower(const std::string& s) {
    std::string out = s;
    for (std::size_t i = 0; i < out.size();) {
        std::size_t len = 1;
        const uint32_t cp = DecodeAt(out, i, len);
        uint32_t lower = cp;
        if (cp >= 'A' && cp <= 'Z') lower = cp + 0x20;
        else if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) lower = cp + 0x20;   // À-Þ
        else if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) lower = cp + 0x20;   // Α-Ω
        else if (cp >= 0x410 && cp <= 0x42F) lower = cp + 0x20;                 // А-Я
        else if (cp >= 0x400 && cp <= 0x40F) lower = cp + 0x50;                 // Ѐ-Џ
        if (lower != cp) {
            // Same length by construction: rewrite the bytes in place.
            if (len == 1) {
                out[i] = static_cast<char>(lower);
            } else if (len == 2) {
                out[i] = static_cast<char>(0xC0 | (lower >> 6));
                out[i + 1] = static_cast<char>(0x80 | (lower & 0x3F));
            }
        }
        i += len;
    }
    return out;
}

// ===== The words that say "code" =====

enum class Match {
    Anywhere,    // inside a word too: compounds ("Anmeldecode", "verifieringskod"), and scripts without spaces
    WordStart,   // at the start of a word, at most three letters after it ("pin", "pins", "codul")
};

struct Keyword {
    const char* text;   // lower case
    Match match;
};

// One entry per word; lower case. Kept to words that mean a code to be
// typed in - not "confirmation" or "order", which have numbers of their own.
const Keyword kKeywords[] = {
    // Latin script
    {"code", Match::Anywhere},        // en, de, fr, nl; "Anmelde-Code", "passcode"
    {"c\xC3\xB3" "digo", Match::Anywhere},   // es, pt "código"
    {"codigo", Match::Anywhere},
    {"codice", Match::Anywhere},      // it
    {"cod", Match::WordStart},        // ro "cod", "codul"
    {"kode", Match::Anywhere},        // da, no, id, ms
    {"kod", Match::Anywhere},         // sv, pl, tr, hr, sr, sl, bs ("engångskod", "kodunuz")
    {"k\xC3\xB3" "d", Match::Anywhere},      // cs, sk, hu "kód"
    {"koodi", Match::Anywhere},       // fi "vahvistuskoodi"
    {"kood", Match::Anywhere},        // et
    {"kods", Match::Anywhere},        // lv
    {"kodas", Match::Anywhere},       // lt
    {"m\xC3\xA3", Match::WordStart},  // vi "mã"
    {"\xC5\xA1" "ifra", Match::WordStart},   // hr, sr, cs "šifra"
    {"otp", Match::WordStart},
    {"pin", Match::WordStart},
    {"tan", Match::WordStart},        // de "TAN"
    {"verif", Match::Anywhere},       // verification, verify, Verifizierung, vérification, verifikasi
    {"v\xC3\xA9" "rif", Match::Anywhere},    // fr "vérification"
    {"authenti", Match::Anywhere},    // authentication, Authentifizierung
    {"one-time", Match::Anywhere},
    {"einmal", Match::Anywhere},      // de "Einmalpasswort"
    // Greek, Cyrillic, Armenian, Georgian
    {"\xCE\xBA\xCF\x89\xCE\xB4\xCE\xB9\xCE\xBA", Match::Anywhere},   // κωδικ(ός)
    {"\xD0\xBA\xD0\xBE\xD0\xB4", Match::Anywhere},                   // код
    {"\xD5\xAF\xD5\xB8\xD5\xA4", Match::Anywhere},                   // կոդ
    {"\xE1\x83\x99\xE1\x83\x9D\xE1\x83\x93", Match::Anywhere},       // კოდ(ი)
    // Hebrew, Arabic, Persian
    {"\xD7\xA7\xD7\x95\xD7\x93", Match::Anywhere},                   // קוד
    {"\xD8\xB1\xD9\x85\xD8\xB2", Match::Anywhere},                   // رمز
    {"\xD9\x83\xD9\x88\xD8\xAF", Match::Anywhere},                   // كود
    {"\xDA\xA9\xD8\xAF", Match::WordStart},                          // fa کد
    // Indic, Thai
    {"\xE0\xA4\x95\xE0\xA5\x8B\xE0\xA4\xA1", Match::Anywhere},       // hi कोड
    {"\xE0\xA6\x95\xE0\xA7\x8B\xE0\xA6\xA1", Match::Anywhere},       // bn কোড
    {"\xE0\xB8\xA3\xE0\xB8\xAB\xE0\xB8\xB1\xE0\xB8\xAA", Match::Anywhere},   // th รหัส
    // Chinese
    {"\xE9\xAA\x8C\xE8\xAF\x81\xE7\xA0\x81", Match::Anywhere},       // 验证码
    {"\xE9\xA9\x97\xE8\xAD\x89\xE7\xA2\xBC", Match::Anywhere},       // 驗證碼
    {"\xE6\xA0\xA1\xE9\xAA\x8C\xE7\xA0\x81", Match::Anywhere},       // 校验码
    {"\xE5\x8A\xA8\xE6\x80\x81\xE7\xA0\x81", Match::Anywhere},       // 动态码
    {"\xE5\x8B\x95\xE6\x85\x8B\xE7\xA2\xBC", Match::Anywhere},       // 動態碼
    {"\xE7\xA1\xAE\xE8\xAE\xA4\xE7\xA0\x81", Match::Anywhere},       // 确认码
    {"\xE7\xA2\xBA\xE8\xAA\x8D\xE7\xA2\xBC", Match::Anywhere},       // 確認碼
    {"\xE8\xAE\xA4\xE8\xAF\x81\xE7\xA0\x81", Match::Anywhere},       // 认证码
    {"\xE8\xAA\x8D\xE8\xAD\x89\xE7\xA2\xBC", Match::Anywhere},       // 認證碼
    {"\xE5\xAE\x89\xE5\x85\xA8\xE7\xA0\x81", Match::Anywhere},       // 安全码
    // Japanese
    {"\xE3\x82\xB3\xE3\x83\xBC\xE3\x83\x89", Match::Anywhere},       // コード
    {"\xE8\xAA\x8D\xE8\xA8\xBC\xE7\x95\xAA\xE5\x8F\xB7", Match::Anywhere},   // 認証番号
    {"\xE7\xA2\xBA\xE8\xAA\x8D\xE7\x95\xAA\xE5\x8F\xB7", Match::Anywhere},   // 確認番号
    {"\xE6\x9A\x97\xE8\xA8\xBC\xE7\x95\xAA\xE5\x8F\xB7", Match::Anywhere},   // 暗証番号
    // Korean
    {"\xEC\x9D\xB8\xEC\xA6\x9D\xEB\xB2\x88\xED\x98\xB8", Match::Anywhere},   // 인증번호
    {"\xED\x99\x95\xEC\x9D\xB8\xEB\xB2\x88\xED\x98\xB8", Match::Anywhere},   // 확인번호
    {"\xEC\xBD\x94\xEB\x93\x9C", Match::Anywhere},                   // 코드
};

// Words before "code" that make it some other code: a postal code is not
// typed into a sign-in form.
const char* const kNotACode[] = {
    "postal", "zip", "area", "country", "sort", "error", "status", "source",
    "bank", "swift", "tax", "product", "article", "item", "post", "dial",
    "postcode", "zipcode", "barcode", "qr", "promo", "discount", "voucher",
    "coupon", "gutschein", "rabatt", "fehler",
};

// The start of the code point before byte `i` (> 0).
std::size_t PreviousStart(const std::string& s, std::size_t i) {
    std::size_t back = i - 1;
    while (back > 0 && (static_cast<unsigned char>(s[back]) & 0xC0) == 0x80) --back;
    return back;
}

// The word (a run of letters) a keyword found at byte `pos` of `lower` sits
// in, where that word starts, and the word before it.
void WordsAround(const std::string& lower, std::size_t pos, std::size_t keywordLength,
                 std::string& word, std::size_t& wordStart, std::string& previousWord) {
    auto letterAt = [&](std::size_t i) {
        std::size_t len = 1;
        return IsLetterCodePoint(DecodeAt(lower, i, len));
    };
    std::size_t start = pos;
    while (start > 0 && letterAt(PreviousStart(lower, start))) start = PreviousStart(lower, start);
    std::size_t end = pos + keywordLength;
    while (end < lower.size()) {
        std::size_t len = 1;
        if (!IsLetterCodePoint(DecodeAt(lower, end, len))) break;
        end += len;
    }
    word = lower.substr(start, end - start);
    wordStart = start;
    // The previous word: back over what is not a letter, then over the letters.
    std::size_t p = start;
    while (p > 0 && !letterAt(PreviousStart(lower, p))) p = PreviousStart(lower, p);
    const std::size_t previousEnd = p;
    while (p > 0 && letterAt(PreviousStart(lower, p))) p = PreviousStart(lower, p);
    previousWord = lower.substr(p, previousEnd - p);
}

bool IsNotACodeWord(const std::string& word) {
    for (const char* w : kNotACode)
        if (word == w) return true;
    return false;
}

struct KeywordHit {
    std::size_t start = 0, end = 0;   // byte range in the text
};

// Every place the text says "code", in reading order.
std::vector<KeywordHit> FindKeywords(const std::string& text) {
    std::vector<KeywordHit> hits;
    const std::string lower = Lower(text);
    for (const Keyword& k : kKeywords) {
        const std::size_t klen = std::strlen(k.text);
        for (std::size_t pos = lower.find(k.text); pos != std::string::npos;
             pos = lower.find(k.text, pos + 1)) {
            std::string word, previous;
            std::size_t wordStart = 0;
            WordsAround(lower, pos, klen, word, wordStart, previous);
            if (k.match == Match::WordStart) {
                // At the start of its word, and at most three letters after it.
                if (wordStart != pos) continue;
                std::size_t extra = 0;
                for (std::size_t i = klen; i < word.size(); ++extra) {
                    std::size_t len = 1;
                    DecodeAt(word, i, len);
                    i += len;
                }
                if (extra > 3) continue;
            }
            if (IsNotACodeWord(previous)) continue;
            // "Postcode", "Barcode": the compound itself says what it is.
            bool compoundNotACode = false;
            for (const char* w : kNotACode) {
                const std::string prefix(w);
                if (word.size() > prefix.size() && word.compare(0, prefix.size(), prefix) == 0 &&
                    word.find(k.text, prefix.size()) != std::string::npos) {
                    compoundNotACode = true;
                    break;
                }
            }
            if (compoundNotACode) continue;
            hits.push_back({pos, pos + klen});
        }
    }
    std::sort(hits.begin(), hits.end(),
              [](const KeywordHit& a, const KeywordHit& b) { return a.start < b.start; });
    return hits;
}

// ===== Candidate codes =====

struct Candidate {
    std::size_t start = 0, end = 0;   // byte range in the text
    std::string shown, code;
};

// Separators allowed inside a code between its groups ("123 456",
// "123-456", "G-123456"), and their byte length at `i` (0: none).
std::size_t JoinerAt(const std::string& s, std::size_t i, bool& isSpace) {
    if (i >= s.size()) return 0;
    if (s[i] == ' ') { isSpace = true; return 1; }
    if (s[i] == '-') { isSpace = false; return 1; }
    if (s.compare(i, 2, "\xC2\xA0") == 0) { isSpace = true; return 2; }   // no-break space
    return 0;
}

bool AllDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), IsAsciiDigit);
}

// Whether a run of ASCII letters and digits (separators removed) has the
// shape of a code.
bool LooksLikeCode(const std::string& compact, bool spaced) {
    const std::size_t n = compact.size();
    int digits = 0, upper = 0, lower = 0;
    for (char c : compact) {
        if (IsAsciiDigit(c)) ++digits;
        else if (IsAsciiUpper(c)) ++upper;
        else if (IsAsciiLower(c)) ++lower;
    }
    if (AllDigits(compact)) {
        if (n < 4 || n > 10) return false;
        // A year on its own is a year.
        if (n == 4 && !spaced && (compact.compare(0, 2, "19") == 0 || compact.compare(0, 2, "20") == 0))
            return false;
        return true;
    }
    // Letters and digits: capitals only, at least two digits and a letter.
    if (lower > 0 || n < 5 || n > 12) return false;
    return digits >= 2 && upper >= 1;
}

// Characters that tie a number to something it is part of: a link, an
// address, a price, a version, an "#order" number.
bool TiedBefore(const std::string& s, std::size_t start) {
    if (start == 0) return false;
    const char c = s[start - 1];
    if (IsAsciiAlnum(c)) return true;
    if (c != '\0' && std::strchr("/=@.#_+&%$:\\", c)) {
        // "Code:123456" is a code; "10:30" a time.
        if (c == ':' && (start < 2 || !IsAsciiDigit(s[start - 2]))) return false;
        return true;
    }
    if (c == '-' && start >= 2 && IsAsciiAlnum(s[start - 2])) return true;   // "ISO-9001"
    // The last group of a longer number ("+49 611 1234567").
    if (c == ' ' && start >= 2 && IsAsciiDigit(s[start - 2])) return true;
    if (start >= 3 && s.compare(start - 3, 3, "\xE2\x82\xAC") == 0) return true;   // €
    if (start >= 2 && s.compare(start - 2, 2, "\xC2\xA3") == 0) return true;          // £
    return false;
}

bool TiedAfter(const std::string& s, std::size_t end) {
    if (end >= s.size()) return false;
    const char c = s[end];
    if (IsAsciiAlnum(c)) return true;
    if (c != '\0' && std::strchr("/=@_%$\\", c)) return true;
    // "1.5", "10:30", "1,000": a number goes on.
    if ((c == '.' || c == ',' || c == ':') && end + 1 < s.size() && IsAsciiDigit(s[end + 1])) return true;
    // The first group of a longer number.
    if (c == ' ' && end + 1 < s.size() && IsAsciiDigit(s[end + 1])) return true;
    if (s.compare(end, 3, "\xE2\x82\xAC") == 0) return true;      // €
    if (c == ' ' && s.compare(end + 1, 3, "\xE2\x82\xAC") == 0) return true;   // " €"
    return false;
}

std::vector<Candidate> FindCandidates(const std::string& s) {
    std::vector<Candidate> out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (!IsAsciiAlnum(s[i]) || (i > 0 && IsAsciiAlnum(s[i - 1]))) { ++i; continue; }
        // The first group.
        std::size_t j = i;
        while (j < s.size() && IsAsciiAlnum(s[j])) ++j;
        std::string compact = s.substr(i, j - i);
        std::string lastGroup = compact;
        bool spaced = false;
        int groups = 1;
        // Further groups: digits after a space (123 456), anything after a dash.
        for (;;) {
            bool isSpace = false;
            const std::size_t jl = JoinerAt(s, j, isSpace);
            if (jl == 0 || j + jl >= s.size() || !IsAsciiAlnum(s[j + jl])) break;
            std::size_t k = j + jl;
            while (k < s.size() && IsAsciiAlnum(s[k])) ++k;
            const std::string group = s.substr(j + jl, k - (j + jl));
            if (isSpace) {
                // Only groups of digits, each 1-4 long: "649 082", "6 4 9 0 8 2".
                if (!AllDigits(lastGroup) || !AllDigits(group) || group.size() > 4 || lastGroup.size() > 4) break;
                spaced = true;
                compact += group;
            } else {
                compact += '-';
                compact += group;
            }
            lastGroup = group;
            j = k;
            if (++groups >= 8) break;
        }
        const std::string shown = s.substr(i, j - i);
        if (!TiedBefore(s, i) && !TiedAfter(s, j)) {
            std::string check = compact;
            check.erase(std::remove(check.begin(), check.end(), '-'), check.end());
            if (LooksLikeCode(check, spaced) && !(spaced && check.size() < 6)) {
                out.push_back({i, j, shown, compact});
            }
        }
        i = j;
    }
    return out;
}

// The block with what surrounds a code on its own taken away: spaces,
// quotes, brackets, Markdown's backticks and stars.
std::string Trimmed(const std::string& s) {
    auto isTrim = [](const std::string& t, std::size_t i, std::size_t& len) {
        static const char* const kTrim[] = {
            " ", "\t", "\r", "\n", "`", "*", "_", "\"", "'", "[", "]", "(", ")", ">", "<",
            "\xC2\xA0", "\xC2\xAB", "\xC2\xBB",                       // nbsp « »
            "\xE2\x80\x8B", "\xE2\x80\x8C", "\xE2\x80\x8D", "\xEF\xBB\xBF",   // zero-width, BOM
            "\xE2\x80\x9C", "\xE2\x80\x9D", "\xE2\x80\x9E",           // “ ” „
            "\xE3\x80\x8C", "\xE3\x80\x8D", "\xE3\x80\x90", "\xE3\x80\x91",   // 「 」 【 】
            "\xEF\xBC\xBB", "\xEF\xBC\xBD",                           // ［ ］
        };
        for (const char* t2 : kTrim) {
            const std::size_t l = std::strlen(t2);
            if (t.compare(i, l, t2) == 0) { len = l; return true; }
        }
        return false;
    };
    std::size_t a = 0, b = s.size();
    std::size_t len = 0;
    while (a < b && isTrim(s, a, len)) a += len;
    // From the end: test each possible length of a trailing piece.
    bool trimmed = true;
    while (trimmed && b > a) {
        trimmed = false;
        for (std::size_t l = 1; l <= 3 && l <= b - a; ++l) {
            std::size_t got = 0;
            if (isTrim(s, b - l, got) && got == l) { b -= l; trimmed = true; break; }
        }
    }
    return s.substr(a, b - a);
}

// Code points between two byte offsets, and whether a digit is among them.
std::size_t CodePointsBetween(const std::string& s, std::size_t from, std::size_t to, bool& digit) {
    std::size_t count = 0;
    digit = false;
    for (std::size_t i = from; i < to && i < s.size();) {
        std::size_t len = 1;
        const uint32_t cp = DecodeAt(s, i, len);
        if (cp < 0x80 && IsAsciiDigit(static_cast<char>(cp))) digit = true;
        i += len;
        ++count;
    }
    return count;
}

// How near the word "code" must be: in characters, with no number between.
constexpr std::size_t kNearChars = 40;

bool NearKeyword(const std::string& text, const Candidate& c, const std::vector<KeywordHit>& hits) {
    for (const KeywordHit& h : hits) {
        bool digit = false;
        if (h.end <= c.start) {
            if (CodePointsBetween(text, h.end, c.start, digit) <= kNearChars && !digit) return true;
        } else if (h.start >= c.end) {
            if (CodePointsBetween(text, c.end, h.start, digit) <= kNearChars && !digit) return true;
        }
    }
    return false;
}

struct Found {
    OneTimeCode code;
    int score = 0;
    std::size_t order = 0;
};

} // namespace

bool MentionsOneTimeCode(const std::string& text) {
    return !FindKeywords(text).empty();
}

std::vector<OneTimeCode> FindOneTimeCodes(const std::string& subject,
                                          const std::vector<std::string>& blocks,
                                          std::size_t maxCodes) {
    std::vector<Found> found;
    std::size_t order = 0;

    const bool subjectSays = MentionsOneTimeCode(subject);
    bool bodySays = false;
    std::vector<std::vector<KeywordHit>> blockHits(blocks.size());
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        blockHits[b] = FindKeywords(blocks[b]);
        if (!blockHits[b].empty()) bodySays = true;
    }

    // Near the word, in the subject ("482913 is your Instagram code").
    {
        const std::vector<KeywordHit> hits = FindKeywords(subject);
        for (const Candidate& c : FindCandidates(subject)) {
            if (!NearKeyword(subject, c, hits)) continue;
            found.push_back({OneTimeCode{c.code, c.shown, -1, false}, 2, order++});
        }
    }
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        const std::string& text = blocks[b];
        const std::vector<Candidate> candidates = FindCandidates(text);
        if (candidates.empty()) continue;
        // A block that is the code and nothing else, in a message about a code.
        const std::string alone = Trimmed(text);
        for (const Candidate& c : candidates) {
            int score = 0;
            bool standalone = false;
            if (alone == c.shown && (subjectSays || bodySays)) {
                standalone = true;
                // The word in the subject or the block just before: surer still.
                bool previousSays = false;
                for (std::size_t p = b; p-- > 0;) {
                    if (Trimmed(blocks[p]).empty()) continue;
                    previousSays = !blockHits[p].empty();
                    break;
                }
                score = (subjectSays || previousSays) ? 4 : 3;
            } else if (NearKeyword(text, c, blockHits[b])) {
                score = 2;
            }
            if (score == 0) continue;
            found.push_back({OneTimeCode{c.code, c.shown, static_cast<int>(b), standalone}, score, order++});
        }
    }

    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.order < b.order;
    });
    std::vector<OneTimeCode> out;
    for (const Found& f : found) {
        if (out.size() >= maxCodes) break;
        const bool seen = std::any_of(out.begin(), out.end(),
                                      [&](const OneTimeCode& o) { return o.code == f.code.code; });
        if (!seen) out.push_back(f.code);
    }
    return out;
}

} // namespace UltraMail
