// core/UltraCanvasNetworkPlaces.cpp
// Platform-independent half of the network places: the path rules, and the
// empty lists of a platform without a backend. Every operating system call
// lives in OS/<Platform>/.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasNetworkPlaces.h"

#include <cctype>

namespace UltraCanvas {

#ifndef ULTRACANVAS_HAS_NETWORK_PLACES
    // No backend on this platform. The platforms that do have one define
    // these in their own file and CMake sets ULTRACANVAS_HAS_NETWORK_PLACES.
    bool NetworkPlacesAvailable() { return false; }
    std::vector<NetworkComputer> ListNetworkComputers() { return {}; }
    bool ListNetworkShares(const std::string&, std::vector<NetworkShare>& out, std::string& error) {
        out.clear();
        error = "this system has no network places in UltraCanvas";
        return false;
    }
    std::vector<WslDistribution> ListWslDistributions() { return {}; }
#endif

    namespace {
        bool IsSeparator(char c) { return c == '\\' || c == '/'; }
    }

    std::string NetworkServerName(const std::string& path) {
        // Two separators, then a name, then at most one separator.
        if (path.size() < 3 || !IsSeparator(path[0]) || !IsSeparator(path[1])) return {};
        std::string name;
        size_t i = 2;
        for (; i < path.size() && !IsSeparator(path[i]); ++i) name += path[i];
        for (; i < path.size(); ++i)
            if (!IsSeparator(path[i])) return {};   // something below the computer
        if (name.empty()) return {};
        // "\\?\C:\" and "\\.\pipe" are Windows' own prefixes, not computers;
        // "\\wsl$" and "\\wsl.localhost" are the WSL roots, which list the
        // distributions rather than shares (ListWslDistributions).
        if (name == "?" || name == ".") return {};
        std::string lower;
        for (char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "wsl$" || lower == "wsl.localhost") return {};
        return name;
    }

    bool IsNetworkServerPath(const std::string& path) {
        return !NetworkServerName(path).empty();
    }

    std::string NetworkServerPath(const std::string& server) {
        std::string name = NetworkServerName(server);
        if (name.empty()) {
            // A bare name: "NAS".
            for (char c : server)
                if (IsSeparator(c)) return {};
            name = server;
        }
        return name.empty() ? std::string() : "\\\\" + name;
    }

} // namespace UltraCanvas
