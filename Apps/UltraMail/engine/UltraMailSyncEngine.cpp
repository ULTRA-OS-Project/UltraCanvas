// Apps/UltraMail/engine/UltraMailSyncEngine.cpp
// Version: 0.9.0 - a body directory every file system takes as one name of its
//                  own: a trailing '.' or space ~t / ~n, a Windows device name
//                  (CON, NUL, COM1, ...) marked ~z, and the characters that
//                  make two spellings of one name for macOS (combining marks,
//                  Hangul jamo, Greek Extended, the Ohm, Kelvin and Angstrom
//                  signs, ...) ~x and their code; MigrateBodyCache moves all
//                  three earlier layouts (.layout-4)
// Version: 0.8.0 - a body directory names its folder unambiguously: '/' is
//                  '_' as before, and a literal '_', '\' and ':' - which
//                  were '_' too ("a/b" and "a_b" shared one directory) - and
//                  the characters Windows refuses in a name (* ? " < > |) are
//                  '~' escapes (CacheDirectoryOf); MigrateBodyCache moves both
//                  earlier layouts (.layout-3)
// Version: 0.7.0 - collision-free body directories: a folder whose name is not
//                  in title case ("work", "WORK") is cached under its name and
//                  a ~<case mask> suffix (CacheDirectoryOf), so case twins no
//                  longer share - and overwrite - one directory on Windows and
//                  macOS; MigrateBodyCache moves an earlier version's
//                  directories once
// Version: 0.6.1 - SyncFolders stores the server's inbox as INBOX whatever it is
//                  listed as ("Inbox", a JMAP mailbox of role inbox): the mail is
//                  synced under INBOX, and the account bar counts the folder
//                  with the inbox role - which was a second, empty folder
// Version: 0.6.0 - message files read and written through UltraMailStorage (sealed at rest)
// Version: 0.5.1 - the envelope date parses an ISO 8601 UTCDate (JMAP's
//                  receivedAt) as well as an RFC 2822 Date header (IMAP)
// Version: 0.5.0 - CreateFolder / DeleteFolder
// Version: 0.4.0 - the verified sender domain is stored with the verdict;
//                  RescanStaleVerdicts after each sync of a folder
// Version: 0.3.0 - SyncFolders keeps the server's separator and drops the folders
//                  the server no longer lists
// Version: 0.2.0 - RefreshFolder / FetchMissing; the UIDNEXT check on the cache
// Version: 0.1.1 - envelope subject/from/to are RFC 2047 decoded when stored
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSyncEngine.h"
#include "UltraMailStorage.h"
#include "UltraMailMimeCodec.h"

#include "UltraMailThreatScan.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <vector>
#include <UltraCanvasUtils.h>
#include <UltraNet/UltraNetMime.h>
#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;
namespace fs = std::filesystem;

namespace UltraMail {

namespace {

// "Anna Schmidt <anna@x.com>" -> name "Anna Schmidt", addr "anna@x.com".
void ParseFromField(const std::string& from, std::string& name, std::string& addr) {
    // Strip surrounding whitespace and double-quotes from the display name and
    // address (e.g. "Schmidt, Anna" <a@x> -> name: Schmidt, Anna).
    const std::string kStrip = " \t\r\n\"";
    std::size_t lt = from.find('<');
    if (lt != std::string::npos) {
        std::size_t gt = from.find('>', lt);
        addr = from.substr(lt + 1, gt == std::string::npos ? std::string::npos : gt - lt - 1);
        name = UltraCanvas::Trim(from.substr(0, lt), kStrip);
    } else {
        addr = UltraCanvas::Trim(from, kStrip);
        name.clear();
    }
    addr = UltraCanvas::Trim(addr, kStrip);
}

uint32_t MapNetFlagsToLocal(UltraNetMailFlags nf) {
    uint32_t f = Flag_None;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Seen))     f |= Flag_Seen;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Answered)) f |= Flag_Answered;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Flagged))  f |= Flag_Flagged;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Deleted))  f |= Flag_Deleted;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Draft))    f |= Flag_Draft;
    return f;
}

UltraNetMailFlags MapLocalFlagToNet(uint32_t lf) {
    UltraNetMailFlags nf = UltraNetMailFlags::NoFlags;
    if (lf & Flag_Seen)     nf |= UltraNetMailFlags::Seen;
    if (lf & Flag_Answered) nf |= UltraNetMailFlags::Answered;
    if (lf & Flag_Flagged)  nf |= UltraNetMailFlags::Flagged;
    if (lf & Flag_Deleted)  nf |= UltraNetMailFlags::Deleted;
    if (lf & Flag_Draft)    nf |= UltraNetMailFlags::Draft;
    return nf;
}

int MonthNum(const char* m) {
    static const char* names[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
    for (int i = 0; i < 12; ++i) if (std::strncmp(m, names[i], 3) == 0) return i + 1;
    return 0;
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
int64_t DaysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Parse an RFC 2822 Date header to epoch seconds (0 on failure). Portable —
// avoids strptime/timegm.
int64_t ParseRfc2822Date(const std::string& s) {
    if (s.empty()) return 0;
    int day = 0, year = 0, h = 0, mi = 0, se = 0;
    char mon[8] = {0}, tz[8] = {0};

    // With leading weekday ("Tue, 14 Jan 2026 14:02:00 +0100").
    int n = std::sscanf(s.c_str(), "%*[^,], %d %3s %d %d:%d:%d %7s",
                        &day, mon, &year, &h, &mi, &se, tz);
    if (n < 6)
        n = std::sscanf(s.c_str(), "%d %3s %d %d:%d:%d %7s",
                        &day, mon, &year, &h, &mi, &se, tz);
    if (n < 6) return 0;

    int month = MonthNum(mon);
    if (month == 0 || year < 1970) return 0;

    int64_t epoch = DaysFromCivil(year, static_cast<unsigned>(month),
                                  static_cast<unsigned>(day)) * 86400
                    + h * 3600 + mi * 60 + se;

    // Apply the timezone offset to reach UTC.
    if (tz[0] == '+' || tz[0] == '-') {
        int sign = tz[0] == '+' ? 1 : -1;
        int hh = (tz[1] - '0') * 10 + (tz[2] - '0');
        int mm = (tz[3] - '0') * 10 + (tz[4] - '0');
        epoch -= sign * (hh * 3600 + mm * 60);
    }
    return epoch;
}

// Parse an ISO 8601 / RFC 3339 timestamp to epoch seconds (0 on failure). JMAP
// delivers a message's time as a UTCDate ("2026-07-17T09:00:00Z"), not an RFC
// 2822 Date header, so the envelope date field holds this shape for a JMAP
// account. Accepts a 'T' or ' ' between date and time, an optional fractional
// second, and a 'Z' / ±hh:mm zone (absent is read as UTC, as JMAP always is).
int64_t ParseIso8601Date(const std::string& s) {
    int Y = 0, Mo = 0, D = 0, h = 0, mi = 0, se = 0;
    int n = std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &se);
    if (n < 6)
        n = std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &Y, &Mo, &D, &h, &mi, &se);
    if (n < 6 || Mo < 1 || Mo > 12 || Y < 1970) return 0;

    int64_t epoch = DaysFromCivil(Y, static_cast<unsigned>(Mo),
                                  static_cast<unsigned>(D)) * 86400
                    + h * 3600 + mi * 60 + se;

    // The zone marker sits after the seconds (past any ".fff"); search skips the
    // date's own two '-'. 'Z'/none = UTC; "+hh:mm" / "-hh:mm" is subtracted.
    std::size_t z = s.find_first_of("Zz+-", 10);
    if (z != std::string::npos && (s[z] == '+' || s[z] == '-')) {
        int sign = s[z] == '+' ? 1 : -1;
        int hh = 0, mm = 0;
        if (std::sscanf(s.c_str() + z + 1, "%d:%d", &hh, &mm) >= 1)
            epoch -= sign * (hh * 3600 + mm * 60);
    }
    return epoch;
}

