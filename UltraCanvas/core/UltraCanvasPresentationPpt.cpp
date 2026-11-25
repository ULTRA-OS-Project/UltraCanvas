// core/UltraCanvasPresentationPpt.cpp
// PowerPoint 97-2003 (.ppt / .pps / .pot) read into the deck model - see
// UltraCanvasPresentationPpt.h for what is read and what is not.
// The drawing layer (OfficeArt: shape properties, the picture store, BLIPs,
// freeforms, shape types) is the .doc reader's, shared through
// Plugins/Documents/Word/UltraCanvasOfficeArt.h.
// Specification: [MS-PPT], [MS-ODRAW].
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasPresentationPpt.h"
#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasPathUtf8.h"
#include "Documents/Word/UltraCanvasOfficeArt.h"
#include "Documents/Word/UltraCanvasWordFormatInternal.h"   // ApplyOfficeShade

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

namespace {

using Bytes = std::vector<uint8_t>;
using OfficeArt::RecordHeader;
using OfficeArt::U16;
using OfficeArt::U32;

// ----- Record types ([MS-PPT] RecordType, [MS-ODRAW]) -----
constexpr uint16_t kRtDocument = 0x03E8, kRtDocumentAtom = 0x03E9, kRtSlide = 0x03EE, kRtSlideAtom = 0x03EF,
                   kRtNotes = 0x03F0, kRtNotesAtom = 0x03F1, kRtEnvironment = 0x03F2, kRtSlidePersistAtom = 0x03F3,
                   kRtMainMaster = 0x03F8, kRtSlideShowSlideInfoAtom = 0x03F9, kRtExObjList = 0x0409,
                   kRtDrawingGroup = 0x040B, kRtDrawing = 0x040C, kRtFontCollection = 0x07D5,
                   kRtColorSchemeAtom = 0x07F0, kRtPlaceholderAtom = 0x0BC3, kRtOutlineTextRefAtom = 0x0F9E,
                   kRtTextHeaderAtom = 0x0F9F, kRtTextCharsAtom = 0x0FA0, kRtStyleTextPropAtom = 0x0FA1,
                   kRtMasterTextPropAtom = 0x0FA2, kRtTextMasterStyleAtom = 0x0FA3, kRtTextRulerAtom = 0x0FA6,
                   kRtTextBytesAtom = 0x0FA8, kRtStyleTextProp9Atom = 0x0FAC, kRtFontEntityAtom = 0x0FB7,
                   kRtCString = 0x0FBA, kRtExHyperlinkAtom = 0x0FD3, kRtExHyperlink = 0x0FD7,
                   kRtTextInteractiveInfoAtom = 0x0FDF, kRtSlideListWithText = 0x0FF0, kRtInteractiveInfo = 0x0FF2,
                   kRtInteractiveInfoAtom = 0x0FF3, kRtUserEditAtom = 0x0FF5, kRtCurrentUserAtom = 0x0FF6,
                   kRtProgTags = 0x1388, kRtProgBinaryTag = 0x138A, kRtBinaryTagDataBlob = 0x138B,
                   kRtPersistDirectoryAtom = 0x1772, kRtCryptSession10Container = 0x2F14;
constexpr uint16_t kOaDggContainer = 0xF000, kOaBStoreContainer = 0xF001, kOaDgContainer = 0xF002,
                   kOaSpgrContainer = 0xF003, kOaSpContainer = 0xF004, kOaFSPGR = 0xF009, kOaFSP = 0xF00A,
                   kOaFOPT = 0xF00B, kOaClientTextbox = 0xF00D, kOaChildAnchor = 0xF00F, kOaClientAnchor = 0xF010,
                   kOaClientData = 0xF011, kOaTertiaryFOPT = 0xF122;

constexpr uint32_t kHeaderTokenEncrypted = 0xF3D1C4DF;
constexpr double kEmuPerMasterUnit = 914400.0 / 576.0;   // 576 master units to the inch
constexpr int kMaxDepth = 32;                            // nesting of containers and groups
constexpr size_t kMaxRecords = 1000000;                  // records looked at in one container
constexpr int kLevels = 5;

int64_t Emu(double masterUnits) { return static_cast<int64_t>(std::llround(masterUnits * kEmuPerMasterUnit)); }

// Visits the records directly in data[from, to), stopping at the first that
// does not fit; `visit` returns false to stop early.
template <typename Visit>
void ForEachRecord(const Bytes& data, size_t from, size_t to, Visit visit) {
    RecordHeader record;
    size_t count = 0;
    for (size_t at = from; at < to && ++count <= kMaxRecords; at = record.end) {
        if (!OfficeArt::ReadRecordHeader(data, at, to, record)) return;
        if (!visit(record)) return;
    }
}

// The first record of `type` directly in data[from, to); false when none.
bool FindRecord(const Bytes& data, size_t from, size_t to, uint16_t type, RecordHeader& found) {
    bool ok = false;
    ForEachRecord(data, from, to, [&](const RecordHeader& r) {
        if (r.type != type) return true;
        found = r;
        ok = true;
        return false;
    });
    return ok;
}

void AppendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

// UTF-16 code units [from, to) as UTF-8: a vertical tab (a line break
// inside the paragraph) as "\n", other control characters but the tab left out.
std::string Utf16ToUtf8(const std::vector<uint16_t>& text, size_t from, size_t to) {
    std::string out;
    to = std::min(to, text.size());
    for (size_t i = from; i < to; ++i) {
        uint32_t c = text[i];
        if (c == 0x0B) { out.push_back('\n'); continue; }
        if (c < 0x20 && c != 0x09) continue;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < to && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        } else if (c >= 0xD800 && c < 0xE000) {
            c = 0xFFFD;
        }
        AppendUtf8(out, c);
    }
    return out;
}

std::string Utf16Field(const Bytes& data, size_t from, size_t to) {
    std::vector<uint16_t> units;
    for (size_t at = from; at + 2 <= to && at + 2 <= data.size(); at += 2) {
        const uint16_t c = U16(data, at);
        if (c == 0) break;
        units.push_back(c);
    }
    return Utf16ToUtf8(units, 0, units.size());
}

std::string Hex(uint8_t r, uint8_t g, uint8_t b) {
    static const char* digits = "0123456789ABCDEF";
    std::string out(6, '0');
    const uint8_t v[3] = {r, g, b};
    for (int i = 0; i < 3; ++i) {
        out[i * 2] = digits[v[i] >> 4];
        out[i * 2 + 1] = digits[v[i] & 0xF];
    }
    return out;
}

using Scheme = std::array<std::string, 8>;   // background, text, shadow, title, fills, accent, hyperlink, followed

// ----- Text formatting ([MS-PPT] TextPFException, TextCFException) -----

// A ColorIndexStruct: red, green, blue and an index - 0xFE the RGB given,
// 0..7 a scheme colour, 0xFF none.
struct ColorIndex {
    uint8_t r = 0, g = 0, b = 0, index = 0xFF;
};

std::string Resolve(const ColorIndex& c, const Scheme& scheme) {
    if (c.index == 0xFE) return Hex(c.r, c.g, c.b);
    if (c.index < 8) return scheme[c.index];
    return std::string();
}

struct ParaFormat {
    enum : uint32_t {
        kHasBullet = 1u << 0, kBulletHasFont = 1u << 1, kBulletHasColor = 1u << 2, kBulletHasSize = 1u << 3,
        kBulletChar = 1u << 4, kBulletFont = 1u << 5, kBulletColor = 1u << 6, kBulletSize = 1u << 7,
        kAlign = 1u << 8, kLineSpacing = 1u << 9, kSpaceBefore = 1u << 10, kSpaceAfter = 1u << 11,
        kLeftMargin = 1u << 12, kIndent = 1u << 13
    };
    uint32_t has = 0;
    bool hasBullet = false, bulletHasFont = false, bulletHasColor = false, bulletHasSize = false;
    uint16_t bulletChar = 0x2022;
    int bulletFontRef = 0;
    int bulletSize = 100;
    ColorIndex bulletColor;
    int align = 0;
    int lineSpacing = 100, spaceBefore = 0, spaceAfter = 0;
    int leftMargin = 0, indent = 0;

    void Over(const ParaFormat& o) {
        if (o.has & kHasBullet) hasBullet = o.hasBullet;
        if (o.has & kBulletHasFont) bulletHasFont = o.bulletHasFont;
        if (o.has & kBulletHasColor) bulletHasColor = o.bulletHasColor;
        if (o.has & kBulletHasSize) bulletHasSize = o.bulletHasSize;
        if (o.has & kBulletChar) bulletChar = o.bulletChar;
        if (o.has & kBulletFont) bulletFontRef = o.bulletFontRef;
        if (o.has & kBulletColor) bulletColor = o.bulletColor;
        if (o.has & kBulletSize) bulletSize = o.bulletSize;
        if (o.has & kAlign) align = o.align;
        if (o.has & kLineSpacing) lineSpacing = o.lineSpacing;
        if (o.has & kSpaceBefore) spaceBefore = o.spaceBefore;
        if (o.has & kSpaceAfter) spaceAfter = o.spaceAfter;
        if (o.has & kLeftMargin) leftMargin = o.leftMargin;
        if (o.has & kIndent) indent = o.indent;
        has |= o.has;
    }
};

struct CharFormat {
    enum : uint32_t {
        kBold = 1u << 0, kItalic = 1u << 1, kUnderline = 1u << 2, kFont = 1u << 3, kSize = 1u << 4,
        kColor = 1u << 5, kPosition = 1u << 6
    };
    uint32_t has = 0;
    bool bold = false, italic = false, underline = false;
    int fontRef = 0;
    int size = 18;
    ColorIndex color;
    int position = 0;

    void Over(const CharFormat& o) {
        if (o.has & kBold) bold = o.bold;
        if (o.has & kItalic) italic = o.italic;
        if (o.has & kUnderline) underline = o.underline;
        if (o.has & kFont) fontRef = o.fontRef;
        if (o.has & kSize) size = o.size;
        if (o.has & kColor) color = o.color;
        if (o.has & kPosition) position = o.position;
        has |= o.has;
    }
};

// A cursor over data[at, end) that refuses to read past it.
struct ByteCursor {
    const Bytes& data;
    size_t at;
    size_t end;
    bool ok = true;
    bool Need(size_t n) {
        if (!ok || at > end || end - at < n) ok = false;
        return ok;
    }
    uint16_t U16v() { if (!Need(2)) return 0; const uint16_t v = U16(data, at); at += 2; return v; }
    int16_t I16v() { return static_cast<int16_t>(U16v()); }
    uint32_t U32v() { if (!Need(4)) return 0; const uint32_t v = U32(data, at); at += 4; return v; }
    ColorIndex Color() {
        ColorIndex c;
        if (!Need(4)) return c;
        c.r = data[at];
        c.g = data[at + 1];
        c.b = data[at + 2];
        c.index = data[at + 3];
        at += 4;
        return c;
    }
    void Skip(size_t n) { if (Need(n)) at += n; }
};

