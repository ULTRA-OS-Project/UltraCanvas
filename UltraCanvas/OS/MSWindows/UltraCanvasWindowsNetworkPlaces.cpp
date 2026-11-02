// OS/MSWindows/UltraCanvasWindowsNetworkPlaces.cpp
// Windows backend of UltraCanvasNetworkPlaces: the computers of the network
// (WNetOpenEnum - the Windows network provider's list of the workgroup or
// domain, plus the servers of the user's own connections), the folders one of
// them shares (NetShareEnum, else the provider's list of the server), and the
// WSL distributions (HKCU\...\Lxss, the registry WSL keeps them in).
//
// mpr.dll and netapi32.dll are loaded from System32 the first time they are
// needed, not imported: an installed package can carry an ImageMagick coder
// named mpr.dll (UltraCanvasCoderModuleRepair.h), and Windows matches modules
// by base name - an import of MPR.dll by the core library would be bound at
// start-up, in every application, for a list only a file manager asks for.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasNetworkPlaces.h"
#include "UltraCanvasUtils.h"   // Utf8ToWide, WideToUtf8

#include <algorithm>
#include <cwctype>
#include <mutex>
#include <set>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winnetwk.h>   // WNetOpenEnumW (mpr)
#include <lm.h>         // NetShareEnum (netapi32)

// The share type's low byte (disk, printer, device, IPC). The Windows SDK
// names it; mingw-w64's lmshare.h does not.
#ifndef STYPE_MASK
#define STYPE_MASK 0x000000FF
#endif

namespace UltraCanvas {

    namespace {

        // The five calls this file makes into mpr.dll and netapi32.dll,
        // looked up once in the copies in System32.
        struct NetworkApi {
            decltype(&WNetOpenEnumW) openEnum = nullptr;
            decltype(&WNetEnumResourceW) enumResource = nullptr;
            decltype(&WNetCloseEnum) closeEnum = nullptr;
            decltype(&NetShareEnum) shareEnum = nullptr;
            decltype(&NetApiBufferFree) bufferFree = nullptr;
        };

        HMODULE LoadSystemLibrary(const wchar_t* name) {
            // System32 only: never a same-named file in the program folder.
            if (HMODULE m = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) return m;
            // A Windows without the search flag (7 before KB2533623): the
            // full path does the same.
            wchar_t dir[MAX_PATH] = {};
            const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return nullptr;
            return LoadLibraryW((std::wstring(dir) + L"\\" + name).c_str());
        }