// The envelope date field holds an RFC 2822 Date header from an IMAP account and
// an ISO 8601 UTCDate from a JMAP one; parse whichever it is. A leading
// "YYYY-MM-DD" (the two dashes at [4] and [7]) marks the ISO form.
int64_t ParseEnvelopeDate(const std::string& s) {
    if (s.size() >= 10 && s[4] == '-' && s[7] == '-')
        return ParseIso8601Date(s);
    return ParseRfc2822Date(s);
}

std::string SanitizeFolder(const std::string& folder) {
    std::string out;
    for (char c : folder)
        out.push_back((c == '/' || c == '\\' || c == ':') ? '_' : c);
    if (out.empty()) out = "INBOX";
    return out;
}

bool SameNameNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// ---- Letter case beyond ASCII ----------------------------------------------
// The simple one-to-one case pairs of the scripts a folder name is likely to
// be in - Latin (Latin-1, Extended-A, -B and Additional), Greek, Cyrillic,
// Armenian, Georgian and fullwidth Latin - the pairs a file system that ignores
// case (NTFS's upcase table, APFS) folds alike. Letters with more than two
// forms (dotless i, long s, final sigma, the sharp s) are left out: they are
// not a pair.
struct CaseRange {
    char32_t first, last;   // the upper-case letters of the range
    int32_t  delta;         // lower = upper + delta; 0: alternating pairs
    bool     oddUpper;      // alternating pairs: the upper case is the odd one
};
constexpr CaseRange kCaseRanges[] = {
    { 0x0041, 0x005A, 32, false },  { 0x00C0, 0x00D6, 32, false },
    { 0x00D8, 0x00DE, 32, false },  { 0x0100, 0x012F, 0, false },
    { 0x0132, 0x0137, 0, false },   { 0x0139, 0x0148, 0, true },
    { 0x014A, 0x0177, 0, false },   { 0x0179, 0x017E, 0, true },
    { 0x0182, 0x0185, 0, false },   { 0x01A0, 0x01A5, 0, false },
    { 0x01CD, 0x01DC, 0, true },    { 0x01DE, 0x01EF, 0, false },
    { 0x01F8, 0x021F, 0, false },   { 0x0222, 0x0233, 0, false },
    { 0x0246, 0x024F, 0, false },
    { 0x0386, 0x0386, 38, false },  { 0x0388, 0x038A, 37, false },
    { 0x038C, 0x038C, 64, false },  { 0x038E, 0x038F, 63, false },
    { 0x0391, 0x03A1, 32, false },  { 0x03A3, 0x03AB, 32, false },
    { 0x03D8, 0x03EF, 0, false },
    { 0x0400, 0x040F, 80, false },  { 0x0410, 0x042F, 32, false },
    { 0x0460, 0x0481, 0, false },   { 0x048A, 0x04BF, 0, false },
    { 0x04C0, 0x04C0, 15, false },  { 0x04C1, 0x04CE, 0, true },
    { 0x04D0, 0x052F, 0, false },
    { 0x0531, 0x0556, 48, false },
    { 0x10A0, 0x10C5, 7264, false },
    { 0x1E00, 0x1E95, 0, false },   { 0x1EA0, 0x1EFF, 0, false },
    { 0xFF21, 0xFF3A, 32, false },
};

// The upper and the lower case of `cp`; both `cp` itself for a character
// without case (or one left out above).
void CaseForms(char32_t cp, char32_t& upper, char32_t& lower) {
    upper = lower = cp;
    if (cp == 0x00FF || cp == 0x0178) { upper = 0x0178; lower = 0x00FF; return; }   // ÿ Ÿ
    for (const CaseRange& r : kCaseRanges) {
        if (r.delta != 0) {
            if (cp >= r.first && cp <= r.last) { upper = cp; lower = cp + static_cast<char32_t>(r.delta); return; }
            const char32_t lo = r.first + static_cast<char32_t>(r.delta), hi = r.last + static_cast<char32_t>(r.delta);
            if (cp >= lo && cp <= hi) { upper = cp - static_cast<char32_t>(r.delta); lower = cp; return; }
            continue;
        }
        if (cp < r.first || cp > r.last) continue;
        const bool odd = (cp & 1u) != 0;
        if (odd == r.oddUpper) { upper = cp; lower = cp + 1; } else { upper = cp - 1; lower = cp; }
        if (upper < r.first || lower > r.last) { upper = lower = cp; }   // the range's loose end
        return;
    }
}

// The code points of `s` with where each starts; a byte that is not valid
// UTF-8 stands for itself (and has no case).
struct CodePoint { char32_t cp; std::size_t at; };
std::vector<CodePoint> DecodeUtf8(const std::string& s) {
    std::vector<CodePoint> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        int extra = b >= 0xF0 && b < 0xF8 ? 3 : b >= 0xE0 ? 2 : b >= 0xC2 && b < 0xE0 ? 1 : 0;
        if (b >= 0xF8) extra = 0;
        char32_t cp = extra == 3 ? (b & 0x07u) : extra == 2 ? (b & 0x0Fu) : extra == 1 ? (b & 0x1Fu) : b;
        bool ok = b < 0x80 || extra > 0;
        for (int k = 1; ok && k <= extra; ++k) {
            const unsigned char c = i + static_cast<std::size_t>(k) < s.size()
                ? static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)])
                : static_cast<unsigned char>(0);
            if ((c & 0xC0u) != 0x80u) { ok = false; break; }
            cp = (cp << 6) | (c & 0x3Fu);
        }
        if (!ok) { out.push_back({ static_cast<char32_t>(0xDC00u + b), i }); ++i; continue; }   // a lone byte
        out.push_back({ cp, i });
        i += 1 + static_cast<std::size_t>(extra);
    }
    return out;
}

