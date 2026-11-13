// Apps/UltraWeb/host/UltraWebPermissions.cpp
// What the user answered for an app's devices (UltraWebPermissions.h).
// The file: a first line naming the format, then "allow" or "block", the
// device and the partition, separated by tabs (a partition is an origin or a
// file URL: it holds no tab or newline, and one that does is not written).
// Version: 0.1.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebPermissions.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

constexpr const char* kFileHeader = "# UltraWeb device permissions 1";
constexpr size_t kMaxFileBytes = 1u << 20;

bool Plain(const std::string& s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) return false;
    }
    return !s.empty();
}

} // namespace

PermissionStore::PermissionStore(std::string path) : path_(std::move(path)) {
    if (path_.empty()) return;
    std::FILE* file = OpenFileUtf8(path_, "rb");
    if (!file) return;
    std::string text;
    char chunk[16384];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        text.append(chunk, got);
        if (text.size() > kMaxFileBytes) break;
    }
    std::fclose(file);
    if (text.size() > kMaxFileBytes || text.rfind(kFileHeader, 0) != 0) return;
    size_t at = text.find('\n');
    while (at != std::string::npos && at + 1 < text.size()) {
        const size_t end = text.find('\n', at + 1);
        std::string line = text.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
        at = end;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find('\t');
        const size_t second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (second == std::string::npos) continue;
        const std::string answer = line.substr(0, first);
        const std::string device = line.substr(first + 1, second - first - 1);
        const std::string partition = line.substr(second + 1);
        if ((answer != "allow" && answer != "block") || !Plain(device) || !Plain(partition)) continue;
        answers_[{partition, device}] = answer == "allow";
    }
}

PermissionStore::Answer PermissionStore::Get(const std::string& partition, const std::string& device) const {
    auto at = answers_.find({partition, device});
    if (at == answers_.end()) return Answer::NotAsked;
    return at->second ? Answer::Allowed : Answer::Blocked;
}

void PermissionStore::Set(const std::string& partition, const std::string& device, bool allowed) {
    answers_[{partition, device}] = allowed;
    Write();
}

void PermissionStore::Clear() {
    answers_.clear();
    Write();
}

bool PermissionStore::Write() const {
    if (path_.empty()) return true;
    std::error_code ec;
    const std::filesystem::path target = PathFromUtf8(path_);
    if (answers_.empty()) {
        std::filesystem::remove(target, ec);
        return !ec;
    }
    std::string text = std::string(kFileHeader) + "\n";
    for (const auto& [key, allowed] : answers_) {
        if (!Plain(key.first) || !Plain(key.second)) continue;
        text += std::string(allowed ? "allow" : "block") + "\t" + key.second + "\t" + key.first + "\n";
    }
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
    return true;
}

std::shared_ptr<PermissionStore> PermissionStore::Shared() {
    static std::shared_ptr<PermissionStore> store = []() {
        const std::filesystem::path settings = UltraCanvasSettingsFolder();
        return std::make_shared<PermissionStore>(settings.empty() ? std::string()
                                                                  : PathToUtf8(settings / "UltraWeb" / "permissions.txt"));
    }();
    return store;
}

} // namespace UltraWeb
