// Plugins/Documents/Word/UltraCanvasDocLegacyFormat.cpp
// Import for legacy Word 97-2003 (.doc) files, with formatting.
// A .doc is an OLE2 Compound File Binary (CFB): a mini filesystem of
// FAT-chained sectors holding named streams. The document text lives in the
// "WordDocument" stream as pieces described by the piece table (Clx/PlcPcd)
// stored in the "0Table"/"1Table" stream — each piece is either 8-bit
// CP1252 or UTF-16LE. Formatting is stored beside the text, not in it:
//   - character runs in CHPX FKPs (PlcBteChpx), paragraphs in PAPX FKPs
//     (PlcBtePapx), both as SPRM lists keyed by file offset;
//   - named styles in the stylesheet (STSH), each based on another;
//   - list definitions in PlfLst/PlfLfo (bullet or number, start value);
//   - tables as paragraphs flagged "in table", cells closed by 0x07 and rows
//     by a table-terminating paragraph that carries the column edges.
// All of it is mapped onto UCRichDocument: headings, bold/italic/underline/
// strike, super/subscript, font, size and colour, alignment, bullet and
// numbered lists (numbering runs on across interruptions), tables with
// column widths and cell alignment, hyperlinks and embedded PNG/JPEG
// pictures. There is deliberately no .doc writer — save as .docx or .odt.
// Specification: [MS-DOC] Word (.doc) Binary File Format.
// Version: 2.0.0
// Last Modified: 2026-09-25
// Author: UltraCanvas Framework

#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasWordFormatInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

constexpr uint32_t kMaxChainLength = 1u << 22;   // loop guard for corrupt FAT chains

uint16_t ReadU16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8)
         | (static_cast<uint32_t>(data[offset + 2]) << 16)
         | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

// ===== COMPOUND FILE BINARY READER =====

class CfbReader {
public:
    bool Load(const std::string& filePath, std::string& error) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            error = "Cannot open file: " + filePath;
            return false;
        }
        data_.assign((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
        static const uint8_t magic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
        if (data_.size() < 512 || std::memcmp(data_.data(), magic, 8) != 0) {
            error = "Not an OLE2 compound file";
            return false;
        }

        uint16_t sectorShift = ReadU16(data_, 0x1E);
        if (sectorShift < 7 || sectorShift > 20) {
            error = "Invalid compound file sector size";
            return false;
        }
        sectorSize_ = static_cast<size_t>(1) << sectorShift;
        miniSectorSize_ = static_cast<size_t>(1) << ReadU16(data_, 0x20);
        miniCutoff_ = ReadU32(data_, 0x38);

        if (!LoadFat() || !LoadDirectory(ReadU32(data_, 0x30))) {
            error = "Corrupt compound file structure";
            return false;
        }
        LoadMiniFat(ReadU32(data_, 0x3C));
        // The mini stream is the root entry's own stream, read via the
        // regular FAT regardless of its size.
        if (!dirs_.empty()) {
            ReadChain(dirs_[0].startSector, dirs_[0].size, miniStream_);
        }
        return true;
    }

    // Reads a named stream (case-sensitive, as Word writes fixed names).
    bool ReadStream(const std::string& name, std::vector<uint8_t>& out) const {
        for (size_t i = 1; i < dirs_.size(); ++i) {
            if (dirs_[i].type == 2 && dirs_[i].name == name) {
                if (dirs_[i].size < miniCutoff_) {
                    return ReadMiniChain(dirs_[i].startSector, dirs_[i].size, out);
                }
                return ReadChain(dirs_[i].startSector, dirs_[i].size, out);
            }
        }
        return false;
    }

private:
    std::vector<uint8_t> data_;
    size_t sectorSize_ = 512;
    size_t miniSectorSize_ = 64;
    size_t miniCutoff_ = 4096;
    std::vector<uint32_t> fat_;
    std::vector<uint32_t> miniFat_;
    std::vector<uint8_t> miniStream_;

    struct DirEntry {
        std::string name;      // decoded to ASCII (stream names Word uses are ASCII)
        uint8_t type = 0;      // 1=storage, 2=stream, 5=root
        uint32_t startSector = 0;
        uint64_t size = 0;
    };
    std::vector<DirEntry> dirs_;

    size_t SectorOffset(uint32_t sector) const {
        return (static_cast<size_t>(sector) + 1) * sectorSize_;
    }

    void AppendFatSector(uint32_t sector) {
        size_t offset = SectorOffset(sector);
        for (size_t i = 0; i + 4 <= sectorSize_ && offset + i + 4 <= data_.size(); i += 4) {
            fat_.push_back(ReadU32(data_, offset + i));
        }
    }

    bool LoadFat() {
        // 109 DIFAT entries live in the header; more come from DIFAT sectors.
        for (int i = 0; i < 109; ++i) {
            uint32_t sector = ReadU32(data_, 0x4C + i * 4);
            if (sector >= 0xFFFFFFFE) break;
            AppendFatSector(sector);
        }
        uint32_t difatSector = ReadU32(data_, 0x44);
        uint32_t difatCount = ReadU32(data_, 0x48);
        for (uint32_t d = 0; d < difatCount && difatSector < 0xFFFFFFFE; ++d) {
            size_t offset = SectorOffset(difatSector);
            if (offset + sectorSize_ > data_.size()) break;
            size_t entries = sectorSize_ / 4 - 1;
            for (size_t i = 0; i < entries; ++i) {
                uint32_t sector = ReadU32(data_, offset + i * 4);
                if (sector < 0xFFFFFFFE) AppendFatSector(sector);
            }
            difatSector = ReadU32(data_, offset + entries * 4);
        }
        return !fat_.empty();
    }

    void LoadMiniFat(uint32_t firstSector) {
        uint32_t sector = firstSector;
        uint32_t guard = 0;
        while (sector < 0xFFFFFFFE && guard++ < kMaxChainLength) {
            size_t offset = SectorOffset(sector);
            if (offset + sectorSize_ > data_.size()) break;
            for (size_t i = 0; i + 4 <= sectorSize_; i += 4) {
                miniFat_.push_back(ReadU32(data_, offset + i));
            }
            sector = (sector < fat_.size()) ? fat_[sector] : 0xFFFFFFFE;
        }
    }

    bool LoadDirectory(uint32_t firstSector) {
        std::vector<uint8_t> dirData;
        if (!ReadChain(firstSector, SIZE_MAX, dirData)) return false;
        for (size_t offset = 0; offset + 128 <= dirData.size(); offset += 128) {
            DirEntry entry;
            uint16_t nameLen = ReadU16(dirData, offset + 0x40);
            if (nameLen >= 2 && nameLen <= 64) {
                for (size_t i = 0; i + 2 < static_cast<size_t>(nameLen); i += 2) {
                    uint16_t ch = ReadU16(dirData, offset + i);
                    entry.name.push_back(
                        (ch > 0 && ch < 128) ? static_cast<char>(ch) : '?');
                }
            }
            entry.type = dirData[offset + 0x42];
            entry.startSector = ReadU32(dirData, offset + 0x74);
            entry.size = ReadU32(dirData, offset + 0x78);   // v3: low 32 bits suffice
            dirs_.push_back(std::move(entry));
        }
        return !dirs_.empty();
    }

    // Follows a regular FAT chain; maxSize==SIZE_MAX reads the whole chain.
    bool ReadChain(uint32_t firstSector, uint64_t maxSize, std::vector<uint8_t>& out) const {
        out.clear();
        uint32_t sector = firstSector;
        uint32_t guard = 0;
        while (sector < 0xFFFFFFFE && guard++ < kMaxChainLength) {
            size_t offset = SectorOffset(sector);
            if (offset + sectorSize_ > data_.size()) break;
            out.insert(out.end(), data_.begin() + offset,
                       data_.begin() + offset + sectorSize_);
            if (maxSize != SIZE_MAX && out.size() >= maxSize) break;
            sector = (sector < fat_.size()) ? fat_[sector] : 0xFFFFFFFE;
        }
        if (maxSize != SIZE_MAX && out.size() > maxSize) out.resize(maxSize);
        return !out.empty();
    }

    bool ReadMiniChain(uint32_t firstSector, uint64_t maxSize, std::vector<uint8_t>& out) const {
        out.clear();
        uint32_t sector = firstSector;
        uint32_t guard = 0;
        while (sector < 0xFFFFFFFE && guard++ < kMaxChainLength) {
            size_t offset = static_cast<size_t>(sector) * miniSectorSize_;
            if (offset + miniSectorSize_ > miniStream_.size()) break;
            out.insert(out.end(), miniStream_.begin() + offset,
                       miniStream_.begin() + offset + miniSectorSize_);
            if (out.size() >= maxSize) break;
            sector = (sector < miniFat_.size()) ? miniFat_[sector] : 0xFFFFFFFE;
        }
        if (out.size() > maxSize) out.resize(maxSize);
        return !out.empty();
    }
};

// ===== TEXT DECODING =====

void AppendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

// CP1252 0x80-0x9F block (0 = unmapped control, dropped).
uint32_t Cp1252ToUnicode(uint8_t byte) {
    static const uint16_t highBlock[32] = {
        0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
        0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178
    };
    if (byte < 0x80) return byte;
    if (byte < 0xA0) return highBlock[byte - 0x80];
    return byte;   // 0xA0-0xFF match Latin-1
}


int16_t ReadI16(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<int16_t>(ReadU16(data, offset));
}

// ===== SPRMS =====
// A SPRM is a 16-bit opcode followed by its operand; bits 13-15 (spra) give
// the operand's size, with 6 meaning "a length byte follows".