bool ParseParaFormat(ByteCursor& c, ParaFormat& pf) {
    const uint32_t masks = c.U32v();
    if (masks & 0x000F) {
        const uint16_t flags = c.U16v();
        if (masks & 0x1) { pf.hasBullet = flags & 0x1; pf.has |= ParaFormat::kHasBullet; }
        if (masks & 0x2) { pf.bulletHasFont = flags & 0x2; pf.has |= ParaFormat::kBulletHasFont; }
        if (masks & 0x4) { pf.bulletHasColor = flags & 0x4; pf.has |= ParaFormat::kBulletHasColor; }
        if (masks & 0x8) { pf.bulletHasSize = flags & 0x8; pf.has |= ParaFormat::kBulletHasSize; }
    }
    if (masks & 0x0080) { pf.bulletChar = c.U16v(); pf.has |= ParaFormat::kBulletChar; }
    if (masks & 0x0010) { pf.bulletFontRef = c.U16v(); pf.has |= ParaFormat::kBulletFont; }
    if (masks & 0x0040) { pf.bulletSize = c.I16v(); pf.has |= ParaFormat::kBulletSize; }
    if (masks & 0x0020) { pf.bulletColor = c.Color(); pf.has |= ParaFormat::kBulletColor; }
    if (masks & 0x0800) { pf.align = c.U16v(); pf.has |= ParaFormat::kAlign; }
    if (masks & 0x1000) { pf.lineSpacing = c.I16v(); pf.has |= ParaFormat::kLineSpacing; }
    if (masks & 0x2000) { pf.spaceBefore = c.I16v(); pf.has |= ParaFormat::kSpaceBefore; }
    if (masks & 0x4000) { pf.spaceAfter = c.I16v(); pf.has |= ParaFormat::kSpaceAfter; }
    if (masks & 0x0100) { pf.leftMargin = c.I16v(); pf.has |= ParaFormat::kLeftMargin; }
    if (masks & 0x0400) { pf.indent = c.I16v(); pf.has |= ParaFormat::kIndent; }
    if (masks & 0x8000) c.Skip(2);                               // defaultTabSize
    if (masks & 0x100000) {                                      // tabStops
        const uint16_t count = c.U16v();
        c.Skip(static_cast<size_t>(count) * 4);
    }
    if (masks & 0x10000) c.Skip(2);                              // fontAlign
    if (masks & 0xE0000) c.Skip(2);                              // wrapFlags
    if (masks & 0x200000) c.Skip(2);                             // textDirection
    return c.ok;
}

bool ParseCharFormat(ByteCursor& c, CharFormat& cf) {
    const uint32_t masks = c.U32v();
    if (masks & 0x3EB7) {                                        // any of the font style's flags
        const uint16_t style = c.U16v();
        if (masks & 0x1) { cf.bold = style & 0x1; cf.has |= CharFormat::kBold; }
        if (masks & 0x2) { cf.italic = style & 0x2; cf.has |= CharFormat::kItalic; }
        if (masks & 0x4) { cf.underline = style & 0x4; cf.has |= CharFormat::kUnderline; }
    }
    if (masks & 0x10000) { cf.fontRef = c.U16v(); cf.has |= CharFormat::kFont; }
    if (masks & 0x200000) c.Skip(2);                             // oldEAFontRef
    if (masks & 0x400000) {                                      // ansiFontRef: the typeface when no fontRef
        const uint16_t ansi = c.U16v();
        if (!(masks & 0x10000)) { cf.fontRef = ansi; cf.has |= CharFormat::kFont; }
    }
    if (masks & 0x800000) c.Skip(2);                             // symbolFontRef
    if (masks & 0x20000) { cf.size = c.U16v(); cf.has |= CharFormat::kSize; }
    if (masks & 0x40000) { cf.color = c.Color(); cf.has |= CharFormat::kColor; }
    if (masks & 0x80000) { cf.position = c.I16v(); cf.has |= CharFormat::kPosition; }
    if (masks & 0x100000) c.Skip(4);                             // pp10ext
    if (masks & 0x1000000) c.Skip(2);                            // newEAFontRef
    if (masks & 0x2000000) c.Skip(2);                            // csFontRef
    if (masks & 0x4000000) c.Skip(4);                            // pp11ext
    return c.ok;
}

// A master's text styles (TxMasterStyleAtom) by text type: 0 title, 1 body,
// 2 notes, 4 other, 5 centred body, 6 centred title, 7 half body, 8 quarter
// body; each with its five levels.
struct TextStyle {
    bool present = false;
    std::array<ParaFormat, kLevels> pf{};
    std::array<CharFormat, kLevels> cf{};
};
using TextStyles = std::array<TextStyle, 9>;

void ParseMasterStyle(const Bytes& data, const RecordHeader& r, TextStyles& styles) {
    if (r.instance > 8) return;
    TextStyle& style = styles[r.instance];
    ByteCursor c{data, r.body, r.end};
    const uint16_t levels = std::min<uint16_t>(c.U16v(), kLevels);
    for (uint16_t i = 0; i < levels && c.ok; ++i) {
        int level = i;
        if (r.instance >= 5) level = c.U16v();
        ParaFormat pf;
        CharFormat cf;
        if (!ParseParaFormat(c, pf) || !ParseCharFormat(c, cf)) break;
        if (level < 0 || level >= kLevels) continue;
        style.pf[static_cast<size_t>(level)] = pf;
        style.cf[static_cast<size_t>(level)] = cf;
        style.present = true;
    }
}

// The base style a text type takes its look from.
int BaseTextType(int type) {
    switch (type) {
        case 0: case 6: return 0;             // title, centred title
        case 1: case 5: case 7: case 8: return 1;   // body and its variants
        case 2: return 2;                     // notes
        default: return 4;                    // other
    }
}

// ----- Text blocks -----

struct Numbering {
    bool on = false;
    int scheme = 3;                           // ANM_ArabicPeriod
    int startAt = 1;
};

struct PFRun { uint32_t count = 0; int level = 0; ParaFormat pf; };
struct CFRun { uint32_t count = 0; CharFormat cf; };
struct LinkRange { uint32_t begin = 0, end = 0; std::string target; };
struct Ruler {
    std::array<int, kLevels> leftMargin{}, indent{};
    std::array<bool, kLevels> hasMargin{}, hasIndent{};
};

// One text: its type, characters and formatting runs, as a ClientTextbox
// or a SlideListWithText holds it.
struct TextBlock {
    int type = 4;
    std::vector<uint16_t> text;
    std::vector<PFRun> pf;
    std::vector<CFRun> cf;
    std::vector<std::pair<uint32_t, int>> masterLevels;   // MasterTextPropAtom: count, level
    Ruler ruler;
    std::vector<LinkRange> links;
    std::vector<Numbering> numbering;                     // by paragraph (StyleTextProp9Atom)
};

constexpr size_t kMaxRuns = 200000;

void ParseStyleTextProp(const Bytes& data, const RecordHeader& r, TextBlock& block) {
    ByteCursor c{data, r.body, r.end};
    const uint64_t total = block.text.size() + 1;   // the last paragraph's end counts
    uint64_t covered = 0;
    while (covered < total && c.ok && block.pf.size() < kMaxRuns) {
        PFRun run;
        run.count = c.U32v();
        run.level = c.U16v();
        if (!c.ok || !ParseParaFormat(c, run.pf)) break;
        covered += run.count;
        block.pf.push_back(run);
        if (run.count == 0 && block.pf.size() > 1) break;   // nothing more to cover
    }
    covered = 0;
    while (covered < total && c.ok && block.cf.size() < kMaxRuns) {
        CFRun run;
        run.count = c.U32v();
        if (!c.ok || !ParseCharFormat(c, run.cf)) break;
        covered += run.count;
        block.cf.push_back(run);
        if (run.count == 0 && block.cf.size() > 1) break;
    }
}

void ParseRuler(const Bytes& data, const RecordHeader& r, Ruler& ruler) {
    ByteCursor c{data, r.body, r.end};
    const uint32_t masks = c.U32v();
    if (masks & 0x2) c.Skip(2);                                  // cLevels
    if (masks & 0x1) c.Skip(2);                                  // defaultTabSize
    if (masks & 0x4) {                                           // tabs
        const uint16_t count = c.U16v();
        c.Skip(static_cast<size_t>(count) * 4);
    }
    for (int level = 0; level < kLevels && c.ok; ++level) {
        if (masks & (0x8u << level)) {
            ruler.leftMargin[static_cast<size_t>(level)] = c.I16v();
            ruler.hasMargin[static_cast<size_t>(level)] = c.ok;
        }
        if (masks & (0x100u << level)) {
            ruler.indent[static_cast<size_t>(level)] = c.I16v();
            ruler.hasIndent[static_cast<size_t>(level)] = c.ok;
        }
    }
}

// StyleTextProp9Atom: per paragraph a TextPFException9, a TextCFException9
// and a TextSIException; the automatic numbering is in the first.
void ParseStyleTextProp9(const Bytes& data, const RecordHeader& r, std::vector<Numbering>& out) {
    ByteCursor c{data, r.body, r.end};
    while (c.ok && c.at < c.end && out.size() < kMaxRuns) {
        Numbering n;
        const uint32_t pf = c.U32v();
        if (pf & 0x800000) c.Skip(2);                            // bulletBlipRef
        if (pf & 0x2000000) n.on = c.U16v() != 0;                // fBulletHasAutoNumber
        if (pf & 0x1000000) {                                    // bulletAutoNumberScheme
            n.scheme = c.U16v();
            n.startAt = std::max<int>(1, c.I16v());
        }
        const uint32_t cf = c.U32v();
        if (cf & 0x100000) c.Skip(4);                            // pp10runid
        const uint32_t si = c.U32v();
        if (si & 0x1) c.Skip(2);                                 // spellInfo
        if (si & 0x2) c.Skip(2);                                 // lang
        if (si & 0x4) c.Skip(2);                                 // altLang
        if (si & 0x40) c.Skip(2);                                // bidi
        if (si & 0x20) c.Skip(4);                                // pp10runid
        if (si & 0x200) {                                        // smartTags
            const uint32_t count = c.U32v();
            if (count > 65536) break;
            c.Skip(static_cast<size_t>(count) * 4);
        }
        if (!c.ok) break;
        out.push_back(n);
    }
}

// The PowerPoint 2000 extension a shape's ClientData may carry: its
// paragraphs' numbering ("___PPT9" binary tag).
void ParseProgTags(const Bytes& data, size_t from, size_t to, std::vector<Numbering>& numbering) {
    ForEachRecord(data, from, to, [&](const RecordHeader& tag) {
        if (tag.type != kRtProgBinaryTag) return true;
        bool ppt9 = false;
        ForEachRecord(data, tag.body, tag.end, [&](const RecordHeader& r) {
            if (r.type == kRtCString) ppt9 = Utf16Field(data, r.body, r.end) == "___PPT9";
            else if (r.type == kRtBinaryTagDataBlob && ppt9) {
                RecordHeader atom;
                if (FindRecord(data, r.body, r.end, kRtStyleTextProp9Atom, atom))
                    ParseStyleTextProp9(data, atom, numbering);
            }
            return true;
        });
        return true;
    });
}