// Which letters of `name` differ from its title case - each word's first
// letter upper case and the rest lower ("Sent Items", "[Gmail]/All Mail",
// "Über", "Входящие"), a leading "inbox" level as INBOX - one bit each in
// letter order, four to a hex digit, lowest first; "" when none does. A word
// starts after anything that is neither a letter nor a digit. Of two names
// that differ only in case, at most one is in title case, and the two masks
// differ.
std::string CaseMask(const std::string& name) {
    const std::vector<CodePoint> cps = DecodeUtf8(name);
    // The leading "inbox" level reads as INBOX.
    std::size_t inboxEnd = 0;
    if (name.size() >= 5 && SameNameNoCase(name.substr(0, 5), "INBOX") &&
        (name.size() == 5 || !(std::isalnum(static_cast<unsigned char>(name[5])) ||
                               static_cast<unsigned char>(name[5]) >= 0x80)))
        inboxEnd = 5;
    std::string mask;
    int nibble = 0, bit = 0;
    bool wordStart = true;
    for (const CodePoint& c : cps) {
        char32_t upper, lower;
        CaseForms(c.cp, upper, lower);
        if (upper == lower) {   // no case: a digit or another letter keeps the word going
            wordStart = c.cp < 0x80 ? !std::isdigit(static_cast<int>(c.cp)) : false;
            if (c.cp >= 0xDC00 && c.cp <= 0xDCFF) wordStart = false;
            continue;
        }
        const char32_t title = (c.at < inboxEnd || wordStart) ? upper : lower;
        wordStart = false;
        if (c.cp != title) nibble |= 1 << bit;
        if (++bit == 4) {
            mask.push_back("0123456789abcdef"[nibble]);
            nibble = 0; bit = 0;
        }
    }
    if (bit > 0) mask.push_back("0123456789abcdef"[nibble]);
    while (!mask.empty() && mask.back() == '0') mask.pop_back();
    return mask;
}

// The mask the second layout used: ASCII letters only, a byte outside ASCII
// neither a letter nor a word break.
bool IsAsciiLetter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
std::string AsciiCaseMask(const std::string& name) {
    std::string title = name;
    bool wordStart = true;
    for (char& ch : title) {
        const auto c = static_cast<unsigned char>(ch);
        if (IsAsciiLetter(c)) {
            ch = static_cast<char>(wordStart ? std::toupper(c) : std::tolower(c));
            wordStart = false;
        } else {
            wordStart = !(std::isdigit(c) || c >= 0x80);
        }
    }
    if (title.size() >= 5 && SameNameNoCase(title.substr(0, 5), "INBOX") &&
        (title.size() == 5 || !(std::isalnum(static_cast<unsigned char>(title[5])) ||
                                static_cast<unsigned char>(title[5]) >= 0x80)))
        for (std::size_t i = 0; i < 5; ++i)
            title[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[i])));
    std::string mask;
    int nibble = 0, bit = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (!IsAsciiLetter(static_cast<unsigned char>(name[i]))) continue;
        if (name[i] != title[i]) nibble |= 1 << bit;
        if (++bit == 4) {
            mask.push_back("0123456789abcdef"[nibble]);
            nibble = 0; bit = 0;
        }
    }
    if (bit > 0) mask.push_back("0123456789abcdef"[nibble]);
    while (!mask.empty() && mask.back() == '0') mask.pop_back();
    return mask;
}

// A character a directory name cannot carry as itself, and the letter it is
// written as after a '~'. None of the letters is a hex digit, so an escape
// never reads as the case mask.
struct Escape { char c; char code; };
constexpr Escape kEscapes[] = {
    { '~', '~' }, { '_', 'u' }, { '\\', 'r' }, { ':', 'k' }, { '*', 's' },
    { '?', 'q' }, { '"', 'w' }, { '<', 'l' }, { '>', 'g' }, { '|', 'p' },
};
// Escapes with no character of their own: ~z (nothing - it breaks up a
// Windows device name), ~t and ~n (a trailing '.' and space), ~x and six hex
// digits (a code point; see NeedsCodeEscape).
constexpr char kNoCharacter = 'z';
constexpr char kTrailingDot = 't';
constexpr char kTrailingSpace = 'n';
constexpr char kCodePoint = 'x';

// A code point written as ~x and its code, so that a name is never two names
// to macOS: APFS and HFS+ take a name in any Unicode normalization form as
// the same name ("ü" and "u" + U+0308), so a directory name holds no
// character that composes with another or stands for one: combining marks,
// the voicing marks of kana, Hangul jamo, the nukta of Indic scripts, and the
// blocks and signs that are another character's canonical equivalent - Greek
// Extended, the Ohm, Kelvin and Angstrom signs, CJK compatibility ideographs.
bool NeedsCodeEscape(char32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || cp == 0x0374 || cp == 0x037E || cp == 0x0387 ||
           (cp >= 0x0483 && cp <= 0x0489) || (cp >= 0x0591 && cp <= 0x05C7) ||
           (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
           cp == 0x093C || cp == 0x09BC || cp == 0x0A3C || cp == 0x0ABC || cp == 0x0B3C ||
           cp == 0x09BE || cp == 0x09D7 || cp == 0x0B3E || cp == 0x0B56 || cp == 0x0B57 ||
           cp == 0x0BBE || cp == 0x0BD7 || cp == 0x0CC2 || cp == 0x0CD5 || cp == 0x0CD6 ||
           cp == 0x0D3E || cp == 0x0D57 || cp == 0x0DCA || cp == 0x0DCF || cp == 0x0DDF ||
           cp == 0x102E || cp == 0x1B35 ||
           (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x1F00 && cp <= 0x1FFF) ||
           cp == 0x2000 || cp == 0x2001 || (cp >= 0x20D0 && cp <= 0x20FF) ||
           cp == 0x2126 || cp == 0x212A || cp == 0x212B || cp == 0x2329 || cp == 0x232A ||
           cp == 0x3099 || cp == 0x309A || (cp >= 0xA960 && cp <= 0xA97F) ||
           (cp >= 0xD7B0 && cp <= 0xD7FF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F) || (cp >= 0x2F800 && cp <= 0x2FA1F);
}

void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) { out.push_back(static_cast<char>(cp)); return; }
    if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    }
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
}