constexpr uint16_t kSprmCFBold = 0x0835;
constexpr uint16_t kSprmCFItalic = 0x0836;
constexpr uint16_t kSprmCFStrike = 0x0837;
constexpr uint16_t kSprmCFVanish = 0x083C;
constexpr uint16_t kSprmCFDStrike = 0x2A53;
constexpr uint16_t kSprmCKul = 0x2A3E;
constexpr uint16_t kSprmCIco = 0x2A42;
constexpr uint16_t kSprmCIss = 0x2A48;
constexpr uint16_t kSprmCHps = 0x4A43;
constexpr uint16_t kSprmCRgFtc0 = 0x4A4F;
constexpr uint16_t kSprmCCv = 0x6870;
constexpr uint16_t kSprmCPicLocation = 0x6A03;
constexpr uint16_t kSprmCSymbol = 0x6A09;
constexpr uint16_t kSprmCHighlight = 0x2A0C;
constexpr uint16_t kSprmCShd80 = 0x4866;
constexpr uint16_t kSprmCShd = 0xCA71;
constexpr uint16_t kSprmPBrcTop80 = 0x6424;
constexpr uint16_t kSprmPBrcLeft80 = 0x6425;
constexpr uint16_t kSprmPBrcBottom80 = 0x6426;
constexpr uint16_t kSprmPBrcRight80 = 0x6427;
constexpr uint16_t kSprmPBrcTop = 0xC64E;
constexpr uint16_t kSprmPBrcLeft = 0xC64F;
constexpr uint16_t kSprmPBrcBottom = 0xC650;
constexpr uint16_t kSprmPBrcRight = 0xC651;
constexpr uint16_t kSprmPShd80 = 0x442D;
constexpr uint16_t kSprmPShd = 0xC64D;
constexpr uint16_t kSprmCFData = 0x0806;
constexpr uint16_t kSprmCFOle2 = 0x080A;
constexpr uint16_t kSprmCFSpec = 0x0855;
constexpr uint16_t kSprmPJc80 = 0x2403;
constexpr uint16_t kSprmPJc = 0x2461;
constexpr uint16_t kSprmPIlvl = 0x260A;
constexpr uint16_t kSprmPIlfo = 0x460B;
constexpr uint16_t kSprmPFInTable = 0x2416;
constexpr uint16_t kSprmPFTtp = 0x2417;
constexpr uint16_t kSprmPItap = 0x6649;
constexpr uint16_t kSprmPFInnerTtp = 0x244C;
constexpr uint16_t kSprmPOutLvl = 0x2640;
constexpr uint16_t kSprmPFPageBreakBefore = 0x2407;
constexpr uint16_t kSprmTDefTable = 0xD608;
constexpr uint16_t kSprmTTableHeader = 0x3404;
constexpr uint16_t kSprmTTableBorders80 = 0xD605;
constexpr uint16_t kSprmTTableBorders = 0xD613;
constexpr uint16_t kSprmTDefTableShd80 = 0xD609;
constexpr uint16_t kSprmTDefTableShd = 0xD612;
constexpr uint16_t kSprmTSetBrc80 = 0xD620;
constexpr uint16_t kSprmTSetBrc = 0xD62F;
constexpr uint16_t kSprmTJc90 = 0x5400;
constexpr uint16_t kSprmTJc = 0x548A;
constexpr uint16_t kSprmTDxaGapHalf = 0x9602;
constexpr uint16_t kSprmTCellPadding = 0xD632;
constexpr uint16_t kSprmTCellPaddingDefault = 0xD634;
constexpr uint16_t kSprmTVertAlign = 0xD62C;
constexpr uint16_t kSprmPChgTabs = 0xC615;
constexpr uint16_t kSprmPChgTabsPapx = 0xC60D;
constexpr uint16_t kSprmPDxaRight80 = 0x840E;
constexpr uint16_t kSprmPDxaLeft80 = 0x840F;
constexpr uint16_t kSprmPDxaLeft1_80 = 0x8411;
constexpr uint16_t kSprmPDxaRight = 0x845D;
constexpr uint16_t kSprmPDxaLeft = 0x845E;
constexpr uint16_t kSprmPDxaLeft1 = 0x8460;
constexpr uint16_t kSprmPDyaBefore = 0xA413;
constexpr uint16_t kSprmPDyaAfter = 0xA414;
constexpr uint16_t kSprmPDyaLine = 0x6412;

struct Sprm {
    uint16_t code = 0;
    size_t operand = 0;       // offset of the operand in the buffer
    size_t operandSize = 0;
};

// Walks a grpprl (SPRM list) in data[begin, end), calling visit for each.
template <typename Visit>
void ForEachSprm(const std::vector<uint8_t>& data, size_t begin, size_t end, Visit visit) {
    end = std::min(end, data.size());
    size_t pos = begin;
    while (pos + 2 <= end) {
        Sprm sprm;
        sprm.code = ReadU16(data, pos);
        pos += 2;
        switch ((sprm.code >> 13) & 7) {
            case 0: case 1: sprm.operandSize = 1; break;
            case 2: case 4: case 5: sprm.operandSize = 2; break;
            case 3: sprm.operandSize = 4; break;
            case 7: sprm.operandSize = 3; break;
            default:
                if (sprm.code == kSprmTDefTable) {
                    // A 16-bit length, counting itself minus one.
                    if (pos + 2 > end) return;
                    sprm.operandSize = ReadU16(data, pos) + 1;
                } else if (sprm.code == kSprmPChgTabs && pos < end && data[pos] == 255) {
                    // Oversized tab-change list: deletions (4 bytes each) then
                    // additions (3 bytes each), each with its own count.
                    if (pos + 2 > end) return;
                    size_t deletions = data[pos + 1];
                    size_t addAt = pos + 2 + deletions * 4;
                    if (addAt >= end) return;
                    sprm.operandSize = 2 + deletions * 4 + 1 + static_cast<size_t>(data[addAt]) * 3;
                } else {
                    if (pos >= end) return;
                    sprm.operandSize = static_cast<size_t>(data[pos]) + 1;
                }
                break;
        }
        sprm.operand = pos;
        if (pos + sprm.operandSize > end) return;
        visit(sprm);
        pos += sprm.operandSize;
    }
}

// ===== PROPERTIES =====

struct DocCharProps {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool hidden = false;
    bool special = false;         // fSpec: 0x01 is a picture, 0x13 a field, ...
    bool ole = false;             // the picture is an embedded OLE object
    int position = 0;             // 0 normal, 1 superscript, 2 subscript
    int halfPoints = 20;          // Word's default size, 10 pt
    int fontIndex = -1;           // into the font table
    std::string color;            // "#RRGGBB"; empty = automatic
    int32_t picLocation = -1;     // picture data offset in the Data stream
    // Insert > Symbol: the text holds a placeholder, the real character and
    // its font are here.
    int symbolFont = -1;
    uint16_t symbolChar = 0;
    std::string highlight;        // highlighter or character shading, "#RRGGBB"
};

