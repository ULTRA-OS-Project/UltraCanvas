// UltraCanvas/Plugins/UltraNet/common/UltraNetPluginHostShim.h
// The plug-in half of the host table (UltraNetPluginHost, UltraNetPlugins.h).
//
// Every UltraNet plug-in compiles UltraNetPluginHostShim.cpp in. It defines,
// inside the plug-in DSO, the core functions a plug-in calls - UltraNet_ParseUrl,
// UltraNet_MimeBuild, UltraNet_HttpRequest, ... - and forwards each call to
// the table the host passed to UltraNet_PluginInit. Plug-in sources keep
// calling the ordinary functions; the DSO just no longer needs the core to
// provide them, so it loads into an app on a static core and a Windows DLL
// links without the core's import library.
//
// Call UltraNetPlugin_AttachHost() first thing in UltraNet_PluginInit and
// register nothing when it refuses the host.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

struct UltraNetPluginHost;

// Keep `host` for the forwarding functions. False (and nothing kept) for a
// null host or one older than ABI 2 - such a host cannot serve the calls the
// plug-in makes, so the plug-in must not register.
bool UltraNetPlugin_AttachHost(const UltraNetPluginHost* host);
