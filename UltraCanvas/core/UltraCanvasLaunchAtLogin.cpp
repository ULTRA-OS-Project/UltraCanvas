// core/UltraCanvasLaunchAtLogin.cpp
// The platform-independent half of UltraCanvasLaunchAtLogin: the id check,
// and the fallback for platforms with no startup list (Android, iOS,
// WebAssembly). The backends are OS/<Platform>/UltraCanvas*LaunchAtLogin.*.
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#include "UltraCanvasLaunchAtLogin.h"
#include "UltraLocalisation/UltraLocalisation.h"

namespace UltraCanvas {

namespace {

// The id names a file on two platforms and a registry value on the third,
// so it is kept to what is safe in all three.
bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > 128 || id.front() == '.') return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

} // namespace

bool LaunchAtLoginAvailable() {
    return LaunchAtLoginBackend::Available();
}

bool IsLaunchAtLoginEnabled(const std::string& id) {
    if (!ValidId(id) || !LaunchAtLoginBackend::Available()) return false;
    return LaunchAtLoginBackend::IsEnabled(id);
}

bool SetLaunchAtLogin(const LaunchAtLoginEntry& entry, bool enabled, std::string& error) {
    error.clear();
    if (!ValidId(entry.id)) {
        error = TrF("The startup list could not be changed: {0}", "\"" + entry.id + "\"");
        return false;
    }
    if (!LaunchAtLoginBackend::Available()) {
        error = Tr("This computer cannot start programs when you sign in.");
        return false;
    }
    return LaunchAtLoginBackend::Set(entry, enabled, error);
}

std::string LaunchAtLoginCommand(const LaunchAtLoginEntry& entry) {
    if (!LaunchAtLoginBackend::Available()) return {};
    return LaunchAtLoginBackend::Command(entry);
}

#ifndef ULTRACANVAS_HAS_NATIVE_LAUNCH_AT_LOGIN
namespace LaunchAtLoginBackend {
    bool Available() { return false; }
    bool IsEnabled(const std::string&) { return false; }
    bool Set(const LaunchAtLoginEntry&, bool, std::string& error) {
        error = Tr("This computer cannot start programs when you sign in.");
        return false;
    }
    std::string Command(const LaunchAtLoginEntry&) { return {}; }
} // namespace LaunchAtLoginBackend
#endif

} // namespace UltraCanvas
