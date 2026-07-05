// core/UltraCanvasZipPackage.cpp
// Implementation of the reusable ZIP package reader/writer.
// Reading wraps miniz's mz_zip_reader. Writing emits the ZIP structures
// directly (using miniz only for deflate + crc32): unlike miniz's own
// writer, local file headers carry the real sizes and NO data-descriptor
// flag — the ODF package readers in LibreOffice/OpenOffice reject archives
// whose "mimetype" entry uses a data descriptor (general-purpose bit 3).
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "UltraCanvasZipPackage.h"

#include "miniz.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace UltraCanvas {

// ===== READER =====

struct UCZipPackageReader::Impl {
    mz_zip_archive zip{};
    bool open = false;
};

UCZipPackageReader::UCZipPackageReader() : impl_(std::make_unique<Impl>()) {}

UCZipPackageReader::~UCZipPackageReader() {
    Close();
}

bool UCZipPackageReader::Open(const std::string& filePath) {
    Close();
    lastError_.clear();
    std::memset(&impl_->zip, 0, sizeof(impl_->zip));
    if (!mz_zip_reader_init_file(&impl_->zip, filePath.c_str(), 0)) {
        lastError_ = "Not a valid ZIP archive: " + filePath;
        return false;
    }
    impl_->open = true;
    return true;
}

void UCZipPackageReader::Close() {
    if (impl_ && impl_->open) {
        mz_zip_reader_end(&impl_->zip);
        impl_->open = false;
    }
}

bool UCZipPackageReader::IsOpen() const {
    return impl_ && impl_->open;
}

bool UCZipPackageReader::HasEntry(const std::string& entryName) const {
    if (!IsOpen()) return false;
    return mz_zip_reader_locate_file(&impl_->zip, entryName.c_str(), nullptr, 0) >= 0;
}

bool UCZipPackageReader::ReadEntry(const std::string& entryName, std::vector<uint8_t>& out) const {
    out.clear();
    lastError_.clear();
    if (!IsOpen()) {
        lastError_ = "ZIP package is not open";
        return false;
    }
    int index = mz_zip_reader_locate_file(&impl_->zip, entryName.c_str(), nullptr, 0);
    if (index < 0) {
        lastError_ = "Entry not found in ZIP package: " + entryName;
        return false;
    }
    size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&impl_->zip, static_cast<mz_uint>(index), &size, 0);
    if (!data) {
        lastError_ = "Failed to extract ZIP entry (corrupt archive?): " + entryName;
        return false;
    }
    out.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    mz_free(data);
    return true;
}

bool UCZipPackageReader::ReadEntry(const std::string& entryName, std::string& out) const {
    std::vector<uint8_t> bytes;
    if (!ReadEntry(entryName, bytes)) {
        out.clear();
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

std::vector<std::string> UCZipPackageReader::EntryNames() const {
    std::vector<std::string> names;
    if (!IsOpen()) return names;
    mz_uint count = mz_zip_reader_get_num_files(&impl_->zip);
    names.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&impl_->zip, i, &st)) {
            names.emplace_back(st.m_filename);
        }
    }
    return names;
}

// ===== WRITER =====

namespace {

// General-purpose flag: only the UTF-8 filename bit. Bit 3 (data descriptor)
// must stay clear for ODF compatibility.
constexpr uint16_t kGpFlagUtf8 = 0x0800;
constexpr uint16_t kVersionNeeded = 20;

struct ZipEntryRecord {
    std::string name;
    uint32_t crc32 = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint16_t method = 0;            // 0 = stored, 8 = deflate
    uint16_t dosTime = 0;
    uint16_t dosDate = 0;
    uint32_t localHeaderOffset = 0;
};

void PutLE16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void PutLE32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void CurrentDosDateTime(uint16_t& dosTime, uint16_t& dosDate) {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    dosTime = static_cast<uint16_t>((local.tm_hour << 11) | (local.tm_min << 5)
                                    | (local.tm_sec >> 1));
    int year = local.tm_year + 1900;
    if (year < 1980) year = 1980;
    dosDate = static_cast<uint16_t>(((year - 1980) << 9) | ((local.tm_mon + 1) << 5)
                                    | local.tm_mday);
}

} // namespace

struct UCZipPackageWriter::Impl {
    std::FILE* file = nullptr;
    std::vector<ZipEntryRecord> entries;
    uint64_t offset = 0;
    bool finalized = false;

    bool WriteBytes(const void* data, size_t size, std::string& lastError) {
        if (size == 0) return true;
        if (std::fwrite(data, 1, size, file) != size) {
            lastError = "Write failed (disk full?)";
            return false;
        }
        offset += size;
        return true;
    }

    void CloseFile() {
        if (file) {
            std::fclose(file);
            file = nullptr;
        }
    }
};

UCZipPackageWriter::UCZipPackageWriter() : impl_(std::make_unique<Impl>()) {}

UCZipPackageWriter::~UCZipPackageWriter() {
    if (impl_) impl_->CloseFile();
}

bool UCZipPackageWriter::Open(const std::string& filePath) {
    lastError_.clear();
    impl_->CloseFile();
    impl_->entries.clear();
    impl_->offset = 0;
    impl_->finalized = false;
    impl_->file = std::fopen(filePath.c_str(), "wb");
    if (!impl_->file) {
        lastError_ = "Cannot create file: " + filePath;
        return false;
    }
    return true;
}