// The texts in data[from, to): each starts at a TextHeaderAtom. `links`
// turns an exHyperlinkIdRef into its target.
void ParseTextBlocks(const Bytes& data, size_t from, size_t to, const std::map<uint32_t, std::string>& links,
                     std::vector<TextBlock>& out, int* outlineRef = nullptr) {
    std::string pendingLink;
    ForEachRecord(data, from, to, [&](const RecordHeader& r) {
        switch (r.type) {
            case kRtTextHeaderAtom: {
                TextBlock block;
                block.type = static_cast<int>(U32(data, r.body));
                out.push_back(std::move(block));
                break;
            }
            case kRtOutlineTextRefAtom:
                if (outlineRef && r.end - r.body >= 4) *outlineRef = static_cast<int>(U32(data, r.body));
                break;
            case kRtTextCharsAtom:
                if (!out.empty()) {
                    TextBlock& b = out.back();
                    b.text.clear();
                    for (size_t at = r.body; at + 2 <= r.end; at += 2) b.text.push_back(U16(data, at));
                }
                break;
            case kRtTextBytesAtom:
                if (!out.empty()) {
                    TextBlock& b = out.back();
                    b.text.assign(data.begin() + static_cast<std::ptrdiff_t>(r.body),
                                  data.begin() + static_cast<std::ptrdiff_t>(r.end));
                }
                break;
            case kRtStyleTextPropAtom:
                if (!out.empty()) ParseStyleTextProp(data, r, out.back());
                break;
            case kRtMasterTextPropAtom:
                if (!out.empty()) {
                    ByteCursor c{data, r.body, r.end};
                    while (c.ok && c.at < c.end && out.back().masterLevels.size() < kMaxRuns) {
                        const uint32_t count = c.U32v();
                        const uint16_t level = c.U16v();
                        if (c.ok) out.back().masterLevels.emplace_back(count, level);
                    }
                }
                break;
            case kRtTextRulerAtom:
                if (!out.empty()) ParseRuler(data, r, out.back().ruler);
                break;
            case kRtInteractiveInfo: {
                pendingLink.clear();
                RecordHeader atom;
                if (FindRecord(data, r.body, r.end, kRtInteractiveInfoAtom, atom) && atom.end - atom.body >= 16) {
                    const uint32_t linkId = U32(data, atom.body + 4);
                    const uint8_t action = data[atom.body + 8];
                    const uint8_t jump = data[atom.body + 10];
                    if (action == 4) {
                        const auto found = links.find(linkId);
                        if (found != links.end()) pendingLink = found->second;
                    } else if (action == 3 && jump == 3) {
                        pendingLink = "#slide1";
                    }
                }
                break;
            }
            case kRtTextInteractiveInfoAtom:
                if (!out.empty() && !pendingLink.empty() && r.end - r.body >= 8) {
                    LinkRange link;
                    link.begin = U32(data, r.body);
                    link.end = U32(data, r.body + 4);
                    link.target = pendingLink;
                    if (link.end > link.begin && out.back().links.size() < kMaxRuns) out.back().links.push_back(link);
                }
                pendingLink.clear();
                break;
            default:
                break;
        }
        return out.size() < kMaxRuns;
    });
}

// ----- Shapes as read -----

struct RawShape {
    int type = 0;                             // MSOSPT
    uint32_t flags = 0;                       // FSP: 0x1 group, 0x4 patriarch, 0x8 deleted, 0x40 / 0x80 flips,
                                              // 0x100 connector, 0x400 background
    uint32_t spid = 0;
    OfficeArt::ShapeProperties props;
    bool hasAnchor = false, hasChildAnchor = false, hasGroupSpace = false;
    double anchor[4] = {0, 0, 0, 0};          // left, top, right, bottom (ClientAnchor: master units)
    double child[4] = {0, 0, 0, 0};           // in the group's space
    double groupSpace[4] = {0, 0, 1, 1};
    int placeholderType = -1;
    std::vector<TextBlock> text;
    int outlineRef = -1;
    std::vector<Numbering> numbering;
};

// A shape, or a group with its members, placed on the sheet in master units.
struct Placed {
    RawShape raw;
    double x = 0, y = 0, w = 0, h = 0;
    bool group = false;
    std::vector<Placed> members;
};

// ----- The reader -----

class PptReader {
public:
    PptReader(const Bytes& doc, const Bytes* pictures, bool withPictures)
        : doc_(doc), pictureStream_(pictures), withPictures_(withPictures) {}

    bool Read(const Bytes* currentUser, PptxPresentation& out, std::string& error) {
        if (!LoadEdits(currentUser, error)) return false;
        const auto docAt = persist_.find(docPersistId_);
        RecordHeader document;
        if (docAt == persist_.end() || !OfficeArt::ReadRecordHeader(doc_, docAt->second, doc_.size(), document) ||
            document.type != kRtDocument) {
            error = "The PowerPoint 97-2003 presentation is damaged: its document record is missing";
            return false;
        }
        ReadDocument(document, out);
        ReadMasters(out);
        ReadSlides(out);
        return true;
    }

private:
    const Bytes& doc_;
    const Bytes* pictureStream_;
    bool withPictures_;

    std::map<uint32_t, size_t> persist_;     // persist id -> offset in the stream
    uint32_t docPersistId_ = 0;

    std::vector<std::string> fonts_;
    TextStyles docStyles_{};
    std::vector<OfficeArt::BlipRef> blips_;
    std::map<uint32_t, std::string> links_;   // exHyperlinkId -> target

    // A SlideListWithText's entry: the persisted sheet, its slide id, and
    // the outline text PowerPoint keeps beside it.
    struct ListEntry {
        uint32_t persistId = 0;
        uint32_t slideId = 0;
        std::vector<TextBlock> texts;
    };
    std::vector<ListEntry> masterList_, slideList_, notesList_;

    struct MasterInfo {
        uint32_t slideId = 0;
        bool titleMaster = false;
        uint32_t mainMasterId = 0;            // a title master's main master
        TextStyles styles{};
        Scheme scheme{};
        std::string background;
        std::vector<PptxShape> placeholders;   // the model's (resolved)
        std::map<int, PptxShape> byPlaceholderType;   // OEPlaceholderAtom type -> shape
        int modelMaster = -1;                 // index into PptxPresentation::masters
        int titleLayout = -1;                 // a title master's layout
    };
    std::vector<MasterInfo> masters_;
    std::map<std::pair<int, int>, int> layoutByMasterGeom_;   // (model master, geom) -> layout

    // What the text of the sheet being read resolves against.
    struct Sheet {
        const TextStyles* styles = nullptr;
        Scheme scheme{};
        const std::vector<TextBlock>* outline = nullptr;
        const MasterInfo* master = nullptr;   // a slide's master: inherited places
        bool design = false;                  // a master: furniture kept
    };

    // ----- Edits and the persist directory -----

    bool LoadEdits(const Bytes* currentUser, std::string& error) {
        size_t editAt = SIZE_MAX;
        if (currentUser) {
            RecordHeader cu;
            if (OfficeArt::ReadRecordHeader(*currentUser, 0, currentUser->size(), cu) &&
                cu.type == kRtCurrentUserAtom && cu.end - cu.body >= 12) {
                if (U32(*currentUser, cu.body + 4) == kHeaderTokenEncrypted) {
                    error = EncryptedMessage();
                    return false;
                }
                editAt = U32(*currentUser, cu.body + 8);
            }
        }
        RecordHeader edit;
        if (editAt == SIZE_MAX || !OfficeArt::ReadRecordHeader(doc_, editAt, doc_.size(), edit) ||
            edit.type != kRtUserEditAtom) {
            // No usable Current User stream: the last edit at the stream's top level.
            editAt = SIZE_MAX;
            ForEachRecord(doc_, 0, doc_.size(), [&](const RecordHeader& r) {
                if (r.type == kRtUserEditAtom) editAt = r.body - 8;
                return true;
            });
            if (editAt == SIZE_MAX || !OfficeArt::ReadRecordHeader(doc_, editAt, doc_.size(), edit)) {
                error = "The PowerPoint 97-2003 presentation is damaged: it has no edit record";
                return false;
            }
        }
        std::set<size_t> seen;
        bool first = true;
        uint32_t cryptRef = 0;
        while (seen.insert(editAt).second && seen.size() < 4096) {
            if (!OfficeArt::ReadRecordHeader(doc_, editAt, doc_.size(), edit) || edit.type != kRtUserEditAtom ||
                edit.end - edit.body < 28) {
                break;
            }
            if (first) {
                docPersistId_ = U32(doc_, edit.body + 16);
                if (edit.end - edit.body >= 32) cryptRef = U32(doc_, edit.body + 28);
                first = false;
            }
            ReadPersistDirectory(U32(doc_, edit.body + 12));
            const uint32_t previous = U32(doc_, edit.body + 8);
            if (previous == 0) break;
            editAt = previous;
        }
        if (first) {
            error = "The PowerPoint 97-2003 presentation is damaged: its edit record cannot be read";
            return false;
        }
        if (cryptRef != 0) {
            const auto at = persist_.find(cryptRef);
            RecordHeader crypt;
            if (at != persist_.end() && OfficeArt::ReadRecordHeader(doc_, at->second, doc_.size(), crypt) &&
                crypt.type == kRtCryptSession10Container) {
                error = EncryptedMessage();
                return false;
            }
        }
        return true;
    }

    static std::string EncryptedMessage() {
        return "The presentation is protected with a password. Password-protected PowerPoint 97-2003 files "
               "(.ppt) cannot be opened here; remove the password in PowerPoint or LibreOffice, or save the "
               "deck as .pptx there, and open that";
    }

    // Newer edits are read first, so an id already known keeps its offset.
    void ReadPersistDirectory(size_t at) {
        RecordHeader dir;
        if (!OfficeArt::ReadRecordHeader(doc_, at, doc_.size(), dir) || dir.type != kRtPersistDirectoryAtom) return;
        ByteCursor c{doc_, dir.body, dir.end};
        while (c.ok && c.at < c.end) {
            const uint32_t entry = c.U32v();
            const uint32_t first = entry & 0xFFFFF;
            const uint32_t count = entry >> 20;
            for (uint32_t i = 0; i < count && c.ok; ++i) {
                const uint32_t offset = c.U32v();
                if (c.ok && offset < doc_.size()) persist_.emplace(first + i, offset);
            }
        }
    }

    bool Persisted(uint32_t id, uint16_t type, RecordHeader& out) const {
        const auto at = persist_.find(id);
        if (at == persist_.end()) return false;
        return OfficeArt::ReadRecordHeader(doc_, at->second, doc_.size(), out) && out.type == type;
    }

    // ----- The document -----

