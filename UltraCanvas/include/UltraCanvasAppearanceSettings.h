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
//   lighttext=firm       firm | plain
//
// lighttext is the one line no host has a word for: firm, the default,
// draws light text on a dark ground a second time so it keeps its weight
// (RenderContextCairo::SetLightTextBoost); plain draws every text once.
//
// ApplyTo puts the choices over a HostAppearance the platform read, before
// ULTRACANVAS_HOST_APPEARANCE (which still wins for one run); the source
// gains "+settings". The application reads it with the host, and the
// settings watcher (UltraCanvasSettingsWatcher.h) re-reads it when
// UOS-Settings writes it, so a change reaches a running application as a
// change of the desktop does. On ULTRA OS, where no host sits above the
// desktop, the file is the host.
//
// Version: 1.1.1 - Save retries a refused replace, then writes in place
// Version: 1.1.0 - lighttext=firm|plain
// Version: 1.0.0
// Last Modified: 2026-11-15
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
        std::string lightText = "firm";    // firm | plain

        bool operator==(const AppearanceSettings&) const = default;

        // Every line says "system": the file changes nothing.
        bool IsSystem() const;

        // The file's text and back. Parse ignores lines it does not know and
        // keeps the default for a value it does not accept (a look that is
        // not light or dark, an accent that is not a colour, a size that is
        // not a positive number, a scale outside 0.5 to 4, a light text that is
        // not firm or plain). Pure.
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
        // reading at that moment sees the old setting or the new one. Where
        // the rename is refused because another program has the file open
        // (Windows: the applications reading the last change, a virus
        // scanner) it is tried again for up to half a second, then the file
        // is written in place. False only when it cannot be written at all.
        bool Save() const;
    };

} // namespace UltraCanvas