// Whether Windows takes `dir` for a device: CON, PRN, AUX, NUL, COM0-9,
// LPT0-9 (and COM/LPT with a superscript 1-3), CONIN$ and CONOUT$, in any
// case, alone or before a '.' - "NUL.txt" is the device as well - and with
// spaces before that '.'.
bool IsWindowsDeviceName(const std::string& dir) {
    std::string base = dir.substr(0, dir.find('.'));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    std::string lower;
    for (char c : base) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (const char* d : { "con", "prn", "aux", "nul", "conin$", "conout$" })
        if (lower == d) return true;
    for (const char* p : { "com", "lpt" }) {
        if (lower.rfind(p, 0) != 0) continue;
        const std::string rest = lower.substr(3);
        if (rest.size() == 1 && rest[0] >= '0' && rest[0] <= '9') return true;
        if (rest == "\xc2\xb9" || rest == "\xc2\xb2" || rest == "\xc2\xb3") return true;   // ¹ ² ³
    }
    return false;
}

// The directories earlier versions cached the folder's bodies in: the name
// with '/', '\' and ':' as '_' (layout 1), and that with the case mask, '~'
// doubled (layout 2).
std::string Layout1CacheDirectory(const std::string& folder) { return SanitizeFolder(folder); }
std::string Layout2CacheDirectory(const std::string& folder) {
    const std::string name = SanitizeFolder(folder);
    std::string out;
    for (char c : name) {
        out.push_back(c);
        if (c == '~') out.push_back('~');
    }
    const std::string mask = AsciiCaseMask(name);
    if (!mask.empty()) out += "~" + mask;
    return out;
}

// The third layout: the escapes of kEscapes and the case mask, none of the
// rest of CacheDirectoryOf's.
std::string Layout3CacheDirectory(const std::string& folder) {
    const std::string name = folder.empty() ? std::string("INBOX") : folder;
    std::string out;
    for (char c : name) {
        if (c == '/') { out.push_back('_'); continue; }
        bool escaped = false;
        for (const Escape& e : kEscapes) {
            if (c != e.c) continue;
            out.push_back('~');
            out.push_back(e.code);
            escaped = true;
            break;
        }
        if (!escaped) out.push_back(c);
    }
    const std::string mask = CaseMask(name);
    if (!mask.empty()) out += "~" + mask;
    return out;
}

} // namespace

std::string CacheDirectoryOf(const std::string& folder) {
    const std::string name = folder.empty() ? std::string("INBOX") : folder;
    const std::vector<CodePoint> cps = DecodeUtf8(name);
    std::string out;
    for (std::size_t k = 0; k < cps.size(); ++k) {
        const char32_t cp = cps[k].cp;
        const bool last = k + 1 == cps.size();
        if (cp == '/') { out.push_back('_'); continue; }
        if (last && (cp == '.' || cp == ' ')) {   // Windows drops a trailing dot or space
            out.push_back('~');
            out.push_back(cp == '.' ? kTrailingDot : kTrailingSpace);
            continue;
        }
        if (cp < 0x80) {
            bool escaped = false;
            for (const Escape& e : kEscapes) {
                if (static_cast<char>(cp) != e.c) continue;
                out.push_back('~');
                out.push_back(e.code);
                escaped = true;
                break;
            }
            if (!escaped) out.push_back(static_cast<char>(cp));
            continue;
        }
        if (NeedsCodeEscape(cp)) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "%06x", static_cast<unsigned>(cp));
            out.push_back('~');
            out.push_back(kCodePoint);
            out += hex;
            continue;
        }
        // Everything else as it was written (a byte that is not UTF-8 too).
        const std::size_t end = last ? name.size() : cps[k + 1].at;
        out.append(name, cps[k].at, end - cps[k].at);
    }
    const std::string mask = CaseMask(name);
    if (!mask.empty()) out += "~" + mask;
    // A Windows device name ("CON", "nul.txt") is broken up after its first
    // letter by ~z, which reads as nothing.
    if (IsWindowsDeviceName(out)) {
        out.insert(1, 1, kNoCharacter);
        out.insert(1, 1, '~');
    }
    return out;
}

std::string FolderOfCacheDirectory(const std::string& directory) {
    std::string out;
    for (std::size_t i = 0; i < directory.size(); ++i) {
        const char c = directory[i];
        if (c == '_') { out.push_back('/'); continue; }
        if (c != '~') { out.push_back(c); continue; }
        const char code = i + 1 < directory.size() ? directory[i + 1] : '\0';
        if (code == kNoCharacter) { ++i; continue; }
        if (code == kTrailingDot) { out.push_back('.'); ++i; continue; }
        if (code == kTrailingSpace) { out.push_back(' '); ++i; continue; }
        if (code == kCodePoint && i + 8 <= directory.size()) {
            const std::string hex = directory.substr(i + 2, 6);
            if (hex.find_first_not_of("0123456789abcdef") == std::string::npos) {
                AppendUtf8(out, static_cast<char32_t>(std::stoul(hex, nullptr, 16)));
                i += 7;
                continue;
            }
        }
        const Escape* found = nullptr;
        for (const Escape& e : kEscapes) if (e.code == code) found = &e;
        if (!found) break;   // the lone '~' before the case mask
        out.push_back(found->c);
        ++i;
    }
    return out;
}

namespace {

// `uid` of a cached body's file name ("123.eml"), or -1.
int64_t UidOfBodyFile(const fs::path& file) {
    if (file.extension() != ".eml") return -1;
    const std::string stem = PathToUtf8(file.stem());
    if (stem.empty() || stem.size() > 18) return -1;
    for (char c : stem) if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
    return std::stoll(stem);
}

// Moves `from` to `to`; a body already at `to` wins and `from` goes.
bool MoveBodyFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::exists(to, ec)) return fs::remove(from, ec) || !fs::exists(from, ec);
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    return !ec;
}

} // namespace