    void ReadDocument(const RecordHeader& document, PptxPresentation& out) {
        ForEachRecord(doc_, document.body, document.end, [&](const RecordHeader& r) {
            switch (r.type) {
                case kRtDocumentAtom:
                    if (r.end - r.body >= 8) {
                        const int32_t w = static_cast<int32_t>(U32(doc_, r.body));
                        const int32_t h = static_cast<int32_t>(U32(doc_, r.body + 4));
                        if (w > 0 && h > 0 && w < 1000000 && h < 1000000) {
                            out.slideWidth = Emu(w);
                            out.slideHeight = Emu(h);
                        }
                    }
                    break;
                case kRtEnvironment:
                    ReadEnvironment(r);
                    break;
                case kRtDrawingGroup: {
                    RecordHeader dgg, store;
                    if (FindRecord(doc_, r.body, r.end, kOaDggContainer, dgg) &&
                        FindRecord(doc_, dgg.body, dgg.end, kOaBStoreContainer, store)) {
                        OfficeArt::ReadBStore(doc_, store.body, store.end, pictureStream_, blips_);
                    }
                    break;
                }
                case kRtExObjList:
                    ReadHyperlinks(r);
                    break;
                case kRtSlideListWithText:
                    if (r.instance == 0) ReadList(r, slideList_);
                    else if (r.instance == 1) ReadList(r, masterList_);
                    else if (r.instance == 2) ReadList(r, notesList_);
                    break;
                default:
                    break;
            }
            return true;
        });
        if (out.slideWidth <= 0 || out.slideHeight <= 0) {
            out.slideWidth = 9144000;   // 10 x 7.5 inches, PowerPoint 97's own
            out.slideHeight = 6858000;
        }
    }

    void ReadEnvironment(const RecordHeader& env) {
        ForEachRecord(doc_, env.body, env.end, [&](const RecordHeader& r) {
            if (r.type == kRtFontCollection) {
                ForEachRecord(doc_, r.body, r.end, [&](const RecordHeader& font) {
                    if (font.type != kRtFontEntityAtom || font.end - font.body < 64) return true;
                    const size_t index = font.instance;
                    if (index >= 4096) return true;
                    if (fonts_.size() <= index) fonts_.resize(index + 1);
                    fonts_[index] = Utf16Field(doc_, font.body, font.body + 64);
                    return true;
                });
            } else if (r.type == kRtTextMasterStyleAtom) {
                ParseMasterStyle(doc_, r, docStyles_);
            }
            return true;
        });
    }

    void ReadHyperlinks(const RecordHeader& list) {
        ForEachRecord(doc_, list.body, list.end, [&](const RecordHeader& r) {
            if (r.type != kRtExHyperlink) return true;
            uint32_t id = 0;
            bool haveId = false;
            std::string target, location;
            ForEachRecord(doc_, r.body, r.end, [&](const RecordHeader& f) {
                if (f.type == kRtExHyperlinkAtom && f.end - f.body >= 4) {
                    id = U32(doc_, f.body);
                    haveId = true;
                } else if (f.type == kRtCString && f.instance == 1) {
                    target = Utf16Field(doc_, f.body, f.end);
                } else if (f.type == kRtCString && f.instance == 3) {
                    location = Utf16Field(doc_, f.body, f.end);
                }
                return true;
            });
            if (!haveId) return true;
            // A jump to a slide: "<slide id>,<slide number>,<title>".
            const size_t comma = location.find(',');
            if (target.empty() && comma != std::string::npos) {
                const size_t next = location.find(',', comma + 1);
                const std::string number = location.substr(comma + 1, next == std::string::npos ? std::string::npos
                                                                                                : next - comma - 1);
                int n = 0;
                for (char ch : number) {
                    if (ch < '0' || ch > '9' || n > 100000) { n = 0; break; }
                    n = n * 10 + (ch - '0');
                }
                if (n > 0) target = "#slide" + std::to_string(n);
            }
            if (target.empty()) target = location;
            if (!target.empty()) links_[id] = target;
            return true;
        });
    }

    void ReadList(const RecordHeader& list, std::vector<ListEntry>& out) {
        size_t textFrom = 0;
        auto flushText = [&](size_t to) {
            if (!out.empty() && textFrom != 0 && to > textFrom)
                ParseTextBlocks(doc_, textFrom, to, links_, out.back().texts);
        };
        ForEachRecord(doc_, list.body, list.end, [&](const RecordHeader& r) {
            if (r.type == kRtSlidePersistAtom) {
                flushText(r.body - 8);
                if (r.end - r.body >= 16) {
                    ListEntry entry;
                    entry.persistId = U32(doc_, r.body);
                    entry.slideId = U32(doc_, r.body + 12);
                    out.push_back(std::move(entry));
                }
                textFrom = r.end;
            }
            return out.size() < 100000;
        });
        flushText(list.end);
    }

    // ----- Sheets: masters, slides, notes -----

    struct SheetRecords {
        RecordHeader self;
        bool haveAtom = false;
        uint32_t geom = 0;
        uint32_t masterIdRef = 0, notesIdRef = 0;
        uint16_t flags = 0x7;                 // fMasterObjects, fMasterScheme, fMasterBackground
        bool haveScheme = false;
        Scheme scheme{};
        bool haveDrawing = false;
        RecordHeader drawing;
        bool haveShow = false;
        uint8_t effectType = 0, effectDirection = 0, speed = 1;
        uint16_t showFlags = 0;
        int32_t slideTime = 0;
        TextStyles styles{};
        uint32_t notesSlideIdRef = 0;
    };

    SheetRecords ReadSheetRecords(const RecordHeader& sheet) const {
        SheetRecords s;
        s.self = sheet;
        bool schemeIsSlides = false;
        ForEachRecord(doc_, sheet.body, sheet.end, [&](const RecordHeader& r) {
            switch (r.type) {
                case kRtSlideAtom:
                    if (r.end - r.body >= 24) {
                        s.haveAtom = true;
                        s.geom = U32(doc_, r.body);
                        s.masterIdRef = U32(doc_, r.body + 12);
                        s.notesIdRef = U32(doc_, r.body + 16);
                        s.flags = U16(doc_, r.body + 20);
                    }
                    break;
                case kRtNotesAtom:
                    if (r.end - r.body >= 6) {
                        s.notesSlideIdRef = U32(doc_, r.body);
                        s.flags = U16(doc_, r.body + 4);
                    }
                    break;
                case kRtColorSchemeAtom:
                    // The sheet's own scheme (instance 1); a master also lists
                    // the schemes a user can pick (instance 6).
                    if (r.end - r.body >= 32 && (!s.haveScheme || (r.instance == 1 && !schemeIsSlides))) {
                        for (size_t i = 0; i < 8; ++i)
                            s.scheme[i] = Hex(doc_[r.body + i * 4], doc_[r.body + i * 4 + 1], doc_[r.body + i * 4 + 2]);
                        s.haveScheme = true;
                        schemeIsSlides = r.instance == 1;
                    }
                    break;
                case kRtDrawing:
                    s.drawing = r;
                    s.haveDrawing = true;
                    break;
                case kRtSlideShowSlideInfoAtom:
                    if (r.end - r.body >= 16) {
                        s.haveShow = true;
                        s.slideTime = static_cast<int32_t>(U32(doc_, r.body));
                        s.effectDirection = doc_[r.body + 8];
                        s.effectType = doc_[r.body + 9];
                        s.showFlags = U16(doc_, r.body + 10);
                        s.speed = doc_[r.body + 12];
                    }
                    break;
                case kRtTextMasterStyleAtom:
                    ParseMasterStyle(doc_, r, s.styles);
                    break;
                default:
                    break;
            }
            return true;
        });
        return s;
    }

    void ReadMasters(PptxPresentation& out) {
        for (const ListEntry& entry : masterList_) {
            RecordHeader record;
            const bool main = Persisted(entry.persistId, kRtMainMaster, record);
            if (!main && !Persisted(entry.persistId, kRtSlide, record)) continue;
            SheetRecords s = ReadSheetRecords(record);
            MasterInfo info;
            info.slideId = entry.slideId;
            info.titleMaster = !main;
            info.mainMasterId = main ? 0 : s.masterIdRef;
            info.styles = s.styles;
            const MasterInfo* parent = main ? nullptr : FindMaster(s.masterIdRef);
            if (parent) {
                // A title master's text looks are its main master's.
                for (size_t t = 0; t < info.styles.size(); ++t)
                    if (!info.styles[t].present) info.styles[t] = parent->styles[t];
            }
            info.scheme = s.haveScheme ? s.scheme : (parent ? parent->scheme : DefaultScheme());
            Sheet sheet;
            sheet.styles = &info.styles;
            sheet.scheme = info.scheme;
            sheet.outline = &entry.texts;
            sheet.design = true;
            std::vector<PptxShape> shapes;
            std::map<int, PptxShape> byType;
            if (s.haveDrawing) info.background = ReadDrawing(s.drawing, sheet, shapes, &byType);
            if (info.background.empty() && parent && (s.flags & 0x4)) info.background = parent->background;
            for (PptxShape& shape : shapes)
                if (!shape.placeholder.empty()) info.placeholders.push_back(shape);
            info.byPlaceholderType = std::move(byType);
            masters_.push_back(std::move(info));
        }
        // The model's masters: the main masters, in order; a title master is
        // its main master's "title" layout.
        for (MasterInfo& m : masters_) {
            if (m.titleMaster) continue;
            PptxMaster master;
            master.name = "Master " + std::to_string(out.masters.size() + 1);
            master.placeholders = m.placeholders;
            master.background = m.background;
            m.modelMaster = static_cast<int>(out.masters.size());
            out.masters.push_back(std::move(master));
        }
        for (MasterInfo& m : masters_) {
            if (!m.titleMaster) continue;
            const MasterInfo* parent = FindMaster(m.mainMasterId);
            if (!parent || parent->modelMaster < 0) continue;
            m.modelMaster = parent->modelMaster;
            PptxLayout layout;
            layout.name = "Title Slide";
            layout.type = "title";
            layout.masterIndex = m.modelMaster;
            layout.placeholders = m.placeholders;
            layout.background = m.background == parent->background ? std::string() : m.background;
            m.titleLayout = static_cast<int>(out.layouts.size());
            out.layouts.push_back(std::move(layout));
        }
        if (!masters_.empty()) {
            const MasterInfo& first = masters_.front();
            static const char* kNames[8] = {"lt1", "dk1", "dk2", "lt2", "accent1", "accent2", "hlink", "folHlink"};
            for (size_t i = 0; i < 8; ++i)
                if (!first.scheme[i].empty()) out.themeColors[kNames[i]] = first.scheme[i];
            const CharFormat& title = first.styles[0].cf[0];
            const CharFormat& body = first.styles[1].cf[0];
            if (first.styles[0].present && (title.has & CharFormat::kFont)) out.majorFont = FontName(title.fontRef);
            if (first.styles[1].present && (body.has & CharFormat::kFont)) out.minorFont = FontName(body.fontRef);
        }
    }

    const MasterInfo* FindMaster(uint32_t slideId) const {
        for (const MasterInfo& m : masters_)
            if (m.slideId == slideId) return &m;
        return nullptr;
    }

    static Scheme DefaultScheme() {
        return Scheme{"FFFFFF", "000000", "808080", "000000", "BBE0E3", "333399", "009999", "99CC00"};
    }