struct DocParaProps {
    int istd = 0;
    int jc = 0;                   // 0 left, 1 centre, 2 right, 3+ justified
    int ilfo = 0;                 // 1-based list override; 0 = not a list
    int ilvl = 0;
    bool inTable = false;
    bool rowEnd = false;          // fTtp: the paragraph that closes a table row
    int tableDepth = 0;
    int outlineLevel = 9;         // 9 = body text
    bool pageBreakBefore = false;
    bool headerRow = false;
    std::vector<int> cellEdges;   // twips, one more than the row's cells
    // Geometry, in twips (1/20 pt). Word's defaults are all zero.
    int leftIndent = 0;
    int rightIndent = 0;
    int firstLineIndent = 0;
    int spaceBefore = 0;
    int spaceAfter = 0;
    float lineSpacing = 0.0f;     // multiple of single; 0 = single / exact height
    float lineHeightPt = 0.0f;    // exact / at-least height; 0 = none
    bool lineHeightAtLeast = false;
    RichBorder frame[4];          // top, bottom, left, right
    std::string background;
    struct Tab { int position = 0; int kind = 0; };   // kind: jc 0 left 1 centre 2 right 3 decimal
    std::vector<Tab> tabs;
    // Table row (the row-ending paragraph): per-cell borders and fill, and
    // the table-wide borders cells fall back to. A side with widthPt < 0 is
    // not stated by the cell.
    struct CellFormat {
        RichBorder top{-1.0f, ""}, left{-1.0f, ""}, bottom{-1.0f, ""}, right{-1.0f, ""};
        std::string background;
        RichVerticalAlign verticalAlign = RichVerticalAlign::Top;
        float padding[4] = {-1.0f, -1.0f, -1.0f, -1.0f};   // top, left, bottom, right (points)
    };
    int tableJc = 0;              // 0 left, 1 centre, 2 right
    int gapHalf = -1;             // twips; half the room between two cells' text
    float defaultPadding[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
    std::vector<CellFormat> cellFormats;
    bool hasTableBorders = false;
    RichBorder tableBorders[6];   // top, left, bottom, right, insideH, insideV
};

// Word's 16-colour palette index as "#RRGGBB" (declared below).
std::string IcoColor(uint8_t ico);

// Brc80 (4 bytes): line width in eighths of a point, line type, palette
// colour. 0xFFFFFFFF is "no border"; a zero type draws nothing.
RichBorder ReadBrc80(const std::vector<uint8_t>& data, size_t at) {
    RichBorder border;
    if (ReadU32(data, at) == 0xFFFFFFFFu) return border;
    const uint8_t width = data[at], type = data[at + 1], ico = data[at + 2];
    if (type == 0 || type == 0xFF) return border;
    border.widthPt = std::max(0.25f, static_cast<float>(width) / 8.0f);
    border.color = IcoColor(ico);
    return border;
}

// Brc (8 bytes): COLORREF, width in eighths of a point, line type, flags.
RichBorder ReadBrc(const std::vector<uint8_t>& data, size_t at) {
    RichBorder border;
    const uint8_t width = data[at + 4], type = data[at + 5];
    if (type == 0 || type == 0xFF) return border;
    border.widthPt = std::max(0.25f, static_cast<float>(width) / 8.0f);
    if (data[at + 3] != 0xFF) {                       // 0xFF000000 = automatic
        static const char* digits = "0123456789ABCDEF";
        border.color = "#";
        for (int i = 0; i < 3; ++i) {
            border.color.push_back(digits[data[at + i] >> 4]);
            border.color.push_back(digits[data[at + i] & 15]);
        }
    }
    return border;
}

// A cell side stated as "none" in TC80 is indistinguishable from "not
// stated" in older files, so a zero Brc80 lets the table borders through.
bool Brc80IsZero(const std::vector<uint8_t>& data, size_t at) { return ReadU32(data, at) == 0; }

std::string ColorRef(const std::vector<uint8_t>& data, size_t at) {
    if (at + 4 > data.size() || data[at + 3] == 0xFF) return "";      // automatic
    static const char* digits = "0123456789ABCDEF";
    std::string color = "#";
    for (int i = 0; i < 3; ++i) {
        color.push_back(digits[data[at + i] >> 4]);
        color.push_back(digits[data[at + i] & 15]);
    }
    return color;
}

// Shading: SHD80 (palette colours, 2 bytes) or SHD (COLORREFs, 10 bytes).
// A solid pattern (1) shows the foreground colour, anything else the
// background - a close enough reading of patterned shading.
std::string Shd80Color(uint16_t shd) {
    const uint8_t fore = shd & 0x1F, back = (shd >> 5) & 0x1F, pattern = (shd >> 10) & 0x3F;
    return IcoColor(pattern == 1 ? fore : back);
}
std::string ShdColor(const std::vector<uint8_t>& data, size_t at) {
    if (at + 10 > data.size()) return "";
    const uint16_t pattern = ReadU16(data, at + 8);
    return ColorRef(data, pattern == 1 ? at : at + 4);
}

// Tab changes (sprmPChgTabsPapx / sprmPChgTabs): positions to delete, then
// stops to add. sprmPChgTabs also carries a tolerance per deletion.
void ApplyTabChanges(const std::vector<uint8_t>& data, size_t operand, size_t size,
                     bool withTolerance, DocParaProps& props) {
    const size_t end = operand + size;
    size_t at = operand + 1;                 // skips the length byte
    if (at >= end) return;
    const size_t deletions = data[at++];
    std::vector<int> deleted, tolerance;
    for (size_t i = 0; i < deletions && at + 2 <= end; ++i, at += 2) deleted.push_back(ReadI16(data, at));
    if (withTolerance) {
        for (size_t i = 0; i < deletions && at + 2 <= end; ++i, at += 2) tolerance.push_back(ReadI16(data, at));
    }
    for (size_t i = 0; i < deleted.size(); ++i) {
        const int slack = i < tolerance.size() ? std::abs(tolerance[i]) : 0;
        props.tabs.erase(std::remove_if(props.tabs.begin(), props.tabs.end(),
                                        [&](const DocParaProps::Tab& t) {
                                            return std::abs(t.position - deleted[i]) <= slack;
                                        }),
                         props.tabs.end());
    }
    if (at >= end) return;
    const size_t additions = data[at++];
    const size_t kinds = at + additions * 2;
    for (size_t i = 0; i < additions && kinds + i < end; ++i) {
        DocParaProps::Tab tab;
        tab.position = ReadI16(data, at + i * 2);
        tab.kind = data[kinds + i] & 0x07;
        if (tab.kind == 4) continue;         // a bar tab draws a line; it is not a stop
        props.tabs.erase(std::remove_if(props.tabs.begin(), props.tabs.end(),
                                        [&](const DocParaProps::Tab& t) { return t.position == tab.position; }),
                         props.tabs.end());
        props.tabs.push_back(tab);
    }
}

// Word's 16-entry colour palette (sprmCIco), 0 = automatic.
std::string IcoColor(uint8_t ico) {
    static const char* const colors[] = {
        "", "#000000", "#0000FF", "#00FFFF", "#00FF00", "#FF00FF", "#FF0000",
        "#FFFF00", "#FFFFFF", "#000080", "#008080", "#008000", "#800080",
        "#800000", "#808000", "#808080", "#C0C0C0"};
    return ico < 17 ? colors[ico] : "";
}

std::string HexColor(uint8_t r, uint8_t g, uint8_t b) {
    static const char* digits = "0123456789ABCDEF";
    std::string out = "#";
    for (uint8_t v : {r, g, b}) {
        out.push_back(digits[v >> 4]);
        out.push_back(digits[v & 15]);
    }
    return out;
}

// A toggle operand: 0/1 set the property, 0x80 keeps the style's value and
// 0x81 inverts it.
bool Toggle(uint8_t operand, bool styleValue) {
    if (operand == 0x80) return styleValue;
    if (operand == 0x81) return !styleValue;
    return operand != 0;
}

void ApplyCharSprms(const std::vector<uint8_t>& data, size_t begin, size_t end,
                    const DocCharProps& style, DocCharProps& props) {
    ForEachSprm(data, begin, end, [&](const Sprm& sprm) {
        uint8_t b = data[sprm.operand];
        switch (sprm.code) {
            case kSprmCFBold: props.bold = Toggle(b, style.bold); break;
            case kSprmCFItalic: props.italic = Toggle(b, style.italic); break;
            case kSprmCFStrike:
            case kSprmCFDStrike: props.strike = Toggle(b, style.strike); break;
            case kSprmCFVanish: props.hidden = Toggle(b, style.hidden); break;
            case kSprmCFSpec: props.special = b != 0; break;
            case kSprmCFOle2: props.ole = b != 0; break;
            case kSprmCFData: break;
            case kSprmCKul: props.underline = b != 0; break;
            case kSprmCIss: props.position = b; break;
            case kSprmCHps: props.halfPoints = std::max<int>(2, ReadU16(data, sprm.operand)); break;
            case kSprmCRgFtc0: props.fontIndex = ReadU16(data, sprm.operand); break;
            case kSprmCIco: props.color = IcoColor(b); break;
            case kSprmCCv: {
                uint8_t r = data[sprm.operand], g = data[sprm.operand + 1],
                        bl = data[sprm.operand + 2], flags = data[sprm.operand + 3];
                props.color = (flags == 0xFF) ? "" : HexColor(r, g, bl);   // 0xFF = automatic
                break;
            }
            case kSprmCHighlight: props.highlight = IcoColor(b); break;
            case kSprmCShd80:
                if (props.highlight.empty()) props.highlight = Shd80Color(ReadU16(data, sprm.operand));
                break;
            case kSprmCShd:
                if (props.highlight.empty()) props.highlight = ShdColor(data, sprm.operand + 1);
                break;
            case kSprmCSymbol:
                props.symbolFont = ReadU16(data, sprm.operand);
                props.symbolChar = ReadU16(data, sprm.operand + 2);
                break;
            case kSprmCPicLocation:
                props.picLocation = static_cast<int32_t>(ReadU32(data, sprm.operand));
                break;
            default: break;
        }
    });
}

void ApplyParaSprms(const std::vector<uint8_t>& data, size_t begin, size_t end,
                    DocParaProps& props) {
    ForEachSprm(data, begin, end, [&](const Sprm& sprm) {
        uint8_t b = data[sprm.operand];
        switch (sprm.code) {
            case kSprmPJc80:
            case kSprmPJc: props.jc = b; break;
            case kSprmPIlvl: props.ilvl = std::min<int>(b, 8); break;
            case kSprmPIlfo: props.ilfo = ReadI16(data, sprm.operand); break;
            case kSprmPFInTable: props.inTable = b != 0; break;
            case kSprmPFTtp: props.rowEnd = b != 0; break;
            case kSprmPFInnerTtp: if (b) props.rowEnd = true; break;
            case kSprmPItap: props.tableDepth = static_cast<int>(ReadU32(data, sprm.operand)); break;
            case kSprmPOutLvl: props.outlineLevel = b; break;
            case kSprmPFPageBreakBefore: props.pageBreakBefore = b != 0; break;
            case kSprmPDxaLeft:
            case kSprmPDxaLeft80: props.leftIndent = ReadI16(data, sprm.operand); break;
            case kSprmPDxaRight:
            case kSprmPDxaRight80: props.rightIndent = ReadI16(data, sprm.operand); break;
            case kSprmPDxaLeft1:
            case kSprmPDxaLeft1_80: props.firstLineIndent = ReadI16(data, sprm.operand); break;
            case kSprmPDyaBefore: props.spaceBefore = ReadU16(data, sprm.operand); break;
            case kSprmPDyaAfter: props.spaceAfter = ReadU16(data, sprm.operand); break;
            case kSprmPDyaLine: {
                // LSPD: dyaLine, then fMultLinespace (240 = single when
                // multiple). Not multiple: > 0 is "at least", < 0 "exactly".
                const int line = ReadI16(data, sprm.operand);
                const bool multiple = ReadU16(data, sprm.operand + 2) != 0;
                props.lineSpacing = (multiple && line > 0) ? static_cast<float>(line) / 240.0f : 0.0f;
                props.lineHeightPt = (!multiple && line != 0) ? static_cast<float>(std::abs(line)) / 20.0f : 0.0f;
                props.lineHeightAtLeast = !multiple && line > 0;
                break;
            }
            case kSprmPBrcTop80: props.frame[0] = ReadBrc80(data, sprm.operand); break;
            case kSprmPBrcBottom80: props.frame[1] = ReadBrc80(data, sprm.operand); break;
            case kSprmPBrcLeft80: props.frame[2] = ReadBrc80(data, sprm.operand); break;
            case kSprmPBrcRight80: props.frame[3] = ReadBrc80(data, sprm.operand); break;
            case kSprmPBrcTop: props.frame[0] = ReadBrc(data, sprm.operand + 1); break;
            case kSprmPBrcBottom: props.frame[1] = ReadBrc(data, sprm.operand + 1); break;
            case kSprmPBrcLeft: props.frame[2] = ReadBrc(data, sprm.operand + 1); break;
            case kSprmPBrcRight: props.frame[3] = ReadBrc(data, sprm.operand + 1); break;
            case kSprmPShd80: props.background = Shd80Color(ReadU16(data, sprm.operand)); break;
            case kSprmPShd: props.background = ShdColor(data, sprm.operand + 1); break;
            case kSprmPChgTabsPapx:
                ApplyTabChanges(data, sprm.operand, sprm.operandSize, false, props);
                break;
            case kSprmPChgTabs:
                ApplyTabChanges(data, sprm.operand, sprm.operandSize, true, props);
                break;
            case kSprmTTableHeader: props.headerRow = b != 0; break;
            case kSprmTDefTable: {
                // cb (2) | itcMac (1) | rgdxaCenter[itcMac + 1] (2 each) |
                // rgTc80[itcMac] (20 each: flags, width, then four Brc80
                // top, left, bottom, right)
                size_t at = sprm.operand + 2;
                if (at >= data.size()) break;
                const size_t end = sprm.operand + sprm.operandSize;
                int columns = data[at];
                props.cellEdges.clear();
                for (int i = 0; i <= columns; ++i) {
                    size_t edge = at + 1 + static_cast<size_t>(i) * 2;
                    if (edge + 2 > end) break;
                    props.cellEdges.push_back(ReadI16(data, edge));
                }
                props.cellFormats.assign(static_cast<size_t>(columns), DocParaProps::CellFormat{});
                const size_t tcs = at + 1 + static_cast<size_t>(columns + 1) * 2;
                for (int i = 0; i < columns; ++i) {
                    const size_t tc = tcs + static_cast<size_t>(i) * 20;
                    if (tc + 20 > end) break;
                    auto& format = props.cellFormats[static_cast<size_t>(i)];
                    const uint16_t tcgrf = ReadU16(data, tc);
                    const int vertical = (tcgrf >> 7) & 3;
                    format.verticalAlign = vertical == 1 ? RichVerticalAlign::Middle
                                         : vertical == 2 ? RichVerticalAlign::Bottom : RichVerticalAlign::Top;
                    RichBorder* sides[4] = {&format.top, &format.left, &format.bottom, &format.right};
                    for (int b = 0; b < 4; ++b) {
                        const size_t brc = tc + 4 + static_cast<size_t>(b) * 4;
                        if (!Brc80IsZero(data, brc)) *sides[b] = ReadBrc80(data, brc);
                    }
                }
                break;
            }
            case kSprmTTableBorders80:
            case kSprmTTableBorders: {
                const size_t size = sprm.code == kSprmTTableBorders ? 8 : 4;
                if (sprm.operandSize < 1 + 6 * size) break;
                props.hasTableBorders = true;
                for (size_t i = 0; i < 6; ++i) {
                    const size_t brc = sprm.operand + 1 + i * size;
                    props.tableBorders[i] = size == 8 ? ReadBrc(data, brc) : ReadBrc80(data, brc);
                }
                break;
            }
            case kSprmTSetBrc80:
            case kSprmTSetBrc: {
                // cb | itcFirst | itcLim | sides (1 top, 2 left, 4 bottom, 8 right) | Brc
                const size_t size = sprm.code == kSprmTSetBrc ? 8 : 4;
                if (sprm.operandSize < 4 + size) break;
                const size_t first = data[sprm.operand + 1], limit = data[sprm.operand + 2];
                const uint8_t sides = data[sprm.operand + 3];
                const size_t brc = sprm.operand + 4;
                const RichBorder border = size == 8 ? ReadBrc(data, brc) : ReadBrc80(data, brc);
                for (size_t i = first; i < limit && i < props.cellFormats.size(); ++i) {
                    auto& format = props.cellFormats[i];
                    if (sides & 1) format.top = border;
                    if (sides & 2) format.left = border;
                    if (sides & 4) format.bottom = border;
                    if (sides & 8) format.right = border;
                }
                break;
            }
            case kSprmTJc90:
            case kSprmTJc: props.tableJc = ReadU16(data, sprm.operand); break;
            case kSprmTDxaGapHalf: props.gapHalf = ReadI16(data, sprm.operand); break;
            case kSprmTVertAlign: {
                // cb | itcFirst | itcLim | vertAlign
                if (sprm.operandSize < 4) break;
                const size_t first = data[sprm.operand + 1], limit = data[sprm.operand + 2];
                const uint8_t vertical = data[sprm.operand + 3];
                for (size_t i = first; i < limit && i < props.cellFormats.size(); ++i) {
                    props.cellFormats[i].verticalAlign = vertical == 1 ? RichVerticalAlign::Middle
                                                       : vertical == 2 ? RichVerticalAlign::Bottom
                                                       : RichVerticalAlign::Top;
                }
                break;
            }
            case kSprmTCellPadding:
            case kSprmTCellPaddingDefault: {
                // cb | itcFirst | itcLim | sides (1 top, 2 left, 4 bottom, 8
                // right) | ftsWidth (3 = twips) | wWidth
                if (sprm.operandSize < 7) break;
                const size_t first = data[sprm.operand + 1], limit = data[sprm.operand + 2];
                const uint8_t sides = data[sprm.operand + 3];
                if (data[sprm.operand + 4] != 3) break;
                const float points = static_cast<float>(ReadU16(data, sprm.operand + 5)) / 20.0f;
                auto apply = [&](float padding[4]) {
                    for (int side = 0; side < 4; ++side) {
                        if (sides & (1 << side)) padding[side] = points;
                    }
                };
                if (sprm.code == kSprmTCellPaddingDefault) {
                    apply(props.defaultPadding);
                } else {
                    for (size_t i = first; i < limit && i < props.cellFormats.size(); ++i) {
                        apply(props.cellFormats[i].padding);
                    }
                }
                break;
            }
            case kSprmTDefTableShd80: {
                // Shd80 per cell: icoFore (5 bits), icoBack (5 bits), pattern.
                const size_t count = (sprm.operandSize - 1) / 2;
                for (size_t i = 0; i < count && i < props.cellFormats.size(); ++i) {
                    const uint16_t shd = ReadU16(data, sprm.operand + 1 + i * 2);
                    const uint8_t back = (shd >> 5) & 0x1F;
                    if (back != 0) props.cellFormats[i].background = IcoColor(back);
                }
                break;
            }
            case kSprmTDefTableShd: {
                // SHD per cell: cvFore, cvBack (COLORREF), pattern.
                const size_t count = (sprm.operandSize - 1) / 10;
                for (size_t i = 0; i < count && i < props.cellFormats.size(); ++i) {
                    const size_t back = sprm.operand + 1 + i * 10 + 4;
                    if (data[back + 3] == 0xFF) continue;        // automatic = none
                    static const char* digits = "0123456789ABCDEF";
                    std::string color = "#";
                    for (int k = 0; k < 3; ++k) {
                        color.push_back(digits[data[back + k] >> 4]);
                        color.push_back(digits[data[back + k] & 15]);
                    }
                    props.cellFormats[i].background = color;
                }
                break;
            }
            default: break;
        }
    });
}

// ===== STYLESHEET =====

struct DocStyle {
    bool defined = false;
    int kind = 0;                 // stk: 1 paragraph, 2 character
    int base = 0x0FFF;            // istdBase; 0x0FFF = none
    int sti = 0x0FFE;             // built-in style identifier (1-9 = Heading 1-9)
    std::string name;
    size_t papx = 0, papxEnd = 0; // grpprl ranges in the table stream
    size_t chpx = 0, chpxEnd = 0;
    // Resolved (base chain applied) properties.
    bool resolved = false;
    bool resolving = false;
    DocCharProps chp;
    DocParaProps pap;
};

class DocStylesheet {
public:
    void Load(const std::vector<uint8_t>& table, uint32_t fc, uint32_t lcb) {
        table_ = &table;
        if (lcb < 4 || static_cast<size_t>(fc) + lcb > table.size()) return;
        const size_t end = static_cast<size_t>(fc) + lcb;
        const size_t cbStshi = ReadU16(table, fc);
        const size_t stshi = fc + 2;
        const size_t count = ReadU16(table, stshi);
        const size_t cbStdBase = ReadU16(table, stshi + 2);
        if (cbStshi >= 14) defaultFont_ = ReadU16(table, stshi + 12);
        size_t pos = stshi + cbStshi;
        styles_.resize(std::min<size_t>(count, 4096));
        for (size_t i = 0; i < styles_.size() && pos + 2 <= end; ++i) {
            const size_t cbStd = ReadU16(table, pos);
            const size_t stdAt = pos + 2;
            pos = stdAt + cbStd;
            if (cbStd == 0 || stdAt + cbStd > end || cbStd < 10) continue;
            DocStyle& style = styles_[i];
            style.defined = true;
            style.sti = ReadU16(table, stdAt) & 0x0FFF;
            style.kind = ReadU16(table, stdAt + 2) & 0x000F;
            style.base = ReadU16(table, stdAt + 2) >> 4;
            const size_t cupx = ReadU16(table, stdAt + 4) & 0x000F;
            // Name: 16-bit count, UTF-16 characters and a terminating zero.
            size_t at = stdAt + cbStdBase;
            const size_t nameLength = ReadU16(table, at);
            at += 2;
            for (size_t c = 0; c < nameLength && at + c * 2 + 2 <= stdAt + cbStd; ++c) {
                uint16_t ch = ReadU16(table, at + c * 2);
                if (ch < 0x80) style.name.push_back(static_cast<char>(ch));
            }
            at += (nameLength + 1) * 2;
            // Property exceptions: a paragraph style has UpxPapx then UpxChpx,
            // a character style only UpxChpx. Each is padded to an even size.
            for (size_t u = 0; u < cupx && at + 2 <= stdAt + cbStd; ++u) {
                const size_t cbUpx = ReadU16(table, at);
                const size_t upx = at + 2;
                const size_t upxEnd = std::min(upx + cbUpx, stdAt + cbStd);
                const bool isPapx = style.kind == 1 && u == 0;
                const bool isChpx = (style.kind == 1 && u == 1) || (style.kind == 2 && u == 0);
                if (isPapx && cbUpx >= 2) {
                    style.papx = upx + 2;      // skips the istd
                    style.papxEnd = upxEnd;
                } else if (isChpx) {
                    style.chpx = upx;
                    style.chpxEnd = upxEnd;
                }
                at = upx + cbUpx + (cbUpx & 1);
            }
        }
    }

