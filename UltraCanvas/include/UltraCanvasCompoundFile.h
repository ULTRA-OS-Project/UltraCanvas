// include/UltraCanvasCompoundFile.h
// Access to an OLE2 Compound File Binary (CFB) container - the
// "structured storage" mini filesystem the legacy Microsoft Office formats are
// stored in: Word 97-2003 (.doc), Excel 5.0-2003 (.xls), PowerPoint (.ppt),
// Outlook (.msg). A compound file is a FAT of fixed-size sectors holding named
// streams; small streams live in a second, finer-grained "mini" stream.
// The .doc importer (Plugins/Documents/Word) and the .xls reader
// (UltraCanvasSpreadsheetXls.h) both read their streams through this class.
// UCCompoundFileWriter builds one, for the password-protected Office files
// UltraCanvasOfficeEncryption.h writes.
// Specification: [MS-CFB] Compound File Binary File Format.
// Version: 1.2.0 - ReadStreamAt / HasStreamAt / StreamNamesAt / StorageNamesAt: below the root
// Version: 1.1.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

class UCCompoundFileReader {
public:
    // True when the file starts with the compound-file signature
    // (D0 CF 11 E0 A1 B1 1A E1). Reads eight bytes; `filePath` is UTF-8.
    static bool HasSignature(const std::string& filePath);
    // The same test for bytes already in memory.
    static bool HasSignature(const std::vector<uint8_t>& data);

    // Reads the whole container into memory and parses its FAT, mini FAT and
    // directory. `filePath` is UTF-8 on every platform. Returns false (see
    // GetLastError) when the file cannot be opened or is not a compound file.
    bool Open(const std::string& filePath);
    // The same for a container already in memory.
    bool OpenFromMemory(std::vector<uint8_t> data);
    bool IsOpen() const { return open_; }

    // Reads a stream stored directly in the root storage. Names compare
    // without regard to ASCII case, as the format specifies. When the root's
    // directory tree lists no streams at all (damaged, or a writer that left
    // the links empty) the first stream of that name anywhere is taken.
    // Returns false when no such stream exists or its sectors are missing.
    bool ReadStream(const std::string& name, std::vector<uint8_t>& out) const;
    bool HasStream(const std::string& name) const;

    // Names of the streams directly in the root storage, as UTF-8.
    std::vector<std::string> StreamNames() const;

    // Below the root: a stream by its '/'-separated path from the root
    // ("__attach_version1.0_#00000000/__substg1.0_37010102"), each name
    // compared without regard to ASCII case. Outlook's .msg keeps every
    // attachment and recipient in a storage of its own.
    bool ReadStreamAt(const std::string& path, std::vector<uint8_t>& out) const;
    bool HasStreamAt(const std::string& path) const;
    // The streams, and the storages, directly in the storage at
    // `storagePath` ("" is the root), as UTF-8 names.
    std::vector<std::string> StreamNamesAt(const std::string& storagePath) const;
    std::vector<std::string> StorageNamesAt(const std::string& storagePath) const;

    // Reason for the most recent failed Open. Empty after success.
    const std::string& GetLastError() const { return lastError_; }

private:
    struct DirEntry {
        std::string name;          // UTF-8
        uint8_t type = 0;          // 1 = storage, 2 = stream, 5 = root
        uint32_t left = 0xFFFFFFFFu;
        uint32_t right = 0xFFFFFFFFu;
        uint32_t child = 0xFFFFFFFFu;
        uint32_t startSector = 0;
        uint64_t size = 0;
    };

    std::vector<uint8_t> data_;
    size_t sectorSize_ = 512;
    size_t miniSectorSize_ = 64;
    uint64_t miniCutoff_ = 4096;
    std::vector<uint32_t> fat_;
    std::vector<uint32_t> miniFat_;
    std::vector<uint8_t> miniStream_;
    std::vector<DirEntry> dirs_;
    std::vector<uint32_t> rootChildren_;   // directory indices
    bool open_ = false;
    std::string lastError_;

    bool Parse();
    size_t SectorOffset(uint32_t sector) const;
    void AppendFatSector(uint32_t sector);
    bool LoadFat();
    void LoadMiniFat(uint32_t firstSector);
    bool LoadDirectory(uint32_t firstSector, uint16_t majorVersion);
    void CollectRootChildren();
    // The directory entries directly in the storage `dirIndex` (its
    // children's tree walked; a looping tree cannot hang it).
    std::vector<uint32_t> ChildrenOf(uint32_t dirIndex) const;
    // The entry at a '/'-separated path from the root; null when none.
    const DirEntry* FindEntryAt(const std::string& path) const;
    bool ReadChain(uint32_t firstSector, uint64_t maxSize, std::vector<uint8_t>& out) const;
    bool ReadMiniChain(uint32_t firstSector, uint64_t maxSize, std::vector<uint8_t>& out) const;
    bool ReadEntry(const DirEntry& entry, std::vector<uint8_t>& out) const;
    const DirEntry* FindStream(const std::string& name) const;
};

// Builds a compound file in memory: version 3 (512-byte sectors), streams
// under 4096 bytes in the mini stream as the format requires, a FAT of any
// size (DIFAT sectors past the header's 109 entries), and each storage's
// children in a valid red-black tree. Readers that check the tree - Office
// among them - accept it.
class UCCompoundFileWriter {
public:
    // `path` names the stream, its storages separated by '/', e.g.
    // "\x06DataSpaces/TransformInfo/StrongEncryptionTransform/\x06Primary".
    // Storages are created on the way. A name may have at most 31 characters
    // (ASCII here); adding the same path twice replaces the stream. Returns
    // false for an empty, over-long or clashing name.
    bool AddStream(const std::string& path, std::vector<uint8_t> data);

    // The whole file.
    std::vector<uint8_t> Build() const;

private:
    struct Node {
        std::string name;
        bool storage = false;
        std::vector<uint8_t> data;
        std::vector<size_t> children;   // indices into nodes_
    };
    std::vector<Node> nodes_{Node{"Root Entry", true, {}, {}}};
};

} // namespace UltraCanvas
