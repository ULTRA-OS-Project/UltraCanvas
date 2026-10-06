// Apps/UltraWeb/host/UltraWebStorage.h
// An app's storage (uc_storage_* in UltraWeb/guest/ultraweb.h): key/value
// bytes kept across runs, one store per origin, like a web page's
// localStorage. A store lives in memory while its app runs and is written
// to one file per store under <settings folder>/UltraWeb/storage, a while
// after a change and when the store closes, through a temporary file and a
// rename so that a crash leaves the old file or the new one, never half.
//
// UI thread only.
// Version: 0.2.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace UltraWeb {

class UltraWebStorage {
public:
    static constexpr size_t kQuotaBytes = 5u << 20;
    static constexpr size_t kMaxKeyBytes = 1024;
    // Counted per key on top of its bytes, so a store of empty values is
    // still bounded.
    static constexpr size_t kEntryOverhead = 32;

    // The store an app's address names: its origin for http(s), the
    // absolute file for an app opened from a file, about:x itself.
    static std::string PartitionFor(const std::string& address);
    // <settings folder>/UltraWeb/storage, or empty when there is no home.
    static std::string DefaultDirectory();
    // The file a partition is kept in, inside a directory; the name stands
    // for the partition one to one ("https://x.org" → "https_3a_2f_2fx.org.json").
    static std::string FileNameFor(const std::string& partition);

    // A store in memory only: tests, and a store whose file cannot be used.
    explicit UltraWebStorage(std::string partition);
    // A store kept in `directory`, read from its file now when there is one.
    // A file that cannot be read, or holds another partition, is left alone:
    // the store then starts empty, in memory only, and `problem` says why.
    static std::shared_ptr<UltraWebStorage> Open(const std::string& directory, const std::string& partition,
                                                 std::string& problem);
    // Writes what is not yet written.
    ~UltraWebStorage();
    UltraWebStorage(const UltraWebStorage&) = delete;
    UltraWebStorage& operator=(const UltraWebStorage&) = delete;

    const std::string& Partition() const { return partition_; }
    const std::string& FilePath() const { return path_; }

    bool Get(const std::string& key, std::string& value) const;
    // UC_OK, or UC_ERR_LIMIT for a key over kMaxKeyBytes or a store that
    // would go over kQuotaBytes (the old value then stays).
    int32_t Set(const std::string& key, const std::string& value);
    bool Remove(const std::string& key);
    void Clear();
    // The index-th key in byte order. Walking 0, 1, 2 ... is linear overall.
    bool KeyAt(size_t index, std::string& key) const;
    size_t KeyCount() const { return items_.size(); }
    size_t UsedBytes() const { return used_; }

    // True once after a change, until the next Flush: the caller schedules
    // one flush for a run of changes rather than one per change.
    bool TakeFlushRequest();
    // Writes the store when it changed; a store left empty removes its file.
    // False when the file could not be written.
    bool Flush();

private:
    static size_t Cost(const std::string& key, const std::string& value) {
        return key.size() + value.size() + kEntryOverhead;
    }
    void Changed();

    std::string partition_;
    std::string path_;            // empty: memory only
    std::map<std::string, std::string> items_;
    size_t used_ = 0;
    bool dirty_ = false;
    bool flushRequested_ = false;
    // KeyAt walks on from where the last call stopped.
    mutable size_t cursorIndex_ = 0;
    mutable std::map<std::string, std::string>::const_iterator cursor_;
    mutable bool cursorValid_ = false;
};

} // namespace UltraWeb