    int DefaultFont() const { return defaultFont_; }

    const DocStyle* Get(int istd) {
        if (istd < 0 || istd >= static_cast<int>(styles_.size())) return nullptr;
        DocStyle& style = styles_[static_cast<size_t>(istd)];
        if (!style.defined) return nullptr;
        Resolve(style);
        return &style;
    }

    // Character properties of paragraph style `istd`, fully resolved.
    DocCharProps CharProps(int istd) {
        const DocStyle* style = Get(istd);
        if (style) return style->chp;
        DocCharProps props;
        props.fontIndex = defaultFont_;
        return props;
    }

    DocParaProps ParaProps(int istd) {
        const DocStyle* style = Get(istd);
        DocParaProps props = style ? style->pap : DocParaProps{};
        props.istd = istd;
        return props;
    }

    // 1-9 when the style is (or is based on) a built-in "Heading N".
    int HeadingLevel(int istd) {
        for (int guard = 0; guard < 16; ++guard) {
            const DocStyle* style = Get(istd);
            if (!style) return 0;
            if (style->sti >= 1 && style->sti <= 9) return style->sti;
            istd = style->base;
        }
        return 0;
    }

    const std::string& Name(int istd) {
        static const std::string empty;
        const DocStyle* style = Get(istd);
        return style ? style->name : empty;
    }

private:
    const std::vector<uint8_t>* table_ = nullptr;
    std::vector<DocStyle> styles_;
    int defaultFont_ = -1;

    void Resolve(DocStyle& style) {
        if (style.resolved || style.resolving) return;
        style.resolving = true;
        DocCharProps chp;
        chp.fontIndex = defaultFont_;
        DocParaProps pap;
        if (style.base != 0x0FFF && style.base < static_cast<int>(styles_.size())
            && styles_[static_cast<size_t>(style.base)].defined) {
            DocStyle& base = styles_[static_cast<size_t>(style.base)];
            Resolve(base);
            chp = base.chp;
            pap = base.pap;
        }
        DocCharProps baseChp = chp;
        if (style.chpxEnd > style.chpx) ApplyCharSprms(*table_, style.chpx, style.chpxEnd, baseChp, chp);
        if (style.papxEnd > style.papx) ApplyParaSprms(*table_, style.papx, style.papxEnd, pap);
        // A table row's layout belongs to the row, never to a style.
        pap.rowEnd = false;
        pap.cellEdges.clear();
        pap.cellFormats.clear();
        pap.hasTableBorders = false;
        pap.tableJc = 0;
        pap.gapHalf = -1;
        style.chp = chp;
        style.pap = pap;
        style.resolved = true;
        style.resolving = false;
    }
};

// ===== LISTS =====

struct DocListLevel {
    bool ordered = false;
    int startAt = 1;
    RichNumberFormat format = RichNumberFormat::Decimal;
    std::string numberTemplate;   // Word's "%1.%2." notation
    std::string bulletText;       // UTF-8, before symbol-font mapping
    int bulletFont = -1;          // font index of the bullet's character run
};

RichNumberFormat NumberFormatFromNfc(uint8_t nfc) {
    switch (nfc) {
        case 1: return RichNumberFormat::UpperRoman;
        case 2: return RichNumberFormat::LowerRoman;
        case 3: return RichNumberFormat::UpperLetter;
        case 4: return RichNumberFormat::LowerLetter;
        case 22: return RichNumberFormat::DecimalZero;
        case 0xFF: return RichNumberFormat::NoNumber;
        default: return RichNumberFormat::Decimal;
    }
}

struct DocList {
    int32_t lsid = 0;
    std::vector<DocListLevel> levels;     // 9, or 1 for a simple list
};

class DocLists {
public:
    void Load(const std::vector<uint8_t>& table, uint32_t fcLst, uint32_t lcbLst,
              uint32_t fcLfo, uint32_t lcbLfo) {
        if (lcbLst >= 2 && static_cast<size_t>(fcLst) + lcbLst <= table.size()) {
            const size_t count = static_cast<size_t>(std::max<int>(0, ReadI16(table, fcLst)));
            // The LVL records follow the PlfLst, in the order of its lists.
            size_t lvl = static_cast<size_t>(fcLst) + lcbLst;
            for (size_t i = 0; i < count && i < 4096; ++i) {
                const size_t lstf = fcLst + 2 + i * 28;
                if (lstf + 28 > table.size()) break;
                DocList list;
                list.lsid = static_cast<int32_t>(ReadU32(table, lstf));
                const bool simple = (table[lstf + 26] & 1) != 0;
                const int levelCount = simple ? 1 : 9;
                for (int l = 0; l < levelCount; ++l) {
                    if (lvl + 28 > table.size()) break;
                    DocListLevel level;
                    level.startAt = std::max(1, static_cast<int>(ReadU32(table, lvl)));
                    const uint8_t nfc = table[lvl + 4];
                    level.ordered = nfc != 23 && nfc != 0xFF;   // 23 bullet, 255 none
                    level.format = NumberFormatFromNfc(nfc);
                    const size_t chpx = table[lvl + 24];
                    const size_t papx = table[lvl + 25];
                    // The bullet's font, from the level's character properties.
                    const size_t chpxAt = lvl + 28 + papx;
                    ForEachSprm(table, chpxAt, chpxAt + chpx, [&](const Sprm& sprm) {
                        if (sprm.code == kSprmCRgFtc0) level.bulletFont = ReadU16(table, sprm.operand);
                    });
                    size_t text = lvl + 28 + papx + chpx;
                    const size_t textLength = ReadU16(table, text);
                    // Number text: characters 0..8 stand for level 1..9's
                    // number, everything else is literal.
                    std::string spelled;
                    for (size_t c = 0; c < textLength && text + 4 + c * 2 <= table.size(); ++c) {
                        const uint16_t ch = ReadU16(table, text + 2 + c * 2);
                        if (ch < 9) spelled += "%" + std::to_string(ch + 1);
                        else AppendUtf8(spelled, ch);
                    }
                    if (level.ordered) level.numberTemplate = spelled;
                    else if (nfc == 23) level.bulletText = spelled;
                    lvl = text + 2 + textLength * 2;
                    list.levels.push_back(level);
                }
                lists_.push_back(std::move(list));
            }
        }
        if (lcbLfo >= 4 && static_cast<size_t>(fcLfo) + lcbLfo <= table.size()) {
            const size_t count = std::min<size_t>(ReadU32(table, fcLfo), 32768);
            std::vector<int> overrideCounts;
            for (size_t i = 0; i < count; ++i) {
                const size_t lfo = fcLfo + 4 + i * 16;
                if (lfo + 16 > table.size()) break;
                Override entry;
                entry.lsid = static_cast<int32_t>(ReadU32(table, lfo));
                overrides_.push_back(entry);
                overrideCounts.push_back(table[lfo + 12]);
            }
            // LFOData: per override, a cp and clfolvl LFOLVLs that may
            // restart a level at a given number.
            size_t at = fcLfo + 4 + count * 16;
            for (size_t i = 0; i < overrides_.size(); ++i) {
                if (overrideCounts[i] == 0) continue;
                at += 4;
                for (int l = 0; l < overrideCounts[i]; ++l) {
                    if (at + 8 > table.size()) return;
                    const int32_t startAt = static_cast<int32_t>(ReadU32(table, at));
                    const uint32_t flags = ReadU32(table, at + 4);
                    at += 8;
                    if (flags & 0x10) {      // fStartAt
                        overrides_[i].restarts.emplace_back(static_cast<int>(flags & 0x0F),
                                                            std::max(1, static_cast<int>(startAt)));
                    }
                    if (flags & 0x20) {      // fFormatting: a whole LVL follows
                        if (at + 28 > table.size()) return;
                        const size_t chpx = table[at + 24];
                        const size_t papx = table[at + 25];
                        const size_t text = at + 28 + papx + chpx;
                        at = text + 2 + static_cast<size_t>(ReadU16(table, text)) * 2;
                    }
                }
            }
        }
    }

