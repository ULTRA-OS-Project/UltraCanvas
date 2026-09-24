// Apps/UltraAuthenticator/Preferences.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "Preferences.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace UltraCanvas {
namespace Authenticator {

namespace {

constexpr const char* kKeyIdle     = "idle_lock_seconds";
constexpr const char* kKeyMinimize = "lock_on_minimize";
constexpr const char* kKeyHide     = "hide_codes";

std::string TrimAscii(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// "true"/"false", "yes"/"no", "on"/"off", "1"/"0". Anything else: unchanged.
void ParseBool(const std::string& value, bool& out) {
    std::string v;
    for (char c : value) v.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (v == "true" || v == "yes" || v == "on" || v == "1")  { out = true;  return; }
    if (v == "false" || v == "no" || v == "off" || v == "0") { out = false; return; }
}

// Decimal, clamped to [0, max]. A sign, a stray letter or an empty value
// leaves `out` unchanged rather than producing 0, which would silently turn a
// timeout off.
void ParseSeconds(const std::string& value, uint32_t max, uint32_t& out) {
    if (value.empty()) return;
    uint64_t n = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return;
        n = n * 10 + static_cast<uint64_t>(c - '0');
        if (n > max) { n = max; break; }
    }
    out = static_cast<uint32_t>(n);
}

} // namespace

std::string Preferences::Serialize() const {
    std::ostringstream out;
    out << "# UltraAuthenticator settings. Not secret; the accounts are in the vault.\n"
        << kKeyIdle     << " = " << idleLockSeconds << "\n"
        << kKeyMinimize << " = " << (lockOnMinimize ? "true" : "false") << "\n"
        << kKeyHide     << " = " << (hideCodes ? "true" : "false") << "\n";
    return out.str();
}

Preferences Preferences::Parse(const std::string& text) {
    Preferences prefs;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = TrimAscii(trimmed.substr(0, eq));
        const std::string value = TrimAscii(trimmed.substr(eq + 1));
        if (key == kKeyIdle) {
            ParseSeconds(value, kMaxIdleLockSeconds, prefs.idleLockSeconds);
        } else if (key == kKeyMinimize) {
            ParseBool(value, prefs.lockOnMinimize);
        } else if (key == kKeyHide) {
            ParseBool(value, prefs.hideCodes);
        }
        // Unknown keys are ignored: a newer build may have written them.
    }
    if (prefs.idleLockSeconds > kMaxIdleLockSeconds) {
        prefs.idleLockSeconds = kMaxIdleLockSeconds;
    }
    return prefs;
}

Preferences Preferences::Load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Preferences{};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return Parse(buffer.str());
}

bool Preferences::Save(const std::string& path) const {
    // Write beside, then rename over: a crash mid-write leaves the previous
    // file intact rather than a truncated one that parses as "all defaults".
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << Serialize();
        if (!out) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace Authenticator
} // namespace UltraCanvas
