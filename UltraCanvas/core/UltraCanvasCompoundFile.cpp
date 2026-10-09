// core/UltraCanvasCompoundFile.cpp
// OLE2 Compound File Binary reader - see UltraCanvasCompoundFile.h.
// Moved here from the .doc importer (Plugins/Documents/Word), which was its
// only user until the .xls reader needed the same container.
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework

#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <cstring>
#include <iterator>

namespace UltraCanvas {

namespace {

constexpr uint32_t kMaxChainLength = 1u << 22;   // loop guard for corrupt FAT chains
constexpr uint32_t kEndOfChain = 0xFFFFFFFEu;    // also covers FREESECT (0xFFFFFFFF)
constexpr uint32_t kNoStream = 0xFFFFFFFFu;
constexpr uint8_t kSignature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

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

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
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

// Directory names compare case-insensitively ([MS-CFB] 2.6.4); the names the
// Office formats use are ASCII, so folding ASCII is enough.
bool NamesEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 32);
        if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 32);
        if (x != y) return false;
    }
    return true;
}

} // namespace

bool UCCompoundFileReader::HasSignature(const std::string& filePath) {
    std::FILE* f = OpenFileUtf8(filePath, "rb");
    if (!f) return false;
    uint8_t head[8] = {0};
    const size_t got = std::fread(head, 1, sizeof(head), f);
    std::fclose(f);
    return got == sizeof(head) && std::memcmp(head, kSignature, sizeof(head)) == 0;
}

bool UCCompoundFileReader::HasSignature(const std::vector<uint8_t>& data) {
    return data.size() >= sizeof(kSignature) &&
           std::memcmp(data.data(), kSignature, sizeof(kSignature)) == 0;
}

bool UCCompoundFileReader::Open(const std::string& filePath) {
    open_ = false;
    lastError_.clear();
    std::FILE* f = OpenFileUtf8(filePath, "rb");
    if (!f) {
        lastError_ = "Cannot open file: " + filePath;
        return false;
    }
    std::vector<uint8_t> data;
    uint8_t buffer[64 * 1024];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        data.insert(data.end(), buffer, buffer + got);
    }
    std::fclose(f);
    return OpenFromMemory(std::move(data));
}

bool UCCompoundFileReader::OpenFromMemory(std::vector<uint8_t> data) {
    open_ = false;
    lastError_.clear();
    data_ = std::move(data);
    fat_.clear();
    miniFat_.clear();
    miniStream_.clear();
    dirs_.clear();
    rootChildren_.clear();
    open_ = Parse();
    return open_;
}

bool UCCompoundFileReader::Parse() {
    if (data_.size() < 512 || !HasSignature(data_)) {
        lastError_ = "Not an OLE2 compound file";
        return false;
    }
    const uint16_t majorVersion = ReadU16(data_, 0x1A);
    const uint16_t sectorShift = ReadU16(data_, 0x1E);
    const uint16_t miniSectorShift = ReadU16(data_, 0x20);
    if (sectorShift < 7 || sectorShift > 20 || miniSectorShift > sectorShift) {
        lastError_ = "Invalid compound file sector size";
        return false;
    }
    sectorSize_ = static_cast<size_t>(1) << sectorShift;
    miniSectorSize_ = static_cast<size_t>(1) << miniSectorShift;
    miniCutoff_ = ReadU32(data_, 0x38);

    if (!LoadFat() || !LoadDirectory(ReadU32(data_, 0x30), majorVersion)) {
        lastError_ = "Corrupt compound file structure";
        return false;
    }
    LoadMiniFat(ReadU32(data_, 0x3C));
    // The mini stream is the root entry's own stream, read via the regular
    // FAT whatever its size.
    if (!dirs_.empty()) {
        ReadChain(dirs_[0].startSector, dirs_[0].size, miniStream_);
    }
    CollectRootChildren();
    return true;
}

size_t UCCompoundFileReader::SectorOffset(uint32_t sector) const {
    return (static_cast<size_t>(sector) + 1) * sectorSize_;
}