    // The list and level definition behind override `ilfo` (1-based).
    bool Lookup(int ilfo, int ilvl, int32_t& lsid, DocListLevel& level) const {
        if (ilfo < 1 || ilfo > static_cast<int>(overrides_.size())) return false;
        lsid = overrides_[static_cast<size_t>(ilfo - 1)].lsid;
        for (const DocList& list : lists_) {
            if (list.lsid != lsid || list.levels.empty()) continue;
            level = list.levels[static_cast<size_t>(
                std::min<int>(ilvl, static_cast<int>(list.levels.size()) - 1))];
            return true;
        }
        return false;
    }

    // Level restarts an override asks for the first time it is used.
    const std::vector<std::pair<int, int>>* Restarts(int ilfo) const {
        if (ilfo < 1 || ilfo > static_cast<int>(overrides_.size())) return nullptr;
        return &overrides_[static_cast<size_t>(ilfo - 1)].restarts;
    }

private:
    struct Override {
        int32_t lsid = 0;
        std::vector<std::pair<int, int>> restarts;   // (level, number)
    };
    std::vector<DocList> lists_;
    std::vector<Override> overrides_;
};

// ===== FORMATTED DISK PAGES =====
// PlcBteChpx / PlcBtePapx point at 512-byte pages (FKPs) in the WordDocument
// stream; each page maps file-offset ranges to a property exception.

struct FkpRange {
    uint32_t fcStart = 0;
    uint32_t fcEnd = 0;
    size_t grpprl = 0;        // into the WordDocument stream; 0 = no exceptions
    size_t grpprlEnd = 0;
    int istd = 0;             // paragraphs only
};

std::vector<FkpRange> LoadFkps(const std::vector<uint8_t>& word, const std::vector<uint8_t>& table,
                               uint32_t fc, uint32_t lcb, bool paragraphs) {
    std::vector<FkpRange> ranges;
    if (lcb < 8 || static_cast<size_t>(fc) + lcb > table.size()) return ranges;
    const size_t count = (lcb - 4) / 8;
    const size_t pnArray = fc + (count + 1) * 4;
    for (size_t i = 0; i < count; ++i) {
        const size_t page = static_cast<size_t>(ReadU32(table, pnArray + i * 4) & 0x3FFFFF) * 512;
        if (page + 512 > word.size()) continue;
        const size_t runs = word[page + 511];
        for (size_t r = 0; r < runs; ++r) {
            FkpRange range;
            range.fcStart = ReadU32(word, page + r * 4);
            range.fcEnd = ReadU32(word, page + (r + 1) * 4);
            const size_t rgb = page + (runs + 1) * 4;
            if (paragraphs) {
                const size_t offset = static_cast<size_t>(word[rgb + r * 13]) * 2;
                if (offset != 0 && offset < 511) {
                    size_t at = page + offset;
                    size_t size = word[at];
                    if (size != 0) {
                        size = size * 2 - 1;
                        at += 1;
                    } else {
                        size = static_cast<size_t>(word[at + 1]) * 2;
                        at += 2;
                    }
                    if (size >= 2 && at + size <= page + 512) {
                        range.istd = ReadU16(word, at);
                        range.grpprl = at + 2;
                        range.grpprlEnd = at + size;
                    }
                }
            } else {
                const size_t offset = static_cast<size_t>(word[rgb + r]) * 2;
                if (offset != 0 && offset < 511) {
                    const size_t at = page + offset;
                    const size_t size = word[at];
                    if (at + 1 + size <= page + 512) {
                        range.grpprl = at + 1;
                        range.grpprlEnd = at + 1 + size;
                    }
                }
            }
            ranges.push_back(range);
        }
    }
    std::sort(ranges.begin(), ranges.end(),
              [](const FkpRange& a, const FkpRange& b) { return a.fcStart < b.fcStart; });
    return ranges;
}

const FkpRange* FindFkpRange(const std::vector<FkpRange>& ranges, uint32_t fc) {
    auto it = std::upper_bound(ranges.begin(), ranges.end(), fc,
                               [](uint32_t value, const FkpRange& r) { return value < r.fcStart; });
    if (it == ranges.begin()) return nullptr;
    --it;
    return (fc >= it->fcStart && fc < it->fcEnd) ? &*it : nullptr;
}

// ===== FONT TABLE =====

std::vector<std::string> LoadFontNames(const std::vector<uint8_t>& table, uint32_t fc, uint32_t lcb) {
    std::vector<std::string> names;
    if (lcb < 4 || static_cast<size_t>(fc) + lcb > table.size()) return names;
    const size_t end = static_cast<size_t>(fc) + lcb;
    size_t count = ReadU16(table, fc);
    size_t pos = fc + 4;                       // cData, cbExtra
    if (count == 0xFFFF) {                     // extended: 32-bit count
        count = ReadU32(table, fc + 2);
        pos = fc + 8;
    }
    for (size_t i = 0; i < count && i < 4096 && pos < end; ++i) {
        const size_t size = table[pos];
        const size_t ffn = pos + 1;
        pos = ffn + size;
        std::string name;
        // ffid, wWeight, chs, ixchSzAlt, panose[10], fs[24], then the name.
        for (size_t at = ffn + 39; at + 2 <= std::min(pos, end); at += 2) {
            uint16_t ch = ReadU16(table, at);
            if (ch == 0) break;
            AppendUtf8(name, ch);
        }
        names.push_back(std::move(name));
    }
    return names;
}

bool IsMonospaceFont(const std::string& family) {
    std::string lower;
    for (char c : family) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lower.find("courier") != std::string::npos || lower.find("consolas") != std::string::npos
        || lower.find("mono") != std::string::npos;
}

// ===== PICTURES =====
// An inline picture is a 0x01 character whose sprmCPicLocation points into
// the Data stream at a PICF header followed by OfficeArt records; the image
// itself is a BLIP record among them.

bool ExtractPicture(const std::vector<uint8_t>& data, int32_t location, std::vector<uint8_t>& image,
                    std::string& mimeType, float& widthPt, float& heightPt) {
    if (location < 0 || static_cast<size_t>(location) + 0x44 > data.size()) return false;
    const size_t pic = static_cast<size_t>(location);
    const size_t total = ReadU32(data, pic);
    const size_t headerSize = ReadU16(data, pic + 4);
    if (total < headerSize || pic + total > data.size()) return false;
    // Display size: dxaGoal/dyaGoal (twips) scaled by mx/my (per mille).
    const float goalX = ReadI16(data, pic + 0x1C), goalY = ReadI16(data, pic + 0x1E);
    const float scaleX = ReadU16(data, pic + 0x20), scaleY = ReadU16(data, pic + 0x22);
    widthPt = goalX * (scaleX > 0 ? scaleX : 1000.0f) / 1000.0f / 20.0f;
    heightPt = goalY * (scaleY > 0 ? scaleY : 1000.0f) / 1000.0f / 20.0f;

    // Scan the OfficeArt records for a PNG or JPEG BLIP. Metafiles (EMF/WMF)
    // and DIBs are not decoded.
    const size_t end = pic + total;
    for (size_t at = pic + headerSize; at + 8 <= end; ++at) {
        const uint16_t verInstance = ReadU16(data, at);
        const uint16_t type = ReadU16(data, at + 2);
        const uint32_t length = ReadU32(data, at + 4);
        if ((verInstance & 0x0F) != 0 || at + 8 + length > end) continue;
        const uint16_t instance = verInstance >> 4;
        size_t header = 0;
        if (type == 0xF01E) {                                  // PNG
            header = (instance == 0x6E1) ? 33 : 17;
            mimeType = "image/png";
        } else if (type == 0xF01D || type == 0xF02A) {         // JPEG
            header = (instance == 0x46B || instance == 0x6E3) ? 33 : 17;
            mimeType = "image/jpeg";
        } else {
            continue;
        }
        if (header >= length) continue;
        image.assign(data.begin() + static_cast<std::ptrdiff_t>(at + 8 + header),
                     data.begin() + static_cast<std::ptrdiff_t>(at + 8 + length));
        const bool png = image.size() > 4 && image[0] == 0x89 && image[1] == 'P';
        const bool jpeg = image.size() > 3 && image[0] == 0xFF && image[1] == 0xD8;
        if ((mimeType == "image/png" && png) || (mimeType == "image/jpeg" && jpeg)) return true;
        image.clear();
    }
    return false;
}

// ===== DOCUMENT BUILDER =====

// One character of the main text with where its bytes live, which is the
// key every property lookup goes by.
struct DocChar {
    uint32_t ch = 0;
    uint32_t fc = 0;
};

class DocReader {
public:
    DocReader(const std::vector<uint8_t>& word, const std::vector<uint8_t>& table,
              const std::vector<uint8_t>& data, UCRichDocument& doc)
        : word_(word), table_(table), data_(data), doc_(doc) {}

    bool Read(std::string& error) {
        // FIB: FibBase (32 bytes), then three counted arrays; the FC/LCB pairs
        // are the third.
        const size_t csw = ReadU16(word_, 0x20);
        const size_t lw = 0x22 + csw * 2;
        const size_t cslw = ReadU16(word_, lw);
        const size_t fcLcbCountAt = lw + 2 + cslw * 4;
        const size_t fcLcbCount = ReadU16(word_, fcLcbCountAt);
        fcLcb_ = fcLcbCountAt + 2;
        fcLcbCount_ = fcLcbCount;
        const uint32_t ccpText = ReadU32(word_, lw + 2 + 3 * 4);
        const uint32_t ccpFtn = ReadU32(word_, lw + 2 + 4 * 4);
        const uint32_t ccpHdd = ReadU32(word_, lw + 2 + 5 * 4);

        // The main text, then the footnotes, then the header/footer stories -
        // one run of character positions through the piece table.
        std::vector<DocChar> allChars;
        if (!ReadPieces(ccpText + ccpFtn + ccpHdd, allChars, error)) return false;
        std::vector<DocChar> chars(allChars.begin(),
                                   allChars.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(ccpText, allChars.size())));

