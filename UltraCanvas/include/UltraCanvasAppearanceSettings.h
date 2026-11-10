// include/UltraCanvasAppearanceSettings.h
// A person's choices over the desktop's look, for every UltraCanvas
// application at once: Appearance.conf in the UltraCanvas settings folder
// (UltraCanvasSettingsFolder.h), the Emoji.conf pattern - every line
// optional, a missing or "system" line meaning "what the host says":
//
//   look=system          system | light | dark
//   accent=system        system | #rrggbb
//   font=system          system | <family>
//   fontsize=system      system | <points>
//   scale=1.0            over the host's text scale, 0.5 to 4
//   contrast=system      system | high | normal
//
// ApplyTo puts the choices over a HostAppearance the platform read, before
// ULTRACANVAS_HOST_APPEARANCE (which still wins for one run); the source
// gains "+settings". The application reads it with the host, and the
// settings watcher (UltraCanvasSettingsWatcher.h) re-reads it when
// UOS-Settings writes it, so a change reaches a running application as a
// change of the desktop does. On ULTRA OS, where no host sits above the
// desktop, the file is the host.
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasHostAppearance.h"

#include <filesystem>
#include <string>

namespace UltraCanvas {

    struct AppearanceSettings {
        std::string look = "system";       // system | light | dark
        std::string accent = "system";     // system | #rrggbb
        std::string font = "system";       // system | a family
        double fontSize = 0.0;             // 0: system; else points
        double scale = 1.0;                // over the host's text scale
        std::string contrast = "system";   // system | high | normal

        bool operator==(const AppearanceSettings&) const = default;

        // Every line says "system": the file changes nothing.
        bool IsSystem() const;

        // The file's text and back. Parse ignores lines it does not know and
        // keeps the default for a value it does not accept (a look that is
        // not light or dark, an accent that is not a colour, a size that is
        // not a positive number, a scale outside 0.5 to 4). Pure.
        static AppearanceSettings Parse(const std::string& text);
        std::string Serialize() const;

        // The choices over what the host said; true when one applied, and
        // then the source gains "+settings". Pure.
        bool ApplyTo(HostAppearance& host) const;

        // <settings folder>/Appearance.conf; empty when the environment names
        // no settings folder.
        static std::filesystem::path FilePath();
        // A missing or unreadable file is the defaults.
        static AppearanceSettings Load();
        // Written beside the file and renamed over it, so an application
        // reading at that moment sees the old setting or the new one.
        bool Save() const;
    };

} // namespace UltraCanvas
