// core/UltraNet/UltraNetOAuth2Apps.cpp
// The process-wide OAuth2 app registry: Set() > environment > INI file >
// built-in, then the alias chain. Pure bookkeeping over a few maps behind one
// mutex; no networking, no crypto. See the header for the contract.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNet/UltraNetOAuth2Apps.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace {

struct Registry {
    std::mutex mutex;
    std::map<std::string, UltraNetOAuth2App> set;       // tier 1
    std::vector<std::string>                 prefixes{"ULTRANET_OAUTH_"};  // tier 2
    std::map<std::string, UltraNetOAuth2App> file;      // tier 3
    std::map<std::string, UltraNetOAuth2App> builtIn;   // tier 4
    std::map<std::string, std::string>       aliases;
};

Registry& Reg() { static Registry r; return r; }

std::string Env(const std::string& name) {
    const char* v = std::getenv(name.c_str());
    return v ? v : "";
}

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The configured entry for `id` in one map, or nullptr. Caller holds the lock.
const UltraNetOAuth2App* Configured(const std::map<std::string, UltraNetOAuth2App>& m,
                                    const std::string& id) {
    auto it = m.find(id);
    return (it != m.end() && it->second.IsConfigured()) ? &it->second : nullptr;
}

// The environment tier for one id: the first prefix that names a client id
// supplies the whole app. Runs outside the lock (getenv only) on a copy of
// the prefix list.
UltraNetOAuth2App FromEnvironment(const std::vector<std::string>& prefixes, const std::string& id) {
    for (const std::string& prefix : prefixes) {
        UltraNetOAuth2App app;
        app.clientId = Env(UltraNet_OAuth2AppEnvName(prefix, id, "_CLIENT_ID"));
        if (!app.IsConfigured()) continue;
        app.clientSecret = Env(UltraNet_OAuth2AppEnvName(prefix, id, "_CLIENT_SECRET"));
        app.redirectUri  = Env(UltraNet_OAuth2AppEnvName(prefix, id, "_REDIRECT_URI"));
        return app;
    }
    return UltraNetOAuth2App{};
}

// One id through the four tiers. Unconfigured when none names it.
UltraNetOAuth2App Lookup(const std::string& id) {
    std::vector<std::string> prefixes;
    {
        std::lock_guard<std::mutex> lock(Reg().mutex);
        if (const auto* a = Configured(Reg().set, id)) return *a;
        prefixes = Reg().prefixes;
    }
    if (UltraNetOAuth2App fromEnv = FromEnvironment(prefixes, id); fromEnv.IsConfigured())
        return fromEnv;
    std::lock_guard<std::mutex> lock(Reg().mutex);
    if (const auto* a = Configured(Reg().file, id)) return *a;
    if (const auto* a = Configured(Reg().builtIn, id)) return *a;
    return UltraNetOAuth2App{};
}

} // namespace

// ---- Registrations ----------------------------------------------------------

void UltraNet_OAuth2SetApp(const std::string& providerId, const UltraNetOAuth2App& app) {
    std::lock_guard<std::mutex> lock(Reg().mutex);
    Reg().set[providerId] = app;
}

void UltraNet_OAuth2SetBuiltInApp(const std::string& providerId, const UltraNetOAuth2App& app) {
    if (!app.IsConfigured()) return;
    std::lock_guard<std::mutex> lock(Reg().mutex);
    Reg().builtIn[providerId] = app;
}

void UltraNet_OAuth2AddAppEnvPrefix(const std::string& prefix) {
    if (prefix.empty()) return;
    std::lock_guard<std::mutex> lock(Reg().mutex);
    auto& p = Reg().prefixes;
    if (std::find(p.begin(), p.end(), prefix) == p.end()) p.push_back(prefix);
}

std::vector<std::string> UltraNet_OAuth2AppEnvPrefixes() {
    std::lock_guard<std::mutex> lock(Reg().mutex);
    return Reg().prefixes;
}

std::string UltraNet_OAuth2AppEnvName(const std::string& prefix, const std::string& providerId,
                                      const std::string& suffix) {
    std::string name = prefix;
    for (char c : providerId)
        name.push_back(std::isalnum(static_cast<unsigned char>(c))
                           ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_');
    return name + suffix;
}

void UltraNet_OAuth2SetAppAlias(const std::string& providerId, const std::string& fallbackId) {
    if (providerId.empty()) return;
    std::lock_guard<std::mutex> lock(Reg().mutex);
    if (fallbackId.empty() || fallbackId == providerId) Reg().aliases.erase(providerId);
    else                                                 Reg().aliases[providerId] = fallbackId;
}

int UltraNet_OAuth2ParseAppsIni(const std::string& text) {
    std::map<std::string, UltraNetOAuth2App> parsed;
    std::istringstream in(text);
    std::string line, section;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = Lower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty()) continue;
        const std::string key   = Lower(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));
        UltraNetOAuth2App& app = parsed[section];
        if      (key == "client_id")     app.clientId     = value;
        else if (key == "client_secret") app.clientSecret = value;
        else if (key == "redirect_uri" && !value.empty()) app.redirectUri = value;
    }
    int count = 0;
    std::lock_guard<std::mutex> lock(Reg().mutex);
    for (auto& [id, app] : parsed) {
        if (!app.IsConfigured()) continue;
        Reg().file[id] = app;
        ++count;
    }
    return count;
}

int UltraNet_OAuth2LoadAppsFile(const std::string& path) {
    std::ifstream is(path);
    if (!is) return 0;
    std::string text((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    return UltraNet_OAuth2ParseAppsIni(text);
}

// ---- Lookup -----------------------------------------------------------------

UltraNetOAuth2App UltraNet_OAuth2GetApp(const std::string& providerId) {
    std::set<std::string> seen;
    std::string id = providerId;
    while (!id.empty() && seen.insert(id).second) {
        if (UltraNetOAuth2App app = Lookup(id); app.IsConfigured()) return app;
        std::lock_guard<std::mutex> lock(Reg().mutex);
        auto it = Reg().aliases.find(id);
        id = (it == Reg().aliases.end()) ? std::string() : it->second;
    }
    return UltraNetOAuth2App{};
}

bool UltraNet_OAuth2HasApp(const std::string& providerId) {
    return UltraNet_OAuth2GetApp(providerId).IsConfigured();
}

void UltraNet_OAuth2ClearApps() {
    std::lock_guard<std::mutex> lock(Reg().mutex);
    Reg().set.clear();
    Reg().file.clear();
}