int MigrateBodyCache(const std::string& emlDir, const LocalStore& store) {
    const fs::path root = PathFromUtf8(emlDir);
    const fs::path marker = root / ".layout-4";
    std::error_code ec;
    if (fs::exists(marker, ec)) return 0;
    int moved = 0;
    bool complete = true;
    std::vector<Account> accounts;
    store.ListAccounts(accounts);
    for (const auto& account : accounts) {
        const fs::path accountDir = root / PathFromUtf8(account.accountId);
        if (!fs::is_directory(accountDir, ec)) continue;
        std::vector<Folder> folders;
        store.ListFolders(account.accountId, folders);
        // Every directory a folder's bodies may be in: the present one first.
        auto directoriesOf = [](const std::string& name) {
            std::vector<std::string> dirs = { CacheDirectoryOf(name) };
            for (const std::string& d : { Layout3CacheDirectory(name), Layout2CacheDirectory(name),
                                          Layout1CacheDirectory(name) })
                if (std::find(dirs.begin(), dirs.end(), d) == dirs.end()) dirs.push_back(d);
            return dirs;
        };
        for (const auto& folder : folders) {
            const std::vector<std::string> dirs = directoriesOf(folder.name);
            const fs::path target = accountDir / PathFromUtf8(dirs.front());
            for (std::size_t li = 1; li < dirs.size(); ++li) {
                const fs::path legacy = accountDir / PathFromUtf8(dirs[li]);
                if (!fs::is_directory(legacy, ec)) continue;
                std::error_code same;
                if (fs::exists(target, same) && fs::equivalent(legacy, target, same)) continue;

                // The folders this directory is also one of: a name that
                // differs only in case on a file system that ignores it
                // (Windows, macOS), or "a/b" and "a_b", which the first
                // layout wrote alike.
                std::set<int64_t> twinUids;
                bool shared = false;
                for (const auto& other : folders) {
                    if (other.name == folder.name) continue;
                    bool theirs = false;
                    for (const std::string& d : directoriesOf(other.name)) {
                        const fs::path p = accountDir / PathFromUtf8(d);
                        std::error_code eq;
                        if (fs::exists(p, eq) && fs::equivalent(legacy, p, eq) && !eq) theirs = true;
                    }
                    if (!theirs) continue;
                    shared = true;
                    std::vector<int64_t> uids;
                    store.ListUids(account.accountId, other.name, uids);
                    twinUids.insert(uids.begin(), uids.end());
                }

                if (!shared && !fs::exists(target, ec)) {
                    fs::rename(legacy, target, ec);
                    if (ec) complete = false; else ++moved;
                    continue;
                }
                // File by file: into a directory that is there already, or out
                // of one another folder shares - then only this folder's own
                // bodies, and one both numbered alike is nobody's for sure: it
                // goes, and each folder downloads its own again when it is read.
                std::set<int64_t> own;
                if (shared) {
                    std::vector<int64_t> uids;
                    store.ListUids(account.accountId, folder.name, uids);
                    own.insert(uids.begin(), uids.end());
                }
                std::vector<fs::path> files;
                for (const auto& entry : fs::directory_iterator(legacy, ec))
                    if (entry.is_regular_file(ec)) files.push_back(entry.path());
                bool touched = false;
                for (const auto& file : files) {
                    const int64_t uid = UidOfBodyFile(file);
                    if (shared) {
                        if (uid < 0 || !own.count(uid)) continue;   // the other's
                        if (twinUids.count(uid)) { fs::remove(file, ec); touched = true; continue; }
                    }
                    if (MoveBodyFile(file, target / file.filename())) touched = true;
                    else complete = false;
                }
                fs::remove(legacy, ec);   // only once it is empty
                if (touched) ++moved;
            }
        }
    }
    if (complete) {
        fs::create_directories(root, ec);
        std::ofstream(marker) << "4\n";
    }
    return moved;
}

std::string CachedBodyPath(const std::string& emlDir, const std::string& accountId,
                           const std::string& folder, int64_t uid) {
    // Every part through PathFromUtf8: joining a std::string straight onto a
    // path converts it in the ANSI code page on Windows, so a folder whose
    // name is outside that code page was cached under a mangled name.
    fs::path p = PathFromUtf8(emlDir) / PathFromUtf8(accountId)
               / PathFromUtf8(CacheDirectoryOf(folder)) / (std::to_string(uid) + ".eml");
    return PathToUtf8(p);
}

std::string SyncEngine::BodyPath(const std::string& accountId,
                                 const std::string& folder, int64_t uid) const {
    return CachedBodyPath(emlDir_, accountId, folder, uid);
}

void SyncEngine::RemoveFolderBodies(const std::string& accountId, const std::string& folder) {
    std::error_code ec;
    fs::remove_all(PathFromUtf8(BodyPath(accountId, folder, 0)).parent_path(), ec);
}

SyncOutcome SyncEngine::SyncFolders(const std::string& accountId,
                                    const std::string& serverUrl,
                                    const UltraNetMailOptions& options) {
    std::vector<UltraNetMailFolder> folders;
    UltraNetResult r = mailbox_.ListFolders(serverUrl, folders, options);
    if (!r) return SyncOutcome::Fail(r);

    SyncOutcome out;
    std::vector<Folder> stored;
    store_.ListFolders(accountId, stored);
    std::set<std::string> listed;
    for (const auto& f : folders) {
        Folder lf;
        lf.accountId = accountId;
        lf.name = f.name;
        lf.role = FolderRoleFromString(f.role);
        // The inbox is INBOX here, as the inbox sync stores its mail, however
        // the server names it: an IMAP server may list "Inbox" (the name is
        // case-insensitive), a JMAP server lists its role=inbox mailbox by
        // its display name. Kept under that name it became a second folder,
        // with the inbox role but none of the mail.
        if (lf.role == FolderRole::Inbox) lf.name = "INBOX";
        lf.selectable = f.selectable;   // \Noselect containers (e.g. "[Gmail]")
        lf.delimiter = f.delimiter;
        if (store_.UpsertFolder(lf)) out.stats.folders++;
        listed.insert(lf.name);
    }

    // A folder the server no longer lists was deleted or renamed there: it
    // leaves the tree, with its messages and their cached bodies. Only on a
    // list that names something - an empty one is never taken for "every
    // folder is gone" - and never the inbox, which every mailbox has.
    if (!listed.empty()) {
        for (const auto& s : stored) {
            if (listed.count(s.name) || s.name == "INBOX") continue;
            if (!store_.RemoveFolder(accountId, s.name)) continue;
            RemoveFolderBodies(accountId, s.name);
            out.stats.foldersRemoved++;
        }
    }
    return out;
}

SyncOutcome SyncEngine::CreateFolder(const std::string& accountId, const std::string& folder,
                                     const std::string& serverUrl,
                                     const UltraNetMailOptions& options) {
    if (folder.empty()) return SyncOutcome::Fail("The folder has no name.");
    UltraNetResult r = mailbox_.CreateFolder(serverUrl, folder, options);
    if (!r) return SyncOutcome::Fail(r);
    // The list names it now, with the attributes and separator the server
    // gave it; a list that cannot be read leaves it for the next sync.
    SyncOutcome listed = SyncFolders(accountId, serverUrl, options);
    return listed.ok ? listed : SyncOutcome{};
}