        template <class F>
        void Resolve(HMODULE module, const char* name, F& out) {
            out = module ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(module, name)))
                         : nullptr;
        }

        const NetworkApi& Api() {
            static NetworkApi api;
            static std::once_flag once;
            std::call_once(once, []() {
                HMODULE mpr = LoadSystemLibrary(L"mpr.dll");
                HMODULE netapi = LoadSystemLibrary(L"netapi32.dll");
                Resolve(mpr, "WNetOpenEnumW", api.openEnum);
                Resolve(mpr, "WNetEnumResourceW", api.enumResource);
                Resolve(mpr, "WNetCloseEnum", api.closeEnum);
                Resolve(netapi, "NetShareEnum", api.shareEnum);
                Resolve(netapi, "NetApiBufferFree", api.bufferFree);
            });
            return api;
        }

        std::wstring Lower(std::wstring text) {
            for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
            return text;
        }

        bool LessIgnoringCase(const std::string& a, const std::string& b) {
            return Lower(Utf8ToWide(a)) < Lower(Utf8ToWide(b));
        }

        // Every NETRESOURCE a WNet enumeration hands back, `container` (null
        // for the top) enumerated with `scope`. One level; the caller descends.
        template <class Visit>
        void EnumerateNetwork(DWORD scope, NETRESOURCEW* container, Visit visit) {
            const NetworkApi& api = Api();
            if (!api.openEnum || !api.enumResource || !api.closeEnum) return;
            HANDLE handle = nullptr;
            if (api.openEnum(scope, RESOURCETYPE_DISK, 0, container, &handle) != NO_ERROR)
                return;
            std::vector<BYTE> buffer(16 * 1024);
            for (;;) {
                DWORD count = static_cast<DWORD>(-1);
                DWORD size = static_cast<DWORD>(buffer.size());
                const DWORD result = api.enumResource(handle, &count, buffer.data(), &size);
                if (result == ERROR_MORE_DATA && count == 0) {
                    buffer.resize(size);   // one entry larger than the buffer
                    continue;
                }
                if (result != NO_ERROR && result != ERROR_MORE_DATA) break;
                const NETRESOURCEW* items = reinterpret_cast<const NETRESOURCEW*>(buffer.data());
                for (DWORD i = 0; i < count; ++i) visit(items[i]);
                if (result == NO_ERROR && count == 0) break;
            }
            api.closeEnum(handle);
        }

        // "\\NAS\photos" -> "NAS"; empty when it is not a UNC path.
        std::wstring ServerOfUnc(const std::wstring& unc) {
            if (unc.size() < 3 || unc[0] != L'\\' || unc[1] != L'\\') return {};
            const size_t end = unc.find(L'\\', 2);
            return unc.substr(2, end == std::wstring::npos ? std::wstring::npos : end - 2);
        }

        std::wstring RegistryString(HKEY key, const wchar_t* name) {
            DWORD type = 0, size = 0;
            if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
                (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0)
                return {};
            std::wstring value(size / sizeof(wchar_t), L'\0');
            if (RegQueryValueExW(key, name, nullptr, nullptr,
                                 reinterpret_cast<BYTE*>(&value[0]), &size) != ERROR_SUCCESS)
                return {};
            value.resize(wcsnlen(value.c_str(), value.size()));
            return value;
        }

    } // namespace

    bool NetworkPlacesAvailable() { return true; }

    std::vector<NetworkComputer> ListNetworkComputers() {
        std::vector<NetworkComputer> computers;
        std::set<std::wstring> seen;
        auto add = [&](const std::wstring& name, const std::wstring& comment) {
            if (name.empty() || !seen.insert(Lower(name)).second) return;
            NetworkComputer c;
            c.name = WideToUtf8(name);
            c.path = "\\\\" + c.name;
            c.comment = WideToUtf8(comment);
            computers.push_back(std::move(c));
        };

        // The computers of this machine's workgroup or domain, as the
        // Windows network provider knows them - the list the old "Network
        // Neighborhood" showed. A domain or workgroup is a container: one
        // level down are its servers.
        EnumerateNetwork(RESOURCE_CONTEXT, nullptr, [&](const NETRESOURCEW& r) {
            if (r.dwDisplayType == RESOURCEDISPLAYTYPE_SERVER && r.lpRemoteName)
                add(ServerOfUnc(r.lpRemoteName), r.lpComment ? r.lpComment : L"");
        });

        // The servers of the user's own connections - a mapped drive, a
        // share opened and remembered: reachable however the network
        // advertises (or does not advertise) its computers.
        for (DWORD scope : {static_cast<DWORD>(RESOURCE_CONNECTED), static_cast<DWORD>(RESOURCE_REMEMBERED)}) {
            EnumerateNetwork(scope, nullptr, [&](const NETRESOURCEW& r) {
                if (r.lpRemoteName) add(ServerOfUnc(r.lpRemoteName), L"");
            });
        }

        std::sort(computers.begin(), computers.end(),
                  [](const NetworkComputer& a, const NetworkComputer& b) {
                      return LessIgnoringCase(a.name, b.name);
                  });
        return computers;
    }

    bool ListNetworkShares(const std::string& server, std::vector<NetworkShare>& out,
                           std::string& error) {
        out.clear();
        const std::string name = NetworkServerName(NetworkServerPath(server));
        if (name.empty()) {
            error = "\"" + server + "\" names no computer";
            return false;
        }
        const std::wstring wideServer = L"\\\\" + Utf8ToWide(name);
        auto add = [&](const std::wstring& share, const std::wstring& comment) {
            // C$, ADMIN$, IPC$, print$: the administrative and hidden shares
            // Explorer leaves out too.
            if (share.empty() || share.back() == L'$') return;
            NetworkShare s;
            s.name = WideToUtf8(share);
            s.path = "\\\\" + name + "\\" + s.name;
            s.comment = WideToUtf8(comment);
            out.push_back(std::move(s));
        };

        const NetworkApi& api = Api();
        SHARE_INFO_1* info = nullptr;
        DWORD read = 0, total = 0, resume = 0;
        const NET_API_STATUS status = !api.shareEnum ? static_cast<NET_API_STATUS>(ERROR_PROC_NOT_FOUND)
                : api.shareEnum(const_cast<LPWSTR>(wideServer.c_str()), 1,
                                reinterpret_cast<LPBYTE*>(&info), MAX_PREFERRED_LENGTH,
                                &read, &total, &resume);
        if ((status == NERR_Success || status == ERROR_MORE_DATA) && info) {
            for (DWORD i = 0; i < read; ++i) {
                // Folders only - not printers, devices or IPC - and not the
                // ones marked special.
                if ((info[i].shi1_type & STYPE_MASK) != STYPE_DISKTREE) continue;
                if (info[i].shi1_type & STYPE_SPECIAL) continue;
                add(info[i].shi1_netname ? info[i].shi1_netname : L"",
                    info[i].shi1_remark ? info[i].shi1_remark : L"");
            }
        }
        if (info && api.bufferFree) api.bufferFree(info);

        if (status != NERR_Success && status != ERROR_MORE_DATA) {
            // Some servers (Samba with share listing restricted to a
            // protocol NetShareEnum does not speak) still answer the
            // network provider: ask it for the server's folders.
            NETRESOURCEW container{};
            container.dwScope = RESOURCE_GLOBALNET;
            container.dwType = RESOURCETYPE_DISK;
            container.dwDisplayType = RESOURCEDISPLAYTYPE_SERVER;
            container.dwUsage = RESOURCEUSAGE_CONTAINER;
            container.lpRemoteName = const_cast<LPWSTR>(wideServer.c_str());
            EnumerateNetwork(RESOURCE_GLOBALNET, &container, [&](const NETRESOURCEW& r) {
                if (!r.lpRemoteName) return;
                const std::wstring unc = r.lpRemoteName;
                const size_t slash = unc.rfind(L'\\');
                if (slash != std::wstring::npos && slash > 1)
                    add(unc.substr(slash + 1), r.lpComment ? r.lpComment : L"");
            });
            if (out.empty()) {
                switch (status) {
                    case ERROR_ACCESS_DENIED:
                        error = "\\\\" + name + " refused to list its shares - sign in to it "
                                "first (open one of its shares by its full name)";
                        break;
                    case ERROR_BAD_NETPATH:
                    case ERROR_BAD_NET_NAME:
                    case NERR_NetNameNotFound:
                        error = "\\\\" + name + " was not found on the network";
                        break;
                    default:
                        error = "\\\\" + name + " did not list its shares (error " +
                                std::to_string(status) + ")";
                        break;
                }
                return false;
            }
        }
        std::sort(out.begin(), out.end(), [](const NetworkShare& a, const NetworkShare& b) {
            return LessIgnoringCase(a.name, b.name);
        });
        return true;
    }

    std::vector<WslDistribution> ListWslDistributions() {
        std::vector<WslDistribution> distributions;
        HKEY lxss = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss",
                          0, KEY_READ, &lxss) != ERROR_SUCCESS)
            return distributions;   // WSL was never set up for this user
        const std::wstring defaultId = Lower(RegistryString(lxss, L"DefaultDistribution"));
        for (DWORD index = 0;; ++index) {
            wchar_t subkey[256];
            DWORD length = 256;
            if (RegEnumKeyExW(lxss, index, subkey, &length, nullptr, nullptr, nullptr, nullptr) !=
                ERROR_SUCCESS)
                break;
            HKEY entry = nullptr;
            if (RegOpenKeyExW(lxss, subkey, 0, KEY_READ, &entry) != ERROR_SUCCESS) continue;
            const std::wstring name = RegistryString(entry, L"DistributionName");
            DWORD state = 1, size = sizeof(state), type = 0;
            // State 1 is installed; one being installed or removed is not a
            // place to open yet. An entry without the value is taken as
            // installed.
            if (RegQueryValueExW(entry, L"State", nullptr, &type,
                                 reinterpret_cast<BYTE*>(&state), &size) != ERROR_SUCCESS ||
                type != REG_DWORD)
                state = 1;
            RegCloseKey(entry);
            if (name.empty() || state != 1) continue;
            WslDistribution d;
            d.name = WideToUtf8(name);
            // \\wsl$ works with every WSL release; \\wsl.localhost is the
            // newer spelling of the same root.
            d.path = "\\\\wsl$\\" + d.name;
            d.isDefault = Lower(subkey) == defaultId;
            distributions.push_back(std::move(d));
        }
        RegCloseKey(lxss);
        std::sort(distributions.begin(), distributions.end(),
                  [](const WslDistribution& a, const WslDistribution& b) {
                      if (a.isDefault != b.isDefault) return a.isDefault;   // the default first
                      return LessIgnoringCase(a.name, b.name);
                  });
        return distributions;
    }

} // namespace UltraCanvas
