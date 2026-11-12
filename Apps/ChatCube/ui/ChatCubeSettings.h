// Apps/ChatCube/ui/ChatCubeSettings.h
// ChatCube's own preferences: what the Settings window sets, kept in
// <settings folder>/ChatCube/settings.json (UltraCanvasAppSettings) - the
// look, how times and dates are written, which key sends, whether typing is
// shown either way, and whether the people on the local network are listed.
// Everything but the look applies at once; the look is read when ChatCube
// starts, since every element of the window takes its colours as it is built.
//
// Version: 0.3.0 - the relay server is a setting (Networks > Relay), ULTRA OS's own relay by default
// Version: 0.2.0 - the look is system, light or dark; system follows the desktop
// Version: 0.1.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace ChatCube {

// How a date is written: 31.10.2026, 2026-10-31 or 10/31/2026.
enum class DateStyle { DayMonthYear, YearMonthDay, MonthDayYear };

struct Settings {
    // Appearance
    // The look, from the next start: "system" (the desktop's, light or
    // dark, and UOS-Settings' Look page over it), "light" or "dark".
    std::string look = "system";
    bool DarkFor(bool hostDark) const { return look == "dark" || (look == "system" && hostDark); }
    bool      clock24 = true;                       // 14:05; else 2:05 PM
    DateStyle dateStyle = DateStyle::DayMonthYear;
    // Messages
    bool      ctrlEnterSends = false;               // Ctrl+Enter sends and Enter breaks the line
    bool      sendTyping = true;                    // tell the other side while I type
    bool      showTyping = true;                    // show "typing…" when they do
    // Networks
    bool      showNearby = true;                    // "On this network" in the sidebar
    // The relay ChatCube Direct signs in through, finds people and keeps
    // messages for the offline on; "" = the one this computer used last,
    // or the local network only when there was none.
    static constexpr const char* kDefaultRelay = "https://relay-chatcube.ultraos.eu";
    std::string relayServer = kDefaultRelay;

    // Reads the file; a missing one leaves the defaults.
    void Load();
    // Writes the file; false when it cannot be.
    bool Save() const;
    // Where it is kept (UTF-8; empty without a settings folder).
    static std::string File();
};

// The words for a date style, as the Settings window offers it: "31.10.2026
// (day, month, year)".
std::string DateStyleExample(DateStyle style);

} // namespace ChatCube
