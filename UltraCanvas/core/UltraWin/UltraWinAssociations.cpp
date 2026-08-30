// core/UltraWin/UltraWinAssociations.cpp
// The program->environment association store: how a launcher's one-time
// environment choice becomes durable and shared. A flat file under the
// UltraWin data directory ("<env>\t<absolute path>" per line) — no
// database, human-inspectable, trivially merged by hand.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinInternal.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

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
    return (fs::path(EnvironmentsRoot()).parent_path() /
            "associations.conf")
        .string();
}

// path -> environment; insertion order is irrelevant, lookups exact.
std::map<std::string, std::string> LoadAssociations() {
    std::map<std::string, std::string> out;
    std::ifstream in(AssociationsPath());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        std::string env = line.substr(0, tab);
        std::string path = line.substr(tab + 1);
        if (!IsValidEnvironmentName(env) || path.empty() || path[0] != '/')
            continue;
        out[path] = env;
    }
    return out;
}

bool SaveAssociations(const std::map<std::string, std::string>& assoc) {
    std::error_code ec;
    fs::create_directories(
        fs::path(AssociationsPath()).parent_path(), ec);
    std::ofstream out(AssociationsPath(), std::ios::trunc);
    if (!out) return false;
    out << "# UltraWin program associations — ENVIRONMENT<TAB>/absolute/"
           "program/path per line.\n";
    for (const auto& [path, env] : assoc) out << env << '\t' << path << '\n';
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
    return it == assoc.end() ? std::string() : it->second;
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
    assoc[programPath] = environment;
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

    auto assoc = LoadAssociations();
    auto own = assoc.find(programPath);
    if (own != assoc.end()) return own->second;

    // A sibling program's association: the multi-exe application case —
    // helper.exe should land where app.exe already lives.
    const std::string dir = fs::path(programPath).parent_path().string();
    for (const auto& [path, env] : assoc) {
        if (fs::path(path).parent_path().string() == dir) return env;
    }

    std::string folder = SanitizeEnvironmentName(
        fs::path(programPath).parent_path().filename().string());
    if (!folder.empty()) return folder;
    std::string stem =
        SanitizeEnvironmentName(fs::path(programPath).stem().string());
    return stem.empty() ? "Default" : stem;
}
