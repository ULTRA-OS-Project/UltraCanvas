// Apps/UltraAuthenticator/Preferences.h
// The handful of settings a user can change, and the file they live in.
//
// These are not secrets, so they do not go in the vault: a preference file is
// readable without the master password by design, and the vault's contents
// stay exactly one thing (accounts). They are also not framework settings, so
// they do not go in the framework's configuration either. What is left is a
// plain key = value file beside the vault, written by this class and by
// nothing else.
//
// Every value is clamped on load. A hand-edited "idle_lock_seconds = -1" or
// "= 999999999" must not disable the lock or overflow a comparison; it just
// becomes the nearest sensible value.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATOR_PREFERENCES_H
#define AUTHENTICATOR_PREFERENCES_H

#include <cstdint>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

struct Preferences {
    // Seconds without mouse or keyboard input to this window before the vault
    // is locked. 0 disables the idle lock (the minimise lock and the manual
    // Lock button still work).
    uint32_t idleLockSeconds = 300;
    static constexpr uint32_t kMaxIdleLockSeconds = 24 * 60 * 60;

    // Lock the moment the window is minimised. On, because a minimised window
    // is one the user has stopped looking at.
    bool lockOnMinimize = true;

    // Show a code only after its card is clicked, and only briefly. Off by
    // default: for most people the whole point is glancing at the code, and
    // the shoulder-surfing case is theirs to opt into.
    bool hideCodes = false;

    // Serialisation. Parse() accepts anything: unknown keys are ignored,
    // malformed values keep the default, and every value is clamped.
    std::string        Serialize() const;
    static Preferences Parse(const std::string& text);

    // A missing or unreadable file yields the defaults, silently — it is the
    // normal state of a fresh installation. Save() returns false only when the
    // file could not be written.
    static Preferences Load(const std::string& path);
    bool               Save(const std::string& path) const;

    bool operator==(const Preferences& other) const {
        return idleLockSeconds == other.idleLockSeconds &&
               lockOnMinimize == other.lockOnMinimize &&
               hideCodes == other.hideCodes;
    }
    bool operator!=(const Preferences& other) const { return !(*this == other); }
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATOR_PREFERENCES_H