        stylesheet_.Load(table_, FcAt(1), LcbAt(1));
        fonts_ = LoadFontNames(table_, FcAt(15), LcbAt(15));
        chpx_ = LoadFkps(word_, table_, FcAt(12), LcbAt(12), false);
        papx_ = LoadFkps(word_, table_, FcAt(13), LcbAt(13), true);
        lists_.Load(table_, FcAt(73), LcbAt(73), FcAt(74), LcbAt(74));
        // Document properties: dxaTab, the default tab width, at offset 10.
        const uint32_t fcDop = FcAt(31), lcbDop = LcbAt(31);
        if (lcbDop >= 12 && static_cast<size_t>(fcDop) + lcbDop <= table_.size()) {
            const int dxaTab = ReadU16(table_, fcDop + 10);
            if (dxaTab > 0) doc_.defaultTabStopPt = static_cast<float>(dxaTab) / 20.0f;
        }

        BuildBlocks(chars);
        LoadSection();
        LoadHeaderStories(allChars, static_cast<size_t>(ccpText) + ccpFtn, ccpHdd);
        return true;
    }

    // The first section's page: size, margins, header/footer distances, and
    // whether its first page has its own header and footer. Word's defaults
    // (US Letter, 1.25" / 1" margins) stand in for anything not stated.
    void LoadSection() {
        RichPageSetup& page = doc_.page;
        int xaPage = 12240, yaPage = 15840, left = 1800, right = 1800, top = 1440, bottom = 1440;
        int headerTop = 720, footerBottom = 720;
        const uint32_t fc = FcAt(6), lcb = LcbAt(6);   // PlcfSed
        if (lcb >= 4 + 12 && static_cast<size_t>(fc) + lcb <= table_.size()) {
            const uint32_t fcSepx = ReadU32(table_, fc + 8 + 2);   // first Sed, after two CPs
            if (fcSepx != 0xFFFFFFFFu && fcSepx + 2 <= word_.size()) {
                const size_t size = ReadU16(word_, fcSepx);
                ForEachSprm(word_, fcSepx + 2, fcSepx + 2 + size, [&](const Sprm& sprm) {
                    const int value = ReadI16(word_, sprm.operand);
                    switch (sprm.code) {
                        case 0xB01F: xaPage = ReadU16(word_, sprm.operand); break;   // sprmSXaPage
                        case 0xB020: yaPage = ReadU16(word_, sprm.operand); break;   // sprmSYaPage
                        case 0xB021: left = value; break;                              // sprmSDxaLeft
                        case 0xB022: right = value; break;                             // sprmSDxaRight
                        case 0x9023: top = std::abs(value); break;                     // sprmSDyaTop
                        case 0x9024: bottom = std::abs(value); break;                  // sprmSDyaBottom
                        case 0xB017: headerTop = value; break;                         // sprmSDyaHdrTop
                        case 0xB018: footerBottom = value; break;                      // sprmSDyaHdrBottom
                        case 0x300A: doc_.firstPageDiffers = word_[sprm.operand] != 0; break;   // sprmSFTitlePage
                        default: break;
                    }
                });
            }
        }
        page.widthPt = static_cast<float>(xaPage) / 20.0f;
        page.heightPt = static_cast<float>(yaPage) / 20.0f;
        page.marginLeftPt = static_cast<float>(left) / 20.0f;
        page.marginRightPt = static_cast<float>(right) / 20.0f;
        page.marginTopPt = static_cast<float>(top) / 20.0f;
        page.marginBottomPt = static_cast<float>(bottom) / 20.0f;
        page.headerTopPt = static_cast<float>(headerTop) / 20.0f;
        page.footerBottomPt = static_cast<float>(footerBottom) / 20.0f;
    }

    // Header and footer stories of the first section (PlcfHdd): six note
    // separators, then per section even/odd header, even/odd footer, first
    // header, first footer. Odd pages' are every page's here.
    void LoadHeaderStories(const std::vector<DocChar>& allChars, size_t storyStart, uint32_t ccpHdd) {
        const uint32_t fc = FcAt(11), lcb = LcbAt(11);
        if (ccpHdd == 0 || lcb < 4 * 13 || static_cast<size_t>(fc) + lcb > table_.size()) return;
        auto story = [&](size_t index, std::vector<RichDocBlock>& out) {
            const size_t begin = ReadU32(table_, fc + index * 4);
            const size_t end = ReadU32(table_, fc + (index + 1) * 4);
            if (end <= begin || storyStart + end > allChars.size()) return;
            std::vector<DocChar> text(allChars.begin() + static_cast<std::ptrdiff_t>(storyStart + begin),
                                      allChars.begin() + static_cast<std::ptrdiff_t>(storyStart + end));
            // The stories are read with the body's machinery, into their own list.
            std::vector<RichDocBlock> body = std::move(doc_.blocks);
            doc_.blocks.clear();
            BuildBlocks(text);
            out = std::move(doc_.blocks);
            doc_.blocks = std::move(body);
            while (!out.empty() && out.back().type == RichBlockType::Paragraph && out.back().runs.empty()) out.pop_back();
        };
        story(7, doc_.pageFurniture.header);
        story(9, doc_.pageFurniture.footer);
        if (doc_.firstPageDiffers) {
            story(10, doc_.firstPageFurniture.header);
            story(11, doc_.firstPageFurniture.footer);
        }
    }

private:
    const std::vector<uint8_t>& word_;
    const std::vector<uint8_t>& table_;
    const std::vector<uint8_t>& data_;
    UCRichDocument& doc_;
    size_t fcLcb_ = 0;
    size_t fcLcbCount_ = 0;
    DocStylesheet stylesheet_;
    DocLists lists_;
    std::vector<std::string> fonts_;
    std::vector<FkpRange> chpx_;
    std::vector<FkpRange> papx_;
    RichListNumbering numbering_;
    std::vector<int> listOverridesSeen_;

    // Field state: instructions are hidden, results shown; a HYPERLINK
    // field's result becomes a link.
    struct Field {
        std::string instruction;
        bool inResult = false;
        std::string link;
        RichTextRun::Field pageField = RichTextRun::Field::None;   // PAGE / NUMPAGES
    };

    RichTextRun::Field ActivePageField() const {
        for (auto it = fields_.rbegin(); it != fields_.rend(); ++it) {
            if (it->inResult && it->pageField != RichTextRun::Field::None) return it->pageField;
        }
        return RichTextRun::Field::None;
    }