    std::string FontName(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= fonts_.size()) return std::string();
        return fonts_[static_cast<size_t>(index)];
    }

    // The layout a slide is on: its title master's, or the one made for its
    // master and slide layout (SlideAtom geom).
    int LayoutFor(const MasterInfo* master, uint32_t geom, PptxPresentation& out) {
        if (!master || master->modelMaster < 0) return -1;
        if (master->titleMaster && master->titleLayout >= 0) return master->titleLayout;
        const auto key = std::make_pair(master->modelMaster, static_cast<int>(geom));
        if (const auto found = layoutByMasterGeom_.find(key); found != layoutByMasterGeom_.end()) return found->second;
        PptxLayout layout;
        layout.masterIndex = master->modelMaster;
        bool title = true, body = true;
        switch (geom) {
            case 0: layout.name = "Title Slide"; layout.type = "title"; break;
            case 7: layout.name = "Title Only"; layout.type = "titleOnly"; body = false; break;
            case 8: case 9: case 10: case 11: case 13: layout.name = "Two Content"; layout.type = "twoObj"; break;
            case 14: layout.name = "Four Content"; layout.type = "fourObj"; break;
            case 15: layout.name = "Content"; layout.type = "objOnly"; title = false; break;
            case 16: layout.name = "Blank"; layout.type = "blank"; title = false; body = false; break;
            case 17: case 18: layout.name = "Vertical Title and Text"; layout.type = "vertTitleAndTx"; break;
            default: layout.name = "Title and Content"; layout.type = "obj"; break;
        }
        for (const PptxShape& p : out.masters[static_cast<size_t>(master->modelMaster)].placeholders) {
            const bool isTitle = p.kind == PptxShapeKind::Title;
            const bool isBody = p.kind == PptxShapeKind::Body;
            if ((isTitle && title) || (isBody && body) || (!isTitle && !isBody)) {
                PptxShape copy = p;
                if (geom == 0 && isTitle) copy.placeholder = "ctrTitle";
                if (geom == 0 && isBody) {
                    copy.placeholder = "subTitle";
                    copy.kind = PptxShapeKind::Subtitle;
                }
                layout.placeholders.push_back(std::move(copy));
            }
        }
        const int index = static_cast<int>(out.layouts.size());
        out.layouts.push_back(std::move(layout));
        layoutByMasterGeom_[key] = index;
        return index;
    }

    void ReadSlides(PptxPresentation& out) {
        for (const ListEntry& entry : slideList_) {
            RecordHeader record;
            if (!Persisted(entry.persistId, kRtSlide, record)) continue;
            SheetRecords s = ReadSheetRecords(record);
            const MasterInfo* master = FindMaster(s.masterIdRef);
            if (!master && !masters_.empty()) master = &masters_.front();
            PptxSlide slide;
            slide.number = static_cast<int>(out.slides.size()) + 1;
            Sheet sheet;
            sheet.styles = master ? &master->styles : &docStyles_;
            const Scheme masterScheme = master ? master->scheme : DefaultScheme();
            sheet.scheme = (s.haveScheme && !(s.flags & 0x2)) ? s.scheme : masterScheme;
            sheet.outline = &entry.texts;
            sheet.master = master;
            std::string background;
            if (s.haveDrawing) background = ReadDrawing(s.drawing, sheet, slide.shapes, nullptr);
            slide.background = (s.flags & 0x4) || background.empty() ? (master ? master->background : std::string())
                                                                     : background;
            slide.textColor = sheet.scheme[1];
            slide.layoutIndex = LayoutFor(master, s.geom, out);
            if (s.haveShow) ReadTransition(s, slide);
            for (const PptxShape& shape : slide.shapes) {
                if (shape.kind != PptxShapeKind::Title || !slide.title.empty()) continue;
                for (const PptxParagraph& p : shape.paragraphs) {
                    const std::string text = p.Text();
                    if (text.empty()) continue;
                    slide.title += (slide.title.empty() ? "" : " ") + text;
                }
            }
            ReadNotes(entry, s.notesIdRef, master, slide);
            out.slides.push_back(std::move(slide));
        }
        // The pictures' bytes, for drawing and for saving.
        if (withPictures_) {
            for (const auto& [index, target] : pictureTargets_) {
                if (index == 0 || index > blips_.size() || !blips_[index - 1].stream) continue;
                std::vector<uint8_t> bytes;
                std::string mime;
                const OfficeArt::BlipRef& blip = blips_[index - 1];
                if (OfficeArt::ReadBlipRecord(*blip.stream, blip.at, blip.end, bytes, mime))
                    out.pictures[target] = std::move(bytes);
            }
        }
    }

    void ReadTransition(const SheetRecords& s, PptxSlide& slide) const {
        slide.hidden = (s.showFlags & 0x0004) != 0;
        PptxTransition& t = slide.transition;
        switch (s.effectType) {
            case 0:  t.kind = s.effectDirection == 1 ? PptxTransitionKind::Fade : PptxTransitionKind::NoTransition; break;
            case 4:  t.kind = PptxTransitionKind::Cover; break;
            case 7:  t.kind = PptxTransitionKind::Cover; break;    // pull: the reverse of a cover
            case 10: t.kind = PptxTransitionKind::Wipe; break;
            case 11: t.kind = PptxTransitionKind::Zoom; break;     // box in / out
            case 13: t.kind = PptxTransitionKind::Split; break;
            case 20: t.kind = PptxTransitionKind::Push; break;
            default: t.kind = PptxTransitionKind::Fade; break;     // fade, dissolve and the rest
        }
        t.durationSec = s.speed == 0 ? 1.0 : s.speed == 1 ? 0.75 : 0.5;
        const bool manual = (s.showFlags & 0x0001) != 0;
        const bool automatic = (s.showFlags & 0x0400) != 0;
        if (automatic && s.slideTime >= 0) t.advanceAfterSec = s.slideTime / 1000.0;
        t.advanceOnClick = manual || !automatic;
    }

    void ReadNotes(const ListEntry& slideEntry, uint32_t notesIdRef, const MasterInfo* master, PptxSlide& slide) {
        const ListEntry* notes = nullptr;
        for (const ListEntry& n : notesList_)
            if (notesIdRef != 0 && n.slideId == notesIdRef) { notes = &n; break; }
        RecordHeader record;
        if (!notes) {
            // By the notes page's own reference to its slide.
            for (const ListEntry& n : notesList_) {
                if (!Persisted(n.persistId, kRtNotes, record)) continue;
                if (ReadSheetRecords(record).notesSlideIdRef == slideEntry.slideId) { notes = &n; break; }
            }
        }
        if (!notes || !Persisted(notes->persistId, kRtNotes, record)) return;
        const SheetRecords s = ReadSheetRecords(record);
        if (!s.haveDrawing) return;
        Sheet sheet;
        sheet.styles = master ? &master->styles : &docStyles_;
        sheet.scheme = master ? master->scheme : DefaultScheme();
        sheet.outline = &notes->texts;
        std::vector<PptxShape> shapes;
        noteShapes_ = true;
        ReadDrawing(s.drawing, sheet, shapes, nullptr);
        noteShapes_ = false;
        for (PptxShape& shape : shapes) {
            if (shape.placeholder != "body" && shape.placeholder != "notes") continue;
            bool words = false;
            for (const PptxParagraph& p : shape.paragraphs) words = words || !p.Text().empty();
            if (!words) continue;   // an empty notes placeholder: no notes
            for (PptxParagraph& p : shape.paragraphs) slide.notes.push_back(std::move(p));
        }
    }
    bool noteShapes_ = false;

    // ----- Drawings -----

    // The shapes of a sheet's drawing into `out`; returns the background's
    // colour ("RRGGBB"; empty: none of its own). `byType` collects a master's
    // placeholders by their OEPlaceholderAtom type.
    std::string ReadDrawing(const RecordHeader& drawing, const Sheet& sheet, std::vector<PptxShape>& out,
                            std::map<int, PptxShape>* byType) {
        std::string background;
        RecordHeader dg;
        if (!FindRecord(doc_, drawing.body, drawing.end, kOaDgContainer, dg)) return background;
        ForEachRecord(doc_, dg.body, dg.end, [&](const RecordHeader& r) {
            if (r.type == kOaSpgrContainer) {
                Placed root = ReadGroup(r, 0);
                for (Placed& member : root.members) Emit(member, sheet, out, byType, 0);
            } else if (r.type == kOaSpContainer) {
                RawShape raw = ReadShapeContainer(r);
                if (raw.flags & 0x400) background = BackgroundColour(raw.props, sheet.scheme);
            }
            return true;
        });
        return background;
    }

    std::string BackgroundColour(const OfficeArt::ShapeProperties& p, const Scheme& scheme) const {
        if (p.filledSet && !p.filled) return std::string();
        const std::string colour = ColourRef(p.fillColor, scheme);
        return colour.empty() ? "FFFFFF" : colour;
    }

    // An OfficeArtCOLORREF: a scheme index (fSchemeIndex), an RGB, or a
    // system colour (which this reader does not know: empty).
    static std::string ColourRef(uint32_t value, const Scheme& scheme) {
        const uint8_t flags = static_cast<uint8_t>(value >> 24);
        if (flags & 0x08) return (value & 0xFF) < 8 ? scheme[value & 0xFF] : std::string();
        if (flags & 0x10) return std::string();
        return Hex(static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16));
    }

    RawShape ReadShapeContainer(const RecordHeader& sp) const {
        RawShape raw;
        ForEachRecord(doc_, sp.body, sp.end, [&](const RecordHeader& r) {
            const size_t length = r.end - r.body;
            switch (r.type) {
                case kOaFSP:
                    if (length >= 8) {
                        raw.type = r.instance;
                        raw.spid = U32(doc_, r.body);
                        raw.flags = U32(doc_, r.body + 4);
                    }
                    break;
                case kOaFOPT:
                case kOaTertiaryFOPT:
                    OfficeArt::ReadProperties(doc_, r.body, r.end, r.instance, raw.props);
                    break;
                case kOaFSPGR:
                    if (length >= 16) {
                        for (size_t i = 0; i < 4; ++i)
                            raw.groupSpace[i] = static_cast<int32_t>(U32(doc_, r.body + i * 4));
                        raw.hasGroupSpace = true;
                    }
                    break;
                case kOaChildAnchor:
                    if (length >= 16) {
                        for (size_t i = 0; i < 4; ++i) raw.child[i] = static_cast<int32_t>(U32(doc_, r.body + i * 4));
                        raw.hasChildAnchor = true;
                    }
                    break;
                case kOaClientAnchor:
                    // top, left, right, bottom: 16-bit (SmallRectStruct) or 32-bit (RectStruct).
                    if (length >= 16) {
                        raw.anchor[1] = static_cast<int32_t>(U32(doc_, r.body));
                        raw.anchor[0] = static_cast<int32_t>(U32(doc_, r.body + 4));
                        raw.anchor[2] = static_cast<int32_t>(U32(doc_, r.body + 8));
                        raw.anchor[3] = static_cast<int32_t>(U32(doc_, r.body + 12));
                        raw.hasAnchor = true;
                    } else if (length >= 8) {
                        raw.anchor[1] = static_cast<int16_t>(U16(doc_, r.body));
                        raw.anchor[0] = static_cast<int16_t>(U16(doc_, r.body + 2));
                        raw.anchor[2] = static_cast<int16_t>(U16(doc_, r.body + 4));
                        raw.anchor[3] = static_cast<int16_t>(U16(doc_, r.body + 6));
                        raw.hasAnchor = true;
                    }
                    break;
                case kOaClientData:
                    ForEachRecord(doc_, r.body, r.end, [&](const RecordHeader& d) {
                        if (d.type == kRtPlaceholderAtom && d.end - d.body >= 8) raw.placeholderType = doc_[d.body + 4];
                        else if (d.type == kRtProgTags) ParseProgTags(doc_, d.body, d.end, raw.numbering);
                        return true;
                    });
                    break;
                case kOaClientTextbox:
                    ParseTextBlocks(doc_, r.body, r.end, links_, raw.text, &raw.outlineRef);
                    ForEachRecord(doc_, r.body, r.end, [&](const RecordHeader& d) {
                        if (d.type == kRtProgTags) ParseProgTags(doc_, d.body, d.end, raw.numbering);
                        return true;
                    });
                    break;
                default:
                    break;
            }
            return true;
        });
        return raw;
    }

    // A group (SpgrContainer): its own shape first - its place and its
    // members' coordinate space - then its members, placed in master units.
    Placed ReadGroup(const RecordHeader& group, int depth) const {
        Placed placed;
        placed.group = true;
        if (depth > kMaxDepth) return placed;
        bool first = true;
        ForEachRecord(doc_, group.body, group.end, [&](const RecordHeader& r) {
            if (r.type == kOaSpContainer) {
                RawShape raw = ReadShapeContainer(r);
                if (first) {
                    placed.raw = std::move(raw);
                    first = false;
                    return true;
                }
                if (raw.flags & 0x8) return true;   // deleted
                Placed shape;
                shape.raw = std::move(raw);
                placed.members.push_back(std::move(shape));
            } else if (r.type == kOaSpgrContainer) {
                first = false;
                placed.members.push_back(ReadGroup(r, depth + 1));
            }
            return placed.members.size() < 100000;
        });
        return placed;
    }

    // Places a member's box (its anchor, or its child anchor in its group's
    // space) on the sheet, and its own members inside it.
    static void PlaceTree(Placed& p, const Placed* parent) {
        const RawShape& raw = p.raw;
        if (parent && raw.hasChildAnchor) {
            const double* g = parent->raw.groupSpace;
            const double sx = parent->w / std::max(1.0, g[2] - g[0]);
            const double sy = parent->h / std::max(1.0, g[3] - g[1]);
            p.x = parent->x + (raw.child[0] - g[0]) * sx;
            p.y = parent->y + (raw.child[1] - g[1]) * sy;
            p.w = (raw.child[2] - raw.child[0]) * sx;
            p.h = (raw.child[3] - raw.child[1]) * sy;
        } else if (raw.hasAnchor) {
            p.x = raw.anchor[0];
            p.y = raw.anchor[1];
            p.w = raw.anchor[2] - raw.anchor[0];
            p.h = raw.anchor[3] - raw.anchor[1];
        }
        // A shape turned by 45 to 135 degrees (or 225 to 315) keeps its box
        // turned: the upright one has the width and height swapped.
        double turn = std::fmod(raw.props.rotation, 360.0);
        if (turn < 0) turn += 360.0;
        if ((turn >= 45 && turn < 135) || (turn >= 225 && turn < 315)) {
            const double cx = p.x + p.w / 2, cy = p.y + p.h / 2;
            std::swap(p.w, p.h);
            p.x = cx - p.w / 2;
            p.y = cy - p.h / 2;
        }
        for (Placed& member : p.members) PlaceTree(member, &p);
    }

    void Emit(Placed& placed, const Sheet& sheet, std::vector<PptxShape>& out, std::map<int, PptxShape>* byType,
              int depth) {
        if (depth == 0) PlaceTree(placed, nullptr);
        if (depth > kMaxDepth || out.size() > 100000) return;
        if (placed.group) {
            PptxShape table;
            if (placed.raw.props.tableProperties & 1 && BuildTable(placed, sheet, table)) {
                out.push_back(std::move(table));
                return;
            }
            for (Placed& member : placed.members) Emit(member, sheet, out, byType, depth + 1);
            return;
        }
        PptxShape shape;
        int placeholderType = -1;
        if (!BuildShape(placed, sheet, shape, placeholderType)) return;
        if (byType && placeholderType >= 0) byType->emplace(placeholderType, shape);
        out.push_back(std::move(shape));
    }

    // ----- One shape -----

    // The model's placeholder type for an OEPlaceholderAtom type; empty for
    // none, "-" for one left out (notes page picture, header).
    static std::string PlaceholderName(int type) {
        switch (type) {
            case 0x01: case 0x0D: case 0x11: return "title";
            case 0x03: case 0x0F: return "ctrTitle";
            case 0x04: case 0x10: return "subTitle";
            case 0x02: case 0x0E: case 0x12: case 0x13: case 0x19: return "body";
            case 0x06: case 0x0C: return "notes";
            case 0x07: return "dt";
            case 0x08: return "sldNum";
            case 0x09: return "ftr";
            case 0x14: return "chart";
            case 0x15: return "tbl";
            case 0x16: return "clipArt";
            case 0x17: return "dgm";
            case 0x18: return "media";
            case 0x1A: return "pic";
            case 0x05: case 0x0A: case 0x0B: return "-";
            default: return std::string();
        }
    }

    // The master's placeholder a slide's takes its place from.
    static const PptxShape* MasterPlaceholder(const MasterInfo* master, const std::string& name) {
        if (!master) return nullptr;
        const int wanted[2] = {name == "title" ? 0x01 : name == "ctrTitle" ? 0x03 : name == "subTitle" ? 0x04
                                   : name == "dt" ? 0x07 : name == "sldNum" ? 0x08 : name == "ftr" ? 0x09 : 0x02,
                               name == "ctrTitle" ? 0x01 : name == "subTitle" ? 0x02 : -1};
        for (int type : wanted) {
            if (type < 0) continue;
            const auto found = master->byPlaceholderType.find(type);
            if (found != master->byPlaceholderType.end()) return &found->second;
        }
        return nullptr;
    }

    std::string PictureTarget(uint32_t pib) {
        if (pib == 0 || pib > blips_.size() || !blips_[pib - 1].stream) return std::string();
        if (const auto found = pictureTargets_.find(pib); found != pictureTargets_.end()) return found->second;
        const OfficeArt::BlipRef& blip = blips_[pib - 1];
        const uint16_t type = U16(*blip.stream, blip.at + 2);
        const char* ext = type == 0xF01E ? ".png" : (type == 0xF01D || type == 0xF02A) ? ".jpeg"
                        : type == 0xF01A ? ".emf" : type == 0xF01B ? ".wmf" : nullptr;
        if (!ext) return std::string();
        const std::string target = "ppt/media/image" + std::to_string(pib) + ext;
        pictureTargets_[pib] = target;
        return target;
    }
    std::map<uint32_t, std::string> pictureTargets_;

    void ApplyFillAndLine(const OfficeArt::ShapeProperties& p, bool fillByDefault, bool lineByDefault,
                          const Scheme& scheme, PptxShape& shape) const {
        const bool filled = p.filledSet ? p.filled : fillByDefault;
        if (filled && p.fillOpacity > 0.0 && p.fillType != 9) {
            std::string colour = ColourRef(p.fillColor, scheme);
            if (colour.empty()) colour = "FFFFFF";
            shape.fill = colour;
            const bool linear = p.fillType == 4 || p.fillType == 7;
            const bool round = p.fillType == 5 || p.fillType == 6;
            const std::string back = ColourRef(p.fillBackColor, scheme);
            if ((linear || round) && !back.empty()) {
                RichShape shade;
                WordFormatInternal::ApplyOfficeShade(shade, "#" + colour, "#" + back, p.fillFocus, p.fillAngle, round);
                for (RichShape::GradientStop stop : shade.gradientStops) {
                    if (!stop.color.empty() && stop.color[0] == '#') stop.color.erase(0, 1);
                    shape.gradientStops.push_back(stop);
                }
                shape.gradientAngleDeg = shade.gradientAngleDeg;
                shape.gradientRadial = round;
                if (!shape.gradientStops.empty()) shape.fill = shape.gradientStops.front().color;
            }
        }
        const bool lined = p.linedSet ? p.lined : lineByDefault;
        if (lined) {
            std::string colour = ColourRef(p.lineColor, scheme);
            shape.line = colour.empty() ? "000000" : colour;
            shape.lineWidthPt = std::max(0.25, p.lineWidth / 12700.0);
            if (p.lineDashing == 2 || p.lineDashing == 5) shape.dash = RichShape::Dash::Dotted;
            else if (p.lineDashing != 0) shape.dash = RichShape::Dash::Dashed;
            shape.arrowStart = p.arrowStart;
            shape.arrowEnd = p.arrowEnd;
        }
    }

    // A freeform's outline - unless its points are formulas (pGuides, which
    // LibreOffice writes for its own shapes) or lie outside its geometry's
    // space: such an outline is left out, and the shape drawn as its box.
    static std::vector<RichShape::PathPoint> Freeform(const OfficeArt::ShapeProperties& p) {
        if (p.geoRight <= p.geoLeft || p.geoBottom <= p.geoTop) return {};
        std::vector<RichShape::PathPoint> path = OfficeArt::FreeformPath(p);
        auto inside = [](float v) { return std::isfinite(v) && v >= -1.0f && v <= 2.0f; };
        for (const RichShape::PathPoint& point : path) {
            if (point.op == RichShape::PathPoint::Op::Close) continue;
            if (!inside(point.x) || !inside(point.y)) return {};
            if (point.op == RichShape::PathPoint::Op::Curve &&
                (!inside(point.c1x) || !inside(point.c1y) || !inside(point.c2x) || !inside(point.c2y)))
                return {};
        }
        return path;
    }

    static PptxAnchor AnchorOf(int anchorText) {
        switch (anchorText) {
            case 1: case 4: case 7: case 9: return PptxAnchor::Middle;
            case 2: case 5: return PptxAnchor::Bottom;
            default: return PptxAnchor::Top;
        }
    }

    bool BuildShape(Placed& placed, const Sheet& sheet, PptxShape& shape, int& placeholderType) {
        const RawShape& raw = placed.raw;
        const OfficeArt::ShapeProperties& p = raw.props;
        if (raw.flags & 0x408) return false;              // deleted, or the background
        placeholderType = raw.placeholderType;
        const std::string ph = PlaceholderName(raw.placeholderType);
        if (ph == "-") return false;
        const bool furniture = ph == "dt" || ph == "sldNum" || ph == "ftr";
        if (furniture && !sheet.design) return false;

        shape.name = p.name.empty() ? "Shape " + std::to_string(raw.spid & 0x3FF) : p.name;
        shape.x = Emu(placed.x);
        shape.y = Emu(placed.y);
        shape.width = Emu(std::max(0.0, placed.w));
        shape.height = Emu(std::max(0.0, placed.h));
        shape.flipH = (raw.flags & 0x40) != 0;
        shape.flipV = (raw.flags & 0x80) != 0;
        // OfficeArt turns a shape before it mirrors it, DrawingML after: a
        // shape mirrored one way turns the other way round.
        double turn = std::fmod(p.rotation, 360.0);
        if (turn < 0) turn += 360.0;
        if (shape.flipH != shape.flipV && turn != 0.0) turn = 360.0 - turn;
        shape.rotationDeg = turn;

        const bool picture = p.pib > 0 && (raw.type == 75 || raw.type == 0 || raw.type == 1 || raw.type == 202) &&
                             raw.text.empty() && raw.outlineRef < 0;
        const bool connector = (raw.flags & 0x100) != 0 || raw.type == 20 || (raw.type >= 32 && raw.type <= 40);
        if (!ph.empty() && !picture) {
            shape.placeholder = ph == "notes" ? "body" : ph;
            if (noteShapes_ && ph == "notes") shape.placeholder = "notes";
            shape.kind = ph == "title" || ph == "ctrTitle" ? PptxShapeKind::Title
                       : ph == "subTitle"                  ? PptxShapeKind::Subtitle
                       : furniture                         ? PptxShapeKind::Text
                       : PptxShapeKind::Body;
            if (!raw.hasAnchor && !raw.hasChildAnchor) {
                if (const PptxShape* m = MasterPlaceholder(sheet.master, ph)) {
                    shape.x = m->x;
                    shape.y = m->y;
                    shape.width = m->width;
                    shape.height = m->height;
                    shape.placeInherited = true;
                }
            }
        } else if (picture) {
            shape.kind = PptxShapeKind::Picture;
            shape.pictureTarget = PictureTarget(p.pib);
            shape.altText = p.description;
            shape.cropLeft = std::clamp(p.cropLeft, 0.0, 0.99);
            shape.cropTop = std::clamp(p.cropTop, 0.0, 0.99);
            shape.cropRight = std::clamp(p.cropRight, 0.0, 0.99);
            shape.cropBottom = std::clamp(p.cropBottom, 0.0, 0.99);
            if (!ph.empty()) shape.placeholder = ph;
        } else if (connector) {
            shape.kind = PptxShapeKind::Connector;
        }

        // The outline.
        if (!p.vertices.empty() && raw.type == 0) {
            shape.customPath = Freeform(p);
        } else if (const char* preset = OfficeArt::PresetOfShapeType(raw.type)) {
            const std::string name = preset;
            if (name != "rect") shape.geometry = name;
        } else if (!p.vertices.empty()) {
            shape.customPath = Freeform(p);
        }
        if (connector && shape.geometry.empty() && shape.customPath.empty()) shape.geometry = "line";

        // Fill and line: a text box, a placeholder and a picture have none
        // unless they say so; a drawn shape has [MS-ODRAW]'s defaults.
        const bool plain = !ph.empty() || picture || raw.type == 202;
        ApplyFillAndLine(p, !plain && !connector, !plain, sheet.scheme, shape);
        if (connector) shape.fill.clear();

        // The text frame.
        if (p.textInsetsSet) {
            shape.insetLeft = std::max(0, p.textLeft);
            shape.insetTop = std::max(0, p.textTop);
            shape.insetRight = std::max(0, p.textRight);
            shape.insetBottom = std::max(0, p.textBottom);
        }
        shape.anchor = AnchorOf(p.anchorText);
        if (p.wrapText == 2) shape.wrap = false;
        if (p.textFlow == 1 || p.textFlow == 5) shape.textRotation = 90;
        else if (p.textFlow == 2) shape.textRotation = 270;

        // The text: in the shape, or the outline text it points at.
        const TextBlock* block = raw.text.empty() ? nullptr : &raw.text.front();
        if (raw.outlineRef >= 0 && sheet.outline && static_cast<size_t>(raw.outlineRef) < sheet.outline->size())
            block = &(*sheet.outline)[static_cast<size_t>(raw.outlineRef)];
        int textType = 4;
        if (block) textType = block->type;
        else if (ph == "title") textType = 0;
        else if (ph == "ctrTitle") textType = 6;
        else if (ph == "subTitle") textType = 5;
        else if (ph == "body") textType = 1;
        else if (ph == "notes") textType = 2;
        if (block && !block->text.empty()) {
            shape.paragraphs = Paragraphs(*block, sheet, raw.numbering);
        } else if (!ph.empty() && !picture) {
            // An empty placeholder: one empty paragraph in its look, for an editor to type into.
            TextBlock empty;
            empty.type = textType;
            PptxParagraph para = Paragraph(empty, 0, 0, 0, sheet, nullptr);
            PptxRun run = Run(empty, 0, sheet, para.level, CharFormat{}, false);
            para.runs.push_back(std::move(run));
            shape.paragraphs.push_back(std::move(para));
        }
        if (shape.kind == PptxShapeKind::Text && shape.paragraphs.empty() && shape.fill.empty() && shape.line.empty() &&
            shape.customPath.empty())
            return false;   // nothing to draw or read
        return true;
    }

    // ----- Text -----

    // The paragraph and character look of text type `type` at `level` before
    // the run's own: the document's defaults, the master's base style for
    // the type, then the master's own for a body or title variant.
    void Inherited(int type, int level, const Sheet& sheet, ParaFormat& pf, CharFormat& cf) const {
        const size_t l = static_cast<size_t>(std::clamp(level, 0, kLevels - 1));
        const int base = BaseTextType(type);
        auto apply = [&](const TextStyles* styles, int t) {
            if (!styles || t < 0 || t > 8 || !(*styles)[static_cast<size_t>(t)].present) return;
            pf.Over((*styles)[static_cast<size_t>(t)].pf[l]);
            cf.Over((*styles)[static_cast<size_t>(t)].cf[l]);
        };
        apply(&docStyles_, 4);
        if (base != 4) apply(&docStyles_, base);
        apply(sheet.styles, base);
        if (type >= 5 && type <= 8) apply(sheet.styles, type);
    }

    static const char* NumberScheme(int scheme) {
        static const char* kSchemes[] = {"alphaLcPeriod", "alphaUcPeriod", "arabicParenR", "arabicPeriod",
                                         "romanLcParenBoth", "romanLcParenR", "romanLcPeriod", "romanUcPeriod",
                                         "alphaLcParenBoth", "alphaLcParenR", "alphaUcParenBoth", "alphaUcParenR",
                                         "arabicParenBoth", "arabicPlain", "romanUcParenBoth", "romanUcParenR"};
        if (scheme < 0 || scheme >= static_cast<int>(std::size(kSchemes))) return "arabicPeriod";
        return kSchemes[scheme];
    }

    // The paragraph run covering character `at`.
    static const PFRun* PFAt(const TextBlock& b, size_t at) {
        uint64_t start = 0;
        for (const PFRun& run : b.pf) {
            if (at < start + run.count) return &run;
            start += run.count;
        }
        return b.pf.empty() ? nullptr : &b.pf.back();
    }

    static int MasterLevelAt(const TextBlock& b, size_t at) {
        uint64_t start = 0;
        for (const auto& [count, level] : b.masterLevels) {
            if (at < start + count) return level;
            start += count;
        }
        return -1;
    }

    PptxParagraph Paragraph(const TextBlock& b, size_t from, size_t index, int, const Sheet& sheet,
                            const std::vector<Numbering>* numbering) const {
        PptxParagraph para;
        const PFRun* run = PFAt(b, from);
        int level = run ? run->level : 0;
        const int masterLevel = MasterLevelAt(b, from);
        if (masterLevel >= 0) level = masterLevel;
        level = std::clamp(level, 0, kLevels - 1);
        para.level = level;
        ParaFormat pf;
        CharFormat cf;
        Inherited(b.type, level, sheet, pf, cf);
        if (run) pf.Over(run->pf);
        const size_t l = static_cast<size_t>(level);
        // The ruler's margins where the paragraph's look says none.
        if (!(pf.has & ParaFormat::kLeftMargin) && b.ruler.hasMargin[l]) {
            pf.leftMargin = b.ruler.leftMargin[l];
            pf.has |= ParaFormat::kLeftMargin;
        }
        if (!(pf.has & ParaFormat::kIndent) && b.ruler.hasIndent[l]) {
            pf.indent = b.ruler.indent[l];
            pf.has |= ParaFormat::kIndent;
        }
        switch (pf.align) {
            case 1: para.align = PptxAlign::Center; break;
            case 2: para.align = PptxAlign::Right; break;
            case 3: case 4: case 5: case 6: para.align = PptxAlign::Justify; break;
            default: para.align = PptxAlign::Left; break;
        }
        auto spacing = [](int value, bool set, PptxSpacing& out) {
            if (!set) return;
            if (value >= 0) out.percent = static_cast<float>(value) / 100.0f;
            else out.points = static_cast<float>(-value) / 8.0f;   // master units: 8 to the point
        };
        spacing(pf.lineSpacing, (pf.has & ParaFormat::kLineSpacing) != 0, para.lineSpacing);
        if (pf.spaceBefore != 0) spacing(pf.spaceBefore, (pf.has & ParaFormat::kSpaceBefore) != 0, para.spaceBefore);
        if (pf.spaceAfter != 0) spacing(pf.spaceAfter, (pf.has & ParaFormat::kSpaceAfter) != 0, para.spaceAfter);
        if (pf.has & ParaFormat::kLeftMargin) {
            para.marginLeft = Emu(std::max(0, pf.leftMargin));
            if (pf.has & ParaFormat::kIndent) para.indent = Emu(pf.indent - pf.leftMargin);
        }
        para.bullet = pf.hasBullet;
        if (para.bullet) {
            std::string ch;
            AppendUtf8(ch, pf.bulletChar ? pf.bulletChar : 0x2022);
            para.bulletChar = ch;
            if (pf.bulletHasFont) para.bulletFont = FontName(pf.bulletFontRef);
            if (pf.bulletHasColor) para.bulletColor = Resolve(pf.bulletColor, sheet.scheme);
            if (pf.bulletHasSize && pf.bulletSize > 0 && pf.bulletSize != 100)
                para.bulletSizePercent = static_cast<float>(std::clamp(pf.bulletSize, 25, 400));
            if (numbering && index < numbering->size() && (*numbering)[index].on) {
                para.numbered = true;
                para.numberScheme = NumberScheme((*numbering)[index].scheme);
                para.numberStartAt = (*numbering)[index].startAt;
                para.bulletChar.clear();
            }
        }
        return para;
    }

    PptxRun Run(const TextBlock& b, size_t, const Sheet& sheet, int level, const CharFormat& own, bool) const {
        ParaFormat pf;
        CharFormat cf;
        cf.color.index = 1;   // the scheme's text colour unless something says otherwise
        Inherited(b.type, level, sheet, pf, cf);
        cf.Over(own);
        PptxRun run;
        run.bold = cf.bold;
        run.italic = cf.italic;
        run.underline = cf.underline;
        run.sizePt = cf.size > 0 && cf.size < 4000 ? static_cast<float>(cf.size) : 18.0f;
        run.color = Resolve(cf.color, sheet.scheme);
        run.font = FontName(cf.fontRef);
        run.baseline = std::clamp(cf.position, -100, 100);
        return run;
    }

    std::vector<PptxParagraph> Paragraphs(const TextBlock& b, const Sheet& sheet,
                                          const std::vector<Numbering>& numbering) const {
        std::vector<PptxParagraph> out;
        const std::vector<Numbering>* nums = !numbering.empty() ? &numbering : (!b.numbering.empty() ? &b.numbering : nullptr);
        const size_t length = b.text.size();
        size_t start = 0;
        size_t index = 0;
        while (start <= length && out.size() < kMaxRuns) {
            size_t end = start;
            while (end < length && b.text[end] != 0x0D) ++end;
            PptxParagraph para = Paragraph(b, start, index, 0, sheet, nums);
            // Its runs: where the character runs and the links change.
            std::vector<size_t> cuts = {start, end};
            uint64_t at = 0;
            for (const CFRun& run : b.cf) {
                at += run.count;
                if (at > start && at < end) cuts.push_back(static_cast<size_t>(at));
                if (at >= end) break;
            }
            for (const LinkRange& link : b.links) {
                if (link.begin > start && link.begin < end) cuts.push_back(link.begin);
                if (link.end > start && link.end < end) cuts.push_back(link.end);
            }
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
            for (size_t i = 0; i + 1 < cuts.size(); ++i) {
                const size_t from = cuts[i], to = cuts[i + 1];
                const std::string text = Utf16ToUtf8(b.text, from, to);
                if (text.empty()) continue;
                CharFormat own;
                uint64_t runStart = 0;
                for (const CFRun& run : b.cf) {
                    if (from < runStart + run.count) { own = run.cf; break; }
                    runStart += run.count;
                }
                PptxRun run = Run(b, from, sheet, para.level, own, false);
                run.text = text;
                for (const LinkRange& link : b.links)
                    if (from >= link.begin && from < link.end) run.hyperlink = link.target;
                // A line break is a run of its own, as the other readers keep it.
                size_t newline;
                while ((newline = run.text.find('\n')) != std::string::npos) {
                    PptxRun before = run;
                    before.text = run.text.substr(0, newline);
                    if (!before.text.empty()) para.runs.push_back(before);
                    PptxRun lineBreak = run;
                    lineBreak.text = "\n";
                    para.runs.push_back(lineBreak);
                    run.text.erase(0, newline + 1);
                }
                if (!run.text.empty()) para.runs.push_back(std::move(run));
            }
            out.push_back(std::move(para));
            if (end >= length) break;
            start = end + 1;
            ++index;
        }
        // The text ends with its last paragraph's mark, not an empty paragraph after it.
        if (out.size() > 1 && out.back().runs.empty() && !b.text.empty() && b.text.back() == 0x0D) out.pop_back();
        return out;
    }

    // ----- Tables -----

    // A group marked as a table: its members with text or a fill are the
    // cells, laid on the grid their edges make; its lines are the borders.
    bool BuildTable(Placed& group, const Sheet& sheet, PptxShape& table) {
        std::vector<const Placed*> cells, lines;
        for (const Placed& m : group.members) {
            if (m.group) return false;
            const bool line = m.raw.type == 20 || (m.raw.flags & 0x100);
            if (line) lines.push_back(&m);
            else cells.push_back(&m);
        }
        if (cells.empty() || cells.size() > 10000) return false;
        auto key = [](double v) { return static_cast<int64_t>(std::llround(v)); };
        std::vector<int64_t> xs, ys;
        for (const Placed* c : cells) {
            xs.push_back(key(c->x));
            xs.push_back(key(c->x + c->w));
            ys.push_back(key(c->y));
            ys.push_back(key(c->y + c->h));
        }
        auto unique = [](std::vector<int64_t>& v) {
            std::sort(v.begin(), v.end());
            // Edges a unit or two apart are one edge.
            std::vector<int64_t> out;
            for (int64_t e : v)
                if (out.empty() || e - out.back() > 2) out.push_back(e);
            v = std::move(out);
        };
        unique(xs);
        unique(ys);
        if (xs.size() < 2 || ys.size() < 2) return false;
        const size_t columns = xs.size() - 1, rows = ys.size() - 1;
        if (columns * rows > 10000) return false;
        auto indexOf = [](const std::vector<int64_t>& edges, int64_t v) -> int {
            for (size_t i = 0; i < edges.size(); ++i)
                if (std::llabs(edges[i] - v) <= 2) return static_cast<int>(i);
            return -1;
        };
        table.kind = PptxShapeKind::Table;
        table.name = group.raw.props.name.empty() ? "Table" : group.raw.props.name;
        table.x = Emu(group.x);
        table.y = Emu(group.y);
        table.width = Emu(group.w);
        table.height = Emu(group.h);
        table.rows.assign(rows, std::vector<std::vector<PptxParagraph>>(columns));
        table.cellStyles.assign(rows, std::vector<PptxCellStyle>(columns));
        for (size_t c = 0; c < columns; ++c) table.columnWidths.push_back(Emu(static_cast<double>(xs[c + 1] - xs[c])));
        for (size_t r = 0; r < rows; ++r) table.rowHeights.push_back(Emu(static_cast<double>(ys[r + 1] - ys[r])));
        std::vector<std::vector<bool>> filled(rows, std::vector<bool>(columns, false));
        for (const Placed* c : cells) {
            const int c0 = indexOf(xs, key(c->x)), c1 = indexOf(xs, key(c->x + c->w));
            const int r0 = indexOf(ys, key(c->y)), r1 = indexOf(ys, key(c->y + c->h));
            if (c0 < 0 || r0 < 0 || c1 <= c0 || r1 <= r0) continue;
            const size_t row = static_cast<size_t>(r0), col = static_cast<size_t>(c0);
            if (filled[row][col]) continue;
            PptxCellStyle& style = table.cellStyles[row][col];
            style.gridSpan = c1 - c0;
            style.rowSpan = r1 - r0;
            for (int r = r0; r < r1; ++r)
                for (int k = c0; k < c1; ++k) {
                    filled[static_cast<size_t>(r)][static_cast<size_t>(k)] = true;
                    if (r != r0 || k != c0) table.cellStyles[static_cast<size_t>(r)][static_cast<size_t>(k)].mergedAway = true;
                }
            PptxShape look;
            ApplyFillAndLine(c->raw.props, false, false, sheet.scheme, look);
            style.fill = look.fill;
            const OfficeArt::ShapeProperties& p = c->raw.props;
            if (p.textInsetsSet) {
                style.insetLeft = std::max(0, p.textLeft);
                style.insetTop = std::max(0, p.textTop);
                style.insetRight = std::max(0, p.textRight);
                style.insetBottom = std::max(0, p.textBottom);
            }
            style.anchor = AnchorOf(p.anchorText);
            const TextBlock* block = c->raw.text.empty() ? nullptr : &c->raw.text.front();
            if (block && !block->text.empty()) table.rows[row][col] = Paragraphs(*block, sheet, c->raw.numbering);
            else table.rows[row][col].push_back(PptxParagraph{});
        }
        for (size_t r = 0; r < rows; ++r)
            for (size_t k = 0; k < columns; ++k)
                if (table.rows[r][k].empty()) table.rows[r][k].push_back(PptxParagraph{});
        // The borders: each line lies along an edge of the grid.
        for (const Placed* l : lines) {
            PptxShape look;
            ApplyFillAndLine(l->raw.props, false, true, sheet.scheme, look);
            if (look.line.empty()) continue;
            PptxCellStyle::Border border{look.line, look.lineWidthPt};
            const int64_t x0 = key(std::min(l->x, l->x + l->w)), x1 = key(std::max(l->x, l->x + l->w));
            const int64_t y0 = key(std::min(l->y, l->y + l->h)), y1 = key(std::max(l->y, l->y + l->h));
            if (y1 - y0 <= 2) {                               // across
                const int edge = indexOf(ys, y0);
                if (edge < 0) continue;
                for (size_t k = 0; k < columns; ++k) {
                    if (xs[k] < x0 - 2 || xs[k + 1] > x1 + 2) continue;
                    if (edge > 0) table.cellStyles[static_cast<size_t>(edge) - 1][k].bottom = border;
                    if (static_cast<size_t>(edge) < rows) table.cellStyles[static_cast<size_t>(edge)][k].top = border;
                }
            } else if (x1 - x0 <= 2) {                        // down
                const int edge = indexOf(xs, x0);
                if (edge < 0) continue;
                for (size_t r = 0; r < rows; ++r) {
                    if (ys[r] < y0 - 2 || ys[r + 1] > y1 + 2) continue;
                    if (edge > 0) table.cellStyles[r][static_cast<size_t>(edge) - 1].right = border;
                    if (static_cast<size_t>(edge) < columns) table.cellStyles[r][static_cast<size_t>(edge)].left = border;
                }
            }
        }
        return true;
    }
};

