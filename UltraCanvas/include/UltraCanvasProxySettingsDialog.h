// include/UltraCanvasProxySettingsDialog.h
// The proxy settings dialog every UltraCanvas application shows - one dialog,
// one setting for all of them (UltraCanvasProxySettings.h):
//
//   ( ) No proxy
//   (o) Use system proxy settings          what the system names now
//         [ ] Automatic proxy configuration URL  [ https://... ]
//   ( ) Manual proxy configuration
//         (o) HTTP  ( ) SOCKS
//         Host name      [ localhost        ]
//         Port           [ 10808 ^v ]
//         No proxy for   [ *.example.com, 192.168.* ]
//         [ ] Proxy authentication
//         User           [ ... ]
//         Password       [ ... ]
//                        [ ] Remember   [ Clear passwords ]
//   [ Check connection ]                         [ Cancel ] [ OK ]
//
// OK saves Proxy.conf; every application following the shared settings uses
// the new ones from its next connection. Check connection opens an address
// with the settings as they stand in the dialog, saved or not, and says
// whether it went through the proxy or directly.
//
// doc-check: UltraCanvas::UltraCanvasWindowBase* window;
// doc-check: example
//   UltraCanvas::ShowProxySettingsDialog(window, [](const UltraCanvas::ProxySettings& saved) {
//       // saved.mode, saved.host ... - already in force
//   });
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasProxySettings.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

    class UltraCanvasWindowBase;
    class UltraCanvasContainer;

    // Shows the dialog over `parent`, filled in from Proxy.conf. `onSaved`
    // runs after OK has saved the settings; nothing runs on Cancel.
    void ShowProxySettingsDialog(UltraCanvasWindowBase* parent,
                                 std::function<void(const ProxySettings&)> onSaved = {});

    // One line saying what the saved settings do, for a settings page that
    // shows them beside a button opening the dialog: "System proxy settings",
    // "No proxy", "SOCKS proxy localhost:10808".
    std::string DescribeProxySettings(const ProxySettings& settings);

    // A whole page for an application's settings window, in the
    // SettingsPageKit look: the title, what the shared settings do now, a
    // Change button opening the dialog over `parent`, and a note that every
    // ULTRA OS application uses them. The line follows a save. `id` prefixes
    // the elements' identifiers.
    std::shared_ptr<UltraCanvasContainer> MakeProxySettingsPage(const std::string& id,
                                                                UltraCanvasWindowBase* parent);

} // namespace UltraCanvas