    static RichTextRun::Field PageFieldFor(const std::string& instruction) {
        std::string word;
        for (char c : instruction) {
            if (c == ' ') { if (!word.empty()) break; continue; }
            word.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        if (word == "PAGE") return RichTextRun::Field::PageNumber;
        if (word == "NUMPAGES" || word == "SECTIONPAGES") return RichTextRun::Field::PageCount;
        return RichTextRun::Field::None;
    }
    std::vector<Field> fields_;

    // The table being assembled.
    bool tableOpen_ = false;
    RichDocBlock tableBlock_;
    RichTableRow row_;
    RichTableCell cell_;
    bool cellHasParagraph_ = false;
    struct RowBorders { bool present = false; RichBorder sides[6]; };
    std::vector<RowBorders> rowTableBorders_;   // per row of the table being built

    uint32_t FcAt(size_t index) const {
        return index < fcLcbCount_ ? ReadU32(word_, fcLcb_ + index * 8) : 0;
    }
    uint32_t LcbAt(size_t index) const {
        return index < fcLcbCount_ ? ReadU32(word_, fcLcb_ + index * 8 + 4) : 0;
    }

    bool ReadPieces(uint32_t ccpText, std::vector<DocChar>& chars, std::string& error) {
        const uint32_t fcClx = FcAt(33);
        const uint32_t lcbClx = LcbAt(33);
        if (lcbClx == 0 || static_cast<size_t>(fcClx) + lcbClx > table_.size()) {
            error = "The document's piece table is missing or corrupt";
            return false;
        }
        // Clx: skip Prc property blocks (type 0x01) to reach the Pcdt (type 0x02).
        size_t pos = fcClx;
        const size_t clxEnd = static_cast<size_t>(fcClx) + lcbClx;
        size_t plcPcdStart = 0, plcPcdSize = 0;
        while (pos < clxEnd) {
            uint8_t clxt = table_[pos];
            if (clxt == 0x01) {
                pos += 3 + ReadU16(table_, pos + 1);
            } else if (clxt == 0x02) {
                plcPcdSize = ReadU32(table_, pos + 1);
                plcPcdStart = pos + 5;
                break;
            } else {
                break;
            }
        }
        if (plcPcdSize < 12 + 4 || plcPcdStart + plcPcdSize > table_.size()) {
            error = "The document's piece table is missing or corrupt";
            return false;
        }
        // PlcPcd: n+1 character positions followed by n 8-byte piece descriptors.
        const size_t pieceCount = (plcPcdSize - 4) / 12;
        const size_t cpArray = plcPcdStart;
        const size_t pcdArray = plcPcdStart + (pieceCount + 1) * 4;
        chars.reserve(ccpText);
        uint32_t emitted = 0;
        for (size_t i = 0; i < pieceCount && emitted < ccpText; ++i) {
            const uint32_t cpStart = ReadU32(table_, cpArray + i * 4);
            const uint32_t cpEnd = ReadU32(table_, cpArray + (i + 1) * 4);
            if (cpEnd <= cpStart) continue;
            uint32_t count = std::min(cpEnd - cpStart, ccpText - emitted);
            const uint32_t fc = ReadU32(table_, pcdArray + i * 8 + 2);
            const bool compressed = (fc & 0x40000000) != 0;
            const uint32_t offset = compressed ? (fc & 0x3FFFFFFF) / 2 : (fc & 0x3FFFFFFF);
            for (uint32_t c = 0; c < count; ++c) {
                DocChar dc;
                if (compressed) {
                    dc.fc = offset + c;
                    if (dc.fc >= word_.size()) break;
                    dc.ch = Cp1252ToUnicode(word_[dc.fc]);
                    if (dc.ch == 0) dc.ch = 0xFFFD;
                } else {
                    dc.fc = offset + c * 2;
                    if (dc.fc + 1 >= word_.size()) break;
                    dc.ch = ReadU16(word_, dc.fc);
                }
                chars.push_back(dc);
            }
            emitted += count;
        }
        return true;
    }

    DocParaProps ParagraphProps(uint32_t fcOfMark) {
        const FkpRange* range = FindFkpRange(papx_, fcOfMark);
        const int istd = range ? range->istd : 0;
        DocParaProps props = stylesheet_.ParaProps(istd);
        if (range && range->grpprlEnd > range->grpprl) {
            ApplyParaSprms(word_, range->grpprl, range->grpprlEnd, props);
        }
        return props;
    }

    DocCharProps CharProps(uint32_t fc, const DocCharProps& paragraphStyle) {
        DocCharProps props = paragraphStyle;
        const FkpRange* range = FindFkpRange(chpx_, fc);
        if (range && range->grpprlEnd > range->grpprl) {
            ApplyCharSprms(word_, range->grpprl, range->grpprlEnd, paragraphStyle, props);
        }
        return props;
    }

    std::string FontName(int index) const {
        return (index >= 0 && index < static_cast<int>(fonts_.size())) ? fonts_[static_cast<size_t>(index)] : "";
    }

    // Converts properties to a run. For a heading, what the heading style
    // itself sets is the block's look, so only what differs from it is kept.
    RichTextRun MakeRun(const DocCharProps& props, const DocCharProps* headingStyle) const {
        RichTextRun run;
        const DocCharProps none;
        const DocCharProps& base = headingStyle ? *headingStyle : none;
        run.bold = props.bold && !base.bold;
        run.italic = props.italic && !base.italic;
        run.underline = props.underline && !base.underline;
        run.strikethrough = props.strike && !base.strike;
        run.superscript = props.position == 1;
        run.subscript = props.position == 2;
        if (!props.color.empty() && props.color != "#000000" && props.color != base.color) {
            run.color = props.color;
        }
        if (!headingStyle || props.halfPoints != base.halfPoints) {
            run.fontSizePt = static_cast<float>(props.halfPoints) / 2.0f;
        }
        if (!headingStyle || props.fontIndex != base.fontIndex) {
            run.fontFamily = FontName(props.fontIndex);
        }
        run.code = IsMonospaceFont(run.fontFamily);
        if (!props.highlight.empty() && props.highlight != base.highlight) run.highlightColor = props.highlight;
        return run;
    }

    // Appends text to runs, merging with the last run when the formatting matches.
    static void AppendText(std::vector<RichTextRun>& runs, const RichTextRun& format,
                           const std::string& text, bool lineBreak) {
        if (!lineBreak && !runs.empty() && runs.back().HasSameFormatting(format)) {
            runs.back().text += text;
            return;
        }
        RichTextRun run = format;
        run.text = text;
        run.lineBreakBefore = lineBreak;
        runs.push_back(std::move(run));
    }

    std::string ActiveLink() const {
        for (auto it = fields_.rbegin(); it != fields_.rend(); ++it) {
            if (it->inResult && !it->link.empty()) return it->link;
        }
        return "";
    }

    bool InFieldInstruction() const {
        for (const Field& field : fields_) {
            if (!field.inResult) return true;
        }
        return false;
    }

    // HYPERLINK "url" [\l "anchor"] — the target, or "" for other fields.
    static std::string HyperlinkTarget(const std::string& instruction) {
        size_t pos = instruction.find_first_not_of(' ');
        if (pos == std::string::npos || instruction.compare(pos, 9, "HYPERLINK") != 0) return "";
        pos += 9;
        std::string target, anchor;
        bool anchorNext = false;
        while (pos < instruction.size()) {
            pos = instruction.find_first_not_of(' ', pos);
            if (pos == std::string::npos) break;
            std::string token;
            if (instruction[pos] == '"') {
                size_t close = instruction.find('"', pos + 1);
                if (close == std::string::npos) close = instruction.size();
                token = instruction.substr(pos + 1, close - pos - 1);
                pos = close + 1;
            } else {
                size_t space = instruction.find(' ', pos);
                if (space == std::string::npos) space = instruction.size();
                token = instruction.substr(pos, space - pos);
                pos = space;
            }
            if (token == "\\l") { anchorNext = true; continue; }
            if (!token.empty() && token[0] == '\\') { anchorNext = false; continue; }
            if (anchorNext) { anchor = token; anchorNext = false; }
            else if (target.empty()) target = token;
        }
        if (!anchor.empty()) target += "#" + anchor;
        return target;
    }

    void BuildBlocks(const std::vector<DocChar>& chars) {
        size_t start = 0;
        for (size_t i = 0; i < chars.size(); ++i) {
            const uint32_t ch = chars[i].ch;
            if (ch == 0x0D || ch == 0x07 || ch == 0x0C) {
                EmitParagraph(chars, start, i);
                start = i + 1;
            }
        }
        if (start < chars.size()) EmitParagraph(chars, start, chars.size());
        CloseTable();
    }

    // chars[begin, end) is the paragraph's text; chars[end] (when in range)
    // is the mark that ended it.
    void EmitParagraph(const std::vector<DocChar>& chars, size_t begin, size_t end) {
        const bool hasMark = end < chars.size();
        const uint32_t mark = hasMark ? chars[end].ch : 0x0D;
        const uint32_t markFc = hasMark ? chars[end].fc
                                        : (end > begin ? chars[end - 1].fc : 0);
        DocParaProps pap = ParagraphProps(markFc);
        const bool inTable = pap.inTable || pap.tableDepth > 0;

        if (inTable && pap.rowEnd && mark == 0x07) {
            FinishRow(pap);
            return;
        }

        const DocCharProps styleChars = stylesheet_.CharProps(pap.istd);
        int headingLevel = stylesheet_.HeadingLevel(pap.istd);
        if (headingLevel == 0 && pap.outlineLevel < 9) headingLevel = pap.outlineLevel + 1;
        const bool heading = headingLevel > 0 && !inTable;

        std::vector<RichTextRun> runs;
        bool lineBreak = false;
        for (size_t i = begin; i < end; ++i) {
            const uint32_t ch = chars[i].ch;
            DocCharProps props = CharProps(chars[i].fc, styleChars);
            if (ch == 0x13) { fields_.push_back(Field{}); continue; }
            if (ch == 0x14) {
                if (!fields_.empty()) {
                    fields_.back().inResult = true;
                    fields_.back().link = HyperlinkTarget(fields_.back().instruction);
                    fields_.back().pageField = PageFieldFor(fields_.back().instruction);
                }
                continue;
            }
            if (ch == 0x15) { if (!fields_.empty()) fields_.pop_back(); continue; }
            if (InFieldInstruction()) {
                if (!fields_.empty() && ch >= 0x20) AppendUtf8(fields_.back().instruction, ch);
                continue;
            }
            if (props.hidden) continue;
            if (ch == 0x0B) { lineBreak = true; continue; }
            if (ch == 0x01 && props.special) {
                AppendPicture(runs, props, lineBreak);
                lineBreak = false;
                continue;
            }
            std::string text;
            if (props.special && props.symbolChar != 0) {
                // The symbol-font mapping after import turns it into Unicode.
                RichTextRun format = MakeRun(props, heading ? &styleChars : nullptr);
                format.fontFamily = FontName(props.symbolFont);
                format.code = false;
                format.linkTarget = ActiveLink();
                AppendUtf8(text, props.symbolChar);
                AppendText(runs, format, text, lineBreak);
                lineBreak = false;
                continue;
            }
            if (ch == 0x09) text = "\t";
            else if (ch == 0x1E) text = "-";            // non-breaking hyphen
            else if (ch == 0x1F) continue;              // optional hyphen
            else if (ch < 0x20) continue;               // other controls: footnote marks, objects
            else AppendUtf8(text, ch);
            RichTextRun format = MakeRun(props, heading ? &styleChars : nullptr);
            format.linkTarget = ActiveLink();
            format.field = ActivePageField();
            AppendText(runs, format, text, lineBreak);
            lineBreak = false;
        }

        if (inTable) {
            // Paragraphs of one cell share it, a line apart; the cell mark
            // closes it. Nested tables flatten into their outer cell.
            if (!tableOpen_) OpenTable();
            if (cellHasParagraph_ && !runs.empty()) runs.front().lineBreakBefore = true;
            if (!cellHasParagraph_) {
                cell_.align = AlignFor(pap.jc);
                // A cell holds text, not paragraphs: its first paragraph's
                // frame stands in where the cell itself has none.
                cell_.borderTop = pap.frame[0];
                cell_.borderBottom = pap.frame[1];
                cell_.borderLeft = pap.frame[2];
                cell_.borderRight = pap.frame[3];
                cell_.backgroundColor = pap.background;
            }
            for (auto& run : runs) cell_.runs.push_back(std::move(run));
            cellHasParagraph_ = true;
            if (mark == 0x07 && pap.tableDepth <= 1) {
                row_.cells.push_back(std::move(cell_));
                cell_ = RichTableCell{};
                cellHasParagraph_ = false;
            }
            return;
        }

        CloseTable();
        if (pap.pageBreakBefore && !doc_.blocks.empty()
            && doc_.blocks.back().type != RichBlockType::PageBreak) {
            RichDocBlock pageBreak;
            pageBreak.type = RichBlockType::PageBreak;
            doc_.blocks.push_back(std::move(pageBreak));
        }
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        block.align = AlignFor(pap.jc);
        ApplyGeometry(block, pap);
        block.runs = std::move(runs);
        int listNumber = 0;
        if (heading) {
            block.type = RichBlockType::Heading;
            block.headingLevel = std::clamp(headingLevel, 1, 6);
        } else if (IsQuoteStyle(stylesheet_.Name(pap.istd))) {
            block.type = RichBlockType::BlockQuote;
        } else if (IsMonospaceFont(FontName(styleChars.fontIndex)) && !block.runs.empty()) {
            // A paragraph style in a monospace font is preformatted text,
            // the way the ODT reader reads it.
            block.type = RichBlockType::CodeBlock;
            for (auto& run : block.runs) run.code = false;
        } else if (pap.ilfo > 0 && pap.ilfo < 0x07FF) {
            int32_t lsid = 0;
            DocListLevel level;
            if (lists_.Lookup(pap.ilfo, pap.ilvl, lsid, level)) {
                block.type = RichBlockType::ListItem;
                block.listLevel = pap.ilvl;
                block.orderedList = level.ordered;
                if (level.ordered) {
                    block.numberFormat = level.format;
                    block.numberTemplate = level.numberTemplate;
                } else {
                    block.bulletText = level.bulletText;
                    // A symbol-font bullet (Symbol U+F0B7, Wingdings "§") is
                    // the character that font draws there.
                    const std::string font = FontName(level.bulletFont);
                    size_t at = 0;
                    const uint32_t cp = level.bulletText.empty() ? 0 : WordFormatInternal::DecodeUtf8(level.bulletText, at);
                    if (const uint32_t unicode = WordFormatInternal::SymbolFontCharToUnicode(font, cp)) {
                        block.bulletText.clear();
                        AppendUtf8(block.bulletText, unicode);
                    }
                }
                if (level.ordered) listNumber = NextListNumber(pap.ilfo, lsid, pap.ilvl, level.startAt);
            }
        }
        // A picture on a line of its own is an image block, as in ODT/DOCX.
        RichDocBlock image;
        if (block.type == RichBlockType::Paragraph && ParagraphIsOnePicture(block.runs, image)) {
            image.align = block.align;
            doc_.blocks.push_back(std::move(image));
        } else {
            doc_.blocks.push_back(std::move(block));
            if (listNumber > 0) {
                RichListNumbering::Apply(doc_.blocks, doc_.blocks.size() - 1, listNumber);
            }
        }
        if (mark == 0x0C) {
            RichDocBlock pageBreak;
            pageBreak.type = RichBlockType::PageBreak;
            doc_.blocks.push_back(std::move(pageBreak));
        }
    }

    int NextListNumber(int ilfo, int32_t lsid, int ilvl, int startAt) {
        const std::string key = "doc-list-" + std::to_string(lsid);
        if (std::find(listOverridesSeen_.begin(), listOverridesSeen_.end(), ilfo)
                == listOverridesSeen_.end()) {
            listOverridesSeen_.push_back(ilfo);
            if (const auto* restarts = lists_.Restarts(ilfo)) {
                for (const auto& [level, number] : *restarts) numbering_.Restart(key, level, number);
            }
        }
        return numbering_.Next(key, ilvl, startAt);
    }

    static bool ParagraphIsOnePicture(const std::vector<RichTextRun>& runs, RichDocBlock& image) {
        const RichTextRun* picture = nullptr;
        for (const auto& run : runs) {
            if (run.IsInlineImage()) {
                if (picture) return false;
                picture = &run;
                continue;
            }
            if (run.text.find_first_not_of(" \t") != std::string::npos) return false;
        }
        if (!picture) return false;
        image.type = RichBlockType::Image;
        image.mediaIndex = picture->mediaIndex;
        image.imageWidthPt = picture->imageWidthPt;
        image.imageHeightPt = picture->imageHeightPt;
        return true;
    }

    void AppendPicture(std::vector<RichTextRun>& runs, const DocCharProps& props, bool lineBreak) {
        if (props.ole || props.picLocation < 0) return;
        std::vector<uint8_t> bytes;
        std::string mimeType;
        float widthPt = 0, heightPt = 0;
        if (!ExtractPicture(data_, props.picLocation, bytes, mimeType, widthPt, heightPt)) return;
        const std::string name = "image" + std::to_string(doc_.media.size() + 1)
                               + UCRichDocument::FileExtensionForMimeType(mimeType);
        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        run.mediaIndex = doc_.AddMedia(name, mimeType, std::move(bytes));
        run.imageWidthPt = widthPt > 0 ? widthPt : 0;
        run.imageHeightPt = heightPt > 0 ? heightPt : 0;
        run.lineBreakBefore = lineBreak;
        runs.push_back(std::move(run));
    }

    static void ApplyGeometry(RichDocBlock& block, const DocParaProps& pap) {
        block.leftIndentPt = static_cast<float>(pap.leftIndent) / 20.0f;
        block.rightIndentPt = static_cast<float>(pap.rightIndent) / 20.0f;
        block.firstLineIndentPt = static_cast<float>(pap.firstLineIndent) / 20.0f;
        block.spaceBeforePt = static_cast<float>(pap.spaceBefore) / 20.0f;
        block.spaceAfterPt = static_cast<float>(pap.spaceAfter) / 20.0f;
        block.lineSpacing = pap.lineSpacing;
        block.lineHeightPt = pap.lineHeightPt;
        block.lineHeightAtLeast = pap.lineHeightAtLeast;
        block.paragraphBorderTop = pap.frame[0];
        block.paragraphBorderBottom = pap.frame[1];
        block.paragraphBorderLeft = pap.frame[2];
        block.paragraphBorderRight = pap.frame[3];
        block.paragraphBackground = pap.background;
        // Word counts tab positions from the text margin, as the model does.
        for (const auto& tab : pap.tabs) {
            RichTabStop stop;
            stop.positionPt = static_cast<float>(tab.position) / 20.0f;
            stop.kind = tab.kind == 1 ? RichTabKind::Center : tab.kind == 2 ? RichTabKind::Right
                      : tab.kind == 3 ? RichTabKind::Decimal : RichTabKind::Left;
            block.tabStops.push_back(stop);
        }
        std::sort(block.tabStops.begin(), block.tabStops.end(),
                  [](const RichTabStop& a, const RichTabStop& b) { return a.positionPt < b.positionPt; });
    }

    static bool IsQuoteStyle(const std::string& name) {
        std::string lower;
        for (char c : name) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return lower.find("quot") != std::string::npos;
    }

    static RichTextAlign AlignFor(int jc) {
        switch (jc) {
            case 1: return RichTextAlign::Center;
            case 2: return RichTextAlign::Right;
            case 0: return RichTextAlign::Default;
            default: return RichTextAlign::Justify;
        }
    }

    void OpenTable() {
        tableOpen_ = true;
        tableBlock_ = RichDocBlock{};
        tableBlock_.type = RichBlockType::Table;
        row_ = RichTableRow{};
        cell_ = RichTableCell{};
        cellHasParagraph_ = false;
    }

    void FinishRow(const DocParaProps& pap) {
        if (!tableOpen_) OpenTable();
        if (cellHasParagraph_) {           // a cell whose mark went missing
            row_.cells.push_back(std::move(cell_));
            cell_ = RichTableCell{};
            cellHasParagraph_ = false;
        }
        row_.header = pap.headerRow;
        // Width, position and alignment come from the first row that states
        // them, like the column widths.
        if (tableBlock_.tableWidthPt <= 0.0f && pap.cellEdges.size() >= 2) {
            tableBlock_.tableWidthPt = static_cast<float>(pap.cellEdges.back() - pap.cellEdges.front()) / 20.0f;
            // The first edge is left of the text by the cell margin, so
            // add that back to line the table's text up with the margin.
            const int margin = std::max(0, pap.gapHalf);
            tableBlock_.tableIndentPt = std::max(0.0f, static_cast<float>(pap.cellEdges.front() + margin) / 20.0f);
            tableBlock_.tableAlign = pap.tableJc == 1 ? RichTextAlign::Center
                                   : pap.tableJc == 2 ? RichTextAlign::Right : RichTextAlign::Left;
        }
        // Column widths come from the first row that states its cell edges.
        if (tableBlock_.tableColumnWidths.empty() && pap.cellEdges.size() == row_.cells.size() + 1) {
            for (size_t c = 0; c + 1 < pap.cellEdges.size(); ++c) {
                tableBlock_.tableColumnWidths.push_back(
                    static_cast<float>(std::max(1, pap.cellEdges[c + 1] - pap.cellEdges[c])));
            }
        }
        // Borders and fill: what each cell states now; the sides it leaves
        // open are settled against the table borders once the table is
        // complete, when it is known which cells lie on its edge.
        for (size_t c = 0; c < row_.cells.size(); ++c) {
            RichTableCell& cell = row_.cells[c];
            const DocParaProps::CellFormat format =
                c < pap.cellFormats.size() ? pap.cellFormats[c] : DocParaProps::CellFormat{};
            // The cell's own frame wins; a side it leaves open keeps what its
            // paragraph drew there.
            auto take = [](RichBorder& side, const RichBorder& own) {
                if (own.IsVisible() || !side.IsVisible()) side = own;
            };
            take(cell.borderTop, format.top);
            take(cell.borderLeft, format.left);
            take(cell.borderBottom, format.bottom);
            take(cell.borderRight, format.right);
            if (!format.background.empty() || cell.backgroundColor.empty()) cell.backgroundColor = format.background;
            cell.verticalAlign = format.verticalAlign;
            // Padding: the cell's, else the row default, else half the gap
            // between cells at the sides (Word's classic cell margin).
            float* targets[4] = {&cell.paddingTopPt, &cell.paddingLeftPt, &cell.paddingBottomPt, &cell.paddingRightPt};
            for (int side = 0; side < 4; ++side) {
                float value = format.padding[side] >= 0.0f ? format.padding[side] : pap.defaultPadding[side];
                if (value < 0.0f && (side == 1 || side == 3) && pap.gapHalf >= 0) {
                    value = static_cast<float>(pap.gapHalf) / 20.0f;
                }
                *targets[side] = value;
            }
        }
        if (!row_.cells.empty()) {
            tableBlock_.tableRows.push_back(std::move(row_));
            RowBorders borders;
            borders.present = pap.hasTableBorders;
            for (int i = 0; i < 6; ++i) borders.sides[i] = pap.tableBorders[i];
            rowTableBorders_.push_back(borders);
        }
        row_ = RichTableRow{};
    }

    // A side a cell did not state takes the table's border for its position:
    // top/bottom/left/right on the table's edge, insideH/insideV within.
    void ResolveTableBorders() {
        tableBlock_.tableBordersFromDocument = true;
        const RichTableGrid grid = BuildTableGrid(tableBlock_);
        for (size_t r = 0; r < tableBlock_.tableRows.size(); ++r) {
            const RowBorders borders = r < rowTableBorders_.size() ? rowTableBorders_[r] : RowBorders{};
            for (size_t c = 0; c < tableBlock_.tableRows[r].cells.size(); ++c) {
                RichTableCell& cell = tableBlock_.tableRows[r].cells[c];
                int top = 0, left = 0;
                grid.OriginOf(static_cast<int>(r), static_cast<int>(c), top, left);
                const bool lastRow = top + std::max(1, cell.rowSpan) - 1 >= grid.rowCount - 1;
                const bool lastColumn = left + std::max(1, cell.columnSpan) - 1 >= grid.columnCount - 1;
                auto settle = [&](RichBorder& side, int outer, int inner, bool onEdge) {
                    if (side.widthPt >= 0.0f) return;
                    side = borders.present ? borders.sides[onEdge ? outer : inner] : RichBorder{};
                };
                settle(cell.borderTop, 0, 4, top == 0);
                settle(cell.borderLeft, 1, 5, left == 0);
                settle(cell.borderBottom, 2, 4, lastRow);
                settle(cell.borderRight, 3, 5, lastColumn);
            }
        }
    }

    void CloseTable() {
        if (!tableOpen_) return;
        if (cellHasParagraph_ || !row_.cells.empty()) {
            DocParaProps none;
            FinishRow(none);
        }
        ResolveTableBorders();
        // Widths only hold when every row has the same number of cells.
        size_t columns = tableBlock_.tableColumnWidths.size();
        for (const auto& row : tableBlock_.tableRows) {
            if (row.cells.size() != columns) { tableBlock_.tableColumnWidths.clear(); break; }
        }
        if (!tableBlock_.tableRows.empty()) doc_.blocks.push_back(std::move(tableBlock_));
        tableOpen_ = false;
        tableBlock_ = RichDocBlock{};
        rowTableBorders_.clear();
    }
};

} // namespace

