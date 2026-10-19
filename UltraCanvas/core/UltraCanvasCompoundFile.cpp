// core/UltraCanvasCompoundFile.cpp
// OLE2 Compound File Binary reader and writer - see UltraCanvasCompoundFile.h.
// Moved here from the .doc importer (Plugins/Documents/Word), which was its
// only user until the .xls reader needed the same container.
// Version: 1.1.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework

#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
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

// ===== WRITER =====

namespace {

// kEndOfChain and kNoStream are the reader's, above.
constexpr uint32_t kFreeSect = 0xFFFFFFFFu;
constexpr uint32_t kFatSect = 0xFFFFFFFDu;
constexpr uint32_t kDifSect = 0xFFFFFFFCu;
constexpr size_t kSector = 512;
constexpr size_t kMiniSector = 64;
constexpr size_t kMiniCutoff = 4096;

void Put16(std::vector<uint8_t>& out, size_t at, uint32_t v) {
    out[at] = static_cast<uint8_t>(v);
    out[at + 1] = static_cast<uint8_t>(v >> 8);
}
void Put32(std::vector<uint8_t>& out, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) out[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

size_t SectorsFor(size_t bytes, size_t unit) { return (bytes + unit - 1) / unit; }

// [MS-CFB] 2.6.4: shorter names first, then the upper-cased names compared
// unit by unit.
bool CfbNameLess(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return a.size() < b.size();
    for (size_t i = 0; i < a.size(); ++i) {
        const int ca = std::toupper(static_cast<unsigned char>(a[i]));
        const int cb = std::toupper(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb;
    }
    return false;
}

bool CfbNameEqual(const std::string& a, const std::string& b) {
    return !CfbNameLess(a, b) && !CfbNameLess(b, a);
}

} // namespace

bool UCCompoundFileWriter::AddStream(const std::string& path, std::vector<uint8_t> data) {
    size_t parent = 0;
    size_t start = 0;
    while (true) {
        const size_t slash = path.find('/', start);
        const std::string name = path.substr(start, slash == std::string::npos ? std::string::npos
                                                                               : slash - start);
        if (name.empty() || name.size() > 31) return false;
        for (char c : name) {
            if (static_cast<unsigned char>(c) >= 0x80 || c == '/' || c == '\\' || c == ':' || c == '!')
                return false;
        }
        const bool last = slash == std::string::npos;
        size_t found = SIZE_MAX;
        for (size_t child : nodes_[parent].children)
            if (CfbNameEqual(nodes_[child].name, name)) found = child;
        if (found != SIZE_MAX && nodes_[found].storage == last) return false;   // stream vs storage
        if (found == SIZE_MAX) {
            nodes_.push_back(Node{name, !last, {}, {}});
            found = nodes_.size() - 1;
            nodes_[parent].children.push_back(found);
        }
        if (last) {
            nodes_[found].data = std::move(data);
            return true;
        }
        parent = found;
        start = slash + 1;
    }
}

std::vector<uint8_t> UCCompoundFileWriter::Build() const {
    const size_t entries = nodes_.size();

    // Stream placement: small ones in the mini stream, the rest in sectors.
    std::vector<uint32_t> startOf(entries, kEndOfChain);
    std::vector<uint8_t> miniStream;
    std::vector<uint32_t> miniFat;
    std::vector<uint32_t> fat;       // grows as sectors are handed out
    auto chain = [](std::vector<uint32_t>& table, size_t first, size_t count) {
        for (size_t i = 0; i < count; ++i)
            table[first + i] = i + 1 < count ? static_cast<uint32_t>(first + i + 1) : kEndOfChain;
    };
    std::vector<size_t> bigStreams;
    for (size_t i = 1; i < entries; ++i) {
        const Node& n = nodes_[i];
        if (n.storage || n.data.empty()) continue;
        if (n.data.size() >= kMiniCutoff) {
            bigStreams.push_back(i);
            continue;
        }
        const size_t first = miniStream.size() / kMiniSector;
        const size_t count = SectorsFor(n.data.size(), kMiniSector);
        miniStream.insert(miniStream.end(), n.data.begin(), n.data.end());
        miniStream.resize((first + count) * kMiniSector, 0);
        miniFat.resize(first + count, kFreeSect);
        chain(miniFat, first, count);
        startOf[i] = static_cast<uint32_t>(first);
    }

    // Sectors in file order: the big streams, the mini stream, the mini FAT,
    // the directory, then the FAT and the DIFAT that describe them all.
    size_t next = 0;
    auto allocate = [&](size_t count) {
        const size_t first = next;
        next += count;
        if (fat.size() < next) fat.resize(next, kFreeSect);
        if (count) chain(fat, first, count);
        return first;
    };
    for (size_t i : bigStreams) startOf[i] = static_cast<uint32_t>(allocate(SectorsFor(nodes_[i].data.size(), kSector)));
    const size_t miniStreamSectors = SectorsFor(miniStream.size(), kSector);
    const size_t miniStreamStart = allocate(miniStreamSectors);
    const size_t miniFatSectors = SectorsFor(miniFat.size() * 4, kSector);
    const size_t miniFatStart = allocate(miniFatSectors);
    const size_t dirSectors = SectorsFor(entries * 128, kSector);
    const size_t dirStart = allocate(dirSectors);

    // FAT sectors F and DIFAT sectors D must cover every sector, their own included.
    size_t fatSectors = 1, difatSectors = 0;
    while (true) {
        difatSectors = fatSectors <= 109 ? 0 : SectorsFor(fatSectors - 109, 127);
        if (fatSectors * 128 >= next + fatSectors + difatSectors) break;
        ++fatSectors;
    }
    const size_t fatStart = next;
    const size_t difatStart = fatStart + fatSectors;
    next += fatSectors + difatSectors;
    fat.resize(fatSectors * 128, kFreeSect);
    for (size_t i = 0; i < fatSectors; ++i) fat[fatStart + i] = kFatSect;
    for (size_t i = 0; i < difatSectors; ++i) fat[difatStart + i] = kDifSect;

    // Each storage's children as a balanced tree: the deepest level is red
    // when it is not full, every other node black - a valid red-black tree.
    std::vector<uint32_t> left(entries, kNoStream), right(entries, kNoStream), child(entries, kNoStream);
    std::vector<uint8_t> red(entries, 0);
    for (size_t s = 0; s < entries; ++s) {
        if (!nodes_[s].storage || nodes_[s].children.empty()) continue;
        std::vector<size_t> sorted = nodes_[s].children;
        std::sort(sorted.begin(), sorted.end(),
                  [&](size_t a, size_t b) { return CfbNameLess(nodes_[a].name, nodes_[b].name); });
        std::vector<int> depth(entries, 0);
        int maxDepth = 0;
        std::function<uint32_t(size_t, size_t, int)> build = [&](size_t lo, size_t hi, int d) -> uint32_t {
            if (lo >= hi) return kNoStream;
            const size_t mid = lo + (hi - lo) / 2;
            const size_t node = sorted[mid];
            depth[node] = d;
            maxDepth = std::max(maxDepth, d);
            left[node] = build(lo, mid, d + 1);
            right[node] = build(mid + 1, hi, d + 1);
            return static_cast<uint32_t>(node);
        };
        child[s] = build(0, sorted.size(), 0);
        const bool full = sorted.size() == (size_t(1) << (maxDepth + 1)) - 1;
        if (!full)
            for (size_t node : sorted) red[node] = depth[node] == maxDepth;
    }

    std::vector<uint8_t> out((1 + next) * kSector, 0);
    // Header.
    static const uint8_t kMagic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    std::memcpy(out.data(), kMagic, 8);
    Put16(out, 0x18, 0x003E);                      // minor version
    Put16(out, 0x1A, 3);                           // major version
    Put16(out, 0x1C, 0xFFFE);                      // byte order
    Put16(out, 0x1E, 9);                           // 512-byte sectors
    Put16(out, 0x20, 6);                           // 64-byte mini sectors
    Put32(out, 0x2C, static_cast<uint32_t>(fatSectors));
    Put32(out, 0x30, static_cast<uint32_t>(dirStart));
    Put32(out, 0x38, static_cast<uint32_t>(kMiniCutoff));
    Put32(out, 0x3C, miniFatSectors ? static_cast<uint32_t>(miniFatStart) : kEndOfChain);
    Put32(out, 0x40, static_cast<uint32_t>(miniFatSectors));
    Put32(out, 0x44, difatSectors ? static_cast<uint32_t>(difatStart) : kEndOfChain);
    Put32(out, 0x48, static_cast<uint32_t>(difatSectors));
    for (size_t i = 0; i < 109; ++i)
        Put32(out, 0x4C + 4 * i, i < fatSectors ? static_cast<uint32_t>(fatStart + i) : kFreeSect);

    auto sectorAt = [&](size_t sector) { return (1 + sector) * kSector; };
    for (size_t i : bigStreams)
        std::memcpy(out.data() + sectorAt(startOf[i]), nodes_[i].data.data(), nodes_[i].data.size());
    if (!miniStream.empty())
        std::memcpy(out.data() + sectorAt(miniStreamStart), miniStream.data(), miniStream.size());
    for (size_t i = 0; i < miniFatSectors * 128; ++i)
        Put32(out, sectorAt(miniFatStart) + 4 * i, i < miniFat.size() ? miniFat[i] : kFreeSect);

    // Directory.
    for (size_t e = 0; e < dirSectors * 4; ++e) {
        const size_t at = sectorAt(dirStart) + 128 * e;
        if (e >= entries) {
            Put32(out, at + 0x44, kNoStream);
            Put32(out, at + 0x48, kNoStream);
            Put32(out, at + 0x4C, kNoStream);
            continue;
        }
        const Node& n = nodes_[e];
        for (size_t c = 0; c < n.name.size(); ++c) Put16(out, at + 2 * c, static_cast<unsigned char>(n.name[c]));
        Put16(out, at + 0x40, static_cast<uint32_t>(2 * (n.name.size() + 1)));
        out[at + 0x42] = e == 0 ? 5 : (n.storage ? 1 : 2);
        out[at + 0x43] = red[e] ? 0 : 1;
        Put32(out, at + 0x44, left[e]);
        Put32(out, at + 0x48, right[e]);
        Put32(out, at + 0x4C, child[e]);
        if (e == 0) {
            Put32(out, at + 0x74, miniStreamSectors ? static_cast<uint32_t>(miniStreamStart) : kEndOfChain);
            Put32(out, at + 0x78, static_cast<uint32_t>(miniStream.size()));
        } else if (!n.storage) {
            Put32(out, at + 0x74, startOf[e]);
            Put32(out, at + 0x78, static_cast<uint32_t>(n.data.size()));
        }
    }

    // FAT, then the DIFAT entries the header had no room for.
    for (size_t i = 0; i < fat.size(); ++i) Put32(out, sectorAt(fatStart) + 4 * i, fat[i]);
    for (size_t d = 0; d < difatSectors; ++d) {
        const size_t at = sectorAt(difatStart + d);
        for (size_t k = 0; k < 127; ++k) {
            const size_t fatIndex = 109 + d * 127 + k;
            Put32(out, at + 4 * k, fatIndex < fatSectors ? static_cast<uint32_t>(fatStart + fatIndex) : kFreeSect);
        }
        Put32(out, at + 4 * 127, d + 1 < difatSectors ? static_cast<uint32_t>(difatStart + d + 1) : kEndOfChain);
    }
    return out;
}

} // namespace UltraCanvas
