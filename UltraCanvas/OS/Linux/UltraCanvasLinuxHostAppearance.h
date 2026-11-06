// OS/Linux/UltraCanvasLinuxHostAppearance.h
// INTERNAL: the desktop's look on Linux and the BSDs, for the X11 and the
// Wayland backends' DetectHostAppearanceNative (T2 of
// Docs/UltraCanvas/UltraCanvasHostThemeProposal.md). The sources are read
// here - the portal over D-Bus, GSettings through GIO, ~/.config/kdeglobals,
// gtk-4.0/ and gtk-3.0/settings.ini - and decoded by the pure readers of
// UltraCanvasHostAppearance.h (HostAppearanceLinux); the X11 backend hands
// in the XSETTINGS property it fetched, Wayland has none. The fonts are the
// font detector's (UltraCanvasLinuxFonts.h). Not a public header.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasHostAppearance.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace UltraCanvas {
namespace LinuxHostAppearance {

    // Fills `bytes` with the _XSETTINGS_SETTINGS property of the screen's
    // manager and returns true; false when no manager runs. The X11 backend
    // gives one, the Wayland backend none.
    using XSettingsFetcher = std::function<bool(std::vector<uint8_t>& bytes)>;

    // The sources as this session has them: the XSETTINGS property through
    // `fetchXSettings` (may be empty), the portal and GSettings only inside a
    // desktop session (with none running, the portal is not there and the
    // schemas answer only their compiled defaults), the files always.
    HostAppearanceLinux::Sources ReadSources(const XSettingsFetcher& fetchXSettings);

    // The look: ReadSources composed by HostAppearanceFromSources, the fonts
    // from the font detector, the source prefixed "linux:".
    HostAppearance Detect(const XSettingsFetcher& fetchXSettings);

    // The portal's org.freedesktop.appearance color-scheme over the session
    // bus (0 none, 1 dark, 2 light); nothing when the portal does not answer
    // within a second. Asked only inside a desktop session.
    std::optional<uint32_t> ReadPortalColorScheme();

} // namespace LinuxHostAppearance
} // namespace UltraCanvas
