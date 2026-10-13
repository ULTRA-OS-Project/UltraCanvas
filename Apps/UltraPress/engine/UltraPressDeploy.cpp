// Apps/UltraPress/engine/UltraPressDeploy.cpp
// See UltraPressDeploy.h.
// Version: 0.5.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressDeploy.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;
using UltraCanvas::JSONValue;

namespace UltraPress {

const char* const kDeployManifestName = ".ultrapress-deploy.json";

std::string DeployResult::Summary() const {
    std::string summary = std::to_string(uploaded.size()) + " uploaded, " + std::to_string(unchanged.size()) +
                          " unchanged, " + std::to_string(failed.size()) + " failed";
    if (!notTried.empty()) summary += ", " + std::to_string(notTried.size()) + " not tried";
    return summary;
}

std::string Fingerprint(const std::string& path) {
    std::ifstream in(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!in) return "";
    uint64_t hash = 1469598103934665603ULL;      // FNV-1a 64: offset basis
    uint64_t size = 0;
    char buffer[65536];
    while (in.read(buffer, sizeof buffer) || in.gcount() > 0) {
        const std::streamsize got = in.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ULL;             // FNV prime
        }
        size += static_cast<uint64_t>(got);
    }
    char out[48];
    std::snprintf(out, sizeof out, "%llu-%016llx", static_cast<unsigned long long>(size),
                  static_cast<unsigned long long>(hash));
    return out;
}

std::string JoinRemote(const std::string& root, const std::string& relative) {
    std::string base = root.empty() ? "/" : root;
    if (base.front() != '/') base.insert(base.begin(), '/');
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    if (relative.empty()) return base;
    return base == "/" ? "/" + relative : base + "/" + relative;
}

namespace {

// The manifest: {"targets": {"<hostKey>": {"<relative>": "<fingerprint>"}}}.
using Fingerprints = std::map<std::string, std::string>;

Fingerprints ReadManifest(const fs::path& file, const std::string& hostKey) {
    Fingerprints known;
    std::error_code ec;
    if (!fs::exists(file, ec)) return known;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue json = UltraCanvas::JSON::ParseFile(UltraCanvas::PathToUtf8(file), &parsed);
    if (!parsed.success) return known;
    for (const JSONValue::Member& member : json["targets"][hostKey].GetMembers()) {
        known[member.first] = member.second.GetString();
    }
    return known;
}

bool WriteManifest(const fs::path& file, const std::string& hostKey, const Fingerprints& current) {
    JSONValue json;
    std::error_code ec;
    if (fs::exists(file, ec)) {
        UltraCanvas::JSONParseResult parsed;
        json = UltraCanvas::JSON::ParseFile(UltraCanvas::PathToUtf8(file), &parsed);
        if (!parsed.success || !json.IsObject()) json = JSONValue();
    }
    JSONValue targets = json["targets"].IsObject() ? json["targets"] : JSONValue::MakeObject();
    JSONValue files = JSONValue::MakeObject();
    for (const auto& [relative, fingerprint] : current) files.Set(relative, fingerprint);
    targets.Set(hostKey, files);
    json = JSONValue::MakeObject();
    json.Set("generator", "UltraPress");
    json.Set("targets", targets);
    UltraCanvas::JSONSerializeOptions pretty;
    pretty.pretty = true;
    pretty.indentWidth = 2;
    return UltraCanvas::JSON::SerializeToFile(UltraCanvas::PathToUtf8(file), json, pretty);
}

// The relative path with '/' between the parts, whatever the platform uses.
std::string RelativeOf(const fs::path& root, const fs::path& file) {
    std::string relative = UltraCanvas::PathToUtf8(file.lexically_relative(root));
    std::replace(relative.begin(), relative.end(), '\\', '/');
    return relative;
}

} // namespace

DeployResult DeployFolder(const std::string& outputDirectory, const Uploader& uploader,
                          const std::string& hostKey, const DeployOptions& options) {
    DeployResult result;
    const fs::path root = UltraCanvas::PathFromUtf8(outputDirectory);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        result.errors.push_back(outputDirectory + ": not a folder");
        return result;
    }
    if (!uploader.upload) {
        result.errors.push_back("no uploader");
        return result;
    }
    const fs::path manifestFile = root / kDeployManifestName;
    const Fingerprints known = options.force ? Fingerprints() : ReadManifest(manifestFile, hostKey);

    // Every file, in path order, with its fingerprint.
    std::vector<std::pair<std::string, std::string>> files;    // relative, fingerprint
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().filename() == kDeployManifestName) continue;
        const std::string relative = RelativeOf(root, entry.path());
        files.emplace_back(relative, Fingerprint(UltraCanvas::PathToUtf8(entry.path())));
    }
    std::sort(files.begin(), files.end());

    Fingerprints current;
    // The folders asked for, by remote path: the root's own levels first
    // (a WebDAV PUT into a collection that is not there is a 409), then
    // each folder under it, outermost first, each once.
    std::set<std::string> made;
    auto ensureFolder = [&](const std::string& remoteFolder) {
        if (!uploader.makeDirectory) return;
        std::string path;
        size_t from = 1;
        for (size_t slash = remoteFolder.find('/', from); ; slash = remoteFolder.find('/', from)) {
            path = remoteFolder.substr(0, slash);
            from = slash + 1;
            if (path.size() > 1 && made.insert(path).second) {
                std::string error;
                if (uploader.makeDirectory(path, error)) ++result.directoriesCreated;
            }
            if (slash == std::string::npos) break;
        }
    };
    size_t index = 0;
    const size_t total = files.size();
    int failuresInARow = 0;
    for (const auto& [relative, fingerprint] : files) {
        ++index;
        auto it = known.find(relative);
        if (it != known.end() && !fingerprint.empty() && it->second == fingerprint) {
            result.unchanged.push_back(relative);
            current[relative] = fingerprint;
            continue;
        }
        if (options.stopAfterFailures > 0 && failuresInARow >= options.stopAfterFailures) {
            result.notTried.push_back(relative);
            continue;
        }
        if (options.onProgress) options.onProgress(relative, index, total);
        const size_t lastSlash = relative.rfind('/');
        ensureFolder(JoinRemote(options.remoteRoot, lastSlash == std::string::npos ? std::string() : relative.substr(0, lastSlash)));
        std::string error;
        const std::string local = UltraCanvas::PathToUtf8(root / UltraCanvas::PathFromUtf8(relative));
        if (uploader.upload(local, JoinRemote(options.remoteRoot, relative), error)) {
            result.uploaded.push_back(relative);
            current[relative] = fingerprint;
            failuresInARow = 0;
        } else {
            result.failed.push_back(relative);
            result.errors.push_back(relative + ": " + (error.empty() ? std::string("upload failed") : error));
            ++failuresInARow;
        }
    }
    if (!result.notTried.empty()) {
        result.errors.push_back("stopped after " + std::to_string(options.stopAfterFailures) +
                                " failures in a row; " + std::to_string(result.notTried.size()) +
                                " file(s) left for the next publish");
    }
    if (!WriteManifest(manifestFile, hostKey, current)) {
        result.errors.push_back("cannot write " + UltraCanvas::PathToUtf8(manifestFile) +
                                " - the next publish uploads everything again");
    }
    result.ok = result.failed.empty() && result.notTried.empty();
    return result;
}

} // namespace UltraPress