bool UCZipPackageWriter::AddEntry(const std::string& entryName, const void* data, size_t size,
                                  bool compress) {
    lastError_.clear();
    if (!impl_->file || impl_->finalized) {
        lastError_ = "ZIP package is not open for writing";
        return false;
    }
    if (size > 0xFFFFFFFFull || impl_->offset > 0xFFFFFFFFull) {
        lastError_ = "ZIP entry too large (>4GB is not supported): " + entryName;
        return false;
    }

    ZipEntryRecord entry;
    entry.name = entryName;
    entry.uncompressedSize = static_cast<uint32_t>(size);
    entry.crc32 = static_cast<uint32_t>(
        mz_crc32(MZ_CRC32_INIT, static_cast<const unsigned char*>(data), size));
    entry.localHeaderOffset = static_cast<uint32_t>(impl_->offset);
    CurrentDosDateTime(entry.dosTime, entry.dosDate);

    // Raw deflate (window -15, no zlib header). Falls back to stored when
    // compression does not help.
    void* compressed = nullptr;
    size_t compressedSize = 0;
    if (compress && size > 0) {
        mz_uint flags = tdefl_create_comp_flags_from_zip_params(
            MZ_DEFAULT_LEVEL, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY);
        compressed = tdefl_compress_mem_to_heap(data, size, &compressedSize, flags);
        if (compressed && compressedSize < size) {
            entry.method = 8;
            entry.compressedSize = static_cast<uint32_t>(compressedSize);
        } else {
            mz_free(compressed);
            compressed = nullptr;
        }
    }
    if (!compressed) {
        entry.method = 0;
        entry.compressedSize = entry.uncompressedSize;
    }

    // Local file header with the real sizes — no data descriptor.
    std::vector<uint8_t> header;
    header.reserve(30 + entry.name.size());
    PutLE32(header, 0x04034b50);
    PutLE16(header, kVersionNeeded);
    PutLE16(header, kGpFlagUtf8);
    PutLE16(header, entry.method);
    PutLE16(header, entry.dosTime);
    PutLE16(header, entry.dosDate);
    PutLE32(header, entry.crc32);
    PutLE32(header, entry.compressedSize);
    PutLE32(header, entry.uncompressedSize);
    PutLE16(header, static_cast<uint16_t>(entry.name.size()));
    PutLE16(header, 0);   // extra field length
    header.insert(header.end(), entry.name.begin(), entry.name.end());

    bool ok = impl_->WriteBytes(header.data(), header.size(), lastError_)
              && impl_->WriteBytes(compressed ? compressed : data,
                                   entry.compressedSize, lastError_);
    mz_free(compressed);
    if (!ok) {
        if (lastError_.empty()) lastError_ = "Failed to write ZIP entry: " + entryName;
        return false;
    }
    impl_->entries.push_back(std::move(entry));
    return true;
}

bool UCZipPackageWriter::AddEntry(const std::string& entryName, const std::string& data,
                                  bool compress) {
    return AddEntry(entryName, data.data(), data.size(), compress);
}

bool UCZipPackageWriter::Finalize() {
    lastError_.clear();
    if (!impl_->file || impl_->finalized) {
        lastError_ = "ZIP package is not open for writing";
        return false;
    }

    uint64_t centralDirStart = impl_->offset;
    std::vector<uint8_t> central;
    for (const auto& entry : impl_->entries) {
        PutLE32(central, 0x02014b50);
        PutLE16(central, kVersionNeeded);    // version made by
        PutLE16(central, kVersionNeeded);    // version needed
        PutLE16(central, kGpFlagUtf8);
        PutLE16(central, entry.method);
        PutLE16(central, entry.dosTime);
        PutLE16(central, entry.dosDate);
        PutLE32(central, entry.crc32);
        PutLE32(central, entry.compressedSize);
        PutLE32(central, entry.uncompressedSize);
        PutLE16(central, static_cast<uint16_t>(entry.name.size()));
        PutLE16(central, 0);   // extra length
        PutLE16(central, 0);   // comment length
        PutLE16(central, 0);   // disk number
        PutLE16(central, 0);   // internal attributes
        PutLE32(central, 0);   // external attributes
        PutLE32(central, entry.localHeaderOffset);
        central.insert(central.end(), entry.name.begin(), entry.name.end());
    }

    // End of central directory.
    uint32_t centralDirSize = static_cast<uint32_t>(central.size());
    PutLE32(central, 0x06054b50);
    PutLE16(central, 0);   // disk number
    PutLE16(central, 0);   // central dir start disk
    PutLE16(central, static_cast<uint16_t>(impl_->entries.size()));
    PutLE16(central, static_cast<uint16_t>(impl_->entries.size()));
    PutLE32(central, centralDirSize);
    PutLE32(central, static_cast<uint32_t>(centralDirStart));
    PutLE16(central, 0);   // comment length

    bool ok = impl_->WriteBytes(central.data(), central.size(), lastError_);
    impl_->CloseFile();
    impl_->finalized = true;
    if (!ok && lastError_.empty()) {
        lastError_ = "Failed to finalize ZIP archive";
    }
    return ok;
}

} // namespace UltraCanvas