void UCCompoundFileReader::AppendFatSector(uint32_t sector) {
    const size_t offset = SectorOffset(sector);
    for (size_t i = 0; i + 4 <= sectorSize_ && offset + i + 4 <= data_.size(); i += 4) {
        fat_.push_back(ReadU32(data_, offset + i));
    }
}

bool UCCompoundFileReader::LoadFat() {
    // 109 DIFAT entries live in the header; more come from DIFAT sectors.
    for (int i = 0; i < 109; ++i) {
        const uint32_t sector = ReadU32(data_, 0x4C + i * 4);
        if (sector >= kEndOfChain) break;
        AppendFatSector(sector);
    }
    uint32_t difatSector = ReadU32(data_, 0x44);
    const uint32_t difatCount = ReadU32(data_, 0x48);
    for (uint32_t d = 0; d < difatCount && difatSector < kEndOfChain; ++d) {
        const size_t offset = SectorOffset(difatSector);
        if (offset + sectorSize_ > data_.size()) break;
        const size_t entries = sectorSize_ / 4 - 1;
        for (size_t i = 0; i < entries; ++i) {
            const uint32_t sector = ReadU32(data_, offset + i * 4);
            if (sector < kEndOfChain) AppendFatSector(sector);
        }
        difatSector = ReadU32(data_, offset + entries * 4);
    }
    return !fat_.empty();
}

void UCCompoundFileReader::LoadMiniFat(uint32_t firstSector) {
    uint32_t sector = firstSector;
    uint32_t guard = 0;
    while (sector < kEndOfChain && guard++ < kMaxChainLength) {
        const size_t offset = SectorOffset(sector);
        if (offset + sectorSize_ > data_.size()) break;
        for (size_t i = 0; i + 4 <= sectorSize_; i += 4) {
            miniFat_.push_back(ReadU32(data_, offset + i));
        }
        sector = (sector < fat_.size()) ? fat_[sector] : kEndOfChain;
    }
}

bool UCCompoundFileReader::LoadDirectory(uint32_t firstSector, uint16_t majorVersion) {
    std::vector<uint8_t> dirData;
    if (!ReadChain(firstSector, UINT64_MAX, dirData)) return false;
    for (size_t offset = 0; offset + 128 <= dirData.size(); offset += 128) {
        DirEntry entry;
        // UTF-16LE, NUL-terminated; the length counts bytes including the NUL.
        const uint16_t nameBytes = ReadU16(dirData, offset + 0x40);
        if (nameBytes >= 2 && nameBytes <= 64) {
            for (size_t i = 0; i + 2 < static_cast<size_t>(nameBytes); i += 2) {
                uint32_t ch = ReadU16(dirData, offset + i);
                if (ch >= 0xD800 && ch <= 0xDBFF && i + 4 < nameBytes) {
                    const uint32_t low = ReadU16(dirData, offset + i + 2);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        ch = 0x10000 + ((ch - 0xD800) << 10) + (low - 0xDC00);
                        i += 2;
                    }
                }
                if (ch != 0) AppendUtf8(entry.name, ch);
            }
        }
        entry.type = dirData[offset + 0x42];
        entry.left = ReadU32(dirData, offset + 0x44);
        entry.right = ReadU32(dirData, offset + 0x48);
        entry.child = ReadU32(dirData, offset + 0x4C);
        entry.startSector = ReadU32(dirData, offset + 0x74);
        entry.size = ReadU32(dirData, offset + 0x78);
        // Version 3 files may leave garbage in the high half of the size.
        if (majorVersion >= 4) {
            entry.size |= static_cast<uint64_t>(ReadU32(dirData, offset + 0x7C)) << 32;
        }
        dirs_.push_back(std::move(entry));
    }
    return !dirs_.empty();
}

