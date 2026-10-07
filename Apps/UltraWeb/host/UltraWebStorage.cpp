// Apps/UltraWeb/host/UltraWebStorage.cpp
// An app's storage (UltraWebStorage.h).
// Version: 0.2.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebStorage.h"
#include "UltraWebFetch.h"

#include "ultraweb.h"   // UltraWeb/guest: the result codes

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasTextUtils.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

// The file format: {"ultrawebStorage": 1, "partition": "...",
// "items": [["<key>", "<value>"], ...]}, keys and values in base64 because
// they are bytes, not text.
constexpr int kFormatVersion = 1;
// A store's file holds at most the quota, base64-inflated, plus syntax.
constexpr size_t kMaxFileBytes = UltraWebStorage::kQuotaBytes * 2 + 65536;

std::string Encode(const std::string& bytes) {
    return Base64Encode(std::vector<uint8_t>(bytes.begin(), bytes.end()), false);
}

std::string Decode(const std::string& text) {
    const std::vector<uint8_t> bytes = Base64Decode(text);
    return std::string(bytes.begin(), bytes.end());
}

bool ReadFile(const std::string& path, std::string& out, std::string& problem) {
    std::FILE* file = OpenFileUtf8(path, "rb");
    if (!file) return false;   // no file yet: an empty store
    out.clear();
    char chunk[65536];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        out.append(chunk, got);
        if (out.size() > kMaxFileBytes) break;
    }
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) { problem = "cannot read " + path; return false; }
    if (out.size() > kMaxFileBytes) { problem = path + " is larger than a store can be"; return false; }
    return true;
}

uint64_t Fnv1a64(const std::string& s) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

std::string UltraWebStorage::PartitionFor(const std::string& address) {
    const std::string origin = FetchRules::OriginOf(address);
    if (!origin.empty()) return origin;
    std::string lower = address;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (lower.rfind("about:", 0) == 0) return lower;
    // A file: the same file whatever path named it, and two files of the
    // same name in different folders apart.
    std::string path = address;
    if (lower.rfind("file://", 0) == 0) path = UltraNet_UrlDecode(address.substr(7));
    // Absolute first: weakly_canonical leaves a relative path relative when
    // no part of it exists yet.
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(PathFromUtf8(path), ec);
    if (ec) absolute = PathFromUtf8(path);
    std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, ec);
    if (ec) canonical = absolute.lexically_normal();
    return "file://" + PathToUtf8(canonical);
}

std::string UltraWebStorage::DefaultDirectory() {
    const std::filesystem::path settings = UltraCanvasSettingsFolder();
    if (settings.empty()) return {};
    return PathToUtf8(settings / "UltraWeb" / "storage");
}

std::string UltraWebStorage::FileNameFor(const std::string& partition) {
    // Letters, digits, '.' and '-' stand for themselves, every other byte
    // for _xx - '_' included, so two partitions never share a name.
    static const char kHex[] = "0123456789abcdef";
    std::string name;
    for (unsigned char c : partition) {
        if (std::isalnum(c) || c == '.' || c == '-') {
            name += char(c);
        } else {
            name += '_';
            name += kHex[c >> 4];
            name += kHex[c & 15];
        }
    }
    // A long one (a deep file path) is cut and told apart by a hash; the
    // partition inside the file catches the rare collision.
    if (name.size() > 120) {
        char hash[17];
        std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(Fnv1a64(partition)));
        name = name.substr(0, 100) + "_" + hash;
    }
    return name + ".json";
}

UltraWebStorage::UltraWebStorage(std::string partition) : partition_(std::move(partition)) {}

