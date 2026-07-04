// Plugins/Documents/eBook/EBookArchive.cpp
// miniz-backed ZIP access and DEFLATE helpers.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "EBookArchive.h"

#include "miniz.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace UltraCanvas {

namespace {

std::string LowerCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

mz_zip_archive* Zip(void* p) { return static_cast<mz_zip_archive*>(p); }

} // namespace

EBookArchive::~EBookArchive() {
    Close();
}

bool EBookArchive::OpenFromFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        lastError = "Failed to open file: " + filePath;
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
    if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
        lastError = "Failed to read file: " + filePath;
        return false;
    }
    return OpenFromMemory(std::move(data));
}

bool EBookArchive::OpenFromMemory(std::vector<uint8_t> data) {
    Close();

    if (!IsZipData(data.data(), data.size())) {
        lastError = "Not a ZIP archive";
        return false;
    }

    buffer = std::move(data);

    auto* archive = new mz_zip_archive();
    std::memset(archive, 0, sizeof(*archive));
    if (!mz_zip_reader_init_mem(archive, buffer.data(), buffer.size(), 0)) {
        lastError = "Failed to read ZIP central directory";
        delete archive;
        buffer.clear();
        return false;
    }

    zip = archive;
    opened = true;
    lastError.clear();
    return true;
}

void EBookArchive::Close() {
    if (zip) {
        mz_zip_reader_end(Zip(zip));
        delete Zip(zip);
        zip = nullptr;
    }
    buffer.clear();
    opened = false;
}

std::vector<std::string> EBookArchive::FileNames() const {
    std::vector<std::string> names;
    if (!opened) return names;

    mz_uint count = mz_zip_reader_get_num_files(Zip(zip));
    names.reserve(count);
    char nameBuffer[1024];
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(Zip(zip), i)) continue;
        mz_uint len = mz_zip_reader_get_filename(Zip(zip), i, nameBuffer,
                                                 sizeof(nameBuffer));
        if (len > 0) names.emplace_back(nameBuffer);
    }
    return names;
}

int EBookArchive::FindIndex(const std::string& name) const {
    if (!opened || name.empty()) return -1;

    int index = mz_zip_reader_locate_file(Zip(zip), name.c_str(), nullptr, 0);
    if (index >= 0) return index;

    // Fall back to a case-insensitive scan; some producers mis-case hrefs.
    std::string wanted = LowerCopy(name);
    mz_uint count = mz_zip_reader_get_num_files(Zip(zip));
    char nameBuffer[1024];
    for (mz_uint i = 0; i < count; ++i) {
        mz_uint len = mz_zip_reader_get_filename(Zip(zip), i, nameBuffer,
                                                 sizeof(nameBuffer));
        if (len > 0 && LowerCopy(nameBuffer) == wanted) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool EBookArchive::Contains(const std::string& name) const {
    return FindIndex(name) >= 0;
}

bool EBookArchive::ReadFile(const std::string& name, std::vector<uint8_t>& out) const {
    out.clear();
    int index = FindIndex(name);
    if (index < 0) {
        lastError = "Not in archive: " + name;
        return false;
    }

    size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(Zip(zip), static_cast<mz_uint>(index),
                                               &size, 0);
    if (!data) {
        lastError = "Failed to extract: " + name;
        return false;
    }

    out.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    mz_free(data);
    return true;
}

std::string EBookArchive::ReadTextFile(const std::string& name) const {
    std::vector<uint8_t> bytes;
    if (!ReadFile(name, bytes)) return {};
    return std::string(bytes.begin(), bytes.end());
}

bool EBookArchive::IsZipData(const uint8_t* data, size_t size) {
    return size >= 4 && data[0] == 'P' && data[1] == 'K' &&
           ((data[2] == 0x03 && data[3] == 0x04) ||
            (data[2] == 0x05 && data[3] == 0x06));
}

namespace {

bool InflateWithFlags(const uint8_t* data, size_t size,
                      std::vector<uint8_t>& out, int flags) {
    out.clear();
    if (size == 0) return true;

    size_t outLen = 0;
    void* result = tinfl_decompress_mem_to_heap(data, size, &outLen, flags);
    if (!result) return false;

    out.assign(static_cast<uint8_t*>(result),
               static_cast<uint8_t*>(result) + outLen);
    mz_free(result);
    return true;
}

} // namespace

bool EBookArchive::InflateZlib(const uint8_t* data, size_t size,
                               std::vector<uint8_t>& out) {
    return InflateWithFlags(data, size, out, TINFL_FLAG_PARSE_ZLIB_HEADER);
}

bool EBookArchive::InflateRaw(const uint8_t* data, size_t size,
                              std::vector<uint8_t>& out) {
    return InflateWithFlags(data, size, out, 0);
}

} // namespace UltraCanvas
