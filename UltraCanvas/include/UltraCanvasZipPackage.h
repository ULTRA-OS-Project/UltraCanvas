// include/UltraCanvasZipPackage.h
// Reusable ZIP container reader/writer for package-based document formats
// (ODF: .odt/.ods — OPC/OOXML: .docx/.xlsx). Wraps the vendored miniz
// library behind a pimpl so consumers never see miniz types.
// The writer supports the ODF rule that the "mimetype" entry must be the
// first entry and stored uncompressed.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// Read-only view of a ZIP package. Entry names use forward slashes and are
// matched exactly (ZIP archives are case-sensitive).
class UCZipPackageReader {
public:
    UCZipPackageReader();
    ~UCZipPackageReader();
    UCZipPackageReader(const UCZipPackageReader&) = delete;
    UCZipPackageReader& operator=(const UCZipPackageReader&) = delete;

    // Opens a ZIP file from disk. Returns false (see GetLastError) if the
    // file is missing or not a valid ZIP archive.
    bool Open(const std::string& filePath);
    void Close();
    bool IsOpen() const;

    bool HasEntry(const std::string& entryName) const;
    // Extracts an entry into text/bytes. Returns false if the entry is
    // absent or the archive data is corrupt.
    bool ReadEntry(const std::string& entryName, std::string& out) const;
    bool ReadEntry(const std::string& entryName, std::vector<uint8_t>& out) const;
    std::vector<std::string> EntryNames() const;

    // Reason for the most recent failed call. Empty after success.
    const std::string& GetLastError() const { return lastError_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::string lastError_;
};

// Streaming ZIP writer. Entries are written in AddEntry call order, so the
// ODF "mimetype first, stored" convention is honored by adding it first
// with compress=false. Finalize() must be called for a valid archive.
class UCZipPackageWriter {
public:
    UCZipPackageWriter();
    ~UCZipPackageWriter();
    UCZipPackageWriter(const UCZipPackageWriter&) = delete;
    UCZipPackageWriter& operator=(const UCZipPackageWriter&) = delete;

    // Creates/overwrites the target file.
    bool Open(const std::string& filePath);
    bool AddEntry(const std::string& entryName, const std::string& data, bool compress = true);
    bool AddEntry(const std::string& entryName, const void* data, size_t size, bool compress = true);
    // Writes the central directory and closes the file. After Finalize the
    // writer cannot accept further entries.
    bool Finalize();

    // Reason for the most recent failed call. Empty after success.
    const std::string& GetLastError() const { return lastError_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string lastError_;
};

} // namespace UltraCanvas
