// Apps/UltraMail/ui/UltraMailPreferences.h
// App-wide UltraMail preferences (view options that are remembered between
// runs, independent of any account). Stored as a small key=value file next to
// the other per-user files under the data directory (preferences.ini), the
// same way oauth.ini lives there. Not per-account server settings — those stay
// on the Account in the local store.
// Version: 0.2.0 - fetchSenderIcons (download the known senders' icons)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraMail {

// The handful of app-wide view options. Add fields here (with a default) and a
// matching key in Load/Save; unknown keys and a missing file are ignored so an
// older/newer file never breaks startup.
struct Preferences {
    // Show the message preview (reading) pane beside the list. When false the
    // list takes the whole content area and a clicked message opens in its
    // place (Gmail-style).
    bool showReadingPane = true;

    // Whether UltraMail may download the icons of the services in its known-
    // sender registry into the sender-icon cache. Only those (a fixed list),
    // and only once each — never a lookup of a stranger's domain. Off means
    // the badge shows the sender's monogram in the brand's colour instead.
    bool fetchSenderIcons = true;

    // Read `path`; missing file or keys keep the defaults. Returns false only
    // when the file exists but could not be opened.
    bool Load(const std::string& path);

    // Write every value to `path` (created/truncated). Returns false on an I/O
    // error.
    bool Save(const std::string& path) const;
};

} // namespace UltraMail