SyncOutcome SyncEngine::DeleteFolder(const std::string& accountId, const std::string& folder,
                                     const std::string& serverUrl,
                                     const UltraNetMailOptions& options) {
    if (folder.empty() || folder == "INBOX")
        return SyncOutcome::Fail("The inbox cannot be deleted.");
    UltraNetResult r = mailbox_.DeleteFolder(serverUrl, folder, options);
    if (!r) return SyncOutcome::Fail(r);
    SyncOutcome out;
    if (store_.RemoveFolder(accountId, folder)) {
        RemoveFolderBodies(accountId, folder);
        out.stats.foldersRemoved = 1;
    }
    SyncOutcome listed = SyncFolders(accountId, serverUrl, options);
    if (listed.ok) {
        listed.stats.foldersRemoved += out.stats.foldersRemoved;
        return listed;
    }
    return out;
}

bool SyncEngine::FolderStillListed(const std::string& accountId, const std::string& folder,
                                   const std::string& serverUrl,
                                   const UltraNetMailOptions& options) {
    if (folder == "INBOX") return true;
    SyncOutcome listed = SyncFolders(accountId, serverUrl, options);
    if (!listed.ok) return true;
    std::vector<Folder> folders;
    if (!store_.ListFolders(accountId, folders)) return true;
    for (const auto& f : folders) if (f.name == folder) return true;
    return false;
}

MessageEnvelope SyncEngine::ToStored(const std::string& accountId, const std::string& folder,
                                    const UltraNetMailEnvelope& e) const {
    MessageEnvelope m;
    m.accountId = accountId;
    m.folder    = folder;
    m.uid       = static_cast<int64_t>(e.uid);
    m.messageId = e.messageId;
    m.inReplyTo = e.inReplyTo;
    // Subject / display names arrive as RFC 2047 encoded-words on the
    // envelope path (unlike a full message parse). Decode them once here
    // so the store — list, preview and collected contacts — holds
    // readable text.
    m.subject   = UltraNet_MimeDecodeHeader(e.subject);
    ParseFromField(e.from, m.fromName, m.fromAddr);
    m.fromName = UltraNet_MimeDecodeHeader(m.fromName);
    m.to    = e.to;
    for (auto& addr : m.to) addr = UltraNet_MimeDecodeHeader(addr);
    m.date  = ParseEnvelopeDate(e.date);
    m.flags = MapNetFlagsToLocal(e.flags);
    // Automated/bulk detection needs List-*/Precedence headers, which the
    // envelope fetch does not carry yet; left false for now.
    m.automated = false;
    return m;
}

SyncOutcome SyncEngine::SyncMessages(const std::string& accountId,
                                     const std::string& folder,
                                     const std::string& serverUrl,
                                     const UltraNetMailOptions& options,
                                     bool fetchBodies,
                                     const std::function<void(const MessageEnvelope&)>& onMessageStored) {
    SyncOutcome out;
    int64_t sinceUid = 0;
    store_.GetMaxUid(accountId, folder, sinceUid);

    // Is the cache from this numbering of the mailbox? Two signs it is not,
    // and either drops the folder's cache so the fetch below starts again
    // from UID 0 - an incremental fetch ("UID > the highest held") would
    // otherwise skip every new message numbered below a stale highest UID:
    //  - UIDVALIDITY changed: the server renumbered the mailbox.
    //  - the cache holds a UID at or above UIDNEXT, which the mailbox has not
    //    handed out yet. A renumbering whose UIDVALIDITY change was missed
    //    (unknown before, or read wrongly), or mail from another server after
    //    the account's server changed.
    // Best-effort: a backend that cannot report STATUS keeps the incremental
    // behaviour.
    UltraNetMailboxStatus status;
    if (mailbox_.GetMailboxStatus(serverUrl, folder, status, options)) {
        out.stats.serverMessages = static_cast<int>(status.messages);
        int64_t stored = 0;
        store_.GetFolderUidValidity(accountId, folder, stored);
        const bool renumbered = status.uidValidity != 0 && stored != 0 &&
                                stored != static_cast<int64_t>(status.uidValidity);
        const bool aheadOfServer = status.uidNext != 0 &&
                                   sinceUid >= static_cast<int64_t>(status.uidNext);
        if (renumbered || aheadOfServer) {
            store_.ClearFolderMessages(accountId, folder);   // drop stale cached UIDs
            // ... and their bodies: every cached UID of the folder is stale, and
            // the new numbering would overwrite some files and orphan the rest.
            RemoveFolderBodies(accountId, folder);
            sinceUid = 0;
            out.stats.cacheReset = true;
        }
        if (status.uidValidity != 0)
            store_.SetFolderUidState(accountId, folder,
                                     static_cast<int64_t>(status.uidValidity),
                                     static_cast<int64_t>(status.uidNext));
    }

    std::vector<uint32_t> bodyUids;
    // Stream envelopes: each header lands one at a time so `onMessageStored` can
    // fill the UI list incrementally instead of the caller waiting for the whole
    // mailbox (a large INBOX otherwise looks like the app has hung).
    UltraNetResult r = mailbox_.FetchEnvelopes(
        serverUrl, folder, static_cast<uint32_t>(sinceUid),
        [&](const UltraNetMailEnvelope& e) {
            // "UID n:*" always matches the highest UID, even one below n:
            // that message is held already.
            if (sinceUid > 0 && static_cast<int64_t>(e.uid) <= sinceUid) return;
            const MessageEnvelope m = ToStored(accountId, folder, e);
            if (store_.UpsertMessage(m)) out.stats.messages++;
            if (fetchBodies) bodyUids.push_back(e.uid);
            if (onMessageStored) onMessageStored(m);
        },
        options);
    if (!r) {
        SyncOutcome fail = SyncOutcome::Fail(r);
        fail.stats = out.stats;
        return fail;
    }

    // Fetch all new bodies over ONE reused connection (see
    // IMailboxProtocolPlugin::FetchMessageBodies) instead of reconnecting per
    // message — the difference between seconds and minutes on Gmail.
    if (fetchBodies && !bodyUids.empty()) {
        mailbox_.FetchMessageBodies(
            serverUrl, folder, bodyUids,
            [&](uint32_t uid, const std::string& raw) {
                if (!WriteBody(accountId, folder, static_cast<int64_t>(uid), raw).empty())
                    out.stats.bodies++;
            },
            options);
    }
    CountStoredAttachments(accountId, folder);
    RescanStaleVerdicts(accountId, folder);
    return out;
}