void UCCompoundFileReader::CollectRootChildren() {
    if (dirs_.empty()) return;
    // The children of a storage form a (red-black) binary tree; walk it with
    // an explicit stack and a visited set so a looping tree cannot hang us.
    std::vector<bool> visited(dirs_.size(), false);
    std::vector<uint32_t> stack;
    if (dirs_[0].child != kNoStream) stack.push_back(dirs_[0].child);
    while (!stack.empty()) {
        const uint32_t index = stack.back();
        stack.pop_back();
        if (index >= dirs_.size() || visited[index]) continue;
        visited[index] = true;
        rootChildren_.push_back(index);
        if (dirs_[index].left != kNoStream) stack.push_back(dirs_[index].left);
        if (dirs_[index].right != kNoStream) stack.push_back(dirs_[index].right);
    }
}

bool UCCompoundFileReader::ReadChain(uint32_t firstSector, uint64_t maxSize,
                                     std::vector<uint8_t>& out) const {
    out.clear();
    uint32_t sector = firstSector;
    uint32_t guard = 0;
    while (sector < kEndOfChain && guard++ < kMaxChainLength) {
        const size_t offset = SectorOffset(sector);
        if (offset + sectorSize_ > data_.size()) {
            // The last sector of a file may be cut short; take what is there.
            if (offset < data_.size()) out.insert(out.end(), data_.begin() + offset, data_.end());
            break;
        }
        out.insert(out.end(), data_.begin() + offset, data_.begin() + offset + sectorSize_);
        if (maxSize != UINT64_MAX && out.size() >= maxSize) break;
        sector = (sector < fat_.size()) ? fat_[sector] : kEndOfChain;
    }
    if (maxSize != UINT64_MAX && out.size() > maxSize) out.resize(static_cast<size_t>(maxSize));
    return !out.empty();
}

bool UCCompoundFileReader::ReadMiniChain(uint32_t firstSector, uint64_t maxSize,
                                         std::vector<uint8_t>& out) const {
    out.clear();
    uint32_t sector = firstSector;
    uint32_t guard = 0;
    while (sector < kEndOfChain && guard++ < kMaxChainLength) {
        const size_t offset = static_cast<size_t>(sector) * miniSectorSize_;
        if (offset + miniSectorSize_ > miniStream_.size()) break;
        out.insert(out.end(), miniStream_.begin() + offset,
                   miniStream_.begin() + offset + miniSectorSize_);
        if (out.size() >= maxSize) break;
        sector = (sector < miniFat_.size()) ? miniFat_[sector] : kEndOfChain;
    }
    if (out.size() > maxSize) out.resize(static_cast<size_t>(maxSize));
    return !out.empty();
}

bool UCCompoundFileReader::ReadEntry(const DirEntry& entry, std::vector<uint8_t>& out) const {
    out.clear();
    if (entry.size == 0) return true;
    if (entry.size < miniCutoff_) return ReadMiniChain(entry.startSector, entry.size, out);
    return ReadChain(entry.startSector, entry.size, out);
}

const UCCompoundFileReader::DirEntry* UCCompoundFileReader::FindStream(
        const std::string& name) const {
    bool rootHasStreams = false;
    for (uint32_t index : rootChildren_) {
        const DirEntry& entry = dirs_[index];
        if (entry.type != 2) continue;
        rootHasStreams = true;
        if (NamesEqual(entry.name, name)) return &entry;
    }
    if (rootHasStreams) return nullptr;
    // No tree to walk (a writer that left the child links empty, or a
    // damaged directory): take the first stream of that name anywhere.
    for (size_t i = 1; i < dirs_.size(); ++i) {
        if (dirs_[i].type == 2 && NamesEqual(dirs_[i].name, name)) return &dirs_[i];
    }
    return nullptr;
}

bool UCCompoundFileReader::ReadStream(const std::string& name, std::vector<uint8_t>& out) const {
    out.clear();
    if (!open_) return false;
    const DirEntry* entry = FindStream(name);
    return entry && ReadEntry(*entry, out);
}

bool UCCompoundFileReader::HasStream(const std::string& name) const {
    return open_ && FindStream(name) != nullptr;
}

std::vector<std::string> UCCompoundFileReader::StreamNames() const {
    std::vector<std::string> names;
    if (!open_) return names;
    for (uint32_t index : rootChildren_) {
        if (dirs_[index].type == 2) names.push_back(dirs_[index].name);
    }
    return names;
}

} // namespace UltraCanvas