bool ReadFromCompoundFile(const UCCompoundFileReader& cfb, PptxPresentation& out, std::string& error,
                          bool withPictures) {
    Bytes document;
    if (!cfb.ReadStream("PowerPoint Document", document) || document.empty()) {
        error = cfb.HasStream("WordDocument")
                    ? "The file is a Word 97-2003 document, not a PowerPoint presentation"
                    : "The file is not a PowerPoint 97-2003 presentation: it has no \"PowerPoint Document\" stream";
        return false;
    }
    Bytes currentUser, pictures;
    const bool haveUser = cfb.ReadStream("Current User", currentUser);
    const bool havePictures = cfb.ReadStream("Pictures", pictures);
    PptReader reader(document, havePictures ? &pictures : nullptr, withPictures);
    return reader.Read(haveUser ? &currentUser : nullptr, out, error);
}

} // namespace

bool IsPptExtension(const std::string& extensionOrPath) {
    const size_t dot = extensionOrPath.rfind('.');
    std::string ext = dot == std::string::npos ? extensionOrPath : extensionOrPath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "ppt" || ext == "pps" || ext == "pot";
}

bool IsPptPresentationFile(const std::string& filePath) {
    if (!UCCompoundFileReader::HasSignature(filePath)) return false;
    UCCompoundFileReader cfb;
    return cfb.Open(filePath) && cfb.HasStream("PowerPoint Document");
}

bool ReadPptPresentationFromMemory(std::vector<uint8_t> data, PptxPresentation& out, std::string& error,
                                   bool withPictures) {
    out = PptxPresentation{};
    error.clear();
    UCCompoundFileReader cfb;
    if (!UCCompoundFileReader::HasSignature(data) || !cfb.OpenFromMemory(std::move(data))) {
        error = "The file is not a valid PowerPoint 97-2003 presentation (.ppt)";
        return false;
    }
    if (!ReadFromCompoundFile(cfb, out, error, withPictures)) {
        out = PptxPresentation{};
        return false;
    }
    return true;
}

bool ReadPptPresentation(const std::string& filePath, PptxPresentation& out, std::string& error, bool withPictures) {
    out = PptxPresentation{};
    error.clear();
    UCCompoundFileReader cfb;
    if (!cfb.Open(filePath)) {
        error = "The file is not a valid PowerPoint 97-2003 presentation (.ppt): " + filePath;
        return false;
    }
    if (!ReadFromCompoundFile(cfb, out, error, withPictures)) {
        out = PptxPresentation{};
        error += ": " + filePath;
        return false;
    }
    return true;
}

} // namespace UltraCanvas
