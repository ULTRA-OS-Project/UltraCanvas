// UltraCloud/providers/UltraCloudMemory.cpp
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudMemory.h>
#include <UltraCloud/UltraCloudWebDav.h>   // NormalizePath

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace UltraCloud {

namespace {

struct Item {
    bool isDirectory = false;
    int64_t size = 0;
    std::string modified;
    std::vector<uint8_t> data;
};
using Tree = std::map<std::string, Item>;   // path → item, "/" implicit

std::mutex& Mutex() { static std::mutex m; return m; }
std::map<std::string, Tree>& Stores() { static std::map<std::string, Tree> s; return s; }
int& LinkCounter() { static int n = 0; return n; }

std::string Parent(const std::string& path) {
    auto slash = path.find_last_of('/');
    return slash == 0 || slash == std::string::npos ? "/" : path.substr(0, slash);
}

} // namespace

ProviderCapabilities MemoryProvider::Capabilities() const {
    ProviderCapabilities c;
    c.browse = true; c.upload = true; c.shareLinks = true;
    c.passwordProtectedLinks = true; c.expiringLinks = true;
    return c;
}

void MemoryProvider::Seed(const std::string& accountId, const std::string& path, int64_t size,
                          const std::string& modified) {
    std::lock_guard<std::mutex> lock(Mutex());
    Item item;
    item.isDirectory = size < 0;
    item.size = size < 0 ? 0 : size;
    item.modified = modified;
    Stores()[accountId][NormalizePath(path)] = std::move(item);
}

void MemoryProvider::Clear() {
    std::lock_guard<std::mutex> lock(Mutex());
    Stores().clear();
    LinkCounter() = 0;
}

Result MemoryProvider::Verify(const Account&, const Credentials&) { return Result::Ok(); }

Result MemoryProvider::List(const Account& account, const Credentials&,
                            const std::string& path, std::vector<Entry>& out) {
    out.clear();
    const std::string folder = NormalizePath(path);
    std::lock_guard<std::mutex> lock(Mutex());
    const Tree& tree = Stores()[account.accountId];
    if (folder != "/" && !tree.count(folder))
        return Result::Error(ResultCode::NotFound, "no folder " + folder);
    for (const auto& [p, item] : tree) {
        if (Parent(p) != folder) continue;
        Entry e;
        e.path = p;
        e.name = p.substr(p.find_last_of('/') + 1);
        e.isDirectory = item.isDirectory;
        e.size = item.size;
        e.modified = item.modified;
        out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return a.name < b.name;
    });
    return Result::Ok();
}

Result MemoryProvider::MakeDirectory(const Account& account, const Credentials&,
                                     const std::string& path) {
    std::lock_guard<std::mutex> lock(Mutex());
    Item dir; dir.isDirectory = true;
    Stores()[account.accountId].emplace(NormalizePath(path), dir);
    return Result::Ok();
}

Result MemoryProvider::Upload(const Account& account, const Credentials&,
                              const std::string& localPath, const std::string& remotePath) {
    std::ifstream is(localPath, std::ios::binary);
    if (!is) return Result::Error(ResultCode::IoError, "cannot read " + localPath);
    Item item;
    item.data.assign(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>());
    item.size = static_cast<int64_t>(item.data.size());
    std::lock_guard<std::mutex> lock(Mutex());
    Stores()[account.accountId][NormalizePath(remotePath)] = std::move(item);
    return Result::Ok();
}

Result MemoryProvider::Download(const Account& account, const Credentials&,
                                const std::string& remotePath, const std::string& localPath) {
    std::lock_guard<std::mutex> lock(Mutex());
    const Tree& tree = Stores()[account.accountId];
    auto it = tree.find(NormalizePath(remotePath));
    if (it == tree.end()) return Result::Error(ResultCode::NotFound, "no file " + remotePath);
    std::ofstream os(localPath, std::ios::binary | std::ios::trunc);
    if (!os) return Result::Error(ResultCode::IoError, "cannot write " + localPath);
    os.write(reinterpret_cast<const char*>(it->second.data.data()),
             static_cast<std::streamsize>(it->second.data.size()));
    return Result::Ok();
}

Result MemoryProvider::CreateShareLink(const Account& account, const Credentials&,
                                       const std::string& remotePath,
                                       const ShareLinkOptions& options, ShareLink& out) {
    std::lock_guard<std::mutex> lock(Mutex());
    const Tree& tree = Stores()[account.accountId];
    if (!tree.count(NormalizePath(remotePath)))
        return Result::Error(ResultCode::NotFound, "no file " + remotePath);
    out = ShareLink{};
    out.id = std::to_string(++LinkCounter());
    out.url = "https://demo.ultra-os.local/s/" + out.id;
    out.expiresAt = options.expiresAt;
    return Result::Ok();
}

} // namespace UltraCloud