std::shared_ptr<UltraWebStorage> UltraWebStorage::Open(const std::string& directory, const std::string& partition,
                                                       std::string& problem) {
    problem.clear();
    auto storage = std::make_shared<UltraWebStorage>(partition);
    if (directory.empty()) { problem = "no settings folder to keep app storage in"; return storage; }
    const std::string path = PathToUtf8(PathFromUtf8(directory) / PathFromUtf8(FileNameFor(partition)));

    std::string text;
    if (!ReadFile(path, text, problem)) {
        if (problem.empty()) storage->path_ = path;   // a new store
        return storage;
    }
    JSONParseResult parsed;
    const JSONValue doc = JSON::Parse(text, &parsed);
    if (!parsed.success || !doc.IsObject() || doc["ultrawebStorage"].GetInteger() != kFormatVersion) {
        problem = path + " is not an UltraWeb store";
        return storage;
    }
    if (doc["partition"].GetString() != partition) {
        problem = path + " holds the store of " + doc["partition"].GetString();
        return storage;
    }
    for (const JSONValue& item : doc["items"].GetElements()) {
        if (!item.IsArray() || item.GetSize() != 2) continue;
        const std::string key = Decode(item[0].GetString());
        const std::string value = Decode(item[1].GetString());
        auto [at, inserted] = storage->items_.emplace(key, value);
        if (inserted) storage->used_ += Cost(key, value);
    }
    storage->path_ = path;
    return storage;
}

UltraWebStorage::~UltraWebStorage() { Flush(); }

bool UltraWebStorage::Get(const std::string& key, std::string& value) const {
    auto at = items_.find(key);
    if (at == items_.end()) return false;
    value = at->second;
    return true;
}

int32_t UltraWebStorage::Set(const std::string& key, const std::string& value) {
    if (key.size() > kMaxKeyBytes) return UC_ERR_LIMIT;
    auto at = items_.find(key);
    const size_t old = at == items_.end() ? 0 : Cost(key, at->second);
    const size_t now = Cost(key, value);
    if (used_ - old + now > kQuotaBytes) return UC_ERR_LIMIT;
    if (at == items_.end()) items_.emplace(key, value);
    else if (at->second == value) return UC_OK;
    else at->second = value;
    used_ = used_ - old + now;
    Changed();
    return UC_OK;
}

bool UltraWebStorage::Remove(const std::string& key) {
    auto at = items_.find(key);
    if (at == items_.end()) return false;
    used_ -= Cost(key, at->second);
    items_.erase(at);
    Changed();
    return true;
}

void UltraWebStorage::Clear() {
    if (items_.empty()) return;
    items_.clear();
    used_ = 0;
    Changed();
}

bool UltraWebStorage::KeyAt(size_t index, std::string& key) const {
    if (index >= items_.size()) return false;
    if (!cursorValid_ || index < cursorIndex_) {
        cursor_ = items_.begin();
        cursorIndex_ = 0;
        cursorValid_ = true;
    }
    std::advance(cursor_, index - cursorIndex_);
    cursorIndex_ = index;
    key = cursor_->first;
    return true;
}

void UltraWebStorage::Changed() {
    dirty_ = true;
    cursorValid_ = false;
}

bool UltraWebStorage::TakeFlushRequest() {
    if (!dirty_ || flushRequested_) return false;
    flushRequested_ = true;
    return true;
}

bool UltraWebStorage::Flush() {
    flushRequested_ = false;
    if (!dirty_) return true;
    if (path_.empty()) { dirty_ = false; return true; }
    std::error_code ec;
    const std::filesystem::path target = PathFromUtf8(path_);
    if (items_.empty()) {
        std::filesystem::remove(target, ec);
        dirty_ = false;
        return !ec;
    }
    JSONValue items = JSONValue::MakeArray();
    for (const auto& [key, value] : items_) {
        JSONValue pair = JSONValue::MakeArray();
        pair.Append(Encode(key));
        pair.Append(Encode(value));
        items.Append(std::move(pair));
    }
    JSONValue doc = JSONValue::MakeObject();
    doc.Set("ultrawebStorage", kFormatVersion);
    doc.Set("partition", partition_);
    doc.Set("items", std::move(items));
    const std::string text = JSON::Serialize(doc);

    std::filesystem::create_directories(target.parent_path(), ec);
    const std::string temporary = path_ + ".tmp";
    std::FILE* file = OpenFileUtf8(temporary, "wb");
    if (!file) return false;
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        std::filesystem::remove(PathFromUtf8(temporary), ec);
        return false;
    }
    std::filesystem::rename(PathFromUtf8(temporary), target, ec);
    if (ec) {
        std::filesystem::remove(PathFromUtf8(temporary), ec);
        return false;
    }
    dirty_ = false;
    return true;
}

} // namespace UltraWeb
