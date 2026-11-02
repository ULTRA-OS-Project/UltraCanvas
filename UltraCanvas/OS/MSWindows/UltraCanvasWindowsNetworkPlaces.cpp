// OS/MSWindows/UltraCanvasWindowsNetworkPlaces.cpp
// Windows backend of UltraCanvasNetworkPlaces: the computers of the network
// (WNetOpenEnum - the Windows network provider's list of the workgroup or
// domain, plus the servers of the user's own connections), the folders one of
// them shares (NetShareEnum, else the provider's list of the server), and the
// WSL distributions (HKCU\...\Lxss, the registry WSL keeps them in).
//
// The computers also come from Function Discovery's WS-Discovery provider:
// current Windows (SMB 1 off) and NAS boxes running wsdd announce themselves
// only that way, so the WNet list misses them while Explorer, which asks
// Function Discovery, shows them. The query runs on a thread of its own in
// the multithreaded apartment - its answers arrive as calls from Function
// Discovery's threads, whatever apartment the caller is in - alongside the
// WNet enumeration, and is waited for at most kWsdSearchTimeout.
// The Function Discovery interfaces are declared here, not taken from
// <functiondiscoveryapi.h>: mingw-w64's copy puts IFunctionInstance's
// QueryService last instead of first (IServiceProvider is its base), which
// would call the wrong slot.
//
// mpr.dll and netapi32.dll are loaded from System32 the first time they are
// needed, not imported: an installed package can carry an ImageMagick coder
// named mpr.dll (UltraCanvasCoderModuleRepair.h), and Windows matches modules
// by base name - an import of MPR.dll by the core library would be bound at
// start-up, in every application, for a list only a file manager asks for.
// Version: 1.1.0 - computers announced only through WS-Discovery (Function Discovery)
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasNetworkPlaces.h"
#include "UltraCanvasUtils.h"   // Utf8ToWide, WideToUtf8

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winnetwk.h>   // WNetOpenEnumW (mpr)
#include <lm.h>         // NetShareEnum (netapi32)
#include <objbase.h>    // CoCreateInstance (ole32)
#include <propidl.h>    // PROPVARIANT, PropVariantClear
#include <propsys.h>    // IPropertyStore

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


        // ===== WS-DISCOVERY (FUNCTION DISCOVERY) =====

        // How long to wait for the WS-Discovery search to report it is
        // complete. The provider probes and collects answers for a few
        // seconds; a network that answers slower is cut off here.
        constexpr std::chrono::seconds kWsdSearchTimeout{6};

        // The Function Discovery interfaces this file calls, in the Windows
        // SDK's slot order (functiondiscoveryapi.idl). Only the slots up to
        // the last one called are declared.
        enum FdUpdateAction { FdUpdateAdd = 0, FdUpdateRemove = 1, FdUpdateChange = 2 };
        constexpr DWORD kFdEventSearchComplete = 1000;   // FD_EVENTID_SEARCHCOMPLETE
        typedef DWORDLONG FdQueryContext;

        struct FdInstance : public IUnknown {               // IFunctionInstance
            virtual HRESULT STDMETHODCALLTYPE QueryService(REFGUID, REFIID, void**) = 0;  // IServiceProvider
            virtual HRESULT STDMETHODCALLTYPE GetID(WCHAR** id) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetProviderInstanceID(WCHAR** id) = 0;
            virtual HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD access, IPropertyStore** store) = 0;
        };
        struct FdInstanceCollection : public IUnknown {     // IFunctionInstanceCollection
            virtual HRESULT STDMETHODCALLTYPE GetCount(DWORD* count) = 0;
            virtual HRESULT STDMETHODCALLTYPE Get(const WCHAR* id, DWORD* index, FdInstance** instance) = 0;
            virtual HRESULT STDMETHODCALLTYPE Item(DWORD index, FdInstance** instance) = 0;
        };
        struct FdNotification : public IUnknown {           // IFunctionDiscoveryNotification
            virtual HRESULT STDMETHODCALLTYPE OnUpdate(FdUpdateAction action, FdQueryContext context,
                                                       FdInstance* instance) = 0;
            virtual HRESULT STDMETHODCALLTYPE OnError(HRESULT hr, FdQueryContext context,
                                                      const WCHAR* provider) = 0;
            virtual HRESULT STDMETHODCALLTYPE OnEvent(DWORD eventId, FdQueryContext context,
                                                      const WCHAR* provider) = 0;
        };
        struct FdCollectionQuery : public IUnknown {        // IFunctionInstanceCollectionQuery
            virtual HRESULT STDMETHODCALLTYPE AddQueryConstraint(const WCHAR* name, const WCHAR* value) = 0;
            virtual HRESULT STDMETHODCALLTYPE AddPropertyConstraint(const PROPERTYKEY& key,
                                                                    const PROPVARIANT* value, int how) = 0;
            virtual HRESULT STDMETHODCALLTYPE Execute(FdInstanceCollection** collection) = 0;
        };
        struct FdDiscovery : public IUnknown {              // IFunctionDiscovery
            virtual HRESULT STDMETHODCALLTYPE GetInstanceCollection(const WCHAR* category,
                const WCHAR* subCategory, BOOL allSubCategories, FdInstanceCollection** collection) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetInstance(const WCHAR* id, FdInstance** instance) = 0;
            virtual HRESULT STDMETHODCALLTYPE CreateInstanceCollectionQuery(const WCHAR* category,
                const WCHAR* subCategory, BOOL allSubCategories, FdNotification* notification,
                FdQueryContext* context, FdCollectionQuery** query) = 0;
        };

        // {C72BE2EC-8E90-452C-B29A-AB8FF1C071FC} CLSID_FunctionDiscovery
        const CLSID kClsidFunctionDiscovery =
            {0xC72BE2EC, 0x8E90, 0x452C, {0xB2, 0x9A, 0xAB, 0x8F, 0xF1, 0xC0, 0x71, 0xFC}};
        // {4DF99B70-E148-4432-B004-4C9EEB535A5E} IID_IFunctionDiscovery
        const IID kIidFunctionDiscovery =
            {0x4DF99B70, 0xE148, 0x4432, {0xB0, 0x04, 0x4C, 0x9E, 0xEB, 0x53, 0x5A, 0x5E}};
        // {5F6C1BA8-5330-422E-A368-572B244D3F87} IID_IFunctionDiscoveryNotification
        const IID kIidFunctionDiscoveryNotification =
            {0x5F6C1BA8, 0x5330, 0x422E, {0xA3, 0x68, 0x57, 0x2B, 0x24, 0x4D, 0x3F, 0x87}};
        const wchar_t* const kCategoryWsDiscovery = L"Provider\\Microsoft.Networking.WSD";

        // The property keys read (functiondiscoverykeys.h, propkey.h).
        const GUID kFmtidPnpx =
            {0x656A3BB3, 0xECC0, 0x43FD, {0x84, 0x77, 0x4A, 0xE0, 0x40, 0x4A, 0x96, 0xCD}};
        const PROPERTYKEY kPkeyPnpxTypes          = {kFmtidPnpx, 0x1001};  // VT_VECTOR|VT_LPWSTR
        const PROPERTYKEY kPkeyFriendlyName       = {kFmtidPnpx, 0x3000};  // VT_LPWSTR
        const PROPERTYKEY kPkeyPnpxDeviceCategory = {kFmtidPnpx, 0x3004};  // VT_VECTOR|VT_LPWSTR
        const PROPERTYKEY kPkeyPnpxIpAddress      = {kFmtidPnpx, 0x3009};  // VT_VECTOR|VT_LPWSTR
        const PROPERTYKEY kPkeyComputerName =                              // VT_LPWSTR
            {{0x28636AA6, 0x953D, 0x11D2, {0xB5, 0xD6, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0}}, 5};

        // A property as strings: one for VT_LPWSTR, each for a vector of them.
        std::vector<std::wstring> PropertyStrings(IPropertyStore* store, const PROPERTYKEY& key) {
            std::vector<std::wstring> values;
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(store->GetValue(key, &value))) {
                if (value.vt == VT_LPWSTR && value.pwszVal) {
                    values.emplace_back(value.pwszVal);
                } else if (value.vt == (VT_VECTOR | VT_LPWSTR)) {
                    for (ULONG i = 0; i < value.calpwstr.cElems; ++i)
                        if (value.calpwstr.pElems[i]) values.emplace_back(value.calpwstr.pElems[i]);
                }
            }
            PropVariantClear(&value);
            return values;
        }

        bool IsIpv4Address(const std::wstring& text) {
            int dots = 0;
            for (wchar_t c : text) {
                if (c == L'.') ++dots;
                else if (c < L'0' || c > L'9') return false;
            }
            return dots == 3;
        }

        // A computer found through WS-Discovery: `server` is what goes after
        // "\\" (its name, or an IPv4 address when it gave none).
        struct WsdComputer {
            std::wstring server;
            std::wstring name;
            std::wstring comment;
        };

        // The computer a WS-Discovery device is, or nothing when it is a
        // printer, scanner, media player or anything else that is not one.
        bool ReadWsdComputer(FdInstance* instance, WsdComputer& out) {
            if (!instance) return false;
            IPropertyStore* store = nullptr;
            if (FAILED(instance->OpenPropertyStore(STGM_READ, &store)) || !store) return false;

            // A computer says so in its types ("pub:Computer", the prefix
            // varies) or in its PnP-X device category ("Computers").
            bool computer = false;
            for (const std::wstring& type : PropertyStrings(store, kPkeyPnpxTypes)) {
                const size_t cut = type.find_last_of(L":/");
                if (Lower(cut == std::wstring::npos ? type : type.substr(cut + 1)) == L"computer")
                    computer = true;
            }
            for (const std::wstring& category : PropertyStrings(store, kPkeyPnpxDeviceCategory))
                if (Lower(category) == L"computers") computer = true;

            if (computer) {
                std::vector<std::wstring> names = PropertyStrings(store, kPkeyComputerName);
                std::wstring name = names.empty() ? std::wstring() : names.front();
                // "NAS/Workgroup:WORKGROUP" is how pub:Computer reads on the wire.
                name = name.substr(0, name.find(L'/'));
                std::vector<std::wstring> friendly = PropertyStrings(store, kPkeyFriendlyName);
                out.comment = friendly.empty() ? std::wstring() : friendly.front();
                if (!name.empty()) {
                    out.server = name;
                    out.name = name;
                } else {
                    // No name: its address, which an SMB path takes as well.
                    for (const std::wstring& address : PropertyStrings(store, kPkeyPnpxIpAddress))
                        if (IsIpv4Address(address)) { out.server = address; break; }
                    out.name = out.comment.empty() ? out.server : out.comment;
                }
                if (Lower(out.comment) == Lower(out.name)) out.comment.clear();
                computer = !out.server.empty();
            }
            store->Release();
            return computer;
        }

        // Receives the WS-Discovery answers on Function Discovery's threads.
        class WsdSink final : public FdNotification {
        public:
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
                if (!object) return E_POINTER;
                if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIidFunctionDiscoveryNotification)) {
                    *object = static_cast<FdNotification*>(this);
                    AddRef();
                    return S_OK;
                }
                *object = nullptr;
                return E_NOINTERFACE;
            }
            ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs)); }
            ULONG STDMETHODCALLTYPE Release() override {
                const LONG left = InterlockedDecrement(&refs);
                if (left == 0) delete this;
                return static_cast<ULONG>(left);
            }

            HRESULT STDMETHODCALLTYPE OnUpdate(FdUpdateAction action, FdQueryContext,
                                               FdInstance* instance) override {
                if (action == FdUpdateRemove) return S_OK;
                Add(instance);
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE OnError(HRESULT, FdQueryContext, const WCHAR*) override {
                Finish();
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE OnEvent(DWORD eventId, FdQueryContext, const WCHAR*) override {
                if (eventId == kFdEventSearchComplete) Finish();
                return S_OK;
            }

            void Add(FdInstance* instance) {
                WsdComputer computer;
                if (!ReadWsdComputer(instance, computer)) return;
                std::lock_guard<std::mutex> lock(mutex);
                found.push_back(std::move(computer));
            }
            void Finish() {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    complete = true;
                }
                changed.notify_all();
            }
            std::vector<WsdComputer> Wait(std::chrono::steady_clock::duration timeout) {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait_for(lock, timeout, [this] { return complete; });
                return found;
            }

        private:
            ~WsdSink() = default;
            LONG refs = 1;
            std::mutex mutex;
            std::condition_variable changed;
            bool complete = false;
            std::vector<WsdComputer> found;
        };

        // The computers WS-Discovery finds. Call on a thread of its own: it
        // enters the multithreaded apartment and blocks up to
        // kWsdSearchTimeout.
        std::vector<WsdComputer> ListWsdComputers() {
            std::vector<WsdComputer> found;
            const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(init)) return found;

            FdDiscovery* discovery = nullptr;
            if (SUCCEEDED(CoCreateInstance(kClsidFunctionDiscovery, nullptr, CLSCTX_INPROC_SERVER,
                                           kIidFunctionDiscovery,
                                           reinterpret_cast<void**>(&discovery))) && discovery) {
                WsdSink* sink = new WsdSink();
                FdQueryContext context = 0;
                FdCollectionQuery* query = nullptr;
                if (SUCCEEDED(discovery->CreateInstanceCollectionQuery(
                        kCategoryWsDiscovery, nullptr, FALSE, sink, &context, &query)) && query) {
                    FdInstanceCollection* collection = nullptr;
                    const HRESULT hr = query->Execute(&collection);
                    // What the provider already knows comes back at once;
                    // E_PENDING means the rest follows through the sink.
                    if (collection) {
                        DWORD count = 0;
                        if (SUCCEEDED(collection->GetCount(&count))) {
                            for (DWORD i = 0; i < count; ++i) {
                                FdInstance* instance = nullptr;
                                if (SUCCEEDED(collection->Item(i, &instance)) && instance) {
                                    sink->Add(instance);
                                    instance->Release();
                                }
                            }
                        }
                        collection->Release();
                    }
                    if (hr == E_PENDING) found = sink->Wait(kWsdSearchTimeout);
                    else if (SUCCEEDED(hr)) found = sink->Wait(std::chrono::seconds(0));
                    query->Release();   // ends the search
                }
                sink->Release();
                discovery->Release();
            }
            CoUninitialize();
            return found;
        }

    } // namespace

    bool NetworkPlacesAvailable() { return true; }

    std::vector<NetworkComputer> ListNetworkComputers() {
        // WS-Discovery waits out its search; run it while WNet enumerates.
        std::vector<WsdComputer> announced;
        std::thread wsd([&announced] { announced = ListWsdComputers(); });

        std::vector<NetworkComputer> computers;
        std::set<std::wstring> seen;
        // `server` is what follows "\\" in the path; `name` is shown.
        auto add = [&](const std::wstring& server, const std::wstring& name,
                       const std::wstring& comment) {
            if (server.empty() || !seen.insert(Lower(server)).second) return;
            NetworkComputer c;
            c.name = WideToUtf8(name.empty() ? server : name);
            c.path = "\\\\" + WideToUtf8(server);
            c.comment = WideToUtf8(comment);
            computers.push_back(std::move(c));
        };

        // The computers of this machine's workgroup or domain, as the
        // Windows network provider knows them - the list the old "Network
        // Neighborhood" showed. A domain or workgroup is a container: one
        // level down are its servers.
        EnumerateNetwork(RESOURCE_CONTEXT, nullptr, [&](const NETRESOURCEW& r) {
            if (r.dwDisplayType == RESOURCEDISPLAYTYPE_SERVER && r.lpRemoteName)
                add(ServerOfUnc(r.lpRemoteName), {}, r.lpComment ? r.lpComment : L"");
        });

        // The servers of the user's own connections - a mapped drive, a
        // share opened and remembered: reachable however the network
        // advertises (or does not advertise) its computers.
        for (DWORD scope : {static_cast<DWORD>(RESOURCE_CONNECTED), static_cast<DWORD>(RESOURCE_REMEMBERED)}) {
            EnumerateNetwork(scope, nullptr, [&](const NETRESOURCEW& r) {
                if (r.lpRemoteName) add(ServerOfUnc(r.lpRemoteName), {}, L"");
            });
        }

        // The computers that announce themselves only through WS-Discovery.
        wsd.join();
        for (const WsdComputer& c : announced) add(c.server, c.name, c.comment);

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
