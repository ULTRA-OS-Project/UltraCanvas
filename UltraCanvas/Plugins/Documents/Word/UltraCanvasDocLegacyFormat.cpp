// Plugins/Documents/Word/UltraCanvasDocLegacyFormat.cpp
// Text-only import for legacy Word 97-2003 (.doc) files.
// A .doc is an OLE2 Compound File Binary (CFB): a mini filesystem of
// FAT-chained sectors holding named streams. The document text lives in the
// "WordDocument" stream as pieces described by the piece table (Clx/PlcPcd)
// stored in the "0Table"/"1Table" stream — each piece is either 8-bit
// CP1252 or UTF-16LE. Formatting is intentionally not parsed (export goes
// to .docx); see Docs/UltraCanvas/ODT-DOCX-Support-Proposal.md §2.5.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <cstdint>
#include <cstring>
#include <fstream>

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

// Converts Word's control characters to plain text; tracks field state so
// field instructions (e.g. "HYPERLINK http://...") are dropped while the
// field's visible result text is kept.
class WordTextSink {
public:
    void PutChar(uint32_t codepoint) {
        bool wasCellMark = lastWasCellMark_;
        lastWasCellMark_ = false;
        switch (codepoint) {
            case 0x0D:                     // paragraph mark
            case 0x0B:                     // hard line break
            case 0x0C: text_.push_back('\n'); break;   // page/section break
            case 0x07:
                // Word marks cell ends AND row ends with 0x07; a row end
                // directly follows the last cell's mark, so a doubled mark
                // closes the row.
                if (wasCellMark) text_.push_back('\n');
                else text_.push_back('\t');
                lastWasCellMark_ = true;
                break;
            case 0x09: text_.push_back('\t'); break;
            case 0x13: fieldDepth_++; inFieldInstruction_ = true; break;
            case 0x14: inFieldInstruction_ = false; break;   // separator: result follows
            case 0x15:                                        // field end
                if (fieldDepth_ > 0) fieldDepth_--;
                if (fieldDepth_ == 0) inFieldInstruction_ = false;
                break;
            case 0x1E: text_.push_back('-'); break;    // non-breaking hyphen
            case 0x1F: break;                          // soft hyphen
            case 0xA0: AppendUtf8(text_, 0xA0); break; // keep NBSP
            default:
                if (codepoint >= 0x20 && !inFieldInstruction_) {
                    AppendUtf8(text_, codepoint);
                }
                break;
        }
    }

    std::string Take() { return std::move(text_); }

private:
    std::string text_;
    int fieldDepth_ = 0;
    bool inFieldInstruction_ = false;
    bool lastWasCellMark_ = false;
};

} // namespace

// ===== LEGACY .DOC TEXT IMPORT =====

bool UCWordDocumentIO::LoadDocText(const std::string& filePath, UCRichDocument& outDocument,
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

    // FIB: which table stream, main-body character count, piece-table location.
    bool useTable1 = (ReadU16(wordStream, 0x0A) & 0x0200) != 0;
    uint32_t ccpText = ReadU32(wordStream, 0x4C);
    uint32_t fcClx = ReadU32(wordStream, 0x1A2);
    uint32_t lcbClx = ReadU32(wordStream, 0x1A6);

    std::vector<uint8_t> tableStream;
    if (!cfb.ReadStream(useTable1 ? "1Table" : "0Table", tableStream)
        || fcClx + lcbClx > tableStream.size() || lcbClx == 0) {
        outError = "The document's piece table is missing or corrupt";
        return false;
    }

    // Clx: skip Prc property blocks (type 0x01) to reach the Pcdt (type 0x02).
    size_t pos = fcClx;
    size_t clxEnd = fcClx + lcbClx;
    size_t plcPcdStart = 0, plcPcdSize = 0;
    while (pos < clxEnd) {
        uint8_t clxt = tableStream[pos];
        if (clxt == 0x01) {
            uint16_t cb = ReadU16(tableStream, pos + 1);
            pos += 3 + cb;
        } else if (clxt == 0x02) {
            uint32_t lcb = ReadU32(tableStream, pos + 1);
            plcPcdStart = pos + 5;
            plcPcdSize = lcb;
            break;
        } else {
            break;
        }
    }
    if (plcPcdSize < 12 + 4 || plcPcdStart + plcPcdSize > tableStream.size()) {
        outError = "The document's piece table is missing or corrupt";
        return false;
    }

    // PlcPcd: n+1 character positions followed by n 8-byte piece descriptors.
    size_t pieceCount = (plcPcdSize - 4) / 12;
    size_t cpArray = plcPcdStart;
    size_t pcdArray = plcPcdStart + (pieceCount + 1) * 4;

    WordTextSink sink;
    uint32_t emitted = 0;
    for (size_t i = 0; i < pieceCount && emitted < ccpText; ++i) {
        uint32_t cpStart = ReadU32(tableStream, cpArray + i * 4);
        uint32_t cpEnd = ReadU32(tableStream, cpArray + (i + 1) * 4);
        if (cpEnd <= cpStart) continue;
        uint32_t count = cpEnd - cpStart;
        if (emitted + count > ccpText) count = ccpText - emitted;

        uint32_t fc = ReadU32(tableStream, pcdArray + i * 8 + 2);
        bool compressed = (fc & 0x40000000) != 0;
        uint32_t offset = fc & 0x3FFFFFFF;
        if (compressed) offset /= 2;

        if (compressed) {
            for (uint32_t c = 0; c < count && offset + c < wordStream.size(); ++c) {
                uint32_t cp = Cp1252ToUnicode(wordStream[offset + c]);
                if (cp) sink.PutChar(cp);
            }
        } else {
            for (uint32_t c = 0; c < count && offset + c * 2 + 1 < wordStream.size(); ++c) {
                uint32_t unit = ReadU16(wordStream, offset + c * 2);
                // Combine UTF-16 surrogate pairs.
                if (unit >= 0xD800 && unit < 0xDC00 && c + 1 < count
                    && offset + (c + 1) * 2 + 1 < wordStream.size()) {
                    uint32_t low = ReadU16(wordStream, offset + (c + 1) * 2);
                    if (low >= 0xDC00 && low < 0xE000) {
                        sink.PutChar(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                        ++c;
                        continue;
                    }
                }
                sink.PutChar(unit);
            }
        }
        emitted += count;
    }

    // One source line becomes one plain paragraph — formatting is not
    // recovered from the binary format by design.
    std::string text = sink.Take();
    std::string line;
    auto flushLine = [&]() {
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        if (!line.empty()) {
            RichTextRun run;
            run.text = line;
            block.runs.push_back(std::move(run));
        }
        outDocument.blocks.push_back(std::move(block));
        line.clear();
    };
    for (char c : text) {
        if (c == '\n') flushLine();
        else line.push_back(c);
    }
    if (!line.empty()) flushLine();

    // Drop trailing empty paragraphs left by the final paragraph mark.
    while (!outDocument.blocks.empty() && outDocument.blocks.back().runs.empty()) {
        outDocument.blocks.pop_back();
    }
    if (outDocument.blocks.empty()) {
        outError = "No text could be extracted from the document";
        return false;
    }
    return true;
}

} // namespace UltraCanvas
