// include/UltraCanvasNetworkPlaces.h
// The places Windows Explorer lists outside "This PC": the computers on the
// network with the folders they share, and the Linux distributions of the
// Windows Subsystem for Linux.
//
//   for (const WslDistribution& d : ListWslDistributions())
//       AddRow(d.path, d.name);                  // "\\wsl$\Ubuntu"
//
//   std::vector<NetworkShare> shares;
//   std::string error;
//   if (ListNetworkShares("\\\\NAS", shares, error))
//       for (const NetworkShare& s : shares) AddRow(s.path, s.name);   // "\\NAS\photos"
//
// What lies below a share or a distribution is an ordinary path - a UNC path
// std::filesystem reads like any folder - so this answers only what is above
// it: which computers there are, which folders a computer shares, which
// distributions are installed. A computer itself ("\\NAS") is not a folder
// anything can list; IsNetworkServerPath tells it apart.
//
// ListNetworkComputers and ListNetworkShares go to the network and can take
// seconds - a computer that does not answer is waited out - so a caller runs
// them off its UI thread. ListWslDistributions reads the registry and touches
// no distribution (opening one starts it).
//
// Backends: Windows (WNetOpenEnum, NetShareEnum, the Lxss registry key).
// Elsewhere every list is empty: a Linux desktop mounts the shares it opens
// through GVFS (ListGvfsVolumes), and macOS mounts them under /Volumes.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {

    // A computer on the network.
    struct NetworkComputer {
        std::string name;       // "NAS"
        std::string path;       // "\\NAS" - what ListNetworkShares takes
        std::string comment;    // what the computer says about itself, may be empty
    };

    // A folder a computer shares.
    struct NetworkShare {
        std::string name;       // "photos"
        std::string path;       // "\\NAS\photos" - an ordinary folder from here on
        std::string comment;
    };

    // A Linux distribution of the Windows Subsystem for Linux.
    struct WslDistribution {
        std::string name;       // "Ubuntu"
        std::string path;       // "\\wsl$\Ubuntu" - its root, browsed like a share
        bool isDefault = false; // the one `wsl` starts without being told
    };

    // Whether this build can list any of these at all (Windows).
    bool NetworkPlacesAvailable();

    // The computers this machine can see on the network: the ones the
    // Windows network lists for its workgroup or domain, and the servers of
    // the connections the user has made (a mapped drive, a share opened
    // before), each once, by name. A network that publishes its computers
    // only through WS-Discovery shows fewer here than in Explorer; a computer
    // missing from the list is still reached by its name (ListNetworkShares).
    // Blocks for as long as the network takes to answer.
    std::vector<NetworkComputer> ListNetworkComputers();

    // The folders `server` shares ("\\NAS", "NAS" or "//NAS"), hidden and
    // administrative shares (C$, IPC$, ADMIN$) left out, by name. False with a
    // reason when the computer cannot be reached or refuses to say. Blocks.
    bool ListNetworkShares(const std::string& server, std::vector<NetworkShare>& out,
                           std::string& error);

    // The installed WSL distributions, by name, from the registry - none of
    // them is started.
    std::vector<WslDistribution> ListWslDistributions();

    // ===== PATHS =====
    // Whether `path` names a computer and nothing below it: "\\NAS",
    // "\\NAS\" and "//NAS" do; "\\NAS\photos", "\\?\C:\" and "\\wsl$" (the WSL
    // root, which no distribution is) do not.
    bool IsNetworkServerPath(const std::string& path);
    // The computer's name in such a path ("NAS"); empty when it names none.
    std::string NetworkServerName(const std::string& path);
    // "\\NAS" for "NAS", "//NAS" or "\\NAS\" - the form the lists use.
    std::string NetworkServerPath(const std::string& server);

} // namespace UltraCanvas
