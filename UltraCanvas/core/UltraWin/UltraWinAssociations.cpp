// core/UltraWin/UltraWinAssociations.cpp
// The program->environment association store: how a launcher's one-time
// environment choice becomes durable and shared. A flat file under the
// UltraWin data directory ("<env>\t<absolute path>" per line) — no
// database, human-inspectable, trivially merged by hand.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinInternal.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace fs = std::filesystem;

namespace ultrawin_internal {

std::string SanitizeEnvironmentName(const std::string& raw) {
    std::string name = raw;
    for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' &&
            c != '_' && c != '-')
            c = '_';
    }
    while (!name.empty() && name.front() == '.') name.erase(0, 1);
    if (name.size() > 64) name.resize(64);
    return name;
}

namespace {

std::string AssociationsPath() {
    // Sibling of ".../ultrawin/environments" and ".../ultrawin/vm".
    return PathToUtf8(PathFromUtf8(EnvironmentsRoot()).parent_path() /
                      "associations.conf");
}

// Cheap identity check for "is this still the same file?": size plus an
// FNV-1a hash of the first 64 KiB. Catches a re-downloaded setup.exe
// replacing a deleted one at the same path — the classic Downloads
// folder case — without hashing whole installers. "" when unreadable.
std::string FileFingerprint(const std::string& path) {
    std::ifstream in(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!in) return {};
    uint64_t hash = 1469598103934665603ull;  // FNV offset basis
    char buf[4096];
    size_t total = 0;
    while (total < 64 * 1024 && in.read(buf, sizeof buf).gcount() > 0) {
        std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            hash ^= static_cast<unsigned char>(buf[i]);
            hash *= 1099511628211ull;  // FNV prime
        }
        total += static_cast<size_t>(n);
        if (n < static_cast<std::streamsize>(sizeof buf)) break;
    }
    std::error_code ec;
    uint64_t size = static_cast<uint64_t>(fs::file_size(UltraCanvas::PathFromUtf8(path), ec));
    if (ec) return {};
    std::ostringstream out;
    out << size << ':' << std::hex << hash;
    return out.str();
}

struct AssociationEntry {
    std::string environment;
    std::string fingerprint;  // "" = legacy entry, matched by path alone
};

// path -> entry. Format per line: ENV \t FINGERPRINT \t PATH, with the
// two-field ENV \t PATH form still read for older stores.
std::map<std::string, AssociationEntry> LoadAssociations() {
    std::map<std::string, AssociationEntry> out;
    std::ifstream in(AssociationsPath());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t tab1 = line.find('\t');
        if (tab1 == std::string::npos || tab1 == 0) continue;
        std::string env = line.substr(0, tab1);
        if (!IsValidEnvironmentName(env)) continue;
        std::string rest = line.substr(tab1 + 1);
        size_t tab2 = rest.find('\t');
        AssociationEntry entry;
        entry.environment = env;
        std::string path;
        if (tab2 == std::string::npos) {
            path = rest;  // legacy two-field line
        } else {
            entry.fingerprint = rest.substr(0, tab2);
            path = rest.substr(tab2 + 1);
        }
        if (path.empty() || path[0] != '/') continue;
        out[path] = std::move(entry);
    }
    return out;
}

bool SaveAssociations(const std::map<std::string, AssociationEntry>& assoc) {
    std::error_code ec;
    fs::create_directories(
        PathFromUtf8(AssociationsPath()).parent_path(), ec);
    std::ofstream out(AssociationsPath(), std::ios::trunc);
    if (!out) return false;
    out << "# UltraWin program associations — "
           "ENVIRONMENT<TAB>SIZE:HASH<TAB>/absolute/program/path per "
           "line.\n";
    for (const auto& [path, entry] : assoc)
        out << entry.environment << '\t' << entry.fingerprint << '\t'
            << path << '\n';
    return static_cast<bool>(out);
}

}  // namespace
}  // namespace ultrawin_internal

// ===========================================================================
// Public entry points
// ===========================================================================

using namespace ultrawin_internal;

std::string UltraWin_EnvironmentForPath(const std::string& hostPath) {
    if (!UltraWin_IsInitialized()) return {};
    return EnvironmentForPath(hostPath);
}

std::string UltraWin_GetAssociation(const std::string& programPath) {
    if (!UltraWin_IsInitialized() || programPath.empty() ||
        programPath[0] != '/')
        return {};
    auto assoc = LoadAssociations();
    auto it = assoc.find(programPath);
    if (it == assoc.end()) return {};
    // A different file at the same path (a re-downloaded setup.exe in
    // Downloads) is a different program: a mismatched fingerprint makes
    // the stored choice stale, so the launcher asks again.
    if (!it->second.fingerprint.empty()) {
        std::string current = FileFingerprint(programPath);
        if (!current.empty() && current != it->second.fingerprint)
            return {};
    }
    return it->second.environment;
}

UltraWinResult UltraWin_SetAssociation(const std::string& programPath,
                                       const std::string& environment) {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    if (programPath.empty() || programPath[0] != '/')
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "programPath must be absolute");
    if (!IsValidEnvironmentName(environment))
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "invalid environment name: " +
                                         environment);
    auto assoc = LoadAssociations();
    assoc[programPath] = {environment, FileFingerprint(programPath)};
    if (!SaveAssociations(assoc))
        return UltraWinResult::Error(UltraWinResultCode::IoError,
                                     "cannot write the association store");
    return UltraWinResult::Ok();
}

UltraWinResult UltraWin_RemoveAssociation(const std::string& programPath) {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    auto assoc = LoadAssociations();
    if (assoc.erase(programPath) == 0)
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "no association stored for " +
                                         programPath);
    if (!SaveAssociations(assoc))
        return UltraWinResult::Error(UltraWinResultCode::IoError,
                                     "cannot write the association store");
    return UltraWinResult::Ok();
}

std::string UltraWin_SuggestEnvironment(const std::string& programPath) {
    if (programPath.empty() || programPath[0] != '/') return {};
    if (!UltraWin_IsInitialized()) return "Default";

    // The program's own still-valid association first.
    std::string own = UltraWin_GetAssociation(programPath);
    if (!own.empty()) return own;

    // A sibling program's association: the multi-exe application case —
    // helper.exe should land where app.exe already lives.
    auto assoc = LoadAssociations();
    const std::string dir = PathToUtf8(PathFromUtf8(programPath).parent_path());
    for (const auto& [path, entry] : assoc) {
        if (path != programPath &&
            PathToUtf8(PathFromUtf8(path).parent_path()) == dir)
            return entry.environment;
    }

    std::string folder = SanitizeEnvironmentName(
        PathToUtf8(PathFromUtf8(programPath).parent_path().filename()));
    if (!folder.empty()) return folder;
    std::string stem =
        SanitizeEnvironmentName(PathToUtf8(PathFromUtf8(programPath).stem()));
    return stem.empty() ? "Default" : stem;
}
