// Plugins/Documents/eBook/EBookArchive.h
// ZIP container access + raw DEFLATE helpers for eBook engines, built on the
// vendored miniz (third_party/miniz). EPUB and packaged OEB/FB2.zip are ZIP
// archives; LRF/RB carry zlib/deflate streams.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace UltraCanvas {

class EBookArchive {
public:
    EBookArchive() = default;
    ~EBookArchive();

    EBookArchive(const EBookArchive&) = delete;
    EBookArchive& operator=(const EBookArchive&) = delete;

    // Reads only the central directory: entries are inflated from the file
    // as they are asked for, so a caller that wants one picture out of a
    // large book does not read the book. The file stays open until Close().
    // `filePath` is UTF-8 on every platform.
    bool OpenFromFile(const std::string& filePath);
    // Takes ownership of the buffer; it must stay alive while the archive is
    // open (the class keeps it).
    bool OpenFromMemory(std::vector<uint8_t> data);
    void Close();
    bool IsOpen() const { return opened; }

    std::vector<std::string> FileNames() const;
    bool Contains(const std::string& name) const;
    // Uncompressed size of a stored file (matched like ReadFile), or 0 when
    // it is absent. Lets a caller refuse an entry before inflating it.
    uint64_t FileSize(const std::string& name) const;

    // Reads a stored file. `name` is matched exactly first, then
    // case-insensitively (some EPUB producers get the case wrong).
    bool ReadFile(const std::string& name, std::vector<uint8_t>& out) const;
    std::string ReadTextFile(const std::string& name) const;

    std::string GetLastError() const { return lastError; }

    // ---- signatures / raw streams ----

    static bool IsZipData(const uint8_t* data, size_t size);

    // zlib-wrapped DEFLATE stream (CMF/FLG header). expectedSize is a hint.
    static bool InflateZlib(const uint8_t* data, size_t size,
                            std::vector<uint8_t>& out);
    // Raw DEFLATE stream (no header).
    static bool InflateRaw(const uint8_t* data, size_t size,
                           std::vector<uint8_t>& out);

private:
    void* zip = nullptr;          // mz_zip_archive*, kept out of the header
    std::vector<uint8_t> buffer;  // backing store for memory archives
    std::FILE* file = nullptr;    // backing file of OpenFromFile archives
    bool opened = false;
    mutable std::string lastError;

    int FindIndex(const std::string& name) const;
};

} // namespace UltraCanvas
