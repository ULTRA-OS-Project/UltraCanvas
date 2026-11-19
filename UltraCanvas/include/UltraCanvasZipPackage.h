// include/UltraCanvasZipPackage.h
// Reusable ZIP container reader/writer for package-based document formats
// (ODF: .odt/.ods — OPC/OOXML: .docx/.xlsx). Wraps the vendored miniz
// library behind a pimpl so consumers never see miniz types.
// The writer supports the ODF rule that the "mimetype" entry must be the
// first entry and stored uncompressed.
// Version: 1.3.0 - UCZipInflateRaw: a raw deflate stream, as an encrypted ODF entry holds
// Version: 1.2.0
// Last Modified: 2026-11-18
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

    // Opens a ZIP file from disk; `filePath` is UTF-8 on every platform.
    // Reads the central directory only, and keeps the file open - shared, so
    // other programs may still read and write it - until Close(). Returns
    // false (see GetLastError) if the file cannot be opened or is not a
    // valid ZIP archive.
    bool Open(const std::string& filePath);
    // Opens a ZIP archive held in memory - a package decrypted from a
    // password-protected Office file, say. The reader keeps the bytes until
    // Close().
    bool OpenFromMemory(std::vector<uint8_t> data);
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
    // Builds the archive in memory instead - to encrypt it before it is
    // written (a password-protected .xlsx / .docx). TakeBytes() hands it
    // over after Finalize().
    bool OpenInMemory();
    std::vector<uint8_t> TakeBytes();
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

// Inflates a raw deflate stream (RFC 1951: no zlib or gzip wrapper) - what a
// ZIP entry holds, and what an encrypted OpenDocument entry holds once
// decrypted (UltraCanvasOdfEncryption.h). False, with `out` empty, for a
// damaged stream, one that ends early, or one that would grow past `limit`
// bytes.
bool UCZipInflateRaw(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t limit);

} // namespace UltraCanvas