SyncOutcome SyncEngine::FetchMissing(const std::string& accountId,
                                     const std::string& folder,
                                     const std::vector<uint32_t>& uids,
                                     const std::string& serverUrl,
                                     const UltraNetMailOptions& options,
                                     bool fetchBodies,
                                     const std::function<void(const MessageEnvelope&)>& onMessageStored) {
    SyncOutcome out;
    if (uids.empty()) return out;
    std::vector<uint32_t> bodyUids;
    UltraNetResult r = mailbox_.FetchEnvelopesByUid(
        serverUrl, folder, uids,
        [&](const UltraNetMailEnvelope& e) {
            const MessageEnvelope m = ToStored(accountId, folder, e);
            if (store_.UpsertMessage(m)) {
                out.stats.messages++;
                out.stats.repaired++;
            }
            if (fetchBodies) bodyUids.push_back(e.uid);
            if (onMessageStored) onMessageStored(m);
        },
        options);
    if (!r) {
        SyncOutcome fail = SyncOutcome::Fail(r);
        fail.stats = out.stats;
        return fail;
    }
    if (fetchBodies && !bodyUids.empty()) {
        mailbox_.FetchMessageBodies(
            serverUrl, folder, bodyUids,
            [&](uint32_t uid, const std::string& raw) {
                if (!WriteBody(accountId, folder, static_cast<int64_t>(uid), raw).empty())
                    out.stats.bodies++;
            },
            options);
    }
    return out;
}

SyncOutcome SyncEngine::RefreshFolder(const std::string& accountId,
                                      const std::string& folder,
                                      const std::string& serverUrl,
                                      const UltraNetMailOptions& options,
                                      bool fetchBodies,
                                      const std::function<void(const MessageEnvelope&)>& onMessageStored) {
    // New mail first: it is what the reader waits for, and it streams into
    // the list as it arrives.
    SyncOutcome out = SyncMessages(accountId, folder, serverUrl, options, fetchBodies,
                                   onMessageStored);
    if (!out.ok) return out;

    // Then the server's whole list: read state changed elsewhere, mail
    // deleted or moved elsewhere, and the UIDs the store lacks.
    std::vector<uint32_t> missing;
    SyncOutcome rec = ReconcileFlags(accountId, folder, serverUrl, options, &missing);
    out.stats.reconciled    = rec.stats.reconciled;
    out.stats.expunged      = rec.stats.expunged;
    out.stats.bodiesRemoved = rec.stats.bodiesRemoved;

    // ... and the rows earlier versions stored blank for a header they
    // could not read. Both are fetched by UID; the repair is best-effort, and
    // what it does not get is asked for again next time.
    std::vector<int64_t> blank;
    store_.ListBlankUids(accountId, folder, blank);
    for (int64_t uid : blank)
        if (uid > 0 && uid <= 0xFFFFFFFFll) missing.push_back(static_cast<uint32_t>(uid));
    std::sort(missing.begin(), missing.end());
    missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
    if (!missing.empty()) {
        SyncOutcome rep = FetchMissing(accountId, folder, missing, serverUrl, options,
                                       fetchBodies, onMessageStored);
        out.stats.messages += rep.stats.messages;
        out.stats.repaired += rep.stats.repaired;
        out.stats.bodies   += rep.stats.bodies;
        CountStoredAttachments(accountId, folder);
    }
    // Bodies an earlier download did not get.
    if (fetchBodies) {
        const int bodies = FetchMissingBodies(accountId, folder, serverUrl, options);
        out.stats.bodies += bodies;
        if (bodies > 0) CountStoredAttachments(accountId, folder);
    }
    return out;
}

int SyncEngine::FetchMissingBodies(const std::string& accountId, const std::string& folder,
                                   const std::string& serverUrl,
                                   const UltraNetMailOptions& options, int limit) {
    std::vector<MessageEnvelope> newest;
    store_.ListMessages(accountId, folder, limit, newest);
    std::vector<uint32_t> uids;
    for (const auto& m : newest) {
        std::error_code ec;
        if (m.uid > 0 && m.uid <= 0xFFFFFFFFll &&
            !fs::exists(PathFromUtf8(BodyPath(accountId, folder, m.uid)), ec))
            uids.push_back(static_cast<uint32_t>(m.uid));
    }
    if (uids.empty()) return 0;
    int cached = 0;
    mailbox_.FetchMessageBodies(
        serverUrl, folder, uids,
        [&](uint32_t uid, const std::string& raw) {
            if (!WriteBody(accountId, folder, static_cast<int64_t>(uid), raw).empty()) ++cached;
        },
        options);
    return cached;
}

int SyncEngine::CountStoredAttachments(const std::string& accountId, const std::string& folder,
                                       int limit) {
    // Bodies downloaded before the count was recorded (or by FetchBody) are
    // counted here from the cache, a bounded batch per sync, newest first -
    // the messages the list shows at the top.
    std::vector<int64_t> uids;
    if (!store_.ListUncountedAttachments(accountId, folder, limit, uids)) return 0;
    int counted = 0;
    for (int64_t uid : uids) {
        const std::string path = BodyPath(accountId, folder, uid);
        std::string raw;
        if (!ReadBodyFile(path, raw)) continue;    // body not downloaded yet
        if (raw.empty()) continue;
        if (store_.SetAttachmentCount(accountId, folder, uid, MimeCodec::CountAttachments(raw)))
            ++counted;
    }
    return counted;
}

std::string SyncEngine::WriteBody(const std::string& accountId, const std::string& folder,
                                  int64_t uid, const std::string& raw) const {
    if (raw.empty()) return std::string();
    const std::string path = BodyPath(accountId, folder, uid);
    std::error_code ec;
    fs::create_directories(PathFromUtf8(path).parent_path(), ec);
    // Sealed with the mailbox's body key when it has one (UltraMailStorage.h).
    if (!WriteBodyFile(path, raw)) return std::string();

    // A body is scanned exactly once — here, where it has just been downloaded
    // and is already in memory. The verdict goes into the index, so the message
    // list can colour its sender badge without re-reading a single .eml file,
    // and a phishing mail is marked before it is ever opened.
    const ThreatReport report = ScanRawMessage(raw);
    MessageSecurity security;
    security.level  = report.level;
    security.score  = report.score;
    security.bulk   = report.bulk;
    security.reason = report.Summary();
    security.verifiedDomain = report.verifiedDomain;
    security.verifiedBy     = report.verifiedBy;
    security.findings       = report.Codes();
    security.attachments = MimeCodec::CountAttachments(raw);   // the list's paperclip
    store_.SetSecurity(accountId, folder, uid, security);
    return path;
}

int SyncEngine::RescanStaleVerdicts(const std::string& accountId, const std::string& folder,
                                    int limit) {
    std::vector<int64_t> uids;
    if (!store_.ListStaleVerdicts(accountId, folder, kThreatRulesRevision, limit, uids))
        return 0;
    int rescanned = 0;
    for (int64_t uid : uids) {
        std::string raw;
        if (!ReadBodyFile(BodyPath(accountId, folder, uid), raw)) continue;
        if (raw.empty()) continue;
        const ThreatReport report = ScanRawMessage(raw);
        MessageSecurity security;
        security.level  = report.level;
        security.score  = report.score;
        security.bulk   = report.bulk;
        security.reason = report.Summary();
        security.verifiedDomain = report.verifiedDomain;
        security.verifiedBy     = report.verifiedBy;
        security.findings       = report.Codes();
        // attachments stays -1: the count already stored is kept.
        if (store_.SetSecurity(accountId, folder, uid, security)) ++rescanned;
    }
    return rescanned;
}