// ===== LEGACY .DOC IMPORT =====

bool UCWordDocumentIO::LoadDoc(const std::string& filePath, UCRichDocument& outDocument,
                               std::string& outError) {
    outDocument = UCRichDocument{};
    outError.clear();

    CfbReader cfb;
    if (!cfb.Load(filePath, outError)) return false;

    std::vector<uint8_t> wordStream;
    if (!cfb.ReadStream("WordDocument", wordStream) || wordStream.size() < 0x200) {
        outError = "The file has no readable WordDocument stream";
        return false;
    }
    if (ReadU16(wordStream, 0) != 0xA5EC) {
        outError = "The WordDocument stream is not a Word 97-2003 document";
        return false;
    }
    if (ReadU16(wordStream, 0x0A) & 0x0100) {
        outError = "The document is encrypted";
        return false;
    }

    const bool useTable1 = (ReadU16(wordStream, 0x0A) & 0x0200) != 0;
    std::vector<uint8_t> tableStream;
    if (!cfb.ReadStream(useTable1 ? "1Table" : "0Table", tableStream)) {
        outError = "The document's table stream is missing";
        return false;
    }
    std::vector<uint8_t> dataStream;
    cfb.ReadStream("Data", dataStream);    // pictures only; optional

    DocReader reader(wordStream, tableStream, dataStream, outDocument);
    if (!reader.Read(outError)) return false;

    // Drop trailing empty paragraphs left by the final paragraph mark.
    while (!outDocument.blocks.empty() && outDocument.blocks.back().type == RichBlockType::Paragraph
           && outDocument.blocks.back().runs.empty()) {
        outDocument.blocks.pop_back();
    }
    if (outDocument.blocks.empty()) {
        outError = "No text could be extracted from the document";
        return false;
    }
    return true;
}

bool UCWordDocumentIO::LoadDocText(const std::string& filePath, UCRichDocument& outDocument,
                                   std::string& outError) {
    return LoadDoc(filePath, outDocument, outError);
}

} // namespace UltraCanvas