std::string SyncEngine::FetchBody(const std::string& accountId, const std::string& folder,
                                  int64_t uid, const std::string& serverUrl,
                                  const UltraNetMailOptions& options) {
    std::string raw;
    UltraNetResult r = mailbox_.FetchMessage(serverUrl, folder,
                                             static_cast<uint32_t>(uid), raw, options);
    if (!r || raw.empty()) return std::string();
    return WriteBody(accountId, folder, uid, raw);
}

SyncOutcome SyncEngine::SetFlag(const std::string& accountId, const std::string& folder,
                                int64_t uid, uint32_t ultramailFlag, bool set,
                                const std::string& serverUrl,
                                const UltraNetMailOptions& options) {
    UltraNetResult r = mailbox_.StoreFlags(
        serverUrl, folder, static_cast<uint32_t>(uid),
        MapLocalFlagToNet(ultramailFlag), set, options);
    if (!r) return SyncOutcome::Fail(r);

    UltraDbResult lr = store_.SetFlags(accountId, folder, uid, ultramailFlag, set);
    if (!lr) return SyncOutcome::Fail(lr.message);
    return SyncOutcome{};
}

SyncOutcome SyncEngine::ReconcileFlags(const std::string& accountId,
                                       const std::string& folder,
                                       const std::string& serverUrl,
                                       const UltraNetMailOptions& options,
                                       std::vector<uint32_t>* missing) {
    if (missing) missing->clear();
    // Snapshot what we hold locally so we can both diff flags and notice UIDs the
    // server has dropped.
    std::vector<MessageEnvelope> locals;
    store_.ListMessages(accountId, folder, 0, locals);
    std::unordered_map<int64_t, uint32_t> localFlags;
    localFlags.reserve(locals.size());
    for (const auto& m : locals) localFlags[m.uid] = m.flags;

    SyncOutcome out;
    std::unordered_set<int64_t> serverUids;
    UltraNetResult r = mailbox_.FetchAllFlags(
        serverUrl, folder,
        [&](uint32_t uid, UltraNetMailFlags nf, bool flagsKnown) {
            const int64_t u = static_cast<int64_t>(uid);
            serverUids.insert(u);   // existence — drives deletion detection
            // Reconcile a flag only when the server flags were actually read;
            // rewriting on an unknown flag would wrongly mark read mail unread.
            // (UIDs new to us are the incremental SyncMessages step's job.)
            if (!flagsKnown) return;
            auto it = localFlags.find(u);
            if (it == localFlags.end()) return;
            const uint32_t want = MapNetFlagsToLocal(nf);
            if (it->second != want &&
                store_.ReplaceFlags(accountId, folder, u, want))
                out.stats.reconciled++;
        },
        options);

    // Expunge locally-held messages the server no longer lists — but NEVER on an
    // empty enumeration while we still hold mail. A failed/empty flag fetch is not
    // "the folder is empty"; treating it as such would delete the whole cache. So
    // the expunge runs only when the server positively enumerated the folder.
    const bool enumerated = r && !(serverUids.empty() && !locals.empty());
    if (enumerated) {
        std::unordered_set<int64_t> kept;
        int64_t maxUid = 0;
        for (const auto& m : locals) {
            maxUid = std::max(maxUid, m.uid);
            if (serverUids.count(m.uid)) {
                kept.insert(m.uid);
                continue;
            }
            if (store_.RemoveMessage(accountId, folder, m.uid)) {
                out.stats.expunged++;
                std::error_code ec;
                if (fs::remove(PathFromUtf8(BodyPath(accountId, folder, m.uid)), ec))
                    out.stats.bodiesRemoved++;
            }
        }
        // Bodies an earlier version left behind when it dropped only the row.
        // Only once the server has positively listed the folder, as above.
        out.stats.bodiesRemoved += PruneBodies(accountId, folder, kept, maxUid);
        // What the server lists and the store does not hold: mail an earlier
        // sync skipped. (A message that arrived since the new-mail step is in
        // here too; fetching it now is just as right.)
        if (missing) {
            for (int64_t uid : serverUids)
                if (!localFlags.count(uid)) missing->push_back(static_cast<uint32_t>(uid));
            std::sort(missing->begin(), missing->end());
        }
    }
    return out;
}

UltraDbResult SyncEngine::ForgetMessage(const std::string& accountId,
                                        const std::string& folder, int64_t uid) {
    UltraDbResult r = store_.RemoveMessage(accountId, folder, uid);
    if (!r) return r;
    std::error_code ec;
    fs::remove(PathFromUtf8(BodyPath(accountId, folder, uid)), ec);   // absent is fine
    return r;
}

int SyncEngine::PruneBodies(const std::string& accountId, const std::string& folder,
                            const std::unordered_set<int64_t>& keep, int64_t maxUid) {
    if (maxUid <= 0) return 0;
    const fs::path dir = PathFromUtf8(BodyPath(accountId, folder, 0)).parent_path();
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;

    std::vector<fs::path> stale;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || PathToUtf8(entry.path().extension()) != ".eml")
            continue;
        const std::string stem = PathToUtf8(entry.path().stem());
        if (stem.empty() || stem.find_first_not_of("0123456789") != std::string::npos)
            continue;   // not a cached body this engine wrote
        const int64_t uid = std::strtoll(stem.c_str(), nullptr, 10);
        if (uid <= maxUid && !keep.count(uid)) stale.push_back(entry.path());
    }
    int removed = 0;
    for (const fs::path& p : stale) {
        if (fs::remove(p, ec)) ++removed;
    }
    return removed;
}

SyncOutcome SyncEngine::MoveMessage(const std::string& accountId,
                                    const std::string& srcFolder, int64_t uid,
                                    const std::string& dstFolder,
                                    const std::string& serverUrl,
                                    const UltraNetMailOptions& options) {
    UltraNetResult r = mailbox_.MoveMessage(
        serverUrl, srcFolder, static_cast<uint32_t>(uid), dstFolder, options);
    if (!r) return SyncOutcome::Fail(r);

    // The server moved it out of srcFolder; drop the local row (and its cached
    // body) so the list stops showing it. The destination folder picks it up on
    // its next sync.
    UltraDbResult lr = ForgetMessage(accountId, srcFolder, uid);
    if (!lr) return SyncOutcome::Fail(lr.message);
    return SyncOutcome{};
}

} // namespace UltraMail
